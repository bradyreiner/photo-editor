#pragma once

#include "photo_component_object_table.h"
#include "photo_smart_document_payload_codec.h"
#include "recipe_asset_store.h"

#include <QMap>
#include <QMetaType>
#include <QSet>
#include <QString>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

#include <limits>
#include <utility>

namespace photo_editor::desktop::component_asset_storage {

inline constexpr auto kWrapperField = "$looksyComponentLayerAssets";
inline constexpr auto kWrapperSchema = "looksy.component-layer-assets";
inline constexpr int kWrapperVersion = 1;
inline constexpr auto kDescriptorType =
    component_object_table::kDescriptorType;
inline constexpr auto kDocumentType =
    component_object_table::kDocumentType;
inline constexpr int kMaximumLayerDepth = 64;

namespace detail {

inline bool fail(QString* error, const QString& message) {
  if (error != nullptr) *error = message;
  return false;
}

inline void clear_error(QString* error) {
  if (error != nullptr) error->clear();
}

inline bool is_map(const QVariant& value) {
  return value.metaType() == QMetaType::fromType<QVariantMap>();
}

inline bool is_list(const QVariant& value) {
  return value.metaType() == QMetaType::fromType<QVariantList>();
}

inline bool exact_string(const QVariant& value, QString* result) {
  if (value.metaType() != QMetaType::fromType<QString>()) return false;
  *result = value.toString();
  return true;
}

inline bool exact_integer_one(const QVariant& value) {
  switch (value.metaType().id()) {
    case QMetaType::Int:
    case QMetaType::UInt:
    case QMetaType::LongLong:
      return value.toLongLong() == 1;
    case QMetaType::ULongLong:
      return value.toULongLong() == 1;
    default:
      return false;
  }
}

inline bool exact_equal(const QVariant& left, const QVariant& right) {
  return left.metaType() == right.metaType() && left == right;
}

inline bool component_layer(const QVariantMap& layer) {
  if (layer.contains(QStringLiteral("smartObject")) ||
      layer.contains(QStringLiteral("smartObjectDocument")) ||
      layer.contains(QString::fromLatin1(
          component_object_table::kDescriptorReferenceField)) ||
      layer.contains(QString::fromLatin1(
          component_object_table::kDocumentReferenceField)))
    return true;
  for (const auto* key : {"children", "layers"}) {
    const auto children = layer.value(QString::fromLatin1(key));
    if (!is_list(children)) continue;
    for (const auto& child : children.toList())
      if (is_map(child) && component_layer(child.toMap())) return true;
  }
  return false;
}

inline bool versioned_reference(const QVariant& value,
                                const QString& expected_type,
                                QString* ref, QString* error) {
  if (!is_map(value)) {
    return fail(error, QStringLiteral(
        "Component layer contains an invalid object reference."));
  }
  const auto reference = value.toMap();
  QString schema;
  QString type;
  if (reference.size() != 4 ||
      !exact_string(reference.value(QStringLiteral("schema")), &schema) ||
      schema != QString::fromLatin1(component_object_table::kReferenceSchema) ||
      !exact_integer_one(reference.value(QStringLiteral("version"))) ||
      !exact_string(reference.value(QStringLiteral("type")), &type) ||
      type != expected_type ||
      !exact_string(reference.value(QStringLiteral("ref")), ref) ||
      ref->isEmpty()) {
    return fail(error, QStringLiteral(
        "Component layer object reference schema or identity is invalid."));
  }
  return true;
}

using RowsByRef = QMap<QString, QVariantMap>;

class ClosureCollector {
 public:
  ClosureCollector(const RowsByRef& rows, QSet<QString>* closure)
      : rows_(rows), closure_(closure) {}

  bool add_layer(const QVariantMap& layer, int document_depth,
                 int layer_depth, QString* error) {
    if (document_depth > component_object_table::kMaximumDocumentDepth ||
        layer_depth > kMaximumLayerDepth) {
      return fail(error, QStringLiteral(
          "Component layer asset closure exceeds the depth limit."));
    }
    const auto descriptor_field = QString::fromLatin1(
        component_object_table::kDescriptorReferenceField);
    const auto document_field = QString::fromLatin1(
        component_object_table::kDocumentReferenceField);
    if (layer.contains(descriptor_field) &&
        !add_reference(layer.value(descriptor_field),
                       QString::fromLatin1(kDescriptorType),
                       document_depth, error))
      return false;
    if (layer.contains(document_field) &&
        !add_reference(layer.value(document_field),
                       QString::fromLatin1(kDocumentType),
                       document_depth, error))
      return false;
    for (const auto* key : {"children", "layers"}) {
      const auto field = QString::fromLatin1(key);
      if (!layer.contains(field)) continue;
      const auto child_values = layer.value(field);
      if (!is_list(child_values)) continue;
      for (const auto& child_value : child_values.toList()) {
        if (is_map(child_value) &&
            !add_layer(child_value.toMap(), document_depth,
                       layer_depth + 1, error))
          return false;
      }
    }
    return true;
  }

