#pragma once

#include "photo_editor/pipeline/color_fill_layer_program.h"

namespace photo_editor::pipeline {

[[nodiscard]] bool apply_compiled_fill_layer_detail(
    std::vector<ColorPixel>* pixels, const CompiledFillLayerProgram& program,
    std::size_t layer_index);

}  // namespace photo_editor::pipeline
