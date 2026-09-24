#pragma once

#include "photo_editor/pipeline/color_fill_layer_program.h"

namespace photo_editor::pipeline {

[[nodiscard]] bool retained_layer_curves_active(const PhotoLayer& layer);
[[nodiscard]] ColorAdjustmentState retained_layer_curve_state(
    const PhotoLayer& layer,
    const ColorAdjustmentState& state);
[[nodiscard]] bool retained_layer_curves_supported(
    const PhotoLayer& layer,
    const ColorAdjustmentState& state);
void append_retained_layer_curves(
    const PhotoLayer& layer,
    const ColorAdjustmentState& state,
    std::size_t index,
    CompiledFillLayerProgram* program);
[[nodiscard]] bool retained_layer_curves_ready(
    const CompiledFillLayerProgram& program,
    const CompiledFillLayer& layer,
    std::size_t index);
void finish_retained_layer_curve_identity(CompiledFillLayerProgram* program);

}  // namespace photo_editor::pipeline
