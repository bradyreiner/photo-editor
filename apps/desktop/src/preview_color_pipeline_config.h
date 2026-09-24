#pragma once

#include "edit_recipe_state.h"
#include "system_monitor_profile_state.h"

#include "photo_editor/pipeline/color_pipeline.h"

namespace photo_editor::desktop {

[[nodiscard]] photo_editor::pipeline::ColorPipelineConfig
compile_preview_color_pipeline_config_for_display(
    const EditRecipeState& state,
    const photo_editor::pipeline::DecodedImageMetadata* metadata,
    const SystemMonitorProfileSnapshot& monitor_snapshot);

}  // namespace photo_editor::desktop
