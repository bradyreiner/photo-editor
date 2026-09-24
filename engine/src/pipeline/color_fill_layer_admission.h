#pragma once

#include "color_working_space_runtime.h"

namespace photo_editor::pipeline {

[[nodiscard]] bool retained_layer_adjustment_supported(
    const PhotoLayer& layer,
    const ColorAdjustmentState& state,
    const PipelineColorContext& context);
[[nodiscard]] bool retained_layer_tone_active(const PhotoLayer& layer);
[[nodiscard]] bool retained_layer_advanced_tone_active(const PhotoLayer& layer);
[[nodiscard]] bool retained_layer_local_chroma_active(const PhotoLayer& layer);
[[nodiscard]] ColorAdjustmentState retained_layer_tone_state(
    const PhotoLayer& layer,
    const ColorAdjustmentState& state);
[[nodiscard]] ColorAdjustmentState retained_layer_look_state(
    const PhotoLayer& layer,
    const ColorAdjustmentState& state);
[[nodiscard]] ColorAdjustmentState retained_layer_advanced_tone_state(
    const PhotoLayer& layer,
    const ColorAdjustmentState& state);
[[nodiscard]] CompiledToneStageLut compile_retained_layer_advanced_tone(
    const PhotoLayer& layer,
    const ColorAdjustmentState& state);

}  // namespace photo_editor::pipeline
