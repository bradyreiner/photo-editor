bool run_timeline_gap_playback_notification_workflow(const QString& workspace_path) {
  const auto fail_workflow = [](const char* message) {
    std::cerr << message << '\n';
    return false;
  };

  const auto first_path = QDir(workspace_path).filePath(QStringLiteral("gap-playback-a.png"));
  const auto second_path = QDir(workspace_path).filePath(QStringLiteral("gap-playback-b.png"));
  QImage first_image(16, 16, QImage::Format_RGBA8888);
  first_image.fill(QColor(210, 50, 60));
  QImage second_image(16, 16, QImage::Format_RGBA8888);
  second_image.fill(QColor(40, 100, 220));
  if (!first_image.save(first_path) || !second_image.save(second_path)) {
    return fail_workflow("gap playback source images should be writable");
  }

  TimelineDocumentController controller;
  controller.createTimelineWithSettings({
      {QStringLiteral("timelineName"), QStringLiteral("Gap Playback Notifications")},
      {QStringLiteral("width"), 160},
      {QStringLiteral("height"), 90},
      {QStringLiteral("frameRate"), 24.0},
  });
  controller.addClipFromGallery(
      QStringLiteral("photo:gap-playback-a"), first_path,
      QStringLiteral("First shot"), 2.0, first_path);
  const auto first_clip_id = controller.currentClip()
                                 .value(QStringLiteral("clipId")).toString();
  if (first_clip_id.isEmpty() ||
      !controller.toggleSelectedClipKeyframe(QStringLiteral("opacity"))) {
    return fail_workflow("the first still should be selected and accept an opacity keyframe");
  }
  controller.setSelectedClipOpacity(0.2);
  controller.seekSequenceSeconds(1.5);
  controller.setSelectedClipOpacity(0.8);
  controller.addClipFromGallery(
      QStringLiteral("photo:gap-playback-b"), second_path,
      QStringLiteral("Second shot"), 2.0, second_path);
  const auto second_clip_id = controller.currentClip()
                                  .value(QStringLiteral("clipId")).toString();
  if (second_clip_id.isEmpty() || second_clip_id == first_clip_id) {
    return fail_workflow("the second still should be appended as a distinct active clip");
  }

  controller.selectClip(0);
  controller.seekSequenceSeconds(0.0);
  const auto authored_hash = controller.deterministicDocumentHash();
  const auto first_program_snapshot = controller.snapshotProgramClip();
  if (first_program_snapshot.value(QStringLiteral("clipId")).toString() != first_clip_id ||
      controller.programClipId() != first_clip_id ||
      controller.property("programClipId").toString() != first_clip_id) {
    return fail_workflow("the scalar Program identity should match the full Program snapshot");
  }

  int selection_notifications = 0;
  int playback_notifications = 0;
  const auto selection_connection = QObject::connect(
      &controller, &TimelineDocumentController::selectionChanged,
      &controller, [&selection_notifications]() { ++selection_notifications; },
      Qt::DirectConnection);
  const auto playback_connection = QObject::connect(
      &controller, &TimelineDocumentController::playbackStateChanged,
      &controller, [&playback_notifications]() { ++playback_notifications; },
      Qt::DirectConnection);

  controller.playSequence();
  selection_notifications = 0;
  playback_notifications = 0;
  controller.syncGapPlaybackPosition(0.75);
  const auto first_tick_clip = controller.currentClip();
  if (selection_notifications != 0 || playback_notifications != 1 ||
      first_tick_clip.value(QStringLiteral("clipId")).toString() != first_clip_id ||
      !nearly_equal(first_tick_clip.value(QStringLiteral("opacity")).toDouble(), 0.5) ||
      controller.programClipId() != first_clip_id ||
      !controller.sequencePlaying()) {
    QObject::disconnect(selection_connection);
    QObject::disconnect(playback_connection);
    return fail_workflow("same-clip playback ticks should publish scalar playback without selection churn and keep evaluated clip reads live");
  }

  controller.syncGapPlaybackPosition(1.25);
  const auto second_tick_clip = controller.currentClip();
  if (selection_notifications != 0 || playback_notifications != 2 ||
      second_tick_clip.value(QStringLiteral("clipId")).toString() != first_clip_id ||
      !nearly_equal(second_tick_clip.value(QStringLiteral("opacity")).toDouble(), 0.7) ||
      controller.programClipId() != first_clip_id) {
    QObject::disconnect(selection_connection);
    QObject::disconnect(playback_connection);
    return fail_workflow("continued playback on one clip should avoid identity notifications while evaluating its current keyframe value");
  }

  controller.syncGapPlaybackPosition(2.25);
  const auto boundary_clip = controller.currentClip();
  const auto second_program_snapshot = controller.snapshotProgramClip();
  QObject::disconnect(selection_connection);
  QObject::disconnect(playback_connection);
  if (selection_notifications != 1 || playback_notifications != 3 ||
      boundary_clip.value(QStringLiteral("clipId")).toString() != second_clip_id ||
      controller.programClipId() != second_clip_id ||
      second_program_snapshot.value(QStringLiteral("clipId")).toString() != second_clip_id ||
      !controller.sequencePlaying()) {
    return fail_workflow("crossing a clip boundary should publish one identity change and update the scalar Program ID");
  }
  if (controller.deterministicDocumentHash() != authored_hash) {
    return fail_workflow("playback identity notifications and keyframe evaluation must not mutate authored document state");
  }
  int seek_selection_notifications = 0;
  int seek_preview_notifications = 0;
  const auto seek_selection_connection = QObject::connect(
      &controller, &TimelineDocumentController::selectionChanged,
      &controller, [&]() { ++seek_selection_notifications; });
  const auto seek_preview_connection = QObject::connect(
      &controller, &TimelineDocumentController::previewSourceChanged,
      &controller, [&]() { ++seek_preview_notifications; });
  controller.seekSequenceSeconds(0.25);
  seek_selection_notifications = 0;
  seek_preview_notifications = 0;
  controller.seekSequenceSeconds(0.5);
  controller.seekSequenceSeconds(0.75);
  const auto scrubbed_first = controller.currentClip();
  if (seek_selection_notifications != 0 || seek_preview_notifications != 0 ||
      !nearly_equal(controller.sequencePlayheadSeconds(), 0.75) ||
      !nearly_equal(scrubbed_first.value(QStringLiteral("opacity")).toDouble(), 0.5)) {
    return fail_workflow("same-clip scrubbing must keep evaluated time/color live without selection or preview churn");
  }
  controller.seekSequenceSeconds(2.25);
  QObject::disconnect(seek_selection_connection);
  QObject::disconnect(seek_preview_connection);
  if (seek_selection_notifications != 1 || seek_preview_notifications != 1 ||
      controller.programClipId() != second_clip_id ||
      controller.deterministicDocumentHash() != authored_hash) {
    return fail_workflow("scrub boundaries must refresh identity once without changing the edit");
  }
  return true;
}
