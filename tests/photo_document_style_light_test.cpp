#include "edit_adjustment_patch.h"
#include "edit_recipe_state.h"
#include "edit_recipe_v13_persistence.h"
#include "editor_preview_controller.h"
#include "photo_layer_style_codec.h"
#include "photo_layer_style_light.h"
#include "photo_render_utils.h"
#include "photo_smart_document.h"
#include "photo_smart_document_expansion.h"
#include "photo_text_style.h"

#include "photo_editor/pipeline/edit_recipe_v13.h"

#include <QColorSpace>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QThread>
#include <QUuid>
#include <QStringList>

#include <cmath>
#include <iostream>
#include <stdexcept>

namespace photo_editor::desktop {
struct EditorPreviewControllerTestAccess {
  static EditRecipeState state(const EditorPreviewController& controller) {
    return controller.currentState();
  }
};
}  // namespace photo_editor::desktop

using namespace photo_editor::desktop;
using photo_editor::pipeline::LayerStyleOperation;
using photo_editor::pipeline::LayerStyleStack;

namespace {

void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

void settle(EditorPreviewController& editor) {
  QElapsedTimer timer;
  timer.start();
  const auto idle = [&editor]() {
    return !editor.processing() && !editor.fullRenderRunning() &&
        !editor.professionalToolsBusy() && !editor.photoPaintBusy() &&
        !editor.photoExportBusy();
  };
  while (!idle() && timer.elapsed() < 90000) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    QThread::msleep(2);
  }
  require(idle(), "The document lighting render must settle.");
}

QString style_stack(const QString& id) {
  LayerStyleOperation operation;
  QString error;
  require(layer_style_codec::create_default_operation(
              QStringLiteral("drop_shadow"), id, &operation, &error),
          "The document lighting fixture style must be constructible.");
  LayerStyleStack stack;
  stack.operations.push_back(std::move(operation));
  const auto encoded = photo_editor::pipeline::serialize_layer_style_stack(
      stack, nullptr);
  require(encoded.has_value(), "The document lighting fixture style must encode.");
  return QString::fromStdString(*encoded);
}

int layer_index(const QVariantList& layers, const QString& name) {
  for (int index = 0; index < layers.size(); ++index) {
    if (layers.at(index).toMap().value(QStringLiteral("name")).toString() == name)
      return index;
  }
  return -1;
}

QVariantMap layer_with_style(const QString& id, const QString& name,
                             bool linked, double local_angle) {
  return {{QStringLiteral("id"), id},
          {QStringLiteral("name"), name},
          {QStringLiteral("kind"), QStringLiteral("fill")},
          {QStringLiteral("enabled"), true},
          {QStringLiteral("opacity"), 1.0},
          {QStringLiteral("fillColor"), QStringLiteral("#7799bb")},
          {QStringLiteral("layerStyleStack"), style_stack(id + QStringLiteral("-shadow"))},
          {QStringLiteral("layerStyleGlobalLightAngle"), local_angle},
          {QStringLiteral("layerStyleLightLinked"), linked}};
}

QVariantMap dimension_text_layer(const QString& id,
                                 const PhotoTextDimensionSettings& settings) {
  return {{QStringLiteral("id"), id},
          {QStringLiteral("name"), QStringLiteral("Dimensional text")},
          {QStringLiteral("kind"), QStringLiteral("text")},
          {QStringLiteral("enabled"), true},
          {QStringLiteral("opacity"), 1.0},
          {QStringLiteral("x"), 8.0},
          {QStringLiteral("y"), 7.0},
          {QStringLiteral("width"), 80.0},
          {QStringLiteral("height"), 28.0},
          {QStringLiteral("text"), QStringLiteral("Looksy")},
          {QStringLiteral("textTypography"),
           encode_photo_text_style(PhotoTextStyle{}).toVariantMap()},
          {QStringLiteral("textDimension"),
           encode_photo_text_dimension_variant_map(settings)}};
}

void restore_history(EditorPreviewController& editor, bool redo) {
  const auto asset_id = editor.assetId();
  bool finished = false;
  bool succeeded = false;
  const auto connection = QObject::connect(
      &editor, &EditorPreviewController::historyRestoreFinished, &editor,
      [&asset_id, &finished, &succeeded](const QString& restored_asset, bool success) {
        if (restored_asset != asset_id) return;
        finished = true;
        succeeded = success;
      });
  if (redo) editor.redo();
  else editor.undo();
  QElapsedTimer timer;
  timer.start();
  while (!finished && timer.elapsed() < 90000) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    QThread::msleep(2);
  }
  QObject::disconnect(connection);
  require(finished && succeeded, "Document lighting history restore must succeed.");
  settle(editor);
}

