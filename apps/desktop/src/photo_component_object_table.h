#pragma once

#include "photo_smart_document_payload_codec.h"

#include <QByteArray>
#include <QMap>
#include <QMetaType>
#include <QSet>
#include <QString>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>
#include <QUuid>

#include <cmath>
#include <limits>
#include <utility>

namespace photo_editor::desktop::component_object_table {

inline constexpr auto kTableField = "componentObjectTable";
inline constexpr auto kTableSchema = "looksy.component-object-table";
inline constexpr int kTableVersion = 1;
inline constexpr auto kReferenceSchema = "looksy.component-object-ref";
inline constexpr int kReferenceVersion = 1;
inline constexpr auto kDescriptorType = "smartObject";
inline constexpr auto kDocumentType = "smartObjectDocument";
inline constexpr auto kDescriptorReferenceField =
    "$looksyComponentObjectTableDescriptorRef";
inline constexpr auto kDocumentReferenceField =
    "$looksyComponentObjectTableDocumentRef";

// A table stores at most 64 MiB of unique JSON-ready payloads. Repeated
// references share those values during inflation, while the occurrence cap
// bounds the expanded layer graph independently.
inline constexpr qsizetype kMaximumPayloadBytes = 64LL * 1024LL * 1024LL;
inline constexpr qsizetype kMaximumObjectCount = 16384;
inline constexpr qsizetype kMaximumExpandedNodeCount = 131072;
inline constexpr int kMaximumDocumentDepth = 8;
inline constexpr int kMaximumLayerDepth = 64;
inline constexpr int kMaximumVariantDepth = 128;

namespace detail {

inline bool fail(QString* error, const QString& message) {
  if (error != nullptr) *error = message;
  return false;
}

inline void clear_error(QString* error) {
  if (error != nullptr) error->clear();
}

inline bool exact_variant(const QVariant& left, const QVariant& right) {
  if (left.metaType() != right.metaType()) return false;
  switch (left.metaType().id()) {
    case QMetaType::QVariantMap: {
      const auto a = left.toMap();
      const auto b = right.toMap();
      if (a.size() != b.size()) return false;
      auto bi = b.cbegin();
      for (auto ai = a.cbegin(); ai != a.cend(); ++ai, ++bi) {
        if (ai.key() != bi.key() || !exact_variant(ai.value(), bi.value()))
          return false;
      }
      return true;
    }
    case QMetaType::QVariantList: {
      const auto a = left.toList();
      const auto b = right.toList();
      if (a.size() != b.size()) return false;
      for (qsizetype i = 0; i < a.size(); ++i)
        if (!exact_variant(a.at(i), b.at(i))) return false;
      return true;
    }
    case QMetaType::UnknownType:
      return true;
    default:
      return left == right;
  }
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

inline bool integer_value(const QVariant& value, qint64* result) {
  switch (value.metaType().id()) {
    case QMetaType::Int:
    case QMetaType::UInt:
    case QMetaType::LongLong: {
      bool ok = false;
      const auto number = value.toLongLong(&ok);
      if (!ok) return false;
      *result = number;
      return true;
    }
    case QMetaType::ULongLong: {
      bool ok = false;
      const auto number = value.toULongLong(&ok);
      if (!ok || number > static_cast<qulonglong>(
                              std::numeric_limits<qint64>::max()))
        return false;
      *result = static_cast<qint64>(number);
      return true;
    }
    case QMetaType::Double: {
      const auto number = value.toDouble();
      if (!std::isfinite(number) || std::trunc(number) != number ||
          number < static_cast<double>(std::numeric_limits<qint64>::min()) ||
          number >= static_cast<double>(std::numeric_limits<qint64>::max()))
        return false;
      *result = static_cast<qint64>(number);
      return true;
    }
    default:
      return false;
  }
}

inline bool positive_integer(const QVariant& value, qint64* result) {
  return integer_value(value, result) && *result > 0;
}

inline bool canonical_uuid(const QString& value) {
  const QUuid parsed(value);
  return !parsed.isNull() &&
      parsed.toString(QUuid::WithoutBraces) == value;
}

inline bool lower_sha256(const QString& value) {
  if (value.size() != 64) return false;
  for (const auto character : value) {
    if (!((character >= QLatin1Char('0') && character <= QLatin1Char('9')) ||
          (character >= QLatin1Char('a') && character <= QLatin1Char('f'))))
      return false;
  }
  return true;
}

inline QString identity_key(const QString& type, const QString& content_id,
                            const QString& revision) {
  return type + QLatin1Char('\n') + content_id + QLatin1Char('\n') + revision;
}

inline QString stable_ref(const QString& type, const QString& content_id,
                          const QString& revision) {
  return type + QLatin1Char(':') + content_id + QLatin1Char(':') + revision;
}

inline bool charge(qsizetype amount, qsizetype limit, qsizetype* used) {
  if (amount < 0 || *used > limit || amount > limit - *used) return false;
  *used += amount;
  return true;
}

inline bool charge_string(const QString& value, qsizetype* byte_count,
                          QString* error) {
  if (!charge(16, kMaximumPayloadBytes, byte_count)) {
    return fail(error, QStringLiteral(
        "Component object table exceeds the payload byte limit."));
  }
  qsizetype utf8_size = 0;
  for (qsizetype i = 0; i < value.size(); ++i) {
    const auto character = value.at(i);
    qsizetype character_bytes = 3;
    if (character.unicode() <= 0x7f) {
      character_bytes = 1;
    } else if (character.unicode() <= 0x7ff) {
      character_bytes = 2;
    } else if (character.isHighSurrogate() && i + 1 < value.size() &&
               value.at(i + 1).isLowSurrogate()) {
      character_bytes = 4;
      ++i;
    }
    if (utf8_size > kMaximumPayloadBytes - *byte_count - character_bytes) {
      return fail(error, QStringLiteral(
          "Component object table exceeds the payload byte limit."));
    }
    utf8_size += character_bytes;
  }
  if (!charge(utf8_size, kMaximumPayloadBytes, byte_count)) {
    return fail(error, QStringLiteral(
        "Component object table exceeds the payload byte limit."));
  }
  return true;
}

inline bool charge_tree(const QVariant& value, int depth,
                        qsizetype* node_count, qsizetype* byte_count,
                        QString* error) {
  if (depth > kMaximumVariantDepth) {
    return fail(error, QStringLiteral(
        "Component object table payload nesting exceeds the safe limit."));
  }
  if (!charge(1, kMaximumExpandedNodeCount, node_count)) {
    return fail(error, QStringLiteral(
        "Component object table exceeds the expanded node limit."));
  }
  const auto byte_overhead = qsizetype{16};
  if (is_map(value)) {
    const auto map = value.toMap();
    if (!charge(byte_overhead, kMaximumPayloadBytes, byte_count))
      return fail(error, QStringLiteral(
          "Component object table exceeds the payload byte limit."));
    for (auto it = map.cbegin(); it != map.cend(); ++it) {
      if (!charge_string(it.key(), byte_count, error) ||
          !charge_tree(it.value(), depth + 1, node_count, byte_count, error))
        return false;
    }
    return true;
  }
  if (is_list(value)) {
    if (!charge(byte_overhead, kMaximumPayloadBytes, byte_count))
      return fail(error, QStringLiteral(
          "Component object table exceeds the payload byte limit."));
    for (const auto& item : value.toList())
      if (!charge_tree(item, depth + 1, node_count, byte_count, error))
        return false;
    return true;
  }
  if (value.metaType() == QMetaType::fromType<QString>()) {
    return charge_string(value.toString(), byte_count, error);
  }
  if (value.metaType() == QMetaType::fromType<QByteArray>()) {
    return fail(error, QStringLiteral(
        "Component object table requires byte payloads to use the tagged JSON form."));
  }
  if (!charge(byte_overhead, kMaximumPayloadBytes, byte_count))
    return fail(error, QStringLiteral(
        "Component object table exceeds the payload byte limit."));
  return true;
}

struct ObjectRow {
  QString type;
  QString content_id;
  QString revision;
  QString ref;
  QVariantMap value;
};

inline QVariantMap reference_value(const QString& type, const QString& ref) {
  return {{QStringLiteral("schema"), QString::fromLatin1(kReferenceSchema)},
          {QStringLiteral("version"), kReferenceVersion},
          {QStringLiteral("type"), type},
          {QStringLiteral("ref"), ref}};
}

inline bool descriptor_identity(const QVariantMap& descriptor,
                                QString* content_id, QString* revision,
                                QString* error) {
  if (!exact_string(descriptor.value(QStringLiteral("contentId")), content_id) ||
      !canonical_uuid(*content_id)) {
    return fail(error, QStringLiteral(
        "Smart Object descriptor has an invalid contentId."));
  }
  if (!exact_string(descriptor.value(QStringLiteral("sourceSha256")), revision) ||
      !lower_sha256(*revision)) {
    return fail(error, QStringLiteral(
        "Smart Object descriptor has an invalid source revision."));
  }
  return true;
}

inline bool document_identity(const QVariantMap& document,
                              QString* content_id, QString* revision,
                              QString* error) {
  if (!exact_string(document.value(QStringLiteral("contentId")), content_id) ||
      !canonical_uuid(*content_id)) {
    return fail(error, QStringLiteral(
        "Smart Object document has an invalid contentId."));
  }
  qint64 number = 0;
  if (!positive_integer(document.value(QStringLiteral("revision")), &number) ||
      number > std::numeric_limits<int>::max()) {
    return fail(error, QStringLiteral(
        "Smart Object document has an invalid revision."));
  }
  *revision = QString::number(number);
  return true;
}

inline bool validate_layer_list(const QVariant& value, QVariantList* layers,
                                QString* error) {
  if (!is_list(value)) {
    return fail(error, QStringLiteral(
        "Component object table requires layer arrays to be lists."));
  }
  *layers = value.toList();
  for (const auto& layer : *layers) {
    if (!is_map(layer)) {
      return fail(error, QStringLiteral(
          "Component object table requires layer entries to be maps."));
    }
  }
  return true;
}

class PackContext {
 public:
  bool process_root(QVariantMap* recipe, QString* error) {
    for (const auto* key : {"layers", "photoLayers"}) {
      const auto field = QString::fromLatin1(key);
      if (!recipe->contains(field)) continue;
      QVariantList layers;
      if (!validate_layer_list(recipe->value(field), &layers, error) ||
          !process_layers(&layers, 0, 0, error))
        return false;
      recipe->insert(field, std::move(layers));
    }

    if (rows_.isEmpty()) return true;
    if (rows_.size() > kMaximumObjectCount) {
      return fail(error, QStringLiteral(
          "Component object table exceeds the object count limit."));
    }

    QVariantList objects;
    objects.reserve(rows_.size());
    for (auto it = rows_.cbegin(); it != rows_.cend(); ++it) {
      const auto& row = it.value();
      objects.append(QVariantMap{
          {QStringLiteral("ref"), row.ref},
          {QStringLiteral("type"), row.type},
          {QStringLiteral("contentId"), row.content_id},
          {QStringLiteral("revision"), row.type == QString::fromLatin1(kDescriptorType)
                                             ? QVariant(row.revision)
                                             : QVariant(row.revision.toInt())},
          {QStringLiteral("value"), row.value}});
    }
    recipe->insert(QString::fromLatin1(kTableField), QVariantMap{
        {QStringLiteral("schema"), QString::fromLatin1(kTableSchema)},
        {QStringLiteral("version"), kTableVersion},
        {QStringLiteral("objects"), std::move(objects)}});
    return true;
  }

 private:
  bool add_node(QString* error) {
    if (!charge(1, kMaximumExpandedNodeCount, &node_count_))
      return fail(error, QStringLiteral(
          "Component object table exceeds the expanded node limit."));
    return true;
  }

  bool intern(const QString& type, const QString& content_id,
              const QString& revision, const QVariantMap& value,
              QString* ref, QString* error) {
    const auto key = identity_key(type, content_id, revision);
    const auto safe_value = value;
    const auto existing = rows_.constFind(key);
    if (existing != rows_.cend()) {
      if (!exact_variant(existing->value, safe_value)) {
        return fail(error, QStringLiteral(
            "Conflicting component object definitions share one content identity and revision."));
      }
      *ref = existing->ref;
      return true;
    }
    if (rows_.size() >= kMaximumObjectCount) {
      return fail(error, QStringLiteral(
          "Component object table exceeds the object count limit."));
    }
    const auto candidate_ref = stable_ref(type, content_id, revision);
    if (!charge_tree(safe_value, 0, &node_count_, &payload_bytes_, error))
      return false;
    rows_.insert(key, ObjectRow{type, content_id, revision, candidate_ref,
                                safe_value});
    *ref = candidate_ref;
    return true;
  }

  bool process_layers(QVariantList* layers, int document_depth,
                      int layer_depth, QString* error) {
    for (auto& layer_value : *layers) {
      if (!is_map(layer_value) ||
          !process_layer(&layer_value, document_depth, layer_depth, error))
        return false;
    }
    return true;
  }

  bool process_layer(QVariant* layer_value, int document_depth,
                     int layer_depth, QString* error) {
    if (layer_depth > kMaximumLayerDepth) {
      return fail(error, QStringLiteral(
          "Component object layer recursion exceeds the depth limit."));
    }
    if (!add_node(error)) return false;
    auto layer = layer_value->toMap();
    if (layer.contains(QString::fromLatin1(kDescriptorReferenceField)) ||
        layer.contains(QString::fromLatin1(kDocumentReferenceField))) {
      return fail(error, QStringLiteral(
          "Layer already contains a component object table reference."));
    }
    if (layer.contains(QStringLiteral("smartObject"))) {
      const auto descriptor_value = layer.value(QStringLiteral("smartObject"));
      if (!is_map(descriptor_value)) {
        return fail(error, QStringLiteral(
            "Smart Object descriptor must be a map before storage."));
      }
      const auto descriptor = descriptor_value.toMap();
      QString content_id;
      QString revision;
      QString ref;
      if (!descriptor_identity(descriptor, &content_id, &revision, error) ||
          !intern(QString::fromLatin1(kDescriptorType), content_id, revision,
                  descriptor, &ref, error))
        return false;
      layer.remove(QStringLiteral("smartObject"));
      layer.insert(QString::fromLatin1(kDescriptorReferenceField),
                   reference_value(QString::fromLatin1(kDescriptorType), ref));
    }
    if (layer.contains(QStringLiteral("smartObjectDocument"))) {
      if (document_depth >= kMaximumDocumentDepth) {
        return fail(error, QStringLiteral(
            "Nested Smart Object document recursion exceeds the depth limit."));
      }
      const auto document_value = layer.value(QStringLiteral("smartObjectDocument"));
      if (!is_map(document_value)) {
        return fail(error, QStringLiteral(
            "Smart Object document must be a map before storage."));
      }
      auto document = document_value.toMap();
      QString content_id;
      QString revision;
      if (!document_identity(document, &content_id, &revision, error)) return false;
      if (active_document_ids_.contains(content_id)) {
        return fail(error, QStringLiteral(
            "Smart Object document cycle detected through a repeated contentId."));
      }
      QVariantList nested_layers;
      if (!validate_layer_list(document.value(QStringLiteral("layers")),
                               &nested_layers, error))
        return false;
      active_document_ids_.insert(content_id);
      const auto nested_ok = process_layers(
          &nested_layers, document_depth + 1, 0, error);
      active_document_ids_.remove(content_id);
      if (!nested_ok) return false;
      document.insert(QStringLiteral("layers"), std::move(nested_layers));
      QString ref;
      if (!intern(QString::fromLatin1(kDocumentType), content_id, revision,
                  document, &ref, error))
        return false;
      layer.remove(QStringLiteral("smartObjectDocument"));
      layer.insert(QString::fromLatin1(kDocumentReferenceField),
                   reference_value(QString::fromLatin1(kDocumentType), ref));
    }

    for (const auto* child_key : {"children", "layers"}) {
      const auto key = QString::fromLatin1(child_key);
      const auto child_value = layer.value(key);
      if (!is_list(child_value)) continue;
      auto children = child_value.toList();
      for (auto& child : children) {
        if (is_map(child) &&
            !process_layer(&child, document_depth, layer_depth + 1, error))
          return false;
      }
      layer.insert(key, std::move(children));
    }
    *layer_value = std::move(layer);
    return true;
  }

  QMap<QString, ObjectRow> rows_;
  QSet<QString> active_document_ids_;
  qsizetype node_count_ = 0;
  qsizetype payload_bytes_ = 0;
};

struct ParsedTable {
  QMap<QString, ObjectRow> rows_by_ref;
  QMap<QString, QString> ref_by_identity;
};

inline bool marker_reference(const QVariant& marker_value,
                             const QString& expected_type,
                             QString* ref, QString* error) {
  if (!is_map(marker_value)) {
    return fail(error, QStringLiteral(
        "Component object reference must be a versioned map."));
  }
  const auto marker = marker_value.toMap();
  QString schema;
  if (marker.size() != 4 ||
      !exact_string(marker.value(QStringLiteral("schema")), &schema) ||
      schema != QString::fromLatin1(kReferenceSchema)) {
    return fail(error, QStringLiteral(
        "Component object reference schema is unsupported."));
  }
  qint64 version = 0;
  if (!integer_value(marker.value(QStringLiteral("version")), &version) ||
      version != kReferenceVersion) {
    return fail(error, QStringLiteral(
        "Component object reference version is unsupported."));
  }
  QString type;
  if (!exact_string(marker.value(QStringLiteral("type")), &type) ||
      type != expected_type ||
      !exact_string(marker.value(QStringLiteral("ref")), ref) || ref->isEmpty()) {
    return fail(error, QStringLiteral(
        "Component object reference type or identifier is invalid."));
  }
  return true;
}

inline bool parse_table(const QVariantMap& recipe, ParsedTable* parsed,
                        qsizetype* node_count, qsizetype* payload_bytes,
                        QString* error) {
  const auto table_value = recipe.value(QString::fromLatin1(kTableField));
  if (!is_map(table_value)) {
    return fail(error, QStringLiteral(
        "Component object table must be a map."));
  }
  const auto table = table_value.toMap();
  QString schema;
  if (table.size() != 3 ||
      !exact_string(table.value(QStringLiteral("schema")), &schema) ||
      schema != QString::fromLatin1(kTableSchema)) {
    return fail(error, QStringLiteral(
        "Component object table schema is unsupported."));
  }
  qint64 version = 0;
  if (!integer_value(table.value(QStringLiteral("version")), &version) ||
      version != kTableVersion) {
    return fail(error, QStringLiteral(
        "Component object table version is unsupported."));
  }
  const auto objects_value = table.value(QStringLiteral("objects"));
  if (!is_list(objects_value)) {
    return fail(error, QStringLiteral(
        "Component object table objects must be a list."));
  }
  const auto objects = objects_value.toList();
  if (objects.size() > kMaximumObjectCount) {
    return fail(error, QStringLiteral(
        "Component object table exceeds the object count limit."));
  }

  for (const auto& object_value : objects) {
    if (!is_map(object_value)) {
      return fail(error, QStringLiteral(
          "Component object table entries must be maps."));
    }
    const auto object = object_value.toMap();
    if (object.size() != 5) {
      return fail(error, QStringLiteral(
          "Component object table entry has an unsupported shape."));
    }
    QString type;
    QString content_id;
    QString ref;
    if (!exact_string(object.value(QStringLiteral("type")), &type) ||
        !exact_string(object.value(QStringLiteral("contentId")), &content_id) ||
        !exact_string(object.value(QStringLiteral("ref")), &ref)) {
      return fail(error, QStringLiteral(
          "Component object table entry has invalid identity fields."));
    }
    QString revision;
    if (type == QString::fromLatin1(kDescriptorType)) {
      if (!canonical_uuid(content_id) ||
          !exact_string(object.value(QStringLiteral("revision")), &revision) ||
          !lower_sha256(revision)) {
        return fail(error, QStringLiteral(
            "Smart Object table entry has an invalid identity."));
      }
    } else if (type == QString::fromLatin1(kDocumentType)) {
      qint64 number = 0;
      if (!canonical_uuid(content_id) ||
          !positive_integer(object.value(QStringLiteral("revision")), &number) ||
          number > std::numeric_limits<int>::max()) {
        return fail(error, QStringLiteral(
            "Smart Object document table entry has an invalid identity."));
      }
      revision = QString::number(number);
    } else {
      return fail(error, QStringLiteral(
          "Component object table entry type is unsupported."));
    }
    const auto expected_ref = stable_ref(type, content_id, revision);
    if (ref != expected_ref) {
      return fail(error, QStringLiteral(
          "Component object table entry reference does not match its identity."));
    }
    const auto value = object.value(QStringLiteral("value"));
    if (!is_map(value)) {
      return fail(error, QStringLiteral(
          "Component object table value must be a map."));
    }
    const auto object_map = value.toMap();
    if (type == QString::fromLatin1(kDescriptorType)) {
      QString value_id;
      QString value_revision;
      if (!descriptor_identity(object_map, &value_id, &value_revision, error) ||
          value_id != content_id || value_revision != revision) {
        if (error != nullptr && error->isEmpty())
          *error = QStringLiteral(
              "Smart Object table value does not match its identity.");
        return false;
      }
    } else {
      QString value_id;
      QString value_revision;
      if (!document_identity(object_map, &value_id, &value_revision, error) ||
          value_id != content_id || value_revision != revision) {
        if (error != nullptr && error->isEmpty())
          *error = QStringLiteral(
              "Smart Object document table value does not match its identity.");
        return false;
      }
      QVariantList ignored_layers;
      if (!validate_layer_list(object_map.value(QStringLiteral("layers")),
                               &ignored_layers, error))
        return false;
    }
    if (parsed->rows_by_ref.contains(ref)) {
      return fail(error, QStringLiteral(
          "Component object table contains a duplicate reference."));
    }
    const auto key = identity_key(type, content_id, revision);
    if (parsed->ref_by_identity.contains(key)) {
      return fail(error, QStringLiteral(
          "Component object table contains duplicate content identities."));
    }
    if (!charge_tree(value, 0, node_count, payload_bytes, error)) return false;
    if (type == QString::fromLatin1(kDescriptorType)) {
      const auto row_revision = revision;
      parsed->rows_by_ref.insert(ref, ObjectRow{
          type, content_id, row_revision, ref, object_map});
    } else {
      parsed->rows_by_ref.insert(ref, ObjectRow{
          type, content_id, revision, ref, object_map});
    }
    parsed->ref_by_identity.insert(key, ref);
  }
  return true;
}

inline bool scan_layers_for_markers(const QVariantList& layers, bool* found,
                                    qsizetype* node_count, QString* error,
                                    bool require_maps = true,
                                    int layer_depth = 0,
                                    int document_depth = 0) {
  if (layer_depth > kMaximumLayerDepth ||
      document_depth > kMaximumDocumentDepth) {
    return fail(error, QStringLiteral(
        "Component object layer graph exceeds the depth limit."));
  }
  for (const auto& value : layers) {
    if (!is_map(value)) {
      if (!require_maps) continue;
      return fail(error, QStringLiteral(
          "Component object layer graph contains a non-map entry."));
    }
    if (!charge(1, kMaximumExpandedNodeCount, node_count))
      return fail(error, QStringLiteral(
          "Component object layer graph exceeds the node limit."));
    const auto layer = value.toMap();
    if (layer.contains(QString::fromLatin1(kDescriptorReferenceField)) ||
        layer.contains(QString::fromLatin1(kDocumentReferenceField))) {
      *found = true;
    }
    if (layer.contains(QStringLiteral("smartObjectDocument")) &&
        is_map(layer.value(QStringLiteral("smartObjectDocument")))) {
      const auto document = layer.value(QStringLiteral("smartObjectDocument")).toMap();
      if (is_list(document.value(QStringLiteral("layers")))) {
        if (!scan_layers_for_markers(
                document.value(QStringLiteral("layers")).toList(), found,
                node_count, error, true, 0, document_depth + 1))
          return false;
      }
    }
    for (const auto* child_key : {"children", "layers"}) {
      const auto children = layer.value(QString::fromLatin1(child_key));
      if (is_list(children) &&
          !scan_layers_for_markers(
              children.toList(), found, node_count, error, false,
              layer_depth + 1, document_depth))
        return false;
    }
  }
  return true;
}

class InflateContext {
 public:
  InflateContext(const ParsedTable& parsed, qsizetype node_count)
      : parsed_(parsed), node_count_(node_count) {}

  bool process_root(QVariantMap* recipe, QString* error) {
    table_body_ = true;
    for (const auto* key : {"layers", "photoLayers"}) {
      const auto field = QString::fromLatin1(key);
      if (!recipe->contains(field)) continue;
      QVariantList layers;
      if (!validate_layer_list(recipe->value(field), &layers, error) ||
          !process_layers(&layers, 0, 0, error))
        return false;
      recipe->insert(field, std::move(layers));
    }
    if (used_refs_.size() != parsed_.rows_by_ref.size()) {
      return fail(error, QStringLiteral(
          "Component object table contains an unreferenced object."));
    }
    recipe->remove(QString::fromLatin1(kTableField));
    return true;
  }

 private:
  bool add_node(QString* error) {
    if (!charge(1, kMaximumExpandedNodeCount, &node_count_))
      return fail(error, QStringLiteral(
          "Expanded component object graph exceeds the node limit."));
    return true;
  }

  bool resolve_reference(const QVariantMap& layer, const QString& marker_field,
                         const QString& expected_type, QString* ref,
                         QString* error) {
    if (!marker_reference(layer.value(marker_field), expected_type, ref, error))
      return false;
    if (!parsed_.rows_by_ref.contains(*ref)) {
      return fail(error, QStringLiteral(
          "Component object reference points to a missing table entry."));
    }
    const auto& row = parsed_.rows_by_ref.value(*ref);
    if (row.type != expected_type) {
      return fail(error, QStringLiteral(
          "Component object reference points to an entry of the wrong type."));
    }
    if (!add_node(error)) return false;
    used_refs_.insert(*ref);
    return true;
  }

  bool inflate_document(const QString& ref, int document_depth,
                        QVariantMap* output, QString* error) {
    if (document_depth > kMaximumDocumentDepth) {
      return fail(error, QStringLiteral(
          "Nested Smart Object document recursion exceeds the depth limit."));
    }
    const auto& row = parsed_.rows_by_ref.value(ref);
    if (active_document_ids_.contains(row.content_id)) {
      return fail(error, QStringLiteral(
          "Smart Object document cycle detected through a repeated contentId."));
    }
    const auto cached = expanded_documents_.constFind(ref);
    if (cached != expanded_documents_.cend()) {
      if (!charge(expanded_document_node_counts_.value(ref),
                  kMaximumExpandedNodeCount, &node_count_))
        return fail(error, QStringLiteral(
            "Expanded component object graph exceeds the node limit."));
      *output = cached.value();
      return true;
    }
    auto document = row.value;
    QVariantList nested_layers;
    if (!validate_layer_list(document.value(QStringLiteral("layers")),
                             &nested_layers, error))
      return false;
    if (!add_node(error)) return false;
    const auto nodes_before_children = node_count_;
    active_document_ids_.insert(row.content_id);
    const auto layers_ok = process_layers(
        &nested_layers, document_depth, 0, error);
    active_document_ids_.remove(row.content_id);
    if (!layers_ok) return false;
    document.insert(QStringLiteral("layers"), std::move(nested_layers));
    expanded_document_node_counts_.insert(
        ref, node_count_ - nodes_before_children + 1);
    expanded_documents_.insert(ref, document);
    *output = std::move(document);
    return true;
  }

  bool process_layers(QVariantList* layers, int document_depth,
                      int layer_depth, QString* error) {
    for (auto& value : *layers) {
      if (!is_map(value) ||
          !process_layer(&value, document_depth, layer_depth, error))
        return false;
    }
    return true;
  }

  bool process_layer(QVariant* value, int document_depth,
                     int layer_depth, QString* error) {
    if (layer_depth > kMaximumLayerDepth) {
      return fail(error, QStringLiteral(
          "Expanded component object layer recursion exceeds the depth limit."));
    }
    if (!add_node(error)) return false;
    auto layer = value->toMap();
    const auto descriptor_marker =
        QString::fromLatin1(kDescriptorReferenceField);
    const auto document_marker =
        QString::fromLatin1(kDocumentReferenceField);
    const bool has_descriptor_ref = layer.contains(descriptor_marker);
    const bool has_document_ref = layer.contains(document_marker);
    if (layer.contains(QStringLiteral("smartObject")) &&
        (has_descriptor_ref || table_body_)) {
      return fail(error, QStringLiteral(
          "Stored component layer contains both inline content and a table reference."));
    }
    if (layer.contains(QStringLiteral("smartObjectDocument")) &&
        (has_document_ref || table_body_)) {
      return fail(error, QStringLiteral(
          "Stored component layer contains both inline content and a table reference."));
    }
    if (has_descriptor_ref) {
      QString ref;
      if (!resolve_reference(layer, descriptor_marker,
                             QString::fromLatin1(kDescriptorType), &ref, error))
        return false;
      layer.remove(descriptor_marker);
      layer.insert(QStringLiteral("smartObject"),
                   parsed_.rows_by_ref.value(ref).value);
    }
    if (has_document_ref) {
      QString ref;
      if (!resolve_reference(layer, document_marker,
                             QString::fromLatin1(kDocumentType), &ref, error))
        return false;
      QVariantMap document;
      if (!inflate_document(ref, document_depth + 1, &document, error))
        return false;
      layer.remove(document_marker);
      layer.insert(QStringLiteral("smartObjectDocument"), std::move(document));
    }
    for (const auto* child_key : {"children", "layers"}) {
      const auto key = QString::fromLatin1(child_key);
      const auto child_value = layer.value(key);
      if (!is_list(child_value)) continue;
      auto children = child_value.toList();
      for (auto& child : children) {
        if (is_map(child) &&
            !process_layer(&child, document_depth, layer_depth + 1, error))
          return false;
      }
      layer.insert(key, std::move(children));
    }
    *value = std::move(layer);
    return true;
  }

  const ParsedTable& parsed_;
  QSet<QString> used_refs_;
  QSet<QString> active_document_ids_;
  QMap<QString, QVariantMap> expanded_documents_;
  QMap<QString, qsizetype> expanded_document_node_counts_;
  qsizetype node_count_ = 0;
  bool table_body_ = false;
};

}  // namespace detail

[[nodiscard]] inline bool has_table(const QVariantMap& recipe) {
  return recipe.contains(QString::fromLatin1(kTableField));
}

// Detects storage references in the root layer aliases and nested component
// documents. This lets recipe-version validation reject v1 refs before it
// reaches the generic layer parser.
[[nodiscard]] inline bool has_references(const QVariantMap& recipe) {
  qsizetype nodes = 0;
  QString ignored_error;
  bool found = false;
  for (const auto* key : {"layers", "photoLayers"}) {
    const auto value = recipe.value(QString::fromLatin1(key));
    if (detail::is_list(value) &&
        !detail::scan_layers_for_markers(value.toList(), &found, &nodes,
                                         &ignored_error))
      return true;
  }
  return found;
}

[[nodiscard]] inline bool has_refs(const QVariantMap& recipe) {
  return has_references(recipe);
}

// Replace repeated inline Smart Object descriptors/documents with stable
// references and one deterministic object table. Input is the JSON-ready
// serialized recipe tree: byte arrays inside documents must already be tagged
// with encode_component_payloads(). On any failure, recipe is untouched.
[[nodiscard]] inline bool externalize_component_objects(
    QVariantMap* recipe, QString* error = nullptr) {
  detail::clear_error(error);
  if (recipe == nullptr)
    return detail::fail(error, QStringLiteral(
        "Component object table received no recipe document."));
  if (has_table(*recipe))
    return detail::fail(error, QStringLiteral(
        "Recipe already contains a component object table."));
  if (has_references(*recipe))
    return detail::fail(error, QStringLiteral(
        "Recipe already contains component object table references."));
  auto candidate = *recipe;
  detail::PackContext context;
  if (!context.process_root(&candidate, error)) return false;
  *recipe = std::move(candidate);
  return true;
}

// Restore inline JSON-ready descriptor/document maps and remove the table.
// Byte tags deliberately remain intact; parse_photo_layers decodes them once
// at its existing recipe ingress boundary. Missing or malformed tables and
// references fail atomically instead of falling back to partial inline data.
[[nodiscard]] inline bool inline_component_objects(
    QVariantMap* recipe, QString* error = nullptr) {
  detail::clear_error(error);
  if (recipe == nullptr)
    return detail::fail(error, QStringLiteral(
        "Component object table received no recipe document."));
  if (!has_table(*recipe)) {
    if (has_references(*recipe))
      return detail::fail(error, QStringLiteral(
          "Recipe contains component object references without a table."));
    return true;
  }

  detail::ParsedTable parsed;
  qsizetype node_count = 0;
  qsizetype payload_bytes = 0;
  if (!detail::parse_table(*recipe, &parsed, &node_count, &payload_bytes, error))
    return false;
  auto candidate = *recipe;
  detail::InflateContext context(parsed, node_count);
  if (!context.process_root(&candidate, error)) return false;
  *recipe = std::move(candidate);
  return true;
}

}  // namespace photo_editor::desktop::component_object_table
