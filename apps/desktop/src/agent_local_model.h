#pragma once

#include <functional>

#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

namespace photo_editor::desktop {

struct AgentLocalModelPlanRequest final {
  QString model_id;
  QVariantMap style;
  QVariantMap asset;
  QString current_recipe_json;
  QString user_instructions;
  QVariantList available_tool_schemas;
  QVariantList prior_tool_results;
  QString strategy_phase = QStringLiteral("sample");
  QVariantMap memory_packet;
  QVariantList few_shot_examples;
  bool allow_tool_call_plan = false;
  std::function<bool()> cancellation_requested;
};

struct AgentLocalModelPlanResult final {
  bool ok = false;
  QString error_message;
  QString model_id;
  QString decision;
  QString plan_summary;
  QString safe_stop_reason;
  QVariantMap prompt_audit;
  QVariantList style_constraint_citations;
  QVariantMap recipe_patch;
  QVariantList tool_call_plan;
  QVariantList user_questions;
  QStringList warnings;
};

struct AgentLocalModelProofCritiqueRequest final {
  QString model_id;
  QVariantMap style;
  QVariantMap asset;
  QString user_instructions;
  QVariantMap validation_report;
  QVariantList previous_attempts;
  std::function<bool()> cancellation_requested;
};

struct AgentLocalModelProofCritiqueResult final {
  bool ok = false;
  QString error_message;
  QString model_id;
  bool pass = false;
  bool needs_user_review = false;
  QString summary;
  QString taste_confidence;
  QVariantList issues;
  QVariantList repair_hints;
  QVariantMap prompt_audit;
  QStringList warnings;
};

[[nodiscard]] QVariantList available_agent_local_models();
[[nodiscard]] QString default_agent_local_model_id();
[[nodiscard]] QVariantMap probe_agent_local_model(const QString& model_id);
[[nodiscard]] AgentLocalModelPlanResult plan_agent_recipe_patch(
    const AgentLocalModelPlanRequest& request);
[[nodiscard]] AgentLocalModelProofCritiqueResult critique_agent_rendered_proof(
    const AgentLocalModelProofCritiqueRequest& request);

}  // namespace photo_editor::desktop
