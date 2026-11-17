#pragma once

#include "edit_recipe_state.h"
#include "photo_component_object_table.h"

namespace photo_editor::desktop {

inline bool pack_recipe_component_objects(QJsonObject* object, QString* error = nullptr,
                                         bool require_table = false) {
  auto packed = object->toVariantMap();
  if (!component_object_table::externalize_component_objects(&packed, error)) return false;
  if (require_table && !component_object_table::has_table(packed)) {
    packed.insert(QString::fromLatin1(component_object_table::kTableField), QVariantMap{
        {"schema", QString::fromLatin1(component_object_table::kTableSchema)},
        {"version", component_object_table::kTableVersion}, {"objects", QVariantList{}}});
  }
  *object = QJsonObject::fromVariantMap(packed);
  return true;
}

inline bool expand_recipe_component_objects(QJsonObject* object, int recipe_version,
                                           QString* error = nullptr) {
  auto expanded = object->toVariantMap();
  const bool table = component_object_table::has_table(expanded);
  const bool refs = component_object_table::has_references(expanded);
  if (recipe_version >= kPhotoComponentObjectTableEditRecipeVersion) {
    if (!table) {
      if (error) *error = QStringLiteral("Recipe version 18 requires a component object table.");
      return false;
    }
    if (!component_object_table::inline_component_objects(&expanded, error)) return false;
  } else if (table || refs) {
    if (error) *error = QStringLiteral("Component object tables require recipe version 18.");
    return false;
  }
  *object = QJsonObject::fromVariantMap(expanded);
  return true;
}

}  // namespace photo_editor::desktop
