#include "edit_recipe_state.h"
#include "edit_adjustment_patch.h"
#include "edit_recipe_v13_adapter.h"
#include "edit_recipe_v13_persistence.h"
#include "editor_preview_controller.h"
#include "canonical_composite_mask_alpha.h"
#include "photo_render_utils.h"
#include "photo_vector_path.h"

#include <QColorSpace>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QTemporaryDir>
#include <QThread>
#include <QThreadPool>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>

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
            editor.selectionWorkBusy() || editor.photoExportBusy()) &&
           timer.elapsed() < 60000);
  require(!editor.processing() && !editor.professionalToolsBusy() &&
              !editor.selectionWorkBusy() && !editor.photoExportBusy(),
          qPrintable(QStringLiteral("Controller work did not settle: %1")
                         .arg(editor.status())));
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

QVariantMap layer_by_id(const EditorPreviewController& editor,
                        const QString& id) {
  const auto index = layer_index(editor, id);
  return index < 0 ? QVariantMap{} : state(editor).layers.at(index).toMap();
}

QStringList instance_ids(const EditorPreviewController& editor,
                         const QString& content_id) {
  QStringList ids;
  for (const auto& value : state(editor).layers) {
    const auto layer = value.toMap();
    if (layer.value(QStringLiteral("smartObject")).toMap()
            .value(QStringLiteral("contentId")).toString() == content_id) {
      ids.push_back(layer.value(QStringLiteral("id")).toString());
    }
  }
  return ids;
}

QVariantMap without_content_fields(QVariantMap layer) {
  for (const auto& key : {
           QStringLiteral("smartObject"), QStringLiteral("sourcePath"),
           QStringLiteral("pixelData"), QStringLiteral("pixelDataEncoding"),
           QStringLiteral("smartObjectIntrinsicWidth"),
           QStringLiteral("smartObjectIntrinsicHeight")}) {
    layer.remove(key);
  }
  return layer;
}

QImage render(const QImage& base, const EditRecipeState& edit) {
  const auto image = render_output_image_with_backend(
      base, edit, QColorSpace(QColorSpace::SRgb), QImage::Format_ARGB32,
      RenderBackendPreference::CpuOnly);
  require(!image.isNull(), "CPU layer render returned an empty image.");
  return image;
}

QVariantMap rectangle_path(const QString& subpath_id,
                           double left, double top,
                           double right, double bottom) {
  vector_paths::VectorPathDocument document;
  QString error;
  require(vector_paths::add_subpath(&document, subpath_id, false, &error),
          "Could not create vector subpath.");
  const QVector<QPointF> points{{left, top}, {right, top},
                                {right, bottom}, {left, bottom}};
  for (qsizetype index = 0; index < points.size(); ++index) {
    vector_paths::PathAnchor anchor;
    anchor.id = QStringLiteral("%1-anchor-%2").arg(subpath_id).arg(index);
    anchor.position = points.at(index);
    require(vector_paths::append_anchor(&document, subpath_id, anchor, &error),
            "Could not append vector anchor.");
  }
  require(vector_paths::set_closed(&document, subpath_id, true, &error),
          "Could not close vector subpath.");
  return vector_paths::to_variant(document, &error);
}

QVariantMap open_line_path(const QString& subpath_id,
                           const QPointF& start, const QPointF& end) {
  vector_paths::VectorPathDocument document;
  QString error;
  require(vector_paths::add_subpath(&document, subpath_id, false, &error),
          "Could not create open vector subpath.");
  for (qsizetype index = 0; index < 2; ++index) {
    vector_paths::PathAnchor anchor;
    anchor.id = QStringLiteral("%1-anchor-%2").arg(subpath_id).arg(index);
    anchor.position = index == 0 ? start : end;
    require(vector_paths::append_anchor(&document, subpath_id, anchor, &error),
            "Could not append open vector anchor.");
  }
  return vector_paths::to_variant(document, &error);
}

QByteArray canonical_selection_alpha(EditorPreviewController& editor,
                                     const QImage& source) {
  const auto selection_id = editor.workingSelectionSetId();
  require(!selection_id.isEmpty(), "Working selection must have a stable ID.");
  QVariantList selection_masks;
  for (const auto& value : editor.maskStack()) {
    if (value.toMap().value(QStringLiteral("maskSetId")).toString() ==
        selection_id) {
      selection_masks.push_back(value);
    }
  }
  const auto evaluated = evaluate_canonical_composite_mask_alpha(
      source, selection_masks, 1.0, {1.0, 1.0, 1.0});
  require(evaluated.valid,
          "Working selection must evaluate through canonical mask alpha.");
  return evaluated.alpha_mask;
}

QPoint source_pixel_for_path_point(EditorPreviewController& editor,
                                   int layer_index, const QPointF& path_point,
                                   const QSize& source_size,
                                   QPointF* view_point) {
  const auto mapped = editor.mapPhotoVectorPathCoordinate(
      layer_index, path_point.x(), path_point.y(), true);
  require(mapped.value(QStringLiteral("valid")).toBool(),
          "Transformed path point must map to the cropped canvas.");
  const QPointF view(mapped.value(QStringLiteral("x")).toDouble(),
                     mapped.value(QStringLiteral("y")).toDouble());
  if (view_point != nullptr) *view_point = view;
  const auto source = editor.mapCanvasCoordinate(
      view.x(), view.y(), false, QStringLiteral("source"));
  require(source.value(QStringLiteral("valid")).toBool(),
          "Cropped canvas point must map back to source coordinates.");
  const auto x = qBound(0, qRound(source.value(QStringLiteral("x")).toDouble() *
                                  (source_size.width() - 1)),
                        source_size.width() - 1);
  const auto y = qBound(0, qRound(source.value(QStringLiteral("y")).toDouble() *
                                  (source_size.height() - 1)),
                        source_size.height() - 1);
  return {x, y};
}

