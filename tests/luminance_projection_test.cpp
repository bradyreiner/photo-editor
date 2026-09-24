#include "color_working_space_runtime.h"
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>

using namespace photo_editor::pipeline;
int main() {
  std::uint32_t seed = 0x12bc4901U;
  const auto next = [&] {
    seed = seed * 1664525U + 1013904223U;
    return seed;
  };
  const auto channel = [&] {
    const auto bits = next();
    const auto value = std::exp2(-30.0 + double(bits & 65535U) * 45.0 / 65535.0);
    return static_cast<float>((bits & 0x10000U) ? value : -value);
  };
  std::size_t comparisons = 0;
  const auto check = [&](const ColorPixel& pixel, const PipelineColorContext& context) {
    ++comparisons;
    const auto expected = pixel_to_xyz_native(pixel, context).y;
    const auto actual = luminance_for_pixel(pixel, context);
    return (std::isnan(expected) && std::isnan(actual)) ||
        std::bit_cast<std::uint64_t>(expected) == std::bit_cast<std::uint64_t>(actual);
  };
  constexpr auto infinity = std::numeric_limits<float>::infinity();
  constexpr auto nan = std::numeric_limits<float>::quiet_NaN();
  const std::array special{
      ColorPixel{-0.0F, 0.0F, -0.0F, -0.0F}, ColorPixel{0, 0, 0, 1},
      ColorPixel{0.18F, 0.18F, 0.18F, 0.5F}, ColorPixel{-0.2F, 4, 8192, 0},
      ColorPixel{infinity, 1, 0, 1}, ColorPixel{1, -infinity, 0, 1},
      ColorPixel{infinity, -infinity, 0, 1}, ColorPixel{nan, 0, 0, 1},
      ColorPixel{0, nan, 0, 1}, ColorPixel{0, 0, nan, 1},
      ColorPixel{std::numeric_limits<float>::denorm_min(), 0, 0, 1},
      ColorPixel{std::numeric_limits<float>::max(), 0, 0, 1}};
  for (const auto profile : {"Linear sRGB", "Linear Display P3", "Linear ProPhoto RGB", "Linear Rec.2020"}) {
    ColorAdjustmentState state;
    state.color_pipeline.working_profile = working_color_profile_from_name(profile);
    const auto context = make_pipeline_color_context(state);
    for (const auto& pixel : special) if (!check(pixel, context)) return 1;
    for (int i = 0; i < 100000; ++i)
      if (!check({channel(), channel(), channel(), float(i % 5) / 4.0F}, context)) return 1;
  }
  // Also exercise arbitrary calibrated matrices, including negative Y weights.
  PipelineColorContext context;
  for (int i = 0; i < 100000; ++i) {
    for (auto& coefficient : context.rgb_to_xyz_native.values)
      coefficient = (double(next() & 65535U) / 65535.0 - 0.5) * 4.0;
    if (!check({channel(), channel(), channel(), 1}, context)) return 1;
  }
  std::cout << comparisons << " exact Y-row comparisons, including signed/HDR and nonfinite inputs, pass\n";
}
