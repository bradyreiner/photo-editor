#include "editor_preview_controller.h"
#include "edit_recipe_state.h"
#include "edit_adjustment_patch.h"
#include "carousel_document_controller.h"
#include "timeline_document_controller.h"
#include "photo_render_utils.h"
#include "photo_editor/pipeline/layer_styles.h"
#include "app_bootstrap.h"
#include <QScopeGuard>
#include <QProcess>
#include <QStandardPaths>
#include <functional>
#include <cstring>
#include <cmath>

#include <QBuffer>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDir>
#include <QColorSpace>
#include <QTemporaryDir>
#include <QThread>
#include <QThreadPool>
#include <iostream>
#include <stdexcept>

using namespace photo_editor::desktop;
namespace photo_editor::desktop {
struct CarouselPreviewTestAccess {
  static QJsonObject persistent(const CarouselDocumentController& document) { return document.persistentDocumentObject(); }
  static bool load(CarouselDocumentController& document, const QJsonObject& object) { return document.loadDocumentObject(object); }
  static QImage render(const CarouselDocumentController& document, int board) { return document.renderSlideImage(board, document.exportPixelSize(), false); }
  static QJsonArray recoverFullPanoramaSources(const CarouselDocumentController& document, const QString& directory) {
    QJsonArray results;
    for (const auto& group : document.groups_) {
      QImage image;
      if (!document.decodeEditedImage(group.media, &image, false)) return {};
      const auto frame = document.exportPixelSize();
      const int width = frame.width() * group.span, height = frame.height();
      const auto scale = (group.media.fit_mode == CarouselDocumentController::FitMode::Contain
          ? qMin(double(width) / image.width(), double(height) / image.height())
          : qMax(double(width) / image.width(), double(height) / image.height())) * group.media.scale;
      const QSize size(qRound(image.width() * scale), qRound(image.height() * scale));
      image = image.scaled(size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
      const auto position = [](int frame_extent, int image_extent, double focal, double offset, bool manual) {
        const int free = frame_extent - image_extent;
        return qRound(manual ? free * (offset + 1) / 2
                            : qBound(double(qMin(0, free)), frame_extent * .5 - image_extent * focal, double(qMax(0, free))));
      };
      const auto path = QDir(directory).absoluteFilePath(QStringLiteral("full-photo-%1.png").arg(group.start_index + 1));
      if (!image.save(path, "PNG")) return {};
      results.append(QJsonObject{{"firstBoard", group.start_index + 1}, {"path", path},
          {"width", size.width()}, {"height", size.height()},
          {"x", position(width, size.width(), group.media.focal_x, group.media.offset_x, group.media.manual_crop)},
          {"y", position(height, size.height(), group.media.focal_y, group.media.offset_y, group.media.manual_crop)}});
    }
    return results;
  }
};
}
namespace {
void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}
void settle(EditorPreviewController& editor) {
  QElapsedTimer timer; timer.start();
  do {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    QThread::msleep(2);
  } while ((editor.processing() || editor.fullRenderRunning()) && timer.elapsed() < 30000);
  require(!editor.processing() && !editor.fullRenderRunning(), "Board controller settles");
}
QVariantMap pixel_layer(const QString& id, const QRect& photo, const QColor& color) {
  QImage image(120, 80, QImage::Format_ARGB32); image.fill(Qt::transparent);
  for (int y = photo.top(); y <= photo.bottom(); ++y)
    for (int x = photo.left(); x <= photo.right(); ++x) image.setPixelColor(x, y, color);
  QByteArray bytes; QBuffer buffer(&bytes); buffer.open(QIODevice::WriteOnly); image.save(&buffer, "PNG");
  return {{"id", id}, {"name", id}, {"kind", "pixel"}, {"enabled", true}, {"opacity", 1.0},
          {"pixelWidth", 120}, {"pixelHeight", 80}, {"pixelData", bytes},
          {"x", 0}, {"y", 0}, {"scale", 1.0}, {"rotation", 0.0}};
}
}
#include "carousel_board_sequence_workflow.inc"
#include "board_video_layout_workflow.inc"
#include "board_photo_recipe_framing_workflow.inc"
#include "board_library_workflow.inc"