int alpha_at(const QByteArray& alpha, const QSize& size, const QPoint& point) {
  const auto offset = point.y() * size.width() + point.x();
  if (offset < 0 || offset >= alpha.size()) return -1;
  return static_cast<unsigned char>(alpha.at(offset));
}

std::string layers_cache_identity(
    const DesktopRenderPlanResult& result) {
  if (!result.ok() || !result.plan.has_value()) return {};
  const auto stage = std::find_if(
      result.plan->stages.cbegin(), result.plan->stages.cend(),
      [](const auto& value) {
        return value.kind == photo_editor::pipeline::RenderStageKind::Layers;
      });
  return stage == result.plan->stages.cend() ? std::string{} : stage->cache_identity;
}

}  // namespace

int main(int argc, char** argv) {
  QTemporaryDir runtime;
  if (!runtime.isValid()) return 1;
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", runtime.filePath("app-data").toUtf8());
  qputenv("PHOTO_EDITOR_CACHE_DIR", runtime.filePath("cache").toUtf8());
  qputenv("PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE", "1");
  QGuiApplication application(argc, argv);

  struct DrainWorkers {
    ~DrainWorkers() { QThreadPool::globalInstance()->waitForDone(60000); }
  } drain_workers;

  try {
    QImage base(240, 100, QImage::Format_RGB32);
    base.setColorSpace(QColorSpace(QColorSpace::SRgb));
    base.fill(QColor(QStringLiteral("#536579")));
    const auto base_path = runtime.filePath(QStringLiteral("base.png"));
    require(base.save(base_path), "Could not save base image fixture.");

    QImage original(60, 60, QImage::Format_RGB32);
    original.setColorSpace(QColorSpace(QColorSpace::SRgb));
    original.fill(QColor(QStringLiteral("#d32918")));
    const auto original_path = runtime.filePath(QStringLiteral("original.jpg"));
    require(original.save(original_path, "JPEG", 96),
            "Could not save original JPEG fixture.");

    QImage replacement(30, 90, QImage::Format_RGB32);
    replacement.setColorSpace(QColorSpace(QColorSpace::SRgb));
    replacement.fill(QColor(QStringLiteral("#1745db")));
    const auto replacement_path = runtime.filePath(QStringLiteral("replacement.jpg"));
    require(replacement.save(replacement_path, "JPEG", 96),
            "Could not save replacement JPEG fixture.");

    EditorPreviewController editor;
    editor.loadAsset(QStringLiteral("professional-tools-base"), base_path,
                     {}, {}, {}, {}, {});
    settle(editor);
    require(editor.addSourceLayer(original_path, QStringLiteral("Product")),
            "Could not add source layer.");
    const auto original_id = state(editor).layers.first().toMap()
                                 .value(QStringLiteral("id")).toString();
    require(!original_id.isEmpty(), "Source layer needs a stable ID.");

    editor.addMask(QStringLiteral("rectangle"));
    editor.setActiveMaskFeather(0.0);
    editor.setActiveMaskCenterX(0.5);
    editor.setActiveMaskCenterY(0.5);
    editor.setActiveMaskRadiusX(0.5);
    editor.setActiveMaskRadiusY(0.5);
    settle(editor);
    require(editor.applyWorkingSelectionToLayer(layer_index(editor, original_id)),
            "Could not attach source layer mask.");
    settle(editor);
    require(editor.setPhotoLayerTransform(layer_index(editor, original_id),
                                          10, 20, 1.0, 0.0),
            "Could not set initial source placement.");
    require(editor.setPhotoLayerAdjustmentValue(layer_index(editor, original_id),
                                                QStringLiteral("exposure"), 0.25),
            "Could not set source color adjustment.");
    require(editor.addPhotoLayerInvert(layer_index(editor, original_id)),
            "Could not add source layer effect.");
    settle(editor);

    const auto before_convert = layer_by_id(editor, original_id);
    const auto mask_id = before_convert.value(QStringLiteral("selectionMaskSetId")).toString();
    require(!mask_id.isEmpty(), "Source fixture must carry a real layer mask.");
    require(editor.convertPhotoLayerToSmartObject(layer_index(editor, original_id), false),
            "Smart Object conversion did not start.");
    settle(editor);
    auto original_layer = layer_by_id(editor, original_id);
    auto descriptor = original_layer.value(QStringLiteral("smartObject")).toMap();
    const auto shared_content_id = descriptor.value(QStringLiteral("contentId")).toString();
    const auto original_content_hash = descriptor.value(QStringLiteral("sourceSha256")).toString();
    require(!descriptor.isEmpty() && shared_content_id.size() == 36 &&
                descriptor.value(QStringLiteral("policy")).toString() == QStringLiteral("embedded") &&
                descriptor.value(QStringLiteral("fileName")).toString() == QStringLiteral("original.jpg") &&
                original_layer.value(QStringLiteral("sourcePath")).toString().isEmpty(),
            "Embedded conversion must store the JPEG content and release its source path.");
    require(QFile::remove(original_path),
            "Original source should be removable after embedded conversion.");

    const auto initial_render = render(base, state(editor));
    require(initial_render.pixelColor(40, 50) != base.pixelColor(40, 50),
            "Embedded JPEG should render after removing its original file.");

    require(editor.duplicatePhotoSmartObject(layer_index(editor, original_id)),
            "First shared instance duplication failed.");
    settle(editor);
    QString copy_one_id;
    for (const auto& id : instance_ids(editor, shared_content_id)) {
      if (id != original_id) copy_one_id = id;
    }
    require(!copy_one_id.isEmpty(), "First duplicate should share source content.");
    require(editor.duplicatePhotoSmartObject(layer_index(editor, original_id)),
            "Second shared instance duplication failed.");
    settle(editor);
    QString copy_two_id;
    for (const auto& id : instance_ids(editor, shared_content_id)) {
      if (id != original_id && id != copy_one_id) copy_two_id = id;
    }
    require(!copy_two_id.isEmpty() && instance_ids(editor, shared_content_id).size() == 3,
            "Three placements should share one immutable source identity.");

    require(editor.setPhotoLayerTransform(layer_index(editor, copy_one_id),
                                          90, 20, 1.0, 0.0),
            "Could not position second Smart Object instance.");
    require(editor.setPhotoLayerTransform(layer_index(editor, copy_two_id),
                                          170, 20, 1.0, 0.0),
            "Could not position third Smart Object instance.");
    settle(editor);

    const auto before_independent_layer = layer_by_id(editor, copy_two_id);
    const auto before_independent_descriptor =
        before_independent_layer.value(QStringLiteral("smartObject")).toMap();
    require(editor.makePhotoSmartObjectIndependent(layer_index(editor, copy_two_id)),
            "Make Independent did not apply.");
    settle(editor);
    const auto independent_layer = layer_by_id(editor, copy_two_id);
    const auto independent_descriptor = independent_layer.value(QStringLiteral("smartObject")).toMap();
    const auto independent_content_id = independent_descriptor.value(QStringLiteral("contentId")).toString();
    auto expected_independent_descriptor = before_independent_descriptor;
    expected_independent_descriptor.insert(QStringLiteral("contentId"), independent_content_id);
    require(independent_content_id != shared_content_id &&
                independent_descriptor.value(QStringLiteral("sourceSha256")) == original_content_hash &&
                independent_descriptor == expected_independent_descriptor &&
                independent_layer.value(QStringLiteral("smartObjectInstanceId")) ==
                    before_independent_layer.value(QStringLiteral("smartObjectInstanceId")),
            "Make Independent must change only logical content identity.");
    require(instance_ids(editor, shared_content_id).size() == 2,
            "Independent placement must leave exactly two shared instances.");

    require(editor.setPhotoLayerLocked(layer_index(editor, copy_one_id), true),
            "Could not lock shared sibling fixture.");
    settle(editor);
    const auto locked_sibling_state = state(editor);
    require(editor.replacePhotoSmartObjectContents(layer_index(editor, original_id), replacement_path),
            "Locked-sibling replacement work should be admitted for atomic validation.");
    settle(editor);
    require(state(editor) == locked_sibling_state &&
                editor.status().contains(QStringLiteral("locked shared instance")),
            "Replacement must fail atomically when any shared sibling is locked.");

    require(editor.setPhotoLayerLocked(layer_index(editor, copy_one_id), false),
            "Could not unlock shared sibling fixture.");
    settle(editor);
    require(editor.setPhotoLayerLocked(layer_index(editor, original_id), true),
            "Could not lock selected Smart Object fixture.");
    settle(editor);
    const auto locked_selection_state = state(editor);
    require(!editor.replacePhotoSmartObjectContents(layer_index(editor, original_id), replacement_path) &&
                state(editor) == locked_selection_state,
            "Locked selected instance must reject Replace Contents without mutation.");
    require(editor.setPhotoLayerLocked(layer_index(editor, original_id), false),
            "Could not unlock selected Smart Object fixture.");
    settle(editor);

    const auto missing_path = runtime.filePath(QStringLiteral("missing-replacement.jpg"));
    const auto before_missing_replace = state(editor);
    require(editor.replacePhotoSmartObjectContents(layer_index(editor, original_id), missing_path),
            "Missing replacement should reach worker-side validation.");
    settle(editor);
    require(state(editor) == before_missing_replace &&
                editor.status().contains(QStringLiteral("missing"), Qt::CaseInsensitive),
            "Missing replacement content must leave shared instances and history state untouched.");

    const auto before_replace = state(editor);
    const auto first_outer_before = layer_by_id(editor, original_id);
    const auto second_outer_before = layer_by_id(editor, copy_one_id);
    const auto independent_before = layer_by_id(editor, copy_two_id);
    const auto before_replace_render = render(base, before_replace);
    const auto base_only_state = [&before_replace] {
      auto value = before_replace;
      value.layers.clear();
      return value;
    }();
    const auto base_only_render = render(base, base_only_state);

    require(editor.replacePhotoSmartObjectContents(layer_index(editor, original_id), replacement_path),
            "Replace Contents did not start.");
    settle(editor);
    require(QFile::remove(replacement_path),
            "Replacement source should be removable after embedded replacement.");
    const auto replaced = state(editor);
    const auto replaced_first = layer_by_id(editor, original_id);
    const auto replaced_second = layer_by_id(editor, copy_one_id);
    const auto replaced_independent = layer_by_id(editor, copy_two_id);
    const auto replaced_descriptor = replaced_first.value(QStringLiteral("smartObject")).toMap();
    require(replaced_descriptor.value(QStringLiteral("contentId")).toString() == shared_content_id &&
                replaced_descriptor.value(QStringLiteral("sourceSha256")).toString() != original_content_hash &&
                replaced_descriptor.value(QStringLiteral("intrinsicWidth")).toInt() == 30 &&
                replaced_descriptor.value(QStringLiteral("intrinsicHeight")).toInt() == 90 &&
                replaced_second.value(QStringLiteral("smartObject")).toMap() == replaced_descriptor &&
                replaced_independent.value(QStringLiteral("smartObject")).toMap()
                    .value(QStringLiteral("sourceSha256")).toString() == original_content_hash &&
                replaced_independent.value(QStringLiteral("smartObject")).toMap()
                    .value(QStringLiteral("contentId")).toString() == independent_content_id,
            "Replacement must update both shared placements and preserve independent source content.");
    require(without_content_fields(replaced_first) == without_content_fields(first_outer_before) &&
                without_content_fields(replaced_second) == without_content_fields(second_outer_before) &&
                without_content_fields(replaced_independent) == without_content_fields(independent_before),
            "Replace Contents changed outer IDs, transforms, masks, color adjustments, or effects.");
    require(replaced_first.value(QStringLiteral("pixelWidth")).toInt() == 60 &&
                replaced_first.value(QStringLiteral("pixelHeight")).toInt() == 60 &&
                replaced_first.value(QStringLiteral("selectionMaskSetId")).toString() == mask_id,
            "Replacement must preserve its placed frame and attached mask identity.");

    const auto after_replace_render = render(base, replaced);
    require(after_replace_render.pixelColor(40, 50) != before_replace_render.pixelColor(40, 50) &&
                after_replace_render.pixelColor(120, 50) == after_replace_render.pixelColor(40, 50) &&
                after_replace_render.pixelColor(200, 50) == before_replace_render.pixelColor(200, 50),
            "Rendered replacement must update shared frames while the independent frame stays unchanged.");
    require(after_replace_render.pixelColor(15, 50) == base_only_render.pixelColor(15, 50) &&
                after_replace_render.pixelColor(40, 50) != base_only_render.pixelColor(40, 50),
            "Replacement aspect ratio must fit inside the old square frame with transparent side margins.");

    editor.undo();
    settle(editor);
    const auto undone = state(editor);
    require(layer_by_id(editor, original_id).value(QStringLiteral("smartObject")).toMap()
                    .value(QStringLiteral("sourceSha256")).toString() == original_content_hash &&
                layer_by_id(editor, copy_two_id).value(QStringLiteral("smartObject")).toMap()
                    .value(QStringLiteral("contentId")).toString() == independent_content_id &&
                render(base, undone) == before_replace_render,
            "Undo must restore shared source pixels without merging the independent instance.");
    editor.redo();
    settle(editor);
    require(render(base, state(editor)) == after_replace_render,
            "Redo must restore shared replacement pixels exactly.");

    QImage blend_fixture(240, 100, QImage::Format_RGB32);
    blend_fixture.setColorSpace(QColorSpace(QColorSpace::SRgb));
    blend_fixture.fill(Qt::black);
    for (int y = 0; y < blend_fixture.height(); ++y) {
      for (int x = blend_fixture.width() / 2; x < blend_fixture.width(); ++x) {
        blend_fixture.setPixelColor(x, y, Qt::white);
      }
    }
    const auto blend_path = runtime.filePath(QStringLiteral("blend-fixture.png"));
    require(blend_fixture.save(blend_path) &&
                editor.addSourceLayer(blend_path, QStringLiteral("Blend If fixture")),
            "Could not add Blend If source fixture.");
    settle(editor);
    const auto blend_id = state(editor).layers.first().toMap()
                              .value(QStringLiteral("id")).toString();
    const auto blend_index = layer_index(editor, blend_id);
    const auto blend_baseline = render(base, state(editor));
    auto blend_settings = editor.photoLayerBlendIf(blend_index);
    blend_settings.insert(QStringLiteral("enabled"), true);
    blend_settings.insert(QStringLiteral("channel"), QStringLiteral("red"));
    blend_settings.insert(QStringLiteral("sourceBlackLow"), 0.1);
    blend_settings.insert(QStringLiteral("sourceBlackHigh"), 0.6);
    require(editor.setPhotoLayerBlendIf(blend_index, blend_settings),
            "Could not enable source-channel Blend If.");
    settle(editor);
    const auto blend_active_state = state(editor);
    const auto blend_active = render(base, blend_active_state);
    require(blend_active.pixelColor(5, 50) != blend_baseline.pixelColor(5, 50) &&
                blend_active.pixelColor(200, 50) == blend_baseline.pixelColor(200, 50),
            "Blend If must render a source-dependent black-channel gate.");

    auto invalid_channel = blend_settings;
    invalid_channel.insert(QStringLiteral("channel"), QStringLiteral("magenta"));
    require(!editor.setPhotoLayerBlendIf(blend_index, invalid_channel) &&
                state(editor) == blend_active_state,
            "Blend If must reject unsupported channel values without mutation.");
    auto invalid_range = blend_settings;
    invalid_range.insert(QStringLiteral("sourceBlackLow"), 0.8);
    invalid_range.insert(QStringLiteral("sourceBlackHigh"), 0.2);
    require(!editor.setPhotoLayerBlendIf(blend_index, invalid_range) &&
                state(editor) == blend_active_state,
            "Blend If must reject reversed thresholds without mutation.");

    auto blend_bypass = blend_settings;
    blend_bypass.insert(QStringLiteral("enabled"), false);
    require(editor.setPhotoLayerBlendIf(blend_index, blend_bypass),
            "Could not bypass Blend If.");
    settle(editor);
    require(render(base, state(editor)) == blend_baseline,
            "Bypassing Blend If must restore the exact ungated compositor output.");
    require(editor.setPhotoLayerLocked(blend_index, true),
            "Could not lock Blend If fixture.");
    settle(editor);
    const auto locked_blend_state = state(editor);
    require(!editor.setPhotoLayerBlendIf(blend_index, blend_settings) &&
                state(editor) == locked_blend_state,
            "Locked layer must reject Blend If changes without mutation.");
    require(editor.setPhotoLayerLocked(layer_index(editor, blend_id), false),
            "Could not unlock Blend If fixture.");
    settle(editor);
    require(editor.setPhotoLayerBlendIf(layer_index(editor, blend_id), blend_settings),
            "Could not restore active Blend If for persistence.");
    settle(editor);

    const auto vector_before = render(base, state(editor));
    const auto path = rectangle_path(QStringLiteral("product-mark"), 0.05, 0.05,
                                     0.30, 0.30);
    require(editor.addPhotoVectorPath(path, QStringLiteral("#12e060"),
                                      QStringLiteral("#ffffff"), 1.5),
            "Could not create a vector path layer.");
    settle(editor);
    const auto vector_id = state(editor).layers.first().toMap()
                               .value(QStringLiteral("id")).toString();
    const auto vector_created = layer_by_id(editor, vector_id);
    const auto vector_created_image = render(base, state(editor));
    require(!vector_created.value(QStringLiteral("vectorPath")).toMap().isEmpty() &&
                vector_created_image.pixelColor(40, 15) !=
                    vector_before.pixelColor(40, 15),
            "Editable vector path must contribute rendered pixels.");

    const auto shifted_path = rectangle_path(QStringLiteral("product-mark"), 0.45, 0.05,
                                             0.70, 0.30);
    require(editor.setPhotoLayerVectorPath(layer_index(editor, vector_id), shifted_path,
        {{QStringLiteral("fill"), QStringLiteral("#ee20c0")},
         {QStringLiteral("stroke"), QStringLiteral("#101010")},
         {QStringLiteral("strokeWidth"), 2.0}}),
        "Could not edit vector path geometry and appearance.");
    settle(editor);
    const auto vector_edited_state = state(editor);
    const auto vector_edited = render(base, vector_edited_state);
    require(layer_by_id(editor, vector_id).value(QStringLiteral("vectorFillColor")).toString() ==
                    QStringLiteral("#ee20c0") &&
                vector_edited.pixelColor(40, 15) == vector_before.pixelColor(40, 15) &&
                vector_edited.pixelColor(40, 15) != vector_created_image.pixelColor(40, 15) &&
                vector_edited.pixelColor(140, 15) != vector_before.pixelColor(140, 15),
            "Vector path edits must persist and change the corresponding rendered region.");
    const auto invalid_path_state = state(editor);
    require(!editor.setPhotoLayerVectorPath(layer_index(editor, vector_id), shifted_path,
        {{QStringLiteral("fill"), QStringLiteral("#ee20c0")},
         {QStringLiteral("stroke"), QStringLiteral("#101010")},
         {QStringLiteral("strokeWidth"), 5000.0}}) &&
                state(editor) == invalid_path_state,
            "Vector path appearance guards must reject invalid stroke widths atomically.");

    require(editor.setPhotoLayerTransform(layer_index(editor, vector_id),
                                          30, 15, 0.8, 22.0),
            "Could not move and rotate the editable path layer.");
    settle(editor);
    const auto path_point = QPointF(0.57, 0.16);
    const auto path_to_view = editor.mapPhotoVectorPathCoordinate(
        layer_index(editor, vector_id), path_point.x(), path_point.y(), true);
    require(path_to_view.value(QStringLiteral("valid")).toBool(),
            "Transformed vector path point must map to the canvas view.");
    const auto view_to_path = editor.mapPhotoVectorPathCoordinate(
        layer_index(editor, vector_id),
        path_to_view.value(QStringLiteral("x")).toDouble(),
        path_to_view.value(QStringLiteral("y")).toDouble(), false);
    require(view_to_path.value(QStringLiteral("valid")).toBool() &&
                std::abs(view_to_path.value(QStringLiteral("x")).toDouble() -
                         path_point.x()) < 1e-6 &&
                std::abs(view_to_path.value(QStringLiteral("y")).toDouble() -
                         path_point.y()) < 1e-6,
            "Vector path coordinate mapping must round-trip after layer translation, scale and rotation.");

    const auto layers_before_path_selection = state(editor).layers;
    if (editor.hasWorkingSelection()) {
      require(editor.deselectWorkingSelection(),
              "Could not clear the earlier mask selection before path selection.");
      settle(editor);
    }
    require(editor.selectPhotoVectorPath(shifted_path, QStringLiteral("add"),
                                         layer_index(editor, vector_id)),
            "Could not create a selection from the transformed vector path.");
    settle(editor);
    const auto path_selection_state = state(editor);
    require(editor.hasWorkingSelection() &&
                !editor.workingSelectionSetId().isEmpty() &&
                path_selection_state.layers == layers_before_path_selection &&
                editor.photoLayerCount() == layers_before_path_selection.size(),
            "Path-to-selection must preserve the transformed layers and create no automatic layer.");

    QImage crop_source(320, 180, QImage::Format_RGB32);
    crop_source.setColorSpace(QColorSpace(QColorSpace::SRgb));
    crop_source.fill(QColor(QStringLiteral("#526779")));
    const auto crop_source_path = runtime.filePath(QStringLiteral("crop-path-source.png"));
    require(crop_source.save(crop_source_path),
            "Could not save crop-aware path-selection fixture.");
    const auto crop_path_a = rectangle_path(
        QStringLiteral("crop-selection-a"), 80.0 / 320.0, 60.0 / 180.0,
        120.0 / 320.0, 100.0 / 180.0);
    const auto crop_path_b = rectangle_path(
        QStringLiteral("crop-selection-b"), 80.0 / 320.0, 100.0 / 180.0,
        120.0 / 320.0, 140.0 / 180.0);
    const QString crop_parent_id = QStringLiteral("crop-path-parent");
    const QString crop_layer_id = QStringLiteral("crop-path-layer");
    QVariantMap crop_parent{{QStringLiteral("id"), crop_parent_id},
                            {QStringLiteral("kind"), QStringLiteral("group")},
                            {QStringLiteral("name"), QStringLiteral("Rotated parent")},
                            {QStringLiteral("canvasWidth"), 320},
                            {QStringLiteral("canvasHeight"), 180},
                            {QStringLiteral("x"), 0},
                            {QStringLiteral("y"), 0},
                            {QStringLiteral("scale"), 1.0},
                            {QStringLiteral("rotation"), 90.0},
                            {QStringLiteral("enabled"), true},
                            {QStringLiteral("opacity"), 1.0}};
    QVariantMap crop_vector_layer{{QStringLiteral("id"), crop_layer_id},
                                  {QStringLiteral("kind"), QStringLiteral("pixel")},
                                  {QStringLiteral("name"), QStringLiteral("Crop path")},
                                  {QStringLiteral("pixelWidth"), 320},
                                  {QStringLiteral("pixelHeight"), 180},
                                  {QStringLiteral("canvasWidth"), 320},
                                  {QStringLiteral("canvasHeight"), 180},
                                  {QStringLiteral("x"), 0},
                                  {QStringLiteral("y"), 0},
                                  {QStringLiteral("scale"), 1.0},
                                  {QStringLiteral("rotation"), 0.0},
                                  {QStringLiteral("coordinateSpace"), QStringLiteral("source-area")},
                                  {QStringLiteral("coordinateArea"), QVariantMap{
                                      {QStringLiteral("x"), 0.0},
                                      {QStringLiteral("y"), 0.0},
                                      {QStringLiteral("width"), 1.0},
                                      {QStringLiteral("height"), 1.0}}},
                                  {QStringLiteral("parentId"), crop_parent_id},
                                  {QStringLiteral("vectorPath"), crop_path_a},
                                  {QStringLiteral("vectorFillColor"), QStringLiteral("#ed20c2")},
                                  {QStringLiteral("vectorStrokeColor"), QStringLiteral("#ed20c2")},
                                  {QStringLiteral("vectorStrokeWidth"), 0.0},
                                  {QStringLiteral("enabled"), true},
                                  {QStringLiteral("opacity"), 1.0}};
    EditRecipeState crop_selection_recipe;
    crop_selection_recipe.crop_rect_x = 20.0 / 320.0;
    crop_selection_recipe.crop_rect_y = 30.0 / 180.0;
    crop_selection_recipe.crop_rect_width = 200.0 / 320.0;
    crop_selection_recipe.crop_rect_height = 100.0 / 180.0;
    crop_selection_recipe.layers = {crop_parent, crop_vector_layer};
    EditorPreviewController crop_editor;
    crop_editor.loadAsset(QStringLiteral("crop-aware-vector-selection"),
                          crop_source_path, {}, {}, {}, {},
                          serialize_edit_recipe_json(crop_selection_recipe));
    settle(crop_editor);
    const auto crop_index = layer_index(crop_editor, crop_layer_id);
    require(crop_index >= 0 &&
                crop_editor.cropRectX() == crop_selection_recipe.crop_rect_x &&
                crop_editor.cropRectY() == crop_selection_recipe.crop_rect_y &&
                crop_editor.cropRectWidth() == crop_selection_recipe.crop_rect_width &&
                crop_editor.cropRectHeight() == crop_selection_recipe.crop_rect_height,
            "Crop-aware path fixture must retain the off-center crop and child identity.");

    QPointF crop_target_view;
    const auto crop_target_source = source_pixel_for_path_point(
        crop_editor, crop_index, QPointF(100.0 / 320.0, 80.0 / 180.0),
        crop_source.size(), &crop_target_view);
    require(std::abs(crop_target_source.x() - 120) <= 2 &&
                std::abs(crop_target_source.y() - 60) <= 2,
            "Crop and parent rotation should map local point (100,80) to source near (120,60), not full-source (170,30).");
    const auto crop_rect = crop_rect_for_image(crop_source.size(), crop_selection_recipe);
    auto crop_base_only_state = crop_selection_recipe;
    crop_base_only_state.layers.clear();
    const auto cropped_source = crop_source.copy(crop_rect);
    const RenderMaskOptions crop_mask_options{
        .source_geometry = DesktopSourceGeometry{crop_source.size(), crop_rect},
    };
    const auto crop_base_render = render_output_image_with_backend(
        cropped_source, crop_base_only_state, QColorSpace(QColorSpace::SRgb),
        QImage::Format_ARGB32, RenderBackendPreference::CpuOnly, nullptr,
        nullptr, crop_mask_options);
    const auto crop_path_render = render_output_image_with_backend(
        cropped_source, state(crop_editor), QColorSpace(QColorSpace::SRgb),
        QImage::Format_ARGB32, RenderBackendPreference::CpuOnly, nullptr,
        nullptr, crop_mask_options);
    if (crop_base_render.isNull() || crop_path_render.isNull() ||
        crop_base_render.size() != crop_rect.size() ||
        crop_path_render.size() != crop_rect.size()) {
      std::cerr << "crop render diagnostics: rect=" << crop_rect.x() << ',' << crop_rect.y()
                << ' ' << crop_rect.width() << 'x' << crop_rect.height()
                << " base=" << crop_base_render.width() << 'x' << crop_base_render.height()
                << " path=" << crop_path_render.width() << 'x' << crop_path_render.height()
                << " model=" << state(crop_editor).photographic_rendering_model.toStdString()
                << " recipe=" << crop_selection_recipe.photographic_rendering_model.toStdString()
                << '\n';
    }
    require(!crop_base_render.isNull() && !crop_path_render.isNull() &&
                crop_base_render.size() == crop_rect.size() &&
                crop_path_render.size() == crop_rect.size(),
            "The cropped-image backend contract must retain full-source crop geometry before selecting its path.");
    const auto rendered_target = QPoint(
        qBound(0, qRound(crop_target_view.x() * (crop_path_render.width() - 1)),
               crop_path_render.width() - 1),
        qBound(0, qRound(crop_target_view.y() * (crop_path_render.height() - 1)),
               crop_path_render.height() - 1));
    require(crop_path_render.pixelColor(rendered_target) !=
                crop_base_render.pixelColor(rendered_target),
            "Mapped path center must land on the vector's true cropped rendered target pixel.");

    require(crop_editor.selectPhotoVectorPath(crop_path_a, QStringLiteral("add"),
                                              crop_index),
            "Could not select the closed path under off-center crop and group rotation.");
    settle(crop_editor);
    const auto crop_alpha_a = canonical_selection_alpha(crop_editor, crop_source);
    require(alpha_at(crop_alpha_a, crop_source.size(), crop_target_source) > 220 &&
                alpha_at(crop_alpha_a, crop_source.size(), QPoint(170, 30)) == 0,
            "Canonical selection alpha must align with the mapped/rendered source point and reject old full-source placement.");

    const auto crop_second_source = source_pixel_for_path_point(
        crop_editor, crop_index, QPointF(100.0 / 320.0, 120.0 / 180.0),
        crop_source.size(), nullptr);
    require(crop_editor.selectPhotoVectorPath(crop_path_b,
                                              QStringLiteral("replace"), crop_index),
            "Legacy replace operation alias should be accepted as additive selection.");
    settle(crop_editor);
    const auto crop_alpha_union = canonical_selection_alpha(crop_editor, crop_source);
    require(alpha_at(crop_alpha_union, crop_source.size(), crop_target_source) > 220 &&
                alpha_at(crop_alpha_union, crop_source.size(), crop_second_source) > 220,
            "The replace alias must preserve old selection coverage and add the new transformed contour.");
    for (qsizetype offset = 0; offset < crop_alpha_a.size(); ++offset) {
      if (static_cast<unsigned char>(crop_alpha_a.at(offset)) >
          static_cast<unsigned char>(crop_alpha_union.at(offset))) {
        throw std::runtime_error(
            "The replace alias reduced existing canonical selection alpha.");
      }
    }

    const auto crop_state_before_open_path = state(crop_editor);
    const auto alpha_before_open_path = canonical_selection_alpha(
        crop_editor, crop_source);
    const auto open_only_path = open_line_path(
        QStringLiteral("open-only-selection"), QPointF(0.2, 0.3),
        QPointF(0.4, 0.45));
    require(!crop_editor.selectPhotoVectorPath(open_only_path,
                                               QStringLiteral("replace"), crop_index) &&
                state(crop_editor) == crop_state_before_open_path &&
                canonical_selection_alpha(crop_editor, crop_source) ==
                    alpha_before_open_path,
            "Open-only paths must be rejected without replacing the current selection or changing recipe state.");

    require(editor.addAdjustmentLayer(QStringLiteral("Adjustment Blend If")),
            "Could not add adjustment layer for Blend If persistence.");
    settle(editor);
    const auto adjustment_id = state(editor).layers.first().toMap()
                                  .value(QStringLiteral("id")).toString();
    const auto adjustment_without_gate = layer_by_id(editor, adjustment_id);
    auto adjustment_gate = editor.photoLayerBlendIf(layer_index(editor, adjustment_id));
    adjustment_gate.insert(QStringLiteral("enabled"), true);
    adjustment_gate.insert(QStringLiteral("channel"), QStringLiteral("red"));
    adjustment_gate.insert(QStringLiteral("sourceBlackLow"), 0.15);
    adjustment_gate.insert(QStringLiteral("sourceBlackHigh"), 0.55);
    require(editor.setPhotoLayerBlendIf(layer_index(editor, adjustment_id),
                                        adjustment_gate),
            "Could not enable adjustment-layer Blend If.");
    settle(editor);

    require(editor.addLayerGroup(QStringLiteral("Group Blend If")),
            "Could not add group layer for Blend If persistence.");
    settle(editor);
    const auto group_id = state(editor).layers.first().toMap()
                              .value(QStringLiteral("id")).toString();
    const auto group_without_gate = layer_by_id(editor, group_id);
    auto group_gate = editor.photoLayerBlendIf(layer_index(editor, group_id));
    group_gate.insert(QStringLiteral("enabled"), true);
    group_gate.insert(QStringLiteral("channel"), QStringLiteral("blue"));
    group_gate.insert(QStringLiteral("sourceWhiteLow"), 0.45);
    group_gate.insert(QStringLiteral("sourceWhiteHigh"), 0.85);
    require(editor.setPhotoLayerBlendIf(layer_index(editor, group_id), group_gate),
            "Could not enable group-layer Blend If.");
    settle(editor);

    EditRecipeState blend_plan_baseline_state;
    blend_plan_baseline_state.layers = {group_without_gate,
                                        adjustment_without_gate};
    DesktopRenderPlanRequest blend_plan_request;
    blend_plan_request.recipe_id =
        QStringLiteral("professional-tools-blend-if-cache");
    blend_plan_request.recipe_payload_digest =
        QStringLiteral("fixed-blend-if-cache-probe-v1");
    blend_plan_request.source_id = QStringLiteral("professional-tools-base");
    blend_plan_request.decoder_provenance =
        QStringLiteral("raster:professional-tools-workflow-v1");
    blend_plan_request.source_width =
        static_cast<std::uint32_t>(base.width());
    blend_plan_request.source_height =
        static_cast<std::uint32_t>(base.height());
    const auto no_gate_plan = compile_desktop_render_plan_with_derived_assets(
        blend_plan_baseline_state, blend_plan_request);
    auto disabled_gate_state = blend_plan_baseline_state;
    auto disabled_adjustment_layer = disabled_gate_state.layers.at(1).toMap();
    auto disabled_adjustment_gate = adjustment_gate;
    disabled_adjustment_gate.insert(QStringLiteral("enabled"), false);
    disabled_adjustment_layer.insert(QStringLiteral("blendIf"), disabled_adjustment_gate);
    disabled_gate_state.layers[1] = disabled_adjustment_layer;
    auto disabled_group_layer = disabled_gate_state.layers.at(0).toMap();
    auto disabled_group_gate = group_gate;
    disabled_group_gate.insert(QStringLiteral("enabled"), false);
    disabled_group_layer.insert(QStringLiteral("blendIf"), disabled_group_gate);
    disabled_gate_state.layers[0] = disabled_group_layer;
    const auto disabled_gate_plan = compile_desktop_render_plan_with_derived_assets(
        disabled_gate_state, blend_plan_request);
    auto adjustment_gate_state = blend_plan_baseline_state;
    auto adjustment_gate_layer = adjustment_gate_state.layers.at(1).toMap();
    adjustment_gate_layer.insert(QStringLiteral("blendIf"), adjustment_gate);
    adjustment_gate_state.layers[1] = adjustment_gate_layer;
    const auto adjustment_gate_plan = compile_desktop_render_plan_with_derived_assets(
        adjustment_gate_state, blend_plan_request);
    auto group_gate_state = blend_plan_baseline_state;
    auto group_gate_layer = group_gate_state.layers.at(0).toMap();
    group_gate_layer.insert(QStringLiteral("blendIf"), group_gate);
    group_gate_state.layers[0] = group_gate_layer;
    const auto group_gate_plan = compile_desktop_render_plan_with_derived_assets(
        group_gate_state, blend_plan_request);
    const auto no_gate_identity = layers_cache_identity(no_gate_plan);
    require(no_gate_plan.ok() && disabled_gate_plan.ok() &&
                photo_editor::pipeline::serialize_render_plan(*no_gate_plan.plan) ==
                    photo_editor::pipeline::serialize_render_plan(*disabled_gate_plan.plan) &&
                !no_gate_identity.empty() &&
                layers_cache_identity(adjustment_gate_plan) != no_gate_identity &&
                layers_cache_identity(group_gate_plan) != no_gate_identity,
            "Disabled gates must keep the no-Blend-If plan byte-stable, while enabled adjustment and group gates change Layers-stage cache identity.");

    const auto final_state = state(editor);
    RecipeAssetStore assets(runtime.filePath(QStringLiteral("recipe-assets")));
    const auto canonical = serialize_desktop_recipe_as_canonical_v13_with_assets(
        final_state,
        {.recipe_id = "professional-tools-workflow",
         .source_id = "professional-tools-base",
         .decoder_provenance = "raster:professional-tools-workflow-v1",
         .raw_source = false},
        assets);
    if (!canonical.ok()) {
      for (const auto& error : canonical.errors) {
        std::cerr << error.path << ": " << error.message << '\n';
      }
    }
    require(canonical.ok() && canonical.stored_asset_count > 0,
            "Canonical v13 serialization must store immutable layer and mask payloads.");
    const auto projected = project_canonical_v13_document_to_desktop_state_with_assets(
        canonical.document, assets);
    if (!projected.ok()) {
      for (const auto& error : projected.errors) {
        std::cerr << error.path << ": " << error.message << '\n';
      }
    }
    require(projected.ok(), "Canonical v13 content assets must project back to desktop layers.");
    const auto normalized_projected = parse_edit_recipe_json(
        serialize_edit_recipe_json(*projected.state));
    require(normalized_projected.layers == final_state.layers,
            "Canonical v13 asset projection must preserve Smart Objects, Blend If and vector path payloads.");

    EditorPreviewController reopened;
    reopened.loadAsset(QStringLiteral("professional-tools-reopened"), base_path,
                       {}, {}, {}, {}, serialize_edit_recipe_json(normalized_projected));
    settle(reopened);
    const auto reopened_state = state(reopened);
    const auto reopened_render = render(base, reopened_state);
    require(reopened_state.layers == final_state.layers &&
                reopened_render == render(base, final_state) &&
                QFileInfo(original_path).exists() == false &&
                QFileInfo(replacement_path).exists() == false,
            "Reopened embedded JPEG, shared replacement, mask, Blend If and vector pixels must render exactly without source files.");
    require(reopened.photoLayerBlendIf(layer_index(reopened, blend_id)) ==
                editor.photoLayerBlendIf(layer_index(editor, blend_id)) &&
                reopened.photoLayerBlendIf(layer_index(reopened, adjustment_id)) ==
                    editor.photoLayerBlendIf(layer_index(editor, adjustment_id)) &&
                reopened.photoLayerBlendIf(layer_index(reopened, group_id)) ==
                    editor.photoLayerBlendIf(layer_index(editor, group_id)) &&
                !layer_by_id(reopened, vector_id).value(QStringLiteral("vectorPath")).toMap().isEmpty() &&
                layer_by_id(reopened, original_id).value(QStringLiteral("smartObject")).toMap()
                    .value(QStringLiteral("sourceSha256")).toString() ==
                    replaced_descriptor.value(QStringLiteral("sourceSha256")).toString(),
            "Save/reopen must retain Blend If settings, editable paths and embedded replacement identity.");

    std::cout << "Smart Object shared replacement/independence and offline JPEG rendering, layer masks/effects/transforms, Blend If render/bypass/guards and adjustment/group cache identity, editable vector path render/edit/selection, canonical v13 asset projection, and controller reopen passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
