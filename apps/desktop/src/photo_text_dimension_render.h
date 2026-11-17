#pragma once

#include "photo_text_dimension.h"

#include <QImage>
#include <QPoint>
#include <QRectF>
#include <QSize>
#include <QTransform>
#include <QVariantMap>
#include <QVector3D>

namespace photo_editor::desktop {

struct PhotoTextDimensionFrame final {
  QImage image;
  // Symmetric raster padding preserves the authored layer's outer transform pivot.
  QPoint padding;
  QRectF logical_bounds;
  QString error;
};

[[nodiscard]] bool photo_text_dimension_is_active(const QVariantMap& layer);
[[nodiscard]] QTransform photo_text_dimension_corner_transform(
    const PhotoTextDimensionSettings& settings, QSizeF box, bool* valid = nullptr);
[[nodiscard]] QTransform photo_text_dimension_plane_transform(
    const QVariantMap& layer, const PhotoTextDimensionSettings& settings,
    QSizeF box, bool* valid = nullptr);
[[nodiscard]] QPointF project_photo_text_dimension_layer_point(
    const QVariantMap& layer, const PhotoTextDimensionSettings& settings,
    QVector3D point, bool* valid = nullptr);
[[nodiscard]] QRectF preflight_photo_text_dimension(
    const QVariantMap& layer, QSize raster_size, QString* error = nullptr);
[[nodiscard]] PhotoTextDimensionFrame render_photo_text_dimension(
    const QVariantMap& layer, QSize raster_size);

}  // namespace photo_editor::desktop
