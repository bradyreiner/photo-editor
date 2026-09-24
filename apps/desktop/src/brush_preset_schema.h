#pragma once

#include <QString>
#include <QStringList>
#include <QVariantMap>

namespace photo_editor::desktop {

[[nodiscard]] inline QString normalized_brush_preset_name(const QString& name) {
  return name.simplified();
}

[[nodiscard]] inline QVariantMap brush_preset_snapshot_from_mask(
    const QString& name,
    const QVariantMap& mask) {
  QVariantMap preset;
  preset.insert(QStringLiteral("name"), normalized_brush_preset_name(name));
  const QStringList double_keys = {
      QStringLiteral("brushSize"), QStringLiteral("brushFlow"),
      QStringLiteral("brushHardness"), QStringLiteral("brushSpacing"),
      QStringLiteral("brushSmoothing"), QStringLiteral("brushPressureSize"),
      QStringLiteral("brushPressureFlow"), QStringLiteral("brushPressureOpacity"),
      QStringLiteral("brushAutoMaskTolerance"), QStringLiteral("feather"),
  };
  for (const auto& key : double_keys) {
    preset.insert(key, mask.value(key).toDouble());
  }
  const QStringList bool_keys = {
      QStringLiteral("brushBuildup"), QStringLiteral("brushEraseMode"),
      QStringLiteral("brushAutoMask"),
      QStringLiteral("brushAutoMaskContinuousSampling"),
      QStringLiteral("brushProtectEdges"),
  };
  for (const auto& key : bool_keys) {
    preset.insert(key, mask.value(key).toBool());
  }
  return preset;
}

inline void apply_custom_brush_preset_to_mask(
    QVariantMap* mask,
    const QVariantMap& preset) {
  if (mask == nullptr) {
    return;
  }
  const auto name = normalized_brush_preset_name(
      preset.value(QStringLiteral("name")).toString());
  if (name.isEmpty()) {
    return;
  }
  mask->insert(QStringLiteral("brushPreset"), name);
  const QStringList double_keys = {
      QStringLiteral("brushSize"), QStringLiteral("brushFlow"),
      QStringLiteral("brushHardness"), QStringLiteral("brushSpacing"),
      QStringLiteral("brushSmoothing"), QStringLiteral("brushPressureSize"),
      QStringLiteral("brushPressureFlow"), QStringLiteral("brushPressureOpacity"),
      QStringLiteral("brushAutoMaskTolerance"), QStringLiteral("feather"),
  };
  for (const auto& key : double_keys) {
    if (preset.contains(key)) {
      mask->insert(key, preset.value(key).toDouble());
    }
  }
  const QStringList bool_keys = {
      QStringLiteral("brushBuildup"), QStringLiteral("brushEraseMode"),
      QStringLiteral("brushAutoMask"),
      QStringLiteral("brushAutoMaskContinuousSampling"),
      QStringLiteral("brushProtectEdges"),
  };
  for (const auto& key : bool_keys) {
    if (preset.contains(key)) {
      mask->insert(key, preset.value(key).toBool());
    }
  }
}

}  // namespace photo_editor::desktop
