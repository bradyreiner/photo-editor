#include "photo_editor/pipeline/color_hdr_local_tone_program.h"

#include "color_blur_runtime.h"
#include "color_math_runtime.h"
#include "color_working_space_runtime.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <ranges>

namespace photo_editor::pipeline {

namespace {

[[nodiscard]] double luminance(
    const ColorPixel& pixel,
    const CompiledHdrLocalToneProgram& program) {
  return static_cast<double>(pixel.red) * program.luminance_coefficients[0] +
         static_cast<double>(pixel.green) * program.luminance_coefficients[1] +
         static_cast<double>(pixel.blue) * program.luminance_coefficients[2];
}

[[nodiscard]] ColorPixel scale_to_luminance(
    const ColorPixel& source,
    double target,
    const CompiledHdrLocalToneProgram& program) {
  const auto source_luminance = luminance(source, program);
  const auto maximum_channel = std::max({
      static_cast<double>(source.red),
      static_cast<double>(source.green),
      static_cast<double>(source.blue),
  });
  auto safe_target = std::max(0.0, target);
  if (maximum_channel <= 1.0e-9 || source_luminance <= 1.0e-9) {
    safe_target = std::min(safe_target, static_cast<double>(program.output_headroom));
  } else {
    safe_target = std::min(
        safe_target,
        std::max(0.0, static_cast<double>(program.output_headroom) *
                          source_luminance / maximum_channel));
  }
  const auto maximum_magnitude = std::max({
      std::abs(static_cast<double>(source.red)),
      std::abs(static_cast<double>(source.green)),
      std::abs(static_cast<double>(source.blue)),
  });
  auto adjusted = source;
  if (maximum_magnitude <= 1.0e-9) {
    adjusted.red = adjusted.green = adjusted.blue = static_cast<float>(safe_target);
  } else if (source_luminance > 1.0e-6) {
    const auto scale = safe_target / source_luminance;
    adjusted.red = static_cast<float>(source.red * scale);
    adjusted.green = static_cast<float>(source.green * scale);
    adjusted.blue = static_cast<float>(source.blue * scale);
  }
  return adjusted;
}

}  // namespace

bool CompiledHdrLocalToneProgram::ready() const noexcept {
  return valid && width > 0U && height > 0U && radius > 0U &&
         static_cast<std::uint64_t>(width) * height <=
             std::numeric_limits<std::size_t>::max() &&
         std::ranges::all_of(luminance_coefficients, [](float value) {
           return std::isfinite(value);
         }) &&
         std::isfinite(strength) && strength >= 0.0F && strength <= 1.0F &&
         std::isfinite(output_headroom) && output_headroom >= 1.0F;
}

bool CompiledHdrLocalToneProgram::active() const noexcept {
  return strength > 1.0e-6F;
}

CompiledHdrLocalToneProgram compile_hdr_local_tone_program(
    const ColorAdjustmentState& state,
    int width,
    int height) {
  auto program = CompiledHdrLocalToneProgram{};
  if (width <= 0 || height <= 0 ||
      !std::isfinite(state.hdr_local_tone_mapping)) {
    return program;
  }
  const auto context = make_pipeline_color_context(state);
  program.luminance_coefficients = {
      static_cast<float>(context.rgb_to_xyz_native.values[3]),
      static_cast<float>(context.rgb_to_xyz_native.values[4]),
      static_cast<float>(context.rgb_to_xyz_native.values[5]),
  };
  program.strength = static_cast<float>(
      std::clamp(state.hdr_local_tone_mapping / 100.0, 0.0, 1.0));
  program.output_headroom = static_cast<float>(sanitized_output_headroom(state));
  program.radius = static_cast<std::uint32_t>(
      std::clamp(std::max(width, height) / 24, 1, 48));
  program.width = static_cast<std::uint32_t>(width);
  program.height = static_cast<std::uint32_t>(height);
  program.valid = true;
  if (!program.ready()) program.valid = false;
  return program;
}

bool apply_compiled_hdr_local_tone_program(
    std::vector<ColorPixel>* pixels,
    const CompiledHdrLocalToneProgram& program) {
  if (pixels == nullptr || !program.ready() ||
      pixels->size() != static_cast<std::size_t>(program.width) * program.height) {
    return false;
  }
  if (!program.active()) return true;
  auto log_luminance = std::vector<float>(pixels->size(), 0.0F);
  for (auto index = std::size_t{0U}; index < pixels->size(); ++index) {
    log_luminance[index] = static_cast<float>(std::log2(
        std::max(1.0e-6, luminance((*pixels)[index], program))));
  }
  const auto local_log = gaussian_approximate_blur(
      log_luminance,
      static_cast<int>(program.width),
      static_cast<int>(program.height),
      static_cast<int>(program.radius));
  constexpr auto kCompressionStart = -0.4739311883324123;
  for (auto index = std::size_t{0U}; index < pixels->size(); ++index) {
    const auto target_log = static_cast<double>(log_luminance[index]);
    const auto local = static_cast<double>(local_log[index]);
    const auto local_excess = std::max(0.0, local - kCompressionStart);
    const auto highlight_weight = smoothstep(
        kCompressionStart, kCompressionStart + 2.2, local);
    const auto detail_log = target_log - local;
    const auto mapped_log =
        target_log - local_excess * program.strength * 0.58 +
        detail_log * program.strength * highlight_weight * 0.45;
    (*pixels)[index] = scale_to_luminance(
        (*pixels)[index], std::exp2(mapped_log), program);
  }
  return true;
}

}  // namespace photo_editor::pipeline
