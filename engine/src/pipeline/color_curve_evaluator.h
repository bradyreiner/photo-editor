#pragma once

#include "photo_editor/pipeline/color_adjustments.h"

#include <span>
#include <vector>

namespace photo_editor::pipeline {

struct CurveEvaluationData final {
  std::vector<CurvePoint> points;
  std::vector<double> tangents;
};

[[nodiscard]] bool has_point_curve(std::span<const CurvePoint> points);

[[nodiscard]] CurveEvaluationData build_curve_data(
    std::span<const CurvePoint> input_points);

[[nodiscard]] CurveEvaluationData build_parametric_curve_data(
    const ColorAdjustmentState& state);

[[nodiscard]] double evaluate_curve_data(
    double value,
    const CurveEvaluationData& data);

[[nodiscard]] double apply_perceptual_point_curve(
    double value,
    const CurveEvaluationData& curve);

[[nodiscard]] double evaluate_tone_curve_target(
    double target_luminance,
    const CurveEvaluationData& curve,
    bool explicit_point_curve,
    double output_headroom);

}  // namespace photo_editor::pipeline
