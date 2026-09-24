#include "color_layer_runtime.h"
#include "color_adjustment_kernel_runtime.h"
#include "color_layer_composite.h"
#include "color_layer_sampling.h"
#include "photo_editor/pipeline/photo_effects.h"

#include "color_adjustment_scheduler.h"
#include "color_blur_runtime.h"
#include "color_curve_evaluator.h"
#include "color_look_runtime.h"
#include "color_lut_runtime.h"
#include "color_math_runtime.h"
#include "color_mixer_runtime.h"
#include "color_spatial_runtime.h"
#include "color_tone_runtime.h"

#include <algorithm>
#include <array>
#include <iterator>
#include <cmath>
#include <functional>
#include <numbers>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace photo_editor::pipeline {

namespace {

constexpr double kPi = std::numbers::pi_v<double>;

template <typename Work>
void parallel_for(int count, Work&& work) {
  run_color_adjustment_parallel_for(
      count, std::function<void(int)>(std::forward<Work>(work)));
}

}  // namespace

[[nodiscard]] bool has_photo_layers(const std::vector<PhotoLayer>& layers) {
  for (const auto& layer : layers) {
    if (layer.kind != "group" && layer.enabled && layer.opacity > 1e-6) {
      return true;
    }
  }
  return false;
}

[[nodiscard]] double photo_layer_mask_weight(
    const PhotoLayer& layer,
    int x,
    int y,
    int width,
    int height) {
  const auto expected_size =
      static_cast<std::size_t>(layer.mask_width) * static_cast<std::size_t>(layer.mask_height);
  if (layer.mask_width <= 0 || layer.mask_height <= 0 || layer.mask_data.size() != expected_size ||
      width <= 0 || height <= 0) {
    return 1.0;
  }

  const auto normalized_x =
      width > 1 ? static_cast<double>(std::clamp(x, 0, width - 1)) / static_cast<double>(width - 1)
                : 0.0;
  const auto normalized_y =
      height > 1 ? static_cast<double>(std::clamp(y, 0, height - 1)) / static_cast<double>(height - 1)
                 : 0.0;
  const auto mask_x = std::clamp(
      static_cast<int>(std::lround(normalized_x * static_cast<double>(layer.mask_width - 1))),
      0,
      layer.mask_width - 1);
  const auto mask_y = std::clamp(
      static_cast<int>(std::lround(normalized_y * static_cast<double>(layer.mask_height - 1))),
      0,
      layer.mask_height - 1);
  const auto index = static_cast<std::size_t>(mask_y * layer.mask_width + mask_x);
  auto weight = static_cast<double>(layer.mask_data[index]) / 255.0;
  if (layer.mask_inverted) {
    weight = 1.0 - weight;
  }
  weight = std::min(weight, std::clamp(layer.mask_density, 0.0, 1.0));
  return clamp_unit(weight * std::clamp(layer.mask_opacity, 0.0, 1.0));
}

[[nodiscard]] double overlay_channel(double base, double source, double headroom) {
  const auto safe_headroom =
      sanitized_scene_working_headroom(headroom);
  const auto normalized_base = std::clamp(base / safe_headroom, 0.0, 1.0);
  const auto normalized_source = std::clamp(source / safe_headroom, 0.0, 1.0);
  const auto blended = normalized_base <= 0.5
                           ? 2.0 * normalized_base * normalized_source
                           : 1.0 - 2.0 * (1.0 - normalized_base) * (1.0 - normalized_source);
  return blended * safe_headroom;
}

[[nodiscard]] double soft_light_channel(double base, double source, double headroom) {
  const auto safe_headroom =
      sanitized_scene_working_headroom(headroom);
  const auto normalized_base = std::clamp(base / safe_headroom, 0.0, 1.0);
  const auto normalized_source = std::clamp(source / safe_headroom, 0.0, 1.0);
  double blended = normalized_base;
  if (normalized_source <= 0.5) {
    blended = normalized_base -
              (1.0 - 2.0 * normalized_source) * normalized_base * (1.0 - normalized_base);
  } else {
    const auto d = normalized_base <= 0.25
                       ? ((16.0 * normalized_base - 12.0) * normalized_base + 4.0) *
                             normalized_base
                       : std::sqrt(normalized_base);
    blended = normalized_base + (2.0 * normalized_source - 1.0) * (d - normalized_base);
  }
  return blended * safe_headroom;
}

[[nodiscard]] ColorPixel blend_layer_color(
    const ColorPixel& base,
    const ColorPixel& source,
    const std::string& mode,
    const PipelineColorContext& context) {
  // Blend equations use the declared creative/output reference range. The
  // much wider scene working headroom is a storage contract, not the neutral
  // for Screen, Overlay, Soft Light, or Linear Light.
  const auto headroom =
      context.layer_blend_headroom;
  const auto base_red = std::max(0.0, static_cast<double>(base.red));
  const auto base_green = std::max(0.0, static_cast<double>(base.green));
  const auto base_blue = std::max(0.0, static_cast<double>(base.blue));
  const auto source_red = std::max(0.0, static_cast<double>(source.red));
  const auto source_green = std::max(0.0, static_cast<double>(source.green));
  const auto source_blue = std::max(0.0, static_cast<double>(source.blue));

  if (mode == "multiply") {
    return ColorPixel{
        static_cast<float>(base_red * source_red),
        static_cast<float>(base_green * source_green),
        static_cast<float>(base_blue * source_blue),
        source.alpha};
  }
  if (mode == "screen") {
    return ColorPixel{
        static_cast<float>(headroom - ((headroom - base_red) * (headroom - source_red) / headroom)),
        static_cast<float>(headroom - ((headroom - base_green) * (headroom - source_green) / headroom)),
        static_cast<float>(headroom - ((headroom - base_blue) * (headroom - source_blue) / headroom)),
        source.alpha};
  }
  if (mode == "overlay") {
    return ColorPixel{
        static_cast<float>(overlay_channel(base_red, source_red, headroom)),
        static_cast<float>(overlay_channel(base_green, source_green, headroom)),
        static_cast<float>(overlay_channel(base_blue, source_blue, headroom)),
        source.alpha};
  }
  if (mode == "soft light" || mode == "soft-light") {
    return ColorPixel{
        static_cast<float>(soft_light_channel(base_red, source_red, headroom)),
        static_cast<float>(soft_light_channel(base_green, source_green, headroom)),
        static_cast<float>(soft_light_channel(base_blue, source_blue, headroom)),
        source.alpha};
  }
  if (mode == "hard light" || mode == "hard-light") {
    return ColorPixel{
        static_cast<float>(overlay_channel(source_red, base_red, headroom)),
        static_cast<float>(overlay_channel(source_green, base_green, headroom)),
        static_cast<float>(overlay_channel(source_blue, base_blue, headroom)),
        source.alpha};
  }
  if (mode == "difference") {
    return ColorPixel{
        static_cast<float>(std::abs(base_red - source_red)),
        static_cast<float>(std::abs(base_green - source_green)),
        static_cast<float>(std::abs(base_blue - source_blue)),
        source.alpha};
  }
  if (mode == "lighten") {
    return ColorPixel{
        static_cast<float>(std::max(base_red, source_red)),
        static_cast<float>(std::max(base_green, source_green)),
        static_cast<float>(std::max(base_blue, source_blue)),
        source.alpha};
  }
  if (mode == "darken") {
    return ColorPixel{
        static_cast<float>(std::min(base_red, source_red)),
        static_cast<float>(std::min(base_green, source_green)),
        static_cast<float>(std::min(base_blue, source_blue)),
        source.alpha};
  }
  if (mode == "linear light" || mode == "linear-light") {
    return ColorPixel{
        static_cast<float>(std::clamp(base_red + 2.0 * source_red - headroom, 0.0, headroom)),
        static_cast<float>(std::clamp(base_green + 2.0 * source_green - headroom, 0.0, headroom)),
        static_cast<float>(std::clamp(base_blue + 2.0 * source_blue - headroom, 0.0, headroom)),
        source.alpha};
  }
  if (mode == "hue" || mode == "saturation" || mode == "color" || mode == "luminosity") {
    auto blend_context = context;
    blend_context.output_headroom = headroom;
    const auto base_oklab = linear_rgb_to_oklab(base, context);
    const auto source_oklab = linear_rgb_to_oklab(source, context);
    auto lightness =
        clamp_oklab_lightness(
            base_oklab.lightness,
            blend_context);
    auto chroma = std::hypot(base_oklab.a, base_oklab.b);
    auto hue = wrap_radians(std::atan2(base_oklab.b, base_oklab.a));
    const auto source_chroma = std::hypot(source_oklab.a, source_oklab.b);
    const auto source_hue = source_chroma > 1.0e-6
                                ? wrap_radians(std::atan2(source_oklab.b, source_oklab.a))
                                : hue;
    if (mode == "hue") {
      hue = source_hue;
    } else if (mode == "saturation") {
      chroma = source_chroma;
    } else if (mode == "color") {
      hue = source_hue;
      chroma = source_chroma;
    } else {
      lightness =
          clamp_oklab_lightness(
              source_oklab.lightness,
              blend_context);
    }
    auto blended =
        gamut_fit_oklch(
            lightness,
            chroma,
            hue,
            source.alpha,
            blend_context);
    blended.alpha = source.alpha;
    return blended;
  }
  return source;
}

