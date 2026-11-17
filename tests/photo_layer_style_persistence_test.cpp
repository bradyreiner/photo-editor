#include "edit_recipe_v13_persistence.h"
#include "mask_workflow_ownership.h"
#include "photo_layer_clipboard.h"
#include "photo_smart_document.h"
#include "photo_smart_document_expansion.h"
#include "photo_editor/pipeline/layer_styles.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <algorithm>
#include <iostream>
#include <stdexcept>

using namespace photo_editor::desktop;
using namespace photo_editor::pipeline;

namespace {

int failures = 0;

void check(bool condition, const char* message) {
  if (condition) return;
  std::cerr << "FAIL: " << message << '\n';
  ++failures;
}

QString style_stack(const QString& mask_ref, bool enabled = true) {
  LayerStyleOperation operation;
  operation.id = "persisted-overlay";
  operation.type_id = "color_overlay";
  operation.plane = LayerStylePlane::OnContent;
  operation.mask_ref = mask_ref.toStdString();
  operation.parameters = LayerStyleColorOverlay{
      LayerStyleColor{.red = 0.2, .green = 0.4, .blue = 0.8, .alpha = 0.75}};
  LayerStyleStack stack;
  stack.enabled = enabled;
  stack.operations.push_back(operation);
  std::string error;
  const auto encoded = serialize_layer_style_stack(stack, &error);
  check(encoded.has_value(), "style fixture encodes with the engine codec");
  return encoded ? QString::fromStdString(*encoded) : QString();
}

QVariantMap nested_document(const QString& mask_reference = QStringLiteral("inner-style-set"),
                            const QString& content_id =
                                QStringLiteral("20000000-0000-4000-8000-000000000001")) {
  const auto child = QVariantMap{
      {QStringLiteral("id"), QStringLiteral("nested-pixels")},
      {QStringLiteral("kind"), QStringLiteral("pixel")},
      {QStringLiteral("pixelWidth"), 2},
      {QStringLiteral("pixelHeight"), 1},
      {QStringLiteral("pixelData"), QByteArray::fromHex("10a0ff55")},
      {QStringLiteral("layerStyleStack"), style_stack(mask_reference)},
      {QStringLiteral("authoredChildField"), QStringLiteral("preserve exactly")}};
  return {
      {QStringLiteral("schema"), QStringLiteral("looksy.smart-object.document")},
      {QStringLiteral("version"), 1},
      {QStringLiteral("contentId"), content_id},
      {QStringLiteral("revision"), 3},
      {QStringLiteral("canvasWidth"), 2},
      {QStringLiteral("canvasHeight"), 1},
      {QStringLiteral("layers"), QVariantList{child}},
      {QStringLiteral("maskStack"), QVariantList{QVariantMap{
          {QStringLiteral("id"), QStringLiteral("inner-mask-source")},
          {QStringLiteral("maskSetId"), QStringLiteral("inner-style-set")},
          {QStringLiteral("kind"), QStringLiteral("raster")},
          {QStringLiteral("maskData"), QByteArray::fromHex("00ff")}}}}};
}

QVariantMap component_descriptor() {
  return {
      {QStringLiteral("schema"), QStringLiteral("looksy.smart-object.source")},
      {QStringLiteral("version"), 1},
      {QStringLiteral("contentId"), QStringLiteral("30000000-0000-4000-8000-000000000001")},
      {QStringLiteral("policy"), QStringLiteral("embedded")},
      {QStringLiteral("payloadKind"), QStringLiteral("layer-png")},
      {QStringLiteral("sourceSha256"), QString(64, QLatin1Char('a'))},
      {QStringLiteral("byteLength"), 8},
      {QStringLiteral("fileName"), QStringLiteral("component.png")},
      {QStringLiteral("intrinsicWidth"), 2},
      {QStringLiteral("intrinsicHeight"), 1},
      {QStringLiteral("dataBase64"), QStringLiteral("AAECAP8BAgM=")}};
}

QVariantMap mask(QString id, QString set_id, QByteArray data) {
  return {{QStringLiteral("id"), std::move(id)},
          {QStringLiteral("maskSetId"), std::move(set_id)},
          {QStringLiteral("kind"), QStringLiteral("brush")},
          {QStringLiteral("operation"), QStringLiteral("add")},
          {QStringLiteral("enabled"), true},
          {QStringLiteral("blendMode"), QStringLiteral("normal")},
          {QStringLiteral("segmentationData"), std::move(data)},
          {QStringLiteral("width"), 2},
          {QStringLiteral("height"), 1}};
}

EditRecipeState component_state() {
  const auto text_layer = QVariantMap{
      {QStringLiteral("id"), QStringLiteral("legacy-text")},
      {QStringLiteral("kind"), QStringLiteral("text")},
      {QStringLiteral("text"), QStringLiteral("Exact legacy heading")},
      {QStringLiteral("fontFamily"), QStringLiteral("Serif Display")},
      {QStringLiteral("fontSize"), 37.25},
      {QStringLiteral("fontWeight"), 600},
      {QStringLiteral("textOriginX"), 0.125},
      {QStringLiteral("textOriginY"), 0.75},
      {QStringLiteral("pixelWidth"), 2},
      {QStringLiteral("pixelHeight"), 1},
      {QStringLiteral("transform"), QVariantMap{
          {QStringLiteral("x"), 14.5},
          {QStringLiteral("y"), -7.25},
          {QStringLiteral("rotation"), 12.0}}},
      {QStringLiteral("layerStyleStack"), style_stack(QStringLiteral("outer-style-set"))},
      {QStringLiteral("authoredTextField"), QStringLiteral("retained")}};
  const auto component_layer = QVariantMap{
      {QStringLiteral("id"), QStringLiteral("component-instance")},
      {QStringLiteral("kind"), QStringLiteral("group")},
      {QStringLiteral("name"), QStringLiteral("Styled shared component")},
      {QStringLiteral("smartObject"), component_descriptor()},
      {QStringLiteral("smartObjectInstanceId"), QStringLiteral("component-instance-identity")},
      {QStringLiteral("smartObjectDocument"), nested_document()},
      {QStringLiteral("transform"), QVariantMap{
          {QStringLiteral("x"), 83.125},
          {QStringLiteral("y"), -21.5},
          {QStringLiteral("rotation"), 17.75},
          {QStringLiteral("scaleX"), 0.875},
          {QStringLiteral("scaleY"), 1.25}}},
      {QStringLiteral("maskData"), QByteArray::fromHex("80c0")},
      {QStringLiteral("layerStyleStack"), style_stack(QStringLiteral("outer-style-set"))},
      {QStringLiteral("authoredPlacementField"), QStringLiteral("retain")}};
  EditRecipeState state;
  state.layers = {component_layer, text_layer};
  state.mask_stack = {mask(QStringLiteral("outer-mask-source"),
                            QStringLiteral("outer-style-set"),
                            QByteArray::fromHex("4080"))};
  return state;
}

DesktopRecipeV13Context context() {
  DesktopRecipeV13Context value;
  value.recipe_id = QStringLiteral("recipe:layer-style-persistence");
  value.source_id = QStringLiteral("source:layer-style-persistence");
  value.decoder_provenance = QStringLiteral("raster:layer-style-persistence-v1");
  return value;
}

QJsonObject provenance(const CanonicalRecipeV13Document& document) {
  return QJsonDocument::fromJson(document.provenance_payload.toUtf8()).object();
}

void canonical_v6_style_roundtrip_and_guards() {
  QTemporaryDir temporary;
  check(temporary.isValid(), "temporary canonical asset store is available");
  if (!temporary.isValid()) return;
  const RecipeAssetStore asset_store(temporary.path());
  const auto state = component_state();
  const auto written = serialize_desktop_recipe_as_canonical_v13_with_assets(
      state, context(), asset_store);
  check(written.ok(), "canonical asset persistence accepts valid style and component layers");
  if (!written.ok()) return;

  const auto saved_provenance = provenance(written.document);
  check(saved_provenance.value(QStringLiteral("schema")).toString() ==
            QStringLiteral("photo-editor-edit-recipe-provenance:v6"),
        "style-bearing canonical documents use provenance v6");
  check(saved_provenance.value(QStringLiteral("requiredCapabilities")).toArray() ==
            QJsonArray{QStringLiteral("component-object-assets-v1"),
                       QStringLiteral("layer-styles-v1"),
                       QStringLiteral("smart-object-documents-v1")},
        "v6 declares the ordered component, style and nested-document capabilities");

  const auto projected = project_canonical_v13_document_to_desktop_state_with_assets(
      written.document, asset_store);
  const auto normalized_masks = normalize_mask_stack(state.mask_stack);
  if (!projected.ok()) {
    for (const auto& error : projected.errors)
      std::cerr << "projection_error=" << error.path << ": " << error.message << '\n';
  } else {
    if (projected.state->layers != state.layers) {
      for (qsizetype index = 0;
           index < std::min(projected.state->layers.size(), state.layers.size());
           ++index) {
        const auto actual = projected.state->layers.at(index).toMap();
        const auto expected = state.layers.at(index).toMap();
        for (const auto& key : actual.keys() + expected.keys())
          if (actual.value(key) != expected.value(key))
            std::cerr << "layer_diff[" << index << "]." << key.toStdString()
                      << " actualType=" << actual.value(key).metaType().id()
                      << " expectedType=" << expected.value(key).metaType().id()
                      << " actual=" << actual.value(key).toString().toStdString()
                      << " expected=" << expected.value(key).toString().toStdString()
                      << '\n';
      }
    }
    if (projected.state->mask_stack != normalized_masks) {
      std::cerr << "mask_normalized_equal=0 actualMasks="
                << projected.state->mask_stack.size() << " expectedMasks="
                << normalized_masks.size() << '\n';
    }
  }
  const bool root_mask_bytes_exact = projected.ok() &&
      projected.state->mask_stack.size() == normalized_masks.size() &&
      !normalized_masks.isEmpty() &&
      projected.state->mask_stack.first().toMap().value(QStringLiteral("segmentationData")) ==
          normalized_masks.first().toMap().value(QStringLiteral("segmentationData"));
  check(projected.ok() && projected.state->layers == state.layers &&
            projected.state->mask_stack == normalized_masks && root_mask_bytes_exact,
        "styles, nested component bytes, masks, transforms and legacy typography round-trip exactly");

  auto downgraded = written.document;
  auto downgraded_provenance = saved_provenance;
  downgraded_provenance.insert(QStringLiteral("schema"),
      QStringLiteral("photo-editor-edit-recipe-provenance:v5"));
  downgraded_provenance.insert(QStringLiteral("requiredCapabilities"),
      QJsonArray{QStringLiteral("component-object-assets-v1"),
                 QStringLiteral("smart-object-documents-v1")});
  downgraded.provenance_payload = QString::fromUtf8(
      QJsonDocument(downgraded_provenance).toJson(QJsonDocument::Compact));
  const auto rejected_downgrade = project_canonical_v13_document_to_desktop_state_with_assets(
      downgraded, asset_store);
  check(!rejected_downgrade.ok(),
        "v5 provenance cannot silently discard restored style stacks");

  auto missing_capability = written.document;
  auto missing_provenance = saved_provenance;
  missing_provenance.insert(QStringLiteral("requiredCapabilities"),
      QJsonArray{QStringLiteral("component-object-assets-v1"),
                 QStringLiteral("smart-object-documents-v1")});
  missing_capability.provenance_payload = QString::fromUtf8(
      QJsonDocument(missing_provenance).toJson(QJsonDocument::Compact));
  check(!project_canonical_v13_document_to_desktop_state_with_assets(
            missing_capability, asset_store).ok(),
        "v6 provenance rejects a missing layer-styles capability");

  auto unknown_capability = written.document;
  auto unknown_provenance = saved_provenance;
  auto capabilities = unknown_provenance.value(
      QStringLiteral("requiredCapabilities")).toArray();
  capabilities.append(QStringLiteral("future-style-capability"));
  unknown_provenance.insert(QStringLiteral("requiredCapabilities"), capabilities);
  unknown_capability.provenance_payload = QString::fromUtf8(
      QJsonDocument(unknown_provenance).toJson(QJsonDocument::Compact));
  check(!project_canonical_v13_document_to_desktop_state_with_assets(
            unknown_capability, asset_store).ok(),
        "v6 provenance rejects unknown required capabilities");

  auto unsupported_version = written.document;
  auto unsupported_provenance = saved_provenance;
  unsupported_provenance.insert(QStringLiteral("schema"),
      QStringLiteral("photo-editor-edit-recipe-provenance:v7"));
  unsupported_version.provenance_payload = QString::fromUtf8(
      QJsonDocument(unsupported_provenance).toJson(QJsonDocument::Compact));
  check(!project_canonical_v13_document_to_desktop_state_with_assets(
            unsupported_version, asset_store).ok(),
        "unsupported provenance versions fail closed");

  const auto decoded_recipe = photo_editor::pipeline::deserialize_edit_recipe_v13(
      written.document.recipe_payload.toStdString());
  check(decoded_recipe.has_value(), "canonical test recipe decodes for tamper checks");
  if (decoded_recipe) {
    auto malformed_recipe = *decoded_recipe;
    for (auto& layer : malformed_recipe.layers) {
      if (layer.id.value != "legacy-text" || !layer.content_asset.has_value()) continue;
      const auto stored = asset_store.getVariant(*layer.content_asset);
      if (!stored || !stored->canConvert<QVariantMap>()) {
        check(false, "text content asset is readable for a style tamper check");
        break;
      }
      auto damaged = stored->toMap();
      damaged.insert(QStringLiteral("layerStyleStack"), QStringLiteral("not-an-engine-envelope"));
      const auto replacement = asset_store.putVariant(damaged);
      if (!replacement.ok()) {
        check(false, "malformed stack content asset can be installed for rejection check");
        break;
      }
      layer.content_asset = *replacement.asset;
      CanonicalRecipeV13Document tampered = written.document;
      tampered.recipe_payload = QString::fromStdString(
          photo_editor::pipeline::serialize_edit_recipe_v13(malformed_recipe));
      tampered.cache_key = QString::fromStdString(
          photo_editor::pipeline::edit_recipe_v13_cache_key(malformed_recipe));
      check(!project_canonical_v13_document_to_desktop_state_with_assets(
                tampered, asset_store).ok(),
            "malformed stored style stacks fail before projection is accepted");
      break;
    }
  }

  auto malformed_state = state;
  auto text = malformed_state.layers[1].toMap();
  text.insert(QStringLiteral("layerStyleStack"), QStringLiteral("malformed"));
  malformed_state.layers[1] = text;
  check(!serialize_desktop_recipe_as_canonical_v13_with_assets(
            malformed_state, context(), asset_store).ok(),
        "canonical preflight rejects malformed engine style strings");

  auto dangling_root_state = state;
  text = dangling_root_state.layers[1].toMap();
  text.insert(QStringLiteral("layerStyleStack"),
      style_stack(QStringLiteral("missing-root-mask-set")));
  dangling_root_state.layers[1] = text;
  check(!serialize_desktop_recipe_as_canonical_v13_with_assets(
            dangling_root_state, context(), asset_store).ok(),
        "asset-backed canonical preflight rejects dangling root style mask references");

  EditRecipeState direct_state;
  direct_state.layers = {QVariantMap{
      {QStringLiteral("id"), QStringLiteral("direct-fill")},
      {QStringLiteral("kind"), QStringLiteral("fill")},
      {QStringLiteral("fillColor"), QStringLiteral("#b06442")},
      {QStringLiteral("layerStyleStack"), style_stack(QString())}}};
  auto direct_context = context();
  direct_context.layer_content_assets.push_back(
      DesktopRecipeV13Context::LayerContentBinding{
          .layer_id = QStringLiteral("direct-fill"),
          .content_asset = photo_editor::pipeline::AssetIdentifier{
              "asset:direct-fill-content"}});
  const auto direct = serialize_desktop_recipe_as_canonical_v13(
      direct_state, direct_context);
  if (!direct.ok()) {
    for (const auto& error : direct.errors)
      std::cerr << "direct_error=" << error.path << ": " << error.message << '\n';
  }
  check(direct.ok() && provenance(direct.document).value(QStringLiteral("schema")).toString() ==
            QStringLiteral("photo-editor-edit-recipe-provenance:v6") &&
            provenance(direct.document).value(QStringLiteral("requiredCapabilities")).toArray() ==
                QJsonArray{QStringLiteral("layer-styles-v1")},
        "direct canonical serialization negotiates style capability v6 without component assets");
  auto dangling_direct_state = direct_state;
  auto direct_layer = dangling_direct_state.layers[0].toMap();
  direct_layer.insert(QStringLiteral("layerStyleStack"),
      style_stack(QStringLiteral("missing-direct-mask-set")));
  dangling_direct_state.layers[0] = direct_layer;
  check(!serialize_desktop_recipe_as_canonical_v13(
            dangling_direct_state, direct_context).ok(),
        "direct canonical preflight also rejects dangling root style mask references");
}

void nested_style_mask_validation_and_instance_namespacing() {
  auto valid = nested_document();
  QString error;
  check(smart_object::nested_document::validate_document(valid, &error),
        "nested documents accept valid style mask references from their local mask stack");

  auto dangling = valid;
  auto dangling_layers = dangling.value(QStringLiteral("layers")).toList();
  auto child = dangling_layers.first().toMap();
  child.insert(QStringLiteral("layerStyleStack"), style_stack(QStringLiteral("missing-set")));
  dangling_layers[0] = child;
  dangling.insert(QStringLiteral("layers"), dangling_layers);
  check(!smart_object::nested_document::validate_document(dangling, &error),
        "nested document validation rejects dangling style mask references");

  LayerStyleOperation future;
  future.id = "future-style";
  future.type_id = "vendor_future_style";
  future.algorithm_version = 4;
  future.enabled = false;
  future.mask_ref = "inner-style-set";
  future.opaque_parameters = "preserve opaque settings";
  LayerStyleStack future_stack;
  future_stack.operations.push_back(future);
  std::string codec_error;
  const auto future_text = serialize_layer_style_stack(future_stack, &codec_error);
  auto unknown_disabled = valid;
  auto unknown_layers = unknown_disabled.value(QStringLiteral("layers")).toList();
  child = unknown_layers.first().toMap();
  child.insert(QStringLiteral("layerStyleStack"),
      future_text ? QString::fromStdString(*future_text) : QString());
  unknown_layers[0] = child;
  unknown_disabled.insert(QStringLiteral("layers"), unknown_layers);
  check(future_text.has_value() &&
            smart_object::nested_document::validate_document(unknown_disabled, &error),
        "nested documents retain disabled unknown style operators with valid local mask bindings");
  future.enabled = true;
  future_stack.operations = {future};
  const auto enabled_future_text = serialize_layer_style_stack(future_stack, &codec_error);
  auto unknown_enabled = valid;
  auto enabled_layers = unknown_enabled.value(QStringLiteral("layers")).toList();
  child = enabled_layers.first().toMap();
  child.insert(QStringLiteral("layerStyleStack"),
      enabled_future_text ? QString::fromStdString(*enabled_future_text) : QString());
  enabled_layers[0] = child;
  unknown_enabled.insert(QStringLiteral("layers"), enabled_layers);
  check(enabled_future_text.has_value() &&
            smart_object::nested_document::validate_document(unknown_enabled, &error),
        "nested document storage retains enabled unknown operators so the renderer can fail closed");

  auto bad_container = valid;
  auto bad_container_layers = bad_container.value(QStringLiteral("layers")).toList();
  child = bad_container_layers.first().toMap();
  child.insert(QStringLiteral("layerStyleStack"), QVariantMap{{QStringLiteral("version"), 1}});
  bad_container_layers[0] = child;
  bad_container.insert(QStringLiteral("layers"), bad_container_layers);
  check(!smart_object::nested_document::validate_document(bad_container, &error),
        "nested document validation rejects JSON-like values instead of engine style strings");

  const QVariantMap instance{
      {QStringLiteral("id"), QStringLiteral("placed-style-component")},
      {QStringLiteral("kind"), QStringLiteral("group")},
      {QStringLiteral("smartObjectDocument"), valid}};
  const auto expanded = expand_smart_documents(QVariantList{instance}, {});
  check(expanded.error.isEmpty() && expanded.layers.size() == 2 &&
            expanded.masks.size() == 1,
        "a styled nested document expands into its owning group and child");
  if (expanded.error.isEmpty() && expanded.layers.size() == 2 &&
      expanded.masks.size() == 1) {
    const auto expanded_child = expanded.layers.at(1).toMap();
    std::string decode_error;
    const auto expanded_stack = deserialize_layer_style_stack(
        expanded_child.value(QStringLiteral("layerStyleStack")).toString().toStdString(),
        &decode_error);
    check(expanded_stack && expanded_stack->operations.front().mask_ref ==
              "placed-style-component::inner-style-set" &&
              expanded.masks.first().toMap().value(QStringLiteral("maskSetId")).toString() ==
                  QStringLiteral("placed-style-component::inner-style-set"),
          "expanded style references and document mask identities share the same instance namespace");
  }
}

void mask_ownership_and_clipboard_style_references() {
  const auto encoded = style_stack(QStringLiteral("owned-style-set"));
  const QVariantMap owner{
      {QStringLiteral("id"), QStringLiteral("style-owner")},
      {QStringLiteral("kind"), QStringLiteral("pixel")},
      {QStringLiteral("layerStyleStack"), encoded}};
  const QVariantList masks{mask(QStringLiteral("style-source"),
                                 QStringLiteral("owned-style-set"),
                                 QByteArray::fromHex("00ff"))};
  check(exclusive_mask_sets_for_deleted_layer(owner, {}, masks).contains(
            QStringLiteral("owned-style-set")),
        "deleting the only style consumer discovers its exclusive mask set");

  auto shared = owner;
  shared.insert(QStringLiteral("id"), QStringLiteral("shared-style-owner"));
  check(exclusive_mask_sets_for_deleted_layer(owner, {shared}, masks).isEmpty(),
        "a surviving style consumer retains shared mask geometry");

  auto locked = shared;
  locked.insert(QStringLiteral("locked"), true);
  check(selection_has_locked_consumer(masks.first().toMap(), {locked}),
        "a locked style consumer protects its referenced mask set");
  auto changed_masks = masks;
  changed_masks[0] = mask(QStringLiteral("style-source"),
                         QStringLiteral("owned-style-set"),
                         QByteArray::fromHex("ff00"));
  check(has_forbidden_locked_selection_change(masks, changed_masks, {locked}),
        "locked style references reject destructive mask coverage edits");

  auto independent = shared;
  independent.insert(QStringLiteral("layerStyleStack"), style_stack(QStringLiteral("independent-style-set")));
  const QVariantList both_masks{
      masks.first(),
      mask(QStringLiteral("independent-source"), QStringLiteral("independent-style-set"),
           QByteArray::fromHex("00ff"))};
  check(exclusive_mask_sets_for_deleted_layer(owner, {independent}, both_masks).contains(
            QStringLiteral("owned-style-set")),
        "an independent duplicate reference does not keep the original style mask alive");

  auto malformed_survivor = shared;
  malformed_survivor.insert(QStringLiteral("layerStyleStack"), QStringLiteral("unknown-envelope"));
  malformed_survivor.insert(QStringLiteral("locked"), true);
  check(exclusive_mask_sets_for_deleted_layer(owner, {malformed_survivor}, masks).isEmpty() &&
            selection_has_locked_consumer(masks.first().toMap(), {malformed_survivor}),
        "malformed style consumers conservatively retain and protect mask geometry");

  PhotoEffectInstance effect;
  effect.id = "clipboard-blur";
  effect.mask_ref = "owned-style-set";
  const auto effect_text = encode_photo_layer_effects(std::vector{effect});
  QVariantMap clipboard_layer = owner;
  clipboard_layer.insert(QStringLiteral("effectStack"), effect_text.value_or(QString()));
  QVariantMap clipboard_document{
      {QStringLiteral("layers"), QVariantList{clipboard_layer}},
      {QStringLiteral("maskStack"), masks}};
  QHash<QString, QString> id_map;
  collect_photo_clipboard_ids(clipboard_document, id_map);
  const auto remapped = remap_photo_clipboard_ids(clipboard_document, id_map).toMap();
  const auto remapped_layer = remapped.value(QStringLiteral("layers")).toList().first().toMap();
  std::string style_error;
  const auto remapped_styles = deserialize_layer_style_stack(
      remapped_layer.value(QStringLiteral("layerStyleStack")).toString().toStdString(),
      &style_error);
  QString effect_error;
  const auto remapped_effects = decode_photo_layer_effects(remapped_layer, &effect_error);
  const auto remapped_set = remapped.value(QStringLiteral("maskStack")).toList()
      .first().toMap().value(QStringLiteral("maskSetId")).toString();
  check(remapped_styles && remapped_effects &&
            remapped_set != QStringLiteral("owned-style-set") &&
            remapped_styles->operations.front().mask_ref == remapped_set.toStdString() &&
            remapped_effects->front().mask_ref == remapped_set.toStdString(),
        "clipboard remapping updates style and effect engine strings with the copied mask identity");
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication application(argc, argv);
  canonical_v6_style_roundtrip_and_guards();
  nested_style_mask_validation_and_instance_namespacing();
  mask_ownership_and_clipboard_style_references();
  if (failures != 0) {
    std::cerr << failures << " layer style persistence checks failed\n";
    return 1;
  }
  std::cout << "layer style persistence checks passed\n";
  return 0;
}
