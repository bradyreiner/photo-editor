#pragma once

#include "brush_preview_compositor.h"
#include "brush_rasterizer.h"

namespace photo_editor::desktop {

// Transient stroke coverage, independent of the committed recipe. Keep earlier
// segments and shade only changed coverage instead of scanning the whole image.
struct BrushLivePreviewRaster final {
  QByteArray alpha;
  QImage image;
  QColor color;
  double opacity = -1;
  QRect last_dirty_rect;

  QRect append(const QSize& size, const QVector<BrushPreviewPoint>& points,
      const BrushPreviewStyle& style, const QImage* source = nullptr,
      double tolerance = 0.18, bool protect_edges = false,
      photo_editor::pipeline::CancellationToken cancellation = {}) {
    if (!size.isValid() || cancellation.cancelled()) return {};
    QRect dirty;
    if (image.size() != size) {
      alpha = QByteArray(size.width() * size.height(), '\0');
      image = QImage(size, QImage::Format_ARGB32_Premultiplied);
      image.fill(Qt::transparent);
    }
    for (const auto& point : points) {
      // Erase feedback shows the stroke footprint. Final subtraction is applied
      // to the target mask by the commit worker, not to this empty overlay.
      dirty = dirty.united(rasterize_brush_point_into_alpha(&alpha,
          size.width(), size.height(), source, point.x, point.y, point.size,
          point.flow, point.opacity, style.feather, false, source != nullptr,
          tolerance, point.auto_mask_anchor_x, point.auto_mask_anchor_y,
          protect_edges, cancellation, nullptr, nullptr, nullptr,
          {.circular = style.circular, .hardness = style.hardness}));
      if (cancellation.cancelled()) return {};
    }
    if (color != style.color || opacity != style.opacity) dirty = image.rect();
    color = style.color;
    opacity = style.opacity;
    last_dirty_rect = dirty;
    if (dirty.isEmpty()) return dirty;
    for (int y = dirty.top(); y <= dirty.bottom(); ++y) {
      if (cancellation.cancelled()) return {};
      auto* row = reinterpret_cast<QRgb*>(image.scanLine(y));
      for (int x = dirty.left(); x <= dirty.right(); ++x) {
        const auto coverage = static_cast<unsigned char>(alpha.at(y * size.width() + x));
        const auto a = qBound(0, qRound(coverage * opacity), 255);
        row[x] = qPremultiply(qRgba(color.red(), color.green(), color.blue(), a));
      }
    }
    return dirty;
  }
};

struct BrushLivePreviewFrame final {
  BrushLivePreviewRaster raster;
  QImage source;
  bool source_attempted = false;
  bool auto_mask = false;
  int generation = 0;
  qsizetype point_count = 0;
  qint64 elapsed_ms = 0;
};

}  // namespace photo_editor::desktop
