#pragma once

#include <QImage>
#include <QRect>
#include <QSize>
#include <QString>
#include <QVector>

namespace photo_editor::desktop::timeline_motion::sparse_title_roi {

inline constexpr int kFilterSupportPixels = 8;
inline constexpr int kAlphaSupportCacheCapacity = 512;

struct PngAlphaSupport final {
  QSize image_size;
  QRect bounds;
  bool valid = false;
  bool cache_hit = false;
  bool decoded = false;
};

// Returns the bounds of pixels with nonzero alpha. An image without alpha is
// treated as fully opaque so callers naturally retain the full-canvas path.
[[nodiscard]] QRect nonzero_alpha_bounds(const QImage& image);

// Reads and hashes the exact PNG bytes on every lookup, then caches only its
// decoded size and alpha bounds by canonical path plus SHA-256. A read or decode
// failure returns an invalid result so callers can use the full-canvas path.
[[nodiscard]] PngAlphaSupport png_alpha_support_for_path(const QString& path);

// Finds one fixed tile size that covers the filtered alpha bounds at every
// sampled output scale. The tile may extend beyond a scaled canvas; callers
// pad its right and bottom edges with transparent pixels before cropping.
[[nodiscard]] QSize maximum_roi_size(
    const QRect& alpha_bounds,
    const QSize& source_canvas,
    const QVector<QSize>& scaled_canvas_sizes,
    int support_pixels = kFilterSupportPixels);

[[nodiscard]] bool materially_smaller_than_canvas(
    const QSize& roi_size,
    const QSize& canvas_size);

// These are AVExpr fragments. Keep the same scaled-dimension expression as
// the full-canvas scale so crop placement uses its exact sampling phase.
[[nodiscard]] QString roi_origin_expression(
    int alpha_origin,
    const QString& scaled_extent_expression,
    int source_extent,
    int support_pixels = kFilterSupportPixels);

[[nodiscard]] QString overlay_origin_expression(
    int canvas_extent,
    const QString& roi_origin,
    const QString& native_crop_origin);

}  // namespace photo_editor::desktop::timeline_motion::sparse_title_roi
