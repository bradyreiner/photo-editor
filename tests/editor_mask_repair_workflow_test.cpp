#include <cstdlib>
#include <functional>
#include <iostream>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QImage>
#include <QColorSpace>
#include <QTemporaryDir>
#include <QThread>

#include "editor_preview_controller.h"
#include "ai_model_runtime.h"
#include "ai_inpaint_patch_region.h"
#include "edit_adjustment_patch.h"
#include "mask_workflow_ownership.h"
#include "edit_recipe_mask_schema.h"
#include "photo_layer_effect_codec.h"
#include "canonical_composite_mask_alpha.h"
#include "flattened_output_renderer.h"

using namespace photo_editor::desktop;

namespace {
void require(bool value, const char* message) {
  if (!value) throw std::runtime_error(message);
}
bool settle(const std::function<bool()>& condition, int timeout = 15000) {
  QElapsedTimer timer;
  timer.start();
  while (!condition() && timer.elapsed() < timeout) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    QThread::msleep(1);
  }
  return condition();
}
void load(EditorPreviewController& controller, const QString& id, const EditRecipeState& state = {}, const QString& path = {}) {
  controller.loadAsset(id, path, {}, {}, {}, {}, serialize_edit_recipe_json(state));
}
EditRecipeState recipe(const EditorPreviewController& controller) {
  const auto patch = parse_adjustment_patch_json(controller.currentAdjustmentPatchJson());
  require(patch.ok(), "Controller adjustment patch must decode before fixture assertions");
  return *patch.state;
}

