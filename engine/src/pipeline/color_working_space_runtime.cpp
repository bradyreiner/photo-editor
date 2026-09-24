#include "color_working_space_runtime.h"

#include "color_math_runtime.h"

#include <algorithm>
#include <cmath>

namespace photo_editor::pipeline {

namespace {

constexpr auto kAces2SceneWorkingHeadroom = 8192.0;

[[nodiscard]] ColorPixel scale_pixel_to_luminance_unstabilized(
    const ColorPixel& pixel,
    double target_luminance,
    const PipelineColorContext& context) {
  const auto clamped_target = std::max(0.0, target_luminance);
  const auto current_luminance = luminance_for_pixel(pixel, context);
  const auto maximum_magnitude = std::max({
      std::abs(static_cast<double>(pixel.red)),
      std::abs(static_cast<double>(pixel.green)),
      std::abs(static_cast<double>(pixel.blue)),
  });
  if (maximum_magnitude <= 1e-9) {
    return ColorPixel{
        .red = static_cast<float>(clamped_target),
        .green = static_cast<float>(clamped_target),
        .blue = static_cast<float>(clamped_target),
        .alpha = pixel.alpha,
    };
  }
  if (current_luminance <= 1e-6) {
    return pixel;
  }
  const auto scale = clamped_target / current_luminance;
  return ColorPixel{
      .red = static_cast<float>(static_cast<double>(pixel.red) * scale),
      .green = static_cast<float>(static_cast<double>(pixel.green) * scale),
      .blue = static_cast<float>(static_cast<double>(pixel.blue) * scale),
      .alpha = pixel.alpha,
  };
}

[[nodiscard]] double headroom_safe_luminance_target(
    const ColorPixel& pixel,
    double target_luminance,
    double output_headroom,
    const PipelineColorContext& context) {
  const auto clamped_target = std::max(0.0, target_luminance);
  const auto headroom = sanitized_scene_working_headroom(output_headroom);
  const auto max_input_channel = std::max({
      static_cast<double>(pixel.red),
      static_cast<double>(pixel.green),
      static_cast<double>(pixel.blue),
  });
  const auto current_luminance = luminance_for_pixel(pixel, context);
  if (max_input_channel <= 1.0e-9 || current_luminance <= 1.0e-9) {
    return std::min(clamped_target, headroom);
  }
  const auto safe_target = headroom * current_luminance / max_input_channel;
  return std::min(clamped_target, std::max(0.0, safe_target));
}

[[nodiscard]] ColorPixel stabilize_lifted_shadow_colorfulness(
    const ColorPixel& source,
    ColorPixel adjusted,
    const PipelineColorContext& context) {
  const auto source_luminance = std::max(0.0, luminance_for_pixel(source, context));
  const auto adjusted_luminance = std::max(0.0, luminance_for_pixel(adjusted, context));
  const auto lift_stops = std::log2(
      (adjusted_luminance + 1.0e-6) / (source_luminance + 1.0e-6));
  if (lift_stops <= 0.0) {
    return adjusted;
  }
  const auto channel_spread = std::max({
      std::abs(static_cast<double>(source.red) - static_cast<double>(source.green)),
      std::abs(static_cast<double>(source.green) - static_cast<double>(source.blue)),
      std::abs(static_cast<double>(source.red) - static_cast<double>(source.blue)),
  });
  const auto shadow_weight = 1.0 - smoothstep(0.012, 0.16, source_luminance);
  const auto lift_weight = smoothstep(0.12, 1.20, lift_stops);
  const auto contamination_weight = 1.0 - smoothstep(0.025, 0.12, channel_spread);
  const auto attenuation = std::clamp(
      shadow_weight * lift_weight * contamination_weight * 0.56,
      0.0,
      0.56);
  if (attenuation <= 1.0e-9) {
    return adjusted;
  }
  const auto chroma_scale = 1.0 - attenuation;
  adjusted.red = static_cast<float>(
      adjusted_luminance +
      (static_cast<double>(adjusted.red) - adjusted_luminance) * chroma_scale);
  adjusted.green = static_cast<float>(
      adjusted_luminance +
      (static_cast<double>(adjusted.green) - adjusted_luminance) * chroma_scale);
  adjusted.blue = static_cast<float>(
      adjusted_luminance +
      (static_cast<double>(adjusted.blue) - adjusted_luminance) * chroma_scale);
  return adjusted;
}

}  // namespace

double sanitized_scene_working_headroom(double value) {
  return std::clamp(value, 1.0, kAces2SceneWorkingHeadroom);
}

double working_headroom_for_rendering_model(const ColorAdjustmentState& state) {
  if (photographic_rendering_model_uses_aces2(
          state.color_pipeline.photographic_rendering_model) &&
      state.color_pipeline.render_intent == RenderIntent::Perceptual) {
    return kAces2SceneWorkingHeadroom;
  }
  return std::clamp(state.color_pipeline.output_headroom, 1.0, 16.0);
}

double sanitized_output_headroom(const ColorAdjustmentState& state) {
  return working_headroom_for_rendering_model(state);
}

PipelineColorContext make_pipeline_color_context(const ColorAdjustmentState& state) {
  PipelineColorContext context;
  context.working_space = state.color_pipeline.working_profile.color_space;
  context.render_intent = state.color_pipeline.render_intent;
  context.output_gamut_mapping_enabled = state.color_pipeline.output_gamut_mapping_enabled;
  context.output_gamut_mapping_policy = state.color_pipeline.output_gamut_mapping_policy;
  context.output_headroom = working_headroom_for_rendering_model(state);
  context.layer_blend_headroom =
      std::clamp(state.color_pipeline.output_headroom, 1.0, 16.0);
  context.chromatic_adaptation_method =
      state.input_transform.chromatic_adaptation_method;
  context.rgb_to_xyz_native = rgb_to_xyz_matrix(context.working_space);
  context.xyz_native_to_rgb = invert_matrix3(context.rgb_to_xyz_native);
  const auto source_white = xyz_from_chromaticity(
      white_point_chromaticity(context.working_space.white_point));
  const auto d65_white = xyz_from_chromaticity(
      white_point_chromaticity(WhitePoint::D65));
  const auto to_d65 = chromatic_adaptation_matrix(
      source_white,
      d65_white,
      context.chromatic_adaptation_method);
  const auto from_d65 = chromatic_adaptation_matrix(
      d65_white,
      source_white,
      context.chromatic_adaptation_method);
  context.rgb_to_xyz_d65 = multiply_matrix3(to_d65, context.rgb_to_xyz_native);
  context.xyz_d65_to_rgb = multiply_matrix3(context.xyz_native_to_rgb, from_d65);
  const auto linear_srgb = make_linear_srgb_color_space();
  context.rgb_to_linear_srgb = multiply_matrix3(
      invert_matrix3(rgb_to_xyz_matrix(linear_srgb)),
      context.rgb_to_xyz_d65);
  return context;
}

PipelineColorContext pipeline_color_context_for_space(
    const PipelineColorContext& base,
    const RgbColorSpace& working_space) {
  auto context = base;
  context.working_space = working_space;
  context.rgb_to_xyz_native = rgb_to_xyz_matrix(context.working_space);
  context.xyz_native_to_rgb = invert_matrix3(context.rgb_to_xyz_native);
  const auto source_white = xyz_from_chromaticity(
      white_point_chromaticity(context.working_space.white_point));
  const auto d65_white = xyz_from_chromaticity(
      white_point_chromaticity(WhitePoint::D65));
  const auto to_d65 = chromatic_adaptation_matrix(
      source_white,
      d65_white,
      context.chromatic_adaptation_method);
  const auto from_d65 = chromatic_adaptation_matrix(
      d65_white,
      source_white,
      context.chromatic_adaptation_method);
  context.rgb_to_xyz_d65 = multiply_matrix3(to_d65, context.rgb_to_xyz_native);
  context.xyz_d65_to_rgb = multiply_matrix3(context.xyz_native_to_rgb, from_d65);
  context.rgb_to_linear_srgb = multiply_matrix3(
      invert_matrix3(rgb_to_xyz_matrix(make_linear_srgb_color_space())),
      context.rgb_to_xyz_d65);
  return context;
}

ColorPixel apply_rgb_matrix(const ColorPixel& pixel, const Matrix3& matrix) {
  const auto transformed = multiply_matrix3_vector(
      matrix,
      {static_cast<double>(pixel.red),
       static_cast<double>(pixel.green),
       static_cast<double>(pixel.blue)});
  return ColorPixel{
      .red = static_cast<float>(transformed[0]),
      .green = static_cast<float>(transformed[1]),
      .blue = static_cast<float>(transformed[2]),
      .alpha = pixel.alpha,
  };
}

XyzColor pixel_to_xyz_native(
    const ColorPixel& pixel,
    const PipelineColorContext& context) {
  return multiply_matrix3_xyz(
      context.rgb_to_xyz_native,
      XyzColor{
          .x = static_cast<double>(pixel.red),
          .y = static_cast<double>(pixel.green),
          .z = static_cast<double>(pixel.blue),
      });
}

XyzColor pixel_to_xyz_d65(
    const ColorPixel& pixel,
    const PipelineColorContext& context) {
  return multiply_matrix3_xyz(
      context.rgb_to_xyz_d65,
      XyzColor{
          .x = static_cast<double>(pixel.red),
          .y = static_cast<double>(pixel.green),
          .z = static_cast<double>(pixel.blue),
      });
}

ColorPixel pixel_to_linear_srgb_reference(
    const ColorPixel& pixel,
    const PipelineColorContext& context) {
  return apply_rgb_matrix(pixel, context.rgb_to_linear_srgb);
}

double luminance_for_pixel(
    const ColorPixel& pixel,
    const PipelineColorContext& context) {
  return pixel_to_xyz_native(pixel, context).y;
}

OklabColor linear_rgb_to_oklab(
    const ColorPixel& pixel,
    const PipelineColorContext& context) {
  const auto xyz = pixel_to_xyz_d65(pixel, context);
  const auto l = 0.8189330101 * xyz.x + 0.3618667424 * xyz.y - 0.1288597137 * xyz.z;
  const auto m = 0.0329845436 * xyz.x + 0.9293118715 * xyz.y + 0.0361456387 * xyz.z;
  const auto s = 0.0482003018 * xyz.x + 0.2643662691 * xyz.y + 0.6338517070 * xyz.z;
  const auto l_root = std::cbrt(l);
  const auto m_root = std::cbrt(m);
  const auto s_root = std::cbrt(s);
  return OklabColor{
      .lightness =
          0.2104542553 * l_root + 0.7936177850 * m_root - 0.0040720468 * s_root,
      .a = 1.9779984951 * l_root - 2.4285922050 * m_root + 0.4505937099 * s_root,
      .b = 0.0259040371 * l_root + 0.7827717662 * m_root - 0.8086757660 * s_root,
  };
}

ColorPixel oklab_to_linear_rgb(
    const OklabColor& color,
    double alpha,
    const PipelineColorContext& context) {
  const auto l_root = color.lightness + 0.3963377774 * color.a + 0.2158037573 * color.b;
  const auto m_root = color.lightness - 0.1055613458 * color.a - 0.0638541728 * color.b;
  const auto s_root = color.lightness - 0.0894841775 * color.a - 1.2914855480 * color.b;
  const auto l = l_root * l_root * l_root;
  const auto m = m_root * m_root * m_root;
  const auto s = s_root * s_root * s_root;
  return apply_rgb_matrix(
      ColorPixel{
          .red = static_cast<float>(+1.2270138511 * l - 0.5577999807 * m + 0.2812561490 * s),
          .green = static_cast<float>(-0.0405801784 * l + 1.1122568696 * m - 0.0716766787 * s),
          .blue = static_cast<float>(-0.0763812845 * l - 0.4214819784 * m + 1.5861632204 * s),
          .alpha = static_cast<float>(alpha),
      },
      context.xyz_d65_to_rgb);
}

double oklab_lightness_ceiling(const PipelineColorContext& context) {
  return std::cbrt(sanitized_scene_working_headroom(context.output_headroom));
}

double clamp_oklab_lightness(
    double lightness,
    const PipelineColorContext& context) {
  return std::clamp(lightness, 0.0, oklab_lightness_ceiling(context));
}

ColorPixel scale_pixel_to_luminance(
    const ColorPixel& pixel,
    double target_luminance,
    const PipelineColorContext& context) {
  return stabilize_lifted_shadow_colorfulness(
      pixel,
      scale_pixel_to_luminance_unstabilized(pixel, target_luminance, context),
      context);
}

ColorPixel scale_pixel_to_luminance_with_headroom(
    const ColorPixel& pixel,
    double target_luminance,
    double output_headroom,
    const PipelineColorContext& context) {
  return scale_pixel_to_luminance(
      pixel,
      headroom_safe_luminance_target(
          pixel,
          target_luminance,
          output_headroom,
          context),
      context);
}

ColorPixel scale_pixel_for_global_tone(
    const ColorPixel& pixel,
    double target_luminance,
    double output_headroom,
    const PipelineColorContext& context) {
  return scale_pixel_to_luminance_with_headroom(
      pixel,
      target_luminance,
      output_headroom,
      context);
}

ColorPixel fit_pixel_to_output_headroom(
    const ColorPixel& pixel,
    double output_headroom) {
  const auto headroom = sanitized_scene_working_headroom(output_headroom);
  if (static_cast<double>(pixel.red) >= 0.0 &&
      static_cast<double>(pixel.green) >= 0.0 &&
      static_cast<double>(pixel.blue) >= 0.0 &&
      static_cast<double>(pixel.red) <= headroom &&
      static_cast<double>(pixel.green) <= headroom &&
      static_cast<double>(pixel.blue) <= headroom) {
    return pixel;
  }
  const auto red = std::max(0.0, static_cast<double>(pixel.red));
  const auto green = std::max(0.0, static_cast<double>(pixel.green));
  const auto blue = std::max(0.0, static_cast<double>(pixel.blue));
  const auto maximum = std::max({red, green, blue});
  const auto scale = maximum > headroom ? headroom / maximum : 1.0;
  return ColorPixel{
      .red = static_cast<float>(red * scale),
      .green = static_cast<float>(green * scale),
      .blue = static_cast<float>(blue * scale),
      .alpha = pixel.alpha,
  };
}

ColorPixel fit_pixel_to_working_headroom_preserving_signed(
    const ColorPixel& pixel,
    double output_headroom) {
  const auto headroom = sanitized_scene_working_headroom(output_headroom);
  const auto maximum = std::max({
      static_cast<double>(pixel.red),
      static_cast<double>(pixel.green),
      static_cast<double>(pixel.blue),
  });
  const auto scale = maximum > headroom ? headroom / maximum : 1.0;
  return ColorPixel{
      .red = static_cast<float>(static_cast<double>(pixel.red) * scale),
      .green = static_cast<float>(static_cast<double>(pixel.green) * scale),
      .blue = static_cast<float>(static_cast<double>(pixel.blue) * scale),
      .alpha = pixel.alpha,
  };
}

ColorPixel clip_pixel_to_output_headroom(
    const ColorPixel& pixel,
    double output_headroom) {
  const auto headroom = std::clamp(output_headroom, 1.0, 100.0);
  return ColorPixel{
      .red = static_cast<float>(std::clamp(static_cast<double>(pixel.red), 0.0, headroom)),
      .green = static_cast<float>(std::clamp(static_cast<double>(pixel.green), 0.0, headroom)),
      .blue = static_cast<float>(std::clamp(static_cast<double>(pixel.blue), 0.0, headroom)),
      .alpha = pixel.alpha,
  };
}

bool in_output_gamut(
    const ColorPixel& pixel,
    const PipelineColorContext& context) {
  const auto headroom = sanitized_scene_working_headroom(context.output_headroom);
  return pixel.red >= -1e-6 && pixel.red <= headroom + 1e-6 &&
         pixel.green >= -1e-6 && pixel.green <= headroom + 1e-6 &&
         pixel.blue >= -1e-6 && pixel.blue <= headroom + 1e-6;
}

ColorPixel clamp_rgb_to_output_headroom(
    const ColorPixel& pixel,
    const PipelineColorContext& context) {
  auto clamped = fit_pixel_to_output_headroom(pixel, context.output_headroom);
  clamped.alpha = static_cast<float>(
      std::clamp(static_cast<double>(clamped.alpha), 0.0, 1.0));
  return clamped;
}

ColorPixel gamut_fit_oklch(
    double lightness,
    double chroma,
    double hue,
    double alpha,
    const PipelineColorContext& context) {
  struct GamutFitResult final {
    double lightness = 0.0;
    double chroma = 0.0;
    ColorPixel pixel;
  };
  const auto should_clip =
      !context.output_gamut_mapping_enabled ||
      context.output_gamut_mapping_policy == GamutMappingPolicy::Clip ||
      context.render_intent == RenderIntent::RelativeColorimetric;

  const auto search_chroma_at_lightness =
      [&](double candidate_lightness,
          double candidate_chroma,
          int iterations) -> GamutFitResult {
    candidate_lightness = clamp_oklab_lightness(candidate_lightness, context);
    candidate_chroma = std::max(0.0, candidate_chroma);
    OklabColor candidate_lab{
        .lightness = candidate_lightness,
        .a = candidate_chroma * std::cos(hue),
        .b = candidate_chroma * std::sin(hue),
    };
    auto candidate = oklab_to_linear_rgb(candidate_lab, alpha, context);
    if (should_clip || in_output_gamut(candidate, context)) {
      return GamutFitResult{
          .lightness = candidate_lightness,
          .chroma = candidate_chroma,
          .pixel = clamp_rgb_to_output_headroom(candidate, context),
      };
    }

    auto low = 0.0;
    auto high = candidate_chroma;
    for (int iteration = 0; iteration < iterations; ++iteration) {
      const auto mid = (low + high) * 0.5;
      candidate_lab = OklabColor{
          .lightness = candidate_lightness,
          .a = mid * std::cos(hue),
          .b = mid * std::sin(hue),
      };
      candidate = oklab_to_linear_rgb(candidate_lab, alpha, context);
      if (in_output_gamut(candidate, context)) {
        low = mid;
      } else {
        high = mid;
      }
    }
    candidate_lab = OklabColor{
        .lightness = candidate_lightness,
        .a = low * std::cos(hue),
        .b = low * std::sin(hue),
    };
    return GamutFitResult{
        .lightness = candidate_lightness,
        .chroma = low,
        .pixel = clamp_rgb_to_output_headroom(
            oklab_to_linear_rgb(candidate_lab, alpha, context),
            context),
    };
  };

  lightness = clamp_oklab_lightness(lightness, context);
  chroma = std::max(0.0, chroma);
  hue = wrap_radians(hue);
  if (chroma <= 1e-9) {
    return clamp_rgb_to_output_headroom(
        oklab_to_linear_rgb(
            OklabColor{
                .lightness = lightness,
                .a = 0.0,
                .b = 0.0,
            },
            alpha,
            context),
        context);
  }

  auto best_fit = search_chroma_at_lightness(lightness, chroma, 16);
  if (best_fit.chroma > std::max(1e-4, chroma * 0.02) ||
      context.output_gamut_mapping_policy == GamutMappingPolicy::PreserveSaturation) {
    return best_fit.pixel;
  }

  constexpr auto target_lightness = 0.5;
  for (int iteration = 1; iteration <= 12; ++iteration) {
    const auto t = static_cast<double>(iteration) / 12.0;
    const auto candidate_lightness = std::lerp(lightness, target_lightness, t);
    const auto candidate_fit =
        search_chroma_at_lightness(candidate_lightness, chroma, 16);
    if (candidate_fit.chroma > best_fit.chroma + 1e-6 ||
        (std::abs(candidate_fit.chroma - best_fit.chroma) <= 1e-6 &&
         std::abs(candidate_fit.lightness - lightness) <
             std::abs(best_fit.lightness - lightness))) {
      best_fit = candidate_fit;
    }
  }
  return best_fit.pixel;
}

ColorPixel gamut_fit_oklab(
    double lightness,
    double a,
    double b,
    double alpha,
    const PipelineColorContext& context) {
  const auto clamped_lightness = clamp_oklab_lightness(lightness, context);
  const auto chroma_sq = (a * a) + (b * b);
  if (chroma_sq <= 1.0e-18) {
    return gamut_fit_oklch(clamped_lightness, 0.0, 0.0, alpha, context);
  }
  const auto direct = oklab_to_linear_rgb(
      OklabColor{
          .lightness = clamped_lightness,
          .a = a,
          .b = b,
      },
      alpha,
      context);
  if (!context.output_gamut_mapping_enabled ||
      context.output_gamut_mapping_policy == GamutMappingPolicy::Clip ||
      context.render_intent == RenderIntent::RelativeColorimetric ||
      in_output_gamut(direct, context)) {
    return clamp_rgb_to_output_headroom(direct, context);
  }
  return gamut_fit_oklch(
      clamped_lightness,
      std::sqrt(chroma_sq),
      wrap_radians(std::atan2(b, a)),
      alpha,
      context);
}

}  // namespace photo_editor::pipeline
