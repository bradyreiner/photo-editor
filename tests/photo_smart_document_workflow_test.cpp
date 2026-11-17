#include "edit_adjustment_patch.h"
#include "edit_recipe_v13_persistence.h"
#include "editor_preview_controller.h"
#include "photo_component_object_table.h"
#include "photo_render_utils.h"
#include "recipe_asset_store.h"

#include <QColorSpace>
#include <QCoreApplication>
#include <QDir>
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
#include <QThreadPool>

#include <iostream>
#include <stdexcept>

using namespace photo_editor::desktop;

namespace {

void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

EditRecipeState state(const EditorPreviewController& editor) {
  const auto parsed = parse_adjustment_patch_json(
      editor.currentAssetBoundAdjustmentPatchJson());
  require(parsed.ok(), "Current controller recipe must parse.");
  return *parsed.state;
}

void settle(EditorPreviewController& editor) {
  QElapsedTimer timer;
  timer.start();
  do {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    QThread::msleep(1);
  } while ((editor.processing() || editor.professionalToolsBusy() ||
            editor.fullRenderRunning() || editor.selectionWorkBusy() ||
            editor.photoExportBusy() || editor.smartDocumentEditState().value("previewBusy").toBool()) &&
           timer.elapsed() < 60000);
  require(!editor.processing() && !editor.professionalToolsBusy() &&
              !editor.fullRenderRunning() && !editor.selectionWorkBusy() &&
              !editor.photoExportBusy() && !editor.smartDocumentEditState().value("previewBusy").toBool(),
          qPrintable(QStringLiteral("Controller work did not settle: %1")
                         .arg(editor.status())));
}

bool begin_component_edit(EditorPreviewController& editor, int index) {
  const bool admitted = editor.beginPhotoComponentEditing(index);
  settle(editor);
  return admitted &&
      editor.smartDocumentEditState().value(QStringLiteral("active")).toBool();
}

void restore_history(EditorPreviewController& editor, bool redo) {
  const auto asset_id = editor.assetId();
  bool finished = false;
  bool succeeded = false;
  const auto connection = QObject::connect(
      &editor, &EditorPreviewController::historyRestoreFinished, &editor,
      [&asset_id, &finished, &succeeded](const QString& restored_asset,
                                         bool success) {
        if (restored_asset != asset_id) return;
        finished = true;
        succeeded = success;
      });
  if (redo) editor.redo();
  else editor.undo();

  QElapsedTimer timer;
  timer.start();
  while (!finished && timer.elapsed() < 60000) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    QThread::msleep(1);
  }
  QObject::disconnect(connection);
  require(finished, "History restore did not emit its completion signal.");
  require(succeeded, "History restore completed unsuccessfully.");
  settle(editor);
}

int layer_index(const EditorPreviewController& editor, const QString& id) {
  const auto layers = state(editor).layers;
  for (int index = 0; index < layers.size(); ++index) {
    if (layers.at(index).toMap().value(QStringLiteral("id")).toString() == id) {
      return index;
    }
  }
  return -1;
}

QVariantMap layer_by_id(const QVariantList& layers, const QString& id) {
  for (const auto& value : layers) {
    const auto layer = value.toMap();
    if (layer.value(QStringLiteral("id")).toString() == id) return layer;
    for (const auto& key : {QStringLiteral("children"),
                            QStringLiteral("layers")}) {
      const auto nested = layer_by_id(layer.value(key).toList(), id);
      if (!nested.isEmpty()) return nested;
    }
  }
  return {};
}

QVariantMap layer_by_id(const EditRecipeState& recipe, const QString& id) {
  return layer_by_id(recipe.layers, id);
}

QVariantMap layer_by_id(const EditorPreviewController& editor,
                        const QString& id) {
  return layer_by_id(state(editor), id);
}

QStringList component_instance_ids(const EditorPreviewController& editor,
                                   const QString& content_id) {
  QStringList result;
  for (const auto& value : state(editor).layers) {
    const auto layer = value.toMap();
    const auto document =
        layer.value(QStringLiteral("smartObjectDocument")).toMap();
    if (document.value(QStringLiteral("contentId")).toString() == content_id) {
      result.push_back(layer.value(QStringLiteral("id")).toString());
    }
  }
  return result;
}

void align_component_placement(EditorPreviewController& editor,
                               const QString& reference_id,
                               const QString& instance_id) {
  const auto reference = layer_by_id(editor, reference_id);
  const auto instance = layer_by_id(editor, instance_id);
  const auto x = reference.value(QStringLiteral("x")).toInt();
  const auto y = reference.value(QStringLiteral("y")).toInt();
  const auto scale = reference.value(QStringLiteral("scale"), 1.0).toDouble();
  const auto rotation = reference.value(QStringLiteral("rotation")).toDouble();
  if (instance.value(QStringLiteral("x")).toInt() != x ||
      instance.value(QStringLiteral("y")).toInt() != y ||
      instance.value(QStringLiteral("scale"), 1.0).toDouble() != scale ||
      instance.value(QStringLiteral("rotation")).toDouble() != rotation) {
    require(editor.setPhotoLayerTransform(layer_index(editor, instance_id),
                                          x, y, scale, rotation),
            "Could not align the duplicate for pixel-equivalence comparison.");
    settle(editor);
  }
}