void deletion_workflow() {
  EditorPreviewController controller;
  load(controller, QStringLiteral("exclusive-mask-owner"));
  controller.addMask(QStringLiteral("linear"));
  static_cast<void>(controller.ensureActiveSelectionAdjustmentLayer());
  controller.setActiveMaskExposure(1.3);
  const auto before = recipe(controller);
  const auto masks = controller.maskStack();
  require(masks.size() == 1 && controller.photoLayerCount() == 1, "Fixture requires a masked adjustment owner");
  require(controller.deletePhotoLayer(0), "Deleting a masked layer should succeed");
  require(controller.maskCount() == 0 && controller.photoLayerCount() == 0 && !controller.hasActiveMask(),
      "Deleting the owner must remove exclusive geometry, mask-local effects and active references");
  controller.setActiveMaskExposure(2.0);
  require(controller.maskCount() == 0 && controller.photoLayerCount() == 0,
      "A property write after deletion must not recreate a selection scaffold");
  controller.undo();
  require(settle([&] { return controller.maskStack() == masks && controller.photoLayerCount() == 1; }),
      "One Undo must restore the owner and exact source geometry/adjustments");
  controller.redo();
  require(settle([&] { return controller.maskCount() == 0 && controller.photoLayerCount() == 0; }),
      "One Redo must remove the entire owned edit");
  EditorPreviewController reopened;
  load(reopened, QStringLiteral("exclusive-reopened"), recipe(controller));
  require(reopened.maskCount() == 0 && reopened.photoLayerCount() == 0, "Deleted masks must stay deleted after recipe reopen");

  auto coincidental_text = before;
  const auto selection_name = before.mask_stack[0].toMap().value(QStringLiteral("maskSetName")).toString();
  coincidental_text.layers.append(QVariantMap{{QStringLiteral("id"), QStringLiteral("unrelated-text")},
      {QStringLiteral("kind"), QStringLiteral("text")}, {QStringLiteral("name"), selection_name},
      {QStringLiteral("text"), selection_name}, {QStringLiteral("locked"), true}});
  load(controller, QStringLiteral("coincidental-mask-name-text"), coincidental_text);
  auto unrelated_text = controller.photoLayers()[1].toMap();
  unrelated_text.remove(QStringLiteral("flatIndex"));
  require(!selection_has_locked_consumer(controller.maskStack()[0].toMap(), {unrelated_text}),
      "An unrelated locked text label must not lock matching selection geometry");
  const auto removed_owner = controller.deletePhotoLayer(0);
  auto surviving_text = controller.photoLayers()[0].toMap();
  require(surviving_text.take(QStringLiteral("flatIndex")).toInt() == 0,
      "Surviving text row must move to its current presentation index");
  if (!removed_owner || controller.maskCount() != 0 || controller.photoLayerCount() != 1 || surviving_text != unrelated_text) {
    std::cerr << "coincidental_name removed=" << removed_owner << " mask_count=" << controller.maskCount()
              << " layers=" << controller.photoLayerCount() << '\n';
    if (controller.photoLayerCount() == 1) {
      const auto actual = surviving_text;
      for (auto it = unrelated_text.cbegin(); it != unrelated_text.cend(); ++it)
        if (actual.value(it.key()) != it.value()) std::cerr << "survivor_diff=" << it.key().toStdString()
            << " expected=" << it.value().toString().toStdString() << " actual=" << actual.value(it.key()).toString().toStdString() << '\n';
    }
  }
  require(removed_owner && controller.maskCount() == 0 &&
      controller.photoLayerCount() == 1 && surviving_text == unrelated_text,
      "Unrelated text/name matching a selection name is not a geometry consumer and cannot retain deleted effects");

  auto shared = before;
  auto second = shared.layers[0].toMap();
  second.insert(QStringLiteral("id"), QStringLiteral("second-owner"));
  shared.layers.append(second);
  load(controller, QStringLiteral("shared-mask-owner"), shared);
  const auto shared_masks = controller.maskStack();
  require(controller.deletePhotoLayer(0) && controller.maskStack() == shared_masks && controller.photoLayerCount() == 1,
      "Deleting one shared owner must preserve the surviving owner's exact coverage and legacy adjustments");
  auto legacy_survivor = before.layers[0].toMap();
  legacy_survivor.remove(QStringLiteral("selectionMaskSetId"));
  legacy_survivor.remove(QStringLiteral("selectionMaskSetIds"));
  require(exclusive_mask_sets_for_deleted_layer(before.layers[0].toMap(), {legacy_survivor}, before.mask_stack).isEmpty(),
      "A real surviving legacy selection-name reference must preserve shared geometry");
  auto opaque_survivor = unrelated_text;
  opaque_survivor.insert(QStringLiteral("effectStack"), QStringLiteral("opaque-invalid-effects"));
  require(exclusive_mask_sets_for_deleted_layer(before.layers[0].toMap(), {opaque_survivor}, before.mask_stack).isEmpty(),
      "Opaque effect consumers must conservatively retain possibly shared geometry");
  const auto set_id = before.mask_stack[0].toMap().value(QStringLiteral("maskSetId")).toString();
  auto intersection_survivor = unrelated_text;
  intersection_survivor.insert(QStringLiteral("selectionScoped"), true);
  intersection_survivor.insert(QStringLiteral("selectionMaskSetId"), QStringLiteral("other-primary-set"));
  intersection_survivor.insert(QStringLiteral("selectionIntersectionMaskSetIds"), QStringList{set_id});
  require(exclusive_mask_sets_for_deleted_layer(before.layers[0].toMap(), {intersection_survivor}, before.mask_stack).isEmpty() &&
      selection_has_locked_consumer(before.mask_stack[0].toMap(), {intersection_survivor}),
      "A surviving locked intersection-only ID consumer must retain and protect shared geometry");
  intersection_survivor.insert(QStringLiteral("selectionIntersectionMaskSetIds"), QStringList{QString()});
  intersection_survivor.insert(QStringLiteral("selectionIntersectionMaskSetNames"), QStringList{selection_name});
  require(exclusive_mask_sets_for_deleted_layer(before.layers[0].toMap(), {intersection_survivor}, before.mask_stack).isEmpty() &&
      selection_has_locked_consumer(before.mask_stack[0].toMap(), {intersection_survivor}),
      "An intersection legacy-name fallback must remain independent of another primary selection ID");
  require(exclusive_mask_sets_for_deleted_layer(intersection_survivor, {}, before.mask_stack).contains(set_id),
      "Deleting an exclusive intersection-name owner must clean up its geometry");
  intersection_survivor.insert(QStringLiteral("selectionIntersectionMaskSetIds"), QStringList{set_id});
  require(exclusive_mask_sets_for_deleted_layer(intersection_survivor, {}, before.mask_stack).contains(set_id),
      "Deleting an exclusive intersection-ID owner must clean up its geometry");
  auto mixed_survivor = unrelated_text;
  mixed_survivor.insert(QStringLiteral("selectionScoped"), true);
  mixed_survivor.insert(QStringLiteral("selectionMaskSetIds"), QVariantList{QStringLiteral("other-primary-set")});
  mixed_survivor.insert(QStringLiteral("selectionMaskSetNames"), QVariantList{QStringLiteral("Other selection"), selection_name});
  require(exclusive_mask_sets_for_deleted_layer(before.layers[0].toMap(), {mixed_survivor}, before.mask_stack).isEmpty() &&
      selection_has_locked_consumer(before.mask_stack[0].toMap(), {mixed_survivor}),
      "A strong primary ID plus a later legacy-name entry must preserve and lock both real consumers");
  require(exclusive_mask_sets_for_deleted_layer(mixed_survivor, {}, before.mask_stack).contains(set_id),
      "Deleting a mixed ID/name owner must clean up its exclusive later legacy-name geometry");

  auto legacy = before;
  auto legacy_layer = legacy.layers[0].toMap();
  legacy_layer.remove(QStringLiteral("selectionMaskSetId"));
  legacy_layer.remove(QStringLiteral("selectionMaskSetIds"));
  legacy.layers[0] = legacy_layer;
  load(controller, QStringLiteral("legacy-name-owner"), legacy);
  require(controller.deletePhotoLayer(0) && controller.maskCount() == 0,
      "A unique legacy name reference may safely identify exclusive geometry");

  auto ambiguous = legacy;
  auto duplicate_name_mask = ambiguous.mask_stack[0].toMap();
  duplicate_name_mask.insert(QStringLiteral("id"), QStringLiteral("ambiguous-source"));
  duplicate_name_mask.insert(QStringLiteral("maskSetId"), QStringLiteral("ambiguous-set"));
  ambiguous.mask_stack.append(duplicate_name_mask);
  load(controller, QStringLiteral("legacy-ambiguous-owner"), ambiguous);
  const auto ambiguous_masks = controller.maskStack();
  require(controller.deletePhotoLayer(0) && controller.maskStack() == ambiguous_masks,
      "Duplicate legacy selection names must never trigger broad geometry deletion");

  load(controller, QStringLiteral("effect-only-owner"));
  require(controller.addFillLayer(QStringLiteral("#335577"), QStringLiteral("Effect owner")), "Effect owner should be created");
  require(controller.addPhotoLayerInvert(0), "Effect fixture requires an editable effect");
  controller.addMask(QStringLiteral("radial"));
  static_cast<void>(controller.ensureActiveSelectionAdjustmentLayer());
  const auto effect_id = controller.photoLayerEffects(1)[0].toMap().value(QStringLiteral("id")).toString();
  require(controller.maskPhotoLayerEffectFromSelection(1, effect_id), "Effect fixture should copy its selection coverage");
  const auto effect_masks = controller.maskStack();
  const auto decoded_effects = decode_photo_layer_effects(controller.photoLayers()[1].toMap());
  require(decoded_effects && !decoded_effects->empty(), "Masked effect must decode");
  const auto effect_set = QString::fromStdString(decoded_effects->front().mask_ref);
  require(controller.deletePhotoLayer(1), "Effect-only owner deletion should succeed");
  for (const auto& value : controller.maskStack())
    require(value.toMap().value(QStringLiteral("maskSetId")).toString() != effect_set,
        "Deleting an exclusive effect owner must remove its private effect geometry");
  require(controller.maskCount() < effect_masks.size() && controller.maskCount() > 0,
      "Deleting effect-private geometry must retain the independent original selection");
}

