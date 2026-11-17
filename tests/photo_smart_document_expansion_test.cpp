#include "photo_smart_document.h"
#include "photo_smart_document_expansion.h"
#include "photo_layer_effect_codec.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>

#include <iostream>

using photo_editor::desktop::ExpandedSmartDocuments;
using photo_editor::desktop::expand_smart_documents;
using photo_editor::desktop::smart_object::nested_document::kSchema;
using photo_editor::desktop::smart_object::nested_document::kVersion;
using photo_editor::desktop::smart_object::nested_document::validate_document;

namespace {

QString new_id() {
  return QUuid::createUuid().toString(QUuid::WithoutBraces).toLower();
}

bool check(bool condition, const char* message) {
  if (!condition) std::cerr << message << '\n';
  return condition;
}

QVariantMap find_layer(const QVariantList& layers, const QString& id) {
  for (const auto& value : layers) {
    const auto layer = value.toMap();
    if (layer.value(QStringLiteral("id")).toString() == id) return layer;
    for (const auto& key : {QStringLiteral("children"), QStringLiteral("layers")}) {
      const auto nested = find_layer(layer.value(key).toList(), id);
      if (!nested.isEmpty()) return nested;
    }
  }
  return {};
}

QVariantMap make_document(const QString& content_id,
                          const QVariantList& layers,
                          const QVariantList& masks = {}) {
  return QVariantMap{
      {QStringLiteral("schema"), QString::fromLatin1(kSchema)},
      {QStringLiteral("version"), kVersion},
      {QStringLiteral("contentId"), content_id},
      {QStringLiteral("revision"), 1},
      {QStringLiteral("canvasWidth"), 64},
      {QStringLiteral("canvasHeight"), 48},
      {QStringLiteral("layers"), layers},
      {QStringLiteral("maskStack"), masks}};
}

QVariantMap make_fixture_document() {
  const QVariantMap nested_document = make_document(new_id(),
      {QVariantMap{{QStringLiteral("id"), QStringLiteral("nested-fill")},
                   {QStringLiteral("kind"), QStringLiteral("fill")},
                   {QStringLiteral("fillColor"), QStringLiteral("#304050")}}});
  const QVariantList masks{
      QVariantMap{{QStringLiteral("id"), QStringLiteral("selection-mask-source")},
                  {QStringLiteral("maskSetId"), QStringLiteral("selection-set")},
                  {QStringLiteral("maskSetName"), QStringLiteral("Selection Mask")},
                  {QStringLiteral("outputId"), QStringLiteral("selection-mask-source")},
                  {QStringLiteral("reusableOutput"), true},
                  {QStringLiteral("kind"), QStringLiteral("brush")}},
      QVariantMap{{QStringLiteral("id"), QStringLiteral("effect-mask-source")},
                  {QStringLiteral("maskSetId"), QStringLiteral("effect-set")},
                  {QStringLiteral("maskSetName"), QStringLiteral("Effect Mask")},
                  {QStringLiteral("kind"), QStringLiteral("luminance")}}};
  const QVariantMap title{
      {QStringLiteral("id"), QStringLiteral("title")},
      {QStringLiteral("parentId"), QStringLiteral("inner-root")},
      {QStringLiteral("kind"), QStringLiteral("text")},
      {QStringLiteral("text"), QStringLiteral("Nested title")},
      {QStringLiteral("selectionMaskSetId"), QStringLiteral("selection-set")},
      {QStringLiteral("selectionMaskSetName"), QStringLiteral("Selection Mask")},
      {QStringLiteral("effectMaskRef"), QStringLiteral("effect-set")},
      {QStringLiteral("effectStack"), QStringLiteral("{\"version\":1,\"maskRef\":\"effect-set\"}")},
      {QStringLiteral("maskRef"), QStringLiteral("selection-set")}};
  photo_editor::pipeline::PhotoEffectInstance typed_effect;
  typed_effect.id = "typed-effect";
  typed_effect.mask_ref = "effect-set";
  const auto typed_effect_stack = photo_editor::desktop::encode_photo_layer_effects(
      std::vector{typed_effect});
  const QVariantMap typed_effect_layer{
      {QStringLiteral("id"), QStringLiteral("typed-effect-layer")},
      {QStringLiteral("parentId"), QStringLiteral("inner-root")},
      {QStringLiteral("kind"), QStringLiteral("fill")},
      {QStringLiteral("effectStack"), typed_effect_stack.value_or(QString())}};
  const QVariantMap pixel{
      {QStringLiteral("id"), QStringLiteral("nested-pixel")},
      {QStringLiteral("parentId"), QStringLiteral("nested-group")},
      {QStringLiteral("kind"), QStringLiteral("pixel")},
      {QStringLiteral("pixelData"), QByteArray::fromHex("112233ff")}};
  const QVariantMap nested_instance{
      {QStringLiteral("id"), QStringLiteral("nested-instance")},
      {QStringLiteral("parentId"), QStringLiteral("inner-root")},
      {QStringLiteral("kind"), QStringLiteral("group")},
      {QStringLiteral("smartObjectInstanceId"), new_id()},
      {QStringLiteral("smartObjectDocument"), nested_document}};
  const QVariantMap nested_group{
      {QStringLiteral("id"), QStringLiteral("nested-group")},
      {QStringLiteral("parentId"), QStringLiteral("inner-root")},
      {QStringLiteral("kind"), QStringLiteral("group")},
      {QStringLiteral("children"), QVariantList{pixel}}};
  const QVariantMap root_group{
      {QStringLiteral("id"), QStringLiteral("inner-root")},
      {QStringLiteral("kind"), QStringLiteral("group")},
      {QStringLiteral("children"), QVariantList{title, typed_effect_layer,
                                                  nested_group, nested_instance}}};
  return make_document(new_id(), {root_group}, masks);
}

QVariantMap component_instance(const QString& id, const QVariantMap& document) {
  return QVariantMap{
      {QStringLiteral("id"), id},
      {QStringLiteral("name"), QStringLiteral("Product %1").arg(id)},
      {QStringLiteral("kind"), QStringLiteral("group")},
      {QStringLiteral("smartObjectInstanceId"), new_id()},
      {QStringLiteral("smartObjectDocument"), document}};
}

QStringList layer_ids(const QVariantList& layers) {
  QStringList result;
  for (const auto& value : layers) {
    const auto layer = value.toMap();
    result.append(layer.value(QStringLiteral("id")).toString());
    result.append(layer_ids(layer.value(QStringLiteral("children")).toList()));
    result.append(layer_ids(layer.value(QStringLiteral("layers")).toList()));
  }
  return result;
}

QStringList mask_ids(const QVariantList& masks) {
  QStringList result;
  for (const auto& value : masks) {
    result.append(value.toMap().value(QStringLiteral("id")).toString());
  }
  return result;
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication application(argc, argv);
  QString error;
  const auto document = make_fixture_document();
  if (!check(validate_document(document, &error),
             "Inline nested component fixture did not pass DTO validation")) {
    std::cerr << error.toStdString() << '\n';
    return 1;
  }

  const auto instance_a = component_instance(QStringLiteral("placed-a"), document);
  const auto instance_b = component_instance(QStringLiteral("placed-b"), document);
  const auto expanded = expand_smart_documents({instance_a, instance_b}, {});
  if (!check(expanded.error.isEmpty(),
             "A valid shared nested component could not be expanded")) {
    std::cerr << expanded.error.toStdString() << '\n';
    return 1;
  }

  const auto expected_a_root_id = QStringLiteral("placed-a::inner-root");
  const auto expected_b_root_id = QStringLiteral("placed-b::inner-root");
  const auto root_a = find_layer(expanded.layers, expected_a_root_id);
  const auto root_b = find_layer(expanded.layers, expected_b_root_id);
  if (!check(!root_a.isEmpty() && !root_b.isEmpty() &&
                 root_a.value(QStringLiteral("parentId")).toString() == QStringLiteral("placed-a") &&
                 root_b.value(QStringLiteral("parentId")).toString() == QStringLiteral("placed-b"),
             "Expanded root groups did not keep separate instance identity and parent links")) return 1;

  const auto all_layer_ids = layer_ids(expanded.layers);
  QSet<QString> unique_layer_ids;
  for (const auto& id : all_layer_ids) unique_layer_ids.insert(id);
  if (!check(unique_layer_ids.size() == all_layer_ids.size(),
             "Expanded shared component children did not receive instance-scoped layer IDs")) return 1;
  const auto title_a = find_layer(expanded.layers, QStringLiteral("placed-a::title"));
  const auto title_b = find_layer(expanded.layers, QStringLiteral("placed-b::title"));
  const auto nested_group_a = find_layer(expanded.layers, QStringLiteral("placed-a::nested-group"));
  const auto pixel_a = find_layer(expanded.layers, QStringLiteral("placed-a::nested-pixel"));
  const auto nested_instance_a = find_layer(expanded.layers, QStringLiteral("placed-a::nested-instance"));
  const auto typed_effect_a = find_layer(expanded.layers,
      QStringLiteral("placed-a::typed-effect-layer"));
  const auto typed_effects_a = photo_editor::desktop::decode_photo_layer_effects(typed_effect_a);
  if (!check(!title_a.isEmpty() && !title_b.isEmpty() &&
                 title_a.value(QStringLiteral("parentId")).toString() == expected_a_root_id &&
                 title_b.value(QStringLiteral("parentId")).toString() == expected_b_root_id &&
                 title_a.value(QStringLiteral("selectionMaskSetId")).toString() ==
                     QStringLiteral("placed-a::selection-set") &&
                 title_a.value(QStringLiteral("selectionMaskSetName")).toString() ==
                     QStringLiteral("placed-a::Selection Mask") &&
                 title_a.value(QStringLiteral("maskRef")).toString() ==
                     QStringLiteral("placed-a::selection-set") &&
                 title_a.value(QStringLiteral("effectMaskRef")).toString() ==
                     QStringLiteral("placed-a::effect-set") &&
                 QJsonDocument::fromJson(title_a.value(QStringLiteral("effectStack")).toByteArray())
                     .object().value(QStringLiteral("maskRef")).toString() ==
                     QStringLiteral("placed-a::effect-set") &&
                 typed_effects_a && typed_effects_a->size() == 1 &&
                 typed_effects_a->front().mask_ref == "placed-a::effect-set" &&
                 !nested_group_a.isEmpty() &&
                 nested_group_a.value(QStringLiteral("parentId")).toString() == expected_a_root_id &&
                 !pixel_a.isEmpty() &&
                 pixel_a.value(QStringLiteral("parentId")).toString() ==
                     QStringLiteral("placed-a::nested-group") &&
                 !nested_instance_a.isEmpty() &&
                 nested_instance_a.value(QStringLiteral("parentId")).toString() == expected_a_root_id &&
                 !nested_instance_a.contains(QStringLiteral("smartObjectDocument")),
             "Expanded descendant IDs, parent links, or selection/effect mask references were not namespaced")) {
    std::cerr << "titleA=" << title_a.value(QStringLiteral("id")).toString().toStdString()
              << " parentA=" << title_a.value(QStringLiteral("parentId")).toString().toStdString()
              << " selectionId=" << title_a.value(QStringLiteral("selectionMaskSetId")).toString().toStdString()
              << " selectionName=" << title_a.value(QStringLiteral("selectionMaskSetName")).toString().toStdString()
              << " maskRef=" << title_a.value(QStringLiteral("maskRef")).toString().toStdString()
              << " effectMaskRef=" << title_a.value(QStringLiteral("effectMaskRef")).toString().toStdString()
              << " nestedInstance=" << nested_instance_a.value(QStringLiteral("id")).toString().toStdString()
              << " nestedInstanceHasDoc=" << nested_instance_a.contains(QStringLiteral("smartObjectDocument"))
              << '\n';
    return 1;
  }

  const auto prefixed_selection_mask = QStringLiteral("placed-a::selection-mask-source");
  const auto prefixed_effect_mask = QStringLiteral("placed-a::effect-mask-source");
  const auto expanded_mask_ids = mask_ids(expanded.masks);
  QSet<QString> unique_mask_ids;
  for (const auto& id : expanded_mask_ids) unique_mask_ids.insert(id);
  if (!check(expanded_mask_ids.contains(prefixed_selection_mask) &&
                 expanded_mask_ids.contains(prefixed_effect_mask) &&
                 expanded_mask_ids.contains(QStringLiteral("placed-b::selection-mask-source")) &&
                 expanded_mask_ids.contains(QStringLiteral("placed-b::effect-mask-source")) &&
                 expanded_mask_ids.size() == 4 && unique_mask_ids.size() == 4,
             "Expanded shared masks did not receive private instance identities")) return 1;

  const auto expanded_a_mask = expanded.masks.at(0).toMap();
  const auto expanded_b_mask = expanded.masks.at(2).toMap();
  if (!check(expanded_a_mask.value(QStringLiteral("outputId")).toString() ==
                 QStringLiteral("placed-a::selection-mask-source") &&
                 expanded_b_mask.value(QStringLiteral("outputId")).toString() ==
                 QStringLiteral("placed-b::selection-mask-source") &&
                 expanded_a_mask.value(QStringLiteral("outputId")) !=
                 expanded_b_mask.value(QStringLiteral("outputId")),
             "Reusable mask output identities were not namespaced per component instance")) return 1;

  if (!check(expanded.masks.size() == 4 &&
                 find_layer(expanded.layers, QStringLiteral("placed-a")).isEmpty() == false &&
                 find_layer(expanded.layers, QStringLiteral("placed-b")).isEmpty() == false,
             "Expansion did not preserve both shared outer instances and both embedded mask stacks")) return 1;

  auto cyclic_document = document;
  auto cyclic_layers = cyclic_document.value(QStringLiteral("layers")).toList();
  auto cyclic_root = cyclic_layers.first().toMap();
  auto cyclic_children = cyclic_root.value(QStringLiteral("children")).toList();
  auto cyclic_nested = cyclic_children.last().toMap();
  auto nested_document = cyclic_nested.value(QStringLiteral("smartObjectDocument")).toMap();
  nested_document.insert(QStringLiteral("contentId"), document.value(QStringLiteral("contentId")));
  cyclic_nested.insert(QStringLiteral("smartObjectDocument"), nested_document);
  cyclic_children.last() = cyclic_nested;
  cyclic_root.insert(QStringLiteral("children"), cyclic_children);
  cyclic_layers[0] = cyclic_root;
  cyclic_document.insert(QStringLiteral("layers"), cyclic_layers);
  const auto malformed = expand_smart_documents(
      {component_instance(QStringLiteral("malformed"), cyclic_document)}, {});
  if (!check(!malformed.error.isEmpty() && malformed.layers.isEmpty(),
             "Malformed nested content cycle was not rejected atomically by expansion")) return 1;

  std::cout << "Smart Object expansion keeps shared instances, child hierarchy, parent links, private masks, and rejects cycles\n";
  return 0;
}