void controller_history_unlink_and_preset_roundtrip() {
  QTemporaryDir temporary;
  require(temporary.isValid(), "A temporary document lighting workspace must exist.");
  QImage source(40, 28, QImage::Format_ARGB32);
  source.fill(QColor(QStringLiteral("#283b50")));
  source.setColorSpace(QColorSpace(QColorSpace::SRgb));
  const auto source_path = temporary.filePath(QStringLiteral("document-light-source.png"));
  require(source.save(source_path, "PNG"), "The document lighting source must be writable.");

  EditorPreviewController editor;
  editor.loadAsset(QStringLiteral("document-style-light-workflow"), source_path,
                   {}, {}, {}, {}, {}, true);
  settle(editor);
  require(editor.addFillLayer(QStringLiteral("#7c95ae"), QStringLiteral("Light A")),
          "The first linked style layer must be created.");
  settle(editor);
  require(editor.addFillLayer(QStringLiteral("#9c7862"), QStringLiteral("Light B")),
          "The second linked style layer must be created.");
  settle(editor);

  auto index_a = layer_index(editor.photoLayers(), QStringLiteral("Light A"));
  auto index_b = layer_index(editor.photoLayers(), QStringLiteral("Light B"));
  require(index_a >= 0 && index_b >= 0, "Both style layers must be addressable.");
  require(editor.addPhotoLayerStyle(index_a, QStringLiteral("drop_shadow")) &&
              editor.addPhotoLayerStyle(index_b, QStringLiteral("drop_shadow")),
          "Both style layers must have a shadow operation.");
  require(editor.setPhotoLayerStyleLightAngle(index_a, 35.0) &&
              editor.setPhotoLayerStyleLightAngle(index_b, -100.0),
          "Unlinked style layers must keep independent legacy light directions.");
  require(editor.setPhotoLayerStyleLightLinked(index_a, true) &&
              editor.setPhotoLayerStyleLightLinked(index_b, true),
          "Linking each layer to document lighting must be explicit.");
  require(editor.photoLayerStyleLightLinked(index_a) &&
              editor.photoLayerStyleLightLinked(index_b),
          "The controller must report each link state.");

  require(editor.setPhotoLayerStyleLightAngle(index_a, 72.0),
          "The existing per-layer angle setter must route linked edits to the document.");
  require(std::abs(editor.documentPhotoLayerStyleLightAngle() - 72.0) < 1e-9 &&
              std::abs(editor.photoLayerStyleLightAngle(index_b) - 72.0) < 1e-9,
          "A linked angle edit must update every linked layer in the document scope.");

  const auto history_before_document_change = editor.editHistoryCurrentIndex();
  require(editor.setDocumentPhotoLayerStyleLightAngle(120.0),
          "The document lighting control must accept a valid shared direction.");
  require(editor.editHistoryCurrentIndex() == history_before_document_change + 1,
          "One linked lighting change must add exactly one history step.");
  settle(editor);
  require(std::abs(editor.photoLayerStyleLightAngle(index_a) - 120.0) < 1e-9 &&
              std::abs(editor.photoLayerStyleLightAngle(index_b) - 120.0) < 1e-9,
          "Both linked layers must render against the same document direction.");
  restore_history(editor, false);
  require(std::abs(editor.documentPhotoLayerStyleLightAngle() - 72.0) < 1e-9,
          "Undo must restore the prior document direction atomically.");
  restore_history(editor, true);
  require(std::abs(editor.documentPhotoLayerStyleLightAngle() - 120.0) < 1e-9,
          "Redo must restore the shared document direction.");

  index_a = layer_index(editor.photoLayers(), QStringLiteral("Light A"));
  index_b = layer_index(editor.photoLayers(), QStringLiteral("Light B"));
  require(editor.setPhotoLayerStyleLightLinked(index_b, false),
          "A single style layer must be unlinkable.");
  require(!editor.photoLayerStyleLightLinked(index_b) &&
              std::abs(editor.photoLayerStyleLightAngle(index_b) - 120.0) < 1e-9,
          "Unlinking must preserve the layer's current visible direction.");
  require(editor.setPhotoLayerStyleLightAngle(index_b, 72.0),
          "An unlinked style layer must retain a local angle control.");
  require(editor.setDocumentPhotoLayerStyleLightAngle(150.0),
          "The remaining linked style must still control document lighting.");
  require(std::abs(editor.photoLayerStyleLightAngle(index_a) - 150.0) < 1e-9 &&
              std::abs(editor.photoLayerStyleLightAngle(index_b) - 72.0) < 1e-9,
          "An unlinked layer must keep its local direction while linked peers move.");

  QString envelope_error;
  const auto linked_envelope = editor.photoLayerStyleEnvelope(index_a);
  const auto unlinked_envelope = editor.photoLayerStyleEnvelope(index_b);
  QString ignored_stack;
  double linked_envelope_angle = -45.0;
  double unlinked_envelope_angle = -45.0;
  require(layer_style_codec::decode_envelope(linked_envelope.toUtf8(),
              &ignored_stack, &linked_envelope_angle, &envelope_error) &&
              std::abs(linked_envelope_angle - 150.0) < 1e-9,
          "An exported linked preset must capture the effective document direction.");
  require(layer_style_codec::decode_envelope(unlinked_envelope.toUtf8(),
              &ignored_stack, &unlinked_envelope_angle, &envelope_error) &&
              std::abs(unlinked_envelope_angle - 72.0) < 1e-9,
          "An exported unlinked preset must capture its local direction.");
  const auto linked_local_before_preset = editor.photoLayers().at(index_a).toMap()
      .value(QStringLiteral("layerStyleGlobalLightAngle")).toDouble();
  require(editor.applyPhotoLayerStyleEnvelope(index_a, unlinked_envelope),
          "A local style preset can be applied to a linked target.");
  require(editor.photoLayerStyleLightLinked(index_a) &&
              std::abs(editor.documentPhotoLayerStyleLightAngle() - 150.0) < 1e-9 &&
              std::abs(editor.photoLayers().at(index_a).toMap()
                  .value(QStringLiteral("layerStyleGlobalLightAngle")).toDouble() -
                  linked_local_before_preset) < 1e-9,
          "Applying a preset to a linked target must preserve its link and document lighting.");
  settle(editor);

  const auto state = EditorPreviewControllerTestAccess::state(editor);
  const auto recipe_json = serialize_edit_recipe_json(state);
  const auto recipe_object = QJsonDocument::fromJson(recipe_json.toUtf8()).object();
  require(recipe_object.value(QStringLiteral("recipeVersion")).toInt() ==
              kPhotoDocumentLayerStyleLightEditRecipeVersion &&
              std::abs(recipe_object.value(QStringLiteral("documentLayerStyleLightAngle")).toDouble() -
                       150.0) < 1e-9,
          "Linked lighting must serialize as desktop recipe version 20 with its document angle.");
  require(validate_edit_recipe_json(recipe_json).valid,
          "The linked-light recipe must pass desktop validation.");
  const auto parsed_recipe = parse_edit_recipe_json(recipe_json);
  require(std::abs(parsed_recipe.document_layer_style_light_angle - 150.0) < 1e-9 &&
              detail::photo_layers_have_linked_style_lighting(parsed_recipe.layers),
          "Recipe parsing must restore both the document angle and explicit layer links.");
  auto downgraded_recipe = recipe_object;
  downgraded_recipe.insert(QStringLiteral("recipeVersion"),
                           kPhotoLayerStyleEditRecipeVersion);
  require(!validate_edit_recipe_json(QString::fromUtf8(
              QJsonDocument(downgraded_recipe).toJson(QJsonDocument::Compact))).valid,
          "Recipe version 19 must reject linked document-light semantics.");

  const auto patch_json = serialize_adjustment_patch_json(
      state, all_adjustment_groups(), AdjustmentPatchScope::AssetBound);
  const auto patch_object = QJsonDocument::fromJson(patch_json.toUtf8()).object();
  require(patch_object.value(QStringLiteral("version")).toInt() == 4,
          "Asset-bound adjustment patches with linked lighting must use version 4.");
  const auto parsed_patch = parse_adjustment_patch_json(patch_json);
  require(parsed_patch.ok() &&
              std::abs(parsed_patch.state->document_layer_style_light_angle - 150.0) < 1e-9,
          "Asset-bound all-groups patches must restore the document angle.");
  const auto layers_patch = parse_adjustment_patch_json(
      serialize_adjustment_patch_json(state, {QStringLiteral("layers")},
                                      AdjustmentPatchScope::AssetBound));
  require(layers_patch.ok() &&
              std::abs(layers_patch.state->document_layer_style_light_angle - 150.0) < 1e-9,
          "The layers adjustment group must carry document lighting.");
  const auto source_agnostic = parse_adjustment_patch_json(
      serialize_adjustment_patch_json(state, all_adjustment_groups(),
                                      AdjustmentPatchScope::SourceAgnostic));
  require(source_agnostic.ok() &&
              std::abs(source_agnostic.state->document_layer_style_light_angle - 150.0) < 1e-9,
          "Source-agnostic all-groups patches must retain document lighting.");
  auto downgraded_patch = patch_object;
  downgraded_patch.insert(QStringLiteral("version"), 3);
  require(!parse_adjustment_patch_json(QString::fromUtf8(
              QJsonDocument(downgraded_patch).toJson(QJsonDocument::Compact))).ok(),
          "Adjustment patch version 3 must reject linked document-light semantics.");

  RecipeAssetStore store(temporary.filePath(QStringLiteral("recipe-assets")));
  DesktopRecipeV13Context context;
  context.recipe_id = QStringLiteral("recipe:document-style-light");
  context.source_id = QStringLiteral("source:document-style-light");
  context.decoder_provenance = QStringLiteral("raster:document-style-light-v1");
  const auto canonical = serialize_desktop_recipe_as_canonical_v13_with_assets(
      state, context, store);
  require(canonical.ok(), "Canonical storage must accept linked lighting.");
  const auto provenance = QJsonDocument::fromJson(
      canonical.document.provenance_payload.toUtf8()).object();
  require(provenance.value(QStringLiteral("schema")).toString() ==
              QStringLiteral("photo-editor-edit-recipe-provenance:v7") &&
              provenance.value(QStringLiteral("requiredCapabilities")).toArray().contains(
                  QStringLiteral("document-layer-style-light-v1")) &&
              std::abs(provenance.value(QStringLiteral("documentLayerStyleLightAngle")).toDouble() -
                       150.0) < 1e-9,
          "Canonical storage must negotiate v7 linked-light capability and preserve the angle.");

  auto changed_document_light = state;
  changed_document_light.document_layer_style_light_angle = 165.0;
  const auto changed_light_canonical =
      serialize_desktop_recipe_as_canonical_v13_with_assets(
          changed_document_light, context, store);
  require(changed_light_canonical.ok() &&
              changed_light_canonical.document.recipe_payload ==
                  canonical.document.recipe_payload &&
              changed_light_canonical.document.cache_key !=
                  canonical.document.cache_key,
          "A linked document-light angle change must change canonical cache identity while preserving engine recipe bytes.");
  const auto changed_light_reopened =
      project_canonical_v13_document_to_desktop_state_with_assets(
          changed_light_canonical.document, store);
  require(changed_light_reopened.ok() && changed_light_reopened.state &&
              std::abs(changed_light_reopened.state->document_layer_style_light_angle -
                       165.0) < 1e-9 &&
              detail::photo_layers_have_linked_style_lighting(
                  changed_light_reopened.state->layers),
          "Canonical cache identity and reopen must carry the changed linked-light dependency.");

  auto unlinked_light = state;
  for (int index = 0; index < unlinked_light.layers.size(); ++index) {
    auto layer = unlinked_light.layers.at(index).toMap();
    layer.remove(QStringLiteral("layerStyleLightLinked"));
    unlinked_light.layers[index] = layer;
  }
  auto unlinked_light_changed = unlinked_light;
  unlinked_light_changed.document_layer_style_light_angle = 165.0;
  const auto unlinked_canonical =
      serialize_desktop_recipe_as_canonical_v13_with_assets(
          unlinked_light, context, store);
  const auto unlinked_changed_canonical =
      serialize_desktop_recipe_as_canonical_v13_with_assets(
          unlinked_light_changed, context, store);
  require(unlinked_canonical.ok() && unlinked_changed_canonical.ok() &&
              unlinked_canonical.document.recipe_payload ==
                  unlinked_changed_canonical.document.recipe_payload &&
              unlinked_canonical.document.cache_key ==
                  unlinked_changed_canonical.document.cache_key,
          "Changing an unused document angle must keep canonical pixel cache identity stable when every style is unlinked.");
  auto local_light_changed = unlinked_light;
  const auto local_index = layer_index(
      local_light_changed.layers, QStringLiteral("Light B"));
  require(local_index >= 0, "The unlinked local-light layer must be retained.");
  auto local_layer = local_light_changed.layers.at(local_index).toMap();
  local_layer.insert(QStringLiteral("layerStyleGlobalLightAngle"), 81.0);
  local_light_changed.layers[local_index] = local_layer;
  const auto local_light_canonical =
      serialize_desktop_recipe_as_canonical_v13_with_assets(
          local_light_changed, context, store);
  require(local_light_canonical.ok() &&
              local_light_canonical.document.cache_key !=
                  unlinked_canonical.document.cache_key,
          "Changing an unlinked layer's local light must remain part of canonical cache identity.");

  const auto reopened = project_canonical_v13_document_to_desktop_state_with_assets(
      canonical.document, store);
  EditRecipeState reopened_recipe_state;
  EditRecipeState expected_recipe_state;
  if (reopened.state) {
    reopened_recipe_state = parse_edit_recipe_json(
        serialize_edit_recipe_json(*reopened.state));
    expected_recipe_state = parse_edit_recipe_json(
        serialize_edit_recipe_json(state));
  }
  const auto reopened_linked_index = reopened.state
      ? layer_index(reopened_recipe_state.layers, QStringLiteral("Light A")) : -1;
  const auto reopened_unlinked_index = reopened.state
      ? layer_index(reopened_recipe_state.layers, QStringLiteral("Light B")) : -1;
  const bool reopened_lighting_matches = reopened.state &&
      std::abs(reopened.state->document_layer_style_light_angle -
               state.document_layer_style_light_angle) < 1e-9 &&
      reopened_linked_index >= 0 && reopened_unlinked_index >= 0 &&
      photo_layer_style_light::is_linked(
          reopened_recipe_state.layers.at(reopened_linked_index).toMap()) &&
      std::abs(photo_layer_style_light::effective_angle(
                   reopened_recipe_state, reopened_linked_index) - 150.0) < 1e-9 &&
      !photo_layer_style_light::is_linked(
          reopened_recipe_state.layers.at(reopened_unlinked_index).toMap()) &&
      std::abs(photo_layer_style_light::effective_angle(
                   reopened_recipe_state, reopened_unlinked_index) - 72.0) < 1e-9;
  const bool authored_layers_match = reopened.state &&
      expected_recipe_state.layers == reopened_recipe_state.layers;
  if (!reopened.ok() || !reopened.state ||
      std::abs(reopened.state->document_layer_style_light_angle - 150.0) >= 1e-9 ||
      !reopened_lighting_matches || !authored_layers_match) {
    std::cerr << "canonical reopen diagnostic: ok=" << reopened.ok()
              << " angle=" << (reopened.state
                    ? reopened.state->document_layer_style_light_angle : -99999.0)
              << " expected-angle=" << state.document_layer_style_light_angle
              << " authored-layers-equal=" << authored_layers_match
              << " linked-index=" << reopened_linked_index
              << " unlinked-index=" << reopened_unlinked_index
              << " error=" << (reopened.errors.empty()
                    ? std::string("none") : reopened.errors.front().path + ": " +
                        reopened.errors.front().message) << '\n';
    if (reopened.state && expected_recipe_state.layers.size() ==
            reopened_recipe_state.layers.size()) {
      for (int index = 0; index < expected_recipe_state.layers.size(); ++index) {
        const auto expected_layer = expected_recipe_state.layers.at(index).toMap();
        const auto reopened_layer = reopened_recipe_state.layers.at(index).toMap();
        QStringList changed_keys;
        QStringList missing_keys;
        QStringList added_keys;
        for (const auto& key : expected_layer.keys())
          if (!reopened_layer.contains(key))
            missing_keys.append(key);
          else if (expected_layer.value(key) != reopened_layer.value(key))
            changed_keys.append(key);
        for (const auto& key : reopened_layer.keys())
          if (!expected_layer.contains(key)) added_keys.append(key);
        std::cerr << "canonical layer " << index << " changed authored keys: "
                  << changed_keys.join(QLatin1Char(',')).toStdString()
                  << " missing=" << missing_keys.join(QLatin1Char(',')).toStdString()
                  << " added=" << added_keys.join(QLatin1Char(',')).toStdString() << '\n';
      }
    }
  }
  require(reopened.ok() &&
              std::abs(reopened.state->document_layer_style_light_angle - 150.0) < 1e-9 &&
              reopened_lighting_matches && authored_layers_match,
          "Canonical save and reopen must preserve layer link policy and document lighting.");
  auto old_provenance = canonical.document;
  const auto decoded_engine_recipe =
      photo_editor::pipeline::deserialize_edit_recipe_v13(
          canonical.document.recipe_payload.toStdString());
  require(decoded_engine_recipe.has_value(),
          "The canonical engine recipe must decode for the legacy capability guard.");
  old_provenance.cache_key = QString::fromStdString(
      photo_editor::pipeline::edit_recipe_v13_cache_key(
          *decoded_engine_recipe));
  auto old_payload = provenance;
  old_payload.insert(QStringLiteral("schema"),
                     QStringLiteral("photo-editor-edit-recipe-provenance:v6"));
  old_payload.remove(QStringLiteral("documentLayerStyleLightAngle"));
  old_payload.insert(QStringLiteral("requiredCapabilities"),
                     QJsonArray{QStringLiteral("layer-styles-v1")});
  old_provenance.provenance_payload = QString::fromUtf8(
      QJsonDocument(old_payload).toJson(QJsonDocument::Compact));
  require(!project_canonical_v13_document_to_desktop_state_with_assets(
              old_provenance, store).ok(),
          "Canonical provenance v6 must fail closed for linked-light semantics.");
}

