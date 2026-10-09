#pragma once
#include "edit_recipe_state.h"
#include <QClipboard>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeData>
#include <QSet>
#include <QImage>
#include <QPainter>
#include <QUuid>
#include <optional>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace photo_editor::desktop {
inline constexpr auto photo_layer_clipboard_mime = "application/x-looksy-photo-layers-v1";
inline constexpr qsizetype maximum_photo_layer_clipboard_bytes = 64 * 1024 * 1024;
struct PhotoLayerClipboard {
  EditRecipeState state;
  QSize canvas;
  QStringList selected_ids;
};
inline QByteArray encode_photo_layer_clipboard(const PhotoLayerClipboard& value) {
  const QJsonObject object{{QStringLiteral("schema"), QStringLiteral("looksy-photo-layers-v1")},
      {QStringLiteral("width"), value.canvas.width()}, {QStringLiteral("height"), value.canvas.height()},
      {QStringLiteral("selectedIds"), QJsonArray::fromStringList(value.selected_ids)},
      {QStringLiteral("recipe"), QJsonDocument::fromJson(serialize_edit_recipe_json(value.state).toUtf8()).object()}};
  const auto bytes = QJsonDocument(object).toJson(QJsonDocument::Compact);
  return bytes.size() <= maximum_photo_layer_clipboard_bytes ? bytes : QByteArray{};
}
inline std::optional<PhotoLayerClipboard> decode_photo_layer_clipboard(const QByteArray& bytes) {
  if (bytes.isEmpty() || bytes.size() > maximum_photo_layer_clipboard_bytes) return std::nullopt;
  QJsonParseError error;
  const auto document = QJsonDocument::fromJson(bytes, &error);
  const auto object = document.object();
  if (error.error != QJsonParseError::NoError || object.value(QStringLiteral("schema")) != QStringLiteral("looksy-photo-layers-v1"))
    return std::nullopt;
  const auto recipe = QString::fromUtf8(QJsonDocument(object.value(QStringLiteral("recipe")).toObject()).toJson(QJsonDocument::Compact));
  if (!validate_edit_recipe_json(recipe).valid) return std::nullopt;
  PhotoLayerClipboard value;
  value.state = parse_edit_recipe_json(recipe);
  value.canvas = QSize(object.value(QStringLiteral("width")).toInt(), object.value(QStringLiteral("height")).toInt());
  if (value.canvas.isEmpty() || value.canvas.width() > 1000000 || value.canvas.height() > 1000000 ||
      value.state.layers.isEmpty() || value.state.layers.size() > 1024 || value.state.mask_stack.size() > 4096) return std::nullopt;
  QSet<QString> ids;
  for (const auto& layer : value.state.layers) {
    const auto map = layer.toMap();
    const auto id = map.value(QStringLiteral("id")).toString();
    if (id.isEmpty() || ids.contains(id) || map.value(QStringLiteral("baseImageEdit")).toBool()) return std::nullopt;
    ids.insert(id);
  }
  for (const auto& id : object.value(QStringLiteral("selectedIds")).toArray()) {
    if (!ids.contains(id.toString())) return std::nullopt;
    if (!value.selected_ids.contains(id.toString())) value.selected_ids.push_back(id.toString());
  }
  if (value.selected_ids.isEmpty()) return std::nullopt;
  return value;
}
inline QByteArray current_photo_layer_clipboard() {
  if (!qobject_cast<QGuiApplication*>(QCoreApplication::instance())) return {};
  const auto* mime = QGuiApplication::clipboard()->mimeData();
  return mime && mime->hasFormat(photo_layer_clipboard_mime) ? mime->data(photo_layer_clipboard_mime) : QByteArray{};
}
inline void collect_photo_clipboard_ids(const QVariant& value, QHash<QString, QString>& ids) {
  if (value.metaType().id() == QMetaType::QVariantMap) {
    const auto map = value.toMap();
    for (auto it = map.cbegin(); it != map.cend(); ++it) {
      if ((it.key() == QStringLiteral("id") || it.key() == QStringLiteral("maskSetId")) && !it->toString().isEmpty() &&
          !ids.contains(it->toString())) ids.insert(it->toString(), QUuid::createUuid().toString(QUuid::WithoutBraces));
      collect_photo_clipboard_ids(it.value(), ids);
    }
  } else if (value.metaType().id() == QMetaType::QVariantList) {
    for (const auto& child : value.toList()) collect_photo_clipboard_ids(child, ids);
  }
}
inline QVariant remap_photo_clipboard_ids(const QVariant& value, const QHash<QString, QString>& ids, const QString& key = {}) {
  if (value.metaType().id() == QMetaType::QVariantMap) {
    auto map = value.toMap();
    for (auto it = map.begin(); it != map.end(); ++it) it.value() = remap_photo_clipboard_ids(it.value(), ids, it.key());
    return map;
  }
  if (value.metaType().id() == QMetaType::QVariantList) {
    auto list = value.toList();
    for (auto& item : list) item = remap_photo_clipboard_ids(item, ids, key);
    return list;
  }
  if (key.endsWith(QStringLiteral("Id"), Qt::CaseInsensitive) || key.endsWith(QStringLiteral("Ids"), Qt::CaseInsensitive) ||
      key.endsWith(QStringLiteral("Ref"), Qt::CaseInsensitive) || key == QStringLiteral("id")) {
    const auto old = value.toString();
    if (ids.contains(old)) return ids.value(old);
  }
  return value;
}

