#pragma once

#include <QJsonObject>
#include <QStringList>

namespace photo_editor::desktop {

void validate_edit_recipe_content_contract(
    const QJsonObject& recipe,
    QStringList* errors);

}  // namespace photo_editor::desktop
