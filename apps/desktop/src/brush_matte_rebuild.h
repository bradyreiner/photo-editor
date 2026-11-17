#pragma once

#include "brush_rasterizer.h"

#include <QSize>
#include <QString>
#include <QVariantMap>

namespace photo_editor::desktop {

// Color/edge-constrained coverage is authoritative once committed. Replaying
// these strokes without the original source would paint a different selection.
[[nodiscard]] bool brush_history_requires_source(const QVariantMap& mask);

[[nodiscard]] QRect rasterize_brush_history_into_mask(
    QVariantMap* mask, const QImage* source_image = nullptr,
    photo_editor::pipeline::CancellationToken cancellation = {},
    bool* cancelled = nullptr, qint64* candidate_pixels = nullptr,
    qint64* edge_pixels = nullptr,
    const QRectF& coordinate_area = QRectF(0.0, 0.0, 1.0, 1.0),
    const QSizeF& basis_pixel_extent = {});

[[nodiscard]] QVariantMap mask_with_rebuilt_brush_matte(
    const QVariantMap& mask, const QSize& target_size, const QString& purpose,
    const QRectF& coordinate_area = QRectF(0.0, 0.0, 1.0, 1.0),
    const QSizeF& basis_pixel_extent = {});

}  // namespace photo_editor::desktop
