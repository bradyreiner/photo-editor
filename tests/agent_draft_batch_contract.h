#pragma once

#include "agent_timeline_commands.h"

#include <QFileInfo>
#include <QTemporaryDir>
#include <cstdlib>
#include <iostream>

inline int timeline_agent_batch_test_failure(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

int run_timeline_agent_draft_batch_contract() {
  QTemporaryDir root;
  if (!root.isValid()) return timeline_agent_batch_test_failure("batch contract should get isolated task storage");

  photo_editor::desktop::TimelineDocumentController timeline;
  timeline.setEditAccessPolicy([] { return true; });
  timeline.createTimeline(QStringLiteral("Batch Contract"));
  const auto resource_id = timeline.agentTaskDocumentId();
  const auto baseline = timeline.agentTaskDocumentState();
  if (resource_id.isEmpty() || baseline.isEmpty() || !timeline.agentTaskDocumentReady())
    return timeline_agent_batch_test_failure("batch contract should start from a stable idle Timeline");

  photo_editor::desktop::AgentTaskCoordinator tasks;
  const QString task_id = QStringLiteral("draft-batch-contract");
  if (!tasks.begin(root.path(), task_id).value(QStringLiteral("ok")).toBool() ||
      !tasks.journal()->recordApprovedSnapshot({
          {QStringLiteral("contract"), QStringLiteral("agent-approved-session-v1")},
          {QStringLiteral("prompt"), QStringLiteral("Create an editable native title")},
          {QStringLiteral("answers"), QVariantMap{}}}).value(QStringLiteral("ok")).toBool() ||
      !tasks.journal()->recordNativeTimelineScope(
          {resource_id}, {}, QFileInfo(root.path()).canonicalFilePath())
          .value(QStringLiteral("ok")).toBool()) {
    return timeline_agent_batch_test_failure("batch contract should persist an approved single-Timeline task scope");
  }

  bool deny_title = false;
  bool target_allowed = true;
  const auto authorize = [&](const QString& name) -> QVariantMap {
    if (!target_allowed)
      return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("Timeline target is outside the task scope.")}};
    if (deny_title && name.contains(QStringLiteral("Title")))
      return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("The child title action is not authorized.")}};
    return {{QStringLiteral("ok"), true}};
  };
  photo_editor::desktop::AgentAppCommandRegistry production_registry;
  photo_editor::desktop::register_agent_timeline_commands(
      production_registry, &timeline, tasks, [&task_id]() { return task_id; }, authorize);
  if (production_registry.schema(QStringLiteral("timeline.action.applyDraftBatch"))
          .value(QStringLiteral("available")).toBool()) {
    return timeline_agent_batch_test_failure(
        "production Timeline registration must keep the unverified batch action absent");
  }

  photo_editor::desktop::AgentAppCommandRegistry registry;
  photo_editor::desktop::register_agent_timeline_commands(
      registry, &timeline, tasks, [&task_id]() { return task_id; }, authorize, {}, true);

  const auto batch_command_id = QStringLiteral("timeline.action.applyDraftBatch");
  const auto batch_schema = registry.schema(batch_command_id);
  if (!batch_schema.value(QStringLiteral("available")).toBool() ||
      batch_schema.value(QStringLiteral("inputSchema")).toMap()
          .value(QStringLiteral("properties")).toMap()
          .value(QStringLiteral("operations")).toMap()
          .value(QStringLiteral("maxItems")).toInt() != 16) {
    return timeline_agent_batch_test_failure("batch command discovery should expose the strict bounded input schema");
  }

  const QVariantMap batch_values{
      {QStringLiteral("batchId"), QStringLiteral("33333333-3333-4333-8333-333333333333")},
      {QStringLiteral("operations"), QVariantList{
          QVariantMap{{QStringLiteral("id"), QStringLiteral("lane")},
              {QStringLiteral("commandId"), QStringLiteral("timeline.action.createTimelineTrack")},
              {QStringLiteral("values"), QVariantMap{{QStringLiteral("kind"), QStringLiteral("video")},
                  {QStringLiteral("name"), QStringLiteral("Batch Lane")}}}},
          QVariantMap{{QStringLiteral("id"), QStringLiteral("title")},
              {QStringLiteral("commandId"), QStringLiteral("timeline.action.createTimelineTitle")},
              {QStringLiteral("values"), QVariantMap{
                  {QStringLiteral("trackId"), QVariantMap{{QStringLiteral("result"), QStringLiteral("lane")},
                      {QStringLiteral("field"), QStringLiteral("trackId")}}},
                  {QStringLiteral("templateKey"), QStringLiteral("headline")},
                  {QStringLiteral("startSeconds"), 3.0}, {QStringLiteral("durationSeconds"), 2.0},
                  {QStringLiteral("values"), QVariantMap{{QStringLiteral("text"), QStringLiteral("Batch draft")}}}}}},
          QVariantMap{{QStringLiteral("id"), QStringLiteral("retitle")},
              {QStringLiteral("commandId"), QStringLiteral("timeline.action.setTimelineClipTitle")},
              {QStringLiteral("values"), QVariantMap{
                  {QStringLiteral("clipId"), QVariantMap{{QStringLiteral("result"), QStringLiteral("title")},
                      {QStringLiteral("field"), QStringLiteral("createdClipId")}}},
                  {QStringLiteral("values"), QVariantMap{{QStringLiteral("text"), QStringLiteral("Batch final")}}}}}}}}};
  const auto batch = registry.invoke(batch_command_id, batch_values);
  const auto steps_after_batch = tasks.state().value(QStringLiteral("steps")).toList();
  const auto batch_results = batch.value(QStringLiteral("batchResults")).toList();
  if (!batch.value(QStringLiteral("ok")).toBool() ||
      batch.value(QStringLiteral("operationCount")).toInt() != 3 || batch_results.size() != 3 ||
      steps_after_batch.size() != 1 || !batch.value(QStringLiteral("changed")).toBool()) {
    return timeline_agent_batch_test_failure("three typed actions should complete under one task journal step and controller transaction");
  }
  const auto track_id = batch_results.at(0).toMap().value(QStringLiteral("trackId")).toString();
  const auto clip_id = batch_results.at(1).toMap().value(QStringLiteral("createdClipId")).toString();
  if (track_id.isEmpty() || clip_id.isEmpty() ||
      !batch_results.at(2).toMap().value(QStringLiteral("ok")).toBool() ||
      batch_results.at(1).toMap().contains(QStringLiteral("clip")) ||
      timeline.inspectTimelineClipById(clip_id).value(QStringLiteral("clip")).toMap()
          .value(QStringLiteral("clipId")).toString() != clip_id) {
    return timeline_agent_batch_test_failure("batch should return compact stable IDs and resolve only earlier trackId/createdClipId references");
  }

  const auto replay = registry.invoke(batch_command_id, batch_values);
  if (!replay.value(QStringLiteral("ok")).toBool() ||
      !replay.value(QStringLiteral("idempotentReplay")).toBool() ||
      timeline.clips().size() != 1 || tasks.state().value(QStringLiteral("steps")).toList().size() != 1) {
    return timeline_agent_batch_test_failure("same-ID same-body retry should return the persisted IDs without repeating the mutation");
  }
  auto changed_batch = batch_values;
  auto changed_operations = changed_batch.value(QStringLiteral("operations")).toList();
  auto changed_last = changed_operations.at(2).toMap();
  auto changed_last_values = changed_last.value(QStringLiteral("values")).toMap();
  changed_last_values.insert(QStringLiteral("values"),
      QVariantMap{{QStringLiteral("text"), QStringLiteral("Different request body")}});
  changed_last.insert(QStringLiteral("values"), changed_last_values);
  changed_operations[2] = changed_last;
  changed_batch.insert(QStringLiteral("operations"), changed_operations);
  if (registry.invoke(batch_command_id, changed_batch).value(QStringLiteral("ok")).toBool() ||
      timeline.clips().size() != 1) {
    return timeline_agent_batch_test_failure("same batch ID with a different action body must be rejected without mutation");
  }

  const auto before_rollback = timeline.agentTaskDocumentState();
  const auto track_count_before_rollback = timeline.trackCount();
  deny_title = true;
  const QVariantMap failing_batch{
      {QStringLiteral("batchId"), QStringLiteral("44444444-4444-4444-8444-444444444444")},
      {QStringLiteral("operations"), QVariantList{
          QVariantMap{{QStringLiteral("id"), QStringLiteral("rollbackLane")},
              {QStringLiteral("commandId"), QStringLiteral("timeline.action.createTimelineTrack")},
              {QStringLiteral("values"), QVariantMap{{QStringLiteral("kind"), QStringLiteral("video")}}}},
          QVariantMap{{QStringLiteral("id"), QStringLiteral("deniedTitle")},
              {QStringLiteral("commandId"), QStringLiteral("timeline.action.createTimelineTitle")},
              {QStringLiteral("values"), QVariantMap{
                  {QStringLiteral("trackId"), QVariantMap{{QStringLiteral("result"), QStringLiteral("rollbackLane")},
                      {QStringLiteral("field"), QStringLiteral("trackId")}}},
                  {QStringLiteral("templateKey"), QStringLiteral("headline")},
                  {QStringLiteral("startSeconds"), 8.0}, {QStringLiteral("durationSeconds"), 2.0},
                  {QStringLiteral("values"), QVariantMap{{QStringLiteral("text"), QStringLiteral("Denied")}}}}}}}}};
  const auto rolled_back = registry.invoke(batch_command_id, failing_batch);
  deny_title = false;
  if (rolled_back.value(QStringLiteral("ok")).toBool() ||
      !rolled_back.value(QStringLiteral("rolledBack")).toBool() ||
      timeline.trackCount() != track_count_before_rollback ||
      timeline.agentTaskDocumentState() != before_rollback) {
    return timeline_agent_batch_test_failure("a denied child after a successful child should restore exact Timeline state");
  }

  const auto before_preflight = timeline.agentTaskDocumentState();
  const QVariantMap future_reference{
      {QStringLiteral("batchId"), QStringLiteral("55555555-5555-4555-8555-555555555555")},
      {QStringLiteral("operations"), QVariantList{
          QVariantMap{{QStringLiteral("id"), QStringLiteral("early")},
              {QStringLiteral("commandId"), QStringLiteral("timeline.action.setTimelineClipTitle")},
              {QStringLiteral("values"), QVariantMap{
                  {QStringLiteral("clipId"), QVariantMap{{QStringLiteral("result"), QStringLiteral("later")},
                      {QStringLiteral("field"), QStringLiteral("createdClipId")}}},
                  {QStringLiteral("values"), QVariantMap{{QStringLiteral("text"), QStringLiteral("Future")}}}}}},
          QVariantMap{{QStringLiteral("id"), QStringLiteral("later")},
              {QStringLiteral("commandId"), QStringLiteral("timeline.action.createTimelineGraphic")},
              {QStringLiteral("values"), QVariantMap{{QStringLiteral("trackId"), track_id},
                  {QStringLiteral("shapeType"), QStringLiteral("rectangle")},
                  {QStringLiteral("startSeconds"), 12.0}, {QStringLiteral("durationSeconds"), 1.0},
                  {QStringLiteral("values"), QVariantMap{}}}}}}}};
  if (registry.invoke(batch_command_id, future_reference).value(QStringLiteral("ok")).toBool() ||
      timeline.agentTaskDocumentState() != before_preflight) {
    return timeline_agent_batch_test_failure("forward references must be rejected before any Timeline action runs");
  }
  const QVariantMap invalid_field_reference{
      {QStringLiteral("batchId"), QStringLiteral("77777777-7777-4777-8777-777777777777")},
      {QStringLiteral("operations"), QVariantList{
          QVariantMap{{QStringLiteral("id"), QStringLiteral("sourceTitle")},
              {QStringLiteral("commandId"), QStringLiteral("timeline.action.createTimelineTitle")},
              {QStringLiteral("values"), QVariantMap{
                  {QStringLiteral("trackId"), track_id},
                  {QStringLiteral("templateKey"), QStringLiteral("headline")},
                  {QStringLiteral("startSeconds"), 14.0}, {QStringLiteral("durationSeconds"), 1.0},
                  {QStringLiteral("values"), QVariantMap{{QStringLiteral("text"), QStringLiteral("Reference source")}}}}}},
          QVariantMap{{QStringLiteral("id"), QStringLiteral("badField")},
              {QStringLiteral("commandId"), QStringLiteral("timeline.action.setTimelineClipTitle")},
              {QStringLiteral("values"), QVariantMap{
                  {QStringLiteral("clipId"), QVariantMap{{QStringLiteral("result"), QStringLiteral("sourceTitle")},
                      {QStringLiteral("field"), QStringLiteral("clip")}}},
                  {QStringLiteral("values"), QVariantMap{{QStringLiteral("text"), QStringLiteral("No detail ref")}}}}}}}}};
  if (registry.invoke(batch_command_id, invalid_field_reference).value(QStringLiteral("ok")).toBool() ||
      timeline.agentTaskDocumentState() != before_preflight) {
    return timeline_agent_batch_test_failure("references to clip/detail/path fields must be rejected before applying child actions");
  }

  const auto step_count_before_forbidden = tasks.state().value(QStringLiteral("steps")).toList().size();
  const QVariantMap forbidden_import{
      {QStringLiteral("batchId"), QStringLiteral("66666666-6666-4666-8666-666666666666")},
      {QStringLiteral("operations"), QVariantList{QVariantMap{
          {QStringLiteral("id"), QStringLiteral("import")},
          {QStringLiteral("commandId"), QStringLiteral("timeline.action.createTimelineClip")},
          {QStringLiteral("values"), QVariantMap{}}}}}};
  if (registry.invoke(batch_command_id, forbidden_import).value(QStringLiteral("ok")).toBool() ||
      tasks.state().value(QStringLiteral("steps")).toList().size() != step_count_before_forbidden) {
    return timeline_agent_batch_test_failure("the strict batch schema must reject media imports before task mutation admission");
  }

  target_allowed = false;
  const auto target_denied = registry.invoke(batch_command_id, batch_values);
  target_allowed = true;
  if (target_denied.value(QStringLiteral("ok")).toBool() ||
      timeline.agentTaskDocumentState() != before_preflight) {
    return timeline_agent_batch_test_failure("the batch outer action must retain the native target authorization gate");
  }

  if (!tasks.revert().value(QStringLiteral("ok")).toBool() ||
      timeline.agentTaskDocumentState() != baseline) {
    return timeline_agent_batch_test_failure("the combined draft batch should remain task-reversible to its exact pre-task bytes");
  }
  std::cout << "timeline_agent_draft_batch_contract=passed\n";
  return EXIT_SUCCESS;
}