void composite_layer_pixel(
    ColorPixel* target,
    const ColorPixel& source,
    double alpha,
    const std::string& mode,
    const PipelineColorContext& context) {
  if (target == nullptr || alpha <= 1e-6) {
    return;
  }
  const auto blended = blend_layer_color(*target, source, mode, context);
  *target = composite_layer_source_over(*target, source, blended, alpha);
}

[[nodiscard]] bool photo_layer_uses_normal_blend(const PhotoLayer& layer) {
  return layer.blend_mode.empty() || layer.blend_mode == "normal";
}

[[nodiscard]] bool has_photo_layer_transform(const PhotoLayer& layer) {
  const auto scale = std::clamp(layer.transform_scale, 0.05, 32.0);
  return std::abs(scale - 1.0) > 1e-6 || std::abs(layer.transform_rotation) > 1e-6;
}

[[nodiscard]] bool has_photo_layer_curve_adjustments(const PhotoLayer& layer) {
  return std::abs(layer.curve_shadows) > 1e-9 || std::abs(layer.curve_darks) > 1e-9 ||
         std::abs(layer.curve_lights) > 1e-9 || std::abs(layer.curve_highlights) > 1e-9 ||
         has_point_curve(layer.master_curve_points) || has_point_curve(layer.luma_curve_points) ||
         has_point_curve(layer.red_curve_points) || has_point_curve(layer.green_curve_points) ||
         has_point_curve(layer.blue_curve_points);
}

[[nodiscard]] bool has_photo_layer_advanced_tone_adjustments(const PhotoLayer& layer) {
  return std::abs(layer.brightness) > 1e-9 || std::abs(layer.filmic_shoulder) > 1e-9 ||
         std::abs(layer.filmic_toe) > 1e-9 || std::abs(layer.adaptive_contrast) > 1e-9 ||
         std::abs(layer.levels_black_point) > 1e-9 || std::abs(layer.levels_gamma) > 1e-9 ||
         std::abs(layer.levels_white_point) > 1e-9 ||
         std::abs(layer.zone_deep_shadows) > 1e-9 ||
         std::abs(layer.zone_shadows) > 1e-9 ||
         std::abs(layer.zone_midtones) > 1e-9 ||
         std::abs(layer.zone_lights) > 1e-9 ||
         std::abs(layer.zone_highlights) > 1e-9;
}

[[nodiscard]] double apply_layer_perceptual_point_curve(double value, const CurveEvaluationData& curve) {
  return apply_perceptual_point_curve(value, curve);
}

[[nodiscard]] ColorPixel apply_layer_luminance_point_curve(
    const ColorPixel& pixel,
    const CurveEvaluationData& curve,
    double output_headroom,
    const PipelineColorContext& context) {
  if (curve.points.size() < 2) {
    return pixel;
  }
  const auto source_luminance = luminance_for_pixel(pixel, context);
  const auto target_luminance = apply_layer_perceptual_point_curve(source_luminance, curve);
  return scale_pixel_to_luminance_with_headroom(
      pixel,
      target_luminance,
      output_headroom,
      context);
}

[[nodiscard]] ColorPixel interpolate_adjustment_pixel(
    const ColorPixel& base,
    const ColorPixel& adjusted,
    double weight) {
  const auto clamped_weight = clamp_unit(weight);
  return ColorPixel{
      .red = static_cast<float>(
          std::lerp(static_cast<double>(base.red), static_cast<double>(adjusted.red), clamped_weight)),
      .green = static_cast<float>(
          std::lerp(static_cast<double>(base.green), static_cast<double>(adjusted.green), clamped_weight)),
      .blue = static_cast<float>(
          std::lerp(static_cast<double>(base.blue), static_cast<double>(adjusted.blue), clamped_weight)),
      .alpha = base.alpha,
  };
}

ColorPixel interpolate_effect_pixel(const ColorPixel& base, const ColorPixel& adjusted, double weight) {
  weight = clamp_unit(weight);
  if (weight == 0) return base;
  const auto alpha = base.alpha * (1 - weight) + adjusted.alpha * weight;
  if (alpha <= 0) return {0, 0, 0, 0};
  const auto channel = [&](float a, float b) {
    return static_cast<float>((a * base.alpha * (1 - weight) + b * adjusted.alpha * weight) / alpha);
  };
  return {channel(base.red, adjusted.red), channel(base.green, adjusted.green),
      channel(base.blue, adjusted.blue), static_cast<float>(alpha)};
}

