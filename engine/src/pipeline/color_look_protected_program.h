#pragma once

#include "photo_editor/pipeline/color_adjustments.h"

namespace photo_editor::pipeline {

void compile_look_protected_program(
    CompiledLookStageProgram* compiled,
    const ColorAdjustmentState& state);

[[nodiscard]] float compiled_look_skin_protection_scale(
    const CompiledLookStageProgram& compiled,
    float a,
    float b);

void apply_compiled_look_replacement(
    const CompiledLookStageProgram& compiled,
    const ColorPixel& input,
    float skin_scale,
    float* lightness,
    float* a,
    float* b,
    float* luminance_reference,
    float* luminance_weight);

[[nodiscard]] ColorPixel apply_compiled_replacement_luminance(
    const CompiledLookStageProgram& compiled,
    const ColorPixel& output,
    float luminance_reference,
    float luminance_weight);

}  // namespace photo_editor::pipeline
