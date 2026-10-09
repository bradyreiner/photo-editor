#pragma once

#include <QCryptographicHash>
#include <QJsonDocument>
#include <QMetaType>
#include <QVariantList>
#include <QVariantMap>

#include "agent_local_model.h"

namespace photo_editor::desktop {

inline QVariantMap agent_tool_call_validation_audit(
    const AgentLocalModelPlanResult& plan) {
  return plan.prompt_audit.value(QStringLiteral("toolCallValidation")).toMap();
}

inline bool agent_has_tool_call_validation_rejection(
    const AgentLocalModelPlanResult& plan) {
  const auto validation = agent_tool_call_validation_audit(plan);
  return validation.value(QStringLiteral("valid")).metaType().id() == QMetaType::Bool &&
      !validation.value(QStringLiteral("valid")).toBool() &&
      !validation.value(QStringLiteral("rejectedCalls")).toList().isEmpty();
}

inline QByteArray agent_tool_call_validation_recovery_fingerprint(
    const AgentLocalModelPlanResult& plan) {
  if (!agent_has_tool_call_validation_rejection(plan)) return {};
  const auto validation = agent_tool_call_validation_audit(plan);
  const QVariantMap rejected_plan_identity{
      {QStringLiteral("rejectedCalls"), validation.value(QStringLiteral("rejectedCalls"))},
      {QStringLiteral("unexecutedCalls"), validation.value(QStringLiteral("unexecutedCalls"))},
  };
  return QCryptographicHash::hash(
      QJsonDocument::fromVariant(rejected_plan_identity).toJson(QJsonDocument::Compact),
      QCryptographicHash::Sha256);
}

inline QVariantMap agent_tool_call_validation_recovery_envelope(
    const AgentLocalModelPlanResult& plan,
    const QVariantList& current_tool_schemas = {}) {
  if (!agent_has_tool_call_validation_rejection(plan)) return {};

  const auto validation = agent_tool_call_validation_audit(plan);
  const auto rejected_calls = validation.value(QStringLiteral("rejectedCalls")).toList();
  QVariantList current_schemas;
  for (const auto& rejected_value : rejected_calls) {
    const auto rejected = rejected_value.toMap();
    auto tool_name = rejected.value(QStringLiteral("toolName")).toString().trimmed();
    if (tool_name.isEmpty()) {
      tool_name = rejected.value(QStringLiteral("originalCall")).toMap()
                      .value(QStringLiteral("name")).toString().trimmed();
    }
    for (const auto& schema_value : current_tool_schemas) {
      const auto schema = schema_value.toMap();
      if (schema.value(QStringLiteral("name")).toString().trimmed() != tool_name) continue;
      current_schemas.append(schema_value);
      break;
    }
  }

  return QVariantMap{
      {QStringLiteral("kind"), QStringLiteral("tool-call-validation-recovery")},
      {QStringLiteral("contract"), QStringLiteral("agent-tool-call-validation-recovery-v1")},
      {QStringLiteral("plannerError"), plan.error_message},
      {QStringLiteral("toolCallValidation"), validation},
      {QStringLiteral("currentToolSchemas"), current_schemas},
      {QStringLiteral("wholeGeneratedPlanWithheld"), true},
      {QStringLiteral("noCallsInRejectedPlanExecuted"), true},
      {QStringLiteral("unexecutedCalls"), validation.value(QStringLiteral("unexecutedCalls"))},
      {QStringLiteral("acceptedMutationsRemainApplied"), true},
      {QStringLiteral("requiresCorrectedToolCallPlan"), true},
      {QStringLiteral("instruction"), QStringLiteral(
          "Every call in the rejected generated plan was withheld; none of its calls executed, including calls before an invalid call. Only successful native receipts from earlier planner rounds are applied. Use the exact rejected calls, the full unexecuted call list, current schemas, and validation errors to produce a corrected productive tool_call_plan. Do not repeat rejected calls unchanged, replay successful earlier operations, or invent operation names or IDs. Use earlier accepted receipts or fresh inspection for dependent edits, and replan all still-needed work from the withheld plan. Do not declare completion while required work remains unresolved.")},
  };
}

}  // namespace photo_editor::desktop