 private:
  bool add_reference(const QVariant& marker, const QString& expected_type,
                     int document_depth, QString* error) {
    QString ref;
    if (!versioned_reference(marker, expected_type, &ref, error)) return false;
    if (!rows_.contains(ref)) {
      return fail(error, QStringLiteral(
          "Component layer references an object missing from the packed table."));
    }
    const auto row = rows_.value(ref);
    if (row.value(QStringLiteral("type")).toString() != expected_type) {
      return fail(error, QStringLiteral(
          "Component layer object reference has the wrong table type."));
    }
    if (expected_type != QString::fromLatin1(kDocumentType)) {
      closure_->insert(ref);
      return true;
    }
    if (active_documents_.contains(ref)) {
      return fail(error, QStringLiteral(
          "Component layer asset closure contains a document cycle."));
    }
    if (closure_->contains(ref)) return true;
    active_documents_.insert(ref);
    closure_->insert(ref);
    const auto document = row.value(QStringLiteral("value")).toMap();
    const auto child_layers_value = document.value(QStringLiteral("layers"));
    if (!is_list(child_layers_value)) {
      active_documents_.remove(ref);
      return fail(error, QStringLiteral(
          "Component document table value has no layer list."));
    }
    for (const auto& child_value : child_layers_value.toList()) {
      if (!is_map(child_value) ||
          !add_layer(child_value.toMap(), document_depth + 1, 0, error)) {
        active_documents_.remove(ref);
        if (error != nullptr && error->isEmpty())
          *error = QStringLiteral(
              "Component document table contains a malformed layer.");
        return false;
      }
    }
    active_documents_.remove(ref);
    return true;
  }

