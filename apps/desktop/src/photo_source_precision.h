#pragma once

#include "edit_recipe_state.h"
#include "photo_layer_adjustment_recipe.h"

#include <QVariantMap>
#include <QImage>

namespace photo_editor::desktop {

// These selections sample source pixels or author coverage on the source
// raster. For settled output, evaluate/composite before publication resizing.
// Draft quality is a caller decision; this predicate describes the recipe.
inline bool needs_native_mask_sampling(const QVariantMap& mask) {
  if (!mask.value(QStringLiteral("enabled"), true).toBool() ||
      mask.value(QStringLiteral("maskSetPreviewBypassed")).toBool()) return false;
  const auto kind = mask.value(QStringLiteral("kind")).toString().trimmed().toLower();
  if (kind == QStringLiteral("brush") || kind == QStringLiteral("color") ||
      kind == QStringLiteral("luminance") || kind == QStringLiteral("depth") ||
      mask.value(QStringLiteral("toolRole")).toString() == QStringLiteral("retouch") ||
      !mask.value(QStringLiteral("retouchMode")).toString().trimmed().isEmpty() ||
      mask.value(QStringLiteral("removeStrength")).toDouble() > 0.0) return true;
  for (const auto* key : {"selectionFineEdge", "selectionDecontaminate",
           "maskSetSelectionFineEdge", "maskSetSelectionDecontaminate"})
    if (mask.value(QLatin1String(key)).toDouble() > 0.0) return true;
  for (const auto* key : {"sources", "components", "masks"})
    for (const auto& child : mask.value(QLatin1String(key)).toList())
      if (needs_native_mask_sampling(child.toMap())) return true;
  return false;
}

inline bool needs_native_composite(const EditRecipeState& state) {
  for (const auto& value : state.mask_stack)
    if (needs_native_mask_sampling(value.toMap())) return true;
  for (const auto& value : state.layers) {
    const auto layer = value.toMap();
    if (!layer.value(QStringLiteral("enabled"), true).toBool()) continue;
    const auto kind = layer.value(QStringLiteral("kind")).toString().trimmed().toLower();
    // currentState() contains the persisted recipe, not photoLayers()' UI-only
    // hasNonNeutralAdjustments annotation. Derive active local edits from the
    // same default-aware recipe contract used by thumbnail rendering.
    const auto has_local_recipe = photo_layer_has_non_neutral_adjustments(layer);
    if ((kind == QStringLiteral("source") ||
         layer.value(QStringLiteral("baseImageEdit")).toBool()) && has_local_recipe) return true;
    if (layer.value(QStringLiteral("selectionScoped")).toBool() ||
        !layer.value(QStringLiteral("maskData")).toByteArray().isEmpty()) return true;
    const auto empty_base = layer.value(QStringLiteral("baseImageEdit")).toBool() &&
        layer.value(QStringLiteral("copyBaseImage")).toBool() &&
        layer.value(QStringLiteral("paintTiles")).toMap().value(QStringLiteral("tiles")).toMap().isEmpty() &&
        layer.value(QStringLiteral("paintDraftTiles")).toMap().isEmpty() &&
        layer.value(QStringLiteral("paintDraftImage")).value<QImage>().isNull() &&
        layer.value(QStringLiteral("pixelData")).toByteArray().isEmpty() &&
        !has_local_recipe &&
        layer.value(QStringLiteral("x")).toDouble() == 0.0 &&
        layer.value(QStringLiteral("y")).toDouble() == 0.0 &&
        layer.value(QStringLiteral("scale"), 1.0).toDouble() == 1.0 &&
        layer.value(QStringLiteral("rotation")).toDouble() == 0.0 &&
        layer.value(QStringLiteral("opacity"), 1.0).toDouble() == 1.0 &&
        (layer.value(QStringLiteral("blendMode")).toString().isEmpty() ||
         layer.value(QStringLiteral("blendMode")).toString() == QStringLiteral("normal"));
    if (kind == QStringLiteral("pixel") && !empty_base &&
        (layer.contains(QStringLiteral("paintTiles")) ||
         layer.contains(QStringLiteral("paintDraftTiles")) ||
         layer.value(QStringLiteral("paintDraftImage")).canConvert<QImage>() ||
         !layer.value(QStringLiteral("pixelData")).toByteArray().isEmpty())) return true;
    if (needs_native_mask_sampling(layer.value(QStringLiteral("mask")).toMap())) return true;
    for (const auto* key : {"masks", "effectMasks", "selectionMasks"})
      for (const auto& mask : layer.value(QLatin1String(key)).toList())
        if (needs_native_mask_sampling(mask.toMap())) return true;
  }
  return false;
}

}  // namespace photo_editor::desktop
