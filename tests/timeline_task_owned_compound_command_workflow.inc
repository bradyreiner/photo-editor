#include "agent_task_coordinator.h"
#include "agent_timeline_compound_commands.h"

bool run_pending_compound_recovery_proof_workflow(const QString& workspace_path);

bool run_timeline_task_owned_compound_command_workflow(const QString& workspace_path) {
  if (QStandardPaths::findExecutable(QStringLiteral("ffmpeg")).isEmpty() ||
      QStandardPaths::findExecutable(QStringLiteral("ffprobe")).isEmpty()) {
    std::cerr << "task-owned compound command test needs ffmpeg and ffprobe\n";
    return false;
  }
  const auto output_directory = QDir(workspace_path).filePath(QStringLiteral("compound-mcp-outputs"));
  const auto journal_root = QDir(workspace_path).filePath(QStringLiteral("compound-mcp-journal"));
  if (!QDir().mkpath(output_directory) || !QDir().mkpath(journal_root)) {
    std::cerr << "compound command output and journal roots should be writable\n";
    return false;
  }
  const auto source_path = QDir(workspace_path).filePath(QStringLiteral("compound-mcp-source.png"));
  QImage source(80, 48, QImage::Format_RGBA8888);
  source.fill(Qt::transparent);
  source.fill(QColor(235, 82, 35, 255));
  if (!source.save(source_path)) {
    std::cerr << "compound MCP source image should be writable\n";
    return false;
  }
  const auto canonical_source = QFileInfo(source_path).canonicalFilePath();
  const auto canonical_output_directory = QFileInfo(output_directory).canonicalFilePath();
  if (canonical_source.isEmpty() || canonical_output_directory.isEmpty()) {
    std::cerr << "compound MCP source and output directory should resolve canonically\n";
    return false;
  }

  photo_editor::desktop::TimelineDocumentController controller;
  controller.createTimelineWithSettings({
      {QStringLiteral("timelineName"), QStringLiteral("Compound MCP Command Test")},
      {QStringLiteral("width"), 128},
      {QStringLiteral("height"), 96},
      {QStringLiteral("frameRate"), 24.0},
  });
  controller.addClipFromGallery(QStringLiteral("photo:compound-mcp-a"), canonical_source,
                                QStringLiteral("Compound A"), 1.0, canonical_source);
  controller.addClipFromGallery(QStringLiteral("photo:compound-mcp-b"), canonical_source,
                                QStringLiteral("Compound B"), 1.0, canonical_source);
  QStringList clip_ids;
  for (const auto& value : controller.clips())
    clip_ids.append(value.toMap().value(QStringLiteral("clipId")).toString());
  if (clip_ids.size() != 2 || clip_ids.contains(QString{})) {
    std::cerr << "compound MCP fixture should have two stable Timeline clip IDs\n";
    return false;
  }
  const auto prepared_timeline = controller.prepareAgentTaskDocumentForAutomation();
  if (!prepared_timeline.value(QStringLiteral("ok")).toBool()) {
    std::cerr << "the Timeline should prepare for exact task admission: "
              << prepared_timeline.value(QStringLiteral("error")).toString().toStdString() << '\n';
    return false;
  }
  QElapsedTimer ready_timer;
  ready_timer.start();
  while (!controller.agentTaskDocumentReady() && ready_timer.elapsed() < 8000) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
    QThread::msleep(2);
  }
  if (!controller.agentTaskDocumentReady() ||
      !controller.acceptAgentTaskDocumentPreparationForAutomation()
           .value(QStringLiteral("ok")).toBool()) {
    std::cerr << "the native Timeline should become stable before task admission\n";
    return false;
  }

  constexpr auto task_id = "native-compound-command";
  const auto resource_id = controller.agentTaskDocumentId();
  photo_editor::desktop::AgentTaskCoordinator tasks;
  if (!tasks.begin(journal_root, QString::fromLatin1(task_id)).value(QStringLiteral("ok")).toBool() ||
      !tasks.journal()->recordApprovedSnapshot({
          {QStringLiteral("contract"), QStringLiteral("agent-approved-session-v1")},
          {QStringLiteral("prompt"), QStringLiteral("Create an editable grouped visual")}})
           .value(QStringLiteral("ok")).toBool() ||
      !tasks.journal()->recordNativeTimelineScope(
          {resource_id}, {canonical_source}, canonical_output_directory)
           .value(QStringLiteral("ok")).toBool()) {
    std::cerr << "compound MCP test should persist its explicit task scope first\n";
    return false;
  }
  const auto timeline_adapter = photo_editor::desktop::AgentTaskJournal::ResourceAdapter{
      [&controller, resource_id]() {
        return controller.agentTaskDocumentId() == resource_id
            ? controller.agentTaskDocumentState() : QByteArray{};
      },
      [&controller, resource_id](const QByteArray& bytes) {
        return controller.agentTaskDocumentId() == resource_id &&
            controller.restoreAgentTaskDocument(bytes);
      },
      {},
  };
  if (!tasks.enroll(resource_id, timeline_adapter).value(QStringLiteral("ok")).toBool()) {
    std::cerr << "the exact parent Timeline should enroll before task mutation\n";
    return false;
  }

  bool edit_authorized = false;
  photo_editor::desktop::AgentAppCommandRegistry registry;
  photo_editor::desktop::AgentTimelineCompoundCommands compound_commands(
      &controller, &tasks,
      [&tasks]() { return tasks.journal()->taskId(); },
      [&edit_authorized](const QString&) {
        return edit_authorized
            ? QVariantMap{{QStringLiteral("ok"), true}}
            : QVariantMap{{QStringLiteral("ok"), false},
                          {QStringLiteral("error"), QStringLiteral("Test authorization denied.")}};
      });
  tasks.setBeforeRevertHandler([&compound_commands](const bool check_only) {
    return check_only ? compound_commands.checkBeforeRevert()
                      : compound_commands.reconcileBeforeRevert();
  });
  if (!compound_commands.registerCommands(registry)) {
    std::cerr << "all typed compound MCP commands should register\n";
    return false;
  }
  const auto create_command = QStringLiteral("timeline.action.createTimelineCompound");
  const auto create_schema = registry.schema(create_command);
  if (!create_schema.value(QStringLiteral("ok")).toBool() ||
      !create_schema.value(QStringLiteral("available")).toBool() ||
      create_schema.value(QStringLiteral("completion")).toString() !=
          QStringLiteral("asynchronous-terminal-receipt")) {
    std::cerr << "compound MCP discovery should expose a task-owned asynchronous receipt contract\n";
    return false;
  }
  const auto request_id = QUuid::createUuid().toString(QUuid::WithoutBraces).toLower();
  const auto child_id = QUuid::createUuid().toString(QUuid::WithoutBraces).toLower();
  const QVariantMap request{
      {QStringLiteral("requestId"), request_id},
      {QStringLiteral("childTimelineId"), child_id},
      {QStringLiteral("label"), QStringLiteral("Editable alpha group")},
      {QStringLiteral("clipIds"), QVariantList{clip_ids.at(0), clip_ids.at(1)}}};
  const auto denied = registry.invoke(create_command, request);
  if (denied.value(QStringLiteral("ok")).toBool() || controller.clips().size() != 2 ||
      !tasks.state().value(QStringLiteral("steps")).toList().isEmpty()) {
    std::cerr << "task capability denial must happen before any child, parent, or journal mutation\n";
    return false;
  }
  edit_authorized = true;
  controller.setCachePressureLevelForTesting(QStringLiteral("critical"));
  const auto created = registry.invoke(create_command, request);
  const auto compound_clip_id = created.value(QStringLiteral("compoundClipId")).toString();
  const auto child_path = QDir(canonical_output_directory).filePath(
      QStringLiteral("compound-%1.json").arg(child_id));
  if (created.value(QStringLiteral("accepted")).toBool() ||
      !created.value(QStringLiteral("prepared")).toBool() ||
      !created.value(QStringLiteral("retryable")).toBool() ||
      QUuid(compound_clip_id).isNull() ||
      controller.clips().size() != 1 || !QFileInfo::exists(child_path) ||
      !tasks.journal()->nativeTimelineOutputPaths().contains(child_path)) {
    std::cerr << "a deferred compound MCP request should retain one editable child and prepared parent without claiming an active producer: "
              << QJsonDocument::fromVariant(created).toJson(QJsonDocument::Compact).toStdString() << '\n';
    return false;
  }
  const auto blocked_revert_preflight = tasks.revert(true);
  if (blocked_revert_preflight.value(QStringLiteral("ok")).toBool() ||
      !blocked_revert_preflight.value(QStringLiteral("compoundReconciliationRequired")).toBool() ||
      !QFileInfo::exists(child_path) || controller.clips().size() != 1) {
    std::cerr << "direct journal preflight must not bypass unresolved compound producer reconciliation\n";
    return false;
  }
  QFile child_file(child_path);
  if (!child_file.open(QIODevice::ReadOnly)) {
    std::cerr << "the task-owned child Timeline should be readable\n";
    return false;
  }
  const auto child_document = QJsonDocument::fromJson(child_file.readAll()).object();
  QStringList persisted_clip_ids;
  for (const auto& track_value : child_document.value(QStringLiteral("tracks")).toArray()) {
    for (const auto& clip_value : track_value.toObject().value(QStringLiteral("clips")).toArray())
      persisted_clip_ids.append(clip_value.toObject().value(QStringLiteral("clipId")).toString());
  }
  if (child_document.value(QStringLiteral("timelineId")).toString() != child_id ||
      persisted_clip_ids.size() != 2 || !persisted_clip_ids.contains(clip_ids.at(0)) ||
      !persisted_clip_ids.contains(clip_ids.at(1))) {
    std::cerr << "the grouped child JSON should preserve both original editable stable IDs\n";
    return false;
  }
  const auto step_count = tasks.state().value(QStringLiteral("steps")).toList().size();
  const auto duplicate = registry.invoke(create_command, request);
  auto changed_request = request;
  changed_request.insert(QStringLiteral("label"), QStringLiteral("Changed group body"));
  const auto changed_replay = registry.invoke(create_command, changed_request);
  if (!duplicate.value(QStringLiteral("ok")).toBool() ||
      !duplicate.value(QStringLiteral("idempotentReplay")).toBool() ||
      duplicate.value(QStringLiteral("requestId")).toString() != request_id ||
      changed_replay.value(QStringLiteral("ok")).toBool() ||
      tasks.state().value(QStringLiteral("steps")).toList().size() != step_count || controller.clips().size() != 1) {
    std::cerr << "identical request replay should not respawn, and changed content must reject UUID reuse\n";
    return false;
  }
  const auto inspected = registry.invoke(QStringLiteral("timeline.action.inspectTimelineCompound"),
      {{QStringLiteral("requestId"), request_id}});
  if (!inspected.value(QStringLiteral("ok")).toBool() ||
      inspected.value(QStringLiteral("status")).toString() != QStringLiteral("prepared") ||
      inspected.value(QStringLiteral("childTimelineId")).toString() != child_id ||
      inspected.value(QStringLiteral("childDocumentPath")).toString() != child_path ||
      inspected.value(QStringLiteral("processPresent")).toBool()) {
    std::cerr << "inspect should report exact prepared compound state without claiming a process: "
              << QJsonDocument::fromVariant(inspected).toJson(QJsonDocument::Compact).toStdString() << '\n';
    return false;
  }
  const auto cancelled = registry.invoke(QStringLiteral("timeline.action.cancelTimelineCompound"),
      {{QStringLiteral("requestId"), request_id}});
  if (!cancelled.value(QStringLiteral("ok")).toBool() ||
      !cancelled.value(QStringLiteral("processStopped")).toBool() ||
      !cancelled.value(QStringLiteral("requiresJournalReconciliation")).toBool() ||
      cancelled.value(QStringLiteral("outputsRemoved")).toBool() ||
      !QFileInfo::exists(child_path)) {
    std::cerr << "cancellation should prove the no-launch state while preserving journal-owned outputs\n";
    return false;
  }
  const auto reconciled = compound_commands.reconcileBeforeRevert();
  if (!reconciled.value(QStringLiteral("ok")).toBool() ||
      compound_commands.hasUnsettledCompounds()) {
    std::cerr << "a settled compound should leave no pending producer before task revert\n";
    return false;
  }
  const auto reverted = tasks.revert();
  const auto paths = QStringList{
      child_path,
      QDir(canonical_output_directory).filePath(QStringLiteral("compound-%1.mov").arg(child_id)),
      QDir(canonical_output_directory).filePath(QStringLiteral("compound-%1.partial.mov").arg(child_id)),
      QDir(canonical_output_directory).filePath(QStringLiteral("compound-%1.ffgraph").arg(child_id)),
      QDir(canonical_output_directory).filePath(QStringLiteral("compound-%1.producer.json").arg(child_id))};
  const auto outputs_gone = std::all_of(paths.cbegin(), paths.cend(),
      [](const QString& path) { return !QFileInfo::exists(path) && !QFileInfo(path).isSymLink(); });
  if (!reverted.value(QStringLiteral("ok")).toBool() || !outputs_gone ||
      controller.clips().size() != 2) {
    std::cerr << "task revert should restore the parent baseline and remove only the five verified absent-baseline outputs: "
              << QJsonDocument::fromVariant(reverted).toJson(QJsonDocument::Compact).toStdString() << '\n';
    return false;
  }
  controller.releaseAgentTaskDocumentForAutomation();
  return run_pending_compound_recovery_proof_workflow(workspace_path);
}

