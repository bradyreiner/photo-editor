#pragma once

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
};

struct AgentLocalModelPlanResult final {
  bool ok = false;
  QString error_message;
  QString model_id;
  QString plan_summary;
  QVariantList style_constraint_citations;
  QVariantMap recipe_patch;
  QStringList warnings;
};

[[nodiscard]] QVariantList available_agent_local_models();
[[nodiscard]] QString default_agent_local_model_id();
[[nodiscard]] AgentLocalModelPlanResult plan_agent_recipe_patch(
    const AgentLocalModelPlanRequest& request);

}  // namespace photo_editor::desktop
