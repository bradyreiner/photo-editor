#include "photo_editor/pipeline/color_adjustments.h"

#include "color_adjustment_kernel_runtime.h"
#include "color_adjustment_scheduler.h"
#include "color_curve_evaluator.h"
#include "color_working_space_runtime.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <functional>
#include <ranges>

namespace photo_editor::pipeline {

namespace {

using StageClock = std::chrono::steady_clock;

[[nodiscard]] bool valid_surface(
    const std::vector<ColorPixel>* pixels,
    int width,
    int height) {
  return pixels != nullptr && width > 0 && height > 0 &&
         pixels->size() == static_cast<std::size_t>(width) *
                               static_cast<std::size_t>(height);
}

[[nodiscard]] bool finite_surface(
    const std::vector<ColorPixel>& pixels,
    bool input_finite_validated = false) {
  if (input_finite_validated) {
    return true;
  }
  return std::ranges::all_of(pixels, [](const ColorPixel& pixel) {
    return std::isfinite(pixel.red) && std::isfinite(pixel.green) &&
           std::isfinite(pixel.blue) && std::isfinite(pixel.alpha);
  });
}

[[nodiscard]] bool has_parametric_curve(const ColorAdjustmentState& state) {
  return std::abs(state.curve_shadows) > 1.0e-9 ||
         std::abs(state.curve_darks) > 1.0e-9 ||
         std::abs(state.curve_lights) > 1.0e-9 ||
         std::abs(state.curve_highlights) > 1.0e-9;
}

[[nodiscard]] bool has_any_curve(const ColorAdjustmentState& state) {
  return has_parametric_curve(state) ||
         has_point_curve(state.master_curve_points) ||
         has_point_curve(state.luma_curve_points) ||
         has_point_curve(state.red_curve_points) ||
         has_point_curve(state.green_curve_points) ||
         has_point_curve(state.blue_curve_points);
}

}  // namespace

bool apply_color_curves_stage(
    std::vector<ColorPixel>* pixels,
    int width,
    int height,
    const ColorAdjustmentState& state,
    CancellationToken cancellation,
    ColorAdjustmentTelemetry* telemetry,
    bool input_finite_validated) {
  if (!valid_surface(pixels, width, height) || cancellation.cancelled()) {
    return false;
  }
  if (!has_any_curve(state)) {
    return finite_surface(*pixels, input_finite_validated);
  }

  const auto timer = StageClock::now();
  const auto safe_width = static_cast<std::size_t>(width);
  const auto color_context = make_pipeline_color_context(state);
  const auto output_headroom = sanitized_output_headroom(state);
  const auto parametric_active = has_parametric_curve(state);
  const auto parametric_curve = build_parametric_curve_data(state);
  const auto master_curve = build_curve_data(state.master_curve_points);
  const auto luma_curve = build_curve_data(state.luma_curve_points);
  const auto red_curve = build_curve_data(state.red_curve_points);
  const auto green_curve = build_curve_data(state.green_curve_points);
  const auto blue_curve = build_curve_data(state.blue_curve_points);
  run_color_adjustment_parallel_for(height, std::function<void(int)>([&](int y) {
    if (cancellation.cancelled()) {
      return;
    }
    const auto row_offset = static_cast<std::size_t>(y) * safe_width;
    for (int x = 0; x < width; ++x) {
      const auto index = row_offset + static_cast<std::size_t>(x);
      auto& pixel = (*pixels)[index];
      if (parametric_active) {
        const auto source_luminance = luminance_for_pixel(pixel, color_context);
        const auto target_luminance = evaluate_tone_curve_target(
            source_luminance, parametric_curve, false, output_headroom);
        pixel = scale_pixel_for_global_tone(
            pixel, target_luminance, output_headroom, color_context);
      }
      if (master_curve.points.size() >= 2U) {
        pixel = apply_master_rgb_point_curve(pixel, master_curve);
      }
      if (luma_curve.points.size() >= 2U) {
        pixel = apply_luminance_point_curve(
            pixel, luma_curve, output_headroom, color_context);
      }
      if (red_curve.points.size() >= 2U) {
        pixel.red = static_cast<float>(
            apply_perceptual_point_curve(pixel.red, red_curve));
      }
      if (green_curve.points.size() >= 2U) {
        pixel.green = static_cast<float>(
            apply_perceptual_point_curve(pixel.green, green_curve));
      }
      if (blue_curve.points.size() >= 2U) {
        pixel.blue = static_cast<float>(
            apply_perceptual_point_curve(pixel.blue, blue_curve));
      }
    }
  }));
  if (telemetry != nullptr) {
    telemetry->curves_ms += std::chrono::duration_cast<std::chrono::milliseconds>(
                                StageClock::now() - timer)
                                .count();
  }
  return !cancellation.cancelled() && finite_surface(*pixels);
}

}  // namespace photo_editor::pipeline
