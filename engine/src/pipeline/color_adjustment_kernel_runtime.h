#pragma once

#include "color_curve_evaluator.h"
#include "color_input_runtime.h"
#include "color_mask_runtime.h"
#include "photo_editor/pipeline/color_adjustments.h"

#include <optional>

namespace photo_editor::pipeline {

// Owned by a single render/mask descriptor, immutable while workers use it.
// It must be compiled with the same working context as the pixel invocation.
struct CompiledLocalAdjustment final {
  ColorAdjustmentState state;
  InputStageParameters input_stage;
  bool has_adjustments = false;
};

[[nodiscard]] std::optional<CompiledLocalAdjustment> compile_local_adjustment(
    const PipelineColorContext& context,
    PhotographicRenderingModel photographic_rendering_model,
    const LocalAdjustmentSettings& settings);

void apply_local_adjustment_mask(
    ColorPixel* pixel,
    const PipelineColorContext& context,
    PhotographicRenderingModel photographic_rendering_model,
    double weight,
    double exposure,
    double contrast,
    double highlights,
    double shadows,
    double whites,
    double blacks,
    double temperature,
    double tint,
    double vibrance,
    double saturation,
    double hue_shift,
    double saturation_shift,
    double colorize_hue,
    double colorize_strength,
    const CompiledLocalAdjustment* compiled = nullptr);

[[nodiscard]] ColorPixel apply_master_rgb_point_curve(
    const ColorPixel& pixel,
    const CurveEvaluationData& curve);
[[nodiscard]] ColorPixel apply_luminance_point_curve(
    const ColorPixel& pixel,
    const CurveEvaluationData& curve,
    double output_headroom,
    const PipelineColorContext& context);

}  // namespace photo_editor::pipeline
