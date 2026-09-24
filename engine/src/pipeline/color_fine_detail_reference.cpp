#include "photo_editor/pipeline/color_spatial_detail_program.h"

#include "color_blur_runtime.h"
#include "color_math_runtime.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

namespace photo_editor::pipeline {
namespace {

[[nodiscard]] std::array<double, 3> multiply(
    const ColorMatrix3& matrix,
    const std::array<double, 3>& value) {
  return {
      matrix.values[0] * value[0] + matrix.values[1] * value[1] +
          matrix.values[2] * value[2],
      matrix.values[3] * value[0] + matrix.values[4] * value[1] +
          matrix.values[5] * value[2],
      matrix.values[6] * value[0] + matrix.values[7] * value[1] +
          matrix.values[8] * value[2],
  };
}

[[nodiscard]] std::array<double, 3> to_oklab(
    const ColorPixel& pixel,
    const CompiledFineDetailProgram& program) {
  const auto xyz = multiply(
      program.rgb_to_xyz_d65,
      {pixel.red, pixel.green, pixel.blue});
  const auto l = std::cbrt(
      0.8189330101 * xyz[0] + 0.3618667424 * xyz[1] -
      0.1288597137 * xyz[2]);
  const auto m = std::cbrt(
      0.0329845436 * xyz[0] + 0.9293118715 * xyz[1] +
      0.0361456387 * xyz[2]);
  const auto s = std::cbrt(
      0.0482003018 * xyz[0] + 0.2643662691 * xyz[1] +
      0.6338517070 * xyz[2]);
  return {
      0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s,
      1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s,
      0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s,
  };
}

[[nodiscard]] ColorPixel from_oklab(
    double lightness,
    double a,
    double b,
    float alpha,
    const CompiledFineDetailProgram& program) {
  const auto lr = lightness + 0.3963377774 * a + 0.2158037573 * b;
  const auto mr = lightness - 0.1055613458 * a - 0.0638541728 * b;
  const auto sr = lightness - 0.0894841775 * a - 1.2914855480 * b;
  const auto l = lr * lr * lr;
  const auto m = mr * mr * mr;
  const auto s = sr * sr * sr;
  const auto rgb = multiply(
      program.xyz_d65_to_rgb,
      {
          1.2270138511 * l - 0.5577999807 * m + 0.2812561490 * s,
          -0.0405801784 * l + 1.1122568696 * m - 0.0716766787 * s,
          -0.0763812845 * l - 0.4214819784 * m + 1.5861632204 * s,
      });
  return {
      .red = static_cast<float>(rgb[0]),
      .green = static_cast<float>(rgb[1]),
      .blue = static_cast<float>(rgb[2]),
      .alpha = alpha,
  };
}

}  // namespace

bool apply_compiled_fine_detail_program(
    std::vector<ColorPixel>* pixels,
    const CompiledFineDetailProgram& program) {
  if (pixels == nullptr || !program.ready() ||
      pixels->size() != static_cast<std::size_t>(program.width) * program.height) {
    return false;
  }
  if (!program.active()) return true;
  const auto count = pixels->size();
  auto lightness = std::vector<float>(count);
  auto chroma_a = std::vector<float>(count);
  auto chroma_b = std::vector<float>(count);
  for (auto index = std::size_t{0U}; index < count; ++index) {
    const auto oklab = to_oklab((*pixels)[index], program);
    lightness[index] = static_cast<float>(oklab[0]);
    chroma_a[index] = static_cast<float>(oklab[1]);
    chroma_b[index] = static_cast<float>(oklab[2]);
  }
  const auto width = static_cast<int>(program.width);
  const auto height = static_cast<int>(program.height);
  if ((program.active_mask & CompiledFineDetailProgram::kSharpen) != 0U) {
    const auto sharp_blur = gaussian_approximate_blur(
        lightness, width, height, static_cast<int>(program.sharpen_radius));
    auto edge_blur = std::vector<float>{};
    if (program.sharpen_mask_strength > 1.0e-6F) {
      edge_blur = gaussian_approximate_blur(
          lightness, width, height,
          static_cast<int>(program.sharpen_edge_radius));
    }
    for (auto index = std::size_t{0U}; index < count; ++index) {
      const auto signal = lightness[index] - sharp_blur[index];
      auto gate = 1.0;
      if (!edge_blur.empty()) {
        gate = smoothstep(
            0.0015,
            0.012 + 0.06 * program.sharpen_mask_strength,
            std::abs(static_cast<double>(lightness[index] - edge_blur[index])));
      }
      lightness[index] += static_cast<float>(
          signal * program.sharpen_strength * program.sharpen_gain * gate);
    }
  }
  if ((program.active_mask &
       CompiledFineDetailProgram::kLuminanceNoiseReduction) != 0U) {
    const auto nr_blur = gaussian_approximate_blur(
        lightness, width, height, 2);
    const auto edge_blur = gaussian_approximate_blur(
        lightness, width, height, 5);
    for (auto index = std::size_t{0U}; index < count; ++index) {
      const auto edge = std::abs(
          static_cast<double>(lightness[index] - edge_blur[index]));
      const auto preserve = 1.0 - smoothstep(0.005, 0.04, edge);
      const auto blend = program.luminance_nr_strength * 0.85 * preserve;
      lightness[index] = static_cast<float>(
          lightness[index] * (1.0 - blend) + nr_blur[index] * blend);
    }
  }
  if ((program.active_mask &
       CompiledFineDetailProgram::kColorNoiseReduction) != 0U) {
    const auto a_blur = gaussian_approximate_blur(chroma_a, width, height, 3);
    const auto b_blur = gaussian_approximate_blur(chroma_b, width, height, 3);
    const auto blend = program.color_nr_strength * 0.90;
    for (auto index = std::size_t{0U}; index < count; ++index) {
      chroma_a[index] = static_cast<float>(
          chroma_a[index] * (1.0 - blend) + a_blur[index] * blend);
      chroma_b[index] = static_cast<float>(
          chroma_b[index] * (1.0 - blend) + b_blur[index] * blend);
    }
  }
  for (auto index = std::size_t{0U}; index < count; ++index) {
    (*pixels)[index] = from_oklab(
        lightness[index], chroma_a[index], chroma_b[index],
        (*pixels)[index].alpha, program);
  }
  return true;
}

}  // namespace photo_editor::pipeline
