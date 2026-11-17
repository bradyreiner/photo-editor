#pragma once

#include <QString>
#include <QStringList>
#include <QVariantMap>

namespace photo_editor::desktop {

[[nodiscard]] const QStringList& photo_layer_adjustment_recipe_keys();
[[nodiscard]] QString layer_adjustment_recipe_schema_contract();
[[nodiscard]] QString layer_adjustment_recipe_compatibility_contract();
[[nodiscard]] QVariantMap photo_layer_adjustment_recipe(const QVariantMap& layer);
// Derive this from persisted controls, never from the UI's optional annotation.
// Neutral and inactive parameter defaults do not require a photographic develop.
[[nodiscard]] bool photo_layer_has_non_neutral_adjustments(const QVariantMap& layer);
[[nodiscard]] bool photo_layer_supports_adjustment_recipe(const QVariantMap& layer);
bool replace_photo_layer_adjustment_recipe(
    QVariantMap* layer,
    const QVariantMap& recipe);

}  // namespace photo_editor::desktop
