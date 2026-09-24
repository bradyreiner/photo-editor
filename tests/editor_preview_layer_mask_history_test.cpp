#include <cmath>
#include <cstdlib>
#include <iostream>

#include <QCoreApplication>
#include <QStringList>

#include "editor_preview_controller.h"
#include "photo_layer_effect_codec.h"
#include "edit_recipe_state.h"

namespace {

int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

bool almost_equal(double left, double right, double tolerance = 1e-6) {
  return std::abs(left - right) <= tolerance;
}

QStringList layer_order_ids(const photo_editor::desktop::EditorPreviewController& controller) {
  QStringList ids;
  const auto layers = controller.photoLayers();
  for (const auto& layer_value : layers) {
    ids.append(layer_value.toMap().value(QStringLiteral("id")).toString());
  }
  return ids;
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);

  photo_editor::desktop::EditorPreviewController controller;
  controller.loadAsset(
      QStringLiteral("layer-mask-history-asset"),
      QString(),
      QString(),
      QString(),
      QString(),
      QString(),
      QString());

  controller.addMask(QStringLiteral("linear"));
  if (controller.maskCount() != 1 || controller.photoLayerCount() != 1 || !controller.canUndo()) {
    return fail("mask creation should create a linked layer and enter undo history");
  }
  controller.undo();
  if (controller.maskCount() != 0 || controller.photoLayerCount() != 0 || !controller.canRedo()) {
    return fail("undo should remove a newly created mask and linked adjustment layer");
  }
  controller.redo();
  if (controller.maskCount() != 1 || controller.photoLayerCount() != 1) {
    return fail("redo should restore a newly created mask and linked adjustment layer");
  }

  controller.setActiveMaskExposure(0.46);
  if (!almost_equal(controller.activeMaskExposure(), 0.46)) {
    return fail("mask edit history proof should edit the active mask");
  }
  controller.undo();
  if (!almost_equal(controller.activeMaskExposure(), 0.0) || !controller.canRedo()) {
    return fail("undo should restore the previous active mask edit");
  }
  controller.redo();
  if (!almost_equal(controller.activeMaskExposure(), 0.46)) {
    return fail("redo should restore the active mask edit");
  }

  if (!controller.setPhotoLayerAdjustmentValue(0, QStringLiteral("vibrance"), 37.0)) {
    return fail("layer-local color history proof should edit the linked adjustment layer");
  }
  controller.undo();
  auto history_layer = controller.photoLayers().at(0).toMap();
  if (!almost_equal(history_layer.value(QStringLiteral("vibrance")).toDouble(), 0.0) ||
      !controller.canRedo()) {
    return fail("undo should restore the previous layer-local color edit");
  }
  controller.redo();
  history_layer = controller.photoLayers().at(0).toMap();
  if (!almost_equal(history_layer.value(QStringLiteral("vibrance")).toDouble(), 37.0)) {
    return fail("redo should restore the layer-local color edit");
  }

  if (!controller.addFillLayer(QStringLiteral("#303840"), QStringLiteral("History Fill")) ||
      controller.photoLayerCount() != 2) {
    return fail("layer reorder/delete history proof should prepare a second layer");
  }
  const auto order_before_move = layer_order_ids(controller);
  if (!controller.movePhotoLayer(0, 1)) {
    return fail("layer reorder history proof should move a layer");
  }
  const auto order_after_move = layer_order_ids(controller);
  if (order_after_move == order_before_move) {
    return fail("layer reorder should change layer order before undo");
  }
  controller.undo();
  if (layer_order_ids(controller) != order_before_move || !controller.canRedo()) {
    return fail("undo should restore layer order before a reorder");
  }
  controller.redo();
  if (layer_order_ids(controller) != order_after_move) {
    return fail("redo should restore layer order after a reorder");
  }

  const auto count_before_delete = controller.photoLayerCount();
  const auto order_before_delete = layer_order_ids(controller);
  if (!controller.deletePhotoLayer(1) || controller.photoLayerCount() != count_before_delete - 1) {
    return fail("layer deletion history proof should delete one layer");
  }
  controller.undo();
  if (controller.photoLayerCount() != count_before_delete ||
      layer_order_ids(controller) != order_before_delete ||
      !controller.canRedo()) {
    return fail("undo should restore a deleted layer in its previous order");
  }
  controller.redo();
  if (controller.photoLayerCount() != count_before_delete - 1) {
    return fail("redo should delete the same layer again");
  }