QVariantMap nested_document(const EditorPreviewController& editor,
                            const QString& instance_id) {
  return layer_by_id(editor, instance_id)
      .value(QStringLiteral("smartObjectDocument")).toMap();
}

QImage render(const QImage& base, const EditRecipeState& recipe) {
  const auto image = render_output_image_with_backend(
      base, recipe, QColorSpace(QColorSpace::SRgb), QImage::Format_ARGB32,
      RenderBackendPreference::CpuOnly);
  require(!image.isNull(), "CPU layer render returned an empty image.");
  return image;
}

QByteArray file_bytes(const QString& path) {
  QFile file(path);
  require(file.open(QIODevice::ReadOnly), "Could not read the embedded PNG fixture.");
  return file.readAll();
}

QString find_id_by_kind(const EditRecipeState& recipe, const QString& kind) {
  for (const auto& value : recipe.layers) {
    const auto layer = value.toMap();
    if (layer.value(QStringLiteral("kind")).toString() == kind) {
      return layer.value(QStringLiteral("id")).toString();
    }
  }
  return {};
}

QString find_component_id_by_name(const EditRecipeState& recipe,
                                  const QString& name) {
  for (const auto& value : recipe.layers) {
    const auto layer = value.toMap();
    if (layer.value(QStringLiteral("kind")).toString() ==
            QStringLiteral("group") &&
        layer.value(QStringLiteral("name")).toString() == name) {
      return layer.value(QStringLiteral("id")).toString();
    }
  }
  return {};
}

bool text_is(const QVariantMap& document, const QString& child_id,
             const QString& expected) {
  const auto child = layer_by_id(document.value(QStringLiteral("layers")).toList(),
                                 child_id);
  return child.value(QStringLiteral("kind")).toString() ==
             QStringLiteral("text") &&
      child.value(QStringLiteral("text")).toString() == expected;
}

QByteArray embedded_image_bytes(const QVariantMap& child) {
  const auto descriptor = child.value(QStringLiteral("smartObject")).toMap();
  return QByteArray::fromBase64(
      descriptor.value(QStringLiteral("dataBase64")).toString().toLatin1());
}

