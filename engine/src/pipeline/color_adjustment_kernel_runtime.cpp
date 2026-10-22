#include "color_adjustment_kernel_runtime.h"

#include "color_curve_evaluator.h"
#include "color_look_runtime.h"
#include "color_math_runtime.h"
#include "color_mixer_runtime.h"
#include "color_tone_runtime.h"
#include "color_working_space_runtime.h"

#include <algorithm>
#include <cmath>
#include <deque>
#include <mutex>
#include <numbers>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace photo_editor::pipeline {

constexpr double kPi = std::numbers::pi_v<double>;

[[nodiscard]] ColorPixel apply_global_color_pipeline(
    const ColorPixel& input,
    const PipelineColorContext& context,
    const InputStageParameters& input_stage,
    const ColorAdjustmentState& state,
    const CurveEvaluationData& parametric_curve,
    const CurveEvaluationData& master_curve,
    const CurveEvaluationData& luma_curve,
    const CurveEvaluationData& red_curve,
    const CurveEvaluationData& green_curve,
    const CurveEvaluationData& blue_curve,
    double saturation_strength,
    double vibrance_strength,
    double dehaze_strength_for_saturation,
    double shoulder_threshold,
    double output_headroom,
    const ToneLuminanceTable* tone_table = nullptr);

std::optional<CompiledLocalAdjustment> compile_local_adjustment(
    const PipelineColorContext& context,
    PhotographicRenderingModel photographic_rendering_model,
    const LocalAdjustmentSettings& settings) {
  if (std::abs(settings.hue_shift) > 1e-6 ||
      std::abs(settings.saturation_shift) > 1e-6 ||
      std::abs(settings.colorize_strength) > 1e-6) return std::nullopt;
  CompiledLocalAdjustment compiled;
  compiled.has_adjustments = std::abs(settings.exposure) > 1e-6 ||
      std::abs(settings.contrast) > 1e-6 || std::abs(settings.highlights) > 1e-6 ||
      std::abs(settings.shadows) > 1e-6 || std::abs(settings.whites) > 1e-6 ||
      std::abs(settings.blacks) > 1e-6 || std::abs(settings.temperature) > 1e-6 ||
      std::abs(settings.tint) > 1e-6 || std::abs(settings.vibrance) > 1e-6 ||
      std::abs(settings.saturation) > 1e-6;
  auto& state = compiled.state;
  state.input_transform.exposure = settings.exposure;
  state.input_transform.temperature = settings.temperature;
  state.input_transform.tint = settings.tint;
  state.contrast = settings.contrast;
  state.highlights = settings.highlights;
  state.shadows = settings.shadows;
  state.whites = settings.whites;
  state.blacks = settings.blacks;
  state.vibrance = settings.vibrance;
  state.saturation = settings.saturation;
  state.color_pipeline.output_headroom = context.output_headroom;
  state.color_pipeline.photographic_rendering_model = photographic_rendering_model;
  if (compiled.has_adjustments)
    compiled.input_stage = make_input_stage_parameters(state, context, nullptr);
  return compiled;
}