inline QVariantMap rebase_photo_clipboard_mask(QVariantMap mask, const QSize& source, const QSize& destination, int dx, int dy) {
  if (source == destination && dx == 0 && dy == 0) return mask;
  const double sx = static_cast<double>(source.width()) / destination.width();
  const double sy = static_cast<double>(source.height()) / destination.height();
  const auto translate_x = [&](double x) { return x * sx + static_cast<double>(dx) / destination.width(); };
  const auto translate_y = [&](double y) { return y * sy + static_cast<double>(dy) / destination.height(); };
  for (const auto* key : {"centerX", "cloneSourceX"}) if (mask.contains(key)) mask.insert(key, translate_x(mask.value(key).toDouble()));
  for (const auto* key : {"centerY", "cloneSourceY"}) if (mask.contains(key)) mask.insert(key, translate_y(mask.value(key).toDouble()));
  for (const auto* key : {"radiusX", "cloneOffsetX"}) if (mask.contains(key)) mask.insert(key, mask.value(key).toDouble() * sx);
  for (const auto* key : {"radiusY", "cloneOffsetY"}) if (mask.contains(key)) mask.insert(key, mask.value(key).toDouble() * sy);
  const auto points = [&](QVariantList list) {
    for (auto& value : list) {
      auto point = value.toMap();
      if (point.contains("x")) point.insert("x", translate_x(point.value("x").toDouble()));
      if (point.contains("y")) point.insert("y", translate_y(point.value("y").toDouble()));
      if (point.contains("autoMaskAnchorX")) point.insert("autoMaskAnchorX", translate_x(point.value("autoMaskAnchorX").toDouble()));
      if (point.contains("autoMaskAnchorY")) point.insert("autoMaskAnchorY", translate_y(point.value("autoMaskAnchorY").toDouble()));
      if (point.contains("size")) point.insert("size", point.value("size").toDouble() * std::min(sx, sy));
      value = point;
    }
    return list;
  };
  for (const auto* key : {"polygonPoints", "brushPoints"}) if (mask.contains(key)) mask.insert(key, points(mask.value(key).toList()));
  auto strokes = mask.value("brushStrokes").toList();
  for (auto& value : strokes) { auto stroke = value.toMap(); stroke.insert("samples", points(stroke.value("samples").toList())); value = stroke; }
  if (mask.contains("brushStrokes")) mask.insert("brushStrokes", strokes);
  if (mask.contains("brushSize")) mask.insert("brushSize", mask.value("brushSize").toDouble() * std::min(sx, sy));
  for (const auto* prefix : {"segmentation", "depth"}) {
    const auto data_key = QString::fromLatin1(prefix) + "Data";
    const auto width_key = QString::fromLatin1(prefix) + "Width";
    const auto height_key = QString::fromLatin1(prefix) + "Height";
    const auto data = mask.value(data_key).toByteArray();
    const auto width = mask.value(width_key).toInt(), height = mask.value(height_key).toInt();
    if (width <= 0 || height <= 0 || data.size() != static_cast<qint64>(width) * height) continue;
    const auto requested_width = std::max(1.0, width / sx), requested_height = std::max(1.0, height / sy);
    const auto reduction = std::min({1.0, 8192.0 / requested_width, 8192.0 / requested_height,
        std::sqrt(32.0 * 1024 * 1024 / (requested_width * requested_height))});
    const QSize size(std::max(1, qRound(requested_width * reduction)), std::max(1, qRound(requested_height * reduction)));
    const QImage original(reinterpret_cast<const uchar*>(data.constData()), width, height, width, QImage::Format_Grayscale8);
    // Draw into the bounded destination directly. A large source pasted into
    // a tiny document must not allocate its entire off-canvas footprint.
    QImage raster(size, QImage::Format_Grayscale8);
    raster.fill(0);
    QPainter painter(&raster);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.drawImage(QRectF(static_cast<double>(dx) * size.width() / destination.width(),
        static_cast<double>(dy) * size.height() / destination.height(),
        static_cast<double>(source.width()) * size.width() / destination.width(),
        static_cast<double>(source.height()) * size.height() / destination.height()), original);
    painter.end();
    QByteArray shifted(size.width() * size.height(), char(0));
    for (int y = 0; y < size.height(); ++y)
      std::memcpy(shifted.data() + y * size.width(), raster.constScanLine(y), static_cast<std::size_t>(size.width()));
    mask.insert(data_key, shifted); mask.insert(width_key, size.width()); mask.insert(height_key, size.height());
  }
  return mask;
}
}  // namespace photo_editor::desktop