  if (!controller.setPhotoLayerGaussianBlur(0, QString(), 4.5, 0.75, true))
    return fail("Gaussian creation should mutate the selected layer");
  const auto added = controller.photoLayerEffects(0);
  if (added.size() != 1) return fail("Gaussian should appear in the effect rows");
  const auto effect_id = added[0].toMap().value(QStringLiteral("id")).toString();
  if (effect_id.isEmpty()) return fail("Gaussian requires a stable ID");
  controller.undo();
  if (!controller.photoLayerEffects(0).isEmpty()) return fail("Undo should remove Gaussian");
  controller.redo();
  if (controller.photoLayerEffects(0) != added) return fail("Redo should preserve effect identity and values");
  if (!controller.setPhotoLayerGaussianBlur(0, effect_id, 12, 0.25, false))
    return fail("Gaussian parameters and bypass should be editable");
  controller.undo();
  if (controller.photoLayerEffects(0) != added) return fail("Undo should restore Gaussian parameters");
  if (controller.setPhotoLayerGaussianBlur(0, QStringLiteral("missing"), 1, 1, true) ||
      controller.setPhotoLayerGaussianBlur(0, effect_id, -1, 1, true))
    return fail("Invalid Gaussian edit should be rejected");
  if (!controller.removePhotoLayerEffect(0, effect_id) || !controller.photoLayerEffects(0).isEmpty())
    return fail("Remove should delete the requested effect");
  controller.undo();
  if (controller.photoLayerEffects(0) != added) return fail("Undo removal should restore the effect");

  photo_editor::desktop::EditorPreviewController reopened;
  reopened.loadAsset(QStringLiteral("layer-mask-history-asset"), QString(), QString(),
      QString(), QString(), QString(), controller.currentAdjustmentPatchJson());
  if (reopened.photoLayerEffects(0) != added)
    return fail("Controller recipe reload should retain Gaussian identity and parameters");
  if (!reopened.setPhotoLayerGaussianBlur(0, QString(), 2, 1, true))
    return fail("Second Gaussian creation should succeed");
  const auto before_reorder = reopened.photoLayerEffects(0);
  if (!reopened.movePhotoLayerEffect(0, effect_id, 1)) return fail("Effect reorder should succeed");
  const auto after_reorder = reopened.photoLayerEffects(0);
  if (after_reorder.size() != 2 || after_reorder[1] != before_reorder[0] ||
      after_reorder[0] != before_reorder[1]) return fail("Reorder should preserve effect payloads");
  reopened.undo();
  if (reopened.photoLayerEffects(0) != before_reorder) return fail("Undo should restore effect order");
  reopened.redo();
  if (reopened.photoLayerEffects(0) != after_reorder) return fail("Redo should restore effect order");
  if (!reopened.duplicatePhotoLayerEffect(0, effect_id)) return fail("Effect duplication should succeed");
  const auto duplicated = reopened.photoLayerEffects(0);
  if (duplicated.size() != 3) return fail("Duplicate should append one adjacent effect");
  auto copy = duplicated[2].toMap();
  const auto copied_id = copy.value(QStringLiteral("id")).toString();
  if (copied_id.isEmpty() || copied_id == effect_id) return fail("Duplicate must receive a new identity");
  copy.insert(QStringLiteral("id"), effect_id);
  if (copy != duplicated[1].toMap()) return fail("Duplicate must preserve all exposed parameters");
  reopened.undo();
  if (reopened.photoLayerEffects(0) != after_reorder) return fail("Undo duplication must preserve originals");
  if (!reopened.setPhotoLayerEffectEdgeMode(0, effect_id, 2) ||
      reopened.photoLayerEffects(0)[1].toMap().value(QStringLiteral("edgeMode")).toInt() != 2)
    return fail("Reflect edge mode should be editable");
  reopened.undo();
  if (reopened.photoLayerEffects(0) != after_reorder) return fail("Undo should restore edge mode");
  if (reopened.setPhotoLayerEffectEdgeMode(0, effect_id, 3)) return fail("Invalid edge mode should fail");
  photo_editor::desktop::EditRecipeState future_state;
  future_state.layers = reopened.photoLayers();
  if (future_state.layers.isEmpty()) return fail("Future fixture requires a layer");
  auto future_layer = future_state.layers[0].toMap();
  auto future_effects = photo_editor::desktop::decode_photo_layer_effects(future_layer);
  if (!future_effects || future_effects->empty()) return fail("Future effect fixture requires a stack");
  auto& future = future_effects->front();
  future.type_id = "future-filter";
  future.opaque_parameters = "preserve these settings";
  const auto future_id = QString::fromStdString(future.id);
  const auto future_payload = photo_editor::desktop::encode_photo_layer_effects(*future_effects);
  if (!future_payload) return fail("Future effect fixture must serialize");
  future_layer.insert(QStringLiteral("effectStack"), *future_payload);
  future_state.layers[0] = future_layer;
  reopened.loadAsset(QStringLiteral("future-effect-asset"), QString(), QString(), QString(),
      QString(), QString(), photo_editor::desktop::serialize_edit_recipe_json(future_state));
  if (!reopened.setPhotoLayerEffectEnabled(0, future_id, false))
    return fail("Unavailable effects must be bypassable");
  const auto disabled = photo_editor::desktop::decode_photo_layer_effects(reopened.photoLayers()[0].toMap());
  if (!disabled || disabled->front().enabled || disabled->front().opaque_parameters != future.opaque_parameters)
    return fail("Bypassing must preserve unavailable effect settings");

