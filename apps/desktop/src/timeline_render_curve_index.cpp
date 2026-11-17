#include "timeline_render_curve_index.h"

#include <QtGlobal>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>

namespace photo_editor::desktop::timeline_motion::render_curve {
namespace {

constexpr double kBoundaryEpsilon = 0.001;
constexpr int kBezierExportSamplesPerSegment = 24;

QString number(double value) {
  return QString::number(value, 'f', 8);
}

bool is_supported(Interpolation interpolation) {
  switch (interpolation) {
    case Interpolation::Linear:
    case Interpolation::Hold:
    case Interpolation::EaseIn:
    case Interpolation::EaseOut:
    case Interpolation::EaseInOut:
    case Interpolation::Bezier:
      return true;
  }
  return false;
}

double cubic_bezier_coordinate(
    double p0,
    double p1,
    double p2,
    double p3,
    double parameter) {
  const auto inverse = 1.0 - parameter;
  return inverse * inverse * inverse * p0 +
         3.0 * inverse * inverse * parameter * p1 +
         3.0 * inverse * parameter * parameter * p2 +
         parameter * parameter * parameter * p3;
}

double cubic_bezier_value_for_progress(
    double progress,
    double x1,
    double value0,
    double value1,
    double x2,
    double value2,
    double value3) {
  const auto target = qBound(0.0, progress, 1.0);
  auto lower = 0.0;
  auto upper = 1.0;
  for (auto iteration = 0; iteration < 24; ++iteration) {
    const auto midpoint = (lower + upper) * 0.5;
    const auto x = cubic_bezier_coordinate(0.0, x1, x2, 1.0, midpoint);
    if (x < target) {
      lower = midpoint;
    } else {
      upper = midpoint;
    }
  }
  return cubic_bezier_coordinate(
      value0, value1, value2, value3, (lower + upper) * 0.5);
}

QString cubic_bezier_segment_expression(
    const QString& progress_expression,
    double x1,
    double value0,
    double value1,
    double x2,
    double value2,
    double value3) {
  auto expression = number(value3);
  for (auto sample = kBezierExportSamplesPerSegment - 1; sample >= 0; --sample) {
    const auto start_progress =
        static_cast<double>(sample) / kBezierExportSamplesPerSegment;
    const auto end_progress =
        static_cast<double>(sample + 1) / kBezierExportSamplesPerSegment;
    const auto start_value = cubic_bezier_value_for_progress(
        start_progress, x1, value0, value1, x2, value2, value3);
    const auto end_value = cubic_bezier_value_for_progress(
        end_progress, x1, value0, value1, x2, value2, value3);
    const auto local_progress = QStringLiteral("((%1-%2)/%3)")
                                    .arg(progress_expression)
                                    .arg(number(start_progress))
                                    .arg(number(end_progress - start_progress));
    const auto sample_expression = QStringLiteral("(%1+(%2-%1)*%3)")
                                       .arg(number(start_value))
                                       .arg(number(end_value))
                                       .arg(local_progress);
    expression = QStringLiteral("if(lt(%1,%2),%3,%4)")
                     .arg(progress_expression)
                     .arg(number(end_progress))
                     .arg(sample_expression)
                     .arg(expression);
  }
  return expression;
}

QString segment_expression(
    const QVector<Keyframe>& keyframes,
    qsizetype index,
    const QString& time_expression) {
  const auto& left = keyframes.at(index);
  const auto& right = keyframes.at(index + 1);
  const auto span = qMax(kBoundaryEpsilon, right.local_seconds - left.local_seconds);
  const auto progress = QStringLiteral("((%1-%2)/%3)")
                            .arg(time_expression)
                            .arg(number(left.local_seconds))
                            .arg(number(span));
  auto shaped_progress = progress;
  switch (left.interpolation) {
    case Interpolation::Hold:
      shaped_progress = QStringLiteral("0");
      break;
    case Interpolation::EaseIn:
      shaped_progress = QStringLiteral("(%1*%1)").arg(progress);
      break;
    case Interpolation::EaseOut:
      shaped_progress = QStringLiteral("(1-(1-%1)*(1-%1))").arg(progress);
      break;
    case Interpolation::EaseInOut:
      shaped_progress = QStringLiteral("(%1*%1*(3-2*%1))").arg(progress);
      break;
    case Interpolation::Linear:
    case Interpolation::Bezier:
      break;
  }
  if (left.interpolation == Interpolation::Bezier) {
    auto x1 = left.bezier_out_custom
                  ? qBound(0.001, left.bezier_out_time_seconds / span, 0.999)
                  : 1.0 / 3.0;
    auto x2 = right.bezier_in_custom
                  ? qBound(0.001, 1.0 + right.bezier_in_time_seconds / span, 0.999)
                  : 2.0 / 3.0;
    if (x2 < x1 + 0.001) {
      const auto midpoint = (x1 + x2) * 0.5;
      x1 = qMax(0.001, midpoint - 0.0005);
      x2 = qMin(0.999, midpoint + 0.0005);
    }
    return cubic_bezier_segment_expression(
        progress,
        x1,
        left.value,
        left.value + (left.bezier_out_custom ? left.bezier_out_value : 0.0),
        x2,
        right.value + (right.bezier_in_custom ? right.bezier_in_value : 0.0),
        right.value);
  }
  return QStringLiteral("(%1+(%2-%1)*%3)")
      .arg(number(left.value))
      .arg(number(right.value))
      .arg(shaped_progress);
}

QString balanced_expression(
    const QVector<Keyframe>& keyframes,
    const Window& window,
    const QString& time_expression,
    double base_value) {
  const auto count = window.end - window.first;
  if (count <= 0) {
    return number(base_value);
  }
  if (count == 1) {
    return number(keyframes.at(window.first).value);
  }
  const auto segment_at = [&](qsizetype relative_index) {
    return segment_expression(keyframes, window.first + relative_index,
                              time_expression);
  };
  std::function<QString(qsizetype, qsizetype)> build_tree =
      [&](qsizetype first_segment, qsizetype last_segment) -> QString {
    if (first_segment == last_segment) {
      return segment_at(first_segment);
    }
    const auto middle_segment = first_segment + (last_segment - first_segment) / 2;
    const auto right_first_segment = middle_segment + 1;
    const auto left_expression = build_tree(first_segment, middle_segment);
    const auto right_expression = build_tree(right_first_segment, last_segment);
    return QStringLiteral("if(lt(%1,%2),%3,%4)")
        .arg(time_expression)
        .arg(number(keyframes.at(window.first + right_first_segment).local_seconds))
        .arg(left_expression)
        .arg(right_expression);
  };
  auto expression = build_tree(0, count - 2);
  expression = QStringLiteral("if(gte(%1,%2),%3,%4)")
                   .arg(time_expression)
                   .arg(number(keyframes.at(window.end - 1).local_seconds))
                   .arg(number(keyframes.at(window.end - 1).value))
                   .arg(expression);
  return QStringLiteral("if(lte(%1,%2),%3,%4)")
      .arg(time_expression)
      .arg(number(keyframes.at(window.first).local_seconds))
      .arg(number(keyframes.at(window.first).value))
      .arg(expression);
}

}  // namespace

std::optional<Interpolation> interpolationFromString(const QString& value) {
  const auto normalized = value.trimmed().toLower().replace(QLatin1Char('_'), QLatin1Char('-'));
  if (normalized == QStringLiteral("linear")) return Interpolation::Linear;
  if (normalized == QStringLiteral("hold")) return Interpolation::Hold;
  if (normalized == QStringLiteral("ease-in")) return Interpolation::EaseIn;
  if (normalized == QStringLiteral("ease-out")) return Interpolation::EaseOut;
  if (normalized == QStringLiteral("ease-in-out")) return Interpolation::EaseInOut;
  if (normalized == QStringLiteral("bezier")) return Interpolation::Bezier;
  return std::nullopt;
}

std::optional<ChannelIndex> ChannelIndex::create(
    QVector<Keyframe> keyframes,
    double base_value,
    QString* error_message) {
  if (error_message != nullptr) {
    error_message->clear();
  }
  const auto fail = [error_message](const QString& message)
      -> std::optional<ChannelIndex> {
    if (error_message != nullptr) {
      *error_message = message;
    }
    return std::nullopt;
  };
  if (!std::isfinite(base_value)) {
    return fail(QStringLiteral("The keyframe channel base value must be finite."));
  }
  for (const auto& keyframe : keyframes) {
    if (!std::isfinite(keyframe.local_seconds) || !std::isfinite(keyframe.value) ||
        !is_supported(keyframe.interpolation) ||
        (keyframe.bezier_out_custom &&
         (!std::isfinite(keyframe.bezier_out_time_seconds) ||
          !std::isfinite(keyframe.bezier_out_value))) ||
        (keyframe.bezier_in_custom &&
         (!std::isfinite(keyframe.bezier_in_time_seconds) ||
          !std::isfinite(keyframe.bezier_in_value)))) {
      return fail(QStringLiteral("The keyframe channel contains unsupported or nonfinite data."));
    }
  }
  std::sort(
      keyframes.begin(), keyframes.end(),
      [](const Keyframe& left, const Keyframe& right) {
        return left.local_seconds < right.local_seconds;
      });
  for (qsizetype index = 1; index < keyframes.size(); ++index) {
    if (keyframes.at(index).local_seconds - keyframes.at(index - 1).local_seconds <=
        0.0) {
      return fail(QStringLiteral("The keyframe channel has duplicate times."));
    }
  }
  ChannelIndex result;
  result.keyframes_ = std::move(keyframes);
  result.base_value_ = base_value;
  return result;
}

std::optional<Window> ChannelIndex::windowForRange(
    double start_seconds,
    double end_seconds,
    QString* error_message) const {
  if (error_message != nullptr) {
    error_message->clear();
  }
  if (!std::isfinite(start_seconds) || !std::isfinite(end_seconds) ||
      end_seconds <= start_seconds) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("The keyframe window must be finite and increasing.");
    }
    return std::nullopt;
  }
  if (keyframes_.isEmpty()) {
    return Window{};
  }
  const auto evaluator_start_seconds = start_seconds - kBoundaryEpsilon;
  if (!std::isfinite(evaluator_start_seconds)) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("The keyframe window start is outside the supported range.");
    }
    return std::nullopt;
  }
  const auto start = std::lower_bound(
      keyframes_.cbegin(), keyframes_.cend(), evaluator_start_seconds,
      [](const Keyframe& keyframe, double seconds) {
        return keyframe.local_seconds < seconds;
      });
  auto first = static_cast<qsizetype>(std::distance(keyframes_.cbegin(), start));
  if (first > 0) {
    --first;
  }
  const auto end = std::lower_bound(
      keyframes_.cbegin(), keyframes_.cend(), end_seconds,
      [](const Keyframe& keyframe, double seconds) {
        return keyframe.local_seconds < seconds;
      });
  auto exclusive_end = static_cast<qsizetype>(std::distance(keyframes_.cbegin(), end));
  if (exclusive_end < keyframes_.size()) {
    ++exclusive_end;
  }
  exclusive_end = qMax(exclusive_end, first + 1);
  exclusive_end = qMin(exclusive_end, keyframes_.size());
  return Window{first, exclusive_end};
}

