#pragma once

#include "photo_editor/pipeline/color_adjustments.h"

#include <string_view>

namespace photo_editor::pipeline {

[[nodiscard]] std::string_view normalized_color_mixer_model(
    const ColorAdjustmentState& state);

[[nodiscard]] double rgb_hue_for_mixer_selection(
    const ColorPixel& pixel);

[[nodiscard]] double mixer_selection_hue(
    const ColorPixel& pixel,
    double oklch_hue,
    double chroma,
    std::string_view model);

void apply_selective_color_adjustment(
    const ColorMixerChannelAdjustment& mixer,
    double mixer_weight,
    double* lightness,
    double* chroma,
    double* hue);

[[nodiscard]] bool has_rgb_channel_mixer_adjustments(
    const ColorAdjustmentState& state);

[[nodiscard]] ColorPixel apply_rgb_channel_mixer(
    const ColorPixel& pixel,
    const RgbChannelMixerAdjustment& mixer);

[[nodiscard]] ColorPixel apply_black_and_white_mixer(
    const ColorPixel& pixel,
    const ColorAdjustmentState& state,
    double native_luminance);

[[nodiscard]] bool has_per_channel_color_mixer_adjustments(
    const ColorAdjustmentState& state);

}  // namespace photo_editor::pipeline