void session_workflow() {
  EditorPreviewController controller;
  load(controller, QStringLiteral("selection-session"));
  controller.addMask(QStringLiteral("radial"));
  static_cast<void>(controller.ensureActiveSelectionAdjustmentLayer());
  const auto original_masks = controller.maskStack();
  const auto global_history = controller.editHistoryEntries().size();
  require(controller.beginSelectionEditSession(), "Existing selection should open a staged session");
  require(!controller.selectionEditSessionDirty() && !controller.canUndo(), "Untouched session must not expose earlier global Undo");
  controller.setActiveMaskExposure(0.5);
  controller.setActiveMaskExposure(0.9);
  require(controller.canUndo(), "Session edits require local Undo");
  controller.undo();
  require(controller.activeMaskExposure() == 0.5, "Local Undo must restore the previous session gesture");
  controller.redo();
  require(controller.activeMaskExposure() == 0.9, "Local Redo must restore the same gesture");
  controller.beginCoalescedEdit();
  controller.setActiveMaskFeather(0.2);
  controller.setActiveMaskFeather(0.3);
  controller.endCoalescedEdit();
  controller.undo();
  require(controller.activeMaskFeather() == original_masks[0].toMap().value(QStringLiteral("feather")).toDouble(),
      "A parameter drag must be one local gesture");
  require(controller.editHistoryEntries().size() == global_history, "Local gestures must not append global history entries");
  load(controller, QStringLiteral("unrelated-photo"));
  require(controller.selectionEditSessionActive() && controller.activeMaskExposure() == 0.9,
      "A dirty session must prevent implicit asset retargeting");
  require(!controller.deletePhotoLayer(0), "A dirty session must resolve before deleting its owner");
  require(controller.cancelSelectionEditSession() && controller.maskStack() == original_masks,
      "Cancel must restore exact recipe geometry and adjustments");
  require(controller.editHistoryEntries().size() == global_history, "Cancel must leave global history unchanged");

  require(controller.beginSelectionEditSession(), "Untouched session should reopen");
  require(controller.finishSelectionEditSession() && controller.editHistoryEntries().size() == global_history,
      "Untouched Done creates no history operation");
  require(controller.beginSelectionEditSession(), "Modified session should reopen");
  controller.startSelectionBrush(false);
  require(controller.maskCount() == 2 && controller.photoLayerCount() == 1 &&
      controller.maskStack()[0].toMap().value(QStringLiteral("maskSetId")) ==
      controller.maskStack()[1].toMap().value(QStringLiteral("maskSetId")),
      "Add Brush must append geometry to the same selected owner");
  controller.setActiveMaskExposure(1.0);
  require(controller.finishSelectionEditSession(), "Done should publish a settled draft");
  require(controller.editHistoryEntries().size() == global_history + 1, "Done must publish exactly one global transaction");
  controller.undo();
  require(settle([&] { return controller.maskStack() == original_masks && controller.photoLayerCount() == 1; }),
      "Global Undo after Done restores the complete pre-session selection");
}

