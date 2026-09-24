#pragma once

#include "photo_editor/pipeline/color_adjustments.h"

#include <string>

namespace photo_editor::pipeline {

using Chromaticity = ColorChromaticity;
using XyzColor = ColorXyz;
using Matrix3 = ColorMatrix3;

struct OklabColor final {
  double lightness = 0.0;
  double a = 0.0;
  double b = 0.0;
};

struct PipelineColorContext final {
  bool operator==(const PipelineColorContext&) const = default;
  RgbColorSpace working_space = make_linear_srgb_color_space();
  RenderIntent render_intent = RenderIntent::Perceptual;
  bool output_gamut_mapping_enabled = true;
  GamutMappingPolicy output_gamut_mapping_policy = GamutMappingPolicy::Perceptual;
  double output_headroom = 1.0;
  double layer_blend_headroom = 1.0;
  std::string chromatic_adaptation_method = "Bradford";
  Matrix3 rgb_to_xyz_native;
  Matrix3 xyz_native_to_rgb;
  Matrix3 rgb_to_xyz_d65;
  Matrix3 xyz_d65_to_rgb;
  Matrix3 rgb_to_linear_srgb;
};

[[nodiscard]] double sanitized_scene_working_headroom(double value);
[[nodiscard]] double working_headroom_for_rendering_model(
    const ColorAdjustmentState& state);
[[nodiscard]] double sanitized_output_headroom(
    const ColorAdjustmentState& state);

[[nodiscard]] PipelineColorContext make_pipeline_color_context(
    const ColorAdjustmentState& state);
[[nodiscard]] PipelineColorContext pipeline_color_context_for_space(
    const PipelineColorContext& base,
    const RgbColorSpace& working_space);

[[nodiscard]] ColorPixel apply_rgb_matrix(
    const ColorPixel& pixel,
    const Matrix3& matrix);
[[nodiscard]] XyzColor pixel_to_xyz_native(
    const ColorPixel& pixel,
    const PipelineColorContext& context);
[[nodiscard]] XyzColor pixel_to_xyz_d65(
    const ColorPixel& pixel,
    const PipelineColorContext& context);
[[nodiscard]] ColorPixel pixel_to_linear_srgb_reference(
    const ColorPixel& pixel,
    const PipelineColorContext& context);
[[nodiscard]] double luminance_for_pixel(
    const ColorPixel& pixel,
    const PipelineColorContext& context);

[[nodiscard]] OklabColor linear_rgb_to_oklab(
    const ColorPixel& pixel,
    const PipelineColorContext& context);
[[nodiscard]] ColorPixel oklab_to_linear_rgb(
    const OklabColor& color,
    double alpha,
    const PipelineColorContext& context);
[[nodiscard]] double oklab_lightness_ceiling(
    const PipelineColorContext& context);
[[nodiscard]] double clamp_oklab_lightness(
    double lightness,
    const PipelineColorContext& context);

[[nodiscard]] ColorPixel scale_pixel_to_luminance(
    const ColorPixel& pixel,
    double target_luminance,
    const PipelineColorContext& context);
[[nodiscard]] ColorPixel scale_pixel_to_luminance_with_headroom(
    const ColorPixel& pixel,
    double target_luminance,
    double output_headroom,
    const PipelineColorContext& context);
[[nodiscard]] ColorPixel scale_pixel_for_global_tone(
    const ColorPixel& pixel,
    double target_luminance,
    double output_headroom,
    const PipelineColorContext& context);

[[nodiscard]] ColorPixel fit_pixel_to_output_headroom(
    const ColorPixel& pixel,
    double output_headroom);
[[nodiscard]] ColorPixel fit_pixel_to_working_headroom_preserving_signed(
    const ColorPixel& pixel,
    double output_headroom);
[[nodiscard]] ColorPixel clip_pixel_to_output_headroom(
    const ColorPixel& pixel,
    double output_headroom);
[[nodiscard]] bool in_output_gamut(
    const ColorPixel& pixel,
    const PipelineColorContext& context);
[[nodiscard]] ColorPixel clamp_rgb_to_output_headroom(
    const ColorPixel& pixel,
    const PipelineColorContext& context);
[[nodiscard]] ColorPixel gamut_fit_oklch(
    double lightness,
    double chroma,
    double hue,
    double alpha,
    const PipelineColorContext& context);
[[nodiscard]] ColorPixel gamut_fit_oklab(
    double lightness,
    double a,
    double b,
    double alpha,
    const PipelineColorContext& context);

}  // namespace photo_editor::pipeline
