#pragma once

#include "edit_recipe_state.h"
#include "photo_editor/pipeline/color_adjustments.h"

#include <QString>

class QImage;

namespace photo_editor::desktop {

[[nodiscard]] bool prewarm_source_image_decode_pyramid(
    const QString& file_path,
    const EditRecipeState& state,
    int settled_edge,
    int draft_edge,
    photo_editor::pipeline::CancellationToken cancellation = {});

// Retrieve an existing decode without triggering I/O, including full-resolution
// and non-RAW sources. The returned image shares cache storage safely.
[[nodiscard]] bool find_source_image_decode_cache(
    const QString& file_path, const EditRecipeState& state, int max_edge, QImage* image);

[[nodiscard]] bool source_image_decode_cache_contains(
    const QString& file_path,
    const EditRecipeState& state,
    int max_edge);

}  // namespace photo_editor::desktop