int main(int argc, char** argv) {
  if (qEnvironmentVariableIsSet("PHOTO_EDITOR_BOARD_LIBRARY_FIXTURE_HELPER")) return board_library_helper(argc, argv);
  qputenv("PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE", "1");
  QGuiApplication app(argc, argv);
  QTemporaryDir runtime;
  if (!runtime.isValid()) return 1;
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", runtime.path().toUtf8());
  qputenv("PHOTO_EDITOR_CAROUSEL_DOCUMENTS_PATH", runtime.filePath("boards").toUtf8());
  qputenv("PHOTO_EDITOR_DATABASE_URL", "postgresql://127.0.0.1:1/unavailable");
  try {
    if (argc == 2 && QString::fromUtf8(argv[1]) == "--board-library") { verify_board_library(runtime); return 0; }
    if (argc == 2 && QString::fromUtf8(argv[1]) == "--photo-framing") { verify_board_photo_recipe_framing(runtime); return 0; }
    if (argc == 2 && QString::fromUtf8(argv[1]) == "--board-video") { verify_board_video_sequence(runtime); return 0; }
    if (argc == 2 && QString::fromUtf8(argv[1]) == "--video-layout") { verify_board_video_layout(runtime); return 0; }
    if (argc == 4 && QString::fromUtf8(argv[1]) == QStringLiteral("--recover-panorama-sources")) {
      QFile input(QString::fromUtf8(argv[2]));
      require(input.open(QIODevice::ReadOnly), "Read original panorama layout");
      const auto wrapper = QJsonDocument::fromJson(input.readAll()).object();
      const auto document = wrapper.value("document").toObject();
      CarouselDocumentController original;
      require(CarouselPreviewTestAccess::load(original, document.value("originalLayoutDocument").toObject()), "Load original full-photo layout");
      const auto directory = QString::fromUtf8(argv[3]);
      require(QDir().mkpath(directory), "Create full-photo recovery artifacts");
      const auto results = CarouselPreviewTestAccess::recoverFullPanoramaSources(original, directory);
      require(results.size() == 3, "Retain all three full panorama photos");
      QFile output(QDir(directory).filePath("full-sources.json"));
      require(output.open(QIODevice::WriteOnly), "Write full-photo recovery geometry");
      output.write(QJsonDocument(results).toJson());
      std::cout << "Recovered three uncropped panorama sources\n";
      return 0;
    }
    if (argc == 3) {
      const auto read_document = [](const QString& path) {
        QFile file(path); require(file.open(QIODevice::ReadOnly), "Read native pixel-proof document");
        const auto wrapper = QJsonDocument::fromJson(file.readAll()).object();
        return wrapper.value("document").isObject() ? wrapper.value("document").toObject() : wrapper.value("document_json").toObject();
      };
      CarouselDocumentController before, after;
      require(CarouselPreviewTestAccess::load(before, read_document(QString::fromUtf8(argv[1]))) &&
              CarouselPreviewTestAccess::load(after, read_document(QString::fromUtf8(argv[2]))), "Load before and shared-canvas documents");
      QJsonArray results;
      for (int index : {0, 1, 4, 5, 13, 14}) {
        const auto original = CarouselPreviewTestAccess::render(before, index).convertToFormat(QImage::Format_ARGB32);
        const auto shared = CarouselPreviewTestAccess::render(after, index).convertToFormat(QImage::Format_ARGB32);
        require(!original.isNull() && original.size() == shared.size(), "Native shared-canvas output retains exact board dimensions");
        int maximum = 0; qsizetype changed = 0;
        for (int y = 0; y < original.height(); ++y) {
          const auto* a = reinterpret_cast<const QRgb*>(original.constScanLine(y));
          const auto* b = reinterpret_cast<const QRgb*>(shared.constScanLine(y));
          for (int x = 0; x < original.width(); ++x) {
            const int difference = qMax(qMax(qAbs(qRed(a[x]) - qRed(b[x])), qAbs(qGreen(a[x]) - qGreen(b[x]))),
                                        qMax(qAbs(qBlue(a[x]) - qBlue(b[x])), qAbs(qAlpha(a[x]) - qAlpha(b[x]))));
            maximum = qMax(maximum, difference); changed += difference > 0;
          }
        }
        results.append(QJsonObject{{"board", index + 1}, {"maxChannelDifference", maximum}, {"changedPixels", changed}});
        std::cout << "Board " << index + 1 << " native pixel difference: " << maximum << ", changed pixels " << changed << '\n';
        require(maximum <= 1, "Native compositor must retain the panorama and brush pixels within one 8-bit rounding level");
      }
      QFile receipt(QString::fromUtf8(argv[2]) + QStringLiteral(".native-pixel-proof.json"));
      require(receipt.open(QIODevice::WriteOnly), "Write native renderer pixel proof"); receipt.write(QJsonDocument(results).toJson());
      QThreadPool::globalInstance()->waitForDone();
      return 0;
    }
    QImage base(120, 80, QImage::Format_RGB32); base.fill(Qt::black);
    const auto path = runtime.filePath("board.png");
    require(base.save(path), "Save the isolated board fixture");
    EditRecipeState recipe;
    recipe.layers = {pixel_layer("top-photo", {10, 4, 100, 32}, Qt::red),
                     pixel_layer("bottom-photo", {10, 44, 100, 32}, Qt::blue)};
    EditorPreviewController editor;
    editor.loadAsset("board-controller-test", path, {}, {}, {}, {}, serialize_edit_recipe_json(recipe), true);
    settle(editor);
    require(editor.photoLayerCount() == 2, "Open two full-canvas PNG photo layers");
    require(editor.photoLayerContainsPoint(0, 60, 20) && !editor.photoLayerContainsPoint(0, 60, 60) &&
            !editor.photoLayerContainsPoint(1, 60, 20) && editor.photoLayerContainsPoint(1, 60, 60),
            "Transparent padding passes clicks through to the visible photo");
    require(editor.setPhotoLayerTransform(0, 20, 10, 0.5, 90), "Rotate and scale the test photo");
    settle(editor);
    const auto bounds = editor.photoLayerBounds(0);
    const double cx = bounds.value("x").toDouble() + bounds.value("width").toDouble() / 2;
    const double cy = bounds.value("y").toDouble() + bounds.value("height").toDouble() / 2;
    require(editor.photoLayerContainsPoint(0, cx + 10, cy) &&
            !editor.photoLayerContainsPoint(0, cx - 10, cy), "Alpha picking follows rotated and scaled photo geometry");
    const auto before = editor.currentAssetBoundAdjustmentPatchJson();
    const auto history = editor.editHistoryCurrentIndex();
    require(editor.applyBoardPhotoLayout({0, 1}, "stack"), "Apply a two-photo stack through the real editor");
    settle(editor);
    const auto after = editor.currentAssetBoundAdjustmentPatchJson();
    require(after != before && editor.editHistoryCurrentIndex() == history + 1,
            "A complete paired layout creates exactly one undo entry");
    const auto layers = editor.photoLayers();
    require(layers.at(0).toMap().value("pixelWidth").toInt() == 100 &&
            layers.at(0).toMap().value("pixelHeight").toInt() == 32 &&
            layers.at(0).toMap().value("y").toInt() < layers.at(1).toMap().value("y").toInt(),
            "Stack trims legacy padding and places the photos in separate rows");
    editor.undo(); settle(editor);
    require(editor.currentAssetBoundAdjustmentPatchJson() == before, "One Undo restores the exact original pixels and transforms");
    editor.redo(); settle(editor);
    require(editor.currentAssetBoundAdjustmentPatchJson() == after, "Redo restores the exact paired layout");
    const auto rejected_history = editor.editHistoryCurrentIndex();
    require(!editor.applyBoardPhotoLayout({0, 0}, "columns") &&
            editor.currentAssetBoundAdjustmentPatchJson() == after &&
            editor.editHistoryCurrentIndex() == rejected_history, "Invalid layouts leave recipe and history untouched");
    EditorPreviewController reopened;
    reopened.loadAsset("board-controller-reopen", path, {}, {}, {}, {}, after, true); settle(reopened);
    const auto restored = parse_adjustment_patch_json(reopened.currentAssetBoundAdjustmentPatchJson());
    const auto saved = parse_adjustment_patch_json(after);
    require(restored.ok() && saved.ok() && restored.state->layers == saved.state->layers,
            "Reopened recipe retains exact photo pixels and layout transforms");
    CarouselDocumentController boards;
    const auto before_border = editor.currentAssetBoundAdjustmentPatchJson();
    const auto border_history = editor.editHistoryCurrentIndex();
    require(editor.applyBoardPhotoLayout({0, 1}, "stack", {{"margin", 4}, {"spacing", 4}, {"border", 2}}),
            "Apply a spaced layout with subtle white borders");
    settle(editor);
    require(editor.editHistoryCurrentIndex() == border_history + 1 &&
            editor.photoLayerStyles(0).isEmpty() && editor.photoLayerStyles(1).isEmpty() &&
            editor.photoLayers().last().toMap().value("kind") == "fill",
            "One white layout backing and transforms commit together in one Undo entry");
    const auto bordered = editor.currentAssetBoundAdjustmentPatchJson();
    require(editor.applyBoardPhotoLayout({0, 1}, "columns", {{"margin", 4}, {"spacing", 4}, {"border", 3}}),
            "Change the layout and border width");
    settle(editor);
    require(editor.photoLayerStyles(0).isEmpty() && editor.photoLayerCount() == 3,
            "Reapplying a border reuses the backing without adding duplicate strokes or layers");
    editor.undo(); settle(editor);
    require(editor.currentAssetBoundAdjustmentPatchJson() == bordered, "Undo restores the previous layout and exact border");
    editor.undo(); settle(editor);
    require(editor.currentAssetBoundAdjustmentPatchJson() == before_border, "Undo removes the complete layout operation");
    QImage landscape(240, 80, QImage::Format_RGB32); landscape.fill(Qt::red);
    QImage portrait(60, 120, QImage::Format_RGB32); portrait.fill(Qt::blue);
    const auto landscape_path = runtime.filePath("landscape.png");
    const auto portrait_path = runtime.filePath("portrait.png");
    require(landscape.save(landscape_path) && portrait.save(portrait_path), "Save mixed-aspect source images");
    EditorPreviewController source_editor;
    source_editor.loadAsset("source-pixel-preservation", path, {}, {}, {}, {}, {}, true); settle(source_editor);
    require(source_editor.addSourceLayer(landscape_path) && source_editor.addSourceLayer(portrait_path),
            "Place images with different aspect ratios as independent source layers");
    settle(source_editor);
    require(source_editor.applyBoardPhotoLayout({0, 1}, "stack", {{"margin", 4}, {"spacing", 4}, {"border", 2}}),
            "Arrange mixed-aspect images with white borders");
    settle(source_editor);
    const auto full_sources = source_editor.photoLayers();
    require(full_sources[0].toMap().value("pixelWidth").toInt() == 60 &&
            full_sources[0].toMap().value("pixelHeight").toInt() == 120 &&
            full_sources[1].toMap().value("pixelWidth").toInt() == 240 &&
            full_sources[1].toMap().value("pixelHeight").toInt() == 80 &&
            QImage(portrait_path) == portrait && QImage(landscape_path) == landscape,
            "Layout retains the complete original image dimensions and pixels");
    const auto positioned_sources = source_editor.currentAssetBoundAdjustmentPatchJson();
    require(source_editor.setPhotoLayerTransform(0, -20, -30, 0.5, 0),
            "Move the retained source beyond the board without trimming it"); settle(source_editor);
    source_editor.undo(); settle(source_editor);
    require(source_editor.currentAssetBoundAdjustmentPatchJson() == positioned_sources,
            "Undo restores source placement, full source pixels, and border exactly");
    CarouselDocumentController project_canvas;
    require(project_canvas.createEditCanvas({{"name", "Project canvas"}, {"width", 120}, {"height", 80},
            {"projectName", "Canvas project"}}), "Create a canvas within a project");
    const auto project_id = project_canvas.savedDocumentId();
    const auto initial_payload = project_canvas.editBoardPayload(0);
    require(project_canvas.applyAdjustmentPatchToPhotoEditTarget(initial_payload.value("targetKey").toString(), positioned_sources) &&
            project_canvas.saveDocument(), "Persist full source image layers in the project canvas");
    const auto before_resize = project_canvas.editBoardPayload(0);
    require(project_canvas.resizeEditCanvas({{"width", 240}, {"height", 160}}) && project_canvas.projectName() == "Canvas project",
            "Canvas resolution changes retain project ownership");
    const auto resized_payload = project_canvas.editBoardPayload(0);
    const auto resized_recipe = parse_adjustment_patch_json(resized_payload.value("adjustmentPatchJson").toString());
    require(resized_recipe.ok() && resized_recipe.state->layers[0].toMap().value("pixelWidth").toInt() == 60 &&
            resized_recipe.state->layers[0].toMap().value("pixelHeight").toInt() == 120 &&
            qFuzzyCompare(resized_recipe.state->layers[0].toMap().value("scale").toDouble(), full_sources[0].toMap().value("scale").toDouble() * 2),
            "Resizing scales placement while preserving the full original source pixels");
    project_canvas.undo();
    require(project_canvas.editBoardPayload(0).value("filePath") == before_resize.value("filePath") &&
            project_canvas.editBoardPayload(0).value("adjustmentPatchJson") == before_resize.value("adjustmentPatchJson"),
            "One Undo restores the previous canvas source, size, and complete layer recipe");
    project_canvas.redo();
    require(project_canvas.saveDocument(), "Save resized canvas");
    CarouselDocumentController restored_canvas;
    require(restored_canvas.loadDocument(project_id) && restored_canvas.projectName() == "Canvas project" &&
            restored_canvas.editCanvasSettings().value("width").toInt() == 240 &&
            restored_canvas.editBoardPayload(0).value("filePath") == resized_payload.value("filePath"),
            "Project and canvas resolution survive save and reopen");
    bool listed_in_project = false;
    for (const auto& value : restored_canvas.recentDocuments()) {
      const auto document = value.toMap();
      if (document.value("documentId") == project_id) listed_in_project = document.value("projectName") == "Canvas project";
    }
    require(listed_in_project, "The saved canvas Library record retains its project");
    // A developed raster or graphic imported as a board must not receive a
    // second photographic view. Exercise real persistence and rendered pixels,
    // including warm paper and a saturated accent rather than white alone.
    QImage editorial(120, 80, QImage::Format_RGB32);
    editorial.setColorSpace(QColorSpace::SRgb);
    editorial.fill(QColor("#f3eee2"));
    for (int y = 20; y < 60; ++y)
      for (int x = 70; x < 110; ++x) editorial.setPixelColor(x, y, QColor("#c4a13b"));
    const auto editorial_path = runtime.filePath("editorial-board.png");
    require(editorial.save(editorial_path), "Save a developed editorial raster with an embedded sRGB profile");
    CarouselDocumentController imported_editorial;
    require(imported_editorial.createEditCanvas({{"name", "Editorial import"}, {"width", 120}, {"height", 80}}) &&
            imported_editorial.addEditPhotoBoard(editorial_path), "Import the developed page through Add photo board");
    const auto editorial_document_id = imported_editorial.savedDocumentId();
    CarouselDocumentController reopened_editorial;
    require(reopened_editorial.loadDocument(editorial_document_id) && reopened_editorial.slideCount() == 2,
            "The imported page survives save and reopen");
    const auto editorial_render = CarouselPreviewTestAccess::render(reopened_editorial, 1);
    require(!editorial_render.isNull() && editorial_render.size() == editorial.size(),
            "Render the reopened editorial board at its original dimensions");
    for (const auto& point : {QPoint(10, 10), QPoint(90, 40)}) {
      const auto expected = editorial.pixelColor(point);
      const auto actual = editorial_render.pixelColor(point);
      require(std::abs(actual.red() - expected.red()) <= 2 &&
              std::abs(actual.green() - expected.green()) <= 2 &&
              std::abs(actual.blue() - expected.blue()) <= 2,
              "Imported graphic paper and accent colors survive save, reopen and render without a second photographic view");
    }
    require(boards.createBoardsFromEdit(path, "Shared canvas", after, 120, 80, 1, {{"kind", "canvas"}}) &&
            boards.addEditBoard(true), "Extend a real editable board into one shared canvas");
    const auto left = boards.editBoardPayload(0), right = boards.editBoardPayload(1);
    require(left.value("imageId") == right.value("imageId") && left.value("targetKey") == right.value("targetKey") &&
            boards.slides()[0].toMap().value("imageSpanCount").toInt() == 2 &&
            boards.slides()[1].toMap().value("imageSpanStart").toInt() == 0,
            "Both export tiles own one shared image and editable layer recipe");
    EditorPreviewController spread;
    spread.loadAsset("shared-spread", left.value("filePath").toString(), {}, {}, {}, {},
                     left.value("adjustmentPatchJson").toString(), true); settle(spread);
    const auto initial_shared = spread.currentAssetBoundAdjustmentPatchJson();
    require(spread.setPhotoLayerTransform(0, 40, 10, .6, 0), "Move and resize a photo across the shared seam"); settle(spread);
    const auto moved_shared = spread.currentAssetBoundAdjustmentPatchJson();
    const auto before_color = parse_adjustment_patch_json(moved_shared);
    require(spread.setPhotoLayerAdjustmentValue(0, "exposure", .8), "Edit the selected photo's colors"); settle(spread);
    const auto colored_shared = spread.currentAssetBoundAdjustmentPatchJson();
    const auto after_color = parse_adjustment_patch_json(colored_shared);
    require(before_color.ok() && after_color.ok() && colored_shared != moved_shared &&
            before_color.state->exposure == after_color.state->exposure &&
            before_color.state->layers[1] == after_color.state->layers[1],
            "Photo color edits leave the other photo and global color recipe unchanged");
    require(spread.addTextLayer("Across the seam"), "Add editable text to the shared canvas"); settle(spread);
    const auto text_shared = spread.currentAssetBoundAdjustmentPatchJson();
    require(spread.addSourceLayer(path), "Add another photo to the shared canvas"); settle(spread);
    const auto complete_shared = spread.currentAssetBoundAdjustmentPatchJson();
    for (const auto& expected : {text_shared, colored_shared, moved_shared, initial_shared}) {
      spread.undo(); settle(spread);
      require(spread.currentAssetBoundAdjustmentPatchJson() == expected, "Each shared-canvas action has exact independent Undo");
    }
    for (int i = 0; i < 4; ++i) { spread.redo(); settle(spread); }
    require(spread.currentAssetBoundAdjustmentPatchJson() == complete_shared &&
            boards.applyAdjustmentPatchToPhotoEditTarget(left.value("targetKey").toString(), complete_shared) &&
            boards.editBoardPayload(0).value("adjustmentPatchJson").toString() == complete_shared &&
            boards.editBoardPayload(1).value("adjustmentPatchJson").toString() == complete_shared && boards.saveDocument(),
            "Movement, resize, photo colors and new content share one persisted recipe across both tiles");
    CarouselDocumentController reopened_boards;
    require(reopened_boards.loadDocument(boards.savedDocumentId()) &&
            reopened_boards.editBoardPayload(1).value("adjustmentPatchJson").toString() == complete_shared &&
            reopened_boards.slides()[1].toMap().value("imageSpanCount").toInt() == 2,
            "Reopening keeps the connected span and all editable content intact");
    QImage full_photo(120, 160, QImage::Format_RGB32); full_photo.fill(Qt::red);
    for (int y = 80; y < 160; ++y)
      for (int x = 0; x < 120; ++x) full_photo.setPixelColor(x, y, Qt::blue);
    const auto full_path = runtime.filePath("full-source.png");
    require(full_photo.save(full_path) && spread.addSourceLayer(full_path), "Place a photo taller than its canvas"); settle(spread);
    const auto before_position = spread.currentAssetBoundAdjustmentPatchJson();
    require(spread.setPhotoLayerTransform(0, 0, -40, 1, 0), "Move the full photo past the canvas edge"); settle(spread);
    const auto moved_photo = spread.photoLayers()[0].toMap();
    const auto exported_path = runtime.filePath("uncropped-photo.png");
    require(moved_photo.value("pixelHeight").toInt() == 160 &&
            spread.photoLayerContainsPoint(0, 30, -20) && spread.exportPhotoLayer(0, exported_path),
            "Moving a source retains all its pixels and selectable geometry outside the canvas");
    const QImage exported(exported_path);
    require(exported.size() == full_photo.size() && exported.pixelColor(30, 20) == QColor(Qt::red) &&
            exported.pixelColor(30, 140) == QColor(Qt::blue) && QImage(full_path) == full_photo,
            "Exporting the moved layer retains both ends of the full photo and leaves its source intact");
    spread.undo(); settle(spread);
    require(spread.currentAssetBoundAdjustmentPatchJson() == before_position,
            "Undo restores only photo placement without changing source content");
    QThreadPool::globalInstance()->waitForDone();
    verify_board_video_sequence(runtime);
    verify_board_video_layout(runtime);
    verify_board_photo_recipe_framing(runtime);
    verify_board_library(runtime);
    std::cout << "Board alpha picking, atomic layout, Undo/Redo and reopen checks passed\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n'; return 1;
  }
  return 0;
}