void controller_component_conversion_keeps_lighting_scope() {
  QTemporaryDir temporary;
  require(temporary.isValid(), "A temporary component lighting workspace must exist.");
  QImage source(40, 28, QImage::Format_ARGB32);
  source.fill(QColor(QStringLiteral("#283b50")));
  source.setColorSpace(QColorSpace(QColorSpace::SRgb));
  const auto source_path = temporary.filePath(QStringLiteral("component-light-source.png"));
  require(source.save(source_path, "PNG"), "The component lighting fixture must be writable.");

  EditorPreviewController editor;
  editor.loadAsset(QStringLiteral("component-style-light-workflow"), source_path,
                   {}, {}, {}, {}, {}, true);
  settle(editor);
  require(editor.addFillLayer(QStringLiteral("#7c95ae"), QStringLiteral("Linked component style")),
          "The component fixture fill layer must be created.");
  settle(editor);
  auto index = layer_index(editor.photoLayers(), QStringLiteral("Linked component style"));
  require(index >= 0 && editor.addPhotoLayerStyle(index, QStringLiteral("drop_shadow")),
          "The component fixture must have an editable layer style.");
  require(editor.setPhotoLayerStyleLightAngle(index, 35.0) &&
              editor.setPhotoLayerStyleLightLinked(index, true) &&
              editor.setDocumentPhotoLayerStyleLightAngle(150.0),
          "The component fixture must use an explicitly linked document light.");
  settle(editor);

  const auto before_state = EditorPreviewControllerTestAccess::state(editor);
  const auto before_render = render_output_image_with_backend(
      source, before_state, QColorSpace(QColorSpace::SRgb),
      QImage::Format_ARGB32, RenderBackendPreference::CpuOnly);
  require(!before_render.isNull(), "The linked style fixture must render before conversion.");
  const auto layer_id = before_state.layers.at(index).toMap()
      .value(QStringLiteral("id")).toString();
  require(editor.convertPhotoLayerToComponent(index),
          "Converting the linked styled layer into a component must be admitted.");
  settle(editor);

  const auto converted_state = EditorPreviewControllerTestAccess::state(editor);
  const auto instance_index = layer_index(converted_state.layers,
      QStringLiteral("Linked component style"));
  require(instance_index >= 0, "The converted component instance must remain addressable.");
  const auto instance = converted_state.layers.at(instance_index).toMap();
  const auto nested_document = instance.value(QStringLiteral("smartObjectDocument")).toMap();
  const auto nested_layers = nested_document.value(QStringLiteral("layers")).toList();
  const auto nested_child_index = layer_index(nested_layers,
      QStringLiteral("Linked component style"));
  require(nested_document.value(QStringLiteral("version")).toInt() ==
              smart_object::nested_document::kDocumentLayerStyleLightVersion &&
              std::abs(nested_document.value(QStringLiteral("documentLayerStyleLightAngle")).toDouble() -
                       150.0) < 1e-9 && nested_child_index >= 0 &&
              photo_layer_style_light::is_linked(
                  nested_layers.at(nested_child_index).toMap()) &&
              std::abs(nested_layers.at(nested_child_index).toMap()
                           .value(QStringLiteral("layerStyleGlobalLightAngle")).toDouble() -
                       35.0) < 1e-9,
          "Conversion must preserve the linked child policy, its local fallback, and its parent light in nested document v2.");
  const auto after_render = render_output_image_with_backend(
      source, converted_state, QColorSpace(QColorSpace::SRgb),
      QImage::Format_ARGB32, RenderBackendPreference::CpuOnly);
  require(!after_render.isNull() && after_render == before_render,
          "Converting a linked styled layer must preserve its rendered appearance.");

  const auto expanded_after_conversion = expand_smart_documents(
      converted_state.layers, converted_state.mask_stack,
      converted_state.document_layer_style_light_angle);
  const auto expanded_child_id = layer_id + QStringLiteral("::") + layer_id;
  bool nested_angle_is_independent = false;
  for (const auto& value : expanded_after_conversion.layers) {
    const auto child = value.toMap();
    if (child.value(QStringLiteral("id")).toString() == expanded_child_id &&
        photo_layer_style_light::is_linked(child)) {
      nested_angle_is_independent = std::abs(child.value(QString::fromLatin1(
          photo_layer_style_light::kExpandedDocumentAngleField)).toDouble() - 150.0) < 1e-9;
    }
  }
  require(expanded_after_conversion.error.isEmpty() && nested_angle_is_independent,
          "The converted linked style must expand using its captured nested document light.");

  require(editor.setDocumentPhotoLayerStyleLightAngle(210.0),
          "The outer document light must remain editable after component conversion.");
  settle(editor);
  const auto outer_changed_state = EditorPreviewControllerTestAccess::state(editor);
  const auto outer_changed_instance = outer_changed_state.layers.at(instance_index).toMap();
  const auto outer_changed_document = outer_changed_instance
      .value(QStringLiteral("smartObjectDocument")).toMap();
  const auto expanded_after_outer_change = expand_smart_documents(
      outer_changed_state.layers, outer_changed_state.mask_stack,
      outer_changed_state.document_layer_style_light_angle);
  bool nested_angle_stayed_independent = false;
  for (const auto& value : expanded_after_outer_change.layers) {
    const auto child = value.toMap();
    if (child.value(QStringLiteral("id")).toString() == expanded_child_id &&
        photo_layer_style_light::is_linked(child)) {
      nested_angle_stayed_independent = std::abs(child.value(QString::fromLatin1(
          photo_layer_style_light::kExpandedDocumentAngleField)).toDouble() - 150.0) < 1e-9;
    }
  }
  const auto outer_changed_render = render_output_image_with_backend(
      source, outer_changed_state, QColorSpace(QColorSpace::SRgb),
      QImage::Format_ARGB32, RenderBackendPreference::CpuOnly);
  require(std::abs(outer_changed_document.value(
              QStringLiteral("documentLayerStyleLightAngle")).toDouble() - 150.0) < 1e-9 &&
              expanded_after_outer_change.error.isEmpty() && nested_angle_stayed_independent &&
              outer_changed_render == after_render,
          "Changing the parent document light must not affect a component's captured lighting or rendered pixels.");

  restore_history(editor, false);
  require(std::abs(editor.documentPhotoLayerStyleLightAngle() - 150.0) < 1e-9,
          "Undo must restore the outer document angle after conversion.");
  restore_history(editor, false);
  const auto undone_conversion_state = EditorPreviewControllerTestAccess::state(editor);
  index = layer_index(undone_conversion_state.layers,
      QStringLiteral("Linked component style"));
  require(index >= 0 && undone_conversion_state.layers.at(index).toMap()
              .value(QStringLiteral("kind")).toString() == QStringLiteral("fill") &&
              photo_layer_style_light::is_linked(
                  undone_conversion_state.layers.at(index).toMap()) &&
              std::abs(photo_layer_style_light::effective_angle(
                  undone_conversion_state, index) - 150.0) < 1e-9,
          "Undoing conversion must restore the linked layer to its parent lighting scope.");
  require(editor.convertPhotoLayerToComponent(index),
          "Redo coverage requires converting the linked style again.");
  settle(editor);

  const auto serialized_state = EditorPreviewControllerTestAccess::state(editor);
  const auto reopened_state = parse_edit_recipe_json(serialize_edit_recipe_json(serialized_state));
  const auto reopened_instance_index = layer_index(reopened_state.layers,
      QStringLiteral("Linked component style"));
  require(reopened_instance_index >= 0,
          "Recipe serialization must retain the converted component instance.");
  const auto reopened_document = reopened_state.layers.at(reopened_instance_index).toMap()
      .value(QStringLiteral("smartObjectDocument")).toMap();
  QString nested_error;
  const auto nested_bytes = smart_object::nested_document::serialize_document(
      reopened_document, &nested_error);
  const auto reopened_nested_document = smart_object::nested_document::deserialize_document(
      nested_bytes, &nested_error);
  require(nested_error.isEmpty() && !reopened_nested_document.isEmpty() &&
              reopened_nested_document.value(QStringLiteral("version")).toInt() ==
                  smart_object::nested_document::kDocumentLayerStyleLightVersion &&
              std::abs(reopened_nested_document.value(
                  QStringLiteral("documentLayerStyleLightAngle")).toDouble() - 150.0) < 1e-9 &&
              photo_layer_style_light::has_linked_lighting_in_scope(
                  reopened_nested_document.value(QStringLiteral("layers")).toList()),
          "Undo/reopen serialization must retain nested linked lighting and its independent angle.");
}

