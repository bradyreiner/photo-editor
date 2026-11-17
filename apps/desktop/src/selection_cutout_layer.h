#pragma once
#include <utility>
#include <QVariantMap>
#include <QUuid>

namespace photo_editor::desktop {
// Shared by editable cutout creation and export-only snapshots.
inline QVariantMap selection_cutout_layer(QVariantMap copy, const QVariantMap& mask, bool from_layer) {
  const auto id = mask.value("maskSetId").toString();
  copy.insert("id", QUuid::createUuid().toString(QUuid::WithoutBraces));
  copy.remove("parentId");
  copy.insert("enabled", true);
  copy.insert("solo", false);
  copy.insert("locked", false);
  copy.insert("clippingMask", false);
  const bool linked = from_layer && copy.value("selectionScoped").toBool();
  copy.insert("selectionScoped", true);
  if (linked) {
    auto intersections = copy.value("selectionIntersectionMaskSetIds").toList();
    auto names = copy.value("selectionIntersectionMaskSetNames").toList();
    auto primary = copy.value("selectionMaskSetIds").toList();
    primary.append(copy.value("selectionMaskSetId"));
    if (!primary.contains(id) && !intersections.contains(id)) {
      intersections.append(id);
      names.append(mask.value("maskSetName"));
    }
    copy.insert("selectionIntersectionMaskSetIds", intersections);
    copy.insert("selectionIntersectionMaskSetNames", names);
  } else {
    copy.insert("selectionMaskSetId", id);
    copy.insert("selectionMaskSetName", mask.value("maskSetName"));
    copy.insert("selectionMaskSetIds", QVariantList{id});
    copy.insert("selectionMaskSetNames", QVariantList{mask.value("maskSetName")});
  }
  copy.insert("maskEnabled", true);
  return copy;
}
}  // namespace photo_editor::desktop
