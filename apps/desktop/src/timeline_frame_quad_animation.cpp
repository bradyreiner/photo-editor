#include "timeline_frame_quad_animation.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <QStringList>

namespace photo_editor::desktop::timeline_motion::frame_warp {
namespace {

constexpr double kTimeEpsilon = 1.0e-9;

void set_error(QString* error_message, const QString& message) {
  if (error_message != nullptr) {
    *error_message = message;
  }
}

double coordinate_value(const QPointF& point, std::size_t coordinate) {
  return (coordinate % 2U) == 0U ? point.x() : point.y();
}

double evaluate_channel(const QVector<FrameQuadScalarKeyframe>& curve,
                        double fallback,
                        double local_seconds,
                        bool left_limit) {
  if (curve.isEmpty()) {
    return fallback;
  }
  for (qsizetype index = 0; index < curve.size(); ++index) {
    const auto& keyframe = curve.at(index);
    if (std::abs(local_seconds - keyframe.local_seconds) > kTimeEpsilon) {
      continue;
    }
    if (!left_limit || index == 0) {
      return keyframe.value;
    }
    const auto& previous = curve.at(index - 1);
    return previous.hold ? previous.value : keyframe.value;
  }
  if (local_seconds < curve.front().local_seconds - kTimeEpsilon) {
    return curve.front().value;
  }
  for (qsizetype index = 1; index < curve.size(); ++index) {
    const auto& left = curve.at(index - 1);
    const auto& right = curve.at(index);
    if (local_seconds < right.local_seconds) {
      if (left.hold) {
        return left.value;
      }
      const auto span = right.local_seconds - left.local_seconds;
      if (span <= kTimeEpsilon) {
        return right.value;
      }
      const auto progress = std::clamp(
          (local_seconds - left.local_seconds) / span, 0.0, 1.0);
      return left.value + (right.value - left.value) * progress;
    }
  }
  return curve.constLast().value;
}

FrameQuad evaluate_frame_quad(const FrameQuad& fallback,
                              const FrameQuadAnimation& animation,
                              double local_seconds,
                              bool left_limit) {
  auto result = fallback;
  for (std::size_t coordinate = 0; coordinate < animation.size(); ++coordinate) {
    auto& point = result.points.at(coordinate / 2U);
    const auto value = evaluate_channel(
        animation.at(coordinate), coordinate_value(fallback.points.at(coordinate / 2U),
                                                   coordinate),
        local_seconds, left_limit);
    if ((coordinate % 2U) == 0U) {
      point.setX(value);
    } else {
      point.setY(value);
    }
  }
  return result;
}

void append_unique_time(QVector<double>* times, double value) {
  if (times == nullptr || !std::isfinite(value)) {
    return;
  }
  for (const auto existing : *times) {
    if (std::abs(existing - value) <= kTimeEpsilon) {
      return;
    }
  }
  times->push_back(value);
}

QVector<double> trajectory_times(const FrameQuadAnimation& animation,
                                 double duration_seconds) {
  QVector<double> times{0.0, duration_seconds};
  for (const auto& curve : animation) {
    for (const auto& keyframe : curve) {
      append_unique_time(&times, keyframe.local_seconds);
    }
  }
  std::sort(times.begin(), times.end());
  return times;
}

double cross(const QPointF& first, const QPointF& second) {
  return first.x() * second.y() - first.y() * second.x();
}

bool positive_cross_product_through_interval(const FrameQuad& first,
                                             const FrameQuad& last,
                                             std::size_t vertex) {
  const auto next = (vertex + 1U) % first.points.size();
  const auto after_next = (vertex + 2U) % first.points.size();
  const auto edge_a0 = first.points.at(next) - first.points.at(vertex);
  const auto edge_a1 = last.points.at(next) - last.points.at(vertex);
  const auto edge_b0 = first.points.at(after_next) - first.points.at(next);
  const auto edge_b1 = last.points.at(after_next) - last.points.at(next);
  const auto delta_a = edge_a1 - edge_a0;
  const auto delta_b = edge_b1 - edge_b0;
  const auto c0 = cross(edge_a0, edge_b0);
  const auto c1 = cross(delta_a, edge_b0) + cross(edge_a0, delta_b);
  const auto c2 = cross(delta_a, delta_b);
  const auto value_at = [c0, c1, c2](double position) {
    return c0 + c1 * position + c2 * position * position;
  };
  if (!std::isfinite(c0) || !std::isfinite(c1) || !std::isfinite(c2) ||
      value_at(0.0) <= kQuadConvexityEpsilon ||
      value_at(1.0) <= kQuadConvexityEpsilon) {
    return false;
  }
  if (std::abs(c2) > std::numeric_limits<double>::epsilon()) {
    const auto extremum = -c1 / (2.0 * c2);
    if (extremum > 0.0 && extremum < 1.0 &&
        value_at(extremum) <= kQuadConvexityEpsilon) {
      return false;
    }
  }
  return true;
}

bool bounded_symmetric_padding(double minimum_normalized,
                               double maximum_normalized,
                               int dimension,
                               int* padding) {
  if (padding == nullptr || dimension < 1 ||
      !std::isfinite(minimum_normalized) ||
      !std::isfinite(maximum_normalized)) {
    return false;
  }
  const auto dimension_as_double = static_cast<double>(dimension);
  const auto before = std::max(0.0, -minimum_normalized * dimension_as_double);
  const auto after = std::max(0.0, (maximum_normalized - 1.0) * dimension_as_double);
  const auto needed = std::max(before, after);
  if (!std::isfinite(needed) ||
      needed > static_cast<double>(std::numeric_limits<int>::max() - 2)) {
    return false;
  }
  *padding = static_cast<int>(std::ceil(needed)) + 1;
  return true;
}

}  // namespace

QString frame_quad_channel_parameter(FrameQuadChannel channel) {
  switch (channel) {
    case FrameQuadChannel::TopLeftX: return QStringLiteral("framePerspectiveQuadTLX");
    case FrameQuadChannel::TopLeftY: return QStringLiteral("framePerspectiveQuadTLY");
    case FrameQuadChannel::TopRightX: return QStringLiteral("framePerspectiveQuadTRX");
    case FrameQuadChannel::TopRightY: return QStringLiteral("framePerspectiveQuadTRY");
    case FrameQuadChannel::BottomRightX: return QStringLiteral("framePerspectiveQuadBRX");
    case FrameQuadChannel::BottomRightY: return QStringLiteral("framePerspectiveQuadBRY");
    case FrameQuadChannel::BottomLeftX: return QStringLiteral("framePerspectiveQuadBLX");
    case FrameQuadChannel::BottomLeftY: return QStringLiteral("framePerspectiveQuadBLY");
  }
  return {};
}

bool frame_quad_channel_from_parameter(const QString& parameter,
                                       FrameQuadChannel* channel) {
  if (channel == nullptr) {
    return false;
  }
  for (std::size_t index = 0; index < 8U; ++index) {
    const auto candidate = static_cast<FrameQuadChannel>(index);
    if (parameter.compare(frame_quad_channel_parameter(candidate),
                          Qt::CaseInsensitive) == 0) {
      *channel = candidate;
      return true;
    }
  }
  return false;
}

bool is_frame_quad_channel_parameter(const QString& parameter) {
  FrameQuadChannel channel = FrameQuadChannel::TopLeftX;
  return frame_quad_channel_from_parameter(parameter, &channel);
}

bool frame_quad_animation_has_keyframes(const FrameQuadAnimation& animation) {
  return std::any_of(animation.cbegin(), animation.cend(),
                     [](const auto& curve) { return !curve.isEmpty(); });
}

bool validate_frame_quad_animation(const FrameQuad& fallback,
                                   const FrameQuadAnimation& animation,
                                   double duration_seconds,
                                   QString* error_message) {
  if (error_message != nullptr) {
    error_message->clear();
  }
  if (!std::isfinite(duration_seconds) || duration_seconds < 0.0 ||
      !validate_frame_quad(fallback, error_message)) {
    if (error_message != nullptr && error_message->isEmpty()) {
      *error_message = QStringLiteral("Perspective animation duration must be finite and nonnegative.");
    }
    return false;
  }
  for (const auto& curve : animation) {
    double previous_time = -1.0;
    for (const auto& keyframe : curve) {
      if (!std::isfinite(keyframe.local_seconds) ||
          !std::isfinite(keyframe.value) || keyframe.local_seconds < 0.0 ||
          keyframe.local_seconds > duration_seconds + kTimeEpsilon ||
          keyframe.value < kMinimumNormalizedCorner ||
          keyframe.value > kMaximumNormalizedCorner ||
          keyframe.local_seconds <= previous_time + kTimeEpsilon) {
        set_error(error_message,
                  QStringLiteral("Perspective channels need strictly increasing in-range times and finite corner values in [-4, 4]."));
        return false;
      }
      previous_time = keyframe.local_seconds;
    }
  }

  const auto times = trajectory_times(animation, duration_seconds);
  for (const auto time : times) {
    if (!validate_frame_quad(
            evaluate_frame_quad(fallback, animation, time, false),
            error_message)) {
      if (error_message != nullptr && error_message->isEmpty()) {
        *error_message = QStringLiteral("A perspective keyframe produces an invalid quadrilateral.");
      }
      return false;
    }
  }
  for (qsizetype index = 1; index < times.size(); ++index) {
    const auto start_time = times.at(index - 1);
    const auto end_time = times.at(index);
    if (end_time - start_time <= kTimeEpsilon) {
      continue;
    }
    const auto first = evaluate_frame_quad(fallback, animation, start_time, false);
    const auto last_limit = evaluate_frame_quad(fallback, animation, end_time, true);
    for (std::size_t vertex = 0; vertex < first.points.size(); ++vertex) {
      if (!positive_cross_product_through_interval(first, last_limit, vertex)) {
        set_error(error_message,
                  QStringLiteral("Perspective animation crosses or degenerates between keyframes."));
        return false;
      }
    }
  }
  return true;
}

FrameQuad evaluate_frame_quad_animation(const FrameQuad& fallback,
                                        const FrameQuadAnimation& animation,
                                        double local_seconds) {
  if (!std::isfinite(local_seconds)) {
    return fallback;
  }
  return evaluate_frame_quad(fallback, animation, local_seconds, false);
}

bool make_frame_quad_animation_overscan_plan(
    const QSize& original_size,
    const FrameQuad& fallback,
    const FrameQuadAnimation& animation,
    double duration_seconds,
    FrameQuadAnimationOverscanPlan* plan,
    QString* error_message) {
  if (error_message != nullptr) {
    error_message->clear();
  }
  if (plan == nullptr) {
    set_error(error_message, QStringLiteral("An animated frame-quad overscan plan is required."));
    return false;
  }
  *plan = FrameQuadAnimationOverscanPlan{};
  if (!original_size.isValid() || original_size.isEmpty() ||
      !validate_frame_quad_animation(fallback, animation, duration_seconds,
                                     error_message)) {
    if (error_message != nullptr && error_message->isEmpty()) {
      *error_message = QStringLiteral("The animated frame-quad source size must be nonempty.");
    }
    return false;
  }

  auto minimum_x = fallback.points.front().x();
  auto maximum_x = minimum_x;
  auto minimum_y = fallback.points.front().y();
  auto maximum_y = minimum_y;
  const auto times = trajectory_times(animation, duration_seconds);
  const auto include_quad = [&](const FrameQuad& quad) {
    for (const auto& point : quad.points) {
      minimum_x = std::min(minimum_x, point.x());
      maximum_x = std::max(maximum_x, point.x());
      minimum_y = std::min(minimum_y, point.y());
      maximum_y = std::max(maximum_y, point.y());
    }
  };
  for (const auto time : times) {
    include_quad(evaluate_frame_quad(fallback, animation, time, false));
    include_quad(evaluate_frame_quad(fallback, animation, time, true));
  }

  int padding_x = 0;
  int padding_y = 0;
  if (!bounded_symmetric_padding(minimum_x, maximum_x, original_size.width(),
                                 &padding_x) ||
      !bounded_symmetric_padding(minimum_y, maximum_y, original_size.height(),
                                 &padding_y)) {
    set_error(error_message,
              QStringLiteral("Animated frame-quad overscan exceeds the supported dimensions."));
    return false;
  }
  const auto width = static_cast<qint64>(original_size.width()) +
                     static_cast<qint64>(padding_x) * 2;
  const auto height = static_cast<qint64>(original_size.height()) +
                      static_cast<qint64>(padding_y) * 2;
  if (width > std::numeric_limits<int>::max() ||
      height > std::numeric_limits<int>::max()) {
    set_error(error_message,
              QStringLiteral("Animated frame-quad overscan exceeds the supported dimensions."));
    return false;
  }
  const QSize output_size(static_cast<int>(width), static_cast<int>(height));
  *plan = FrameQuadAnimationOverscanPlan{
      original_size,
      output_size,
      QMargins(padding_x, padding_y, padding_x, padding_y),
      QPointF(static_cast<double>(padding_x) + original_size.width() * 0.5,
              static_cast<double>(padding_y) + original_size.height() * 0.5)};
  return true;
}

bool conjugate_frame_quad_for_overscan(const QSize& original_size,
                                      const QMargins& padding,
                                      const FrameQuad& quad,
                                      FrameQuad* expanded_quad,
                                      FrameQuad* content_quad,
                                      QString* error_message) {
  if (error_message != nullptr) {
    error_message->clear();
  }
  if (expanded_quad == nullptr || content_quad == nullptr ||
      !original_size.isValid() || original_size.isEmpty() ||
      padding.left() < 0 || padding.top() < 0 ||
      padding.right() != padding.left() ||
      padding.bottom() != padding.top() ||
      !validate_frame_quad(quad, error_message)) {
    if (error_message != nullptr && error_message->isEmpty()) {
      *error_message = QStringLiteral("A valid symmetric frame-quad overscan transform is required.");
    }
    return false;
  }
  const auto output_width = static_cast<qint64>(original_size.width()) +
                            static_cast<qint64>(padding.left()) + padding.right();
  const auto output_height = static_cast<qint64>(original_size.height()) +
                             static_cast<qint64>(padding.top()) + padding.bottom();
  if (output_width > std::numeric_limits<int>::max() ||
      output_height > std::numeric_limits<int>::max()) {
    set_error(error_message, QStringLiteral("The padded frame size exceeds the supported range."));
    return false;
  }
  const QSize output_size(static_cast<int>(output_width),
                          static_cast<int>(output_height));
  QTransform local_transform;
  if (!frame_quad_to_transform(original_size, original_size, quad,
                               &local_transform, error_message)) {
    return false;
  }
  const std::array<QPointF, 4> corners{
      QPointF(0.0, 0.0), QPointF(output_size.width(), 0.0),
      QPointF(output_size.width(), output_size.height()),
      QPointF(0.0, output_size.height())};
  for (std::size_t index = 0; index < corners.size(); ++index) {
    const auto projected = local_transform.map(
        corners.at(index) - QPointF(padding.left(), padding.top())) +
        QPointF(padding.left(), padding.top());
    if (!std::isfinite(projected.x()) || !std::isfinite(projected.y())) {
      set_error(error_message, QStringLiteral("The animated padded homography is not finite."));
      return false;
    }
    expanded_quad->points.at(index) = QPointF(
        projected.x() / static_cast<double>(output_size.width()),
        projected.y() / static_cast<double>(output_size.height()));
    const auto& source = quad.points.at(index);
    content_quad->points.at(index) = QPointF(
        (static_cast<double>(padding.left()) +
         source.x() * static_cast<double>(original_size.width())) /
            static_cast<double>(output_size.width()),
        (static_cast<double>(padding.top()) +
         source.y() * static_cast<double>(original_size.height())) /
            static_cast<double>(output_size.height()));
  }
  if (!validate_frame_quad(*expanded_quad, error_message) ||
      !validate_frame_quad(*content_quad, error_message)) {
    if (error_message != nullptr && error_message->isEmpty()) {
      *error_message = QStringLiteral("The animated padded homography is invalid.");
    }
    return false;
  }
  return true;
}

namespace {

bool valid_animation_filter_geometry(
    const QSize& original_size,
    const QMargins& padding,
    const std::array<QString, 8>& corner_expressions,
    QSize* output_size,
    QString* error_message) {
  if (error_message != nullptr) {
    error_message->clear();
  }
  if (output_size == nullptr || !original_size.isValid() || original_size.isEmpty() ||
      padding.left() < 0 || padding.top() < 0 ||
      padding.right() != padding.left() || padding.bottom() != padding.top()) {
    set_error(error_message,
              QStringLiteral("Animated perspective requires a valid source and symmetric padding."));
    return false;
  }
  for (const auto& expression : corner_expressions) {
    if (expression.trimmed().isEmpty()) {
      set_error(error_message,
                QStringLiteral("Every animated perspective corner needs a frame-clock expression."));
      return false;
    }
  }
  const auto width = static_cast<qint64>(original_size.width()) +
                     static_cast<qint64>(padding.left()) + padding.right();
  const auto height = static_cast<qint64>(original_size.height()) +
                      static_cast<qint64>(padding.top()) + padding.bottom();
  if (width < 1 || height < 1 || width > std::numeric_limits<int>::max() ||
      height > std::numeric_limits<int>::max()) {
    set_error(error_message,
              QStringLiteral("Animated perspective output exceeds the supported dimensions."));
    return false;
  }
  *output_size = QSize(static_cast<int>(width), static_cast<int>(height));
  return true;
}

struct ProjectiveExpressions final {
  QString g;
  QString h;
  QString a;
  QString b;
  QString c;
  QString d;
  QString e;
  QString f;
};

ProjectiveExpressions projective_expressions(
    const std::array<QString, 8>& q) {
  const auto x0 = q.at(0);
  const auto y0 = q.at(1);
  const auto x1 = q.at(2);
  const auto y1 = q.at(3);
  const auto x2 = q.at(4);
  const auto y2 = q.at(5);
  const auto x3 = q.at(6);
  const auto y3 = q.at(7);
  const auto dx1 = QStringLiteral("((%1)-(%2))").arg(x1, x2);
  const auto dx2 = QStringLiteral("((%1)-(%2))").arg(x3, x2);
  const auto dx3 = QStringLiteral("((%1)-(%2)+(%3)-(%4))")
                       .arg(x0, x1, x2, x3);
  const auto dy1 = QStringLiteral("((%1)-(%2))").arg(y1, y2);
  const auto dy2 = QStringLiteral("((%1)-(%2))").arg(y3, y2);
  const auto dy3 = QStringLiteral("((%1)-(%2)+(%3)-(%4))")
                       .arg(y0, y1, y2, y3);
  const auto denominator = QStringLiteral("((%1)*(%2)-(%3)*(%4))")
                               .arg(dx1, dy2, dx2, dy1);
  const auto guarded_divide = [&denominator](const QString& numerator) {
    return QStringLiteral("if(gt(abs(%1),0.000000000001),(%2)/(%1),0)")
        .arg(denominator, numerator);
  };
  ProjectiveExpressions result;
  result.g = guarded_divide(QStringLiteral("((%1)*(%2)-(%3)*(%4))")
                                .arg(dx3, dy2, dx2, dy3));
  result.h = guarded_divide(QStringLiteral("((%1)*(%2)-(%3)*(%4))")
                                .arg(dx1, dy3, dx3, dy1));
  // Solve H so the unit-square corners map to TL, TR, BR, BL respectively:
  // evaluating at (1,0) multiplies g by x1/y1, and at (0,1) multiplies h by
  // x3/y3. The expanded-canvas texture warp depends on these coefficients;
  // the separate alpha mask can otherwise make a wrong warp look like a
  // correct outer silhouette.
  result.a = QStringLiteral("((%1)-(%2)+(%3)*(%1))").arg(x1, x0, result.g);
  result.b = QStringLiteral("((%1)-(%2)+(%3)*(%1))").arg(x3, x0, result.h);
  result.c = x0;
  result.d = QStringLiteral("((%1)-(%2)+(%3)*(%1))").arg(y1, y0, result.g);
  result.e = QStringLiteral("((%1)-(%2)+(%3)*(%1))").arg(y3, y0, result.h);
  result.f = y0;
  return result;
}

QString mapped_padded_coordinate(const ProjectiveExpressions& h,
                                 const QSize& original_size,
                                 const QMargins& padding,
                                 int padded_x,
                                 int padded_y,
                                 bool x_coordinate) {
  const auto u = QStringLiteral("((%1)/%2)")
                     .arg(QString::number(padded_x - padding.left(), 'f', 8))
                     .arg(original_size.width());
  const auto v = QStringLiteral("((%1)/%2)")
                     .arg(QString::number(padded_y - padding.top(), 'f', 8))
                     .arg(original_size.height());
  const auto denominator = QStringLiteral("((%1)*(%2)+(%3)*(%4)+1)")
                               .arg(h.g, u, h.h, v);
  if (x_coordinate) {
    const auto numerator = QStringLiteral("((%1)*(%2)+(%3)*(%4)+(%5))")
                               .arg(h.a, u, h.b, v, h.c);
    return QStringLiteral("(%1+%2*(%3)/(%4))")
        .arg(padding.left())
        .arg(original_size.width())
        .arg(numerator, denominator);
  }
  const auto numerator = QStringLiteral("((%1)*(%2)+(%3)*(%4)+(%5))")
                             .arg(h.d, u, h.e, v, h.f);
  return QStringLiteral("(%1+%2*(%3)/(%4))")
      .arg(padding.top())
      .arg(original_size.height())
      .arg(numerator, denominator);
}

}  // namespace

QString ffmpeg_animated_destination_perspective_filter(
    const QSize& original_size,
    const QMargins& padding,
    const std::array<QString, 8>& corner_expressions,
    QString* error_message) {
  QSize output_size;
  if (!valid_animation_filter_geometry(original_size, padding,
                                       corner_expressions, &output_size,
                                       error_message)) {
    return {};
  }
  const auto h = projective_expressions(corner_expressions);
  const std::array<QPoint, 4> input_corners{
      QPoint(0, 0), QPoint(output_size.width(), 0),
      QPoint(output_size.width(), output_size.height()),
      QPoint(0, output_size.height())};
  QStringList options;
  options.reserve(8);
  for (std::size_t index = 0; index < input_corners.size(); ++index) {
    const auto source_index = index == 2U ? 3U : index == 3U ? 2U : index;
    const auto& corner = input_corners.at(index);
    const auto x = mapped_padded_coordinate(h, original_size, padding,
                                            corner.x(), corner.y(), true);
    const auto y = mapped_padded_coordinate(h, original_size, padding,
                                            corner.x(), corner.y(), false);
    options.push_back(QStringLiteral("x%1='%2'").arg(source_index).arg(x));
    options.push_back(QStringLiteral("y%1='%2'").arg(source_index).arg(y));
  }
  return QStringLiteral(
             "perspective=%1:sense=destination:eval=frame:interpolation=linear,")
      .arg(options.join(QLatin1Char(':')));
}

QString ffmpeg_animated_destination_perspective_alpha_filter(
    const QSize& original_size,
    const QMargins& padding,
    const std::array<QString, 8>& corner_expressions,
    QString* error_message) {
  QSize output_size;
  if (!valid_animation_filter_geometry(original_size, padding,
                                       corner_expressions, &output_size,
                                       error_message)) {
    return {};
  }
  QStringList cached_coordinates;
  cached_coordinates.reserve(8);
  for (std::size_t index = 0; index < corner_expressions.size(); ++index) {
    const auto dimension = (index % 2U) == 0U
                               ? original_size.width()
                               : original_size.height();
    const auto offset = (index % 2U) == 0U ? padding.left() : padding.top();
    cached_coordinates.push_back(
        QStringLiteral("st(%1,(%2)*%3+%4)")
            .arg(index)
            .arg(corner_expressions.at(index))
            .arg(dimension)
            .arg(offset));
  }
  cached_coordinates.push_back(QStringLiteral("st(8,N+1)"));
  const auto prelude = QStringLiteral(
      "if(not(eq(ld(8),N+1)),%1,0)")
                           .arg(cached_coordinates.join(QLatin1Char(';')));
  QStringList edge_distances;
  edge_distances.reserve(4);
  for (std::size_t vertex = 0; vertex < 4U; ++vertex) {
    const auto next = (vertex + 1U) % 4U;
    const auto x = static_cast<int>(vertex * 2U);
    const auto y = x + 1;
    const auto next_x = static_cast<int>(next * 2U);
    const auto next_y = next_x + 1;
    const auto dx = QStringLiteral("(ld(%1)-ld(%2))").arg(next_x).arg(x);
    const auto dy = QStringLiteral("(ld(%1)-ld(%2))").arg(next_y).arg(y);
    edge_distances.push_back(QStringLiteral(
        "((%1*(Y+0.5-ld(%2))-%3*(X+0.5-ld(%4)))/max(sqrt(pow(%1,2)+pow(%3,2)),0.001))")
        .arg(dx).arg(y).arg(dy).arg(x));
  }
  const auto minimum_distance = QStringLiteral("min(min(%1,%2),min(%3,%4))")
                                    .arg(edge_distances.at(0), edge_distances.at(1),
                                         edge_distances.at(2), edge_distances.at(3));
  return QStringLiteral(
      "format=rgba,geq=r='r(X,Y)':g='g(X,Y)':b='b(X,Y)':"
      "a='%1;alpha(X,Y)*clip(%2+0.5,0,1)',")
      .arg(prelude, minimum_distance);
}

}  // namespace photo_editor::desktop::timeline_motion::frame_warp
