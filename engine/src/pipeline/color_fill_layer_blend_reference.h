#pragma once

#include "photo_editor/pipeline/color_fill_layer_program.h"

namespace photo_editor::pipeline {

[[nodiscard]] ColorPixel apply_retained_layer_blend(
    const CompiledFillLayerProgram& program,
    const ColorPixel& base,
    const ColorPixel& source,
    std::uint32_t mode);

}  // namespace photo_editor::pipeline
