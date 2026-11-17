#include "pipeline/color_spatial_runtime.h"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string_view>
#include <vector>

namespace {

using namespace photo_editor::pipeline;

struct Scenario final {
  std::string_view name;
  ColorAdjustmentState state;
};

[[nodiscard]] std::vector<ColorPixel> make_pixels(int width, int height) {
  auto pixels = std::vector<ColorPixel>(
      static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
  auto value = std::uint32_t{0x6d2b79f5U};
  const auto next_channel = [&value]() {
    value = value * 1'664'525U + 1'013'904'223U;
    return static_cast<float>((value >> 8U) & 0x00ff'ffffU) /
           static_cast<float>(0x00ff'ffffU);
  };
  for (auto& pixel : pixels) {
    pixel = ColorPixel{
        .red = next_channel(),
        .green = next_channel(),
        .blue = next_channel(),
        .alpha = next_channel(),
    };
  }
  return pixels;
}

[[nodiscard]] std::uint64_t pixel_hash(const std::vector<ColorPixel>& pixels) {
  auto hash = std::uint64_t{14'695'981'039'346'656'037ULL};
  for (const auto& pixel : pixels) {
    for (const auto value : {pixel.red, pixel.green, pixel.blue, pixel.alpha}) {
      hash ^= std::bit_cast<std::uint32_t>(value);
      hash *= 1'099'511'628'211ULL;
    }
  }
  return hash;
}

[[nodiscard]] bool same_pixels(
    const std::vector<ColorPixel>& left,
    const std::vector<ColorPixel>& right) {
  if (left.size() != right.size()) return false;
  for (std::size_t index = 0U; index < left.size(); ++index) {
    const auto& a = left[index];
    const auto& b = right[index];
    if (std::bit_cast<std::uint32_t>(a.red) != std::bit_cast<std::uint32_t>(b.red) ||
        std::bit_cast<std::uint32_t>(a.green) != std::bit_cast<std::uint32_t>(b.green) ||
        std::bit_cast<std::uint32_t>(a.blue) != std::bit_cast<std::uint32_t>(b.blue) ||
        std::bit_cast<std::uint32_t>(a.alpha) != std::bit_cast<std::uint32_t>(b.alpha)) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] double mirrored_coordinate(double value, int extent) {
  if (extent <= 1) return 0.0;
  const auto maximum = static_cast<double>(extent - 1);
  const auto period = maximum * 2.0;
  auto mirrored = std::fmod(value, period);
  if (mirrored < 0.0) mirrored += period;
  return mirrored <= maximum ? mirrored : period - mirrored;
}

[[nodiscard]] ColorPixel sample_crop_fill(
    const std::vector<ColorPixel>& pixels,
    int width,
    int height,
    double x,
    double y,
    bool content_aware_fill) {
  if (!content_aware_fill) return sample_bilinear(pixels, width, height, x, y);
  return sample_bilinear(
      pixels,
      width,
      height,
      mirrored_coordinate(x, width),
      mirrored_coordinate(y, height));
}

// This preserves the pre-optimization per-pixel rotation calculation as a
// runtime reference. The tests intentionally use the production bilinear
// sampler so they compare the transform coordinates and channel selection.
[[nodiscard]] std::vector<ColorPixel> reference_geometry(
    const std::vector<ColorPixel>& source,
    int width,
    int height,
    const ColorAdjustmentState& state) {
  constexpr auto kPi = 3.14159265358979323846;
  const auto distortion_strength = clamp_signed_percent(state.lens_distortion) / 100.0;
  const auto ca_strength = clamp_unit(state.chromatic_aberration / 100.0);
  const auto vertical_perspective = clamp_signed_percent(state.perspective_vertical) / 100.0;
  const auto horizontal_perspective = clamp_signed_percent(state.perspective_horizontal) / 100.0;
  const auto transform_rotation =
      std::clamp(state.transform_rotation, -45.0, 45.0) * kPi / 180.0;
  const auto transform_scale = std::clamp(state.transform_scale, 0.25, 4.0);
  const auto transform_skew_x = clamp_signed_percent(state.transform_skew_x) / 100.0 * 0.60;
  const auto transform_skew_y = clamp_signed_percent(state.transform_skew_y) / 100.0 * 0.60;
  if (std::abs(clamp_signed_percent(state.liquify_strength) / 100.0) > 1e-6) return {};

  auto corrected = std::vector<ColorPixel>(source.size());
  const auto center_x = static_cast<double>(width - 1) * 0.5;
  const auto center_y = static_cast<double>(height - 1) * 0.5;
  const auto max_radius = std::max(1.0, std::hypot(center_x, center_y));
  for (int y = 0; y < height; ++y) {
    const auto row_offset = static_cast<std::size_t>(y) * static_cast<std::size_t>(width);
    for (int x = 0; x < width; ++x) {
      auto dx = static_cast<double>(x) - center_x;
      auto dy = static_cast<double>(y) - center_y;
      if (std::abs(transform_rotation) > 1e-6 ||
          std::abs(transform_scale - 1.0) > 1e-6 ||
          std::abs(transform_skew_x) > 1e-6 ||
          std::abs(transform_skew_y) > 1e-6) {
        dx /= transform_scale;
        dy /= transform_scale;
        const auto skewed_dx = dx + dy * transform_skew_x;
        const auto skewed_dy = dy + dx * transform_skew_y;
        const auto cosine = std::cos(-transform_rotation);
        const auto sine = std::sin(-transform_rotation);
        dx = skewed_dx * cosine - skewed_dy * sine;
        dy = skewed_dx * sine + skewed_dy * cosine;
      }
      const auto radius = std::hypot(dx, dy) / max_radius;
      const auto radius_sq = radius * radius;
      const auto distortion_scale = 1.0 / std::max(
          0.40,
          1.0 + distortion_strength *
                    (0.55 * radius_sq + 0.20 * radius_sq * radius_sq));
      const auto normalized_dx = center_x > 0.0 ? dx / center_x : 0.0;
      const auto normalized_dy = center_y > 0.0 ? dy / center_y : 0.0;
      const auto vertical_scale =
          std::max(0.35, 1.0 + vertical_perspective * normalized_dy * 0.45);
      const auto horizontal_scale =
          std::max(0.35, 1.0 + horizontal_perspective * normalized_dx * 0.45);
      const auto sample_x = center_x + dx * distortion_scale * vertical_scale;
      const auto sample_y = center_y + dy * distortion_scale * horizontal_scale;

      auto corrected_pixel = sample_crop_fill(
          source,
          width,
          height,
          sample_x,
          sample_y,
          state.content_aware_crop_fill);
      if (ca_strength > 1e-6 && radius > 1e-6) {
        const auto inverse_length = 1.0 / std::max(1e-6, std::hypot(dx, dy));
        const auto radial_x = dx * inverse_length;
        const auto radial_y = dy * inverse_length;
        const auto shift = ca_strength * radius_sq * 2.2;
        const auto red_sample = sample_crop_fill(
            source, width, height,
            sample_x - radial_x * shift * 1.15,
            sample_y - radial_y * shift * 1.15,
            state.content_aware_crop_fill);
        const auto blue_sample = sample_crop_fill(
            source, width, height,
            sample_x - radial_x * shift * 0.85,
            sample_y - radial_y * shift * 0.85,
            state.content_aware_crop_fill);
        corrected_pixel.red = red_sample.red;
        corrected_pixel.blue = blue_sample.blue;
      }
      corrected[row_offset + static_cast<std::size_t>(x)] = ColorPixel{
          .red = corrected_pixel.red,
          .green = corrected_pixel.green,
          .blue = corrected_pixel.blue,
          .alpha = static_cast<float>(clamp_unit(corrected_pixel.alpha)),
      };
    }
  }
  return corrected;
}

[[nodiscard]] std::int64_t median(std::vector<std::int64_t> values) {
  std::sort(values.begin(), values.end());
  return values.at(values.size() / 2U);
}

[[nodiscard]] bool run_case(const Scenario& scenario) {
  constexpr auto kWidth = 257;
  constexpr auto kHeight = 193;
  const auto source = make_pixels(kWidth, kHeight);
  auto expected = reference_geometry(source, kWidth, kHeight, scenario.state);
  auto actual = source;
  apply_geometry_corrections(&actual, kWidth, kHeight, scenario.state);
  if (expected.size() != actual.size() || !same_pixels(actual, expected)) {
    std::cerr << "exact geometry reference parity failed for " << scenario.name << '\n';
    return false;
  }
  std::cout << "geometry_transform_case name=" << scenario.name
            << " exact_pixel_parity=1 hash=" << pixel_hash(actual) << '\n';
  return true;
}

[[nodiscard]] bool benchmark_production_geometry() {
  constexpr auto kWidth = 1920;
  constexpr auto kHeight = 1280;
  constexpr auto kWarmupCount = 2U;
  constexpr auto kTrialCount = 9U;
  const auto source = make_pixels(kWidth, kHeight);
  auto state = ColorAdjustmentState{};
  state.transform_rotation = 37.0;
  state.transform_scale = 1.12;
  state.transform_skew_x = 9.0;
  state.transform_skew_y = -6.0;

  auto samples = std::vector<std::int64_t>{};
  samples.reserve(kTrialCount);
  auto expected_hash = std::uint64_t{0};
  for (auto iteration = 0U; iteration < kWarmupCount + kTrialCount; ++iteration) {
    auto pixels = source;
    const auto start = std::chrono::steady_clock::now();
    apply_geometry_corrections(&pixels, kWidth, kHeight, state);
    const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
                             std::chrono::steady_clock::now() - start)
                             .count();
    const auto actual_hash = pixel_hash(pixels);
    if (iteration == 0U) {
      expected_hash = actual_hash;
    } else if (actual_hash != expected_hash) {
      std::cerr << "production geometry result changed between iterations\n";
      return false;
    }
    if (iteration >= kWarmupCount) samples.push_back(elapsed);
  }
  const auto median_us = median(std::move(samples));
  std::cout << "geometry_transform_benchmark width=" << kWidth
            << " height=" << kHeight
            << " pixels=" << static_cast<std::uint64_t>(kWidth) *
                                  static_cast<std::uint64_t>(kHeight)
            << " trials=" << kTrialCount
            << " median_us=" << median_us
            << " hash=" << expected_hash << '\n';
  return true;
}

}  // namespace

int main() {
  auto positive_rotation = ColorAdjustmentState{};
  positive_rotation.transform_rotation = 45.0;
  positive_rotation.transform_scale = 1.35;

  auto negative_rotation = ColorAdjustmentState{};
  negative_rotation.transform_rotation = -45.0;
  negative_rotation.transform_scale = 0.72;
  negative_rotation.transform_skew_x = 17.0;
  negative_rotation.transform_skew_y = -11.0;

  auto identity_lens = ColorAdjustmentState{};
  identity_lens.lens_distortion = 31.0;
  identity_lens.perspective_vertical = -9.0;

  auto zero_rotation_skew_scale = ColorAdjustmentState{};
  zero_rotation_skew_scale.transform_scale = 1.58;
  zero_rotation_skew_scale.transform_skew_x = 19.0;
  zero_rotation_skew_scale.transform_skew_y = -8.0;

  auto content_aware_aberration = ColorAdjustmentState{};
  content_aware_aberration.transform_rotation = -45.0;
  content_aware_aberration.transform_scale = 0.82;
  content_aware_aberration.chromatic_aberration = 24.0;
  content_aware_aberration.lens_distortion = 12.0;
  content_aware_aberration.content_aware_crop_fill = true;

  auto ca_below_threshold = ColorAdjustmentState{};
  ca_below_threshold.transform_rotation = 23.0;
  ca_below_threshold.chromatic_aberration = 0.00009;

  auto ca_above_threshold = ColorAdjustmentState{};
  ca_above_threshold.transform_rotation = -23.0;
  ca_above_threshold.chromatic_aberration = 0.00011;

  auto tiny_nonzero_distortion = ColorAdjustmentState{};
  tiny_nonzero_distortion.transform_rotation = 11.0;
  tiny_nonzero_distortion.lens_distortion = 0.00011;

  const auto scenarios = std::array{
      Scenario{"positive_45_rotation", positive_rotation},
      Scenario{"negative_45_rotation_skew_scale", negative_rotation},
      Scenario{"identity_transform_active_lens", identity_lens},
      Scenario{"zero_rotation_skew_scale", zero_rotation_skew_scale},
      Scenario{"content_aware_ca", content_aware_aberration},
      Scenario{"ca_just_below_threshold", ca_below_threshold},
      Scenario{"ca_just_above_threshold", ca_above_threshold},
      Scenario{"tiny_nonzero_distortion", tiny_nonzero_distortion},
  };
  for (const auto& scenario : scenarios) {
    if (!run_case(scenario)) return EXIT_FAILURE;
  }
  if (!benchmark_production_geometry()) return EXIT_FAILURE;
  return EXIT_SUCCESS;
}
