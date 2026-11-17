#include "edit_recipe_state.h"
#include "editor_preview_controller.h"
#include "edit_adjustment_patch.h"
#include "photo_editor/pipeline/layer_styles.h"
#include "photo_layer_style_codec.h"
#include "photo_layer_style_validation.h"
#include "photo_render_utils.h"

#include <QColorSpace>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QTemporaryDir>
#include <QThread>
#include <QThreadPool>
#include <QUuid>

#include <algorithm>
#include <cstring>
#include <iostream>
#include <stdexcept>

namespace photo_editor::desktop {
struct EditorPreviewControllerTestAccess {
  static EditState state(const EditorPreviewController& controller) {
    return controller.currentState();
  }
};
}  // namespace photo_editor::desktop

using namespace photo_editor::desktop;

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
  require(idle(), "The layer style controller render must settle.");
}

bool same_pixels(const QImage& left, const QImage& right) {
  if (left.isNull() || right.isNull() || left.size() != right.size()) return false;
  const auto a = left.convertToFormat(QImage::Format_RGBA8888);
  const auto b = right.convertToFormat(QImage::Format_RGBA8888);
  const auto bytes = static_cast<std::size_t>(a.width()) * 4U;
  for (int y = 0; y < a.height(); ++y) {
    if (std::memcmp(a.constScanLine(y), b.constScanLine(y), bytes) != 0) return false;
  }
  return true;
}

QImage render(const QImage& source, const EditorPreviewController& editor) {
  const auto image = render_output_image_with_backend(
      source, EditorPreviewControllerTestAccess::state(editor),
      QColorSpace(QColorSpace::SRgb), QImage::Format_ARGB32,
      RenderBackendPreference::CpuOnly);
  require(!image.isNull(), "The layer style workflow CPU render must be available.");
  return image;
}

QVariantMap layer_at(EditorPreviewController& editor, int index) {
  const auto layers = editor.photoLayers();
  require(index >= 0 && index < layers.size(), "The expected style target must exist.");
  return layers.at(index).toMap();
}

EditRecipeState recipe(const EditorPreviewController& editor, const QString& phase) {
  const auto json = editor.currentAssetBoundAdjustmentPatchJson();
  const auto parsed = parse_adjustment_patch_json(json);
  if (!parsed.ok()) {
    const auto state = EditorPreviewControllerTestAccess::state(editor);
    QString storage_error;
    const auto storage_valid = validate_photo_layer_style_storage(
        state.layers, &storage_error, 0, &state.mask_stack);
    const auto direct_serialization = serialize_adjustment_patch_json(
        state, all_adjustment_groups(), AdjustmentPatchScope::AssetBound);
    std::cerr << "Style storage diagnostic during '" << phase.toStdString()
              << "': valid=" << (storage_valid ? "true" : "false")
              << " error='" << storage_error.toStdString() << "'"
              << " direct_patch_bytes=" << direct_serialization.toUtf8().size()
              << " masks=" << state.mask_stack.size()
              << " layers=" << state.layers.size() << '\n';
    for (const auto& layer_value : state.layers) {
      const auto layer = layer_value.toMap();
      if (!layer.contains(QStringLiteral("layerStyleStack"))) continue;
      const auto serialized = layer.value(QStringLiteral("layerStyleStack"));
      std::cerr << "  style layer id='"
                << layer.value(QStringLiteral("id")).toString().toStdString()
                << "' kind='" << layer.value(QStringLiteral("kind")).toString().toStdString()
                << "' stack_type='"
                << (serialized.metaType().name() != nullptr ? serialized.metaType().name() : "unknown")
                << "' stack_bytes=" << serialized.toString().toUtf8().size()
                << " angle="
                << layer.value(QStringLiteral("layerStyleGlobalLightAngle"), -45.0).toDouble()
                << '\n';
    }
    throw std::runtime_error(QStringLiteral(
        "Style workflow recipe parse failed during '%1': %2 (patch bytes=%3).")
        .arg(phase, parsed.error)
        .arg(static_cast<qlonglong>(json.toUtf8().size())).toStdString());
  }
  return *parsed.state;
}

QVariantMap layer_by_id(const QVariantList& layers, const QString& id) {
  for (const auto& value : layers) {
    const auto layer = value.toMap();
    if (layer.value(QStringLiteral("id")).toString() == id) return layer;
  }
  return {};
}

QVariantList masks_for_set(const QVariantList& masks, const QString& set_id) {
  QVariantList result;
  for (const auto& value : masks)
    if (value.toMap().value(QStringLiteral("maskSetId")).toString() == set_id)
      result.append(value);
  return result;
}

void commit_style_mask_brush(EditorPreviewController& editor, int layer_index,
                             double x, double y, bool erase) {
  require(editor.beginPhotoPaintStroke(layer_index,
              {{QStringLiteral("erase"), erase},
               {QStringLiteral("color"), QStringLiteral("#1688ff")},
               {QStringLiteral("size"), 12.0},
               {QStringLiteral("hardness"), 1.0},
               {QStringLiteral("opacity"), 1.0},
               {QStringLiteral("flow"), 1.0},
               {QStringLiteral("pressureSize"), false}}),
          "Begin a brush stroke on the selected style mask.");
  editor.addPhotoPaintPoint(x, y);
  require(editor.finishPhotoPaintStroke(), "Commit the style-mask brush stroke.");
  settle(editor);
  require(editor.photoPaintError().isEmpty(), "The style-mask brush must finish without an error.");
}

int index_by_id(const QVariantList& layers, const QString& id) {
  for (int index = 0; index < layers.size(); ++index) {
    if (layers.at(index).toMap().value(QStringLiteral("id")).toString() == id)
      return index;
  }
  return -1;
}

QString newly_added_layer_id(const QVariantList& before, const QVariantList& after) {
  for (const auto& value : after) {
    const auto candidate_id = value.toMap().value(QStringLiteral("id")).toString();
    if (candidate_id.isEmpty()) continue;
    const auto existed = std::any_of(before.cbegin(), before.cend(),
        [&candidate_id](const QVariant& previous) {
          return previous.toMap().value(QStringLiteral("id")).toString() == candidate_id;
        });
    if (!existed) return candidate_id;
  }
  return {};
}

QVariantMap without_style_fields(QVariantMap layer) {
  layer.remove(QStringLiteral("layerStyleStack"));
  layer.remove(QStringLiteral("layerStyleGlobalLightAngle"));
  return layer;
}

QVariantList without_item_field(QVariantList items, const QString& field) {
  for (auto& value : items) {
    auto item = value.toMap();
    item.remove(field);
    value = item;
  }
  return items;
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
  require(finished && succeeded, "Layer style history restore must succeed.");
  settle(editor);
}