void repair_workflow() {
  auto first = default_mask_definition(QStringLiteral("brush"));
  first.insert(QStringLiteral("id"), QStringLiteral("repair-one-source"));
  first.insert(QStringLiteral("repairId"), QStringLiteral("repair-one"));
  first.insert(QStringLiteral("toolRole"), QStringLiteral("retouch"));
  first.insert(QStringLiteral("retouchMode"), QStringLiteral("clone"));
  first.insert(QStringLiteral("committed"), false);
  auto companion = first;
  companion.insert(QStringLiteral("id"), QStringLiteral("repair-one-companion"));
  auto second = first;
  second.insert(QStringLiteral("id"), QStringLiteral("repair-two-source"));
  second.insert(QStringLiteral("repairId"), QStringLiteral("repair-two"));
  EditRecipeState state;
  state.mask_stack = {first, companion, second};
  state.active_mask_index = 0;
  EditorPreviewController controller;
  load(controller, QStringLiteral("repair-identity"), state);
  controller.setActiveMaskIndex(0);
  const auto pending = controller.maskStack();
  controller.cancelActiveRetouch();
  require(controller.maskStack() == pending, "Cancel generation must preserve editable pending repair regions");
  controller.discardActiveRetouch();
  require(controller.maskCount() == 1 && controller.maskStack()[0].toMap().value(QStringLiteral("repairId")) == QStringLiteral("repair-two"),
      "Discard must remove only the current repair's source components");
  controller.undo();
  require(settle([&] { return controller.maskCount() == 3; }), "One Undo restores discarded repair components");
  controller.setActiveMaskIndex(0);
  controller.applyActiveRetouch();
  require(controller.maskStack()[0].toMap().value(QStringLiteral("committed")).toBool() &&
      controller.maskStack()[1].toMap().value(QStringLiteral("committed")).toBool() &&
      !controller.maskStack()[2].toMap().value(QStringLiteral("committed")).toBool(),
      "Apply must commit only the selected repair's components, never a same-mode neighbor");

  QVariantMap legacy_preview{{QStringLiteral("inpaintPatchData"), QByteArray("patch")},
      {QStringLiteral("inpaintQualityMode"), QStringLiteral(" PREVIEW ")}};
  require(retouch_patch_requires_final(legacy_preview) && !unfinished_inpaint_repair_id({legacy_preview}).isEmpty(),
      "Legacy preview patches without role/ID/committed metadata must require finalization");
  legacy_preview.insert(QStringLiteral("inpaintQualityMode"), QStringLiteral("final"));
  require(!retouch_patch_requires_final(legacy_preview), "Accepted final patches must not be blocked as previews");
  require(retouch_runtime_fallback_requires_retry(QStringLiteral("remove"), true, QStringLiteral("Provider inference failed")) &&
      !retouch_runtime_fallback_requires_retry(QStringLiteral("remove"), true, {}) &&
      !retouch_runtime_fallback_requires_retry(QStringLiteral("heal"), true, QStringLiteral("Provider inference failed")),
      "Failed object-model fallback requires retry; intentional local spot/heal algorithms remain valid");
}

