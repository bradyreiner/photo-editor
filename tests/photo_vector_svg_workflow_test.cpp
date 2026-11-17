#include "edit_adjustment_patch.h"
#include "edit_recipe_state.h"
#include "editor_preview_controller.h"
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

#include <iostream>
#include <stdexcept>

using namespace photo_editor::desktop;

namespace {

namespace vector_paths = photo_editor::desktop::vector_paths;

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
    if (layers.at(index).toMap().value(QStringLiteral("id")).toString() == id)
      return index;
  }
  return -1;
}

QVector<QString> vector_layer_ids(const EditorPreviewController& editor) {
  QVector<QString> ids;
  for (const auto& value : state(editor).layers) {
    const auto layer = value.toMap();
    if (!layer.value(QStringLiteral("vectorPath")).toMap().isEmpty())
      ids.append(layer.value(QStringLiteral("id")).toString());
  }
  return ids;
}

QVariantMap layer_by_id(const EditorPreviewController& editor,
                        const QString& id) {
  const auto index = layer_index(editor, id);
  return index < 0 ? QVariantMap{} : state(editor).layers.at(index).toMap();
}

QImage render(const QImage& base, const EditRecipeState& edit) {
  const auto image = render_output_image_with_backend(
      base, edit, QColorSpace(QColorSpace::SRgb), QImage::Format_ARGB32,
      RenderBackendPreference::CpuOnly);
  require(!image.isNull(), "CPU layer render returned an empty image.");
  return image;
}

bool write_file(const QString& path, const QByteArray& bytes) {
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly)) return false;
  return file.write(bytes) == bytes.size() && file.flush();
}

QByteArray supported_svg_fixture() {
  return QByteArrayLiteral(R"svg(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 160 100">
    <path d="M20 15 C40 5 65 5 80 15 S105 35 80 45 L20 45 Z"
          fill="#ff000080" fill-rule="evenodd" stroke="none"/>
    <path d="M96 70 C110 60 130 80 145 70"
          fill="none" stroke="#00ff00" stroke-width="3"/>
  </svg>)svg");
}

void verify_imported_path_layers(EditorPreviewController& editor) {
  const auto ids = vector_layer_ids(editor);
  require(ids.size() == 2, "SVG import should create one editable layer for each path element.");

  const auto fill_layer = [&]() {
    for (const auto& id : ids) {
      const auto layer = layer_by_id(editor, id);
      if (layer.value(QStringLiteral("vectorFillEnabled")).toBool()) return layer;
    }
    return QVariantMap{};
  }();
  const auto stroke_layer = [&]() {
    for (const auto& id : ids) {
      const auto layer = layer_by_id(editor, id);
      if (layer.value(QStringLiteral("vectorStrokeEnabled")).toBool()) return layer;
    }
    return QVariantMap{};
  }();
  require(!fill_layer.isEmpty() && !stroke_layer.isEmpty(),
          "Imported paths should preserve their enabled fill/stroke choices.");
  require(fill_layer.value(QStringLiteral("vectorStrokeEnabled")).toBool() == false &&
              stroke_layer.value(QStringLiteral("vectorFillEnabled")).toBool() == false,
          "SVG none styles should remain disabled rather than becoming visible colors.");
  const QColor fill_color(fill_layer.value(QStringLiteral("vectorFillColor")).toString());
  const QColor stroke_color(stroke_layer.value(QStringLiteral("vectorStrokeColor")).toString());
  require(fill_color.isValid() && fill_color.red() == 255 && fill_color.green() == 0 &&
              fill_color.blue() == 0 && fill_color.alpha() == 128,
          "CSS #RRGGBBAA fill color must retain its red channel and alpha byte.");
  require(stroke_color.isValid() && stroke_color.red() == 0 &&
              stroke_color.green() == 255 && stroke_color.blue() == 0 &&
              stroke_color.alpha() == 255,
          "Imported solid stroke color should remain green and opaque.");

  vector_paths::VectorPathDocument fill_document;
  QString error;
  require(vector_paths::to_document(
              fill_layer.value(QStringLiteral("vectorPath")).toMap(),
              &fill_document, &error),
          "Imported fill layer should keep its editable cubic path document.");
  require(fill_document.subpaths.size() == 1 &&
              fill_document.subpaths.first().closed &&
              fill_document.subpaths.first().anchors.size() >= 4,
          "Imported fill path should retain closed anchors.");
  bool has_curve_handle = false;
  for (const auto& anchor : fill_document.subpaths.first().anchors)
    has_curve_handle = has_curve_handle || !anchor.incoming.isNull() || !anchor.outgoing.isNull();
  require(has_curve_handle, "SVG cubic and smooth commands should remain editable handles.");

  vector_paths::VectorPathDocument stroke_document;
  require(vector_paths::to_document(
              stroke_layer.value(QStringLiteral("vectorPath")).toMap(),
              &stroke_document, &error),
          "Imported stroke layer should keep its editable path document.");
  require(stroke_document.subpaths.size() == 1 &&
              !stroke_document.subpaths.first().closed &&
              stroke_document.subpaths.first().anchors.size() >= 2,
          "Stroke-only open contours should stay open and editable.");
}

}  // namespace

