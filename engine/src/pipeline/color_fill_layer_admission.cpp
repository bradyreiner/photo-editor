#include "color_fill_layer_admission.h"
#include "color_fill_layer_curve_program.h"
#include "color_fill_layer_look_program.h"

#include "color_layer_runtime.h"
#include "color_tone_runtime.h"

#include <array>
#include <bit>
#include <cmath>
#include <ranges>

namespace photo_editor::pipeline {

bool retained_layer_adjustment_supported(
    const PhotoLayer& layer, const ColorAdjustmentState& state,
    const PipelineColorContext& context) {
  const auto admitted = std::array{
      layer.exposure, layer.contrast, layer.highlights, layer.shadows,
      layer.whites, layer.blacks, layer.temperature, layer.tint,
      layer.texture, layer.clarity,
      layer.sharpening, layer.sharpen_radius, layer.sharpen_detail,
      layer.sharpen_masking, layer.luminance_nr, layer.color_nr,
      layer.vibrance, layer.saturation, layer.hue_shift,
      layer.saturation_shift, layer.colorize_hue, layer.colorize_strength,
      layer.brightness, layer.filmic_shoulder, layer.filmic_toe,
      layer.adaptive_contrast,
      layer.levels_black_point, layer.levels_gamma, layer.levels_white_point,
      layer.zone_deep_shadows, layer.zone_shadows, layer.zone_midtones,
      layer.zone_lights, layer.zone_highlights, layer.lens_vignette, layer.dehaze,
      layer.lens_distortion, layer.chromatic_aberration,
      layer.perspective_vertical, layer.perspective_horizontal,
      layer.global_transform_rotation, layer.global_transform_scale,
      layer.transform_skew_x, layer.transform_skew_y,
      layer.liquify_center_x, layer.liquify_center_y,
      layer.liquify_radius, layer.liquify_strength,
      layer.curve_shadows,
      layer.curve_darks, layer.curve_lights, layer.curve_highlights};
  return layer.kind == "adjustment" &&
         std::ranges::all_of(admitted, [](double value) {
           return std::isfinite(value);
         }) &&
         retained_layer_downstream_look_supported(layer, state, context);
}

bool retained_layer_tone_active(const PhotoLayer& layer) {
  return std::ranges::any_of(
      std::array{layer.contrast, layer.highlights, layer.shadows,
                 layer.whites, layer.blacks},
      [](double value) { return std::abs(value) > 1.0e-9; });
}

bool retained_layer_advanced_tone_active(const PhotoLayer& layer) {
  return std::ranges::any_of(
      std::array{
          layer.brightness, layer.filmic_toe, layer.adaptive_contrast,
          layer.filmic_shoulder,
          layer.levels_black_point, layer.levels_gamma, layer.levels_white_point,
          layer.zone_deep_shadows, layer.zone_shadows, layer.zone_midtones,
          layer.zone_lights, layer.zone_highlights},
      [](double value) { return std::abs(value) > 1.0e-9; });
}

bool retained_layer_local_chroma_active(const PhotoLayer& layer) {
  return std::abs(layer.hue_shift) > 1.0e-9 ||
         std::abs(layer.saturation_shift) > 1.0e-9 ||
         std::abs(layer.colorize_strength) > 1.0e-9;
}

ColorAdjustmentState retained_layer_tone_state(
    const PhotoLayer& layer, const ColorAdjustmentState& state) {
  auto tone_state = ColorAdjustmentState{};
  tone_state.color_pipeline = state.color_pipeline;
  tone_state.contrast = layer.contrast;
  tone_state.highlights = layer.highlights;
  tone_state.shadows = layer.shadows;
  tone_state.whites = layer.whites;
  tone_state.blacks = layer.blacks;
  return tone_state;
}

ColorAdjustmentState retained_layer_look_state(
    const PhotoLayer& layer, const ColorAdjustmentState& state) {
  auto look_state = ColorAdjustmentState{};
  look_state.color_pipeline = state.color_pipeline;
  look_state.saturation = layer.saturation;
  look_state.vibrance = layer.vibrance;
  return look_state;
}

ColorAdjustmentState retained_layer_advanced_tone_state(
    const PhotoLayer& layer, const ColorAdjustmentState& state) {
  auto tone_state = ColorAdjustmentState{};
  tone_state.color_pipeline = state.color_pipeline;
  tone_state.brightness = layer.brightness;
  tone_state.filmic_toe = layer.filmic_toe;
  tone_state.adaptive_contrast = layer.adaptive_contrast;
  tone_state.levels_black_point = layer.levels_black_point;
  tone_state.levels_gamma = layer.levels_gamma;
  tone_state.levels_white_point = layer.levels_white_point;
  tone_state.zone_deep_shadows = layer.zone_deep_shadows;
  tone_state.zone_shadows = layer.zone_shadows;
  tone_state.zone_midtones = layer.zone_midtones;
  tone_state.zone_lights = layer.zone_lights;
  tone_state.zone_highlights = layer.zone_highlights;
  return tone_state;
}

CompiledToneStageLut compile_retained_layer_advanced_tone(
    const PhotoLayer& layer, const ColorAdjustmentState& state) {
  auto compiled = compile_tone_stage_lut(
      retained_layer_advanced_tone_state(layer, state));
  if (!compiled.ready() || std::abs(layer.filmic_shoulder) <= 1.0e-9) {
    return compiled;
  }
  for (auto& sample : compiled.samples) {
    const auto threshold = shoulder_threshold_for_max_luminance(
        sample, compiled.output_headroom, layer.filmic_shoulder);
    if (threshold < compiled.output_headroom) {
      sample = static_cast<float>(apply_highlight_shoulder(
          sample, threshold, compiled.output_headroom, layer.filmic_shoulder));
    }
  }
  auto identity = std::uint64_t{14695981039346656037ULL};
  for (const auto sample : compiled.samples) {
    identity ^= std::bit_cast<std::uint32_t>(sample);
    identity *= 1099511628211ULL;
  }
  compiled.cache_identity = identity != 0U ? identity : 1U;
  return compiled;
}

}  // namespace photo_editor::pipeline
