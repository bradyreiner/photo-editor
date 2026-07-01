#pragma once

#include <QImage>
#include <QVariantMap>

namespace photo_editor::desktop {

struct EditRecipeState;

[[nodiscard]] QVariantMap validate_agent_edit_image(
    const QImage& image,
    const QVariantMap& style = {});

[[nodiscard]] QVariantMap validate_agent_edit_image(
    const QImage& image,
    const EditRecipeState& recipe,
    const QVariantMap& style = {});

}  // namespace photo_editor::desktop
