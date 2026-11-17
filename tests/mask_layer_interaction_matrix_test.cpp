#include "canonical_composite_mask_alpha.h"
#include "edit_adjustment_patch.h"
#include "editor_preview_controller.h"
#include "mask_workflow_ownership.h"
#include "photo_render_utils.h"

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

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace photo_editor::desktop;

namespace photo_editor::desktop {
struct EditorPreviewControllerTestAccess {
  static void setGeneratedPreview(EditorPreviewController& controller,
                                  const QByteArray& alpha, int width, int height) {
    controller.smart_select_preview_mask_ = {
        {QStringLiteral("segmentationData"), alpha},
        {QStringLiteral("segmentationWidth"), width},
        {QStringLiteral("segmentationHeight"), height}};
  }
};
}  // namespace photo_editor::desktop

namespace {
void require(bool condition, const QString& message) {
  if (!condition) throw std::runtime_error(message.toStdString());
}

void settle(EditorPreviewController& editor) {
  QElapsedTimer timer;
  timer.start();
  do {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    QThread::msleep(1);
  } while ((editor.processing() || editor.selectionWorkBusy() || editor.brushStrokeActive() ||
            editor.photoPaintBusy()) && timer.elapsed() < 20000);
  require(!editor.processing() && !editor.selectionWorkBusy() && !editor.brushStrokeActive() &&
              !editor.photoPaintBusy(),
          QStringLiteral("Controller work must settle before the next matrix action"));
  require(editor.photoPaintError().isEmpty(), editor.photoPaintError());
}

EditState recipe(const EditorPreviewController& editor) {
  const auto parsed = parse_adjustment_patch_json(editor.currentAssetBoundAdjustmentPatchJson());
  require(parsed.ok(), QStringLiteral("Current photo recipe must decode"));
  return *parsed.state;
}

QVariantMap layer_by_id(const EditorPreviewController& editor, const QString& id) {
  for (const auto& value : recipe(editor).layers)
    if (value.toMap().value(QStringLiteral("id")).toString() == id) return value.toMap();
  return {};
}

int layer_index(const EditorPreviewController& editor, const QString& id) {
  const auto layers = recipe(editor).layers;
  for (int index = 0; index < layers.size(); ++index)
    if (layers.at(index).toMap().value(QStringLiteral("id")).toString() == id) return index;
  return -1;
}

QVariantList masks_for_set(const QVariantList& stack, const QString& id) {
  QVariantList result;
  for (const auto& value : stack)
    if (value.toMap().value(QStringLiteral("maskSetId")).toString() == id) result.append(value);
  return result;
}

QByteArray coverage(EditorPreviewController& editor, const QImage& source, const QString& id) {
  const auto result = evaluate_canonical_composite_mask_alpha(
      source, masks_for_set(editor.maskStack(), id), 1.0, {1.0, 1.0, 1.0});
  require(result.valid, QStringLiteral("Mask set %1 must evaluate").arg(id));
  return result.alpha_mask;
}

QImage render(EditorPreviewController& editor, const QImage& source) {
  const auto image = render_output_image_with_backend(
      source, recipe(editor), QColorSpace(QColorSpace::SRgb), QImage::Format_ARGB32,
      RenderBackendPreference::CpuOnly);
  require(!image.isNull() && image.size() == source.size(),
          QStringLiteral("Layer recipe must render at source dimensions"));
  return image;
}

qsizetype count_covered(const QByteArray& alpha) {
  qsizetype count = 0;
  for (const char byte : alpha)
    if (static_cast<unsigned char>(byte) != 0) ++count;
  return count;
}

qsizetype count_changed_inside(const QImage& before, const QImage& after,
                               const QByteArray& alpha) {
  require(before.size() == after.size() && alpha.size() == before.width() * before.height(),
          QStringLiteral("Rendered images and coverage dimensions must match"));
  qsizetype changed = 0;
  for (int y = 0; y < before.height(); ++y)
    for (int x = 0; x < before.width(); ++x)
      if (static_cast<unsigned char>(alpha.at(y * before.width() + x)) != 0 &&
          before.pixel(x, y) != after.pixel(x, y)) ++changed;
  return changed;
}

qsizetype count_changed_outside(const QImage& before, const QImage& after,
                                const QByteArray& alpha, bool require_unchanged) {
  require(before.size() == after.size() && alpha.size() == before.width() * before.height(),
          QStringLiteral("Rendered images and coverage dimensions must match"));
  qsizetype changed = 0;
  for (int y = 0; y < before.height(); ++y) {
    for (int x = 0; x < before.width(); ++x) {
      if (static_cast<unsigned char>(alpha.at(y * before.width() + x)) != 0) continue;
      if (before.pixel(x, y) != after.pixel(x, y)) ++changed;
    }
  }
  if (require_unchanged)
    require(changed == 0, QStringLiteral("A layer edit changed %1 pixels outside mask coverage").arg(changed));
  return changed;
}

void require_same_selection_algebra(const QByteArray& actual, const QByteArray& left,
                                    const QByteArray& right, const QString& operation) {
  require(actual.size() == left.size() && right.size() == left.size(),
          QStringLiteral("Selection operation dimensions must match"));
  for (qsizetype index = 0; index < actual.size(); ++index) {
    const int a = static_cast<unsigned char>(left.at(index));
    const int b = static_cast<unsigned char>(right.at(index));
    const int expected = operation == QStringLiteral("add")
        ? qRound(a + b - a * b / 255.0)
        : operation == QStringLiteral("subtract")
            ? std::max(0, a - b)
            : qRound(a * b / 255.0);
    require(std::abs(static_cast<int>(static_cast<unsigned char>(actual.at(index))) - expected) <= 1,
            QStringLiteral("%1 selection coverage differs at pixel %2").arg(operation).arg(index));
  }
}

void create_composed_selection(EditorPreviewController& editor, const QImage& source,
                               const QString& source_path) {
  editor.addMask(QStringLiteral("rectangle"));
  editor.setActiveMaskFeather(0.0);
  editor.setActiveMaskCenterX(0.29);
  editor.setActiveMaskCenterY(0.5);
  editor.setActiveMaskRadiusX(0.24);
  editor.setActiveMaskRadiusY(0.39);
  settle(editor);
  const auto rectangle_set = editor.workingSelectionSetId();
  require(!rectangle_set.isEmpty(), QStringLiteral("Rectangle must create a working selection"));

  editor.addMask(QStringLiteral("brush"));
  editor.setActiveMaskOperation(QStringLiteral("add"));
  editor.setActiveMaskBrushSize(0.19);
  editor.setActiveMaskBrushHardness(1.0);
  editor.setActiveMaskFeather(0.0);
  editor.beginActiveBrushSourceStroke(QStringLiteral("add"));
  editor.addBrushPoint(0.79, 0.79);
  editor.endActiveBrushStroke();
  settle(editor);
  require(editor.workingSelectionSetId() == rectangle_set,
          QStringLiteral("Brush component must preserve the working selection identity"));

  editor.addMask(QStringLiteral("object"));
  editor.setActiveMaskOperation(QStringLiteral("add"));
  QByteArray generated(source.width() * source.height(), char(0));
  for (int y = 10; y < 31; ++y)
    for (int x = source.width() - 37; x < source.width() - 12; ++x)
      generated[y * source.width() + x] = char(255);
  EditorPreviewControllerTestAccess::setGeneratedPreview(editor, generated,
      source.width(), source.height());
  require(editor.acceptSmartSelectPreview(QStringLiteral("selection-ai"), false) ==
              QStringLiteral("selection-ai"),
          QStringLiteral("Generated segmentation must commit through the production preview acceptance path"));
  settle(editor);
  const auto components = editor.workingSelectionMasks();
  require(components.size() == 3 && editor.workingSelectionSetId() == rectangle_set,
          QStringLiteral("Shape, brush and generated components must share one working selection"));
  require(components.at(0).toMap().value(QStringLiteral("kind")).toString() == QStringLiteral("rectangle") &&
              components.at(1).toMap().value(QStringLiteral("kind")).toString() == QStringLiteral("brush") &&
              components.at(2).toMap().value(QStringLiteral("generatedMaskMetadata")).toMap().size() > 0,
          QStringLiteral("The composed selection must retain all three distinct source types"));
  const auto composed = coverage(editor, source, rectangle_set);
  require(count_covered(composed) > source.width() * source.height() / 5,
          QStringLiteral("Composed shape, brush and generated selection must have real coverage"));

  require(editor.saveWorkingSelection(QStringLiteral("Generated matrix seed")),
          QStringLiteral("Save generated selection snapshot"));
  const auto saved = editor.savedSelections();
  require(!saved.isEmpty(), QStringLiteral("Saved generated selection must be discoverable"));
  const auto source_set = saved.constLast().toMap().value(QStringLiteral("id")).toString();

  // Run algebra against a separate real controller so each target-kind case
  // keeps the original three-source working selection live and editable.
  EditorPreviewController algebra;
  algebra.loadAsset(QStringLiteral("mask-layer-matrix-algebra"), source_path, {}, {}, {}, {},
                    serialize_edit_recipe_json(recipe(editor)));
  settle(algebra);
  require(algebra.workingSelectionSetId() == rectangle_set,
          QStringLiteral("Reload must preserve the composed working selection identity"));
  require(algebra.deselectWorkingSelection(), QStringLiteral("Clear algebra controller selection"));
  algebra.addMask(QStringLiteral("ellipse"));
  algebra.setActiveMaskFeather(0.0);
  algebra.setActiveMaskCenterX(0.56);
  algebra.setActiveMaskCenterY(0.50);
  algebra.setActiveMaskRadiusX(0.36);
  algebra.setActiveMaskRadiusY(0.43);
  settle(algebra);
  const auto algebra_set = algebra.workingSelectionSetId();
  require(!algebra_set.isEmpty() && algebra_set != source_set,
          QStringLiteral("Second selection must have an identity independent from the saved generated mask"));
  const auto baseline = coverage(algebra, source, algebra_set);
  const auto incoming = coverage(algebra, source, source_set);
  for (const auto& operation : {QStringLiteral("add"), QStringLiteral("subtract"), QStringLiteral("intersect")}) {
    require(algebra.loadMaskAsSelection(source_set, operation),
            QStringLiteral("Apply %1 to working selection").arg(operation));
    settle(algebra);
    require(algebra.workingSelectionSetId() == algebra_set,
            QStringLiteral("%1 must preserve the working selection identity").arg(operation));
    require_same_selection_algebra(coverage(algebra, source, algebra_set), baseline, incoming, operation);
    algebra.undo();
    settle(algebra);
    require(coverage(algebra, source, algebra_set) == baseline,
            QStringLiteral("Undo must restore selection before %1").arg(operation));
  }
  const auto before_invert = coverage(algebra, source, algebra_set);
  require(algebra.invertActiveSelection(), QStringLiteral("Invert algebra working selection"));
  settle(algebra);
  const auto inverted = coverage(algebra, source, algebra_set);
  for (qsizetype index = 0; index < before_invert.size(); ++index)
    require(std::abs(static_cast<int>(static_cast<unsigned char>(before_invert.at(index))) +
                     static_cast<int>(static_cast<unsigned char>(inverted.at(index))) - 255) <= 1,
            QStringLiteral("Invert selection must produce exact complement"));
  algebra.undo();
  settle(algebra);
  require(coverage(algebra, source, algebra_set) == before_invert,
          QStringLiteral("Undo must restore algebra selection after inversion"));
  require(algebra.deselectWorkingSelection() && !algebra.hasWorkingSelection(),
          QStringLiteral("Deselect algebra working selection"));
  require(algebra.reselectWorkingSelection() && algebra.workingSelectionSetId() == algebra_set,
          QStringLiteral("Reselect must preserve algebra working selection identity"));
  settle(algebra);
  require(coverage(algebra, source, algebra_set) == before_invert,
          QStringLiteral("Reselect must restore algebra coverage"));
  require(editor.workingSelectionSetId() == rectangle_set &&
              editor.workingSelectionMasks().size() == 3 &&
              coverage(editor, source, rectangle_set) == composed,
          QStringLiteral("Selection algebra must leave original composed working selection intact"));
}

enum class TargetKind { Adjustment, Pixel, Source, Fill, Text, Shape, Group };

QString target_name(TargetKind kind) {
  switch (kind) {
    case TargetKind::Adjustment: return QStringLiteral("adjustment");
    case TargetKind::Pixel: return QStringLiteral("pixel");
    case TargetKind::Source: return QStringLiteral("source");
    case TargetKind::Fill: return QStringLiteral("fill");
    case TargetKind::Text: return QStringLiteral("text");
    case TargetKind::Shape: return QStringLiteral("shape");
    case TargetKind::Group: return QStringLiteral("group");
  }
  return QStringLiteral("unknown");
}

QString create_target(EditorPreviewController& editor, TargetKind kind,
                      const QString& source_path, int width, int height) {
  switch (kind) {
    case TargetKind::Adjustment:
      require(editor.addAdjustmentLayer(QStringLiteral("Matrix Exposure")),
              QStringLiteral("Create adjustment target"));
      require(editor.setPhotoLayerAdjustmentValue(0, QStringLiteral("exposure"), 1.2),
              QStringLiteral("Give adjustment target visible content"));
      break;
    case TargetKind::Pixel: {
      require(editor.addPaintLayer(), QStringLiteral("Create pixel target"));
      require(editor.beginPhotoPaintStroke(0, {{QStringLiteral("color"), QStringLiteral("#e83e74")},
          {QStringLiteral("size"), 170.0}, {QStringLiteral("hardness"), 1.0},
          {QStringLiteral("opacity"), 1.0}, {QStringLiteral("flow"), 1.0},
          {QStringLiteral("pressureSize"), false}}), QStringLiteral("Begin pixel target paint"));
      editor.addPhotoPaintPoint(width * 0.5, height * 0.5);
      require(editor.finishPhotoPaintStroke(), QStringLiteral("Finish pixel target paint"));
      settle(editor);
      break;
    }
    case TargetKind::Source:
      require(editor.addSourceLayer(source_path, QStringLiteral("Matrix Source")),
              QStringLiteral("Create source target"));
      break;
    case TargetKind::Fill:
      require(editor.addFillLayer(QStringLiteral("#e83e74"), QStringLiteral("Matrix Fill")),
              QStringLiteral("Create fill target"));
      break;
    case TargetKind::Text:
      require(editor.addParagraphTextLayer(QStringLiteral("MASK"), 8, 12, 100, 70, width, height),
              QStringLiteral("Create text target"));
      break;
    case TargetKind::Shape:
      require(editor.addPhotoShape(QStringLiteral("rectangle"), 5, 5, width - 5,
              height - 5, width, height, QStringLiteral("#e83e74"), QString(), 0.0),
              QStringLiteral("Create shape target"));
      break;
    case TargetKind::Group: {
      require(editor.addFillLayer(QStringLiteral("#e83e74"), QStringLiteral("Grouped Fill")),
              QStringLiteral("Create group content"));
      const auto child_id = recipe(editor).layers.first().toMap().value(QStringLiteral("id")).toString();
      require(editor.addLayerGroup(QStringLiteral("Matrix Group")), QStringLiteral("Create group target"));
      const auto group_id = recipe(editor).layers.first().toMap().value(QStringLiteral("id")).toString();
      require(editor.movePhotoLayerIntoGroup(layer_index(editor, child_id), layer_index(editor, group_id)),
              QStringLiteral("Place visible fill under group target"));
      return group_id;
    }
  }
  const auto id = recipe(editor).layers.first().toMap().value(QStringLiteral("id")).toString();
  require(!id.isEmpty(), QStringLiteral("Created target must have a stable layer id"));
  return id;
}

QJsonObject run_target_case(const QImage& base, const QString& base_path,
                            const QString& target_source_path, TargetKind kind) {
  const auto name = target_name(kind);
  EditorPreviewController editor;
  editor.loadAsset(QStringLiteral("mask-layer-matrix-%1").arg(name), base_path, {}, {}, {}, {}, {});
  settle(editor);
  create_composed_selection(editor, base, base_path);
  const auto working_id = editor.workingSelectionSetId();
  const auto working_alpha = coverage(editor, base, working_id);
  const auto target_id = create_target(editor, kind, target_source_path, base.width(), base.height());
  const auto target_index_before_mask = layer_index(editor, target_id);
  require(target_index_before_mask >= 0, QStringLiteral("Locate %1 target before mask attach").arg(name));
  require(editor.applyWorkingSelectionToLayer(target_index_before_mask),
          QStringLiteral("Attach working selection to %1 target").arg(name));
  settle(editor);
  auto target = layer_by_id(editor, target_id);
  const auto attached_id = target.value(QStringLiteral("selectionMaskSetId")).toString();
  require(!attached_id.isEmpty() && attached_id != working_id &&
              editor.workingSelectionSetId() == working_id,
          QStringLiteral("%1 mask must have independent identity from the working selection").arg(name));
  require(coverage(editor, base, attached_id) == working_alpha,
          QStringLiteral("%1 attached mask must preserve composed source coverage").arg(name));

  // A non-destructive mask edit must leave every pixel outside the selected
  // region unchanged. This is checked against actual CPU composition, not a
  // model-only preview.
  const auto full_mask_render = render(editor, base);
  auto target_index = layer_index(editor, target_id);
  require(editor.setPhotoLayerMaskOpacity(target_index, 0.35),
          QStringLiteral("Reduce %1 mask opacity").arg(name));
  settle(editor);
  const auto reduced_mask_render = render(editor, base);
  const auto mask_opacity_inside = count_changed_inside(full_mask_render, reduced_mask_render, working_alpha);
  const auto mask_opacity_outside = count_changed_outside(full_mask_render, reduced_mask_render, working_alpha, true);
  require(mask_opacity_inside > 0,
          QStringLiteral("%1 mask opacity must change rendered pixels inside coverage").arg(name));
  require(editor.setPhotoLayerMaskOpacity(target_index, 1.0),
          QStringLiteral("Restore %1 mask opacity").arg(name));
  settle(editor);
  const auto opacity_restored = render(editor, base);
  require(opacity_restored == full_mask_render,
          QStringLiteral("Restoring %1 mask opacity must reproduce the original render").arg(name));

  require(editor.setPhotoLayerMaskDensity(target_index, 0.4),
          QStringLiteral("Reduce %1 mask density").arg(name));
  settle(editor);
  const auto reduced_density_render = render(editor, base);
  const auto density_inside = count_changed_inside(full_mask_render, reduced_density_render, working_alpha);
  const auto density_outside = count_changed_outside(full_mask_render, reduced_density_render, working_alpha, true);
  require(density_inside > 0,
          QStringLiteral("%1 mask density must change rendered pixels inside coverage").arg(name));
  require(editor.setPhotoLayerMaskDensity(target_index, 1.0),
          QStringLiteral("Restore %1 mask density").arg(name));
  settle(editor);

  const auto before_layer_opacity = render(editor, base);
  require(editor.setPhotoLayerOpacity(target_index, 0.42),
          QStringLiteral("Reduce %1 target opacity").arg(name));
  settle(editor);
  const auto after_layer_opacity = render(editor, base);
  const auto layer_opacity_inside = count_changed_inside(before_layer_opacity, after_layer_opacity, working_alpha);
  const auto layer_opacity_outside = count_changed_outside(before_layer_opacity, after_layer_opacity, working_alpha, true);
  require(layer_opacity_inside > 0,
          QStringLiteral("%1 target opacity must affect rendered content inside coverage").arg(name));
  require(editor.setPhotoLayerOpacity(target_index, 1.0),
          QStringLiteral("Restore %1 target opacity").arg(name));
  settle(editor);

  const auto before_bypass = render(editor, base);
  require(editor.setPhotoLayerMaskEnabled(target_index, false),
          QStringLiteral("Bypass %1 attached mask").arg(name));
  settle(editor);
  const auto bypass = render(editor, base);
  const auto bypass_changed = count_changed_inside(before_bypass, bypass, working_alpha);
  const auto bypass_outside = count_changed_outside(before_bypass, bypass, working_alpha, false);
  require(bypass_changed + bypass_outside > 0,
          QStringLiteral("Bypassing %1 mask must change rendered pixels").arg(name));
  require(editor.setPhotoLayerMaskEnabled(target_index, true),
          QStringLiteral("Restore %1 attached mask").arg(name));
  settle(editor);

  // Copy, then move both independent and original masks. Selection identity
  // and coverage remain stable across each ownership transition.
  require(editor.addFillLayer(QStringLiteral("#2259c8"), QStringLiteral("Mask Destination")),
          QStringLiteral("Create mask transfer destination"));
  const auto destination_id = recipe(editor).layers.first().toMap().value(QStringLiteral("id")).toString();
  target_index = layer_index(editor, target_id);
  require(editor.transferLayerMask(target_id, attached_id, destination_id, true),
          QStringLiteral("Copy %1 attached mask (Alt copy semantics)").arg(name));
  settle(editor);
  const auto copied_id = layer_by_id(editor, destination_id)
      .value(QStringLiteral("selectionMaskSetId")).toString();
  require(!copied_id.isEmpty() && copied_id != attached_id && copied_id != working_id &&
              coverage(editor, base, copied_id) == working_alpha &&
              editor.workingSelectionSetId() == working_id,
          QStringLiteral("Copied %1 mask must have distinct identity and equal coverage").arg(name));

  require(editor.transferLayerMask(destination_id, copied_id, target_id, false),
          QStringLiteral("Move copied %1 mask back to owner").arg(name));
  settle(editor);
  require(layer_by_id(editor, destination_id).value(QStringLiteral("selectionMaskSetId")).toString().isEmpty() &&
              layer_by_id(editor, target_id).value(QStringLiteral("selectionMaskSetIds")).toList().size() == 2,
          QStringLiteral("Move must detach source and attach mask atomically"));
  require(editor.transferLayerMask(target_id, attached_id, destination_id, false),
          QStringLiteral("Move original %1 mask to destination").arg(name));
  settle(editor);
  require(layer_by_id(editor, target_id).value(QStringLiteral("selectionMaskSetId")).toString() == copied_id &&
              layer_by_id(editor, destination_id).value(QStringLiteral("selectionMaskSetId")).toString() == attached_id,
          QStringLiteral("Move must preserve source and destination mask identities"));
  require(editor.transferLayerMask(destination_id, attached_id, target_id, false),
          QStringLiteral("Restore original %1 mask owner").arg(name));
  settle(editor);

  // A mask lock protects mask editing and ownership operations. A layer lock
  // blocks attachment and transfer; all failures must preserve the recipe and
  // history cursor exactly.
  target_index = layer_index(editor, target_id);
  require(editor.selectLayerMask(target_index, attached_id),
          QStringLiteral("Inspect attached %1 mask").arg(name));
  editor.setActiveMaskLocked(true);
  settle(editor);
  const auto locked_mask_recipe = editor.currentAssetBoundAdjustmentPatchJson();
  const auto locked_mask_history = editor.editHistoryCurrentIndex();
  require(!editor.invertLayerMask(target_index, attached_id) &&
              !editor.removeLayerMask(target_index, attached_id) &&
              !editor.transferLayerMask(target_id, attached_id, destination_id, false),
          QStringLiteral("Locked %1 mask must reject invert, remove and transfer").arg(name));
  require(editor.currentAssetBoundAdjustmentPatchJson() == locked_mask_recipe &&
              editor.editHistoryCurrentIndex() == locked_mask_history,
          QStringLiteral("Rejected locked-mask operations must preserve recipe and history"));
  editor.setActiveMaskLocked(false);
  editor.finishLayerMaskEditing();
  settle(editor);

  require(editor.setPhotoLayerLocked(target_index, true),
          QStringLiteral("Lock %1 target layer").arg(name));
  settle(editor);
  const auto locked_layer_recipe = editor.currentAssetBoundAdjustmentPatchJson();
  const auto locked_layer_history = editor.editHistoryCurrentIndex();
  require(!editor.applyWorkingSelectionToLayer(target_index) &&
              !editor.removeLayerMask(target_index, copied_id) &&
              !editor.transferLayerMask(target_id, copied_id, destination_id, true) &&
              !editor.transferLayerMask(destination_id, attached_id, target_id, false),
          QStringLiteral("Locked %1 owner must reject attachment, removal and source/destination transfer").arg(name));
  require(editor.currentAssetBoundAdjustmentPatchJson() == locked_layer_recipe &&
              editor.editHistoryCurrentIndex() == locked_layer_history,
          QStringLiteral("Rejected locked-layer operations must preserve recipe and history"));
  require(editor.setPhotoLayerLocked(target_index, false),
          QStringLiteral("Unlock %1 target layer").arg(name));
  settle(editor);

  // Successful removal is a single reversible ownership edit. The working
  // selection survives and the second attached mask remains independently
  // owned by the target.
  const auto before_remove = recipe(editor);
  require(editor.removeLayerMask(target_index, copied_id),
          QStringLiteral("Remove copied %1 attached mask").arg(name));
  settle(editor);
  require(layer_by_id(editor, target_id).value(QStringLiteral("selectionMaskSetId")).toString() == attached_id &&
              editor.workingSelectionSetId() == working_id,
          QStringLiteral("Removing one %1 mask must retain the other mask and working selection").arg(name));
  editor.undo();
  settle(editor);
  require(recipe(editor).layers == before_remove.layers && recipe(editor).mask_stack == before_remove.mask_stack,
          QStringLiteral("Undo must restore removed %1 mask identity and coverage").arg(name));
  editor.redo();
  settle(editor);
  require(layer_by_id(editor, target_id).value(QStringLiteral("selectionMaskSetIds")).toList().size() == 1,
          QStringLiteral("Redo must remove only the copied %1 mask").arg(name));
  editor.undo();
  settle(editor);

  // Selection inversion/deselect/reselect operate on the working selection,
  // not the independent attached mask. The saved copy remains immutable.
  const auto attached_before_selection_changes = coverage(editor, base, attached_id);
  const auto working_before_invert = coverage(editor, base, working_id);
  require(editor.invertActiveSelection(), QStringLiteral("Invert working selection for %1").arg(name));
  settle(editor);
  const auto inverted_working = coverage(editor, base, working_id);
  for (qsizetype index = 0; index < working_before_invert.size(); ++index)
    require(std::abs(static_cast<int>(static_cast<unsigned char>(working_before_invert.at(index))) +
                     static_cast<int>(static_cast<unsigned char>(inverted_working.at(index))) - 255) <= 1,
            QStringLiteral("Working selection inversion must be a complement"));
  require(coverage(editor, base, attached_id) == attached_before_selection_changes,
          QStringLiteral("Working selection inversion must preserve attached %1 mask").arg(name));
  editor.undo();
  settle(editor);
  require(coverage(editor, base, working_id) == working_before_invert,
          QStringLiteral("Undo must restore %1 working selection"));
  require(editor.deselectWorkingSelection() && !editor.hasWorkingSelection(),
          QStringLiteral("Deselect working selection for %1").arg(name));
  require(coverage(editor, base, attached_id) == attached_before_selection_changes,
          QStringLiteral("Deselecting must preserve attached %1 mask").arg(name));
  require(editor.reselectWorkingSelection() && editor.workingSelectionSetId() == working_id,
          QStringLiteral("Reselect same working selection for %1").arg(name));
  settle(editor);

  const auto final_state = recipe(editor);
  const auto final_render = render(editor, base);
  EditorPreviewController reopened;
  reopened.loadAsset(QStringLiteral("mask-layer-matrix-reopen-%1").arg(name), base_path,
                     {}, {}, {}, {}, serialize_edit_recipe_json(final_state));
  settle(reopened);
  const auto reopened_state = recipe(reopened);
  const auto reopened_render = render(reopened, base);
  require(reopened_state.layers == final_state.layers && reopened_state.mask_stack == final_state.mask_stack &&
              reopened.workingSelectionSetId() == working_id && reopened_render == final_render,
          QStringLiteral("%1 layer/mask ownership and rendered pixels must survive reopen").arg(name));

  if (kind == TargetKind::Adjustment) {
    const auto before_delete = recipe(editor);
    const auto before_delete_render = render(editor, base);
    target_index = layer_index(editor, target_id);
    require(editor.deletePhotoLayer(target_index), QStringLiteral("Delete masked adjustment target"));
    settle(editor);
    require(layer_index(editor, target_id) < 0,
            QStringLiteral("Deleting masked adjustment must remove its layer"));
    editor.undo();
    settle(editor);
    require(recipe(editor).layers == before_delete.layers && recipe(editor).mask_stack == before_delete.mask_stack &&
                render(editor, base) == before_delete_render,
            QStringLiteral("Undo must restore deleted layer, attached mask and rendered pixels"));
    editor.redo();
    settle(editor);
    require(layer_index(editor, target_id) < 0,
            QStringLiteral("Redo must remove the same masked adjustment layer"));
    editor.undo();
    settle(editor);
  }

  return QJsonObject{
      {QStringLiteral("case"), name},
      {QStringLiteral("workingSelectionId"), working_id},
      {QStringLiteral("attachedMaskId"), attached_id},
      {QStringLiteral("workingMaskPixels"), static_cast<qint64>(count_covered(working_alpha))},
      {QStringLiteral("attachedMaskPixels"), static_cast<qint64>(count_covered(coverage(editor, base, attached_id)))},
      {QStringLiteral("maskOpacityChangedInside"), static_cast<qint64>(mask_opacity_inside)},
      {QStringLiteral("maskOpacityChangedOutside"), static_cast<qint64>(mask_opacity_outside)},
      {QStringLiteral("maskDensityChangedInside"), static_cast<qint64>(density_inside)},
      {QStringLiteral("maskDensityChangedOutside"), static_cast<qint64>(density_outside)},
      {QStringLiteral("layerOpacityChangedInside"), static_cast<qint64>(layer_opacity_inside)},
      {QStringLiteral("layerOpacityChangedOutside"), static_cast<qint64>(layer_opacity_outside)},
      {QStringLiteral("maskBypassChangedInside"), static_cast<qint64>(bypass_changed)},
      {QStringLiteral("maskBypassChangedOutside"), static_cast<qint64>(bypass_outside)},
      {QStringLiteral("copyMoveAndLockChecks"), 12},
      {QStringLiteral("reopenedPixelsIdentical"), true},
      {QStringLiteral("deleteUndoRedo"), kind == TargetKind::Adjustment}};
}

QJsonObject run_ancestor_lock_case(const QImage& base, const QString& base_path) {
  EditorPreviewController editor;
  editor.loadAsset(QStringLiteral("mask-ancestor-lock-matrix"), base_path, {}, {}, {}, {}, {});
  settle(editor);
  create_composed_selection(editor, base, base_path);
  const auto working_id = editor.workingSelectionSetId();

  require(editor.addFillLayer(QStringLiteral("#e83e74"), QStringLiteral("Locked Parent Source")),
          QStringLiteral("Create child layer for source group"));
  const auto source_child = recipe(editor).layers.first().toMap().value(QStringLiteral("id")).toString();
  require(editor.addLayerGroup(QStringLiteral("Source Parent")), QStringLiteral("Create source ancestor group"));
  const auto source_group = recipe(editor).layers.first().toMap().value(QStringLiteral("id")).toString();
  require(editor.movePhotoLayerIntoGroup(layer_index(editor, source_child), layer_index(editor, source_group)),
          QStringLiteral("Parent source child"));

  require(editor.addFillLayer(QStringLiteral("#3266dc"), QStringLiteral("Locked Parent Destination")),
          QStringLiteral("Create child layer for destination group"));
  const auto destination_child = recipe(editor).layers.first().toMap().value(QStringLiteral("id")).toString();
  require(editor.addLayerGroup(QStringLiteral("Destination Parent")), QStringLiteral("Create destination ancestor group"));
  const auto destination_group = recipe(editor).layers.first().toMap().value(QStringLiteral("id")).toString();
  require(editor.movePhotoLayerIntoGroup(layer_index(editor, destination_child), layer_index(editor, destination_group)),
          QStringLiteral("Parent destination child"));

  require(editor.applyWorkingSelectionToLayer(layer_index(editor, source_child)),
          QStringLiteral("Attach mask to group child"));
  settle(editor);
  const auto mask_id = layer_by_id(editor, source_child).value(QStringLiteral("selectionMaskSetId")).toString();
  require(!mask_id.isEmpty(), QStringLiteral("Child must own the attached mask"));
  require(editor.setPhotoLayerLocked(layer_index(editor, source_group), true),
          QStringLiteral("Lock source ancestor group"));
  settle(editor);
  require(editor.selectLayerMask(layer_index(editor, source_child), mask_id),
          QStringLiteral("Locked ancestry must still permit mask inspection"));
  const auto locked_source_recipe = editor.currentAssetBoundAdjustmentPatchJson();
  const auto locked_source_history = editor.editHistoryCurrentIndex();
  require(!editor.deletePhotoLayer(layer_index(editor, source_child)) &&
              !editor.applyWorkingSelectionToLayer(layer_index(editor, source_child)) &&
              !editor.invertLayerMask(layer_index(editor, source_child), mask_id) &&
              !editor.removeLayerMask(layer_index(editor, source_child), mask_id) &&
              !editor.transferLayerMask(source_child, mask_id, destination_child, false),
          QStringLiteral("Locked source ancestry must reject delete, attach, invert, remove and move"));
  require(editor.currentAssetBoundAdjustmentPatchJson() == locked_source_recipe &&
              editor.editHistoryCurrentIndex() == locked_source_history,
          QStringLiteral("Rejected source-ancestor operations must preserve recipe and history"));

  require(editor.setPhotoLayerLocked(layer_index(editor, source_group), false),
          QStringLiteral("Unlock source ancestor group"));
  require(editor.setPhotoLayerLocked(layer_index(editor, destination_group), true),
          QStringLiteral("Lock destination ancestor group"));
  settle(editor);
  const auto locked_destination_recipe = editor.currentAssetBoundAdjustmentPatchJson();
  const auto locked_destination_history = editor.editHistoryCurrentIndex();
  require(!editor.transferLayerMask(source_child, mask_id, destination_child, false) &&
              !editor.transferLayerMask(source_child, mask_id, destination_child, true),
          QStringLiteral("Locked destination ancestry must reject move and copy"));
  require(editor.currentAssetBoundAdjustmentPatchJson() == locked_destination_recipe &&
              editor.editHistoryCurrentIndex() == locked_destination_history,
          QStringLiteral("Rejected destination-ancestor operations must preserve recipe and history"));

  require(editor.setPhotoLayerLocked(layer_index(editor, destination_group), false),
          QStringLiteral("Unlock destination ancestor group"));
  require(editor.transferLayerMask(source_child, mask_id, destination_child, false),
          QStringLiteral("Unlocked source and destination ancestry must permit move"));
  settle(editor);
  require(layer_by_id(editor, source_child).value(QStringLiteral("selectionMaskSetId")).toString().isEmpty() &&
              layer_by_id(editor, destination_child).value(QStringLiteral("selectionMaskSetId")).toString() == mask_id &&
              editor.workingSelectionSetId() == working_id,
          QStringLiteral("Unlocked move must transfer ownership while preserving working selection"));
  require(editor.transferLayerMask(destination_child, mask_id, source_child, true),
          QStringLiteral("Unlocked ancestors must permit Alt copy back to source child"));
  settle(editor);
  const auto copied_id = layer_by_id(editor, source_child).value(QStringLiteral("selectionMaskSetId")).toString();
  require(!copied_id.isEmpty() && copied_id != mask_id &&
              coverage(editor, base, copied_id) == coverage(editor, base, mask_id),
          QStringLiteral("Ancestor test Alt copy must retain equal coverage with independent identity"));

  editor.selectLayerMask(layer_index(editor, source_child), copied_id);
  editor.setActiveMaskLocked(true);
  settle(editor);
  const auto locked_mask_recipe = editor.currentAssetBoundAdjustmentPatchJson();
  const auto locked_mask_history = editor.editHistoryCurrentIndex();
  require(!editor.invertLayerMask(layer_index(editor, source_child), copied_id) &&
              !editor.removeLayerMask(layer_index(editor, source_child), copied_id) &&
              !editor.transferLayerMask(source_child, copied_id, destination_child, true),
          QStringLiteral("Locked mask source must reject invert, remove and transfer even in unlocked groups"));
  require(editor.currentAssetBoundAdjustmentPatchJson() == locked_mask_recipe &&
              editor.editHistoryCurrentIndex() == locked_mask_history,
          QStringLiteral("Rejected mask-lock operations must preserve recipe and history"));
  return QJsonObject{
      {QStringLiteral("case"), QStringLiteral("nested-ancestor-locks")},
      {QStringLiteral("sourceLayerId"), source_child},
      {QStringLiteral("destinationLayerId"), destination_child},
      {QStringLiteral("workingSelectionId"), working_id},
      {QStringLiteral("attachedMaskId"), mask_id},
      {QStringLiteral("ancestorLockFailuresPreservedState"), true},
      {QStringLiteral("lockedMaskFailuresPreservedState"), true},
      {QStringLiteral("readOnlyInspectAllowed"), true}};
}

QStringList attached_mask_ids(const EditorPreviewController& editor, const QString& layer_id) {
  QStringList result;
  for (const auto& value : layer_by_id(editor, layer_id)
                               .value(QStringLiteral("selectionMaskSetIds")).toList()) {
    const auto id = value.toString();
    if (!id.isEmpty()) result.push_back(id);
  }
  const auto legacy = layer_by_id(editor, layer_id)
      .value(QStringLiteral("selectionMaskSetId")).toString();
  if (!legacy.isEmpty() && !result.contains(legacy)) result.prepend(legacy);
  return result;
}

QJsonObject run_bypassed_multi_mask_case(const QImage& base, const QString& base_path) {
  EditorPreviewController editor;
  editor.loadAsset(QStringLiteral("mask-layer-matrix-bypassed-multi-mask"), base_path,
                   {}, {}, {}, {}, {});
  settle(editor);
  create_composed_selection(editor, base, base_path);
  const auto working_id = editor.workingSelectionSetId();

  require(editor.addFillLayer(QStringLiteral("#e74c53"), QStringLiteral("Multi Mask Source")),
          QStringLiteral("Create multi-mask source layer"));
  const auto source_id = recipe(editor).layers.first().toMap().value(QStringLiteral("id")).toString();
  const auto source_index = layer_index(editor, source_id);
  require(editor.applyWorkingSelectionToLayer(source_index) &&
              editor.applyWorkingSelectionToLayer(source_index),
          QStringLiteral("Attach two independent masks to source layer"));
  settle(editor);
  const auto source_masks = attached_mask_ids(editor, source_id);
  require(source_masks.size() == 2 && source_masks.at(0) != source_masks.at(1),
          QStringLiteral("Source layer must have two independently identified masks"));

  require(editor.addFillLayer(QStringLiteral("#344fc6"), QStringLiteral("Multi Mask Destination")),
          QStringLiteral("Create multi-mask destination layer"));
  const auto destination_id = recipe(editor).layers.first().toMap().value(QStringLiteral("id")).toString();
  require(editor.transferLayerMask(QString(), working_id, destination_id, true),
          QStringLiteral("Copy free working selection as destination's first attached mask"));
  settle(editor);
  const auto destination_first = attached_mask_ids(editor, destination_id).constLast();
  require(layer_by_id(editor, destination_id).value(QStringLiteral("maskEnabled")).toBool(),
          QStringLiteral("First attached mask defaults to enabled"));

  require(editor.setPhotoLayerMaskEnabled(layer_index(editor, source_id), false) &&
              editor.setPhotoLayerMaskEnabled(layer_index(editor, destination_id), false),
          QStringLiteral("Bypass both multi-mask owners"));
  settle(editor);
  require(!layer_by_id(editor, source_id).value(QStringLiteral("maskEnabled")).toBool() &&
              !layer_by_id(editor, destination_id).value(QStringLiteral("maskEnabled")).toBool(),
          QStringLiteral("Both owners must retain explicit bypass state"));

  require(editor.transferLayerMask(source_id, source_masks.at(0), destination_id, true),
          QStringLiteral("Copy into an already bypassed destination with one mask"));
  settle(editor);
  auto destination_masks = attached_mask_ids(editor, destination_id);
  require(destination_masks.size() == 2 && destination_masks.constFirst() == destination_first &&
              layer_by_id(editor, destination_id).value(QStringLiteral("maskEnabled")).toBool() == false &&
              layer_by_id(editor, source_id).value(QStringLiteral("maskEnabled")).toBool() == false,
          QStringLiteral("Copy must preserve bypass on both existing source and destination masks"));
  const auto copied_id = destination_masks.constLast();

  const auto before_remove = recipe(editor);
  require(editor.removeLayerMask(layer_index(editor, destination_id), copied_id),
          QStringLiteral("Remove one mask from bypassed destination"));
  settle(editor);
  require(attached_mask_ids(editor, destination_id) == QStringList{destination_first} &&
              !layer_by_id(editor, destination_id).value(QStringLiteral("maskEnabled")).toBool(),
          QStringLiteral("Removing one mask must not re-enable the remaining bypassed mask"));
  editor.undo();
  settle(editor);
  require(recipe(editor).layers == before_remove.layers && recipe(editor).mask_stack == before_remove.mask_stack &&
              !layer_by_id(editor, destination_id).value(QStringLiteral("maskEnabled")).toBool(),
          QStringLiteral("Undo remove must restore mask list while preserving bypass"));
  editor.redo();
  settle(editor);
  require(attached_mask_ids(editor, destination_id) == QStringList{destination_first} &&
              !layer_by_id(editor, destination_id).value(QStringLiteral("maskEnabled")).toBool(),
          QStringLiteral("Redo remove must retain destination bypass"));
  editor.undo();
  settle(editor);

  require(editor.transferLayerMask(source_id, source_masks.at(1), destination_id, false),
          QStringLiteral("Move one mask off a bypassed multi-mask source"));
  settle(editor);
  require(attached_mask_ids(editor, source_id) == QStringList{source_masks.at(0)} &&
              !layer_by_id(editor, source_id).value(QStringLiteral("maskEnabled")).toBool() &&
              !layer_by_id(editor, destination_id).value(QStringLiteral("maskEnabled")).toBool(),
          QStringLiteral("Moving one mask must preserve bypass on source and destination owners"));
  require(editor.transferLayerMask(destination_id, source_masks.at(1), source_id, false),
          QStringLiteral("Move mask back to bypassed source"));
  settle(editor);
  require(attached_mask_ids(editor, source_id).size() == 2 &&
              !layer_by_id(editor, source_id).value(QStringLiteral("maskEnabled")).toBool() &&
              !layer_by_id(editor, destination_id).value(QStringLiteral("maskEnabled")).toBool() &&
              editor.workingSelectionSetId() == working_id,
          QStringLiteral("Move back must preserve both bypass flags and working selection identity"));
  return QJsonObject{
      {QStringLiteral("case"), QStringLiteral("bypassed-multi-mask-remove-move-copy")},
      {QStringLiteral("sourceLayerId"), source_id},
      {QStringLiteral("destinationLayerId"), destination_id},
      {QStringLiteral("sourceMaskCount"), source_masks.size()},
      {QStringLiteral("destinationFirstMaskId"), destination_first},
      {QStringLiteral("workingSelectionId"), working_id},
      {QStringLiteral("firstAttachEnabledByDefault"), true},
      {QStringLiteral("removeMoveCopyPreservedBypass"), true}};
}

QJsonObject run_direct_mask_bypass_attach_case(const QImage& base,
                                               const QString& base_path) {
  EditorPreviewController editor;
  editor.loadAsset(QStringLiteral("mask-layer-matrix-direct-mask-bypass-attach"),
                   base_path, {}, {}, {}, {}, {});
  settle(editor);
  create_composed_selection(editor, base, base_path);
  const auto working_id = editor.workingSelectionSetId();

  require(editor.addFillLayer(QStringLiteral("#e74c53"),
                              QStringLiteral("Legacy Raster Mask Owner")),
          QStringLiteral("Create a layer for a legacy raster mask."));
  const auto layer_id = recipe(editor).layers.first().toMap()
                            .value(QStringLiteral("id")).toString();
  auto index = layer_index(editor, layer_id);
  require(index >= 0 && editor.setPhotoLayerMaskEnabled(index, true),
          QStringLiteral("Create the direct legacy raster mask."));
  settle(editor);
  require(editor.setPhotoLayerMaskEnabled(layer_index(editor, layer_id), false),
          QStringLiteral("Explicitly bypass the direct legacy raster mask."));
  settle(editor);

  const auto before_recipe = editor.currentAssetBoundAdjustmentPatchJson();
  const auto before_layer = layer_by_id(editor, layer_id);
  const auto direct_mask_data = before_layer.value(QStringLiteral("maskData")).toByteArray();
  const auto before_render = render(editor, base);
  require(!direct_mask_data.isEmpty() &&
              before_layer.value(QStringLiteral("maskWidth")).toInt() == 1 &&
              before_layer.value(QStringLiteral("maskHeight")).toInt() == 1 &&
              !before_layer.value(QStringLiteral("maskEnabled")).toBool(),
          QStringLiteral("The fixture must begin with an explicitly bypassed direct raster mask."));

  require(editor.applyWorkingSelectionToLayer(layer_index(editor, layer_id)),
          QStringLiteral("Attach the first selection mask to the legacy mask owner."));
  settle(editor);
  const auto attached_layer = layer_by_id(editor, layer_id);
  require(!attached_layer.value(QStringLiteral("selectionMaskSetId")).toString().isEmpty() &&
              attached_layer.value(QStringLiteral("maskData")).toByteArray() == direct_mask_data &&
              !attached_layer.value(QStringLiteral("maskEnabled")).toBool(),
          QStringLiteral("First selection attachment must preserve the direct mask and its bypass."));
  require(render(editor, base) == before_render,
          QStringLiteral("Attaching a disabled selection must not change direct-mask bypass output."));
  const auto attached_recipe = editor.currentAssetBoundAdjustmentPatchJson();

  editor.undo();
  settle(editor);
  const auto undone_layer = layer_by_id(editor, layer_id);
  require(editor.currentAssetBoundAdjustmentPatchJson() == before_recipe &&
              undone_layer.value(QStringLiteral("maskData")).toByteArray() == direct_mask_data &&
              !undone_layer.value(QStringLiteral("maskEnabled")).toBool() &&
              render(editor, base) == before_render,
          QStringLiteral("Undo must restore the directly bypassed mask without changing pixels."));
  editor.redo();
  settle(editor);
  const auto redone_layer = layer_by_id(editor, layer_id);
  require(editor.currentAssetBoundAdjustmentPatchJson() == attached_recipe &&
              !redone_layer.value(QStringLiteral("selectionMaskSetId")).toString().isEmpty() &&
              redone_layer.value(QStringLiteral("maskData")).toByteArray() == direct_mask_data &&
              !redone_layer.value(QStringLiteral("maskEnabled")).toBool() &&
              render(editor, base) == before_render,
          QStringLiteral("Redo must restore the first attached mask and preserve bypass pixels."));

  return QJsonObject{
      {QStringLiteral("case"), QStringLiteral("direct-mask-bypass-first-selection-attachment")},
      {QStringLiteral("layerId"), layer_id},
      {QStringLiteral("workingSelectionId"), working_id},
      {QStringLiteral("directMaskBytesPreserved"), true},
      {QStringLiteral("firstAttachPreservedBypass"), true},
      {QStringLiteral("pixelIdentityAcrossUndoRedo"), true}};
}

QJsonObject run_empty_selection_and_redo_case(const QImage& base, const QString& base_path) {
  EditorPreviewController editor;
  editor.loadAsset(QStringLiteral("mask-empty-selection-and-redo"), base_path, {}, {}, {}, {}, {});
  settle(editor);
  const auto developed_base = render(editor, base);
  require(editor.addFillLayer(QStringLiteral("#ed432a"), QStringLiteral("Empty Selection Target")),
          QStringLiteral("Create target for active-empty selection"));
  const auto target_id = recipe(editor).layers.first().toMap().value(QStringLiteral("id")).toString();
  const auto unmasked = render(editor, base);

  editor.addMask(QStringLiteral("brush"));
  settle(editor);
  const auto empty_set = editor.workingSelectionSetId();
  require(!empty_set.isEmpty() && editor.hasWorkingSelection(),
          QStringLiteral("An authored empty brush is still an active working selection"));
  const auto empty_alpha = coverage(editor, base, empty_set);
  require(count_covered(empty_alpha) == 0,
          QStringLiteral("Empty authored brush must evaluate to zero selected pixels"));
  require(editor.applyWorkingSelectionToLayer(layer_index(editor, target_id)),
          QStringLiteral("Attach empty authored selection as a layer mask"));
  settle(editor);
  const auto attached_id = layer_by_id(editor, target_id).value(QStringLiteral("selectionMaskSetId")).toString();
  require(!attached_id.isEmpty() && attached_id != empty_set &&
              count_covered(coverage(editor, base, attached_id)) == 0,
          QStringLiteral("Active-empty attachment must remain distinct and empty"));
  const auto hidden = render(editor, base);
  require(hidden == developed_base && hidden != unmasked,
          QStringLiteral("An active-empty selection must mask the target completely"));

  require(editor.deselectWorkingSelection() && !editor.hasWorkingSelection() &&
              editor.workingSelectionSetId().isEmpty(),
          QStringLiteral("Deselect must represent no working selection, distinct from active-empty"));
  settle(editor);
  require(render(editor, base) == hidden &&
              layer_by_id(editor, target_id).value(QStringLiteral("selectionMaskSetId")).toString() == attached_id,
          QStringLiteral("Deselect must leave the empty attached layer mask unchanged"));
  require(editor.reselectWorkingSelection() && editor.workingSelectionSetId() == empty_set &&
              count_covered(coverage(editor, base, empty_set)) == 0,
          QStringLiteral("Reselect must restore the same active-empty working selection"));
  settle(editor);

  require(editor.removeLayerMask(layer_index(editor, target_id), attached_id),
          QStringLiteral("Remove active-empty mask"));
  settle(editor);
  require(render(editor, base) == unmasked,
          QStringLiteral("Removing an empty attached mask must restore unmasked target pixels"));
  editor.undo();
  settle(editor);
  require(render(editor, base) == hidden &&
              layer_by_id(editor, target_id).value(QStringLiteral("selectionMaskSetId")).toString() == attached_id,
          QStringLiteral("Undo must restore the empty attached mask"));
  editor.redo();
  settle(editor);
  require(render(editor, base) == unmasked,
          QStringLiteral("Redo must remove the empty attached mask again"));

  const auto target_index = layer_index(editor, target_id);
  require(editor.setPhotoLayerOpacity(target_index, 0.63),
          QStringLiteral("Create a redoable layer change"));
  settle(editor);
  editor.undo();
  settle(editor);
  const auto before_rejected = editor.currentAssetBoundAdjustmentPatchJson();
  const auto before_rejected_history = editor.editHistoryCurrentIndex();
  require(!editor.applyWorkingSelectionToLayer(-1) &&
              !editor.removeLayerMask(999, attached_id) &&
              !editor.transferLayerMask(QStringLiteral("missing-source"), attached_id,
                                       target_id, false),
          QStringLiteral("Invalid-target failures must reject without consuming redo"));
  require(editor.currentAssetBoundAdjustmentPatchJson() == before_rejected &&
              editor.editHistoryCurrentIndex() == before_rejected_history,
          QStringLiteral("Failed no-ops must leave recipe and history cursor unchanged"));
  editor.redo();
  settle(editor);
  require(std::abs(layer_by_id(editor, target_id).value(QStringLiteral("opacity")).toDouble() - 0.63) < 0.001,
          QStringLiteral("Rejected no-op calls must preserve the pending redo entry"));

  require(editor.deselectWorkingSelection() && !editor.hasWorkingSelection(),
          QStringLiteral("Clear active-empty selection before testing unselected paint scope"));
  require(editor.addPaintLayer(), QStringLiteral("Create paint layer with no working selection"));
  const auto paint_id = recipe(editor).layers.first().toMap().value(QStringLiteral("id")).toString();
  const auto before_unselected_paint = render(editor, base);
  require(editor.beginPhotoPaintStroke(layer_index(editor, paint_id), {
              {QStringLiteral("color"), QStringLiteral("#35dcfa")},
              {QStringLiteral("size"), 18.0}, {QStringLiteral("hardness"), 1.0},
              {QStringLiteral("opacity"), 1.0}, {QStringLiteral("flow"), 1.0},
              {QStringLiteral("pressureSize"), false}}),
          QStringLiteral("Start paint with no working selection"));
  editor.addPhotoPaintPoint(9.5, 9.5);
  require(editor.finishPhotoPaintStroke(), QStringLiteral("Commit paint with no working selection"));
  settle(editor);
  const auto after_unselected_paint = render(editor, base);
  require(after_unselected_paint.pixel(9, 9) != before_unselected_paint.pixel(9, 9),
          QStringLiteral("Deselecting must restore unbounded photo-paint scope"));
  return QJsonObject{
      {QStringLiteral("case"), QStringLiteral("active-empty-vs-deselected-and-redo")},
      {QStringLiteral("activeEmptyWorkingSelectionId"), empty_set},
      {QStringLiteral("emptyAttachedMaskId"), attached_id},
      {QStringLiteral("activeEmptyCoveragePixels"), static_cast<qint64>(count_covered(empty_alpha))},
      {QStringLiteral("deselectKeepsMask"), true},
      {QStringLiteral("failedNoopsPreserveRedo"), true},
      {QStringLiteral("deselectedPaintScopeIsFull"), true}};
}

QJsonObject run_shared_locked_selection_case(const QImage& base, const QString& base_path,
                                             bool ancestor_locked) {
  EditorPreviewController seed;
  seed.loadAsset(ancestor_locked ? QStringLiteral("mask-shared-selection-locked-ancestor-seed")
                                 : QStringLiteral("mask-shared-selection-locked-layer-seed"),
                 base_path, {}, {}, {}, {}, {});
  settle(seed);
  create_composed_selection(seed, base, base_path);
  const auto working_id = seed.workingSelectionSetId();
  require(seed.addPaintLayer(),
          QStringLiteral("Create shared-selection layer owner"));
  const auto owner_id = recipe(seed).layers.first().toMap().value(QStringLiteral("id")).toString();

  QString group_id;
  if (ancestor_locked) {
    require(seed.addLayerGroup(QStringLiteral("Locked Selection Parent")),
            QStringLiteral("Create shared-selection ancestor"));
    group_id = recipe(seed).layers.first().toMap().value(QStringLiteral("id")).toString();
    require(seed.movePhotoLayerIntoGroup(layer_index(seed, owner_id), layer_index(seed, group_id)),
            QStringLiteral("Move shared-selection owner under group"));
  }
  require(seed.linkActiveSelectionToPhotoLayer(layer_index(seed, owner_id)),
          QStringLiteral("Link working selection directly to layer owner"));
  require(layer_by_id(seed, owner_id).value(QStringLiteral("selectionMaskSetId")).toString() == working_id,
          QStringLiteral("Shared selection owner must reference the same working-set identity"));
  const auto saved = seed.savedSelections();
  require(!saved.isEmpty(), QStringLiteral("Shared-selection algebra needs a distinct incoming set"));
  const auto incoming_id = saved.constLast().toMap().value(QStringLiteral("id")).toString();
  require(!incoming_id.isEmpty() && incoming_id != working_id,
          QStringLiteral("Incoming algebra source must be independent from the linked set"));

  require(seed.addFillLayer(QStringLiteral("#4366d2"), QStringLiteral("Redo Control Layer")),
          QStringLiteral("Create unlocked redo control layer"));
  const auto control_id = recipe(seed).layers.first().toMap().value(QStringLiteral("id")).toString();
  if (ancestor_locked) {
    require(seed.setPhotoLayerLocked(layer_index(seed, group_id), true),
            QStringLiteral("Lock shared-selection ancestor"));
  } else {
    require(seed.setPhotoLayerLocked(layer_index(seed, owner_id), true),
            QStringLiteral("Lock shared-selection owner"));
  }
  settle(seed);

  EditorPreviewController editor;
  editor.loadAsset(ancestor_locked ? QStringLiteral("mask-shared-selection-locked-ancestor")
                                  : QStringLiteral("mask-shared-selection-locked-layer"),
                   base_path, {}, {}, {}, {}, serialize_edit_recipe_json(recipe(seed)));
  settle(editor);
  require(editor.workingSelectionSetId() == working_id &&
              layer_by_id(editor, owner_id).value(QStringLiteral("selectionMaskSetId")).toString() == working_id,
          QStringLiteral("Reopened shared selection must retain owner linkage"));
  require(!editor.activeMaskEditDisabledReason().isEmpty(),
          QStringLiteral("A locked owner or ancestor must disable shared selection edits"));
  require(!editor.beginPhotoPaintStroke(layer_index(editor, owner_id), {
              {QStringLiteral("color"), QStringLiteral("#fc6438")},
              {QStringLiteral("size"), 18.0}, {QStringLiteral("hardness"), 1.0}}),
          QStringLiteral("Pixel paint must reject a locked owner or ancestor at stroke start"));
  require(editor.selectLayerMask(layer_index(editor, owner_id), working_id),
          QStringLiteral("Inspect shared mask under locked ancestry"));
  require(!editor.beginPhotoPaintStroke(layer_index(editor, owner_id), {
              {QStringLiteral("erase"), true}, {QStringLiteral("size"), 18.0},
              {QStringLiteral("hardness"), 1.0}}),
          QStringLiteral("Mask paint must reject a locked owner or ancestor at stroke start"));
  editor.finishLayerMaskEditing();
  settle(editor);

  require(editor.setPhotoLayerOpacity(layer_index(editor, control_id), 0.72),
          QStringLiteral("Create a redo entry beside locked shared selection"));
  settle(editor);
  editor.undo();
  settle(editor);
  require(editor.canRedo(), QStringLiteral("Locked-selection regression must begin with redo available"));

  const auto before = editor.currentAssetBoundAdjustmentPatchJson();
  const auto before_state = recipe(editor);
  const auto before_history = editor.editHistoryCurrentIndex();
  require(!editor.invertActiveSelection(),
          QStringLiteral("Invert must reject a selection shared with a locked owner"));
  require(!editor.transformWorkingSelection({{QStringLiteral("x"), 0.11},
              {QStringLiteral("y"), 0.0}, {QStringLiteral("scaleX"), 1.0},
              {QStringLiteral("scaleY"), 1.0}}),
          QStringLiteral("Transform must reject a selection shared with a locked owner"));
  settle(editor);
  editor.removeActiveMask();
  settle(editor);
  static_cast<void>(editor.loadMaskAsSelection(incoming_id, QStringLiteral("subtract")));
  settle(editor);
  require(editor.currentAssetBoundAdjustmentPatchJson() == before &&
              recipe(editor).mask_stack == before_state.mask_stack &&
              recipe(editor).layers == before_state.layers &&
              editor.editHistoryCurrentIndex() == before_history && editor.canRedo(),
          QStringLiteral("Rejected shared-set edits must preserve mask ownership, recipe, history and redo"));
  editor.redo();
  settle(editor);
  require(std::abs(layer_by_id(editor, control_id).value(QStringLiteral("opacity")).toDouble() - 0.72) < 0.001,
          QStringLiteral("Shared-set no-ops must leave the redo action intact"));

  return QJsonObject{
      {QStringLiteral("case"), ancestor_locked ? QStringLiteral("shared-set-locked-ancestor")
                                                : QStringLiteral("shared-set-direct-lock")},
      {QStringLiteral("ownerLayerId"), owner_id},
      {QStringLiteral("lockedAncestorId"), group_id},
      {QStringLiteral("workingSelectionId"), working_id},
      {QStringLiteral("incomingSelectionId"), incoming_id},
      {QStringLiteral("invertTransformRemoveAndCombineRejected"), true},
      {QStringLiteral("pixelAndMaskPaintStartRejected"), true},
      {QStringLiteral("redoPreserved"), true}};
}

QJsonObject run_locked_mask_alias_scope_case() {
  const QVariantList layers{
      QVariantMap{{QStringLiteral("id"), QStringLiteral("locked-parent")},
                  {QStringLiteral("kind"), QStringLiteral("group")},
                  {QStringLiteral("locked"), true}},
      QVariantMap{{QStringLiteral("id"), QStringLiteral("legacy-owner")},
                  {QStringLiteral("kind"), QStringLiteral("fill")},
                  {QStringLiteral("parentId"), QStringLiteral("locked-parent")},
                  // Legacy data may store a source ID where modern data stores
                  // the enclosing mask-set ID.
                  {QStringLiteral("selectionMaskSetId"), QStringLiteral("protected-source")}}};
  const QVariantList before{
      QVariantMap{{QStringLiteral("id"), QStringLiteral("protected-source")},
                  {QStringLiteral("maskSetId"), QStringLiteral("protected-set")},
                  {QStringLiteral("kind"), QStringLiteral("rectangle")},
                  {QStringLiteral("centerX"), 0.35}},
      QVariantMap{{QStringLiteral("id"), QStringLiteral("free-source")},
                  {QStringLiteral("maskSetId"), QStringLiteral("unrelated-set")},
                  {QStringLiteral("kind"), QStringLiteral("ellipse")},
                  {QStringLiteral("centerX"), 0.6}}};
  auto unrelated_edit = before;
  auto free_source = unrelated_edit.at(1).toMap();
  free_source.insert(QStringLiteral("centerX"), 0.72);
  unrelated_edit[1] = free_source;
  unrelated_edit.append(QVariantMap{{QStringLiteral("id"), QStringLiteral("free-source-2")},
      {QStringLiteral("maskSetId"), QStringLiteral("unrelated-set")},
      {QStringLiteral("kind"), QStringLiteral("brush")}});
  require(!has_forbidden_locked_selection_change(before, unrelated_edit, layers),
          QStringLiteral("An unrelated mask-set edit must remain available beside a locked owner"));

  auto protected_edit = before;
  auto protected_source = protected_edit.first().toMap();
  protected_source.insert(QStringLiteral("centerX"), 0.45);
  protected_edit[0] = protected_source;
  require(has_forbidden_locked_selection_change(before, protected_edit, layers),
          QStringLiteral("A legacy owner source-ID alias must protect its entire mask set"));
  return QJsonObject{
      {QStringLiteral("case"), QStringLiteral("locked-source-alias-scopes-only-referenced-set")},
      {QStringLiteral("legacySourceAliasProtectedWholeSet"), true},
      {QStringLiteral("unrelatedMaskEditAllowed"), true}};
}

QJsonObject run_mask_paint_lock_race_case(const QImage& base, const QString& base_path) {
  EditorPreviewController editor;
  editor.loadAsset(QStringLiteral("mask-paint-ancestor-lock-race"), base_path, {}, {}, {}, {}, {});
  settle(editor);
  create_composed_selection(editor, base, base_path);
  const auto working_id = editor.workingSelectionSetId();
  require(editor.addFillLayer(QStringLiteral("#536b8a"), QStringLiteral("Mask Stroke Owner")),
          QStringLiteral("Create mask stroke owner"));
  const auto owner_id = recipe(editor).layers.first().toMap().value(QStringLiteral("id")).toString();
  require(editor.addLayerGroup(QStringLiteral("Mask Stroke Parent")),
          QStringLiteral("Create mask stroke parent"));
  const auto group_id = recipe(editor).layers.first().toMap().value(QStringLiteral("id")).toString();
  require(editor.movePhotoLayerIntoGroup(layer_index(editor, owner_id), layer_index(editor, group_id)),
          QStringLiteral("Place mask stroke owner under parent"));
  require(editor.linkActiveSelectionToPhotoLayer(layer_index(editor, owner_id)),
          QStringLiteral("Share working selection with mask stroke owner"));
  require(editor.selectLayerMask(layer_index(editor, owner_id), working_id),
          QStringLiteral("Select shared mask for stroke race"));

  const auto before_cancel = recipe(editor);
  const auto before_cancel_history = editor.editHistoryCurrentIndex();
  require(editor.beginPhotoPaintStroke(layer_index(editor, owner_id), {
              {QStringLiteral("erase"), true}, {QStringLiteral("size"), 36.0},
              {QStringLiteral("hardness"), 1.0}, {QStringLiteral("pressureSize"), false}}),
          QStringLiteral("Start a mask stroke to cancel in flight"));
  for (int point = 0; point < 48; ++point)
    editor.addPhotoPaintPoint(24.0 + point * 1.25, 48.0);
  require(editor.finishPhotoPaintStroke(), QStringLiteral("Dispatch cancellable mask stroke"));
  editor.cancelPhotoPaint();
  settle(editor);
  require(recipe(editor).mask_stack == before_cancel.mask_stack &&
              recipe(editor).layers == before_cancel.layers &&
              serialize_edit_recipe_json(recipe(editor)) == serialize_edit_recipe_json(before_cancel) &&
              editor.editHistoryCurrentIndex() == before_cancel_history,
          QStringLiteral("Cancelling an in-flight mask stroke must preserve coverage and history"));

  require(editor.beginPhotoPaintStroke(layer_index(editor, owner_id), {
              {QStringLiteral("erase"), true}, {QStringLiteral("size"), 36.0},
              {QStringLiteral("hardness"), 1.0}, {QStringLiteral("pressureSize"), false}}),
          QStringLiteral("Start a mask stroke before locking its ancestor"));
  for (int point = 0; point < 48; ++point)
    editor.addPhotoPaintPoint(24.0 + point * 1.25, 48.0);
  require(editor.finishPhotoPaintStroke(), QStringLiteral("Dispatch mask stroke before ancestor lock"));
  require(editor.setPhotoLayerLocked(layer_index(editor, group_id), true),
          QStringLiteral("Lock ancestor before the mask worker commits"));
  const auto locked_snapshot = recipe(editor);
  const auto locked_history = editor.editHistoryCurrentIndex();
  QElapsedTimer timer;
  timer.start();
  do {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    QThread::msleep(1);
  } while ((editor.processing() || editor.selectionWorkBusy() || editor.brushStrokeActive() ||
            editor.photoPaintBusy()) && timer.elapsed() < 20000);
  require(!editor.processing() && !editor.selectionWorkBusy() && !editor.brushStrokeActive() &&
              !editor.photoPaintBusy(),
          QStringLiteral("A mask stroke rejected after ancestor lock must settle"));
  require(!editor.photoPaintError().isEmpty(),
          QStringLiteral("The rejected in-flight mask stroke must report that its target changed"));
  require(recipe(editor).mask_stack == locked_snapshot.mask_stack &&
              recipe(editor).layers == locked_snapshot.layers &&
              serialize_edit_recipe_json(recipe(editor)) == serialize_edit_recipe_json(locked_snapshot) &&
              editor.editHistoryCurrentIndex() == locked_history &&
              editor.canUndo(),
          QStringLiteral("A late ancestor lock must reject the pending mask write and preserve the locked snapshot"));
  return QJsonObject{
      {QStringLiteral("case"), QStringLiteral("mask-paint-cancel-and-ancestor-lock-race")},
      {QStringLiteral("workingSelectionId"), working_id},
      {QStringLiteral("cancelPreservesRecipeAndHistory"), true},
      {QStringLiteral("ancestorLockBeforeCommitPreservesRecipeAndHistory"), true},
      {QStringLiteral("pendingPaintSettled"), true}};
}
}  // namespace