bool run_pending_compound_recovery_proof_workflow(const QString& workspace_path) {
  const auto fail_at = [](const char* stage) {
    std::cerr << "pending compound recovery proof failed at " << stage << '\n';
    return false;
  };
  const auto fail_with_result = [](const char* stage, const QVariantMap& result) {
    std::cerr << "pending compound recovery proof failed at " << stage << ": "
              << QJsonDocument::fromVariant(result).toJson(QJsonDocument::Compact).toStdString()
              << '\n';
    return false;
  };
  const auto ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
  const auto ffprobe = QStandardPaths::findExecutable(QStringLiteral("ffprobe"));
  if (ffmpeg.isEmpty() || ffprobe.isEmpty()) return fail_at("ffmpeg/ffprobe discovery");
  const auto output_directory = QDir(workspace_path).filePath(QStringLiteral("compound-recovery-outputs"));
  const auto journal_root = QDir(workspace_path).filePath(QStringLiteral("compound-recovery-journal"));
  if (!QDir().mkpath(output_directory) || !QDir().mkpath(journal_root))
    return fail_at("create fixture output/journal directories");
  const auto source_path = QFileInfo(QDir(workspace_path).filePath(QStringLiteral("compound-mcp-source.png")))
                               .canonicalFilePath();
  const auto canonical_output_directory = QFileInfo(output_directory).canonicalFilePath();
  if (source_path.isEmpty() || canonical_output_directory.isEmpty())
    return fail_at("resolve fixture source/output paths canonically");

  TimelineDocumentController controller;
  controller.createTimelineWithSettings({
      {QStringLiteral("timelineName"), QStringLiteral("Pending compound recovery proof")},
      {QStringLiteral("width"), 128},
      {QStringLiteral("height"), 96},
      {QStringLiteral("frameRate"), 24.0},
  });
  controller.addClipFromGallery(QStringLiteral("photo:compound-proof-a"), source_path,
                                QStringLiteral("Recovery proof A"), 1.0, source_path);
  controller.addClipFromGallery(QStringLiteral("photo:compound-proof-b"), source_path,
                                QStringLiteral("Recovery proof B"), 1.0, source_path);
  QStringList clip_ids;
  for (const auto& value : controller.clips())
    clip_ids.append(value.toMap().value(QStringLiteral("clipId")).toString());
  if (clip_ids.size() != 2 || clip_ids.contains(QString{}))
    return fail_at("two stable source clip IDs");
  const auto secondary_track_result = controller.createTimelineTrackForAutomation(
      QStringLiteral("video"), QStringLiteral("Secondary recovery lane"));
  const auto secondary_track_id =
      secondary_track_result.value(QStringLiteral("trackId")).toString();
  const auto secondary_track_row = secondary_track_result.value(QStringLiteral("track")).toMap();
  if (!secondary_track_result.value(QStringLiteral("ok")).toBool() ||
      secondary_track_id.isEmpty() ||
      secondary_track_row.value(QStringLiteral("trackRole")).toString() != QStringLiteral("media") ||
      secondary_track_row.value(QStringLiteral("laneIndex")).toInt() <= 0) {
    return fail_with_result("create a nonprimary media lane for legacy-mirror coverage", secondary_track_result);
  }
  controller.addClipFromGallery(QStringLiteral("photo:compound-proof-secondary-a"), source_path,
                                QStringLiteral("Secondary recovery proof A"), 1.0, source_path);
  controller.addClipFromGallery(QStringLiteral("photo:compound-proof-secondary-b"), source_path,
                                QStringLiteral("Secondary recovery proof B"), 1.0, source_path);
  QStringList secondary_clip_ids;
  for (const auto& track_value : controller.sequenceTracks()) {
    const auto track = track_value.toMap();
    if (track.value(QStringLiteral("trackId")).toString() != secondary_track_id) continue;
    for (const auto& clip_value : track.value(QStringLiteral("clips")).toList())
      secondary_clip_ids.append(clip_value.toMap().value(QStringLiteral("clipId")).toString());
    break;
  }
  if (secondary_clip_ids.size() != 2 || secondary_clip_ids.contains(QString{}))
    return fail_at("two stable clips on the nonprimary media lane");
  const auto preparation = controller.prepareAgentTaskDocumentForAutomation();
  if (!preparation.value(QStringLiteral("ok")).toBool())
    return fail_with_result("Timeline preparation", preparation);
  QElapsedTimer preparation_wait;
  preparation_wait.start();
  while (!controller.agentTaskDocumentReady() && preparation_wait.elapsed() < 8000) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    QThread::msleep(2);
  }
  if (!controller.agentTaskDocumentReady() ||
      !controller.acceptAgentTaskDocumentPreparationForAutomation().value(QStringLiteral("ok")).toBool()) {
    return fail_at("Timeline readiness/admission");
  }

  photo_editor::desktop::AgentTaskCoordinator tasks;
  const auto task_id = QUuid::createUuid().toString(QUuid::WithoutBraces).toLower();
  const auto resource_id = controller.agentTaskDocumentId();
  if (!tasks.begin(journal_root, task_id).value(QStringLiteral("ok")).toBool() ||
      !tasks.journal()->recordApprovedSnapshot({
          {QStringLiteral("contract"), QStringLiteral("agent-approved-session-v1")},
          {QStringLiteral("prompt"), QStringLiteral("Verify an accepted transparent compound recovery proof")}})
           .value(QStringLiteral("ok")).toBool() ||
      !tasks.journal()->recordNativeTimelineScope(
          {resource_id}, {source_path}, canonical_output_directory).value(QStringLiteral("ok")).toBool()) {
    return fail_with_result("task scope setup", tasks.state());
  }
  const auto adapter = photo_editor::desktop::AgentTaskJournal::ResourceAdapter{
      [&controller, resource_id]() {
        return controller.agentTaskDocumentId() == resource_id
            ? controller.agentTaskDocumentState() : QByteArray{};
      },
      [&controller, resource_id](const QByteArray& bytes) {
        return controller.agentTaskDocumentId() == resource_id &&
               controller.restoreAgentTaskDocument(bytes);
      },
      {},
  };
  const auto enrolled = tasks.enroll(resource_id, adapter);
  if (!enrolled.value(QStringLiteral("ok")).toBool())
    return fail_with_result("Timeline resource enrollment", enrolled);

  bool edit_authorized = true;
  photo_editor::desktop::AgentAppCommandRegistry registry;
  photo_editor::desktop::AgentTimelineCompoundCommands compound_commands(
      &controller, &tasks,
      [&tasks]() { return tasks.journal()->taskId(); },
      [&edit_authorized](const QString&) {
        return edit_authorized
            ? QVariantMap{{QStringLiteral("ok"), true}}
            : QVariantMap{{QStringLiteral("ok"), false}};
      });
  if (!compound_commands.registerCommands(registry))
    return fail_at("compound command registration");
  tasks.setBeforeRevertHandler([&compound_commands](const bool check_only) {
    return check_only ? compound_commands.checkBeforeRevert()
                      : compound_commands.reconcileBeforeRevert();
  });

  const auto request_id = QUuid::createUuid().toString(QUuid::WithoutBraces).toLower();
  const auto child_id = QUuid::createUuid().toString(QUuid::WithoutBraces).toLower();
  const QVariantMap request{
      {QStringLiteral("requestId"), request_id},
      {QStringLiteral("childTimelineId"), child_id},
      {QStringLiteral("label"), QStringLiteral("Recovery guarded group")},
      {QStringLiteral("clipIds"), QVariantList{clip_ids.at(0), clip_ids.at(1)}}};
  controller.setCachePressureLevelForTesting(QStringLiteral("normal"));
  auto create = registry.invoke(QStringLiteral("timeline.action.createTimelineCompound"), request);
  const auto start_deadline = QDateTime::currentMSecsSinceEpoch() + 12000;
  while (!create.value(QStringLiteral("accepted")).toBool() &&
         QDateTime::currentMSecsSinceEpoch() < start_deadline) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
    const auto priority = WorkerQueueRegistry::priorityDecision(
        QStringLiteral("compound-render"), QStringLiteral("timeline"),
        create.value(QStringLiteral("compoundClipId")).toString());
    if (create.value(QStringLiteral("prepared")).toBool() &&
        !priority.value(QStringLiteral("deferred")).toBool()) {
      create = registry.invoke(QStringLiteral("timeline.action.startTimelineCompound"),
          {{QStringLiteral("requestId"), request_id}});
    } else {
      QThread::msleep(10);
    }
  }
  const auto compound_clip_id = create.value(QStringLiteral("compoundClipId")).toString();
  if (!create.value(QStringLiteral("accepted")).toBool() || compound_clip_id.isEmpty()) {
    std::cerr << "the recovery proof fixture should start only after the native queue admits it: "
              << QJsonDocument::fromVariant(create).toJson(QJsonDocument::Compact).toStdString() << '\n';
    return false;
  }
  QVariantMap terminal_status;
  const auto render_deadline = QDateTime::currentMSecsSinceEpoch() + 15000;
  while (QDateTime::currentMSecsSinceEpoch() < render_deadline) {
    terminal_status = controller.compoundRenderStatusForAutomation(compound_clip_id);
    if (terminal_status.value(QStringLiteral("terminal")).toBool()) break;
    QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
    QThread::msleep(10);
  }
  if (!terminal_status.value(QStringLiteral("terminal")).toBool() ||
      terminal_status.value(QStringLiteral("status")).toString() != QStringLiteral("ready")) {
    std::cerr << "the recovery proof fixture compound should reach a terminal ready render: "
              << QJsonDocument::fromVariant(terminal_status).toJson(QJsonDocument::Compact).toStdString() << '\n';
    return false;
  }

  const auto preview = tasks.journal()->report();
  const auto state = controller.agentTaskDocumentState();
  auto primary_track_copy_count = 0;
  auto primary_legacy_copy_count = 0;
  QJsonParseError primary_state_error;
  const auto primary_state_document = QJsonDocument::fromJson(state, &primary_state_error);
  if (primary_state_error.error == QJsonParseError::NoError &&
      primary_state_document.isObject()) {
    const auto primary_root = primary_state_document.object();
    for (const auto& track_value : primary_root.value(QStringLiteral("tracks")).toArray()) {
      for (const auto& clip_value : track_value.toObject().value(QStringLiteral("clips")).toArray()) {
        if (clip_value.toObject().value(QStringLiteral("clipId")).toString() == compound_clip_id)
          ++primary_track_copy_count;
      }
    }
    for (const auto& clip_value : primary_root.value(QStringLiteral("clips")).toArray()) {
      if (clip_value.toObject().value(QStringLiteral("clipId")).toString() == compound_clip_id)
        ++primary_legacy_copy_count;
    }
  }
  if (primary_track_copy_count != 1 || primary_legacy_copy_count != 1)
    return fail_at("primary-lane compound should have one track copy and one matching legacy mirror");
  QFile journal_file(tasks.journal()->journalPath());
  if (!journal_file.open(QIODevice::ReadOnly))
    return fail_at("open journal before tamper probes");
  const auto journal_bytes_before_probes = journal_file.readAll();
  journal_file.close();
  const auto proof = photo_editor::desktop::AgentTimelineCompoundCommands::verifyPendingCompoundRecovery(
      preview, resource_id, state);
  if (!proof.value(QStringLiteral("ok")).toBool()) {
    std::cerr << "the accepted, completed-but-unsettled compound should pass exact recovery proof: "
              << QJsonDocument::fromVariant(proof).toJson(QJsonDocument::Compact).toStdString() << '\n';
    return false;
  }

  auto altered_request = preview;
  auto steps = altered_request.value(QStringLiteral("steps")).toList();
  auto altered_step = steps.last().toMap();
  auto arguments = altered_step.value(QStringLiteral("arguments")).toMap();
  arguments.insert(QStringLiteral("label"), QStringLiteral("Tampered label"));
  altered_step.insert(QStringLiteral("arguments"), arguments);
  steps.replace(steps.size() - 1, altered_step);
  altered_request.insert(QStringLiteral("steps"), steps);
  const auto request_tamper = photo_editor::desktop::AgentTimelineCompoundCommands::verifyPendingCompoundRecovery(
      altered_request, resource_id, state);

  auto altered_launch_spec = preview;
  auto launch_steps = altered_launch_spec.value(QStringLiteral("steps")).toList();
  auto launch_step = launch_steps.last().toMap();
  auto launch_arguments = launch_step.value(QStringLiteral("arguments")).toMap();
  auto launch_spec = launch_arguments.value(QStringLiteral("producerLaunchSpec")).toMap();
  auto launch_argv = launch_spec.value(QStringLiteral("argv")).toList();
  launch_argv.append(QStringLiteral("--tampered"));
  launch_spec.insert(QStringLiteral("argv"), launch_argv);
  launch_arguments.insert(QStringLiteral("producerLaunchSpec"), launch_spec);
  launch_step.insert(QStringLiteral("arguments"), launch_arguments);
  launch_steps.replace(launch_steps.size() - 1, launch_step);
  altered_launch_spec.insert(QStringLiteral("steps"), launch_steps);
  const auto launch_tamper = photo_editor::desktop::AgentTimelineCompoundCommands::verifyPendingCompoundRecovery(
      altered_launch_spec, resource_id, state);

  auto prepared_preview = preview;
  auto prepared_steps = prepared_preview.value(QStringLiteral("steps")).toList();
  auto prepared_step = prepared_steps.last().toMap();
  prepared_step.insert(QStringLiteral("status"), QStringLiteral("prepared"));
  prepared_step.remove(QStringLiteral("outcome"));
  prepared_steps.replace(prepared_steps.size() - 1, prepared_step);
  prepared_preview.insert(QStringLiteral("steps"), prepared_steps);
  prepared_preview.insert(QStringLiteral("uncertainOperation"), true);
  const auto uncertain_recovery = photo_editor::desktop::AgentTimelineCompoundCommands::verifyPendingCompoundRecovery(
      prepared_preview, resource_id, state);

  auto missing_receipt = preview;
  auto receipt_steps = missing_receipt.value(QStringLiteral("steps")).toList();
  auto receipt_step = receipt_steps.last().toMap();
  auto receipt_outcome = receipt_step.value(QStringLiteral("outcome")).toMap();
  receipt_outcome.remove(QStringLiteral("producerReceipt"));
  receipt_step.insert(QStringLiteral("outcome"), receipt_outcome);
  receipt_steps.replace(receipt_steps.size() - 1, receipt_step);
  missing_receipt.insert(QStringLiteral("steps"), receipt_steps);
  const auto receipt_tamper = photo_editor::desktop::AgentTimelineCompoundCommands::verifyPendingCompoundRecovery(
      missing_receipt, resource_id, state);

  auto altered_scope = preview;
  auto scope = altered_scope.value(QStringLiteral("nativeTimelineScope")).toMap();
  auto output_paths = scope.value(QStringLiteral("outputPaths")).toStringList();
  output_paths.removeAll(QDir(canonical_output_directory).filePath(
      QStringLiteral("compound-%1.producer.json").arg(child_id)));
  scope.insert(QStringLiteral("outputPaths"), output_paths);
  altered_scope.insert(QStringLiteral("nativeTimelineScope"), scope);
  const auto scope_tamper = photo_editor::desktop::AgentTimelineCompoundCommands::verifyPendingCompoundRecovery(
      altered_scope, resource_id, state);

  auto altered_resource = preview;
  auto resources = altered_resource.value(QStringLiteral("resources")).toList();
  const auto receipt_path = QDir(canonical_output_directory).filePath(
      QStringLiteral("compound-%1.producer.json").arg(child_id));
  const auto receipt_resource_id =
      photo_editor::desktop::AgentTaskCoordinator::taskOwnedTimelineOutputFileResource(receipt_path).resource_id;
  auto receipt_resource_found = false;
  for (qsizetype index = 0; index < resources.size(); ++index) {
    auto resource = resources.at(index).toMap();
    if (resource.value(QStringLiteral("id")).toString() != receipt_resource_id) continue;
    resource.insert(QStringLiteral("currentHash"), QString(64, QLatin1Char('0')));
    resources.replace(index, resource);
    receipt_resource_found = true;
    break;
  }
  altered_resource.insert(QStringLiteral("resources"), resources);
  const auto resource_tamper = photo_editor::desktop::AgentTimelineCompoundCommands::verifyPendingCompoundRecovery(
      altered_resource, resource_id, state);

  auto altered_state_document = QJsonDocument::fromJson(state).object();
  altered_state_document.insert(QStringLiteral("timelineName"), QStringLiteral("Unrelated edit"));
  const auto document_tamper = photo_editor::desktop::AgentTimelineCompoundCommands::verifyPendingCompoundRecovery(
      preview, resource_id, QJsonDocument(altered_state_document).toJson(QJsonDocument::Compact));
  if (!journal_file.open(QIODevice::ReadOnly))
    return fail_at("reopen journal after tamper probes");
  const auto journal_bytes_after_probes = journal_file.readAll();
  journal_file.close();
  if (request_tamper.value(QStringLiteral("ok")).toBool() ||
      launch_tamper.value(QStringLiteral("ok")).toBool() ||
      uncertain_recovery.value(QStringLiteral("ok")).toBool() ||
      receipt_tamper.value(QStringLiteral("ok")).toBool() ||
      scope_tamper.value(QStringLiteral("ok")).toBool() ||
      resource_tamper.value(QStringLiteral("ok")).toBool() || !receipt_resource_found ||
      document_tamper.value(QStringLiteral("ok")).toBool() ||
      journal_bytes_before_probes != journal_bytes_after_probes ||
      !tasks.journal()->pending(resource_id) ||
      !tasks.journal()->report().value(QStringLiteral("steps")).toList().last().toMap()
           .value(QStringLiteral("outcome")).toMap().value(QStringLiteral("accepted")).toBool()) {
    std::cerr << "recovery proof must reject changed request/scope/document and preserve the pending accepted journal step: "
              << "request=" << QJsonDocument::fromVariant(request_tamper).toJson(QJsonDocument::Compact).toStdString()
              << " launch=" << QJsonDocument::fromVariant(launch_tamper).toJson(QJsonDocument::Compact).toStdString()
              << " uncertain=" << QJsonDocument::fromVariant(uncertain_recovery).toJson(QJsonDocument::Compact).toStdString()
              << " receipt=" << QJsonDocument::fromVariant(receipt_tamper).toJson(QJsonDocument::Compact).toStdString()
              << " scope=" << QJsonDocument::fromVariant(scope_tamper).toJson(QJsonDocument::Compact).toStdString()
              << " resource=" << QJsonDocument::fromVariant(resource_tamper).toJson(QJsonDocument::Compact).toStdString()
              << " document=" << QJsonDocument::fromVariant(document_tamper).toJson(QJsonDocument::Compact).toStdString()
              << '\n';
    return false;
  }

  const auto settle = registry.invoke(QStringLiteral("timeline.action.settleTimelineCompound"),
      {{QStringLiteral("requestId"), request_id}});
  if (!settle.value(QStringLiteral("ok")).toBool() ||
      tasks.journal()->pending(resource_id) || compound_commands.hasUnsettledCompounds()) {
    std::cerr << "recovery proof fixture should settle normally after read-only tamper probes: "
              << QJsonDocument::fromVariant(settle).toJson(QJsonDocument::Compact).toStdString() << '\n';
    return false;
  }

  const auto secondary_request_id = QUuid::createUuid().toString(QUuid::WithoutBraces).toLower();
  const auto secondary_child_id = QUuid::createUuid().toString(QUuid::WithoutBraces).toLower();
  const QVariantMap secondary_request{
      {QStringLiteral("requestId"), secondary_request_id},
      {QStringLiteral("childTimelineId"), secondary_child_id},
      {QStringLiteral("label"), QStringLiteral("Secondary lane recovery group")},
      {QStringLiteral("clipIds"),
       QVariantList{secondary_clip_ids.at(0), secondary_clip_ids.at(1)}}};
  auto secondary_create = registry.invoke(
      QStringLiteral("timeline.action.createTimelineCompound"), secondary_request);
  const auto secondary_start_deadline = QDateTime::currentMSecsSinceEpoch() + 12000;
  while (!secondary_create.value(QStringLiteral("accepted")).toBool() &&
         QDateTime::currentMSecsSinceEpoch() < secondary_start_deadline) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
    const auto priority = WorkerQueueRegistry::priorityDecision(
        QStringLiteral("compound-render"), QStringLiteral("timeline"),
        secondary_create.value(QStringLiteral("compoundClipId")).toString());
    if (secondary_create.value(QStringLiteral("prepared")).toBool() &&
        !priority.value(QStringLiteral("deferred")).toBool()) {
      secondary_create = registry.invoke(
          QStringLiteral("timeline.action.startTimelineCompound"),
          {{QStringLiteral("requestId"), secondary_request_id}});
    } else {
      QThread::msleep(10);
    }
  }
  const auto secondary_compound_id =
      secondary_create.value(QStringLiteral("compoundClipId")).toString();
  if (!secondary_create.value(QStringLiteral("accepted")).toBool() ||
      secondary_compound_id.isEmpty()) {
    return fail_with_result("create a pending compound on the secondary media lane", secondary_create);
  }
  QVariantMap secondary_terminal_status;
  const auto secondary_render_deadline = QDateTime::currentMSecsSinceEpoch() + 15000;
  while (QDateTime::currentMSecsSinceEpoch() < secondary_render_deadline) {
    secondary_terminal_status =
        controller.compoundRenderStatusForAutomation(secondary_compound_id);
    if (secondary_terminal_status.value(QStringLiteral("terminal")).toBool()) break;
    QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
    QThread::msleep(10);
  }
  if (!secondary_terminal_status.value(QStringLiteral("terminal")).toBool() ||
      secondary_terminal_status.value(QStringLiteral("status")).toString() != QStringLiteral("ready")) {
    return fail_with_result("finish the secondary lane compound before its recovery proof", secondary_terminal_status);
  }
  const auto secondary_preview = tasks.journal()->report();
  const auto secondary_state = controller.agentTaskDocumentState();
  const auto secondary_proof =
      photo_editor::desktop::AgentTimelineCompoundCommands::verifyPendingCompoundRecovery(
          secondary_preview, resource_id, secondary_state);
  auto secondary_track_copy_count = 0;
  auto secondary_legacy_copy_count = 0;
  QJsonParseError secondary_state_error;
  const auto secondary_state_document =
      QJsonDocument::fromJson(secondary_state, &secondary_state_error);
  if (secondary_state_error.error == QJsonParseError::NoError &&
      secondary_state_document.isObject()) {
    const auto secondary_root = secondary_state_document.object();
    for (const auto& track_value : secondary_root.value(QStringLiteral("tracks")).toArray()) {
      for (const auto& clip_value : track_value.toObject().value(QStringLiteral("clips")).toArray()) {
        if (clip_value.toObject().value(QStringLiteral("clipId")).toString() ==
            secondary_compound_id) {
          ++secondary_track_copy_count;
        }
      }
    }
    for (const auto& clip_value : secondary_root.value(QStringLiteral("clips")).toArray()) {
      if (clip_value.toObject().value(QStringLiteral("clipId")).toString() ==
          secondary_compound_id) {
        ++secondary_legacy_copy_count;
      }
    }
  }
  if (!secondary_proof.value(QStringLiteral("ok")).toBool() ||
      secondary_track_copy_count != 1 || secondary_legacy_copy_count != 0) {
    std::cerr << "a secondary-lane compound should have one track copy, no legacy mirror, and pass exact recovery proof: "
              << QJsonDocument::fromVariant(secondary_proof).toJson(QJsonDocument::Compact).toStdString()
              << " track_copies=" << secondary_track_copy_count
              << " legacy_copies=" << secondary_legacy_copy_count << '\n';
    return false;
  }
  const auto secondary_settle = registry.invoke(
      QStringLiteral("timeline.action.settleTimelineCompound"),
      {{QStringLiteral("requestId"), secondary_request_id}});
  if (!secondary_settle.value(QStringLiteral("ok")).toBool() ||
      tasks.journal()->pending(resource_id) || compound_commands.hasUnsettledCompounds()) {
    return fail_with_result("settle the secondary-lane compound", secondary_settle);
  }

  const auto reverted = tasks.revert();
  controller.releaseAgentTaskDocumentForAutomation();
  if (!reverted.value(QStringLiteral("ok")).toBool()) {
    std::cerr << "recovery proof fixture should clean up through the task journal only: "
              << QJsonDocument::fromVariant(reverted).toJson(QJsonDocument::Compact).toStdString() << '\n';
    return false;
  }
  return true;
}