void baked_repair_coverage_workflow() {
  AiInpaintResult context_result;
  context_result.patch_image = QImage(100, 80, QImage::Format_RGBA64);
  context_result.patch_image.setColorSpace(QColorSpace(QColorSpace::SRgb));
  for (int y = 0; y < 80; ++y)
    for (int x = 0; x < 100; ++x)
      reinterpret_cast<QRgba64*>(context_result.patch_image.scanLine(y))[x] =
          QRgba64::fromRgba64(static_cast<quint16>(x * 511), static_cast<quint16>(y * 617), 31001, 65535);
  context_result.candidate_images = {context_result.patch_image, context_result.patch_image.rgbSwapped()};
  context_result.patch_x = 7;
  context_result.patch_y = 9;
  context_result.patch_width = 100;
  context_result.patch_height = 80;
  const auto original_context = context_result;
  const QRect reviewed(20, 23, 30, 20);
  require(retain_reviewed_inpaint_output(&context_result, reviewed) &&
      context_result.patch_image == original_context.patch_image.copy(13, 14, 30, 20) &&
      context_result.candidate_images[1] == original_context.candidate_images[1].copy(13, 14, 30, 20) &&
      context_result.patch_x == 20 && context_result.patch_y == 23 &&
      context_result.patch_width == 30 && context_result.patch_height == 20 &&
      context_result.patch_image.format() == QImage::Format_RGBA64 &&
      context_result.patch_image.colorSpace() == original_context.patch_image.colorSpace(),
      "Persisting reviewed output must preserve exact16-bit pixels, candidates, coordinates and color space");
  const auto retained = context_result.patch_image;
  require(!retain_reviewed_inpaint_output(&context_result, QRect(0, 0, 200, 200)) &&
      context_result.patch_image == retained,
      "Invalid reviewed crop must fail atomically without replacing accepted pixels");
  QTemporaryDir fixture;
  QImage source(96, 72, QImage::Format_RGB32);
  source.setColorSpace(QColorSpace(QColorSpace::SRgb));
  for (int y = 0; y < source.height(); ++y)
    for (int x = 0; x < source.width(); ++x)
      source.setPixelColor(x, y, QColor((x * 7 + y * 3) % 256, (x * 2 + y * 5) % 256, (x * 3 + y * 7) % 256));
  const auto path = fixture.filePath(QStringLiteral("coverage.png"));
  require(source.save(path), "Coverage fixture must save");
  EditorPreviewController controller;
  load(controller, QStringLiteral("baked-repair-coverage"), {}, path);
  require(settle([&] { return !controller.processing(); }), "Coverage source must load");
  controller.startHealBrush();
  auto mask = controller.maskStack()[controller.activeMaskIndex()].toMap();
  require(mask.value(QStringLiteral("selectionDespeckle")).toDouble() == 0.0 &&
      mask.value(QStringLiteral("selectionFillHoles")).toDouble() == 0.0 &&
      mask.value(QStringLiteral("selectionRemoveIslands")).toDouble() == 0.0,
      "New retouch brushes must retain painted coverage without generated-matte cleanup");
  controller.beginActiveBrushSourceStroke(QStringLiteral("add"));
  controller.addBrushPoint(0.48, 0.48);
  controller.addBrushPoint(0.56, 0.53);
  controller.endActiveBrushStroke();
  require(settle([&] { return !controller.processing(); }), "Painted repair raster must settle");
  controller.setActiveMaskSelectionExpand(4.0);
  controller.setActiveMaskSelectionSmooth(3.0);
  controller.setActiveMaskOpacity(0.75);
  controller.setActiveMaskDensity(0.8);
  require(settle([&] { return !controller.processing(); }), "Explicit repair refinements must settle");
  const auto draft = controller.maskStack();
  auto geometric = draft[controller.activeMaskIndex()].toMap();
  geometric.insert(QStringLiteral("operation"), QStringLiteral("add"));
  geometric.insert(QStringLiteral("blendMode"), QStringLiteral("normal"));
  geometric.insert(QStringLiteral("opacity"), 1.0);
  geometric.insert(QStringLiteral("density"), 1.0);
  const auto expected = evaluate_canonical_composite_mask_alpha(source, {geometric}, 1.0, {1.0, 1.0, 1.0});
  require(expected.valid && expected.alpha_mask != geometric.value(QStringLiteral("segmentationData")).toByteArray(),
      "Explicit refinement fixture must actually alter painted coverage");
  const auto baseline = recipe(controller);
  controller.applyActiveRetouch();
  require(settle([&] { return !controller.processing() && !controller.inpaintProgressActive(); }), "Local Heal Apply must finish");
  const auto accepted_raw = controller.maskStack();
  const auto accepted = recipe(controller);
  mask = accepted.mask_stack[controller.activeMaskIndex()].toMap();
  require(mask.value(QStringLiteral("committed")).toBool() &&
      mask.value(QStringLiteral("segmentationData")).toByteArray() == expected.alpha_mask &&
      mask.value(QStringLiteral("inpaintSourceMaskData")).toByteArray() == expected.alpha_mask,
      "Accepted coverage must be exactly the refined raster sent to the repair algorithm");
  require(mask.value(QStringLiteral("selectionExpand")).toDouble() == 0.0 &&
      mask.value(QStringLiteral("selectionSmooth")).toDouble() == 0.0 &&
      mask.value(QStringLiteral("opacity")).toDouble() == 0.75 &&
      mask.value(QStringLiteral("density")).toDouble() == 0.8,
      "Accepted geometry must be baked once while preserving result-strength controls");
  const auto render = [&](const EditRecipeState& state) {
    return render_flattened_output_file(QStringLiteral("coverage-render"), path, state,
        QColorSpace(QColorSpace::SRgb), QImage::Format_RGBA64);
  };
  const auto before = render(baseline);
  const auto after = render(accepted);
  require(!before.isNull() && before.size() == after.size() && before != after, "Heal must change selected output pixels");
  auto diagnostic_state = accepted;
  auto diagnostic_mask = mask;
  diagnostic_mask.insert(QStringLiteral("inpaintRuntimeProvider"), QStringLiteral("cpu"));
  diagnostic_mask.insert(QStringLiteral("inpaintAccelerationWarning"), QStringLiteral("Acceleration unavailable; using CPU."));
  QVariantMap copied_payload;
  copy_inpaint_patch_payload(&copied_payload, diagnostic_mask);
  const auto normalized_diagnostic = normalized_mask_definition(diagnostic_mask);
  const auto diagnostic_model = MaskSourceModel::fromDefinition(normalized_diagnostic, 0);
  for (const auto* key : {"inpaintRuntimeProvider", "inpaintAccelerationWarning"}) {
    const auto name = QString::fromLatin1(key);
    require(copied_payload.value(name) == diagnostic_mask.value(name) &&
        normalized_diagnostic.value(name) == diagnostic_mask.value(name) &&
        diagnostic_model.metadata.value(name) == diagnostic_mask.value(name) && !diagnostic_model.geometry.contains(name),
        "Provider and acceleration diagnostics must survive payload copy/normalization as nongeometry metadata");
  }
  diagnostic_state.mask_stack[0] = normalized_diagnostic;
  const auto diagnostic_json = serialize_edit_recipe_json(diagnostic_state);
  const auto diagnostic_roundtrip = parse_edit_recipe_json(diagnostic_json);
  require(diagnostic_roundtrip.mask_stack[0].toMap().value(QStringLiteral("inpaintRuntimeProvider")).toString() == QStringLiteral("cpu") &&
      diagnostic_roundtrip.mask_stack[0].toMap().value(QStringLiteral("inpaintAccelerationWarning")) ==
          diagnostic_mask.value(QStringLiteral("inpaintAccelerationWarning")) &&
      render(diagnostic_roundtrip) == after,
      "Diagnostic metadata must roundtrip through the production recipe codec without changing repair pixels");
  for (int y = 0; y < source.height(); ++y)
    for (int x = 0; x < source.width(); ++x)
      if (expected.alpha_mask[y * source.width() + x] == 0)
        require(before.pixelColor(x, y) == after.pixelColor(x, y), "Accepted repair must preserve pixels outside its baked coverage");
  controller.undo();
  require(settle([&] { return controller.maskStack() == draft; }), "Undo Apply must restore exact draft geometry and explicit refinement controls");
  controller.redo();
  if (!settle([&] { return controller.maskStack() == accepted_raw; })) {
    const auto actual = controller.maskStack()[0].toMap();
    const auto expected_mask = accepted_raw[0].toMap();
    auto keys = expected_mask.keys();
    for (const auto& key : actual.keys()) if (!keys.contains(key)) keys.append(key);
    for (const auto& key : keys) {
      if (actual.value(key) == expected_mask.value(key)) continue;
      std::cerr << "accepted_redo_diff=" << key.toStdString();
      if (expected_mask.value(key).metaType().id() == QMetaType::QByteArray || actual.value(key).metaType().id() == QMetaType::QByteArray)
        std::cerr << " expected_bytes=" << expected_mask.value(key).toByteArray().size() << " actual_bytes=" << actual.value(key).toByteArray().size();
      else std::cerr << " expected=" << expected_mask.value(key).toString().toStdString() << " actual=" << actual.value(key).toString().toStdString();
      std::cerr << '\n';
    }
    require(false, "Redo Apply must restore exact accepted recipe fields");
  }
  require(recipe(controller).mask_stack == accepted.mask_stack,
      "Redo must also preserve the canonical persisted repair recipe exactly");
  auto legacy = accepted;
  auto legacy_mask = legacy.mask_stack[0].toMap();
  legacy_mask.insert(QStringLiteral("selectionSmooth"), 7.0);
  legacy_mask.insert(QStringLiteral("selectionDespeckle"), 2.0);
  legacy.mask_stack[0] = legacy_mask;
  EditorPreviewController reopened;
  load(reopened, QStringLiteral("legacy-accepted-coverage"), legacy, path);
  const auto reopened_patch = reopened.currentAdjustmentPatchJson();
  require(settle([&] { return !reopened.processing(); }), "Legacy accepted repair must reopen");
  require(reopened.currentAdjustmentPatchJson() == reopened_patch &&
      reopened.maskStack()[0].toMap().value(QStringLiteral("selectionDespeckle")).toDouble() == 2.0 &&
      reopened.maskStack()[0].toMap().value(QStringLiteral("selectionSmooth")).toDouble() == 7.0 &&
      render(recipe(reopened)) == render(legacy),
      "Opening an existing accepted repair must retain its historical refinements and pixels without migration");
}