  const RowsByRef& rows_;
  QSet<QString>* closure_ = nullptr;
  QSet<QString> active_documents_;
};

inline QVariantMap asset_binding(const QVariantMap& row,
                                 const QString& asset_id) {
  return {{QStringLiteral("ref"), row.value(QStringLiteral("ref"))},
          {QStringLiteral("type"), row.value(QStringLiteral("type"))},
          {QStringLiteral("contentId"), row.value(QStringLiteral("contentId"))},
          {QStringLiteral("revision"), row.value(QStringLiteral("revision"))},
          {QStringLiteral("assetId"), asset_id}};
}

inline bool build_wrapper(const QVariantMap& packed_layer,
                          const QVariantList& table_rows,
                          const RowsByRef& rows_by_ref,
                          const QMap<QString, QString>& asset_by_ref,
                          QVariantMap* wrapper, QString* error) {
  QSet<QString> closure;
  ClosureCollector collector(rows_by_ref, &closure);
  if (!collector.add_layer(packed_layer, 0, 0, error)) return false;
  if (closure.isEmpty()) {
    return fail(error, QStringLiteral(
        "Component layer has no externalized object references."));
  }

  QVariantList object_assets;
  for (const auto& row_value : table_rows) {
    const auto row = row_value.toMap();
    const auto ref = row.value(QStringLiteral("ref")).toString();
    if (!closure.contains(ref)) continue;
    if (!asset_by_ref.contains(ref)) {
      return fail(error, QStringLiteral(
          "Component layer object asset was not stored."));
    }
    object_assets.append(asset_binding(row, asset_by_ref.value(ref)));
  }
  *wrapper = {{QStringLiteral("schema"), QString::fromLatin1(kWrapperSchema)},
              {QStringLiteral("version"), kWrapperVersion},
              {QStringLiteral("packedLayer"), packed_layer},
              {QStringLiteral("objectAssets"), std::move(object_assets)}};
  return true;
}

}  // namespace detail

[[nodiscard]] inline bool has_component_layer_asset_wrapper(
    const QVariantMap& asset_map) {
  return asset_map.contains(QString::fromLatin1(kWrapperField));
}

// Encode the layer tree once, externalize all shared objects in a preflight,
// store each table row once, then return runtime layers with only component
// placements wrapped. Non-component layers are returned unchanged for legacy
// asset storage. `stored_asset_count` counts only unique object-row assets.
[[nodiscard]] inline bool prepare_component_layer_asset_wrappers(
    const QVariantList& runtime_layers,
    const RecipeAssetStore& asset_store,
    QVariantList* prepared_layers,
    int* stored_asset_count,
    QString* error = nullptr) {
  detail::clear_error(error);
  if (prepared_layers == nullptr || stored_asset_count == nullptr) {
    return detail::fail(error, QStringLiteral(
        "Component layer asset preparation requires output storage."));
  }
  if (runtime_layers.size() > component_object_table::kMaximumExpandedNodeCount) {
    return detail::fail(error, QStringLiteral(
        "Component layer asset preparation exceeds the layer node limit."));
  }
  for (const auto& value : runtime_layers) {
    if (!detail::is_map(value)) {
      return detail::fail(error, QStringLiteral(
          "Component layer asset preparation requires layer maps."));
    }
  }

  bool has_components = false;
  for (const auto& value : runtime_layers)
    if (detail::component_layer(value.toMap())) has_components = true;
  if (!has_components) {
    *prepared_layers = runtime_layers;
    *stored_asset_count = 0;
    return true;
  }

  const auto encoded_layers =
      encode_component_payloads(runtime_layers).toList();
  QVariantMap packed_recipe{{QStringLiteral("layers"), encoded_layers}};
  if (!component_object_table::externalize_component_objects(
          &packed_recipe, error))
    return false;
  if (!component_object_table::has_table(packed_recipe)) {
    return detail::fail(error, QStringLiteral(
        "Component layer asset preparation produced no object table."));
  }

  const auto table = packed_recipe.value(QString::fromLatin1(
      component_object_table::kTableField)).toMap();
  const auto table_rows = table.value(QStringLiteral("objects")).toList();
  detail::RowsByRef rows_by_ref;
  QMap<QString, QString> asset_by_ref;
  for (const auto& row_value : table_rows) {
    if (!detail::is_map(row_value)) {
      return detail::fail(error, QStringLiteral(
          "Component object table has a malformed row."));
    }
    const auto row = row_value.toMap();
    const auto ref = row.value(QStringLiteral("ref")).toString();
    if (ref.isEmpty() || rows_by_ref.contains(ref)) {
      return detail::fail(error, QStringLiteral(
          "Component object table has a duplicate or empty reference."));
    }
    rows_by_ref.insert(ref, row);
  }

  int stored_count = 0;
  for (const auto& row_value : table_rows) {
    const auto row = row_value.toMap();
    const auto ref = row.value(QStringLiteral("ref")).toString();
    const auto stored = asset_store.putVariant(row);
    if (!stored.ok()) {
      return detail::fail(error,
          stored.error.isEmpty()
              ? QStringLiteral("Unable to store component object row.")
              : stored.error);
    }
    const auto asset_id = QString::fromStdString(stored.asset->value);
    asset_by_ref.insert(ref, asset_id);
    ++stored_count;
  }

  const auto packed_layers = packed_recipe.value(QStringLiteral("layers")).toList();
  if (packed_layers.size() != runtime_layers.size()) {
    return detail::fail(error, QStringLiteral(
        "Component object table changed the root layer count."));
  }
  QVariantList output;
  output.reserve(runtime_layers.size());
  for (qsizetype index = 0; index < runtime_layers.size(); ++index) {
    const auto packed_layer = packed_layers.at(index).toMap();
    if (!detail::component_layer(runtime_layers.at(index).toMap())) {
      output.append(runtime_layers.at(index));
      continue;
    }
    QVariantMap wrapper;
    if (!detail::build_wrapper(packed_layer, table_rows, rows_by_ref,
                               asset_by_ref, &wrapper, error))
      return false;
    output.append(QVariantMap{
        {QString::fromLatin1(kWrapperField), std::move(wrapper)}});
  }
  *prepared_layers = std::move(output);
  *stored_asset_count = stored_count;
  return true;
}

// Restore one wrapped layer asset. The wrapper's closure must be complete and
// contain no unrelated rows; the shared table inflater validates ref versions,
// identities, conflicts, cycles, depth, and payload budgets. Legacy plain layer
// assets bypass this helper and retain their original read behavior.
[[nodiscard]] inline bool restore_component_layer_asset_wrapper(
    const QVariantMap& asset_map,
    const RecipeAssetStore& asset_store,
    QVariantMap* runtime_layer,
    QString* error = nullptr) {
  detail::clear_error(error);
  if (runtime_layer == nullptr) {
    return detail::fail(error, QStringLiteral(
        "Component layer asset restoration requires output storage."));
  }
  const auto wrapper_value = asset_map.value(QString::fromLatin1(kWrapperField));
  if (!detail::is_map(wrapper_value)) {
    return detail::fail(error, QStringLiteral(
        "Component layer asset wrapper is malformed."));
  }
  const auto wrapper = wrapper_value.toMap();
  QString schema;
  if (wrapper.size() != 4 ||
      !detail::exact_string(wrapper.value(QStringLiteral("schema")), &schema) ||
      schema != QString::fromLatin1(kWrapperSchema)) {
    return detail::fail(error, QStringLiteral(
        "Component layer asset wrapper schema is unsupported."));
  }
  if (!detail::exact_integer_one(wrapper.value(QStringLiteral("version")))) {
    return detail::fail(error, QStringLiteral(
        "Component layer asset wrapper version is unsupported."));
  }
  const auto packed_layer_value = wrapper.value(QStringLiteral("packedLayer"));
  const auto object_assets_value = wrapper.value(QStringLiteral("objectAssets"));
  if (!detail::is_map(packed_layer_value) ||
      !detail::is_list(object_assets_value)) {
    return detail::fail(error, QStringLiteral(
        "Component layer asset wrapper payload is malformed."));
  }
  const auto packed_layer = packed_layer_value.toMap();
  const auto object_assets = object_assets_value.toList();
  if (object_assets.isEmpty() || object_assets.size() >
          component_object_table::kMaximumObjectCount) {
    return detail::fail(error, QStringLiteral(
        "Component layer asset wrapper object closure is empty or too large."));
  }

  QVariantList rows;
  rows.reserve(object_assets.size());
  QSet<QString> seen_refs;
  for (const auto& binding_value : object_assets) {
    if (!detail::is_map(binding_value)) {
      return detail::fail(error, QStringLiteral(
          "Component layer object asset binding is malformed."));
    }
    const auto binding = binding_value.toMap();
    QString ref;
    QString type;
    QString content_id;
    QString asset_id;
    if (binding.size() != 5 ||
        !detail::exact_string(binding.value(QStringLiteral("ref")), &ref) ||
        !detail::exact_string(binding.value(QStringLiteral("type")), &type) ||
        !detail::exact_string(binding.value(QStringLiteral("contentId")), &content_id) ||
        !detail::exact_string(binding.value(QStringLiteral("assetId")), &asset_id) ||
        ref.isEmpty() || asset_id.isEmpty() || seen_refs.contains(ref)) {
      return detail::fail(error, QStringLiteral(
          "Component layer object asset binding identity is invalid."));
    }
    seen_refs.insert(ref);
    const auto restored = asset_store.getVariant(
        photo_editor::pipeline::AssetIdentifier{asset_id.toStdString()}, error);
    if (!restored.has_value() || !detail::is_map(*restored)) {
      if (error != nullptr && error->isEmpty())
        *error = QStringLiteral(
            "Component object asset payload is missing or is not a map.");
      return false;
    }
    const auto row = restored->toMap();
    if (row.size() != 5 ||
        !detail::exact_equal(row.value(QStringLiteral("ref")),
                             binding.value(QStringLiteral("ref"))) ||
        !detail::exact_equal(row.value(QStringLiteral("type")),
                             binding.value(QStringLiteral("type"))) ||
        !detail::exact_equal(row.value(QStringLiteral("contentId")),
                             binding.value(QStringLiteral("contentId"))) ||
        !detail::exact_equal(row.value(QStringLiteral("revision")),
                             binding.value(QStringLiteral("revision")))) {
      return detail::fail(error, QStringLiteral(
          "Component object asset metadata does not match its stored row."));
    }
    rows.append(row);
  }

  QVariantMap packed_recipe{
      {QStringLiteral("componentObjectTable"), QVariantMap{
           {QStringLiteral("schema"), QString::fromLatin1(
                component_object_table::kTableSchema)},
           {QStringLiteral("version"), component_object_table::kTableVersion},
           {QStringLiteral("objects"), rows}}},
      {QStringLiteral("layers"), QVariantList{packed_layer}}};
  if (!component_object_table::inline_component_objects(&packed_recipe, error))
    return false;
  const auto restored_layers = packed_recipe.value(QStringLiteral("layers")).toList();
  if (restored_layers.size() != 1 || !detail::is_map(restored_layers.constFirst())) {
    return detail::fail(error, QStringLiteral(
        "Component layer asset wrapper did not restore one layer."));
  }
  const auto decoded = decode_component_payloads(restored_layers.constFirst());
  if (!detail::is_map(decoded)) {
    return detail::fail(error, QStringLiteral(
        "Component layer payload decoding did not produce a map."));
  }
  *runtime_layer = decoded.toMap();
  return true;
}

}  // namespace photo_editor::desktop::component_asset_storage
