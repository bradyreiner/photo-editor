#pragma once

#include "brush_rasterizer.h"

#include <QSize>
#include <QString>
#include <QVariantMap>

namespace photo_editor::desktop {

[[nodiscard]] QRect rasterize_brush_history_into_mask(
    QVariantMap* mask, const QImage* source_image = nullptr,
    photo_editor::pipeline::CancellationToken cancellation = {},
    bool* cancelled = nullptr, qint64* candidate_pixels = nullptr,
    qint64* edge_pixels = nullptr);

[[nodiscard]] QVariantMap mask_with_rebuilt_brush_matte(
    const QVariantMap& mask, const QSize& target_size, const QString& purpose);

}  // namespace photo_editor::desktop