void rapid_stroke_and_lock_workflow() {
  QTemporaryDir fixture;
  require(fixture.isValid(), "Rapid stroke fixture needs isolated storage");
  QImage source(320, 240, QImage::Format_RGB32);
  source.setColorSpace(QColorSpace(QColorSpace::SRgb));
  source.fill(QColor(QStringLiteral("#667788")));
  const auto path = fixture.filePath(QStringLiteral("photo.png"));
  require(source.save(path), "Rapid stroke source must save");
  EditorPreviewController controller;
  load(controller, QStringLiteral("rapid-selection"), {}, path);
  require(settle([&] { return !controller.processing(); }), "Rapid stroke source must load");
  controller.addMask(QStringLiteral("radial"));
  static_cast<void>(controller.ensureActiveSelectionAdjustmentLayer());
  const auto original = controller.maskStack();
  require(controller.setPhotoLayerLocked(0, true), "Selection owner should lock");
  const auto locked = controller.maskStack();
  controller.startSelectionBrush(false);
  controller.setActiveMaskExposure(2.0);
  controller.startSelectionEdgeBrush(false);
  require(controller.maskStack() == locked && !controller.activeMaskEditDisabledReason().isEmpty(),
      "A locked owner must reject brush append, refinement and mask-local adjustment changes");
  require(controller.setPhotoLayerLocked(0, false), "Selection owner should unlock");
  require(controller.beginSelectionEditSession(), "Rapid strokes need a staged session");
  controller.startSelectionBrush(false);
  const auto empty_brush = controller.maskStack();
  QVariantList after_first;
  const auto capture = QObject::connect(&controller, &EditorPreviewController::maskStateChanged, [&] {
    if (controller.hasActiveMask() && controller.maskStack()[controller.activeMaskIndex()].toMap()
        .value(QStringLiteral("brushStrokes")).toList().size() == 1)
      after_first = controller.maskStack();
  });
  const auto paint = [&](double x) {
    controller.beginActiveBrushSourceStroke(QStringLiteral("add"));
    controller.addBrushPoint(x, 0.25);
    controller.addBrushPoint(x + 0.08, 0.4);
    controller.endActiveBrushStroke();
  };
  paint(0.2);
  paint(0.7); // Deliberately no event-loop wait between completed gestures.
  require(settle([&] {
    return !controller.processing() && controller.maskStack()[controller.activeMaskIndex()].toMap()
        .value(QStringLiteral("brushStrokes")).toList().size() == 2;
  }), "Two rapid released strokes must both commit independently");
  QObject::disconnect(capture);
  require(!after_first.isEmpty(), "First stroke must publish its own authoritative boundary");
  const auto both = controller.maskStack();
  controller.undo();
  require(controller.maskStack() == after_first, "One local Undo must remove only the second rapid stroke");
  controller.undo();
  if (controller.maskStack() != empty_brush) {
    const auto actual = controller.maskStack();
    for (int i = 0; i < actual.size() && i < empty_brush.size(); ++i) {
      const auto left = actual[i].toMap();
      const auto right = empty_brush[i].toMap();
      for (auto it = left.cbegin(); it != left.cend(); ++it) {
        if (it.value() == right.value(it.key())) continue;
        const auto describe = [](const QVariant& value) {
          if (value.metaType().id() == QMetaType::QByteArray) return QStringLiteral("bytes:%1").arg(value.toByteArray().size());
          if (value.metaType().id() == QMetaType::QVariantList) return QStringLiteral("items:%1").arg(value.toList().size());
          return value.toString();
        };
        std::cerr << "rapid undo difference mask=" << i << " key=" << it.key().toStdString()
                  << " actual=" << describe(it.value()).toStdString() << " expected=" << describe(right.value(it.key())).toStdString() << '\n';
      }
    }
  }
  require(controller.maskStack() == empty_brush, "Second local Undo must remove only the first rapid stroke");
  controller.redo();
  controller.redo();
  require(controller.maskStack() == both, "Local Redo must restore each exact released stroke");
  controller.beginActiveBrushSourceStroke(QStringLiteral("add"));
  controller.addBrushPoint(0.5, 0.7);
  controller.undo();
  require(!controller.brushStrokeActive() && controller.maskStack() == both,
      "Mid-stroke Undo must abort only the live gesture");
  require(controller.cancelSelectionEditSession() && controller.maskStack() == original,
      "Cancel after rapid stroke undo/redo must restore the complete session baseline");

  require(controller.beginSelectionEditSession(), "Queued/live abort fixture should start");
  controller.startSelectionBrush(false);
  paint(0.2);
  controller.beginActiveBrushSourceStroke(QStringLiteral("add"));
  controller.addBrushPoint(0.75, 0.75);
  controller.undo();
  static_cast<void>(controller.finishSelectionEditSession());
  require(settle([&] { return !controller.selectionEditSessionActive() && !controller.processing(); }),
      "Done must wait for an earlier released gesture after aborting the live one");
  require(controller.maskStack().last().toMap().value(QStringLiteral("brushStrokes")).toList().size() == 1,
      "Aborting a live gesture must not discard the earlier queued stroke");

  load(controller, QStringLiteral("queued-repair-cancel"), {}, path);
  require(settle([&] { return !controller.processing(); }), "Queued repair fixture must load");
  controller.startRemoveBrush();
  controller.beginActiveBrushSourceStroke(QStringLiteral("add"));
  controller.addBrushPoint(0.4, 0.4);
  controller.addBrushPoint(0.5, 0.5);
  controller.endActiveBrushStroke();
  controller.applyActiveRetouch();
  require(controller.inpaintProgressActive(), "Immediate Apply must own one visible pending request");
  controller.applyActiveRetouch(); // Coalesces; cannot queue a second inference.
  controller.cancelActiveRetouch();
  require(settle([&] { return !controller.processing(); }), "Cancelled queued repair must let raster work settle");
  const auto cancelled_region = controller.maskStack()[controller.activeMaskIndex()].toMap();
  require(!controller.inpaintProgressActive() && !cancelled_region.value(QStringLiteral("committed")).toBool() &&
      cancelled_region.value(QStringLiteral("inpaintPatchData")).toByteArray().isEmpty() &&
      !cancelled_region.value(QStringLiteral("segmentationData")).toByteArray().isEmpty(),
      "Cancel must stop deferred Apply without deleting the editable region or later applying a patch");

  const auto previous_model_dir = qgetenv("PHOTO_EDITOR_AI_MODEL_DIR");
  shutdown_ai_model_runtime();
  qputenv("PHOTO_EDITOR_AI_MODEL_DIR", fixture.filePath(QStringLiteral("missing-models")).toUtf8());
  const auto before_missing_model = controller.maskStack();
  controller.applyActiveRetouch();
  require(controller.maskStack() == before_missing_model && !controller.inpaintProgressActive() &&
      controller.status().contains(QStringLiteral("Settings")) && controller.maskActionRetryAvailable(),
      "Missing Remove model must preserve the region and expose Settings plus a repair-owned retry");
  shutdown_ai_model_runtime();
  if (previous_model_dir.isEmpty()) qunsetenv("PHOTO_EDITOR_AI_MODEL_DIR");
  else qputenv("PHOTO_EDITOR_AI_MODEL_DIR", previous_model_dir);

  load(controller, QStringLiteral("pending-selection-owner"), {}, path);
  require(settle([&] { return !controller.processing(); }), "Pending selection fixture must load");
  controller.addMask(QStringLiteral("linear"));
  static_cast<void>(controller.ensureActiveSelectionAdjustmentLayer());
  const auto selection_baseline = controller.maskStack();
  require(controller.beginSelectionEditSession(), "Pending selection should own a staged session");
  const auto unchanged_recipe = controller.currentAdjustmentPatchJson();
  const auto previous_fake_models = qgetenv("PHOTO_EDITOR_TEST_FAKE_AI_MODELS");
  qputenv("PHOTO_EDITOR_TEST_FAKE_AI_MODELS", "1");
  require(controller.pickMaskAt(0.4, 0.4, QStringLiteral("selection"), true) == QStringLiteral("pending"),
      "Ownership regression must queue the actual asynchronous selection command (deterministic provider stub)");
  require(controller.currentAdjustmentPatchJson() == unchanged_recipe && controller.selectionEditSessionDirty(),
      "Owned authoritative work must count as dirty before it changes recipe bytes");
  load(controller, QStringLiteral("must-not-retarget"), {}, path);
  require(controller.assetId() == QStringLiteral("pending-selection-owner") && !controller.deletePhotoLayer(0) &&
      !controller.finishSelectionEditSession(), "Pending authoritative selection must block source/deletion and keep Done waiting");
  require(controller.cancelSelectionEditSession() && controller.maskStack() == selection_baseline,
      "Cancel must restore the selection baseline while its authoritative worker is still pending");
  require(controller.beginSelectionEditSession() && !controller.selectionEditSessionDirty() && controller.finishSelectionEditSession(),
      "A canceled prior-generation watcher must not make a new untouched session dirty or block it");
  load(controller, QStringLiteral("after-cancel-source"), {}, path);
  require(settle([&] { return !controller.processing(); }) && controller.assetId() == QStringLiteral("after-cancel-source") && controller.maskCount() == 0,
      "Late canceled selection work must not publish into a new source");
  if (previous_fake_models.isEmpty()) qunsetenv("PHOTO_EDITOR_TEST_FAKE_AI_MODELS");
  else qputenv("PHOTO_EDITOR_TEST_FAKE_AI_MODELS", previous_fake_models);
}