QString ChannelIndex::ffmpegExpression(
    const QString& time_expression,
    QString* error_message) const {
  if (error_message != nullptr) {
    error_message->clear();
  }
  if (time_expression.trimmed().isEmpty()) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("The keyframe expression needs a time variable.");
    }
    return {};
  }
  if (keyframes_.isEmpty()) {
    return number(base_value_);
  }
  return balanced_expression(
      keyframes_, Window{0, keyframes_.size()}, time_expression, base_value_);
}

QString ChannelIndex::ffmpegExpressionForRange(
    double start_seconds,
    double end_seconds,
    const QString& time_expression,
    QString* error_message) const {
  if (error_message != nullptr) {
    error_message->clear();
  }
  if (time_expression.trimmed().isEmpty()) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("The keyframe expression needs a time variable.");
    }
    return {};
  }
  const auto window = windowForRange(start_seconds, end_seconds, error_message);
  if (!window.has_value()) {
    return {};
  }
  if (keyframes_.isEmpty()) {
    return number(base_value_);
  }
  return balanced_expression(keyframes_, *window, time_expression, base_value_);
}

double ChannelIndex::evaluate(double local_seconds) const {
  return evaluate(Window{0, keyframes_.size()}, local_seconds);
}

double ChannelIndex::evaluate(
    const Window& window,
    double local_seconds) const {
  if (!std::isfinite(local_seconds) || window.first < 0 ||
      window.end < window.first || window.end > keyframes_.size()) {
    return std::numeric_limits<double>::quiet_NaN();
  }
  const auto count = window.end - window.first;
  if (count == 0) {
    return base_value_;
  }
  const auto& first = keyframes_.at(window.first);
  const auto& last = keyframes_.at(window.end - 1);
  if (window.first == 0 &&
      local_seconds <= first.local_seconds + kBoundaryEpsilon) {
    return first.value;
  }
  if (window.end == keyframes_.size() &&
      local_seconds >= last.local_seconds - kBoundaryEpsilon) {
    return last.value;
  }
  if (window.end - window.first > 1) {
    const auto first_right = keyframes_.cbegin() + window.first + 1;
    const auto end = keyframes_.cbegin() + window.end;
    const auto right_it = std::lower_bound(
        first_right, end, local_seconds - kBoundaryEpsilon,
        [](const Keyframe& keyframe, double threshold) {
          return keyframe.local_seconds < threshold;
        });
    if (right_it == end) {
      return last.value;
    }
    const auto& right = *right_it;
    const auto& left = *(right_it - 1);
    const auto span = qMax(kBoundaryEpsilon, right.local_seconds - left.local_seconds);
    auto progress = qBound(0.0, (local_seconds - left.local_seconds) / span, 1.0);
    switch (left.interpolation) {
      case Interpolation::Hold:
        progress = 0.0;
        break;
      case Interpolation::EaseIn:
        progress *= progress;
        break;
      case Interpolation::EaseOut:
        progress = 1.0 - ((1.0 - progress) * (1.0 - progress));
        break;
      case Interpolation::EaseInOut:
        progress = progress * progress * (3.0 - (2.0 * progress));
        break;
      case Interpolation::Linear:
      case Interpolation::Bezier:
        break;
    }
    if (left.interpolation == Interpolation::Bezier) {
      auto x1 = left.bezier_out_custom
                    ? qBound(0.001, left.bezier_out_time_seconds / span, 0.999)
                    : 1.0 / 3.0;
      auto x2 = right.bezier_in_custom
                    ? qBound(0.001, 1.0 + right.bezier_in_time_seconds / span, 0.999)
                    : 2.0 / 3.0;
      if (x2 < x1 + 0.001) {
        const auto midpoint = (x1 + x2) * 0.5;
        x1 = qMax(0.001, midpoint - 0.0005);
        x2 = qMin(0.999, midpoint + 0.0005);
      }
      return cubic_bezier_value_for_progress(
          qBound(0.0, (local_seconds - left.local_seconds) / span, 1.0),
          x1,
          left.value,
          left.value + (left.bezier_out_custom ? left.bezier_out_value : 0.0),
          x2,
          right.value + (right.bezier_in_custom ? right.bezier_in_value : 0.0),
          right.value);
    }
    return left.value + ((right.value - left.value) * progress);
  }
  return last.value;
}

}  // namespace photo_editor::desktop::timeline_motion::render_curve
