#pragma once

#include "photo_editor/pipeline/layer_styles.h"

#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QSet>
#include <cmath>

namespace photo_editor::desktop {

inline bool validate_photo_layer_style_storage(const QVariantList& layers,
    QString* error = nullptr, int depth = 0, const QVariantList* masks = nullptr) {
  if (depth > 64) {
    if (error) *error = QStringLiteral("Layer style nesting exceeds the document limit.");
    return false;
  }
  for (const auto& value : layers) {
    const auto layer = value.toMap();
    const auto linked_value = layer.value(QStringLiteral("layerStyleLightLinked"));
    if (linked_value.isValid() &&
        linked_value.metaType().id() != QMetaType::Bool) {
      if (error) *error = QStringLiteral("Layer style light link state must be a boolean.");
      return false;
    }
    if (linked_value.toBool() &&
        !layer.contains(QStringLiteral("layerStyleStack"))) {
      if (error) *error = QStringLiteral("Linked layer lighting requires a layer style stack.");
      return false;
    }
    if (layer.contains(QStringLiteral("layerStyleStack"))) {
      std::string detail;
      const auto stack = layer.value(QStringLiteral("layerStyleStack"));
      const auto decoded = stack.metaType().id() == QMetaType::QString
          ? photo_editor::pipeline::deserialize_layer_style_stack(stack.toString().toStdString(), &detail)
          : std::nullopt;
      if (!decoded) {
        if (error) *error = QStringLiteral("Invalid layer styles: %1").arg(QString::fromStdString(detail));
        return false;
      }
      if (masks) {
        QSet<QString> available;
        for (const auto& mask : *masks) {
          const auto map = mask.toMap();
          available.insert(map.value(QStringLiteral("maskSetId")).toString());
        }
        for (const auto& operation : decoded->operations) {
          if (!operation.mask_ref.empty() && !available.contains(QString::fromStdString(operation.mask_ref))) {
            if (error) *error = QStringLiteral("Layer style mask '%1' is missing from the document.")
                .arg(QString::fromStdString(operation.mask_ref));
            return false;
          }
        }
      }
      bool valid = true;
      const auto light = layer.value(QStringLiteral("layerStyleGlobalLightAngle"), -45.0).toDouble(&valid);
      if (!valid || !std::isfinite(light) || light < -3600 || light > 3600) {
        if (error) *error = QStringLiteral("Layer style light angle must be finite and within 3600 degrees.");
        return false;
      }
    }
    for (const auto* key : {"children", "layers"})
      if (!validate_photo_layer_style_storage(layer.value(QString::fromLatin1(key)).toList(), error, depth + 1, masks)) return false;
    const auto document = layer.value(QStringLiteral("smartObjectDocument")).toMap();
    if (document.contains(QStringLiteral("documentLayerStyleLightAngle"))) {
      bool angle_ok = false;
      const auto angle = document.value(
          QStringLiteral("documentLayerStyleLightAngle")).toDouble(&angle_ok);
      if (!angle_ok || !std::isfinite(angle) || angle < -3600.0 || angle > 3600.0) {
        if (error) *error = QStringLiteral("Nested document layer style light angle must be finite and within 3600 degrees.");
        return false;
      }
      if (document.value(QStringLiteral("version")).toInt() < 2) {
        if (error) *error = QStringLiteral("Nested document layer style lighting requires Smart Object document version 2.");
        return false;
      }
    }
    const auto document_masks = document.value(QStringLiteral("maskStack")).toList();
    if (!validate_photo_layer_style_storage(document.value(QStringLiteral("layers")).toList(),
        error, depth + 1, &document_masks)) return false;
  }
  return true;
}

}  // namespace photo_editor::desktop