[[nodiscard]] ColorAdjustmentState compile_photo_layer_advanced_tone(const PhotoLayer& layer) {
  ColorAdjustmentState tone_state;
  tone_state.brightness = layer.brightness;
  tone_state.filmic_shoulder = layer.filmic_shoulder;
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

[[nodiscard]] ColorPixel apply_photo_layer_advanced_tone_adjustments(
    const ColorPixel& input,
    const PipelineColorContext& context,
    const PhotoLayer& layer,
    const ColorAdjustmentState* prepared_tone) {
  // Preserve the scalar callback path as the oracle; standard layers share
  // this immutable state across all pixels and spatial-preparation workers.
  if (prepared_tone == nullptr) {
    const auto tone = compile_photo_layer_advanced_tone(layer);
    return apply_photo_layer_advanced_tone_adjustments(input, context, layer, &tone);
  }
  const auto& tone_state = *prepared_tone;
  auto target_luminance = apply_global_tone(luminance_for_pixel(input, context), tone_state);
  const auto layer_output_headroom = context.output_headroom;
  if (layer.filmic_shoulder != 0.0) {
    const auto shoulder_threshold = shoulder_threshold_for_max_luminance(
        target_luminance,
        layer_output_headroom,
        layer.filmic_shoulder);
    if (shoulder_threshold < layer_output_headroom) {
      target_luminance = apply_highlight_shoulder(
          target_luminance,
          shoulder_threshold,
          layer_output_headroom,
          layer.filmic_shoulder);
    }
  }
  auto pixel = scale_pixel_for_global_tone(
      input,
      target_luminance,
      layer_output_headroom,
      context);
  pixel.alpha = input.alpha;
  return pixel;
}

struct PhotoLayerCurveRuntime final {
  CurveEvaluationData parametric;
  CurveEvaluationData master;
  CurveEvaluationData luma;
  CurveEvaluationData red;
  CurveEvaluationData green;
  CurveEvaluationData blue;
};

[[nodiscard]] PhotoLayerCurveRuntime compile_photo_layer_curves(const PhotoLayer& layer) {
  ColorAdjustmentState curve_state;
  curve_state.curve_shadows = layer.curve_shadows;
  curve_state.curve_darks = layer.curve_darks;
  curve_state.curve_lights = layer.curve_lights;
  curve_state.curve_highlights = layer.curve_highlights;
  curve_state.master_curve_points = layer.master_curve_points;
  return {build_parametric_curve_data(curve_state), build_curve_data(layer.master_curve_points),
      build_curve_data(layer.luma_curve_points), build_curve_data(layer.red_curve_points),
      build_curve_data(layer.green_curve_points), build_curve_data(layer.blue_curve_points)};
}

[[nodiscard]] ColorPixel apply_photo_layer_curve_adjustments(
    const ColorPixel& input,
    const PipelineColorContext& context,
    const PhotoLayer& layer,
    const PhotoLayerCurveRuntime* compiled = nullptr) {
  // The fallback remains available to scalar callers and parity tests.
  const auto fallback = compiled == nullptr
      ? std::optional{compile_photo_layer_curves(layer)} : std::nullopt;
  const auto& curves = compiled != nullptr ? *compiled : *fallback;
  const auto layer_output_headroom = context.output_headroom;
  auto pixel = input;
  const auto& parametric_curve = curves.parametric;
  const auto curve_target = evaluate_tone_curve_target(
      luminance_for_pixel(pixel, context),
      parametric_curve,
      false,
      layer_output_headroom);
  pixel = scale_pixel_to_luminance_with_headroom(
      pixel,
      curve_target,
      layer_output_headroom,
      context);

  const auto& master_curve = curves.master;
  if (master_curve.points.size() >= 2) {
    pixel.red = static_cast<float>(apply_layer_perceptual_point_curve(pixel.red, master_curve));
    pixel.green = static_cast<float>(apply_layer_perceptual_point_curve(pixel.green, master_curve));
    pixel.blue = static_cast<float>(apply_layer_perceptual_point_curve(pixel.blue, master_curve));
  }

  const auto& luma_curve = curves.luma;
  if (luma_curve.points.size() >= 2) {
    pixel = apply_layer_luminance_point_curve(
        pixel,
        luma_curve,
        layer_output_headroom,
        context);
  }

  const auto& red_curve = curves.red;
  if (red_curve.points.size() >= 2) {
    pixel.red = static_cast<float>(apply_layer_perceptual_point_curve(pixel.red, red_curve));
  }
  const auto& green_curve = curves.green;
  if (green_curve.points.size() >= 2) {
    pixel.green = static_cast<float>(apply_layer_perceptual_point_curve(pixel.green, green_curve));
  }
  const auto& blue_curve = curves.blue;
  if (blue_curve.points.size() >= 2) {
    pixel.blue = static_cast<float>(apply_layer_perceptual_point_curve(pixel.blue, blue_curve));
  }

  pixel.alpha = input.alpha;
  return pixel;
}

[[nodiscard]] ColorAdjustmentState photo_layer_color_adjustment_state(const PhotoLayer& layer) {
  ColorAdjustmentState color_state;
  color_state.color_mixer_model = layer.color_mixer_model;
  color_state.color_mixer_channel = layer.color_mixer_channel;
  color_state.hue_adjustment = layer.hue_adjustment;
  color_state.saturation_adjustment = layer.saturation_adjustment;
  color_state.luminance_adjustment = layer.luminance_adjustment;
  color_state.color_mixer_channels = layer.color_mixer_channels;
  color_state.rgb_channel_mixer = layer.rgb_channel_mixer;
  color_state.black_and_white_enabled = layer.black_and_white_enabled;
  color_state.black_and_white_mixer = layer.black_and_white_mixer;
  color_state.color_replacement_enabled = layer.color_replacement_enabled;
  color_state.color_replacement_source_hue = layer.color_replacement_source_hue;
  color_state.color_replacement_target_hue = layer.color_replacement_target_hue;
  color_state.color_replacement_range = layer.color_replacement_range;
  color_state.color_replacement_softness = layer.color_replacement_softness;
  color_state.color_replacement_strength = layer.color_replacement_strength;
  color_state.color_replacement_preserve_luminance = layer.color_replacement_preserve_luminance;
  color_state.color_replacement_preserve_saturation = layer.color_replacement_preserve_saturation;
  color_state.skin_tone_protection_enabled = layer.skin_tone_protection_enabled;
  color_state.skin_tone_line_hue = layer.skin_tone_line_hue;
  color_state.skin_tone_protection_range = layer.skin_tone_protection_range;
  color_state.skin_tone_protection_strength = layer.skin_tone_protection_strength;
  color_state.color_harmony_enabled = layer.color_harmony_enabled;
  color_state.color_harmony_mode = layer.color_harmony_mode;
  color_state.color_harmony_base_hue = layer.color_harmony_base_hue;
  color_state.color_harmony_strength = layer.color_harmony_strength;
  color_state.lut_enabled = layer.lut_enabled;
  color_state.lut_strength = layer.lut_strength;
  color_state.lut_name = layer.lut_name;
  color_state.lut_table = layer.lut_table;
  color_state.lut_size = layer.lut_size;
  color_state.lut_domain_min = layer.lut_domain_min;
  color_state.lut_domain_max = layer.lut_domain_max;
  color_state.lut_input_color_space = layer.lut_input_color_space;
  color_state.lut_output_color_space = layer.lut_output_color_space;
  color_state.lut_stack = layer.lut_stack;
  color_state.grading_shadows_hue = layer.grading_shadows_hue;
  color_state.grading_shadows_saturation = layer.grading_shadows_saturation;
  color_state.grading_shadows_luminance = layer.grading_shadows_luminance;
  color_state.grading_midtones_hue = layer.grading_midtones_hue;
  color_state.grading_midtones_saturation = layer.grading_midtones_saturation;
  color_state.grading_midtones_luminance = layer.grading_midtones_luminance;
  color_state.grading_highlights_hue = layer.grading_highlights_hue;
  color_state.grading_highlights_saturation = layer.grading_highlights_saturation;
  color_state.grading_highlights_luminance = layer.grading_highlights_luminance;
  color_state.grading_amount = layer.grading_amount;
  color_state.grading_balance = layer.grading_balance;
  return color_state;
}

[[nodiscard]] bool has_photo_layer_color_adjustments(
    const ColorAdjustmentState& color_state,
    const std::vector<CompiledUserLut>& user_luts) {
  return has_rgb_channel_mixer_adjustments(color_state) || color_state.black_and_white_enabled ||
         has_post_tone_color_adjustments(color_state, 0.0, 0.0) || !user_luts.empty();
}

bool photo_layer_requires_gamut_fit(
    const PhotoLayer& layer,
    const PipelineColorContext& context) {
  const auto layer_state = photo_layer_color_adjustment_state(layer);
  return has_photo_layer_color_adjustments(
      layer_state,
      compile_user_lut_runtimes(
          layer_state,
          context.working_space,
          context.chromatic_adaptation_method));
}


[[nodiscard]] ColorPixel apply_photo_layer_color_adjustments(
    const ColorPixel& input,
    const PipelineColorContext& context,
    const ColorAdjustmentState& color_state,
    const std::vector<CompiledUserLut>& user_luts) {
  auto pixel = input;
  if (has_rgb_channel_mixer_adjustments(color_state)) {
    pixel = apply_rgb_channel_mixer(pixel, color_state.rgb_channel_mixer);
  }
  if (color_state.black_and_white_enabled) {
    pixel = apply_black_and_white_mixer(
        pixel,
        color_state,
        luminance_for_pixel(pixel, context));
  }
  pixel = apply_post_tone_color_adjustments(
      pixel,
      context,
      color_state,
      0.0,
      0.0,
      0.0,
      context.output_headroom);
  if (!user_luts.empty()) {
    pixel = apply_user_luts_to_pixel(pixel, user_luts);
  }
  pixel.alpha = input.alpha;
  return pixel;
}

[[nodiscard]] constexpr std::string_view photo_layer_effect_detail_order_contract() {
  return "photo-layer-effect-detail-order:v1|layer-recipe:tone->curves->color->effects-detail|effects-detail:before-mask-opacity-blend|mask-opacity-density:alpha-only|global-spatial-detail:after-layer-composite";
}

[[nodiscard]] bool has_photo_layer_effect_detail_adjustments(const PhotoLayer& layer) {
  return std::abs(layer.dehaze) > 1e-9 || std::abs(layer.texture) > 1e-9 ||
         std::abs(layer.clarity) > 1e-9 || std::abs(layer.sharpening) > 1e-9 ||
         std::abs(layer.luminance_nr) > 1e-9 || std::abs(layer.color_nr) > 1e-9;
}

[[nodiscard]] bool has_photo_layer_spatial_effect_detail_adjustments(const PhotoLayer& layer) {
  return std::abs(layer.texture) > 1e-9 || std::abs(layer.clarity) > 1e-9;
}

[[nodiscard]] bool has_photo_layer_optical_adjustments(const PhotoLayer& layer) {
  return std::abs(layer.lens_distortion) > 1e-9 ||
         std::abs(layer.lens_vignette) > 1e-9 ||
         std::abs(layer.chromatic_aberration) > 1e-9 ||
         std::abs(layer.perspective_vertical) > 1e-9 ||
         std::abs(layer.perspective_horizontal) > 1e-9 ||
         std::abs(layer.global_transform_rotation) > 1e-9 ||
         std::abs(layer.global_transform_scale - 1.0) > 1e-9 ||
         std::abs(layer.transform_skew_x) > 1e-9 ||
         std::abs(layer.transform_skew_y) > 1e-9 ||
         std::abs(layer.liquify_strength) > 1e-9;
}

[[nodiscard]] ColorAdjustmentState photo_layer_optical_adjustment_state(const PhotoLayer& layer) {
  ColorAdjustmentState state;
  state.lens_distortion = layer.lens_distortion;
  state.lens_vignette = layer.lens_vignette;
  state.chromatic_aberration = layer.chromatic_aberration;
  state.perspective_vertical = layer.perspective_vertical;
  state.perspective_horizontal = layer.perspective_horizontal;
  state.transform_rotation = layer.global_transform_rotation;
  state.transform_scale = layer.global_transform_scale;
  state.transform_skew_x = layer.transform_skew_x;
  state.transform_skew_y = layer.transform_skew_y;
  state.liquify_center_x = layer.liquify_center_x;
  state.liquify_center_y = layer.liquify_center_y;
  state.liquify_radius = layer.liquify_radius;
  state.liquify_strength = layer.liquify_strength;
  state.content_aware_crop_fill = layer.content_aware_crop_fill;
  return state;
}

struct PhotoLayerEffectDetailContext final {
  const std::vector<float>* luminance = nullptr;
  const std::vector<float>* texture_blur = nullptr;
  const std::vector<float>* clarity_blur = nullptr;
  int width = 0;
  int height = 0;
};

[[nodiscard]] bool valid_photo_layer_effect_detail_context(
    const PhotoLayerEffectDetailContext* detail_context,
    std::size_t index) {
  return detail_context != nullptr &&
         detail_context->luminance != nullptr &&
         detail_context->width > 0 &&
         detail_context->height > 0 &&
         index < detail_context->luminance->size();
}

[[nodiscard]] ColorPixel apply_photo_layer_effect_detail_adjustments(
    const ColorPixel& input,
    const PipelineColorContext& context,
    const PhotoLayer& layer,
    const PhotoLayerEffectDetailContext* detail_context = nullptr,
    std::size_t index = 0) {
  auto pixel = input;
  const auto dehaze_strength = clamp_signed_percent(layer.dehaze) / 100.0;
  if (std::abs(dehaze_strength) > 1e-9) {
    auto lab = linear_rgb_to_oklab(pixel, context);
    const auto chroma = std::hypot(lab.a, lab.b);
    if (chroma > 1e-6) {
      const auto scale = std::max(0.0, 1.0 + dehaze_strength * 0.10);
      lab.a *= scale;
      lab.b *= scale;
      pixel = clamp_rgb_to_output_headroom(oklab_to_linear_rgb(lab, pixel.alpha, context), context);
    }
  }

  const auto texture_strength = clamp_signed_percent(layer.texture) / 100.0;
  const auto clarity_strength = clamp_signed_percent(layer.clarity) / 100.0;
  if ((std::abs(texture_strength) > 1e-9 || std::abs(clarity_strength) > 1e-9) &&
      valid_photo_layer_effect_detail_context(detail_context, index)) {
    const auto& luminance_buffer = *detail_context->luminance;
    const auto safe_width = static_cast<std::size_t>(detail_context->width);
    const auto safe_height = static_cast<std::size_t>(detail_context->height);
    const auto x = index % safe_width;
    const auto y = index / safe_width;
    const auto luminance = static_cast<double>(luminance_buffer[index]);
    auto local_activity = 0.0;
    if (x > 0) {
      local_activity = std::max(
          local_activity,
          std::abs(luminance - static_cast<double>(luminance_buffer[index - 1])));
    }
    if (x + 1 < safe_width) {
      local_activity = std::max(
          local_activity,
          std::abs(luminance - static_cast<double>(luminance_buffer[index + 1])));
    }
    if (y > 0) {
      local_activity = std::max(
          local_activity,
          std::abs(luminance - static_cast<double>(luminance_buffer[index - safe_width])));
    }
    if (y + 1 < safe_height) {
      local_activity = std::max(
          local_activity,
          std::abs(luminance - static_cast<double>(luminance_buffer[index + safe_width])));
    }

    const auto midtone_weight = bell_weight(luminance, 0.5, 0.42);
    const auto shadow_guard = smoothstep(0.025, 0.12, luminance);
    const auto highlight_headroom = 1.0 - smoothstep(0.74, 0.98, luminance);
    const auto tone_guard = shadow_guard * highlight_headroom;
    auto target_luminance = luminance;

    if (std::abs(texture_strength) > 1e-9 &&
        detail_context->texture_blur != nullptr &&
        detail_context->clarity_blur != nullptr &&
        index < detail_context->texture_blur->size() &&
        index < detail_context->clarity_blur->size()) {
      const auto texture_signal =
          static_cast<double>(luminance_buffer[index] - (*detail_context->texture_blur)[index]);
      const auto broad_edge_signal =
          static_cast<double>(luminance_buffer[index] - (*detail_context->clarity_blur)[index]);
      const auto broad_edge_guard =
          1.0 - smoothstep(0.08, 0.36, std::abs(broad_edge_signal)) * 0.72;
      const auto texture_gain = texture_strength >= 0.0 ? 1.30 : 0.48;
      target_luminance +=
          texture_strength * texture_signal * (0.46 + midtone_weight * 0.70) *
          broad_edge_guard * tone_guard * texture_gain;
    }

    if (std::abs(clarity_strength) > 1e-9 &&
        detail_context->clarity_blur != nullptr &&
        index < detail_context->clarity_blur->size()) {
      const auto clarity_signal =
          static_cast<double>(luminance_buffer[index] - (*detail_context->clarity_blur)[index]);
      const auto edge_limit =
          1.0 - smoothstep(0.24, 0.58, std::abs(clarity_signal)) * 0.28;
      const auto structure_gate = smoothstep(0.003, 0.035, local_activity);
      const auto clarity_gain = clarity_strength >= 0.0 ? 1.08 : 0.46;
      target_luminance +=
          clarity_strength * clarity_signal * (0.36 + midtone_weight * 0.82) *
          tone_guard * edge_limit * structure_gate * clarity_gain;
    }

    pixel = scale_pixel_to_luminance_with_headroom(
        pixel,
        target_luminance,
        context.output_headroom,
        context);
  }
  pixel.alpha = input.alpha;
  return pixel;
}

void apply_photo_layer_sharpen_noise_adjustments(
    std::vector<ColorPixel>* pixels,
    int width,
    int height,
    const PipelineColorContext& context,
    const PhotoLayer& layer) {
  if (pixels == nullptr || pixels->empty() || width <= 0 || height <= 0) {
    return;
  }

  const auto sharpen_strength = clamp_signed_percent(layer.sharpening) / 100.0;
  const auto sharpen_radius = std::clamp(layer.sharpen_radius, 0.5, 3.0);
  const auto sharpen_detail = std::clamp(layer.sharpen_detail, 0.0, 100.0) / 100.0;
  const auto sharpen_mask_strength = clamp_signed_percent(layer.sharpen_masking) / 100.0;
  const auto luma_nr_strength = clamp_signed_percent(layer.luminance_nr) / 100.0;
  const auto color_nr_strength = clamp_signed_percent(layer.color_nr) / 100.0;
  if (std::abs(sharpen_strength) <= 1e-6 &&
      std::abs(luma_nr_strength) <= 1e-6 &&
      std::abs(color_nr_strength) <= 1e-6) {
    return;
  }

  const auto pixel_count = pixels->size();
  const auto safe_width = static_cast<std::size_t>(width);
  std::vector<float> lightness_buf(pixel_count);
  std::vector<float> chroma_a_buf(pixel_count);
  std::vector<float> chroma_b_buf(pixel_count);
  BlurWorkspace detail_blur_workspace;

  parallel_for(height, [&](int y) {
    const auto row_offset = static_cast<std::size_t>(y) * safe_width;
    for (int x = 0; x < width; ++x) {
      const auto index = row_offset + static_cast<std::size_t>(x);
      const auto ok = linear_rgb_to_oklab((*pixels)[index], context);
      lightness_buf[index] = static_cast<float>(ok.lightness);
      chroma_a_buf[index] = static_cast<float>(ok.a);
      chroma_b_buf[index] = static_cast<float>(ok.b);
    }
  });

  if (std::abs(sharpen_strength) > 1e-6) {
    const auto sharp_radius = std::max(1, static_cast<int>(std::lround(sharpen_radius)));
    const auto edge_radius =
        std::max(sharp_radius + 2, static_cast<int>(std::lround(sharpen_radius * 3.0)));
    const auto sharpen_gain = 1.9 + sharpen_detail * 1.15;
    std::vector<float> sharp_blur;
    gaussian_approximate_blur_into(
        lightness_buf,
        width,
        height,
        sharp_radius,
        &sharp_blur,
        &detail_blur_workspace);
    std::vector<float> edge_blur;
    if (sharpen_mask_strength > 1e-6) {
      gaussian_approximate_blur_into(
          lightness_buf,
          width,
          height,
          edge_radius,
          &edge_blur,
          &detail_blur_workspace);
    }

    parallel_for(height, [&](int y) {
      const auto row_offset = static_cast<std::size_t>(y) * safe_width;
      for (int x = 0; x < width; ++x) {
        const auto index = row_offset + static_cast<std::size_t>(x);
        const auto detail_signal = lightness_buf[index] - sharp_blur[index];
        auto mask_gate = 1.0;
        if (sharpen_mask_strength > 1e-6) {
          const auto edge_strength = std::abs(lightness_buf[index] - edge_blur[index]);
          mask_gate = smoothstep(
              0.0015,
              0.012 + 0.06 * sharpen_mask_strength,
              static_cast<double>(edge_strength));
        }
        lightness_buf[index] += static_cast<float>(
            static_cast<double>(detail_signal) * sharpen_strength * sharpen_gain * mask_gate);
      }
    });
  }

  if (std::abs(luma_nr_strength) > 1e-6) {
    std::vector<float> nr_blur;
    gaussian_approximate_blur_into(
        lightness_buf,
        width,
        height,
        2,
        &nr_blur,
        &detail_blur_workspace);
    std::vector<float> nr_edge_blur;
    gaussian_approximate_blur_into(
        lightness_buf,
        width,
        height,
        5,
        &nr_edge_blur,
        &detail_blur_workspace);

    parallel_for(height, [&](int y) {
      const auto row_offset = static_cast<std::size_t>(y) * safe_width;
      for (int x = 0; x < width; ++x) {
        const auto index = row_offset + static_cast<std::size_t>(x);
        const auto edge = std::abs(lightness_buf[index] - nr_edge_blur[index]);
        const auto edge_preserve = 1.0 - smoothstep(0.005, 0.04, static_cast<double>(edge));
        const auto blend = luma_nr_strength * 0.85 * edge_preserve;
        lightness_buf[index] = static_cast<float>(
            static_cast<double>(lightness_buf[index]) * (1.0 - blend) +
            static_cast<double>(nr_blur[index]) * blend);
      }
    });
  }

  if (std::abs(color_nr_strength) > 1e-6) {
    std::vector<float> a_blur;
    gaussian_approximate_blur_into(
        chroma_a_buf,
        width,
        height,
        3,
        &a_blur,
        &detail_blur_workspace);
    std::vector<float> b_blur;
    gaussian_approximate_blur_into(
        chroma_b_buf,
        width,
        height,
        3,
        &b_blur,
        &detail_blur_workspace);

    parallel_for(height, [&](int y) {
      const auto row_offset = static_cast<std::size_t>(y) * safe_width;
      const auto blend = color_nr_strength * 0.90;
      for (int x = 0; x < width; ++x) {
        const auto index = row_offset + static_cast<std::size_t>(x);
        chroma_a_buf[index] = static_cast<float>(
            static_cast<double>(chroma_a_buf[index]) * (1.0 - blend) +
            static_cast<double>(a_blur[index]) * blend);
        chroma_b_buf[index] = static_cast<float>(
            static_cast<double>(chroma_b_buf[index]) * (1.0 - blend) +
            static_cast<double>(b_blur[index]) * blend);
      }
    });
  }

  parallel_for(height, [&](int y) {
    const auto row_offset = static_cast<std::size_t>(y) * safe_width;
    for (int x = 0; x < width; ++x) {
      const auto index = row_offset + static_cast<std::size_t>(x);
      const auto lightness =
          static_cast<double>(lightness_buf[index]);
      const auto a = static_cast<double>(chroma_a_buf[index]);
      const auto b = static_cast<double>(chroma_b_buf[index]);
      (*pixels)[index] = oklab_to_linear_rgb(
          OklabColor{
              .lightness = lightness,
              .a = a,
              .b = b,
          },
          (*pixels)[index].alpha,
          context);
    }
  });
}

namespace {
bool run_layer_effects(std::vector<ColorPixel>* pixels, int width, int height,
    const PhotoLayer& layer, CancellationToken cancellation, std::string* error) {
  std::vector<PhotoEffectMaskBinding> masks;
  for (const auto& mask : layer.effect_masks) masks.push_back({mask.id, mask.coverage});
  const auto result = apply_photo_effect_stack_tiled(pixels, width, height, layer.effects,
      {.scale_x = layer.effect_scale_x, .scale_y = layer.effect_scale_y, .cancellation = cancellation}, masks);
  if (!result.ok() && error != nullptr) *error = "Layer '" + layer.name + "': " + result.error;
  return result.ok();
}

bool prepare_single_effect_content(PhotoLayer* layer, int width, int height,
    CancellationToken cancellation, std::string* error) {
  if (layer->kind == "fill") {
    layer->kind = "pixel";
    layer->pixel_width = width;
    layer->pixel_height = height;
    layer->x = 0;
    layer->y = 0;
    layer->transform_scale = 1;
    layer->transform_rotation = 0;
    layer->pixels.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), layer->fill_color);
  }
  if (layer->pixel_width <= 0 || layer->pixel_height <= 0 ||
      layer->pixels.size() != static_cast<std::size_t>(layer->pixel_width) * static_cast<std::size_t>(layer->pixel_height)) {
    if (error) *error = "Photo effect layer has no valid raster content.";
    return false;
  }
  PhotoEffectSupport support;
  const auto admission = photo_effect_stack_support(layer->effects,
      {.scale_x = layer->effect_scale_x, .scale_y = layer->effect_scale_y}, &support);
  if (!admission.ok()) { if (error) *error = admission.error; return false; }
  // Extent expansion is for transparent boundaries. Extending/reflecting image
  // edges should not create an infinite opaque rectangle around a cutout.
  const auto transparent = std::any_of(layer->effects.begin(), layer->effects.end(), [](const auto& effect) {
    const auto edge = effect.type_id == "box_blur" ? effect.box.edge_mode : effect.gaussian.edge_mode;
    return effect.enabled && effect.amount > 0 && edge == PhotoEffectEdgeMode::Transparent;
  });
  if (!transparent || (support.x == 0 && support.y == 0))
    return run_layer_effects(&layer->pixels, layer->pixel_width, layer->pixel_height, *layer, cancellation, error);
  const auto expanded_width = static_cast<std::int64_t>(layer->pixel_width) + 2LL * support.x;
  const auto expanded_height = static_cast<std::int64_t>(layer->pixel_height) + 2LL * support.y;
  constexpr std::int64_t max_pixels = 512LL * 1024 * 1024 / sizeof(ColorPixel);
  if (expanded_width <= 0 || expanded_height <= 0 || expanded_width > max_pixels / expanded_height) {
    if (error) *error = "Expanded effect content exceeds the raster budget.";
    return false;
  }
  const auto new_width = static_cast<int>(expanded_width), new_height = static_cast<int>(expanded_height);
  const auto count = static_cast<std::size_t>(new_width) * static_cast<std::size_t>(new_height);
  std::vector<ColorPixel> expanded(count, ColorPixel{0, 0, 0, 0});
  for (int y = 0; y < layer->pixel_height; ++y) {
    if (cancellation.cancelled()) return false;
    std::copy_n(layer->pixels.begin() + static_cast<std::ptrdiff_t>(y) * layer->pixel_width, layer->pixel_width,
        expanded.begin() + static_cast<std::ptrdiff_t>(y + support.y) * new_width + support.x);
  }
  for (auto& mask : layer->effect_masks) {
    if (mask.coverage.size() != layer->pixels.size()) { if (error) *error = "Content effect mask size mismatch."; return false; }
    std::vector<float> padded(count, 0);
    for (int y = 0; y < layer->pixel_height; ++y)
      std::copy_n(mask.coverage.begin() + static_cast<std::ptrdiff_t>(y) * layer->pixel_width, layer->pixel_width,
          padded.begin() + static_cast<std::ptrdiff_t>(y + support.y) * new_width + support.x);
    mask.coverage = std::move(padded);
  }
  layer->pixel_width = new_width;
  layer->pixel_height = new_height;
  layer->effect_padding_x += support.x;
  layer->effect_padding_y += support.y;
  layer->pixels = std::move(expanded);
  return run_layer_effects(&layer->pixels, new_width, new_height, *layer, cancellation, error);
}