void helper_locks_and_nested_document_scopes() {
  EditRecipeState locked;
  auto locked_group = QVariantMap{{QStringLiteral("id"), QStringLiteral("locked-group")},
      {QStringLiteral("kind"), QStringLiteral("group")},
      {QStringLiteral("locked"), true}};
  auto locked_child = layer_with_style(QStringLiteral("locked-child"),
      QStringLiteral("Locked child"), true, 10.0);
  locked_child.insert(QStringLiteral("parentId"), QStringLiteral("locked-group"));
  locked.layers = {locked_group, locked_child};
  const auto rejected = photo_layer_style_light::with_document_angle(
      locked, 90.0, QSize(32, 24));
  require(!rejected.error.isEmpty() && !rejected.applied &&
              std::abs(rejected.state.document_layer_style_light_angle + 45.0) < 1e-9,
          "A locked ancestor must reject a document-light change atomically.");

  EditRecipeState state;
  state.document_layer_style_light_angle = 45.0;
  auto root_layer = layer_with_style(QStringLiteral("root-style"),
      QStringLiteral("Root style"), true, 18.0);
  auto nested_layer = layer_with_style(QStringLiteral("nested-style"),
      QStringLiteral("Nested style"), true, -20.0);
  const QVariantMap nested_document{
      {QStringLiteral("schema"), QString::fromLatin1(smart_object::nested_document::kSchema)},
      {QStringLiteral("version"), smart_object::nested_document::kDocumentLayerStyleLightVersion},
      {QStringLiteral("contentId"), QStringLiteral("20000000-0000-4000-8000-000000000001")},
      {QStringLiteral("revision"), 1},
      {QStringLiteral("canvasWidth"), 32},
      {QStringLiteral("canvasHeight"), 24},
      {QStringLiteral("documentLayerStyleLightAngle"), 135.0},
      {QStringLiteral("layers"), QVariantList{nested_layer}},
      {QStringLiteral("maskStack"), QVariantList{}}};
  const QVariantMap component_instance{
      {QStringLiteral("id"), QStringLiteral("component-instance")},
      {QStringLiteral("kind"), QStringLiteral("group")},
      {QStringLiteral("name"), QStringLiteral("Shared component")},
      {QStringLiteral("enabled"), true},
      {QStringLiteral("opacity"), 1.0},
      {QStringLiteral("smartObjectInstanceId"), QStringLiteral("30000000-0000-4000-8000-000000000001")},
      {QStringLiteral("smartObjectDocument"), nested_document}};
  state.layers = {root_layer, component_instance};
  const auto expanded = expand_smart_documents(
      state.layers, {}, state.document_layer_style_light_angle);
  require(expanded.error.isEmpty(), "A linked nested component document must expand.");
  bool root_angle_found = false;
  bool nested_angle_found = false;
  for (const auto& value : expanded.layers) {
    const auto layer = value.toMap();
    if (!photo_layer_style_light::is_linked(layer)) continue;
    const auto angle = layer.value(QString::fromLatin1(
        photo_layer_style_light::kExpandedDocumentAngleField)).toDouble();
    if (layer.value(QStringLiteral("id")).toString() == QStringLiteral("root-style"))
      root_angle_found = std::abs(angle - 45.0) < 1e-9;
    if (layer.value(QStringLiteral("id")).toString() ==
        QStringLiteral("component-instance::nested-style"))
      nested_angle_found = std::abs(angle - 135.0) < 1e-9;
  }
  require(root_angle_found && nested_angle_found,
          "Root and nested component styles must retain independent lighting scopes.");

  EditRecipeState angle_only;
  angle_only.document_layer_style_light_angle = 30.0;
  const auto change = photo_layer_style_light::with_document_angle(
      EditRecipeState{}, 30.0, QSize(32, 24));
  require(change.applied &&
              std::abs(change.state.document_layer_style_light_angle - 30.0) < 1e-9,
          "A configured document angle must persist even when no layer is linked.");
  const auto angle_only_recipe = serialize_edit_recipe_json(change.state);
  const auto angle_only_object = QJsonDocument::fromJson(
      angle_only_recipe.toUtf8()).object();
  require(angle_only_object.value(QStringLiteral("recipeVersion")).toInt() ==
              kPhotoDocumentLayerStyleLightEditRecipeVersion &&
              std::abs(angle_only_object.value(QStringLiteral("documentLayerStyleLightAngle")).toDouble() -
                       30.0) < 1e-9,
          "A non-default document angle without linked layers must still use recipe version 20.");
  const auto angle_only_patch_json = serialize_adjustment_patch_json(
      change.state, all_adjustment_groups(), AdjustmentPatchScope::AssetBound);
  const auto angle_only_patch_object = QJsonDocument::fromJson(
      angle_only_patch_json.toUtf8()).object();
  const auto angle_only_patch = parse_adjustment_patch_json(angle_only_patch_json);
  require(angle_only_patch_object.value(QStringLiteral("version")).toInt() == 4 &&
              angle_only_patch_object.value(QStringLiteral("adjustmentState")).toObject()
                  .contains(QStringLiteral("componentObjectTable")) &&
              angle_only_patch.ok() && angle_only_patch.state &&
              std::abs(angle_only_patch.state->document_layer_style_light_angle - 30.0) < 1e-9,
          "An angle-only asset-bound patch must use the negotiated lighting schema.");
  (void)angle_only;
}

