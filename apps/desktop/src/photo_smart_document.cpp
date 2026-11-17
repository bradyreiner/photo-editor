#include "photo_smart_document.h"

#include "photo_smart_object.h"
#include "photo_paint_surface.h"
#include "photo_layer_style_light.h"
#include "photo_text_dimension.h"
#include "photo_editor/pipeline/layer_styles.h"

#include <QBuffer>
#include <QFileInfo>
#include <QHash>
#include <QImageReader>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSet>
#include <QUuid>

#include <algorithm>
#include <cmath>
#include <limits>

namespace photo_editor::desktop::smart_object::nested_document {
namespace {

constexpr auto kDocumentField = "smartObjectDocument";
constexpr auto kInstanceIdField = "smartObjectInstanceId";
constexpr auto kEmbeddedPolicy = "embedded";

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

bool valid_id(const QString& value) {
  const QUuid parsed(value);
  return !parsed.isNull() &&
         parsed.toString(QUuid::WithoutBraces).toLower() == value;
}

bool variant_integer(const QVariant& value, int* result) {
  if (result == nullptr) return false;
  const auto type = value.metaType().id();
  if (type != QMetaType::Int && type != QMetaType::UInt &&
      type != QMetaType::LongLong && type != QMetaType::ULongLong &&
      type != QMetaType::Double) {
    return false;
  }
  bool ok = false;
  const auto number = value.toDouble(&ok);
  if (!ok || !std::isfinite(number) || std::floor(number) != number ||
      number < static_cast<double>(std::numeric_limits<int>::min()) ||
      number > static_cast<double>(std::numeric_limits<int>::max())) {
    return false;
  }
  *result = static_cast<int>(number);
  return true;
}

bool bounded_canvas(int width, int height, QString* error) {
  if (width <= 0 || height <= 0 || width > kMaximumCanvasDimension ||
      height > kMaximumCanvasDimension ||
      static_cast<qint64>(width) * static_cast<qint64>(height) >
          kMaximumCanvasPixels) {
    return fail(error, QStringLiteral(
        "Smart Object canvas dimensions are invalid or exceed the 64 MP limit."));
  }
  return true;
}

bool valid_component_post_composite_retouch_mask(const QVariantMap& mask) {
  int marker_version = 0;
  int width = 0;
  int height = 0;
  const auto mode = mask.value(QStringLiteral("retouchMode")).toString().trimmed().toLower();
  const auto repair_id = mask.value(QStringLiteral("repairId")).toString().trimmed();
  return variant_integer(
             mask.value(QStringLiteral("componentPostCompositeRetouchVersion")),
             &marker_version) &&
      marker_version == 1 &&
      mask.value(QStringLiteral("toolRole")).toString().trimmed().toLower() ==
          QStringLiteral("retouch") &&
      mask.value(QStringLiteral("kind")).toString().trimmed().toLower() ==
          QStringLiteral("brush") &&
      (mode == QStringLiteral("remove") || mode == QStringLiteral("heal") ||
       mode == QStringLiteral("patch") || mode == QStringLiteral("clone")) &&
      !repair_id.isEmpty() && repair_id.size() <= 80 &&
      variant_integer(mask.value(QStringLiteral("componentRetouchCanvasWidth")), &width) &&
      variant_integer(mask.value(QStringLiteral("componentRetouchCanvasHeight")), &height) &&
      width > 0 && height > 0 && width <= kMaximumCanvasDimension &&
      height <= kMaximumCanvasDimension &&
      static_cast<qint64>(width) * static_cast<qint64>(height) <=
          kMaximumCanvasPixels;
}

bool has_component_post_composite_retouch_marker(const QVariantMap& mask) {
  return mask.contains(QStringLiteral("componentPostCompositeRetouchVersion")) ||
      mask.contains(QStringLiteral("componentRetouchCanvasWidth")) ||
      mask.contains(QStringLiteral("componentRetouchCanvasHeight"));
}

bool estimate_json_bytes(const QVariant& value, qsizetype* total,
                         int depth = 0) {
  if (total == nullptr || depth > kMaximumLayerDepth + kMaximumDocumentDepth + 4) {
    return false;
  }
  const auto add = [total](qsizetype amount) {
    if (amount < 0 || *total > kMaximumSerializedBytes - amount) return false;
    *total += amount;
    return true;
  };
  const auto json_string_size = [](const QString& string) {
    const auto utf8 = string.toUtf8();
    qsizetype escaped_size = 2;
    for (const auto byte : utf8) {
      const auto code = static_cast<unsigned char>(byte);
      if (code == '"' || code == '\\' || code == '\b' || code == '\f' ||
          code == '\n' || code == '\r' || code == '\t') {
        escaped_size += 2;
      } else if (code < 0x20U) {
        escaped_size += 6;
      } else {
        ++escaped_size;
      }
    }
    return escaped_size;
  };
  if (value.metaType().id() == QMetaType::QVariantMap) {
    const auto map = value.toMap();
    if (!add(2)) return false;
    bool first = true;
    for (auto it = map.cbegin(); it != map.cend(); ++it) {
      if ((!first && !add(1)) || !add(json_string_size(it.key()) + 1) ||
          !estimate_json_bytes(it.value(), total, depth + 1)) {
        return false;
      }
      first = false;
    }
    return true;
  }
  if (value.metaType().id() == QMetaType::QVariantList ||
      value.metaType().id() == QMetaType::QStringList) {
    const auto list = value.toList();
    if (!add(2)) return false;
    bool first = true;
    for (const auto& item : list) {
      if (!first && !add(1)) return false;
      if (!estimate_json_bytes(item, total, depth + 1)) return false;
      first = false;
    }
    return true;
  }
  if (value.metaType().id() == QMetaType::QByteArray) {
    const auto bytes = static_cast<qsizetype>(value.toByteArray().size());
    const auto encoded = ((bytes + 2) / 3) * 4;
    return add(encoded + 2);
  }
  if (value.metaType().id() == QMetaType::QString) {
    return add(json_string_size(value.toString()));
  }
  if (value.metaType().id() == QMetaType::Bool) return add(8);
  if (!value.isValid() || value.isNull()) return add(8);
  return add(32);
}

bool is_byte_payload_key(const QString& key) {
  return key == QStringLiteral("pixels") ||
         key.endsWith(QStringLiteral("Data"), Qt::CaseInsensitive) ||
         key.endsWith(QStringLiteral("Pixels"), Qt::CaseInsensitive);
}

QVariant normalize_serialized_payloads(const QVariant& value,
                                       bool encode,
                                       QString* error,
                                       const QString& key = {}) {
  if (value.metaType().id() == QMetaType::QVariantMap) {
    auto map = value.toMap();
    for (auto it = map.begin(); it != map.end(); ++it) {
      it.value() = normalize_serialized_payloads(it.value(), encode, error, it.key());
      if (error != nullptr && !error->isEmpty()) return {};
    }
    return map;
  }
  if (value.metaType().id() == QMetaType::QVariantList) {
    auto list = value.toList();
    for (auto& item : list) {
      item = normalize_serialized_payloads(item, encode, error, key);
      if (error != nullptr && !error->isEmpty()) return {};
    }
    return list;
  }
  if (!is_byte_payload_key(key)) return value;
  if (encode && value.metaType().id() == QMetaType::QByteArray) {
    return QString::fromLatin1(value.toByteArray().toBase64());
  }
  if (!encode && value.metaType().id() == QMetaType::QString) {
    const auto encoded = value.toString().toLatin1();
    const auto decoded = QByteArray::fromBase64(encoded);
    if (!decoded.isEmpty() && decoded.toBase64() != encoded) {
      fail(error, QStringLiteral("Smart Object document contains a noncanonical byte payload."));
      return {};
    }
    if (decoded.isEmpty() && !encoded.isEmpty()) {
      fail(error, QStringLiteral("Smart Object document contains an invalid base64 byte payload."));
      return {};
    }
    return decoded;
  }
  return value;
}

bool list_has_maps_only(const QVariantList& list) {
  for (const auto& value : list) {
    if (value.metaType().id() != QMetaType::QVariantMap) return false;
  }
  return true;
}

struct ValidationContext {
  int layer_count = 0;
  int mask_count = 0;
  qint64 decoded_paint_pixels = 0;
  QSet<QString> document_path;
};

bool validate_document_recursive(const QVariantMap& document,
                                ValidationContext* context,
                                int document_depth,
                                QString* error);

bool is_supported_embedded_source(const QVariantMap& layer,
                                  QString* error) {
  const auto descriptor = layer.value(QStringLiteral("smartObject")).toMap();
  if (descriptor.isEmpty()) {
    return fail(error, QStringLiteral(
        "Nested image layers must be embedded before creating a Smart Object component."));
  }
  QString descriptor_error;
  if (!validate_content_descriptor(descriptor, false, &descriptor_error)) {
    return fail(error, QStringLiteral("Embedded image child is invalid: %1")
                           .arg(descriptor_error));
  }
  if (descriptor.value(QStringLiteral("policy")).toString() !=
      QString::fromLatin1(kEmbeddedPolicy)) {
    return fail(error, QStringLiteral(
        "Nested image layers must use embedded content; linked source references are not portable."));
  }
  if (descriptor.value(QStringLiteral("payloadKind")).toString() !=
      QStringLiteral("source-file")) {
    return fail(error, QStringLiteral(
        "Nested image layers must preserve their original source-file bytes."));
  }
  const auto name = descriptor.value(QStringLiteral("fileName")).toString();
  if (!smart_object::supports_nested_source_file_format(name)) {
    return fail(error, QStringLiteral(
        "Nested Smart Object image children do not support this source format. Use a supported raster, RAW, or HDR source."));
  }
  return true;
}

bool validate_committed_paint_tiles(const QVariantMap& layer,
                                    const QString& kind,
                                    ValidationContext* context,
                                    QString* error) {
  if (layer.contains(QStringLiteral("paintDraftTiles")) ||
      layer.contains(QStringLiteral("paintDraftImage"))) {
    return fail(error, QStringLiteral(
        "Uncommitted paint drafts cannot be embedded in a Smart Object document."));
  }
  if (layer.value(QStringLiteral("baseImageEdit")).toBool() ||
      layer.value(QStringLiteral("copyBaseImage")).toBool() ||
      layer.value(QStringLiteral("copyBaseImageThroughAdjustments")).toBool()) {
    return fail(error, QStringLiteral(
        "Base-image paint dependencies cannot be moved into an isolated Smart Object component."));
  }
  if (!layer.contains(QStringLiteral("paintTiles"))) return true;
  if (kind != QStringLiteral("pixel") ||
      layer.value(QStringLiteral("paintTiles")).metaType().id() != QMetaType::QVariantMap) {
    return fail(error, QStringLiteral(
        "Committed paintTiles are supported only as a pixel-layer map."));
  }
  if (context == nullptr) {
    return fail(error, QStringLiteral("No Smart Object validation context was provided."));
  }

  int width = 0;
  int height = 0;
  if (!variant_integer(layer.value(QStringLiteral("pixelWidth")), &width) ||
      !variant_integer(layer.value(QStringLiteral("pixelHeight")), &height)) {
    return fail(error, QStringLiteral(
        "Committed paint tile canvas dimensions must be integer pixelWidth/pixelHeight values."));
  }
  if (!bounded_canvas(width, height, error)) return false;

  const auto payload = layer.value(QStringLiteral("paintTiles")).toMap();
  int version = 0;
  int tile_size = 0;
  const auto format_value = payload.value(QStringLiteral("format"));
  const auto icc_value = payload.value(QStringLiteral("icc"));
  const auto tiles_value = payload.value(QStringLiteral("tiles"));
  if (!variant_integer(payload.value(QStringLiteral("version")), &version) ||
      version != 1 ||
      format_value.metaType().id() != QMetaType::QString ||
      format_value.toString() != QString::fromLatin1(kPhotoPaintFormat) ||
      !variant_integer(payload.value(QStringLiteral("tileSize")), &tile_size) ||
      tile_size != kPhotoPaintTileEdge ||
      icc_value.metaType().id() != QMetaType::QString ||
      tiles_value.metaType().id() != QMetaType::QVariantMap) {
    return fail(error, QStringLiteral(
        "Committed paintTiles have an unsupported format or malformed profile/tile map."));
  }
  const auto icc_text = icc_value.toString().toLatin1();
  const auto icc_bytes = QByteArray::fromBase64(
      icc_text, QByteArray::AbortOnBase64DecodingErrors);
  if (icc_bytes.isEmpty() || icc_bytes.toBase64() != icc_text) {
    return fail(error, QStringLiteral(
        "Committed paintTiles use a malformed or noncanonical ICC profile."));
  }
  const auto encoded_tiles = tiles_value.toMap();
  for (auto it = encoded_tiles.cbegin(); it != encoded_tiles.cend(); ++it) {
    if (it.value().metaType().id() != QMetaType::QString) {
      return fail(error, QStringLiteral(
          "Committed paint tile payloads must be canonical base64 strings."));
    }
  }

  const auto legacy_value = layer.value(QStringLiteral("pixelData"));
  if (legacy_value.isValid() && !legacy_value.isNull() &&
      legacy_value.metaType().id() != QMetaType::QByteArray) {
    return fail(error, QStringLiteral(
        "Committed paint tile legacy pixelData must remain an encoded byte array."));
  }
  const auto legacy_png = legacy_value.toByteArray();
  if (!legacy_png.isEmpty()) {
    QBuffer buffer;
    buffer.setData(legacy_png);
    if (!buffer.open(QIODevice::ReadOnly)) {
      return fail(error, QStringLiteral("Committed paint tile legacy pixelData could not be opened."));
    }
    QImageReader reader(&buffer, "PNG");
    if (!reader.canRead() || reader.size() != QSize(width, height)) {
      return fail(error, QStringLiteral(
          "Committed paint tile legacy pixelData must be a PNG matching its bounded canvas."));
    }
  }

  PhotoPaintSurface surface;
  QString surface_error;
  if (!surface.load(layer, &surface_error)) {
    return fail(error, QStringLiteral("Committed paintTiles are invalid: %1")
                           .arg(surface_error));
  }
  for (auto it = surface.tiles.cbegin(); it != surface.tiles.cend(); ++it) {
    const auto parts = it.key().split(',');
    bool x_ok = false;
    bool y_ok = false;
    const auto x = parts.value(0).toInt(&x_ok);
    const auto y = parts.value(1).toInt(&y_ok);
    if (!x_ok || !y_ok) {
      return fail(error, QStringLiteral("Committed paintTiles contain invalid tile coordinates."));
    }
    const auto rect = surface.tile_rect(x, y);
    const auto tile_pixels = static_cast<qint64>(rect.width()) * rect.height();
    if (tile_pixels <= 0 ||
        context->decoded_paint_pixels >
            kMaximumDecodedPaintPixels - tile_pixels) {
      return fail(error, QStringLiteral(
          "Nested Smart Object paint tiles exceed the 64 MP decoded-pixel limit."));
    }
    context->decoded_paint_pixels += tile_pixels;

    const auto tile = surface.tile(x, y, &surface_error);
    if (tile.isNull() || tile.size() != rect.size()) {
      return fail(error, QStringLiteral("Committed paint tile is truncated or corrupt: %1")
                             .arg(surface_error));
    }
    for (int row_index = 0; row_index < tile.height(); ++row_index) {
      const auto* row = reinterpret_cast<const QRgbaFloat32*>(
          tile.constScanLine(row_index));
      for (int column = 0; column < tile.width(); ++column) {
        const auto& pixel = row[column];
        const auto red = pixel.red();
        const auto green = pixel.green();
        const auto blue = pixel.blue();
        const auto alpha = pixel.alpha();
        if (!std::isfinite(red) || !std::isfinite(green) ||
            !std::isfinite(blue) || !std::isfinite(alpha) ||
            alpha < 0.0F || alpha > 1.0F ||
            (alpha == 0.0F &&
             (red != 0.0F || green != 0.0F || blue != 0.0F))) {
          return fail(error, QStringLiteral(
              "Committed paint tiles must contain finite premultiplied RGBA with alpha in [0, 1]."));
        }
      }
    }
  }
  return true;
}

bool check_layer_references(const QVariantMap& layer,
                            const QSet<QString>& mask_set_ids,
                            const QSet<QString>& mask_set_names,
                            const QSet<QString>& layer_ids,
                            QString* error) {
  static const QStringList external_reference_keys{
      QStringLiteral("nestedDocumentRef"),
      QStringLiteral("nestedSmartObjectId"),
      QStringLiteral("nestedSmartObjectRef"),
      QStringLiteral("smartDocumentRef"),
      QStringLiteral("smartDocumentId"),
      QStringLiteral("smartObjectContentRef"),
      QStringLiteral("smartObjectDocumentId"),
      QStringLiteral("smartObjectDocumentRef")};
  for (const auto& key : external_reference_keys) {
    if (!layer.value(key).toString().trimmed().isEmpty()) {
      return fail(error, QStringLiteral(
          "Smart Object documents cannot contain unresolved nested-document references (%1).")
                             .arg(key));
    }
  }
  if (!layer.value(QStringLiteral("clippedToLayerId")).toString().trimmed().isEmpty() &&
      !layer_ids.contains(layer.value(QStringLiteral("clippedToLayerId")).toString().trimmed())) {
    return fail(error, QStringLiteral(
        "Smart Object layer references a clipping target outside the component."));
  }
  if (!layer.value(QStringLiteral("clippingBaseLayerId")).toString().trimmed().isEmpty() &&
      !layer_ids.contains(layer.value(QStringLiteral("clippingBaseLayerId")).toString().trimmed())) {
    return fail(error, QStringLiteral(
        "Smart Object layer references a clipping base outside the component."));
  }

  auto check_mask_list = [&](const QString& key, const QSet<QString>& available) {
    const auto value = layer.value(key);
    QStringList references;
    if (value.metaType().id() == QMetaType::QString) {
      references.append(value.toString());
    } else if (value.metaType().id() == QMetaType::QStringList) {
      references = value.toStringList();
    } else {
      for (const auto& item : value.toList()) references.append(item.toString());
    }
    for (const auto& reference : references) {
      const auto id = reference.trimmed();
      if (!id.isEmpty() && !available.contains(id)) {
        return false;
      }
    }
    return true;
  };
  for (const auto& key : {QStringLiteral("selectionMaskSetId"),
                           QStringLiteral("selectionMaskSetIds"),
                           QStringLiteral("selectionIntersectionMaskSetIds"),
                           QStringLiteral("maskRef"),
                           QStringLiteral("effectMaskRef"),
                           QStringLiteral("effectMaskRefs")}) {
    if (!check_mask_list(key, mask_set_ids)) {
      return fail(error, QStringLiteral(
          "Smart Object layer has a mask reference that is missing from its embedded maskStack."));
    }
  }
  for (const auto& key : {QStringLiteral("selectionMaskSetName"),
                           QStringLiteral("selectionMaskSetNames"),
                           QStringLiteral("selectionIntersectionMaskSetNames"),
                           QStringLiteral("maskSetName"),
                           QStringLiteral("effectMaskName"),
                           QStringLiteral("effectMaskNames")}) {
    if (!check_mask_list(key, mask_set_names)) {
      return fail(error, QStringLiteral(
          "Smart Object layer has a mask name reference that is missing from its embedded maskStack."));
    }
  }
  const auto linked_style_light = layer.value(
      QStringLiteral("layerStyleLightLinked"));
  if (linked_style_light.isValid() &&
      linked_style_light.metaType().id() != QMetaType::Bool) {
    return fail(error, QStringLiteral(
        "Smart Object layer style light link state must be a boolean."));
  }
  if (linked_style_light.toBool() &&
      !layer.contains(QStringLiteral("layerStyleStack"))) {
    return fail(error, QStringLiteral(
        "Linked Smart Object layer lighting requires a style stack."));
  }
  if (layer.contains(QStringLiteral("layerStyleStack"))) {
    const auto encoded = layer.value(QStringLiteral("layerStyleStack"));
    if (encoded.metaType().id() != QMetaType::QString) {
      return fail(error, QStringLiteral(
          "Smart Object layer styles must use the engine string envelope."));
    }
    std::string style_error;
    const auto styles = photo_editor::pipeline::deserialize_layer_style_stack(
        encoded.toString().toStdString(), &style_error);
    if (!styles) {
      return fail(error, QStringLiteral(
          "Smart Object layer has an invalid style stack: %1")
          .arg(QString::fromStdString(style_error)));
    }
    bool light_ok = false;
    const auto light = layer.value(
        QStringLiteral("layerStyleGlobalLightAngle"), -45.0).toDouble(&light_ok);
    if (!light_ok || !std::isfinite(light) || light < -3600.0 || light > 3600.0) {
      return fail(error, QStringLiteral(
          "Smart Object layer style light angle must be finite and within 3600 degrees."));
    }
    for (const auto& operation : styles->operations) {
      const auto reference = QString::fromUtf8(
          operation.mask_ref.data(),
          static_cast<qsizetype>(operation.mask_ref.size())).trimmed();
      if (!reference.isEmpty() && !mask_set_ids.contains(reference)) {
        return fail(error, QStringLiteral(
            "Smart Object layer style references a mask missing from its embedded maskStack."));
      }
    }
  }
  return true;
}

void collect_parent_links(const QVariantList& layers,
                          QSet<QString>* ids,
                          QHash<QString, QString>* parent_links) {
  if (ids == nullptr || parent_links == nullptr) return;
  for (const auto& value : layers) {
    const auto layer = value.toMap();
    const auto id = layer.value(QStringLiteral("id")).toString().trimmed();
    if (!id.isEmpty()) {
      ids->insert(id);
      const auto parent_id = layer.value(QStringLiteral("parentId")).toString().trimmed();
      if (!parent_id.isEmpty()) parent_links->insert(id, parent_id);
    }
    collect_parent_links(layer.value(QStringLiteral("children")).toList(), ids, parent_links);
    collect_parent_links(layer.value(QStringLiteral("layers")).toList(), ids, parent_links);
  }
}

bool validate_parent_links(const QSet<QString>& layer_ids,
                           const QHash<QString, QString>& parent_links,
                           QString* error) {
  for (auto it = parent_links.cbegin(); it != parent_links.cend(); ++it) {
    if (!layer_ids.contains(it.value())) {
      return fail(error, QStringLiteral(
          "Smart Object layer depends on a parent outside the component; select its parent group."));
    }
  }
  QSet<QString> complete;
  for (auto it = parent_links.cbegin(); it != parent_links.cend(); ++it) {
    QSet<QString> path;
    auto current = it.key();
    while (parent_links.contains(current)) {
      if (path.contains(current)) {
        return fail(error, QStringLiteral("Smart Object layer parent hierarchy contains a cycle."));
      }
      if (complete.contains(current)) break;
      path.insert(current);
      current = parent_links.value(current);
    }
    complete.unite(path);
  }
  return true;
}

bool validate_layer_list(const QVariantList& layers,
                         const QSet<QString>& mask_set_ids,
                         ValidationContext* context,
                         int document_depth,
                         int layer_depth,
                         QSet<QString>* all_layer_ids,
                         const QSet<QString>& mask_set_names,
                         QSet<QString>* seen_layer_ids,
                         QSet<QString>* instance_ids,
                         bool* contains_renderable_content,
                         QString* error) {
  if (layer_depth > kMaximumLayerDepth) {
    return fail(error, QStringLiteral(
        "Smart Object layer nesting exceeds the 64-level limit."));
  }
  for (const auto& value : layers) {
    if (value.metaType().id() != QMetaType::QVariantMap) {
      return fail(error, QStringLiteral(
          "Smart Object layer collections must contain only layer maps."));
    }
    const auto layer = value.toMap();
    const auto id = layer.value(QStringLiteral("id")).toString().trimmed();
    if (id.isEmpty() || id.size() > 160 || seen_layer_ids->contains(id)) {
      return fail(error, QStringLiteral(
          "Smart Object child layer IDs must be nonempty, bounded, and unique within the document."));
    }
    seen_layer_ids->insert(id);
    ++context->layer_count;
    if (context->layer_count > kMaximumLayerCount) {
      return fail(error, QStringLiteral(
          "Smart Object document expansion exceeds the 4096-layer limit."));
    }
    if (!check_layer_references(layer, mask_set_ids, mask_set_names,
                                *all_layer_ids, error)) return false;

    const auto kind = layer.value(QStringLiteral("kind")).toString().trimmed().toLower();
    static const QSet<QString> supported_kinds{
        QStringLiteral("adjustment"), QStringLiteral("fill"),
        QStringLiteral("group"), QStringLiteral("pixel"),
        QStringLiteral("shape"), QStringLiteral("source"),
        QStringLiteral("text"), QStringLiteral("vector")};
    if (!supported_kinds.contains(kind)) {
      return fail(error, QStringLiteral(
          "Smart Object child layer kind is unsupported: %1.").arg(kind));
    }
    if (!validate_committed_paint_tiles(layer, kind, context, error)) return false;
    if (!layer.value(QStringLiteral("sourcePath")).toString().trimmed().isEmpty()) {
      return fail(error, QStringLiteral(
          "Nested image layers cannot retain an external sourcePath; embed the original image first."));
    }

    if (kind == QStringLiteral("source")) {
      if (!is_supported_embedded_source(layer, error)) return false;
    } else if (kind == QStringLiteral("pixel")) {
      if (!layer.value(QStringLiteral("smartObject")).toMap().isEmpty()) {
        const auto descriptor = layer.value(QStringLiteral("smartObject")).toMap();
        QString descriptor_error;
        if (!validate_content_descriptor(descriptor, false, &descriptor_error) ||
            descriptor.value(QStringLiteral("policy")).toString() !=
                QString::fromLatin1(kEmbeddedPolicy) ||
            descriptor.value(QStringLiteral("payloadKind")).toString() !=
                QStringLiteral("layer-png")) {
          return fail(error, QStringLiteral(
              "Pixel image children must carry a valid embedded PNG source descriptor."));
        }
      } else if (layer.value(QStringLiteral("pixelData")).toByteArray().isEmpty() &&
                 !layer.contains(QStringLiteral("paintTiles"))) {
        return fail(error, QStringLiteral(
            "Pixel children need editable pixelData, committed paintTiles, or an embedded PNG source descriptor."));
      }
    }

    const auto instance_id = layer.value(QString::fromLatin1(kInstanceIdField)).toString();
    const auto nested_value = layer.value(QString::fromLatin1(kDocumentField));
    if (nested_value.isValid() && !nested_value.isNull()) {
      if (nested_value.metaType().id() != QMetaType::QVariantMap) {
        return fail(error, QStringLiteral(
            "Inline nested Smart Object content must be a document map."));
      }
      if (!valid_id(instance_id) || instance_ids->contains(instance_id)) {
        return fail(error, QStringLiteral(
            "Nested Smart Object instances require unique instance IDs."));
      }
      instance_ids->insert(instance_id);
      if (!validate_document_recursive(nested_value.toMap(), context,
                                       document_depth + 1, error)) {
        return false;
      }
      *contains_renderable_content = true;
    }

    for (const auto& child_key : {QStringLiteral("children"),
                                  QStringLiteral("layers")}) {
      if (!layer.contains(child_key)) continue;
      const auto children_value = layer.value(child_key);
      if (children_value.metaType().id() != QMetaType::QVariantList) {
        return fail(error, QStringLiteral(
            "Smart Object group children must be a layer list."));
      }
      if (!validate_layer_list(children_value.toList(), mask_set_ids, context,
                               document_depth, layer_depth + 1, all_layer_ids,
                               mask_set_names,
                               seen_layer_ids,
                               instance_ids, contains_renderable_content,
                               error)) {
        return false;
      }
    }
    if (kind != QStringLiteral("adjustment")) {
      *contains_renderable_content = true;
    }
  }
  return true;
}

bool validate_document_recursive(const QVariantMap& document,
                                ValidationContext* context,
                                int document_depth,
                                QString* error) {
  if (context == nullptr) {
    return fail(error, QStringLiteral("No Smart Object validation context was provided."));
  }
  if (document_depth > kMaximumDocumentDepth) {
    return fail(error, QStringLiteral(
        "Nested Smart Object recursion exceeds the 8-document limit."));
  }
  const QSet<QString> allowed_fields{
      QStringLiteral("schema"), QStringLiteral("version"),
      QStringLiteral("contentId"), QStringLiteral("revision"),
      QStringLiteral("canvasWidth"), QStringLiteral("canvasHeight"),
      QStringLiteral("layers"), QStringLiteral("maskStack"),
      QStringLiteral("documentLayerStyleLightAngle")};
  for (const auto& key : document.keys()) {
    if (!allowed_fields.contains(key)) {
      return fail(error, QStringLiteral(
          "Smart Object document has an unsupported field: %1.").arg(key));
    }
  }
  int version = 0;
  if (document.value(QStringLiteral("schema")).toString() !=
          QString::fromLatin1(kSchema) ||
      !variant_integer(document.value(QStringLiteral("version")), &version) ||
      (version != kVersion && version != kDocumentLayerStyleLightVersion &&
       version != kTextDimensionVersion)) {
    return fail(error, QStringLiteral(
        "Smart Object document schema or version is unsupported."));
  }
  const auto has_document_light = document.contains(
      QStringLiteral("documentLayerStyleLightAngle"));
  if ((version == kVersion && has_document_light) ||
      (version == kDocumentLayerStyleLightVersion && !has_document_light)) {
    return fail(error, QStringLiteral(
        "Smart Object document layer style lighting requires document version 2."));
  }
  if (has_document_light) {
    bool angle_ok = false;
    const auto angle = document.value(
        QStringLiteral("documentLayerStyleLightAngle")).toDouble(&angle_ok);
    if (!angle_ok || !std::isfinite(angle) || angle < -3600.0 || angle > 3600.0) {
      return fail(error, QStringLiteral(
          "Smart Object document layer style light angle must be finite and within 3600 degrees."));
    }
  }
  const auto content_id = document.value(QStringLiteral("contentId")).toString();
  if (!valid_id(content_id)) {
    return fail(error, QStringLiteral(
        "Smart Object document contentId must be a canonical UUID."));
  }
  if (context->document_path.contains(content_id)) {
    return fail(error, QStringLiteral(
        "Smart Object document cycle detected through a repeated contentId."));
  }
  int revision = 0;
  if (!variant_integer(document.value(QStringLiteral("revision")), &revision) ||
      revision <= 0) {
    return fail(error, QStringLiteral(
        "Smart Object document revision must be a positive integer."));
  }
  int width = 0;
  int height = 0;
  const auto width_ok = variant_integer(document.value(QStringLiteral("canvasWidth")), &width);
  const auto height_ok = variant_integer(document.value(QStringLiteral("canvasHeight")), &height);
  if (!width_ok || !height_ok || !bounded_canvas(width, height, error)) return false;

  const auto layers_value = document.value(QStringLiteral("layers"));
  const auto masks_value = document.value(QStringLiteral("maskStack"));
  if (layers_value.metaType().id() != QMetaType::QVariantList ||
      masks_value.metaType().id() != QMetaType::QVariantList) {
    return fail(error, QStringLiteral(
        "Smart Object document layers and maskStack must be lists."));
  }
  const auto layers = layers_value.toList();
  const auto masks = masks_value.toList();
  if (layers.isEmpty() || !list_has_maps_only(layers) ||
      !list_has_maps_only(masks)) {
    return fail(error, QStringLiteral(
        "Smart Object document requires layer maps and a mask stack of maps."));
  }
  QString text_dimension_error;
  if (!validate_photo_text_dimensions_in_layers(
          layers, &text_dimension_error)) {
    return fail(error, QStringLiteral(
        "Smart Object text dimension settings are invalid: %1")
            .arg(text_dimension_error));
  }
  const bool has_text_dimensions =
      photo_text_dimensions_present_in_layers(layers);
  if ((has_text_dimensions && version != kTextDimensionVersion) ||
      (!has_text_dimensions && version == kTextDimensionVersion)) {
    return fail(error, QStringLiteral(
        "Smart Object dimensional text requires document version 3."));
  }
  if (photo_layer_style_light::has_linked_lighting_in_scope(layers) &&
      !has_document_light) {
    return fail(error, QStringLiteral(
        "Linked Smart Object lighting requires a document light angle."));
  }
  if (photo_layer_style_light::has_linked_lighting_in_scope(layers) &&
      version != kDocumentLayerStyleLightVersion &&
      version != kTextDimensionVersion) {
    return fail(error, QStringLiteral(
        "Linked layer style lighting requires Smart Object document version 2."));
  }
  context->mask_count += masks.size();
  if (context->mask_count > kMaximumMaskCount) {
    return fail(error, QStringLiteral(
        "Nested Smart Object expansion exceeds the 8192-mask limit."));
  }
  QSet<QString> mask_ids;
  QSet<QString> mask_set_ids;
  QSet<QString> mask_set_names;
  for (const auto& value : masks) {
    const auto mask = value.toMap();
    const auto id = mask.value(QStringLiteral("id")).toString().trimmed();
    const auto set_id = mask.value(QStringLiteral("maskSetId")).toString().trimmed();
    const auto is_retouch = mask.value(QStringLiteral("toolRole")).toString()
                                .trimmed().toLower() == QStringLiteral("retouch");
    const auto has_component_marker = has_component_post_composite_retouch_marker(mask);
    const auto is_component_retouch =
        valid_component_post_composite_retouch_mask(mask);
    if (has_component_marker && !is_component_retouch) {
      return fail(error, QStringLiteral(
          "Component retouch masks require supported v1 mode, repair identity, and a canvas basis within 64 MP."));
    }
    if (id.isEmpty() || mask_ids.contains(id) ||
        (set_id.isEmpty() && (!is_retouch || !is_component_retouch))) {
      return fail(error, QStringLiteral(
          "Smart Object mask definitions need unique IDs and nonempty maskSetId values."));
    }
    mask_ids.insert(id);
    // Retouch masks are repair operations, not reusable selection sets. Their
    // ids must never satisfy a child layer's selection-mask reference.
    if (!is_retouch) {
      mask_set_ids.insert(set_id);
      const auto set_name = mask.value(QStringLiteral("maskSetName")).toString().trimmed();
      if (!set_name.isEmpty()) mask_set_names.insert(set_name);
    }
  }
  for (const auto& value : masks) {
    const auto mask = value.toMap();
    const auto parent_id = mask.value(QStringLiteral("parentId")).toString().trimmed();
    const auto output_id = mask.value(QStringLiteral("outputId")).toString().trimmed();
    if (!parent_id.isEmpty() && !mask_ids.contains(parent_id)) {
      return fail(error, QStringLiteral(
          "Smart Object mask definition references a parent outside its embedded maskStack."));
    }
    if (!output_id.isEmpty() && !mask_ids.contains(output_id) &&
        !mask_set_ids.contains(output_id)) {
      return fail(error, QStringLiteral(
          "Smart Object mask definition references an output outside its embedded maskStack."));
    }
  }

  qsizetype estimated_bytes = 0;
  if (!estimate_json_bytes(document, &estimated_bytes)) {
    return fail(error, QStringLiteral(
        "Smart Object document exceeds the 64 MiB serialized-content limit."));
  }

  context->document_path.insert(content_id);
  QSet<QString> layer_ids;
  QSet<QString> seen_layer_ids;
  QHash<QString, QString> parent_links;
  QSet<QString> instance_ids;
  bool contains_renderable_content = false;
  collect_parent_links(layers, &layer_ids, &parent_links);
  if (!validate_parent_links(layer_ids, parent_links, error)) {
    context->document_path.remove(content_id);
    return false;
  }
  const auto valid = validate_layer_list(layers, mask_set_ids, context,
      document_depth, 1, &layer_ids, mask_set_names,
      &seen_layer_ids, &instance_ids,
      &contains_renderable_content, error);
  context->document_path.remove(content_id);
  return valid;
}

bool collect_stack_records(const QVariantList& stack,
                           QHash<QString, QVariantMap>* records,
                           QHash<QString, bool>* parent_locked,
                           QString* error, int depth = 0,
                           bool locked = false) {
  if (depth > kMaximumLayerDepth) {
    return fail(error, QStringLiteral("Layer hierarchy exceeds the 64-level limit."));
  }
  for (const auto& value : stack) {
    if (value.metaType().id() != QMetaType::QVariantMap) {
      return fail(error, QStringLiteral("Layer stack contains a non-layer value."));
    }
    const auto layer = value.toMap();
    const auto id = layer.value(QStringLiteral("id")).toString().trimmed();
    if (id.isEmpty() || records->contains(id)) {
      return fail(error, QStringLiteral("Layer IDs must be nonempty and unique."));
    }
    records->insert(id, layer);
    parent_locked->insert(id, locked);
    if (records->size() > kMaximumLayerCount) {
      return fail(error, QStringLiteral("Layer stack exceeds the 4096-layer operation limit."));
    }
    const auto child_locked = locked || layer.value(QStringLiteral("locked")).toBool();
    for (const auto& child_key : {QStringLiteral("children"), QStringLiteral("layers")}) {
      if (!layer.contains(child_key)) continue;
      if (layer.value(child_key).metaType().id() != QMetaType::QVariantList ||
          !collect_stack_records(layer.value(child_key).toList(), records,
                                 parent_locked, error, depth + 1, child_locked)) {
        return false;
      }
    }
  }
  return true;
}

bool validate_stack(const QVariantList& stack,
                    QHash<QString, QVariantMap>* records,
                    QHash<QString, bool>* parent_locked,
                    QString* error) {
  if (!collect_stack_records(stack, records, parent_locked, error)) return false;
  for (auto it = records->cbegin(); it != records->cend(); ++it) {
    auto parent_id = it->value(QStringLiteral("parentId")).toString().trimmed();
    QSet<QString> visited{it.key()};
    bool inherited_lock = parent_locked->value(it.key());
    while (!parent_id.isEmpty()) {
      if (visited.contains(parent_id)) {
        return fail(error, QStringLiteral("Layer parent hierarchy contains a cycle."));
      }
      const auto parent = records->constFind(parent_id);
      if (parent == records->cend()) {
        return fail(error, QStringLiteral("Layer references a parent that is missing from the layer stack."));
      }
      visited.insert(parent_id);
      inherited_lock = inherited_lock || parent_locked->value(parent_id) ||
                       parent->value(QStringLiteral("locked")).toBool();
      parent_id = parent->value(QStringLiteral("parentId")).toString().trimmed();
    }
    parent_locked->insert(it.key(), inherited_lock);
  }
  return true;
}

void add_nested_descendants(const QVariantMap& layer,
                            const QHash<QString, QVariantMap>& records,
                            QSet<QString>* ids) {
  if (ids == nullptr) return;
  for (const auto& child_key : {QStringLiteral("children"),
                                QStringLiteral("layers")}) {
    for (const auto& value : layer.value(child_key).toList()) {
      const auto child_id = value.toMap().value(QStringLiteral("id")).toString().trimmed();
      if (child_id.isEmpty() || ids->contains(child_id)) continue;
      ids->insert(child_id);
      add_nested_descendants(records.value(child_id), records, ids);
    }
  }
}

QSet<QString> selection_subtree_ids(const QHash<QString, QVariantMap>& records,
                                    const QSet<QString>& selected_ids) {
  auto closure = selected_ids;
  bool changed = true;
  while (changed) {
    changed = false;
    const auto current = closure.values();
    for (const auto& id : current) {
      const auto before = closure.size();
      add_nested_descendants(records.value(id), records, &closure);
      changed = changed || closure.size() != before;
    }
    for (auto it = records.cbegin(); it != records.cend(); ++it) {
      if (closure.contains(it.key())) continue;
      const auto parent_id = it->value(QStringLiteral("parentId")).toString().trimmed();
      if (!parent_id.isEmpty() && closure.contains(parent_id)) {
        closure.insert(it.key());
        changed = true;
      }
    }
  }
  return closure;
}

QVariantList replace_and_remove_subtree(const QVariantList& stack,
                                        const QString& root_id,
                                        const QSet<QString>& descendants,
                                        const QVariantMap& replacement,
                                        bool* replaced) {
  QVariantList result;
  result.reserve(stack.size());
  for (const auto& value : stack) {
    auto layer = value.toMap();
    const auto id = layer.value(QStringLiteral("id")).toString().trimmed();
    if (id == root_id) {
      result.append(replacement);
      *replaced = true;
      continue;
    }
    if (descendants.contains(id)) continue;
    for (const auto& child_key : {QStringLiteral("children"),
                                  QStringLiteral("layers")}) {
      if (!layer.contains(child_key)) continue;
      bool child_replaced = false;
      layer.insert(child_key, replace_and_remove_subtree(
          layer.value(child_key).toList(), root_id, descendants,
          replacement, &child_replaced));
      *replaced = *replaced || child_replaced;
    }
    result.append(layer);
  }
  return result;
}

QVariantList mutate_documents(const QVariantList& stack,
                              const QHash<QString, QVariantMap>& replacements) {
  QVariantList result;
  result.reserve(stack.size());
  for (const auto& value : stack) {
    auto layer = value.toMap();
    const auto layer_id = layer.value(QStringLiteral("id")).toString().trimmed();
    const auto replacement = replacements.constFind(layer_id);
    if (replacement != replacements.cend()) layer = replacement.value();
    for (const auto& child_key : {QStringLiteral("children"),
                                  QStringLiteral("layers")}) {
      if (layer.contains(child_key)) {
        layer.insert(child_key, mutate_documents(
            layer.value(child_key).toList(), replacements));
      }
    }
    result.append(layer);
  }
  return result;
}

struct SharedGroup {
  QVariantMap document;
  QStringList instance_ids;
};

bool validate_instance_identity(const QVariantMap& layer, QString* error) {
  if (layer.value(QStringLiteral("id")).toString().trimmed().isEmpty()) {
    return fail(error, QStringLiteral("Smart Object instance has no layer ID."));
  }
  if (!valid_id(layer.value(QString::fromLatin1(kInstanceIdField)).toString())) {
    return fail(error, QStringLiteral(
        "Smart Object instance has a missing or malformed instance ID."));
  }
  return true;
}

bool collect_shared_groups(const QVariantList& layer_stack,
                           const QStringList& selected_layer_ids,
                           QHash<QString, QVariantMap>* records,
                           QHash<QString, bool>* parent_locked,
                           QHash<QString, SharedGroup>* groups,
                           QString* error) {
  if (selected_layer_ids.isEmpty()) {
    return fail(error, QStringLiteral("Select at least one component instance."));
  }
  if (!validate_stack(layer_stack, records, parent_locked, error)) return false;
  QSet<QString> selected;
  for (const auto& raw_id : selected_layer_ids) {
    const auto layer_id = raw_id.trimmed();
    if (layer_id.isEmpty() || selected.contains(layer_id)) {
      return fail(error, QStringLiteral("Selected layer IDs must be nonempty and unique."));
    }
    if (!records->contains(layer_id)) {
      return fail(error, QStringLiteral("A selected component no longer exists."));
    }
    const auto layer = records->value(layer_id);
    const auto document = layer.value(QString::fromLatin1(kDocumentField)).toMap();
    QString validation_error;
    if (document.isEmpty() || !validate_document(document, &validation_error)) {
      return fail(error, document.isEmpty()
          ? QStringLiteral("Selected layer is not an editable Smart Object component.")
          : QStringLiteral("Selected component is invalid: %1").arg(validation_error));
    }
    if (!validate_instance_identity(layer, error)) return false;
    if (parent_locked->value(layer_id) || layer.value(QStringLiteral("locked")).toBool()) {
      return fail(error, QStringLiteral("A selected component or parent group is locked."));
    }
    selected.insert(layer_id);
    const auto content_id = document.value(QStringLiteral("contentId")).toString();
    auto it = groups->find(content_id);
    if (it == groups->end()) {
      groups->insert(content_id, SharedGroup{document, {layer_id}});
    } else {
      if (it->document != document) {
        return fail(error, QStringLiteral(
            "Instances with one contentId contain conflicting Smart Object documents."));
      }
      it->instance_ids.append(layer_id);
    }
  }

  QSet<QString> seen_instance_ids;
  for (auto it = records->cbegin(); it != records->cend(); ++it) {
    if (!it->contains(QString::fromLatin1(kDocumentField))) continue;
    const auto document = it->value(QString::fromLatin1(kDocumentField)).toMap();
    if (document.isEmpty()) {
      return fail(error, QStringLiteral("Layer has a corrupt nested Smart Object document."));
    }
    if (!validate_instance_identity(it.value(), error)) return false;
    const auto instance_id = it->value(QString::fromLatin1(kInstanceIdField)).toString();
    if (seen_instance_ids.contains(instance_id)) {
      return fail(error, QStringLiteral("Smart Object instance IDs must be unique within a layer stack."));
    }
    seen_instance_ids.insert(instance_id);
    QString validation_error;
    if (!validate_document(document, &validation_error)) {
      return fail(error, QStringLiteral("Layer contains invalid nested Smart Object content: %1")
                             .arg(validation_error));
    }
    const auto content_id = document.value(QStringLiteral("contentId")).toString();
    auto group = groups->find(content_id);
    if (group == groups->end()) continue;
    if (group->document != document) {
      return fail(error, QStringLiteral(
          "Instances with one contentId contain conflicting Smart Object documents."));
    }
    if (!group->instance_ids.contains(it.key())) group->instance_ids.append(it.key());
  }
  return true;
}

QVariantMap with_document(const QVariantMap& layer,
                          const QVariantMap& document) {
  auto result = layer;
  result.insert(QString::fromLatin1(kDocumentField), document);
  return result;
}

bool has_renderable_content(const QVariantList& layers) {
  for (const auto& value : layers) {
    const auto layer = value.toMap();
    const auto kind = layer.value(QStringLiteral("kind")).toString().trimmed().toLower();
    if (kind != QStringLiteral("adjustment") && kind != QStringLiteral("group")) return true;
    if (has_renderable_content(layer.value(QStringLiteral("children")).toList()) ||
        has_renderable_content(layer.value(QStringLiteral("layers")).toList())) return true;
    const auto nested = layer.value(QString::fromLatin1(kDocumentField)).toMap();
    if (!nested.isEmpty() && has_renderable_content(nested.value(QStringLiteral("layers")).toList())) return true;
  }
  return false;
}

bool check_clipping_references(const QVariantList& layers, QString* error) {
  QSet<QString> parents_with_preceding_layer;
  for (const auto& value : layers) {
    const auto layer = value.toMap();
    const auto parent_id = layer.value(QStringLiteral("parentId")).toString().trimmed();
    if (layer.value(QStringLiteral("clippingMask")).toBool() &&
        !parents_with_preceding_layer.contains(parent_id)) {
      return fail(error, QStringLiteral(
          "A selected component has a clipping mask whose base layer is outside the component."));
    }
    parents_with_preceding_layer.insert(parent_id);
    for (const auto& child_key : {QStringLiteral("children"),
                                  QStringLiteral("layers")}) {
      if (layer.contains(child_key) &&
          !check_clipping_references(layer.value(child_key).toList(), error)) return false;
    }
  }
  return true;
}

QVariantList extract_selection(const QVariantList& layers,
                               const QSet<QString>& selected_ids,
                               QSet<QString>* found_ids) {
  QVariantList result;
  for (const auto& value : layers) {
    auto layer = value.toMap();
    const auto id = layer.value(QStringLiteral("id")).toString().trimmed();
    if (selected_ids.contains(id)) {
      result.append(layer);
      // The selected layer carries its whole existing subtree.
      const auto mark_descendants = [&](const auto& self, const QVariantList& children) -> void {
        for (const auto& child_value : children) {
          const auto child = child_value.toMap();
          const auto child_id = child.value(QStringLiteral("id")).toString().trimmed();
          if (selected_ids.contains(child_id)) found_ids->insert(child_id);
          self(self, child.value(QStringLiteral("children")).toList());
          self(self, child.value(QStringLiteral("layers")).toList());
        }
      };
      found_ids->insert(id);
      mark_descendants(mark_descendants, layer.value(QStringLiteral("children")).toList());
      mark_descendants(mark_descendants, layer.value(QStringLiteral("layers")).toList());
      continue;
    }
    bool included = false;
    for (const auto& child_key : {QStringLiteral("children"),
                                  QStringLiteral("layers")}) {
      if (!layer.contains(child_key)) continue;
      const auto selected_children = extract_selection(
          layer.value(child_key).toList(), selected_ids, found_ids);
      if (!selected_children.isEmpty()) {
        layer.insert(child_key, selected_children);
        included = true;
      }
    }
    if (included) result.append(layer);
  }
  return result;
}

QVariant strip_thumbnail_fields(const QVariant& value, int depth = 0) {
  if (depth > kMaximumLayerDepth + kMaximumDocumentDepth + 4) return value;
  if (value.metaType().id() == QMetaType::QVariantMap) {
    auto map = value.toMap();
    for (auto it = map.begin(); it != map.end();) {
      if (it.key().startsWith(QStringLiteral("thumbnail"), Qt::CaseInsensitive)) {
        it = map.erase(it);
      } else {
        it.value() = strip_thumbnail_fields(it.value(), depth + 1);
        ++it;
      }
    }
    return map;
  }
  if (value.metaType().id() == QMetaType::QVariantList) {
    auto list = value.toList();
    for (auto& item : list) item = strip_thumbnail_fields(item, depth + 1);
    return list;
  }
  return value;
}

bool validate_selection(const QVariantList& layer_stack,
                        const QStringList& selected_ids,
                        QHash<QString, QVariantMap>* records,
                        QHash<QString, bool>* parent_locked,
                        QSet<QString>* selected_set,
                        QString* error) {
  if (selected_ids.isEmpty()) {
    return fail(error, QStringLiteral("Select one layer or group to convert."));
  }
  if (!validate_stack(layer_stack, records, parent_locked, error)) return false;
  for (const auto& raw_id : selected_ids) {
    const auto id = raw_id.trimmed();
    if (id.isEmpty() || selected_set->contains(id) || !records->contains(id)) {
      return fail(error, QStringLiteral("Selected layer IDs must be unique and exist in the layer stack."));
    }
    if (parent_locked->value(id) || records->value(id).value(QStringLiteral("locked")).toBool()) {
      return fail(error, QStringLiteral("A selected layer or parent group is locked."));
    }
    selected_set->insert(id);
  }
  return true;
}

}  // namespace

QVariantList strip_transient_layer_view_fields(const QVariantList& layers) {
  return strip_thumbnail_fields(layers).toList();
}

QVariantMap document_from_layers(const QVariantList& layers,
                                 const QVariantList& mask_stack,
                                 const QStringList& selected_layer_ids,
                                 int canvas_width,
                                 int canvas_height,
                                 QString* error,
                                 double document_layer_style_light_angle) {
  clear_error(error);
  if (!bounded_canvas(canvas_width, canvas_height, error)) return {};
  if (!photo_layer_style_light::valid_angle(
          document_layer_style_light_angle)) {
    return fail_map(error, QStringLiteral(
        "The document layer style light angle is outside the supported range."));
  }
  if (mask_stack.size() > kMaximumMaskCount || !list_has_maps_only(mask_stack)) {
    return fail_map(error, QStringLiteral(
        "The parent maskStack must contain no more than 8192 mask definition maps."));
  }
  QHash<QString, QVariantMap> records;
  QHash<QString, bool> parent_locked;
  QSet<QString> selected;
  if (!validate_selection(layers, selected_layer_ids, &records,
                          &parent_locked, &selected, error)) return {};
  const auto subtree_ids = selection_subtree_ids(records, selected);
  QSet<QString> found;
  const auto selected_layers = extract_selection(layers, subtree_ids, &found);
  if (selected_layers.isEmpty() || found.size() != subtree_ids.size()) {
    return fail_map(error, QStringLiteral("A selected layer was not present in the document subtree."));
  }
  const auto durable_layers = strip_transient_layer_view_fields(selected_layers);
  if (!check_clipping_references(durable_layers, error)) return {};
  const auto has_scoped_light = photo_layer_style_light::has_linked_lighting_in_scope(
      durable_layers);
  const auto has_non_default_light = std::abs(
      document_layer_style_light_angle -
      photo_layer_style_light::kDefaultAngleDegrees) >= 1e-9;
  const bool has_text_dimensions =
      photo_text_dimensions_present_in_layers(durable_layers);
  const auto version = has_text_dimensions
      ? kTextDimensionVersion
      : has_scoped_light || has_non_default_light
      ? kDocumentLayerStyleLightVersion : kVersion;
  QVariantMap result{
      {QStringLiteral("schema"), QString::fromLatin1(kSchema)},
      {QStringLiteral("version"), version},
      {QStringLiteral("contentId"), new_id()},
      {QStringLiteral("revision"), 1},
      {QStringLiteral("canvasWidth"), canvas_width},
      {QStringLiteral("canvasHeight"), canvas_height},
      {QStringLiteral("layers"), durable_layers},
      {QStringLiteral("maskStack"), mask_stack}};
  if (version == kDocumentLayerStyleLightVersion ||
      (version == kTextDimensionVersion &&
       (has_scoped_light || has_non_default_light))) {
    result.insert(QStringLiteral("documentLayerStyleLightAngle"),
                  document_layer_style_light_angle);
  }
  if (!validate_document(result, error)) return {};
  return result;
}

bool validate_document(const QVariantMap& document, QString* error) {
  clear_error(error);
  ValidationContext context;
  if (!validate_document_recursive(document, &context, 1, error)) return false;
  return true;
}

QByteArray serialize_document(const QVariantMap& document, QString* error) {
  clear_error(error);
  if (!validate_document(document, error)) return {};
  QString normalization_error;
  const auto encoded = normalize_serialized_payloads(
      document, true, &normalization_error).toMap();
  if (!normalization_error.isEmpty()) {
    fail(error, normalization_error);
    return {};
  }
  const auto bytes = QJsonDocument::fromVariant(encoded).toJson(QJsonDocument::Compact);
  if (bytes.isEmpty() || bytes.size() > kMaximumSerializedBytes) {
    fail(error, QStringLiteral("Smart Object document exceeds the 64 MiB serialized-content limit."));
    return {};
  }
  return bytes;
}

QVariantMap deserialize_document(const QByteArray& bytes, QString* error) {
  clear_error(error);
  if (bytes.isEmpty() || bytes.size() > kMaximumSerializedBytes) {
    return fail_map(error, QStringLiteral("Smart Object document input is empty or exceeds 64 MiB."));
  }
  QJsonParseError parse_error;
  const auto parsed = QJsonDocument::fromJson(bytes, &parse_error);
  if (parse_error.error != QJsonParseError::NoError || !parsed.isObject()) {
    return fail_map(error, QStringLiteral("Smart Object document JSON is invalid."));
  }
  auto result = parsed.object().toVariantMap();
  QString normalization_error;
  result = normalize_serialized_payloads(result, false,
                                         &normalization_error).toMap();
  if (!normalization_error.isEmpty()) {
    return fail_map(error, normalization_error);
  }
  if (!validate_document(result, error)) return {};
  return result;
}

QVariantList create_component(const QVariantList& layer_stack,
                              const QVariantList& mask_stack,
                              const QStringList& selected_layer_ids,
                              int canvas_width,
                              int canvas_height,
                              QString* error,
                              double document_layer_style_light_angle) {
  clear_error(error);
  if (!bounded_canvas(canvas_width, canvas_height, error)) return layer_stack;
  if (!photo_layer_style_light::valid_angle(
          document_layer_style_light_angle)) {
    return fail_list(layer_stack, error, QStringLiteral(
        "The document layer style light angle is outside the supported range."));
  }
  if (selected_layer_ids.size() != 1) {
    return fail_list(layer_stack, error, QStringLiteral(
        "Convert one layer or group at a time in the first Smart Object component slice."));
  }
  QHash<QString, QVariantMap> records;
  QHash<QString, bool> parent_locked;
  QSet<QString> selected;
  if (!validate_selection(layer_stack, selected_layer_ids, &records,
                          &parent_locked, &selected, error)) return layer_stack;
  const auto selected_id = selected_layer_ids.constFirst().trimmed();
  const auto source_layer = records.value(selected_id);
  if (!source_layer.value(QStringLiteral("parentId")).toString().trimmed().isEmpty()) {
    return fail_list(layer_stack, error, QStringLiteral(
        "Convert the containing group so the component keeps its parent hierarchy and transforms."));
  }
  const auto document = document_from_layers(layer_stack, mask_stack,
      selected_layer_ids, canvas_width, canvas_height, error,
      document_layer_style_light_angle);
  if (document.isEmpty()) return layer_stack;
  if (!has_renderable_content(document.value(QStringLiteral("layers")).toList())) {
    return fail_list(layer_stack, error, QStringLiteral(
        "A component needs editable visible content; an adjustment-only subtree depends on its parent backdrop."));
  }
  QVariantMap instance{
      {QStringLiteral("id"), selected_id},
      {QStringLiteral("name"), source_layer.value(QStringLiteral("name"), QStringLiteral("Smart Object"))},
      {QStringLiteral("kind"), QStringLiteral("group")},
      {QStringLiteral("parentId"), source_layer.value(QStringLiteral("parentId"))},
      {QStringLiteral("visible"), source_layer.value(QStringLiteral("visible"), true)},
      {QStringLiteral("enabled"), source_layer.value(QStringLiteral("enabled"), true)},
      {QStringLiteral("locked"), source_layer.value(QStringLiteral("locked"), false)},
      {QStringLiteral("opacity"), 1.0},
      {QStringLiteral("blendMode"), QStringLiteral("normal")},
      {QStringLiteral("x"), 0.0},
      {QStringLiteral("y"), 0.0},
      {QStringLiteral("canvasWidth"), canvas_width},
      {QStringLiteral("canvasHeight"), canvas_height},
      {QStringLiteral("pixelWidth"), canvas_width},
      {QStringLiteral("pixelHeight"), canvas_height},
      {QString::fromLatin1(kInstanceIdField), new_id()},
      {QString::fromLatin1(kDocumentField), document}};
  auto descendants = selection_subtree_ids(records, selected);
  descendants.remove(selected_id);
  bool replaced = false;
  const auto result = replace_and_remove_subtree(
      layer_stack, selected_id, descendants, instance, &replaced);
  if (!replaced) return fail_list(layer_stack, error, QStringLiteral("Selected layer disappeared during component conversion."));
  return result;
}

QVariantMap duplicate_instance(const QVariantMap& instance,
                               const QString& new_layer_id,
                               QString* error) {
  clear_error(error);
  const auto id = new_layer_id.trimmed();
  const auto document = instance.value(QString::fromLatin1(kDocumentField)).toMap();
  if (id.isEmpty() || id == instance.value(QStringLiteral("id")).toString().trimmed()) {
    return fail_map(error, QStringLiteral("A duplicate component needs a distinct nonempty layer ID."));
  }
  if (document.isEmpty() || !validate_document(document, error)) {
    if (error != nullptr && error->isEmpty()) *error = QStringLiteral("Layer is not a Smart Object component.");
    return {};
  }
  if (!validate_instance_identity(instance, error)) return {};
  auto result = instance;
  result.insert(QStringLiteral("id"), id);
  result.insert(QString::fromLatin1(kInstanceIdField), new_id());
  return result;
}

QVariantList make_independent(const QVariantList& layer_stack,
                              const QStringList& selected_layer_ids,
                              QString* error) {
  clear_error(error);
  QHash<QString, QVariantMap> records;
  QHash<QString, bool> parent_locked;
  if (selected_layer_ids.isEmpty()) {
    return fail_list(layer_stack, error, QStringLiteral("Select at least one component instance."));
  }
  if (!validate_stack(layer_stack, &records, &parent_locked, error)) return layer_stack;
  QHash<QString, QVariantMap> replacements;
  QSet<QString> selected;
  for (const auto& raw_id : selected_layer_ids) {
    const auto id = raw_id.trimmed();
    if (id.isEmpty() || selected.contains(id) || !records.contains(id)) {
      return fail_list(layer_stack, error, QStringLiteral("Selected layer IDs must be unique and exist."));
    }
    const auto layer = records.value(id);
    auto document = layer.value(QString::fromLatin1(kDocumentField)).toMap();
    if (document.isEmpty() || !validate_document(document, error)) {
      if (error != nullptr && error->isEmpty()) *error = QStringLiteral("Selected layer is not an editable component.");
      return layer_stack;
    }
    if (!validate_instance_identity(layer, error)) return layer_stack;
    if (parent_locked.value(id) || layer.value(QStringLiteral("locked")).toBool()) {
      return fail_list(layer_stack, error, QStringLiteral("A selected component or parent group is locked."));
    }
    selected.insert(id);
    document.insert(QStringLiteral("contentId"), new_id());
    auto candidate = with_document(layer, document);
    replacements.insert(id, candidate);
  }
  return mutate_documents(layer_stack, replacements);
}

QVariantList replace_shared_document(const QVariantList& layer_stack,
                                    const QStringList& selected_layer_ids,
                                    const QVariantMap& replacement_document,
                                    QString* error) {
  clear_error(error);
  auto durable_replacement_document = replacement_document;
  if (durable_replacement_document.value(QStringLiteral("layers")).metaType().id() ==
      QMetaType::QVariantList) {
    durable_replacement_document.insert(QStringLiteral("layers"),
        strip_transient_layer_view_fields(
            durable_replacement_document.value(QStringLiteral("layers")).toList()));
  }
  if (!validate_document(durable_replacement_document, error)) return layer_stack;
  QHash<QString, QVariantMap> records;
  QHash<QString, bool> parent_locked;
  QHash<QString, SharedGroup> groups;
  if (!collect_shared_groups(layer_stack, selected_layer_ids, &records,
                             &parent_locked, &groups, error)) return layer_stack;
  for (auto it = groups.cbegin(); it != groups.cend(); ++it) {
    for (const auto& instance_id : it->instance_ids) {
      const auto layer = records.value(instance_id);
      if (parent_locked.value(instance_id) || layer.value(QStringLiteral("locked")).toBool()) {
        return fail_list(layer_stack, error, QStringLiteral(
            "Replace Contents would change a locked shared component or parent group."));
      }
    }
  }
  QHash<QString, QVariantMap> replacements;
  for (auto it = groups.cbegin(); it != groups.cend(); ++it) {
    const auto old_revision = it->document.value(QStringLiteral("revision")).toInt();
    const auto requested_revision = durable_replacement_document.value(QStringLiteral("revision")).toInt();
    const auto base_revision = std::max(old_revision, requested_revision);
    if (base_revision == std::numeric_limits<int>::max()) {
      return fail_list(layer_stack, error, QStringLiteral("Smart Object content revision limit reached."));
    }
    auto updated = durable_replacement_document;
    updated.insert(QStringLiteral("contentId"), it.key());
    updated.insert(QStringLiteral("revision"), base_revision + 1);
    if (!validate_document(updated, error)) return layer_stack;
    for (const auto& instance_id : it->instance_ids) {
      replacements.insert(instance_id, with_document(records.value(instance_id), updated));
    }
  }
  return mutate_documents(layer_stack, replacements);
}

QVariantList apply_shared_document(const QVariantList& layer_stack,
                                   const QStringList& selected_layer_ids,
                                   const QVariantMap& edited_document,
                                   int expected_revision,
                                   QString* error) {
  clear_error(error);
  auto durable_edited_document = edited_document;
  if (durable_edited_document.value(QStringLiteral("layers")).metaType().id() ==
      QMetaType::QVariantList) {
    durable_edited_document.insert(QStringLiteral("layers"),
        strip_transient_layer_view_fields(
            durable_edited_document.value(QStringLiteral("layers")).toList()));
  }
  if (!validate_document(durable_edited_document, error)) return layer_stack;
  QHash<QString, QVariantMap> records;
  QHash<QString, bool> parent_locked;
  QHash<QString, SharedGroup> groups;
  if (!collect_shared_groups(layer_stack, selected_layer_ids, &records,
                             &parent_locked, &groups, error)) return layer_stack;
  for (auto it = groups.cbegin(); it != groups.cend(); ++it) {
    const auto old_revision = it->document.value(QStringLiteral("revision")).toInt();
    if (old_revision != expected_revision) {
      return fail_list(layer_stack, error, QStringLiteral(
          "Smart Object content changed while its nested editor was open; reopen the component before applying."));
    }
    if (durable_edited_document.value(QStringLiteral("contentId")).toString() != it.key()) {
      return fail_list(layer_stack, error, QStringLiteral(
          "Edited Smart Object contentId does not match the selected shared component."));
    }
    if (old_revision == std::numeric_limits<int>::max()) {
      return fail_list(layer_stack, error, QStringLiteral("Smart Object content revision limit reached."));
    }
    for (const auto& instance_id : it->instance_ids) {
      const auto layer = records.value(instance_id);
      if (parent_locked.value(instance_id) || layer.value(QStringLiteral("locked")).toBool()) {
        return fail_list(layer_stack, error, QStringLiteral(
            "Editing shared content would change a locked component or parent group."));
      }
    }
  }
  QHash<QString, QVariantMap> replacements;
  for (auto it = groups.cbegin(); it != groups.cend(); ++it) {
    auto updated = durable_edited_document;
    updated.insert(QStringLiteral("contentId"), it.key());
    updated.insert(QStringLiteral("revision"), expected_revision + 1);
    if (!validate_document(updated, error)) return layer_stack;
    for (const auto& instance_id : it->instance_ids) {
      replacements.insert(instance_id, with_document(records.value(instance_id), updated));
    }
  }
  return mutate_documents(layer_stack, replacements);
}

}  // namespace photo_editor::desktop::smart_object::nested_document
