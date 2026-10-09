#pragma once
#include <QVariantList>
#include <QVariantMap>

namespace photo_editor::desktop {
inline bool is_base_image_edit(const QVariantMap& layer) {
  return layer.value(QStringLiteral("baseImageEdit")).toBool();
}
inline QVariantList visible_photo_layers(const QVariantList& layers) {
  QVariantList visible;
  visible.reserve(layers.size());
  for (const auto& value : layers) if (!is_base_image_edit(value.toMap())) visible.push_back(value);
  return visible;
}
}  // namespace photo_editor::desktop
