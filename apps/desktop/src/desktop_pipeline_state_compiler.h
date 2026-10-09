#pragma once

#include "edit_recipe_state.h"
#include "local_mask_compiler.h"

#include "photo_editor/pipeline/color_adjustments.h"
#include "photo_editor/pipeline/color_pipeline.h"

#include <QSize>
#include <QStringList>
#include <QVariantList>

#include <span>
#include <vector>

namespace photo_editor::desktop {

enum class DesktopRenderPurpose {
  Preview,
  Output,
};

struct DisplayReferredTextPolicySnapshot final {
  QString render_color_domain = QStringLiteral("working-scene-linear");
  QString eligibility = QStringLiteral("legacy-working-scene");
  QString reason = QStringLiteral("no-topmost-plain-text-suffix");
  QString version = QStringLiteral("display-referred-text-suffix-v1");
  QString limits = QStringLiteral(
      "Opaque solid root text only; direct own-mask rasters must cover the full canvas with matching aspect ratio and valid bytes. Preview ICC is applied after text only through the exact PhotographicOutput->DeviceTransform->Publish suffix with a ready SDR profile. Soft proof, export ICC, HDR targets, selection masks, effects, clipping, and non-identity detail/geometry remain in the working-scene path.");
  QStringList eligible_layer_ids;
};

[[nodiscard]] DisplayReferredTextPolicySnapshot
classify_display_referred_text_layers(
    const EditRecipeState& state,
    const photo_editor::pipeline::ColorPipelineConfig& color_pipeline_config,
    DesktopRenderPurpose purpose,
    const QSize& output_size);

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
