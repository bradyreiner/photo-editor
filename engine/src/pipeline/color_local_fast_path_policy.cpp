#include "color_local_fast_path_policy.h"

#include <cmath>

namespace photo_editor::pipeline {

bool local_mask_set_is_exposure_only(const LocalMaskSet& mask_set) {
  return std::abs(mask_set.exposure) > 1e-6 &&
         std::abs(mask_set.contrast) <= 1e-6 &&
         std::abs(mask_set.highlights) <= 1e-6 &&
         std::abs(mask_set.shadows) <= 1e-6 &&
         std::abs(mask_set.whites) <= 1e-6 &&
         std::abs(mask_set.blacks) <= 1e-6 &&
         std::abs(mask_set.temperature) <= 1e-6 &&
         std::abs(mask_set.tint) <= 1e-6 &&
         std::abs(mask_set.vibrance) <= 1e-6 &&
         std::abs(mask_set.saturation) <= 1e-6 &&
         std::abs(mask_set.hue_shift) <= 1e-6 &&
         std::abs(mask_set.saturation_shift) <= 1e-6 &&
         std::abs(mask_set.colorize_hue) <= 1e-6 &&
         std::abs(mask_set.colorize_strength) <= 1e-6;
}

bool brush_mask_source_has_default_refinement(const LocalMask& mask) {
  return mask.parent_id.empty() &&
         !mask.invert &&
         !mask.selection_invert &&
         std::abs(mask.opacity - 1.0) <= 1e-6 &&
         std::abs(mask.density - 1.0) <= 1e-6 &&
         std::abs(mask.selection_expand) <= 1e-6 &&
         std::abs(mask.selection_smooth) <= 1e-6 &&
         std::abs(mask.selection_fine_edge) <= 1e-6 &&
         std::abs(mask.selection_edge_contrast) <= 1e-6 &&
         std::abs(mask.selection_decontaminate) <= 1e-6 &&
         std::abs(mask.selection_grow) <= 1e-6 &&
         std::abs(mask.selection_shrink) <= 1e-6 &&
         std::abs(mask.selection_feather) <= 1e-6 &&
         std::abs(mask.selection_blur) <= 1e-6 &&
         std::abs(mask.selection_harden) <= 1e-6 &&
         std::abs(mask.selection_despeckle) <= 1e-6 &&
         std::abs(mask.selection_fill_holes) <= 1e-6 &&
         std::abs(mask.selection_remove_islands) <= 1e-6 &&
         std::abs(mask.refine_luminance_min) <= 1e-6 &&
         std::abs(mask.refine_luminance_max - 1.0) <= 1e-6 &&
         std::abs(mask.refine_hue_range - 0.5) <= 1e-6 &&
         std::abs(mask.refine_saturation_min) <= 1e-6 &&
         std::abs(mask.refine_saturation_max - 1.0) <= 1e-6 &&
         std::abs(mask.refine_depth_min) <= 1e-6 &&
         std::abs(mask.refine_depth_max - 1.0) <= 1e-6 &&
         std::abs(mask.refine_edge_confidence_min) <= 1e-6 &&
         mask.segmentation_mask.empty() &&
         mask.depth_map.empty();
}

LocalAdjustmentCoordinateMapping default_local_adjustment_coordinate_mapping(
    int width,
    int height) {
  return LocalAdjustmentCoordinateMapping{
      .source_x = 0,
      .source_y = 0,
      .source_width = width,
      .source_height = height,
  };
}

bool local_adjustment_coordinate_mapping_is_valid(
    const LocalAdjustmentCoordinateMapping& mapping,
    int width,
    int height) {
  if (mapping.source_x < 0 || mapping.source_y < 0 ||
      mapping.source_width <= 0 || mapping.source_height <= 0) {
    return false;
  }
  if (width <= 0 || height <= 0 ||
      width > mapping.source_width || height > mapping.source_height) {
    return false;
  }
  return mapping.source_x <= mapping.source_width - width &&
         mapping.source_y <= mapping.source_height - height;
}

double normalized_source_coordinate(
    int offset,
    int source_origin,
    int source_dimension) {
  return source_dimension > 1
             ? static_cast<double>(source_origin + offset) /
                   static_cast<double>(source_dimension - 1)
             : 0.5;
}

}  // namespace photo_editor::pipeline
