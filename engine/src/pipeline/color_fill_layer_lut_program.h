#pragma once

#include "photo_editor/pipeline/color_adjustments.h"
#include "photo_editor/pipeline/color_lut_program.h"

namespace photo_editor::pipeline {

[[nodiscard]] CompiledUserLutStageProgram compile_retained_layer_user_luts(
    const PhotoLayer& layer,
    const ColorAdjustmentState& state);

}  // namespace photo_editor::pipeline