void test_recipe_format_guards(const EditRecipeState& component_state,
                               const QString& asset_id,
                               const QString& asset_root) {
  const auto legacy_component_object =
      build_edit_recipe_object(component_state);
  require(legacy_component_object.value(QStringLiteral("recipeVersion")).toInt() ==
              kPhotoComponentObjectTableEditRecipeVersion &&
              legacy_component_object.contains(QString::fromLatin1(
                  component_object_table::kTableField)),
          "Editable component recipes must serialize with desktop version 18 and an object table.");
  const auto component_table = legacy_component_object.value(QString::fromLatin1(
      component_object_table::kTableField)).toVariant().toMap();
  require(component_table.value(QStringLiteral("schema")).toString() ==
              QString::fromLatin1(component_object_table::kTableSchema) &&
              component_table.value(QStringLiteral("version")).toInt() ==
                  component_object_table::kTableVersion &&
              !component_table.value(QStringLiteral("objects")).toList().isEmpty() &&
              legacy_component_object.value(QStringLiteral("layers")) ==
                  legacy_component_object.value(QStringLiteral("photoLayers")),
          "Version 18 component tables must be typed, populated, and shared by both layer aliases.");
  const auto legacy_component_json = QString::fromUtf8(
      QJsonDocument(legacy_component_object)
          .toJson(QJsonDocument::Compact));
  const auto legacy_v17 = migrate_edit_recipe_json(legacy_component_json, 17);
  require(legacy_v17.migrated && legacy_v17.errors.isEmpty() &&
              QJsonDocument::fromJson(legacy_v17.recipe_json.toUtf8()).object()
                      .value(QStringLiteral("recipeVersion")).toInt() ==
                  kPhotoComponentEditRecipeVersion &&
              !QJsonDocument::fromJson(legacy_v17.recipe_json.toUtf8()).object()
                   .contains(QString::fromLatin1(component_object_table::kTableField)),
          "Explicit version 17 migration must retain the supported inline component format.");
  const auto downgraded = migrate_edit_recipe_json(legacy_v17.recipe_json, 16);
  require(downgraded.recipe_json.isEmpty() && !downgraded.errors.isEmpty() &&
              downgraded.errors.join(QLatin1Char(' ')).contains(
                  QStringLiteral("components"), Qt::CaseInsensitive),
          "Version 17 inline component JSON must refuse a version 16 downgrade when it contains editable components.");

  const QByteArray binary_payload("legacy-pixel\0bytes\xff\x10", 20);
  EditRecipeState legacy_pixel_state;
  legacy_pixel_state.layers = {QVariantMap{
      {QStringLiteral("id"), QStringLiteral("legacy-binary-pixel")},
      {QStringLiteral("kind"), QStringLiteral("pixel")},
      {QStringLiteral("name"), QStringLiteral("Legacy binary payload")},
      {QStringLiteral("pixelWidth"), 2},
      {QStringLiteral("pixelHeight"), 2},
      {QStringLiteral("pixelData"), binary_payload},
  }};
  const auto legacy_pixel_json = serialize_edit_recipe_json(legacy_pixel_state);
  const auto legacy_pixel_object =
      QJsonDocument::fromJson(legacy_pixel_json.toUtf8()).object();
  const auto legacy_pixel_reopened = parse_edit_recipe_json(legacy_pixel_json);
  const auto reopened_pixel = layer_by_id(
      legacy_pixel_reopened.layers, QStringLiteral("legacy-binary-pixel"));
  require(legacy_pixel_object.value(QStringLiteral("recipeVersion")).toInt() ==
                  kCurrentEditRecipeVersion &&
              reopened_pixel.value(QStringLiteral("pixelData")).toByteArray() ==
                  binary_payload,
          "Ordinary legacy JSON must round-trip binary pixelData byte-for-byte.");

  DesktopRecipeV13Context context;
  context.recipe_id = QStringLiteral("recipe:smart-document-workflow");
  context.source_id = asset_id;
  context.decoder_provenance = QStringLiteral("raster:smart-document-workflow-v1");
  RecipeAssetStore asset_store(asset_root);
  const auto canonical = serialize_desktop_recipe_as_canonical_v13_with_assets(
      component_state, context, asset_store);
  require(canonical.ok(),
          "The canonical document API must serialize a component-bearing recipe.");
  const auto provenance = QJsonDocument::fromJson(
      canonical.document.provenance_payload.toUtf8()).object();
  require(provenance.value(QStringLiteral("schema")).toString() ==
                  QStringLiteral("photo-editor-edit-recipe-provenance:v5") &&
              provenance.value(QStringLiteral("requiredCapabilities")).toArray() ==
                  QJsonArray{QStringLiteral("component-object-assets-v1"),
                             QStringLiteral("smart-object-documents-v1")},
          "Canonical component provenance must declare schema v5 and its required asset capabilities.");

  const auto decoded_recipe =
      photo_editor::pipeline::deserialize_edit_recipe_v13(
          canonical.document.recipe_payload.toStdString());
  require(decoded_recipe && decoded_recipe->input.source.value ==
                                asset_id.toStdString(),
          "The canonical document must remain bound to the current source asset.");
  const auto projected = project_canonical_v13_document_to_desktop_state_with_assets(
      canonical.document, asset_store);
  require(projected.ok() && projected.state->layers == component_state.layers,
          "A supported canonical component document must project back to the exact desktop layer tree.");

  auto legacy_recipe = *decoded_recipe;
  for (auto& canonical_layer : legacy_recipe.layers) {
    for (const auto& source_layer_value : component_state.layers) {
      const auto source_layer = source_layer_value.toMap();
      if (source_layer.value(QStringLiteral("id")).toString().toStdString() !=
          canonical_layer.id.value)
        continue;
      const auto legacy_content_asset = asset_store.putVariant(source_layer);
      require(legacy_content_asset.ok(),
              "A legacy plain component layer asset must be storable.");
      canonical_layer.content_asset = *legacy_content_asset.asset;
      break;
    }
  }
  CanonicalRecipeV13Document legacy_v4_document;
  legacy_v4_document.recipe_payload = QString::fromStdString(
      photo_editor::pipeline::serialize_edit_recipe_v13(legacy_recipe));
  legacy_v4_document.cache_key = QString::fromStdString(
      photo_editor::pipeline::edit_recipe_v13_cache_key(legacy_recipe));
  auto legacy_v4_provenance = provenance;
  legacy_v4_provenance.insert(
      QStringLiteral("schema"),
      QStringLiteral("photo-editor-edit-recipe-provenance:v4"));
  legacy_v4_provenance.insert(
      QStringLiteral("requiredCapabilities"),
      QJsonArray{QStringLiteral("smart-object-documents-v1")});
  legacy_v4_document.provenance_payload = QString::fromUtf8(
      QJsonDocument(legacy_v4_provenance).toJson(QJsonDocument::Compact));
  const auto legacy_v4_projection =
      project_canonical_v13_document_to_desktop_state_with_assets(
          legacy_v4_document, asset_store);
  if (!legacy_v4_projection.ok()) {
    for (const auto& error : legacy_v4_projection.errors)
      std::cerr << "legacy_v4_projection_error=" << error.path << ": "
                << error.message << '\n';
  }
  require(legacy_v4_projection.ok() &&
              legacy_v4_projection.state->layers == component_state.layers,
          "Legacy v4 provenance and plain layer assets must remain readable.");

  auto corrupted = canonical.document;
  auto capabilities = provenance.value(
      QStringLiteral("requiredCapabilities")).toArray();
  capabilities.append(QStringLiteral("unknown-future-capability"));
  auto corrupt_provenance = provenance;
  corrupt_provenance.insert(QStringLiteral("requiredCapabilities"), capabilities);
  corrupted.provenance_payload = QString::fromUtf8(
      QJsonDocument(corrupt_provenance).toJson(QJsonDocument::Compact));
  const auto rejected = project_canonical_v13_document_to_desktop_state_with_assets(
      corrupted, asset_store);
  require(!rejected.ok() && !rejected.errors.empty() &&
              rejected.errors.front().path == "provenance_payload",
          "Canonical projection must reject unknown required component capabilities.");
}