void apply_local_adjustment_mask(
    ColorPixel* pixel,
    const PipelineColorContext& context,
    PhotographicRenderingModel photographic_rendering_model,
    double weight,
    double exposure,
    double contrast,
    double highlights,
    double shadows,
    double whites,
    double blacks,
    double temperature,
    double tint,
    double vibrance,
    double saturation,
    double hue_shift,
    double saturation_shift,
    double colorize_hue,
    double colorize_strength,
    const CompiledLocalAdjustment* compiled) {
  if (pixel == nullptr || weight <= 0.0) {
    return;
  }

  const auto has_local_only_color =
      std::abs(hue_shift) > 1e-6 ||
      std::abs(saturation_shift) > 1e-6 ||
      std::abs(colorize_strength) > 1e-6;
  if (!has_local_only_color) {
    if (compiled != nullptr) {
      if (!compiled->has_adjustments) return;
      const auto empty_curve = CurveEvaluationData{};
      const auto adjusted = apply_global_color_pipeline(
          *pixel, context, compiled->input_stage, compiled->state,
          empty_curve, empty_curve, empty_curve, empty_curve, empty_curve, empty_curve,
          clamp_signed_percent(compiled->state.saturation) / 100.0,
          clamp_signed_percent(compiled->state.vibrance) / 100.0,
          0.0, context.output_headroom, context.output_headroom);
      const auto blend = clamp_unit(weight);
      pixel->red = static_cast<float>(std::lerp(
          static_cast<double>(pixel->red), static_cast<double>(adjusted.red), blend));
      pixel->green = static_cast<float>(std::lerp(
          static_cast<double>(pixel->green), static_cast<double>(adjusted.green), blend));
      pixel->blue = static_cast<float>(std::lerp(
          static_cast<double>(pixel->blue), static_cast<double>(adjusted.blue), blend));
      return;
    }
    const auto has_equivalent_adjustment =
        std::abs(exposure) > 1e-6 ||
        std::abs(contrast) > 1e-6 ||
        std::abs(highlights) > 1e-6 ||
        std::abs(shadows) > 1e-6 ||
        std::abs(whites) > 1e-6 ||
        std::abs(blacks) > 1e-6 ||
        std::abs(temperature) > 1e-6 ||
        std::abs(tint) > 1e-6 ||
        std::abs(vibrance) > 1e-6 ||
        std::abs(saturation) > 1e-6;
    if (!has_equivalent_adjustment) {
      return;
    }
    ColorAdjustmentState equivalent_state;
    equivalent_state.input_transform.exposure = exposure;
    equivalent_state.input_transform.temperature = temperature;
    equivalent_state.input_transform.tint = tint;
    equivalent_state.contrast = contrast;
    equivalent_state.highlights = highlights;
    equivalent_state.shadows = shadows;
    equivalent_state.whites = whites;
    equivalent_state.blacks = blacks;
    equivalent_state.vibrance = vibrance;
    equivalent_state.saturation = saturation;
    equivalent_state.color_pipeline.output_headroom =
        context.output_headroom;
    equivalent_state.color_pipeline.photographic_rendering_model =
        photographic_rendering_model;
    const auto input_stage = make_input_stage_parameters(
        equivalent_state, context, nullptr);
    const auto empty_curve = CurveEvaluationData{};
    const auto adjusted = apply_global_color_pipeline(
        *pixel,
        context,
        input_stage,
        equivalent_state,
        empty_curve,
        empty_curve,
        empty_curve,
        empty_curve,
        empty_curve,
        empty_curve,
        clamp_signed_percent(saturation) / 100.0,
        clamp_signed_percent(vibrance) / 100.0,
        0.0,
        context.output_headroom,
        context.output_headroom);
    const auto blend = clamp_unit(weight);
    pixel->red = static_cast<float>(std::lerp(
        static_cast<double>(pixel->red),
        static_cast<double>(adjusted.red),
        blend));
    pixel->green = static_cast<float>(std::lerp(
        static_cast<double>(pixel->green),
        static_cast<double>(adjusted.green),
        blend));
    pixel->blue = static_cast<float>(std::lerp(
        static_cast<double>(pixel->blue),
        static_cast<double>(adjusted.blue),
        blend));
    return;
  }

  const auto weighted_contrast = contrast * weight;
  const auto weighted_highlights = highlights * weight;
  const auto weighted_shadows = shadows * weight;
  const auto weighted_whites = whites * weight;
  const auto weighted_blacks = blacks * weight;
  const auto has_local_tone =
      std::abs(weighted_contrast) > 1e-6 ||
      std::abs(weighted_highlights) > 1e-6 ||
      std::abs(weighted_shadows) > 1e-6 ||
      std::abs(weighted_whites) > 1e-6 ||
      std::abs(weighted_blacks) > 1e-6;
  const auto weighted_vibrance = vibrance * weight;
  const auto weighted_saturation = saturation * weight;
  const auto has_local_chroma =
      std::abs(weighted_vibrance) > 1e-6 ||
      std::abs(weighted_saturation) > 1e-6 ||
      std::abs(hue_shift) > 1e-6 ||
      std::abs(saturation_shift) > 1e-6 ||
      std::abs(colorize_strength) > 1e-6;
  const auto has_local_white_balance =
      std::abs(temperature) > 1e-6 || std::abs(tint) > 1e-6;

  if (!has_local_white_balance && !has_local_tone && !has_local_chroma) {
    if (std::abs(exposure) > 1e-6) {
      const auto local_exposure_gain = std::pow(2.0, exposure * weight);
      pixel->red = static_cast<float>(static_cast<double>(pixel->red) * local_exposure_gain);
      pixel->green = static_cast<float>(static_cast<double>(pixel->green) * local_exposure_gain);
      pixel->blue = static_cast<float>(static_cast<double>(pixel->blue) * local_exposure_gain);
    }
    return;
  }

  const auto local_white_balance = white_balance_matrix(temperature * weight, tint * weight, context);
  const auto local_exposure_gain = std::pow(2.0, exposure * weight);
  const auto balanced = apply_rgb_matrix(*pixel, local_white_balance);
  pixel->red = static_cast<float>(std::lerp(static_cast<double>(pixel->red), static_cast<double>(balanced.red), clamp_unit(weight)));
  pixel->green = static_cast<float>(std::lerp(static_cast<double>(pixel->green), static_cast<double>(balanced.green), clamp_unit(weight)));
  pixel->blue = static_cast<float>(std::lerp(static_cast<double>(pixel->blue), static_cast<double>(balanced.blue), clamp_unit(weight)));
  pixel->red = static_cast<float>(static_cast<double>(pixel->red) * local_exposure_gain);
  pixel->green = static_cast<float>(static_cast<double>(pixel->green) * local_exposure_gain);
  pixel->blue = static_cast<float>(static_cast<double>(pixel->blue) * local_exposure_gain);

  if (has_local_tone) {
    ColorAdjustmentState local_tone_state;
    local_tone_state.color_pipeline.photographic_rendering_model =
        photographic_rendering_model;
    local_tone_state.contrast = weighted_contrast;
    local_tone_state.highlights = weighted_highlights;
    local_tone_state.shadows = weighted_shadows;
    local_tone_state.whites = weighted_whites;
    local_tone_state.blacks = weighted_blacks;
    const auto source_pixel = *pixel;
    *pixel = scale_pixel_to_luminance(
        source_pixel,
        apply_global_tone(luminance_for_pixel(source_pixel, context), local_tone_state),
        context);
  }

  if (!has_local_chroma) {
    return;
  }

  auto oklab = linear_rgb_to_oklab(*pixel, context);
  auto lightness = oklab.lightness;
  auto chroma = std::hypot(oklab.a, oklab.b);
  auto hue = wrap_radians(std::atan2(oklab.b, oklab.a));

  if (chroma <= 1e-6) {
    hue = wrap_unit(colorize_hue) * 2.0 * kPi;
  } else {
    const auto low_chroma_weight = 1.0 - smoothstep(0.035, 0.24, chroma);
    chroma *= std::max(
        0.0,
        1.0 + (clamp_signed_percent(weighted_vibrance) / 100.0) *
                  low_chroma_weight * 0.82);
    const auto weighted_saturation_control = clamp_signed_percent(weighted_saturation) / 100.0;
    if (weighted_saturation_control >= 0.0) {
      chroma *= std::max(
          0.0,
          1.0 + weighted_saturation_control * 0.85);
    } else {
      chroma *= std::max(0.0, 1.0 + weighted_saturation_control);
    }
    chroma *= highlight_chroma_compression_scale(lightness, chroma);
  }

  hue = wrap_radians(hue + (clamp_signed_percent(hue_shift) / 100.0) * kPi * weight);
  chroma *= std::max(
      0.0,
      1.0 + (clamp_signed_percent(saturation_shift) / 100.0) * 0.92 * weight);

  const auto colorize_amount = clamp_unit(colorize_strength / 100.0) * weight;
  if (colorize_amount > 1e-6) {
    const auto target_hue = wrap_unit(colorize_hue) * 2.0 * kPi;
    hue = wrap_radians(hue + shortest_angular_delta(hue, target_hue) * colorize_amount);
    chroma = std::lerp(chroma, std::max(chroma, 0.18), colorize_amount);
  }

  *pixel = oklab_to_linear_rgb(
      OklabColor{
          .lightness = lightness,
          .a = chroma * std::cos(hue),
          .b = chroma * std::sin(hue),
      },
      pixel->alpha,
      context);
}

