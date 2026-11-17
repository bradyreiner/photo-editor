bool run_timeline_agent_mutation_batch_workflow() {
  const auto check = [](bool condition, const char* message) {
    if (!condition) std::cerr << message << '\n';
    return condition;
  };

  TimelineDocumentController controller;
  controller.createTimelineWithSettings({
      {QStringLiteral("timelineName"), QStringLiteral("Atomic Agent Batch")},
      {QStringLiteral("width"), 128},
      {QStringLiteral("height"), 96},
      {QStringLiteral("frameRate"), 24.0},
  });
  QString base_video_track_id;
  for (const auto& value : controller.sequenceTracks()) {
    const auto row = value.toMap();
    if (row.value(QStringLiteral("trackKind")).toString() == QStringLiteral("video") &&
        !row.value(QStringLiteral("locked")).toBool()) {
      base_video_track_id = row.value(QStringLiteral("trackId")).toString();
      break;
    }
  }
  if (!check(!base_video_track_id.isEmpty(),
             "the batch fixture should have an unlocked native video track")) {
    return false;
  }
  const auto seed_result = controller.createTimelineTitleForAutomation(
      base_video_track_id, QStringLiteral("basic"), 0.0, 2.0, {});
  const auto seed_clip_id = seed_result.value(QStringLiteral("createdClipId")).toString();
  if (!check(seed_result.value(QStringLiteral("ok")).toBool() && !seed_clip_id.isEmpty(),
             "the batch fixture should have a preexisting editable title")) {
    return false;
  }
  if (!check(controller.saveTimeline(),
             "the batch fixture should establish a saved clean baseline")) {
    return false;
  }
  const auto preparation = controller.prepareAgentTaskDocumentForAutomation();
  if (!check(preparation.value(QStringLiteral("ok")).toBool() &&
                 preparation.value(QStringLiteral("ready")).toBool() &&
                 controller.acceptAgentTaskDocumentPreparationForAutomation()
                     .value(QStringLiteral("ok")).toBool(),
             "the saved Timeline should enter its stable task-owned state")) {
    return false;
  }

  const auto baseline_state = controller.agentTaskDocumentState();
  const auto baseline_revision =
      photo_editor::desktop::TimelineHistoryTestAccess::documentEditRevision(controller);
  const auto baseline_undo_depth =
      photo_editor::desktop::TimelineHistoryTestAccess::undoDepth(controller);
  const auto baseline_redo_depth =
      photo_editor::desktop::TimelineHistoryTestAccess::redoDepth(controller);

  const auto failed_frame = [&]() {
    return controller.setTimelineClipFrameForAutomation(
        seed_clip_id, 0.25, -0.1, 0.9, 12.0, 0.02, -0.01, 0.7,
        QStringLiteral("fit"), 0.8, 0.75, 0.03, 0.0, 0.0, 0.0, 0.0);
  };
  const QVector<std::function<QVariantMap()>> initial_failure_operations{
      failed_frame,
      []() {
        return QVariantMap{
            {QStringLiteral("ok"), false},
            {QStringLiteral("error"), QStringLiteral("intentional batch rollback probe")}};
      }};
  const auto initial_failure =
      controller.runAgentTaskMutationBatch(initial_failure_operations);
  if (!check(!initial_failure.value(QStringLiteral("ok")).toBool() &&
                 initial_failure.value(QStringLiteral("rolledBack")).toBool() &&
                 controller.agentTaskDocumentState() == baseline_state &&
                 !controller.dirty() && controller.canUndo() == (baseline_undo_depth > 0) &&
                 controller.canRedo() == (baseline_redo_depth > 0) &&
                 photo_editor::desktop::TimelineHistoryTestAccess::undoDepth(controller) ==
                     baseline_undo_depth &&
                 photo_editor::desktop::TimelineHistoryTestAccess::redoDepth(controller) ==
                     baseline_redo_depth &&
                 photo_editor::desktop::TimelineHistoryTestAccess::documentEditRevision(controller) ==
                     baseline_revision,
             "a failed batch should restore exact document, dirty, revision, undo, and redo state")) {
    return false;
  }

  const auto no_op_state = controller.agentTaskDocumentState();
  const auto no_op_undo_depth =
      photo_editor::desktop::TimelineHistoryTestAccess::undoDepth(controller);
  const QVector<std::function<QVariantMap()>> no_op_operations{
      []() { return QVariantMap{{QStringLiteral("ok"), true}}; }};
  const auto no_op_batch = controller.runAgentTaskMutationBatchWithPreimage(
      no_op_operations, no_op_state);
  if (!check(no_op_batch.value(QStringLiteral("ok")).toBool() &&
                 !no_op_batch.value(QStringLiteral("changed")).toBool() &&
                 controller.agentTaskDocumentState() == no_op_state &&
                 photo_editor::desktop::TimelineHistoryTestAccess::undoDepth(controller) ==
                     no_op_undo_depth,
             "a trusted exact preimage should preserve no-op batch detection without adding Undo")) {
    return false;
  }
  const auto empty_preimage_batch = controller.runAgentTaskMutationBatchWithPreimage(
      no_op_operations, QByteArray{});
  if (!check(!empty_preimage_batch.value(QStringLiteral("ok")).toBool() &&
                 empty_preimage_batch.value(QStringLiteral("mutationNotApplied")).toBool() &&
                 controller.agentTaskDocumentState() == no_op_state,
             "an empty coordinator preimage should fail closed before a batch can mutate")) {
    return false;
  }

  auto created_track_id = QString{};
  auto created_clip_id = QString{};
  QVariantMap track_result;
  QVariantMap title_result;
  QVariantMap frame_result;
  QVariantMap frame_after_animation_result;
  QVariantMap animation_result;
  QVariantMap stable_id_readback;
  const QVariantList opacity_channel{
      QVariantMap{{QStringLiteral("localSeconds"), 0.0},
                  {QStringLiteral("value"), 0.85},
                  {QStringLiteral("interpolation"), QStringLiteral("linear")}},
      QVariantMap{{QStringLiteral("localSeconds"), 1.0},
                  {QStringLiteral("value"), 0.35},
                  {QStringLiteral("interpolation"), QStringLiteral("ease-out")}},
  };
  const QVariantList animation_channels{
      QVariantMap{{QStringLiteral("parameter"), QStringLiteral("opacity")},
                  {QStringLiteral("keyframes"), opacity_channel}},
  };
  const QVector<std::function<QVariantMap()>> operations{
      [&]() {
        track_result = controller.createTimelineTrackForAutomation(
            QStringLiteral("graphics"), QStringLiteral("Batch Overlays"));
        created_track_id = track_result.value(QStringLiteral("trackId")).toString();
        return track_result;
      },
      [&]() {
        title_result = controller.createTimelineTitleForAutomation(
            created_track_id, QStringLiteral("headline"), 0.0, 1.5,
            {{QStringLiteral("text"), QStringLiteral("Atomic Batch Title")},
             {QStringLiteral("fontFamily"), QStringLiteral("Anton")},
             {QStringLiteral("fontSize"), 42.0}});
        created_clip_id = title_result.value(QStringLiteral("createdClipId")).toString();
        return title_result;
      },
      [&]() {
        frame_result = controller.setTimelineClipFrameForAutomation(
            created_clip_id, 0.12, -0.08, 0.82, 4.0, 0.015, -0.01, 0.9,
            QStringLiteral("fit"), 0.72, 0.66, 0.02, 0.0, 0.0, 0.0, 0.0);
        return frame_result;
      },
      [&]() {
        animation_result = controller.setTimelineClipAnimationForAutomation(
            created_clip_id, animation_channels);
        return animation_result;
      },
      [&]() {
        frame_after_animation_result = controller.setTimelineClipFrameForAutomation(
            created_clip_id, 0.18, -0.13, 0.95, 0.0, 0.04, -0.02, 0.65,
            QStringLiteral("fit"), 0.72, 0.66, 0.02, 0.0, 0.0, 0.0, 0.0);
        return frame_after_animation_result;
      },
      [&]() {
        const auto clip_result = controller.inspectTimelineClipById(created_clip_id);
        const auto clip = clip_result.value(QStringLiteral("clip")).toMap();
        auto opacity_keyframes = QVariantList{};
        for (const auto& keyframe_value : clip.value(QStringLiteral("keyframes")).toList()) {
          const auto keyframe = keyframe_value.toMap();
          if (keyframe.value(QStringLiteral("parameter")).toString() == QStringLiteral("opacity")) {
            opacity_keyframes.push_back(keyframe);
          }
        }
        auto track_found = false;
        auto clip_on_track = false;
        for (const auto& track_value : controller.timelineTracks()) {
          const auto track = track_value.toMap();
          if (track.value(QStringLiteral("trackId")).toString() != created_track_id) {
            continue;
          }
          track_found = true;
          for (const auto& clip_value : track.value(QStringLiteral("clips")).toList()) {
            if (clip_value.toMap().value(QStringLiteral("clipId")).toString() == created_clip_id) {
              clip_on_track = true;
              break;
            }
          }
        }
        stable_id_readback = {
            {QStringLiteral("ok"), clip_result.value(QStringLiteral("ok")).toBool() &&
                                       track_found && clip_on_track &&
                                       clip.value(QStringLiteral("clipId")).toString() == created_clip_id &&
                                       clip.value(QStringLiteral("trackId")).toString() == created_track_id &&
                                       nearly_equal(clip.value(QStringLiteral("baseOpacity")).toDouble(), 0.65) &&
                                       opacity_keyframes.size() == 2 &&
                                       nearly_equal(opacity_keyframes.at(0).toMap()
                                                        .value(QStringLiteral("value")).toDouble(), 0.85) &&
                                       nearly_equal(opacity_keyframes.at(1).toMap()
                                                        .value(QStringLiteral("value")).toDouble(), 0.35)},
            {QStringLiteral("trackId"), created_track_id},
            {QStringLiteral("clipId"), created_clip_id},
            {QStringLiteral("opacityKeyframesPreserved"), opacity_keyframes.size() == 2},
            {QStringLiteral("clip"), clip},
        };
        return stable_id_readback;
      },
  };

  int clips_changed_count = 0;
  int sequence_changed_count = 0;
  int dirty_changed_count = 0;
  int undo_changed_count = 0;
  const auto clips_connection = QObject::connect(
      &controller, &TimelineDocumentController::clipsChanged, &controller,
      [&]() { ++clips_changed_count; });
  const auto sequence_connection = QObject::connect(
      &controller, &TimelineDocumentController::sequenceChanged, &controller,
      [&]() { ++sequence_changed_count; });
  const auto dirty_connection = QObject::connect(
      &controller, &TimelineDocumentController::dirtyChanged, &controller,
      [&]() { ++dirty_changed_count; });
  const auto undo_connection = QObject::connect(
      &controller, &TimelineDocumentController::undoStateChanged, &controller,
      [&]() { ++undo_changed_count; });
  const auto batch = controller.runAgentTaskMutationBatchWithPreimage(
      operations, no_op_state);
  QObject::disconnect(clips_connection);
  QObject::disconnect(sequence_connection);
  QObject::disconnect(dirty_connection);
  QObject::disconnect(undo_connection);

  const auto batch_state = controller.agentTaskDocumentState();
  const auto batch_revision =
      photo_editor::desktop::TimelineHistoryTestAccess::documentEditRevision(controller);
  const auto batch_undo_depth =
      photo_editor::desktop::TimelineHistoryTestAccess::undoDepth(controller);
  if (!check(batch.value(QStringLiteral("ok")).toBool() &&
                 batch.value(QStringLiteral("changed")).toBool() &&
                 batch.value(QStringLiteral("operationCount")).toInt() ==
                     static_cast<int>(operations.size()) &&
                 stable_id_readback.value(QStringLiteral("ok")).toBool() &&
                 !created_track_id.isEmpty() && !created_clip_id.isEmpty() &&
                 frame_result.value(QStringLiteral("ok")).toBool() &&
                 animation_result.value(QStringLiteral("ok")).toBool() &&
                 frame_after_animation_result.value(QStringLiteral("ok")).toBool() &&
                 controller.dirty() && batch_undo_depth == baseline_undo_depth + 1 &&
                 batch_revision == baseline_revision + 1 &&
                 clips_changed_count == 1 && sequence_changed_count == 1 &&
                 dirty_changed_count == 1 && undo_changed_count == 1,
             "a successful createTrack→createTitle→frame→animation→base-frame batch should preserve curves and publish one Undo/signal unit")) {
    std::cerr << "batch=" << QJsonDocument::fromVariant(batch).toJson(QJsonDocument::Compact).toStdString()
              << " stableReadback=" << QJsonDocument::fromVariant(stable_id_readback)
                     .toJson(QJsonDocument::Compact).toStdString()
              << " signals=" << clips_changed_count << ',' << sequence_changed_count << ','
              << dirty_changed_count << ',' << undo_changed_count << '\n';
    return false;
  }

  controller.undo();
  if (!check(controller.agentTaskDocumentState() == baseline_state && controller.canRedo() &&
                 photo_editor::desktop::TimelineHistoryTestAccess::undoDepth(controller) ==
                     baseline_undo_depth &&
                 photo_editor::desktop::TimelineHistoryTestAccess::redoDepth(controller) ==
                     baseline_redo_depth + 1,
             "one Undo should remove every batch edit and restore the exact baseline document")) {
    return false;
  }

  const auto rollback_dirty_before = controller.dirty();
  const auto rollback_revision_before =
      photo_editor::desktop::TimelineHistoryTestAccess::documentEditRevision(controller);
  const auto rollback_undo_before =
      photo_editor::desktop::TimelineHistoryTestAccess::undoDepth(controller);
  const auto rollback_redo_before =
      photo_editor::desktop::TimelineHistoryTestAccess::redoDepth(controller);
  const QVector<std::function<QVariantMap()>> redo_branch_failure_operations{
      failed_frame,
      []() {
        return QVariantMap{{QStringLiteral("ok"), false},
                           {QStringLiteral("error"), QStringLiteral("intentional redo preservation probe")}};
      }};
  const auto redo_branch_failure =
      controller.runAgentTaskMutationBatch(redo_branch_failure_operations);
  if (!check(!redo_branch_failure.value(QStringLiteral("ok")).toBool() &&
                 redo_branch_failure.value(QStringLiteral("rolledBack")).toBool() &&
                 controller.agentTaskDocumentState() == baseline_state &&
                 controller.dirty() == rollback_dirty_before && controller.canRedo() &&
                 photo_editor::desktop::TimelineHistoryTestAccess::documentEditRevision(controller) ==
                     rollback_revision_before &&
                 photo_editor::desktop::TimelineHistoryTestAccess::undoDepth(controller) ==
                     rollback_undo_before &&
                 photo_editor::desktop::TimelineHistoryTestAccess::redoDepth(controller) ==
                     rollback_redo_before,
             "a failed batch after Undo should preserve exact state, dirty/revision, and the existing redo branch")) {
    return false;
  }

  controller.redo();
  if (!check(controller.agentTaskDocumentState() == batch_state && controller.canUndo() &&
                 !controller.canRedo() &&
                 photo_editor::desktop::TimelineHistoryTestAccess::undoDepth(controller) ==
                     batch_undo_depth,
             "Redo should restore the exact batched document after a failed rollback probe")) {
    return false;
  }

  controller.releaseAgentTaskDocumentForAutomation();
  return true;
}