bool prepare_effect_content(PhotoLayer* layer, int width, int height,
    CancellationToken cancellation, std::string* error) {
  auto effects = std::move(layer->effects);
  for (const auto& effect : effects) {
    if (!effect.enabled || effect.amount == 0) continue;
    layer->effects = {effect};
    if (!prepare_single_effect_content(layer, width, height, cancellation, error)) return false;
  }
  layer->effects = std::move(effects);
  return true;
}
}  // namespace

static bool apply_photo_layers_impl(
    std::vector<ColorPixel>* pixels,
    int width,
    int height,
    const PipelineColorContext& context,
    const std::vector<PhotoLayer>& layers,
    const PhotoLayerCommonAdjustment& apply_common_adjustment,
    bool common_adjustment_uses_layer_fields,
    bool common_adjustment_thread_safe, CancellationToken cancellation, std::string* error) {
  (void)photo_layer_effect_detail_order_contract();
  if (pixels == nullptr || width <= 0 || height <= 0 ||
      !apply_common_adjustment || pixels->size() != static_cast<std::size_t>(width) * static_cast<std::size_t>(height)) {
    return false;
  }

  const auto participates = [](const PhotoLayer& layer) {
    if (!layer.enabled || layer.opacity <= 1e-6 || layer.kind == "group") return false;
    if (layer.kind == "adjustment" || layer.kind == "fill") return true;
    return (layer.kind == "pixel" || layer.kind == "text" || layer.kind == "source") &&
        layer.pixel_width > 0 && layer.pixel_height > 0 &&
        layer.pixels.size() == static_cast<std::size_t>(layer.pixel_width * layer.pixel_height);
  };
  const auto pixel_count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
  std::vector<double> previous_layer_alpha(common_adjustment_uses_layer_fields ? 0 : pixel_count, 1.0);
  for (auto layer_it = layers.begin(); layer_it != layers.end(); ++layer_it) {
    if (cancellation.cancelled()) return false;
    const auto& original_layer = *layer_it;
    if (!original_layer.enabled || original_layer.opacity <= 1e-6) continue;
    std::optional<PhotoLayer> prepared_layer;
    if (original_layer.kind != "adjustment" && photo_layer_has_active_effects(original_layer)) {
      prepared_layer = original_layer;
      if (!prepare_effect_content(&*prepared_layer, width, height, cancellation, error)) return false;
    }
    const auto& layer = prepared_layer ? *prepared_layer : original_layer;
    if (layer.kind == "group") {
      continue;
    }
    if (!layer.enabled || layer.opacity <= 1e-6) {
      continue;
    }
    const auto next_layer = std::find_if(std::next(layer_it), layers.end(), participates);
    const auto retain_alpha = !common_adjustment_uses_layer_fields ||
        (next_layer != layers.end() && next_layer->clipping_mask);
    std::vector<double> current_layer_alpha(retain_alpha ? pixel_count : 0, 0.0);
    const auto opacity = clamp_unit(layer.opacity);
    if (layer.kind == "adjustment") {
      const std::array common_values{layer.exposure, layer.contrast, layer.highlights,
          layer.shadows, layer.whites, layer.blacks, layer.temperature, layer.tint,
          layer.vibrance, layer.saturation, layer.hue_shift, layer.saturation_shift,
          layer.colorize_strength};
      const auto has_common_adjustments = !common_adjustment_uses_layer_fields ||
          std::any_of(common_values.begin(), common_values.end(),
              [](double value) { return std::abs(value) > 1e-6; });
      const auto has_advanced_tone_adjustments = has_photo_layer_advanced_tone_adjustments(layer);
      const auto layer_tone = common_adjustment_uses_layer_fields && has_advanced_tone_adjustments
          ? std::optional{compile_photo_layer_advanced_tone(layer)} : std::nullopt;
      const auto has_curve_adjustments = has_photo_layer_curve_adjustments(layer);
      const auto layer_curves = common_adjustment_uses_layer_fields && has_curve_adjustments
          ? std::optional{compile_photo_layer_curves(layer)} : std::nullopt;
      const auto layer_color_state = photo_layer_color_adjustment_state(layer);
      const auto layer_user_luts = compile_user_lut_runtimes(
          layer_color_state,
          context.working_space,
          context.chromatic_adaptation_method);
      const auto has_color_adjustments =
          has_photo_layer_color_adjustments(layer_color_state, layer_user_luts);
      const auto has_effect_detail_adjustments = has_photo_layer_effect_detail_adjustments(layer);
      const auto has_spatial_effect_detail_adjustments =
          has_photo_layer_spatial_effect_detail_adjustments(layer);
      const auto has_optical_adjustments = has_photo_layer_optical_adjustments(layer);
      const auto has_creative_effects = photo_layer_has_active_effects(layer);
      const auto precompute_adjusted_layer =
          has_optical_adjustments || has_effect_detail_adjustments || has_creative_effects;
      const auto neutral_normal_layer = !has_common_adjustments &&
          !has_advanced_tone_adjustments && !has_curve_adjustments && !has_color_adjustments &&
          !precompute_adjusted_layer && photo_layer_uses_normal_blend(layer) &&
          !layer.clipping_mask && std::isfinite(layer.opacity) &&
          std::isfinite(layer.mask_opacity) && std::isfinite(layer.mask_density) &&
          std::all_of(pixels->begin(), pixels->end(), [](const ColorPixel& pixel) {
            return std::isfinite(pixel.red) && std::isfinite(pixel.green) && std::isfinite(pixel.blue);
          });
      if (neutral_normal_layer) {
        // A neutral normal adjustment leaves finite RGB and alpha unchanged.
        // Its coverage still matters if the next participating layer clips to it.
        if (retain_alpha) {
          for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x)
              current_layer_alpha[static_cast<std::size_t>(y * width + x)] =
                  opacity * photo_layer_mask_weight(layer, x, y, width, height);
        }
        previous_layer_alpha = std::move(current_layer_alpha);
        continue;
      }
      std::vector<ColorPixel> effect_detail_input_pixels;
      std::vector<float> effect_detail_luminance;
      std::vector<float> effect_detail_texture_blur;
      std::vector<float> effect_detail_clarity_blur;
      PhotoLayerEffectDetailContext effect_detail_context;
      const auto apply_layer_recipe_without_effect_detail =
          [&](const ColorPixel& source_pixel) {
            auto adjusted = source_pixel;
            if (has_common_adjustments) apply_common_adjustment(&adjusted, layer);
            if (has_advanced_tone_adjustments) {
              adjusted = apply_photo_layer_advanced_tone_adjustments(
                  adjusted, context, layer, layer_tone ? &*layer_tone : nullptr);
            }
            if (has_curve_adjustments) {
              adjusted = apply_photo_layer_curve_adjustments(
                  adjusted, context, layer, layer_curves ? &*layer_curves : nullptr);
            }
            if (has_color_adjustments) {
              adjusted = apply_photo_layer_color_adjustments(
                  adjusted,
                  context,
                  layer_color_state,
                  layer_user_luts);
            }
            return adjusted;
          };
      if (precompute_adjusted_layer) {
        effect_detail_input_pixels = *pixels;
        const auto prepare_row = [&](int y) {
          for (int x = 0; x < width; ++x) {
            auto& pixel = effect_detail_input_pixels[static_cast<std::size_t>(y * width + x)];
            pixel = apply_layer_recipe_without_effect_detail(pixel);
          }
        };
        if (common_adjustment_uses_layer_fields &&
            (!has_common_adjustments || common_adjustment_thread_safe)) {
          parallel_for(height, prepare_row);
        } else {
          for (int y = 0; y < height; ++y) prepare_row(y);
        }
        if (has_optical_adjustments) {
          auto optical_state = photo_layer_optical_adjustment_state(layer);
          apply_lens_vignette_correction(
              &effect_detail_input_pixels,
              width,
              height,
              optical_state);
          apply_geometry_corrections(
              &effect_detail_input_pixels,
              width,
              height,
              optical_state);
        }
        effect_detail_luminance.resize(effect_detail_input_pixels.size());
        for (std::size_t index = 0; index < effect_detail_input_pixels.size(); ++index) {
          effect_detail_luminance[index] =
              static_cast<float>(luminance_for_pixel(effect_detail_input_pixels[index], context));
        }
        BlurWorkspace effect_detail_blur_workspace;
        if (std::abs(layer.texture) > 1e-9) {
          guided_luminance_blur_into(
              effect_detail_luminance,
              width,
              height,
              2,
              0.00045,
              &effect_detail_texture_blur,
              &effect_detail_blur_workspace);
        }
        if (std::abs(layer.texture) > 1e-9 || std::abs(layer.clarity) > 1e-9) {
          guided_luminance_blur_into(
              effect_detail_luminance,
              width,
              height,
              8,
              0.0018,
              &effect_detail_clarity_blur,
              &effect_detail_blur_workspace);
        }
        effect_detail_context = PhotoLayerEffectDetailContext{
            .luminance = &effect_detail_luminance,
            .texture_blur = effect_detail_texture_blur.empty() ? nullptr : &effect_detail_texture_blur,
            .clarity_blur = effect_detail_clarity_blur.empty() ? nullptr : &effect_detail_clarity_blur,
            .width = width,
            .height = height,
        };
        if (has_effect_detail_adjustments) {
          for (std::size_t index = 0; index < effect_detail_input_pixels.size(); ++index) {
            effect_detail_input_pixels[index] = apply_photo_layer_effect_detail_adjustments(
                effect_detail_input_pixels[index],
                context,
                layer,
                has_spatial_effect_detail_adjustments ? &effect_detail_context : nullptr,
                index);
          }
          apply_photo_layer_sharpen_noise_adjustments(
              &effect_detail_input_pixels,
              width,
              height,
              context,
              layer);
        }
      }
      if (has_creative_effects && !run_layer_effects(&effect_detail_input_pixels, width, height, layer, cancellation, error)) return false;
      const auto apply_adjustment_row = [&](int y) {
        for (int x = 0; x < width; ++x) {
          const auto index = static_cast<std::size_t>(y * width + x);
          auto& pixel = (*pixels)[index];
          const auto clip_weight = layer.clipping_mask && !previous_layer_alpha.empty() ? previous_layer_alpha[index] : 1.0;
          const auto layer_weight =
              opacity * clip_weight * photo_layer_mask_weight(layer, x, y, width, height);
          if (retain_alpha) current_layer_alpha[index] = layer_weight;
          if (layer_weight <= 1e-6) {
            continue;
          }
          const auto before_layer = pixel;
          auto adjusted = precompute_adjusted_layer
                              ? effect_detail_input_pixels[index]
                              : apply_layer_recipe_without_effect_detail(before_layer);
          if (has_creative_effects) {
            auto blended = photo_layer_uses_normal_blend(layer) ? adjusted :
                blend_layer_color(before_layer, adjusted, layer.blend_mode, context);
            blended.alpha = adjusted.alpha;
            pixel = interpolate_effect_pixel(before_layer, blended, layer_weight);
          } else if (photo_layer_uses_normal_blend(layer)) {
            pixel = interpolate_adjustment_pixel(before_layer, adjusted, layer_weight);
          } else {
            pixel = interpolate_adjustment_pixel(before_layer,
                blend_layer_color(before_layer, adjusted, layer.blend_mode, context), layer_weight);
          }
        }
      };
      // Prepared layer inputs are immutable and rows write disjoint pixels.
      // Custom callbacks stay serial unless the caller explicitly opts in.
      if (common_adjustment_uses_layer_fields &&
          (!has_common_adjustments || common_adjustment_thread_safe)) {
        parallel_for(height, apply_adjustment_row);
      } else {
        for (int y = 0; y < height; ++y) apply_adjustment_row(y);
      }
      previous_layer_alpha = std::move(current_layer_alpha);
      continue;
    }
    if (layer.kind == "fill") {
      for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
          const auto index = static_cast<std::size_t>(y * width + x);
          const auto clip_weight = layer.clipping_mask && !previous_layer_alpha.empty() ? previous_layer_alpha[index] : 1.0;
          const auto alpha = clamp_unit(
              static_cast<double>(layer.fill_color.alpha) * opacity * clip_weight *
              photo_layer_mask_weight(layer, x, y, width, height));
          if (retain_alpha) current_layer_alpha[index] = alpha;
          if (alpha <= 1e-6) {
            continue;
          }
          auto& target = (*pixels)[index];
          composite_layer_pixel(&target, layer.fill_color, alpha, layer.blend_mode, context);
        }
      }
      previous_layer_alpha = std::move(current_layer_alpha);
      continue;
    }
    if ((layer.kind != "pixel" && layer.kind != "text" && layer.kind != "source") ||
        layer.pixel_width <= 0 || layer.pixel_height <= 0 ||
        layer.pixels.size() != static_cast<std::size_t>(layer.pixel_width * layer.pixel_height)) {
      continue;
    }
    if (!has_photo_layer_transform(layer)) {
      for (int layer_y = 0; layer_y < layer.pixel_height; ++layer_y) {
        const auto target_y = layer.y + layer_y - layer.effect_padding_y;
        if (target_y < 0 || target_y >= height) {
          continue;
        }
        for (int layer_x = 0; layer_x < layer.pixel_width; ++layer_x) {
          const auto target_x = layer.x + layer_x - layer.effect_padding_x;
          if (target_x < 0 || target_x >= width) {
            continue;
          }
          const auto source_index = static_cast<std::size_t>(layer_y * layer.pixel_width + layer_x);
          const auto target_index = static_cast<std::size_t>(target_y * width + target_x);
          const auto& source = layer.pixels[source_index];
          auto& target = (*pixels)[target_index];
          const auto clip_weight = layer.clipping_mask && !previous_layer_alpha.empty() ? previous_layer_alpha[target_index] : 1.0;
          const auto alpha = clamp_unit(
              static_cast<double>(source.alpha) * opacity * clip_weight *
              photo_layer_mask_weight(layer, target_x, target_y, width, height));
          if (retain_alpha) current_layer_alpha[target_index] = std::max(current_layer_alpha[target_index], alpha);
          if (alpha <= 1e-6) {
            continue;
          }
          composite_layer_pixel(&target, source, alpha, layer.blend_mode, context);
        }
      }
      previous_layer_alpha = std::move(current_layer_alpha);
      continue;
    }

    const auto scale = std::clamp(layer.transform_scale, 0.05, 32.0);
    const auto radians = -layer.transform_rotation * kPi / 180.0;
    const auto cos_rotation = std::cos(radians);
    const auto sin_rotation = std::sin(radians);
    const auto center_x = static_cast<double>(layer.x) - layer.effect_padding_x * scale +
                          (static_cast<double>(layer.pixel_width) * scale * 0.5);
    const auto center_y = static_cast<double>(layer.y) - layer.effect_padding_y * scale +
                          (static_cast<double>(layer.pixel_height) * scale * 0.5);
    for (int target_y = 0; target_y < height; ++target_y) {
      for (int target_x = 0; target_x < width; ++target_x) {
        const auto dx = static_cast<double>(target_x) + 0.5 - center_x;
        const auto dy = static_cast<double>(target_y) + 0.5 - center_y;
        const auto local_x =
            ((dx * cos_rotation - dy * sin_rotation) / scale) +
            (static_cast<double>(layer.pixel_width) * 0.5) - 0.5;
        const auto local_y =
            ((dx * sin_rotation + dy * cos_rotation) / scale) +
            (static_cast<double>(layer.pixel_height) * 0.5) - 0.5;
        const auto source = sample_layer_filtered(
            layer.pixels, layer.pixel_width, layer.pixel_height, local_x, local_y, scale);
        const auto target_index = static_cast<std::size_t>(target_y * width + target_x);
        auto& target = (*pixels)[target_index];
        const auto clip_weight = layer.clipping_mask && !previous_layer_alpha.empty() ? previous_layer_alpha[target_index] : 1.0;
        const auto alpha = clamp_unit(
            static_cast<double>(source.alpha) * opacity * clip_weight *
            photo_layer_mask_weight(layer, target_x, target_y, width, height));
        if (retain_alpha) current_layer_alpha[target_index] = std::max(current_layer_alpha[target_index], alpha);
        if (alpha <= 1e-6) {
          continue;
        }
        composite_layer_pixel(&target, source, alpha, layer.blend_mode, context);
      }
    }
    previous_layer_alpha = std::move(current_layer_alpha);
  }
  return !cancellation.cancelled();
}

