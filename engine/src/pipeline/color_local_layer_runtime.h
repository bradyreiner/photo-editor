#pragma once

#include "photo_editor/pipeline/color_adjustments.h"

namespace photo_editor::pipeline {

struct PipelineColorContext;

// Applies repair masks to a private component-group composite. The caller
// invokes this before applying the instance group's transform and opacity.
[[nodiscard]] bool apply_component_retouch_operations(
    std::vector<ColorPixel>* pixels,
    int width,
    int height,
    const PipelineColorContext& color_context,
    const std::vector<LocalMask>& retouch_masks,
    RemovalQuality removal_quality,
    CancellationToken cancellation = {});

}  // namespace photo_editor::pipeline