void agent_session_ownership() {
  EditorPreviewController controller;
  load(controller, QStringLiteral("agent-session-owner"));
  require(controller.beginAgentTask(QStringLiteral("session-ownership-task")).value(QStringLiteral("ok")).toBool(),
      "Agent task must capture a baseline before the human session starts");
  require(controller.beginSelectionEditSession() && !controller.selectionEditSessionDirty(),
      "Human authoring can own an untouched draft after the agent baseline was captured");
  const auto before = controller.currentAdjustmentPatchJson();
  bool invoked = false;
  const auto result = controller.runAgentTaskNativeCommand(QStringLiteral("session-ownership-task"),
      QStringLiteral("test_mutation"), {}, [&] {
        invoked = true;
        controller.addMask(QStringLiteral("linear"));
        static_cast<void>(controller.ensureActiveSelectionAdjustmentLayer());
        return QVariantMap{{QStringLiteral("ok"), true}};
      });
  require(!invoked && !result.value(QStringLiteral("ok")).toBool() &&
      result.value(QStringLiteral("mutationNotApplied")).toBool() && result.value(QStringLiteral("recoverable")).toBool() &&
      controller.currentAdjustmentPatchJson() == before,
      "A queued agent command cannot steal an untouched human session merely because recipe bytes still match");
  require(controller.cancelSelectionEditSession(), "Human session must remain cancellable after rejected agent mutation");
}
}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  QTemporaryDir runtime;
  require(runtime.isValid(), "Workflow test needs an isolated runtime directory");
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", runtime.path().toUtf8());
  qputenv("PHOTO_EDITOR_CACHE_DIR", (runtime.path() + QStringLiteral("/cache")).toUtf8());
  qputenv("PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE", "1");
  try {
    deletion_workflow();
    session_workflow();
    repair_workflow();
    baked_repair_coverage_workflow();
    rapid_stroke_and_lock_workflow();
    agent_session_ownership();
    std::cout << "Exclusive/shared/legacy mask deletion, session undo, same-owner brush and repair isolation passed\n";
    return EXIT_SUCCESS;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return EXIT_FAILURE;
  }
}