  photo_editor::desktop::EditRecipeState selection_state;
  selection_state.layers = reopened.photoLayers();
  selection_state.mask_stack = {QVariantMap{{QStringLiteral("id"), QStringLiteral("selection-component")},
      {QStringLiteral("kind"), QStringLiteral("linear")},
      {QStringLiteral("toolRole"), QStringLiteral("selection")},
      {QStringLiteral("maskSetId"), QStringLiteral("source-selection")},
      {QStringLiteral("maskSetName"), QStringLiteral("Original selection")}}};
  auto second_component = selection_state.mask_stack[0].toMap();
  second_component.insert(QStringLiteral("id"), QStringLiteral("selection-subtract"));
  second_component.insert(QStringLiteral("kind"), QStringLiteral("radial"));
  second_component.insert(QStringLiteral("operation"), QStringLiteral("subtract"));
  selection_state.mask_stack.push_back(second_component);
  reopened.loadAsset(QStringLiteral("selection-effect-asset"), QString(), QString(), QString(), QString(),
      QString(), photo_editor::desktop::serialize_edit_recipe_json(selection_state));
  reopened.setActiveMaskIndex(0);
  const auto original_mask_count = reopened.maskCount();
  if (!reopened.maskPhotoLayerEffectFromSelection(0, future_id))
    return fail("Selection should initialize an effect mask");
  const auto masked = photo_editor::desktop::decode_photo_layer_effects(reopened.photoLayers()[0].toMap());
  if (!masked || masked->front().mask_ref.empty() || masked->front().mask_ref == "source-selection" ||
      reopened.maskCount() != original_mask_count + 2)
    return fail("Effect mask must copy the selection into an independent set");
  int copied_components = 0;
  bool copied_subtraction = false;
  for (const auto& value : reopened.maskStack()) {
    const auto component = value.toMap();
    if (component.value(QStringLiteral("maskSetId")).toString().toStdString() != masked->front().mask_ref)
      continue;
    ++copied_components;
    copied_subtraction = copied_subtraction ||
        component.value(QStringLiteral("operation")).toString() == QStringLiteral("subtract");
    if (component.value(QStringLiteral("id")).toString() == QStringLiteral("selection-component") ||
        component.value(QStringLiteral("id")).toString() == QStringLiteral("selection-subtract"))
      return fail("Copied selection components require new IDs");
  }
  if (copied_components != 2 || !copied_subtraction)
    return fail("Effect masks must retain all selection components and their operations");
  const auto masked_layers_before_duplicate = reopened.photoLayers();
  const auto masks_before_duplicate = reopened.maskStack();
  if (!reopened.duplicatePhotoLayerEffect(0, future_id)) return fail("Masked effect duplication should succeed");
  const auto duplicated_masked = photo_editor::desktop::decode_photo_layer_effects(reopened.photoLayers()[0].toMap());
  if (!duplicated_masked || duplicated_masked->size() != masked->size() + 1 ||
      duplicated_masked->at(1).mask_ref == masked->front().mask_ref || duplicated_masked->at(1).mask_ref.empty() ||
      reopened.maskCount() != original_mask_count + 4)
    return fail("Duplicated effects require an independent copy of their mask set");
  int duplicate_components = 0;
  for (const auto& value : reopened.maskStack()) {
    const auto component = value.toMap();
    if (component.value("maskSetId").toString().toStdString() != duplicated_masked->at(1).mask_ref) continue;
    ++duplicate_components;
    for (const auto& prior : masks_before_duplicate)
      if (prior.toMap().value("id") == component.value("id"))
        return fail("Duplicated mask components must receive fresh IDs");
  }
  if (duplicate_components != 2) return fail("Duplicated effect masks must copy every component");
  photo_editor::desktop::EditRecipeState duplicated_state;
  duplicated_state.layers = reopened.photoLayers();
  duplicated_state.mask_stack = reopened.maskStack();
  photo_editor::desktop::EditorPreviewController duplicated_reopened;
  duplicated_reopened.loadAsset(QStringLiteral("duplicate-reopen"), QString(), QString(), QString(),
      QString(), QString(), photo_editor::desktop::serialize_edit_recipe_json(duplicated_state));
  if (duplicated_reopened.photoLayers() != duplicated_state.layers ||
      duplicated_reopened.maskStack() != duplicated_state.mask_stack)
    return fail("Duplicated effect references and mask payloads must survive reopening");
  if (!duplicated_reopened.selectPhotoLayerEffectMask(0,
      QString::fromStdString(duplicated_masked->at(1).id))) return fail("Reopened duplicate mask must be editable");
  duplicated_reopened.setActiveMaskOpacity(0.37);
  if (duplicated_reopened.activeMaskOpacity() != 0.37)
    return fail("Reopened duplicate mask must apply its opacity edit");
  for (const auto& original : duplicated_state.mask_stack) {
    const auto component = original.toMap();
    if (component.value("maskSetId").toString().toStdString() != masked->front().mask_ref) continue;
    bool unchanged = false;
    for (const auto& current : duplicated_reopened.maskStack())
      if (current.toMap().value("id") == component.value("id")) unchanged = current.toMap() == component;
    if (!unchanged) return fail("Editing a reopened duplicate mask must not alter original mask components");
  }

