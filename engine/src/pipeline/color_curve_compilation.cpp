#include "photo_editor/pipeline/color_adjustments.h"

#include "color_curve_evaluator.h"
#include "color_working_space_runtime.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>

namespace photo_editor::pipeline {

namespace {

[[nodiscard]] std::uint64_t hash_append(
    std::uint64_t hash,
    std::uint32_t value) {
  constexpr auto kFnvPrime = std::uint64_t{1099511628211ULL};
  for (auto shift = 0U; shift < 32U; shift += 8U) {
    hash ^= static_cast<std::uint8_t>((value >> shift) & 0xffU);
    hash *= kFnvPrime;
  }
  return hash;
}

template <typename Evaluator>
[[nodiscard]] std::vector<float> build_lut(Evaluator&& evaluator) {
  auto lut = std::vector<float>(
      static_cast<std::size_t>(CompiledCurveStageLuts::kSampleCount));
  for (auto index = 0; index < CompiledCurveStageLuts::kSampleCount; ++index) {
    const auto input =
        static_cast<double>(index) /
        static_cast<double>(CompiledCurveStageLuts::kSampleCount - 1);
    lut[static_cast<std::size_t>(index)] =
        static_cast<float>(evaluator(input));
  }
  return lut;
}

[[nodiscard]] float sample_lut(
    const std::vector<float>& lut,
    float value) {
  if (value > 1.0F) {
    return value * lut.back();
  }
  if (value < 0.0F) {
    return value + lut.front();
  }
  const auto position = std::clamp(value, 0.0F, 1.0F) *
                        static_cast<float>(CompiledCurveStageLuts::kSampleCount - 1);
  const auto lower = std::min(
      static_cast<int>(position),
      CompiledCurveStageLuts::kSampleCount - 2);
  return std::lerp(
      lut[static_cast<std::size_t>(lower)],
      lut[static_cast<std::size_t>(lower + 1)],
      position - static_cast<float>(lower));
}

[[nodiscard]] float luminance(
    const CompiledCurveStageLuts& compiled,
    const ColorPixel& pixel) {
  return pixel.red * compiled.luminance_coefficients[0] +
         pixel.green * compiled.luminance_coefficients[1] +
         pixel.blue * compiled.luminance_coefficients[2];
}

[[nodiscard]] float smoothstep(
    float edge0,
    float edge1,
    float value) {
  const auto unit =
      std::clamp((value - edge0) / (edge1 - edge0), 0.0F, 1.0F);
  return unit * unit * (3.0F - 2.0F * unit);
}

[[nodiscard]] ColorPixel scale_to_luminance(
    const CompiledCurveStageLuts& compiled,
    const ColorPixel& source,
    float target) {
  const auto source_luminance = luminance(compiled, source);
  const auto maximum_channel = std::max({source.red, source.green, source.blue});
  auto safe_target = std::max(0.0F, target);
  if (maximum_channel <= 1.0e-9F || source_luminance <= 1.0e-9F) {
    safe_target = std::min(safe_target, compiled.output_headroom);
  } else {
    safe_target = std::min(
        safe_target,
        std::max(
            0.0F,
            compiled.output_headroom * source_luminance / maximum_channel));
  }
  const auto maximum_magnitude = std::max({
      std::abs(source.red), std::abs(source.green), std::abs(source.blue)});
  auto adjusted = source;
  if (maximum_magnitude <= 1.0e-9F) {
    adjusted.red = safe_target;
    adjusted.green = safe_target;
    adjusted.blue = safe_target;
  } else if (source_luminance > 1.0e-6F) {
    const auto scale = safe_target / source_luminance;
    adjusted.red *= scale;
    adjusted.green *= scale;
    adjusted.blue *= scale;
  }
  const auto adjusted_luminance =
      std::max(0.0F, luminance(compiled, adjusted));
  const auto nonnegative_source = std::max(0.0F, source_luminance);
  const auto lift_stops = std::log2(
      (adjusted_luminance + 1.0e-6F) /
      (nonnegative_source + 1.0e-6F));
  if (lift_stops <= 0.0F) {
    return adjusted;
  }
  const auto spread = std::max({
      std::abs(source.red - source.green),
      std::abs(source.green - source.blue),
      std::abs(source.red - source.blue)});
  const auto attenuation = std::clamp(
      (1.0F - smoothstep(0.012F, 0.16F, nonnegative_source)) *
          smoothstep(0.12F, 1.20F, lift_stops) *
          (1.0F - smoothstep(0.025F, 0.12F, spread)) * 0.56F,
      0.0F,
      0.56F);
  const auto chroma_scale = 1.0F - attenuation;
  adjusted.red = adjusted_luminance +
                 (adjusted.red - adjusted_luminance) * chroma_scale;
  adjusted.green = adjusted_luminance +
                   (adjusted.green - adjusted_luminance) * chroma_scale;
  adjusted.blue = adjusted_luminance +
                  (adjusted.blue - adjusted_luminance) * chroma_scale;
  return adjusted;
}

}  // namespace

CompiledCurveStageLuts compile_curve_stage_luts(
    const ColorAdjustmentState& state) {
  auto compiled = CompiledCurveStageLuts{};
  const auto context = make_pipeline_color_context(state);
  compiled.luminance_coefficients = {
      static_cast<float>(context.rgb_to_xyz_native.values[3]),
      static_cast<float>(context.rgb_to_xyz_native.values[4]),
      static_cast<float>(context.rgb_to_xyz_native.values[5]),
  };
  compiled.output_headroom =
      static_cast<float>(sanitized_output_headroom(state));
  const auto has_parametric =
      std::abs(state.curve_shadows) > 1.0e-9 ||
      std::abs(state.curve_darks) > 1.0e-9 ||
      std::abs(state.curve_lights) > 1.0e-9 ||
      std::abs(state.curve_highlights) > 1.0e-9;
  if (has_parametric) {
    const auto curve = build_parametric_curve_data(state);
    compiled.parametric = build_lut([&](double input) {
      return evaluate_tone_curve_target(
          input, curve, false, compiled.output_headroom);
    });
  }
  const auto compile_point_curve = [&](std::span<const CurvePoint> points) {
    if (!has_point_curve(points)) {
      return std::vector<float>{};
    }
    const auto curve = build_curve_data(points);
    return build_lut([&](double input) {
      return apply_perceptual_point_curve(input, curve);
    });
  };
  compiled.master = compile_point_curve(state.master_curve_points);
  compiled.luma = compile_point_curve(state.luma_curve_points);
  compiled.red = compile_point_curve(state.red_curve_points);
  compiled.green = compile_point_curve(state.green_curve_points);
  compiled.blue = compile_point_curve(state.blue_curve_points);
  if (!compiled.active()) {
    return compiled;
  }
  auto identity = std::uint64_t{14695981039346656037ULL};
  for (const auto* lut : {&compiled.parametric, &compiled.master, &compiled.luma,
                          &compiled.red, &compiled.green, &compiled.blue}) {
    identity = hash_append(identity, static_cast<std::uint32_t>(lut->size()));
    for (const auto sample : *lut) {
      identity = hash_append(identity, std::bit_cast<std::uint32_t>(sample));
    }
  }
  for (const auto coefficient : compiled.luminance_coefficients) {
    identity = hash_append(
        identity, std::bit_cast<std::uint32_t>(coefficient));
  }
  identity = hash_append(
      identity, std::bit_cast<std::uint32_t>(compiled.output_headroom));
  compiled.cache_identity = identity != 0U ? identity : 1U;
  return compiled;
}

ColorPixel apply_compiled_curve_stage_luts(
    const CompiledCurveStageLuts& compiled,
    const ColorPixel& pixel) {
  if (!compiled.ready()) {
    return pixel;
  }
  auto adjusted = pixel;
  if (!compiled.parametric.empty()) {
    const auto source_luminance = luminance(compiled, adjusted);
    const auto target = source_luminance > 1.0F
                            ? std::clamp(
                                  source_luminance,
                                  0.0F,
                                  compiled.output_headroom)
                            : sample_lut(compiled.parametric, source_luminance);
    adjusted = scale_to_luminance(compiled, adjusted, target);
  }
  if (!compiled.master.empty()) {
    adjusted.red = sample_lut(compiled.master, adjusted.red);
    adjusted.green = sample_lut(compiled.master, adjusted.green);
    adjusted.blue = sample_lut(compiled.master, adjusted.blue);
  }
  if (!compiled.luma.empty()) {
    adjusted = scale_to_luminance(
        compiled,
        adjusted,
        sample_lut(compiled.luma, luminance(compiled, adjusted)));
  }
  if (!compiled.red.empty()) {
    adjusted.red = sample_lut(compiled.red, adjusted.red);
  }
  if (!compiled.green.empty()) {
    adjusted.green = sample_lut(compiled.green, adjusted.green);
  }
  if (!compiled.blue.empty()) {
    adjusted.blue = sample_lut(compiled.blue, adjusted.blue);
  }
  return adjusted;
}

}  // namespace photo_editor::pipeline
