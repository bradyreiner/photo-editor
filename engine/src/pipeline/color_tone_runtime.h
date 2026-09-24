#pragma once

#include "photo_editor/pipeline/color_adjustments.h"

#include <memory>
#include <span>
#include <vector>

namespace photo_editor::pipeline {

[[nodiscard]] bool uses_legacy_fixed_anchor_tone_runtime(
    PhotographicRenderingModel model) noexcept;

[[nodiscard]] bool color_tone_stage_is_identity(
    const ColorAdjustmentState& state) noexcept;

[[nodiscard]] double apply_photographic_tone_runtime(
    double value,
    const ColorAdjustmentState& state);

[[nodiscard]] double tone_zone_exposure_delta(
    double stops,
    const ColorAdjustmentState& state);

[[nodiscard]] double shoulder_threshold_for_max_luminance(
    double max_luminance,
    double output_headroom,
    double filmic_shoulder = 0.0);

[[nodiscard]] double apply_highlight_shoulder(
    double value,
    double threshold,
    double output_headroom,
    double filmic_shoulder = 0.0);

[[nodiscard]] double robust_highlight_reference_from_row_maxima(
    std::span<const double> row_maxima);

struct ToneLuminanceTable final {
  static constexpr double kMinimumStops = -20.0;
  static constexpr double kMaximumStops = 20.0;
  static constexpr int kSampleCount = 16385;

  std::vector<double> samples;
  double stop_step =
      (kMaximumStops - kMinimumStops) /
      static_cast<double>(kSampleCount - 1);

  [[nodiscard]] bool ready() const {
    return samples.size() == static_cast<std::size_t>(kSampleCount);
  }
};

[[nodiscard]] double apply_global_tone(
    double value,
    const ColorAdjustmentState& state);

[[nodiscard]] std::shared_ptr<const ToneLuminanceTable>
acquire_tone_luminance_table(
    const ColorAdjustmentState& state,
    ColorAdjustmentTelemetry* telemetry);

[[nodiscard]] double evaluate_tone_luminance_table(
    const ToneLuminanceTable& table,
    double value,
    const ColorAdjustmentState& state);

}  // namespace photo_editor::pipeline
