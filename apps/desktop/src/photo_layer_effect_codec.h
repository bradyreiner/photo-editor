#pragma once

#include "photo_editor/pipeline/photo_effect_codec.h"

#include <QString>
#include <QVariantMap>

namespace photo_editor::desktop {

inline std::optional<std::vector<photo_editor::pipeline::PhotoEffectInstance>>
decode_photo_layer_effects(const QVariantMap& layer, QString* error = nullptr) {
  if (error != nullptr) error->clear();
  if (!layer.contains(QStringLiteral("effectStack")))
    return std::vector<photo_editor::pipeline::PhotoEffectInstance>{};
  const auto value = layer.value(QStringLiteral("effectStack"));
  if (value.metaType().id() != QMetaType::QString) {
    if (error != nullptr) *error = QStringLiteral("Layer effect stack must be a versioned string envelope.");
    return std::nullopt;
  }
  std::string detail;
  auto effects = photo_editor::pipeline::deserialize_photo_effect_stack(
      value.toString().toUtf8().toStdString(), &detail);
  if (!effects && error != nullptr) *error = QString::fromStdString(detail);
  return effects;
}

inline std::optional<QString> encode_photo_layer_effects(
    std::span<const photo_editor::pipeline::PhotoEffectInstance> effects,
    QString* error = nullptr) {
  if (error != nullptr) error->clear();
  std::string detail;
  const auto payload = photo_editor::pipeline::serialize_photo_effect_stack(effects, &detail);
  if (!payload) {
    if (error != nullptr) *error = QString::fromStdString(detail);
    return std::nullopt;
  }
  return QString::fromStdString(*payload);
}

}  // namespace photo_editor::desktop
