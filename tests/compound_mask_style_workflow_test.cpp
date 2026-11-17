#include "canonical_composite_mask_alpha.h"
#include "edit_adjustment_patch.h"
#include "edit_recipe_mask_schema.h"
#include "edit_recipe_state.h"
#include "editor_preview_controller.h"

#include <QColorSpace>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QMetaType>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>
#include <QTemporaryDir>
#include <QThread>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace photo_editor::desktop {
struct EditorPreviewControllerTestAccess {
  static EditState state(const EditorPreviewController& controller) {
    return controller.currentState();
  }

  static void setSyntheticSmartSelect(EditorPreviewController& controller,
                                      const QByteArray& alpha, int width, int height) {
    controller.smart_select_preview_mask_ = {
        {QStringLiteral("segmentationData"), alpha},
        {QStringLiteral("segmentationWidth"), width},
        {QStringLiteral("segmentationHeight"), height}};
  }
};
}  // namespace photo_editor::desktop

using namespace photo_editor::desktop;

namespace {

void require(bool condition, const QString& message) {
  if (!condition) throw std::runtime_error(message.toStdString());
}

void settle(EditorPreviewController& editor, const QString& phase) {
  QElapsedTimer timer;
  timer.start();
  const auto busy = [&editor]() {
    return editor.processing() || editor.fullRenderRunning() ||
        editor.selectionWorkBusy() || editor.professionalToolsBusy() ||
        editor.photoPaintBusy() || editor.photoExportBusy();
  };
  while (busy() && timer.elapsed() < 30000) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    QThread::msleep(1);
  }
  require(!busy(), QStringLiteral("%1 must finish within the bounded controller wait").arg(phase));
  require(editor.photoPaintError().isEmpty(), editor.photoPaintError());
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
  while (!finished && timer.elapsed() < 30000) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    QThread::msleep(1);
  }
  QObject::disconnect(connection);
  require(finished && succeeded, QStringLiteral("The requested history restore must succeed"));
  settle(editor, QStringLiteral("History restore"));
}

EditState state(const EditorPreviewController& editor) {
  return EditorPreviewControllerTestAccess::state(editor);
}

QString recipe_json(const EditorPreviewController& editor) {
  return serialize_edit_recipe_json(state(editor));
}

QString layer_id_by_name(const QVariantList& layers, const QString& name) {
  for (const auto& value : layers) {
    const auto layer = value.toMap();
    if (layer.value(QStringLiteral("name")).toString() == name)
      return layer.value(QStringLiteral("id")).toString();
  }
  return {};
}

int layer_index(const QVariantList& layers, const QString& id) {
  for (int index = 0; index < layers.size(); ++index) {
    if (layers.at(index).toMap().value(QStringLiteral("id")).toString() == id)
      return index;
  }
  return -1;
}

QVariantMap layer_by_id(const QVariantList& layers, const QString& id) {
  const auto index = layer_index(layers, id);
  return index < 0 ? QVariantMap{} : layers.at(index).toMap();
}

QVariantList masks_for_set(const QVariantList& masks, const QString& set_id) {
  QVariantList result;
  for (const auto& value : masks) {
    if (value.toMap().value(QStringLiteral("maskSetId")).toString() == set_id)
      result.append(value);
  }
  return result;
}

QVariantList without_mask_source_identity(QVariantList sources) {
  for (auto& value : sources) {
    auto source = value.toMap();
    source.remove(QStringLiteral("id"));
    source.remove(QStringLiteral("maskSetId"));
    source.remove(QStringLiteral("maskSetName"));
    value = source;
  }
  return sources;
}

EditState authored_recipe_projection(EditState document) {
  const QStringList presentation_fields{
      QStringLiteral("flatIndex"), QStringLiteral("kindLabel"),
      QStringLiteral("displayName"), QStringLiteral("displaySubtitle"),
      QStringLiteral("hasNonNeutralAdjustments"),
      QStringLiteral("changedAdjustmentGroups"),
      QStringLiteral("changedAdjustmentGroupLabel"),
      QStringLiteral("adjustmentsPreviewBypassed"),
      QStringLiteral("opacityPercent"), QStringLiteral("hasLayerMask"),
      QStringLiteral("maskOpacityPercent"), QStringLiteral("maskDensityPercent"),
      QStringLiteral("selectionMaskSetCount"),
      QStringLiteral("selectionMaskSetSignature")};
  for (auto index = 0; index < document.layers.size(); ++index) {
    auto layer = document.layers.at(index).toMap();
    for (const auto& key : presentation_fields) layer.remove(key);
    for (auto it = layer.begin(); it != layer.end();) {
      if (it.key().startsWith(QStringLiteral("thumbnail"))) it = layer.erase(it);
      else ++it;
    }
    document.layers[index] = layer;
  }
  return document;
}

QStringList differing_keys(const QVariantMap& left, const QVariantMap& right) {
  QStringList result;
  for (auto it = left.cbegin(); it != left.cend(); ++it)
    if (right.value(it.key()) != it.value()) result.append(it.key());
  for (auto it = right.cbegin(); it != right.cend(); ++it)
    if (!left.contains(it.key())) result.append(it.key());
  result.removeDuplicates();
  return result;
}

QStringList differing_serialized_top_level_keys(const QString& left_json,
                                                 const QString& right_json) {
  const auto left_document = QJsonDocument::fromJson(left_json.toUtf8());
  const auto right_document = QJsonDocument::fromJson(right_json.toUtf8());
  const auto left = left_document.object();
  const auto right = right_document.object();
  QStringList result;
  for (auto it = left.begin(); it != left.end(); ++it)
    if (right.value(it.key()) != it.value()) result.append(it.key());
  for (auto it = right.begin(); it != right.end(); ++it)
    if (!left.contains(it.key())) result.append(it.key());
  result.removeDuplicates();
  result.sort();
  return result;
}

