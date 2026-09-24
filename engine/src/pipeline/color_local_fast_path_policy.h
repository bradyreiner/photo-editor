#pragma once

#include "photo_editor/pipeline/color_adjustments.h"

namespace photo_editor::pipeline {

[[nodiscard]] bool local_mask_set_is_exposure_only(const LocalMaskSet& mask_set);
[[nodiscard]] bool brush_mask_source_has_default_refinement(const LocalMask& mask);
[[nodiscard]] LocalAdjustmentCoordinateMapping default_local_adjustment_coordinate_mapping(
    int width,
    int height);
[[nodiscard]] bool local_adjustment_coordinate_mapping_is_valid(
    const LocalAdjustmentCoordinateMapping& mapping,
    int width,
    int height);
[[nodiscard]] double normalized_source_coordinate(
    int offset,
    int source_origin,
    int source_dimension);

}  // namespace photo_editor::pipeline
