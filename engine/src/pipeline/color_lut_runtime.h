#pragma once

#include "photo_editor/pipeline/color_lut_program.h"
#include "photo_editor/pipeline/color_transform.h"

#include <string_view>
#include <vector>

namespace photo_editor::pipeline {

[[nodiscard]] std::vector<CompiledUserLut> compile_user_lut_runtimes(
    const ColorAdjustmentState& state,
    const RgbColorSpace& working_space,
    std::string_view chromatic_adaptation_method);

[[nodiscard]] ColorPixel apply_user_luts_to_pixel(
    const ColorPixel& input,
    const std::vector<CompiledUserLut>& user_luts);

}  // namespace photo_editor::pipeline