QStringList mask_ids(const QVariantMap& layer, const QString& key) {
  QStringList result;
  const auto value = layer.value(key);
  if (value.metaType().id() == QMetaType::QStringList) return value.toStringList();
  for (const auto& item : value.toList()) result.append(item.toString());
  return result;
}

QByteArray coverage(const QImage& source, const QVariantList& masks,
                    const QString& set_id) {
  const auto evaluated = evaluate_canonical_composite_mask_alpha(
      source, masks_for_set(masks, set_id), 1.0, {1.0, 1.0, 1.0});
  require(evaluated.valid,
          QStringLiteral("Canonical mask coverage must resolve for set %1").arg(set_id));
  return evaluated.alpha_mask;
}

void require_selection_algebra(const QByteArray& actual, const QByteArray& left,
                               const QByteArray& right, const QString& operation) {
  require(actual.size() == left.size() && left.size() == right.size(),
          QStringLiteral("Selection coverage sizes must agree for %1").arg(operation));
  for (qsizetype index = 0; index < actual.size(); ++index) {
    const int a = static_cast<unsigned char>(left.at(index));
    const int b = static_cast<unsigned char>(right.at(index));
    const int expected = operation == QStringLiteral("add")
        ? qRound(a + b - a * b / 255.0)
        : operation == QStringLiteral("subtract") ? std::max(0, a - b)
                                                    : qRound(a * b / 255.0);
    const int observed = static_cast<unsigned char>(actual.at(index));
    require(std::abs(observed - expected) <= 1,
            QStringLiteral("%1 selection algebra differs at sample %2").arg(operation).arg(index));
  }
}

void append_synthetic_subject_source(EditState* document, const QString& selection_id,
                                     int width, int height) {
  require(document != nullptr, QStringLiteral("Synthetic source fixture needs a recipe"));
  const auto current_sources = masks_for_set(document->mask_stack, selection_id);
  require(!current_sources.isEmpty(), QStringLiteral("Working selection sources must exist"));
  const auto set_reference = current_sources.first().toMap();
  auto synthetic_subject = default_mask_definition(QStringLiteral("subject"),
      static_cast<int>(document->mask_stack.size()) + 1);
  synthetic_subject.insert(QStringLiteral("id"), QStringLiteral("fixture-subject-source"));
  synthetic_subject.insert(QStringLiteral("maskSetId"), selection_id);
  synthetic_subject.insert(QStringLiteral("maskSetName"),
      set_reference.value(QStringLiteral("maskSetName")));
  synthetic_subject.insert(QStringLiteral("operation"), QStringLiteral("add"));
  synthetic_subject.insert(QStringLiteral("coordinateSpace"), QStringLiteral("source"));
  synthetic_subject.insert(QStringLiteral("generatedMaskMetadata"),
      QVariantMap{{QStringLiteral("kind"), QStringLiteral("subject")},
                  {QStringLiteral("runtimeLabel"), QStringLiteral("Authored deterministic fixture")},
                  {QStringLiteral("fixtureOnly"), true}});
  for (const auto* key : {"maskSetSelectionInvert", "maskSetSelectionExpand",
                           "maskSetSelectionSmooth", "maskSetSelectionFineEdge",
                           "maskSetSelectionEdgeContrast", "maskSetDensity",
                           "maskSetOpacity", "maskSetPreviewBypassed"}) {
    if (set_reference.contains(QLatin1String(key)))
      synthetic_subject.insert(QLatin1String(key), set_reference.value(QLatin1String(key)));
  }
  QByteArray authored_alpha(width * height, char(0));
  for (int y = height * 2 / 3; y < height - 2; ++y)
    for (int x = width / 3; x < width * 2 / 3; ++x)
      authored_alpha[y * width + x] = char(255);
  synthetic_subject.insert(QStringLiteral("segmentationData"), authored_alpha);
  synthetic_subject.insert(QStringLiteral("segmentationWidth"), width);
  synthetic_subject.insert(QStringLiteral("segmentationHeight"), height);
  document->mask_stack.append(synthetic_subject);
}

void compare_and_save_export(EditorPreviewController& editor, const QString& path,
                             QImage* image) {
  require(editor.exportFlattenedPhoto(path),
          QStringLiteral("Ordinary flattened PNG export must complete at %1").arg(path));
  *image = QImage(path);
  require(!image->isNull(), QStringLiteral("Flattened export must be readable"));
}

void commit_paint(EditorPreviewController& editor, int index, double x, double y,
                  bool erase, const QString& label) {
  require(editor.beginPhotoPaintStroke(index,
              {{QStringLiteral("erase"), erase},
               {QStringLiteral("color"), QStringLiteral("#f12659")},
               {QStringLiteral("size"), 9.0},
               {QStringLiteral("hardness"), 1.0},
               {QStringLiteral("opacity"), 1.0},
               {QStringLiteral("flow"), 1.0},
               {QStringLiteral("pressureSize"), false}}),
          QStringLiteral("Begin %1 paint stroke").arg(label));
  editor.addPhotoPaintPoint(x, y);
  require(editor.finishPhotoPaintStroke(),
          QStringLiteral("Commit %1 paint stroke").arg(label));
  settle(editor, label);
}

