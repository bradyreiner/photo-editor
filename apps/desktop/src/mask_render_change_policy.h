#pragma once

#include "mask_workflow_ownership.h"

#include <QHash>
#include <QSet>
#include <cmath>

namespace photo_editor::desktop {

// Free, neutral selections affect overlays and future gestures, not existing
// photograph pixels. Keep legacy local color, retouch, and all mask consumers
// conservative: those changes still need the complete photographic pipeline.
inline bool mask_stack_change_affects_photo(
    const QVariantList& before, const QVariantList& after, const QVariantList& layers) {
  if (before == after) return false;
  QHash<QString, QVariantMap> previous;
  QSet<QString> changed_references;
  const auto references = [](const QVariantMap& mask) {
    return QStringList{mask.value("id").toString(), mask.value("maskSetId").toString(),
        mask.value("maskSetName").toString()};
  };
  const auto needs_local_render = [](const QVariantMap& mask) {
    if (mask.value("toolRole").toString() != QStringLiteral("selection") ||
        mask.value("solo").toBool()) return true;
    for (const auto* key : {"exposure", "contrast", "highlights", "shadows", "whites", "blacks",
             "temperature", "tint", "vibrance", "saturation", "hueShift", "saturationShift",
             "colorizeHue", "colorizeStrength"}) {
      const auto value = mask.value(QLatin1String(key)).toDouble();
      if (!std::isfinite(value) || std::abs(value) > 1e-6) return true;
    }
    return false;
  };
  for (const auto& value : before) {
    const auto mask = value.toMap();
    if (needs_local_render(mask) || mask.value("id").toString().isEmpty()) return true;
    previous.insert(mask.value("id").toString(), mask);
  }
  for (const auto& value : after) {
    const auto mask = value.toMap();
    const auto id = mask.value("id").toString();
    if (needs_local_render(mask) || id.isEmpty()) return true;
    const auto old = previous.take(id);
    if (mask != old) {
      for (const auto& reference : references(mask) + references(old))
        if (!reference.isEmpty()) changed_references.insert(reference);
    }
  }
  for (const auto& mask : previous)
    for (const auto& reference : references(mask))
      if (!reference.isEmpty()) changed_references.insert(reference);
  // Reordering identical sources can change composed mask coverage.
  if (changed_references.isEmpty())
    for (const auto& value : after)
      for (const auto& reference : references(value.toMap()))
        if (!reference.isEmpty()) changed_references.insert(reference);
  for (const auto& value : layers) {
    const auto layer = value.toMap();
    if (mask_owner_contains_reference(layer, changed_references)) return true;
    // Embedded documents have independent mask namespaces and may contain
    // legacy references. Do not infer independence through those boundaries.
    if (layer.contains("smartObjectDocument") || layer.contains("children") || layer.contains("layers")) return true;
    QSet<QString> operations;
    if (!collect_layer_operation_mask_references(layer, &operations)) return true;
    if (operations.intersects(changed_references)) return true;
  }
  return false;
}

}  // namespace photo_editor::desktop
