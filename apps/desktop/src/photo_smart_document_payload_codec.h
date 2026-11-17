#pragma once
#include <QByteArray>
#include <QVariant>
#include <QVariantMap>
#include <QVariantList>
namespace photo_editor::desktop {
// The legacy recipe JSON codec otherwise coerces nested QByteArray into text.
// Tagged byte values keep component pixels/masks lossless without depending on
// the desktop component service from the small recipe-only test targets.
inline QVariant encode_component_payloads(const QVariant& value) {
  if (value.metaType() == QMetaType::fromType<QByteArray>())
    return QVariantMap{{"$looksyByteArrayV1", QString::fromLatin1(value.toByteArray().toBase64())}};
  if (value.metaType() == QMetaType::fromType<QVariantMap>()) {
    auto map = value.toMap(); for (auto it = map.begin(); it != map.end(); ++it) it.value() = encode_component_payloads(it.value()); return map;
  }
  if (value.metaType() == QMetaType::fromType<QVariantList>()) {
    QVariantList list; for (const auto& item : value.toList()) list.append(encode_component_payloads(item)); return list;
  }
  return value;
}
inline QVariant decode_component_payloads(const QVariant& value) {
  if (value.metaType() == QMetaType::fromType<QVariantMap>()) {
    auto map = value.toMap();
    if (map.size() == 1 && map.contains("$looksyByteArrayV1") && map.value("$looksyByteArrayV1").metaType() == QMetaType::fromType<QString>()) {
      const auto encoded = map.value("$looksyByteArrayV1").toString().toLatin1(); const auto decoded = QByteArray::fromBase64(encoded);
      if (decoded.toBase64() == encoded) return decoded;
      return value;
    }
    for (auto it = map.begin(); it != map.end(); ++it) it.value() = decode_component_payloads(it.value()); return map;
  }
  if (value.metaType() == QMetaType::fromType<QVariantList>()) {
    QVariantList list; for (const auto& item : value.toList()) list.append(decode_component_payloads(item)); return list;
  }
  return value;
}
}