  reopened.undo();
  if (reopened.photoLayers() != masked_layers_before_duplicate || reopened.maskStack() != masks_before_duplicate)
    return fail("One undo must restore both the effect stack and selection masks after duplication");
  reopened.undo();
  const auto unmasked = photo_editor::desktop::decode_photo_layer_effects(reopened.photoLayers()[0].toMap());
  if (!unmasked || !unmasked->front().mask_ref.empty() || reopened.maskCount() != original_mask_count)
    return fail("Undo effect masking must remove its copied selection");
  reopened.redo();
  const auto mask_count_before_clear = reopened.maskCount();
  if (!reopened.clearPhotoLayerEffectMask(0, future_id)) return fail("Effect mask removal should succeed");
  const auto cleared = photo_editor::desktop::decode_photo_layer_effects(reopened.photoLayers()[0].toMap());
  if (!cleared || !cleared->front().mask_ref.empty() || reopened.maskCount() != mask_count_before_clear)
    return fail("Clearing coverage must retain the saved selection");
  reopened.undo();
  const auto restored_mask = photo_editor::desktop::decode_photo_layer_effects(reopened.photoLayers()[0].toMap());
  if (!restored_mask || restored_mask->front().mask_ref != masked->front().mask_ref)
    return fail("Undo mask removal should reconnect the same selection");
  if (!reopened.selectPhotoLayerEffectMask(0, future_id)) return fail("Effect mask should be selectable");
  const auto selected_mask = reopened.maskStack()[reopened.activeMaskIndex()].toMap();
  if (selected_mask.value(QStringLiteral("maskSetId")).toString().toStdString() != masked->front().mask_ref)
    return fail("Edit mask must select the effect copy, not the original selection");

