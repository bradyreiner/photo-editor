bool run_timeline_animated_xy_perspective_export_workflow(
    const QString& workspace_path) {
  const auto fail = [](const char* message) {
    std::cerr << "timeline_animated_xy_perspective: " << message << '\n';
    return false;
  };
  constexpr double kFps = 30.0;
  constexpr double kDuration = 2.0;
  constexpr double kLastFrameSeconds = 59.0 / kFps;
  const auto source_path = QDir(workspace_path).filePath(
      QStringLiteral("animated-xy-perspective-source.png"));
  QImage source(160, 90, QImage::Format_RGBA8888);
  for (int y = 0; y < source.height(); ++y) {
    for (int x = 0; x < source.width(); ++x) {
      source.setPixelColor(
          x, y,
          QColor((x * 7 + y * 3) % 256, (x * 2 + y * 11) % 256,
                 (x * 13 + y * 5) % 256, 255));
    }
  }
  if (!source.save(source_path, "PNG")) {
    return fail("the perspective oracle source should be writable");
  }

  const auto capture_baseline =
      qEnvironmentVariableIsSet("LOOKSY_PERSPECTIVE_CAPTURE_BASELINE");
  const auto retain_pixel_proof =
      qEnvironmentVariableIsSet("LOOKSY_PERSPECTIVE_RETAIN_FIXED_PROOF");
  const auto retain_evidence = capture_baseline || retain_pixel_proof;
  const auto evidence_directory = qEnvironmentVariable(
      "LOOKSY_PERSPECTIVE_EVIDENCE_DIR", workspace_path);
  if (retain_evidence && !QDir().mkpath(evidence_directory)) {
    return fail("the external baseline-evidence directory should be writable");
  }
  const auto record_failure = [&](const QString& phase,
                                  const QString& error) {
    if (!retain_evidence) {
      return;
    }
    const auto prefix = capture_baseline ? QStringLiteral("baseline")
                                         : QStringLiteral("fixed-proof");
    const auto log_path = QDir(evidence_directory).filePath(
        QStringLiteral("%1-%2-ffmpeg-stderr.txt").arg(prefix, phase));
    QFile log(log_path);
    if (log.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
      log.write(error.toUtf8());
      log.write("\n");
    }
  };

  TimelineDocumentController animated;
  animated.setEditAccessPolicy([] { return true; });
  animated.createTimelineWithSettings({
      {QStringLiteral("timelineName"), QStringLiteral("Animated legacy perspective export")},
      {QStringLiteral("width"), 160},
      {QStringLiteral("height"), 90},
      {QStringLiteral("frameRate"), kFps},
  });
  animated.addClipFromGallery(
      QStringLiteral("photo:animated-xy-perspective"), source_path,
      QStringLiteral("Animated XY perspective"), kDuration, source_path);
  const auto animated_clips = animated.clips();
  const auto clip_id = animated_clips.isEmpty()
      ? QString{}
      : animated_clips.first().toMap().value(QStringLiteral("clipId")).toString();
  if (clip_id.isEmpty()) {
    return fail("the animated perspective fixture should create a stable photo clip");
  }
  const auto set_frame = [](TimelineDocumentController& controller,
                            const QString& requested_clip_id,
                            double perspective_x,
                            double perspective_y) {
    return controller.setTimelineClipFrameForAutomation(
        requested_clip_id, 0.0, 0.0, 1.0, 0.0,
        perspective_x, perspective_y, 1.0,
        QStringLiteral("stretch"), 1.0, 1.0, 0.0,
        0.0, 0.0, 1.0, 1.0);
  };
  const auto initial_frame = set_frame(animated, clip_id, 0.04, -0.02);
  const auto perspective_channel = [kLastFrameSeconds](const QString& parameter,
                                                       double first,
                                                       double last) {
    return QVariantMap{
        {QStringLiteral("parameter"), parameter},
        {QStringLiteral("keyframes"), QVariantList{
             QVariantMap{{QStringLiteral("localSeconds"), 0.0},
                         {QStringLiteral("value"), first},
                         {QStringLiteral("interpolation"), QStringLiteral("linear")}},
             QVariantMap{{QStringLiteral("localSeconds"), kLastFrameSeconds},
                         {QStringLiteral("value"), last},
                         {QStringLiteral("interpolation"), QStringLiteral("linear")}},
         }},
    };
  };
  const QVariantList channels{
      perspective_channel(QStringLiteral("framePerspectiveX"), 0.04, 0.20),
      perspective_channel(QStringLiteral("framePerspectiveY"), -0.02, 0.12),
  };
  const auto animation =
      animated.setTimelineClipAnimationForAutomation(clip_id, channels);
  if (!initial_frame.value(QStringLiteral("ok")).toBool() ||
      !animation.value(QStringLiteral("ok")).toBool()) {
    return fail("the clip should accept valid local-time X/Y perspective curves");
  }

  const auto channel_snapshot = [&]() {
    const auto document =
        photo_editor::desktop::TimelineHistoryTestAccess::documentObject(animated);
    for (const auto& track_value : document.value(QStringLiteral("tracks")).toArray()) {
      for (const auto& clip_value : track_value.toObject()
                                        .value(QStringLiteral("clips")).toArray()) {
        const auto clip = clip_value.toObject();
        if (clip.value(QStringLiteral("clipId")).toString() == clip_id) {
          QJsonArray result;
          for (const auto& keyframe_value :
               clip.value(QStringLiteral("keyframes")).toArray()) {
            const auto parameter = keyframe_value.toObject()
                                       .value(QStringLiteral("parameter")).toString();
            if (parameter == QStringLiteral("framePerspectiveX") ||
                parameter == QStringLiteral("framePerspectiveY")) {
              result.push_back(keyframe_value);
            }
          }
          return result;
        }
      }
    }
    return QJsonArray{};
  };
  const auto keyframes_before = channel_snapshot();
  if (keyframes_before.size() != 4) {
    return fail("both animated perspective channels should retain their two authored keys");
  }

  const auto make_static_controller = [&](TimelineDocumentController& controller,
                                          const QString& name,
                                          double x,
                                          double y) {
    controller.setEditAccessPolicy([] { return true; });
    controller.createTimelineWithSettings({
        {QStringLiteral("timelineName"), name},
        {QStringLiteral("width"), 160},
        {QStringLiteral("height"), 90},
        {QStringLiteral("frameRate"), kFps},
    });
    controller.addClipFromGallery(
        QStringLiteral("photo:animated-xy-perspective-reference"), source_path,
        QStringLiteral("Static perspective reference"), kDuration, source_path);
    const auto clips = controller.clips();
    if (clips.isEmpty()) {
      return false;
    }
    const auto static_clip_id = clips.first().toMap()
                                    .value(QStringLiteral("clipId")).toString();
    return set_frame(controller, static_clip_id, x, y)
        .value(QStringLiteral("ok")).toBool();
  };
  TimelineDocumentController static_start;
  TimelineDocumentController static_midpoint;
  TimelineDocumentController static_end;
  const auto midpoint_progress = 0.5 / kLastFrameSeconds;
  const auto midpoint_x = 0.04 + (0.20 - 0.04) * midpoint_progress;
  const auto midpoint_y = -0.02 + (0.12 - (-0.02)) * midpoint_progress;
  if (!make_static_controller(static_start,
                              QStringLiteral("Static starting perspective"),
                              0.04, -0.02) ||
      !make_static_controller(static_midpoint,
                              QStringLiteral("Static midpoint perspective"),
                              midpoint_x, midpoint_y) ||
      !make_static_controller(static_end,
                              QStringLiteral("Static ending perspective"),
                              0.20, 0.12)) {
    return fail("static endpoint reference clips should be constructible");
  }

  const auto baseline_graph = capture_baseline
      ? QDir(evidence_directory).filePath(
            QStringLiteral("baseline-unfixed-perspective.ffgraph"))
      : retain_pixel_proof
          ? QDir(evidence_directory).filePath(
                QStringLiteral("animated-xy-perspective-frameclock.ffgraph"))
      : QDir(workspace_path).filePath(QStringLiteral("animated-perspective-full.ffgraph"));
  if (retain_evidence) {
    QFile::copy(source_path, QDir(evidence_directory).filePath(
        capture_baseline ? QStringLiteral("baseline-perspective-source.png")
                         : QStringLiteral("fixed-perspective-source.png")));
  }
  const auto full_pattern = QDir(workspace_path).filePath(
      QStringLiteral("animated-perspective-full-%06d.png"));
  QString render_error;
  if (!render_timeline_image_sequence(
          animated, full_pattern, &render_error, 0.0, kDuration, baseline_graph)) {
    record_failure(QStringLiteral("full"), render_error);
    std::cerr << "timeline_animated_xy_perspective: full export failed: "
              << render_error.toStdString() << '\n';
    return false;
  }
  if (QDir(workspace_path).entryList(
          {QStringLiteral("animated-perspective-full-*.png")}, QDir::Files).size() != 60) {
    return fail("a two-second 30 fps render should contain exactly 60 frames");
  }
  QFile full_filter_graph(baseline_graph);
  if (!full_filter_graph.open(QIODevice::ReadOnly)) {
    return fail("the full animated-perspective filter graph should be available for clock inspection");
  }
  const auto graph_bytes = full_filter_graph.readAll();
  const auto fps_normalization = graph_bytes.indexOf(
      "fps=30.000000000:round=near:start_time=0");
  const auto perspective_filter_start = graph_bytes.indexOf("perspective=x0=");
  const auto perspective_filter_end = graph_bytes.indexOf(
      ":sense=destination:eval=frame", perspective_filter_start);
  const auto frame_count_clock = graph_bytes.indexOf("((on-1)/30.000000000)");
  if (fps_normalization < 0 || perspective_filter_start < fps_normalization ||
      perspective_filter_end < perspective_filter_start ||
      frame_count_clock < perspective_filter_start ||
      frame_count_clock > perspective_filter_end ||
      graph_bytes.mid(perspective_filter_start,
                      perspective_filter_end - perspective_filter_start)
          .contains("(t+")) {
    return fail("legacy X/Y perspective expressions should use the normalized per-frame clock, never t");
  }

  const auto range_pattern = QDir(workspace_path).filePath(
      QStringLiteral("animated-perspective-range-%06d.png"));
  if (!render_timeline_image_sequence(
          animated, range_pattern, &render_error, 0.5, 1.5)) {
    record_failure(QStringLiteral("range"), render_error);
    std::cerr << "timeline_animated_xy_perspective: nonzero range export failed: "
              << render_error.toStdString() << '\n';
    return false;
  }
  if (QDir(workspace_path).entryList(
          {QStringLiteral("animated-perspective-range-*.png")}, QDir::Files).size() != 30) {
    return fail("the one-second nonzero range should contain exactly 30 frames");
  }

  const auto static_start_pattern = QDir(workspace_path).filePath(
      QStringLiteral("animated-perspective-static-start-%06d.png"));
  const auto static_midpoint_pattern = QDir(workspace_path).filePath(
      QStringLiteral("animated-perspective-static-midpoint-%06d.png"));
  const auto static_end_pattern = QDir(workspace_path).filePath(
      QStringLiteral("animated-perspective-static-end-%06d.png"));
  if (!render_timeline_image_sequence(
          static_start, static_start_pattern, &render_error, 0.0, 1.0 / kFps) ||
      !render_timeline_image_sequence(
          static_midpoint, static_midpoint_pattern, &render_error, 0.0, 1.0 / kFps) ||
      !render_timeline_image_sequence(
          static_end, static_end_pattern, &render_error, 0.0, 1.0 / kFps)) {
    record_failure(QStringLiteral("static-reference"), render_error);
    return fail("static perspective endpoint references should render");
  }
  const QImage animated_first(timeline_image_sequence_frame(full_pattern, 1));
  const QImage animated_last(timeline_image_sequence_frame(full_pattern, 60));
  const QImage expected_first(timeline_image_sequence_frame(static_start_pattern, 1));
  const QImage expected_midpoint(
      timeline_image_sequence_frame(static_midpoint_pattern, 1));
  const QImage expected_last(timeline_image_sequence_frame(static_end_pattern, 1));
  if (animated_first.isNull() || animated_last.isNull() ||
      expected_first.isNull() || expected_midpoint.isNull() || expected_last.isNull() ||
      animated_first != expected_first || animated_last != expected_last ||
      QImage(timeline_image_sequence_frame(full_pattern, 16)) != expected_midpoint ||
      QImage(timeline_image_sequence_frame(range_pattern, 1)) != expected_midpoint ||
      animated_first == animated_last) {
    return fail("animated start, midpoint, and end poses should match independent static perspective renders");
  }

  for (int frame = 1; frame <= 30; ++frame) {
    const QImage full_frame(timeline_image_sequence_frame(full_pattern, frame + 15));
    const QImage range_frame(timeline_image_sequence_frame(range_pattern, frame));
    if (full_frame.isNull() || range_frame.isNull() || full_frame != range_frame) {
      return fail("the nonzero-range frames should match the same global frames from the full render");
    }
  }

  const auto undo_depth =
      photo_editor::desktop::TimelineHistoryTestAccess::undoDepth(animated);
  const auto revision =
      photo_editor::desktop::TimelineHistoryTestAccess::documentEditRevision(animated);
  const auto updated_frame = set_frame(animated, clip_id, 0.12, 0.08);
  const auto keyframes_after = channel_snapshot();
  if (!updated_frame.value(QStringLiteral("ok")).toBool() ||
      !updated_frame.value(QStringLiteral("changed")).toBool() ||
      keyframes_after != keyframes_before ||
      photo_editor::desktop::TimelineHistoryTestAccess::undoDepth(animated) != undo_depth + 1 ||
      photo_editor::desktop::TimelineHistoryTestAccess::documentEditRevision(animated) != revision + 1) {
    return fail("changing base perspective under active keyframes should preserve both curves and create one undo step");
  }
  const auto updated_details = animated.inspectTimelineClipById(clip_id)
                                   .value(QStringLiteral("clip")).toMap();
  if (qAbs(updated_details.value(QStringLiteral("baseFramePerspectiveX")).toDouble() - 0.12) > 1.0e-9 ||
      qAbs(updated_details.value(QStringLiteral("baseFramePerspectiveY")).toDouble() - 0.08) > 1.0e-9) {
    return fail("the authored perspective base should change independently of its animated curve");
  }

  const auto after_full_pattern = QDir(workspace_path).filePath(
      QStringLiteral("animated-perspective-after-full-%06d.png"));
  const auto after_range_pattern = QDir(workspace_path).filePath(
      QStringLiteral("animated-perspective-after-range-%06d.png"));
  if (!render_timeline_image_sequence(
          animated, after_full_pattern, &render_error, 0.0, kDuration) ||
      !render_timeline_image_sequence(
          animated, after_range_pattern, &render_error, 0.5, 1.5)) {
    record_failure(QStringLiteral("after-base-update"), render_error);
    return fail("the animated perspective should still render after changing its authored base");
  }
  for (int frame = 1; frame <= 60; ++frame) {
    if (QImage(timeline_image_sequence_frame(full_pattern, frame)) !=
        QImage(timeline_image_sequence_frame(after_full_pattern, frame))) {
      return fail("changing only the animated perspective base should not alter rendered keyframed pixels");
    }
  }
  for (int frame = 1; frame <= 30; ++frame) {
    if (QImage(timeline_image_sequence_frame(range_pattern, frame)) !=
        QImage(timeline_image_sequence_frame(after_range_pattern, frame))) {
      return fail("the nonzero-range render should remain pixel-identical after a base-only change");
    }
  }

  animated.undo();
  auto details = animated.inspectTimelineClipById(clip_id)
                     .value(QStringLiteral("clip")).toMap();
  if (qAbs(details.value(QStringLiteral("baseFramePerspectiveX")).toDouble() - 0.04) > 1.0e-9 ||
      qAbs(details.value(QStringLiteral("baseFramePerspectiveY")).toDouble() + 0.02) > 1.0e-9 ||
      channel_snapshot() != keyframes_before) {
    return fail("undo should restore the original perspective base without changing either curve");
  }
  animated.redo();
  details = animated.inspectTimelineClipById(clip_id)
                .value(QStringLiteral("clip")).toMap();
  if (qAbs(details.value(QStringLiteral("baseFramePerspectiveX")).toDouble() - 0.12) > 1.0e-9 ||
      qAbs(details.value(QStringLiteral("baseFramePerspectiveY")).toDouble() - 0.08) > 1.0e-9 ||
      channel_snapshot() != keyframes_before) {
    return fail("redo should restore the new perspective base with both curves intact");
  }
  if (retain_pixel_proof) {
    const auto retain_frame = [&](const QString& pattern,
                                  int number,
                                  const QString& name) {
      const auto destination = QDir(evidence_directory).filePath(name);
      return QFile::copy(timeline_image_sequence_frame(pattern, number), destination);
    };
    const std::array<std::tuple<QString, int, QString>, 11> evidence_frames{{
        {full_pattern, 1, QStringLiteral("animated-start.png")},
        {full_pattern, 16, QStringLiteral("animated-midpoint-global-0_5s.png")},
        {full_pattern, 60, QStringLiteral("animated-end.png")},
        {range_pattern, 1, QStringLiteral("animated-range-start-0_5s.png")},
        {range_pattern, 30, QStringLiteral("animated-range-end-1_4667s.png")},
        {static_start_pattern, 1, QStringLiteral("static-start-pose.png")},
        {static_midpoint_pattern, 1, QStringLiteral("static-midpoint-pose.png")},
        {static_end_pattern, 1, QStringLiteral("static-end-pose.png")},
        {after_full_pattern, 16, QStringLiteral("after-base-animated-midpoint.png")},
        {after_range_pattern, 1, QStringLiteral("after-base-range-start.png")},
        {after_full_pattern, 60, QStringLiteral("after-base-animated-end.png")},
    }};
    for (const auto& [pattern, number, name] : evidence_frames) {
      if (!retain_frame(pattern, number, name)) {
        return fail("the representative rendered pixel evidence should be retainable");
      }
    }
  }
  return true;
}
