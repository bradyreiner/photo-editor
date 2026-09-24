#include "photo_editor/pipeline/color_spatial_detail_program.h"

#include "color_working_space_runtime.h"
#include "color_blur_runtime.h"
#include "color_math_runtime.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <ranges>

namespace photo_editor::pipeline {

namespace {

[[nodiscard]] double luminance(
    const ColorPixel& pixel,
    const CompiledSpatialDetailProgram& program) {
  return static_cast<double>(pixel.red) * program.luminance_coefficients[0] +
         static_cast<double>(pixel.green) * program.luminance_coefficients[1] +
         static_cast<double>(pixel.blue) * program.luminance_coefficients[2];
}

[[nodiscard]] ColorPixel scale_to_luminance(
    const ColorPixel& source,
    double target,
    const CompiledSpatialDetailProgram& program) {
  const auto source_luminance = luminance(source, program);
  const auto maximum_channel = std::max({
      static_cast<double>(source.red),
      static_cast<double>(source.green),
      static_cast<double>(source.blue)});
  auto safe_target = std::max(0.0, target);
  if (maximum_channel <= 1.0e-9 || source_luminance <= 1.0e-9) {
    safe_target = std::min(safe_target, static_cast<double>(program.output_headroom));
  } else {
    safe_target = std::min(
        safe_target,
        std::max(
            0.0,
            static_cast<double>(program.output_headroom) *
                source_luminance / maximum_channel));
  }
  const auto maximum_magnitude = std::max({
      std::abs(static_cast<double>(source.red)),
      std::abs(static_cast<double>(source.green)),
      std::abs(static_cast<double>(source.blue))});
  auto adjusted = source;
  if (maximum_magnitude <= 1.0e-9) {
    adjusted.red = adjusted.green = adjusted.blue =
        static_cast<float>(safe_target);
  } else if (source_luminance > 1.0e-6) {
    const auto scale = safe_target / source_luminance;
    adjusted.red = static_cast<float>(source.red * scale);
    adjusted.green = static_cast<float>(source.green * scale);
    adjusted.blue = static_cast<float>(source.blue * scale);
  }
  const auto adjusted_luminance = std::max(0.0, luminance(adjusted, program));
  const auto nonnegative_source = std::max(0.0, source_luminance);
  const auto lift_stops = std::log2(
      (adjusted_luminance + 1.0e-6) / (nonnegative_source + 1.0e-6));
  if (lift_stops > 0.0) {
    const auto spread = std::max({
        std::abs(static_cast<double>(source.red - source.green)),
        std::abs(static_cast<double>(source.green - source.blue)),
        std::abs(static_cast<double>(source.red - source.blue))});
    const auto attenuation = std::clamp(
        (1.0 - smoothstep(0.012, 0.16, nonnegative_source)) *
            smoothstep(0.12, 1.20, lift_stops) *
            (1.0 - smoothstep(0.025, 0.12, spread)) * 0.56,
        0.0,
        0.56);
    const auto chroma_scale = 1.0 - attenuation;
    adjusted.red = static_cast<float>(
        adjusted_luminance + (adjusted.red - adjusted_luminance) * chroma_scale);
    adjusted.green = static_cast<float>(
        adjusted_luminance + (adjusted.green - adjusted_luminance) * chroma_scale);
    adjusted.blue = static_cast<float>(
        adjusted_luminance + (adjusted.blue - adjusted_luminance) * chroma_scale);
  }
  return adjusted;
}

}  // namespace

bool CompiledSpatialDetailProgram::ready() const noexcept {
  return valid && width > 0U && height > 0U &&
         static_cast<std::uint64_t>(width) * height <=
             std::numeric_limits<std::size_t>::max() &&
         std::ranges::all_of(luminance_coefficients, [](float value) {
           return std::isfinite(value);
         }) && std::isfinite(texture_strength) &&
         std::isfinite(clarity_strength) &&
         std::isfinite(dehaze_strength) &&
         std::isfinite(output_headroom) && output_headroom >= 1.0F;
}

bool CompiledSpatialDetailProgram::active() const noexcept {
  return active_mask != 0U;
}

bool CompiledFineDetailProgram::ready() const noexcept {
  const auto finite_matrix = [](const ColorMatrix3& matrix) {
    return std::ranges::all_of(matrix.values, [](double value) {
      return std::isfinite(value);
    });
  };
  return valid && width > 0U && height > 0U &&
         static_cast<std::uint64_t>(width) * height <=
             std::numeric_limits<std::size_t>::max() &&
         finite_matrix(rgb_to_xyz_d65) && finite_matrix(xyz_d65_to_rgb) &&
         std::isfinite(sharpen_strength) && std::isfinite(sharpen_gain) &&
         std::isfinite(sharpen_mask_strength) &&
         std::isfinite(luminance_nr_strength) &&
         std::isfinite(color_nr_strength) && sharpen_radius >= 1U &&
         sharpen_edge_radius >= sharpen_radius;
}

bool CompiledFineDetailProgram::active() const noexcept {
  return active_mask != 0U;
}

CompiledSpatialDetailProgram compile_interactive_spatial_detail_program(
    const ColorAdjustmentState& state,
    int width,
    int height) {
  auto program = CompiledSpatialDetailProgram{};
  if (width <= 0 || height <= 0 ||
      !std::isfinite(state.texture) || !std::isfinite(state.clarity) ||
      !std::isfinite(state.dehaze)) {
    return program;
  }
  const auto context = make_pipeline_color_context(state);
  program.luminance_coefficients = {
      static_cast<float>(context.rgb_to_xyz_native.values[3]),
      static_cast<float>(context.rgb_to_xyz_native.values[4]),
      static_cast<float>(context.rgb_to_xyz_native.values[5]),
  };
  program.texture_strength = static_cast<float>(
      std::clamp(state.texture, -100.0, 100.0) / 100.0);
  program.clarity_strength = static_cast<float>(
      std::clamp(state.clarity, -100.0, 100.0) / 100.0);
  program.dehaze_strength = static_cast<float>(
      std::clamp(state.dehaze, -100.0, 100.0) / 100.0);
  program.output_headroom = static_cast<float>(
      sanitized_output_headroom(state));
  program.width = static_cast<std::uint32_t>(width);
  program.height = static_cast<std::uint32_t>(height);
  if (std::abs(program.texture_strength) > 1.0e-6F) {
    program.active_mask |= CompiledSpatialDetailProgram::kTexture;
  }
  if (std::abs(program.clarity_strength) > 1.0e-6F) {
    program.active_mask |= CompiledSpatialDetailProgram::kClarity;
  }
  if (std::abs(program.dehaze_strength) > 1.0e-6F) {
    program.active_mask |= CompiledSpatialDetailProgram::kDehaze;
  }
  program.valid = true;
  if (!program.ready()) program.valid = false;
  return program;
}

CompiledFineDetailProgram compile_fine_detail_program(
    const ColorAdjustmentState& state,
    int width,
    int height) {
  auto program = CompiledFineDetailProgram{};
  const auto values = std::array{
      state.sharpening, state.sharpen_radius, state.sharpen_detail,
      state.sharpen_masking, state.luminance_nr, state.color_nr};
  if (width <= 0 || height <= 0 ||
      !std::ranges::all_of(values, [](double value) {
        return std::isfinite(value);
      })) {
    return program;
  }
  const auto context = make_pipeline_color_context(state);
  program.rgb_to_xyz_d65 = context.rgb_to_xyz_d65;
  program.xyz_d65_to_rgb = context.xyz_d65_to_rgb;
  program.sharpen_strength = static_cast<float>(
      std::clamp(state.sharpening, -100.0, 100.0) / 100.0);
  const auto radius = std::clamp(state.sharpen_radius, 0.5, 3.0);
  const auto detail = std::clamp(state.sharpen_detail, 0.0, 100.0) / 100.0;
  program.sharpen_gain = static_cast<float>(1.9 + detail * 1.15);
  program.sharpen_mask_strength = static_cast<float>(
      std::clamp(state.sharpen_masking, -100.0, 100.0) / 100.0);
  program.luminance_nr_strength = static_cast<float>(
      std::clamp(state.luminance_nr, -100.0, 100.0) / 100.0);
  program.color_nr_strength = static_cast<float>(
      std::clamp(state.color_nr, -100.0, 100.0) / 100.0);
  program.sharpen_radius = static_cast<std::uint32_t>(
      std::max(1, static_cast<int>(std::lround(radius))));
  program.sharpen_edge_radius = static_cast<std::uint32_t>(std::max(
      static_cast<int>(program.sharpen_radius) + 2,
      static_cast<int>(std::lround(radius * 3.0))));
  program.width = static_cast<std::uint32_t>(width);
  program.height = static_cast<std::uint32_t>(height);
  if (std::abs(program.sharpen_strength) > 1.0e-6F) {
    program.active_mask |= CompiledFineDetailProgram::kSharpen;
  }
  if (std::abs(program.luminance_nr_strength) > 1.0e-6F) {
    program.active_mask |=
        CompiledFineDetailProgram::kLuminanceNoiseReduction;
  }
  if (std::abs(program.color_nr_strength) > 1.0e-6F) {
    program.active_mask |= CompiledFineDetailProgram::kColorNoiseReduction;
  }
  program.valid = true;
  if (!program.ready()) program.valid = false;
  return program;
}

bool apply_compiled_interactive_spatial_detail_program(
    std::vector<ColorPixel>* pixels,
    const CompiledSpatialDetailProgram& program) {
  if (pixels == nullptr || !program.ready() ||
      pixels->size() != static_cast<std::size_t>(program.width) * program.height) {
    return false;
  }
  if (!program.active()) return true;
  const auto pixel_count = pixels->size();
  auto luminance_buffer = std::vector<float>(pixel_count, 0.0F);
  for (auto index = std::size_t{0U}; index < pixel_count; ++index) {
    luminance_buffer[index] = static_cast<float>(luminance((*pixels)[index], program));
  }
  const auto texture_active =
      (program.active_mask & CompiledSpatialDetailProgram::kTexture) != 0U;
  const auto clarity_active =
      (program.active_mask & CompiledSpatialDetailProgram::kClarity) != 0U;
  const auto dehaze_active =
      (program.active_mask & CompiledSpatialDetailProgram::kDehaze) != 0U;
  const auto texture_blur = texture_active
      ? box_blur(luminance_buffer, static_cast<int>(program.width),
                 static_cast<int>(program.height), 2)
      : std::vector<float>{};
  const auto clarity_blur = clarity_active
      ? box_blur(luminance_buffer, static_cast<int>(program.width),
                 static_cast<int>(program.height), 8)
      : std::vector<float>{};
  const auto haze_blur = dehaze_active
      ? box_blur(luminance_buffer, static_cast<int>(program.width),
                 static_cast<int>(program.height), 20)
      : std::vector<float>{};
  for (auto y = std::uint32_t{0U}; y < program.height; ++y) {
    for (auto x = std::uint32_t{0U}; x < program.width; ++x) {
      const auto index = static_cast<std::size_t>(y) * program.width + x;
      const auto current = static_cast<double>(luminance_buffer[index]);
      auto activity = 0.0;
      if (x > 0U) activity = std::max(
          activity, std::abs(current - luminance_buffer[index - 1U]));
      if (x + 1U < program.width) activity = std::max(
          activity, std::abs(current - luminance_buffer[index + 1U]));
      if (y > 0U) activity = std::max(
          activity, std::abs(current - luminance_buffer[index - program.width]));
      if (y + 1U < program.height) activity = std::max(
          activity, std::abs(current - luminance_buffer[index + program.width]));
      const auto midtone = bell_weight(current, 0.5, 0.42);
      const auto shadow_guard = smoothstep(0.025, 0.12, current);
      const auto highlight = 1.0 - smoothstep(0.74, 0.98, current);
      const auto tone_guard = shadow_guard * highlight;
      auto target = current;
      if (texture_active) {
        const auto signal = current - texture_blur[index];
        const auto broad = clarity_active
            ? current - clarity_blur[index] : activity;
        const auto guard =
            1.0 - smoothstep(0.08, 0.36, std::abs(broad)) * 0.72;
        const auto gain = program.texture_strength >= 0.0F ? 1.30 : 0.48;
        target += program.texture_strength * signal *
            (0.46 + midtone * 0.70) * guard * tone_guard * gain;
      }
      if (clarity_active) {
        const auto signal = current - clarity_blur[index];
        const auto edge =
            1.0 - smoothstep(0.24, 0.58, std::abs(signal)) * 0.28;
        const auto structure = smoothstep(0.003, 0.035, activity);
        const auto gain = program.clarity_strength >= 0.0F ? 1.08 : 0.46;
        target += program.clarity_strength * signal *
            (0.36 + midtone * 0.82) * tone_guard * edge * structure * gain;
      }
      if (dehaze_active) {
        const auto signal = std::clamp(current - haze_blur[index], -0.18, 0.18);
        const auto activity_gate = smoothstep(0.0015, 0.018, activity);
        const auto edge_guard =
            1.0 - smoothstep(0.11, 0.18, std::abs(signal)) * 0.95;
        const auto gain = program.dehaze_strength >= 0.0F ? 0.95 : 0.36;
        target += program.dehaze_strength * signal *
            (0.52 + midtone * 0.78) * highlight * activity_gate * edge_guard * gain;
        const auto shadow = 1.0 - smoothstep(0.08, 0.20, current);
        const auto structure = smoothstep(0.008, 0.065, std::abs(signal)) *
                               activity_gate * edge_guard;
        if (program.dehaze_strength > 0.0F) {
          target *= std::max(
              0.0, 1.0 - program.dehaze_strength * shadow * structure * 0.52);
          target *= std::max(
              0.0, 1.0 - program.dehaze_strength * shadow * activity_gate *
                             edge_guard * 0.035);
        } else {
          target += (-program.dehaze_strength) * shadow * structure * 0.075;
        }
      }
      (*pixels)[index] = scale_to_luminance((*pixels)[index], target, program);
    }
  }
  return true;
}

}  // namespace photo_editor::pipeline
