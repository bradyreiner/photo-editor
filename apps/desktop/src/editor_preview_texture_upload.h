#pragma once

#include <QImage>

namespace photo_editor::desktop {

// Qt Quick blends premultiplied display pixels. Pipeline/export pixels retain
// their straight-alpha contract; conversion belongs at the display boundary.
inline QImage premultiplied_preview_upload_image(const QImage& image, bool floating_point) {
  const auto format = floating_point ? QImage::Format_RGBA16FPx4_Premultiplied
                                     : QImage::Format_ARGB32_Premultiplied;
  if (image.isNull() || image.format() == format ||
      (!floating_point && image.format() == QImage::Format_RGB32)) return image;
  if (floating_point) {
    // Some Qt conversion paths from associated integer pixels to half-float
    // associated pixels discard alpha. Unassociate in the source precision
    // first; the subsequent conversion preserves alpha and HDR float values.
    auto straight_format = image.format();
    switch (image.format()) {
      case QImage::Format_ARGB32_Premultiplied: straight_format = QImage::Format_ARGB32; break;
      case QImage::Format_RGBA8888_Premultiplied: straight_format = QImage::Format_RGBA8888; break;
      case QImage::Format_RGBA64_Premultiplied: straight_format = QImage::Format_RGBA64; break;
      case QImage::Format_RGBA32FPx4_Premultiplied: straight_format = QImage::Format_RGBA32FPx4; break;
      default: break;
    }
    if (straight_format != image.format())
      return image.convertToFormat(straight_format).convertToFormat(format);
  }
  return image.convertToFormat(format);
}

struct PreviewUploadRegion {
  QImage image;
  QPoint source_offset;
};

inline PreviewUploadRegion premultiplied_preview_upload_region(
    const QImage& image, const QRect& region, bool floating_point) {
  const auto clipped = region.intersected(image.rect());
  if (image.isNull() || clipped.isEmpty()) return {};
  const auto format = floating_point ? QImage::Format_RGBA16FPx4_Premultiplied
                                    : QImage::Format_ARGB32_Premultiplied;
  // Compatible images remain shared. Cross-format conversions keep the full
  // image because Qt's precision/rounding can depend on row alignment.
  if (image.format() == format ||
      (!floating_point && image.format() == QImage::Format_RGB32) ||
      clipped == image.rect() ||
      (floating_point ? image.format() != QImage::Format_RGBA16FPx4
                      : image.format() != QImage::Format_ARGB32)) {
    return {premultiplied_preview_upload_image(image, floating_point), clipped.topLeft()};
  }
  // Premultiplication is per pixel. Convert only the requested patch rather
  // than allocating and converting a full Retina/HDR canvas for a small edit.
  return {premultiplied_preview_upload_image(image.copy(clipped), floating_point), {}};
}

}  // namespace photo_editor::desktop
