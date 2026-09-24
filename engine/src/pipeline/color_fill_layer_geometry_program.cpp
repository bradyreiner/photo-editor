#include "color_fill_layer_geometry_program.h"

namespace photo_editor::pipeline {

CompiledGeometryStageProgram compile_retained_layer_geometry(
    const PhotoLayer& layer, const ColorAdjustmentState& state,
    int width, int height) {
  auto geometry_state = ColorAdjustmentState{};
  geometry_state.color_pipeline = state.color_pipeline;
  geometry_state.lens_distortion = layer.lens_distortion;
  geometry_state.chromatic_aberration = layer.chromatic_aberration;
  geometry_state.perspective_vertical = layer.perspective_vertical;
  geometry_state.perspective_horizontal = layer.perspective_horizontal;
  geometry_state.transform_rotation = layer.global_transform_rotation;
  geometry_state.transform_scale = layer.global_transform_scale;
  geometry_state.transform_skew_x = layer.transform_skew_x;
  geometry_state.transform_skew_y = layer.transform_skew_y;
  geometry_state.liquify_center_x = layer.liquify_center_x;
  geometry_state.liquify_center_y = layer.liquify_center_y;
  geometry_state.liquify_radius = layer.liquify_radius;
  geometry_state.liquify_strength = layer.liquify_strength;
  geometry_state.content_aware_crop_fill = layer.content_aware_crop_fill;
  return compile_geometry_stage_program(geometry_state, width, height);
}

}  // namespace photo_editor::pipeline
