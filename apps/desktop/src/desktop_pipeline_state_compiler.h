#pragma once

#include "edit_recipe_state.h"
#include "local_mask_compiler.h"

#include "photo_editor/pipeline/color_adjustments.h"
#include "photo_editor/pipeline/color_pipeline.h"

#include <QSize>
#include <QVariantList>

#include <span>
#include <vector>

namespace photo_editor::desktop {

enum class DesktopRenderPurpose {
  Preview,
  Output,
};

[[nodiscard]] std::vector<photo_editor::pipeline::LocalMaskSet>
compile_pipeline_local_mask_sets(
    const QVariantList& masks,
    DesktopRenderPurpose purpose,
    LocalMaskRasterPolicy raster_policy =
        LocalMaskRasterPolicy::UseStoredRaster,
    const QSize& output_size = {});

[[nodiscard]] std::vector<photo_editor::pipeline::LocalMask>
compile_pipeline_retouch_masks(
    const QVariantList& masks,
    LocalMaskRasterPolicy raster_policy =
        LocalMaskRasterPolicy::UseStoredRaster,
    const QSize& output_size = {});

[[nodiscard]] photo_editor::pipeline::ColorAdjustmentState
compile_desktop_pipeline_state(
    const EditRecipeState& state,
    const photo_editor::pipeline::DecodedImageMetadata* metadata,
    const photo_editor::pipeline::ColorPipelineConfig& color_pipeline_config,
    DesktopRenderPurpose purpose,
    LocalMaskRasterPolicy raster_policy =
        LocalMaskRasterPolicy::UseStoredRaster,
    const QSize& output_size = {},
    std::span<const photo_editor::pipeline::ColorPixel> source_pixels = {});

}  // namespace photo_editor::desktop
