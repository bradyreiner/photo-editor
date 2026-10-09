if (qEnvironmentVariableIsSet("PHOTO_EDITOR_BENCHMARK_CHECKPOINT_TOOL")) {
  using CheckpointAccess = photo_editor::desktop::EditorPreviewControllerTestAccess;
  if (qEnvironmentVariableIsSet("PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE"))
    return fail(QStringLiteral("Checkpoint tool proof requires real local persistence."));
  CheckpointAccess::install_checkpoint_raster(editor_preview, 2048);
  std::vector<double> commits;
  qint64 checkpoint_bytes = 0;
  for (int edit = 0; edit < 21; ++edit) {
    editor_preview.beginCoalescedEdit();
    editor_preview.setExposure(static_cast<double>(edit + 1) / 100.0);
    QElapsedTimer commit_timer;
    commit_timer.start();
    editor_preview.endCoalescedEdit();
    const double elapsed_ms = static_cast<double>(commit_timer.nsecsElapsed()) / 1e6;
    if (edit > 0) commits.push_back(elapsed_ms);
    // Isolate the synchronous, crash-safe local checkpoint from database helper
    // dispatch. This never disables checkpoint writes or uses a live catalog.
    checkpoint_bytes = CheckpointAccess::stop_checkpoint_helper_and_size(editor_preview);
    if (checkpoint_bytes < 4 * 1024 * 1024)
      return fail(QStringLiteral("Raster edit did not produce its durable checkpoint."));
    if (!wait_for_condition([&] { return CheckpointAccess::all_idle(editor_preview); }, timeout_ms))
      return fail(QStringLiteral("Checkpoint tool render did not settle."));
    CheckpointAccess::stop_checkpoint_helper_and_size(editor_preview);
  }
  log_summary(QStringLiteral("checkpoint_tool_commit"), summarize_samples(commits),
      static_cast<int>(commits.size()), {{"photo_iteration", iteration},
      {"checkpoint_bytes", checkpoint_bytes}, {"local_persistence", true},
      {"database_helper_dispatch", false}, {"raster_bytes", 4 * 1024 * 1024}});
  save_capture_if_requested(root_window, capture_dir,
      QStringLiteral("checkpoint-tool-%1").arg(iteration), &captures);
  continue;
}