int run_workflow() {
  QTemporaryDir temporary;
  require(temporary.isValid(), "The style workflow must create isolated temporary storage.");
  QImage source(40, 28, QImage::Format_ARGB32);
  source.fill(QColor(QStringLiteral("#293d52")));
  source.setColorSpace(QColorSpace(QColorSpace::SRgb));
  const auto source_path = temporary.filePath(QStringLiteral("style-source.png"));
  require(source.save(source_path, "PNG"), "The style workflow source image must be writable.");

  EditorPreviewController editor;
  editor.loadAsset(QStringLiteral("layer-style-controller-workflow"), source_path,
                   {}, {}, {}, {}, {}, true);
  settle(editor);
  const auto layers_before_target = recipe(editor, QStringLiteral("after asset load")).layers;
  require(editor.addFillLayer(QStringLiteral("#617d99"), QStringLiteral("Style target")),
          "Create a local layer for style authoring.");
  settle(editor);
  auto target_id = newly_added_layer_id(layers_before_target,
      recipe(editor, QStringLiteral("after style target creation")).layers);
  auto target_index = index_by_id(
      recipe(editor, QStringLiteral("after style target creation")).layers, target_id);
  require(!target_id.isEmpty() && target_index >= 0 && editor.setPhotoLayerTransform(
              target_index, 4, 2, 1.12, 17.0),
          "The newly created style target must be addressable and retain an outer transform.");
  editor.addMask(QStringLiteral("rectangle"));
  editor.setActiveMaskFeather(0.0);
  editor.setActiveMaskCenterX(0.47);
  editor.setActiveMaskCenterY(0.52);
  editor.setActiveMaskRadiusX(0.36);
  editor.setActiveMaskRadiusY(0.34);
  target_index = index_by_id(
      recipe(editor, QStringLiteral("after applying target mask")).layers, target_id);
  require(target_index >= 0 && editor.applyWorkingSelectionToLayer(target_index) &&
              editor.setPhotoLayerMaskOpacity(target_index, 0.78),
          "The style target must retain an editable selection-backed layer mask.");
  settle(editor);
  const auto target_before_style = layer_by_id(
      recipe(editor, QStringLiteral("after target mask setup")).layers, target_id);
  require(target_before_style.contains(QStringLiteral("selectionMasks")) ||
              target_before_style.contains(QStringLiteral("maskStack")) ||
              target_before_style.contains(QStringLiteral("maskIds")) ||
              target_before_style.contains(QStringLiteral("maskEnabled")),
          "The style target must carry a layer mask before style authoring.");
  const auto original_pixels = render(source, editor);

  const auto stroke_history_before = editor.editHistoryCurrentIndex();
  require(editor.addPhotoLayerStyle(target_index, QStringLiteral("stroke")),
          "Add a stroke to test alignment-aware placement against opaque layer content.");
  auto stroke = editor.photoLayerStyles(target_index).constLast().toMap();
  const auto stroke_id = stroke.value(QStringLiteral("id")).toString();
  require(!stroke_id.isEmpty(), "The alignment test stroke must have a stable identity.");
  const QVariantMap red_fill{
      {QStringLiteral("kind"), QStringLiteral("solid")},
      {QStringLiteral("color"), QVariantMap{
          {QStringLiteral("r"), 1.0}, {QStringLiteral("g"), 0.0},
          {QStringLiteral("b"), 0.0}, {QStringLiteral("a"), 1.0}}}};
  const QVariantMap inside_stroke_parameters{
      {QStringLiteral("width"), 8.0},
      {QStringLiteral("alignment"), QStringLiteral("inside")},
      {QStringLiteral("fill"), red_fill}};
  require(editor.setPhotoLayerStyleParameters(target_index, stroke_id,
              {{QStringLiteral("parameters"), inside_stroke_parameters}}),
          "Setting stroke alignment without placement must infer the visible on-artwork plane.");
  require(editor.editHistoryCurrentIndex() == stroke_history_before + 2,
          "Adding and configuring the test stroke must each create one history entry.");
  settle(editor);
  stroke = editor.photoLayerStyles(target_index).constLast().toMap();
  require(stroke.value(QStringLiteral("plane")).toString() == QStringLiteral("onContent"),
          "An inside stroke must default to the on-artwork plane instead of hiding below opaque content.");
  const auto inside_stroke_pixels = render(source, editor);
  require(!same_pixels(original_pixels, inside_stroke_pixels),
          "An inside stroke on the opaque fill layer must produce visible rendered pixels.");
  require(editor.setPhotoLayerStyleParameters(target_index, stroke_id,
              {{QStringLiteral("plane"), QStringLiteral("aboveContent")}}) &&
              editor.setPhotoLayerStyleParameters(target_index, stroke_id,
                  {{QStringLiteral("alignment"), QStringLiteral("outside")}}),
          "An explicit style plane must remain stable when alignment changes later.");
  require(editor.photoLayerStyles(target_index).constLast().toMap()
              .value(QStringLiteral("plane")).toString() == QStringLiteral("aboveContent"),
          "Alignment inference must preserve an explicitly overridden style plane.");
  require(editor.removePhotoLayerStyle(target_index, stroke_id),
          "Remove the temporary alignment test stroke before the typed-style workflow.");
  settle(editor);

  const auto available = editor.availablePhotoLayerStyles();
  require(available.size() == 8,
          "The desktop adapter must expose every currently supported engine style type.");
  for (const auto& type_value : available) {
    const auto type = type_value.toMap().value(QStringLiteral("typeId")).toString();
    const auto history_before_type = editor.editHistoryCurrentIndex();
    require(!type.isEmpty() && editor.addPhotoLayerStyle(target_index, type),
            "Each exposed style type must create an editable operation.");
    require(editor.editHistoryCurrentIndex() == history_before_type + 1,
            "Adding one style type must create exactly one history step.");
    settle(editor);
    const auto items = editor.photoLayerStyles(target_index);
    require(!items.isEmpty() && items.constLast().toMap()
                .value(QStringLiteral("supported")).toBool() &&
                !items.constLast().toMap().value(QStringLiteral("parameters")).toMap().isEmpty(),
            "Each style type must round-trip its typed parameter map through the engine stack codec.");
    const auto id = items.constLast().toMap().value(QStringLiteral("id")).toString();
    require(editor.removePhotoLayerStyle(target_index, id),
            "Every exposed style type must be removable.");
    require(editor.editHistoryCurrentIndex() == history_before_type + 2,
            "Removing one style type must create exactly one additional history step.");
    settle(editor);
  }
  require(editor.photoLayerStyles(target_index).isEmpty(),
          "The typed-default round trip must leave no temporary style operations.");

  const auto history_before_add = editor.editHistoryCurrentIndex();
  require(editor.addPhotoLayerStyle(target_index, QStringLiteral("color_overlay")),
          "Add a color overlay to the editable layer.");
  settle(editor);
  auto styles = editor.photoLayerStyles(target_index);
  require(styles.size() == 1 && styles.first().toMap().value(QStringLiteral("typeId")).toString() ==
              QStringLiteral("color_overlay") &&
              styles.first().toMap().value(QStringLiteral("supported")).toBool() &&
              editor.editHistoryCurrentIndex() == history_before_add + 1,
          "Adding one style must create a typed style item and one undoable history entry.");
  const auto overlay_id = styles.first().toMap().value(QStringLiteral("id")).toString();
  require(without_style_fields(layer_by_id(
              recipe(editor, QStringLiteral("after adding color overlay")).layers, target_id)) ==
              without_style_fields(target_before_style),
          "Adding a style must preserve the exact layer mask, transform, and every unrelated recipe field.");
  const auto initial_styled_pixels = render(source, editor);
  require(!same_pixels(original_pixels, initial_styled_pixels),
          "An active layer style must affect the rendered pixels.");

  auto overlay_parameters = styles.first().toMap()
      .value(QStringLiteral("parameters")).toMap();
  overlay_parameters.insert(QStringLiteral("color"),
      QVariantMap{{QStringLiteral("r"), 0.10}, {QStringLiteral("g"), 0.82},
          {QStringLiteral("b"), 0.24}, {QStringLiteral("a"), 1.0}});
  require(editor.setPhotoLayerStyleParameters(target_index, overlay_id,
              {{QStringLiteral("parameters"), overlay_parameters},
               {QStringLiteral("opacity"), 0.72},
               {QStringLiteral("blendMode"), QStringLiteral("screen")}}),
          "Edit typed color values and common blend controls in one operation.");
  require(editor.editHistoryCurrentIndex() == history_before_add + 2,
          "A typed parameter update must add exactly one history step.");
  settle(editor);
  styles = editor.photoLayerStyles(target_index);
  const auto edited_overlay = styles.first().toMap();
  const auto edited_color = edited_overlay.value(QStringLiteral("parameters")).toMap()
      .value(QStringLiteral("color")).toMap();
  require(qAbs(edited_color.value(QStringLiteral("g")).toDouble() - 0.82) < 1e-12 &&
              qAbs(edited_overlay.value(QStringLiteral("opacity")).toDouble() - 0.72) < 1e-12 &&
              edited_overlay.value(QStringLiteral("blendMode")).toString() ==
                  QStringLiteral("screen"),
          "Style item output must expose the saved linear-sRGB color and generic parameters.");
  const auto color_styled_pixels = render(source, editor);
  require(!same_pixels(initial_styled_pixels, color_styled_pixels),
          "Typed style edits must update rendered pixels.");

  require(editor.deselectWorkingSelection(),
          "Temporarily clear the working selection to cover the missing-source state.");
  const auto history_without_selection = editor.editHistoryCurrentIndex();
  require(!editor.maskPhotoLayerStyleFromSelection(target_index, overlay_id) &&
              editor.editHistoryCurrentIndex() == history_without_selection &&
              editor.status().contains(QStringLiteral("selection"), Qt::CaseInsensitive),
          "Creating a style mask without a working selection must explain how to proceed without history.");
  require(editor.reselectWorkingSelection(),
          "Restore the existing working selection after missing-source coverage.");

  const auto source_selection_id = editor.workingSelectionSetId();
  const auto source_selection_before = masks_for_set(
      recipe(editor, QStringLiteral("before style mask copy")).mask_stack,
      source_selection_id);
  require(!source_selection_id.isEmpty() && !source_selection_before.isEmpty(),
          "The style workflow must retain an editable current selection as mask source.");
  const auto before_style_mask_history = editor.editHistoryCurrentIndex();
  require(editor.maskPhotoLayerStyleFromSelection(target_index, overlay_id) &&
              editor.editHistoryCurrentIndex() == before_style_mask_history + 1,
          "Creating a style mask from the current selection must be one undoable controller edit.");
  settle(editor);
  auto overlay = editor.photoLayerStyles(target_index).first().toMap();
  auto mask_set_id = overlay.value(QStringLiteral("maskRef")).toString();
  auto state_with_style_mask = recipe(editor, QStringLiteral("after style mask copy"));
  const auto copied_style_mask = masks_for_set(state_with_style_mask.mask_stack, mask_set_id);
  require(!mask_set_id.isEmpty() && mask_set_id != source_selection_id &&
              copied_style_mask.size() == source_selection_before.size() &&
              masks_for_set(state_with_style_mask.mask_stack, source_selection_id) ==
                  source_selection_before &&
              overlay.value(QStringLiteral("maskRef")).toString() == mask_set_id,
          "A style mask must own an independent copied set while the working selection stays intact.");
  auto source_mask_metadata = source_selection_before.first().toMap();
  auto copied_mask_metadata = copied_style_mask.first().toMap();
  for (const auto* key : {"id", "maskSetId", "maskSetName"}) {
    source_mask_metadata.remove(QLatin1String(key));
    copied_mask_metadata.remove(QLatin1String(key));
  }
  require(source_mask_metadata == copied_mask_metadata,
          "The copied style mask must preserve selection geometry, refinement, and payload metadata.");
  require(editor.selectPhotoLayerStyleMask(target_index, overlay_id) &&
              editor.editingLayerMask() && editor.maskEditLayerId() == target_id &&
              editor.workingSelectionSetId() == source_selection_id,
          "Selecting a style mask must enter the existing mask target without replacing the working selection.");
  const auto locked_style_mask_id = mask_set_id;
  const auto history_before_locking_style_mask = editor.editHistoryCurrentIndex();
  editor.setActiveMaskLocked(true);
  require(editor.activeMaskLocked() &&
              editor.editHistoryCurrentIndex() == history_before_locking_style_mask + 1,
          "The style-mask source lock must be a committed mask-source edit.");
  const auto history_before_replacing_locked_mask = editor.editHistoryCurrentIndex();
  require(editor.maskPhotoLayerStyleFromSelection(target_index, overlay_id) &&
              editor.editHistoryCurrentIndex() == history_before_replacing_locked_mask + 1,
          "Replacing a style mask must copy the current selection and commit as one edit.");
  auto state_after_mask_replace = recipe(editor, QStringLiteral("after replacing locked style mask"));
  overlay = editor.photoLayerStyles(target_index).first().toMap();
  mask_set_id = overlay.value(QStringLiteral("maskRef")).toString();
  const auto retained_locked_mask = masks_for_set(state_after_mask_replace.mask_stack,
                                                   locked_style_mask_id);
  require(!mask_set_id.isEmpty() && mask_set_id != locked_style_mask_id &&
              retained_locked_mask.size() == copied_style_mask.size() &&
              retained_locked_mask.first().toMap().value(QStringLiteral("locked")).toBool() &&
              editor.workingSelectionSetId() == source_selection_id,
          "Replacing a mask must keep its locked prior source and preserve the working selection.");
  require(editor.selectPhotoLayerStyleMask(target_index, overlay_id) &&
              editor.editingLayerMask() && editor.maskEditLayerId() == target_id,
          "The replacement style mask must remain available to the shared mask editor.");
  const auto history_before_clearing_replacement = editor.editHistoryCurrentIndex();
  require(editor.clearPhotoLayerStyleMask(target_index, overlay_id) &&
              editor.editHistoryCurrentIndex() == history_before_clearing_replacement + 1 &&
              editor.photoLayerStyles(target_index).first().toMap()
                  .value(QStringLiteral("maskRef")).toString().isEmpty() &&
              masks_for_set(recipe(editor, QStringLiteral("after clearing replacement mask")).mask_stack,
                            mask_set_id).isEmpty() &&
              !masks_for_set(recipe(editor, QStringLiteral("preserve locked previous source")).mask_stack,
                             locked_style_mask_id).isEmpty(),
          "Clearing the replacement must delete only its exclusive unlocked set and retain the locked source.");
  restore_history(editor, false);
  require(editor.photoLayerStyles(target_index).first().toMap()
              .value(QStringLiteral("maskRef")).toString() == mask_set_id &&
              editor.editingLayerMask() && editor.maskEditLayerId() == target_id,
          "Undo must restore the replacement source and editing owner after clear.");
  restore_history(editor, true);
  require(editor.photoLayerStyles(target_index).first().toMap()
              .value(QStringLiteral("maskRef")).toString().isEmpty() &&
              !editor.editingLayerMask(),
          "Redo must clear the replacement reference again without deleting the locked source.");
  restore_history(editor, false);
  require(editor.photoLayerStyles(target_index).first().toMap()
              .value(QStringLiteral("maskRef")).toString() == mask_set_id &&
              editor.selectPhotoLayerStyleMask(target_index, overlay_id) &&
              editor.editingLayerMask() && editor.maskEditLayerId() == target_id,
          "Restoring the mask edit must reselect its stable owner before brush coverage checks.");
  const auto state_before_style_mask_paint = recipe(
      editor, QStringLiteral("before style mask painting"));
  const auto target_before_mask_paint = layer_by_id(state_before_style_mask_paint.layers,
                                                      target_id);
  const auto source_before_mask_paint = masks_for_set(
      state_before_style_mask_paint.mask_stack, source_selection_id);
  const auto style_mask_before_erase = masks_for_set(
      recipe(editor, QStringLiteral("before style mask eraser")).mask_stack, mask_set_id);
  const auto history_before_style_mask_paint = editor.editHistoryCurrentIndex();
  commit_style_mask_brush(editor, target_index, 20.0, 14.0, true);
  auto state_after_style_erase = recipe(editor, QStringLiteral("after style mask eraser"));
  const auto style_mask_after_erase = masks_for_set(state_after_style_erase.mask_stack, mask_set_id);
  const auto history_delta = editor.editHistoryCurrentIndex() - history_before_style_mask_paint;
  const bool style_mask_changed = style_mask_after_erase != style_mask_before_erase;
  const bool source_selection_unchanged =
      masks_for_set(state_after_style_erase.mask_stack, source_selection_id) ==
          source_before_mask_paint;
  const bool target_layer_unchanged =
      layer_by_id(state_after_style_erase.layers, target_id) == target_before_mask_paint;
  const bool working_selection_unchanged = editor.workingSelectionSetId() == source_selection_id;
  const bool style_mask_owner_active = editor.editingLayerMask() &&
      editor.maskEditLayerId() == target_id;
  if (history_delta != 1 || !style_mask_changed || !source_selection_unchanged ||
      !target_layer_unchanged || !working_selection_unchanged || !style_mask_owner_active) {
    const auto active_index = editor.activeMaskIndex();
    const auto mask_stack = editor.maskStack();
    const auto active_set_id = active_index >= 0 && active_index < mask_stack.size()
        ? mask_stack.at(active_index).toMap().value(QStringLiteral("maskSetId")).toString()
        : QString();
    std::cerr << "Style-mask eraser diagnostics: history_delta=" << history_delta
              << " style_mask_changed=" << style_mask_changed
              << " source_selection_unchanged=" << source_selection_unchanged
              << " target_layer_unchanged=" << target_layer_unchanged
              << " working_selection_unchanged=" << working_selection_unchanged
              << " style_mask_owner_active=" << style_mask_owner_active
              << " editing_layer_mask=" << editor.editingLayerMask()
              << " mask_edit_layer_id='" << editor.maskEditLayerId().toStdString()
              << "' active_mask_index=" << active_index
              << " active_mask_set_id='" << active_set_id.toStdString()
              << "' expected_style_mask_set_id='" << mask_set_id.toStdString()
              << "' working_selection_set_id='"
              << editor.workingSelectionSetId().toStdString()
              << "' expected_working_selection_set_id='"
              << source_selection_id.toStdString() << "'\n";
  }
  require(history_delta == 1 && style_mask_changed && source_selection_unchanged &&
              target_layer_unchanged && working_selection_unchanged && style_mask_owner_active,
          "Eraser strokes must alter only the copied style mask and create one history step.");
  restore_history(editor, false);
  require(masks_for_set(recipe(editor, QStringLiteral("undo style mask eraser")).mask_stack,
                        mask_set_id) == style_mask_before_erase &&
              editor.editingLayerMask() && editor.maskEditLayerId() == target_id,
          "Undo must restore style-mask coverage and its editing owner.");
  restore_history(editor, true);
  require(masks_for_set(recipe(editor, QStringLiteral("redo style mask eraser")).mask_stack,
                        mask_set_id) == style_mask_after_erase &&
              editor.editingLayerMask() && editor.maskEditLayerId() == target_id,
          "Redo must restore style-mask coverage and its editing owner.");
  const auto after_erase_selection = masks_for_set(
      recipe(editor, QStringLiteral("before style mask reveal")).mask_stack,
      source_selection_id);
  const auto history_before_style_mask_reveal = editor.editHistoryCurrentIndex();
  commit_style_mask_brush(editor, target_index, 20.0, 14.0, false);
  const auto after_style_mask_reveal = recipe(editor, QStringLiteral("after style mask reveal"));
  require(editor.editHistoryCurrentIndex() == history_before_style_mask_reveal + 1 &&
              masks_for_set(after_style_mask_reveal.mask_stack, source_selection_id) ==
                  after_erase_selection &&
              layer_by_id(after_style_mask_reveal.layers, target_id) ==
                  target_before_mask_paint,
          "Reveal strokes must remain isolated to the style mask, away from source selection and layer pixels.");
  editor.finishLayerMaskEditing();
  require(!editor.editingLayerMask() && editor.workingSelectionSetId() == source_selection_id,
          "Finishing style-mask editing must return to the same working selection.");

  const auto history_before_effect = editor.editHistoryCurrentIndex();
  require(editor.setPhotoLayerEffectParameters(target_index, QStringLiteral("film_grain"),
              QString(), {}, 1.0, true) &&
              editor.editHistoryCurrentIndex() == history_before_effect + 1,
          "Add one filter operation so duplicate mask behavior covers its mask reference too.");
  settle(editor);
  auto source_effects = editor.photoLayerEffects(target_index);
  require(source_effects.size() == 1,
          "The duplicate source layer must carry one editable filter operation.");
  const auto effect_id = source_effects.first().toMap()
      .value(QStringLiteral("id")).toString();
  const auto history_before_effect_mask = editor.editHistoryCurrentIndex();
  require(!effect_id.isEmpty() && editor.maskPhotoLayerEffectFromSelection(target_index, effect_id) &&
              editor.editHistoryCurrentIndex() == history_before_effect_mask + 1,
          "Mask the filter from the existing selection mask set.");
  settle(editor);
  source_effects = editor.photoLayerEffects(target_index);
  const auto source_effect = source_effects.first().toMap();
  const auto effect_mask_id = source_effect.value(QStringLiteral("maskId")).toString();
  require(!effect_mask_id.isEmpty(),
          "The source filter must reference a valid document mask before duplication.");
  require(editor.selectPhotoLayerEffectMask(target_index, effect_id) &&
              editor.editingLayerMask() && editor.maskEditLayerId() == target_id &&
              editor.workingSelectionSetId() == source_selection_id,
          "The adjacent effect-mask selector must use the same direct-mask target and preserve the working selection.");
  editor.finishLayerMaskEditing();

  const auto before_with_masks = recipe(editor, QStringLiteral("before duplicate with masks"));
  const auto source_with_masks = layer_by_id(before_with_masks.layers, target_id);
  const auto source_styles_with_masks = editor.photoLayerStyles(target_index);
  const auto source_effects_with_masks = editor.photoLayerEffects(target_index);
  const auto before_with_masks_patch = editor.currentAssetBoundAdjustmentPatchJson();
  const auto duplicate_with_masks_history = editor.editHistoryCurrentIndex();
  require(editor.duplicatePhotoLayerWithMasks(target_index) &&
              editor.editHistoryCurrentIndex() == duplicate_with_masks_history + 1,
          "Duplicating with masks must be one undoable controller action.");
  settle(editor);
  auto after_with_masks = recipe(editor, QStringLiteral("after duplicate with masks"));
  const auto with_masks_id = newly_added_layer_id(before_with_masks.layers, after_with_masks.layers);
  auto with_masks_index = index_by_id(after_with_masks.layers, with_masks_id);
  auto with_masks_layer = layer_by_id(after_with_masks.layers, with_masks_id);
  require(!with_masks_id.isEmpty() && with_masks_id != target_id && with_masks_index >= 0 &&
              editor.photoLayerStyles(with_masks_index) == source_styles_with_masks &&
              editor.photoLayerEffects(with_masks_index) == source_effects_with_masks &&
              with_masks_layer.value(QStringLiteral("layerStyleStack")).toString() ==
                  source_with_masks.value(QStringLiteral("layerStyleStack")).toString() &&
              with_masks_layer.value(QStringLiteral("effectStack")).toString() ==
                  source_with_masks.value(QStringLiteral("effectStack")).toString() &&
              after_with_masks.mask_stack == before_with_masks.mask_stack &&
              layer_by_id(after_with_masks.layers, target_id) == source_with_masks,
          "Duplicate with masks must retain both mask references/stacks, source coverage, and a distinct ID.");
  const auto with_masks_patch = editor.currentAssetBoundAdjustmentPatchJson();
  restore_history(editor, false);
  require(editor.currentAssetBoundAdjustmentPatchJson() == before_with_masks_patch &&
              layer_by_id(recipe(editor, QStringLiteral("undo duplicate with masks")).layers,
                          with_masks_id).isEmpty(),
          "Undo must remove the mask-preserving duplicate.");
  restore_history(editor, true);
  after_with_masks = recipe(editor, QStringLiteral("redo duplicate with masks"));
  with_masks_index = index_by_id(after_with_masks.layers, with_masks_id);
  require(editor.currentAssetBoundAdjustmentPatchJson() == with_masks_patch &&
              with_masks_index >= 0 &&
              layer_by_id(after_with_masks.layers, with_masks_id) == with_masks_layer &&
              editor.photoLayerStyles(with_masks_index) == source_styles_with_masks &&
              editor.photoLayerEffects(with_masks_index) == source_effects_with_masks &&
              after_with_masks.mask_stack == before_with_masks.mask_stack,
          "Redo must restore the same mask-preserving duplicate identity, stacks, references, and coverage.");

  target_index = index_by_id(after_with_masks.layers, target_id);
  const auto before_without_masks = after_with_masks;
  const auto source_without_masks = layer_by_id(before_without_masks.layers, target_id);
  const auto source_styles_without_masks = editor.photoLayerStyles(target_index);
  const auto source_effects_without_masks = editor.photoLayerEffects(target_index);
  const auto before_without_masks_patch = editor.currentAssetBoundAdjustmentPatchJson();
  const auto duplicate_without_masks_history = editor.editHistoryCurrentIndex();
  require(editor.duplicatePhotoLayerWithoutMasks(target_index) &&
              editor.editHistoryCurrentIndex() == duplicate_without_masks_history + 1,
          "Duplicating without masks must be one undoable controller action.");
  settle(editor);
  auto after_without_masks = recipe(editor, QStringLiteral("after duplicate without masks"));
  const auto without_masks_id = newly_added_layer_id(
      before_without_masks.layers, after_without_masks.layers);
  const auto without_masks_index = index_by_id(after_without_masks.layers, without_masks_id);
  const auto without_masks_layer = layer_by_id(after_without_masks.layers, without_masks_id);
  const auto without_styles = editor.photoLayerStyles(without_masks_index);
  const auto without_effects = editor.photoLayerEffects(without_masks_index);
  require(!without_masks_id.isEmpty() && without_masks_id != target_id &&
              without_masks_index >= 0 && without_masks_layer.contains(QStringLiteral("layerStyleStack")) &&
              without_masks_layer.contains(QStringLiteral("effectStack")) &&
              without_masks_layer.value(QStringLiteral("selectionScoped")).toBool() == false &&
              without_masks_layer.value(QStringLiteral("hasLayerMask")).toBool() == false &&
              without_masks_layer.value(QStringLiteral("maskEnabled")).toBool() == false &&
              !without_masks_layer.contains(QStringLiteral("maskData")) &&
              without_styles.size() == source_styles_without_masks.size() &&
              without_item_field(without_styles, QStringLiteral("maskRef")) ==
                  without_item_field(source_styles_without_masks, QStringLiteral("maskRef")) &&
              without_effects.size() == source_effects_without_masks.size() &&
              without_item_field(without_effects, QStringLiteral("maskId")) ==
                  without_item_field(source_effects_without_masks, QStringLiteral("maskId")) &&
              std::all_of(without_styles.cbegin(), without_styles.cend(),
                  [](const QVariant& value) {
                    return value.toMap().value(QStringLiteral("maskRef")).toString().isEmpty();
                  }) &&
              std::all_of(without_effects.cbegin(), without_effects.cend(),
                  [](const QVariant& value) {
                    return value.toMap().value(QStringLiteral("maskId")).toString().isEmpty();
                  }) &&
              after_without_masks.mask_stack == before_without_masks.mask_stack &&
              layer_by_id(after_without_masks.layers, target_id) == source_without_masks,
          "Duplicate without masks must clear layer/style/filter links but retain operation stacks and source coverage.");
  const auto without_masks_patch = editor.currentAssetBoundAdjustmentPatchJson();
  restore_history(editor, false);
  require(editor.currentAssetBoundAdjustmentPatchJson() == before_without_masks_patch &&
              layer_by_id(recipe(editor, QStringLiteral("undo duplicate without masks")).layers,
                          without_masks_id).isEmpty(),
          "Undo must remove the maskless duplicate without changing its source.");
  restore_history(editor, true);
  after_without_masks = recipe(editor, QStringLiteral("redo duplicate without masks"));
  const auto redone_without_masks_index = index_by_id(after_without_masks.layers,
                                                     without_masks_id);
  require(editor.currentAssetBoundAdjustmentPatchJson() == without_masks_patch &&
              redone_without_masks_index >= 0 &&
              layer_by_id(after_without_masks.layers, without_masks_id) == without_masks_layer &&
              editor.photoLayerStyles(redone_without_masks_index) == without_styles &&
              editor.photoLayerEffects(redone_without_masks_index) == without_effects &&
              after_without_masks.mask_stack == before_without_masks.mask_stack,
          "Redo must restore the same maskless duplicate identity, operation stacks, and source coverage.");
  target_index = index_by_id(after_without_masks.layers, target_id);

  const auto history_before_light = editor.editHistoryCurrentIndex();
  require(editor.setPhotoLayerStyleLightAngle(target_index, 214.0) &&
              qAbs(editor.photoLayerStyleLightAngle(target_index) - 214.0) < 1e-12,
          "A layer's shared light angle must be independently editable.");
  require(editor.editHistoryCurrentIndex() == history_before_light + 1,
          "Changing the shared light angle must add exactly one history step.");
  const auto history_before_shadow = editor.editHistoryCurrentIndex();
  require(editor.addPhotoLayerStyle(target_index, QStringLiteral("drop_shadow")),
          "Add a second operation to test stack order.");
  require(editor.editHistoryCurrentIndex() == history_before_shadow + 1,
          "Adding the second style must add exactly one history step.");
  settle(editor);
  styles = editor.photoLayerStyles(target_index);
  require(styles.size() == 2, "The layer should hold two independently addressable styles.");
  const auto shadow_id = styles.last().toMap().value(QStringLiteral("id")).toString();
  auto history_before_order = editor.editHistoryCurrentIndex();
  require(editor.movePhotoLayerStyle(target_index, shadow_id, 0),
          "Reorder a style within its layer stack.");
  require(editor.editHistoryCurrentIndex() == history_before_order + 1,
          "Reordering styles must add exactly one history step.");
  settle(editor);
  styles = editor.photoLayerStyles(target_index);
  require(styles.first().toMap().value(QStringLiteral("id")).toString() == shadow_id,
          "Layer style order must round-trip through the serialized stack.");
  history_before_order = editor.editHistoryCurrentIndex();
  require(editor.setPhotoLayerStyleEnabled(target_index, shadow_id, false),
          "Bypass a single style operation.");
  require(editor.editHistoryCurrentIndex() == history_before_order + 1,
          "Bypassing one style must add exactly one history step.");
  settle(editor);
  require(!editor.photoLayerStyles(target_index).first().toMap()
              .value(QStringLiteral("enabled")).toBool(),
          "Style bypass state must be exposed to QML.");
  history_before_order = editor.editHistoryCurrentIndex();
  require(editor.setPhotoLayerStyleEnabled(target_index, shadow_id, true),
          "Re-enable a supported style operation.");
  require(editor.editHistoryCurrentIndex() == history_before_order + 1,
          "Re-enabling one style must add exactly one history step.");
  settle(editor);

  const auto copied_envelope = editor.photoLayerStyleEnvelope(target_index);
  QString copied_stack;
  double copied_angle = 0.0;
  QString codec_error;
  require(!copied_envelope.isEmpty() && layer_style_codec::decode_envelope(
              copied_envelope.toUtf8(), &copied_stack, &copied_angle, &codec_error) &&
              qAbs(copied_angle - 214.0) < 1e-12,
          "Copy and preset envelopes must be versioned, bounded linear-sRGB documents.");

  const auto layers_before_preset_target = recipe(
      editor, QStringLiteral("before adding preset target")).layers;
  require(editor.addFillLayer(QStringLiteral("#81634a"), QStringLiteral("Preset target")),
          "Create a second target for the style envelope copy.");
  settle(editor);
  const auto preset_target_id = newly_added_layer_id(
      layers_before_preset_target,
      recipe(editor, QStringLiteral("after preset target creation")).layers);
  const auto preset_target_index = index_by_id(
      recipe(editor, QStringLiteral("after preset target creation")).layers,
      preset_target_id);
  target_index = index_by_id(
      recipe(editor, QStringLiteral("after preset target creation")).layers, target_id);
  require(preset_target_index >= 0 && target_index >= 0 && preset_target_id != target_id,
          "Both original and preset target layers must remain addressable by stable identity.");
  const auto target_before_copy = layer_by_id(
      recipe(editor, QStringLiteral("immediately before preset paste")).layers, target_id);
  require(!target_before_copy.isEmpty(),
          "The source layer must have an exact snapshot immediately before preset paste.");
  const auto paste_history_before = editor.editHistoryCurrentIndex();
  require(editor.applyPhotoLayerStyleEnvelope(preset_target_index, copied_envelope),
          "Paste a layer style envelope to another editable layer.");
  require(editor.editHistoryCurrentIndex() == paste_history_before + 1,
          "Pasting a style envelope must add exactly one history step.");
  settle(editor);
  const auto target_styles = editor.photoLayerStyles(preset_target_index);
  require(target_styles == editor.photoLayerStyles(target_index) &&
              qAbs(editor.photoLayerStyleLightAngle(preset_target_index) - 214.0) < 1e-12,
          "Style preset paste must preserve typed parameters, order, and shared-light settings.");
  auto copied_layer = layer_at(editor, preset_target_index);
  require(copied_layer.value(QStringLiteral("layerStyleStack")).toString().size() > 0 &&
              qAbs(copied_layer.value(QStringLiteral("layerStyleGlobalLightAngle")).toDouble() -
                   214.0) < 1e-12,
          "The editor recipe must store the engine stack string and sibling global-light angle.");
  const auto original_after_paste = layer_by_id(
      recipe(editor, QStringLiteral("after envelope paste")).layers, target_id);
  require(original_after_paste == target_before_copy,
          "Envelope copy must preserve every source layer field exactly.");

  auto missing_stack = photo_editor::pipeline::LayerStyleStack{};
  require(layer_style_codec::decode_stack(copied_stack, &missing_stack, &codec_error),
          "Decode the copied stack before testing missing mask references.");
  missing_stack.operations.front().mask_ref = "missing-style-mask-reference";
  QString missing_stack_text;
  require(layer_style_codec::encode_stack(missing_stack, &missing_stack_text, &codec_error),
          "Encode a structurally valid stack with a missing mask reference.");
  const auto missing_mask_envelope = QString::fromUtf8(
      layer_style_codec::encode_envelope(missing_stack_text, copied_angle, &codec_error));
  const auto before_missing_mask = editor.currentAssetBoundAdjustmentPatchJson();
  const auto missing_mask_history = editor.editHistoryCurrentIndex();
  require(!missing_mask_envelope.isEmpty() &&
              !editor.applyPhotoLayerStyleEnvelope(preset_target_index, missing_mask_envelope) &&
              editor.currentAssetBoundAdjustmentPatchJson() == before_missing_mask &&
              editor.editHistoryCurrentIndex() == missing_mask_history,
          "Paste must reject missing mask references before changing recipe or history.");

  const auto preset_path = temporary.filePath(QStringLiteral("layer-style-preset.json"));
  require(!editor.exportPhotoLayerStyleFile(preset_target_index, preset_path) &&
              !QFile::exists(preset_path),
          "Reusable preset export must reject document-local style mask references.");
  const auto pasted_styles_with_mask = editor.photoLayerStyles(preset_target_index);
  auto mask_operation = QString{};
  for (const auto& style_value : pasted_styles_with_mask) {
    const auto style = style_value.toMap();
    if (!style.value(QStringLiteral("maskRef")).toString().isEmpty()) {
      mask_operation = style.value(QStringLiteral("id")).toString();
      mask_set_id = style.value(QStringLiteral("maskRef")).toString();
      break;
    }
  }
  const auto history_before_style_mask_clear = editor.editHistoryCurrentIndex();
  require(!mask_operation.isEmpty() && !mask_set_id.isEmpty() &&
              editor.clearPhotoLayerStyleMask(preset_target_index, mask_operation) &&
              editor.editHistoryCurrentIndex() == history_before_style_mask_clear + 1 &&
              masks_for_set(recipe(editor, QStringLiteral("after shared style mask clear")).mask_stack,
                            mask_set_id).size() > 0,
          "Clearing one shared style mask must be undoable and preserve the remaining document consumer.");
  require(editor.exportPhotoLayerStyleFile(preset_target_index, preset_path),
          "Export the bounded style preset envelope to a file.");
  const auto before_remove = editor.photoLayerStyles(preset_target_index);
  const auto remove_history_before = editor.editHistoryCurrentIndex();
  require(editor.removePhotoLayerStyle(preset_target_index,
              before_remove.first().toMap().value(QStringLiteral("id")).toString()),
          "Remove one imported style before testing file restore.");
  require(editor.editHistoryCurrentIndex() == remove_history_before + 1,
          "Removing a pasted style must add exactly one history step.");
  settle(editor);
  const auto import_history_before = editor.editHistoryCurrentIndex();
  require(editor.importPhotoLayerStyleFile(preset_target_index, preset_path) &&
              editor.photoLayerStyles(preset_target_index) == before_remove,
          "Import must restore the complete style stack from the bounded file envelope.");
  require(editor.editHistoryCurrentIndex() == import_history_before + 1,
          "Importing one preset must add exactly one history step.");
  settle(editor);

  auto overbudget_stack = photo_editor::pipeline::LayerStyleStack{};
  require(layer_style_codec::decode_stack(copied_stack, &overbudget_stack, &codec_error),
          "Decode the copied style stack for render-budget guard coverage.");
  QString overbudget_shadow_id;
  for (auto& operation : overbudget_stack.operations) {
    if (auto* shadow = std::get_if<photo_editor::pipeline::LayerStyleDropShadow>(
            &operation.parameters)) {
      shadow->blur = 400.0;
      overbudget_shadow_id = QString::fromStdString(operation.id);
      break;
    }
  }
  QString overbudget_stack_text;
  require(!overbudget_shadow_id.isEmpty() &&
              layer_style_codec::encode_stack(overbudget_stack, &overbudget_stack_text,
                                              &codec_error),
          "Create a structurally valid preset whose blur exceeds render padding limits.");
  const auto overbudget_envelope = QString::fromUtf8(
      layer_style_codec::encode_envelope(overbudget_stack_text, copied_angle, &codec_error));
  const auto overbudget_file_path = temporary.filePath(QStringLiteral("overbudget-style-preset.json"));
  const auto overbudget_bytes = overbudget_envelope.toUtf8();
  QFile overbudget_file(overbudget_file_path);
  require(!overbudget_bytes.isEmpty() && overbudget_file.open(QIODevice::WriteOnly) &&
              overbudget_file.write(overbudget_bytes) == overbudget_bytes.size() &&
              overbudget_file.flush(),
          "Write a valid over-budget style preset for import validation.");
  overbudget_file.close();
  const auto before_overbudget_edit = editor.currentAssetBoundAdjustmentPatchJson();
  const auto overbudget_edit_history = editor.editHistoryCurrentIndex();
  const auto before_overbudget_pixels = render(source, editor);
  require(!editor.setPhotoLayerStyleParameters(preset_target_index, overbudget_shadow_id,
              {{QStringLiteral("parameters"),
                QVariantMap{{QStringLiteral("blur"), 400.0}}}}) &&
              editor.currentAssetBoundAdjustmentPatchJson() == before_overbudget_edit &&
              editor.editHistoryCurrentIndex() == overbudget_edit_history &&
              editor.status().contains(QStringLiteral("reduce"), Qt::CaseInsensitive) &&
              same_pixels(render(source, editor), before_overbudget_pixels),
          "An over-budget blur edit must be rejected with actionable status before changing history or pixels.");
  require(!editor.importPhotoLayerStyleFile(preset_target_index, overbudget_file_path) &&
              editor.currentAssetBoundAdjustmentPatchJson() == before_overbudget_edit &&
              editor.editHistoryCurrentIndex() == overbudget_edit_history &&
              same_pixels(render(source, editor), before_overbudget_pixels),
          "An over-budget preset import must leave the exact recipe, history, and pixels unchanged.");

  const auto current_patch = editor.currentAssetBoundAdjustmentPatchJson();
  const auto history_before_bad_input = editor.editHistoryCurrentIndex();
  require(!editor.applyPhotoLayerStyleEnvelope(preset_target_index, QStringLiteral("{}")) &&
              !editor.importPhotoLayerStyleFile(preset_target_index,
                  temporary.filePath(QStringLiteral("missing.json"))) &&
              editor.currentAssetBoundAdjustmentPatchJson() == current_patch &&
              editor.editHistoryCurrentIndex() == history_before_bad_input,
          "Malformed or unavailable preset input must leave the recipe and history untouched.");

  const auto current_pixels = render(source, editor);
  const auto canonical_patch = editor.currentAssetBoundAdjustmentPatchJson();
  EditorPreviewController reopened;
  reopened.loadAsset(editor.assetId(), source_path, {}, {}, {}, {}, canonical_patch, true);
  settle(reopened);
  const auto reopened_preset_target_index = index_by_id(
      recipe(reopened, QStringLiteral("after canonical reopen")).layers, preset_target_id);
  require(recipe(reopened, QStringLiteral("compare reopened recipe")).layers ==
              recipe(editor, QStringLiteral("compare saved recipe")).layers &&
              reopened_preset_target_index >= 0 &&
              reopened.photoLayerStyles(reopened_preset_target_index) == before_remove &&
              same_pixels(render(source, reopened), current_pixels),
          "Canonical recipe reopen must preserve the typed style stack and exact rendered pixels.");
  const auto export_path = temporary.filePath(QStringLiteral("styled-output.png"));
  require(editor.startFlattenedPhotoExport(export_path),
          "Export the photo with its applied layer styles.");
  settle(editor);
  const QImage exported(export_path);
  require(editor.photoExportError().isEmpty() && !exported.isNull() &&
              same_pixels(exported, current_pixels),
          "Flattened export must preserve the style-rendered pixels.");
  const auto history_before_undo = editor.editHistoryCurrentIndex();
  restore_history(editor, false);
  require(editor.editHistoryCurrentIndex() == history_before_undo - 1 &&
              editor.photoLayerStyles(preset_target_index) != before_remove,
          "Undo must restore the state before the last style file import.");
  restore_history(editor, true);
  require(editor.photoLayerStyles(preset_target_index) == before_remove &&
              same_pixels(render(source, editor), current_pixels),
          "Redo must restore the imported style stack and its rendered output.");

  require(editor.setPhotoLayerLocked(preset_target_index, true), "Lock the preset style target.");
  settle(editor);
  const auto locked_history = editor.editHistoryCurrentIndex();
  require(!editor.addPhotoLayerStyle(preset_target_index, QStringLiteral("stroke")) &&
              !editor.removePhotoLayerStyle(preset_target_index,
                  editor.photoLayerStyles(preset_target_index).first().toMap()
                      .value(QStringLiteral("id")).toString()) &&
              editor.editHistoryCurrentIndex() == locked_history,
          "Locked style targets must reject edits without creating history entries.");

  require(editor.setPhotoLayerLocked(preset_target_index, false),
          "Unlock the style target before grouping.");
  settle(editor);
  target_index = index_by_id(
      recipe(editor, QStringLiteral("before grouping layers")).layers, target_id);
  const auto current_preset_target_index = index_by_id(
      recipe(editor, QStringLiteral("before grouping layers")).layers, preset_target_id);
  require(target_index >= 0 && current_preset_target_index >= 0 &&
              editor.groupPhotoLayers(QVariantList{target_index, current_preset_target_index},
                                      QStringLiteral("Style parent")),
          "Group the two styled layers to verify ancestor lock handling.");
  settle(editor);
  const auto grouped_layers = recipe(
      editor, QStringLiteral("after grouping styled targets")).layers;
  const auto grouped_target = layer_by_id(grouped_layers, target_id);
  const auto group_id = grouped_target.value(QStringLiteral("parentId")).toString();
  const auto group_index = index_by_id(grouped_layers, group_id);
  target_index = index_by_id(grouped_layers, target_id);
  require(!group_id.isEmpty() && group_index >= 0 && target_index >= 0 &&
              editor.setPhotoLayerLocked(group_index, true),
          "Lock the containing style group.");
  settle(editor);
  const auto ancestor_history = editor.editHistoryCurrentIndex();
  require(!editor.addPhotoLayerStyle(target_index, QStringLiteral("stroke")) &&
              !editor.applyPhotoLayerStyleEnvelope(target_index, copied_envelope) &&
              editor.editHistoryCurrentIndex() == ancestor_history,
          "A locked ancestor group must block child style mutation and paste.");

  const auto layers_before_text = recipe(
      editor, QStringLiteral("before adding legacy text")).layers;
  require(editor.addTextLayer(QStringLiteral("Exact legacy typography"),
                              QStringLiteral("Legacy text")),
          "Add a legacy text layer to check coexistence with the style stack.");
  settle(editor);
  const auto legacy_text_id = newly_added_layer_id(layers_before_text,
      recipe(editor, QStringLiteral("after adding legacy text")).layers);
  const auto legacy_text_index = index_by_id(
      recipe(editor, QStringLiteral("after adding legacy text")).layers, legacy_text_id);
  const auto legacy_text = layer_by_id(
      recipe(editor, QStringLiteral("immediately before styling legacy text")).layers,
      legacy_text_id);
  require(!legacy_text_id.isEmpty() &&
              !legacy_text.contains(QStringLiteral("textTypography")) &&
              legacy_text.contains(QStringLiteral("fontFamily")) &&
              legacy_text.contains(QStringLiteral("fontSize")) &&
              editor.addPhotoLayerStyle(legacy_text_index, QStringLiteral("stroke")),
          "A layer style can be added to text without replacing its legacy typography.");
  settle(editor);
  const auto styled_text = layer_by_id(
      recipe(editor, QStringLiteral("after styling legacy text")).layers, legacy_text_id);
  require(!styled_text.contains(QStringLiteral("textTypography")) &&
              without_style_fields(styled_text) == without_style_fields(legacy_text),
          "Adding a style must preserve exact legacy text content and typography fields.");

  const auto layers_before_adjustment = recipe(
      editor, QStringLiteral("before adding adjustment layer")).layers;
  require(editor.addAdjustmentLayer(QStringLiteral("Unsupported style target")),
          "Add an adjustment layer to verify unsupported style targets fail explicitly.");
  settle(editor);
  const auto unsupported_layer_id = newly_added_layer_id(
      layers_before_adjustment,
      recipe(editor, QStringLiteral("after adding adjustment layer")).layers);
  const auto unsupported_layer_index = index_by_id(
      recipe(editor, QStringLiteral("after adding adjustment layer")).layers,
      unsupported_layer_id);
  const auto unsupported_history = editor.editHistoryCurrentIndex();
  require(unsupported_layer_index >= 0 &&
              !editor.addPhotoLayerStyle(unsupported_layer_index, QStringLiteral("stroke")) &&
              !editor.applyPhotoLayerStyleEnvelope(unsupported_layer_index, copied_envelope) &&
              editor.photoLayerStyles(unsupported_layer_index).isEmpty() &&
              editor.editHistoryCurrentIndex() == unsupported_history,
          "Unsupported layer kinds must reject style edits and paste without changing history.");

  std::cout << "Layer style adapter workflow passed: typed defaults, parameter edits, bypass, order, shared light, bounded copy/import/export envelopes, render changes, history, and locks.\n";
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  QTemporaryDir runtime;
  if (!runtime.isValid()) return 1;
  qputenv("PHOTO_EDITOR_APP_DATA_DIR",
          runtime.filePath(QStringLiteral("app-data")).toUtf8());
  qputenv("PHOTO_EDITOR_CACHE_DIR",
          runtime.filePath(QStringLiteral("cache")).toUtf8());
  qputenv("PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE", QByteArrayLiteral("1"));
  QGuiApplication application(argc, argv);
  struct DrainWorkers {
    ~DrainWorkers() { QThreadPool::globalInstance()->waitForDone(90000); }
  } drain_workers;
  try {
    return run_workflow();
  } catch (const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
}