void test_component_document_workflow(const QString& base_path,
                                      const QString& png_path,
                                      const QString& replacement_path,
                                      const QImage& base,
                                      const QByteArray& png_bytes,
                                      const QByteArray& replacement_bytes,
                                      const QString& runtime_path) {
  EditorPreviewController editor;
  const auto asset_id = QStringLiteral("smart-document-workflow-base");
  editor.loadAsset(asset_id, base_path, {}, {}, {}, {}, {});
  settle(editor);

  require(editor.addFillLayer(QStringLiteral("#28445e"),
                              QStringLiteral("Brand background")),
          "Could not add the full-canvas fill child.");
  require(editor.addSourceLayer(png_path, QStringLiteral("Product image")),
          "Could not add the PNG image child.");
  require(editor.addTextLayer(QStringLiteral("EDITABLE PRODUCT TITLE"),
                              QStringLiteral("Headline")),
          "Could not add the editable text child.");
  settle(editor);

  const auto source_recipe = state(editor);
  const auto fill_id = find_id_by_kind(source_recipe, QStringLiteral("fill"));
  const auto source_id = find_id_by_kind(source_recipe, QStringLiteral("source"));
  const auto text_id = find_id_by_kind(source_recipe, QStringLiteral("text"));
  require(!fill_id.isEmpty() && !source_id.isEmpty() && !text_id.isEmpty(),
          "The grouped source must contain fill, PNG image, and editable text layers.");

  const auto before_group_render = render(base, source_recipe);
  const QVariantList selected_indices{
      layer_index(editor, text_id), layer_index(editor, source_id),
      layer_index(editor, fill_id)};
  require(selected_indices.at(0).toInt() >= 0 &&
              selected_indices.at(1).toInt() >= 0 &&
              selected_indices.at(2).toInt() >= 0 &&
              editor.groupPhotoLayers(selected_indices,
                                      QStringLiteral("Reusable product")),
          "Could not group the fill, PNG image, and text children.");
  settle(editor);
  const auto group_id = find_component_id_by_name(
      state(editor), QStringLiteral("Reusable product"));
  require(!group_id.isEmpty(), "The grouped component needs a stable layer ID.");

  const auto before_convert_state = state(editor);
  const auto before_convert_render = render(base, before_convert_state);
  require(editor.convertPhotoLayerToComponent(layer_index(editor, group_id)),
          "Component conversion was not admitted.");
  settle(editor);

  auto instance = layer_by_id(editor, group_id);
  auto document = instance.value(QStringLiteral("smartObjectDocument")).toMap();
  require(!document.isEmpty() &&
              instance.value(QStringLiteral("kind")).toString() ==
                  QStringLiteral("group") &&
              instance.value(QStringLiteral("children")).toList().isEmpty(),
          "Conversion must replace the group contents with a component document.");
  require(layer_index(editor, source_id) == -1 &&
              layer_index(editor, text_id) == -1 &&
              layer_index(editor, fill_id) == -1,
          "The full-editor layer stack must remain shallow; nested children are reached through the explicit Open Contents action.");
  const auto fill_child = layer_by_id(
      document.value(QStringLiteral("layers")).toList(), fill_id);
  const auto source_child = layer_by_id(
      document.value(QStringLiteral("layers")).toList(), source_id);
  const auto text_child = layer_by_id(
      document.value(QStringLiteral("layers")).toList(), text_id);
  require(fill_child.value(QStringLiteral("kind")).toString() ==
              QStringLiteral("fill") &&
              source_child.value(QStringLiteral("kind")).toString() ==
                  QStringLiteral("source") &&
              text_child.value(QStringLiteral("kind")).toString() ==
                  QStringLiteral("text") &&
              text_child.value(QStringLiteral("text")).toString() ==
                  QStringLiteral("EDITABLE PRODUCT TITLE") &&
              !text_child.contains(QStringLiteral("pixelData")),
          "Nested content must retain editable fill/text/image definitions without flattening text.");

  const auto embedded_source =
      source_child.value(QStringLiteral("smartObject")).toMap();
  const auto embedded_bytes = QByteArray::fromBase64(
      embedded_source.value(QStringLiteral("dataBase64")).toString().toLatin1());
  require(embedded_source.value(QStringLiteral("policy")).toString() ==
              QStringLiteral("embedded") &&
              embedded_bytes == png_bytes &&
              source_child.value(QStringLiteral("sourcePath")).toString().isEmpty(),
          "The conversion worker must embed the exact PNG bytes and release its source path.");
  require(QFile::remove(png_path),
          "The original PNG should be removable after embedded conversion.");

  const auto converted_render = render(base, state(editor));
  require(converted_render == before_convert_render &&
              converted_render == before_group_render,
          "Converting the group to a component must preserve rendered pixels.");

  const auto shared_document_id =
      document.value(QStringLiteral("contentId")).toString();
  require(!shared_document_id.isEmpty(),
          "The component needs a durable shared content identity.");
  const auto first_instance_id = instance.value(QStringLiteral("smartObjectInstanceId"))
                                     .toString();
  require(!first_instance_id.isEmpty(),
          "The component instance needs an independent stable identity.");

  require(editor.duplicatePhotoComponent(layer_index(editor, group_id)),
          "Could not duplicate the shared component.");
  settle(editor);
  QString second_instance_id;
  for (const auto& candidate :
       component_instance_ids(editor, shared_document_id)) {
    if (candidate != group_id) second_instance_id = candidate;
  }
  require(!second_instance_id.isEmpty(),
          "The duplicate must share content under a new layer identity.");

  require(editor.duplicatePhotoComponent(layer_index(editor, group_id)),
          "Could not create a second shared component placement.");
  settle(editor);
  QString independent_instance_id;
  for (const auto& candidate :
       component_instance_ids(editor, shared_document_id)) {
    if (candidate != group_id && candidate != second_instance_id) {
      independent_instance_id = candidate;
    }
  }
  require(!independent_instance_id.isEmpty() &&
              component_instance_ids(editor, shared_document_id).size() == 3,
          "Three component placements should initially share one document.");

  const auto shared_instances = component_instance_ids(editor, shared_document_id);
  QStringList smart_instance_ids;
  for (const auto& id : shared_instances) {
    smart_instance_ids.push_back(
        layer_by_id(editor, id)
            .value(QStringLiteral("smartObjectInstanceId")).toString());
  }
  require(smart_instance_ids.size() == 3 &&
              smart_instance_ids.at(0) != smart_instance_ids.at(1) &&
              smart_instance_ids.at(0) != smart_instance_ids.at(2) &&
              smart_instance_ids.at(1) != smart_instance_ids.at(2),
          "Shared placements must have distinct instance identities.");
  align_component_placement(editor, group_id, second_instance_id);
  align_component_placement(editor, group_id, independent_instance_id);
  require(render(base, state(editor)) == before_convert_render,
          "Duplicating the editable component must retain rendered identity.");

  const auto before_independent = layer_by_id(editor, independent_instance_id);
  const auto before_independent_doc =
      before_independent.value(QStringLiteral("smartObjectDocument")).toMap();
  require(editor.makePhotoComponentIndependent(
              layer_index(editor, independent_instance_id)),
          "Make Independent did not apply to the duplicate component.");
  settle(editor);
  instance = layer_by_id(editor, independent_instance_id);
  document = instance.value(QStringLiteral("smartObjectDocument")).toMap();
  auto expected_independent_document = before_independent_doc;
  expected_independent_document.insert(QStringLiteral("contentId"),
      document.value(QStringLiteral("contentId")));
  require(!document.value(QStringLiteral("contentId")).toString().isEmpty() &&
              document.value(QStringLiteral("contentId")).toString() !=
                  shared_document_id &&
              document == expected_independent_document &&
              instance.value(QStringLiteral("smartObjectInstanceId")) ==
                  before_independent.value(QStringLiteral("smartObjectInstanceId")) &&
              render(base, state(editor)) == before_convert_render,
          "Make Independent must change content identity without changing appearance or placement.");

  const auto before_draft = state(editor);
  const auto shared_revision = nested_document(editor, group_id)
                                   .value(QStringLiteral("revision")).toInt();
  require(begin_component_edit(editor, layer_index(editor, group_id)),
          "Could not enter the isolated component-document editor.");
  auto edit_state = editor.smartDocumentEditState();
  const auto session_id = edit_state.value(QStringLiteral("sessionId")).toString();
  require(edit_state.value(QStringLiteral("active")).toBool() &&
              !session_id.isEmpty() &&
              text_is({{QStringLiteral("layers"),
                       edit_state.value(QStringLiteral("layers"))}},
                     text_id, QStringLiteral("EDITABLE PRODUCT TITLE")),
          "The edit session must expose a separate draft containing the original editable text.");

  const auto revised_text = QStringLiteral("REVISED PRODUCT TITLE");
  require(editor.changePhotoComponentChild(
              text_id, {{QStringLiteral("text"), revised_text}}),
          "The nested text layer should accept direct editable changes.");
  edit_state = editor.smartDocumentEditState();
  const auto draft_layers = edit_state.value(QStringLiteral("layers")).toList();
  const auto draft_text = layer_by_id(draft_layers, text_id);
  require(draft_text.value(QStringLiteral("text")).toString() == revised_text &&
              state(editor) == before_draft &&
              render(base, state(editor)) == before_convert_render,
          "Draft edits must remain isolated from the parent recipe and preview until Apply.");

  const int history_index_before_apply = editor.editHistoryCurrentIndex();
  require(editor.applyPhotoComponentEditing(),
          "Applying the nested component edit was not admitted.");
  settle(editor);
  const auto after_apply = state(editor);
  const auto original_doc_after_apply = nested_document(editor, group_id);
  const auto shared_doc_after_apply = nested_document(editor, second_instance_id);
  const auto independent_doc_after_apply =
      nested_document(editor, independent_instance_id);
  require(editor.editHistoryCurrentIndex() == history_index_before_apply + 1 &&
              original_doc_after_apply.value(QStringLiteral("revision")).toInt() >
                  shared_revision &&
              shared_doc_after_apply == original_doc_after_apply &&
              text_is(original_doc_after_apply, text_id, revised_text) &&
              text_is(shared_doc_after_apply, text_id, revised_text) &&
              text_is(independent_doc_after_apply, text_id,
                      QStringLiteral("EDITABLE PRODUCT TITLE")),
          "One Apply history step must update shared instances and leave the independent document unchanged.");
  const auto applied_render = render(base, after_apply);
  require(applied_render != before_convert_render,
          "Applying an inner text edit must update rendered component output.");

  const auto canonical_asset_patch = editor.currentAssetBoundAdjustmentPatchJson();
  const auto canonical_decode = parse_adjustment_patch_json(canonical_asset_patch);
  require(canonical_decode.ok() &&
              canonical_decode.state->layers == after_apply.layers,
          "The canonical asset-bound recipe must project the inline component document.");
  test_recipe_format_guards(
      after_apply, asset_id,
      QDir(runtime_path).filePath(QStringLiteral("canonical-assets")));

  EditorPreviewController reopened;
  reopened.loadAsset(asset_id, base_path, {}, {}, {}, {}, canonical_asset_patch);
  settle(reopened);
  require(state(reopened).layers == after_apply.layers &&
              render(base, state(reopened)) == applied_render &&
              text_is(nested_document(reopened, group_id), text_id, revised_text),
          "Reopening the canonical recipe must retain editable nested text and matching output.");

  const auto reopened_state = state(reopened);
  require(reopened_state.output_color_space == QStringLiteral("sRGB"),
          "The component export check uses the recipe's explicit sRGB output setting.");
  const auto exported_component_path = QDir(runtime_path).filePath(
      QStringLiteral("reopened-component.png"));
  require(reopened.startFlattenedPhotoExport(exported_component_path),
          "The reopened component recipe must enter the real flattened PNG export path.");
  settle(reopened);
  require(reopened.photoExportError().isEmpty(),
          "The reopened component PNG export should finish without an export error.");
  const QImage exported_component(exported_component_path);
  const auto ordinary_component_render =
      render(base, reopened_state).convertToFormat(QImage::Format_RGBA8888);
  const auto exported_component_pixels =
      exported_component.convertToFormat(QImage::Format_RGBA8888);
  require(!exported_component.isNull() &&
              exported_component.size() == base.size() &&
              ordinary_component_render.size() == exported_component_pixels.size(),
          "The real PNG export must preserve the original full-size output dimensions.");
  for (int y = 0; y < ordinary_component_render.height(); ++y) {
    for (int x = 0; x < ordinary_component_render.width(); ++x) {
      const auto expected_pixel = ordinary_component_render.pixel(x, y);
      const auto exported_pixel = exported_component_pixels.pixel(x, y);
      if (qAbs(qRed(expected_pixel) - qRed(exported_pixel)) > 2 ||
          qAbs(qGreen(expected_pixel) - qGreen(exported_pixel)) > 2 ||
          qAbs(qBlue(expected_pixel) - qBlue(exported_pixel)) > 2 ||
          qAbs(qAlpha(expected_pixel) - qAlpha(exported_pixel)) > 2) {
        require(false,
                "The reopened component PNG pixels must match the ordinary sRGB render within 2 code values.");
      }
    }
  }
  const auto evidence_directory = qEnvironmentVariable("LOOKSY_PROFESSIONAL_EVIDENCE_DIR");
  if (!evidence_directory.isEmpty()) {
    require(QDir().mkpath(evidence_directory), "Create the component export evidence directory.");
    QFile receipt(QDir(evidence_directory).filePath(QStringLiteral("component-ordinary-export.png")));
    const auto exported_bytes = file_bytes(exported_component_path);
    require(receipt.open(QIODevice::WriteOnly) && receipt.write(exported_bytes) == exported_bytes.size(),
            "Preserve the real component PNG export evidence.");
    require(ordinary_component_render.save(QDir(evidence_directory).filePath(QStringLiteral("component-export-reference.png"))),
            "Preserve the ordinary render reference evidence.");
  }

  restore_history(editor, false);
  require(state(editor) == before_draft &&
              render(base, state(editor)) == before_convert_render &&
              text_is(nested_document(editor, group_id), text_id,
                      QStringLiteral("EDITABLE PRODUCT TITLE")),
          "Undo must restore the pre-Apply shared component document and pixels.");
  restore_history(editor, true);
  require(state(editor) == after_apply &&
              render(base, state(editor)) == applied_render &&
              nested_document(editor, independent_instance_id) ==
                  independent_doc_after_apply,
          "Redo must restore the shared edit while preserving independent content.");

  const auto applied_state = state(editor);
  const auto original_shared_source = layer_by_id(
      nested_document(editor, group_id).value(QStringLiteral("layers")).toList(),
      source_id);
  const auto original_independent_source = layer_by_id(
      nested_document(editor, independent_instance_id)
          .value(QStringLiteral("layers")).toList(), source_id);
  const auto original_content_id = original_shared_source
      .value(QStringLiteral("smartObject")).toMap()
      .value(QStringLiteral("contentId")).toString();
  const auto original_instance_id = original_shared_source
      .value(QStringLiteral("smartObjectInstanceId")).toString();
  const auto original_independent_doc =
      nested_document(editor, independent_instance_id);
  require(embedded_image_bytes(original_shared_source) == png_bytes &&
              embedded_image_bytes(original_independent_source) == png_bytes,
          "Shared and independent component documents must begin with the same exact embedded image bytes.");

  const auto history_before_image_cancel = editor.editHistoryCurrentIndex();
  require(begin_component_edit(editor, layer_index(editor, group_id)),
          "Could not open the source component for replacement preview.");
  auto image_edit_state = editor.smartDocumentEditState();
  auto image_session_id = image_edit_state.value(QStringLiteral("sessionId")).toString();
  require(!image_session_id.isEmpty() &&
              editor.replacePhotoComponentImage(
                  image_session_id, source_id, replacement_path),
          "The component source replacement should be admitted asynchronously.");
  settle(editor);
  require(state(editor) == applied_state &&
              editor.smartDocumentEditState().value(QStringLiteral("active")).toBool(),
          "Replacing an image in the open document must remain isolated from parent layers until Apply.");
  editor.cancelPhotoComponentEditing();
  settle(editor);
  require(state(editor) == applied_state &&
              editor.editHistoryCurrentIndex() == history_before_image_cancel &&
              embedded_image_bytes(layer_by_id(
                  nested_document(editor, group_id)
                      .value(QStringLiteral("layers")).toList(), source_id)) == png_bytes,
          "Cancel must discard an asynchronously replaced source image and retain the original embedded descriptor.");

  require(begin_component_edit(editor, layer_index(editor, group_id)),
          "Could not reopen the component to apply a replacement image.");
  image_edit_state = editor.smartDocumentEditState();
  image_session_id = image_edit_state.value(QStringLiteral("sessionId")).toString();
  require(editor.replacePhotoComponentImage(
              image_session_id, source_id, replacement_path),
          "The replacement image should be admitted for the Apply path.");
  settle(editor);
  const int history_before_image_apply = editor.editHistoryCurrentIndex();
  require(state(editor) == applied_state &&
              editor.applyPhotoComponentEditing(),
          "An image replacement must remain a draft until its explicit Apply action.");
  settle(editor);
  const auto after_image_apply = state(editor);
  const auto replaced_shared_doc = nested_document(editor, group_id);
  const auto replaced_second_doc = nested_document(editor, second_instance_id);
  const auto untouched_independent_doc =
      nested_document(editor, independent_instance_id);
  const auto replaced_shared_source = layer_by_id(
      replaced_shared_doc.value(QStringLiteral("layers")).toList(), source_id);
  const auto replaced_second_source = layer_by_id(
      replaced_second_doc.value(QStringLiteral("layers")).toList(), source_id);
  const auto untouched_independent_source = layer_by_id(
      untouched_independent_doc.value(QStringLiteral("layers")).toList(), source_id);
  const auto replaced_descriptor =
      replaced_shared_source.value(QStringLiteral("smartObject")).toMap();
  require(editor.editHistoryCurrentIndex() == history_before_image_apply + 1 &&
              !original_content_id.isEmpty() &&
              replaced_shared_doc == replaced_second_doc &&
              replaced_descriptor.value(QStringLiteral("policy")).toString() ==
                  QStringLiteral("embedded") &&
              embedded_image_bytes(replaced_shared_source) == replacement_bytes &&
              embedded_image_bytes(replaced_second_source) == replacement_bytes &&
              replaced_descriptor.value(QStringLiteral("contentId")).toString() ==
                  original_content_id &&
              replaced_shared_source.value(QStringLiteral("id")).toString() == source_id &&
              replaced_shared_source.value(QStringLiteral("smartObjectInstanceId")) ==
                  original_instance_id &&
              untouched_independent_doc == original_independent_doc &&
              embedded_image_bytes(untouched_independent_source) == png_bytes &&
              untouched_independent_source.value(QStringLiteral("smartObject")).toMap()
                      .value(QStringLiteral("contentId")).toString() ==
                  original_independent_source.value(QStringLiteral("smartObject")).toMap()
                      .value(QStringLiteral("contentId")).toString(),
          "Applying a replacement must create one history step, update all shared instances with preserved embedded identities, and leave the independent component unchanged.");
  const auto replaced_render = render(base, after_image_apply);
  require(replaced_render != applied_render,
          "Applying a visibly different embedded image must update component output.");
  restore_history(editor, false);
  require(state(editor) == applied_state &&
              render(base, state(editor)) == applied_render,
          "Undo must restore the prior shared source image and exact output.");
  restore_history(editor, true);
  require(state(editor) == after_image_apply &&
              render(base, state(editor)) == replaced_render &&
              nested_document(editor, independent_instance_id) ==
                  untouched_independent_doc,
          "Redo must restore the shared replacement while preserving independent embedded source content.");
  const auto post_image_state = state(editor);

  const int history_index_before_cancel = editor.editHistoryCurrentIndex();
  require(begin_component_edit(editor, layer_index(editor, group_id)) &&
              editor.changePhotoComponentChild(
                  text_id, {{QStringLiteral("text"),
                             QStringLiteral("CANCELLED DRAFT")}}),
          "Could not author an isolated draft for Cancel.");
  editor.cancelPhotoComponentEditing();
  settle(editor);
  require(!editor.smartDocumentEditState().value(QStringLiteral("active")).toBool() &&
              state(editor) == post_image_state &&
              editor.editHistoryCurrentIndex() == history_index_before_cancel &&
              render(base, state(editor)) == replaced_render,
          "Cancel must discard the isolated draft without changing parent content or history.");

  require(editor.setPhotoLayerLocked(layer_index(editor, group_id), true),
          "Could not lock the selected component parent.");
  settle(editor);
  const auto locked_parent_state = state(editor);
  require(!begin_component_edit(editor, layer_index(editor, group_id)) &&
              state(editor) == locked_parent_state,
          "A locked component parent must reject nested editing without mutation.");
  require(editor.setPhotoLayerLocked(layer_index(editor, group_id), false),
          "Could not unlock the selected component parent.");
  settle(editor);

  // Parent edits or an asset switch must invalidate a draft captured against
  // an older document revision. If the controller permits the parent edit,
  // applying that stale draft must not overwrite the newer parent state.
  require(begin_component_edit(editor, layer_index(editor, group_id)) &&
              editor.changePhotoComponentChild(
                  text_id, {{QStringLiteral("text"),
                             QStringLiteral("STALE DRAFT")}}),
          "Could not prepare a stale-parent draft.");
  const auto outer = layer_by_id(editor, group_id);
  const auto moved = editor.setPhotoLayerTransform(
      layer_index(editor, group_id), outer.value(QStringLiteral("x")).toInt() + 3,
      outer.value(QStringLiteral("y")).toInt(),
      outer.value(QStringLiteral("scale"), 1.0).toDouble(),
      outer.value(QStringLiteral("rotation")).toDouble());
  settle(editor);
  if (moved) {
    const auto parent_after_move = state(editor);
    static_cast<void>(editor.applyPhotoComponentEditing());
    settle(editor);
    require(state(editor) == parent_after_move &&
                text_is(nested_document(editor, group_id), text_id, revised_text),
            "A stale draft must not overwrite a newer parent transform or shared content.");
  } else {
    require(state(editor) == post_image_state,
            "The controller may reject parent edits while a nested draft is open, but must leave the parent unchanged.");
  }
  if (editor.smartDocumentEditState().value(QStringLiteral("active")).toBool()) {
    editor.cancelPhotoComponentEditing();
    settle(editor);
  }

  require(editor.setPhotoLayerLocked(layer_index(editor, second_instance_id), true),
          "Could not lock a shared component sibling.");
  settle(editor);
  const auto locked_sibling_state = state(editor);
  const bool sibling_edit_started =
      begin_component_edit(editor, layer_index(editor, group_id));
  if (sibling_edit_started) {
    require(editor.changePhotoComponentChild(
                text_id, {{QStringLiteral("text"),
                           QStringLiteral("LOCKED SIBLING DRAFT")}}),
            "Could not prepare an edit against a locked shared sibling.");
    static_cast<void>(editor.applyPhotoComponentEditing());
    settle(editor);
    if (editor.smartDocumentEditState().value(QStringLiteral("active")).toBool()) {
      editor.cancelPhotoComponentEditing();
      settle(editor);
    }
  }
  require(state(editor) == locked_sibling_state &&
              text_is(nested_document(editor, group_id), text_id, revised_text) &&
              text_is(nested_document(editor, second_instance_id), text_id,
                      revised_text),
          "A locked shared sibling must prevent partial component-document publication.");
  require(editor.setPhotoLayerLocked(layer_index(editor, second_instance_id), false),
          "Could not unlock the shared component sibling.");
  settle(editor);

  require(begin_component_edit(editor, layer_index(editor, group_id)) &&
              editor.changePhotoComponentChild(
                  text_id, {{QStringLiteral("text"),
                             QStringLiteral("STALE ASSET DRAFT")}}),
          "Could not prepare a stale-asset draft.");
  editor.loadAsset(QStringLiteral("smart-document-workflow-switched"),
                   base_path, {}, {}, {}, {}, {});
  settle(editor);
  const auto switched_asset_state = state(editor);
  static_cast<void>(editor.applyPhotoComponentEditing());
  settle(editor);
  require(editor.assetId() == QStringLiteral("smart-document-workflow-switched") &&
              !editor.smartDocumentEditState().value(QStringLiteral("active")).toBool() &&
              state(editor) == switched_asset_state &&
              layer_by_id(state(editor), group_id).isEmpty(),
          "An edit session captured against another asset must not publish stale content.");
}

}  // namespace

