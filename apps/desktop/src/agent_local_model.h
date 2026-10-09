#pragma once

#include <functional>

#include <QList>
#include <QSize>
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
  // Require a structured tool plan instead of accepting a final numeric edit
  // intent. This is request-scoped and only meaningful when tools are allowed.
  bool require_tool_call_plan = false;
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

// Read-only conversation planning. This transport can suggest questions and an
// approved plan, but it has no command or mutation interface.
struct AgentLocalModelTaskSessionRequest final {
  QString model_id;
  QVariantMap brief;
  QVariantMap context;
  QVariantMap answers;
  QVariantList prior_questions;
  std::function<bool()> cancellation_requested;
  std::function<void(const QString&)> activity_received;
};

struct AgentLocalModelTaskSessionResult final {
  bool ok = false;
  QString error_message;
  QString model_id;
  QString decision;
  QVariantList questions;
  QVariantMap plan;
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
[[nodiscard]] AgentLocalModelTaskSessionResult plan_agent_task_session(
    const AgentLocalModelTaskSessionRequest& request);
[[nodiscard]] QStringList agent_composition_text_layer_ids(const QVariantList& tool_reports);
[[nodiscard]] QVariantMap agent_graphic_proof_layout_metrics(const QVariantMap& layer);
[[nodiscard]] QVariantList agent_graphic_proof_layers(
    const QVariantList& layers, const QStringList& composition_text_layer_ids = {});

namespace agent_detail {
// Bind each approved text action to one primary role and exact copy.
[[nodiscard]] QVariantMap approved_typography_role_copy(
    const QVariantList& plan_actions, const QVariantList& expected_copy);
[[nodiscard]] QVariantMap typography_hierarchy_fit_guidance(
    const QVariantMap& headline_layer, double minimum_ratio);
// Shared production audit helper for image-token context reservations. Missing,
// malformed, unrecognized, or larger images intentionally keep the 4096-token
// fallback; tests use this same implementation for those fail-safe branches.
[[nodiscard]] QVariantList ollama_image_context_reservations(
    const QString& model_name,
    const QList<QSize>& transmitted_sizes,
    int attached_image_count);
}  // namespace agent_detail

}  // namespace photo_editor::desktop
