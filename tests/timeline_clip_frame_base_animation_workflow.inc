bool run_timeline_clip_frame_base_animation_workflow(
    const QString& workspace_path) {
  const auto fail = [](const char* message) {
    std::cerr << "timeline_clip_frame_base_animation: " << message << '\n';
    return false;
  };

  QImage source(80, 60, QImage::Format_RGBA8888);
  for (int y = 0; y < source.height(); ++y) {
    for (int x = 0; x < source.width(); ++x) {
      source.setPixelColor(
          x, y,
          QColor((x * 3 + y) % 256, (y * 5 + 29) % 256,
                 (x * 7 + 51) % 256, 255));
    }
  }
  const auto source_path =
      QDir(workspace_path).filePath(QStringLiteral("frame-base-animation-source.png"));
  if (!source.save(source_path, "PNG")) {
    return fail("the patterned still source should be writable");
  }

  TimelineDocumentController controller;
  controller.setEditAccessPolicy([] { return true; });
  controller.createTimelineWithSettings({
      {QStringLiteral("timelineName"), QStringLiteral("Base frame with animation")},
      {QStringLiteral("width"), 128},
      {QStringLiteral("height"), 96},
      {QStringLiteral("frameRate"), 24.0},
  });
  controller.addClipFromGallery(
      QStringLiteral("photo:frame-base-animation"), source_path,
      QStringLiteral("Frame base animation source"), 2.0, source_path);
  const auto initial_clips = controller.clips();
  const auto clip_id = initial_clips.isEmpty()
      ? QString{}
      : initial_clips.first().toMap().value(QStringLiteral("clipId")).toString();
  if (clip_id.isEmpty()) {
    return fail("the still source should create a stable photo clip");
  }

  const QVariantMap shadow{
      {QStringLiteral("enabled"), true},
      {QStringLiteral("color"), QStringLiteral("#99000000")},
      {QStringLiteral("offsetX"), 3.0},
      {QStringLiteral("offsetY"), 2.0},
      {QStringLiteral("blurRadius"), 5.0},
      {QStringLiteral("spread"), 1.0},
  };
  const auto initial_frame = controller.setTimelineClipFrameForAutomation(
      clip_id, 0.11, -0.09, 0.90, 6.0, 0.02, -0.01, 0.85,
      QStringLiteral("fill"), 0.75, 0.62, 0.08,
      0.05, 0.04, 0.94, 0.97, shadow);
  const QVariantMap source_mask{
      {QStringLiteral("shape"), QStringLiteral("rectangle")},
      {QStringLiteral("left"), 0.12},
      {QStringLiteral("top"), 0.10},
      {QStringLiteral("right"), 0.88},
      {QStringLiteral("bottom"), 0.90},
      {QStringLiteral("cornerRadius"), 0.02},
  };
  const auto mask_result =
      controller.setTimelineClipMaskForAutomation(clip_id, source_mask);
  if (!initial_frame.value(QStringLiteral("ok")).toBool() ||
      !mask_result.value(QStringLiteral("ok")).toBool()) {
    return fail("the fixture should set its initial frame and sequence mask");
  }

  const auto channel = [](const QString& parameter,
                          double first,
                          double last,
                          const QString& interpolation = QStringLiteral("linear")) {
    return QVariantMap{
        {QStringLiteral("parameter"), parameter},
        {QStringLiteral("keyframes"), QVariantList{
             QVariantMap{{QStringLiteral("localSeconds"), 0.0},
                         {QStringLiteral("value"), first},
                         {QStringLiteral("interpolation"), interpolation}},
             QVariantMap{{QStringLiteral("localSeconds"), 1.0},
                         {QStringLiteral("value"), last},
                         {QStringLiteral("interpolation"), interpolation}},
         }},
    };
  };
  const QVariantList channels{
      channel(QStringLiteral("frameOffsetX"), -0.18, 0.16),
      channel(QStringLiteral("frameOffsetY"), 0.14, -0.12),
      channel(QStringLiteral("frameScale"), 0.82, 1.14),
      channel(QStringLiteral("frameRotationDegrees"), -18.0, 12.0),
      channel(QStringLiteral("framePerspectiveX"), 0.05, -0.04),
      channel(QStringLiteral("framePerspectiveY"), -0.03, 0.06),
      channel(QStringLiteral("frameCornerRadius"), 0.02, 0.12),
      channel(QStringLiteral("opacity"), 0.61, 0.91),
      channel(QStringLiteral("mask:right"), 0.86, 0.72),
  };
  const auto animation =
      controller.setTimelineClipAnimationForAutomation(clip_id, channels);
  if (!animation.value(QStringLiteral("ok")).toBool()) {
    return fail("the fixture should author nonzero first-key values on transform and opacity channels");
  }

  const auto original_variant = controller.activeOutputVariant();
  controller.applyOutputPreset(QStringLiteral("square"));
  controller.applyOutputPreset(original_variant);
  if (controller.activeOutputVariant() != original_variant) {
    return fail("the fixture should return to its original active output variant");
  }

  // The current FFmpeg perspective filter path rejects animated `t` expressions
  // before encoding. Keep the perspective curves in the controller under test,
  // and use a render-only sibling with the remaining animated transform and
  // opacity channels for the pixel-parity assertion below.
  TimelineDocumentController parity_controller;
  parity_controller.setEditAccessPolicy([] { return true; });
  parity_controller.createTimelineWithSettings({
      {QStringLiteral("timelineName"), QStringLiteral("Frame base pixel parity")},
      {QStringLiteral("width"), 128},
      {QStringLiteral("height"), 96},
      {QStringLiteral("frameRate"), 24.0},
  });
  parity_controller.addClipFromGallery(
      QStringLiteral("photo:frame-base-animation-parity"), source_path,
      QStringLiteral("Frame base animation parity source"), 2.0, source_path);
  const auto parity_clips = parity_controller.clips();
  const auto parity_clip_id = parity_clips.isEmpty()
      ? QString{}
      : parity_clips.first().toMap().value(QStringLiteral("clipId")).toString();
  const auto parity_initial_frame = parity_controller.setTimelineClipFrameForAutomation(
      parity_clip_id, 0.11, -0.09, 0.90, 6.0, 0.0, 0.0, 0.85,
      QStringLiteral("fill"), 0.75, 0.62, 0.08,
      0.05, 0.04, 0.94, 0.97, shadow);
  const auto parity_mask =
      parity_controller.setTimelineClipMaskForAutomation(parity_clip_id, source_mask);
  QVariantList parity_channels;
  for (const auto& channel_value : channels) {
    const auto parameter = channel_value.toMap()
                               .value(QStringLiteral("parameter")).toString();
    if (parameter != QStringLiteral("framePerspectiveX") &&
        parameter != QStringLiteral("framePerspectiveY")) {
      parity_channels.push_back(channel_value);
    }
  }
  const auto parity_animation = parity_controller.setTimelineClipAnimationForAutomation(
      parity_clip_id, parity_channels);
  if (parity_clip_id.isEmpty() ||
      !parity_initial_frame.value(QStringLiteral("ok")).toBool() ||
      !parity_mask.value(QStringLiteral("ok")).toBool() ||
      !parity_animation.value(QStringLiteral("ok")).toBool()) {
    return fail("the render-only sibling should contain the supported animated channels");
  }

  const auto find_clip_json = [](const QJsonObject& document,
                                 const QString& requested_id) {
    for (const auto& track_value : document.value(QStringLiteral("tracks")).toArray()) {
      for (const auto& clip_value : track_value.toObject()
                                        .value(QStringLiteral("clips")).toArray()) {
        const auto clip = clip_value.toObject();
        if (clip.value(QStringLiteral("clipId")).toString() == requested_id) {
          return clip;
        }
      }
    }
    return QJsonObject{};
  };
  const auto relevant_channel_keyframes = [](const QJsonObject& clip) {
    const QSet<QString> parameters{
        QStringLiteral("frameOffsetX"), QStringLiteral("frameOffsetY"),
        QStringLiteral("frameScale"), QStringLiteral("frameRotationDegrees"),
        QStringLiteral("framePerspectiveX"), QStringLiteral("framePerspectiveY"),
        QStringLiteral("frameCornerRadius"), QStringLiteral("opacity"),
        QStringLiteral("mask:right"),
    };
    QJsonObject result;
    for (const auto& keyframe_value : clip.value(QStringLiteral("keyframes")).toArray()) {
      const auto keyframe = keyframe_value.toObject();
      const auto parameter = keyframe.value(QStringLiteral("parameter")).toString();
      if (parameters.contains(parameter)) {
        auto values = result.value(parameter).toArray();
        values.push_back(keyframe);
        result.insert(parameter, values);
      }
    }
    return result;
  };
  const auto clip_detail = [&controller, &clip_id]() {
    return controller.inspectTimelineClipById(clip_id)
        .value(QStringLiteral("clip")).toMap();
  };
  const auto document_before =
      photo_editor::desktop::TimelineHistoryTestAccess::documentObject(controller);
  const auto clip_before = find_clip_json(document_before, clip_id);
  const auto before_detail = clip_detail();
  const auto square_override_before = clip_before
      .value(QStringLiteral("outputVariantOverrides")).toObject()
      .value(QStringLiteral("square")).toObject();
  if (clip_before.isEmpty() || before_detail.isEmpty() ||
      square_override_before.isEmpty() ||
      relevant_channel_keyframes(clip_before).size() != channels.size()) {
    return fail("the fixture should serialize all animated frame, mask, and opacity channels plus a separate square variant");
  }

  const auto before_pattern = QDir(workspace_path).filePath(
      QStringLiteral("frame-base-animation-before-%06d.png"));
  QString render_error;
  if (!render_timeline_image_sequence(
          parity_controller, before_pattern, &render_error, 0.0, 1.0)) {
    std::cerr << "timeline_clip_frame_base_animation: baseline export failed: "
              << render_error.toStdString() << '\n';
    return false;
  }

  const auto undo_depth_before =
      photo_editor::desktop::TimelineHistoryTestAccess::undoDepth(controller);
  const auto revision_before =
      photo_editor::desktop::TimelineHistoryTestAccess::documentEditRevision(controller);
  const auto updated_frame = controller.setTimelineClipFrameForAutomation(
      clip_id, 0.21, -0.17, 1.05, 0.0, -0.12, 0.08, 0.43,
      QStringLiteral("fill"), 0.75, 0.62, 0.04,
      0.05, 0.04, 0.94, 0.97);
  const auto parity_updated_frame = parity_controller.setTimelineClipFrameForAutomation(
      parity_clip_id, 0.21, -0.17, 1.05, 0.0, 0.0, 0.0, 0.43,
      QStringLiteral("fill"), 0.75, 0.62, 0.04,
      0.05, 0.04, 0.94, 0.97);
  const auto after_detail = clip_detail();
  const auto document_after =
      photo_editor::desktop::TimelineHistoryTestAccess::documentObject(controller);
  const auto clip_after = find_clip_json(document_after, clip_id);
  const auto square_override_after = clip_after
      .value(QStringLiteral("outputVariantOverrides")).toObject()
      .value(QStringLiteral("square")).toObject();
  if (!updated_frame.value(QStringLiteral("ok")).toBool() ||
      !parity_updated_frame.value(QStringLiteral("ok")).toBool() ||
      !updated_frame.value(QStringLiteral("changed")).toBool() ||
      updated_frame.value(QStringLiteral("clipId")).toString() != clip_id ||
      photo_editor::desktop::TimelineHistoryTestAccess::undoDepth(controller) !=
          undo_depth_before + 1 ||
      photo_editor::desktop::TimelineHistoryTestAccess::documentEditRevision(controller) !=
          revision_before + 1 ||
      after_detail.isEmpty() ||
      !nearly_equal(after_detail.value(QStringLiteral("baseFrameOffsetX")).toDouble(), 0.21) ||
      !nearly_equal(after_detail.value(QStringLiteral("baseFrameOffsetY")).toDouble(), -0.17) ||
      !nearly_equal(after_detail.value(QStringLiteral("baseFrameScale")).toDouble(), 1.05) ||
      !nearly_equal(after_detail.value(QStringLiteral("baseFrameRotationDegrees")).toDouble(), 0.0) ||
      !nearly_equal(after_detail.value(QStringLiteral("baseFramePerspectiveX")).toDouble(), -0.12) ||
      !nearly_equal(after_detail.value(QStringLiteral("baseFramePerspectiveY")).toDouble(), 0.08) ||
      !nearly_equal(after_detail.value(QStringLiteral("baseOpacity")).toDouble(), 0.43) ||
      !nearly_equal(clip_after.value(QStringLiteral("frameCornerRadius")).toDouble(), 0.04) ||
      relevant_channel_keyframes(clip_after) != relevant_channel_keyframes(clip_before) ||
      square_override_after != square_override_before ||
      clip_after.value(QStringLiteral("clipMask")) != clip_before.value(QStringLiteral("clipMask")) ||
      clip_after.value(QStringLiteral("frameShadow")) != clip_before.value(QStringLiteral("frameShadow")) ||
      clip_after.value(QStringLiteral("sourceCropLeft")) != clip_before.value(QStringLiteral("sourceCropLeft")) ||
      clip_after.value(QStringLiteral("sourceCropTop")) != clip_before.value(QStringLiteral("sourceCropTop")) ||
      clip_after.value(QStringLiteral("sourceCropRight")) != clip_before.value(QStringLiteral("sourceCropRight")) ||
      clip_after.value(QStringLiteral("sourceCropBottom")) != clip_before.value(QStringLiteral("sourceCropBottom"))) {
    std::cerr << "timeline_clip_frame_base_animation: update result="
              << QJsonDocument::fromVariant(updated_frame).toJson(QJsonDocument::Compact).toStdString()
              << " beforeChannels="
              << QJsonDocument(relevant_channel_keyframes(clip_before)).toJson(QJsonDocument::Compact).toStdString()
              << " afterChannels="
              << QJsonDocument(relevant_channel_keyframes(clip_after)).toJson(QJsonDocument::Compact).toStdString()
              << '\n';
    return fail("base geometry should change once while every animation, mask, crop, shadow, and other variant stays exact");
  }

  const auto no_op_depth =
      photo_editor::desktop::TimelineHistoryTestAccess::undoDepth(controller);
  const auto no_op_revision =
      photo_editor::desktop::TimelineHistoryTestAccess::documentEditRevision(controller);
  const auto no_op = controller.setTimelineClipFrameForAutomation(
      clip_id, 0.21, -0.17, 1.05, 0.0, -0.12, 0.08, 0.43,
      QStringLiteral("fill"), 0.75, 0.62, 0.04,
      0.05, 0.04, 0.94, 0.97);
  if (!no_op.value(QStringLiteral("ok")).toBool() ||
      no_op.value(QStringLiteral("changed")).toBool() ||
      !no_op.value(QStringLiteral("alreadySatisfied")).toBool() ||
      photo_editor::desktop::TimelineHistoryTestAccess::undoDepth(controller) != no_op_depth ||
      photo_editor::desktop::TimelineHistoryTestAccess::documentEditRevision(controller) != no_op_revision) {
    return fail("an identical frame request should not add an undo entry or edit revision");
  }

  const auto after_pattern = QDir(workspace_path).filePath(
      QStringLiteral("frame-base-animation-after-%06d.png"));
  if (!render_timeline_image_sequence(
          parity_controller, after_pattern, &render_error, 0.0, 1.0)) {
    std::cerr << "timeline_clip_frame_base_animation: post-edit export failed: "
              << render_error.toStdString() << '\n';
    return false;
  }
  for (const auto frame_number : {1, 7, 13, 19, 24}) {
    const QImage before_frame(timeline_image_sequence_frame(before_pattern, frame_number));
    const QImage after_frame(timeline_image_sequence_frame(after_pattern, frame_number));
    if (before_frame.isNull() || after_frame.isNull() || before_frame != after_frame) {
      return fail("export should evaluate the preserved keyframe curves identically before and after a base-only frame update");
    }
  }

  controller.undo();
  const auto undone_detail = clip_detail();
  const auto clip_undone = find_clip_json(
      photo_editor::desktop::TimelineHistoryTestAccess::documentObject(controller), clip_id);
  if (!nearly_equal(undone_detail.value(QStringLiteral("baseFrameScale")).toDouble(), 0.90) ||
      !nearly_equal(undone_detail.value(QStringLiteral("baseFrameRotationDegrees")).toDouble(), 6.0) ||
      relevant_channel_keyframes(clip_undone) != relevant_channel_keyframes(clip_before)) {
    return fail("Undo should restore the original frame base and preserve its animation curves");
  }
  controller.redo();
  const auto redone_detail = clip_detail();
  const auto clip_redone = find_clip_json(
      photo_editor::desktop::TimelineHistoryTestAccess::documentObject(controller), clip_id);
  if (!nearly_equal(redone_detail.value(QStringLiteral("baseFrameScale")).toDouble(), 1.05) ||
      !nearly_equal(redone_detail.value(QStringLiteral("baseFrameRotationDegrees")).toDouble(), 0.0) ||
      relevant_channel_keyframes(clip_redone) != relevant_channel_keyframes(clip_before)) {
    return fail("Redo should restore the requested frame base without changing its animation curves");
  }

  controller.applyOutputPreset(QStringLiteral("square"));
  controller.applyOutputPreset(original_variant);
  const auto variant_roundtrip_detail = clip_detail();
  const auto variant_roundtrip_clip = find_clip_json(
      photo_editor::desktop::TimelineHistoryTestAccess::documentObject(controller), clip_id);
  if (!nearly_equal(variant_roundtrip_detail.value(QStringLiteral("baseFrameScale")).toDouble(), 1.05) ||
      relevant_channel_keyframes(variant_roundtrip_clip) != relevant_channel_keyframes(clip_before)) {
    return fail("switching output variants away and back should retain the changed active base and untouched curves");
  }

  const auto saved_path = QDir(workspace_path).filePath(
      QStringLiteral("frame-base-animation-roundtrip.json"));
  const auto saved = controller.saveTimelineCopyForAutomation(saved_path);
  TimelineDocumentController reopened;
  reopened.setEditAccessPolicy([] { return true; });
  if (!saved.value(QStringLiteral("ok")).toBool() || !reopened.loadTimeline(saved_path)) {
    return fail("the changed base and its curves should save to a native project copy");
  }
  const auto reopened_clip = find_clip_json(
      photo_editor::desktop::TimelineHistoryTestAccess::documentObject(reopened), clip_id);
  const auto reopened_detail = reopened.inspectTimelineClipById(clip_id)
                                  .value(QStringLiteral("clip")).toMap();
  if (!nearly_equal(reopened_detail.value(QStringLiteral("baseFrameScale")).toDouble(), 1.05) ||
      !nearly_equal(reopened_detail.value(QStringLiteral("baseFrameRotationDegrees")).toDouble(), 0.0) ||
      relevant_channel_keyframes(reopened_clip) != relevant_channel_keyframes(clip_before) ||
      reopened_clip.value(QStringLiteral("clipMask")) != clip_before.value(QStringLiteral("clipMask")) ||
      reopened_clip.value(QStringLiteral("frameShadow")) != clip_before.value(QStringLiteral("frameShadow")) ||
      reopened_clip.value(QStringLiteral("sourceCropLeft")) != clip_before.value(QStringLiteral("sourceCropLeft")) ||
      reopened_clip.value(QStringLiteral("sourceCropTop")) != clip_before.value(QStringLiteral("sourceCropTop")) ||
      reopened_clip.value(QStringLiteral("sourceCropRight")) != clip_before.value(QStringLiteral("sourceCropRight")) ||
      reopened_clip.value(QStringLiteral("sourceCropBottom")) != clip_before.value(QStringLiteral("sourceCropBottom"))) {
    return fail("reopening should retain the active base, independent variant framing, curves, mask, crop, and shadow");
  }
  return true;
}
