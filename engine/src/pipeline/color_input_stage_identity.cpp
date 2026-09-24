#include "color_input_stage_identity.h"

#include <array>

namespace photo_editor::pipeline {

namespace {

[[nodiscard]] bool matrix_is_exact_identity(const Matrix3& matrix) {
  static constexpr std::array<double, 9> kIdentity = {
      1.0, 0.0, 0.0,
      0.0, 1.0, 0.0,
      0.0, 0.0, 1.0,
  };
  return matrix.values == kIdentity;
}

}  // namespace

bool normalize_raw_stage_is_identity(const InputStageParameters& parameters) {
  return (parameters.white_balance_after_camera_profile ||
          matrix_is_exact_identity(parameters.white_balance)) &&
         parameters.raw_black_level == 0.0 && parameters.raw_white_level == 1.0 &&
         !parameters.clamp_camera_raw_black_floor &&
         !parameters.execute_camera_linear_highlight_reconstruction;
}

bool camera_characterization_stage_is_identity(
    const InputStageParameters& parameters) {
  return !parameters.execute_dcp_profile_rendering &&
         matrix_is_exact_identity(parameters.camera_profile) &&
         (!parameters.white_balance_after_camera_profile ||
          matrix_is_exact_identity(parameters.white_balance)) &&
         matrix_is_exact_identity(parameters.camera_matching_look) &&
         matrix_is_exact_identity(parameters.calibration) &&
         (parameters.execute_camera_linear_highlight_reconstruction ||
          parameters.highlight_reconstruction_strength <= 1.0e-6) &&
         parameters.baseline_exposure_gain == 1.0 &&
         !parameters.clamp_camera_raw_black_floor;
}

bool scene_adjustments_stage_is_identity(const InputStageParameters& parameters) {
  return parameters.scene_exposure_gain == 1.0;
}

}  // namespace photo_editor::pipeline