void dimensional_text_schema_and_canonical_roundtrip() {
  QTemporaryDir temporary;
  require(temporary.isValid(), "A temporary dimensional-text workspace must exist.");

  auto settings = PhotoTextDimensionSettings{};
  settings.enabled = true;
  settings.depth = 48.5;
  settings.bevel_width = 3.25;
  settings.rotation_x = 17.0;
  settings.rotation_y = -11.5;
  settings.use_text_color = false;
  settings.front_color = QColor(QStringLiteral("#FFF5EBDD"));
  settings.corner_pin_enabled = true;
  settings.corner_pins = {QPointF(0.08, 0.04), QPointF(0.94, 0.12),
                          QPointF(0.82, 0.91), QPointF(0.13, 0.8)};
  const auto text = dimension_text_layer(QStringLiteral("dimension-text"), settings);

  EditRecipeState state;
  state.layers = {text};
  state.document_layer_style_light_angle = 120.0;
  const auto recipe_json = serialize_edit_recipe_json(state);
  const auto recipe = QJsonDocument::fromJson(recipe_json.toUtf8()).object();
  require(recipe.value(QStringLiteral("recipeVersion")).toInt() ==
              kPhotoTextDimensionEditRecipeVersion &&
              validate_edit_recipe_json(recipe_json).valid,
          "A dimensional text layer must negotiate desktop recipe version 21 and pass strict validation.");
  const auto reopened_recipe = parse_edit_recipe_json(recipe_json);
  require(reopened_recipe.layers.size() == 1 &&
              decode_photo_text_dimension_variant_map(
                  reopened_recipe.layers.front().toMap()
                      .value(QStringLiteral("textDimension")).toMap()) == settings,
          "Desktop recipe save and reopen must preserve the full typed dimensional-text envelope.");

  auto downgraded_recipe = recipe;
  downgraded_recipe.insert(QStringLiteral("recipeVersion"),
                           kPhotoDocumentLayerStyleLightEditRecipeVersion);
  require(!validate_edit_recipe_json(QString::fromUtf8(
              QJsonDocument(downgraded_recipe).toJson(QJsonDocument::Compact))).valid &&
              parse_edit_recipe_json(QString::fromUtf8(
                  QJsonDocument(downgraded_recipe).toJson(QJsonDocument::Compact)))
                  .layers.isEmpty(),
          "Desktop recipe version 20 must fail closed when it contains dimensional text.");

  const auto patch_json = serialize_adjustment_patch_json(
      state, all_adjustment_groups(), AdjustmentPatchScope::AssetBound);
  const auto patch = QJsonDocument::fromJson(patch_json.toUtf8()).object();
  const auto restored_patch = parse_adjustment_patch_json(patch_json);
  require(patch.value(QStringLiteral("version")).toInt() == 5 &&
              restored_patch.ok() && restored_patch.state &&
              decode_photo_text_dimension_variant_map(
                  restored_patch.state->layers.front().toMap()
                      .value(QStringLiteral("textDimension")).toMap()) == settings,
          "Adjustment patch version 5 must preserve dimensional-text settings.");

  QString nested_error;
  const auto nested = smart_object::nested_document::document_from_layers(
      {text}, {}, {QStringLiteral("dimension-text")}, 96, 48, &nested_error);
  require(!nested.isEmpty() && nested.value(QStringLiteral("version")).toInt() ==
              smart_object::nested_document::kTextDimensionVersion &&
              smart_object::nested_document::validate_document(nested, &nested_error),
          "Nested Smart Object documents with dimensional text must negotiate and validate version 3.");
  auto legacy_nested = nested;
  legacy_nested.insert(QStringLiteral("version"),
                       smart_object::nested_document::kVersion);
  require(!smart_object::nested_document::validate_document(legacy_nested, &nested_error),
          "Nested Smart Object document versions 1 and 2 must reject dimensional text.");

  RecipeAssetStore store(temporary.filePath(QStringLiteral("dimension-assets")));
  DesktopRecipeV13Context context;
  context.recipe_id = QStringLiteral("recipe:text-dimension");
  context.source_id = QStringLiteral("source:text-dimension");
  context.decoder_provenance = QStringLiteral("raster:text-dimension-v1");
  const auto canonical = serialize_desktop_recipe_as_canonical_v13_with_assets(
      state, context, store);
  require(canonical.ok(), "Canonical storage must accept dimensional text.");
  const auto provenance = QJsonDocument::fromJson(
      canonical.document.provenance_payload.toUtf8()).object();
  require(provenance.value(QStringLiteral("schema")).toString() ==
              QStringLiteral("photo-editor-edit-recipe-provenance:v8") &&
              provenance.value(QStringLiteral("requiredCapabilities")).toArray().contains(
                  QStringLiteral("text-dimension-v1")) &&
              provenance.value(QStringLiteral("requiredCapabilities")).toArray().contains(
                  QStringLiteral("document-layer-style-light-v1")) &&
              std::abs(provenance.value(QStringLiteral("documentLayerStyleLightAngle")).toDouble() -
                       120.0) < 1e-9,
          "Canonical storage must negotiate v8 dimensional-text and document-light capabilities together.");

  auto changed_state = state;
  auto changed_layer = changed_state.layers.front().toMap();
  auto changed_settings = settings;
  changed_settings.depth = 76.0;
  changed_layer.insert(QStringLiteral("textDimension"),
                       encode_photo_text_dimension_variant_map(changed_settings));
  changed_state.layers.front() = changed_layer;
  const auto changed_canonical =
      serialize_desktop_recipe_as_canonical_v13_with_assets(
          changed_state, context, store);
  require(changed_canonical.ok() &&
              changed_canonical.document.cache_key !=
                  canonical.document.cache_key &&
              changed_canonical.document.cache_key.startsWith(
                  QStringLiteral("edit-recipe-v13-text-dimension-v1-sha256-")),
          "Changing exact dimensional-text settings must change the versioned canonical cache identity.");

  const auto reopened = project_canonical_v13_document_to_desktop_state_with_assets(
      canonical.document, store);
  require(reopened.ok() && reopened.state &&
              reopened.state->layers.size() == 1 &&
              decode_photo_text_dimension_variant_map(
                  reopened.state->layers.front().toMap()
                      .value(QStringLiteral("textDimension")).toMap()) == settings,
          "Canonical save and reopen must retain dimensional-text settings in the layer content asset.");
  auto unsupported_provenance = canonical.document;
  auto unsupported_object = provenance;
  unsupported_object.insert(QStringLiteral("schema"),
                            QStringLiteral("photo-editor-edit-recipe-provenance:v7"));
  unsupported_object.insert(QStringLiteral("documentLayerStyleLightAngle"), -45.0);
  unsupported_object.insert(QStringLiteral("requiredCapabilities"),
                            QJsonArray{QStringLiteral("text-dimension-v1")});
  unsupported_provenance.provenance_payload = QString::fromUtf8(
      QJsonDocument(unsupported_object).toJson(QJsonDocument::Compact));
  require(!project_canonical_v13_document_to_desktop_state_with_assets(
               unsupported_provenance, store).ok(),
          "Canonical provenance v7 must fail closed when dimensional text requires v8.");

  EditRecipeState legacy_typography;
  auto legacy_text = text;
  legacy_text.remove(QStringLiteral("textDimension"));
  legacy_typography.layers = {legacy_text};
  const auto legacy_json = serialize_edit_recipe_json(legacy_typography);
  require(QJsonDocument::fromJson(legacy_json.toUtf8()).object()
                  .value(QStringLiteral("recipeVersion")).toInt() ==
              kPhotoTypographyEditRecipeVersion,
          "Existing textTypography without textDimension must keep recipe version 15.");
}

}  // namespace

int main(int argc, char** argv) {
  QGuiApplication application(argc, argv);
  try {
    controller_history_unlink_and_preset_roundtrip();
    controller_component_conversion_keeps_lighting_scope();
    helper_locks_and_nested_document_scopes();
    dimensional_text_schema_and_canonical_roundtrip();
  } catch (const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
