#pragma once

#include "color_highlight_runtime.h"
#include "color_working_space_runtime.h"
#include "photo_editor/pipeline/color_adjustments.h"

#include <memory>

namespace photo_editor::pipeline {

struct InputStageParameters final {
  Matrix3 white_balance{};
  bool white_balance_after_camera_profile = false;
  Matrix3 camera_profile{};
  Matrix3 camera_matching_look{};
  Matrix3 calibration{};
  std::shared_ptr<const DcpProfile> dcp_profile;
  std::shared_ptr<const DcpProfileRenderPlan> dcp_render_plan;
  Matrix3 dcp_profile_to_working{};
  PipelineColorContext dcp_profile_context{};
  bool execute_dcp_profile_rendering = false;
  double dcp_baseline_exposure_gain = 1.0;
  double baseline_exposure_gain = 1.0;
  double scene_exposure_gain = 1.0;
  bool execute_camera_linear_highlight_reconstruction = false;
  HighlightReconstructionAnalysis highlight_reconstruction;
  double highlight_reconstruction_strength = 0.0;
  double raw_black_level = 0.0;
  double raw_white_level = 1.0;
  bool clamp_camera_raw_black_floor = false;
  double source_area_x = 0.0;
  double source_area_y = 0.0;
  double source_area_width = 1.0;
  double source_area_height = 1.0;
};

[[nodiscard]] Matrix3 white_balance_matrix(
    double temperature,
    double tint,
    const PipelineColorContext& context);
[[nodiscard]] InputStageParameters make_input_stage_parameters(
    const ColorAdjustmentState& state,
    const PipelineColorContext& context,
    const HighlightReconstructionAnalysis* highlight_analysis = nullptr);
[[nodiscard]] ColorPixel apply_normalize_raw_stage(
    const ColorPixel& input,
    const InputStageParameters& parameters,
    const PipelineColorContext& context);
[[nodiscard]] ColorPixel apply_camera_characterization_stage(
    const ColorPixel& input,
    const InputStageParameters& parameters,
    const PipelineColorContext& context,
    double normalized_x = 0.5,
    double normalized_y = 0.5);
[[nodiscard]] ColorPixel apply_scene_adjustments_stage(
    const ColorPixel& input,
    const InputStageParameters& parameters);
[[nodiscard]] ColorPixel apply_input_stage(
    const ColorPixel& input,
    const InputStageParameters& parameters,
    const PipelineColorContext& context,
    double normalized_x = 0.5,
    double normalized_y = 0.5);

}  // namespace photo_editor::pipeline