double clamp_signed_percent(double value) { return std::clamp(value, -100.0, 100.0); }

double clamp_unit(double value) { return std::clamp(value, 0.0, 1.0); }

double perceptual_delta(
    const ColorPixel& left,
    const ColorPixel& right,
    const RgbColorSpace& color_space,
    std::string_view chromatic_adaptation_method) {
  ColorAdjustmentState state;
  state.color_pipeline.working_profile.profile_name = color_space.name;
  state.color_pipeline.working_profile.color_space = color_space;
  state.input_transform.chromatic_adaptation_method = std::string(chromatic_adaptation_method);
  const auto context = make_pipeline_color_context(state);
  const auto left_lab = linear_rgb_to_oklab(left, context);
  const auto right_lab = linear_rgb_to_oklab(right, context);
  const auto delta_l = left_lab.lightness - right_lab.lightness;
  const auto delta_a = left_lab.a - right_lab.a;
  const auto delta_b = left_lab.b - right_lab.b;
  return std::sqrt(delta_l * delta_l + delta_a * delta_a + delta_b * delta_b);
}


[[nodiscard]] ColorPixel apply_master_rgb_point_curve(
    const ColorPixel& pixel,
    const CurveEvaluationData& curve) {
  if (curve.points.size() < 2) {
    return pixel;
  }

  // The composite RGB curve is a display-referred curve applied to all three
  // working RGB channels. This intentionally differs from the Luma curve,
  // which remaps Y and preserves chromatic ratios. Keeping that distinction is
  // also required for CPU/export parity with the Metal post-color kernel.
  return ColorPixel{
      .red = static_cast<float>(apply_perceptual_point_curve(pixel.red, curve)),
      .green = static_cast<float>(apply_perceptual_point_curve(pixel.green, curve)),
      .blue = static_cast<float>(apply_perceptual_point_curve(pixel.blue, curve)),
      .alpha = pixel.alpha,
  };
}

[[nodiscard]] ColorPixel apply_luminance_point_curve(
    const ColorPixel& pixel,
    const CurveEvaluationData& curve,
    double output_headroom,
    const PipelineColorContext& context) {
  if (curve.points.size() < 2) {
    return pixel;
  }
  const auto source_luminance = luminance_for_pixel(pixel, context);
  const auto target_luminance = apply_perceptual_point_curve(source_luminance, curve);
  return scale_pixel_to_luminance_with_headroom(
      pixel,
      target_luminance,
      output_headroom,
      context);
}



