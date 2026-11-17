#include "timeline_frame_quad_warp.h"

#include <QMetaType>
#include <QPainter>
#include <QPolygonF>
#include <QStringList>
#include <QVariantList>

#include <cmath>

namespace photo_editor::desktop::timeline_motion::frame_warp {
namespace {

void set_error(QString* error_message, const QString& message) {
  if (error_message != nullptr) {
    *error_message = message;
  }
}

double cross_product(const QPointF& first, const QPointF& second,
                     const QPointF& third) {
  const auto first_x = second.x() - first.x();
  const auto first_y = second.y() - first.y();
  const auto second_x = third.x() - second.x();
  const auto second_y = third.y() - second.y();
  return first_x * second_y - first_y * second_x;
}

bool read_number(const QVariant& value, double* number) {
  if (number == nullptr || !value.isValid() ||
      value.metaType().id() == QMetaType::QString ||
      value.metaType().id() == QMetaType::QByteArray ||
      value.metaType().id() == QMetaType::Bool) {
    return false;
  }
  bool ok = false;
  const auto converted = value.toDouble(&ok);
  if (!ok || !std::isfinite(converted)) {
    return false;
  }
  *number = converted;
  return true;
}

QString ffmpeg_coordinate(double value) {
  const auto normalized = std::abs(value) < 5.0e-10 ? 0.0 : value;
  return QString::number(normalized, 'f', 9);
}

}  // namespace

bool validate_frame_quad(const FrameQuad& quad, QString* error_message) {
  if (error_message != nullptr) {
    error_message->clear();
  }
  for (const auto& point : quad.points) {
    if (!std::isfinite(point.x()) || !std::isfinite(point.y()) ||
        point.x() < kMinimumNormalizedCorner ||
        point.x() > kMaximumNormalizedCorner ||
        point.y() < kMinimumNormalizedCorner ||
        point.y() > kMaximumNormalizedCorner) {
      set_error(error_message,
                QStringLiteral("Perspective frame corners must be finite and within [-4, 4]."));
      return false;
    }
  }

  auto twice_area = 0.0;
  for (std::size_t index = 0; index < quad.points.size(); ++index) {
    const auto& current = quad.points.at(index);
    const auto& next = quad.points.at((index + 1) % quad.points.size());
    twice_area += current.x() * next.y() - next.x() * current.y();
  }
  if (!std::isfinite(twice_area) || twice_area <= kQuadConvexityEpsilon) {
    set_error(error_message,
              QStringLiteral("Perspective frame corners must have positive nonzero area."));
    return false;
  }
  for (std::size_t index = 0; index < quad.points.size(); ++index) {
    if (cross_product(quad.points.at(index),
                      quad.points.at((index + 1) % quad.points.size()),
                      quad.points.at((index + 2) % quad.points.size())) <=
        kQuadConvexityEpsilon) {
      set_error(error_message,
                QStringLiteral("Perspective frame corners must form a convex TL/TR/BR/BL quad."));
      return false;
    }
  }
  return true;
}

QVariantMap frame_quad_to_variant_map(const FrameQuad& quad, bool enabled) {
  if (!enabled) {
    return {{QStringLiteral("enabled"), false}};
  }
  QVariantList points;
  points.reserve(static_cast<qsizetype>(quad.points.size()));
  for (const auto& point : quad.points) {
    points.push_back(QVariantMap{
        {QStringLiteral("x"), point.x()},
        {QStringLiteral("y"), point.y()},
    });
  }
  return {
      {QStringLiteral("enabled"), true},
      {QStringLiteral("points"), points},
  };
}

bool frame_quad_from_variant_map(const QVariantMap& value,
                                FrameQuad* quad,
                                QString* error_message) {
  if (error_message != nullptr) {
    error_message->clear();
  }
  if (quad == nullptr) {
    set_error(error_message, QStringLiteral("A frame-quad output is required."));
    return false;
  }
  const auto enabled = value.constFind(QStringLiteral("enabled"));
  if (enabled == value.cend() || enabled->metaType().id() != QMetaType::Bool) {
    set_error(error_message, QStringLiteral("A frame quad needs a boolean enabled field."));
    return false;
  }
  if (!enabled->toBool()) {
    if (value.size() != 1 || value.contains(QStringLiteral("points"))) {
      set_error(error_message, QStringLiteral("A disabled frame quad must contain only its enabled flag."));
      return false;
    }
    *quad = FrameQuad{};
    return true;
  }
  const auto point_values = value.value(QStringLiteral("points")).toList();
  if (point_values.size() != static_cast<qsizetype>(quad->points.size())) {
    set_error(error_message,
              QStringLiteral("An enabled perspective frame requires exactly four corners."));
    return false;
  }
  if (value.size() != 2) {
    set_error(error_message, QStringLiteral("A frame quad may contain only its enabled flag and four points."));
    return false;
  }
  FrameQuad parsed;
  for (qsizetype index = 0; index < point_values.size(); ++index) {
    const auto point = point_values.at(index).toMap();
    double x = 0.0;
    double y = 0.0;
    if (point.size() != 2 || !point.contains(QStringLiteral("x")) ||
        !point.contains(QStringLiteral("y")) ||
        !read_number(point.value(QStringLiteral("x")), &x) ||
        !read_number(point.value(QStringLiteral("y")), &y)) {
      set_error(error_message,
                QStringLiteral("Each perspective frame corner needs numeric x and y coordinates."));
      return false;
    }
    parsed.points.at(static_cast<std::size_t>(index)) = QPointF(x, y);
  }
  if (!validate_frame_quad(parsed, error_message)) {
    return false;
  }
  *quad = parsed;
  return true;
}

bool frame_quad_to_transform(const QSize& source_size,
                             const QSize& output_size,
                             const FrameQuad& quad,
                             QTransform* transform,
                             QString* error_message) {
  if (error_message != nullptr) {
    error_message->clear();
  }
  if (transform == nullptr || !source_size.isValid() || source_size.isEmpty() ||
      !output_size.isValid() || output_size.isEmpty()) {
    set_error(error_message, QStringLiteral("Perspective source and output sizes must be nonempty."));
    return false;
  }
  if (!validate_frame_quad(quad, error_message)) {
    return false;
  }
  const QPolygonF source_points{
      QPointF(0.0, 0.0),
      QPointF(static_cast<double>(source_size.width()), 0.0),
      QPointF(static_cast<double>(source_size.width()),
              static_cast<double>(source_size.height())),
      QPointF(0.0, static_cast<double>(source_size.height())),
  };
  QPolygonF destination_points;
  destination_points.reserve(static_cast<qsizetype>(quad.points.size()));
  for (const auto& point : quad.points) {
    destination_points.push_back(QPointF(
        point.x() * static_cast<double>(output_size.width()),
        point.y() * static_cast<double>(output_size.height())));
  }
  if (!QTransform::quadToQuad(source_points, destination_points, *transform)) {
    set_error(error_message, QStringLiteral("The perspective frame quad has no stable homography."));
    return false;
  }
  return true;
}

bool css_perspective_rotate_y_quad(const QSize& frame_size,
                                   double perspective_distance_pixels,
                                   double angle_degrees,
                                   FrameQuad* quad,
                                   QString* error_message) {
  if (error_message != nullptr) {
    error_message->clear();
  }
  if (quad == nullptr || !frame_size.isValid() || frame_size.isEmpty() ||
      !std::isfinite(perspective_distance_pixels) ||
      perspective_distance_pixels <= 0.0 || !std::isfinite(angle_degrees) ||
      angle_degrees <= -89.0 || angle_degrees >= 89.0) {
    set_error(error_message,
              QStringLiteral("CSS perspective needs a valid frame, positive distance, and yaw in (-89, 89)."));
    return false;
  }
  constexpr double kDegreesToRadians = 0.017453292519943295769236907684886;
  const auto angle = angle_degrees * kDegreesToRadians;
  const auto cosine = std::cos(angle);
  const auto sine = std::sin(angle);
  const auto half_width = static_cast<double>(frame_size.width()) * 0.5;
  const auto half_height = static_cast<double>(frame_size.height()) * 0.5;
  const std::array<QPointF, 4> centered_corners{
      QPointF(-half_width, -half_height),
      QPointF(half_width, -half_height),
      QPointF(half_width, half_height),
      QPointF(-half_width, half_height),
  };
  FrameQuad projected;
  for (std::size_t index = 0; index < centered_corners.size(); ++index) {
    const auto& corner = centered_corners.at(index);
    const auto rotated_x = cosine * corner.x();
    const auto rotated_z = -sine * corner.x();
    const auto perspective_w = 1.0 - rotated_z / perspective_distance_pixels;
    if (!std::isfinite(perspective_w) || perspective_w <= 1.0e-6) {
      set_error(error_message,
                QStringLiteral("CSS perspective projects a corner through the camera plane."));
      return false;
    }
    projected.points.at(index) = QPointF(
        (half_width + rotated_x / perspective_w) /
            static_cast<double>(frame_size.width()),
        (half_height + corner.y() / perspective_w) /
            static_cast<double>(frame_size.height()));
  }
  if (!validate_frame_quad(projected, error_message)) {
    return false;
  }
  *quad = projected;
  return true;
}

QImage render_frame_quad_warp(const QImage& source,
                              const QSize& output_size,
                              const FrameQuad& quad,
                              QString* error_message) {
  if (error_message != nullptr) {
    error_message->clear();
  }
  if (source.isNull()) {
    set_error(error_message, QStringLiteral("A perspective source image is required."));
    return {};
  }
  QTransform transform;
  if (!frame_quad_to_transform(source.size(), output_size, quad, &transform,
                               error_message)) {
    return {};
  }
  const auto source_pixels = source.convertToFormat(QImage::Format_ARGB32_Premultiplied);
  QImage result(output_size, QImage::Format_ARGB32_Premultiplied);
  if (result.isNull()) {
    set_error(error_message, QStringLiteral("The perspective output image could not be allocated."));
    return {};
  }
  result.fill(Qt::transparent);
  QPainter painter(&result);
  painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
  painter.setWorldTransform(transform);
  painter.drawImage(QRectF(0.0, 0.0, source.width(), source.height()),
                    source_pixels,
                    QRectF(0.0, 0.0, source.width(), source.height()));
  painter.end();
  return result;
}

QString ffmpeg_destination_perspective_filter(const QSize& output_size,
                                             const FrameQuad& quad,
                                             QString* error_message) {
  if (error_message != nullptr) {
    error_message->clear();
  }
  if (!output_size.isValid() || output_size.isEmpty()) {
    set_error(error_message, QStringLiteral("The perspective output size must be nonempty."));
    return {};
  }
  if (!validate_frame_quad(quad, error_message)) {
    return {};
  }
  const auto destination_point = [&output_size](const QPointF& point) {
    return QPointF(point.x() * static_cast<double>(output_size.width()),
                   point.y() * static_cast<double>(output_size.height()));
  };
  const auto top_left = destination_point(quad.points.at(0));
  const auto top_right = destination_point(quad.points.at(1));
  const auto bottom_right = destination_point(quad.points.at(2));
  const auto bottom_left = destination_point(quad.points.at(3));
  const QStringList options{
      QStringLiteral("x0=%1").arg(ffmpeg_coordinate(top_left.x())),
      QStringLiteral("y0=%1").arg(ffmpeg_coordinate(top_left.y())),
      QStringLiteral("x1=%1").arg(ffmpeg_coordinate(top_right.x())),
      QStringLiteral("y1=%1").arg(ffmpeg_coordinate(top_right.y())),
      QStringLiteral("x2=%1").arg(ffmpeg_coordinate(bottom_left.x())),
      QStringLiteral("y2=%1").arg(ffmpeg_coordinate(bottom_left.y())),
      QStringLiteral("x3=%1").arg(ffmpeg_coordinate(bottom_right.x())),
      QStringLiteral("y3=%1").arg(ffmpeg_coordinate(bottom_right.y())),
  };
  return QStringLiteral("perspective=%1:sense=destination:interpolation=linear:eval=init")
      .arg(options.join(QLatin1Char(':')));
}

QString ffmpeg_destination_perspective_alpha_filter(const QSize& output_size,
                                                    const FrameQuad& quad,
                                                    QString* error_message) {
  if (error_message != nullptr) {
    error_message->clear();
  }
  const auto perspective =
      ffmpeg_destination_perspective_filter(output_size, quad, error_message);
  if (perspective.isEmpty()) {
    return {};
  }

  QStringList edge_distances;
  edge_distances.reserve(static_cast<qsizetype>(quad.points.size()));
  for (std::size_t index = 0; index < quad.points.size(); ++index) {
    const auto& first = quad.points.at(index);
    const auto& second = quad.points.at((index + 1) % quad.points.size());
    const auto first_x = first.x() * output_size.width();
    const auto first_y = first.y() * output_size.height();
    const auto dx = (second.x() - first.x()) * output_size.width();
    const auto dy = (second.y() - first.y()) * output_size.height();
    const auto edge_length = std::hypot(dx, dy);
    if (!std::isfinite(edge_length) || edge_length <= kQuadConvexityEpsilon) {
      set_error(error_message,
                QStringLiteral("Perspective frame edges must have nonzero length."));
      return {};
    }
    edge_distances.push_back(
        QStringLiteral("((%1)*(Y+0.5-(%2))-(%3)*(X+0.5-(%4)))/(%5)")
            .arg(QString::number(dx, 'f', 9),
                 QString::number(first_y, 'f', 9),
                 QString::number(dy, 'f', 9),
                 QString::number(first_x, 'f', 9),
                 QString::number(edge_length, 'f', 9)));
  }
  const auto minimum_distance = QStringLiteral("min(min(%1,%2),min(%3,%4))")
                                    .arg(edge_distances.at(0), edge_distances.at(1),
                                         edge_distances.at(2), edge_distances.at(3));
  return QStringLiteral(
             "format=rgba,%1,geq=r='r(X,Y)':g='g(X,Y)':b='b(X,Y)':"
             "a='alpha(X,Y)*clip(%2+0.5,0,1)'")
      .arg(perspective, minimum_distance);
}

}  // namespace photo_editor::desktop::timeline_motion::frame_warp
