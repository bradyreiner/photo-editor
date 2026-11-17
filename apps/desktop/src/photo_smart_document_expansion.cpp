#include "photo_smart_document_expansion.h"
#include "photo_smart_document.h"
#include "photo_layer_effect_codec.h"
#include "photo_layer_style_light.h"
#include "photo_render_profile_for_tests.h"
#include "photo_editor/pipeline/layer_styles.h"
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSet>

namespace photo_editor::desktop {
namespace {
QVariant namespace_mask_references(const QVariant& value, const QString& key, const QString& prefix) {
  static const QSet<QString> single{"selectionMaskSetId", "selectionMaskSetName", "maskRef", "mask_ref", "effectMaskRef", "maskSetId", "maskSetName", "maskGroupId", "outputId", "repairId"};
  static const QSet<QString> multiple{"selectionMaskSetIds", "selectionMaskSetNames", "selectionIntersectionMaskSetIds", "selectionIntersectionMaskSetNames"};
  if (key == "effectStack" && (value.metaType() == QMetaType::fromType<QByteArray>() || value.metaType() == QMetaType::fromType<QString>())) {
    const bool was_bytes = value.metaType() == QMetaType::fromType<QByteArray>();
    const auto serialized = was_bytes
        ? QString::fromUtf8(value.toByteArray()) : value.toString();
    const QVariantMap layer{{QStringLiteral("effectStack"), serialized}};
    QString error;
    auto effects = decode_photo_layer_effects(layer, &error);
    if (effects) {
      for (auto& effect : *effects)
        if (!effect.mask_ref.empty()) effect.mask_ref = prefix.toStdString() + effect.mask_ref;
      const auto encoded = encode_photo_layer_effects(*effects, &error);
      if (encoded)
        return was_bytes ? QVariant(encoded->toUtf8()) : QVariant(*encoded);
    }

    // Older component recipes stored effectStack as a JSON object/map. Keep
    // expanding those documents while preferring the current engine envelope
    // above. Their known mask-reference fields use the same recursive mapping
    // as the surrounding QVariant layer fields.
    QJsonParseError parse_error;
    const auto legacy_document = QJsonDocument::fromJson(serialized.toUtf8(), &parse_error);
    if (parse_error.error == QJsonParseError::NoError &&
        (legacy_document.isObject() || legacy_document.isArray())) {
      const auto namespaced = namespace_mask_references(
          legacy_document.toVariant(), {}, prefix);
      const auto namespaced_document = QJsonDocument::fromVariant(namespaced);
      const auto encoded = namespaced_document.toJson(QJsonDocument::Compact);
      return was_bytes ? QVariant(encoded) : QVariant(QString::fromUtf8(encoded));
    }
  }
  if (key == "layerStyleStack" &&
      value.metaType() == QMetaType::fromType<QString>()) {
    std::string error;
    auto styles = photo_editor::pipeline::deserialize_layer_style_stack(
        value.toString().toStdString(), &error);
    if (styles) {
      for (auto& operation : styles->operations)
        if (!operation.mask_ref.empty())
          operation.mask_ref = prefix.toStdString() + operation.mask_ref;
      const auto encoded = photo_editor::pipeline::serialize_layer_style_stack(
          *styles, &error);
      if (encoded) return QString::fromStdString(*encoded);
    }
  }
  if (single.contains(key)) return value.toString().isEmpty() ? value : QVariant(prefix + value.toString());
  if (multiple.contains(key)) {
    QVariantList result;
    for (const auto& item : value.toList()) result.append(item.toString().isEmpty() ? item : QVariant(prefix + item.toString()));
    if (value.metaType() == QMetaType::fromType<QStringList>()) {
      QStringList strings; for (const auto& item : value.toStringList()) strings.append(item.isEmpty() ? item : prefix + item);
      return strings;
    }
    return result;
  }
  if (value.metaType() == QMetaType::fromType<QVariantMap>()) {
    auto map = value.toMap();
    for (auto it = map.begin(); it != map.end(); ++it)
      if (it.key() != "smartObjectDocument") it.value() = namespace_mask_references(it.value(), it.key(), prefix);
    return map;
  }
  if (value.metaType() == QMetaType::fromType<QVariantList>()) {
    QVariantList list; for (const auto& item : value.toList()) list.append(namespace_mask_references(item, {}, prefix)); return list;
  }
  return value;
}
bool flatten_layers(const QVariantList& input, const QString& prefix, const QString& inherited_parent,
    QVariantList* output, QString* error, int depth = 0) {
  if (depth > 64 || output->size() > 32768) { *error = "Component layer nesting exceeds the supported budget."; return false; }
  for (const auto& item : input) {
    auto authored = item.toMap();
    const auto children = authored.take("children").toList(), nested_layers = authored.take("layers").toList();
    auto layer = prefix.isEmpty() ? authored : namespace_mask_references(authored, {}, prefix).toMap();
    const auto id = layer.value("id").toString(), parent = layer.value("parentId").toString();
    if (!prefix.isEmpty() && id.isEmpty()) { *error = "Component child identity is missing."; return false; }
    layer.insert("id", prefix + id);
    if (!parent.isEmpty()) layer.insert("parentId", prefix + parent);
    else if (!inherited_parent.isEmpty()) layer.insert("parentId", inherited_parent);
    output->append(layer);
    if (!flatten_layers(children, prefix, prefix + id, output, error, depth + 1) ||
        !flatten_layers(nested_layers, prefix, prefix + id, output, error, depth + 1)) return false;
  }
  return true;
}
bool expand(const QVariantList& input, ExpandedSmartDocuments* result, int depth,
            QSet<QString> ancestors, double document_light_angle) {
  if (depth > smart_object::nested_document::kMaximumDocumentDepth) { result->error = "Component nesting exceeds the supported depth."; return false; }
  for (const auto& item : input) {
    auto layer = item.toMap(); const auto document = layer.value("smartObjectDocument").toMap();
    if (photo_layer_style_light::is_linked(layer)) {
      layer.insert(QString::fromLatin1(
          photo_layer_style_light::kExpandedDocumentAngleField),
          document_light_angle);
    }
    if (!layer.contains("smartObjectDocument")) { result->layers.append(layer); continue; }
    QString error;
    if (!smart_object::nested_document::validate_document(document, &error)) { result->error = error; return false; }
    const auto content_id = document.value("contentId").toString();
    if (ancestors.contains(content_id)) { result->error = "Component documents contain a cycle."; return false; }
    auto branch = ancestors; branch.insert(content_id);
    const auto instance_id = layer.value("id").toString();
    if (instance_id.isEmpty()) { result->error = "Component instance identity is missing."; return false; }
    const auto prefix = instance_id + "::";
    layer.remove("smartObjectDocument"); layer.insert("kind", "group");
    result->layers.append(layer);
    QVariantList children;
    if (!flatten_layers(document.value("layers").toList(), prefix, instance_id, &children, &result->error)) return false;
    for (auto& child : children) { auto scoped = child.toMap(); scoped.insert("_smartDocumentRootId", instance_id); child = scoped; }
    QVariantList component_retouch_masks;
    for (const auto& mask_value : document.value("maskStack").toList()) {
      auto mask = namespace_mask_references(mask_value, {}, prefix).toMap();
      if (mask.contains("id")) mask.insert("id", prefix + mask.value("id").toString());
      if (!mask.value("parentId").toString().isEmpty()) mask.insert("parentId", prefix + mask.value("parentId").toString());
      if (mask.value("toolRole").toString().trimmed().toLower() == "retouch" &&
          mask.value("componentPostCompositeRetouchVersion").toInt() == 1) {
        component_retouch_masks.append(mask);
      }
      result->masks.append(mask);
    }
    if (!component_retouch_masks.isEmpty()) {
      layer.insert("_smartDocumentPostCompositeRetouchMasks", component_retouch_masks);
      result->layers.last() = layer;
    }
    const auto nested_light_angle = document.value(
        QStringLiteral("documentLayerStyleLightAngle"),
        photo_layer_style_light::kDefaultAngleDegrees)
        .toDouble();
    if (!expand(children, result, depth + 1, branch, nested_light_angle)) return false;
    if (result->layers.size() > 32768 || result->masks.size() > 65536) { result->error = "Expanded component content exceeds the supported budget."; return false; }
  }
  return true;
}
}
ExpandedSmartDocuments expand_smart_documents(
    const QVariantList& layers, const QVariantList& masks,
    double document_layer_style_light_angle) {
  auto& profile = photo_render_profile_for_tests;
  if (profile.enabled) ++profile.expand_smart_documents_calls;
  PhotoRenderProfileTimerForTests profile_timer(
      profile.enabled ? &profile.expand_smart_documents_ns : nullptr);
  ExpandedSmartDocuments result; result.masks = masks;
  QVariantList flattened;
  if (!flatten_layers(layers, {}, {}, &flattened, &result.error) ||
      !expand(flattened, &result, 0, {}, document_layer_style_light_angle))
    result.layers.clear();
  if (profile.enabled) {
    profile.expanded_smart_document_layers += result.layers.size();
    profile.expanded_smart_document_masks += result.masks.size();
  }
  return result;
}
}
