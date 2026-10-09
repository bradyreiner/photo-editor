#include "agent_tool_progress.h"
#include "agent_tool_call_recovery.h"

#include <iostream>

using photo_editor::desktop::AgentToolProgress;

#define CHECK(condition, message) do { if (!(condition)) { std::cerr << message << '\n'; return 1; } } while (false)

QVariantList receipt(const QString& hash, int telemetry = 0) {
  return {QVariantMap{{"toolName", "photo.edit_composition"}, {"ok", true},
      {"arguments", QVariantMap{{"operation", "paint"}, {"layerId", "pixels"}}},
      {"structuredContent", QVariantMap{{"taskStateHash", hash}, {"changed", true},
          {"layers", QVariantList{QVariantMap{{"id", "pixels"},
              {"thumbnailDiagnostics", telemetry}, {"selected", telemetry % 2 == 0}}}}}}}};
}

int main() {
  AgentToolProgress productive;
  for (int i = 0; i < 96; ++i)
    CHECK(productive.record(receipt(QString::number(i))), "Productive pixel edits have no total-step limit");

  AgentToolProgress stuck;
  CHECK(stuck.record(receipt("same", 0)), "First observation is new evidence");
  CHECK(stuck.record(receipt("same", 1)), "Allow a repeat while the model reconciles results");
  CHECK(stuck.record(receipt("same", 2)), "Telemetry is not document progress");
  CHECK(!stuck.record(receipt("same", 3)), "Repeated state must not loop indefinitely");

  AgentToolProgress cycle;
  CHECK(cycle.record(receipt("A")) && cycle.record(receipt("B")), "Read two distinct states");
  CHECK(cycle.record(receipt("A")) && cycle.record(receipt("B")), "Detect cycling rather than consecutive identity only");
  CHECK(!cycle.record(receipt("A")), "An observation cycle is unproductive");

  AgentToolProgress resumes;
  CHECK(resumes.record(receipt("A")) && resumes.record(receipt("A")), "Read and reconcile");
  CHECK(resumes.record(receipt("B")) && resumes.repeatedRounds() == 0, "New state resets repeated-round count");

  AgentToolProgress beforeContinuation;
  CHECK(beforeContinuation.record(receipt("completed batch")), "Completed tool batch is retained");
  CHECK(beforeContinuation.record(receipt("completed batch")), "A repeated batch advances its retained count");
  AgentToolProgress afterContinuation;
  CHECK(afterContinuation.restore(beforeContinuation.snapshot()), "Async continuation restores bounded progress state");
  CHECK(afterContinuation.repeatedRounds() == 1, "Restored state preserves repeated-round count");
  CHECK(afterContinuation.record(receipt("completed batch")), "Continuation detects the same completed result");
  CHECK(!afterContinuation.record(receipt("completed batch")), "Continuation cannot reset the repeated-result guard");

  auto malformed = beforeContinuation.snapshot();
  malformed.insert(QStringLiteral("signatures"), QStringList{QStringLiteral("not-a-sha256")});
  CHECK(!afterContinuation.restore(malformed), "Malformed progress snapshots are rejected without resetting the guard");
  CHECK(afterContinuation.repeatedRounds() == 3, "Rejected restore leaves accumulated progress intact");

  photo_editor::desktop::AgentLocalModelPlanResult rejected_plan;
  rejected_plan.error_message = QStringLiteral("photo.edit_composition call 0: arguments.values.x is not allowed for typography.");
  const QVariantMap original_call{
      {QStringLiteral("name"), QStringLiteral("photo.edit_composition")},
      {QStringLiteral("arguments"), QVariantMap{
          {QStringLiteral("assetId"), QStringLiteral("asset-1")},
          {QStringLiteral("operation"), QStringLiteral("typography")},
          {QStringLiteral("layerId"), QStringLiteral("created-title-7")},
          {QStringLiteral("values"), QVariantMap{{QStringLiteral("size"), 96},
              {QStringLiteral("x"), 300}, {QStringLiteral("y"), 160}}},
      }},
  };
  const QVariantMap input_schema{
      {QStringLiteral("type"), QStringLiteral("object")},
      {QStringLiteral("properties"), QVariantMap{{QStringLiteral("size"), QVariantMap{
          {QStringLiteral("type"), QStringLiteral("number")}}}}},
      {QStringLiteral("additionalProperties"), false},
  };
  const QVariantMap rejected_call{
      {QStringLiteral("index"), 0},
      {QStringLiteral("toolName"), QStringLiteral("photo.edit_composition")},
      {QStringLiteral("originalCall"), original_call},
      {QStringLiteral("arguments"), original_call.value(QStringLiteral("arguments"))},
      {QStringLiteral("inputSchema"), input_schema},
      {QStringLiteral("errors"), QStringList{QStringLiteral("arguments.values.x is not allowed for typography.")}},
  };
  const QVariantMap validation{
      {QStringLiteral("valid"), false},
      {QStringLiteral("rejectedCalls"), QVariantList{rejected_call}},
      {QStringLiteral("unexecutedCalls"), QVariantList{original_call}},
  };
  rejected_plan.prompt_audit.insert(QStringLiteral("toolCallValidation"), validation);
  const QVariantMap current_schema{
      {QStringLiteral("name"), QStringLiteral("photo.edit_composition")},
      {QStringLiteral("inputSchema"), input_schema},
  };
  const auto recovery = photo_editor::desktop::agent_tool_call_validation_recovery_envelope(
      rejected_plan, QVariantList{current_schema});
  CHECK(recovery.value(QStringLiteral("kind")).toString() == QStringLiteral("tool-call-validation-recovery") &&
      recovery.value(QStringLiteral("contract")).toString() == QStringLiteral("agent-tool-call-validation-recovery-v1") &&
      recovery.value(QStringLiteral("toolCallValidation")).toMap() == validation &&
      recovery.value(QStringLiteral("currentToolSchemas")).toList() == QVariantList{current_schema} &&
      recovery.value(QStringLiteral("wholeGeneratedPlanWithheld")).toBool() &&
      recovery.value(QStringLiteral("noCallsInRejectedPlanExecuted")).toBool() &&
      recovery.value(QStringLiteral("unexecutedCalls")).toList() == QVariantList{original_call} &&
      recovery.value(QStringLiteral("acceptedMutationsRemainApplied")).toBool() &&
      recovery.value(QStringLiteral("requiresCorrectedToolCallPlan")).toBool() &&
      !recovery.contains(QStringLiteral("toolCalls")),
      "Validation recovery must carry the exact rejected call, error schema and accepted-state safeguards");
  const auto recovered_call = recovery.value(QStringLiteral("toolCallValidation")).toMap()
                                  .value(QStringLiteral("rejectedCalls")).toList().first().toMap();
  CHECK(recovered_call.value(QStringLiteral("originalCall")).toMap() == original_call &&
      recovered_call.value(QStringLiteral("arguments")).toMap() == original_call.value(QStringLiteral("arguments")).toMap() &&
      recovered_call.value(QStringLiteral("inputSchema")).toMap() == input_schema &&
      recovery.value(QStringLiteral("plannerError")).toString() == rejected_plan.error_message &&
      recovery.value(QStringLiteral("instruction")).toString().contains(
          QStringLiteral("including calls before an invalid call")) &&
      recovery.value(QStringLiteral("toolCallValidation")).toMap()
          .value(QStringLiteral("rejectedCalls")).toList().size() == 1,
      "Recovery must preserve model-authored operation and IDs exactly without adding executable calls");
  const auto rejection_fingerprint = photo_editor::desktop::agent_tool_call_validation_recovery_fingerprint(rejected_plan);
  CHECK(!rejection_fingerprint.isEmpty() &&
      rejection_fingerprint == photo_editor::desktop::agent_tool_call_validation_recovery_fingerprint(rejected_plan),
      "The same invalid model plan must have a stable identity for the repeated-plan guard");
  auto different_withheld_plan = rejected_plan;
  different_withheld_plan.prompt_audit.insert(QStringLiteral("toolCallValidation"),
      QVariantMap{{QStringLiteral("valid"), false},
          {QStringLiteral("rejectedCalls"), QVariantList{rejected_call}},
          {QStringLiteral("unexecutedCalls"), QVariantList{QVariantMap{
              {QStringLiteral("name"), QStringLiteral("photo.edit_composition")},
              {QStringLiteral("arguments"), QVariantMap{{QStringLiteral("operation"), QStringLiteral("add_text")}}},
          }}}});
  CHECK(rejection_fingerprint !=
          photo_editor::desktop::agent_tool_call_validation_recovery_fingerprint(different_withheld_plan),
      "The repeated-plan guard must distinguish a changed valid prefix in the withheld generated plan");
  auto corrected_plan = rejected_plan;
  corrected_plan.prompt_audit.insert(QStringLiteral("toolCallValidation"),
      QVariantMap{{QStringLiteral("valid"), true}, {QStringLiteral("rejectedCalls"), QVariantList{}}});
  CHECK(photo_editor::desktop::agent_tool_call_validation_recovery_envelope(corrected_plan).isEmpty() &&
      photo_editor::desktop::agent_tool_call_validation_recovery_fingerprint(corrected_plan).isEmpty(),
      "A valid corrected plan must not receive another recovery envelope");
  std::cout << "Productive long tasks continue; repeated identical state and cycles stop.\n";
  return 0;
}
