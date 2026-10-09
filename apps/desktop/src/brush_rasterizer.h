#pragma once

#include "photo_editor/pipeline/color_adjustments.h"

#include <QImage>
#include <QRect>
#include <QVariantMap>

namespace photo_editor::desktop {

// Version 2 uses a circular tip in image pixels. Hardness controls the solid
// core; feather softens that core within the cursor's outer footprint.
struct BrushRasterGeometry final {
  bool circular = false;  // Legacy normalized-space tips remain unchanged.
  double hardness = 1.0;
};

// In-place target for transient stroke buffers. Cancellation may leave partial writes;
// callers discard a cancelled transient target. The recipe wrapper remains atomic.
[[nodiscard]] QRect rasterize_brush_point_into_alpha(
    QByteArray* alpha, int width, int height, const QImage* source_image, double normalized_x,
    double normalized_y, double brush_size, double brush_flow,
    double brush_opacity, double feather, bool erase = false,
    bool auto_mask = false, double auto_mask_tolerance = 0.18,
    double auto_mask_anchor_x = -1.0, double auto_mask_anchor_y = -1.0,
    bool protect_edges = false,
    photo_editor::pipeline::CancellationToken cancellation = {},
    bool* cancelled = nullptr, qint64* auto_mask_candidate_pixels = nullptr,
    qint64* auto_mask_edge_evaluation_pixels = nullptr,
    BrushRasterGeometry geometry = {});

[[nodiscard]] QRect rasterize_brush_point_into_mask(
    QVariantMap* mask, const QImage* source_image, double normalized_x,
    double normalized_y, double brush_size, double brush_flow,
    double brush_opacity, double feather, bool erase = false,
    bool auto_mask = false, double auto_mask_tolerance = 0.18,
    double auto_mask_anchor_x = -1.0, double auto_mask_anchor_y = -1.0,
    bool protect_edges = false,
    photo_editor::pipeline::CancellationToken cancellation = {},
    bool* cancelled = nullptr, qint64* auto_mask_candidate_pixels = nullptr,
    qint64* auto_mask_edge_evaluation_pixels = nullptr,
    BrushRasterGeometry geometry = {});

}  // namespace photo_editor::desktop
