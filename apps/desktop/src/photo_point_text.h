#pragma once

#include "photo_text_layout.h"
#include <QVariantMap>
#include <numbers>

namespace photo_editor::desktop {

inline QPointF photo_text_origin(const QVariantMap& layer) {
  return {layer.value("textOriginX").toDouble(), layer.value("textOriginY").toDouble()};
}

inline bool fit_photo_point_text(QVariantMap* layer) {
  const auto style = decode_photo_text_style(QJsonObject::fromVariantMap(layer->value("textTypography").toMap()));
  if (!style) return false;
  if (!style->point_text) return true;
  const auto geometry = measure_photo_point_text(layer->value("text").toString(), *style);
  if (!geometry.error.isEmpty()) return false;
  const auto scale = std::clamp(layer->value("scale", 1.0).toDouble(), 0.05, 32.0);
  const auto radians = layer->value("rotation").toDouble() * std::numbers::pi / 180.0;
  const auto rotate = [radians](QPointF p) {
    return QPointF(p.x() * std::cos(radians) - p.y() * std::sin(radians),
        p.x() * std::sin(radians) + p.y() * std::cos(radians));
  };
  const QPointF old_half(layer->value("pixelWidth").toDouble()/2, layer->value("pixelHeight").toDouble()/2);
  const QPointF new_half(geometry.size.width()/2.0, geometry.size.height()/2.0);
  const auto anchor = QPointF(layer->value("x").toDouble(), layer->value("y").toDouble()) +
      old_half * scale + rotate(photo_text_origin(*layer) - old_half) * scale;
  const auto position = anchor - new_half * scale - rotate(geometry.origin - new_half) * scale;
  layer->insert("x", qRound(position.x()));
  layer->insert("y", qRound(position.y()));
  layer->insert("pixelWidth", geometry.size.width());
  layer->insert("pixelHeight", geometry.size.height());
  layer->insert("textOriginX", geometry.origin.x());
  layer->insert("textOriginY", geometry.origin.y());
  return true;
}

}  // namespace photo_editor::desktop
