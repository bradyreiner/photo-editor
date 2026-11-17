#include "edit_recipe_state.h"
#include "photo_layer_style_light.h"
#include "photo_smart_document_payload_codec.h"

#include <array>

namespace photo_editor::desktop::detail {

bool photo_layers_have_styles(const QVariantList& layers) {
  for (const auto& value : layers) {
    const auto layer = value.toMap();
    if (layer.contains("layerStyleStack")) return true;
    for (const auto* key : {"children", "layers"})
      if (photo_layers_have_styles(layer.value(QString::fromLatin1(key)).toList())) return true;
    if (photo_layers_have_styles(layer.value("smartObjectDocument").toMap().value("layers").toList())) return true;
  }
  return false;
}

bool photo_layers_have_linked_style_lighting(const QVariantList& layers) {
  return photo_layer_style_light::has_linked_lighting(layers);
}

bool recipe_requires_document_layer_style_light(const EditRecipeState& state) {
  return photo_layer_style_light::requires_document_light_capability(state);
}

bool photo_layers_have_component_objects(const QVariantList& layers) {
  for (const auto& value : layers) {
    const auto layer = value.toMap();
    if (layer.contains("smartObject") || layer.contains("smartObjectDocument")) return true;
    for (const auto* key : {"children", "layers"})
      if (photo_layers_have_component_objects(layer.value(QString::fromLatin1(key)).toList())) return true;
  }
  return false;
}

bool photo_layers_have_components(const QVariantList& layers) {
  for (const auto& value : layers) {
    const auto layer = value.toMap(); if (layer.contains("smartObjectDocument")) return true;
    for (const auto* key : {"children", "layers"}) if (photo_layers_have_components(layer.value(QString::fromLatin1(key)).toList())) return true;
  }
  return false;
}

bool photo_layers_have_paint_tiles(const QVariantList& layers) {
  for (const auto& value : layers) {
    const auto layer = value.toMap();
    if (layer.contains(QStringLiteral("paintTiles"))) return true;
    for (const auto* key : {"children", "layers"})
      if (photo_layers_have_paint_tiles(layer.value(QString::fromLatin1(key)).toList())) return true;
  }
  return false;
}

bool photo_layers_have_typography(const QVariantList& layers) {
  for (const auto& value : layers) {
    const auto layer = value.toMap();
    if (layer.contains(QStringLiteral("textTypography"))) return true;
    for (const auto* key : {"children", "layers"})
      if (photo_layers_have_typography(layer.value(QString::fromLatin1(key)).toList())) return true;
  }
  return false;
}

bool photo_layers_have_text_dimensions(const QVariantList& layers) {
  for (const auto& value : layers) {
    const auto layer = value.toMap();
    if (layer.contains(QStringLiteral("textDimension"))) return true;
    for (const auto* key : {"children", "layers"})
      if (photo_layers_have_text_dimensions(
              layer.value(QString::fromLatin1(key)).toList()))
        return true;
    const auto nested = layer.value(QStringLiteral("smartObjectDocument")).toMap();
    if (photo_layers_have_text_dimensions(
            nested.value(QStringLiteral("layers")).toList()))
      return true;
  }
  return false;
}

bool photo_layers_have_effect_stacks(const QVariantList& layers) {
  for (const auto& value : layers) {
    const auto layer = value.toMap();
    if (layer.contains(QStringLiteral("effectStack"))) return true;
    for (const auto* key : {"children", "layers"}) {
      if (photo_layers_have_effect_stacks(layer.value(QString::fromLatin1(key)).toList())) return true;
    }
  }
  return false;
}

namespace {

QVariantMap normalize_photo_layer_payloads(QVariantMap layer) {
  if (layer.contains("smartObjectDocument")) layer.insert("smartObjectDocument", decode_component_payloads(layer.value("smartObjectDocument")));
  for (const auto* key : {"children", "layers"}) {
    if (!layer.contains(QString::fromLatin1(key))) continue;
    auto children = layer.value(QString::fromLatin1(key)).toList();
    for (auto& child : children) {
      if (child.canConvert<QVariantMap>())
        child = normalize_photo_layer_payloads(child.toMap());
    }
    layer.insert(QString::fromLatin1(key), children);
  }
  constexpr std::array<const char*, 2> kBytePayloadKeys{
      "maskData", "pixelData"};
  for (const auto* raw_key : kBytePayloadKeys) {
    const auto key = QString::fromLatin1(raw_key);
    const auto value = layer.value(key);
    if (!value.isValid() ||
        value.metaType().id() == QMetaType::QByteArray) {
      continue;
    }
    layer.insert(
        key,
        QByteArray::fromBase64(value.toString().toLatin1()));
  }
  return layer;
}

QVariantMap encode_photo_layer_payloads(QVariantMap layer) {
  if (layer.contains("smartObjectDocument")) layer.insert("smartObjectDocument", encode_component_payloads(layer.value("smartObjectDocument")));
  for (auto iterator = layer.begin(); iterator != layer.end();) {
    if (iterator.key().startsWith(
            QStringLiteral("thumbnail"), Qt::CaseInsensitive)) {
      iterator = layer.erase(iterator);
      continue;
    }
    ++iterator;
  }
  for (const auto& child_key : {
           QStringLiteral("children"),
           QStringLiteral("layers"),
       }) {
    if (!layer.contains(child_key)) {
      continue;
    }
    QVariantList encoded_children;
    const auto children = layer.value(child_key).toList();
    encoded_children.reserve(children.size());
    for (const auto& child : children) {
      encoded_children.append(
          child.canConvert<QVariantMap>()
              ? encode_photo_layer_payloads(child.toMap())
              : child);
    }
    layer.insert(child_key, encoded_children);
  }
  constexpr std::array<const char*, 2> kBytePayloadKeys{
      "maskData", "pixelData"};
  for (const auto* raw_key : kBytePayloadKeys) {
    const auto key = QString::fromLatin1(raw_key);
    const auto value = layer.value(key);
    if (!value.isValid()) {
      continue;
    }
    const auto data =
        value.metaType().id() == QMetaType::QByteArray
            ? value.toByteArray()
            : QByteArray::fromBase64(value.toString().toLatin1());
    layer.insert(key, QString::fromLatin1(data.toBase64()));
  }
  return layer;
}

}  // namespace

QVariantList parse_photo_layers(const QJsonArray& layers) {
  QVariantList parsed_layers;
  parsed_layers.reserve(layers.size());
  for (const auto& layer_value : layers) {
    if (!layer_value.isObject()) {
      parsed_layers.append(layer_value.toVariant());
      continue;
    }
    parsed_layers.append(
        normalize_photo_layer_payloads(
            layer_value.toObject().toVariantMap()));
  }
  return parsed_layers;
}

QVariantList serialize_photo_layers(const QVariantList& layers) {
  QVariantList serialized_layers;
  serialized_layers.reserve(layers.size());
  for (const auto& layer_value : layers) {
    if (!layer_value.canConvert<QVariantMap>()) {
      serialized_layers.append(layer_value);
      continue;
    }
    serialized_layers.append(
        encode_photo_layer_payloads(layer_value.toMap()));
  }
  return serialized_layers;
}

}  // namespace photo_editor::desktop::detail