bool apply_photo_layers(std::vector<ColorPixel>* pixels, int width, int height,
    const PipelineColorContext& context, const std::vector<PhotoLayer>& layers,
    const PhotoLayerCommonAdjustment& apply_common_adjustment,
    bool common_adjustment_uses_layer_fields, bool common_adjustment_thread_safe,
    CancellationToken cancellation, std::string* error) {
  if (error) error->clear();
  bool effects_active = false;
  for (const auto& layer : layers) {
    if (!layer.enabled || layer.opacity <= 1e-6) continue;
    effects_active = effects_active || photo_layer_has_active_effects(layer);
    if (!layer.effect_error.empty()) { if (error) *error = layer.effect_error; return false; }
    for (const auto& effect : layer.effects) {
      if (!effect.enabled || effect.amount == 0) continue;
      const auto expected = layer.kind == "adjustment" ? PhotoEffectInputScope::CompositeBelow : PhotoEffectInputScope::LayerContent;
      if (layer.kind == "group" || effect.input_scope != expected) {
        if (error) *error = "Photo effect input scope is incompatible with its layer.";
        return false;
      }
    }
  }
  if (pixels == nullptr || cancellation.cancelled()) return false;
  try {
    if (!effects_active) return apply_photo_layers_impl(pixels, width, height, context, layers,
        apply_common_adjustment, common_adjustment_uses_layer_fields, common_adjustment_thread_safe, cancellation, error);
    auto output = *pixels;
    if (!apply_photo_layers_impl(&output, width, height, context, layers, apply_common_adjustment,
            common_adjustment_uses_layer_fields, common_adjustment_thread_safe, cancellation, error)) return false;
    pixels->swap(output);
    return true;
  } catch (const std::bad_alloc&) {
    if (error) *error = "Could not allocate photo layer effect buffers.";
    return false;
  }
}

