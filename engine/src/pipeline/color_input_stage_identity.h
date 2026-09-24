#pragma once

#include "color_input_runtime.h"

namespace photo_editor::pipeline {

[[nodiscard]] bool normalize_raw_stage_is_identity(
    const InputStageParameters& parameters);
[[nodiscard]] bool camera_characterization_stage_is_identity(
    const InputStageParameters& parameters);
[[nodiscard]] bool scene_adjustments_stage_is_identity(
    const InputStageParameters& parameters);

}  // namespace photo_editor::pipeline
