#pragma once

#include "photo_editor/pipeline/color_adjustments.h"

#include <cstdint>

namespace photo_editor::pipeline {

void compile_look_grading_program(CompiledLookStageProgram* compiled,
                                  const ColorAdjustmentState& state);

void apply_compiled_look_grading(
    const CompiledLookStageProgram& compiled,
    float skin_scale,
    float* lightness,
    float* a,
    float* b);

void compile_look_mixer_program(CompiledLookStageProgram* compiled,
                                const ColorAdjustmentState& state);

void apply_compiled_look_mixer(
    const CompiledLookStageProgram& compiled,
    const ColorPixel& input,
    float skin_scale,
    float* lightness,
    float* a,
    float* b);

void compile_look_harmony_program(CompiledLookStageProgram* compiled,
                                  const ColorAdjustmentState& state);

void apply_compiled_look_harmony(
    const CompiledLookStageProgram& compiled,
    float skin_scale,
    float* a,
    float* b);

[[nodiscard]] std::uint64_t look_stage_program_payload_identity(const CompiledLookStageProgram& compiled);
}  // namespace photo_editor::pipeline