int main(int argc, char** argv) {
  QTemporaryDir runtime;
  if (!runtime.isValid()) return 1;
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", runtime.filePath(QStringLiteral("app-data")).toUtf8());
  qputenv("PHOTO_EDITOR_CACHE_DIR", runtime.filePath(QStringLiteral("cache")).toUtf8());
  qputenv("PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE", "1");
  QGuiApplication application(argc, argv);

  struct DrainWorkers {
    ~DrainWorkers() { QThreadPool::globalInstance()->waitForDone(60000); }
  } drain_workers;

  try {
    QImage base(160, 100, QImage::Format_RGB32);
    base.setColorSpace(QColorSpace(QColorSpace::SRgb));
    base.fill(QColor(QStringLiteral("#4d6073")));
    const auto base_path = runtime.filePath(QStringLiteral("base.png"));
    require(base.save(base_path), "Could not save the base-image fixture.");

    const auto import_path = runtime.filePath(QStringLiteral("supported.svg"));
    require(write_file(import_path, supported_svg_fixture()),
            "Could not write the supported SVG fixture.");
    const auto unsupported_path = runtime.filePath(QStringLiteral("unsupported.svg"));
    require(write_file(unsupported_path,
                QByteArrayLiteral("<svg viewBox='0 0 10 10'><path d='M0 0 A4 4 0 0 1 8 8'/></svg>")),
            "Could not write the unsupported SVG fixture.");

    EditorPreviewController editor;
    editor.loadAsset(QStringLiteral("svg-source-asset"), base_path, {}, {}, {}, {}, {});
    settle(editor);
    const auto before_import = editor.currentAssetBoundAdjustmentPatchJson();
    const auto base_render = render(base, state(editor));
    require(editor.importPhotoVectorSvg(editor.assetId(), import_path),
            "Controller should admit a supported SVG import.");
    settle(editor);
    const auto after_import = editor.currentAssetBoundAdjustmentPatchJson();
    require(after_import != before_import,
            "Successful SVG import should record the new editable layers.");
    verify_imported_path_layers(editor);
    const auto imported_render = render(base, state(editor));
    require(imported_render != base_render,
            "Imported fill and stroke paths should contribute rendered pixels.");

    editor.undo();
    settle(editor);
    require(editor.currentAssetBoundAdjustmentPatchJson() == before_import,
            "Undo should remove all paths from one multi-path SVG import operation.");
    editor.redo();
    settle(editor);
    require(editor.currentAssetBoundAdjustmentPatchJson() == after_import &&
                render(base, state(editor)) == imported_render,
            "Redo should restore every imported path and its exact visible result.");

    const auto before_invalid_import = editor.currentAssetBoundAdjustmentPatchJson();
    editor.importPhotoVectorSvg(editor.assetId(), unsupported_path);
    settle(editor);
    require(editor.currentAssetBoundAdjustmentPatchJson() == before_invalid_import,
            "Rejecting an SVG arc command must leave the document unchanged atomically.");

    const auto before_stale_import = editor.currentAssetBoundAdjustmentPatchJson();
    require(!editor.importPhotoVectorSvg(QStringLiteral("different-asset"), import_path) &&
                !editor.professionalToolsBusy() &&
                editor.currentAssetBoundAdjustmentPatchJson() == before_stale_import,
            "A stale asset ID must not start or mutate an SVG import.");

    auto ids = vector_layer_ids(editor);
    require(ids.size() == 2, "Both imported vector layer identities should remain available.");
    const auto locked_layer_id = ids.first();
    const auto locked_index = layer_index(editor, locked_layer_id);
    require(locked_index >= 0 && editor.setPhotoLayerLocked(locked_index, true),
            "Could not lock a vector path layer before read-only export.");
    settle(editor);

    const auto patch_before_export = editor.currentAssetBoundAdjustmentPatchJson();
    QVector<QString> export_paths;
    for (qsizetype index = 0; index < ids.size(); ++index) {
      const auto destination = runtime.filePath(QStringLiteral("path-%1.svg").arg(index));
      require(editor.exportPhotoVectorSvg(editor.assetId(), ids.at(index), destination),
              "Read-only SVG export should be allowed, including for a locked path layer.");
      settle(editor);
      require(QFileInfo::exists(destination) && QFileInfo(destination).size() > 0,
              "SVG export should produce a non-empty path-only file.");
      export_paths.append(destination);
    }
    require(editor.currentAssetBoundAdjustmentPatchJson() == patch_before_export,
            "Export should leave the editable document unchanged.");
    const auto stale_export_path = runtime.filePath(QStringLiteral("stale.svg"));
    require(!editor.exportPhotoVectorSvg(QStringLiteral("different-asset"), ids.first(),
                                         stale_export_path) &&
                !QFileInfo::exists(stale_export_path),
            "A stale asset ID must not export another document's vector path.");
    require(!editor.exportPhotoVectorSvg(editor.assetId(), QStringLiteral("missing-layer"),
                                         stale_export_path) &&
                !QFileInfo::exists(stale_export_path),
            "A stale layer ID must not export a different layer at the old index.");

    EditorPreviewController reopened;
    reopened.loadAsset(QStringLiteral("svg-roundtrip-asset"), base_path,
                       {}, {}, {}, {}, {});
    settle(reopened);
    // New imported layers are inserted above existing layers. Rebuild them
    // from bottom to top to preserve the rendered stack order.
    for (qsizetype index = export_paths.size(); index > 0; --index) {
      require(reopened.importPhotoVectorSvg(
                  reopened.assetId(), export_paths.at(index - 1)),
              "Round-trip controller should admit each exported editable path.");
      settle(reopened);
    }
    const auto round_trip_render = render(base, state(reopened));
    require(round_trip_render == imported_render,
            "Export and reimport should reproduce the same vector pixels and alpha.");

    std::cout << "photo_vector_svg_workflow_test passed\n";
    return 0;
  } catch (const std::exception& exception) {
    std::cerr << "photo_vector_svg_workflow_test failed: " << exception.what() << '\n';
    return 1;
  }
}
