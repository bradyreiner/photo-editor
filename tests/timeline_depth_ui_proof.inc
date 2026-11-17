int run_timeline_depth_ui_proof(
    QQuickWindow* window, QObject* root,
    photo_editor::desktop::TimelineDocumentController* document,
    photo_editor::desktop::TimelinePlaybackController* playback,
    photo_editor::desktop::CatalogAssetModel* catalog,
    const QString& source_path, const QString& output_dir, bool inactive_diagnostic) {
  if (!window || !root || !document || !playback || !catalog || !QDir().mkpath(output_dir))
    return fail("Timeline depth proof needs the actual desktop shell");
  root->setProperty("recentUserInput", true);
  activate_native_test_application();
  window->requestActivate();
  const auto video_path = QDir(output_dir).filePath("depth-camera.mp4");
  if (!write_timeline_proof_video_file(source_path, video_path))
    return fail("The Timeline interaction proof needs a generated real video");
  auto asset = make_asset("depth-video", video_path);
  asset.media_type = "video";
  asset.duration_seconds = 10;
  asset.duration_text = "0:10";
  catalog->replaceAssetsForTesting({asset});
  document->createTimelineWithSettings({{"timelineName", "Timeline interaction proof"},
      {"width", 640}, {"height", 360}, {"frameRate", 30.0}});
  document->addClipFromGallery("depth-video", video_path, "Camera A", 10.0,
                                QUrl::fromLocalFile(source_path).toString());
  document->selectClip(0);
  document->seekSequenceSeconds(1);
  QVariant result;
  if (!QMetaObject::invokeMethod(root, "switchToWorkspaceIndex", Q_RETURN_ARG(QVariant, result),
      Q_ARG(QVariant, root->property("timelineWorkspaceIndex"))) || !result.toBool() ||
      !wait_for_condition([&] {
        auto* view = active_workspace(root);
        return view && view->objectName() == "timelineWorkspace" &&
            view->property("timelineEditorPageLoaded").toBool() &&
            named_item_visible(view, "timelineProgramMonitor");
      }, 30000, "timeline-depth-route"))
    return fail("The interaction proof must route to the production Timeline");
  auto* workspace = active_workspace(root);
  const auto invoke = [&](const char* name, const QVariant& value) {
    return QMetaObject::invokeMethod(workspace, name, Q_ARG(QVariant, value));
  };
  if (!QMetaObject::invokeMethod(workspace, "resetCurrentTimelineWorkspaceLayout"))
    return fail("The proof needs a reproducible initial panel arrangement");
  window->resize(1366, 900);
  window->requestActivate();
  const bool native_window_active = wait_for_condition([&] { return window->isActive() && window->isExposed(); }, 5000);
  if (!native_window_active && (!inactive_diagnostic || !window->isExposed()))
    return fail("The native interaction proof needs an active exposed window");
  QJsonArray captures;
  const auto capture = [&](const char* name) {
    process_layout_events(6);
    save_capture_if_requested(window, output_dir, QString::fromUtf8(name), &captures);
  };
  const auto click = [&](const QString& name, Qt::MouseButton button = Qt::LeftButton) {
    auto* item = find_visible_named_item(workspace, name);
    if (!item || !item->isEnabled()) return false;
    QTest::mouseClick(window, button, Qt::NoModifier,
        item->mapToScene(QPointF(item->width()/2, item->height()/2)).toPoint());
    process_layout_events(5);
    return true;
  };
  if (named_item_visible(workspace, "timelineSaveButton") ||
      named_item_visible(workspace, "timelineSaveStateChip") ||
      named_item_visible(workspace, "timelineHeaderMoreMenuButton") ||
      !find_named_object(root, "renderTimelineAction") || !find_named_object(root, "timelineProjectFileMenu"))
    return fail("Save, Render and Project must be owned by File instead of Timeline chrome");

  workspace->setProperty("programTransformControlsVisible", true);
  workspace->setProperty("programGeometryMode", "transform");
  document->setSelectedClipTransform(0.1, -0.1, 1.6, 20);
  process_layout_events(10);
  auto* stage = find_visible_named_item(workspace, "timelineProgramMonitorStage");
  if (!stage) return fail("The transform viewer stage must be visible");
  for (int index = 0; index < 4; ++index) {
    auto* handle = find_visible_named_item(workspace,
        QStringLiteral("timelineProgramResizeHandle%1").arg(index));
    if (!handle) return fail("Every enlarged/rotated transform corner must remain exposed");
    const auto center = handle->mapToItem(stage, QPointF(handle->width()/2, handle->height()/2));
    if (!QRectF(0, 0, stage->width(), stage->height()).contains(center))
      return fail("Transform corner centers must fit inside the viewer, beyond the clipped sequence canvas");
  }
  auto* transform_overlay = find_visible_named_item(workspace, "timelineProgramTransformOverlay");
  auto* transform_move_area = find_visible_named_item(workspace, "timelineProgramTransformMoveArea");
  auto* crop_tool_button = find_visible_named_item(workspace, "timelineProgramCropToolButton");
  if (!transform_overlay || !transform_move_area || !crop_tool_button)
    return fail("The transform containment check needs the live overlay and Crop tool");
  const auto crop_tool_center = crop_tool_button->mapToScene(
      QPointF(crop_tool_button->width()/2, crop_tool_button->height()/2));
  const auto crop_point_in_viewport = transform_overlay->mapFromScene(crop_tool_center);
  const auto crop_point_in_move_area = transform_move_area->mapFromScene(crop_tool_center);
  if (crop_point_in_viewport.x() >= 0 && crop_point_in_viewport.y() >= 0 &&
      crop_point_in_viewport.x() < transform_overlay->width() &&
      crop_point_in_viewport.y() < transform_overlay->height())
    return fail("The Crop routing fixture must place its trigger outside the transform viewport");
  const bool crop_point_overlaps_move_area = transform_move_area->contains(crop_point_in_move_area);
  capture("timeline-transform-bounds");
  const bool crop_click_sent = click("timelineProgramCropToolButton");
  process_layout_events(12);
  if (crop_click_sent && playback->processedFrameSource().isEmpty()) {
    const auto poster_crop_geometry_matches = [&](bool requireVisiblePoster) {
      auto* crop_overlay = find_visible_named_item(workspace, "timelineProgramSourceCropOverlay");
      auto* poster = find_named_item(workspace, "timelineProgramPrimaryPosterImage");
      auto* poster_effect = find_named_item(workspace, "timelineProgramPrimaryPosterEffect");
      if (!poster && !poster_effect) return false;
      if (requireVisiblePoster && (!poster || !poster->isVisible()) &&
          (!poster_effect || !poster_effect->isVisible())) return false;
      if ((!poster || poster->width() <= 0 || poster->height() <= 0) && poster_effect)
        poster = poster_effect;
      if (!crop_overlay || !poster) return false;
      const auto overlay_origin = crop_overlay->mapToScene(QPointF(0, 0));
      const auto poster_origin = poster->mapToScene(QPointF(0, 0));
      return std::abs(overlay_origin.x() - poster_origin.x()) <= 1.0 &&
          std::abs(overlay_origin.y() - poster_origin.y()) <= 1.0 &&
          std::abs(crop_overlay->width() - poster->width()) <= 1.0 &&
          std::abs(crop_overlay->height() - poster->height()) <= 1.0;
    };
    if (!poster_crop_geometry_matches(true))
      return fail("Poster-backed crop handles must use the displayed raw-source bounds");
    std::cerr << "Poster-backed crop bounds verified before the first decoded frame; viewer="
              << window->width() << 'x' << window->height() << '\n';
    window->resize(1080, 760);
    process_layout_events(20);
    if (!poster_crop_geometry_matches(false))
      return fail("Poster-backed crop bounds must track a narrower viewer resize");
    std::cerr << "Raw-source crop bounds remain aligned after narrow resize; viewer="
              << window->width() << 'x' << window->height() << '\n';
    window->resize(1366, 900);
    process_layout_events(20);
  } else if (crop_click_sent) {
    std::cerr << "Poster-only crop geometry skipped because the decoded frame was already ready\n";
  }
  const bool crop_tool_ready = wait_for_condition([&] {
    return named_item_visible(workspace, "timelineProgramSourceCropOverlay") &&
        !named_item_visible(workspace, "timelineProgramTransformOverlay");
  }, 3000);
  if (!crop_click_sent || !crop_tool_ready) {
    auto* source_crop_overlay = find_named_item(workspace, "timelineProgramSourceCropOverlay");
    auto* crop_button = find_named_item(workspace, "timelineProgramCropToolButton");
    auto* video_frame = find_named_item(workspace, "timelineProgramProcessedVideoFrame");
    QVariant should_show_video;
    QMetaObject::invokeMethod(workspace, "shouldShowVideoMonitor", Q_RETURN_ARG(QVariant, should_show_video));
    QVariant has_playable_clip;
    QVariant clip_source_url;
    QMetaObject::invokeMethod(workspace, "hasPlayableClip", Q_RETURN_ARG(QVariant, has_playable_clip));
    QMetaObject::invokeMethod(workspace, "clipSourceUrl", Q_RETURN_ARG(QVariant, clip_source_url));
    const auto trim_feedback = workspace->property("trimFeedbackState").toMap();
    std::cerr << "Crop routing state: sent=" << crop_click_sent
              << " ready=" << crop_tool_ready
              << " mode=" << workspace->property("programGeometryMode").toString().toStdString()
              << " controls=" << workspace->property("programTransformControlsVisible").toBool()
              << " cropActive=" << workspace->property("sourceCropEditingActive").toBool()
              << " selectedVisual=" << workspace->property("selectedVisualClipActive").toBool()
              << " playing=" << workspace->property("sequencePlaying").toBool()
              << " hasPlayable=" << has_playable_clip.toBool()
              << " clipSourceUrl=" << clip_source_url.toString().toStdString()
              << " shouldShowVideo=" << should_show_video.toBool()
              << " processedSource=" << playback->processedFrameSource().toStdString()
              << " renderCache=" << playback->usingRenderCache()
              << " cropPreview=" << playback->sourceCropPreviewEnabled()
              << " trimFeedback=" << trim_feedback.value("visible").toBool()
              << " videoFrameVisible=" << (video_frame && video_frame->isVisible())
              << " videoFrameSource=" << (video_frame ? video_frame->property("source").toString().toStdString() : "<missing>")
              << " videoFrameClip=" << (video_frame ? video_frame->property("clipId").toString().toStdString() : "<missing>")
              << " cropOverlay=" << (source_crop_overlay && source_crop_overlay->isVisible())
              << " transformOverlay=" << named_item_visible(workspace, "timelineProgramTransformOverlay")
              << " cropButtonEnabled=" << (crop_button && crop_button->isEnabled())
              << " overlayPoint=" << crop_point_in_viewport.x() << ',' << crop_point_in_viewport.y()
              << " moveOverlap=" << crop_point_overlaps_move_area << '\n';
    capture("timeline-crop-routing-failure");
    return fail("Crop and Transform must be separate, mutually exclusive viewer tools");
  }
  document->setSelectedFrameMode("fill");
  if (!document->setSelectedSourceCrop(0.15, 0.1, 0.85, 0.9) ||
      !wait_for_condition([&] {
        auto* video = find_visible_named_item(workspace, "timelineProgramProcessedVideoFrame");
        return playback->sourceCropPreviewEnabled() && !playback->usingRenderCache() &&
            video && std::abs(video->scale() - 1.0) < 0.001 && std::abs(video->rotation()) < 0.001 &&
            named_item_visible(workspace, "timelineProgramSourceCropHandle-left");
      }, 5000))
    return fail("Source crop must expose the original independently of Fill, scale and rotation");
  process_layout_events(10);
  auto* crop_handle = find_visible_named_item(workspace, "timelineProgramSourceCropHandle-left");
  auto* crop_overlay = find_visible_named_item(workspace, "timelineProgramSourceCropOverlay");
  if (!crop_handle || !crop_overlay) return fail("The source crop must expose its left edge");
  const auto crop_start = crop_handle->mapToScene(QPointF(crop_handle->width()/2, crop_handle->height()/2)).toPoint();
  const QPoint crop_delta(qRound(crop_overlay->width() * 0.1), 0);
  QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, crop_start);
  QTest::mouseMove(window, crop_start + crop_delta, 30);
  QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, crop_start + crop_delta);
  if (!wait_for_condition([&] {
        return std::abs(document->currentClip().value("sourceCropLeft").toDouble() - 0.25) < 0.015;
      }, 3000))
    return fail("Dragging a source crop edge must map screen distance to the normalized original source");
  capture("timeline-crop-tool");
  document->undo();
  if (std::abs(document->currentClip().value("sourceCropLeft").toDouble() - 0.15) > 0.001)
    return fail("One Undo must restore the full source-crop pointer gesture");
  if (!click("timelineProgramCropDoneButton") || playback->sourceCropPreviewEnabled())
    return fail("Done must restore the authored Program framing");
  document->resetSelectedSourceCrop();
  document->setSelectedFrameMode("fit");
  workspace->setProperty("programTransformControlsVisible", true);
  workspace->setProperty("programGeometryMode", "transform");
  document->setSelectedClipTransform(0, 0, 1, 0);
  document->seekSequenceSeconds(0);
  document->toggleSelectedClipKeyframe("frameScale");
  document->seekSequenceSeconds(4);
  document->setSelectedFrameScale(1.4);
  const auto curve_before = document->currentClip().value("keyframes");
  const int graph_clip_count = document->clipCount();
  if (!invoke("openInspectorTab", "animate") || !invoke("openCurveEditor", "frameScale") ||
      !wait_for_condition([&] {
        return named_item_visible(workspace, "timelineKeyframeCurveGraph") &&
            named_item_visible(workspace, "timelineCurvePoint0");
      }, 5000, "timeline-depth-curve"))
    return fail("Animate must open a usable production keyframe graph");
  if (!click("timelineProgramCurveButton") || workspace->property("curveEditorOpen").toBool() ||
      !click("timelineProgramCurveButton") || !wait_for_condition([&] {
        return named_item_visible(workspace, "timelineKeyframeCurveGraph") &&
            named_item_visible(workspace, "timelineCurvePoint0");
      }, 5000) || document->currentClip().value("keyframes") != curve_before)
    return fail("The Graph toolbar button must close and reopen its panel without editing keys");
  QSignalSpy graph_frames(window, &QQuickWindow::frameSwapped);
  window->update();
  if (graph_frames.isEmpty() && !graph_frames.wait(1000)) return fail("The keyframe layout must present a real frame before pointer input");
  process_layout_events(6);
  auto* graph = find_visible_named_item(workspace, "timelineKeyframeCurveGraph");
  auto* point = find_visible_named_item(workspace, "timelineCurvePoint0");
  QPointer<QQuickItem> pointer_owner(find_visible_named_item(workspace, "timelineCurveAddPointArea"));
  if (!graph || !point || !pointer_owner) return fail("The curve needs points and a stable pointer owner");
  const auto start = point->mapToScene(QPointF(point->width()/2, point->height()/2)).toPoint();
  const QPoint delta(qRound(graph->width() * 0.15), -qRound(graph->height() * 0.12));
  QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, start);
  for (int step = 1; step <= 6; ++step) QTest::mouseMove(window, start + delta*step/6, 15);
  process_layout_events(6);
  const bool live_drag_changed = wait_for_condition([&] {
    return pointer_owner && pointer_owner->property("pressed").toBool()
        && document->currentClip().value("keyframes") != curve_before;
  }, 1000, "timeline-depth-live-drag-publish");
  if (!live_drag_changed) {
    auto* curve = find_named_object(workspace, "timelineKeyframeCurveWindow");
    std::cerr << "Curve pointer: index=" << (pointer_owner ? pointer_owner->property("pointIndex").toInt() : -99)
              << " pressed=" << (pointer_owner && pointer_owner->property("pressed").toBool())
              << " editing=" << (pointer_owner && pointer_owner->property("editing").toBool())
              << " editable=" << (curve && curve->property("curveTargetEditable").toBool())
              << " start=" << start.x() << ',' << start.y()
              << " graph=" << item_scene_rect(graph).x() << ',' << item_scene_rect(graph).y()
              << ' ' << graph->width() << 'x' << graph->height() << '\n';
    capture("timeline-keyframe-drag-failure");
    return fail("Dragging a keyframe must update its real value/time before release without losing pointer capture");
  }
  QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, start + delta);
  process_layout_events(6);
  auto* curve_window = find_named_object(workspace, "timelineKeyframeCurveWindow");
  if (curve_window && curve_window->property("curveGestureActive").toBool())
    return fail("A completed keyframe drag must finish its continuous edit scope");
  capture("timeline-keyframe-live-edit");
  document->undo();
  if (document->currentClip().value("keyframes") != curve_before)
    return fail("One Undo must restore the full keyframe pointer gesture");
  process_layout_events(6);
  auto* curve_state = find_named_object(workspace, "timelineKeyframeCurveWindow");
  auto* cancel_point = find_visible_named_item(workspace, "timelineCurvePoint0");
  const auto before_escape_drag = document->currentClip().value("keyframes");
  if (!curve_state || !cancel_point) return fail("The graph must remain addressable for Escape cancellation");
  const auto cancel_start = cancel_point->mapToScene(QPointF(cancel_point->width()/2, cancel_point->height()/2)).toPoint();
  const QPoint cancel_delta(qRound(graph->width() * 0.08), qRound(graph->height() * 0.10));
  QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, cancel_start);
  for (int step = 1; step <= 5; ++step) QTest::mouseMove(window, cancel_start + cancel_delta*step/5, 15);
  process_layout_events(6);
  if (!wait_for_condition([&] {
        return pointer_owner && pointer_owner->property("editing").toBool()
            && document->currentClip().value("keyframes") != before_escape_drag;
      }, 1000, "timeline-depth-escape-live-drag"))
    return fail("Escape cancellation must be exercised after a keyframe drag has published a live edit");
  QTest::keyClick(window, Qt::Key_Escape);
  QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, cancel_start + cancel_delta);
  process_layout_events(6);
  if (curve_state->property("curveGestureActive").toBool() ||
      document->currentClip().value("keyframes") != before_escape_drag)
    return fail("Escape during a keyframe drag must roll back the complete edit and ignore the later release");

  const auto playhead_before_gutter = document->sequencePlayheadSeconds();
  const double plot_left = curve_state->property("graphInsetLeft").toDouble();
  const auto gutter = graph->mapToScene(QPointF(std::max(1.0, plot_left / 2.0), graph->height() / 2)).toPoint();
  QTest::mouseDClick(window, Qt::LeftButton, Qt::NoModifier, gutter);
  process_layout_events(5);
  if (document->currentClip().value("keyframes") != before_escape_drag ||
      std::abs(document->sequencePlayheadSeconds() - playhead_before_gutter) > 0.001)
    return fail("Double-clicking the graph's axis gutter must not add a key or seek to clip start");

  curve_state->setProperty("graphMode", "channels");
  process_layout_events(6);
  auto* channelPointer = find_visible_named_item(workspace, "timelineAnimationLanePointer-frameScale");
  if (!channelPointer) return fail("The channel graph must expose its real lane pointer surface");
  const auto channelGutter = channelPointer->mapToScene(QPointF(
      std::max(1.0, plot_left / 2.0), channelPointer->height() / 2)).toPoint();
  const auto before_channel_gutter = document->currentClip().value("keyframes");
  const auto channel_playhead = document->sequencePlayheadSeconds();
  QTest::mouseDClick(window, Qt::LeftButton, Qt::NoModifier, channelGutter);
  process_layout_events(5);
  if (document->currentClip().value("keyframes") != before_channel_gutter ||
      std::abs(document->sequencePlayheadSeconds() - channel_playhead) > 0.001)
    return fail("Double-clicking a channel lane's axis gutter must not add a key or seek");
  curve_state->setProperty("graphMode", "value");
  process_layout_events(5);

  const auto box_select_curve = [&] {
    auto* first = find_visible_named_item(workspace, "timelineCurvePoint0");
    auto* second = find_visible_named_item(workspace, "timelineCurvePoint1");
    if (!first || !second) return false;
    const auto p0 = first->mapToScene(QPointF(first->width()/2, first->height()/2));
    const auto p1 = second->mapToScene(QPointF(second->width()/2, second->height()/2));
    const auto plot_left = graph->mapToScene(QPointF(curve_state->property("graphInsetLeft").toDouble(), 0));
    const QPoint begin(qRound(plot_left.x()), qRound(std::min(p0.y(), p1.y())));
    const QPoint end(qRound(std::max(p0.x(), p1.x()) + 12), qRound(std::max(p0.y(), p1.y()) + 12));
    QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, begin);
    for (int step = 1; step <= 8; ++step) QTest::mouseMove(window, begin + (end - begin)*step/8, 12);
    process_layout_events(4);
    if (!named_item_visible(workspace, "timelineCurveMarquee")) {
      QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, end);
      return false;
    }
    capture("timeline-keyframe-box-selection");
    QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, end);
    process_layout_events(4);
    return curve_state && curve_state->property("selectedSeconds").toList().size() == 2;
  };
  if (!box_select_curve() || document->currentClip().value("keyframes") != curve_before)
    return fail("Dragging an empty graph region must select both real keyframes without editing the curve");
  const auto curve_keys_before_nudge = curve_state->property("curveKeyframes").toList();
  QTest::keyClick(window, Qt::Key_Right);
  process_layout_events(6);
  const auto curve_keys_after_nudge = curve_state->property("curveKeyframes").toList();
  if (curve_keys_after_nudge.size() != curve_keys_before_nudge.size() ||
      curve_state->property("selectedSeconds").toList().size() != 2)
    return fail("A keyboard nudge must preserve the multi-selection and keyframe count");
  for (int index = 0; index < curve_keys_before_nudge.size(); ++index) {
    const auto before = curve_keys_before_nudge[index].toMap();
    const auto after = curve_keys_after_nudge[index].toMap();
    if (std::abs(after.value("seconds").toDouble() - before.value("seconds").toDouble() - 1.0/30.0) > 0.0001 ||
        after.value("value") != before.value("value"))
      return fail("Right arrow in the graph must move every selected keyframe exactly one authored frame, not step Program alone");
  }
  document->undo();
  if (document->currentClip().value("keyframes") != curve_before)
    return fail("One Undo must restore the entire keyboard selection nudge");
  QTest::keyClick(window, Qt::Key_Escape);
  QTest::keyClick(window, Qt::Key_A, Qt::ControlModifier);
  process_layout_events(4);
  // Command is the native Select All modifier on macOS; Control is also an
  // explicitly supported graph-local selection shortcut.
  if (curve_state->property("selectedSeconds").toList().size() != 2)
    return fail("Select All must target graph keyframes while the graph owns keyboard focus");
  QTest::keyClick(window, Qt::Key_Up, Qt::ShiftModifier);
  process_layout_events(5);
  const auto raised = curve_state->property("curveKeyframes").toList();
  if (raised.size() != 2 || raised[0].toMap().value("value").toDouble() <= curve_keys_before_nudge[0].toMap().value("value").toDouble() ||
      raised[1].toMap().value("value").toDouble() <= curve_keys_before_nudge[1].toMap().value("value").toDouble())
    return fail("Shift Up must increase both selected values with a coarse nudge");
  document->undo();
  if (document->currentClip().value("keyframes") != curve_before)
    return fail("One Undo must restore a multi-key value nudge");
  process_layout_events(4);
  QTest::keyClick(window, Qt::Key_A, Qt::MetaModifier);
  QTest::keyClick(window, Qt::Key_Delete);
  process_layout_events(4);
  if (!curve_state->property("curveKeyframes").toList().isEmpty() || document->clipCount() != graph_clip_count)
    return fail("Delete in the focused graph must remove the selected keys while preserving the video and audio clips");
  document->undo();
  if (document->currentClip().value("keyframes") != curve_before)
    return fail("One Undo must restore every deleted keyframe");
  process_layout_events(4);
  QTest::keyClick(window, Qt::Key_A, Qt::MetaModifier);
  QTest::keyClick(window, Qt::Key_C, Qt::MetaModifier);
  if (document->keyframeClipboardCount() != 2)
    return fail("Copy in the graph must use the selected keyframes clipboard");
  document->seekSequenceSeconds(5.0);
  QTest::keyClick(window, Qt::Key_V, Qt::MetaModifier);
  process_layout_events(5);
  if (curve_state->property("curveKeyframes").toList().size() != 4 || document->clipCount() != graph_clip_count)
    return fail("Paste in the graph must add both keys at the playhead while preserving the clips");
  document->undo();
  if (document->currentClip().value("keyframes") != curve_before)
    return fail("Pasting a selected keyframe group must undo as one edit");
  process_layout_events(4);
  document->seekSequenceSeconds(2.0);
  if (!click("timelineCurvePreviousKeyButton") ||
      std::abs(workspace->property("curveEditorSelectedSeconds").toDouble()) > 0.0001 ||
      !click("timelineCurveNextKeyButton") ||
      std::abs(workspace->property("curveEditorSelectedSeconds").toDouble() - 4.0) > 0.0001 ||
      curve_state->property("selectedSeconds").toList().size() != 1)
    return fail("Previous and next controls must navigate actual keys");
  auto* zoom_window = find_named_object(workspace, "timelineKeyframeCurveWindow");
  const double span_before = zoom_window ? zoom_window->property("graphTimeSpan").toDouble() : 0;
  if (!click("timelineCurveZoomMenuPrimaryAction") ||
      !wait_for_condition([&] {
        auto* popup = find_named_object(workspace, "timelineCurveZoomMenuPopup");
        return popup && popup->property("opened").toBool();
      }, 3000) || !click("timelineCurveZoomMenuPopupItem1") ||
      !zoom_window || zoom_window->property("graphTimeSpan").toDouble() >= span_before)
    return fail("The graph Zoom menu must fit the actual keyframes into a narrower editable time range");
  if (zoom_window->property("valueDisplayUnit").toString() != "%")
    return fail("Scale keyframes must expose a human-readable percent value");
  if (!wait_for_condition([&] {
        auto* popup = find_named_object(workspace, "timelineCurveZoomMenuPopup");
        return popup && !popup->property("visible").toBool();
      }, 2000)) return fail("The Zoom menu must finish dismissing before the next graph gesture");
  if (!click("timelineKeyframeCurveGraph", Qt::RightButton) || !wait_for_condition([&] {
        auto* menu = find_named_object(workspace, "timelineCurveContextMenu");
        return menu && menu->property("opened").toBool();
      }, 3000)) {
    capture("timeline-keyframe-bottom-context-failure");
    return fail("The short bottom-dock graph must retain its actual context menu");
  }
  QTest::keyClick(window, Qt::Key_Escape);
  process_layout_events(6);
  auto* panel_state = find_named_object(workspace, "timelinePanelDockState");
  QVariant moved_curve;
  if (!panel_state || !QMetaObject::invokeMethod(panel_state, "movePanel", Q_RETURN_ARG(QVariant, moved_curve),
      Q_ARG(QVariant, "keyframes"), Q_ARG(QVariant, "right"), Q_ARG(QVariant, 999)) || !moved_curve.toBool())
    return fail("The refined graph must support the production right-side dock");
  window->resize(1100, 760);
  process_layout_events(12);
  auto* curve_surface = find_visible_named_item(workspace, "timelineCurveEditorContentSurface");
  const QStringList contained_controls = {"timelineCurvePrimaryControlsRow", "timelineCurveActionControlsRow",
      "timelineKeyframeValueDetailRow", "timelineKeyframeCurveGraph", "timelineCurveAdvancedControlsRow"};
  if (!curve_surface) return fail("A narrow right dock must expose the actual graph surface");
  for (const auto& name : contained_controls) {
    auto* item = find_visible_named_item(workspace, name);
    if (!item) return fail("The narrow curve dock must retain its primary editing controls");
    const auto rect = item->mapRectToItem(curve_surface, QRectF(0, 0, item->width(), item->height()));
    if (rect.left() < -1 || rect.top() < -1 || rect.right() > curve_surface->width() + 1 || rect.bottom() > curve_surface->height() + 1) {
      std::cerr << "Curve overflow: " << name.toStdString() << " rect=" << rect.x() << ',' << rect.y()
                << ' ' << rect.width() << 'x' << rect.height() << " surface="
                << curve_surface->width() << 'x' << curve_surface->height() << '\n';
      capture("timeline-keyframe-narrow-overflow");
      return fail("Curve controls must remain contained after resizing the actual workspace");
    }
  }
  capture("timeline-keyframe-narrow-dock");
  window->resize(1366, 900);
  process_layout_events(10);
  auto* overview = find_visible_named_item(workspace, "timelineCurveOverviewPointer");
  auto* overview_range = find_visible_named_item(workspace, "timelineCurveOverviewRange");
  if (!overview || !overview_range) return fail("A tall side dock must provide the graph range overview");
  const auto span_before_overview = curve_state->property("graphTimeSpan").toDouble();
  const auto edge = overview_range->mapToScene(QPointF(overview_range->width() - 2, overview_range->height()/2)).toPoint();
  QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, edge);
  QTest::mouseMove(window, edge - QPoint(qRound(overview->width() * 0.06), 0), 30);
  QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, edge - QPoint(qRound(overview->width() * 0.06), 0));
  process_layout_events(6);
  if (curve_state->property("graphTimeSpan").toDouble() >= span_before_overview ||
      document->currentClip().value("keyframes") != curve_before) {
    const auto pointer_edge = overview->mapFromScene(QPointF(edge));
    std::cerr << "Overview edge drag: span=" << span_before_overview << " -> "
              << curve_state->property("graphTimeSpan").toDouble()
              << " range=" << overview_range->x() << ',' << overview_range->width()
              << " overviewWidth=" << overview->width()
              << " pointerLocal=" << pointer_edge.x() << ',' << pointer_edge.y()
              << " pressX=" << overview->property("pressX").toDouble()
              << " detectedEdge=" << overview->property("edge").toInt()
              << " sharedParent=" << (overview->parentItem() == overview_range->parentItem())
              << " sameWindow=" << (overview->window() == overview_range->window())
              << " pointerParent=" << (overview->parentItem()
                  ? overview->parentItem()->objectName().toStdString() : "<none>")
              << " rangeParent=" << (overview_range->parentItem()
                  ? overview_range->parentItem()->objectName().toStdString() : "<none>")
              << " matchingRanges="
              << find_named_items(workspace, "timelineCurveOverviewRange").size()
              << " matchingPointers="
              << find_named_items(workspace, "timelineCurveOverviewPointer").size()
              << " documentChanged=" << (document->currentClip().value("keyframes") != curve_before)
              << '\n';
    capture("timeline-keyframe-overview-zoom-failure");
    return fail("Dragging the overview edge must zoom the graph without changing the document");
  }
  const auto start_before_pan = curve_state->property("graphTimeStart").toDouble();
  const auto center = overview_range->mapToScene(QPointF(overview_range->width()/2, overview_range->height()/2)).toPoint();
  QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, center);
  QTest::mouseMove(window, center + QPoint(qRound(overview->width() * 0.06), 0), 30);
  QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, center + QPoint(qRound(overview->width() * 0.06), 0));
  process_layout_events(6);
  if (curve_state->property("graphTimeStart").toDouble() <= start_before_pan ||
      document->currentClip().value("keyframes") != curve_before)
    return fail("Dragging the overview range must pan the graph without editing keyframes");
  capture("timeline-keyframe-overview-zoom");
  window->resize(1366, 900);
  process_layout_events(8);
  if (!click("timelineKeyframeCurveGraph", Qt::RightButton) ||
      !wait_for_condition([&] {
        auto* menu = find_named_object(workspace, "timelineCurveContextMenu");
        return menu && menu->property("opened").toBool();
      }, 3000))
  {
    capture("timeline-keyframe-context-failure");
    auto* menu = find_named_object(workspace, "timelineCurveContextMenu");
    std::cerr << "Curve context state opened=" << (menu && menu->property("opened").toBool())
              << " visible=" << (menu && menu->property("visible").toBool()) << '\n';
    return fail("The keyframe graph must expose a contextual interpolation/add/delete menu");
  }
  capture("timeline-keyframe-context-menu");
  if (!invoke("closeTimelinePanel", "keyframes")) return fail("The curve panel must close through its dock owner");
  if (!wait_for_condition([&] {
        auto* menu = find_named_object(workspace, "timelineCurveContextMenu");
        return menu && !menu->property("visible").toBool();
      }, 2000)) return fail("Closing the graph panel must also dismiss its contextual menu");
  // The graph has just shared the Inspector's dock during the responsive pass.
  // Expose the Inspector explicitly before checking its own action geometry.
  if (!invoke("showTimelinePanel", "inspector") || !invoke("openInspectorTab", "animate") ||
      !wait_for_condition([&] { return named_item_visible(workspace, "timelineAnimationChannelsInspector"); }, 3000))
    return fail("The Inspector tab must return to its animation properties after closing the graph");
  process_layout_events(8);
  auto* animation_body = find_visible_named_item(workspace, "timelineAnimationChannelsInspector");
  auto* open_graph = find_visible_named_item(workspace, "timelineAnimateOpenCurveButton");
  if (!animation_body || !open_graph || !item_scene_rect(animation_body).contains(item_scene_rect(open_graph)))
    return fail("Animation actions must remain visible inside the inspector at laptop height");
  capture("timeline-animation-properties");
  document->seekSequenceSeconds(1);
  workspace->setProperty("programTransformControlsVisible", false);

  if (!invoke("applyTimelineLayoutPreset", "color") ||
      !wait_for_condition([&] { return named_item_visible(workspace, "timelineColorConsole_exposureValueField"); }, 5000))
    return fail("Choosing Color must reveal its actual controls");
  if (!invoke("closeTimelinePanel", "color") || !invoke("applyTimelineLayoutPreset", "color") ||
      !wait_for_condition([&] { return named_item_visible(workspace, "timelineColorConsole_exposureValueField"); }, 5000))
    return fail("Choosing Color must reopen its primary panel even after it was closed in the saved layout");
  const auto frame_ready = [&] {
    auto* image = find_visible_named_item(workspace, "timelineProgramProcessedVideoFrame");
    return image && image->property("status").toInt() == 1 &&
        image->property("source").toString() == playback->processedFrameSource() &&
        !playback->processedFrameSource().isEmpty();
  };
  if (!wait_for_condition(frame_ready, 10000, "timeline-depth-decoded-frame"))
    return fail("Color output proof requires a real decoded frame in Program");
  const auto picture = [&] {
    process_layout_events(6);
    auto* monitor = find_visible_named_item(workspace, "timelineProgramMonitor");
    const auto image = window->grabWindow();
    const double dpr = static_cast<double>(image.width()) / window->width();
    const auto bounds = item_scene_rect(monitor);
    const QRect rect(qRound((bounds.x() + bounds.width()*0.2)*dpr),
        qRound((bounds.y() + bounds.height()*0.2)*dpr), qRound(bounds.width()*0.6*dpr), qRound(bounds.height()*0.6*dpr));
    return image.copy(rect).scaled(64, 48, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
  };
  const auto mean_difference = [](const QImage& first, const QImage& second) {
    if (first.isNull() || second.isNull() || first.size() != second.size()) return 0.0;
    double sum = 0;
    for (int y = 0; y < first.height(); ++y) for (int x = 0; x < first.width(); ++x) {
      const auto a = first.pixelColor(x,y), b = second.pixelColor(x,y);
      sum += std::abs(a.red()-b.red()) + std::abs(a.green()-b.green()) + std::abs(a.blue()-b.blue());
    }
    return sum / (first.width()*first.height()*3);
  };
  const auto original_picture = picture();
  const auto original_frame = playback->processedFrameSource();
  if (!click("timelineColorConsole_exposureValueField")) return fail("The real exposure field must receive pointer focus");
  QTest::keyClick(window, Qt::Key_A, Qt::ControlModifier);
  QTest::keyClick(window, '1'); QTest::keyClick(window, '.'); QTest::keyClick(window, '5');
  QTest::keyClick(window, Qt::Key_Return);
  if (!wait_for_condition([&] { return frame_ready() && playback->processedFrameSource() != original_frame; },
                          10000, "timeline-depth-grade-frame"))
    return fail("Typing Color exposure must publish a fresh decoded Program frame");
  const auto graded_picture = picture();
  const double grade_difference = mean_difference(original_picture, graded_picture);
  if (grade_difference < 8) return fail("Color controls must visibly change actual Program pixels, not just readback metadata");
  capture("timeline-color-applied");
  document->undo();
  if (!invoke("applyTimelineLayoutPreset", "edit") || !invoke("showTimelinePanel", "effects"))
    return fail("Shared filters must be discoverable in Effects");
  if (!wait_for_condition([&] { return named_item_visible(workspace, "timelinePhotoFilterSearch"); }, 5000))
    return fail("The production Effects panel must include the shared Edit filter catalog");
  if (!wait_for_condition(frame_ready, 10000)) return fail("Shared filter proof requires decoded Program pixels");
  const auto before_filter = picture();
  const auto before_filter_source = playback->processedFrameSource();
  const auto clip_id = document->currentClip().value("clipId").toString();
  const auto filter_result = document->editClipPhotoFilter(clip_id, "add", {}, "invert_linear_rgb", {});
  if (!filter_result.value("ok").toBool() ||
      !wait_for_condition([&] { return named_item_visible(workspace, "timelinePhotoFilterExpand_0"); }, 5000))
    return fail("Adding an Edit filter must expose its editable timeline stack entry");
  if (!click("timelinePhotoFilterExpand_0")) return fail("The shared filter row must expand for tuning");
  if (!wait_for_condition([&] { return frame_ready() && playback->processedFrameSource() != before_filter_source; }, 10000))
    return fail("The shared Edit filter must publish a fresh decoded Program image");
  const double filter_difference = mean_difference(before_filter, picture());
  if (filter_difference < 12) return fail("Adding a shared Edit filter must visibly change the actual Program pixels");
  capture("timeline-shared-edit-filter");
  auto* dock_state = find_named_object(workspace, "timelinePanelDockState");
  const auto panel_dock = [&](const QString& id) {
    QVariant dock;
    if (dock_state) QMetaObject::invokeMethod(dock_state, "dock", Q_RETURN_ARG(QVariant, dock), Q_ARG(QVariant, id));
    return dock.toString();
  };
  const auto drag_panel_to_tab = [&](const QString& id, const QString& destination) {
    auto* tab = find_visible_named_item(workspace, "timelineDockTabDrag_" + id);
    auto* target = find_visible_named_item(workspace, "timelineDockTabDrag_" + destination);
    if (!tab || !target) return false;
    const auto start = tab->mapToScene(QPointF(tab->width()/2, tab->height()/2)).toPoint();
    const auto end = target->mapToScene(QPointF(target->width()/2, target->height()/2)).toPoint();
    QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, start);
    for (int step = 1; step <= 8; ++step) QTest::mouseMove(window, start + (end-start)*step/8, 20);
    QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, end);
    process_layout_events(6);
    return true;
  };
  const auto playhead_before_docking = document->sequencePlayheadSeconds();
  if (!drag_panel_to_tab("effects", "media") || !wait_for_condition([&] { return panel_dock("effects") == "left"; }, 3000))
    return fail("Dragging an Effects tab onto Media must move the actual panel to the left dock");
  capture("timeline-panel-drag");
  if (!drag_panel_to_tab("effects", "inspector") || !wait_for_condition([&] { return panel_dock("effects") == "right"; }, 3000) ||
      document->currentClip().value("clipId").toString() != clip_id ||
      std::abs(document->sequencePlayheadSeconds() - playhead_before_docking) > 0.001)
    return fail("Dragging the panel back must preserve the selected clip and playhead");
  if (!click("timelineDockTabDrag_effects", Qt::RightButton) || !wait_for_condition([&] {
        auto* menu = find_named_object(workspace, "timelineDockMoveMenu_right");
        return menu && menu->property("opened").toBool();
      }, 3000))
    return fail("Right-clicking a panel tab must expose Close, Detach and docking commands");
  capture("timeline-panel-context-menu");
  if (!click("timelineProgramMonitor") || !wait_for_condition([&] {
        auto* menu = find_named_object(workspace, "timelineDockMoveMenu_right");
        return menu && !menu->property("visible").toBool();
      }, 2000)) return fail("Clicking outside a panel menu must dismiss it");
  if (!document->editClipPhotoFilter(clip_id, "add", {}, "gaussian_blur", {}).value("ok").toBool() ||
      !wait_for_condition([&] {
        auto* status = find_visible_named_item(workspace, "timelinePhotoFilterExportStatus");
        return status && status->property("text").toString().startsWith("Export:") &&
            !document->currentClip().value("colorGradeSequenceExportPending").toBool() &&
            !document->currentClip().value("colorGradeSequenceExportSupported").toBool();
      }, 5000))
    return fail("Spatial filters on an animated source must disclose their current export restriction in the actual panel");
  capture("timeline-filter-export-restriction");
  const QJsonObject receipt{{"schema", "timeline-depth-ui-proof:v1"}, {"passed", native_window_active},
      {"interactionAssertionsPassed", true}, {"nativeWindowActive", native_window_active}, {"inactiveDiagnostic", inactive_diagnostic},
      {"captures", captures}, {"transformCornersInsideViewer", true}, {"cropTransformExclusive", true}, {"sourceCropFillPointerMapping", true}, {"cropGestureUndo", true},
      {"keyframeBoxSelection", true}, {"keyframeFrameNudge", true}, {"keyframeValueNudge", true},
      {"keyframeSelectionDeleteUndo", true}, {"keyframeClipboardShortcuts", true}, {"keyframeNavigation", true}, {"curveResponsiveBounds", true},
      {"curveOverviewZoomPan", true}, {"keyframeLivePointerUpdate", true}, {"keyframeGestureUndo", true}, {"keyframeContextMenu", true}, {"graphZoomMenuExercised", true}, {"keyframePercentReadout", true},
      {"colorTaskReopensPrimaryPanel", true}, {"colorChangesProgramPixels", true},
      {"programGradeMeanAbsolutePixelDifference", grade_difference}, {"sharedEditFilterExposed", true}, {"sharedEditFilterChangesProgramPixels", true}, {"programFilterMeanAbsolutePixelDifference", filter_difference},
      {"fileOwnsSaveRenderProject", true}, {"nativePanelDragRoundTrip", true}, {"panelTabContextMenu", true}, {"filterExportRestrictionVisible", true}};
  QFile file(QDir(output_dir).filePath("timeline-depth-proof.json"));
  if (!file.open(QIODevice::WriteOnly) || file.write(QJsonDocument(receipt).toJson()) <= 0)
    return fail("The interaction proof must retain its result and captures");
  std::cout << QJsonDocument(receipt).toJson().constData();
  return native_window_active ? EXIT_SUCCESS : EXIT_FAILURE;
}
