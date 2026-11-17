#pragma once

#include "photo_editor/pipeline/color_adjustments.h"

#include <QSize>
#include <QRectF>
#include <QVariantMap>

namespace photo_editor::desktop {

enum class LocalMaskRasterPolicy {
  UseStoredRaster,
  RebuildEditableAtOutputResolution,
};

[[nodiscard]] photo_editor::pipeline::LocalMask compile_pipeline_local_mask(
    const QVariantMap& mask,
    LocalMaskRasterPolicy raster_policy =
        LocalMaskRasterPolicy::UseStoredRaster,
    const QSize& output_size = {},
    const QRectF& source_area = QRectF(0.0,0.0,1.0,1.0),
    const QSizeF& original_basis_pixel_extent = {});

}  // namespace photo_editor::desktop