ColorPixel apply_global_color_pipeline(
    const ColorPixel& input,
    const PipelineColorContext& context,
    const InputStageParameters& input_stage,
    const ColorAdjustmentState& state,
    const CurveEvaluationData& parametric_curve,
    const CurveEvaluationData& master_curve,
    const CurveEvaluationData& luma_curve,
    const CurveEvaluationData& red_curve,
    const CurveEvaluationData& green_curve,
    const CurveEvaluationData& blue_curve,
    double saturation_strength,
    double vibrance_strength,
    double dehaze_strength_for_saturation,
    double shoulder_threshold,
    double output_headroom,
    const ToneLuminanceTable* tone_table) {
  auto pixel = apply_input_stage(input, input_stage, context);

  const auto source_luminance =
      luminance_for_pixel(pixel, context);
  auto target_luminance =
      tone_table != nullptr
          ? evaluate_tone_luminance_table(
                *tone_table,
                source_luminance,
                state)
          : apply_global_tone(source_luminance, state);
  if (shoulder_threshold < output_headroom) {
    target_luminance = apply_highlight_shoulder(
        target_luminance,
        shoulder_threshold,
        output_headroom,
        state.filmic_shoulder);
  }
  const auto curve_target = evaluate_tone_curve_target(
      target_luminance,
      parametric_curve,
      has_point_curve(state.master_curve_points),
      output_headroom);
  pixel = scale_pixel_for_global_tone(
      pixel,
      curve_target,
      output_headroom,
      context);

  if (master_curve.points.size() >= 2) {
    pixel = apply_master_rgb_point_curve(pixel, master_curve);
  }
  if (luma_curve.points.size() >= 2) {
    pixel = apply_luminance_point_curve(pixel, luma_curve, output_headroom, context);
  }
  if (red_curve.points.size() >= 2) {
    pixel.red = static_cast<float>(apply_perceptual_point_curve(pixel.red, red_curve));
  }
  if (green_curve.points.size() >= 2) {
    pixel.green = static_cast<float>(apply_perceptual_point_curve(pixel.green, green_curve));
  }
  if (blue_curve.points.size() >= 2) {
    pixel.blue = static_cast<float>(apply_perceptual_point_curve(pixel.blue, blue_curve));
  }
  if (has_rgb_channel_mixer_adjustments(state)) {
    pixel = apply_rgb_channel_mixer(pixel, state.rgb_channel_mixer);
  }
  if (state.black_and_white_enabled) {
    pixel = apply_black_and_white_mixer(
        pixel,
        pixel_to_linear_srgb_reference(pixel, context),
        state,
        luminance_for_pixel(pixel, context));
  }
  return apply_post_tone_color_adjustments(
      pixel,
      context,
      state,
      saturation_strength,
      vibrance_strength,
      dehaze_strength_for_saturation,
      output_headroom);
}

