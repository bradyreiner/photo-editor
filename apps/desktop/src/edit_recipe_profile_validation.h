#pragma once

#include <QJsonObject>
#include <QStringList>

namespace photo_editor::desktop {

void validate_edit_recipe_profile_contract(
    const QJsonObject& color_pipeline,
    QStringList* errors);

}  // namespace photo_editor::desktop
