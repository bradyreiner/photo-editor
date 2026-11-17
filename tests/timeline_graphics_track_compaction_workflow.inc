bool run_timeline_graphics_track_compaction_workflow(
    const QString& workspace_path) {
  const auto check = [](bool condition, const char* message) {
    if (!condition) {
      std::cerr << "timeline_graphics_track_compaction: " << message << '\n';
    }
    return condition;
  };

  TimelineDocumentController controller;
  controller.setEditAccessPolicy([] { return true; });
  controller.createTimelineWithSettings({
      {QStringLiteral("timelineName"),
       QStringLiteral("Graphics track compaction workflow")},
      {QStringLiteral("width"), 128},
      {QStringLiteral("height"), 96},
      {QStringLiteral("frameRate"), 24.0},
  });

  struct Layer final {
    QString label;
    QString color;
    double start = 0.0;
    double duration = 0.0;
  };
  const QVector<Layer> layers{
      {QStringLiteral("Base A"), QStringLiteral("#ffff0000"), 0.0, 1.0},
      {QStringLiteral("Base B"), QStringLiteral("#ff0000ff"), 1.0, 1.0},
      {QStringLiteral("Animated overlay"), QStringLiteral("#ff00ff00"), 0.5, 1.0},
      {QStringLiteral("Later base"), QStringLiteral("#ffffff00"), 2.0, 1.0},
  };
  QStringList clip_ids;
  QStringList track_ids;
  for (const auto& layer : layers) {
    const auto created_track = controller.createTimelineTrackForAutomation(
        QStringLiteral("graphics"), layer.label, QString{});
    const auto track_id =
        created_track.value(QStringLiteral("trackId")).toString();
    if (!check(created_track.value(QStringLiteral("ok")).toBool() &&
                   !track_id.isEmpty(),
               "source graphics tracks should be created with stable IDs")) {
      return false;
    }
    const auto created_clip = controller.createTimelineGraphicForAutomation(
        track_id, QStringLiteral("rectangle"), layer.start, layer.duration,
        QVariantMap{
            {QStringLiteral("positionX"), 0.5},
            {QStringLiteral("positionY"), 0.5},
            {QStringLiteral("shapeWidth"), 1.0},
            {QStringLiteral("shapeHeight"), 1.0},
            {QStringLiteral("shapeStrokeWidth"), 0.0},
            {QStringLiteral("backgroundPadding"), 0.0},
            {QStringLiteral("backgroundColor"), layer.color},
        });
    const auto clip_id =
        created_clip.value(QStringLiteral("createdClipId")).toString();
    if (!check(created_clip.value(QStringLiteral("ok")).toBool() &&
                   !clip_id.isEmpty(),
               "each source graphics track should hold its editable shape")) {
      return false;
    }
    track_ids.push_back(track_id);
    clip_ids.push_back(clip_id);
  }

  const QVariantList opacity_keyframes{
      QVariantMap{{QStringLiteral("localSeconds"), 0.0},
                  {QStringLiteral("value"), 0.4},
                  {QStringLiteral("interpolation"), QStringLiteral("linear")}},
      QVariantMap{{QStringLiteral("localSeconds"), 1.0},
                  {QStringLiteral("value"), 1.0},
                  {QStringLiteral("interpolation"), QStringLiteral("linear")}}};
  const auto animation = controller.setTimelineClipAnimationForAutomation(
      clip_ids.at(2),
      QVariantList{QVariantMap{
          {QStringLiteral("parameter"), QStringLiteral("opacity")},
          {QStringLiteral("keyframes"), opacity_keyframes}}});
  const auto mask = controller.setTimelineClipMaskForAutomation(
      clip_ids.at(3),
      QVariantMap{{QStringLiteral("shape"), QStringLiteral("rectangle")},
                  {QStringLiteral("left"), 0.05},
                  {QStringLiteral("top"), 0.1},
                  {QStringLiteral("right"), 0.95},
                  {QStringLiteral("bottom"), 0.9},
                  {QStringLiteral("cornerRadius"), 0.0}});
  const auto blend = controller.setTimelineClipBlendModeForAutomation(
      clip_ids.at(2), QStringLiteral("screen"));
  if (!check(animation.value(QStringLiteral("ok")).toBool() &&
                 mask.value(QStringLiteral("ok")).toBool() &&
                 blend.value(QStringLiteral("ok")).toBool(),
             "clip animation, mask, and blend state should be authored before compaction")) {
    return false;
  }

  controller.seekSequenceSeconds(1.25);
  const auto first_marker_id =
      controller.addSequenceMarker(QStringLiteral("Compaction marker A"));
  controller.seekSequenceSeconds(2.5);
  const auto second_marker_id =
      controller.addSequenceMarker(QStringLiteral("Compaction marker B"));
  if (!check(!first_marker_id.isEmpty() && !second_marker_id.isEmpty(),
             "sequence markers should be authored before the compaction baseline")) {
    return false;
  }

  const auto track_rows = [&controller]() {
    QVector<QVariantMap> graphics;
    for (const auto& value : controller.timelineTracks()) {
      const auto row = value.toMap();
      if (row.value(QStringLiteral("trackRole")).toString() ==
          QStringLiteral("graphics")) {
        graphics.push_back(row);
      }
    }
    std::sort(graphics.begin(), graphics.end(),
              [](const QVariantMap& left, const QVariantMap& right) {
                return left.value(QStringLiteral("laneIndex")).toInt() <
                       right.value(QStringLiteral("laneIndex")).toInt();
              });
    return graphics;
  };
  const auto clip_payloads = [&clip_ids](
                                 const TimelineDocumentController& source) {
    QHash<QString, QJsonObject> result;
    const auto document =
        photo_editor::desktop::TimelineHistoryTestAccess::documentObject(source);
    for (const auto& track_value : document.value(QStringLiteral("tracks")).toArray()) {
      const auto track = track_value.toObject();
      for (const auto& clip_value : track.value(QStringLiteral("clips")).toArray()) {
        const auto clip = clip_value.toObject();
        const auto clip_id = clip.value(QStringLiteral("clipId")).toString();
        if (clip_ids.contains(clip_id)) result.insert(clip_id, clip);
      }
    }
    return result;
  };
  const auto serialized_markers = [](
                                      const TimelineDocumentController& source) {
    return photo_editor::desktop::TimelineHistoryTestAccess::documentObject(source)
        .value(QStringLiteral("markers")).toArray();
  };
  const auto report_clip_payload_differences = [](
      const QHash<QString, QJsonObject>& expected,
      const QHash<QString, QJsonObject>& actual,
      const char* context) {
    for (auto iterator = expected.cbegin(); iterator != expected.cend(); ++iterator) {
      const auto actual_clip = actual.value(iterator.key());
      if (actual_clip == iterator.value()) continue;
      QStringList differing_keys;
      for (const auto& key : iterator.value().keys()) {
        if (iterator.value().value(key) != actual_clip.value(key)) {
          differing_keys.push_back(key);
        }
      }
      for (const auto& key : actual_clip.keys()) {
        if (!iterator.value().contains(key) && !differing_keys.contains(key)) {
          differing_keys.push_back(key);
        }
      }
      std::sort(differing_keys.begin(), differing_keys.end());
      std::cerr << "timeline_graphics_track_compaction: " << context
                << " clip=" << iterator.key().toStdString()
                << " differing serialized fields="
                << differing_keys.join(QLatin1Char(',')).toStdString() << '\n';
    }
    if (expected.size() != actual.size()) {
      std::cerr << "timeline_graphics_track_compaction: " << context
                << " serialized clip count expected=" << expected.size()
                << " actual=" << actual.size() << '\n';
    }
  };
  const auto render = [](TimelineDocumentController& source,
                         const QString& pattern) {
    QString error;
    if (render_timeline_image_sequence(source, pattern, &error)) return true;
    std::cerr << "timeline_graphics_track_compaction: render failed: "
              << error.toStdString() << '\n';
    return false;
  };
  const auto compare_samples = [&check](const QString& left_pattern,
                                        const QString& right_pattern) {
    constexpr double kFps = 24.0;
    const QVector<double> sample_seconds{
        0.125, 0.5, 0.75, 0.9583333333333334, 1.0, 1.25,
        1.5, 1.75, 2.0, 2.375, 2.875};
    for (const auto seconds : sample_seconds) {
      const auto frame_number = qRound(seconds * kFps) + 1;
      const QImage left(timeline_image_sequence_frame(left_pattern, frame_number));
      const QImage right(timeline_image_sequence_frame(right_pattern, frame_number));
      if (!check(!left.isNull() && !right.isNull() &&
                     left.convertToFormat(QImage::Format_RGBA8888) ==
                         right.convertToFormat(QImage::Format_RGBA8888),
                 "overlap, boundary, and sampled pixels should match exactly")) {
        std::cerr << "timeline_graphics_track_compaction: pixel mismatch at t="
                  << seconds << "s, frame=" << frame_number << '\n';
        return false;
      }
    }
    return true;
  };

  const auto before_tracks = track_rows();
  const auto original_markers = serialized_markers(controller);
  if (!check(before_tracks.size() == layers.size(),
             "the fixture should begin with four graphics tracks")) {
    return false;
  }
  if (!check(original_markers.size() == 2 &&
                 original_markers.at(0).toObject().value(QStringLiteral("seconds")).toDouble() == 1.25 &&
                 original_markers.at(1).toObject().value(QStringLiteral("seconds")).toDouble() == 2.5,
             "serialized sequence markers should match the authored baseline times")) {
    return false;
  }
  const auto original_clip_payloads = clip_payloads(controller);
  if (!check(original_clip_payloads.size() == clip_ids.size(),
             "all source clip fields should be inspectable before compaction")) {
    return false;
  }

  const auto preview = controller.compactTimelineGraphicsTracksForAutomation(
      false, true);
  if (!check(preview.value(QStringLiteral("ok")).toBool() &&
                 preview.value(QStringLiteral("wouldChange")).toBool() &&
                 !preview.value(QStringLiteral("applied")).toBool() &&
                 preview.value(QStringLiteral("originalGraphicsTrackCount")).toInt() == 4 &&
                 preview.value(QStringLiteral("compactedGraphicsTrackCount")).toInt() == 2 &&
                 preview.value(QStringLiteral("graphicsClipCount")).toInt() == 4 &&
                 preview.value(QStringLiteral("assignments")).toList().size() == 4 &&
                 preview.value(QStringLiteral("moves")).toList().size() == 3 &&
                 preview.value(QStringLiteral("removedTracks")).toList().size() == 2 &&
                 track_rows() == before_tracks &&
                 serialized_markers(controller) == original_markers &&
                 clip_payloads(controller) == original_clip_payloads,
             "read-only preview should return an exact plan without changing clips, tracks, or markers")) {
    return false;
  }

  const auto lock_row = [&before_tracks](const QString& id) {
    for (const auto& row : before_tracks) {
      if (row.value(QStringLiteral("trackId")).toString() == id) {
        return row.value(QStringLiteral("trackIndex")).toInt();
      }
    }
    return -1;
  }(track_ids.at(3));
  if (!check(lock_row >= 0, "the donor-track lock fixture should be locatable")) {
    return false;
  }
  controller.toggleTrackLocked(lock_row);
  const auto locked_tracks = controller.timelineTracks();
  const auto locked_clips = clip_payloads(controller);
  const auto rejected = controller.compactTimelineGraphicsTracksForAutomation(true);
  const auto unchanged_after_rejection =
      controller.timelineTracks() == locked_tracks &&
      clip_payloads(controller) == locked_clips;
  controller.toggleTrackLocked(lock_row);
  if (!check(!rejected.value(QStringLiteral("ok")).toBool() &&
                 rejected.value(QStringLiteral("mutationNotApplied")).toBool() &&
                 unchanged_after_rejection,
             "a locked non-neutral track should reject before changing source state")) {
    return false;
  }

  const auto before_pattern = QDir(workspace_path).filePath(
      QStringLiteral("graphics-compaction-before-%06d.png"));
  if (!render(controller, before_pattern)) return false;
  const auto compacted = controller.compactTimelineGraphicsTracksForAutomation(
      true, true);
  const auto after_tracks = track_rows();
  const auto compact_clip_payloads = clip_payloads(controller);
  if (!check(compacted.value(QStringLiteral("ok")).toBool() &&
                 compacted.value(QStringLiteral("applied")).toBool() &&
                 compacted.value(QStringLiteral("changed")).toBool() &&
                 compacted.value(QStringLiteral("removedTracks")).toList().size() == 2 &&
                 compacted.value(QStringLiteral("renamedTracks")).toList().size() == 2 &&
                 after_tracks.size() == 2 &&
                 serialized_markers(controller) == original_markers &&
                 compact_clip_payloads == original_clip_payloads,
             "one atomic compaction should retain clip fields and every sequence marker")) {
    report_clip_payload_differences(original_clip_payloads,
                                    compact_clip_payloads,
                                    "after compaction");
    return false;
  }
  for (qsizetype index = 0; index < after_tracks.size(); ++index) {
    const auto expected = QStringLiteral("Graphics %1").arg(
        static_cast<qulonglong>(index + 1), 2, 10, QLatin1Char('0'));
    if (!check(after_tracks.at(index).value(QStringLiteral("customName")).toString() ==
                   expected,
               "explicit row renaming should produce stable ordered Graphics NN names")) {
      return false;
    }
  }

  const auto after_pattern = QDir(workspace_path).filePath(
      QStringLiteral("graphics-compaction-after-%06d.png"));
  if (!render(controller, after_pattern) ||
      !compare_samples(before_pattern, after_pattern)) {
    return false;
  }

  controller.undo();
  if (!check(track_rows().size() == 4 &&
                 serialized_markers(controller) == original_markers &&
                 clip_payloads(controller) == original_clip_payloads,
             "one undo should restore every original graphics row, clip field, and marker")) {
    report_clip_payload_differences(original_clip_payloads,
                                    clip_payloads(controller),
                                    "after undo");
    return false;
  }
  const auto undo_pattern = QDir(workspace_path).filePath(
      QStringLiteral("graphics-compaction-undo-%06d.png"));
  if (!render(controller, undo_pattern) ||
      !compare_samples(before_pattern, undo_pattern)) {
    return false;
  }

  controller.redo();
  if (!check(track_rows().size() == 2 &&
                 serialized_markers(controller) == original_markers &&
                 clip_payloads(controller) == original_clip_payloads,
             "one redo should reapply compaction without changing clip fields or markers")) {
    report_clip_payload_differences(original_clip_payloads,
                                    clip_payloads(controller),
                                    "after redo");
    return false;
  }
  const auto redo_pattern = QDir(workspace_path).filePath(
      QStringLiteral("graphics-compaction-redo-%06d.png"));
  if (!render(controller, redo_pattern) ||
      !compare_samples(after_pattern, redo_pattern)) {
    return false;
  }

  if (!check(controller.saveTimeline(false) &&
                 !controller.savedDocumentPath().isEmpty(),
             "the compacted native project should save before reopen verification")) {
    return false;
  }
  TimelineDocumentController reopened;
  const auto cached_empty_duration = reopened.totalDurationSecondsValue();
  if (!check(cached_empty_duration == 0.0,
             "the reopened controller should cache its empty duration before loading")) {
    return false;
  }
  if (!check(reopened.loadTimeline(controller.savedDocumentPath()),
             "the saved compacted native project should reopen")) {
    return false;
  }
  const auto reopened_tracks = [&reopened]() {
    auto rows = reopened.timelineTracks();
    rows.erase(std::remove_if(rows.begin(), rows.end(), [](const QVariant& value) {
      return value.toMap().value(QStringLiteral("trackRole")).toString() !=
             QStringLiteral("graphics");
    }), rows.end());
    return rows;
  }();
  if (!check(reopened_tracks.size() == 2,
             "the saved project should retain the compacted graphics-track count")) {
    return false;
  }
  if (!check(serialized_markers(reopened) == original_markers,
             "load after cached empty duration should preserve exact serialized marker records")) {
    return false;
  }
  for (const auto& clip_id : clip_ids) {
    const auto inspected = reopened.inspectTimelineClipById(clip_id);
    const auto reopened_payloads = clip_payloads(reopened);
    if (!check(inspected.value(QStringLiteral("ok")).toBool() &&
                   reopened_payloads.value(clip_id) ==
                       original_clip_payloads.value(clip_id),
               "save/reopen should retain each stable clip and its complete authored fields")) {
      report_clip_payload_differences(
          original_clip_payloads, reopened_payloads, "after save/reopen");
      return false;
    }
  }
  const auto reopened_pattern = QDir(workspace_path).filePath(
      QStringLiteral("graphics-compaction-reopen-%06d.png"));
  return render(reopened, reopened_pattern) &&
         compare_samples(after_pattern, reopened_pattern);
}
