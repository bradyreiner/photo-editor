#pragma once

#include "edit_recipe_state.h"
#include "photo_editor/pipeline/color_adjustments.h"

#include <QString>

namespace photo_editor::desktop {

[[nodiscard]] bool prewarm_source_image_decode_pyramid(
    const QString& file_path,
    const EditRecipeState& state,
    int settled_edge,
    int draft_edge,
    photo_editor::pipeline::CancellationToken cancellation = {});

[[nodiscard]] bool source_image_decode_cache_contains(
    const QString& file_path,
    const EditRecipeState& state,
    int max_edge);

}  // namespace photo_editor::desktop