bool apply_standard_photo_layers(
    std::vector<ColorPixel>* pixels, int width, int height,
    const PipelineColorContext& context, PhotographicRenderingModel rendering_model,
    const std::vector<PhotoLayer>& layers, CancellationToken cancellation, std::string* error) {
  std::vector<std::optional<CompiledLocalAdjustment>> compiled;
  compiled.reserve(layers.size());
  for (const auto& layer : layers) {
    compiled.push_back(layer.enabled && layer.kind == "adjustment"
        ? compile_local_adjustment(context, rendering_model, LocalAdjustmentSettings{
            .exposure = layer.exposure, .contrast = layer.contrast,
            .highlights = layer.highlights, .shadows = layer.shadows,
            .whites = layer.whites, .blacks = layer.blacks,
            .temperature = layer.temperature, .tint = layer.tint,
            .vibrance = layer.vibrance, .saturation = layer.saturation,
            .hue_shift = layer.hue_shift, .saturation_shift = layer.saturation_shift,
            .colorize_hue = layer.colorize_hue, .colorize_strength = layer.colorize_strength})
        : std::nullopt);
  }
  return apply_photo_layers(pixels, width, height, context, layers,
      [&](ColorPixel* pixel, const PhotoLayer& layer) {
        const auto index = static_cast<std::size_t>(&layer - layers.data());
        const auto& prepared = compiled[index];
        apply_local_adjustment_mask(pixel, context, rendering_model, 1.0,
            layer.exposure, layer.contrast, layer.highlights, layer.shadows,
            layer.whites, layer.blacks, layer.temperature, layer.tint,
            layer.vibrance, layer.saturation, layer.hue_shift, layer.saturation_shift,
            layer.colorize_hue, layer.colorize_strength, prepared ? &*prepared : nullptr);
      }, true, true, cancellation, error);
}

}  // namespace photo_editor::pipeline
