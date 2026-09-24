#include "color_spatial_runtime.h"

#include "color_adjustment_scheduler.h"
#include "color_math_runtime.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <functional>
#include <numbers>
#include <utility>
#include <vector>

namespace photo_editor::pipeline {

namespace {

constexpr double kPi = std::numbers::pi_v<double>;

[[nodiscard]] double mirrored_sample_coordinate(double value, int extent) {
  if (extent <= 1) {
    return 0.0;
  }
  const auto max_coordinate = static_cast<double>(extent - 1);
  const auto period = max_coordinate * 2.0;
  auto mirrored = std::fmod(value, period);
  if (mirrored < 0.0) {
    mirrored += period;
  }
  return mirrored <= max_coordinate ? mirrored : period - mirrored;
}

[[nodiscard]] ColorPixel sample_bilinear_crop_fill(
    const std::vector<ColorPixel>& pixels,
    int width,
    int height,
    double x,
    double y,
    bool content_aware_fill) {
  if (!content_aware_fill) {
    return sample_bilinear(pixels, width, height, x, y);
  }
  return sample_bilinear(
      pixels,
      width,
      height,
      mirrored_sample_coordinate(x, width),
      mirrored_sample_coordinate(y, height));
}

}  // namespace

ColorPixel sample_bilinear(
    const std::vector<ColorPixel>& pixels,
    int width,
    int height,
    double x,
    double y) {
  if (pixels.empty() || width <= 0 || height <= 0) {
    return {};
  }

  const auto clamped_x = std::clamp(x, 0.0, static_cast<double>(width - 1));
  const auto clamped_y = std::clamp(y, 0.0, static_cast<double>(height - 1));
  const auto x0 = static_cast<int>(std::floor(clamped_x));
  const auto y0 = static_cast<int>(std::floor(clamped_y));
  const auto x1 = std::min(width - 1, x0 + 1);
  const auto y1 = std::min(height - 1, y0 + 1);
  const auto fx = clamped_x - static_cast<double>(x0);
  const auto fy = clamped_y - static_cast<double>(y0);

  const auto& top_left = pixels[static_cast<std::size_t>(y0 * width + x0)];
  const auto& top_right = pixels[static_cast<std::size_t>(y0 * width + x1)];
  const auto& bottom_left = pixels[static_cast<std::size_t>(y1 * width + x0)];
  const auto& bottom_right = pixels[static_cast<std::size_t>(y1 * width + x1)];

  const auto lerp = [](double a, double b, double t) { return a * (1.0 - t) + b * t; };
  const auto mix_channel = [&](auto ColorPixel::*member) {
    const auto top = lerp(top_left.*member, top_right.*member, fx);
    const auto bottom = lerp(bottom_left.*member, bottom_right.*member, fx);
    return lerp(top, bottom, fy);
  };

  return ColorPixel{
      .red = static_cast<float>(mix_channel(&ColorPixel::red)),
      .green = static_cast<float>(mix_channel(&ColorPixel::green)),
      .blue = static_cast<float>(mix_channel(&ColorPixel::blue)),
      .alpha = static_cast<float>(mix_channel(&ColorPixel::alpha)),
  };
}

void apply_lens_vignette_correction(
    std::vector<ColorPixel>* pixels,
    int width,
    int height,
    const ColorAdjustmentState& state) {
  if (pixels == nullptr || pixels->empty() || width <= 1 || height <= 1) {
    return;
  }
  const auto vignette_strength =
      clamp_signed_percent(state.lens_vignette) / 100.0;
  if (std::abs(vignette_strength) <= 1.0e-6) {
    return;
  }
  const auto center_x = static_cast<double>(width - 1) * 0.5;
  const auto center_y = static_cast<double>(height - 1) * 0.5;
  const auto max_radius = std::max(1.0, std::hypot(center_x, center_y));
  run_color_adjustment_parallel_for(
      height,
      std::function<void(int)>([&](int y) {
        const auto row_offset =
            static_cast<std::size_t>(y) * static_cast<std::size_t>(width);
        for (int x = 0; x < width; ++x) {
          const auto dx = static_cast<double>(x) - center_x;
          const auto dy = static_cast<double>(y) - center_y;
          const auto radius = std::hypot(dx, dy) / max_radius;
          const auto edge_weight = smoothstep(0.22, 1.0, radius);
          const auto gain = std::max(
              0.20,
              1.0 + vignette_strength * edge_weight * 0.85);
          auto& pixel = (*pixels)[row_offset + static_cast<std::size_t>(x)];
          pixel.red = static_cast<float>(static_cast<double>(pixel.red) * gain);
          pixel.green = static_cast<float>(static_cast<double>(pixel.green) * gain);
          pixel.blue = static_cast<float>(static_cast<double>(pixel.blue) * gain);
        }
      }));
}

void apply_geometry_corrections(
    std::vector<ColorPixel>* pixels,
    int width,
    int height,
    const ColorAdjustmentState& state) {
  if (pixels == nullptr || pixels->empty() || width <= 1 || height <= 1) {
    return;
  }

  const auto distortion_strength = clamp_signed_percent(state.lens_distortion) / 100.0;
  const auto ca_strength = clamp_unit(state.chromatic_aberration / 100.0);
  const auto vertical_perspective = clamp_signed_percent(state.perspective_vertical) / 100.0;
  const auto horizontal_perspective = clamp_signed_percent(state.perspective_horizontal) / 100.0;
  const auto transform_rotation =
      std::clamp(state.transform_rotation, -45.0, 45.0) * kPi / 180.0;
  const auto transform_scale = std::clamp(state.transform_scale, 0.25, 4.0);
  const auto transform_skew_x = clamp_signed_percent(state.transform_skew_x) / 100.0 * 0.60;
  const auto transform_skew_y = clamp_signed_percent(state.transform_skew_y) / 100.0 * 0.60;
  const auto liquify_strength = clamp_signed_percent(state.liquify_strength) / 100.0;
  const auto liquify_center_x =
      std::clamp(state.liquify_center_x, 0.0, 1.0) * static_cast<double>(width - 1);
  const auto liquify_center_y =
      std::clamp(state.liquify_center_y, 0.0, 1.0) * static_cast<double>(height - 1);
  const auto liquify_radius =
      std::clamp(state.liquify_radius, 0.02, 1.0) *
      std::max(static_cast<double>(width), static_cast<double>(height));
  if (std::abs(distortion_strength) <= 1e-6 && ca_strength <= 1e-6 &&
      std::abs(vertical_perspective) <= 1e-6 &&
      std::abs(horizontal_perspective) <= 1e-6 &&
      std::abs(transform_rotation) <= 1e-6 &&
      std::abs(transform_scale - 1.0) <= 1e-6 &&
      std::abs(transform_skew_x) <= 1e-6 &&
      std::abs(transform_skew_y) <= 1e-6 &&
      std::abs(liquify_strength) <= 1e-6) {
    return;
  }

  const auto source = *pixels;
  std::vector<ColorPixel> corrected(source.size());
  const auto center_x = static_cast<double>(width - 1) * 0.5;
  const auto center_y = static_cast<double>(height - 1) * 0.5;
  const auto max_radius = std::max(1.0, std::hypot(center_x, center_y));

  for (int y = 0; y < height; ++y) {
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
      auto sample_x = center_x + dx * distortion_scale * vertical_scale;
      auto sample_y = center_y + dy * distortion_scale * horizontal_scale;
      if (std::abs(liquify_strength) > 1e-6) {
        const auto liquify_dx = sample_x - liquify_center_x;
        const auto liquify_dy = sample_y - liquify_center_y;
        const auto liquify_distance = std::hypot(liquify_dx, liquify_dy);
        if (liquify_distance < liquify_radius) {
          const auto falloff = 1.0 - smoothstep(
              0.0,
              1.0,
              liquify_distance / std::max(1.0, liquify_radius));
          const auto liquify_scale =
              std::clamp(1.0 - liquify_strength * 0.55 * falloff, 0.30, 2.50);
          sample_x = liquify_center_x + liquify_dx * liquify_scale;
          sample_y = liquify_center_y + liquify_dy * liquify_scale;
        }
      }

      auto corrected_pixel = sample_bilinear_crop_fill(
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
        const auto red_sample = sample_bilinear_crop_fill(
            source,
            width,
            height,
            sample_x - radial_x * shift * 1.15,
            sample_y - radial_y * shift * 1.15,
            state.content_aware_crop_fill);
        const auto blue_sample = sample_bilinear_crop_fill(
            source,
            width,
            height,
            sample_x - radial_x * shift * 0.85,
            sample_y - radial_y * shift * 0.85,
            state.content_aware_crop_fill);
        corrected_pixel.red = red_sample.red;
        corrected_pixel.blue = blue_sample.blue;
      }

      corrected[static_cast<std::size_t>(y * width + x)] = ColorPixel{
          .red = corrected_pixel.red,
          .green = corrected_pixel.green,
          .blue = corrected_pixel.blue,
          .alpha = static_cast<float>(clamp_unit(corrected_pixel.alpha)),
      };
    }
  }

  *pixels = std::move(corrected);
}

}  // namespace photo_editor::pipeline
