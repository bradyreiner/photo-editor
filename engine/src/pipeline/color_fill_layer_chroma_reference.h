#pragma once

#include "photo_editor/pipeline/color_fill_layer_program.h"

namespace photo_editor::pipeline {

[[nodiscard]] ColorPixel apply_compiled_fill_layer_chroma(
    const CompiledFillLayerProgram& program,
    std::size_t layer_index,
    const ColorPixel& input);

}  // namespace photo_editor::pipeline
