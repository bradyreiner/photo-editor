#pragma once

#include "photo_editor/pipeline/color_geometry_program.h"

#include <QPointF>
#include <QImage>
#include <QRect>
#include <QRectF>
#include <QSize>
#include <QVariantMap>

#include <algorithm>
#include <cmath>

namespace photo_editor::desktop {

// Source masks use sample-center endpoints; paint surfaces use pixel edges.
// Both are upstream of final Geometry. Output coordinates always use image edges.
enum class CanvasCoordinateSpace { SourceMask, CroppedMask, PaintPixels };

struct CanvasCoordinateResult final {
  QPointF point;
  bool valid = false;
};

[[nodiscard]] inline QRect mask_coordinate_source_rect(
    const QVariantMap& mask, const QSize& source_size, const QRect& current_crop) {
  const auto space = mask.value(QStringLiteral("coordinateSpace")).toString();
  if (space == QStringLiteral("source") ||
      (space.isEmpty() && (mask.value(QStringLiteral("toolRole")).toString() == QStringLiteral("retouch") ||
                           mask.value(QStringLiteral("kind")).toString() != QStringLiteral("brush")))) {
    return {QPoint(0, 0), source_size};
  }
  if (space != QStringLiteral("source-area")) return current_crop;
  const auto area = mask.value(QStringLiteral("coordinateArea")).toMap();
  const auto x_extent = std::max(1, source_size.width() - 1);
  const auto y_extent = std::max(1, source_size.height() - 1);
  const auto left = qRound(area.value(QStringLiteral("x"), 0.0).toDouble() * x_extent);
  const auto top = qRound(area.value(QStringLiteral("y"), 0.0).toDouble() * y_extent);
  const auto width = qRound(area.value(QStringLiteral("width"), 1.0).toDouble() * x_extent) + 1;
  const auto height = qRound(area.value(QStringLiteral("height"), 1.0).toDouble() * y_extent) + 1;
  return QRect(left, top, width, height).intersected(QRect(QPoint(0, 0), source_size));
}

[[nodiscard]] inline QPointF geometry_sample_point(
    const pipeline::CompiledGeometryStageProgram& program, const QPointF& point) {
  const auto sample = pipeline::geometry_sample_coordinates(
      program, static_cast<float>(point.x()), static_cast<float>(point.y()));
  return {sample.x, sample.y};
}

// Invert the same output-to-source sampler used by rendering. A nonlinear warp
// need not have a visible inverse; return invalid rather than invent a position.
[[nodiscard]] inline CanvasCoordinateResult geometry_output_point(
    const pipeline::CompiledGeometryStageProgram& program, const QPointF& target) {
  auto point = target;
  for (int iteration = 0; iteration < 24; ++iteration) {
    const auto sample = geometry_sample_point(program, point);
    const auto error = target - sample;
    if (std::hypot(error.x(), error.y()) < 0.002) return {point, true};
    constexpr auto step = 0.5;
    const auto column_x = (geometry_sample_point(program, point + QPointF(step, 0)) -
                           geometry_sample_point(program, point - QPointF(step, 0))) / (2 * step);
    const auto column_y = (geometry_sample_point(program, point + QPointF(0, step)) -
                           geometry_sample_point(program, point - QPointF(0, step))) / (2 * step);
    const auto determinant = column_x.x() * column_y.y() - column_y.x() * column_x.y();
    if (!std::isfinite(determinant) || std::abs(determinant) < 1e-8) return {};
    auto delta = QPointF((error.x() * column_y.y() - error.y() * column_y.x()) / determinant,
                        (column_x.x() * error.y() - column_x.y() * error.x()) / determinant);
    // Bound Newton steps for strong distortion/liquify and small images.
    const auto maximum_step = std::max(program.width, program.height) * 0.25;
    const auto length = std::hypot(delta.x(), delta.y());
    if (length > maximum_step) delta *= maximum_step / length;
    point += delta;
    if (!std::isfinite(point.x()) || !std::isfinite(point.y())) return {};
  }
  return {};
}

[[nodiscard]] inline double extended_geometry_coordinate(
    double value, int extent, bool mirror) {
  if (extent <= 1) return 0;
  const auto maximum = static_cast<double>(extent - 1);
  if (!mirror) return std::clamp(value, 0.0, maximum);
  auto wrapped = std::fmod(value, maximum * 2);
  if (wrapped < 0) wrapped += maximum * 2;
  return wrapped <= maximum ? wrapped : maximum * 2 - wrapped;
}

[[nodiscard]] inline CanvasCoordinateResult map_canvas_coordinate(
    const pipeline::CompiledGeometryStageProgram& program,
    const QSize& source_size, const QRect& crop, const QPointF& normalized,
    bool image_to_view, CanvasCoordinateSpace space, bool allow_outside = false) {
  if (!program.ready() || source_size.isEmpty() || crop.isEmpty() ||
      !std::isfinite(normalized.x()) || !std::isfinite(normalized.y())) return {};
  const auto width = static_cast<double>(program.width);
  const auto height = static_cast<double>(program.height);
  const auto original_x = static_cast<double>(std::max(1, source_size.width() - 1));
  const auto original_y = static_cast<double>(std::max(1, source_size.height() - 1));
  if (!image_to_view) {
    auto point = geometry_sample_point(program,
        {normalized.x() * width - 0.5, normalized.y() * height - 0.5});
    // Geometry handles may live beyond the image. Their authoring coordinates
    // must extrapolate instead of borrowing the mask sampler's edge extension.
    if (!allow_outside) {
      point.setX(extended_geometry_coordinate(point.x(), static_cast<int>(program.width), program.content_aware_crop_fill));
      point.setY(extended_geometry_coordinate(point.y(), static_cast<int>(program.height), program.content_aware_crop_fill));
    }
    if (space == CanvasCoordinateSpace::SourceMask) {
      point = {source_size.width() == 1 ? 0.5 :
                   (crop.x() + point.x() * std::max(0, crop.width() - 1) / std::max(1.0, width - 1)) / original_x,
               source_size.height() == 1 ? 0.5 :
                   (crop.y() + point.y() * std::max(0, crop.height() - 1) / std::max(1.0, height - 1)) / original_y};
    } else if (space == CanvasCoordinateSpace::PaintPixels) {
      point = {(point.x() + 0.5) / width, (point.y() + 0.5) / height};
    } else {
      point = {width == 1 ? 0.5 : point.x() / (width - 1),
               height == 1 ? 0.5 : point.y() / (height - 1)};
    }
    return {point, std::isfinite(point.x()) && std::isfinite(point.y()) && (allow_outside ||
        (normalized.x() >= 0 && normalized.x() <= 1 && normalized.y() >= 0 && normalized.y() <= 1))};
  }
  auto target = QPointF{};
  if (space == CanvasCoordinateSpace::SourceMask) {
    if ((source_size.width() > 1 && crop.width() == 1 &&
         std::abs(normalized.x() * original_x - crop.x()) > 0.50001) ||
        (source_size.height() > 1 && crop.height() == 1 &&
         std::abs(normalized.y() * original_y - crop.y()) > 0.50001)) return {};
    target = {crop.width() == 1 ? (source_size.width() == 1 ? normalized.x() - 0.5 : normalized.x() * original_x - crop.x())
                  : (normalized.x() * original_x - crop.x()) * (width - 1) / (crop.width() - 1),
              crop.height() == 1 ? (source_size.height() == 1 ? normalized.y() - 0.5 : normalized.y() * original_y - crop.y())
                  : (normalized.y() * original_y - crop.y()) * (height - 1) / (crop.height() - 1)};
  } else if (space == CanvasCoordinateSpace::PaintPixels) {
    target = {normalized.x() * width - 0.5, normalized.y() * height - 0.5};
  } else {
    target = {width == 1 ? normalized.x() - 0.5 : normalized.x() * (width - 1),
              height == 1 ? normalized.y() - 0.5 : normalized.y() * (height - 1)};
  }
  auto result = geometry_output_point(program, target);
  result.point = {(result.point.x() + 0.5) / width, (result.point.y() + 0.5) / height};
  result.valid = result.valid && (allow_outside ||
      (result.point.x() >= 0 && result.point.x() <= 1 && result.point.y() >= 0 && result.point.y() <= 1));
  return result;
}

// Project transient mask feedback exactly as the final Geometry stage projects
// its committed alpha. The input remains in the immutable authoring basis.
[[nodiscard]] inline QImage project_mask_canvas_preview(
    const QImage& input, const QSize& output_size,
    const pipeline::CompiledGeometryStageProgram& program,
    const QSize& source_size, const QRect& crop, const QRect& basis,
    const QRect& output_rect = {}, const QSize& full_output_size = {},
    bool sparse_source = false) {
  if (input.isNull() || output_size.isEmpty() || basis.isEmpty()) return {};
  const auto full_size = full_output_size.isEmpty() ? output_size : full_output_size;
  const auto region = output_rect.isEmpty() ? QRect(QPoint(), output_size) : output_rect;
  if (region.size() != output_size || full_size.isEmpty()) return {};
  if (!program.active && basis == crop && input.size() == output_size &&
      region == QRect(QPoint(), full_size)) return input;
  const auto source = input.convertToFormat(QImage::Format_ARGB32_Premultiplied);
  auto output = QImage(output_size, QImage::Format_ARGB32_Premultiplied);
  output.fill(Qt::transparent);
  // With no geometry warp the mapping is separable. Evaluate the exact
  // sampler once per axis rather than once per output pixel; retain its float
  // conversion, edge extension and interpolation arithmetic unchanged.
  std::vector<double> sample_columns;
  if (!program.active) {
    sample_columns.reserve(static_cast<std::size_t>(output.width()));
    for (int x = 0; x < output.width(); ++x) {
      const auto mapped = map_canvas_coordinate(program, source_size, crop,
          {(region.x() + x + 0.5) / full_size.width(),
           (region.y() + 0.5) / full_size.height()}, false, CanvasCoordinateSpace::SourceMask);
      sample_columns.push_back((mapped.point.x() * std::max(1, source_size.width() - 1) - basis.x()) /
          std::max(1, basis.width() - 1) * std::max(0, source.width() - 1));
    }
  }

  int first_column = 0;
  int last_column = output.width() - 1;
  int first_row = 0;
  int last_row = output.height() - 1;
  const auto output_area = static_cast<qint64>(output.width()) * output.height();
  const auto source_area = static_cast<qint64>(source.width()) * source.height();
  if (sparse_source && !program.active && !source.isNull() &&
      source.width() > 1 && source.height() > 1 && output_area >= source_area) {
    QRect source_bounds;
    const auto* top_row = reinterpret_cast<const QRgb*>(source.constScanLine(0));
    const auto* bottom_row = reinterpret_cast<const QRgb*>(source.constScanLine(source.height() - 1));
    if (top_row[0] != 0 && top_row[source.width() - 1] != 0 &&
        bottom_row[0] != 0 && bottom_row[source.width() - 1] != 0) {
      source_bounds = source.rect();
    } else {
      int min_x = source.width();
      int min_y = source.height();
      int max_x = -1;
      int max_y = -1;
      for (int y = 0; y < source.height(); ++y) {
        const auto* source_row = reinterpret_cast<const QRgb*>(source.constScanLine(y));
        for (int x = 0; x < source.width(); ++x) {
          if (source_row[x] == 0) continue;
          min_x = std::min(min_x, x);
          min_y = std::min(min_y, y);
          max_x = std::max(max_x, x);
          max_y = std::max(max_y, y);
        }
      }
      if (max_x >= 0) source_bounds = QRect(QPoint(min_x, min_y), QPoint(max_x, max_y));
    }
    if (source_bounds.isEmpty()) return output;
    if (source_bounds != source.rect()) {
      first_column = output.width();
      last_column = -1;
      for (int x = 0; x < output.width(); ++x) {
        const auto left = static_cast<int>(std::floor(sample_columns[static_cast<std::size_t>(x)]));
        if (left > source_bounds.right() || left + 1 < source_bounds.left()) continue;
        first_column = std::min(first_column, x);
        last_column = std::max(last_column, x);
      }
      first_row = output.height();
      last_row = -1;
      for (int y = 0; y < output.height(); ++y) {
        const auto mapped = map_canvas_coordinate(program, source_size, crop,
            {(region.x() + 0.5) / full_size.width(),
             (region.y() + y + 0.5) / full_size.height()}, false,
            CanvasCoordinateSpace::SourceMask);
        const auto sample_row = (mapped.point.y() * std::max(1, source_size.height() - 1) - basis.y()) /
            std::max(1, basis.height() - 1) * std::max(0, source.height() - 1);
        const auto top = static_cast<int>(std::floor(sample_row));
        if (top > source_bounds.bottom() || top + 1 < source_bounds.top()) continue;
        first_row = std::min(first_row, y);
        last_row = std::max(last_row, y);
      }
      if (first_column > last_column || first_row > last_row) return output;
    }
  }

  for (int y = first_row; y <= last_row; ++y) {
    auto* row = reinterpret_cast<QRgb*>(output.scanLine(y));
    double sample_row = 0;
    if (!program.active) {
      const auto mapped = map_canvas_coordinate(program, source_size, crop,
          {(region.x() + 0.5) / full_size.width(),
           (region.y() + y + 0.5) / full_size.height()}, false, CanvasCoordinateSpace::SourceMask);
      sample_row = (mapped.point.y() * std::max(1, source_size.height() - 1) - basis.y()) /
          std::max(1, basis.height() - 1) * std::max(0, source.height() - 1);
    }
    for (int x = first_column; x <= last_column; ++x) {
      double sample_x = 0, sample_y = sample_row;
      if (!program.active) {
        sample_x = sample_columns[static_cast<std::size_t>(x)];
      } else {
        const auto mapped = map_canvas_coordinate(program, source_size, crop,
            {(region.x() + x + 0.5) / full_size.width(),
             (region.y() + y + 0.5) / full_size.height()}, false,
            CanvasCoordinateSpace::SourceMask);
        sample_x = (mapped.point.x() * std::max(1, source_size.width() - 1) - basis.x()) /
            std::max(1, basis.width() - 1) * std::max(0, source.width() - 1);
        sample_y = (mapped.point.y() * std::max(1, source_size.height() - 1) - basis.y()) /
            std::max(1, basis.height() - 1) * std::max(0, source.height() - 1);
      }
      const auto left = static_cast<int>(std::floor(sample_x));
      const auto top = static_cast<int>(std::floor(sample_y));
      const auto fx = sample_x - left;
      const auto fy = sample_y - top;
      double red = 0, green = 0, blue = 0, alpha = 0;
      for (int dy = 0; dy < 2; ++dy) for (int dx = 0; dx < 2; ++dx) {
        const auto sx = left + dx;
        const auto sy = top + dy;
        if (sx < 0 || sy < 0 || sx >= source.width() || sy >= source.height()) continue;
        const auto weight = (dx == 0 ? 1 - fx : fx) * (dy == 0 ? 1 - fy : fy);
        const auto pixel = reinterpret_cast<const QRgb*>(source.constScanLine(sy))[sx];
        red += qRed(pixel) * weight; green += qGreen(pixel) * weight;
        blue += qBlue(pixel) * weight; alpha += qAlpha(pixel) * weight;
      }
      row[x] = qRgba(qRound(red), qRound(green), qRound(blue), qRound(alpha));
    }
  }
  return output;
}

}  // namespace photo_editor::desktop
