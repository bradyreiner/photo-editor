#include "photo_editor/pipeline/color_adjustments.h"

#include "color_tone_runtime.h"
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

[[nodiscard]] float luminance(
    const CompiledToneStageLut& compiled,
    const ColorPixel& pixel) {
  return pixel.red * compiled.luminance_coefficients[0] +
         pixel.green * compiled.luminance_coefficients[1] +
         pixel.blue * compiled.luminance_coefficients[2];
}

[[nodiscard]] float target_luminance(
    const CompiledToneStageLut& compiled,
    float value) {
  if (value <= 0.0F) {
    return 0.0F;
  }
  const auto stops =
      std::log2(std::max(1.0e-8F, value) /
                CompiledToneStageLut::kMiddleGray);
  const auto position = std::clamp(
      (stops - CompiledToneStageLut::kMinimumStops) /
          compiled.stop_step,
      0.0F,
      static_cast<float>(CompiledToneStageLut::kSampleCount - 1));
  const auto lower = std::min(
      static_cast<int>(position),
      CompiledToneStageLut::kSampleCount - 2);
  const auto fraction = position - static_cast<float>(lower);
  return std::lerp(
      compiled.samples[static_cast<std::size_t>(lower)],
      compiled.samples[static_cast<std::size_t>(lower + 1)],
      fraction);
}

[[nodiscard]] float smoothstep(
    float edge0,
    float edge1,
    float value) {
  const auto unit =
      std::clamp((value - edge0) / (edge1 - edge0), 0.0F, 1.0F);
  return unit * unit * (3.0F - 2.0F * unit);
}

}  // namespace

CompiledToneStageLut compile_tone_stage_lut(
    const ColorAdjustmentState& state,
    ColorAdjustmentTelemetry* telemetry) {
  const auto table = acquire_tone_luminance_table(state, telemetry);
  const auto context = make_pipeline_color_context(state);
  auto compiled = CompiledToneStageLut{};
  if (table == nullptr || !table->ready()) {
    return compiled;
  }
  compiled.samples.reserve(table->samples.size());
  for (const auto sample : table->samples) {
    compiled.samples.push_back(static_cast<float>(sample));
  }
  compiled.stop_step = static_cast<float>(table->stop_step);
  compiled.luminance_coefficients = {
      static_cast<float>(context.rgb_to_xyz_native.values[3]),
      static_cast<float>(context.rgb_to_xyz_native.values[4]),
      static_cast<float>(context.rgb_to_xyz_native.values[5]),
  };
  compiled.output_headroom =
      static_cast<float>(sanitized_output_headroom(state));

  auto identity = std::uint64_t{14695981039346656037ULL};
  for (const auto sample : compiled.samples) {
    identity = hash_append(
        identity, std::bit_cast<std::uint32_t>(sample));
  }
  for (const auto coefficient : compiled.luminance_coefficients) {
    identity = hash_append(
        identity, std::bit_cast<std::uint32_t>(coefficient));
  }
  identity = hash_append(
      identity, std::bit_cast<std::uint32_t>(compiled.stop_step));
  identity = hash_append(
      identity, std::bit_cast<std::uint32_t>(compiled.output_headroom));
  compiled.cache_identity = identity != 0U ? identity : 1U;
  return compiled;
}

ColorPixel apply_compiled_tone_stage_lut(
    const CompiledToneStageLut& compiled,
    const ColorPixel& pixel) {
  if (!compiled.ready()) {
    return pixel;
  }
  const auto source_luminance = luminance(compiled, pixel);
  const auto target =
      std::max(0.0F, target_luminance(compiled, source_luminance));
  const auto maximum_channel = std::max({pixel.red, pixel.green, pixel.blue});
  auto safe_target = target;
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
      std::abs(pixel.red),
      std::abs(pixel.green),
      std::abs(pixel.blue),
  });
  auto adjusted = pixel;
  if (maximum_magnitude <= 1.0e-9F) {
    adjusted.red = safe_target;
    adjusted.green = safe_target;
    adjusted.blue = safe_target;
  } else if (source_luminance > 1.0e-6F) {
    const auto scale = safe_target / source_luminance;
    adjusted.red = pixel.red * scale;
    adjusted.green = pixel.green * scale;
    adjusted.blue = pixel.blue * scale;
  }

  const auto adjusted_luminance =
      std::max(0.0F, luminance(compiled, adjusted));
  const auto nonnegative_source_luminance = std::max(0.0F, source_luminance);
  const auto lift_stops = std::log2(
      (adjusted_luminance + 1.0e-6F) /
      (nonnegative_source_luminance + 1.0e-6F));
  if (lift_stops <= 0.0F) {
    return adjusted;
  }
  const auto channel_spread = std::max({
      std::abs(pixel.red - pixel.green),
      std::abs(pixel.green - pixel.blue),
      std::abs(pixel.red - pixel.blue),
  });
  const auto attenuation = std::clamp(
      (1.0F - smoothstep(0.012F, 0.16F, nonnegative_source_luminance)) *
          smoothstep(0.12F, 1.20F, lift_stops) *
          (1.0F - smoothstep(0.025F, 0.12F, channel_spread)) * 0.56F,
      0.0F,
      0.56F);
  if (attenuation <= 1.0e-9F) {
    return adjusted;
  }
  const auto chroma_scale = 1.0F - attenuation;
  adjusted.red = adjusted_luminance +
                 (adjusted.red - adjusted_luminance) * chroma_scale;
  adjusted.green = adjusted_luminance +
                   (adjusted.green - adjusted_luminance) * chroma_scale;
  adjusted.blue = adjusted_luminance +
                  (adjusted.blue - adjusted_luminance) * chroma_scale;
  return adjusted;
}

}  // namespace photo_editor::pipeline
