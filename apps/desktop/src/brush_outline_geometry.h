#pragma once

#include "brush_preview_compositor.h"

#include <QVariantList>
#include <QVariantMap>
#include <QVector>

namespace photo_editor::desktop {

void append_outline_path(QVariantList* outline_paths, const QVariantList& outline);

[[nodiscard]] QVariantList outline_path_for_brush_samples(
    const QVector<BrushPreviewPoint>& samples);

[[nodiscard]] QVariantList fast_outline_paths_for_brush_mask(
    const QVariantMap& mask);

}  // namespace photo_editor::desktop
