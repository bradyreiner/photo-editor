#include "timeline_frame_quad_overscan.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <QStringList>

namespace photo_editor::desktop::timeline_motion::frame_warp {
namespace {

void set_error(QString* error_message, const QString& message) {
  if (error_message != nullptr) {
    *error_message = message;
  }
}

bool bounded_symmetric_padding(double minimum_normalized,
                               double maximum_normalized,
                               int dimension,
                               int* padding) {
  if (padding == nullptr || dimension < 1 || !std::isfinite(minimum_normalized) ||
      !std::isfinite(maximum_normalized)) {
    return false;
  }
  const auto dimension_as_double = static_cast<double>(dimension);
  const auto before = std::max(0.0, -minimum_normalized * dimension_as_double);
  const auto after = std::max(0.0, (maximum_normalized - 1.0) * dimension_as_double);
  const auto needed = std::max(before, after);
  if (!std::isfinite(needed) || needed > static_cast<double>(std::numeric_limits<int>::max() - 2)) {
    return false;
  }
  *padding = static_cast<int>(std::ceil(needed)) + 1;
  return true;
}

QPointF map_through_padded_transform(const QTransform& local_transform,
                                     const QPointF& point,
                                     const QPoint& padding) {
  return local_transform.map(point - QPointF(padding)) + QPointF(padding);
}

QString alpha_mask_filter(const QSize& output_size, const FrameQuad& quad) {
  QStringList edge_distances;
  edge_distances.reserve(static_cast<qsizetype>(quad.points.size()));
  for (std::size_t index = 0; index < quad.points.size(); ++index) {
    const auto& first = quad.points.at(index);
    const auto& second = quad.points.at((index + 1) % quad.points.size());
    const auto first_x = first.x() * static_cast<double>(output_size.width());
    const auto first_y = first.y() * static_cast<double>(output_size.height());
    const auto dx = (second.x() - first.x()) * static_cast<double>(output_size.width());
    const auto dy = (second.y() - first.y()) * static_cast<double>(output_size.height());
    const auto edge_length = std::hypot(dx, dy);
    if (!std::isfinite(edge_length) || edge_length <= kQuadConvexityEpsilon) {
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
             "geq=r='r(X,Y)':g='g(X,Y)':b='b(X,Y)':"
             "a='alpha(X,Y)*clip(%1+0.5,0,1)'")
      .arg(minimum_distance);
}

}  // namespace

bool make_frame_quad_overscan_plan(const QSize& original_size,
                                   const FrameQuad& quad,
                                   FrameQuadOverscanPlan* plan,
                                   QString* error_message) {
  if (error_message != nullptr) {
    error_message->clear();
  }
  if (plan == nullptr) {
    set_error(error_message, QStringLiteral("A frame-quad overscan plan output is required."));
    return false;
  }
  *plan = FrameQuadOverscanPlan{};
  if (!original_size.isValid() || original_size.isEmpty() ||
      !validate_frame_quad(quad, error_message)) {
    if (error_message != nullptr && error_message->isEmpty()) {
      *error_message = QStringLiteral("The original frame size must be nonempty.");
    }
    return false;
  }

  double minimum_x = quad.points.front().x();
  double maximum_x = minimum_x;
  double minimum_y = quad.points.front().y();
  double maximum_y = minimum_y;
  for (const auto& point : quad.points) {
    minimum_x = std::min(minimum_x, point.x());
    maximum_x = std::max(maximum_x, point.x());
    minimum_y = std::min(minimum_y, point.y());
    maximum_y = std::max(maximum_y, point.y());
  }

  int padding_x = 0;
  int padding_y = 0;
  if (!bounded_symmetric_padding(minimum_x, maximum_x, original_size.width(),
                                 &padding_x) ||
      !bounded_symmetric_padding(minimum_y, maximum_y, original_size.height(),
                                 &padding_y)) {
    set_error(error_message,
              QStringLiteral("Frame-quad overscan dimensions exceed the supported range."));
    return false;
  }

  const auto twice_padding_x = static_cast<qint64>(padding_x) * 2;
  const auto twice_padding_y = static_cast<qint64>(padding_y) * 2;
  if (static_cast<qint64>(original_size.width()) + twice_padding_x >
          std::numeric_limits<int>::max() ||
      static_cast<qint64>(original_size.height()) + twice_padding_y >
          std::numeric_limits<int>::max()) {
    set_error(error_message,
              QStringLiteral("Frame-quad overscan dimensions exceed the supported range."));
    return false;
  }
  const auto expanded_size = QSize(
      static_cast<int>(static_cast<qint64>(original_size.width()) + twice_padding_x),
      static_cast<int>(static_cast<qint64>(original_size.height()) + twice_padding_y));
  QTransform local_transform;
  if (!frame_quad_to_transform(original_size, original_size, quad,
                               &local_transform, error_message)) {
    return false;
  }

  const QPoint padding(padding_x, padding_y);
  const std::array<QPointF, 4> expanded_input_corners{
      QPointF(0.0, 0.0), QPointF(expanded_size.width(), 0.0),
      QPointF(expanded_size.width(), expanded_size.height()),
      QPointF(0.0, expanded_size.height())};
  FrameQuad expanded_quad;
  FrameQuad content_quad;
  for (std::size_t index = 0; index < expanded_input_corners.size(); ++index) {
    const auto projected = map_through_padded_transform(
        local_transform, expanded_input_corners.at(index), padding);
    if (!std::isfinite(projected.x()) || !std::isfinite(projected.y())) {
      set_error(error_message,
                QStringLiteral("The conjugated frame homography is not finite across the expanded canvas."));
      return false;
    }
    expanded_quad.points.at(index) = QPointF(
        projected.x() / static_cast<double>(expanded_size.width()),
        projected.y() / static_cast<double>(expanded_size.height()));
    const auto& source_point = quad.points.at(index);
    content_quad.points.at(index) = QPointF(
        (static_cast<double>(padding_x) +
         source_point.x() * static_cast<double>(original_size.width())) /
            static_cast<double>(expanded_size.width()),
        (static_cast<double>(padding_y) +
         source_point.y() * static_cast<double>(original_size.height())) /
            static_cast<double>(expanded_size.height()));
  }
  if (!validate_frame_quad(expanded_quad, error_message)) {
    set_error(error_message,
              QStringLiteral("The conjugated frame homography crosses or exceeds the bounded expanded canvas."));
    return false;
  }
  if (!validate_frame_quad(content_quad, error_message)) {
    set_error(error_message,
              QStringLiteral("The expanded overscan does not contain the projected frame."));
    return false;
  }
  const auto perspective_filter =
      ffmpeg_destination_perspective_filter(expanded_size, expanded_quad, error_message);
  const auto content_alpha_filter = alpha_mask_filter(expanded_size, content_quad);
  if (perspective_filter.isEmpty() || content_alpha_filter.isEmpty()) {
    if (error_message != nullptr && error_message->isEmpty()) {
      *error_message = QStringLiteral("The expanded frame filters could not be constructed.");
    }
    return false;
  }

  *plan = FrameQuadOverscanPlan{
      original_size,
      expanded_size,
      QMargins(padding_x, padding_y, padding_x, padding_y),
      padding,
      QPointF(static_cast<double>(padding_x) +
                  static_cast<double>(original_size.width()) * 0.5,
              static_cast<double>(padding_y) +
                  static_cast<double>(original_size.height()) * 0.5),
      local_transform,
      perspective_filter,
      content_alpha_filter,
      expanded_quad,
      content_quad};
  return true;
}

}  // namespace photo_editor::desktop::timeline_motion::frame_warp