ColorLut3D build_color_lut(const ColorAdjustmentState& state) {
  const auto curve_points_equal = [](std::span<const CurvePoint> left, std::span<const CurvePoint> right) {
    if (left.size() != right.size()) {
      return false;
    }
    for (std::size_t i = 0; i < left.size(); ++i) {
      if (std::abs(left[i].x - right[i].x) > 1e-9 || std::abs(left[i].y - right[i].y) > 1e-9) {
        return false;
      }
    }
    return true;
  };
  const auto lut_inputs_equal = [&](const ColorAdjustmentState& left, const ColorAdjustmentState& right) {
    const auto equal = [](double a, double b) { return std::abs(a - b) <= 1e-9; };
    const auto equal_matrix = [&](
                                  const std::array<double, 9>& a,
                                  const std::array<double, 9>& b) {
      return std::equal(
          a.cbegin(),
          a.cend(),
          b.cbegin(),
          [&](double left_value, double right_value) {
            return equal(left_value, right_value);
          });
    };
    const auto equal_display_target = [&](const DisplayTarget& a, const DisplayTarget& b) {
      return a.target_name == b.target_name &&
             a.color_space.name == b.color_space.name &&
             a.color_space.primaries == b.color_space.primaries &&
             a.color_space.transfer_function == b.color_space.transfer_function &&
             a.color_space.white_point == b.color_space.white_point &&
             a.eotf == b.eotf &&
             equal(a.peak_luminance_nits, b.peak_luminance_nits) &&
             equal(a.black_luminance_nits, b.black_luminance_nits) &&
             equal(a.reference_white_nits, b.reference_white_nits) &&
             equal(a.surround_luminance_nits, b.surround_luminance_nits) &&
             a.surround == b.surround &&
             a.mastering_mode == b.mastering_mode;
    };
    if (left.input_transform.input_profile_name != right.input_transform.input_profile_name ||
        !equal(left.input_transform.exposure, right.input_transform.exposure) ||
        !equal(left.input_transform.temperature, right.input_transform.temperature) ||
        !equal(left.input_transform.tint, right.input_transform.tint) ||
        left.input_transform.raw_input_transform_enabled != right.input_transform.raw_input_transform_enabled ||
        left.input_transform.highlight_recovery_enabled != right.input_transform.highlight_recovery_enabled ||
        left.input_transform.highlight_reconstruction_mode !=
            right.input_transform.highlight_reconstruction_mode ||
        left.input_transform.highlight_reconstruction_model !=
            right.input_transform.highlight_reconstruction_model ||
        left.input_transform.chromatic_adaptation_method !=
            right.input_transform.chromatic_adaptation_method ||
        left.input_transform.raw_white_balance_override_enabled !=
            right.input_transform.raw_white_balance_override_enabled ||
        left.input_transform.camera_matching_look != right.input_transform.camera_matching_look ||
        !equal(left.input_transform.raw_white_balance_temperature,
               right.input_transform.raw_white_balance_temperature) ||
        !equal(left.input_transform.raw_white_balance_tint, right.input_transform.raw_white_balance_tint) ||
        !equal(left.input_transform.raw_black_level, right.input_transform.raw_black_level) ||
        !equal(left.input_transform.raw_white_level, right.input_transform.raw_white_level) ||
        !equal(left.input_transform.calibration_hue, right.input_transform.calibration_hue) ||
        !equal(left.input_transform.calibration_saturation, right.input_transform.calibration_saturation) ||
        !equal(left.input_transform.red_primary_hue, right.input_transform.red_primary_hue) ||
        !equal(left.input_transform.red_primary_saturation, right.input_transform.red_primary_saturation) ||
        !equal(left.input_transform.green_primary_hue, right.input_transform.green_primary_hue) ||
        !equal(left.input_transform.green_primary_saturation, right.input_transform.green_primary_saturation) ||
        !equal(left.input_transform.blue_primary_hue, right.input_transform.blue_primary_hue) ||
        !equal(left.input_transform.blue_primary_saturation, right.input_transform.blue_primary_saturation) ||
        !equal(left.input_transform.source_area_x, right.input_transform.source_area_x) ||
        !equal(left.input_transform.source_area_y, right.input_transform.source_area_y) ||
        !equal(left.input_transform.source_area_width, right.input_transform.source_area_width) ||
        !equal(left.input_transform.source_area_height, right.input_transform.source_area_height) ||
        left.color_pipeline.input_profile.profile_name != right.color_pipeline.input_profile.profile_name ||
        left.color_pipeline.input_profile.kind != right.color_pipeline.input_profile.kind ||
        left.color_pipeline.camera_profile_rendering_model !=
            right.color_pipeline.camera_profile_rendering_model ||
        left.color_pipeline.camera_profile.profile_name != right.color_pipeline.camera_profile.profile_name ||
        left.color_pipeline.camera_profile.profile_format != right.color_pipeline.camera_profile.profile_format ||
        left.color_pipeline.camera_profile.valid != right.color_pipeline.camera_profile.valid ||
        left.color_pipeline.camera_profile.selection_source !=
            right.color_pipeline.camera_profile.selection_source ||
        left.color_pipeline.camera_profile.selection_precedence_rank !=
            right.color_pipeline.camera_profile.selection_precedence_rank ||
        left.color_pipeline.camera_profile.fallback_used !=
            right.color_pipeline.camera_profile.fallback_used ||
        left.color_pipeline.camera_profile.payload_sha256 !=
            right.color_pipeline.camera_profile.payload_sha256 ||
        left.color_pipeline.camera_profile.payload_byte_size !=
            right.color_pipeline.camera_profile.payload_byte_size ||
        left.color_pipeline.camera_profile.dual_illuminant != right.color_pipeline.camera_profile.dual_illuminant ||
        left.color_pipeline.camera_profile.triple_illuminant != right.color_pipeline.camera_profile.triple_illuminant ||
        left.color_pipeline.camera_profile.illuminant_data_kinds !=
            right.color_pipeline.camera_profile.illuminant_data_kinds ||
        left.color_pipeline.camera_profile.execution_model !=
            right.color_pipeline.camera_profile.execution_model ||
        !equal(left.color_pipeline.camera_profile.second_illuminant_weight,
               right.color_pipeline.camera_profile.second_illuminant_weight) ||
        !std::equal(
            left.color_pipeline.camera_profile.illuminant_weights.cbegin(),
            left.color_pipeline.camera_profile.illuminant_weights.cend(),
            right.color_pipeline.camera_profile.illuminant_weights.cbegin(),
            [&equal](double left_weight, double right_weight) {
              return equal(left_weight, right_weight);
            }) ||
        left.color_pipeline.camera_profile.used_forward_matrix !=
            right.color_pipeline.camera_profile.used_forward_matrix ||
        left.color_pipeline.camera_profile.used_camera_calibration !=
            right.color_pipeline.camera_profile.used_camera_calibration ||
        left.color_pipeline.camera_profile.has_hue_sat_map !=
            right.color_pipeline.camera_profile.has_hue_sat_map ||
        left.color_pipeline.camera_profile.has_profile_gain_table_map !=
            right.color_pipeline.camera_profile.has_profile_gain_table_map ||
        left.color_pipeline.camera_profile.has_rgb_tables !=
            right.color_pipeline.camera_profile.has_rgb_tables ||
        left.color_pipeline.camera_profile.has_look_table !=
            right.color_pipeline.camera_profile.has_look_table ||
        left.color_pipeline.camera_profile.has_profile_tone_curve !=
            right.color_pipeline.camera_profile.has_profile_tone_curve ||
        left.color_pipeline.camera_profile.profile_tone_method !=
            right.color_pipeline.camera_profile.profile_tone_method ||
        left.color_pipeline.camera_profile.profile_dynamic_range_hdr !=
            right.color_pipeline.camera_profile.profile_dynamic_range_hdr ||
        !equal(left.color_pipeline.camera_profile.profile_hint_max_output_value,
               right.color_pipeline.camera_profile.profile_hint_max_output_value) ||
        !equal(left.color_pipeline.camera_profile.baseline_exposure_offset,
               right.color_pipeline.camera_profile.baseline_exposure_offset) ||
        !equal_matrix(left.color_pipeline.camera_profile.camera_to_xyz_d50,
                      right.color_pipeline.camera_profile.camera_to_xyz_d50) ||
        !equal_matrix(left.color_pipeline.camera_profile.camera_to_xyz_d50_tungsten,
                      right.color_pipeline.camera_profile.camera_to_xyz_d50_tungsten) ||
        !equal_matrix(left.color_pipeline.camera_profile.camera_to_xyz_d50_daylight,
                      right.color_pipeline.camera_profile.camera_to_xyz_d50_daylight) ||
        left.color_pipeline.camera_dcp_profile_asset.reference.asset_id !=
            right.color_pipeline.camera_dcp_profile_asset.reference.asset_id ||
        left.color_pipeline.camera_dcp_profile_asset.reference.payload_sha256 !=
            right.color_pipeline.camera_dcp_profile_asset.reference.payload_sha256 ||
        left.color_pipeline.camera_dcp_profile_asset.reference.byte_size !=
            right.color_pipeline.camera_dcp_profile_asset.reference.byte_size ||
        !equal(left.color_pipeline.input_transform_recipe.source_temperature,
               right.color_pipeline.input_transform_recipe.source_temperature) ||
        left.color_pipeline.input_transform_recipe.decoder_white_balance_applied !=
            right.color_pipeline.input_transform_recipe.decoder_white_balance_applied ||
        !equal(left.color_pipeline.input_transform_recipe.source_tint,
               right.color_pipeline.input_transform_recipe.source_tint) ||
        !equal(left.color_pipeline.input_transform_recipe.baseline_exposure,
               right.color_pipeline.input_transform_recipe.baseline_exposure) ||
        !equal(left.color_pipeline.input_transform_recipe.highlight_headroom,
               right.color_pipeline.input_transform_recipe.highlight_headroom) ||
        left.color_pipeline.working_profile.profile_name != right.color_pipeline.working_profile.profile_name ||
        left.color_pipeline.preview_output_profile.profile_name !=
            right.color_pipeline.preview_output_profile.profile_name ||
        left.color_pipeline.export_output_profile.profile_name !=
            right.color_pipeline.export_output_profile.profile_name ||
        left.color_pipeline.photographic_rendering_model !=
            right.color_pipeline.photographic_rendering_model ||
        !equal_display_target(
            left.color_pipeline.preview_display_target,
            right.color_pipeline.preview_display_target) ||
        !equal_display_target(
            left.color_pipeline.export_display_target,
            right.color_pipeline.export_display_target) ||
        left.color_pipeline.render_intent != right.color_pipeline.render_intent ||
        left.color_pipeline.output_gamut_mapping_enabled !=
            right.color_pipeline.output_gamut_mapping_enabled ||
        left.color_pipeline.output_gamut_mapping_policy !=
            right.color_pipeline.output_gamut_mapping_policy ||
        !equal(left.color_pipeline.output_headroom, right.color_pipeline.output_headroom) ||
        !equal(left.contrast, right.contrast) ||
        !equal(left.highlights, right.highlights) ||
        !equal(left.shadows, right.shadows) ||
        !equal(left.whites, right.whites) ||
        !equal(left.blacks, right.blacks) ||
        !equal(left.brightness, right.brightness) ||
        !equal(left.hdr_local_tone_mapping, right.hdr_local_tone_mapping) ||
        !equal(left.filmic_shoulder, right.filmic_shoulder) ||
        !equal(left.filmic_toe, right.filmic_toe) ||
        !equal(left.adaptive_contrast, right.adaptive_contrast) ||
        !equal(left.levels_black_point, right.levels_black_point) ||
        !equal(left.levels_gamma, right.levels_gamma) ||
        !equal(left.levels_white_point, right.levels_white_point) ||
        !equal(left.zone_deep_shadows, right.zone_deep_shadows) ||
        !equal(left.zone_shadows, right.zone_shadows) ||
        !equal(left.zone_midtones, right.zone_midtones) ||
        !equal(left.zone_lights, right.zone_lights) ||
        !equal(left.zone_highlights, right.zone_highlights) ||
        !equal(left.dehaze, right.dehaze) ||
        !equal(left.vibrance, right.vibrance) ||
        !equal(left.saturation, right.saturation) ||
        !equal(left.curve_shadows, right.curve_shadows) ||
        !equal(left.curve_darks, right.curve_darks) ||
        !equal(left.curve_lights, right.curve_lights) ||
        !equal(left.curve_highlights, right.curve_highlights) ||
        left.color_mixer_model != right.color_mixer_model ||
        left.color_mixer_channel != right.color_mixer_channel ||
        !equal(left.hue_adjustment, right.hue_adjustment) ||
        !equal(left.saturation_adjustment, right.saturation_adjustment) ||
        !equal(left.luminance_adjustment, right.luminance_adjustment) ||
        !equal(left.rgb_channel_mixer.red_from_red, right.rgb_channel_mixer.red_from_red) ||
        !equal(left.rgb_channel_mixer.red_from_green, right.rgb_channel_mixer.red_from_green) ||
        !equal(left.rgb_channel_mixer.red_from_blue, right.rgb_channel_mixer.red_from_blue) ||
        !equal(left.rgb_channel_mixer.green_from_red, right.rgb_channel_mixer.green_from_red) ||
        !equal(left.rgb_channel_mixer.green_from_green, right.rgb_channel_mixer.green_from_green) ||
        !equal(left.rgb_channel_mixer.green_from_blue, right.rgb_channel_mixer.green_from_blue) ||
        !equal(left.rgb_channel_mixer.blue_from_red, right.rgb_channel_mixer.blue_from_red) ||
        !equal(left.rgb_channel_mixer.blue_from_green, right.rgb_channel_mixer.blue_from_green) ||
        !equal(left.rgb_channel_mixer.blue_from_blue, right.rgb_channel_mixer.blue_from_blue) ||
        left.black_and_white_enabled != right.black_and_white_enabled ||
        left.color_replacement_enabled != right.color_replacement_enabled ||
        !equal(left.color_replacement_source_hue, right.color_replacement_source_hue) ||
        !equal(left.color_replacement_target_hue, right.color_replacement_target_hue) ||
        !equal(left.color_replacement_range, right.color_replacement_range) ||
        !equal(left.color_replacement_softness, right.color_replacement_softness) ||
        !equal(left.color_replacement_strength, right.color_replacement_strength) ||
        left.color_replacement_preserve_luminance != right.color_replacement_preserve_luminance ||
        left.color_replacement_preserve_saturation != right.color_replacement_preserve_saturation ||
        left.skin_tone_protection_enabled != right.skin_tone_protection_enabled ||
        !equal(left.skin_tone_line_hue, right.skin_tone_line_hue) ||
        !equal(left.skin_tone_protection_range, right.skin_tone_protection_range) ||
        !equal(left.skin_tone_protection_strength, right.skin_tone_protection_strength) ||
        left.color_harmony_enabled != right.color_harmony_enabled ||
        left.color_harmony_mode != right.color_harmony_mode ||
        !equal(left.color_harmony_base_hue, right.color_harmony_base_hue) ||
        !equal(left.color_harmony_strength, right.color_harmony_strength) ||
        !equal(left.grading_shadows_hue, right.grading_shadows_hue) ||
        !equal(left.grading_shadows_saturation, right.grading_shadows_saturation) ||
        !equal(left.grading_shadows_luminance, right.grading_shadows_luminance) ||
        !equal(left.grading_midtones_hue, right.grading_midtones_hue) ||
        !equal(left.grading_midtones_saturation, right.grading_midtones_saturation) ||
        !equal(left.grading_midtones_luminance, right.grading_midtones_luminance) ||
        !equal(left.grading_highlights_hue, right.grading_highlights_hue) ||
        !equal(left.grading_highlights_saturation, right.grading_highlights_saturation) ||
        !equal(left.grading_highlights_luminance, right.grading_highlights_luminance) ||
        !equal(left.grading_amount, right.grading_amount) ||
        !equal(left.grading_balance, right.grading_balance) ||
        !curve_points_equal(left.master_curve_points, right.master_curve_points) ||
        !curve_points_equal(left.luma_curve_points, right.luma_curve_points) ||
        !curve_points_equal(left.red_curve_points, right.red_curve_points) ||
        !curve_points_equal(left.green_curve_points, right.green_curve_points) ||
        !curve_points_equal(left.blue_curve_points, right.blue_curve_points)) {
      return false;
    }

    for (std::size_t i = 0; i < left.color_mixer_channels.size(); ++i) {
      if (!equal(left.color_mixer_channels[i].hue, right.color_mixer_channels[i].hue) ||
          !equal(left.color_mixer_channels[i].saturation, right.color_mixer_channels[i].saturation) ||
          !equal(left.color_mixer_channels[i].luminance, right.color_mixer_channels[i].luminance) ||
          !equal(left.color_mixer_channels[i].selective_cyan, right.color_mixer_channels[i].selective_cyan) ||
          !equal(left.color_mixer_channels[i].selective_magenta, right.color_mixer_channels[i].selective_magenta) ||
          !equal(left.color_mixer_channels[i].selective_yellow, right.color_mixer_channels[i].selective_yellow) ||
          !equal(left.color_mixer_channels[i].selective_black, right.color_mixer_channels[i].selective_black)) {
        return false;
      }
    }
    for (std::size_t i = 0; i < left.black_and_white_mixer.size(); ++i) {
      if (!equal(left.black_and_white_mixer[i], right.black_and_white_mixer[i])) {
        return false;
      }
    }
    return true;
  };
  struct LutCacheEntry final {
    ColorAdjustmentState state;
    ColorLut3D lut;
  };
  struct LutCache final {
    std::mutex mutex;
    std::deque<LutCacheEntry> entries;
  };
  constexpr std::size_t kMaximumCachedColorLuts = 4;
  static LutCache cache;
  static std::uint64_t next_cache_identity = 0U;
  std::unique_lock cache_lock(cache.mutex);
  const auto cached = std::find_if(
      cache.entries.begin(),
      cache.entries.end(),
      [&](const LutCacheEntry& entry) {
        return lut_inputs_equal(entry.state, state);
      });
  if (cached != cache.entries.end()) {
    auto lut = cached->lut;
    if (cached != cache.entries.begin()) {
      auto entry = std::move(*cached);
      cache.entries.erase(cached);
      cache.entries.push_front(std::move(entry));
    }
    return lut;
  }

  ColorLut3D lut;
  lut.size = kLutSize;
  const auto total = static_cast<std::size_t>(kLutSize * kLutSize * kLutSize);
  lut.table.resize(total);

  const auto color_context = make_pipeline_color_context(state);
  const auto input_stage = make_input_stage_parameters(state, color_context);
  const auto parametric_curve = build_parametric_curve_data(state);
  const auto master_curve = build_curve_data(state.master_curve_points);
  const auto luma_curve = build_curve_data(state.luma_curve_points);
  const auto red_curve = build_curve_data(state.red_curve_points);
  const auto green_curve = build_curve_data(state.green_curve_points);
  const auto blue_curve = build_curve_data(state.blue_curve_points);
  const auto saturation_strength = clamp_signed_percent(state.saturation) / 100.0;
  const auto vibrance_strength = clamp_signed_percent(state.vibrance) / 100.0;
  const auto dehaze_strength = clamp_signed_percent(state.dehaze) / 100.0;

  // Pre-pass: find max target luminance across all LUT samples for consistent
  // highlight shoulder rolloff (matches the slow-path global approach).
  auto max_target_luminance = 0.0;
  for (int ri = 0; ri < kLutSize; ++ri) {
    for (int gi = 0; gi < kLutSize; ++gi) {
      for (int bi = 0; bi < kLutSize; ++bi) {
        const auto r = static_cast<double>(ri) / static_cast<double>(kLutSize - 1);
        const auto g = static_cast<double>(gi) / static_cast<double>(kLutSize - 1);
        const auto b = static_cast<double>(bi) / static_cast<double>(kLutSize - 1);
        auto sample = apply_input_stage(
            ColorPixel{
                .red = static_cast<float>(r),
                .green = static_cast<float>(g),
                .blue = static_cast<float>(b),
                .alpha = 1.0F,
            },
            input_stage,
            color_context);
        const auto lum = luminance_for_pixel(sample, color_context);
        const auto target = apply_global_tone(lum, state);
        max_target_luminance = std::max(max_target_luminance, target);
      }
    }
  }
  const auto output_headroom = sanitized_output_headroom(state);
  const auto shoulder_threshold =
      shoulder_threshold_for_max_luminance(max_target_luminance, output_headroom, state.filmic_shoulder);

  for (int ri = 0; ri < kLutSize; ++ri) {
    for (int gi = 0; gi < kLutSize; ++gi) {
      for (int bi = 0; bi < kLutSize; ++bi) {
        const auto r = static_cast<double>(ri) / static_cast<double>(kLutSize - 1);
        const auto g = static_cast<double>(gi) / static_cast<double>(kLutSize - 1);
        const auto b = static_cast<double>(bi) / static_cast<double>(kLutSize - 1);
        const auto index = static_cast<std::size_t>(
            (ri * kLutSize + gi) * kLutSize + bi);
        lut.table[index] = apply_global_color_pipeline(
            ColorPixel{
                .red = static_cast<float>(r),
                .green = static_cast<float>(g),
                .blue = static_cast<float>(b),
                .alpha = 1.0F,
            },
            color_context,
            input_stage, state,
            parametric_curve,
            master_curve, luma_curve, red_curve, green_curve, blue_curve,
            saturation_strength, vibrance_strength, dehaze_strength,
            shoulder_threshold,
            output_headroom);
      }
    }
  }

  lut.shoulder_threshold = shoulder_threshold;
  lut.shoulder_headroom = output_headroom;
  lut.cache_identity = ++next_cache_identity;
  lut.valid = true;
  auto cache_state = state;
  cache_state.lut_table.clear();
  cache_state.lut_stack.clear();
  cache_state.local_mask_sets.clear();
  cache_state.retouch_masks.clear();
  cache_state.local_masks.clear();
  cache_state.photo_layers.clear();
  cache.entries.push_front(LutCacheEntry{
      .state = std::move(cache_state),
      .lut = lut,
  });
  if (cache.entries.size() > kMaximumCachedColorLuts) {
    cache.entries.pop_back();
  }
  return lut;
}

}  // namespace photo_editor::pipeline