  const auto before_invert = reopened.photoLayerEffects(0);
  if (!reopened.addPhotoLayerInvert(0)) return fail("Invert creation should succeed");
  const auto invert_row = reopened.photoLayerEffects(0).last().toMap();
  const auto invert_id = invert_row.value(QStringLiteral("id")).toString();
  if (invert_row.value(QStringLiteral("type")).toString() != QStringLiteral("invert_linear_rgb") ||
      !reopened.setPhotoLayerEffectAmount(0, invert_id, 0.4)) return fail("Invert amount should be editable");
  reopened.undo();
  if (reopened.photoLayerEffects(0).last().toMap().value(QStringLiteral("amount")).toDouble() != 1)
    return fail("Undo amount should restore full invert");
  reopened.undo();
  if (reopened.photoLayerEffects(0) != before_invert) return fail("Undo invert should preserve the earlier stack");
  if (!reopened.setPhotoLayerBoxBlur(0, QString(), 3, 0.8, true)) return fail("Box blur creation should succeed");
  const auto box_row = reopened.photoLayerEffects(0).last().toMap();
  const auto box_id = box_row.value(QStringLiteral("id")).toString();
  if (box_row.value(QStringLiteral("type")).toString() != QStringLiteral("box_blur") ||
      box_row.value(QStringLiteral("radius")).toInt() != 3 ||
      !reopened.setPhotoLayerBoxBlur(0, box_id, 7, 0.8, true)) return fail("Box radius should be editable");
  reopened.undo();
  if (reopened.photoLayerEffects(0).last().toMap() != box_row) return fail("Undo box edit should restore radius");
  reopened.undo();
  if (reopened.photoLayerEffects(0) != before_invert) return fail("Undo box creation should preserve prior effects");
  reopened.redo();
  if (!reopened.setPhotoLayerEffectEdgeMode(0, box_id, 2)) return fail("Box edge mode should change");
  const auto before_reset = reopened.photoLayerEffects(0);
  if (!reopened.resetPhotoLayerEffect(0, box_id)) return fail("Reset box parameters should succeed");
  const auto reset_box = reopened.photoLayerEffects(0).last().toMap();
  if (reset_box.value(QStringLiteral("id")).toString() != box_id ||
      reset_box.value(QStringLiteral("radius")).toInt() != 8 ||
      reset_box.value(QStringLiteral("edgeMode")).toInt() != 0 ||
      reset_box.value(QStringLiteral("amount")).toDouble() != 1)
    return fail("Reset should restore box defaults and preserve identity");
  reopened.undo();
  if (reopened.photoLayerEffects(0) != before_reset) return fail("Reset should be one undo operation");
  const auto before_posterize = reopened.photoLayerEffects(0);
  if (!reopened.setPhotoLayerPosterize(0, QString(), 6, 0.8, true)) return fail("Posterize creation should succeed");
  const auto posterize = reopened.photoLayerEffects(0).last().toMap();
  const auto posterize_id = posterize.value("id").toString();
  if (posterize.value("levels").toInt() != 6 || posterize.value("type") != "posterize_linear_rgb" ||
      reopened.setPhotoLayerPosterize(0, posterize_id, 1, 1, true)) return fail("Posterize levels require validation");
  if (!reopened.resetPhotoLayerEffect(0, posterize_id) ||
      reopened.photoLayerEffects(0).last().toMap().value("levels").toInt() != 4)
    return fail("Posterize reset must restore four levels");
  reopened.undo();
  if (reopened.photoLayerEffects(0).last().toMap() != posterize) return fail("Undo reset must restore posterize settings");
  reopened.undo();
  if (reopened.photoLayerEffects(0) != before_posterize) return fail("Undo posterize must preserve the earlier stack");
  const auto preview_baseline = reopened.photoLayers();
  const auto preview_masks = reopened.maskStack();
  const bool had_redo = reopened.canRedo();
  if (!reopened.previewPhotoLayerEffect(0, QStringLiteral("gaussian_blur")))
    return fail("Gaussian preview should start");
  auto preview_id = reopened.photoLayerEffects(0).last().toMap().value("id").toString();
  const auto draft_layers = reopened.photoLayers();
  reopened.undo();
  reopened.redo();
  if (reopened.photoLayers() != draft_layers)
    return fail("Global history shortcuts must not cross an active filter preview boundary");
  if (!reopened.setPhotoLayerGaussianBlur(0, preview_id, 24, 0.5, true) ||
      !reopened.finishPhotoLayerEffectPreview(false) || reopened.photoLayers() != preview_baseline ||
      reopened.maskStack() != preview_masks || reopened.canRedo() != had_redo)
    return fail("Cancel must restore exact layers and masks without clearing redo");
  if (!reopened.previewPhotoLayerEffect(0, QStringLiteral("gaussian_blur")) ||
      !reopened.previewPhotoLayerEffect(0, QStringLiteral("posterize_linear_rgb")))
    return fail("Changing browser choice should replace the temporary effect");
  if (reopened.photoLayerEffects(0).size() != before_posterize.size() + 1 ||
      reopened.photoLayerEffects(0).last().toMap().value("type") != "posterize_linear_rgb")
    return fail("Preview choices must not accumulate effects");
  preview_id = reopened.photoLayerEffects(0).last().toMap().value("id").toString();
  if (!reopened.setPhotoLayerPosterize(0, preview_id, 11, 0.7, true) ||
      !reopened.finishPhotoLayerEffectPreview(true)) return fail("Apply should accept the preview");
  const auto applied_preview = reopened.photoLayers();
  reopened.undo();
  if (reopened.photoLayers() != preview_baseline) return fail("Apply and parameter changes must be one undo step");
  reopened.redo();
  if (reopened.photoLayers() != applied_preview) return fail("Redo must restore the accepted effect identity and settings");
  reopened.undo();
  reopened.beginCoalescedEdit();
  if (reopened.previewPhotoLayerEffect(0, QStringLiteral("box_blur")))
    return fail("A filter preview must not take ownership of another edit transaction");
  reopened.cancelCoalescedEdit();
  if (reopened.photoLayers() != preview_baseline) return fail("Rejected preview must leave the recipe unchanged");
  const auto preview_asset = reopened.assetId();
  if (!reopened.previewPhotoLayerEffect(0, QStringLiteral("invert_linear_rgb")))
    return fail("Asset-switch preview should start");
  reopened.loadAsset(QStringLiteral("preview-other-asset"), {}, {}, {}, {}, {}, {});
  if (reopened.finishPhotoLayerEffectPreview(true)) return fail("Switching assets must invalidate the preview");
  reopened.loadAsset(preview_asset, {}, {}, {}, {}, {}, {});
  if (reopened.photoLayers() != preview_baseline)
    return fail("Returning to an asset must not recover an unaccepted filter preview");

