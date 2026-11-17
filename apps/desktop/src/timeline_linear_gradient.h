#pragma once

#include <QColor>
#include <QImage>
#include <QLinearGradient>
#include <QPainter>
#include <QSize>
#include <QVariantList>
#include <QVariantMap>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace photo_editor::desktop::timeline_motion {

// CSS-compatible angle: 0 points up, 90 right, 180 down. The projected
// corner distance preserves the same stop coordinates at every aspect ratio.
inline QImage render_linear_gradient_frame(
    const QSize& size, double angle_degrees, const QVariantList& stops) {
  if (!size.isValid() || size.isEmpty() || !std::isfinite(angle_degrees) || stops.size() < 2)
    return {};
  const auto radians = angle_degrees * std::numbers::pi / 180.0;
  const QPointF direction(std::sin(radians), -std::cos(radians));
  const auto length = std::abs(size.width() * direction.x()) +
                      std::abs(size.height() * direction.y());
  const QPointF centre(size.width() * 0.5, size.height() * 0.5);
  QLinearGradient gradient(centre - direction * length * 0.5,
                           centre + direction * length * 0.5);
  gradient.setSpread(QGradient::PadSpread);
  for (const auto& entry : stops) {
    const auto stop = entry.toMap();
    const auto position = stop.value(QStringLiteral("position")).toDouble();
    auto color = QColor(stop.value(QStringLiteral("color")).toString());
    const auto opacity = stop.value(QStringLiteral("opacity"), 1.0).toDouble();
    if (!color.isValid() || !std::isfinite(position) || !std::isfinite(opacity) ||
        position < 0.0 || position > 1.0 || opacity < 0.0 || opacity > 1.0)
      return {};
    color.setAlphaF(static_cast<float>(color.alphaF() * opacity));
    gradient.setColorAt(position, color);
  }
  QImage image(size, QImage::Format_ARGB32_Premultiplied);
  image.fill(Qt::transparent);
  QPainter painter(&image);
  painter.fillRect(image.rect(), gradient);
  return image;
}

}  // namespace photo_editor::desktop::timeline_motion
