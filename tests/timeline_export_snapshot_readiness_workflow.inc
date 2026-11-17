bool run_timeline_export_snapshot_readiness_workflow() {
  const auto check = [](bool condition, const char* message) {
    if (!condition) std::cerr << message << '\n';
    return condition;
  };

  TimelineDocumentController controller;
  controller.createTimelineWithSettings({
      {QStringLiteral("timelineName"), QStringLiteral("Export Snapshot Readiness")},
      {QStringLiteral("width"), 128},
      {QStringLiteral("height"), 96},
      {QStringLiteral("frameRate"), 24.0},
  });
  const auto expected_hash = controller.deterministicDocumentHash();
  const auto task_snapshot = controller.agentTaskDocumentState();
  if (!check(!expected_hash.isEmpty() && !task_snapshot.isEmpty() &&
                 controller.agentTaskDocumentReady(),
             "a new idle Timeline should satisfy the unchanged native task readiness contract")) {
    return false;
  }

  photo_editor::desktop::TimelineHistoryTestAccess::stageExportSnapshotDerivedWorkFixture(
      controller);
  const auto readiness = controller.agentExportSnapshotReadiness();
  const auto derived = readiness.value(QStringLiteral("derivedWork")).toMap();
  const auto inspected = controller.inspectAgentDocument();
  const auto export_snapshot = controller.agentExportSnapshotState(expected_hash);
  if (!check(!controller.agentTaskDocumentReady(),
             "native task readiness must continue blocking the existing derived-work set") ||
      !check(readiness.value(QStringLiteral("ready")).toBool() &&
                 readiness.value(QStringLiteral("blockingReasons")).toList().isEmpty(),
             "independent derived work should not block a stable export snapshot") ||
      !check(derived.value(QStringLiteral("sourceIntegrityActive")).toBool() &&
                 derived.value(QStringLiteral("waveformActive")).toBool() &&
                 derived.value(QStringLiteral("proxyGenerationActive")).toBool() &&
                 derived.value(QStringLiteral("posterGenerationActive")).toBool() &&
                 derived.value(QStringLiteral("renderCacheActive")).toBool() &&
                 derived.value(QStringLiteral("sourceIntegrityManifestRequired")).toBool(),
             "readiness diagnostics should expose derived jobs and preserve the manifest gate") ||
      !check(!inspected.value(QStringLiteral("ready")).toBool() &&
                 inspected.value(QStringLiteral("exportSnapshotReady")).toBool() &&
                 inspected.value(QStringLiteral("exportSnapshotReadiness")).toMap() == readiness,
             "document inspection should report legacy task readiness separately from export snapshot readiness") ||
      !check(!export_snapshot.isEmpty() && export_snapshot == task_snapshot &&
                 controller.deterministicDocumentHash() == expected_hash,
             "derived work should produce the exact immutable snapshot for the inspected hash") ||
      !check(controller.agentExportSnapshotState(QString(64, QLatin1Char('f'))).isEmpty(),
             "export snapshot serialization must reject a stale caller hash")) {
    return false;
  }

  photo_editor::desktop::TimelineHistoryTestAccess::setContinuousEditFixture(
      controller, true);
  const auto edit_readiness = controller.agentExportSnapshotReadiness();
  const auto edit_blockers = edit_readiness.value(QStringLiteral("blockingReasons")).toList();
  if (!check(!edit_readiness.value(QStringLiteral("ready")).toBool() &&
                 edit_blockers.contains(QStringLiteral("continuous-edit-active")) &&
                 controller.agentExportSnapshotState(expected_hash).isEmpty(),
             "an active continuous edit must block export snapshot creation")) {
    return false;
  }
  photo_editor::desktop::TimelineHistoryTestAccess::setContinuousEditFixture(
      controller, false);

  photo_editor::desktop::TimelineHistoryTestAccess::stageExportSnapshotMetadataWorkFixture(
      controller);
  const auto metadata_readiness = controller.agentExportSnapshotReadiness();
  if (!check(!metadata_readiness.value(QStringLiteral("ready")).toBool() &&
                 metadata_readiness.value(QStringLiteral("blockingReasons")).toList().contains(
                     QStringLiteral("source-metadata-analysis-active")) &&
                 controller.agentExportSnapshotState(expected_hash).isEmpty(),
             "render-input source metadata analysis must fail closed before snapshotting")) {
    return false;
  }
  return true;
}