int run_compound_journey() {
  QTemporaryDir temporary;
  require(temporary.isValid(), QStringLiteral("Compound workflow needs isolated storage"));
  constexpr int width = 48;
  constexpr int height = 36;
  QImage source(width, height, QImage::Format_ARGB32);
  source.setColorSpace(QColorSpace(QColorSpace::SRgb));
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      source.setPixelColor(x, y, QColor(24 + x * 3, 35 + y * 4,
                                        70 + (x + y) % 40));
    }
  }
  const auto source_path = temporary.filePath(QStringLiteral("compound-source.png"));
  require(source.save(source_path, "PNG"), QStringLiteral("Write deterministic source image"));

  EditorPreviewController editor;
  const auto asset_id = QStringLiteral("compound-mask-style-workflow");
  editor.loadAsset(asset_id, source_path, {}, {}, {}, {}, {}, true);
  settle(editor, QStringLiteral("Initial asset render"));

  // Compose a saved selection from a geometric source, a deterministic
  // synthetic object result accepted through the public preview path, and a
  // painted brush source. No segmentation model or semantic accuracy is
  // implied by the fixture pixels.
  editor.addMask(QStringLiteral("ellipse"));
  editor.setActiveMaskFeather(0.0);
  editor.setActiveMaskCenterX(0.48);
  editor.setActiveMaskCenterY(0.49);
  editor.setActiveMaskRadiusX(0.38);
  editor.setActiveMaskRadiusY(0.39);
  settle(editor, QStringLiteral("Geometric selection"));
  const auto selection_id = editor.workingSelectionSetId();
  require(!selection_id.isEmpty(), QStringLiteral("Ellipse must start the working selection"));

  editor.addMask(QStringLiteral("object"));
  editor.setActiveMaskOperation(QStringLiteral("add"));
  QByteArray object_alpha(width * height, char(0));
  for (int y = 7; y < 23; ++y)
    for (int x = 5; x < 20; ++x)
      object_alpha[y * width + x] = char(255);
  EditorPreviewControllerTestAccess::setSyntheticSmartSelect(editor, object_alpha, width, height);
  require(editor.acceptSmartSelectPreview(QStringLiteral("selection"), false) ==
              QStringLiteral("selection-ai"),
          QStringLiteral("Accept deterministic synthetic object selection"));
  settle(editor, QStringLiteral("Synthetic object selection"));
  require(editor.workingSelectionSetId() == selection_id,
          QStringLiteral("Object source must join the existing working selection"));

  editor.addMask(QStringLiteral("brush"));
  editor.setActiveMaskOperation(QStringLiteral("add"));
  editor.setActiveMaskBrushSize(0.16);
  editor.setActiveMaskBrushHardness(1.0);
  editor.setActiveMaskFeather(0.0);
  editor.beginActiveBrushSourceStroke(QStringLiteral("add"));
  editor.addBrushPoint(0.84, 0.22);
  editor.endActiveBrushStroke();
  settle(editor, QStringLiteral("Brush source"));

  auto document = state(editor);
  append_synthetic_subject_source(&document, selection_id, width, height);
  editor.loadAsset(asset_id, source_path, {}, {}, {}, {},
                   serialize_edit_recipe_json(document), true);
  settle(editor, QStringLiteral("Synthetic authored subject source reopen"));
  const auto composed_sources = editor.workingSelectionMasks();
  const auto composed_alpha = coverage(source, editor.maskStack(), selection_id);
  QStringList source_kinds;
  for (const auto& value : composed_sources)
    source_kinds.append(value.toMap().value(QStringLiteral("kind")).toString());
  require(source_kinds.contains(QStringLiteral("ellipse")) &&
              source_kinds.contains(QStringLiteral("object")) &&
              source_kinds.contains(QStringLiteral("brush")) &&
              source_kinds.contains(QStringLiteral("subject")) &&
              composed_sources.size() == 4,
          QStringLiteral("Ellipse, brush, object, and authored subject-compatible sources must share one set"));
  require(std::count_if(composed_alpha.cbegin(), composed_alpha.cend(), [](char value) {
            return static_cast<unsigned char>(value) > 0;
          }) > width * height / 2,
          QStringLiteral("Composed working selection must have broad deterministic coverage"));
  require(editor.saveWorkingSelection(QStringLiteral("Compound working selection")),
          QStringLiteral("Save the composed selection for repeatable operations"));
  settle(editor, QStringLiteral("Saved selection"));
  const auto saved_selection_id = editor.savedSelections().constLast().toMap()
      .value(QStringLiteral("id")).toString();
  require(!saved_selection_id.isEmpty() && saved_selection_id != selection_id,
          QStringLiteral("Saved selection must be an independent source set"));

  // Exercise the same saved compound selection against a fresh working set;
  // each operation must retain its destination ID and undo to exact coverage.
  EditorPreviewController algebra;
  algebra.loadAsset(asset_id + QStringLiteral("-algebra"), source_path, {}, {}, {}, {},
                    recipe_json(editor), true);
  settle(algebra, QStringLiteral("Algebra controller reopen"));
  require(algebra.deselectWorkingSelection(), QStringLiteral("Clear the algebra working set"));
  algebra.addMask(QStringLiteral("ellipse"));
  algebra.setActiveMaskFeather(0.0);
  algebra.setActiveMaskCenterX(0.57);
  algebra.setActiveMaskCenterY(0.51);
  algebra.setActiveMaskRadiusX(0.28);
  algebra.setActiveMaskRadiusY(0.31);
  settle(algebra, QStringLiteral("Algebra destination ellipse"));
  const auto algebra_set_id = algebra.workingSelectionSetId();
  const auto algebra_baseline = coverage(source, algebra.maskStack(), algebra_set_id);
  const auto incoming = coverage(source, algebra.maskStack(), saved_selection_id);
  for (const auto& operation : {QStringLiteral("add"), QStringLiteral("subtract"),
                                QStringLiteral("intersect")}) {
    require(algebra.loadMaskAsSelection(saved_selection_id, operation),
            QStringLiteral("Apply %1 to saved compound selection").arg(operation));
    settle(algebra, QStringLiteral("Selection %1").arg(operation));
    require(algebra.workingSelectionSetId() == algebra_set_id,
            QStringLiteral("%1 must retain the working selection identity").arg(operation));
    require_selection_algebra(coverage(source, algebra.maskStack(), algebra_set_id),
                              algebra_baseline, incoming, operation);
    restore_history(algebra, false);
    require(coverage(source, algebra.maskStack(), algebra_set_id) == algebra_baseline,
            QStringLiteral("Undo %1 must restore exact destination coverage").arg(operation));
  }
  require(algebra.invertActiveSelection(), QStringLiteral("Invert the compound selection result"));
  settle(algebra, QStringLiteral("Selection inversion"));
  const auto inverted = coverage(source, algebra.maskStack(), algebra_set_id);
  for (qsizetype index = 0; index < algebra_baseline.size(); ++index) {
    const int before = static_cast<unsigned char>(algebra_baseline.at(index));
    const int after = static_cast<unsigned char>(inverted.at(index));
    require(std::abs(before + after - 255) <= 1,
            QStringLiteral("Inversion must complement coverage at sample %1").arg(index));
  }
  restore_history(algebra, false);
  require(coverage(source, algebra.maskStack(), algebra_set_id) == algebra_baseline,
          QStringLiteral("Undo inversion must restore the exact destination mask"));

  // Build a two-level group around the paint layer, transform the outer group,
  // then verify selection clipping and exact erasure in ordinary PNG output.
  const auto layers_before_paint = state(editor).layers;
  require(editor.addPaintLayer(), QStringLiteral("Create transformed paint target"));
  auto current = state(editor);
  QString paint_id;
  for (const auto& value : current.layers) {
    const auto candidate_id = value.toMap().value(QStringLiteral("id")).toString();
    if (candidate_id.isEmpty()) continue;
    if (layer_index(layers_before_paint, candidate_id) < 0) {
      paint_id = candidate_id;
      break;
    }
  }
  require(!paint_id.isEmpty(), QStringLiteral("Resolve paint target by its authored name"));
  require(editor.addLayerGroup(QStringLiteral("Inner Group")), QStringLiteral("Create inner group"));
  current = state(editor);
  const auto inner_group_id = layer_id_by_name(current.layers, QStringLiteral("Inner Group"));
  require(!inner_group_id.isEmpty() &&
              editor.movePhotoLayerIntoGroup(layer_index(current.layers, paint_id),
                  layer_index(current.layers, inner_group_id)),
          QStringLiteral("Nest paint target inside inner group"));
  require(editor.addLayerGroup(QStringLiteral("Outer Group")), QStringLiteral("Create outer group"));
  current = state(editor);
  const auto outer_group_id = layer_id_by_name(current.layers, QStringLiteral("Outer Group"));
  require(!outer_group_id.isEmpty() &&
              editor.movePhotoLayerIntoGroup(layer_index(current.layers, inner_group_id),
                  layer_index(current.layers, outer_group_id)),
          QStringLiteral("Nest inner group inside outer group"));
  current = state(editor);
  require(editor.setPhotoLayerTransform(layer_index(current.layers, outer_group_id), 0, 0, 1.04, 0.0),
          QStringLiteral("Transform the containing group"));
  current = state(editor);
  const auto paint_index = layer_index(current.layers, paint_id);
  require(paint_index >= 0 && editor.applyWorkingSelectionToLayer(paint_index),
          QStringLiteral("Attach a cloned selection to the nested paint target"));
  settle(editor, QStringLiteral("Nested paint target mask"));
  current = state(editor);
  const auto paint_mask_id = layer_by_id(current.layers, paint_id)
      .value(QStringLiteral("selectionMaskSetId")).toString();
  require(!paint_mask_id.isEmpty() && paint_mask_id != selection_id &&
              editor.workingSelectionSetId() == selection_id,
          QStringLiteral("Ordinary layer mask must be independent of working selection"));

  QImage baseline_export;
  const auto baseline_path = temporary.filePath(QStringLiteral("before-clipped-paint.png"));
  compare_and_save_export(editor, baseline_path, &baseline_export);
  auto stroke_index = layer_index(state(editor).layers, paint_id);
  const auto history_before_inside_paint = editor.editHistoryCurrentIndex();
  commit_paint(editor, stroke_index, 24.0, 18.0, false,
               QStringLiteral("selection-clipped paint"));
  require(editor.editHistoryCurrentIndex() == history_before_inside_paint + 1,
          QStringLiteral("A committed clipped paint stroke must be one history entry"));
  const auto history_before_outside_paint = editor.editHistoryCurrentIndex();
  commit_paint(editor, stroke_index, 3.0, 3.0, false,
               QStringLiteral("outside-selection paint"));
  require(editor.editHistoryCurrentIndex() == history_before_outside_paint,
          QStringLiteral("A stroke fully outside the selection must not create a content history edit"));
  QImage painted_export;
  const auto painted_path = temporary.filePath(QStringLiteral("after-clipped-paint.png"));
  compare_and_save_export(editor, painted_path, &painted_export);
  require(painted_export.pixel(24, 18) != baseline_export.pixel(24, 18) &&
              painted_export.pixel(3, 3) == baseline_export.pixel(3, 3),
          QStringLiteral("Paint must change the selected center and preserve an outside-selection pixel"));
  stroke_index = layer_index(state(editor).layers, paint_id);
  const auto history_before_erase = editor.editHistoryCurrentIndex();
  commit_paint(editor, stroke_index, 24.0, 18.0, true,
               QStringLiteral("selection-clipped erase"));
  require(editor.editHistoryCurrentIndex() == history_before_erase + 1,
          QStringLiteral("A committed clipped erase stroke must be one history entry"));
  QImage erased_export;
  const auto erased_path = temporary.filePath(QStringLiteral("after-clipped-erase.png"));
  compare_and_save_export(editor, erased_path, &erased_export);
  require(erased_export.pixel(24, 18) == baseline_export.pixel(24, 18) &&
              erased_export.pixel(3, 3) == baseline_export.pixel(3, 3),
          QStringLiteral("Erase must restore the selected center and preserve outside pixels"));

  const auto before_cancel = recipe_json(editor);
  const auto history_before_cancel = editor.editHistoryCurrentIndex();
  stroke_index = layer_index(state(editor).layers, paint_id);
  require(editor.beginPhotoPaintStroke(stroke_index,
              {{QStringLiteral("color"), QStringLiteral("#f12659")},
               {QStringLiteral("size"), 9.0},
               {QStringLiteral("hardness"), 1.0}}),
          QStringLiteral("Begin a cancellable asynchronous paint stroke"));
  editor.addPhotoPaintPoint(24.0, 18.0);
  editor.cancelPhotoPaint();
  settle(editor, QStringLiteral("Cancelled asynchronous paint stroke"));
  require(recipe_json(editor) == before_cancel &&
              editor.editHistoryCurrentIndex() == history_before_cancel,
          QStringLiteral("Cancelling an in-flight stroke must restore exact recipe and history"));

  // A second owner carries ordinary, style, and effect masks copied from the
  // same working set. Switching mask editors and painting must mutate only
  // the selected copy while the free selection remains unchanged.
  require(editor.addFillLayer(QStringLiteral("#617d99"),
              QStringLiteral("Style and effect target")),
          QStringLiteral("Create style/effect target"));
  current = state(editor);
  const auto style_target_id = layer_id_by_name(current.layers,
      QStringLiteral("Style and effect target"));
  auto style_target_index = layer_index(current.layers, style_target_id);
  require(style_target_index >= 0 && editor.applyWorkingSelectionToLayer(style_target_index),
          QStringLiteral("Attach independent ordinary mask to style/effect target"));
  settle(editor, QStringLiteral("Ordinary style target mask"));
  style_target_index = layer_index(state(editor).layers, style_target_id);
  const auto style_target_before_add = layer_by_id(state(editor).layers, style_target_id);
  const auto style_history_before_add = editor.editHistoryCurrentIndex();
  const auto style_added = editor.addPhotoLayerStyle(
      style_target_index, QStringLiteral("color_overlay"));
  if (!style_added) {
    std::cerr << "Style-add diagnostic: id='" << style_target_id.toStdString()
              << "' index=" << style_target_index
              << " kind='" << style_target_before_add.value(QStringLiteral("kind")).toString().toStdString()
              << " locked=" << style_target_before_add.value(QStringLiteral("locked")).toBool()
              << " history_before=" << style_history_before_add
              << " history_after=" << editor.editHistoryCurrentIndex()
              << " status='" << editor.status().toStdString() << "'\n";
  }
  require(style_added,
          QStringLiteral("Add a style operation"));
  auto styles = editor.photoLayerStyles(style_target_index);
  const auto style_id = styles.constLast().toMap().value(QStringLiteral("id")).toString();
  require(!style_id.isEmpty() && editor.maskPhotoLayerStyleFromSelection(style_target_index, style_id),
          QStringLiteral("Copy working selection into an independent style mask"));
  settle(editor, QStringLiteral("Style mask creation"));
  styles = editor.photoLayerStyles(style_target_index);
  const auto style_mask_id = styles.constLast().toMap().value(QStringLiteral("maskRef")).toString();
  require(!style_mask_id.isEmpty() && style_mask_id != selection_id &&
              style_mask_id != paint_mask_id,
          QStringLiteral("Style mask must have identity independent from working and ordinary masks"));

  require(editor.setPhotoLayerEffectParameters(style_target_index,
              QStringLiteral("film_grain"), QString(), {}, 1.0, true),
          QStringLiteral("Add a deterministic effect operation"));
  auto effects = editor.photoLayerEffects(style_target_index);
  const auto effect_id = effects.constLast().toMap().value(QStringLiteral("id")).toString();
  editor.setActiveMaskIndex(-1);
  current = state(editor);
  const auto working_sources_for_effect = masks_for_set(current.mask_stack, selection_id);
  require(editor.activeMaskIndex() == -1 && editor.hasWorkingSelection() &&
              editor.workingSelectionSetId() == selection_id &&
              !working_sources_for_effect.isEmpty(),
          QStringLiteral("Effect-mask copy must start from the persistent working selection, independent of the inspected mask"));
  const auto effect_mask_created = !effect_id.isEmpty() &&
      editor.maskPhotoLayerEffectFromSelection(style_target_index, effect_id);
  if (!effect_mask_created) {
    std::cerr << "Effect-mask diagnostic: target_id='" << style_target_id.toStdString()
              << "' index=" << style_target_index
              << " effect_id='" << effect_id.toStdString()
              << "' active_mask_index=" << editor.activeMaskIndex()
              << " working_selection_id='" << editor.workingSelectionSetId().toStdString()
              << "' has_working_selection=" << editor.hasWorkingSelection()
              << " status='" << editor.status().toStdString() << "'\n";
  }
  require(effect_mask_created,
          QStringLiteral("Copy working selection into an independent effect mask"));
  require(editor.hasWorkingSelection() &&
              editor.workingSelectionSetId() == selection_id,
          QStringLiteral("Creating the effect mask must preserve the independent working selection"));
  settle(editor, QStringLiteral("Effect mask creation"));
  effects = editor.photoLayerEffects(style_target_index);
  const auto effect_mask_id = effects.constLast().toMap().value(QStringLiteral("maskId")).toString();
  current = state(editor);
  const auto copied_effect_sources = masks_for_set(current.mask_stack, effect_mask_id);
  require(without_mask_source_identity(copied_effect_sources) ==
              without_mask_source_identity(working_sources_for_effect),
          QStringLiteral("Effect mask must clone the working selection's exact source geometry and metadata"));
  const auto style_layer = layer_by_id(current.layers, style_target_id);
  const auto ordinary_ids = mask_ids(style_layer, QStringLiteral("selectionMaskSetIds"));
  require(!effect_mask_id.isEmpty() && effect_mask_id != style_mask_id &&
              effect_mask_id != selection_id && ordinary_ids.size() == 1 &&
              ordinary_ids.first() != effect_mask_id && ordinary_ids.first() != style_mask_id,
          QStringLiteral("Ordinary, style, effect, and working masks must remain four independent sets"));

  const auto source_selection_before_mask_edits = masks_for_set(current.mask_stack, selection_id);
  const auto ordinary_before_mask_edits = masks_for_set(current.mask_stack, ordinary_ids.first());
  const auto style_before_mask_edits = masks_for_set(current.mask_stack, style_mask_id);
  const auto effect_before_mask_edits = masks_for_set(current.mask_stack, effect_mask_id);
  require(editor.selectPhotoLayerStyleMask(style_target_index, style_id) &&
              editor.editingLayerMask() && editor.maskEditLayerId() == style_target_id &&
              editor.workingSelectionSetId() == selection_id,
          QStringLiteral("Selecting style mask must preserve working selection identity"));
  commit_paint(editor, style_target_index, 24.0, 18.0, true,
               QStringLiteral("style-mask erase"));
  current = state(editor);
  const auto style_after_erase = masks_for_set(current.mask_stack, style_mask_id);
  require(style_after_erase != style_before_mask_edits &&
              masks_for_set(current.mask_stack, selection_id) == source_selection_before_mask_edits &&
              masks_for_set(current.mask_stack, ordinary_ids.first()) == ordinary_before_mask_edits &&
              masks_for_set(current.mask_stack, effect_mask_id) == effect_before_mask_edits &&
              layer_by_id(current.layers, style_target_id) == style_layer &&
              editor.workingSelectionSetId() == selection_id,
          QStringLiteral("Style-mask editing must preserve the source, ordinary mask, effect mask, and selection"));
  editor.finishLayerMaskEditing();
  require(editor.selectPhotoLayerEffectMask(style_target_index, effect_id) &&
              editor.editingLayerMask() && editor.maskEditLayerId() == style_target_id &&
              editor.workingSelectionSetId() == selection_id,
          QStringLiteral("Switching to effect mask must preserve the same working selection"));
  commit_paint(editor, style_target_index, 24.0, 18.0, false,
               QStringLiteral("effect-mask reveal"));
  current = state(editor);
  require(masks_for_set(current.mask_stack, selection_id) == source_selection_before_mask_edits &&
              masks_for_set(current.mask_stack, ordinary_ids.first()) == ordinary_before_mask_edits &&
              masks_for_set(current.mask_stack, style_mask_id) == style_after_erase &&
              masks_for_set(current.mask_stack, effect_mask_id) != effect_before_mask_edits &&
              layer_by_id(current.layers, style_target_id) == style_layer &&
              editor.workingSelectionSetId() == selection_id,
          QStringLiteral("Effect-mask editing must not collapse or replace other mask owners"));
  editor.finishLayerMaskEditing();
  settle(editor, QStringLiteral("Finish mask target switches"));

  // Duplicate one ordinary-mask consumer to make a genuinely shared set.
  // Move the original owner's link, then Alt-copy the retained shared set to
  // a third owner and assert exact IDs, equal coverage, and limited mutation.
  const auto before_duplicate = state(editor);
  const auto paint_index_before_duplicate = layer_index(before_duplicate.layers, paint_id);
  require(editor.duplicatePhotoLayerWithMasks(paint_index_before_duplicate),
          QStringLiteral("Duplicate nested paint owner with shared mask"));
  settle(editor, QStringLiteral("Shared-mask duplicate"));
  current = state(editor);
  QString duplicate_id;
  for (const auto& value : current.layers) {
    const auto candidate = value.toMap();
    if (candidate.value(QStringLiteral("duplicateMaskPolicy")).toString() ==
            QStringLiteral("with-masks") &&
        candidate.value(QStringLiteral("parentId")).toString() == inner_group_id &&
        candidate.value(QStringLiteral("id")).toString() != paint_id) {
      duplicate_id = candidate.value(QStringLiteral("id")).toString();
      break;
    }
  }
  require(!duplicate_id.isEmpty() &&
              mask_ids(layer_by_id(current.layers, duplicate_id),
                       QStringLiteral("selectionMaskSetIds")).contains(paint_mask_id),
          QStringLiteral("Duplicate must share the exact ordinary mask set under the same nested group"));
  require(editor.transferLayerMask(paint_id, paint_mask_id, style_target_id, false),
          QStringLiteral("Move a shared mask from one owner to the styled target"));
  settle(editor, QStringLiteral("Move shared mask"));
  current = state(editor);
  require(!mask_ids(layer_by_id(current.layers, paint_id),
                    QStringLiteral("selectionMaskSetIds")).contains(paint_mask_id) &&
              mask_ids(layer_by_id(current.layers, duplicate_id),
                       QStringLiteral("selectionMaskSetIds")).contains(paint_mask_id) &&
              mask_ids(layer_by_id(current.layers, style_target_id),
                       QStringLiteral("selectionMaskSetIds")).contains(paint_mask_id),
          QStringLiteral("Moving a shared mask must detach only the intended owner"));
  require(editor.transferLayerMask(duplicate_id, paint_mask_id, paint_id, true),
          QStringLiteral("Alt-copy the retained shared mask back to the paint owner"));
  settle(editor, QStringLiteral("Alt-copy shared mask"));
  current = state(editor);
  const auto copied_paint_mask_id = mask_ids(layer_by_id(current.layers, paint_id),
      QStringLiteral("selectionMaskSetIds")).last();
  require(!copied_paint_mask_id.isEmpty() && copied_paint_mask_id != paint_mask_id &&
              mask_ids(layer_by_id(current.layers, duplicate_id),
                       QStringLiteral("selectionMaskSetIds")).contains(paint_mask_id) &&
              mask_ids(layer_by_id(current.layers, style_target_id),
                       QStringLiteral("selectionMaskSetIds")).contains(paint_mask_id) &&
              coverage(source, current.mask_stack, copied_paint_mask_id) ==
                  coverage(source, current.mask_stack, paint_mask_id),
          QStringLiteral("Alt-copy must create independent equal coverage while retaining shared consumers"));

  const auto outer_index = layer_index(current.layers, outer_group_id);
  require(outer_index >= 0 && editor.setPhotoLayerLocked(outer_index, true),
          QStringLiteral("Lock the nested parent group"));
  settle(editor, QStringLiteral("Nested lock"));
  require(!editor.beginPhotoPaintStroke(layer_index(state(editor).layers, duplicate_id),
              {{QStringLiteral("color"), QStringLiteral("#f12659")},
               {QStringLiteral("size"), 9.0}, {QStringLiteral("hardness"), 1.0}}),
          QStringLiteral("Inherited lock must block a nested paint stroke"));

  // A reversible effect edit on the same compound target provides a redo
  // entry so rejected operations can prove they preserve the exact recipe.
  style_target_index = layer_index(state(editor).layers, style_target_id);
  require(style_target_index >= 0 &&
              editor.setPhotoLayerEffectAmount(style_target_index, effect_id, 0.83),
          QStringLiteral("Create redo control on the same compound layer target"));
  settle(editor, QStringLiteral("Redo control operation"));
  const auto effect_amount_after_control = editor.photoLayerEffects(style_target_index)
      .constLast().toMap().value(QStringLiteral("amount")).toDouble();
  restore_history(editor, false);
  require(editor.canRedo(), QStringLiteral("Locked-operation checks must start with redo available"));
  const auto before_rejections = recipe_json(editor);
  const auto before_rejection_history = editor.editHistoryCurrentIndex();
  require(!editor.transferLayerMask(duplicate_id, paint_mask_id, style_target_id, false) &&
              !editor.transferLayerMask(style_target_id, paint_mask_id, duplicate_id, true) &&
              !editor.beginPhotoPaintStroke(layer_index(state(editor).layers, duplicate_id),
                  {{QStringLiteral("erase"), true}, {QStringLiteral("size"), 9.0},
                   {QStringLiteral("hardness"), 1.0}}),
          QStringLiteral("Inherited lock must reject source and destination transfers and mask painting"));
  settle(editor, QStringLiteral("Rejected locked operations"));
  require(recipe_json(editor) == before_rejections &&
              editor.editHistoryCurrentIndex() == before_rejection_history && editor.canRedo(),
          QStringLiteral("Rejected nested edits must preserve recipe, history, and redo"));
  restore_history(editor, true);
  style_target_index = layer_index(state(editor).layers, style_target_id);
  require(style_target_index >= 0 &&
              std::abs(editor.photoLayerEffects(style_target_index).constLast().toMap()
                           .value(QStringLiteral("amount")).toDouble() -
                       effect_amount_after_control) < 0.001,
          QStringLiteral("Redo must still restore the same target's effect edit after rejected operations"));

  // Reopen the combined recipe through the canonical desktop recipe serializer
  // and compare ordinary export pixels. This remains synthetic-media evidence.
  const auto before_reopen_state = recipe_json(editor);
  const auto authored_before_reopen = authored_recipe_projection(state(editor));
  const auto authored_before_reopen_json = serialize_edit_recipe_json(authored_before_reopen);
  const auto parsed_authored_before_reopen = parse_edit_recipe_json(authored_before_reopen_json);
  const auto parsed_authored_before_reopen_json =
      serialize_edit_recipe_json(parsed_authored_before_reopen);
  const auto working_sources_before_reopen = masks_for_set(
      authored_before_reopen.mask_stack, selection_id);
  QImage final_export;
  const auto final_path = temporary.filePath(QStringLiteral("compound-final.png"));
  compare_and_save_export(editor, final_path, &final_export);
  EditorPreviewController reopened;
  reopened.loadAsset(asset_id + QStringLiteral("-reopened"), source_path, {}, {}, {}, {},
                     before_reopen_state, true);
  settle(reopened, QStringLiteral("Canonical recipe reopen"));
  const auto raw_reopened_state = recipe_json(reopened);
  const auto authored_after_reopen = authored_recipe_projection(state(reopened));
  const auto authored_after_reopen_json = serialize_edit_recipe_json(authored_after_reopen);
  const auto parsed_authored_after_reopen = parse_edit_recipe_json(authored_after_reopen_json);
  const auto parsed_authored_after_reopen_json =
      serialize_edit_recipe_json(parsed_authored_after_reopen);
  const bool authored_before_roundtrip_stable =
      parsed_authored_before_reopen_json == authored_before_reopen_json;
  const bool authored_after_roundtrip_stable =
      parsed_authored_after_reopen_json == authored_after_reopen_json;
  const bool authored_canonical_states_equal =
      parsed_authored_before_reopen_json == parsed_authored_after_reopen_json;
  const bool authored_recipe_preserved = authored_after_roundtrip_stable &&
      authored_canonical_states_equal;
  const auto working_sources_after_reopen = masks_for_set(
      authored_after_reopen.mask_stack, selection_id);
  const bool working_selection_identity_preserved = reopened.hasWorkingSelection() &&
      reopened.workingSelectionSetId() == selection_id;
  const bool working_selection_sources_preserved =
      !working_sources_before_reopen.isEmpty() &&
      working_sources_after_reopen == working_sources_before_reopen;
  const bool mask_editing_closed = !reopened.editingLayerMask();
  const bool raw_recipe_equal = raw_reopened_state == before_reopen_state;
  if (!authored_recipe_preserved || !working_selection_identity_preserved ||
      !working_selection_sources_preserved || !mask_editing_closed) {
    std::cerr << "Canonical reopen diagnostic: authored_recipe_equal="
              << authored_recipe_preserved
              << " before_roundtrip_stable=" << authored_before_roundtrip_stable
              << " after_roundtrip_stable=" << authored_after_roundtrip_stable
              << " canonical_states_equal=" << authored_canonical_states_equal
              << " authored_diff_top_level='"
              << differing_serialized_top_level_keys(
                     parsed_authored_before_reopen_json,
                     parsed_authored_after_reopen_json).join(',').toStdString()
              << "' baseline_parse_normalization_top_level='"
              << differing_serialized_top_level_keys(
                     authored_before_reopen_json,
                     parsed_authored_before_reopen_json).join(',').toStdString()
              << "' raw_recipe_equal=" << raw_recipe_equal
              << " working_selection_exists=" << reopened.hasWorkingSelection()
              << " working_selection_id_matches="
              << (reopened.workingSelectionSetId() == selection_id)
              << " working_sources_equal=" << working_selection_sources_preserved
              << " source_count_before=" << working_sources_before_reopen.size()
              << " source_count_after=" << working_sources_after_reopen.size()
              << " mask_editing_closed=" << mask_editing_closed << '\n';
    if (working_sources_before_reopen.size() == working_sources_after_reopen.size()) {
      for (auto index = 0; index < working_sources_before_reopen.size(); ++index) {
        const auto before_source = working_sources_before_reopen.at(index).toMap();
        const auto after_source = working_sources_after_reopen.at(index).toMap();
        if (before_source == after_source) continue;
        std::cerr << "  working-source index=" << index
                  << " before_kind='" << before_source.value(QStringLiteral("kind")).toString().toStdString()
                  << "' after_kind='" << after_source.value(QStringLiteral("kind")).toString().toStdString()
                  << "' differing_fields='"
                  << differing_keys(before_source, after_source).join(',').toStdString()
                  << "'\n";
      }
    }
    if (!authored_recipe_preserved) {
      for (auto index = 0; index < authored_before_reopen.layers.size() &&
          index < authored_after_reopen.layers.size(); ++index) {
        const auto before_layer = authored_before_reopen.layers.at(index).toMap();
        const auto after_layer = authored_after_reopen.layers.at(index).toMap();
        if (before_layer == after_layer) continue;
        std::cerr << "  authored-layer index=" << index << " id='"
                  << before_layer.value(QStringLiteral("id")).toString().toStdString()
                  << "' differing_fields='"
                  << differing_keys(before_layer, after_layer).join(',').toStdString()
                  << "'\n";
      }
    }
  }
  require(authored_recipe_preserved && working_selection_identity_preserved &&
              working_selection_sources_preserved && mask_editing_closed,
          QStringLiteral("Canonical reopen must preserve the combined recipe and free working selection"));
  QImage reopened_export;
  const auto reopened_path = temporary.filePath(QStringLiteral("compound-reopened.png"));
  compare_and_save_export(reopened, reopened_path, &reopened_export);
  require(final_export == reopened_export,
          QStringLiteral("Ordinary flattened exports must match exactly after canonical reopen"));

  std::cout << "compound_selection_layers=1 composed_sources=ellipse,synthetic-object,brush,authored-subject-fixture"
            << " nested_transform=1 clipped_paint_erase=1 algebra=add-subtract-intersect-invert"
            << " ordinary_style_effect_masks=independent shared_mask_move_alt_copy=1"
            << " inherited_lock_rejections=1 stroke_cancel=1 redo_preserved=1 canonical_reopen_export=1"
            << " semantic_segmentation_quality=not_claimed native_app=not_run\n";
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  QGuiApplication application(argc, argv);
  try {
    return run_compound_journey();
  } catch (const std::exception& error) {
    std::cerr << "compound mask/style workflow failed: " << error.what() << '\n';
    return 1;
  }
}
