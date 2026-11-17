#include "edit_adjustment_patch.h"
#include "edit_recipe_state.h"
#include "editor_preview_controller.h"
#include "photo_render_utils.h"
#include "photo_smart_object.h"

#include <QColorSpace>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QSet>
#include <QTemporaryDir>
#include <QThread>
#include <QThreadPool>
#include <QUrl>

#include <iostream>
#include <stdexcept>

using namespace photo_editor::desktop;
using namespace photo_editor::desktop::smart_object;

namespace {

void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

EditRecipeState state(const EditorPreviewController& editor) {
  const auto parsed = parse_adjustment_patch_json(
      editor.currentAssetBoundAdjustmentPatchJson());
  require(parsed.ok(), "Current recipe must parse.");
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
            editor.photoExportBusy()) &&
           timer.elapsed() < 60000);
  require(!editor.processing() && !editor.professionalToolsBusy() &&
              !editor.fullRenderRunning() && !editor.selectionWorkBusy() &&
              !editor.photoExportBusy(),
          qPrintable(QStringLiteral("Controller work did not settle: %1")
                         .arg(editor.status())));
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

QVariantMap layer_by_id(const EditorPreviewController& editor,
                        const QString& id) {
  const auto index = layer_index(editor, id);
  return index < 0 ? QVariantMap{} : state(editor).layers.at(index).toMap();
}

QVariantMap without_content_fields(QVariantMap layer) {
  for (const auto& key : {
           QStringLiteral("smartObject"), QStringLiteral("smartObjectInstanceId"),
           QStringLiteral("sourcePath"), QStringLiteral("pixelData"),
           QStringLiteral("pixelDataEncoding"),
           QStringLiteral("smartObjectIntrinsicWidth"),
           QStringLiteral("smartObjectIntrinsicHeight")}) {
    layer.remove(key);
  }
  return layer;
}

