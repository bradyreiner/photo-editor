bool run_timeline_opacity_window_pruning_workflow(
    const QString& workspace_path) {
  constexpr double kFps = 30.0;
  const QSize frame_size(120, 68);
  TimelineDocumentController controller;
  controller.createTimelineWithSettings({
      {QStringLiteral("timelineName"),
       QStringLiteral("Exact opacity window pruning")},
      {QStringLiteral("width"), frame_size.width()},
      {QStringLiteral("height"), frame_size.height()},
      {QStringLiteral("frameRate"), kFps},
  });
  const QVariantMap vector_path{
      {QStringLiteral("viewBoxWidth"), 120.0},
      {QStringLiteral("viewBoxHeight"), 68.0},
      {QStringLiteral("commands"), QVariantList{
           QVariantMap{{QStringLiteral("verb"), QStringLiteral("M")},
                       {QStringLiteral("points"), QVariantList{4.0, 34.0}}},
           QVariantMap{{QStringLiteral("verb"), QStringLiteral("L")},
                       {QStringLiteral("points"), QVariantList{116.0, 34.0}}},
       }},
      {QStringLiteral("stroke"), QStringLiteral("#ffffff")},
      {QStringLiteral("strokeWidth"), 3.0},
      {QStringLiteral("fill"), QStringLiteral("#00000000")},
  };
  const QVariantMap mask{
      {QStringLiteral("shape"), QStringLiteral("ellipse")},
      {QStringLiteral("left"), 0.05},
      {QStringLiteral("top"), 0.05},
      {QStringLiteral("right"), 0.95},
      {QStringLiteral("bottom"), 0.95},
      {QStringLiteral("cornerRadius"), 0.0},
  };
  const auto create_generator = [&controller, &vector_path](
                                    const QString& label,
                                    double opacity) {
    const auto track = controller.createTimelineTrackForAutomation(
        QStringLiteral("graphics"), label, {});
    if (!track.value(QStringLiteral("ok")).toBool()) {
      return QVariantMap{{QStringLiteral("ok"), false},
                         {QStringLiteral("error"), QStringLiteral("track creation failed")}};
    }
    return controller.createTimelineGeneratorForAutomation(
        track.value(QStringLiteral("trackId")).toString(),
        QStringLiteral("vector-path"), 0.0, 2.0,
        {{QStringLiteral("vectorPath"), vector_path},
         {QStringLiteral("opacity"), opacity}});
  };
  const auto attach_mask = [&controller, &mask](const QVariantMap& created) {
    const auto clip_id = created.value(QStringLiteral("clip"))
                             .toMap()
                             .value(QStringLiteral("clipId"))
                             .toString();
    const auto applied = controller.setTimelineClipMaskForAutomation(clip_id, mask);
    return applied.value(QStringLiteral("ok")).toBool();
  };
  const auto requests_for = [&controller, &frame_size](double end_seconds,
                                                        bool skip_zero) {
    return photo_editor::desktop::TimelineHistoryTestAccess::
        nativeGeneratorFrameSequenceRequestsForRange(
            controller, 0.0, end_seconds, frame_size, skip_zero);
  };
  const auto masks_for = [&controller, &frame_size](double end_seconds) {
    return photo_editor::desktop::TimelineHistoryTestAccess::
        nativeMaskFrameSequenceRequestsForRange(
            controller, 0.0, end_seconds, frame_size);
  };
  const auto arguments_for = [&controller, &workspace_path](
                                 const QString& name,
                                 double end_seconds) {
    QString error;
    const auto arguments =
        photo_editor::desktop::TimelineHistoryTestAccess::renderArguments(
            controller,
            QDir(workspace_path).filePath(name + QStringLiteral(".mp4")),
            QStringLiteral("bounded-h264-video"), 0.0, end_seconds, &error);
    if (arguments.isEmpty()) {
      std::cerr << "The bounded graph should be preparable in opacity workflow: "
                << error.toStdString() << '\n';
    }
    return arguments;
  };
  const auto has_png_sequence_input = [](const QStringList& arguments) {
    return std::any_of(arguments.cbegin(), arguments.cend(),
                       [](const QString& argument) {
      return argument.contains(QStringLiteral("%06d.png"), Qt::CaseInsensitive);
    });
  };

  const auto hidden_clip = create_generator(QStringLiteral("clip opacity zero"), 0.0);
  const auto hidden_clip_id = hidden_clip.value(QStringLiteral("clip"))
                                  .toMap()
                                  .value(QStringLiteral("clipId"))
                                  .toString();
  if (!hidden_clip.value(QStringLiteral("ok")).toBool() ||
      !attach_mask(hidden_clip) || hidden_clip_id.isEmpty() ||
      !photo_editor::desktop::TimelineHistoryTestAccess::
          clipOpacityIsZeroForEveryOutputFrameInRange(
              controller, hidden_clip_id, 0.0, 1.0) ||
      requests_for(1.0, false).size() != 1 ||
      requests_for(1.0, true).size() != 0 ||
      !masks_for(1.0).isEmpty() ||
      has_png_sequence_input(arguments_for(QStringLiteral("hidden-clip"), 1.0))) {
    std::cerr << "A clip whose base opacity is zero should omit its generator and mask frames and graph inputs\n";
    return false;
  }

  const auto hidden_track = create_generator(QStringLiteral("track opacity zero"), 1.0);
  const auto hidden_track_id = hidden_track.value(QStringLiteral("clip"))
                                   .toMap()
                                   .value(QStringLiteral("clipId"))
                                   .toString();
  const auto hidden_track_track = hidden_track.value(QStringLiteral("trackId")).toString();
  if (!hidden_track.value(QStringLiteral("ok")).toBool() ||
      !attach_mask(hidden_track)) {
    std::cerr << "The track-opacity fixture should be created\n";
    return false;
  }
  photo_editor::desktop::TimelineHistoryTestAccess::setTrackOpacityForTest(
      controller, hidden_track_track, 0.0);
  if (!photo_editor::desktop::TimelineHistoryTestAccess::
          clipOpacityIsZeroForEveryOutputFrameInRange(
              controller, hidden_track_id, 0.0, 1.0) ||
      requests_for(1.0, true).size() != 0 || !masks_for(1.0).isEmpty() ||
      has_png_sequence_input(arguments_for(QStringLiteral("hidden-track"), 1.0))) {
    std::cerr << "A track at zero opacity should suppress sources even when its clip remains opaque\n";
    return false;
  }

  const auto mixed = create_generator(QStringLiteral("mixed channel opacity"), 1.0);
  const auto mixed_clip_id = mixed.value(QStringLiteral("clip"))
                                 .toMap()
                                 .value(QStringLiteral("clipId"))
                                 .toString();
  const auto mixed_track_id = mixed.value(QStringLiteral("trackId")).toString();
  if (!mixed.value(QStringLiteral("ok")).toBool() || !attach_mask(mixed)) {
    std::cerr << "The mixed clip/track opacity fixture should be created\n";
    return false;
  }
  const auto mixed_animation = controller.setTimelineClipAnimationForAutomation(
      mixed_clip_id,
      QVariantList{QVariantMap{
          {QStringLiteral("parameter"), QStringLiteral("opacity")},
          {QStringLiteral("keyframes"), QVariantList{
               QVariantMap{{QStringLiteral("localSeconds"), 0.0},
                           {QStringLiteral("value"), 0.0}},
               QVariantMap{{QStringLiteral("localSeconds"), 1.0 / kFps},
                           {QStringLiteral("value"), 1.0}},
               QVariantMap{{QStringLiteral("localSeconds"), 2.0 / kFps},
                           {QStringLiteral("value"), 1.0}},
               QVariantMap{{QStringLiteral("localSeconds"), 3.0 / kFps},
                           {QStringLiteral("value"), 0.0}},
               QVariantMap{{QStringLiteral("localSeconds"), 4.0 / kFps},
                           {QStringLiteral("value"), 1.0}},
           }},
      }});
  photo_editor::desktop::TimelineHistoryTestAccess::
      setTrackOpacityKeyframesForTest(
          controller, mixed_track_id,
          {{0.0, 1.0}, {1.0 / kFps, 0.0}, {2.0 / kFps, 1.0},
           {3.0 / kFps, 1.0}, {4.0 / kFps, 0.0}});
  const auto two_frames_end = 2.0 / kFps;
  const auto five_frames_end = 5.0 / kFps;
  const auto two_frame_requests = requests_for(two_frames_end, true);
  const auto five_frame_requests = requests_for(five_frames_end, true);
  const auto two_frame_graph = arguments_for(QStringLiteral("mixed-two-frames"),
                                             two_frames_end);
  const auto five_frame_graph = arguments_for(QStringLiteral("mixed-five-frames"),
                                              five_frames_end);
  if (!mixed_animation.value(QStringLiteral("ok")).toBool() ||
      !photo_editor::desktop::TimelineHistoryTestAccess::
          clipOpacityIsZeroForEveryOutputFrameInRange(
              controller, mixed_clip_id, 0.0, two_frames_end) ||
      photo_editor::desktop::TimelineHistoryTestAccess::
          clipOpacityIsZeroForEveryOutputFrameInRange(
              controller, mixed_clip_id, 0.0, five_frames_end) ||
      photo_editor::desktop::TimelineHistoryTestAccess::
          clipOpacityIsZeroForEveryOutputFrameInRange(
              controller, mixed_clip_id, 1.0 / kFps + 0.001,
              3.0 / kFps + 0.001) ||
      !two_frame_requests.isEmpty() || five_frame_requests.size() != 1 ||
      !masks_for(two_frames_end).isEmpty() || masks_for(five_frames_end).size() != 1 ||
      has_png_sequence_input(two_frame_graph) ||
      !has_png_sequence_input(five_frame_graph)) {
    std::cerr << "Per-frame global-clock pruning should accept alternating zero clip/track channels, then retain a visible midpoint sample\n";
    return false;
  }

  const auto unsupported = create_generator(QStringLiteral("unsupported bezier opacity"), 1.0);
  const auto unsupported_clip_id = unsupported.value(QStringLiteral("clip"))
                                      .toMap()
                                      .value(QStringLiteral("clipId"))
                                      .toString();
  if (!unsupported.value(QStringLiteral("ok")).toBool() ||
      !attach_mask(unsupported)) {
    std::cerr << "The conservative interpolation fixture should be created\n";
    return false;
  }
  const auto unsupported_animation = controller.setTimelineClipAnimationForAutomation(
      unsupported_clip_id,
      QVariantList{QVariantMap{
          {QStringLiteral("parameter"), QStringLiteral("opacity")},
          {QStringLiteral("keyframes"), QVariantList{
               QVariantMap{{QStringLiteral("localSeconds"), 0.0},
                           {QStringLiteral("value"), 0.0},
                           {QStringLiteral("interpolation"), QStringLiteral("bezier")}},
               QVariantMap{{QStringLiteral("localSeconds"), 1.0},
                           {QStringLiteral("value"), 0.0}},
           }},
      }});
  if (!unsupported_animation.value(QStringLiteral("ok")).toBool() ||
      photo_editor::desktop::TimelineHistoryTestAccess::
          clipOpacityIsZeroForEveryOutputFrameInRange(
              controller, unsupported_clip_id, 0.0, 0.5) ||
      requests_for(0.5, true).size() != 2 || masks_for(0.5).size() != 2 ||
      photo_editor::desktop::TimelineHistoryTestAccess::
          clipOpacityIsZeroForEveryOutputFrameInRange(
              controller, mixed_clip_id, 0.0, 0.0)) {
    std::cerr << "Unsupported Bezier opacity or invalid windows must retain sources conservatively\n";
    return false;
  }

  std::cout << "Exact global-frame opacity pruning omitted only fully transparent generator, mask, and graph inputs\n";
  return true;
}