int main(int argc, char** argv) {
  QTemporaryDir runtime;
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", runtime.filePath(QStringLiteral("app-data")).toUtf8());
  qputenv("PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE", "1");
  QGuiApplication app(argc, argv);
  try {
    constexpr int width = 128;
    constexpr int height = 96;
    QImage base(width, height, QImage::Format_RGB32);
    QImage target_source(width, height, QImage::Format_RGB32);
    for (int y = 0; y < height; ++y) {
      for (int x = 0; x < width; ++x) {
        base.setPixelColor(x, y, QColor(42 + x, 55 + y, 105 + (x + y) / 2));
        target_source.setPixelColor(x, y, QColor(210 - x / 2, 35 + y / 2, 72 + (x + y) / 3));
      }
    }
    const auto base_path = runtime.filePath(QStringLiteral("base.png"));
    const auto target_source_path = runtime.filePath(QStringLiteral("target-source.png"));
    require(base.save(base_path) && target_source.save(target_source_path),
            QStringLiteral("Source and linked-layer fixtures must save"));

    QJsonArray cases;
    const auto case_filter = qEnvironmentVariable("PHOTO_EDITOR_MASK_MATRIX_CASE");
    int target_kind_count = 0;
    const auto run_case = [&](const QString& name, auto callback) {
      if (!case_filter.isEmpty() && case_filter != name) return;
      QElapsedTimer case_timer;
      case_timer.start();
      std::cout << "mask-matrix case=" << name.toStdString() << std::endl;
      cases.append(callback());
      std::cout << "mask-matrix completed=" << name.toStdString()
                << " elapsed_ms=" << case_timer.elapsed() << std::endl;
    };
    for (const auto kind : {TargetKind::Adjustment, TargetKind::Pixel, TargetKind::Source,
                            TargetKind::Fill, TargetKind::Text, TargetKind::Shape,
                            TargetKind::Group}) {
      run_case(target_name(kind), [&] {
        ++target_kind_count;
        return run_target_case(base, base_path, target_source_path, kind);
      });
    }
    run_case("nested-ancestor-locks", [&] { return run_ancestor_lock_case(base, base_path); });
    run_case("bypassed-multi-mask-remove-move-copy", [&] { return run_bypassed_multi_mask_case(base, base_path); });
    run_case("direct-mask-bypass-first-selection-attachment", [&] {
      return run_direct_mask_bypass_attach_case(base, base_path);
    });
    run_case("active-empty-vs-deselected-and-redo", [&] { return run_empty_selection_and_redo_case(base, base_path); });
    run_case("shared-set-locked-layer", [&] { return run_shared_locked_selection_case(base, base_path, false); });
    run_case("shared-set-locked-ancestor", [&] { return run_shared_locked_selection_case(base, base_path, true); });
    run_case("locked-source-alias-scopes-only-referenced-set", [&] { return run_locked_mask_alias_scope_case(); });
    run_case("mask-paint-cancel-and-ancestor-lock-race", [&] { return run_mask_paint_lock_race_case(base, base_path); });
    require(!cases.isEmpty(), QStringLiteral("The requested matrix case must exist"));
    const QJsonObject report{{QStringLiteral("suite"), QStringLiteral("mask-layer-interaction-matrix")},
        {QStringLiteral("targetKindCount"), target_kind_count}, {QStringLiteral("caseCount"), cases.size()},
        {QStringLiteral("caseFilter"), case_filter},
        {QStringLiteral("cases"), cases}};
    const auto proof_directory = qEnvironmentVariable("PHOTO_EDITOR_VISUAL_PROOF_DIR").trimmed();
    if (!proof_directory.isEmpty()) {
      require(QDir().mkpath(proof_directory),
              QStringLiteral("Create optional visual proof directory"));
      QFile proof(QDir(proof_directory).filePath(QStringLiteral("mask_layer_interaction_matrix.json")));
      require(proof.open(QIODevice::WriteOnly | QIODevice::Truncate),
              QStringLiteral("Write optional JSON interaction matrix report"));
      proof.write(QJsonDocument(report).toJson(QJsonDocument::Indented));
      proof.close();
    }
    std::cout << "mask/layer interaction matrix passed: " << target_kind_count << " target kinds, "
              << cases.size() << " named controller/render cases\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "mask/layer interaction matrix failed: " << error.what() << '\n';
    return 1;
  }
}
