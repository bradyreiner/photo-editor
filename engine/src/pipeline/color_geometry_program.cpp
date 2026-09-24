#include "photo_editor/pipeline/color_geometry_program.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <numbers>
#include <utility>

namespace photo_editor::pipeline {
namespace {

[[nodiscard]] float mirrored_coordinate(float value, std::uint32_t extent) {
  if (extent <= 1U) return 0.0F;
  const auto maximum = static_cast<float>(extent - 1U);
  const auto period = maximum * 2.0F;
  auto mirrored = std::fmod(value, period);
  if (mirrored < 0.0F) mirrored += period;
  return mirrored <= maximum ? mirrored : period - mirrored;
}

[[nodiscard]] ColorPixel sample(
    const std::vector<ColorPixel>& source,
    float x,
    float y,
    const CompiledGeometryStageProgram& program) {
  if (program.content_aware_crop_fill) {
    x = mirrored_coordinate(x, program.width);
    y = mirrored_coordinate(y, program.height);
  }
  x = std::clamp(x, 0.0F, static_cast<float>(program.width - 1U));
  y = std::clamp(y, 0.0F, static_cast<float>(program.height - 1U));
  const auto x0 = static_cast<std::uint32_t>(std::floor(x));
  const auto y0 = static_cast<std::uint32_t>(std::floor(y));
  const auto x1 = std::min(program.width - 1U, x0 + 1U);
  const auto y1 = std::min(program.height - 1U, y0 + 1U);
  const auto fx = x - static_cast<float>(x0);
  const auto fy = y - static_cast<float>(y0);
  const auto at = [&](std::uint32_t sample_x, std::uint32_t sample_y) {
    return source[static_cast<std::size_t>(sample_y) * program.width + sample_x];
  };
  const auto mix = [](float left, float right, float amount) {
    return left * (1.0F - amount) + right * amount;
  };
  const auto mix_channel = [&](auto ColorPixel::*member) {
    const auto top = mix(at(x0, y0).*member, at(x1, y0).*member, fx);
    const auto bottom = mix(at(x0, y1).*member, at(x1, y1).*member, fx);
    return mix(top, bottom, fy);
  };
  return {
      .red = mix_channel(&ColorPixel::red),
      .green = mix_channel(&ColorPixel::green),
      .blue = mix_channel(&ColorPixel::blue),
      .alpha = mix_channel(&ColorPixel::alpha),
  };
}

[[nodiscard]] float smoothstep(float edge0, float edge1, float value) {
  const auto t = std::clamp((value - edge0) / (edge1 - edge0), 0.0F, 1.0F);
  return t * t * (3.0F - 2.0F * t);
}

}  // namespace

bool CompiledGeometryStageProgram::ready() const noexcept {
  return valid && width > 0U && height > 0U &&
         static_cast<std::uint64_t>(width) * height <=
             std::numeric_limits<std::size_t>::max();
}

CompiledGeometryStageProgram compile_geometry_stage_program(
    const ColorAdjustmentState& state,
    int width,
    int height) {
  auto program = CompiledGeometryStageProgram{};
  const auto values = std::array{
      state.lens_distortion, state.chromatic_aberration,
      state.perspective_vertical, state.perspective_horizontal,
      state.transform_rotation, state.transform_scale,
      state.transform_skew_x, state.transform_skew_y,
      state.liquify_strength, state.liquify_center_x,
      state.liquify_center_y, state.liquify_radius};
  if (width <= 0 || height <= 0 ||
      !std::ranges::all_of(values, [](double value) {
        return std::isfinite(value);
      })) {
    return program;
  }
  const auto center_x = static_cast<double>(width - 1) * 0.5;
  const auto center_y = static_cast<double>(height - 1) * 0.5;
  program.distortion_strength = static_cast<float>(
      std::clamp(state.lens_distortion, -100.0, 100.0) / 100.0);
  program.chromatic_aberration_strength = static_cast<float>(
      std::clamp(state.chromatic_aberration / 100.0, 0.0, 1.0));
  program.vertical_perspective = static_cast<float>(
      std::clamp(state.perspective_vertical, -100.0, 100.0) / 100.0);
  program.horizontal_perspective = static_cast<float>(
      std::clamp(state.perspective_horizontal, -100.0, 100.0) / 100.0);
  program.transform_rotation = static_cast<float>(
      std::clamp(state.transform_rotation, -45.0, 45.0) *
      std::numbers::pi_v<double> / 180.0);
  program.transform_scale =
      static_cast<float>(std::clamp(state.transform_scale, 0.25, 4.0));
  program.transform_skew_x = static_cast<float>(
      std::clamp(state.transform_skew_x, -100.0, 100.0) / 100.0 * 0.60);
  program.transform_skew_y = static_cast<float>(
      std::clamp(state.transform_skew_y, -100.0, 100.0) / 100.0 * 0.60);
  program.liquify_strength = static_cast<float>(
      std::clamp(state.liquify_strength, -100.0, 100.0) / 100.0);
  program.liquify_center_x = static_cast<float>(
      std::clamp(state.liquify_center_x, 0.0, 1.0) * (width - 1));
  program.liquify_center_y = static_cast<float>(
      std::clamp(state.liquify_center_y, 0.0, 1.0) * (height - 1));
  program.liquify_radius = static_cast<float>(
      std::clamp(state.liquify_radius, 0.02, 1.0) * std::max(width, height));
  program.center_x = static_cast<float>(center_x);
  program.center_y = static_cast<float>(center_y);
  program.maximum_radius = static_cast<float>(
      std::max(1.0, std::hypot(center_x, center_y)));
  program.width = static_cast<std::uint32_t>(width);
  program.height = static_cast<std::uint32_t>(height);
  program.content_aware_crop_fill = state.content_aware_crop_fill;
  program.active = width > 1 && height > 1 &&
      (std::abs(program.distortion_strength) > 1.0e-6F ||
       program.chromatic_aberration_strength > 1.0e-6F ||
       std::abs(program.vertical_perspective) > 1.0e-6F ||
       std::abs(program.horizontal_perspective) > 1.0e-6F ||
       std::abs(program.transform_rotation) > 1.0e-6F ||
       std::abs(program.transform_scale - 1.0F) > 1.0e-6F ||
       std::abs(program.transform_skew_x) > 1.0e-6F ||
       std::abs(program.transform_skew_y) > 1.0e-6F ||
       std::abs(program.liquify_strength) > 1.0e-6F);
  program.valid = true;
  return program;
}

bool apply_compiled_geometry_stage_program(
    std::vector<ColorPixel>* pixels,
    const CompiledGeometryStageProgram& program) {
  if (pixels == nullptr || !program.ready() ||
      pixels->size() != static_cast<std::size_t>(program.width) * program.height) {
    return false;
  }
  if (!program.active) return true;
  const auto source = *pixels;
  auto output = std::vector<ColorPixel>(source.size());
  for (std::uint32_t y = 0U; y < program.height; ++y) {
    for (std::uint32_t x = 0U; x < program.width; ++x) {
      auto dx = static_cast<float>(x) - program.center_x;
      auto dy = static_cast<float>(y) - program.center_y;
      if (std::abs(program.transform_rotation) > 1.0e-6F ||
          std::abs(program.transform_scale - 1.0F) > 1.0e-6F ||
          std::abs(program.transform_skew_x) > 1.0e-6F ||
          std::abs(program.transform_skew_y) > 1.0e-6F) {
        dx /= program.transform_scale;
        dy /= program.transform_scale;
        const auto skewed_dx = dx + dy * program.transform_skew_x;
        const auto skewed_dy = dy + dx * program.transform_skew_y;
        const auto cosine = std::cos(-program.transform_rotation);
        const auto sine = std::sin(-program.transform_rotation);
        dx = skewed_dx * cosine - skewed_dy * sine;
        dy = skewed_dx * sine + skewed_dy * cosine;
      }
      const auto radius = std::hypot(dx, dy) / program.maximum_radius;
      const auto radius_squared = radius * radius;
      const auto distortion_scale = 1.0F / std::max(
          0.40F, 1.0F + program.distortion_strength *
              (0.55F * radius_squared +
               0.20F * radius_squared * radius_squared));
      const auto normalized_dx = program.center_x > 0.0F
          ? dx / program.center_x : 0.0F;
      const auto normalized_dy = program.center_y > 0.0F
          ? dy / program.center_y : 0.0F;
      auto sample_x = program.center_x + dx * distortion_scale * std::max(
          0.35F, 1.0F + program.vertical_perspective * normalized_dy * 0.45F);
      auto sample_y = program.center_y + dy * distortion_scale * std::max(
          0.35F, 1.0F + program.horizontal_perspective * normalized_dx * 0.45F);
      if (std::abs(program.liquify_strength) > 1.0e-6F) {
        const auto liquify_dx = sample_x - program.liquify_center_x;
        const auto liquify_dy = sample_y - program.liquify_center_y;
        const auto distance = std::hypot(liquify_dx, liquify_dy);
        if (distance < program.liquify_radius) {
          const auto falloff = 1.0F - smoothstep(
              0.0F, 1.0F, distance / std::max(1.0F, program.liquify_radius));
          const auto scale = std::clamp(
              1.0F - program.liquify_strength * 0.55F * falloff,
              0.30F, 2.50F);
          sample_x = program.liquify_center_x + liquify_dx * scale;
          sample_y = program.liquify_center_y + liquify_dy * scale;
        }
      }
      auto corrected = sample(source, sample_x, sample_y, program);
      if (program.chromatic_aberration_strength > 1.0e-6F &&
          radius > 1.0e-6F) {
        const auto inverse_length = 1.0F / std::max(1.0e-6F, std::hypot(dx, dy));
        const auto radial_x = dx * inverse_length;
        const auto radial_y = dy * inverse_length;
        const auto shift = program.chromatic_aberration_strength *
                           radius_squared * 2.2F;
        corrected.red = sample(
            source, sample_x - radial_x * shift * 1.15F,
            sample_y - radial_y * shift * 1.15F, program).red;
        corrected.blue = sample(
            source, sample_x - radial_x * shift * 0.85F,
            sample_y - radial_y * shift * 0.85F, program).blue;
      }
      corrected.alpha = std::clamp(corrected.alpha, 0.0F, 1.0F);
      output[static_cast<std::size_t>(y) * program.width + x] = corrected;
    }
  }
  *pixels = std::move(output);
  return true;
}

}  // namespace photo_editor::pipeline
