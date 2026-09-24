#pragma once

#include "photo_editor/pipeline/color_fill_layer_program.h"

namespace photo_editor::pipeline {

[[nodiscard]] ColorPixel apply_compiled_fill_layer_point_effects(
    const CompiledFillLayerProgram& program, const CompiledFillLayer& layer,
    std::uint32_t x, std::uint32_t y, const ColorPixel& input);

}  // namespace photo_editor::pipeline
