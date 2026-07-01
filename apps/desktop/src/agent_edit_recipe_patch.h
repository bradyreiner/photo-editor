#pragma once

#include <QStringList>
#include <QVariantMap>

#include "edit_recipe_state.h"

namespace photo_editor::desktop {

struct AgentRecipePatchValidationResult final {
  bool valid = false;
  QStringList errors;
  QStringList warnings;
};

[[nodiscard]] AgentRecipePatchValidationResult validate_agent_recipe_patch(
    const QVariantMap& patch,
    const QVariantMap& style);
[[nodiscard]] QVariantMap agent_recipe_patch_tool_schema();
[[nodiscard]] QVariantMap agent_recipe_patch_validation_coverage();
[[nodiscard]] QVariantMap agent_recipe_patch_diff(
    const EditRecipeState& before,
    const EditRecipeState& after,
    const QVariantMap& patch);
void apply_agent_recipe_patch(EditRecipeState* state, const QVariantMap& patch);

}  // namespace photo_editor::desktop
