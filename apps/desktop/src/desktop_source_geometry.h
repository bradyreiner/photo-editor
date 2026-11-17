#pragma once

#include <QRect>
#include <QRectF>
#include <QSize>

namespace photo_editor::desktop {

// Physical decode/crop geometry, retained before compiling masks or layers.
// Paint and camera sampling use image edges; normalized masks use pixel-center
// endpoints. Keeping both derived from one integer crop avoids half-pixel and
// qRound disagreements between preview, thumbnails and export.
struct DesktopSourceGeometry final {
  QSize source_size;
  QRect crop_rect;

  [[nodiscard]] bool valid() const {
    return source_size.isValid() && !source_size.isEmpty() && !crop_rect.isEmpty() &&
        QRect(QPoint(), source_size).contains(crop_rect);
  }

  [[nodiscard]] QRectF edge_area() const {
    if (!valid()) return {0, 0, 1, 1};
    return {static_cast<double>(crop_rect.x()) / source_size.width(),
        static_cast<double>(crop_rect.y()) / source_size.height(),
        static_cast<double>(crop_rect.width()) / source_size.width(),
        static_cast<double>(crop_rect.height()) / source_size.height()};
  }

  [[nodiscard]] QRectF mask_area() const {
    if (!valid()) return {0, 0, 1, 1};
    return {source_size.width() > 1 ? static_cast<double>(crop_rect.x()) / (source_size.width() - 1) : 0,
        source_size.height() > 1 ? static_cast<double>(crop_rect.y()) / (source_size.height() - 1) : 0,
        source_size.width() > 1 ? static_cast<double>(crop_rect.width() - 1) / (source_size.width() - 1) : 1,
        source_size.height() > 1 ? static_cast<double>(crop_rect.height() - 1) / (source_size.height() - 1) : 1};
  }
};

}  // namespace photo_editor::desktop
