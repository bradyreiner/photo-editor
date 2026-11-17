int run_timeline_interaction_feedback_proof(
    QQuickWindow* window, QObject* root,
    photo_editor::desktop::TimelineDocumentController* document,
    const QString& source_path, const QString& output_dir) {
  if (!window || !root || !document || !QDir().mkpath(output_dir)) return fail("Missing interaction proof runtime");
  const bool video_frames = QCoreApplication::arguments().contains("--timeline-interaction-video-frames");
  QString media_path = source_path;
  if (video_frames) {
    media_path = QDir(output_dir).filePath("moving-frames.mp4");
    QProcess encode;
    encode.start(QStandardPaths::findExecutable("ffmpeg"), {"-y", "-f", "lavfi", "-i",
        "testsrc2=size=320x180:rate=30:duration=10", "-an", "-c:v", "libx264", "-pix_fmt", "yuv420p", media_path});
    if (!encode.waitForStarted(5000) || !encode.waitForFinished(30000) || encode.exitCode() != 0)
      return fail("Real-frame proof must encode a moving video fixture");
  }
  document->createTimelineWithSettings({{"timelineName", "Timeline interaction proof"},
      {"width", 1280}, {"height", 720}, {"frameRate", 30.0}});
  document->addClipFromGallery("feedback-a", media_path, "Selection A", 4.0);
  document->addClipFromGallery("feedback-b", media_path, "Selection B", 4.0);
  document->addClipFromGallery("feedback-short", media_path, "Short clip", 0.15);
  root->setProperty("currentWorkspace", root->property("timelineWorkspaceIndex"));
  if (!wait_for_condition([&] { return active_workspace(root) &&
        named_item_visible(root, "timelineViewport") && named_item_visible(root, "timelineClip-Selection A"); },
        30000, "timeline-feedback-route")) return fail("Interaction proof must open the real timeline");
  auto* workspace = active_workspace(root);
  workspace->setProperty("timelineZoom", 60.0);
  document->setActiveTool("select");
  document->deselectAllClips();
  process_layout_events(6);
  auto clip = [&](const char* label) { return find_named_item(workspace, QString::fromUtf8(label)); };
  auto* a = clip("timelineClip-Selection A");
  auto* b = clip("timelineClip-Selection B");
  auto* short_clip = clip("timelineClip-Short clip");
  if (!a || !b || !short_clip) return fail("Interaction proof clips must be visible");
  const auto bounds = [&](QQuickItem* item) { return item->mapRectToScene(QRectF(0, 0, item->width(), item->height())); };
  const auto a_bounds = bounds(a), b_bounds = bounds(b);
  const QPoint start(qRound(b_bounds.right() - 24), qRound(b_bounds.bottom() + 3));
  const QPoint finish(qRound(a_bounds.left() + 3), qRound(a_bounds.top() + 3));
  QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, start);
  QTest::mouseMove(window, finish, 50);
  process_layout_events(3);
  auto* marquee = find_named_item(workspace, "timelineMarqueeSelection");
  if (!marquee || !marquee->isVisible() || marquee->width() < 100 || marquee->height() < 5) {
    QJsonArray failure_captures;
    save_capture_if_requested(window, output_dir, "marquee-failure", &failure_captures);
    std::cerr << "start=" << start.x() << ',' << start.y() << " finish=" << finish.x() << ',' << finish.y()
        << " a=" << a_bounds.x() << ',' << a_bounds.y() << ',' << a_bounds.width() << ',' << a_bounds.height()
        << " marquee=" << workspace->property("marqueeActive").toBool()
        << " grabber=" << (window->mouseGrabberItem() ? window->mouseGrabberItem()->objectName().toStdString() : "none") << '\n';
    return fail("Unmodified background dragging must show the selection rectangle");
  }
  QJsonArray captures;
  save_capture_if_requested(window, output_dir, "marquee-drag", &captures);
  QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, finish);
  if (!wait_for_condition([&] { return document->selectedClipCount() == 2; }, 5000))
    return fail("Box selection must include both intersected clips, including index zero");
  process_layout_events(5);
  a = clip("timelineClip-Selection A"); b = clip("timelineClip-Selection B"); short_clip = clip("timelineClip-Short clip");
  auto* selection = find_named_item(a, "timelineClipSelectionOverlay");
  auto* glow = find_named_item(a, "timelineClipSelectionGlow");
  if (!selection || !selection->isVisible() || selection->opacity() < 0.9 || !glow || glow->opacity() < 0.9)
    return fail("Selected clips must show their fill, bright border and glow");
  if (short_clip->width() > 12 || short_clip->width() < 1 ||
      bounds(short_clip).left() < bounds(b).right())
    return fail("Short clip geometry must follow its real duration without overlapping its neighbor");
  auto* quick = find_named_item(short_clip, "timelineClipQuickActions");
  auto* trim = find_named_item(short_clip, "timelineClipLeftTrimHandle");
  if (!quick || quick->isVisible() || !trim || trim->isVisible())
    return fail("Short clips must hide controls that cannot fit");
  if (video_frames) {
    const auto decoded_tiles = [&]() {
      QStringList sources;
      std::function<void(QQuickItem*)> collect = [&](QQuickItem* item) {
        if (item->objectName() == "timelineFilmstripFrame" && item->property("frameReady").toBool())
          sources.append(item->property("decodedSource").toString());
        for (auto* child : item->childItems()) collect(child);
      };
      collect(clip("timelineClip-Selection A"));
      return sources;
    };
    if (!wait_for_condition([&] { return decoded_tiles().size() >= 2; }, 15000, "timeline-filmstrip-real-frames"))
      return fail("Visible filmstrip tiles must decode real video frames asynchronously");
    const auto sources = decoded_tiles();
    if (sources[0] == sources[1] || QImage(QUrl(sources[0]).toLocalFile()) == QImage(QUrl(sources[1]).toLocalFile()))
      return fail("Different filmstrip times must show different moving-video frames, not a repeated poster");
  }
  save_capture_if_requested(window, output_dir, "selected-and-short-clips", &captures);
  document->setActiveTool("razor");
  process_layout_events(4);
  a = clip("timelineClip-Selection A");
  const QPoint blade_point = a->mapToScene(QPointF(a->width() * 0.45, a->height() * 0.5)).toPoint();
  QTest::mouseMove(window, blade_point, 20);
  process_layout_events(4);
  auto* preview = find_named_item(workspace, "timelineBladePreview");
  auto* input = find_named_item(a, "timelineClipDirectToolInput");
  if (!preview || !preview->isVisible() || !input ||
      input->property("cursorShape").toInt() != static_cast<int>(Qt::CrossCursor))
    return fail("Blade hover must show its exact cut guide and tool cursor");
  const auto state = workspace->property("bladePreview").value<QJSValue>().toVariant().toMap();
  const double cut_seconds = state.value("seconds").toDouble();
  if (std::abs(cut_seconds * 30 - std::round(cut_seconds * 30)) > 1e-6)
    return fail("Blade guide must land on a sequence frame");
  if (video_frames) {
    auto* frame = find_named_item(window->contentItem(), "timelineBladeSourceFrame");
    if (!frame || !wait_for_condition([&] { return frame->property("frameReady").toBool(); }, 12000, "timeline-blade-real-frame"))
      return fail("Blade hover must decode and display its actual source frame");
    const auto frame_bounds = frame->mapRectToScene(QRectF(0, 0, frame->width(), frame->height()));
    if (!frame->isVisible() || !QRectF(0, 0, window->width(), window->height()).contains(frame_bounds) ||
        std::abs(frame_bounds.left() - blade_point.x()) > frame->width() + 24 || frame_bounds.bottom() > blade_point.y())
      return fail("The actual source frame must be visible above the cut, within the native window");
    const auto frame_data = frame->property("frameData").value<QJSValue>().toVariant().toMap();
    const double requested_source = frame->property("requestedSeconds").toDouble();
    if (std::abs(requested_source - cut_seconds) > 0.002 || frame_data.value("clipId").toString().isEmpty())
      return fail("The blade image must correspond to the displayed cut frame, not the poster");
  }
  save_capture_if_requested(window, output_dir, "blade-preview", &captures);
  const auto count_before = document->clips().size();
  QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, blade_point);
  if (!wait_for_condition([&] { return document->clips().size() == count_before + 1; }, 5000))
    return fail("Blade click must split the hovered clip through the preview guide");
  bool cut_matches = false;
  for (const auto& value : document->clips()) {
    const auto entry = value.toMap();
    if (std::abs(entry.value("startSeconds").toDouble() - cut_seconds) < 1e-6) cut_matches = true;
  }
  if (!cut_matches) return fail("Actual edit point must match the displayed blade time");
  document->undo();
  if (document->clips().size() != count_before) return fail("Blade cut must undo in one step");
  document->setActiveTool("hand"); process_layout_events(3);
  a = clip("timelineClip-Selection A"); input = find_named_item(a, "timelineClipDirectToolInput");
  if (!input || input->property("cursorShape").toInt() != static_cast<int>(Qt::OpenHandCursor) || preview->isVisible())
    return fail("Changing tools must change the pointer and clear the blade guide");
  std::cout << QJsonDocument(QJsonObject{{"schema", "timeline-interaction-feedback-v1"},
      {"ok", true}, {"realVideoFrames", video_frames}, {"platform", QGuiApplication::platformName()}, {"unmodifiedMarquee", true},
      {"multiClipSelection", true}, {"selectionGlow", true}, {"shortClipsDoNotOverlap", true},
      {"shortClipControlsHidden", true}, {"frameAlignedBladePreviewAndCut", true},
      {"toolCursors", true}, {"cutUndo", true}, {"captures", captures}}).toJson().constData();
  return EXIT_SUCCESS;
}