int main(int argc, char** argv) {
  QTemporaryDir runtime;
  if (!runtime.isValid()) return 1;
  qputenv("PHOTO_EDITOR_APP_DATA_DIR",
          runtime.filePath(QStringLiteral("app-data")).toUtf8());
  qputenv("PHOTO_EDITOR_CACHE_DIR",
          runtime.filePath(QStringLiteral("cache")).toUtf8());
  qputenv("PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE", "1");
  QGuiApplication application(argc, argv);

  struct DrainWorkers {
    ~DrainWorkers() { QThreadPool::globalInstance()->waitForDone(60000); }
  } drain_workers;

  try {
    QImage base(256, 144, QImage::Format_RGB32);
    base.setColorSpace(QColorSpace(QColorSpace::SRgb));
    base.fill(QColor(QStringLiteral("#101923")));
    const auto base_path = runtime.filePath(QStringLiteral("base.png"));
    require(base.save(base_path), "Could not save the base image fixture.");

    QImage png(48, 48, QImage::Format_ARGB32);
    png.setColorSpace(QColorSpace(QColorSpace::SRgb));
    png.fill(Qt::transparent);
    for (int y = 0; y < png.height(); ++y) {
      for (int x = 0; x < png.width(); ++x) {
        if ((x + y) % 3 != 0) {
          png.setPixelColor(x, y, QColor(QStringLiteral("#df4b32")));
        }
      }
    }
    const auto png_path = runtime.filePath(QStringLiteral("product.png"));
    require(png.save(png_path, "PNG"), "Could not save the PNG child fixture.");
    const auto png_bytes = file_bytes(png_path);

    QImage replacement(48, 48, QImage::Format_ARGB32);
    replacement.setColorSpace(QColorSpace(QColorSpace::SRgb));
    replacement.fill(QColor(QStringLiteral("#2378d4")));
    const auto replacement_path =
        runtime.filePath(QStringLiteral("replacement.png"));
    require(replacement.save(replacement_path, "PNG"),
            "Could not save the replacement PNG fixture.");
    const auto replacement_bytes = file_bytes(replacement_path);

    test_component_document_workflow(
        base_path, png_path, replacement_path, base, png_bytes,
        replacement_bytes, runtime.path());
  } catch (const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }

  std::cout << "photo_smart_document_workflow_test passed\n";
  return 0;
}
