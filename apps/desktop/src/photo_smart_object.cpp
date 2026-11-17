#include "photo_smart_object.h"

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSet>
#include <QUuid>

namespace photo_editor::desktop::smart_object {
namespace {

constexpr auto kSchema = "looksy.smart-object.source";
constexpr auto kVersion = 1;
constexpr qint64 kMaximumSourceBytes = 512LL * 1024LL * 1024LL;
constexpr auto kEmbedded = "embedded";
constexpr auto kLinked = "linked";
constexpr auto kSourceFile = "source-file";
constexpr auto kLayerPng = "layer-png";

void clear_error(QString* error) {
  if (error != nullptr) error->clear();
}

bool fail(QString* error, const QString& message) {
  if (error != nullptr) *error = message;
  return false;
}

QVariantMap fail_map(QString* error, const QString& message) {
  fail(error, message);
  return {};
}

QVariantList fail_list(const QVariantList& unchanged, QString* error,
                       const QString& message) {
  fail(error, message);
  return unchanged;
}

QString new_id() {
  return QUuid::createUuid().toString(QUuid::WithoutBraces).toLower();
}

QString policy_name(ContentPolicy policy) {
  return policy == ContentPolicy::Embedded ? QString::fromLatin1(kEmbedded)
                                           : QString::fromLatin1(kLinked);
}

bool is_lower_hex_sha256(const QString& value) {
  if (value.size() != 64) return false;
  for (const auto character : value) {
    const auto code = character.unicode();
    if (!((code >= QLatin1Char('0').unicode() && code <= QLatin1Char('9').unicode()) ||
          (code >= QLatin1Char('a').unicode() && code <= QLatin1Char('f').unicode()))) {
      return false;
    }
  }
  return true;
}

bool read_file_bounded(const QString& path, QByteArray* bytes, QString* error) {
  if (bytes == nullptr) return fail(error, QStringLiteral("No output buffer was provided."));
  const QFileInfo info(path);
  if (path.trimmed().isEmpty() || !info.exists() || !info.isFile() || !info.isReadable()) {
    return fail(error, QStringLiteral("Linked Smart Object source is missing or unreadable: %1")
                           .arg(path));
  }
  if (info.size() <= 0) {
    return fail(error, QStringLiteral("Smart Object source is empty: %1").arg(path));
  }
  if (info.size() > kMaximumSourceBytes) {
    return fail(error, QStringLiteral("Smart Object source exceeds the 512 MiB content limit: %1")
                           .arg(path));
  }

  QFile file(info.absoluteFilePath());
  if (!file.open(QIODevice::ReadOnly)) {
    return fail(error, QStringLiteral("Could not read Smart Object source: %1 (%2)")
                           .arg(path, file.errorString()));
  }
  const auto read = file.read(kMaximumSourceBytes + 1);
  if (file.error() != QFileDevice::NoError || read.isEmpty()) {
    return fail(error, QStringLiteral("Could not read Smart Object source: %1 (%2)")
                           .arg(path, file.errorString()));
  }
  if (read.size() > kMaximumSourceBytes || file.size() != read.size()) {
    return fail(error, QStringLiteral("Smart Object source changed while it was being read or exceeds the 512 MiB content limit: %1")
                           .arg(path));
  }
  *bytes = read;
  return true;
}

QByteArray decode_base64(const QString& encoded, QString* error) {
  const auto input = encoded.toLatin1();
  const auto decoded = QByteArray::fromBase64(input);
  // A canonical re-encode rejects whitespace, invalid characters, and partial
  // data that QByteArray's permissive decoder would otherwise silently ignore.
  if (decoded.isEmpty() || decoded.toBase64() != input) {
    fail(error, QStringLiteral("Embedded Smart Object bytes are not valid canonical base64."));
    return {};
  }
  return decoded;
}

bool has_png_signature(const QByteArray& bytes) {
  static const QByteArray signature = QByteArray::fromHex("89504e470d0a1a0a");
  return bytes.size() >= signature.size() && bytes.startsWith(signature);
}

bool nested_source_suffix_supported(const QString& file_name) {
  const auto suffix = QFileInfo(file_name).suffix().toLower();
  // Keep this list aligned with the source families admitted by the desktop
  // decoder. The component workspace performs a byte-backed decoder preflight
  // before publishing, so an installed-but-unavailable codec fails explicitly.
  static const QSet<QString> supported_suffixes{
      QStringLiteral("3fr"), QStringLiteral("arw"), QStringLiteral("avif"),
      QStringLiteral("bmp"), QStringLiteral("cr2"), QStringLiteral("cr3"),
      QStringLiteral("dng"), QStringLiteral("erf"), QStringLiteral("exr"),
      QStringLiteral("gif"), QStringLiteral("hdr"), QStringLiteral("heic"),
      QStringLiteral("iiq"), QStringLiteral("jpeg"), QStringLiteral("jpg"),
      QStringLiteral("kdc"), QStringLiteral("mef"), QStringLiteral("mos"),
      QStringLiteral("nef"), QStringLiteral("nrw"), QStringLiteral("orf"),
      QStringLiteral("pef"), QStringLiteral("png"), QStringLiteral("raf"),
      QStringLiteral("raw"), QStringLiteral("rw2"), QStringLiteral("sr2"),
      QStringLiteral("srf"), QStringLiteral("tif"), QStringLiteral("tiff"),
      QStringLiteral("webp"), QStringLiteral("x3f")};
  return supported_suffixes.contains(suffix);
}

bool structure_error(const QVariantMap& descriptor, QString* error,
                     QByteArray* embedded_bytes = nullptr) {
  const auto reject = [error](const QString& message) {
    fail(error, message);
    return true;
  };
  const QSet<QString> allowed_fields{
      QStringLiteral("schema"), QStringLiteral("version"), QStringLiteral("contentId"),
      QStringLiteral("policy"), QStringLiteral("payloadKind"),
      QStringLiteral("sourceSha256"), QStringLiteral("byteLength"),
      QStringLiteral("fileName"), QStringLiteral("intrinsicWidth"),
      QStringLiteral("intrinsicHeight"), QStringLiteral("sourcePath"),
      QStringLiteral("dataBase64")};
  for (const auto& key : descriptor.keys()) {
    if (!allowed_fields.contains(key)) {
      return reject(QStringLiteral("Smart Object source content has an unsupported field: %1")
                        .arg(key));
    }
  }
  if (descriptor.value(QStringLiteral("schema")).toString() != QString::fromLatin1(kSchema)) {
    return reject(QStringLiteral("Smart Object content has an unknown schema."));
  }
  if (descriptor.value(QStringLiteral("version")).toInt() != kVersion) {
    return reject(QStringLiteral("Smart Object content version is unsupported."));
  }
  const auto content_id = descriptor.value(QStringLiteral("contentId")).toString();
  const QUuid parsed_id(content_id);
  if (parsed_id.isNull() ||
      parsed_id.toString(QUuid::WithoutBraces) != content_id) {
    return reject(QStringLiteral("Smart Object content has an invalid contentId."));
  }
  const auto policy = descriptor.value(QStringLiteral("policy")).toString();
  if (policy != QString::fromLatin1(kEmbedded) && policy != QString::fromLatin1(kLinked)) {
    return reject(QStringLiteral("Smart Object content policy must be embedded or linked."));
  }
  const auto payload_kind = descriptor.value(QStringLiteral("payloadKind")).toString();
  if (payload_kind != QString::fromLatin1(kSourceFile) &&
      payload_kind != QString::fromLatin1(kLayerPng)) {
    return reject(QStringLiteral("Smart Object source payload kind is unsupported."));
  }
  const auto byte_length = descriptor.value(QStringLiteral("byteLength")).toLongLong();
  if (byte_length <= 0 || byte_length > kMaximumSourceBytes) {
    return reject(QStringLiteral("Smart Object source byteLength is invalid or exceeds 512 MiB."));
  }
  if (!is_lower_hex_sha256(descriptor.value(QStringLiteral("sourceSha256")).toString())) {
    return reject(QStringLiteral("Smart Object source SHA-256 is malformed."));
  }
  if (descriptor.value(QStringLiteral("fileName")).toString().trimmed().isEmpty()) {
    return reject(QStringLiteral("Smart Object source fileName is missing."));
  }
  for (const auto& dimension_key : {QStringLiteral("intrinsicWidth"), QStringLiteral("intrinsicHeight")}) {
    bool ok = false;
    const auto dimension = descriptor.value(dimension_key).toInt(&ok);
    if (!ok || dimension < 0) {
      return reject(QStringLiteral("Smart Object intrinsic dimensions are invalid."));
    }
  }
  if (policy == QString::fromLatin1(kEmbedded)) {
    if (!descriptor.value(QStringLiteral("sourcePath")).toString().isEmpty()) {
      return reject(QStringLiteral("Embedded Smart Object content must not depend on sourcePath."));
    }
    const auto encoded = descriptor.value(QStringLiteral("dataBase64")).toString();
    if (encoded.isEmpty()) {
      return reject(QStringLiteral("Embedded Smart Object content has no source bytes."));
    }
    QString decode_error;
    const auto bytes = decode_base64(encoded, &decode_error);
    if (bytes.isEmpty()) return reject(decode_error);
    if (bytes.size() != byte_length) {
      return reject(QStringLiteral("Embedded Smart Object byteLength does not match its payload."));
    }
    if (QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex()) !=
        descriptor.value(QStringLiteral("sourceSha256")).toString()) {
      return reject(QStringLiteral("Embedded Smart Object source hash does not match its payload."));
    }
    if (payload_kind == QString::fromLatin1(kLayerPng) && !has_png_signature(bytes)) {
      return reject(QStringLiteral("Embedded pixel-layer Smart Object content is not a PNG image."));
    }
    if (embedded_bytes != nullptr) *embedded_bytes = bytes;
  } else {
    if (payload_kind != QString::fromLatin1(kSourceFile)) {
      return reject(QStringLiteral("Linked Smart Object content must reference an original source file."));
    }
    if (descriptor.value(QStringLiteral("sourcePath")).toString().trimmed().isEmpty()) {
      return reject(QStringLiteral("Linked Smart Object content has no sourcePath."));
    }
    if (!descriptor.value(QStringLiteral("dataBase64")).toString().isEmpty()) {
      return reject(QStringLiteral("Linked Smart Object content must not contain embedded bytes."));
    }
  }
  return false;
}

bool linked_source_error(const QVariantMap& descriptor, QString* error) {
  QByteArray bytes;
  const auto path = descriptor.value(QStringLiteral("sourcePath")).toString();
  if (!read_file_bounded(path, &bytes, error)) return true;
  if (bytes.size() != descriptor.value(QStringLiteral("byteLength")).toLongLong()) {
    fail(error, QStringLiteral("Linked Smart Object source changed size since it was recorded: %1")
                    .arg(path));
    return true;
  }
  const auto actual_hash = QString::fromLatin1(
      QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
  if (actual_hash != descriptor.value(QStringLiteral("sourceSha256")).toString()) {
    fail(error, QStringLiteral("Linked Smart Object source changed since it was recorded: %1")
                    .arg(path));
    return true;
  }
  return false;
}

QVariantMap resolve_with_bytes(const QVariantMap& layer,
                               const QVariantMap& descriptor,
                               const QByteArray& bytes) {
  auto result = layer;
  result.insert(QStringLiteral("smartObject"), descriptor);
  result.insert(QStringLiteral("smartObjectIntrinsicWidth"),
                descriptor.value(QStringLiteral("intrinsicWidth")));
  result.insert(QStringLiteral("smartObjectIntrinsicHeight"),
                descriptor.value(QStringLiteral("intrinsicHeight")));
  if (result.value(QStringLiteral("pixelWidth")).toInt() <= 0) {
    result.insert(QStringLiteral("pixelWidth"), descriptor.value(QStringLiteral("intrinsicWidth")));
  }
  if (result.value(QStringLiteral("pixelHeight")).toInt() <= 0) {
    result.insert(QStringLiteral("pixelHeight"), descriptor.value(QStringLiteral("intrinsicHeight")));
  }

  const auto linked = descriptor.value(QStringLiteral("policy")).toString() ==
                      QString::fromLatin1(kLinked);
  const auto payload_kind = descriptor.value(QStringLiteral("payloadKind")).toString();
  if (linked) {
    result.insert(QStringLiteral("kind"), QStringLiteral("source"));
    result.insert(QStringLiteral("sourcePath"), descriptor.value(QStringLiteral("sourcePath")));
    result.remove(QStringLiteral("pixelData"));
    result.remove(QStringLiteral("pixelDataEncoding"));
  } else if (payload_kind == QString::fromLatin1(kSourceFile)) {
    result.insert(QStringLiteral("kind"), QStringLiteral("source"));
    result.insert(QStringLiteral("sourcePath"), QString());
    result.insert(QStringLiteral("pixelData"), bytes);
    result.insert(QStringLiteral("pixelDataEncoding"), QStringLiteral("source-file"));
  } else {
    result.insert(QStringLiteral("kind"), QStringLiteral("pixel"));
    result.insert(QStringLiteral("sourcePath"), QString());
    result.insert(QStringLiteral("pixelData"), bytes);
    result.insert(QStringLiteral("pixelDataEncoding"), QStringLiteral("png"));
  }
  return result;
}

bool supported_source_layer(const QVariantMap& layer, QString* error) {
  if (layer.contains(QStringLiteral("paintTiles")) ||
      layer.contains(QStringLiteral("paintDraftTiles")) ||
      layer.contains(QStringLiteral("paintDraftImage"))) {
    return fail(error, QStringLiteral("Paint tile layers need worker-side rasterization before they can become Smart Objects."));
  }
  const auto kind = layer.value(QStringLiteral("kind")).toString().trimmed().toLower();
  if (kind == QStringLiteral("source")) {
    if (layer.value(QStringLiteral("sourcePath")).toString().trimmed().isEmpty()) {
      return fail(error, QStringLiteral("Source layer has no sourcePath to convert."));
    }
    return true;
  }
  if (kind == QStringLiteral("pixel")) {
    if (!layer.value(QStringLiteral("shapeDefinition")).toMap().isEmpty()) {
      return fail(error, QStringLiteral("Editable vector shape layers are not supported by source-only Smart Objects yet."));
    }
    if (layer.value(QStringLiteral("pixelData")).toByteArray().isEmpty()) {
      return fail(error, QStringLiteral("Pixel layer has no immutable pixelData payload to convert."));
    }
    return true;
  }
  return fail(error, QStringLiteral("Smart Objects currently support source layers and PNG-backed pixel layers only."));
}

bool supported_instance_template(const QVariantMap& layer, QString* error) {
  const auto descriptor = layer.value(QStringLiteral("smartObject")).toMap();
  if (descriptor.isEmpty()) return supported_source_layer(layer, error);
  if (layer.contains(QStringLiteral("paintTiles")) ||
      layer.contains(QStringLiteral("paintDraftTiles")) ||
      layer.contains(QStringLiteral("paintDraftImage"))) {
    return fail(error, QStringLiteral("Paint tile instances need worker-side rasterization before they can be duplicated."));
  }
  const auto kind = layer.value(QStringLiteral("kind")).toString().trimmed().toLower();
  if (kind != QStringLiteral("source") && kind != QStringLiteral("pixel")) {
    return fail(error, QStringLiteral("Only source and pixel Smart Object instances can be duplicated in this slice."));
  }
  return validate_content_descriptor(descriptor, false, error);
}

bool payload_matches_layer(const QVariantMap& layer,
                           const QVariantMap& descriptor,
                           QString* error) {
  const auto kind = layer.value(QStringLiteral("kind")).toString().trimmed().toLower();
  const auto payload_kind = descriptor.value(QStringLiteral("payloadKind")).toString();
  if ((kind == QStringLiteral("source") && payload_kind == QString::fromLatin1(kSourceFile)) ||
      (kind == QStringLiteral("pixel") && payload_kind == QString::fromLatin1(kLayerPng))) {
    return true;
  }
  return fail(error, QStringLiteral("Smart Object source content must match the layer's existing source or PNG pixel payload kind."));
}

struct LayerRecord {
  QVariantMap layer;
  bool parent_locked = false;
};

bool collect_layers(const QVariantList& stack, bool parent_locked,
                    QHash<QString, LayerRecord>* records, QString* error,
                    int depth = 0) {
  if (records == nullptr) return fail(error, QStringLiteral("No layer index was provided."));
  if (depth > 64) {
    return fail(error, QStringLiteral("Layer nesting exceeds the Smart Object operation limit of 64 levels."));
  }
  for (const auto& value : stack) {
    if (!value.canConvert<QVariantMap>()) continue;
    const auto layer = value.toMap();
    const auto id = layer.value(QStringLiteral("id")).toString().trimmed();
    const auto locked = parent_locked || layer.value(QStringLiteral("locked")).toBool();
    if (!id.isEmpty()) {
      if (records->contains(id)) {
        return fail(error, QStringLiteral("Layer IDs must be unique before Smart Object changes can be applied."));
      }
      records->insert(id, LayerRecord{layer, parent_locked});
    }
    if (!collect_layers(layer.value(QStringLiteral("children")).toList(), locked, records, error, depth + 1) ||
        !collect_layers(layer.value(QStringLiteral("layers")).toList(), locked, records, error, depth + 1)) {
      return false;
    }
  }
  return true;
}

bool selected_records(const QVariantList& stack, const QStringList& selected_ids,
                      QHash<QString, LayerRecord>* records,
                      QSet<QString>* selected_set, QString* error) {
  if (selected_ids.isEmpty()) return fail(error, QStringLiteral("Select at least one layer."));
  if (!collect_layers(stack, false, records, error)) return false;
  for (const auto& raw_id : selected_ids) {
    const auto id = raw_id.trimmed();
    if (id.isEmpty() || selected_set->contains(id)) {
      return fail(error, QStringLiteral("Selected layer IDs must be nonempty and unique."));
    }
    const auto it = records->constFind(id);
    if (it == records->cend()) {
      return fail(error, QStringLiteral("A selected layer no longer exists in the layer stack."));
    }
    if (it->parent_locked || it->layer.value(QStringLiteral("locked")).toBool()) {
      return fail(error, QStringLiteral("A selected layer is locked."));
    }
    selected_set->insert(id);
  }
  return true;
}

QVariantList mutate_layers(const QVariantList& stack,
                           const QHash<QString, QVariantMap>& replacements) {
  QVariantList result;
  result.reserve(stack.size());
  for (const auto& value : stack) {
    if (!value.canConvert<QVariantMap>()) {
      result.push_back(value);
      continue;
    }
    auto layer = value.toMap();
    const auto id = layer.value(QStringLiteral("id")).toString().trimmed();
    const auto replacement = replacements.constFind(id);
    if (replacement != replacements.cend()) layer = replacement.value();
    if (layer.contains(QStringLiteral("children"))) {
      layer.insert(QStringLiteral("children"), mutate_layers(layer.value(QStringLiteral("children")).toList(), replacements));
    }
    if (layer.contains(QStringLiteral("layers"))) {
      layer.insert(QStringLiteral("layers"), mutate_layers(layer.value(QStringLiteral("layers")).toList(), replacements));
    }
    result.push_back(layer);
  }
  return result;
}

QVariantMap with_instance_content(const QVariantMap& layer,
                                  const QVariantMap& descriptor,
                                  const QString& instance_id,
                                  const QByteArray& source_bytes,
                                  QString* error) {
  auto candidate = layer;
  candidate.insert(QStringLiteral("smartObject"), descriptor);
  candidate.insert(QStringLiteral("smartObjectInstanceId"), instance_id);
  Q_UNUSED(error);
  return resolve_with_bytes(candidate, descriptor, source_bytes);
}

bool content_id_from_layer(const QVariantMap& layer, QString* content_id,
                           QString* error) {
  const auto descriptor = layer.value(QStringLiteral("smartObject")).toMap();
  if (descriptor.isEmpty()) {
    return fail(error, QStringLiteral("Selected layer is not a Smart Object instance."));
  }
  if (structure_error(descriptor, error)) return false;
  const auto id = descriptor.value(QStringLiteral("contentId")).toString();
  if (id.isEmpty()) return fail(error, QStringLiteral("Selected layer is not a Smart Object instance."));
  if (content_id != nullptr) *content_id = id;
  return true;
}

struct LinkedGroup {
  QVariantMap descriptor;
  QStringList layer_ids;
};

bool collect_linked_groups(const QVariantList& layer_stack,
                           const QStringList& selected_layer_ids,
                           QHash<QString, LayerRecord>* records,
                           QHash<QString, LinkedGroup>* groups,
                           QString* error) {
  QSet<QString> selected;
  if (!selected_records(layer_stack, selected_layer_ids, records, &selected, error)) return false;

  QSet<QString> content_ids;
  for (const auto& layer_id : selected) {
    const auto layer = records->value(layer_id).layer;
    QString content_id;
    if (!content_id_from_layer(layer, &content_id, error)) return false;
    const auto descriptor = layer.value(QStringLiteral("smartObject")).toMap();
    if (descriptor.value(QStringLiteral("policy")).toString() != QString::fromLatin1(kLinked) ||
        descriptor.value(QStringLiteral("payloadKind")).toString() != QString::fromLatin1(kSourceFile)) {
      return fail(error, QStringLiteral("The selected Smart Object is not linked to a source file."));
    }
    content_ids.insert(content_id);
  }

  for (auto it = records->cbegin(); it != records->cend(); ++it) {
    const auto layer = it->layer;
    const auto descriptor = layer.value(QStringLiteral("smartObject")).toMap();
    const auto content_id = descriptor.value(QStringLiteral("contentId")).toString();
    if (!content_ids.contains(content_id)) continue;

    QString validated_id;
    if (!content_id_from_layer(layer, &validated_id, error)) return false;
    if (descriptor.value(QStringLiteral("policy")).toString() != QString::fromLatin1(kLinked) ||
        descriptor.value(QStringLiteral("payloadKind")).toString() != QString::fromLatin1(kSourceFile)) {
      return fail(error, QStringLiteral("A shared Smart Object group contains a non-linked instance."));
    }
    if (it->parent_locked || layer.value(QStringLiteral("locked")).toBool()) {
      return fail(error, QStringLiteral("Changing linked contents would affect a locked shared instance or parent group."));
    }

    auto canonical = descriptor;
    canonical.remove(QStringLiteral("contentId"));
    auto group = groups->find(content_id);
    if (group == groups->end()) {
      groups->insert(content_id, LinkedGroup{descriptor, {it.key()}});
    } else {
      auto existing_canonical = group->descriptor;
      existing_canonical.remove(QStringLiteral("contentId"));
      if (canonical != existing_canonical) {
        return fail(error, QStringLiteral("Shared Smart Object instances have conflicting content descriptors."));
      }
      group->layer_ids.append(it.key());
    }
  }

  if (groups->isEmpty()) return fail(error, QStringLiteral("No linked Smart Object instances were found."));
  return true;
}

QVariantMap descriptor_with_source_bytes(const QVariantMap& original,
                                        const QString& source_path,
                                        const QByteArray& bytes,
                                        bool embed) {
  auto descriptor = original;
  descriptor.insert(QStringLiteral("policy"), embed ? QString::fromLatin1(kEmbedded)
                                                      : QString::fromLatin1(kLinked));
  descriptor.insert(QStringLiteral("sourceSha256"), QString::fromLatin1(
      QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex()));
  descriptor.insert(QStringLiteral("byteLength"), static_cast<qlonglong>(bytes.size()));
  descriptor.insert(QStringLiteral("fileName"), QFileInfo(source_path).fileName());
  if (embed) {
    descriptor.remove(QStringLiteral("sourcePath"));
    descriptor.insert(QStringLiteral("dataBase64"), QString::fromLatin1(bytes.toBase64()));
  } else {
    descriptor.insert(QStringLiteral("sourcePath"), QFileInfo(source_path).absoluteFilePath());
    descriptor.remove(QStringLiteral("dataBase64"));
  }
  return descriptor;
}

bool update_intrinsic_dimensions(QVariantMap* descriptor, int width, int height,
                                 QString* error) {
  if (descriptor == nullptr || width < 0 || height < 0 ||
      ((width == 0) != (height == 0))) {
    return fail(error, QStringLiteral("Smart Object source dimensions must both be positive or both be zero."));
  }
  if (width > 0) {
    descriptor->insert(QStringLiteral("intrinsicWidth"), width);
    descriptor->insert(QStringLiteral("intrinsicHeight"), height);
  }
  return true;
}

QVariantList apply_linked_group_descriptors(
    const QVariantList& layer_stack,
    const QHash<QString, LayerRecord>& records,
    const QHash<QString, LinkedGroup>& groups,
    const QHash<QString, QVariantMap>& replacement_descriptors,
    const QHash<QString, QByteArray>& replacement_bytes,
    QString* error) {
  QHash<QString, QVariantMap> replacements;
  for (auto group_it = groups.cbegin(); group_it != groups.cend(); ++group_it) {
    const auto descriptor_it = replacement_descriptors.constFind(group_it.key());
    const auto bytes_it = replacement_bytes.constFind(group_it.key());
    if (descriptor_it == replacement_descriptors.cend() || bytes_it == replacement_bytes.cend() ||
        !validate_content_descriptor(descriptor_it.value(), false, error)) {
      return layer_stack;
    }
    for (const auto& layer_id : group_it->layer_ids) {
      auto layer = records.value(layer_id).layer;
      const auto instance_id = layer.value(QStringLiteral("smartObjectInstanceId")).toString();
      const auto resolved = with_instance_content(layer, descriptor_it.value(), instance_id,
                                                  bytes_it.value(), error);
      if (resolved.isEmpty()) return layer_stack;
      replacements.insert(layer_id, resolved);
    }
  }
  if (replacements.isEmpty()) {
    fail(error, QStringLiteral("No linked Smart Object contents were updated."));
    return layer_stack;
  }
  return mutate_layers(layer_stack, replacements);
}

}  // namespace

bool supports_nested_source_file_format(const QString& file_name) {
  return nested_source_suffix_supported(file_name);
}

QVariantMap create_content_descriptor(const QVariantMap& source_layer,
                                      ContentPolicy policy,
                                      QString* error) {
  clear_error(error);
  if (policy != ContentPolicy::Embedded && policy != ContentPolicy::Linked) {
    return fail_map(error, QStringLiteral("Smart Object content policy must be embedded or linked."));
  }
  if (!supported_source_layer(source_layer, error)) return {};

  const auto kind = source_layer.value(QStringLiteral("kind")).toString().trimmed().toLower();
  const auto linked = policy == ContentPolicy::Linked;
  if (linked && kind != QStringLiteral("source")) {
    return fail_map(error, QStringLiteral("Linked Smart Objects require a source layer with a file path."));
  }
  QString path;
  QByteArray bytes;
  QString payload_kind;
  if (kind == QStringLiteral("source")) {
    path = source_layer.value(QStringLiteral("sourcePath")).toString().trimmed();
    if (!read_file_bounded(path, &bytes, error)) return {};
    payload_kind = QString::fromLatin1(kSourceFile);
  } else {
    bytes = source_layer.value(QStringLiteral("pixelData")).toByteArray();
    if (bytes.isEmpty()) return fail_map(error, QStringLiteral("Pixel layer content is empty."));
    if (!has_png_signature(bytes)) {
      return fail_map(error, QStringLiteral("Pixel layers can become Smart Objects only when their pixelData is a PNG."));
    }
    if (bytes.size() > kMaximumSourceBytes) {
      return fail_map(error, QStringLiteral("Pixel-layer content exceeds the 512 MiB Smart Object limit."));
    }
    payload_kind = QString::fromLatin1(kLayerPng);
  }

  if (linked && path.isEmpty()) {
    return fail_map(error, QStringLiteral("Linked Smart Objects require a source file path."));
  }
  auto intrinsic_width = qMax(0, source_layer.value(QStringLiteral("pixelWidth")).toInt());
  auto intrinsic_height = qMax(0, source_layer.value(QStringLiteral("pixelHeight")).toInt());
  if (intrinsic_width == 0 || intrinsic_height == 0) {
    intrinsic_width = 0;
    intrinsic_height = 0;
  }
  const QFileInfo source_info(path);
  QVariantMap descriptor{
      {QStringLiteral("schema"), QString::fromLatin1(kSchema)},
      {QStringLiteral("version"), kVersion},
      {QStringLiteral("contentId"), new_id()},
      {QStringLiteral("policy"), policy_name(policy)},
      {QStringLiteral("payloadKind"), payload_kind},
      {QStringLiteral("sourceSha256"), QString::fromLatin1(
          QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex())},
      {QStringLiteral("byteLength"), static_cast<qlonglong>(bytes.size())},
      {QStringLiteral("fileName"), path.isEmpty() ? QStringLiteral("layer.png") : source_info.fileName()},
      {QStringLiteral("intrinsicWidth"), intrinsic_width},
      {QStringLiteral("intrinsicHeight"), intrinsic_height}};

  if (linked) {
    descriptor.insert(QStringLiteral("sourcePath"), source_info.absoluteFilePath());
  } else {
    descriptor.insert(QStringLiteral("dataBase64"), QString::fromLatin1(bytes.toBase64()));
  }
  return descriptor;
}

bool validate_content_descriptor(const QVariantMap& descriptor,
                                bool require_linked_source_available,
                                QString* error) {
  clear_error(error);
  QString validation_error;
  if (structure_error(descriptor, &validation_error)) return fail(error, validation_error);
  if (require_linked_source_available &&
      descriptor.value(QStringLiteral("policy")).toString() == QString::fromLatin1(kLinked) &&
      linked_source_error(descriptor, &validation_error)) {
    return fail(error, validation_error);
  }
  return true;
}

QVariantMap inspect_link_status(const QVariantMap& descriptor, QString* error) {
  clear_error(error);
  QString validation_error;
  if (structure_error(descriptor, &validation_error)) return fail_map(error, validation_error);

  const auto policy = descriptor.value(QStringLiteral("policy")).toString();
  const auto path = descriptor.value(QStringLiteral("sourcePath")).toString();
  QVariantMap result{
      {QStringLiteral("policy"), policy},
      {QStringLiteral("fileName"), descriptor.value(QStringLiteral("fileName"))},
      {QStringLiteral("sourcePath"), path},
      {QStringLiteral("expectedByteLength"), descriptor.value(QStringLiteral("byteLength"))},
      {QStringLiteral("expectedSha256"), descriptor.value(QStringLiteral("sourceSha256"))}};
  if (policy == QString::fromLatin1(kEmbedded)) {
    result.insert(QStringLiteral("status"), QStringLiteral("embedded"));
    return result;
  }

  const QFileInfo info(path);
  if (!info.exists()) {
    result.insert(QStringLiteral("status"), QStringLiteral("missing"));
    return result;
  }
  if (!info.isFile() || !info.isReadable()) {
    result.insert(QStringLiteral("status"), QStringLiteral("unreadable"));
    return result;
  }
  QByteArray bytes;
  QString read_error;
  if (!read_file_bounded(path, &bytes, &read_error)) {
    result.insert(QStringLiteral("status"), QStringLiteral("unreadable"));
    result.insert(QStringLiteral("detail"), read_error);
    return result;
  }
  const auto actual_hash = QString::fromLatin1(
      QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
  result.insert(QStringLiteral("actualByteLength"), static_cast<qlonglong>(bytes.size()));
  result.insert(QStringLiteral("actualSha256"), actual_hash);
  result.insert(QStringLiteral("status"),
      bytes.size() == descriptor.value(QStringLiteral("byteLength")).toLongLong() &&
              actual_hash == descriptor.value(QStringLiteral("sourceSha256")).toString()
          ? QStringLiteral("current")
          : QStringLiteral("changed"));
  return result;
}

QByteArray read_content_bytes(const QVariantMap& descriptor, QString* error) {
  clear_error(error);
  QByteArray embedded_bytes;
  QString validation_error;
  if (structure_error(descriptor, &validation_error, &embedded_bytes)) {
    fail(error, validation_error);
    return {};
  }
  if (descriptor.value(QStringLiteral("policy")).toString() == QString::fromLatin1(kEmbedded)) {
    return embedded_bytes;
  }
  QByteArray bytes;
  if (!read_file_bounded(descriptor.value(QStringLiteral("sourcePath")).toString(), &bytes, error)) return {};
  const auto expected_length = descriptor.value(QStringLiteral("byteLength")).toLongLong();
  const auto expected_hash = descriptor.value(QStringLiteral("sourceSha256")).toString();
  const auto actual_hash = QString::fromLatin1(
      QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
  if (bytes.size() != expected_length || actual_hash != expected_hash) {
    fail(error, QStringLiteral("Linked Smart Object source changed since it was recorded: %1")
                    .arg(descriptor.value(QStringLiteral("sourcePath")).toString()));
    return {};
  }
  return bytes;
}

QByteArray serialize_content_descriptor(const QVariantMap& descriptor,
                                        QString* error) {
  clear_error(error);
  if (!validate_content_descriptor(descriptor, false, error)) return {};
  return QJsonDocument(QJsonObject::fromVariantMap(descriptor)).toJson(QJsonDocument::Compact);
}

QVariantMap deserialize_content_descriptor(const QByteArray& bytes,
                                           QString* error) {
  clear_error(error);
  if (bytes.isEmpty() || bytes.size() > kMaximumSourceBytes * 2) {
    return fail_map(error, QStringLiteral("Serialized Smart Object content is empty or too large."));
  }
  QJsonParseError parse_error;
  const auto document = QJsonDocument::fromJson(bytes, &parse_error);
  if (parse_error.error != QJsonParseError::NoError || !document.isObject()) {
    return fail_map(error, QStringLiteral("Serialized Smart Object content is not a valid JSON object."));
  }
  const auto descriptor = document.object().toVariantMap();
  if (!validate_content_descriptor(descriptor, false, error)) return {};
  return descriptor;
}

QVariantMap create_instance(const QVariantMap& template_layer,
                            const QVariantMap& content_descriptor,
                            const QString& new_layer_id,
                            QString* error) {
  clear_error(error);
  QString reason;
  if (!supported_instance_template(template_layer, &reason)) return fail_map(error, reason);
  if (new_layer_id.trimmed().isEmpty() ||
      new_layer_id.trimmed() == template_layer.value(QStringLiteral("id")).toString().trimmed()) {
    return fail_map(error, QStringLiteral("A new, distinct layer ID is required for a Smart Object instance."));
  }
  if (!payload_matches_layer(template_layer, content_descriptor, &reason)) {
    return fail_map(error, reason);
  }
  const auto source_bytes = read_content_bytes(content_descriptor, &reason);
  if (source_bytes.isEmpty()) return fail_map(error, reason);
  auto candidate = template_layer;
  candidate.insert(QStringLiteral("id"), new_layer_id.trimmed());
  const auto resolved = with_instance_content(candidate, content_descriptor, new_id(), source_bytes, &reason);
  if (resolved.isEmpty()) return fail_map(error, reason);
  return resolved;
}

QVariantList create_instances(const QVariantList& layer_stack,
                              const QStringList& selected_layer_ids,
                              const QVariantMap& content_descriptor,
                              QString* error) {
  clear_error(error);
  QHash<QString, LayerRecord> records;
  QSet<QString> selected;
  QString reason;
  if (!selected_records(layer_stack, selected_layer_ids, &records, &selected, &reason)) {
    return fail_list(layer_stack, error, reason);
  }
  const auto source_bytes = read_content_bytes(content_descriptor, &reason);
  if (source_bytes.isEmpty()) return fail_list(layer_stack, error, reason);

  QHash<QString, QVariantMap> replacements;
  for (const auto& id : selected) {
    const auto record = records.value(id);
    if (record.layer.contains(QStringLiteral("smartObject"))) {
      return fail_list(layer_stack, error,
                       QStringLiteral("Selected layers already belong to Smart Objects."));
    }
    if (!supported_source_layer(record.layer, &reason)) {
      return fail_list(layer_stack, error, reason);
    }
    if (!payload_matches_layer(record.layer, content_descriptor, &reason)) {
      return fail_list(layer_stack, error, reason);
    }
    const auto resolved = with_instance_content(record.layer, content_descriptor, new_id(), source_bytes, &reason);
    replacements.insert(id, resolved);
  }
  return mutate_layers(layer_stack, replacements);
}

QVariantList replace_contents(const QVariantList& layer_stack,
                              const QStringList& selected_layer_ids,
                              const QVariantMap& replacement_descriptor,
                              QString* error) {
  clear_error(error);
  QHash<QString, LayerRecord> records;
  QSet<QString> selected;
  QString reason;
  if (!selected_records(layer_stack, selected_layer_ids, &records, &selected, &reason)) {
    return fail_list(layer_stack, error, reason);
  }
  const auto source_bytes = read_content_bytes(replacement_descriptor, &reason);
  if (source_bytes.isEmpty()) return fail_list(layer_stack, error, reason);

  QSet<QString> target_content_ids;
  for (const auto& layer_id : selected) {
    QString content_id;
    const auto selected_layer = records.value(layer_id).layer;
    if (!content_id_from_layer(selected_layer, &content_id, &reason)) {
      return fail_list(layer_stack, error, reason);
    }
    const auto current_descriptor = selected_layer.value(QStringLiteral("smartObject")).toMap();
    if (current_descriptor.value(QStringLiteral("payloadKind")) !=
        replacement_descriptor.value(QStringLiteral("payloadKind"))) {
      return fail_list(layer_stack, error,
                       QStringLiteral("Replace Contents must keep the shared layer source payload kind."));
    }
    target_content_ids.insert(content_id);
  }

  QHash<QString, QString> layer_content_ids;
  QHash<QString, QVariantMap> canonical_descriptors_by_id;
  for (auto it = records.cbegin(); it != records.cend(); ++it) {
    const auto descriptor = it->layer.value(QStringLiteral("smartObject")).toMap();
    if (descriptor.isEmpty()) continue;
    const auto candidate_id = descriptor.value(QStringLiteral("contentId")).toString();
    if (!target_content_ids.contains(candidate_id)) continue;
    QString content_id;
    if (!content_id_from_layer(it->layer, &content_id, &reason)) {
      return fail_list(layer_stack, error, reason);
    }
    auto descriptor_without_identity = descriptor;
    descriptor_without_identity.remove(QStringLiteral("contentId"));
    const auto canonical_descriptor = canonical_descriptors_by_id.constFind(content_id);
    if (canonical_descriptor != canonical_descriptors_by_id.cend() &&
        canonical_descriptor.value() != descriptor_without_identity) {
      return fail_list(layer_stack, error,
                       QStringLiteral("Shared Smart Object instances have conflicting content descriptors."));
    }
    canonical_descriptors_by_id.insert(content_id, descriptor_without_identity);
    if (it->parent_locked || it->layer.value(QStringLiteral("locked")).toBool()) {
      return fail_list(layer_stack, error,
                       QStringLiteral("Replace Contents would change a locked shared instance."));
    }
    layer_content_ids.insert(it.key(), content_id);
  }

  QHash<QString, QVariantMap> replacements;
  for (auto it = layer_content_ids.cbegin(); it != layer_content_ids.cend(); ++it) {
    auto descriptor = replacement_descriptor;
    descriptor.insert(QStringLiteral("contentId"), it.value());
    auto layer = records.value(it.key()).layer;
    const auto instance_id = layer.value(QStringLiteral("smartObjectInstanceId")).toString();
    const auto resolved = with_instance_content(layer, descriptor,
        instance_id.isEmpty() ? new_id() : instance_id, source_bytes, &reason);
    replacements.insert(it.key(), resolved);
  }
  if (replacements.isEmpty()) {
    return fail_list(layer_stack, error, QStringLiteral("No matching Smart Object instances were found."));
  }
  return mutate_layers(layer_stack, replacements);
}

QVariantList relink_contents(const QVariantList& layer_stack,
                             const QStringList& selected_layer_ids,
                             const QString& new_source_path,
                             QString* error) {
  return relink_contents(layer_stack, selected_layer_ids, new_source_path, 0, 0, error);
}

QVariantList relink_contents(const QVariantList& layer_stack,
                             const QStringList& selected_layer_ids,
                             const QString& new_source_path,
                             int intrinsic_width,
                             int intrinsic_height,
                             QString* error) {
  clear_error(error);
  QHash<QString, LayerRecord> records;
  QHash<QString, LinkedGroup> groups;
  if (!collect_linked_groups(layer_stack, selected_layer_ids, &records, &groups, error)) {
    return layer_stack;
  }
  QByteArray source_bytes;
  if (!read_file_bounded(new_source_path, &source_bytes, error)) return layer_stack;

  QHash<QString, QVariantMap> descriptors;
  QHash<QString, QByteArray> bytes_by_group;
  for (auto it = groups.cbegin(); it != groups.cend(); ++it) {
    auto descriptor = descriptor_with_source_bytes(it->descriptor, new_source_path, source_bytes, false);
    descriptor.insert(QStringLiteral("contentId"), it.key());
    if (!update_intrinsic_dimensions(&descriptor, intrinsic_width, intrinsic_height, error)) {
      return layer_stack;
    }
    if (!validate_content_descriptor(descriptor, false, error)) return layer_stack;
    descriptors.insert(it.key(), descriptor);
    bytes_by_group.insert(it.key(), source_bytes);
  }
  return apply_linked_group_descriptors(layer_stack, records, groups, descriptors,
                                        bytes_by_group, error);
}

QVariantList refresh_linked_contents(const QVariantList& layer_stack,
                                     const QStringList& selected_layer_ids,
                                     QString* error) {
  return refresh_linked_contents(layer_stack, selected_layer_ids, 0, 0, error);
}

QVariantList refresh_linked_contents(const QVariantList& layer_stack,
                                     const QStringList& selected_layer_ids,
                                     int intrinsic_width,
                                     int intrinsic_height,
                                     QString* error) {
  clear_error(error);
  QHash<QString, LayerRecord> records;
  QHash<QString, LinkedGroup> groups;
  if (!collect_linked_groups(layer_stack, selected_layer_ids, &records, &groups, error)) {
    return layer_stack;
  }

  QHash<QString, QVariantMap> descriptors;
  QHash<QString, QByteArray> bytes_by_group;
  for (auto it = groups.cbegin(); it != groups.cend(); ++it) {
    const auto source_path = it->descriptor.value(QStringLiteral("sourcePath")).toString();
    QByteArray source_bytes;
    if (!read_file_bounded(source_path, &source_bytes, error)) return layer_stack;
    auto descriptor = descriptor_with_source_bytes(it->descriptor, source_path, source_bytes, false);
    descriptor.insert(QStringLiteral("contentId"), it.key());
    if (!update_intrinsic_dimensions(&descriptor, intrinsic_width, intrinsic_height, error)) {
      return layer_stack;
    }
    if (!validate_content_descriptor(descriptor, false, error)) return layer_stack;
    descriptors.insert(it.key(), descriptor);
    bytes_by_group.insert(it.key(), source_bytes);
  }
  return apply_linked_group_descriptors(layer_stack, records, groups, descriptors,
                                        bytes_by_group, error);
}

QVariantList embed_linked_contents(const QVariantList& layer_stack,
                                   const QStringList& selected_layer_ids,
                                   QString* error) {
  clear_error(error);
  QHash<QString, LayerRecord> records;
  QHash<QString, LinkedGroup> groups;
  if (!collect_linked_groups(layer_stack, selected_layer_ids, &records, &groups, error)) {
    return layer_stack;
  }

  QHash<QString, QVariantMap> descriptors;
  QHash<QString, QByteArray> bytes_by_group;
  for (auto it = groups.cbegin(); it != groups.cend(); ++it) {
    const auto source_path = it->descriptor.value(QStringLiteral("sourcePath")).toString();
    QByteArray source_bytes;
    if (!read_file_bounded(source_path, &source_bytes, error)) return layer_stack;
    auto descriptor = descriptor_with_source_bytes(it->descriptor, source_path, source_bytes, true);
    descriptor.insert(QStringLiteral("contentId"), it.key());
    if (!validate_content_descriptor(descriptor, false, error)) return layer_stack;
    descriptors.insert(it.key(), descriptor);
    bytes_by_group.insert(it.key(), source_bytes);
  }
  return apply_linked_group_descriptors(layer_stack, records, groups, descriptors,
                                        bytes_by_group, error);
}

QVariantList make_independent(const QVariantList& layer_stack,
                              const QStringList& selected_layer_ids,
                              QString* error) {
  clear_error(error);
  QHash<QString, LayerRecord> records;
  QSet<QString> selected;
  QString reason;
  if (!selected_records(layer_stack, selected_layer_ids, &records, &selected, &reason)) {
    return fail_list(layer_stack, error, reason);
  }

  QHash<QString, QVariantMap> replacements;
  for (const auto& layer_id : selected) {
    auto layer = records.value(layer_id).layer;
    auto descriptor = layer.value(QStringLiteral("smartObject")).toMap();
    if (!validate_content_descriptor(descriptor, false, &reason)) {
      return fail_list(layer_stack, error,
                       reason.isEmpty() ? QStringLiteral("Selected layer is not a Smart Object instance.") : reason);
    }
    descriptor.insert(QStringLiteral("contentId"), new_id());
    layer.insert(QStringLiteral("smartObject"), descriptor);
    if (layer.value(QStringLiteral("smartObjectInstanceId")).toString().trimmed().isEmpty()) {
      layer.insert(QStringLiteral("smartObjectInstanceId"), new_id());
    }
    replacements.insert(layer_id, layer);
  }
  return mutate_layers(layer_stack, replacements);
}

QVariantMap resolve_instance_source(const QVariantMap& layer, QString* error) {
  clear_error(error);
  const auto descriptor = layer.value(QStringLiteral("smartObject")).toMap();
  const auto bytes = read_content_bytes(descriptor, error);
  if (bytes.isEmpty()) return {};
  return resolve_with_bytes(layer, descriptor, bytes);
}

}  // namespace photo_editor::desktop::smart_object
