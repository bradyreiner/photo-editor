#pragma once

#include <QPointF>
#include <QSize>
#include <QVariantList>
#include <QVector>

namespace photo_editor::desktop {

inline constexpr double kMaskOutlineSimplificationTolerancePixels = 1.5;

struct OutlineSimplificationStats final {
  int input_point_count = 0;
  int output_point_count = 0;
  double tolerance_pixels = kMaskOutlineSimplificationTolerancePixels;
  double max_error_pixels = 0.0;
  bool applied = false;
};

[[nodiscard]] double normalized_outline_display_scale(double scale);
[[nodiscard]] bool variant_value_is_outline_point(const QVariant& value);
[[nodiscard]] QVector<QPointF> outline_path_to_pixel_points(
    const QVariantList& path, const QSize& raster_size);
[[nodiscard]] double max_closed_outline_error_pixels(
    const QVector<QPointF>& original_points,
    const QVector<QPointF>& simplified_points);
[[nodiscard]] QVector<QPointF> tolerance_simplified_closed_outline_points(
    const QVector<QPointF>& points, double tolerance_pixels);
[[nodiscard]] QVector<QPointF> bounded_closed_outline_points(
    const QVector<QPointF>& points, int max_points);
[[nodiscard]] QVariantList outline_path_from_pixel_points(
    const QVector<QPointF>& points, const QSize& raster_size);
[[nodiscard]] QVariantList simplify_outline_path_with_pixel_tolerance(
    const QVariantList& path, const QSize& raster_size,
    double tolerance_pixels, OutlineSimplificationStats* stats);
[[nodiscard]] QVariantList simplify_outline_paths_with_pixel_tolerance(
    const QVariantList& paths, const QSize& raster_size,
    double tolerance_pixels, OutlineSimplificationStats* stats);

}  // namespace photo_editor::desktop