QStringList shared_ids(const EditorPreviewController& editor,
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

QImage render(const QImage& base, const EditRecipeState& edit) {
  const auto image = render_output_image_with_backend(
      base, edit, QColorSpace(QColorSpace::SRgb), QImage::Format_ARGB32,
      RenderBackendPreference::CpuOnly);
  require(!image.isNull(), "CPU layer render returned an empty image.");
  return image;
}

void diagnose_smart_object_redo(const QVariantMap& expected_descriptor,
                                const QVariantMap& actual_descriptor,
                                const QImage& expected_render,
                                const QImage& actual_render) {
  std::cerr << "Smart Object redo diagnostic: descriptorEqual="
            << (expected_descriptor == actual_descriptor)
            << " renderEqual=" << (expected_render == actual_render)
            << " expectedPolicy="
            << expected_descriptor.value(QStringLiteral("policy")).toString().toStdString()
            << " actualPolicy="
            << actual_descriptor.value(QStringLiteral("policy")).toString().toStdString()
            << " expectedContentId="
            << expected_descriptor.value(QStringLiteral("contentId")).toString().toStdString()
            << " actualContentId="
            << actual_descriptor.value(QStringLiteral("contentId")).toString().toStdString()
            << " expectedHash="
            << expected_descriptor.value(QStringLiteral("sourceSha256")).toString().toStdString()
            << " actualHash="
            << actual_descriptor.value(QStringLiteral("sourceSha256")).toString().toStdString()
            << " expectedPath="
            << expected_descriptor.value(QStringLiteral("sourcePath")).toString().toStdString()
            << " actualPath="
            << actual_descriptor.value(QStringLiteral("sourcePath")).toString().toStdString()
            << '\n';

  auto keys = expected_descriptor.keys();
  for (const auto& key : actual_descriptor.keys()) {
    if (!keys.contains(key)) keys.append(key);
  }
  keys.sort();
  for (const auto& key : keys) {
    const auto expected_value = expected_descriptor.value(key);
    const auto actual_value = actual_descriptor.value(key);
    if (expected_value == actual_value) continue;
    if (key == QStringLiteral("dataBase64")) {
      std::cerr << "  dataBase64 expectedChars=" << expected_value.toString().size()
                << " actualChars=" << actual_value.toString().size() << '\n';
    } else {
      std::cerr << "  " << key.toStdString() << " expected="
                << expected_value.toString().toStdString() << " actual="
                << actual_value.toString().toStdString() << '\n';
    }
  }

  std::cerr << "  expectedRender=" << expected_render.width() << 'x'
            << expected_render.height() << " actualRender="
            << actual_render.width() << 'x' << actual_render.height() << '\n';
  if (expected_render.size() != actual_render.size()) return;
  int difference_count = 0;
  QPoint first_difference(-1, -1);
  for (int y = 0; y < expected_render.height(); ++y) {
    for (int x = 0; x < expected_render.width(); ++x) {
      if (expected_render.pixelColor(x, y) == actual_render.pixelColor(x, y)) continue;
      if (difference_count == 0) first_difference = QPoint(x, y);
      ++difference_count;
    }
  }
  std::cerr << "  differingPixels=" << difference_count;
  if (difference_count > 0) {
    const auto expected_color = expected_render.pixelColor(first_difference);
    const auto actual_color = actual_render.pixelColor(first_difference);
    std::cerr << " firstAt=" << first_difference.x() << ',' << first_difference.y()
              << " expectedArgb=" << expected_color.name(QColor::HexArgb).toStdString()
              << " actualArgb=" << actual_color.name(QColor::HexArgb).toStdString();
  }
  std::cerr << '\n';
}

QByteArray file_bytes(const QString& path) {
  QFile file(path);
  require(file.open(QIODevice::ReadOnly), "Could not read a workflow fixture file.");
  return file.readAll();
}

bool write_bytes(const QString& path, const QByteArray& bytes) {
  QFile file(path);
  return file.open(QIODevice::WriteOnly) &&
      file.write(bytes) == static_cast<qint64>(bytes.size());
}

QString link_status(const EditorPreviewController& editor, const QString& id) {
  return editor.smartObjectLinkStatuses().value(id).toMap()
      .value(QStringLiteral("status")).toString();
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
    QImage base(180, 100, QImage::Format_RGB32);
    base.setColorSpace(QColorSpace(QColorSpace::SRgb));
    base.fill(QColor(QStringLiteral("#4f6377")));
    const auto base_path = runtime.filePath(QStringLiteral("base.png"));
    require(base.save(base_path), "Could not save the workflow base image.");

    QImage original(48, 48, QImage::Format_RGB32);
    original.setColorSpace(QColorSpace(QColorSpace::SRgb));
    original.fill(QColor(QStringLiteral("#d52d1b")));
    const auto linked_path = runtime.filePath(QStringLiteral("linked-source.png"));
    require(original.save(linked_path), "Could not save the linked source fixture.");
    const auto original_source_bytes = file_bytes(linked_path);

    QImage updated(24, 72, QImage::Format_RGB32);
    updated.setColorSpace(QColorSpace(QColorSpace::SRgb));
    updated.fill(QColor(QStringLiteral("#1681d4")));

    QImage relinked_image(72, 30, QImage::Format_RGB32);
    relinked_image.setColorSpace(QColorSpace(QColorSpace::SRgb));
    relinked_image.fill(QColor(QStringLiteral("#3bb85c")));
    const auto relinked_path = runtime.filePath(QStringLiteral("relinked-source.png"));
    require(relinked_image.save(relinked_path), "Could not save the relink destination fixture.");

    EditorPreviewController editor;
    editor.loadAsset(QStringLiteral("smart-link-workflow"), base_path,
                     {}, {}, {}, {}, {});
    settle(editor);
    require(editor.addSourceLayer(linked_path, QStringLiteral("Linked product")),
            "Could not add the linked source layer.");
    const auto layer_id = state(editor).layers.first().toMap()
                              .value(QStringLiteral("id")).toString();
    require(!layer_id.isEmpty(), "Linked source layer needs a stable ID.");

    editor.addMask(QStringLiteral("rectangle"));
    editor.setActiveMaskFeather(0.0);
    editor.setActiveMaskCenterX(0.5);
    editor.setActiveMaskCenterY(0.5);
    editor.setActiveMaskRadiusX(0.5);
    editor.setActiveMaskRadiusY(0.5);
    settle(editor);
    require(editor.applyWorkingSelectionToLayer(layer_index(editor, layer_id)),
            "Could not attach a layer mask to the linked source.");
    require(editor.setPhotoLayerTransform(layer_index(editor, layer_id),
                                          12, 8, 0.9, 11.0),
            "Could not set the linked source placement.");
    settle(editor);

    const auto before_conversion = layer_by_id(editor, layer_id);
    const auto layer_mask_id = before_conversion.value(QStringLiteral("selectionMaskSetId")).toString();
    require(!layer_mask_id.isEmpty(), "Linked workflow needs a real layer mask.");
    require(editor.convertPhotoLayerToSmartObject(layer_index(editor, layer_id), true),
            "Linked Smart Object conversion did not start.");
    settle(editor);
    const auto first_layer = layer_by_id(editor, layer_id);
    const auto initial_descriptor = first_layer.value(QStringLiteral("smartObject")).toMap();
    const auto content_id = initial_descriptor.value(QStringLiteral("contentId")).toString();
    const auto instance_id = first_layer.value(QStringLiteral("smartObjectInstanceId")).toString();
    require(initial_descriptor.value(QStringLiteral("policy")).toString() == QStringLiteral("linked") &&
                initial_descriptor.value(QStringLiteral("sourcePath")).toString() == QFileInfo(linked_path).absoluteFilePath() &&
                !content_id.isEmpty() && !instance_id.isEmpty(),
            "Linked conversion must preserve the absolute source dependency and assign stable identities.");

    require(editor.duplicatePhotoSmartObject(layer_index(editor, layer_id)),
            "Could not duplicate the linked instance.");
    settle(editor);
    const auto ids = shared_ids(editor, content_id);
    require(ids.size() == 2, "The duplicate must join the same shared content group.");
    const auto sibling_id = ids.at(0) == layer_id ? ids.at(1) : ids.at(0);
    require(!sibling_id.isEmpty() &&
                layer_by_id(editor, sibling_id).value(QStringLiteral("smartObjectInstanceId")) != instance_id,
            "Shared instances must retain distinct layer and instance identities.");
    require(editor.setPhotoLayerTransform(layer_index(editor, sibling_id),
                                          104, 18, 1.2, -7.0),
            "Could not set the second linked placement.");
    settle(editor);
    const auto original_outer = layer_by_id(editor, layer_id);
    const auto sibling_outer = layer_by_id(editor, sibling_id);
    const auto original_instance_id = original_outer.value(QStringLiteral("smartObjectInstanceId"));
    const auto sibling_instance_id = sibling_outer.value(QStringLiteral("smartObjectInstanceId"));
    const auto original_render = render(base, state(editor));

    require(editor.checkPhotoSmartObjectLink(layer_index(editor, layer_id)),
            "Read-only link check did not start.");
    settle(editor);
    require(link_status(editor, layer_id) == QStringLiteral("current"),
            "Link check must report a matching source as current.");

    require(editor.setPhotoLayerLocked(layer_index(editor, layer_id), true),
            "Could not lock the linked layer for read-only status inspection.");
    settle(editor);
    require(editor.checkPhotoSmartObjectLink(layer_index(editor, layer_id)),
            "Read-only source check should remain available for a locked layer.");
    settle(editor);
    require(link_status(editor, layer_id) == QStringLiteral("current"),
            "Locked linked layer should still report a readable current source.");
    require(editor.setPhotoLayerLocked(layer_index(editor, layer_id), false),
            "Could not unlock the linked layer.");
    settle(editor);

    require(updated.save(linked_path), "Could not write the changed linked source revision.");
    const auto updated_source_bytes = file_bytes(linked_path);
    require(editor.checkPhotoSmartObjectLink(layer_index(editor, layer_id)),
            "Changed-source status check did not start.");
    settle(editor);
    require(link_status(editor, layer_id) == QStringLiteral("changed"),
            "Link check must detect source bytes changed on disk.");

    require(editor.setPhotoLayerLocked(layer_index(editor, sibling_id), true),
            "Could not lock the shared sibling fixture.");
    settle(editor);
    const auto locked_shared_state = state(editor);
    require(editor.refreshPhotoSmartObjectContents(layer_index(editor, layer_id)),
            "Refresh should reach worker-side shared-lock validation.");
    settle(editor);
    require(state(editor) == locked_shared_state &&
                editor.status().contains(QStringLiteral("locked"), Qt::CaseInsensitive),
            "Refresh must reject a locked shared sibling atomically.");
    require(editor.setPhotoLayerLocked(layer_index(editor, sibling_id), false),
            "Could not unlock the shared sibling fixture.");
    settle(editor);

    const auto before_refresh = state(editor);
    require(editor.refreshPhotoSmartObjectContents(layer_index(editor, layer_id)),
            "Refresh of a changed linked source did not start.");
    settle(editor);
    const auto refreshed_state = state(editor);
    const auto refreshed_layer = layer_by_id(editor, layer_id);
    const auto refreshed_sibling = layer_by_id(editor, sibling_id);
    const auto refreshed_descriptor = refreshed_layer.value(QStringLiteral("smartObject")).toMap();
    require(refreshed_descriptor.value(QStringLiteral("contentId")).toString() == content_id &&
                refreshed_descriptor.value(QStringLiteral("sourceSha256")).toString() !=
                    initial_descriptor.value(QStringLiteral("sourceSha256")).toString() &&
                refreshed_descriptor.value(QStringLiteral("intrinsicWidth")).toInt() == 24 &&
                refreshed_descriptor.value(QStringLiteral("intrinsicHeight")).toInt() == 72 &&
                refreshed_sibling.value(QStringLiteral("smartObject")).toMap() == refreshed_descriptor &&
                refreshed_layer.value(QStringLiteral("smartObjectInstanceId")) == original_instance_id &&
                refreshed_sibling.value(QStringLiteral("smartObjectInstanceId")) == sibling_instance_id &&
                without_content_fields(refreshed_layer) == without_content_fields(original_outer) &&
                without_content_fields(refreshed_sibling) == without_content_fields(sibling_outer) &&
                refreshed_layer.value(QStringLiteral("selectionMaskSetId")).toString() == layer_mask_id,
            "Refresh must update both shared descriptors while preserving layer IDs, masks, transforms and instance IDs.");
    const auto refreshed_render = render(base, refreshed_state);
    require(refreshed_render != original_render,
            "Refreshing changed linked bytes must change the rendered component.");

    restore_history(editor, false);
    require(write_bytes(linked_path, original_source_bytes),
            "Could not restore the original linked bytes for undo rendering.");
    require(layer_by_id(editor, layer_id).value(QStringLiteral("smartObject")).toMap()
                    .value(QStringLiteral("sourceSha256")) ==
                initial_descriptor.value(QStringLiteral("sourceSha256")) &&
                render(base, state(editor)) == original_render,
            "Undo must restore the prior linked revision and rendered output.");
    restore_history(editor, true);
    require(write_bytes(linked_path, updated_source_bytes),
            "Could not restore the updated linked bytes for redo rendering.");
    require(state(editor) == refreshed_state && render(base, state(editor)) == refreshed_render,
            "Redo must restore the refreshed shared link revision exactly.");

    require(QFile::remove(linked_path), "Could not remove the linked file for missing-link coverage.");
    require(editor.checkPhotoSmartObjectLink(layer_index(editor, layer_id)),
            "Missing-source status check did not start.");
    settle(editor);
    require(link_status(editor, layer_id) == QStringLiteral("missing"),
            "Link check must report a missing source file.");
    const auto before_missing_refresh = state(editor);
    require(editor.refreshPhotoSmartObjectContents(layer_index(editor, layer_id)),
            "Missing source refresh should reach worker-side validation.");
    settle(editor);
    require(state(editor) == before_missing_refresh &&
                editor.status().contains(QStringLiteral("missing"), Qt::CaseInsensitive),
            "Missing-link refresh must leave the document and history unchanged.");

    require(editor.relinkPhotoSmartObjectContents(layer_index(editor, layer_id),
                QUrl::fromLocalFile(relinked_path).toString()),
            "Relink to a valid replacement source did not start.");
    settle(editor);
    const auto relinked_layer = layer_by_id(editor, layer_id);
    const auto relinked_sibling = layer_by_id(editor, sibling_id);
    const auto relinked_descriptor = relinked_layer.value(QStringLiteral("smartObject")).toMap();
    require(relinked_descriptor.value(QStringLiteral("contentId")).toString() == content_id &&
                relinked_descriptor.value(QStringLiteral("sourcePath")).toString() == QFileInfo(relinked_path).absoluteFilePath() &&
                relinked_descriptor.value(QStringLiteral("intrinsicWidth")).toInt() == 72 &&
                relinked_descriptor.value(QStringLiteral("intrinsicHeight")).toInt() == 30 &&
                relinked_sibling.value(QStringLiteral("smartObject")).toMap() == relinked_descriptor &&
                without_content_fields(relinked_layer) == without_content_fields(original_outer) &&
                without_content_fields(relinked_sibling) == without_content_fields(sibling_outer),
            "Relink must retarget the complete shared group while preserving placements and logical identity.");
    const auto linked_render = render(base, state(editor));

    require(editor.embedPhotoSmartObjectContents(layer_index(editor, layer_id)),
            "Embed linked source operation did not start.");
    settle(editor);
    const auto embedded_layer = layer_by_id(editor, layer_id);
    const auto embedded_sibling = layer_by_id(editor, sibling_id);
    const auto embedded_descriptor = embedded_layer.value(QStringLiteral("smartObject")).toMap();
    QFile embedded_source(relinked_path);
    require(embedded_source.open(QIODevice::ReadOnly),
            "Could not read the source bytes before removing the linked file.");
    const auto linked_source_bytes = embedded_source.readAll();
    embedded_source.close();
    require(embedded_descriptor.value(QStringLiteral("policy")).toString() == QStringLiteral("embedded") &&
                embedded_descriptor.value(QStringLiteral("sourcePath")).toString().isEmpty() &&
                embedded_descriptor.value(QStringLiteral("contentId")).toString() == content_id &&
                embedded_sibling.value(QStringLiteral("smartObject")).toMap() == embedded_descriptor &&
                read_content_bytes(embedded_descriptor) == linked_source_bytes &&
                without_content_fields(embedded_layer) == without_content_fields(relinked_layer) &&
                without_content_fields(embedded_sibling) == without_content_fields(relinked_sibling),
            "Embed must convert the whole group to portable bytes without changing placement state.");
    const auto embedded_render = render(base, state(editor));
    require(embedded_render == linked_render,
            "Embedding a linked source must preserve the rendered pixels.");

    restore_history(editor, false);
    require(layer_by_id(editor, layer_id).value(QStringLiteral("smartObject")).toMap()
                    .value(QStringLiteral("policy")).toString() == QStringLiteral("linked") &&
                render(base, state(editor)) == linked_render,
            "Undo must restore the linked policy and identical pixels.");
    restore_history(editor, true);
    const auto redo_descriptor = layer_by_id(editor, layer_id)
        .value(QStringLiteral("smartObject")).toMap();
    const auto redo_render = render(base, state(editor));
    if (redo_descriptor != embedded_descriptor || redo_render != embedded_render) {
      diagnose_smart_object_redo(embedded_descriptor, redo_descriptor,
                                 embedded_render, redo_render);
    }
    require(redo_descriptor == embedded_descriptor && redo_render == embedded_render,
            "Redo must restore the portable embedded revision and identical pixels.");
    require(QFile::remove(relinked_path),
            "Embedded Smart Object should no longer depend on its source file.");
    require(read_content_bytes(embedded_descriptor) == linked_source_bytes,
            "Embedded Smart Object bytes must remain readable after source removal.");

    const auto serialized = serialize_edit_recipe_json(state(editor));
    EditorPreviewController reopened;
    reopened.loadAsset(QStringLiteral("smart-link-workflow-reopened"), base_path,
                       {}, {}, {}, {}, serialized);
    settle(reopened);
    require(state(reopened).layers == state(editor).layers &&
                render(base, state(reopened)) == embedded_render &&
                read_content_bytes(layer_by_id(reopened, layer_id)
                    .value(QStringLiteral("smartObject")).toMap()) == linked_source_bytes,
            "Reopened embedded content must retain its bytes and rendered output without the source file.");

    // Complete an outstanding worker after switching documents. Its stale
    // result must not mutate the new recipe or cache link state for the old one.
    QImage stale_source(16, 16, QImage::Format_RGB32);
    stale_source.fill(QColor(QStringLiteral("#b24a2c")));
    const auto stale_source_path = runtime.filePath(QStringLiteral("stale-source.png"));
    require(stale_source.save(stale_source_path), "Could not save stale-guard source fixture.");
    EditorPreviewController stale_editor;
    stale_editor.loadAsset(QStringLiteral("smart-link-stale-before"), base_path,
                           {}, {}, {}, {}, {});
    settle(stale_editor);
    require(stale_editor.addSourceLayer(stale_source_path, QStringLiteral("Stale linked source")),
            "Could not add stale-guard source layer.");
    const auto stale_layer_id = state(stale_editor).layers.first().toMap()
                                    .value(QStringLiteral("id")).toString();
    require(stale_editor.convertPhotoLayerToSmartObject(
                layer_index(stale_editor, stale_layer_id), true),
            "Stale-guard linked conversion did not start.");
    settle(stale_editor);
    const auto stale_before_relink = layer_by_id(stale_editor, stale_layer_id);
    const auto stale_descriptor_before_relink = stale_before_relink
        .value(QStringLiteral("smartObject")).toMap();
    const auto stale_opacity_before_relink = stale_before_relink
        .value(QStringLiteral("opacity"), 1.0).toDouble();
    require(stale_descriptor_before_relink.value(QStringLiteral("policy")).toString() ==
                QStringLiteral("linked") &&
                stale_before_relink.value(QStringLiteral("sourcePath")).toString() ==
                    stale_descriptor_before_relink.value(QStringLiteral("sourcePath")).toString() &&
                !stale_descriptor_before_relink.value(QStringLiteral("sourcePath")).toString().isEmpty(),
            "Linked source path must remain in the persisted Smart Object descriptor.");
    const auto stale_relink_path = runtime.filePath(QStringLiteral("stale-relink.png"));
    require(relinked_image.save(stale_relink_path), "Could not save stale-guard relink target.");
    require(stale_editor.relinkPhotoSmartObjectContents(
                layer_index(stale_editor, stale_layer_id),
                QUrl::fromLocalFile(stale_relink_path).toString()),
            "Stale-guard relink work did not start.");
    require(stale_editor.setPhotoLayerOpacity(
                layer_index(stale_editor, stale_layer_id), 0.61),
            "Could not create a concurrent layer edit while relink preparation was pending.");
    settle(stale_editor);
    const auto stale_after_relink = layer_by_id(stale_editor, stale_layer_id);
    require(stale_after_relink.value(QStringLiteral("smartObject")).toMap() ==
                stale_descriptor_before_relink &&
                stale_after_relink.value(QStringLiteral("sourcePath")).toString() ==
                    stale_descriptor_before_relink.value(QStringLiteral("sourcePath")).toString() &&
                qAbs(stale_after_relink.value(QStringLiteral("opacity")).toDouble() - 0.61) < 0.0001,
            "A stale relink result must preserve a concurrent edit in the same document.");
    restore_history(stale_editor, false);
    const auto after_stale_undo = layer_by_id(stale_editor, stale_layer_id);
    require(after_stale_undo.value(QStringLiteral("smartObject")).toMap() ==
                stale_descriptor_before_relink &&
                qAbs(after_stale_undo.value(QStringLiteral("opacity")).toDouble() -
                     stale_opacity_before_relink) < 0.0001,
            "Only the accepted opacity edit should be on history after stale relink rejection.");
    require(stale_editor.relinkPhotoSmartObjectContents(
                layer_index(stale_editor, stale_layer_id),
                QUrl::fromLocalFile(stale_relink_path).toString()),
            "Second stale-guard relink work did not start.");
    stale_editor.loadAsset(QStringLiteral("smart-link-stale-after"), base_path,
                           {}, {}, {}, {}, {});
    settle(stale_editor);
    require(stale_editor.assetId() == QStringLiteral("smart-link-stale-after") &&
                state(stale_editor).layers.isEmpty() &&
                !stale_editor.smartObjectLinkStatuses().contains(stale_layer_id),
            "A completed stale relink must not mutate or cache status for the newly opened document.");

    std::cout << "Linked Smart Object status, changed-file refresh, missing-link handling, shared relink/embed, masks/transforms, locks, history, portable reopen and stale-document guards passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
