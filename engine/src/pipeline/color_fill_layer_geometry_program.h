#pragma once

#include "photo_editor/pipeline/color_geometry_program.h"

namespace photo_editor::pipeline {

[[nodiscard]] CompiledGeometryStageProgram compile_retained_layer_geometry(
    const PhotoLayer& layer,
    const ColorAdjustmentState& state,
    int width,
    int height);

}  // namespace photo_editor::pipeline
