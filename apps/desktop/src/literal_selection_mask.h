#pragma once

#include "edit_recipe_mask_schema.h"

namespace photo_editor::desktop {

// Authored coverage is already refined. Use the literal brush-raster path so
// importing an edit never thresholds soft alpha or applies automatic cleanup.
inline QVariantMap literal_selection_mask(const QByteArray& alpha, const QSize& size) {
  auto mask = default_mask_definition(QStringLiteral("brush"));
  mask.insert(QStringLiteral("name"), QStringLiteral("Mask"));
  mask.insert(QStringLiteral("coordinateSpace"), QStringLiteral("source"));
  mask.insert(QStringLiteral("feather"), 0.0);
  for (const auto* key : {"selectionSmooth", "selectionFineEdge", "selectionEdgeContrast",
           "selectionDecontaminate", "selectionGrow", "selectionShrink", "selectionFeather",
           "selectionBlur", "selectionHarden", "selectionDespeckle", "selectionFillHoles",
           "selectionRemoveIslands"})
    mask.insert(QLatin1String(key), 0.0);
  mask.insert(QStringLiteral("segmentationData"), alpha);
  mask.insert(QStringLiteral("segmentationWidth"), size.width());
  mask.insert(QStringLiteral("segmentationHeight"), size.height());
  return mask;
}

}  // namespace photo_editor::desktop
