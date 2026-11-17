#pragma once

#include <cmath>

#include <QImage>
#include <QRect>
#include <QSize>
#include <QString>
#include <QtMath>

namespace photo_editor::desktop {

// Source-space edges are normalized to the oriented source frame. Framing
// transforms are applied after this rectangle, so crop remains independent of
// sequence aspect ratio and placement animation.
struct TimelineSourceCrop final {
  double left = 0.0;
  double top = 0.0;
  double right = 1.0;
  double bottom = 1.0;

  [[nodiscard]] bool isValid() const {
    constexpr auto kMinimumSpan = 0.01;
    return std::isfinite(left) && std::isfinite(top) && std::isfinite(right) &&
           std::isfinite(bottom) && left >= 0.0 && top >= 0.0 && right <= 1.0 &&
           bottom <= 1.0 && right - left >= kMinimumSpan &&
           bottom - top >= kMinimumSpan;
  }

  [[nodiscard]] bool isDefault() const {
    return left == 0.0 && top == 0.0 && right == 1.0 && bottom == 1.0;
  }

  [[nodiscard]] QRect pixelRect(const QSize& size) const {
    if (!isValid() || !size.isValid() || size.isEmpty()) {
      return {};
    }
    const auto x = qBound(0, qFloor(left * size.width()), size.width() - 1);
    const auto y = qBound(0, qFloor(top * size.height()), size.height() - 1);
    const auto right_edge = qBound(
        x + 1, qCeil(right * size.width()), size.width());
    const auto bottom_edge = qBound(
        y + 1, qCeil(bottom * size.height()), size.height());
    return QRect(x, y, right_edge - x, bottom_edge - y);
  }

  [[nodiscard]] QImage apply(const QImage& source) const {
    if (source.isNull() || isDefault()) {
      return source;
    }
    const auto rect = pixelRect(source.size());
    return rect.isValid() && rect.size() != source.size()
               ? source.copy(rect)
               : source;
  }

  [[nodiscard]] QString ffmpegFilter() const {
    if (!isValid() || isDefault()) {
      return {};
    }
    const auto number = [](double value) {
      return QString::number(value, 'f', 12);
    };
    return QStringLiteral(
               "crop=w='max(1,min(iw-floor(iw*%1),ceil(iw*%2)-floor(iw*%1)))':"
               "h='max(1,min(ih-floor(ih*%3),ceil(ih*%4)-floor(ih*%3)))':"
               "x='floor(iw*%1)':y='floor(ih*%3)':exact=1")
        .arg(number(left), number(right), number(top), number(bottom));
  }
};

}  // namespace photo_editor::desktop
