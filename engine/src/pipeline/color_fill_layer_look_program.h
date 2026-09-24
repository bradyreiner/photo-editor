#pragma once

#include "photo_editor/pipeline/color_fill_layer_program.h"
#include "color_working_space_runtime.h"

namespace photo_editor::pipeline {

[[nodiscard]] ColorAdjustmentState retained_layer_downstream_look_state(
    const PhotoLayer& layer,
    const ColorAdjustmentState& state);
[[nodiscard]] bool retained_layer_downstream_look_supported(
    const PhotoLayer& layer,
    const ColorAdjustmentState& state,
    const PipelineColorContext& context);
void append_retained_layer_downstream_look(
    const PhotoLayer& layer,
    const ColorAdjustmentState& state,
    std::size_t index,
    CompiledFillLayerProgram* program);
[[nodiscard]] bool retained_layer_downstream_look_ready(
    const CompiledFillLayerProgram& program,
    const CompiledFillLayer& layer,
    std::size_t index);
void finish_retained_layer_downstream_look_identity(
    CompiledFillLayerProgram* program);

}  // namespace photo_editor::pipeline