  for (const auto& type : {QStringLiteral("motion_blur"), QStringLiteral("unsharp_mask")}) {
    const auto baseline = reopened.photoLayers();
    if (!reopened.previewPhotoLayerEffect(0, type)) return fail("Spatial preview must begin");
    const auto id = reopened.photoLayerEffects(0).last().toMap().value("id").toString();
    const bool edited = type == "motion_blur"
        ? reopened.setPhotoLayerMotionBlur(0, id, 37.25, -48.5, 0.65, true)
        : reopened.setPhotoLayerUnsharpMask(0, id, 2.75, 1.85, 0.025, 0.65, true);
    if (!edited || !reopened.finishPhotoLayerEffectPreview(true)) return fail("Spatial preview Apply");
    const auto applied = reopened.photoLayers();
    reopened.undo();
    if (reopened.photoLayers() != baseline) return fail("Spatial Apply must be one undo step");
    reopened.redo();
    if (reopened.photoLayers() != applied) return fail("Spatial redo must restore exact settings");
    if (!reopened.resetPhotoLayerEffect(0, id)) return fail("Spatial reset");
    const auto defaults = reopened.photoLayerEffects(0).last().toMap();
    if ((type == "motion_blur" && (defaults.value("distance").toDouble() != 20 || defaults.value("angle").toDouble() != 0)) ||
        (type == "unsharp_mask" && (defaults.value("sharpenRadius").toDouble() != 2 || defaults.value("strength").toDouble() != 1)))
      return fail("Spatial reset restores defaults");
    reopened.undo();
    if (reopened.photoLayers() != applied) return fail("Undo spatial reset");
    if (!reopened.previewPhotoLayerEffect(0, type) || !reopened.finishPhotoLayerEffectPreview(false) ||
        reopened.photoLayers() != applied) return fail("Cancel spatial preview leaves accepted stack exact");
    if (!reopened.setPhotoLayerLocked(0, true)) return fail("Lock spatial target");
    if (reopened.previewPhotoLayerEffect(0, type)) return fail("Locked target must reject preview");
    reopened.undo();
    if (reopened.photoLayers() != applied) return fail("Rejected preview must not consume undo");
  }

  return EXIT_SUCCESS;
}
