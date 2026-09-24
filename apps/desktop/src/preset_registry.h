#pragma once

#include <QJsonArray>
#include <QMap>
#include <QString>
#include <QStringList>

#include "edit_recipe_state.h"

namespace photo_editor::desktop {

[[nodiscard]] QMap<QString, EditRecipeState> built_in_preset_map();
[[nodiscard]] QMap<QString, QString> built_in_preset_pack_map();
[[nodiscard]] QMap<QString, QString> built_in_preset_category_map();
[[nodiscard]] QMap<QString, QStringList> built_in_preset_group_map();
[[nodiscard]] QMap<QString, QJsonArray> built_in_preset_adaptive_rule_map();

}  // namespace photo_editor::desktop
