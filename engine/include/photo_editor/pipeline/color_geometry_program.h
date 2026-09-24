#pragma once

#include "photo_editor/pipeline/color_adjustments.h"

#include <cstdint>
#include <vector>

namespace photo_editor::pipeline {

struct CompiledGeometryStageProgram final {
  float distortion_strength = 0.0F;
  float chromatic_aberration_strength = 0.0F;
  float vertical_perspective = 0.0F;
  float horizontal_perspective = 0.0F;
  float transform_rotation = 0.0F;
  float transform_scale = 1.0F;
  float transform_skew_x = 0.0F;
  float transform_skew_y = 0.0F;
  float liquify_strength = 0.0F;
  float liquify_center_x = 0.0F;
  float liquify_center_y = 0.0F;
  float liquify_radius = 1.0F;
  float center_x = 0.0F;
  float center_y = 0.0F;
  float maximum_radius = 1.0F;
  std::uint32_t width = 0U;
  std::uint32_t height = 0U;
  bool content_aware_crop_fill = false;
  bool active = false;
  bool valid = false;

  [[nodiscard]] bool ready() const noexcept;
};

[[nodiscard]] CompiledGeometryStageProgram compile_geometry_stage_program(
    const ColorAdjustmentState& state,
    int width,
    int height);

[[nodiscard]] bool apply_compiled_geometry_stage_program(
    std::vector<ColorPixel>* pixels,
    const CompiledGeometryStageProgram& program);

}  // namespace photo_editor::pipeline
