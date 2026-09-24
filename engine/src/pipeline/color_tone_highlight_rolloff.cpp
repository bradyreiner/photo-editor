#include "color_tone_runtime.h"

#include "color_math_runtime.h"
#include "color_working_space_runtime.h"

#include <algorithm>
#include <cmath>
#include <span>
#include <vector>

namespace photo_editor::pipeline {

double shoulder_threshold_for_max_luminance(
    double max_luminance,
    double output_headroom,
    double filmic_shoulder) {
  const auto headroom =
      sanitized_scene_working_headroom(output_headroom);
  const auto control = clamp_signed_percent(filmic_shoulder) / 100.0;
  if (std::abs(control) > 1.0e-9) {
    // An explicit shoulder is a scene-tone control, not an output-device
    // limiter. Its onset therefore stays relative to diffuse white even when
    // the ACES working graph preserves thousands of scene-linear units.
    // Positive values start the rolloff sooner; negative values defer it.
    return std::clamp(
        0.90 -
            (control >= 0.0 ? control * 0.25 : control * 0.18),
        0.62,
        1.08);
  }
  if (max_luminance <= headroom) {
    return headroom;
  }

  const auto overshoot = std::max(0.0, max_luminance - headroom);
  const auto strength = overshoot / (overshoot + headroom);
  return headroom * std::clamp(0.98 - strength * 0.18, 0.64, 0.995);
}

double robust_highlight_reference_from_row_maxima(
    std::span<const double> row_maxima) {
  if (row_maxima.empty()) {
    return 0.0;
  }

  std::vector<double> finite_maxima;
  finite_maxima.reserve(row_maxima.size());
  for (const auto value : row_maxima) {
    if (std::isfinite(value)) {
      finite_maxima.push_back(std::max(0.0, value));
    }
  }
  if (finite_maxima.empty()) {
    return 0.0;
  }

  // A handful of specular/hot pixels must not lower the shoulder for the
  // entire photograph. Row maxima retain broad highlight regions while the
  // 98th percentile rejects isolated outliers without a full-frame histogram.
  const auto quantile_index = static_cast<std::size_t>(std::floor(
      0.98 * static_cast<double>(finite_maxima.size() - 1)));
  std::nth_element(
      finite_maxima.begin(),
      finite_maxima.begin() + static_cast<std::ptrdiff_t>(quantile_index),
      finite_maxima.end());
  return finite_maxima[quantile_index];
}

double apply_highlight_shoulder(
    double value,
    double threshold,
    double output_headroom,
    double filmic_shoulder) {
  if (value <= threshold) {
    return value;
  }

  const auto headroom =
      sanitized_scene_working_headroom(output_headroom);
  const auto control = clamp_signed_percent(filmic_shoulder) / 100.0;
  if (std::abs(control) > 1.0e-9) {
    const auto excess = std::max(0.0, value - threshold);
    if (control > 0.0) {
      // Log compression stays strictly increasing and unbounded before the
      // final clamp, so an explicit creative shoulder cannot flatten distinct
      // RAW highlight values into a display-referred plateau.
      const auto curvature = 0.80 + control * 0.70;
      return std::min(
          headroom,
          threshold + std::log1p(excess * curvature) / curvature);
    }

    // Negative shoulder values deliberately open the upper tonal range. Use
    // the inverse monotone curve, bounded at scene working headroom, so the
    // signed control has a real response on both sides of neutral.
    const auto curvature = std::max(0.10, -control * 0.80);
    const auto maximum_scaled_excess = std::log1p(
        std::max(0.0, headroom - threshold) * curvature);
    const auto scaled_excess = std::min(
        excess * curvature,
        maximum_scaled_excess);
    return threshold + std::expm1(scaled_excess) / curvature;
  }
  const auto bounded_threshold = std::min(threshold, headroom);
  const auto shoulder_scale = std::max(1e-6, headroom - bounded_threshold);
  return bounded_threshold +
         shoulder_scale *
             (1.0 - std::exp(-(value - bounded_threshold) / shoulder_scale));
}

}  // namespace photo_editor::pipeline
