#pragma once

#include <functional>

#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

namespace photo_editor::desktop {

bool agent_instructions_require_grounded_edit(const QString& instructions);

struct AgentLocalModelPlanRequest final {
  QString model_id;
  QVariantMap style;
  QVariantMap asset;
  QString current_adjustment_patch_json;
  QString user_instructions;
  QVariantList available_tool_schemas;
  QVariantList prior_tool_results;
  QString strategy_phase = QStringLiteral("sample");
  QVariantMap memory_packet;
  QVariantList few_shot_examples;
  bool allow_tool_call_plan = false;
  bool force_model_planning = false;
  bool allow_deterministic_fallback = true;
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
  QVariantMap edit_intent;
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
  QVariantList graphic_layers;
  QVariantMap validation_report;
  QVariantList previous_attempts;
  QVariantMap photographer_calibration;
  QVariantMap project_north_star;
  std::function<bool()> cancellation_requested;
};

struct AgentLocalModelProofCritiqueResult final {
  bool ok = false;
  QString error_message;
  QString model_id;
  bool pass = false;
  // A transport, image, or response-validation failure is never verification.
  bool needs_user_review = true;
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
