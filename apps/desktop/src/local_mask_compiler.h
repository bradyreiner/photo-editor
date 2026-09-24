#pragma once

#include "photo_editor/pipeline/color_adjustments.h"

#include <QSize>
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
    const QSize& output_size = {});

}  // namespace photo_editor::desktop
