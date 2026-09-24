#pragma once

#include "edit_recipe_state.h"
#include "photo_editor/pipeline/render_executor.h"

#include <QColorSpace>
#include <QImage>

namespace photo_editor::desktop {

[[nodiscard]] QImage render_canonical_thumbnail_image(
    const QImage& image,
    const EditRecipeState& state,
    const QColorSpace& target_color_space,
    const photo_editor::pipeline::RenderPlan* canonical_plan);

}  // namespace photo_editor::desktop
