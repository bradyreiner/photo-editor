#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iostream>

#include <QApplication>
#include <QColor>
#include <QColorSpace>
#include <QClipboard>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFontDatabase>
#include <QImage>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeData>
#include <QPainter>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUrl>

#include "app_bootstrap.h"
#include "carousel_document_controller.h"
#include "decoded_image_cache.h"
#include "edit_adjustment_patch.h"
#include "photo_point_text.h"

namespace {

using photo_editor::desktop::CarouselDocumentController;

int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

bool wait_until(const std::function<bool()>& predicate, int timeout_ms = 10000) {
  QElapsedTimer timer;
  timer.start();
  while (!predicate() && timer.elapsed() < timeout_ms) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
  }
  return predicate();
}

QString write_image(const QDir& directory, const QString& name, const QImage& image) {
  const auto path = directory.filePath(name);
  if (!image.save(path)) {
    return {};
  }
  return path;
}

bool write_json(const QString& path, const QJsonObject& object) {
  QDir().mkpath(QFileInfo(path).absolutePath());
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    return false;
  }
  return file.write(QJsonDocument(object).toJson(QJsonDocument::Indented)) >= 0;
}

double probe_media_duration(const QString& ffprobe, const QString& path) {
  QProcess process;
  process.start(
      ffprobe,
      {QStringLiteral("-v"), QStringLiteral("error"),
       QStringLiteral("-show_entries"), QStringLiteral("format=duration"),
       QStringLiteral("-of"), QStringLiteral("default=noprint_wrappers=1:nokey=1"),
       path});
  if (!process.waitForStarted(5000) || !process.waitForFinished(30000) ||
      process.exitCode() != 0) {
    return -1.0;
  }
  bool ok = false;
  const auto duration = QString::fromUtf8(process.readAllStandardOutput()).trimmed().toDouble(&ok);
  return ok ? duration : -1.0;
}

bool media_has_audio_stream(const QString& ffprobe, const QString& path) {
  QProcess process;
  process.start(
      ffprobe,
      {QStringLiteral("-v"), QStringLiteral("error"),
       QStringLiteral("-select_streams"), QStringLiteral("a"),
       QStringLiteral("-show_entries"), QStringLiteral("stream=index"),
       QStringLiteral("-of"), QStringLiteral("csv=p=0"), path});
  if (!process.waitForStarted(5000) || !process.waitForFinished(30000) ||
      process.exitCode() != 0) {
    return false;
  }
  return !QString::fromUtf8(process.readAllStandardOutput()).trimmed().isEmpty();
}

QJsonObject read_json(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    return {};
  }
  const auto document = QJsonDocument::fromJson(file.readAll());
  return document.isObject() ? document.object() : QJsonObject{};
}

QJsonObject read_stored_carousel(const QString& document_id) {
  QProcess process;
  QProcessEnvironment environment;
  if (photo_editor::desktop::AppBootstrap::prepareProcessEnvironment(
          &environment,
          nullptr)) {
    process.setProcessEnvironment(environment);
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(
        photo_editor::desktop::AppBootstrap::helperProgram(
            QStringLiteral("catalog_carousel_documents")),
        photo_editor::desktop::AppBootstrap::helperArguments(
            QStringLiteral("catalog_carousel_documents"),
            {QStringLiteral("get"), document_id}));
    if (process.waitForStarted(5000) &&
        process.waitForFinished(5000) &&
        process.exitCode() == 0) {
      const auto document =
          QJsonDocument::fromJson(
              process.readAllStandardOutput());
      if (document.isObject() &&
          (document.object().value(
               QStringLiteral("document_json"))
               .isObject() ||
           document.object().value(
               QStringLiteral("document"))
               .isObject())) {
        return document.object();
      }
    }
  }
  const auto fallback_root = QString::fromUtf8(
      qgetenv(
          "PHOTO_EDITOR_CAROUSEL_DOCUMENTS_PATH"));
  const auto fallback_path =
      QDir(fallback_root)
          .filePath(
              QStringLiteral("documents/%1.json")
                  .arg(document_id));
  auto fallback = read_json(fallback_path);
  if (!fallback.value(
           QStringLiteral("document_json"))
           .isObject() &&
      fallback.value(QStringLiteral("document"))
          .isObject()) {
    fallback.insert(
        QStringLiteral("document_json"),
        fallback.value(QStringLiteral("document")));
  }
  return fallback;
}

QImage split_image(const QSize& size, const QColor& left, const QColor& right) {
  QImage image(size, QImage::Format_ARGB32);
  image.fill(Qt::black);
  for (int y = 0; y < size.height(); ++y) {
    for (int x = 0; x < size.width(); ++x) {
      image.setPixelColor(x, y, x < (size.width() / 2) ? left : right);
    }
  }
  return image;
}

QImage quadrant_image(
    const QSize& size,
    const QColor& top_left,
    const QColor& top_right,
    const QColor& bottom_left,
    const QColor& bottom_right) {
  QImage image(size, QImage::Format_ARGB32);
  image.fill(Qt::black);
  for (int y = 0; y < size.height(); ++y) {
    for (int x = 0; x < size.width(); ++x) {
      if (x < size.width() / 2) {
        image.setPixelColor(x, y, y < size.height() / 2 ? top_left : bottom_left);
      } else {
        image.setPixelColor(x, y, y < size.height() / 2 ? top_right : bottom_right);
      }
    }
  }
  return image;
}

bool color_matches(const QColor& actual, const QColor& expected) {
  return actual.red() == expected.red() && actual.green() == expected.green() &&
         actual.blue() == expected.blue();
}

bool color_is_dominantly_red(const QColor& color) {
  return color.red() >= color.green() + 80 &&
         color.red() >= color.blue() + 80;
}

bool color_is_dominantly_blue(const QColor& color) {
  return color.blue() >= color.red() + 100 &&
         color.blue() >= color.green() + 50;
}

bool color_is_dominantly_green(const QColor& color) {
  return color.green() >= color.red() + 50 &&
         color.green() >= color.blue() + 50;
}

bool color_is_dominantly_yellow(const QColor& color) {
  return color.red() >= color.blue() + 100 &&
         color.green() >= color.blue() + 100 &&
         std::abs(color.red() - color.green()) <= 80;
}

bool color_is_dominantly_orange(const QColor& color) {
  return color.red() >= color.green() + 40 &&
         color.green() >= color.blue() + 60;
}

bool color_is_dominantly_cyan(const QColor& color) {
  return color.blue() >= color.red() + 60 &&
         color.green() >= color.red() + 40;
}

QVariantMap first_preflight_issue(
    const QVariantList& issues,
    const QString& code,
    const QString& item_id = QString()) {
  for (const auto& value : issues) {
    const auto issue = value.toMap();
    if (issue.value(QStringLiteral("code")).toString() == code &&
        (item_id.isEmpty() || issue.value(QStringLiteral("itemId")).toString() == item_id)) {
      return issue;
    }
  }
  return {};
}

int preflight_issue_count(const QVariantList& issues, const QString& code) {
  return static_cast<int>(std::count_if(
      issues.cbegin(), issues.cend(), [&code](const QVariant& value) {
        return value.toMap().value(QStringLiteral("code")).toString() == code;
      }));
}

bool region_contains_non_background(
    const QImage& image,
    const QRect& region,
    const QColor& background) {
  const auto bounded = region.intersected(image.rect());
  for (int y = bounded.top(); y <= bounded.bottom(); ++y) {
    for (int x = bounded.left(); x <= bounded.right(); ++x) {
      if (!color_matches(image.pixelColor(x, y), background)) {
        return true;
      }
    }
  }
  return false;
}

QRect non_background_bounds(const QImage& image, const QRect& region, const QColor& background) {
  const auto bounded = region.intersected(image.rect());
  QRect bounds;
  for (int y = bounded.top(); y <= bounded.bottom(); ++y) {
    for (int x = bounded.left(); x <= bounded.right(); ++x) {
      if (!color_matches(image.pixelColor(x, y), background)) {
        const QPoint point(x, y);
        bounds = bounds.isNull() ? QRect(point, QSize(1, 1)) : bounds.united(QRect(point, QSize(1, 1)));
      }
    }
  }
  return bounds;
}

int run_creative_ux_linked_text_test(const QDir& dir) {
  const auto source = write_image(
      dir, QStringLiteral("creative-ux-linked-text.png"),
      split_image(QSize(320, 240), QColor(QStringLiteral("#182b42")),
                  QColor(QStringLiteral("#947044"))));
  photo_editor::desktop::PhotoTextStyle text_style;
  text_style.family = QStringLiteral("Helvetica");
  text_style.size = 28.0;
  text_style.color = QColor(QStringLiteral("#fff4d8"));
  text_style.point_text = true;
  QVariantMap text_layer{
      {QStringLiteral("id"), QStringLiteral("creative-ux-tour-dates")},
      {QStringLiteral("kind"), QStringLiteral("text")},
      {QStringLiteral("text"), QStringLiteral("July 10–12, 2026")},
      {QStringLiteral("locked"), false},
      {QStringLiteral("x"), 40.0},
      {QStringLiteral("y"), 36.0},
      {QStringLiteral("pixelWidth"), 240},
      {QStringLiteral("pixelHeight"), 56},
      {QStringLiteral("textOriginX"), 0.0},
      {QStringLiteral("textOriginY"), 0.0},
      {QStringLiteral("scale"), 1.0},
      {QStringLiteral("rotation"), 0.0},
      {QStringLiteral("textTypography"),
       photo_editor::desktop::encode_photo_text_style(text_style).toVariantMap()},
  };
  if (!photo_editor::desktop::fit_photo_point_text(&text_layer)) {
    return fail("Focused shared-text fixture should fit its native text layer");
  }
  CarouselDocumentController::RecipeState recipe;
  recipe.layers = {text_layer};
  const auto base_patch = photo_editor::desktop::serialize_adjustment_patch_json(
      recipe, photo_editor::desktop::all_adjustment_groups(),
      photo_editor::desktop::AdjustmentPatchScope::AssetBound);
  CarouselDocumentController controller;
  if (source.isEmpty() ||
      !controller.createBoardsFromEdit(source, QStringLiteral("Guided tour"), base_patch,
          320, 240, 1) ||
      !controller.createBoardVariant()) {
    return fail("Focused shared-text fixture should create a source board and variant");
  }

  const auto layer_id = QStringLiteral("creative-ux-tour-dates");
  const auto source_board_id = controller.slides().at(0).toMap()
      .value(QStringLiteral("boardId")).toString();
  const auto variant_board_id = controller.slides().at(1).toMap()
      .value(QStringLiteral("boardId")).toString();
  controller.editBoardPayload(0);
  const auto candidates = controller.photoTextLinkCandidates(layer_id);
  if (source_board_id.isEmpty() || variant_board_id.isEmpty() ||
      source_board_id == variant_board_id || candidates.size() != 2 ||
      controller.photoTextLinkState(layer_id).value(QStringLiteral("linked")).toBool() ||
      !controller.linkEditBoardPhotoText(layer_id, QStringLiteral("Tour dates"),
          {variant_board_id})) {
    return fail("Focused shared-text fixture should explicitly link the two matching board layers");
  }

  const auto patch_text = [&controller](int board, const QString& text) {
    const auto payload = controller.editBoardPayload(board);
    const auto decoded = photo_editor::desktop::parse_adjustment_patch_json(
        payload.value(QStringLiteral("adjustmentPatchJson")).toString());
    if (!decoded.ok() || decoded.state->layers.isEmpty()) return false;
    auto state = *decoded.state;
    auto layer = state.layers.constFirst().toMap();
    layer.insert(QStringLiteral("text"), text);
    if (layer.contains(QStringLiteral("textTypography")) &&
        !photo_editor::desktop::fit_photo_point_text(&layer)) return false;
    state.layers[0] = layer;
    const auto patch = photo_editor::desktop::serialize_adjustment_patch_json(
        state, decoded.groups, photo_editor::desktop::AdjustmentPatchScope::AssetBound);
    return controller.applyAdjustmentPatchToPhotoEditTarget(
        payload.value(QStringLiteral("targetKey")).toString(), patch);
  };
  const auto text_on = [&controller](int board) {
    const auto decoded = photo_editor::desktop::parse_adjustment_patch_json(
        controller.editBoardPayload(board).value(QStringLiteral("adjustmentPatchJson")).toString());
    return decoded.ok() && !decoded.state->layers.isEmpty()
        ? decoded.state->layers.constFirst().toMap().value(QStringLiteral("text")).toString()
        : QString{};
  };
  if (!patch_text(1, QStringLiteral("July 18–20, 2026")) ||
      text_on(0) != QStringLiteral("July 18–20, 2026") ||
      !patch_text(0, QStringLiteral("July 25–27, 2026")) ||
      text_on(1) != QStringLiteral("July 25–27, 2026")) {
    return fail("Shared native text should fan out from either board");
  }
  controller.undo();
  if (text_on(0) != QStringLiteral("July 18–20, 2026") ||
      text_on(1) != QStringLiteral("July 18–20, 2026")) {
    return fail("Undo should restore the shared text on both boards as one edit");
  }
  controller.redo();
  if (text_on(0) != QStringLiteral("July 25–27, 2026") ||
      text_on(1) != QStringLiteral("July 25–27, 2026")) {
    return fail("Redo should restore the shared text on both boards as one edit");
  }

  auto variant_payload = controller.editBoardPayload(1);
  auto variant_recipe = photo_editor::desktop::parse_adjustment_patch_json(
      variant_payload.value(QStringLiteral("adjustmentPatchJson")).toString());
  if (!variant_recipe.ok() || variant_recipe.state->layers.isEmpty()) {
    return fail("Focused shared-text peer should retain a valid recipe before locking");
  }
  auto variant_state = *variant_recipe.state;
  auto variant_layer = variant_state.layers.constFirst().toMap();
  variant_layer.insert(QStringLiteral("locked"), true);
  variant_state.layers[0] = variant_layer;
  const auto locked_patch = photo_editor::desktop::serialize_adjustment_patch_json(
      variant_state, variant_recipe.groups,
      photo_editor::desktop::AdjustmentPatchScope::AssetBound);
  if (!controller.applyAdjustmentPatchToPhotoEditTarget(
          variant_payload.value(QStringLiteral("targetKey")).toString(), locked_patch)) {
    return fail("Focused shared-text peer should allow an explicit lock before rejection test");
  }
  const auto before_rejection = controller.documentContentHash();
  if (patch_text(0, QStringLiteral("August 1–3, 2026")) ||
      controller.documentContentHash() != before_rejection ||
      !controller.status().contains(QStringLiteral("Unlock the matching text"))) {
    return fail("A locked peer should reject shared text changes without a partial source update");
  }

  const auto persisted_document = controller.savedDocumentId();
  if (!controller.saveDocument() || persisted_document.isEmpty()) {
    return fail("Focused shared-text fixture should persist its link group");
  }
  CarouselDocumentController reopened;
  if (!reopened.loadDocument(persisted_document)) {
    return fail("Focused shared-text fixture should reopen successfully");
  }
  reopened.selectSlide(0);
  if (!reopened.photoTextLinkState(layer_id).value(QStringLiteral("linked")).toBool()) {
    return fail("Reopened document should retain its opt-in shared-text group");
  }
  return EXIT_SUCCESS;
}

}  // namespace

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));
  QApplication app(argc, argv);

  QTemporaryDir workspace;
  if (!workspace.isValid()) {
    return fail("temporary workspace should be created");
  }
  const QDir dir(workspace.path());
  qputenv(
      "PHOTO_EDITOR_CAROUSEL_DESIGN_LIBRARY_PATH",
      QFile::encodeName(dir.filePath(QStringLiteral("carousel-design-library.json"))));
  qputenv(
      "PHOTO_EDITOR_CAROUSEL_DOCUMENTS_PATH",
      QFile::encodeName(dir.filePath(QStringLiteral("carousel-documents"))));
  qputenv(
      "PHOTO_EDITOR_APP_DATA_DIR",
      QFile::encodeName(dir.filePath(QStringLiteral("app-data"))));

  if (app.arguments().contains(QStringLiteral("--creative-ux"))) {
    qputenv("PHOTO_EDITOR_DATABASE_URL", QByteArrayLiteral("postgres://example/test"));
    return run_creative_ux_linked_text_test(dir);
  }

  if (app.arguments().contains(QStringLiteral("--edit-boards"))) {
    qputenv("PHOTO_EDITOR_DATABASE_URL", QByteArrayLiteral("postgres://example/test"));
    const auto source = write_image(dir, QStringLiteral("edit-source.png"),
        split_image(QSize(640, 400), QColor("#c09055"), QColor("#334477")));
    CarouselDocumentController::RecipeState state;
    state.exposure = 0.45;
    state.layers = {QVariantMap{{"id", "editable-shape"}, {"kind", "shape"},
        {"visible", true}, {"opacity", 1.0}, {"x", 50.0}, {"y", 30.0},
        {"width", 120.0}, {"height", 80.0}, {"fillColor", "#ff3344"}}};
    const auto patch = photo_editor::desktop::serialize_adjustment_patch_json(state,
        photo_editor::desktop::all_adjustment_groups(), photo_editor::desktop::AdjustmentPatchScope::AssetBound);
    CarouselDocumentController controller;
    controller.createCarouselWithSettings({{"carouselName", "Existing document"}, {"slideCount", 1}});
    controller.addTextItem("Keep this existing work");
    if (!controller.saveDocument()) return fail("Save existing board document");
    const auto previous = controller.savedDocumentId();
    controller.addTextItem("Preserve this pending change too");
    if (!controller.createBoardsFromEdit(source, "Editable canvas", patch, 640, 400) ||
        controller.savedDocumentId().isEmpty() || controller.savedDocumentId() == previous ||
        controller.documentDirty() || controller.slideCount() != 2 || controller.currentIndex() != 0 ||
        controller.documentPixelWidth() != 1280 || controller.documentPixelHeight() != 400)
      { std::cerr << controller.status().toStdString() << " id=" << controller.savedDocumentId().toStdString()
                  << " previous=" << previous.toStdString() << " size=" << controller.documentPixelWidth() << "x" << controller.documentPixelHeight()
                  << " count=" << controller.slideCount() << " dirty=" << controller.documentDirty() << "\n";
        return fail("Adding boards saves a new exact-size two-board document"); }
    const auto saved = controller.savedDocumentId();
    const auto decoded = photo_editor::desktop::parse_adjustment_patch_json(
        controller.selectedPhotoEditPayload().value("adjustmentPatchJson").toString());
    if (!decoded.ok() || !(*decoded.state == state)) return fail("Board 1 preserves the entire editable recipe");
    if (controller.createBoardsFromEdit(source, "Invalid", "{}", 640, 400) ||
        controller.savedDocumentId() != saved || controller.slideCount() != 2)
      return fail("Invalid conversion cannot replace the current document");
    controller.selectSlide(1);
    if (!controller.selectedPhotoEditPayload().value("filePath").toString().isEmpty())
      return fail("Board 2 starts without copied photo content");
    controller.addShapeItem();
    controller.addTextItem("Board 2 type");
    if (!controller.saveDocument()) return fail("Save independent board 2 content");
    CarouselDocumentController reopened;
    if (!reopened.loadDocument(saved) || reopened.slideCount() != 2 ||
        reopened.documentPixelWidth() != 1280 || reopened.documentPixelHeight() != 400)
      return fail("Saved boards reopen with their exact dimensions");
    reopened.selectSlide(0);
    const auto reloaded = photo_editor::desktop::parse_adjustment_patch_json(
        reopened.selectedPhotoEditPayload().value("adjustmentPatchJson").toString());
    if (!reloaded.ok() || !(*reloaded.state == state)) return fail("Reopening retains the original editable source layers");
    reopened.selectSlide(1);
    if (reopened.canvasItems().size() != 2) return fail("New board graphics persist independently");
    if (!reopened.loadDocument(previous) || reopened.canvasItems().size() != 2)
      return fail("The previous board document remains saved and intact");
    state.crop_rect_width = 0.5;
    state.crop_rect_height = 0.5;
    const auto cropped_patch = photo_editor::desktop::serialize_adjustment_patch_json(state,
        photo_editor::desktop::all_adjustment_groups(), photo_editor::desktop::AdjustmentPatchScope::AssetBound);
    if (!reopened.createBoardsFromEdit(source, "Cropped edit", cropped_patch, 640, 400) ||
        reopened.documentPixelWidth() != 640 || reopened.documentPixelHeight() != 200)
      return fail("Adding boards preserves the displayed crop dimensions");
    const auto cropped_id = reopened.savedDocumentId();
    if (!reopened.loadDocument(cropped_id) || reopened.documentPixelWidth() != 640 || reopened.documentPixelHeight() != 200)
      return fail("Cropped board dimensions survive reopening");
    if (!reopened.loadDocument(saved) || !reopened.prepareEditBoards()) return fail("Prepare saved layout boards for the standard Edit tools");
    const auto board0 = reopened.editBoardPayload(0);
    const auto board1 = reopened.editBoardPayload(1);
    const auto board1_state = photo_editor::desktop::parse_adjustment_patch_json(board1.value("adjustmentPatchJson").toString());
    if (board0.value("boardId").toString().isEmpty() || board0.value("boardId") == board1.value("boardId") ||
        !board1_state.ok() || board1_state.state->layers.size() != 2 || !reopened.canvasItems().isEmpty())
      return fail("Each existing board gets its own base and imported graphic layers");
    if (!reopened.addEditBoard(false) || reopened.slideCount() != 3 || reopened.currentIndex() != 2)
      return fail("Add an independent blank board directly to an Edit document");
    const auto blank = reopened.editBoardPayload(2);
    const QImage blank_source(blank.value("filePath").toString());
    const auto blank_state = photo_editor::desktop::parse_adjustment_patch_json(blank.value("adjustmentPatchJson").toString());
    if (blank_source.size() != QSize(640,400) || blank_source.pixelColor(20,20).alpha() != 0 ||
        !blank_state.ok() || !blank_state.state->layers.isEmpty() || blank.value("filePath") == board1.value("filePath"))
      return fail("A new board has a separate transparent base and empty layer stack");
    auto edge_state = *blank_state.state;
    QFile pixel_file(source);
    if (!pixel_file.open(QIODevice::ReadOnly)) return fail("Read edge fixture pixels");
    edge_state.layers = {QVariantMap{{"id", "edge-shape"}, {"kind", "pixel"}, {"pixelData", pixel_file.readAll()},
        {"pixelWidth", 40}, {"pixelHeight", 30}, {"canvasWidth", 640}, {"canvasHeight", 400},
        {"x", 300}, {"y", 180}, {"enabled", true}, {"opacity", 1.0}, {"scale", 1.0}}};
    const auto edge_patch = photo_editor::desktop::serialize_adjustment_patch_json(edge_state,
        photo_editor::desktop::all_adjustment_groups(), photo_editor::desktop::AdjustmentPatchScope::AssetBound);
    if (!reopened.applyAdjustmentPatchToPhotoEditTarget(blank.value("targetKey").toString(), edge_patch) ||
        !reopened.addEditBoard(true) || reopened.slideCount() != 4)
      return fail("Extend an existing canvas into an adjoining board");
    const auto extended = reopened.editBoardPayload(3);
    const auto shared = reopened.editBoardPayload(2);
    const auto extended_state = photo_editor::desktop::parse_adjustment_patch_json(extended.value("adjustmentPatchJson").toString());
    if (!extended_state.ok() || !(*extended_state.state == edge_state) ||
        extended.value("targetKey") != shared.value("targetKey") || extended.value("filePath") != shared.value("filePath") ||
        extended.value("imageId") != shared.value("imageId") || extended.value("boardId") == shared.value("boardId") ||
        !reopened.slides().at(3).toMap().value("joinedPrevious").toBool())
      return fail("Extension shares the exact original image and editable recipe across distinct export tiles");
    reopened.editBoardPayload(3);
    edge_state.exposure = 0.3;
    const auto shared_patch = photo_editor::desktop::serialize_adjustment_patch_json(edge_state,
        photo_editor::desktop::all_adjustment_groups(), photo_editor::desktop::AdjustmentPatchScope::AssetBound);
    if (!reopened.applyAdjustmentPatchToPhotoEditTarget(extended.value("targetKey").toString(), shared_patch) || reopened.currentIndex() != 3)
      return fail("Editing a later tile must retain the active board");
    if (reopened.editBoardPayload(2).value("adjustmentPatchJson") != shared_patch)
      return fail("Changes on an extended tile are live on the original tile");
    if (!reopened.addEditBoard(false) || reopened.slideCount() != 5 || reopened.savedDocumentId() != saved ||
        reopened.editBoardPayload(3).value("targetKey") != extended.value("targetKey"))
      return fail("Adding an independent board retains the shared image and carousel identity");
    reopened.editBoardPayload(3);
    if (!reopened.removeEditBoard() || reopened.slideCount() != 4 || reopened.savedDocumentId() != saved ||
        reopened.editBoardPayload(2).value("imageId") != extended.value("imageId") ||
        reopened.editBoardPayload(2).value("adjustmentPatchJson") != shared_patch)
      return fail("Removing one extended tile keeps the other tile and its entire shared recipe");
    reopened.undo();
    if (reopened.slideCount() != 5 || reopened.editBoardPayload(3).value("imageId") != extended.value("imageId"))
      return fail("Undo board removal restores the shared image membership");
    reopened.redo();
    reopened.editBoardPayload(2);
    if (!reopened.addEditBoard(true) || reopened.slideCount() != 5 ||
        reopened.editBoardPayload(3).value("imageId") != extended.value("imageId"))
      return fail("Extending again preserves the image identity and all editable layers");
    reopened.setCurrentSlideName("Continuation");
    if (!reopened.saveDocument() || !reopened.loadDocument(saved) || reopened.slideCount() != 5 ||
        reopened.editBoardPayload(3).value("adjustmentPatchJson") != shared_patch ||
        reopened.slides().at(3).toMap().value("boardName").toString() != QStringLiteral("Continuation"))
      return fail("Shared photo, layers, board identity and names survive reopening");
    const auto export_dir = dir.filePath("shared-export");
    QDir().mkpath(export_dir);
    reopened.setExportImageFormat("png");
    reopened.setExportManifestEnabled(false);
    if (!reopened.exportCarouselToDirectory(export_dir)) { std::cerr << reopened.status().toStdString(); return fail("Export shared tiles"); }
    const auto outputs = QDir(export_dir).entryList({"*.png"}, QDir::Files, QDir::Name);
    if (outputs.size() != 5) return fail("Every board exports as a separate image");
    QImage stitched(1280,400,QImage::Format_RGBA8888);
    stitched.fill(Qt::transparent);
    QPainter stitch(&stitched);
    for (int tile=0; tile<2; ++tile) {
      const QImage pixels(QDir(export_dir).filePath(outputs.at(2+tile)));
      if (pixels.size() != QSize(640,400)) return fail("All carousel outputs use the same board dimensions");
      stitch.drawImage(tile*640,0,pixels);
    }
    stitch.end();
    CarouselDocumentController reference;
    if (!reference.createBoardsFromEdit(blank.value("filePath").toString(), "Reference", shared_patch,1280,400,1))
      return fail("Create unsplit reference composition");
    reference.setCurrentFitMode("cover");
    reference.setCurrentMediaCropMode("manual");
    reference.setExportManifestEnabled(false);
    const auto reference_dir = dir.filePath("reference-export");
    QDir().mkpath(reference_dir);
    if (!reference.exportCarouselToDirectory(reference_dir)) return fail("Export unsplit reference composition");
    const auto reference_files = QDir(reference_dir).entryList({"*.png"},QDir::Files);
    const auto reference_pixels = QImage(QDir(reference_dir).filePath(reference_files.first())).convertToFormat(QImage::Format_RGBA8888);
    if (stitched.size() != reference_pixels.size()) return fail("Continuous reference dimensions");
    for (int row=0; row<stitched.height(); ++row) {
      if (std::memcmp(stitched.constScanLine(row),reference_pixels.constScanLine(row),static_cast<std::size_t>(stitched.width()*4)) != 0) {
        std::cerr << "Split mismatch at row " << row << " stitched=" << stitched.pixelColor(640,row).name().toStdString()
                  << " reference=" << reference_pixels.pixelColor(640,row).name().toStdString() << '\n';
        return fail("Stitched tile pixels must exactly match the single continuous edited image");
      }
    }
    if (!reference.createEditCanvas({{"name","Unified canvas"},{"width",640},{"height",400}}) ||
        reference.slideCount() != 1 || reference.editBoardPayload(0).value("boardId").toString().isEmpty() ||
        !reference.addEditBoard(true) || reference.slideCount() != 2)
      return fail("A new canvas is the first board of an extendable saved document");
    if (!reference.addEditPhotoBoard(source) || reference.slideCount() != 3 ||
        reference.editBoardPayload(2).value("filePath").toString() != source || !reference.addEditBoard(true) ||
        reference.editBoardPayload(3).value("filePath").toString() != source)
      return fail("A different photo joins the same-ratio carousel with its own base and live extension");
    QImage replacement_portrait(400,640,QImage::Format_ARGB32);
    replacement_portrait.fill(Qt::white);
    { QPainter painter(&replacement_portrait); painter.fillRect(150,270,100,100,QColor("#338833")); }
    const auto replacement_path = write_image(dir, QStringLiteral("portrait-replacement.png"), replacement_portrait);
    reference.editBoardPayload(2);
    if (!reference.applyAdjustmentPatchToPhotoEditTarget(reference.editBoardPayload(2).value("targetKey").toString(), shared_patch))
      return fail("Prepare editable shared graphics before replacing the base");
    const auto replacement_before = reference.editBoardPayload(2);
    const auto replacement_board_ids = reference.slides();
    if (reference.replaceEditBoardImage(dir.filePath("missing.png")) ||
        reference.editBoardPayload(2).value("filePath") != replacement_before.value("filePath"))
      return fail("Unreadable replacement images leave the shared board unchanged");
    if (!reference.replaceEditBoardImage(QUrl::fromLocalFile(replacement_path).toString()) ||
        !reference.editBoardImageBusy() || !wait_until([&] { return !reference.editBoardImageBusy(); }))
      return fail("A dropped file URL replaces the base asynchronously");
    const auto replaced = reference.editBoardPayload(2);
    const QImage replaced_pixels(replaced.value("filePath").toString());
    if (replaced.value("filePath") == replacement_before.value("filePath") || replaced_pixels.size() != QSize(640, 400) ||
        replaced.value("adjustmentPatchJson") != replacement_before.value("adjustmentPatchJson") ||
        reference.editBoardPayload(3).value("filePath") != replaced.value("filePath") ||
        reference.editBoardPayload(3).value("imageId") != replacement_before.value("imageId") ||
        reference.slides().at(2).toMap().value("boardId") != replacement_board_ids.at(2).toMap().value("boardId") ||
        reference.slides().at(3).toMap().value("boardId") != replacement_board_ids.at(3).toMap().value("boardId"))
      return fail("Replacement keeps exact layer geometry, shared image identity and separate board IDs");
    int green_width=0;
    int green_height=0;
    const auto is_green=[](const QColor& pixel) {return pixel.green()>100 && pixel.red()<100 && pixel.blue()<100;};
    for (int x=0;x<replaced_pixels.width();++x) green_width+=is_green(replaced_pixels.pixelColor(x,200));
    for (int y=0;y<replaced_pixels.height();++y) green_height+=is_green(replaced_pixels.pixelColor(320,y));
    if (std::abs(green_width-green_height)>2 || green_width<150 || replaced_pixels.pixelColor(0,0).alpha()!=255)
      return fail("The replacement fills the canonical canvas while keeping photo proportions");
    if (reference.undoEditBoardImageReplacement(QStringLiteral("invalid baseline")) ||
        reference.editBoardPayload(2).value("filePath") != replaced.value("filePath"))
      return fail("Replacement undo rejects a mismatched baseline without touching history");
    auto replacement_edit = *photo_editor::desktop::parse_adjustment_patch_json(
        replaced.value("adjustmentPatchJson").toString()).state;
    replacement_edit.exposure += 0.2;
    if (!reference.applyAdjustmentPatchToPhotoEditTarget(replaced.value("targetKey").toString(),
            photo_editor::desktop::serialize_adjustment_patch_json(replacement_edit,
                photo_editor::desktop::all_adjustment_groups(), photo_editor::desktop::AdjustmentPatchScope::AssetBound)) ||
        !reference.applyAdjustmentPatchToPhotoEditTarget(replaced.value("targetKey").toString(),
            replaced.value("adjustmentPatchJson").toString()) ||
        !reference.undoEditBoardImageReplacement(replaced.value("adjustmentPatchJson").toString()))
      return fail("Returning to the replacement baseline can undo the source through mirrored photo history");
    if (reference.editBoardPayload(2).value("filePath") != replacement_before.value("filePath"))
      return fail("One undo restores the original shared base image");
    reference.redo();
    if (reference.editBoardPayload(3).value("filePath") != replaced.value("filePath") ||
        !reference.saveDocument() || !reference.loadDocument(reference.savedDocumentId()) ||
        reference.editBoardPayload(2).value("filePath") != replaced.value("filePath"))
      return fail("The replaced base survives redo, save and reopening");
    const auto order_ids = [](const CarouselDocumentController& document) {
      QStringList ids;
      for (const auto& board : document.slides()) ids.push_back(board.toMap().value("boardId").toString());
      return ids;
    };
    const auto original_order = order_ids(reference);
    const auto selected_tile = reference.editBoardPayload(3).value("boardId");
    const auto move_recipe = reference.editBoardPayload(3).value("adjustmentPatchJson").toString();
    const auto before_move_export = dir.filePath("before-move-export");
    QDir().mkpath(before_move_export);
    reference.setExportImageFormat("png");
    reference.setExportManifestEnabled(false);
    if (!reference.exportCarouselToDirectory(before_move_export)) return fail("Export the move baseline");
    const auto before_files = QDir(before_move_export).entryList({"*.png"}, QDir::Files, QDir::Name);
    const auto history_label = reference.undoText();
    if (reference.moveEditBoard(-1,0) || reference.moveEditBoard(0,3) || reference.moveEditBoard(3,2) ||
        order_ids(reference) != original_order || reference.undoText() != history_label)
      return fail("Invalid, unchanged and split-image insertion points leave order and history intact");
    if (!reference.moveEditBoard(3,0) || order_ids(reference) != QStringList{original_order.at(2),original_order.at(3),original_order.at(0),original_order.at(1)} ||
        reference.currentIndex()!=1 || reference.slides().at(1).toMap().value("boardId")!=selected_tile ||
        reference.slides().at(0).toMap().value("imageSpanCount").toInt()!=2 ||
        reference.slides().at(1).toMap().value("imageSpanStart").toInt()!=0 ||
        reference.editBoardPayload(1).value("adjustmentPatchJson").toString()!=move_recipe)
      return fail("Dragging either shared tile moves the entire image and preserves the active tile and editable stack");
    const auto moved_order = order_ids(reference);
    const auto after_move_export = dir.filePath("after-move-export");
    QDir().mkpath(after_move_export);
    if (!reference.exportCarouselToDirectory(after_move_export)) return fail("Export reordered boards");
    const auto after_files = QDir(after_move_export).entryList({"*.png"}, QDir::Files, QDir::Name);
    if (before_files.size()!=4 || after_files.size()!=4) return fail("Reordered boards still export individually");
    for (int tile=0;tile<2;++tile) {
      const QImage before(QDir(before_move_export).filePath(before_files.at(tile+2)));
      const QImage after(QDir(after_move_export).filePath(after_files.at(tile)));
      if (before.isNull() || before.convertToFormat(QImage::Format_RGBA8888)!=after.convertToFormat(QImage::Format_RGBA8888))
        return fail("Reordering changes export order without changing shared-tile pixels or seams");
    }
    if (!reference.canUndoEditBoardMove() || !reference.undoEditBoardMove() || order_ids(reference)!=original_order)
      return fail("One undo restores the complete board order");
    reference.redo();
    auto move_edit = *photo_editor::desktop::parse_adjustment_patch_json(move_recipe).state;
    move_edit.exposure += 0.2;
    const auto move_target = reference.editBoardPayload(1).value("targetKey").toString();
    if (!reference.applyAdjustmentPatchToPhotoEditTarget(move_target,
            photo_editor::desktop::serialize_adjustment_patch_json(move_edit,
                photo_editor::desktop::all_adjustment_groups(), photo_editor::desktop::AdjustmentPatchScope::AssetBound)) ||
        reference.canUndoEditBoardMove() || reference.undoEditBoardMove())
      return fail("Reorder undo cannot discard later photo edits");
    if (!reference.applyAdjustmentPatchToPhotoEditTarget(move_target,move_recipe)) return fail("Restore the mirrored edit baseline");
    reference.selectSlide(2);
    if (!reference.canUndoEditBoardMove() || !reference.undoEditBoardMove() || order_ids(reference)!=original_order)
      return fail("Undo reaches the earlier move after photo edits return to baseline, regardless of selected board");
    reference.redo();
    if (order_ids(reference)!=moved_order || !reference.saveDocument() || !reference.loadDocument(reference.savedDocumentId()) ||
        order_ids(reference)!=moved_order || reference.editBoardPayload(0).value("adjustmentPatchJson").toString()!=move_recipe)
      return fail("Reordered identities and shared layers survive redo, save and reopening");
    CarouselDocumentController legacy;
    legacy.createCarouselWithSettings({{"primaryMediaPath",source},{"slideCount",1}});
    legacy.applyTemplateToCurrent("linked-horizontal-pair");
    const auto legacy_target = legacy.selectedPhotoEditPayload().value("targetKey");
    if (legacy.slideCount()!=2 || !legacy.prepareEditBoards() ||
        legacy.editBoardPayload(1).value("targetKey")!=legacy_target ||
        legacy.editBoardPayload(0).value("filePath").toString()!=source ||
        !legacy.addEditBoard(false) || legacy.editBoardPayload(1).value("targetKey")!=legacy_target)
      return fail("Existing photo spreads retain their shared source and editable identity when opened or expanded in Edit");
    legacy.selectSlide(0);
    legacy.selectSlideRange(1);
    if (!legacy.createBoardSection(QStringLiteral("Connected opening")) || !legacy.moveEditBoard(1,3) ||
        legacy.slides().at(0).toMap().value("groupSpan").toInt()!=0 ||
        legacy.slides().at(1).toMap().value("moveSpanCount").toInt()!=2 ||
        legacy.slides().at(2).toMap().value("moveSpanStart").toInt()!=1)
      return fail("Existing board sections and their spanning images move intact");
    std::cout << "Edit boards: unified canvas, shared image/recipe, active-tile edits, add/delete/undo/reopen and exact split-export parity passed\n";
    return EXIT_SUCCESS;
  }

  if (app.arguments().contains(
          QStringLiteral("--adjustment-patch-only"))) {
    const auto source_path = write_image(
        dir,
        QStringLiteral("adjustment-patch-round-trip.png"),
        split_image(
            QSize(800, 500),
            QColor(QStringLiteral("#7a3028")),
            QColor(QStringLiteral("#285c7a"))));
    CarouselDocumentController controller;
    controller.addPhotoItem(
        source_path,
        QStringLiteral("patch round trip"));
    const auto payload =
        controller.selectedPhotoEditPayload();
    CarouselDocumentController::RecipeState state;
    state.exposure = 0.65;
    const auto patch =
        photo_editor::desktop::
            serialize_adjustment_patch_json(
                state,
                photo_editor::desktop::
                    all_adjustment_groups(),
                photo_editor::desktop::
                    AdjustmentPatchScope::AssetBound);
    if (source_path.isEmpty() ||
        payload.value(QStringLiteral("targetKey"))
            .toString()
            .isEmpty() ||
        !controller
             .applyAdjustmentPatchToPhotoEditTarget(
                 payload.value(
                            QStringLiteral("targetKey"))
                     .toString(),
                 patch)) {
      return fail(
          "asset-bound Carousel photo adjustments should apply");
    }
    const auto applied =
        photo_editor::desktop::
            parse_adjustment_patch_json(
                controller.selectedPhotoEditPayload()
                    .value(
                        QStringLiteral(
                            "adjustmentPatchJson"))
                    .toString());
    if (!applied.ok() ||
        std::abs(
            applied.state->exposure -
            state.exposure) > 1e-9) {
      return fail(
          "Carousel should return the exact asset-bound adjustments");
    }
    if (!controller.saveDocument(
            QStringLiteral(
                "Adjustment Patch Round Trip"))) {
      return fail(
          "patch-native Carousel state should save");
    }
    auto stored_payload =
        read_stored_carousel(
            controller.savedDocumentId());
    if (stored_payload.isEmpty()) {
      stored_payload = read_json(
          dir.filePath(
              QStringLiteral(
                  "carousel-documents/documents/%1.json")
                  .arg(controller.savedDocumentId())));
    }
    auto stored_document =
        stored_payload
            .value(QStringLiteral("document_json"))
            .toObject();
    if (stored_document.isEmpty()) {
      stored_document =
          stored_payload
              .value(QStringLiteral("document"))
              .toObject();
    }
    const auto stored_items =
        stored_document
            .value(QStringLiteral("items"))
            .toArray();
    if (stored_items.size() != 1 ||
        !stored_items.at(0)
             .toObject()
             .value(QStringLiteral("adjustmentPatch"))
             .isObject() ||
        stored_items.at(0)
            .toObject()
            .contains(QStringLiteral("recipeJson"))) {
      return fail(
          "new Carousel documents should persist only an adjustmentPatch object");
    }
    CarouselDocumentController reopened;
    if (!reopened.loadDocument(
            controller.savedDocumentId())) {
      return fail(
          "patch-native Carousel documents should reopen");
    }
    const auto reopened_adjustments =
        photo_editor::desktop::
            parse_adjustment_patch_json(
                reopened.selectedPhotoEditPayload()
                    .value(
                        QStringLiteral(
                            "adjustmentPatchJson"))
                    .toString());
    if (!reopened_adjustments.ok() ||
        std::abs(
            reopened_adjustments.state->exposure -
            state.exposure) > 1e-9) {
      return fail(
          "saved Carousel adjustment patches should reopen exactly");
    }
    controller.undo();
    if (!controller.selectedPhotoEditPayload()
             .value(
                 QStringLiteral(
                     "adjustmentPatchJson"))
             .toString()
             .isEmpty()) {
      return fail(
          "Carousel adjustment round trips should undo atomically");
    }
    return EXIT_SUCCESS;
  }

  {
    const auto library_path = dir.filePath(QStringLiteral("carousel-design-library.json"));
    const QJsonObject legacy_text{
        {QStringLiteral("id"), QStringLiteral("legacy-template-text")},
        {QStringLiteral("type"), QStringLiteral("text")},
        {QStringLiteral("text"), QStringLiteral("Migrated template")},
        {QStringLiteral("fontFamily"), QStringLiteral("serif")},
        {QStringLiteral("fontSize"), 58},
        {QStringLiteral("color"), QStringLiteral("#fefefe")},
        {QStringLiteral("bounds"), QJsonObject{
             {QStringLiteral("x"), 90},
             {QStringLiteral("y"), 120},
             {QStringLiteral("width"), 720},
             {QStringLiteral("height"), 180},
         }},
        {QStringLiteral("futureItemSetting"), QStringLiteral("preserve-me")},
    };
    const QJsonObject legacy_library{
        {QStringLiteral("futureLibrarySetting"), QStringLiteral("preserve-root")},
        {QStringLiteral("savedDesigns"), QJsonArray{
             QJsonObject{
                 {QStringLiteral("id"), QStringLiteral("legacy-board-template")},
                 {QStringLiteral("name"), QStringLiteral("Legacy Board")},
                 {QStringLiteral("format"), QStringLiteral("4x5")},
                 {QStringLiteral("sourceWidth"), 1080},
                 {QStringLiteral("sourceHeight"), 1350},
                 {QStringLiteral("slide"), QJsonObject{
                      {QStringLiteral("layout"), QStringLiteral("single")},
                      {QStringLiteral("background"), QStringLiteral("#102030")},
                      {QStringLiteral("futureSlideSetting"), true},
                  }},
                 {QStringLiteral("items"), QJsonArray{legacy_text}},
                 {QStringLiteral("futureTemplateSetting"), QStringLiteral("preserve-template")},
             },
             QJsonObject{
                 {QStringLiteral("id"), QStringLiteral("legacy-component")},
                 {QStringLiteral("name"), QStringLiteral("Legacy Component")},
                 {QStringLiteral("format"), QStringLiteral("4x5")},
                 {QStringLiteral("sourceWidth"), 720},
                 {QStringLiteral("sourceHeight"), 180},
                 {QStringLiteral("items"), QJsonArray{legacy_text}},
             },
         }},
        {QStringLiteral("brands"), QJsonArray{
             QJsonObject{
                 {QStringLiteral("id"), QStringLiteral("legacy-brand")},
                 {QStringLiteral("name"), QStringLiteral("Legacy Brand")},
                 {QStringLiteral("background"), QStringLiteral("#102030")},
                 {QStringLiteral("foreground"), QStringLiteral("#fefefe")},
                 {QStringLiteral("accent"), QStringLiteral("#ff8800")},
                 {QStringLiteral("headingFont"), QStringLiteral("serif")},
                 {QStringLiteral("futureBrandSetting"), 42},
             },
         }},
    };
    if (!write_json(library_path, legacy_library)) {
      return fail("legacy design-library fixture should save");
    }

    CarouselDocumentController migrated;
    const auto templates = migrated.userTemplates();
    const auto brands = migrated.brandKits();
    if (templates.size() != 2 || brands.size() != 1 ||
        templates.at(0).toMap().value(QStringLiteral("kind")).toString() != QStringLiteral("board") ||
        templates.at(1).toMap().value(QStringLiteral("kind")).toString() != QStringLiteral("component") ||
        brands.constFirst().toMap().value(QStringLiteral("backgroundColor")).toString() !=
            QStringLiteral("#102030")) {
      return fail("legacy board templates, components, and brand kits should migrate to explicit artifact schemas");
    }
    migrated.applyUserTemplate(QStringLiteral("legacy-board-template"), false);
    if (migrated.canvasItems().size() != 1 ||
        migrated.canvasItems().constFirst().toMap().value(QStringLiteral("textFontFamily")).toString() !=
            QStringLiteral("serif") ||
        migrated.slides().constFirst().toMap().value(QStringLiteral("backgroundColor")).toString() !=
            QStringLiteral("#102030")) {
      return fail("migrated template aliases should recreate editable text and board styling");
    }
    migrated.renameUserTemplate(QStringLiteral("legacy-board-template"), QStringLiteral("Migrated Board"));
    const auto saved_library = read_json(library_path);
    const auto saved_templates = saved_library.value(QStringLiteral("templates")).toArray();
    const auto saved_brands = saved_library.value(QStringLiteral("brandKits")).toArray();
    const auto migrated_brand = brands.constFirst().toMap();
    const auto migrated_palette = migrated_brand.value(QStringLiteral("palette")).toMap();
    const auto migrated_roles = migrated_brand.value(QStringLiteral("textRoles")).toMap();
    if (migrated_brand.value(QStringLiteral("artifactVersion")).toInt() != 2 ||
        migrated_palette.value(QStringLiteral("primary")).toList().isEmpty() ||
        migrated_palette.value(QStringLiteral("secondary")).toList().isEmpty() ||
        migrated_palette.value(QStringLiteral("accent")).toList().isEmpty() ||
        migrated_roles.value(QStringLiteral("title")).toMap().value(QStringLiteral("fontFamily")).toString() !=
            QStringLiteral("serif") ||
        migrated_brand.value(QStringLiteral("fontFaces")).toList().isEmpty() ||
        !migrated_brand.value(QStringLiteral("logoAssets")).canConvert<QVariantList>() ||
        migrated_brand.value(QStringLiteral("cornerRadiusScale")).toMap().isEmpty() ||
        migrated_brand.value(QStringLiteral("strokeScale")).toMap().isEmpty() ||
        migrated_brand.value(QStringLiteral("spacingScale")).toMap().isEmpty()) {
      return fail("legacy brand kits should gain named palettes, text roles, font faces, logo assets, and token scales");
    }
    if (saved_library.value(QStringLiteral("version")).toInt() != 3 ||
        saved_library.value(QStringLiteral("futureLibrarySetting")).toString() !=
            QStringLiteral("preserve-root") ||
        saved_templates.at(0).toObject().value(QStringLiteral("futureTemplateSetting")).toString() !=
            QStringLiteral("preserve-template") ||
        saved_templates.at(0).toObject().value(QStringLiteral("items")).toArray().at(0).toObject().value(
            QStringLiteral("futureItemSetting")).toString() != QStringLiteral("preserve-me") ||
        !saved_templates.at(0).toObject().value(QStringLiteral("slide")).toObject().value(
            QStringLiteral("futureSlideSetting")).toBool() ||
        saved_brands.at(0).toObject().value(QStringLiteral("futureBrandSetting")).toInt() != 42) {
      return fail("design-library migration should preserve unknown root, template, item, slide, and brand fields");
    }
    migrated.deleteUserTemplate(QStringLiteral("legacy-board-template"));
    migrated.deleteUserTemplate(QStringLiteral("legacy-component"));
    migrated.deleteBrandKit(QStringLiteral("legacy-brand"));
  }

  {
    const auto legacy_document_id = QStringLiteral("legacy-schema-document");
    const auto legacy_document_path = dir.filePath(
        QStringLiteral("carousel-documents/documents/%1.json").arg(legacy_document_id));
    const QJsonObject legacy_document{
        {QStringLiteral("version"), 3},
        {QStringLiteral("name"), QStringLiteral("Migrated Carousel")},
        {QStringLiteral("format"), QStringLiteral("custom")},
        {QStringLiteral("pixelWidth"), 777},
        {QStringLiteral("pixelHeight"), 888},
        {QStringLiteral("outputColorSpace"), QStringLiteral("Display P3")},
        {QStringLiteral("boards"), QJsonArray{
             QJsonObject{
                 {QStringLiteral("layout"), QStringLiteral("single")},
                 {QStringLiteral("background"), QStringLiteral("#203040")},
                 {QStringLiteral("name"), QStringLiteral("Legacy opening")},
                 {QStringLiteral("role"), QStringLiteral("hook")},
                 {QStringLiteral("primary"), QJsonObject{
                      {QStringLiteral("type"), QStringLiteral("empty")},
                      {QStringLiteral("futureMediaSetting"), QStringLiteral("keep-media")},
                  }},
                 {QStringLiteral("futureSlideSetting"), QStringLiteral("keep-slide")},
             },
         }},
        {QStringLiteral("layers"), QJsonArray{
             QJsonObject{
                 {QStringLiteral("id"), QStringLiteral("legacy-text")},
                 {QStringLiteral("type"), QStringLiteral("text")},
                 {QStringLiteral("text"), QStringLiteral("Legacy headline")},
                 {QStringLiteral("fontFamily"), QStringLiteral("serif")},
                 {QStringLiteral("fontSize"), 64},
                 {QStringLiteral("color"), QStringLiteral("#f0f0f0")},
                 {QStringLiteral("bounds"), QJsonObject{
                      {QStringLiteral("x"), 100},
                      {QStringLiteral("y"), 120},
                      {QStringLiteral("width"), 520},
                      {QStringLiteral("height"), 180},
                      {QStringLiteral("futureBoundsSetting"), true},
                  }},
                 {QStringLiteral("futureItemSetting"), QStringLiteral("keep-item")},
             },
         }},
        {QStringLiteral("linkedGroups"), QJsonArray{}},
        {QStringLiteral("guides"), QJsonArray{
             QJsonObject{
                 {QStringLiteral("guideId"), 41},
                 {QStringLiteral("orientation"), QStringLiteral("vertical")},
                 {QStringLiteral("position"), 311.5},
                 {QStringLiteral("futureGuideSetting"), QStringLiteral("keep-guide")},
             },
         }},
        {QStringLiteral("futureDocumentSetting"), QJsonObject{
             {QStringLiteral("mode"), QStringLiteral("keep-root")},
         }},
    };
    const QJsonObject legacy_payload{
        {QStringLiteral("documentId"), legacy_document_id},
        {QStringLiteral("name"), QStringLiteral("Migrated Carousel")},
        {QStringLiteral("documentFormat"), QStringLiteral("custom")},
        {QStringLiteral("slideCount"), 1},
        {QStringLiteral("updatedAt"), QStringLiteral("2025-01-02T03:04:05Z")},
        {QStringLiteral("document"), legacy_document},
    };
    if (!write_json(legacy_document_path, legacy_payload)) {
      return fail("legacy carousel document fixture should save");
    }

    CarouselDocumentController migrated;
    if (!migrated.loadDocument(legacy_document_id) ||
        migrated.documentFormat() != QStringLiteral("custom") ||
        migrated.documentPixelWidth() != 777 || migrated.documentPixelHeight() != 888 ||
        migrated.exportColorSpace() != QStringLiteral("Display P3") ||
        migrated.layoutSettings().value(QStringLiteral("customGuides")).toList().size() != 1 ||
        migrated.slides().constFirst().toMap().value(QStringLiteral("boardRole")).toString() !=
            QStringLiteral("hook") ||
        migrated.canvasItems().constFirst().toMap().value(QStringLiteral("textFontFamily")).toString() !=
            QStringLiteral("serif")) {
      return fail("legacy document fields, guides, color settings, boards, and layers should migrate deterministically");
    }
    migrated.setCurrentSlideName(QStringLiteral("Migrated and edited"));
    if (!migrated.saveDocument()) {
      return fail("migrated carousel document should save in the current schema");
    }
    const auto saved_payload = read_stored_carousel(legacy_document_id);
    const auto saved_document =
        saved_payload.value(QStringLiteral("document_json")).toObject();
    const auto saved_slide = saved_document.value(QStringLiteral("slides")).toArray().at(0).toObject();
    const auto saved_item = saved_document.value(QStringLiteral("items")).toArray().at(0).toObject();
    const auto saved_guide = saved_document.value(QStringLiteral("layoutSettings")).toObject().value(
        QStringLiteral("customGuides")).toArray().at(0).toObject();
    if (saved_document.value(QStringLiteral("version")).toInt() != 11 ||
        saved_document.value(QStringLiteral("futureDocumentSetting")).toObject().value(
            QStringLiteral("mode")).toString() != QStringLiteral("keep-root") ||
        saved_slide.value(QStringLiteral("futureSlideSetting")).toString() !=
            QStringLiteral("keep-slide") ||
        saved_slide.value(QStringLiteral("primary")).toObject().value(
            QStringLiteral("futureMediaSetting")).toString() != QStringLiteral("keep-media") ||
        saved_item.value(QStringLiteral("futureItemSetting")).toString() !=
            QStringLiteral("keep-item") ||
        !saved_item.value(QStringLiteral("bounds")).toObject().value(
            QStringLiteral("futureBoundsSetting")).toBool() ||
        saved_guide.value(QStringLiteral("futureGuideSetting")).toString() !=
            QStringLiteral("keep-guide")) {
      return fail("current-schema save should preserve unknown nested fields from migrated documents");
    }

    const auto future_document_id = QStringLiteral("future-schema-document");
    const auto future_document_path = dir.filePath(
        QStringLiteral("carousel-documents/documents/%1.json").arg(future_document_id));
    if (!write_json(future_document_path, QJsonObject{
            {QStringLiteral("documentId"), future_document_id},
            {QStringLiteral("name"), QStringLiteral("Future Carousel")},
            {QStringLiteral("documentFormat"), QStringLiteral("4x5")},
            {QStringLiteral("document"), QJsonObject{
                 {QStringLiteral("version"), 12},
                 {QStringLiteral("slides"), QJsonArray{QJsonObject{}}},
             }},
        })) {
      return fail("future carousel document fixture should save");
    }
    CarouselDocumentController future;
    if (future.loadDocument(future_document_id) ||
        !future.status().contains(QStringLiteral("schema 12")) ||
        !future.status().contains(QStringLiteral("supports up to 11"))) {
      return fail("newer unsupported document schemas should fail read-only with an actionable version reason");
    }
  }

  {
    CarouselDocumentController author;
    author.setCurrentSlideBackgroundColor(QStringLiteral("#112233"));
    author.addShapeItem(QStringLiteral("rectangle"));
    author.setSelectedShapeFillColor(QStringLiteral("#ff8800"));
    author.setSelectedShapeCornerRadius(18.0);
    author.setSelectedShapeStrokeWidth(3.0);
    author.addTextItem(QStringLiteral("BRAND TITLE"));
    author.setSelectedTextRole(QStringLiteral("title"));
    author.setSelectedTextColor(QStringLiteral("#f5f5f5"));
    author.setSelectedTextFontFamily(QStringLiteral("serif"));
    author.setSelectedTextWeight(QStringLiteral("black"));
    author.addTextItemWithRole(QStringLiteral("body"), QStringLiteral("Brand body"));
    author.setSelectedTextFontFamily(QStringLiteral("system"));
    author.setSelectedTextWeight(QStringLiteral("regular"));
    QImage brand_logo(48, 48, QImage::Format_ARGB32);
    brand_logo.fill(QColor(QStringLiteral("#ff8800")));
    const auto brand_logo_path = write_image(dir, QStringLiteral("brand-logo.png"), brand_logo);
    author.addPhotoItem(
        brand_logo_path,
        QStringLiteral("Primary Logo"));
    if (!author.saveCurrentBoardAsTemplate(QStringLiteral("Launch Board")) ||
        !author.saveCurrentBoardAsBrandKit(QStringLiteral("Launch Brand")) ||
        author.userTemplates().size() != 1 || author.brandKits().size() != 1) {
      return fail("board templates and brand kits should save into the persistent design library");
    }

    const auto template_id = author.userTemplates().constFirst().toMap().value(QStringLiteral("templateId")).toString();
    const auto template_library_entries = author.templateLibraryEntries();
    if (template_library_entries.size() != 9) {
      return fail("the unified template library should combine eight installed packs with saved custom templates");
    }
    bool found_bounded_custom_preview = false;
    for (const auto& value : template_library_entries) {
      const auto entry = value.toMap();
      if (entry.value(QStringLiteral("sourceSection")).toString() == QStringLiteral("installed")) {
        if (!entry.value(QStringLiteral("previewSource")).toString().isEmpty() ||
            entry.value(QStringLiteral("previewKind")).toString() != QStringLiteral("role-metadata") ||
            entry.value(QStringLiteral("previewMaximumPixelWidth")).toInt() != 0) {
          return fail("installed template cards should use lightweight role metadata without image assets");
        }
        continue;
      }
      if (entry.value(QStringLiteral("templateId")).toString() != template_id) {
        continue;
      }
      const auto preview_path = QUrl(entry.value(QStringLiteral("previewSource")).toString()).toLocalFile();
      const auto preview_size = QImageReader(preview_path).size();
      found_bounded_custom_preview =
          entry.value(QStringLiteral("sourceSection")).toString() == QStringLiteral("custom") &&
          entry.value(QStringLiteral("entryKind")).toString() == QStringLiteral("board") &&
          entry.value(QStringLiteral("previewKind")).toString() == QStringLiteral("bounded-thumbnail") &&
          entry.value(QStringLiteral("previewMaximumPixelWidth")).toInt() == 320 &&
          entry.value(QStringLiteral("previewMaximumPixelHeight")).toInt() == 400 &&
          preview_size.isValid() && preview_size.width() <= 320 && preview_size.height() <= 400;
    }
    if (!found_bounded_custom_preview) {
      return fail("custom template cards should expose only their persisted bounded preview thumbnail");
    }
    author.toggleFavoriteTemplate(QStringLiteral("product-launch"));
    author.toggleFavoriteTemplate(template_id);
    const auto saved_brand = author.brandKits().constFirst().toMap();
    const auto brand_id = saved_brand.value(QStringLiteral("brandId")).toString();
    const auto saved_roles = saved_brand.value(QStringLiteral("textRoles")).toMap();
    if (saved_brand.value(QStringLiteral("artifactVersion")).toInt() != 2 ||
        saved_brand.value(QStringLiteral("palette")).toMap().value(QStringLiteral("accent")).toList()
                .constFirst().toMap().value(QStringLiteral("value")).toString() != QStringLiteral("#ff8800") ||
        saved_roles.value(QStringLiteral("title")).toMap().value(QStringLiteral("fontFamily")).toString() !=
            QStringLiteral("serif") ||
        saved_roles.value(QStringLiteral("title")).toMap().value(QStringLiteral("fontWeight")).toString() !=
            QStringLiteral("black") ||
        saved_brand.value(QStringLiteral("logoAssets")).toList().constFirst().toMap().value(
            QStringLiteral("name")).toString() != QStringLiteral("Primary Logo") ||
        saved_brand.value(QStringLiteral("cornerRadiusScale")).toMap().value(QStringLiteral("medium")).toDouble() != 18.0 ||
        saved_brand.value(QStringLiteral("strokeScale")).toMap().value(QStringLiteral("standard")).toDouble() != 3.0) {
      return fail("saved brand kits should capture role typography, named colors, logo assets, radii, and stroke scales");
    }
    author.beginBrandKitEditor();
    author.setBrandKitDraftField(QStringLiteral("name"), QStringLiteral("Discarded Brand"));
    author.setBrandKitDraftField(QStringLiteral("logoRequired"), true);
    author.setBrandKitDraftField(QStringLiteral("logoPath"), dir.filePath(QStringLiteral("missing-logo.png")));
    if (!author.brandKitEditorActive() ||
        author.brandKitValidation().value(QStringLiteral("canSave")).toBool() ||
        author.brandKitValidation().value(QStringLiteral("missingRequiredAssets")).toList().isEmpty() ||
        author.saveBrandKitDraft()) {
      return fail("the brand editor should block a required missing logo with an explicit validation result");
    }
    author.cancelBrandKitEditor();
    if (author.brandKitEditorActive() || author.brandKits().size() != 1) {
      return fail("canceling a new brand-kit draft should not mutate the saved design library");
    }
    author.beginBrandKitEditor(brand_id);
    author.setBrandKitDraftField(QStringLiteral("name"), QStringLiteral("Launch Brand Revised"));
    author.setBrandKitDraftField(QStringLiteral("fontLicenseNotes"), QStringLiteral("Desktop license verified"));
    author.setBrandKitDraftField(QStringLiteral("logoRequired"), true);
    author.setBrandKitDraftField(QStringLiteral("logoPath"), brand_logo_path);
    author.setBrandKitDraftField(QStringLiteral("cornerMedium"), 20.0);
    if (!author.brandKitValidation().value(QStringLiteral("canSave")).toBool() ||
        author.brandKitValidation().value(QStringLiteral("licenseReminderPending")).toBool() ||
        !author.saveBrandKitDraft() || author.brandKits().size() != 1 ||
        author.brandKits().constFirst().toMap().value(QStringLiteral("name")).toString() !=
            QStringLiteral("Launch Brand Revised") ||
        author.brandKits().constFirst().toMap().value(QStringLiteral("cornerRadiusScale")).toMap().value(
            QStringLiteral("medium")).toDouble() != 20.0) {
      return fail("the brand editor should update a complete validated kit without creating a duplicate");
    }
    CarouselDocumentController consumer;
    if (consumer.userTemplates().size() != 1 || consumer.brandKits().size() != 1 ||
        consumer.brandKits().constFirst().toMap().value(QStringLiteral("spacingScale")).toMap().isEmpty() ||
        !consumer.favoriteTemplateIds().contains(QStringLiteral("product-launch")) ||
        !consumer.favoriteTemplateIds().contains(template_id)) {
      return fail("new carousel controllers should load saved templates, brand kits, and template favorites");
    }
    if (!consumer.applyTemplateLibraryEntry(template_id) || consumer.canvasItems().size() != 4 ||
        consumer.recentTemplateIds().constFirst() != template_id ||
        consumer.slides().constFirst().toMap().value(QStringLiteral("backgroundColor")).toString() != QStringLiteral("#112233")) {
      return fail("the unified template library should apply saved boards and record them as recent");
    }
    consumer.undo();
    if (!consumer.canvasItems().isEmpty()) {
      return fail("saved template application should undo as one edit");
    }

    CarouselDocumentController installed_pack_consumer;
    if (!installed_pack_consumer.applyTemplateLibraryEntry(QStringLiteral("product-launch")) ||
        installed_pack_consumer.slideCount() != 8 ||
        installed_pack_consumer.recentTemplateIds().value(0) != QStringLiteral("product-launch") ||
        installed_pack_consumer.recentTemplateIds().value(1) != template_id) {
      return fail("the unified template library should apply installed packs and persist recency order");
    }

    consumer.addSlide();
    consumer.selectSlide(0);
    consumer.selectSlideRange(1);
    consumer.applyBrandKit(brand_id, QStringLiteral("selected"));
    const auto branded_slides = consumer.slides();
    if (branded_slides.at(0).toMap().value(QStringLiteral("backgroundColor")).toString() != QStringLiteral("#112233") ||
        branded_slides.at(1).toMap().value(QStringLiteral("backgroundColor")).toString() != QStringLiteral("#112233")) {
      return fail("brand kits should apply consistently across the selected board range");
    }
    consumer.undo();
    consumer.deleteUserTemplate(template_id);
    consumer.deleteBrandKit(brand_id);
    consumer.toggleFavoriteTemplate(QStringLiteral("product-launch"));
    consumer.clearRecentTemplates();
    CarouselDocumentController cleaned;
    if (!cleaned.userTemplates().isEmpty() || !cleaned.brandKits().isEmpty() ||
        !cleaned.favoriteTemplateIds().isEmpty() || !cleaned.recentTemplateIds().isEmpty()) {
      return fail("deleted templates and brand kits should stay removed without stale library favorites or recents");
    }
  }

  {
    const auto selected_export_dir = dir.filePath(QStringLiteral("selected-range-export"));
    const auto current_export_dir = dir.filePath(QStringLiteral("current-board-export"));
    QDir().mkpath(selected_export_dir);
    QDir().mkpath(current_export_dir);
    CarouselDocumentController controller;
    controller.createCarouselWithSettings(QVariantMap{
        {QStringLiteral("carouselName"), QStringLiteral("Scoped Export Proof")},
        {QStringLiteral("documentFormat"), QStringLiteral("1x1")},
        {QStringLiteral("slideCount"), 3},
    });
    const QStringList board_names{
        QStringLiteral("Broken source"),
        QStringLiteral("Middle delivery"),
        QStringLiteral("Closing delivery"),
    };
    const QStringList board_colors{
        QStringLiteral("#cc3344"),
        QStringLiteral("#33aa77"),
        QStringLiteral("#3355cc"),
    };
    for (int index = 0; index < 3; ++index) {
      controller.selectSlide(index);
      controller.setCurrentSlideName(board_names.at(index));
      controller.setCurrentSlideBackgroundColor(board_colors.at(index));
    }
    controller.selectSlide(0);
    controller.applyTemplateToCurrent(QStringLiteral("split-pair"));
    controller.selectSlide(1);
    controller.selectSlideRange(2);
    controller.setExportBoardScope(QStringLiteral("selected"));
    controller.setExportDirectory(selected_export_dir);

    const auto selected_preview = controller.exportOutputPreview();
    if (controller.exportBoardScope() != QStringLiteral("selected") ||
        controller.exportSummary().value(QStringLiteral("boardCount")).toInt() != 2 ||
        controller.exportSummary().value(QStringLiteral("boardOrder")).toString() !=
            QStringLiteral("2, 3") ||
        selected_preview.size() != 3 ||
        selected_preview.at(0).toMap().value(QStringLiteral("boardIndex")).toInt() != 1 ||
        selected_preview.at(1).toMap().value(QStringLiteral("boardIndex")).toInt() != 2 ||
        selected_preview.at(0).toMap().value(QStringLiteral("boardName")).toString() !=
            board_names.at(1) ||
        !selected_preview.at(0).toMap().contains(QStringLiteral("previewSource")) ||
        selected_preview.at(1).toMap().value(QStringLiteral("backgroundColor")).toString() !=
            board_colors.at(2)) {
      return fail("selected-range export review should expose the exact document-ordered boards, thumbnails, names, and output count");
    }
    bool scoped_export_has_blocker = false;
    for (const auto& value : controller.exportPreflightIssues()) {
      const auto issue = value.toMap();
      scoped_export_has_blocker =
          scoped_export_has_blocker || issue.value(QStringLiteral("blocking")).toBool();
    }
    if (scoped_export_has_blocker) {
      return fail("blocking issues on an unselected board should not block a selected-range export");
    }
    if (!controller.exportConfiguredCarousel()) {
      return fail("a selected-range export should render after scoped preflight passes");
    }
    const auto selected_images = QDir(selected_export_dir).entryList(
        {QStringLiteral("*.png")}, QDir::Files, QDir::Name);
    const auto selected_manifests = QDir(selected_export_dir).entryList(
        {QStringLiteral("*.json")}, QDir::Files, QDir::Name);
    if (selected_images.size() != 2 || selected_manifests.size() != 1) {
      return fail("selected-range export should write only its two reviewed boards and one manifest");
    }
    const auto selected_manifest = read_json(
        QDir(selected_export_dir).filePath(selected_manifests.constFirst()));
    const auto selected_outputs = selected_manifest.value(QStringLiteral("outputs")).toArray();
    if (selected_manifest.value(QStringLiteral("boardScope")).toString() !=
            QStringLiteral("selected") ||
        selected_outputs.size() != 2 ||
        selected_outputs.at(0).toObject().value(QStringLiteral("boardIndex")).toInt() != 1 ||
        selected_outputs.at(1).toObject().value(QStringLiteral("boardIndex")).toInt() != 2) {
      return fail("selected-range manifest should preserve the exact reviewed board order and original indexes");
    }

    controller.selectSlide(2);
    controller.setExportBoardScope(QStringLiteral("current"));
    controller.setExportManifestEnabled(false);
    controller.setExportDirectory(current_export_dir);
    const auto current_preview = controller.exportOutputPreview();
    if (current_preview.size() != 1 ||
        current_preview.constFirst().toMap().value(QStringLiteral("boardIndex")).toInt() != 2 ||
        !controller.exportConfiguredCarousel() ||
        QDir(current_export_dir).entryList({QStringLiteral("*.png")}, QDir::Files).size() != 1) {
      return fail("current-board export should review and write only the active board");
    }

    controller.setExportBoardScope(QStringLiteral("all"));
    bool all_export_has_blocker = false;
    for (const auto& value : controller.exportPreflightIssues()) {
      all_export_has_blocker =
          all_export_has_blocker || value.toMap().value(QStringLiteral("blocking")).toBool();
    }
    if (!all_export_has_blocker) {
      return fail("all-board export should retain blocking issues from every included board");
    }
    controller.setExportBoardScope(QStringLiteral("selected"));
    if (!controller.saveDocument()) {
      return fail("scoped export setting should save with the carousel document");
    }
    CarouselDocumentController restored;
    if (!restored.loadDocument(controller.savedDocumentId()) ||
        restored.exportBoardScope() != QStringLiteral("selected")) {
      return fail("selected export scope should survive save and reopen");
    }
  }

  {
    const auto double_export_dir = dir.filePath(QStringLiteral("double-size-export"));
    const auto custom_export_dir = dir.filePath(QStringLiteral("custom-size-export"));
    QDir().mkpath(double_export_dir);
    QDir().mkpath(custom_export_dir);
    CarouselDocumentController controller;
    controller.createCarouselWithSettings(QVariantMap{
        {QStringLiteral("carouselName"), QStringLiteral("Export Size Proof")},
        {QStringLiteral("documentFormat"), QStringLiteral("4x5")},
        {QStringLiteral("slideCount"), 1},
    });
    controller.setExportManifestEnabled(false);
    auto size_settings = controller.exportSizeSettings();
    if (size_settings.value(QStringLiteral("mode")).toString() != QStringLiteral("1x") ||
        size_settings.value(QStringLiteral("width")).toInt() != 1080 ||
        size_settings.value(QStringLiteral("height")).toInt() != 1350 ||
        size_settings.value(QStringLiteral("previewResolution")).toString() !=
            QStringLiteral("1080 × 1350")) {
      return fail("1x export should match the document resolution and report it separately from export settings");
    }
    controller.setExportSizeMode(QStringLiteral("2x"));
    size_settings = controller.exportSizeSettings();
    controller.setExportDirectory(double_export_dir);
    if (size_settings.value(QStringLiteral("width")).toInt() != 2160 ||
        size_settings.value(QStringLiteral("height")).toInt() != 2700 ||
        size_settings.value(QStringLiteral("exportResolution")).toString() !=
            QStringLiteral("2160 × 2700") ||
        size_settings.value(QStringLiteral("warning")).toString().isEmpty() ||
        !controller.exportConfiguredCarousel()) {
      return fail("2x export should resolve, warn about platform downsampling, and render at double resolution");
    }
    const auto double_files = QDir(double_export_dir).entryList(
        {QStringLiteral("*.png")}, QDir::Files, QDir::Name);
    if (double_files.size() != 1 ||
        QImage(QDir(double_export_dir).filePath(double_files.constFirst())).size() !=
            QSize(2160, 2700)) {
      return fail("2x export should write the exact reviewed pixel dimensions");
    }

    controller.setExportSizeMode(QStringLiteral("custom"));
    controller.setExportCustomPixelSize(1600, 999);
    size_settings = controller.exportSizeSettings();
    if (!size_settings.value(QStringLiteral("aspectLocked")).toBool() ||
        size_settings.value(QStringLiteral("width")).toInt() != 1600 ||
        size_settings.value(QStringLiteral("height")).toInt() != 2000) {
      return fail("locked custom export should preserve the document aspect ratio from an edited dimension");
    }
    controller.setExportDirectory(custom_export_dir);
    if (!controller.exportConfiguredCarousel()) {
      return fail("locked custom export should render at its resolved dimensions");
    }
    const auto custom_files = QDir(custom_export_dir).entryList(
        {QStringLiteral("*.png")}, QDir::Files, QDir::Name);
    if (custom_files.size() != 1 ||
        QImage(QDir(custom_export_dir).filePath(custom_files.constFirst())).size() !=
            QSize(1600, 2000)) {
      return fail("custom export should write the exact aspect-locked dimensions");
    }

    controller.setExportAspectLocked(false);
    controller.setExportCustomPixelSize(1600, 900);
    size_settings = controller.exportSizeSettings();
    if (size_settings.value(QStringLiteral("width")).toInt() != 1600 ||
        size_settings.value(QStringLiteral("height")).toInt() != 900 ||
        size_settings.value(QStringLiteral("warning")).toString().isEmpty() ||
        first_preflight_issue(
            controller.exportPreflightIssues(), QStringLiteral("custom-export-aspect")).isEmpty()) {
      return fail("unlocked custom export should preserve exact dimensions and warn when aspect changes");
    }
    controller.setExportAspectLocked(true);
    controller.setExportCustomPixelSize(5000, 6250);
    if (first_preflight_issue(
            controller.exportPreflightIssues(), QStringLiteral("platform-output-size")).isEmpty()) {
      return fail("large custom output should surface a platform downsampling warning before export");
    }
    controller.setExportAspectLocked(false);
    controller.setExportCustomPixelSize(1600, 900);
    if (!controller.saveDocument()) {
      return fail("custom export dimensions and aspect-lock state should save with the document");
    }
    CarouselDocumentController restored;
    if (!restored.loadDocument(controller.savedDocumentId())) {
      return fail("custom export size proof should reopen its saved document");
    }
    const auto restored_size = restored.exportSizeSettings();
    if (restored_size.value(QStringLiteral("mode")).toString() != QStringLiteral("custom") ||
        restored_size.value(QStringLiteral("width")).toInt() != 1600 ||
        restored_size.value(QStringLiteral("height")).toInt() != 900 ||
        restored_size.value(QStringLiteral("aspectLocked")).toBool()) {
      return fail("custom export dimensions and unlocked aspect state should survive save and reopen");
    }
  }

  {
    CarouselDocumentController current_pack;
    current_pack.createCarouselWithSettings(QVariantMap{
        {QStringLiteral("carouselName"), QStringLiteral("Versioned Listicle Source")},
        {QStringLiteral("documentFormat"), QStringLiteral("4x5")},
        {QStringLiteral("slideCount"), 1},
    });
    if (!current_pack.applySequenceTemplate(QStringLiteral("listicle")) ||
        current_pack.templatePackUpdateState().value(QStringLiteral("sourceVersion")).toInt() != 2 ||
        current_pack.templatePackUpdateState().value(QStringLiteral("updateAvailable")).toBool() ||
        !current_pack.saveDocument()) {
      return fail("new sequence documents should persist their exact current pack provenance without an update prompt");
    }

    const auto current_payload =
        read_stored_carousel(current_pack.savedDocumentId());
    auto legacy_document =
        current_payload.value(QStringLiteral("document_json")).toObject();
    if (legacy_document.isEmpty()) {
      return fail("versioned template migration fixture should read the saved source document");
    }
    legacy_document.insert(QStringLiteral("sourceTemplateVersion"), 1);
    legacy_document.insert(QStringLiteral("sourceTemplateMigrationHistory"), QJsonArray{});
    auto legacy_items = legacy_document.value(QStringLiteral("items")).toArray();
    int progress_index = 0;
    int prompt_count = 0;
    for (qsizetype index = 0; index < legacy_items.size(); ++index) {
      auto item = legacy_items.at(index).toObject();
      const auto component = item.value(QStringLiteral("componentLinkId")).toString();
      if (component == QStringLiteral("listicle:progress-label")) {
        ++progress_index;
        const auto text = progress_index == 3
                              ? QStringLiteral("CUSTOM PROGRESS COPY")
                              : QStringLiteral("%1 / 5").arg(progress_index);
        item.insert(QStringLiteral("text"), text);
        item.insert(QStringLiteral("displayLabel"), text);
        legacy_items.replace(index, item);
      } else if (component == QStringLiteral("listicle:prompt-label")) {
        ++prompt_count;
        item.insert(QStringLiteral("text"), QStringLiteral("SHARE YOUR TAKE →"));
        item.insert(QStringLiteral("displayLabel"), QStringLiteral("SHARE YOUR TAKE →"));
        legacy_items.replace(index, item);
      }
    }
    legacy_document.insert(QStringLiteral("items"), legacy_items);
    auto legacy_slides = legacy_document.value(QStringLiteral("slides")).toArray();
    if (legacy_slides.size() != 8 || progress_index != 5 || prompt_count != 1) {
      return fail("versioned template fixture should contain every v1 migration target");
    }
    auto closing_slide = legacy_slides.at(7).toObject();
    closing_slide.insert(
        QStringLiteral("messageNote"), QStringLiteral("End with a general engagement prompt."));
    legacy_slides.replace(7, closing_slide);
    legacy_document.insert(QStringLiteral("slides"), legacy_slides);

    const auto legacy_pack_id = QStringLiteral("versioned-listicle-v1-proof");
    const auto legacy_pack_path = dir.filePath(
        QStringLiteral("carousel-documents/documents/%1.json").arg(legacy_pack_id));
    if (!write_json(legacy_pack_path, QJsonObject{
            {QStringLiteral("documentId"), legacy_pack_id},
            {QStringLiteral("name"), QStringLiteral("Versioned Listicle v1")},
            {QStringLiteral("documentFormat"), QStringLiteral("4x5")},
            {QStringLiteral("document"), legacy_document},
        })) {
      return fail("versioned v1 template document fixture should save");
    }

    CarouselDocumentController legacy_pack;
    if (!legacy_pack.loadDocument(legacy_pack_id)) {
      return fail("older template-pack documents should open without automatic migration");
    }
    const auto update_state = legacy_pack.templatePackUpdateState();
    const auto before_preview_hash = legacy_pack.documentContentHash();
    if (!update_state.value(QStringLiteral("updateAvailable")).toBool() ||
        update_state.value(QStringLiteral("sourceVersion")).toInt() != 1 ||
        update_state.value(QStringLiteral("currentVersion")).toInt() != 2 ||
        update_state.value(QStringLiteral("changes")).toList().size() != 3 ||
        update_state.value(QStringLiteral("automaticMigration")).toBool() ||
        !update_state.value(QStringLiteral("requiresApproval")).toBool()) {
      return fail("older documents should report a component-level explicit update without changing their saved version");
    }
    if (legacy_pack.applyPreviewedTemplatePackUpdate()) {
      return fail("template pack migration should be impossible without a current explicit review");
    }
    if (!legacy_pack.previewTemplatePackUpdate() ||
        legacy_pack.documentContentHash() != before_preview_hash) {
      return fail("template pack update review must be non-mutating");
    }
    const auto update_preview = legacy_pack.templatePackUpdatePreview();
    if (!update_preview.value(QStringLiteral("available")).toBool() ||
        update_preview.value(QStringLiteral("updateCount")).toInt() != 6 ||
        update_preview.value(QStringLiteral("preservedOverrideCount")).toInt() != 1 ||
        update_preview.value(QStringLiteral("changes")).toList().size() != 3 ||
        !update_preview.value(QStringLiteral("requiresApproval")).toBool()) {
      return fail("template update review should explain exact safe changes and local overrides before approval");
    }
    if (!legacy_pack.applyPreviewedTemplatePackUpdate() ||
        legacy_pack.templatePackUpdateState().value(QStringLiteral("sourceVersion")).toInt() != 2 ||
        legacy_pack.templatePackUpdateState().value(QStringLiteral("updateAvailable")).toBool() ||
        legacy_pack.templatePackUpdateState().value(QStringLiteral("migrationHistory")).toList().size() != 1 ||
        legacy_pack.undoText() != QStringLiteral("Update template pack") ||
        !legacy_pack.templatePackUpdatePreview().isEmpty()) {
      return fail("approved template migration should update provenance once, record history, and remain one-step undoable");
    }
    int current_progress_count = 0;
    int preserved_progress_count = 0;
    int current_prompt_count = 0;
    for (const auto& value : legacy_pack.canvasItems()) {
      const auto item = value.toMap();
      if (item.value(QStringLiteral("componentLinkId")).toString() ==
          QStringLiteral("listicle:progress-label")) {
        if (item.value(QStringLiteral("text")).toString() == QStringLiteral("CUSTOM PROGRESS COPY")) {
          ++preserved_progress_count;
        } else if (item.value(QStringLiteral("text")).toString().startsWith(QStringLiteral("POINT "))) {
          ++current_progress_count;
        }
      } else if (item.value(QStringLiteral("componentLinkId")).toString() ==
                     QStringLiteral("listicle:prompt-label") &&
                 item.value(QStringLiteral("text")).toString() ==
                     QStringLiteral("WHAT WOULD YOU ADD? →")) {
        ++current_prompt_count;
      }
    }
    if (current_progress_count != 4 || preserved_progress_count != 1 || current_prompt_count != 1 ||
        legacy_pack.slides().at(7).toMap().value(QStringLiteral("messageNote")).toString() !=
            QStringLiteral("End with one specific question that invites a useful response.")) {
      return fail("explicit migration should update only unchanged v1 components and preserve local copy overrides");
    }
    legacy_pack.undo();
    if (legacy_pack.documentContentHash() != before_preview_hash ||
        !legacy_pack.templatePackUpdateState().value(QStringLiteral("updateAvailable")).toBool()) {
      return fail("one undo should restore the complete older pack document and its available-update state");
    }
    legacy_pack.redo();
    if (legacy_pack.templatePackUpdateState().value(QStringLiteral("sourceVersion")).toInt() != 2 ||
        !legacy_pack.saveDocument()) {
      return fail("redo should restore the approved pack update and allow it to be saved");
    }
    CarouselDocumentController migrated_pack_reloaded;
    if (!migrated_pack_reloaded.loadDocument(legacy_pack_id) ||
        migrated_pack_reloaded.templatePackUpdateState().value(QStringLiteral("sourceVersion")).toInt() != 2 ||
        migrated_pack_reloaded.templatePackUpdateState().value(QStringLiteral("updateAvailable")).toBool() ||
        migrated_pack_reloaded.templatePackUpdateState().value(QStringLiteral("migrationHistory")).toList().size() != 1) {
      return fail("approved template version and migration history should survive save and reopen");
    }
  }

  {
    QGuiApplication::clipboard()->clear();
    CarouselDocumentController controller;
    controller.createCarouselWithSettings(QVariantMap{
        {QStringLiteral("carouselName"), QStringLiteral("Repeated Stack Identity")},
        {QStringLiteral("documentFormat"), QStringLiteral("4x5")},
        {QStringLiteral("slideCount"), 2},
    });
    controller.selectSlide(0);
    controller.addTextItem(QStringLiteral("Repeated stack title"));
    const auto repeated_stack_title_id = controller.selectedItemId();
    controller.setSelectedItemSize(360.0, 72.0);
    controller.setSelectedItemCenter(320.0, 190.0);
    controller.addShapeItem(QStringLiteral("rectangle"));
    const auto repeated_stack_card_id = controller.selectedItemId();
    controller.setSelectedItemSize(420.0, 240.0);
    controller.setSelectedItemCenter(320.0, 520.0);
    controller.selectItem(repeated_stack_title_id);
    controller.addItemToSelection(repeated_stack_card_id);
    if (!controller.createConstraintStack(QStringLiteral("vertical"), 24.0, 24.0) ||
        !controller.createRepeatedElementLink(
            QStringLiteral("Repeated Stack"), QStringLiteral("all"))) {
      return fail("a complete explicit stack should link across boards as a repeated element");
    }
    QString source_stack_id;
    QString target_stack_id;
    QString repeated_title_component_id;
    for (const auto& value : controller.canvasItems()) {
      const auto item = value.toMap();
      const auto slide_index = item.value(QStringLiteral("slideIndex")).toInt();
      const auto stack_id = item.value(QStringLiteral("constraintStackId")).toString();
      if (slide_index == 0) {
        if (!source_stack_id.isEmpty() && source_stack_id != stack_id) {
          return fail("source repeated-stack members should share one stack identity");
        }
        source_stack_id = stack_id;
        if (item.value(QStringLiteral("itemId")).toString() == repeated_stack_title_id) {
          repeated_title_component_id =
              item.value(QStringLiteral("componentLinkId")).toString();
        }
      } else {
        if (!target_stack_id.isEmpty() && target_stack_id != stack_id) {
          return fail("target repeated-stack members should share one stack identity");
        }
        target_stack_id = stack_id;
      }
    }
    if (source_stack_id.isEmpty() || target_stack_id.isEmpty() ||
        source_stack_id == target_stack_id || repeated_title_component_id.isEmpty()) {
      return fail("repeated stacks should remap identity independently for each board");
    }
    controller.selectItem(repeated_stack_title_id);
    controller.setSelectedText(QStringLiteral(
        "A much longer repeated title that wraps and forces every linked stack instance to reflow"));
    QVariantMap target_title;
    QVariantMap target_card;
    for (const auto& value : controller.canvasItems()) {
      const auto item = value.toMap();
      if (item.value(QStringLiteral("slideIndex")).toInt() != 1) {
        continue;
      }
      if (item.value(QStringLiteral("componentLinkId")).toString() ==
          repeated_title_component_id) {
        target_title = item;
      } else if (item.value(QStringLiteral("constraintStackId")).toString() ==
                 target_stack_id) {
        target_card = item;
      }
    }
    if (target_title.isEmpty() || target_card.isEmpty() ||
        target_title.value(QStringLiteral("constraintStackId")).toString() != target_stack_id ||
        qAbs(target_card.value(QStringLiteral("y")).toDouble() -
             (target_title.value(QStringLiteral("y")).toDouble() +
              target_title.value(QStringLiteral("height")).toDouble() + 24.0)) > 0.001) {
      return fail("copy changes should reflow each repeated-stack instance without sharing stack identity");
    }
  }

  {
    const auto proof_media_path = write_image(
        dir,
        QStringLiteral("pdf-proof-media.png"),
        split_image(
            QSize(320, 400),
            QColor(QStringLiteral("#1e4a68")),
            QColor(QStringLiteral("#d29a4a"))));
    if (proof_media_path.isEmpty()) {
      return fail("PDF proof should create its color-managed media fixture");
    }

    CarouselDocumentController controller;
    controller.createCarouselWithSettings(QVariantMap{
        {QStringLiteral("carouselName"), QStringLiteral("Client Review Proof")},
        {QStringLiteral("documentFormat"), QStringLiteral("4x5")},
        {QStringLiteral("slideCount"), 5},
    });
    controller.setExportCustomPixelSize(320, 400);
    for (int index = 0; index < 5; ++index) {
      controller.selectSlide(index);
      controller.assignPrimaryImage(
          proof_media_path,
          QStringLiteral("Proof image %1").arg(index + 1));
      controller.setCurrentSlideName(QStringLiteral("Review board %1").arg(index + 1));
      controller.setCurrentSlideRole(index == 0 ? QStringLiteral("cover")
                                                 : QStringLiteral("content"));
      controller.setCurrentSlideNotes(
          QStringLiteral("Confirm message %1 with the client.").arg(index + 1),
          QStringLiteral("Confirm crop %1.").arg(index + 1),
          QStringLiteral("Source reference %1.").arg(index + 1));
    }
    controller.setExportProofSheetEnabled(true);
    controller.setExportProofIncludeNotes(false);
    controller.undo();
    if (!controller.exportProofIncludeNotes()) {
      return fail("PDF proof notes setting should undo");
    }
    controller.redo();
    if (controller.exportProofIncludeNotes()) {
      return fail("PDF proof notes setting should redo");
    }
    controller.setExportProofIncludeNotes(true);
    if (!controller.exportProofSheetEnabled() ||
        !controller.exportProofIncludeNotes() ||
        !controller.saveDocument()) {
      return fail("PDF proof and optional-note settings should be enabled and saved");
    }

    CarouselDocumentController restored;
    if (!restored.loadDocument(controller.savedDocumentId()) ||
        !restored.exportProofSheetEnabled() ||
        !restored.exportProofIncludeNotes()) {
      return fail("PDF proof and optional-note settings should survive save and reopen");
    }
    const auto proof_export_dir = dir.filePath(QStringLiteral("pdf-contact-sheet-export"));
    QDir().mkpath(proof_export_dir);
    restored.setExportDirectory(proof_export_dir);
    const auto output_preview = restored.exportOutputPreview();
    QVariantMap proof_plan;
    for (const auto& value : output_preview) {
      const auto plan = value.toMap();
      if (plan.value(QStringLiteral("artifactType")).toString() == QStringLiteral("proof")) {
        proof_plan = plan;
        break;
      }
    }
    if (proof_plan.value(QStringLiteral("fileType")).toString() != QStringLiteral("PDF") ||
        proof_plan.value(QStringLiteral("pageSize")).toString() !=
            QStringLiteral("A4 landscape") ||
        proof_plan.value(QStringLiteral("boardsPerPage")).toInt() != 4 ||
        !proof_plan.value(QStringLiteral("includeNotes")).toBool() ||
        proof_plan.value(QStringLiteral("thumbnailColorManagement")).toString() !=
            QStringLiteral("sRGB to sRGB proof space") ||
        !proof_plan.value(QStringLiteral("resolvedName")).toString().endsWith(
            QStringLiteral(".pdf"))) {
      return fail("PDF proof plan should resolve page layout, note inclusion, color management, and filename");
    }
    if (!restored.exportConfiguredCarousel()) {
      std::cerr << restored.status().toStdString() << '\n';
      return fail("PDF proof should export beside the platform-ready board images");
    }
    const auto proof_path = proof_plan.value(QStringLiteral("outputPath")).toString();
    QFile proof_file(proof_path);
    if (!proof_file.open(QIODevice::ReadOnly) ||
        !proof_file.read(8).startsWith("%PDF-") ||
        proof_file.size() < 5000 ||
        QDir(proof_export_dir).entryList(
            QStringList{QStringLiteral("*.png")}, QDir::Files).size() != 5 ||
        QDir(proof_export_dir).entryList(
            QStringList{QStringLiteral("*.pdf")}, QDir::Files).size() != 1) {
      return fail("PDF proof should be a real separate PDF without replacing five image outputs");
    }
    proof_file.close();

    const auto pdfinfo = QStandardPaths::findExecutable(QStringLiteral("pdfinfo"));
    QProcess pdfinfo_process;
    pdfinfo_process.start(pdfinfo, {proof_path});
    if (pdfinfo.isEmpty() || !pdfinfo_process.waitForStarted(5000) ||
        !pdfinfo_process.waitForFinished(30000) || pdfinfo_process.exitCode() != 0) {
      return fail("PDF proof verification requires pdfinfo");
    }
    const auto pdf_metadata = QString::fromUtf8(pdfinfo_process.readAllStandardOutput());
    if (!QRegularExpression(QStringLiteral("(?:^|\\n)Pages:\\s+2(?:\\s|$)"))
             .match(pdf_metadata).hasMatch() ||
        !pdf_metadata.contains(QStringLiteral("Client Review Proof")) ||
        !pdf_metadata.contains(QStringLiteral("A4"))) {
      return fail("five proof boards should resolve to two titled A4 PDF pages");
    }

    const auto pdftoppm = QStandardPaths::findExecutable(QStringLiteral("pdftoppm"));
    const auto rendered_prefix = dir.filePath(QStringLiteral("pdf-contact-sheet-page"));
    QProcess render_process;
    render_process.start(
        pdftoppm,
        {QStringLiteral("-png"), QStringLiteral("-r"), QStringLiteral("72"),
         proof_path, rendered_prefix});
    if (pdftoppm.isEmpty() || !render_process.waitForStarted(5000) ||
        !render_process.waitForFinished(30000) || render_process.exitCode() != 0) {
      return fail("PDF proof verification should render both contact-sheet pages");
    }
    const QImage proof_page_one(rendered_prefix + QStringLiteral("-1.png"));
    const QImage proof_page_two(rendered_prefix + QStringLiteral("-2.png"));
    if (proof_page_one.isNull() || proof_page_two.isNull() ||
        proof_page_one.width() <= proof_page_one.height() ||
        proof_page_two.width() <= proof_page_two.height() ||
        !region_contains_non_background(
            proof_page_one, proof_page_one.rect(), QColor(Qt::white)) ||
        !region_contains_non_background(
            proof_page_two, proof_page_two.rect(), QColor(Qt::white))) {
      return fail("PDF proof pages should render as populated landscape contact sheets");
    }

    restored.setExportCollisionPolicy(QStringLiteral("skip"));
    QVariantMap colliding_proof_plan;
    for (const auto& value : restored.exportOutputPreview()) {
      const auto plan = value.toMap();
      if (plan.value(QStringLiteral("artifactType")).toString() == QStringLiteral("proof")) {
        colliding_proof_plan = plan;
        break;
      }
    }
    if (!colliding_proof_plan.value(QStringLiteral("collision")).toBool() ||
        colliding_proof_plan.value(QStringLiteral("action")).toString() !=
            QStringLiteral("skip")) {
      return fail("PDF proof should obey the shared export collision policy");
    }
  }

  {
    CarouselDocumentController controller;
    controller.createCarouselWithSettings(QVariantMap{
        {QStringLiteral("carouselName"), QStringLiteral("Named Text Style Proof")},
        {QStringLiteral("documentFormat"), QStringLiteral("4x5")},
        {QStringLiteral("slideCount"), 3},
    });
    QStringList heading_ids;
    for (int board_index = 0; board_index < 3; ++board_index) {
      controller.selectSlide(board_index);
      controller.addTextItemWithRole(
          QStringLiteral("heading"),
          QStringLiteral("Heading %1").arg(board_index + 1));
      heading_ids.push_back(controller.selectedItemId());
    }
    if (controller.textStyles().size() != 9) {
      return fail("new carousel documents should expose the complete nine-role type scale");
    }
    controller.selectItem(heading_ids.at(0));
    controller.setSelectedTextPointSize(88.0);
    controller.setSelectedTextColor(QStringLiteral("#224466"));
    controller.setSelectedTextLineHeight(1.35);
    if (!controller.previewTextStyleUpdate(QStringLiteral("heading"), QStringLiteral("all"))) {
      return fail("a selected heading should preview an all-document style-definition update");
    }
    auto style_preview = controller.textStylePreview();
    if (style_preview.value(QStringLiteral("mode")).toString() != QStringLiteral("update") ||
        style_preview.value(QStringLiteral("affectedLayerCount")).toInt() != 3 ||
        style_preview.value(QStringLiteral("affectedBoardCount")).toInt() != 3 ||
        style_preview.value(QStringLiteral("changes")).toList().size() != 3 ||
        style_preview.value(QStringLiteral("style")).toMap().value(
            QStringLiteral("pointSize")).toDouble() != 88.0) {
      return fail("text-style update preview should list exact layers, boards, and proposed type values");
    }
    if (!controller.applyTextStylePreview()) {
      return fail("an all-document text-style preview should apply explicitly");
    }
    auto item_by_id = [&controller](const QString& item_id) {
      for (const auto& value : controller.canvasItems()) {
        const auto item = value.toMap();
        if (item.value(QStringLiteral("itemId")).toString() == item_id) {
          return item;
        }
      }
      return QVariantMap{};
    };
    for (const auto& item_id : heading_ids) {
      const auto item = item_by_id(item_id);
      if (qAbs(item.value(QStringLiteral("textPointSize")).toDouble() - 88.0) > 0.01 ||
          item.value(QStringLiteral("textColor")).toString() != QStringLiteral("#224466") ||
          qAbs(item.value(QStringLiteral("textLineHeight")).toDouble() - 1.35) > 0.01 ||
          item.value(QStringLiteral("textStyleId")).toString() != QStringLiteral("heading")) {
        return fail("applying a named heading style should synchronize its full type scale across boards");
      }
    }
    controller.undo();
    if (qAbs(item_by_id(heading_ids.at(1)).value(QStringLiteral("textPointSize")).toDouble() - 88.0) <
        0.01) {
      return fail("an all-document text-style application should undo in one step");
    }
    controller.redo();
    if (qAbs(item_by_id(heading_ids.at(2)).value(QStringLiteral("textPointSize")).toDouble() - 88.0) >
        0.01) {
      return fail("redo should restore the complete named-style application");
    }

    controller.selectItem(heading_ids.at(0));
    if (!controller.createTextStyleFromSelection(QStringLiteral("Campaign Heading")) ||
        controller.textStyles().size() != 10) {
      return fail("a selected text layer should save as a reusable custom named style");
    }
    const auto custom_style_id =
        controller.selectionDetails().value(QStringLiteral("textStyleId")).toString();
    controller.setSelectedTextPointSize(77.0);
    controller.selectSlide(1);
    controller.selectSlideRange(2);
    if (!controller.previewTextStyleApplication(custom_style_id, QStringLiteral("selected"))) {
      return fail("a custom named style should preview against the selected board range");
    }
    style_preview = controller.textStylePreview();
    if (style_preview.value(QStringLiteral("affectedLayerCount")).toInt() != 2 ||
        style_preview.value(QStringLiteral("affectedBoardNumbers")).toList() !=
            QVariantList{2, 3} ||
        !controller.applyTextStylePreview()) {
      return fail("selected-range text-style preview should list and apply only boards 2 and 3");
    }
    if (qAbs(item_by_id(heading_ids.at(0)).value(QStringLiteral("textPointSize")).toDouble() - 77.0) >
            0.01 ||
        qAbs(item_by_id(heading_ids.at(1)).value(QStringLiteral("textPointSize")).toDouble() - 88.0) >
            0.01 ||
        item_by_id(heading_ids.at(2)).value(QStringLiteral("textStyleId")).toString() !=
            custom_style_id) {
      return fail("selected-range style application should leave an out-of-range local edit untouched");
    }
    if (!controller.saveDocument()) {
      return fail("named text styles should save with their document");
    }
    CarouselDocumentController restored;
    if (!restored.loadDocument(controller.savedDocumentId()) || restored.textStyles().size() != 10) {
      return fail("built-in and custom named text styles should survive save and reopen");
    }
    bool restored_custom_style = false;
    for (const auto& value : restored.textStyles()) {
      const auto style = value.toMap();
      restored_custom_style = restored_custom_style ||
                              (style.value(QStringLiteral("styleId")).toString() == custom_style_id &&
                               style.value(QStringLiteral("name")).toString() ==
                                   QStringLiteral("Campaign Heading"));
    }
    if (!restored_custom_style) {
      return fail("custom text-style identity and name should remain reusable after reopening");
    }
  }

  {
    CarouselDocumentController controller;
    controller.createCarouselWithSettings(QVariantMap{
        {QStringLiteral("carouselName"), QStringLiteral("Document Copy Proof")},
        {QStringLiteral("documentFormat"), QStringLiteral("4x5")},
        {QStringLiteral("slideCount"), 3},
    });
    controller.setDocumentVariable(QStringLiteral("title"), QStringLiteral("Summer Field Notes"));
    controller.setDocumentVariable(QStringLiteral("client"), QStringLiteral("Northwind"));
    controller.setDocumentVariable(QStringLiteral("campaign"), QStringLiteral("Open Country"));
    controller.setDocumentVariable(QStringLiteral("date"), QStringLiteral("July 20, 2026"));
    controller.setDocumentVariable(QStringLiteral("url"), QStringLiteral("northwind.test/field"));
    controller.setDocumentVariable(QStringLiteral("handle"), QStringLiteral("@northwind"));
    const auto variable_copy = QStringLiteral(
        "{{title}} · {{client}} · {{campaign}} · {{date}} · {{url}} · {{handle}} · "
        "{{board_number}}/{{total_boards}}");
    controller.selectSlide(0);
    controller.addTextItem(variable_copy);
    const auto variable_item_id = controller.selectedItemId();
    auto selection = controller.selectionDetails();
    const auto resolved_board_one = QStringLiteral(
        "Summer Field Notes · Northwind · Open Country · July 20, 2026 · "
        "northwind.test/field · @northwind · 1/3");
    if (selection.value(QStringLiteral("text")).toString() != variable_copy ||
        selection.value(QStringLiteral("resolvedText")).toString() != resolved_board_one) {
      return fail("document variables should preserve raw authored tokens and expose every resolved value");
    }
    QString joined_layout;
    for (const auto& value : controller.canvasItems().constFirst().toMap().value(
             QStringLiteral("textLayoutLines")).toList()) {
      joined_layout += value.toMap().value(QStringLiteral("text")).toString();
    }
    if (!joined_layout.contains(QStringLiteral("Summer Field Notes")) ||
        joined_layout.contains(QStringLiteral("{{title}}"))) {
      return fail("resolved document variables should drive the shared preview and export line layout");
    }
    if (!controller.saveSelectedTextAsSnippet(QStringLiteral("Campaign footer")) ||
        controller.copySnippets().size() != 1 ||
        controller.copySnippets().constFirst().toMap().value(QStringLiteral("text")).toString() !=
            variable_copy) {
      return fail("copy snippets should retain reusable raw text and variable tokens");
    }

    controller.moveSlide(0, 2);
    QVariantMap moved_item;
    for (const auto& value : controller.canvasItems()) {
      const auto item = value.toMap();
      if (item.value(QStringLiteral("itemId")).toString() == variable_item_id) {
        moved_item = item;
        break;
      }
    }
    if (moved_item.value(QStringLiteral("resolvedText")).toString() !=
        QString(resolved_board_one).replace(QStringLiteral("1/3"), QStringLiteral("3/3"))) {
      return fail("board-number variables should recompute after board reordering");
    }
    controller.undo();
    controller.selectSlide(1);
    controller.addTextItem(QStringLiteral("Recipient: "));
    const auto snippet_id = controller.copySnippets().constFirst().toMap().value(
        QStringLiteral("snippetId")).toString();
    if (!controller.insertCopySnippet(snippet_id) ||
        !controller.selectionDetails().value(QStringLiteral("text")).toString().contains(
            QStringLiteral("{{client}}")) ||
        !controller.selectionDetails().value(QStringLiteral("resolvedText")).toString().endsWith(
            QStringLiteral("2/3"))) {
      return fail("a saved copy snippet should insert raw tokens and resolve in its destination board context");
    }
    const auto inserted_raw = controller.selectionDetails().value(QStringLiteral("text")).toString();
    const auto inserted_resolved = controller.selectionDetails().value(
        QStringLiteral("resolvedText")).toString();
    if (!controller.detachSelectedTextVariables() ||
        controller.selectionDetails().value(QStringLiteral("text")).toString() != inserted_resolved ||
        controller.selectionDetails().value(QStringLiteral("resolvedText")).toString() != inserted_resolved) {
      return fail("detaching variables should freeze the exact visible result as ordinary text");
    }
    controller.undo();
    if (controller.selectionDetails().value(QStringLiteral("text")).toString() != inserted_raw) {
      return fail("detach-to-text should restore every raw token in one undo step");
    }
    controller.redo();
    controller.setDocumentVariable(QStringLiteral("client"), QStringLiteral("Changed Client"));
    if (controller.selectionDetails().value(QStringLiteral("text")).toString() != inserted_resolved) {
      return fail("detached ordinary text should not change when document variables change later");
    }
    if (!controller.deleteCopySnippet(snippet_id) || !controller.copySnippets().isEmpty()) {
      return fail("copy snippets should support exact deletion");
    }
    controller.undo();
    if (controller.copySnippets().size() != 1) {
      return fail("copy snippet deletion should undo in one step");
    }
    if (!controller.saveDocument()) {
      return fail("document variables and copy snippets should save with their document");
    }
    CarouselDocumentController restored;
    if (!restored.loadDocument(controller.savedDocumentId()) ||
        restored.documentVariables().value(QStringLiteral("client")).toString() !=
            QStringLiteral("Changed Client") ||
        restored.documentVariables().value(QStringLiteral("total_boards")).toInt() != 3 ||
        restored.copySnippets().size() != 1) {
      return fail("document variables and copy snippets should survive save and reopen");
    }
    bool raw_tokens_restored = false;
    for (const auto& value : restored.canvasItems()) {
      const auto item = value.toMap();
      raw_tokens_restored = raw_tokens_restored ||
                            (item.value(QStringLiteral("itemId")).toString() == variable_item_id &&
                             item.value(QStringLiteral("text")).toString() == variable_copy &&
                             item.value(QStringLiteral("resolvedText")).toString().contains(
                                 QStringLiteral("Changed Client")));
    }
    if (!raw_tokens_restored) {
      return fail("reopened variable layers should keep raw tokens while resolving current document values");
    }
  }

  {
    CarouselDocumentController controller;
    controller.createCarouselWithSettings(QVariantMap{
        {QStringLiteral("carouselName"), QStringLiteral("Lists and Numbering Proof")},
        {QStringLiteral("documentFormat"), QStringLiteral("4x5")},
        {QStringLiteral("slideCount"), 3},
    });
    auto item_by_id = [&controller](const QString& item_id) {
      for (const auto& value : controller.canvasItems()) {
        const auto item = value.toMap();
        if (item.value(QStringLiteral("itemId")).toString() == item_id) {
          return item;
        }
      }
      return QVariantMap{};
    };
    controller.selectSlide(0);
    const auto raw_list = QStringLiteral(
        "A deliberately long first action that wraps onto a continuation line\n"
        "A second action that also remains ordinary editable authored text");
    controller.addTextItem(raw_list);
    const auto list_item_id = controller.selectedItemId();
    controller.setSelectedTextPointSize(42.0);
    controller.setSelectedTextFrameSize(360.0, 420.0);
    controller.setSelectedTextListStyle(QStringLiteral("bullets"));
    controller.setSelectedTextHangingIndent(48.0);
    auto list_item = item_by_id(list_item_id);
    if (list_item.value(QStringLiteral("text")).toString() != raw_list ||
        list_item.value(QStringLiteral("textListStyle")).toString() != QStringLiteral("bullets") ||
        !list_item.value(QStringLiteral("resolvedText")).toString().startsWith(
            QStringLiteral("• A deliberately")) ||
        !list_item.value(QStringLiteral("resolvedText")).toString().contains(
            QStringLiteral("\n• A second"))) {
      return fail("bullet formatting should remain non-destructive while resolving every paragraph");
    }
    bool found_hanging_line = false;
    bool found_first_line = false;
    for (const auto& value : list_item.value(QStringLiteral("textLayoutLines")).toList()) {
      const auto line = value.toMap();
      found_first_line = found_first_line || line.value(QStringLiteral("x")).toDouble() < 1.0;
      found_hanging_line = found_hanging_line ||
                           qAbs(line.value(QStringLiteral("x")).toDouble() - 48.0) < 1.0;
    }
    if (!found_first_line || !found_hanging_line) {
      return fail("hanging indents should be encoded in the shared preview and export line geometry");
    }
    controller.setSelectedTextListStyle(QStringLiteral("numbering"));
    list_item = item_by_id(list_item_id);
    if (!list_item.value(QStringLiteral("resolvedText")).toString().startsWith(
            QStringLiteral("1. A deliberately")) ||
        !list_item.value(QStringLiteral("resolvedText")).toString().contains(
            QStringLiteral("\n2. A second"))) {
      return fail("numbered lists should derive stable paragraph numbers without rewriting source copy");
    }
    controller.undo();
    if (item_by_id(list_item_id).value(QStringLiteral("textListStyle")).toString() !=
        QStringLiteral("bullets")) {
      return fail("list style changes should undo in one step");
    }
    controller.redo();

    controller.addTextItemWithRole(QStringLiteral("number"), QStringLiteral("Manual board number"));
    const auto board_number_id = controller.selectedItemId();
    controller.setSelectedTextAutoNumberSource(QStringLiteral("board"));
    if (item_by_id(board_number_id).value(QStringLiteral("resolvedText")).toString() !=
            QStringLiteral("1") ||
        item_by_id(board_number_id).value(QStringLiteral("text")).toString() !=
            QStringLiteral("Manual board number")) {
      return fail("board-order number fields should preserve fallback copy and resolve the current board number");
    }

    QStringList step_ids;
    for (int board_index = 0; board_index < 3; ++board_index) {
      controller.selectSlide(board_index);
      controller.addTextItemWithRole(QStringLiteral("number"), QStringLiteral("Manual step"));
      controller.setSelectedTextAutoNumberSource(QStringLiteral("step"));
      step_ids.push_back(controller.selectedItemId());
    }
    for (int index = 0; index < step_ids.size(); ++index) {
      if (item_by_id(step_ids.at(index)).value(QStringLiteral("resolvedText")).toInt() != index + 1) {
        return fail("step-order number fields should count automatic steps in board order");
      }
    }
    controller.moveSlide(2, 0);
    if (item_by_id(step_ids.at(2)).value(QStringLiteral("resolvedText")).toString() !=
            QStringLiteral("1") ||
        item_by_id(step_ids.at(0)).value(QStringLiteral("resolvedText")).toString() !=
            QStringLiteral("2") ||
        item_by_id(board_number_id).value(QStringLiteral("resolvedText")).toString() !=
            QStringLiteral("2")) {
      return fail("board and step automatic number fields should recompute after board reordering");
    }
    controller.undo();
    if (item_by_id(step_ids.at(0)).value(QStringLiteral("resolvedText")).toString() !=
            QStringLiteral("1") ||
        item_by_id(board_number_id).value(QStringLiteral("resolvedText")).toString() !=
            QStringLiteral("1")) {
      return fail("automatic number results should restore when board reordering is undone");
    }
    if (!controller.saveDocument()) {
      return fail("list geometry and automatic number sources should save with the document");
    }
    CarouselDocumentController restored;
    if (!restored.loadDocument(controller.savedDocumentId())) {
      return fail("lists and automatic numbers proof should reopen its saved document");
    }
    QVariantMap restored_list;
    QVariantMap restored_board_number;
    for (const auto& value : restored.canvasItems()) {
      const auto item = value.toMap();
      if (item.value(QStringLiteral("itemId")).toString() == list_item_id) {
        restored_list = item;
      } else if (item.value(QStringLiteral("itemId")).toString() == board_number_id) {
        restored_board_number = item;
      }
    }
    if (restored_list.value(QStringLiteral("textListStyle")).toString() !=
            QStringLiteral("numbering") ||
        qAbs(restored_list.value(QStringLiteral("textHangingIndent")).toDouble() - 48.0) > 0.01 ||
        restored_board_number.value(QStringLiteral("textAutoNumberSource")).toString() !=
            QStringLiteral("board") ||
        restored_board_number.value(QStringLiteral("resolvedText")).toString() !=
            QStringLiteral("1")) {
      return fail("list style, hanging indent, and automatic number source should survive save and reopen");
    }
  }

  {
    CarouselDocumentController controller;
    controller.createCarouselWithSettings(QVariantMap{
        {QStringLiteral("carouselName"), QStringLiteral("Focused Crop Proof")},
        {QStringLiteral("documentFormat"), QStringLiteral("4x5")},
        {QStringLiteral("slideCount"), 2},
    });
    const auto crop_source = write_image(
        dir,
        QStringLiteral("focused-crop-source.png"),
        split_image(QSize(800, 400), QColor(QStringLiteral("#ff453a")),
                    QColor(QStringLiteral("#0a84ff"))));
    controller.selectSlide(0);
    controller.addPhotoItem(crop_source, QStringLiteral("First crop source"));
    const auto first_media_id = controller.selectedItemId();
    controller.setSelectedItemSize(320.0, 320.0);
    controller.setCurrentFitMode(QStringLiteral("cover"));
    controller.setSelectedItemMediaScale(1.5);
    controller.setCurrentMediaCropMode(QStringLiteral("manual"));
    controller.nudgeSelectedItemMediaByPixels(10.0, 0.0);
    auto cropped = controller.selectionDetails();
    const auto nudged_offset = cropped.value(QStringLiteral("mediaOffsetX")).toDouble();
    if (cropped.value(QStringLiteral("cropMode")).toString() != QStringLiteral("manual")) {
      return fail("focused crop keyboard nudge should enable manual crop");
    }
    if (qAbs(nudged_offset) < 0.001) {
      return fail("focused crop keyboard nudge should move the selected source by exact canvas pixels");
    }
    controller.undo();
    if (qAbs(controller.selectionDetails().value(QStringLiteral("mediaOffsetX")).toDouble()) > 0.001) {
      return fail("focused crop keyboard nudge should undo as one crop edit");
    }
    controller.redo();
    if (qAbs(controller.selectionDetails().value(QStringLiteral("mediaOffsetX")).toDouble() -
             nudged_offset) > 0.001) {
      return fail("redo should restore the exact focused-crop keyboard nudge");
    }

    controller.selectSlide(1);
    controller.addPhotoItem(crop_source, QStringLiteral("Second crop source"));
    const auto second_media_id = controller.selectedItemId();
    if (!controller.selectAdjacentMediaItem(1) || controller.selectedItemId() != first_media_id ||
        controller.currentIndex() != 0 || !controller.selectAdjacentMediaItem(-1) ||
        controller.selectedItemId() != second_media_id || controller.currentIndex() != 1) {
      return fail("focused crop previous and next navigation should wrap across media layers and open their owning boards");
    }
    controller.selectItem(first_media_id);
    if (!controller.saveDocument()) {
      return fail("focused manual crop state should save with the document");
    }
    CarouselDocumentController restored;
    if (!restored.loadDocument(controller.savedDocumentId())) {
      return fail("focused crop proof should reopen its saved document");
    }
    QVariantMap restored_crop;
    for (const auto& value : restored.canvasItems()) {
      const auto item = value.toMap();
      if (item.value(QStringLiteral("itemId")).toString() == first_media_id) {
        restored_crop = item;
        break;
      }
    }
    if (restored_crop.value(QStringLiteral("mediaCropMode")).toString() !=
            QStringLiteral("manual") ||
        qAbs(restored_crop.value(QStringLiteral("mediaOffsetX")).toDouble() - nudged_offset) > 0.001) {
      return fail("focused crop manual authority and numeric position should survive save and reopen");
    }
    restored.selectItem(first_media_id);
    restored.resetSelectedItemMediaCrop();
    const auto reset = restored.selectionDetails();
    if (reset.value(QStringLiteral("cropMode")).toString() != QStringLiteral("smart") ||
        qAbs(reset.value(QStringLiteral("mediaScale")).toDouble() - 1.0) > 0.001 ||
        qAbs(reset.value(QStringLiteral("mediaOffsetX")).toDouble()) > 0.001 ||
        qAbs(reset.value(QStringLiteral("mediaOffsetY")).toDouble()) > 0.001) {
      return fail("focused crop reset should restore smart framing, one-times zoom, and centered position");
    }
  }

  {
    CarouselDocumentController controller;
    controller.createCarouselWithSettings(QVariantMap{
        {QStringLiteral("carouselName"), QStringLiteral("Crop Ownership Proof")},
        {QStringLiteral("documentFormat"), QStringLiteral("4x5")},
        {QStringLiteral("slideCount"), 1},
    });
    const auto ownership_source = write_image(
        dir,
        QStringLiteral("crop-ownership-source.png"),
        split_image(QSize(800, 400), QColor(QStringLiteral("#ff453a")),
                    QColor(QStringLiteral("#0a84ff"))));
    photo_editor::desktop::EditRecipeState edited_crop;
    edited_crop.crop_rect_x = 0.5;
    edited_crop.crop_rect_y = 0.0;
    edited_crop.crop_rect_width = 0.5;
    edited_crop.crop_rect_height = 1.0;
    const auto ownership_patch =
        photo_editor::desktop::serialize_adjustment_patch_json(
            edited_crop,
            photo_editor::desktop::all_adjustment_groups(),
            photo_editor::desktop::AdjustmentPatchScope::AssetBound);
    controller.addPhotoItem(
        ownership_source, QStringLiteral("Explicit crop ownership"), ownership_patch);
    const auto ownership_item_id = controller.selectedItemId();
    controller.setSelectedItemSize(600.0, 300.0);
    controller.setCurrentFitMode(QStringLiteral("cover"));
    controller.setSelectedItemMediaOffset(0.0, 0.0);
    if (controller.selectionDetails().value(QStringLiteral("cropSource")).toString() !=
        QStringLiteral("edit")) {
      return fail("new media should explicitly default to using the non-destructive Edit crop");
    }

    const auto edit_dir = dir.filePath(QStringLiteral("crop-owner-edit"));
    const auto original_dir = dir.filePath(QStringLiteral("crop-owner-original"));
    const auto carousel_dir = dir.filePath(QStringLiteral("crop-owner-carousel"));
    QDir().mkpath(edit_dir);
    QDir().mkpath(original_dir);
    QDir().mkpath(carousel_dir);
    controller.setExportManifestEnabled(false);
    if (!controller.exportCarouselToDirectory(edit_dir)) {
      return fail("Use Edit crop should export through the normal Carousel renderer");
    }
    controller.setCurrentMediaCropSource(QStringLiteral("original"));
    if (controller.selectionDetails().value(QStringLiteral("cropSource")).toString() !=
        QStringLiteral("original")) {
      return fail("Use original should become an explicit per-instance crop source");
    }
    controller.undo();
    if (controller.selectionDetails().value(QStringLiteral("cropSource")).toString() !=
        QStringLiteral("edit")) {
      return fail("crop ownership changes should undo without mutating the saved photo recipe");
    }
    controller.redo();
    if (!controller.exportCarouselToDirectory(original_dir)) {
      return fail("Use original should export through the normal Carousel renderer");
    }
    controller.setCurrentMediaCropSource(QStringLiteral("carousel"));
    if (controller.selectionDetails().value(QStringLiteral("cropSource")).toString() !=
            QStringLiteral("carousel") ||
        controller.selectionDetails().value(QStringLiteral("cropMode")).toString() !=
            QStringLiteral("manual")) {
      return fail("Use Carousel crop should make Carousel framing explicitly authoritative");
    }
    if (!controller.exportCarouselToDirectory(carousel_dir)) {
      return fail("Use Carousel crop should export through the normal Carousel renderer");
    }
    const QImage edit_export(QDir(edit_dir).filePath(QStringLiteral("carousel-01.png")));
    const QImage original_export(QDir(original_dir).filePath(QStringLiteral("carousel-01.png")));
    const QImage carousel_export(QDir(carousel_dir).filePath(QStringLiteral("carousel-01.png")));
    const auto frame = controller.selectionDetails();
    const auto sample_y = qRound(frame.value(QStringLiteral("centerY")).toDouble());
    const auto left_sample_x = qRound(frame.value(QStringLiteral("x")).toDouble() + 150.0);
    const auto edit_pixel = edit_export.pixelColor(left_sample_x, sample_y);
    const auto original_pixel = original_export.pixelColor(left_sample_x, sample_y);
    const auto carousel_pixel = carousel_export.pixelColor(left_sample_x, sample_y);
    if (edit_pixel.blue() < 180 || edit_pixel.red() > 80 ||
        original_pixel.red() < 180 || original_pixel.blue() > 80 ||
        carousel_pixel.red() < 180 || carousel_pixel.blue() > 80) {
      return fail("Edit, original, and Carousel crop ownership should produce distinct truthful exported source framing");
    }

    controller.clearSelection();
    controller.assignPrimaryImage(
        ownership_source, QStringLiteral("Slot crop ownership"), ownership_patch);
    controller.setCurrentMediaCropSource(QStringLiteral("original"));
    if (controller.selectionDetails().value(QStringLiteral("cropSource")).toString() !=
        QStringLiteral("original")) {
      return fail("slot media should expose the same explicit crop ownership choices as layers");
    }
    if (!controller.saveDocument()) {
      return fail("crop ownership choices should save without rewriting source edits");
    }
    CarouselDocumentController restored;
    if (!restored.loadDocument(controller.savedDocumentId()) ||
        restored.selectionDetails().value(QStringLiteral("cropSource")).toString() !=
            QStringLiteral("original")) {
      return fail("slot crop ownership should survive save and reopen");
    }
    bool restored_carousel_owner = false;
    for (const auto& value : restored.canvasItems()) {
      const auto item = value.toMap();
      restored_carousel_owner = restored_carousel_owner ||
                                  (item.value(QStringLiteral("itemId")).toString() ==
                                       ownership_item_id &&
                                   item.value(QStringLiteral("mediaCropSource")).toString() ==
                                       QStringLiteral("carousel"));
    }
    if (!restored_carousel_owner) {
      return fail("layer crop ownership should survive save and reopen independently of slot ownership");
    }
  }

  {
    CarouselDocumentController controller;
    controller.createCarouselWithSettings(QVariantMap{
        {QStringLiteral("carouselName"), QStringLiteral("Photo Frame Components Proof")},
        {QStringLiteral("documentFormat"), QStringLiteral("4x5")},
        {QStringLiteral("slideCount"), 1},
    });
    const auto frame_source = write_image(
        dir,
        QStringLiteral("photo-frame-source.png"),
        split_image(QSize(800, 600), QColor(QStringLiteral("#ff453a")),
                    QColor(QStringLiteral("#0a84ff"))));
    const auto presets = controller.photoFramePresets();
    if (presets.size() != 6) {
      return fail("photo frame library should expose all six requested editable compositions");
    }
    const QStringList expected_ids = {
        QStringLiteral("bordered-print"),
        QStringLiteral("rounded-card"),
        QStringLiteral("instant-photo"),
        QStringLiteral("contact-sheet"),
        QStringLiteral("browser-device"),
        QStringLiteral("editorial-mount"),
    };
    for (int preset_index = 0; preset_index < presets.size(); ++preset_index) {
      CarouselDocumentController framed;
      framed.createCarouselWithSettings(QVariantMap{
          {QStringLiteral("carouselName"), QStringLiteral("Frame %1").arg(preset_index + 1)},
          {QStringLiteral("documentFormat"), QStringLiteral("4x5")},
          {QStringLiteral("slideCount"), 1},
      });
      framed.addPhotoItem(frame_source, QStringLiteral("Editable source photo"));
      const auto photo_id = framed.selectedItemId();
      const auto preset = presets.at(preset_index).toMap();
      if (preset.value(QStringLiteral("presetId")).toString() != expected_ids.at(preset_index) ||
          !framed.applyPhotoFramePreset(expected_ids.at(preset_index))) {
        return fail("every photo frame preset should apply to a selected photo");
      }
      const auto expected_layers = preset.value(QStringLiteral("layerCount")).toInt();
      const auto framed_selection = framed.selectionDetails();
      QSet<QString> group_ids;
      int image_count = 0;
      int shape_count = 0;
      int text_count = 0;
      for (const auto& value : framed.canvasItems()) {
        const auto item = value.toMap();
        group_ids.insert(item.value(QStringLiteral("objectGroupId")).toString());
        image_count += item.value(QStringLiteral("type")).toString() == QStringLiteral("image");
        shape_count += item.value(QStringLiteral("type")).toString() == QStringLiteral("shape");
        text_count += item.value(QStringLiteral("type")).toString() == QStringLiteral("text");
      }
      if (framed.canvasItems().size() != expected_layers || group_ids.size() != 1 ||
          group_ids.contains(QString{}) || image_count != 1 || shape_count != 1 ||
          text_count != expected_layers - 2 ||
          framed_selection.value(QStringLiteral("objectGroupCount")).toInt() != expected_layers ||
          !framed_selection.value(QStringLiteral("isObjectGroup")).toBool()) {
        return fail("photo frames should be grouped compositions of ordinary editable photo, shape, and optional text layers");
      }
      framed.undo();
      if (framed.canvasItems().size() != 1 || framed.selectedItemId() != photo_id) {
        return fail("applying a photo frame composition should undo atomically to the source photo");
      }
      framed.redo();
      if (framed.canvasItems().size() != expected_layers) {
        return fail("redo should restore the complete editable photo frame composition");
      }

      if (expected_ids.at(preset_index) == QStringLiteral("editorial-mount")) {
        const auto export_dir = dir.filePath(QStringLiteral("editorial-frame-export"));
        QDir().mkpath(export_dir);
        framed.setExportManifestEnabled(false);
        if (!framed.exportCarouselToDirectory(export_dir)) {
          return fail("editable photo frame compositions should export through normal layer rendering");
        }
        const QImage exported(QDir(export_dir).filePath(QStringLiteral("carousel-01.png")));
        const auto photo = framed.selectionDetails();
        const auto mount_pixel = exported.pixelColor(
            qRound(photo.value(QStringLiteral("x")).toDouble() - 24.0),
            qRound(photo.value(QStringLiteral("centerY")).toDouble()));
        if (mount_pixel.red() < 180 || mount_pixel.green() < 170 || mount_pixel.blue() < 150) {
          return fail("editorial mount should render visibly beyond the photo frame in export");
        }
        if (!framed.saveDocument()) {
          return fail("editable photo frame composition should save");
        }
        CarouselDocumentController restored;
        if (!restored.loadDocument(framed.savedDocumentId()) ||
            restored.canvasItems().size() != expected_layers ||
            restored.selectionDetails().value(QStringLiteral("objectGroupCount")).toInt() !=
                expected_layers) {
          return fail("editable photo frame layers and grouping should survive save and reopen");
        }
      }
    }
  }

  {
    CarouselDocumentController controller;
    controller.createCarouselWithSettings(QVariantMap{
        {QStringLiteral("carouselName"), QStringLiteral("Component Lifecycle Proof")},
        {QStringLiteral("documentFormat"), QStringLiteral("4x5")},
        {QStringLiteral("slideCount"), 3},
    });
    const auto item_by_id = [](const CarouselDocumentController& document,
                               const QString& item_id) {
      for (const auto& value : document.canvasItems()) {
        const auto item = value.toMap();
        if (item.value(QStringLiteral("itemId")).toString() == item_id) {
          return item;
        }
      }
      return QVariantMap{};
    };
    const auto stable_item_payload = [](const QVariantMap& item) {
      return QVariantMap{
          {QStringLiteral("type"), item.value(QStringLiteral("type"))},
          {QStringLiteral("text"), item.value(QStringLiteral("text"))},
          {QStringLiteral("fillColor"), item.value(QStringLiteral("fillColor"))},
          {QStringLiteral("x"), item.value(QStringLiteral("x"))},
          {QStringLiteral("y"), item.value(QStringLiteral("y"))},
          {QStringLiteral("width"), item.value(QStringLiteral("width"))},
          {QStringLiteral("height"), item.value(QStringLiteral("height"))},
          {QStringLiteral("definition"), item.value(QStringLiteral("componentDefinitionId"))},
          {QStringLiteral("instance"), item.value(QStringLiteral("componentInstanceId"))},
          {QStringLiteral("part"), item.value(QStringLiteral("componentPartId"))},
          {QStringLiteral("version"), item.value(QStringLiteral("componentDefinitionVersion"))},
      };
    };
    controller.addShapeItem(QStringLiteral("rectangle"));
    const auto authored_shape_id = controller.selectedItemId();
    controller.setSelectedShapeFillColor(QStringLiteral("#3159a8"));
    controller.setSelectedItemSize(700.0, 430.0);
    controller.setSelectedItemCenter(540.0, 650.0);
    controller.addTextItemWithRole(
        QStringLiteral("heading"), QStringLiteral("COMPONENT V1"));
    const auto authored_title_id = controller.selectedItemId();
    controller.setSelectedItemSize(560.0, 110.0);
    controller.setSelectedItemCenter(540.0, 590.0);
    controller.addTextItemWithRole(
        QStringLiteral("caption"), QStringLiteral("Supporting copy v1"));
    const auto authored_caption_id = controller.selectedItemId();
    controller.setSelectedItemSize(560.0, 80.0);
    controller.setSelectedItemCenter(540.0, 710.0);
    controller.selectItem(authored_shape_id);
    controller.addItemToSelection(authored_title_id);
    controller.addItemToSelection(authored_caption_id);
    if (!controller.saveSelectionAsTemplate(
            QStringLiteral("Lifecycle Card"), QStringLiteral("Components"))) {
      return fail("component lifecycle proof should create a custom definition");
    }
    QString template_id;
    for (const auto& value : controller.componentLibraryEntries()) {
      const auto entry = value.toMap();
      if (entry.value(QStringLiteral("source")).toString() == QStringLiteral("custom") &&
          entry.value(QStringLiteral("name")).toString() == QStringLiteral("Lifecycle Card")) {
        template_id = entry.value(QStringLiteral("componentId")).toString();
        break;
      }
    }
    if (template_id.isEmpty()) {
      return fail("saved custom component should expose its definition identity");
    }
    controller.selectSlide(0);
    const auto before_a = controller.canvasItems().size();
    controller.applyUserTemplate(template_id, false);
    const auto after_a = controller.canvasItems();
    QString a_instance_id;
    QString a_shape_id;
    QString a_title_id;
    QString a_caption_id;
    for (qsizetype index = before_a; index < after_a.size(); ++index) {
      const auto item = after_a.at(index).toMap();
      a_instance_id = item.value(QStringLiteral("componentInstanceId")).toString();
      if (item.value(QStringLiteral("type")).toString() == QStringLiteral("shape")) {
        a_shape_id = item.value(QStringLiteral("itemId")).toString();
      } else if (item.value(QStringLiteral("text")).toString() == QStringLiteral("COMPONENT V1")) {
        a_title_id = item.value(QStringLiteral("itemId")).toString();
      } else if (item.value(QStringLiteral("text")).toString() == QStringLiteral("Supporting copy v1")) {
        a_caption_id = item.value(QStringLiteral("itemId")).toString();
      }
    }
    controller.selectSlide(1);
    const auto before_b = controller.canvasItems().size();
    controller.applyUserTemplate(template_id, false);
    const auto after_b = controller.canvasItems();
    QString b_instance_id;
    QString b_shape_id;
    QString b_title_id;
    QString b_caption_id;
    for (qsizetype index = before_b; index < after_b.size(); ++index) {
      const auto item = after_b.at(index).toMap();
      b_instance_id = item.value(QStringLiteral("componentInstanceId")).toString();
      if (item.value(QStringLiteral("type")).toString() == QStringLiteral("shape")) {
        b_shape_id = item.value(QStringLiteral("itemId")).toString();
      } else if (item.value(QStringLiteral("text")).toString() == QStringLiteral("COMPONENT V1")) {
        b_title_id = item.value(QStringLiteral("itemId")).toString();
      } else if (item.value(QStringLiteral("text")).toString() == QStringLiteral("Supporting copy v1")) {
        b_caption_id = item.value(QStringLiteral("itemId")).toString();
      }
    }
    if (a_instance_id.isEmpty() || b_instance_id.isEmpty() ||
        a_instance_id == b_instance_id || a_shape_id.isEmpty() || a_title_id.isEmpty() ||
        a_caption_id.isEmpty() || b_shape_id.isEmpty() || b_title_id.isEmpty() ||
        b_caption_id.isEmpty()) {
      return fail("component lifecycle proof should place two complete independent instances");
    }

    controller.selectItem(b_title_id);
    controller.ungroupSelectedItems();
    controller.selectItem(b_title_id);
    controller.setSelectedItemCenter(
        item_by_id(controller, b_title_id).value(QStringLiteral("centerX")).toDouble() + 36.0,
        item_by_id(controller, b_title_id).value(QStringLiteral("centerY")).toDouble() + 18.0);
    controller.setSelectedText(QStringLiteral("B LOCAL TITLE"));
    controller.selectItem(b_caption_id);
    controller.setSelectedText(QStringLiteral("B LOCAL REMOVED PART"));
    const auto b_before_definition_update = QVariantMap{
        {QStringLiteral("shape"), stable_item_payload(item_by_id(controller, b_shape_id))},
        {QStringLiteral("title"), stable_item_payload(item_by_id(controller, b_title_id))},
        {QStringLiteral("caption"), stable_item_payload(item_by_id(controller, b_caption_id))},
    };

    controller.selectItem(a_title_id);
    controller.setSelectedText(QStringLiteral("DEFINITION V2 TITLE"));
    controller.selectItem(a_shape_id);
    controller.setSelectedShapeFillColor(QStringLiteral("#d14f70"));
    controller.selectItem(a_caption_id);
    controller.ungroupSelectedItems();
    controller.selectItem(a_caption_id);
    controller.removeSelectedItem();
    controller.addTextItemWithRole(
        QStringLiteral("caption"), QStringLiteral("NEW DEFINITION PART"));
    const auto a_new_part_id = controller.selectedItemId();
    controller.setSelectedItemSize(520.0, 70.0);
    controller.setSelectedItemCenter(540.0, 760.0);
    controller.selectItem(a_title_id);
    controller.addItemToSelection(a_new_part_id);
    if (!controller.updateSelectedComponentDefinition()) {
      return fail("updating a saved component should create a new explicit definition version");
    }
    if (stable_item_payload(item_by_id(controller, b_shape_id)) !=
            b_before_definition_update.value(QStringLiteral("shape")).toMap() ||
        stable_item_payload(item_by_id(controller, b_title_id)) !=
            b_before_definition_update.value(QStringLiteral("title")).toMap() ||
        stable_item_payload(item_by_id(controller, b_caption_id)) !=
            b_before_definition_update.value(QStringLiteral("caption")).toMap()) {
      return fail("saving a newer component definition must not silently rewrite another placed instance");
    }
    controller.selectItem(b_title_id);
    auto lifecycle_state = controller.componentInstanceState();
    if (!lifecycle_state.value(QStringLiteral("updateAvailable")).toBool() ||
        lifecycle_state.value(QStringLiteral("version")).toInt() != 1 ||
        lifecycle_state.value(QStringLiteral("currentVersion")).toInt() != 2 ||
        lifecycle_state.value(QStringLiteral("localOverrideCount")).toInt() < 2) {
      return fail("older instances should report the newer version and their local overrides without applying it");
    }
    const auto b_title_bounds_before = QRectF(
        item_by_id(controller, b_title_id).value(QStringLiteral("x")).toDouble(),
        item_by_id(controller, b_title_id).value(QStringLiteral("y")).toDouble(),
        item_by_id(controller, b_title_id).value(QStringLiteral("width")).toDouble(),
        item_by_id(controller, b_title_id).value(QStringLiteral("height")).toDouble());
    if (!controller.applySelectedComponentUpdate()) {
      return fail("a placed instance should apply a newer definition only through an explicit command");
    }
    const auto b_title_after = item_by_id(controller, b_title_id);
    const auto b_shape_after = item_by_id(controller, b_shape_id);
    const auto b_caption_after = item_by_id(controller, b_caption_id);
    bool found_new_part = false;
    QString b_new_part_id;
    for (const auto& value : controller.canvasItems()) {
      const auto item = value.toMap();
      if (item.value(QStringLiteral("componentInstanceId")).toString() == b_instance_id &&
          item.value(QStringLiteral("text")).toString() == QStringLiteral("NEW DEFINITION PART")) {
        found_new_part = true;
        b_new_part_id = item.value(QStringLiteral("itemId")).toString();
        break;
      }
    }
    const QRectF b_title_bounds_after(
        b_title_after.value(QStringLiteral("x")).toDouble(),
        b_title_after.value(QStringLiteral("y")).toDouble(),
        b_title_after.value(QStringLiteral("width")).toDouble(),
        b_title_after.value(QStringLiteral("height")).toDouble());
    if (b_title_after.value(QStringLiteral("text")).toString() !=
            QStringLiteral("B LOCAL TITLE") ||
        b_title_bounds_after != b_title_bounds_before ||
        b_shape_after.value(QStringLiteral("fillColor")).toString() !=
            QStringLiteral("#d14f70") ||
        b_caption_after.value(QStringLiteral("text")).toString() !=
            QStringLiteral("B LOCAL REMOVED PART") ||
        !b_caption_after.value(QStringLiteral("componentDefinitionId")).toString().isEmpty() ||
        !found_new_part || b_new_part_id.isEmpty()) {
      return fail("explicit component update should merge new fields and parts while retaining and detaching local overrides");
    }
    lifecycle_state = controller.componentInstanceState();
    if (lifecycle_state.value(QStringLiteral("updateAvailable")).toBool() ||
        lifecycle_state.value(QStringLiteral("version")).toInt() != 2 ||
        lifecycle_state.value(QStringLiteral("currentVersion")).toInt() != 2) {
      return fail("an explicitly updated instance should report the current definition version");
    }
    controller.undo();
    if (item_by_id(controller, b_shape_id).value(QStringLiteral("fillColor")).toString() !=
            QStringLiteral("#3159a8") ||
        item_by_id(controller, b_title_id).value(QStringLiteral("text")).toString() !=
            QStringLiteral("B LOCAL TITLE") ||
        item_by_id(controller, b_caption_id).value(QStringLiteral("componentInstanceId")).toString() !=
            b_instance_id || !item_by_id(controller, b_new_part_id).isEmpty()) {
      return fail("component update should undo atomically to the older instance and retained overrides");
    }
    controller.redo();
    if (item_by_id(controller, b_shape_id).value(QStringLiteral("fillColor")).toString() !=
            QStringLiteral("#d14f70") ||
        item_by_id(controller, b_new_part_id).isEmpty()) {
      return fail("component update should redo atomically");
    }
    controller.selectItem(b_title_id);
    const auto attached_payload = QVariantMap{
        {QStringLiteral("title"), item_by_id(controller, b_title_id).value(QStringLiteral("text"))},
        {QStringLiteral("titleX"), item_by_id(controller, b_title_id).value(QStringLiteral("x"))},
        {QStringLiteral("shapeColor"), item_by_id(controller, b_shape_id).value(QStringLiteral("fillColor"))},
        {QStringLiteral("newPart"), item_by_id(controller, b_new_part_id).value(QStringLiteral("text"))},
    };
    if (!controller.detachSelectedComponentInstance()) {
      return fail("an updated component instance should detach explicitly");
    }
    if (!item_by_id(controller, b_title_id).value(QStringLiteral("componentDefinitionId")).toString().isEmpty() ||
        !item_by_id(controller, b_shape_id).value(QStringLiteral("componentInstanceId")).toString().isEmpty() ||
        !item_by_id(controller, b_new_part_id).value(QStringLiteral("componentPartId")).toString().isEmpty() ||
        item_by_id(controller, b_title_id).value(QStringLiteral("text")) !=
            attached_payload.value(QStringLiteral("title")) ||
        item_by_id(controller, b_title_id).value(QStringLiteral("x")) !=
            attached_payload.value(QStringLiteral("titleX")) ||
        item_by_id(controller, b_shape_id).value(QStringLiteral("fillColor")) !=
            attached_payload.value(QStringLiteral("shapeColor")) ||
        item_by_id(controller, b_new_part_id).value(QStringLiteral("text")) !=
            attached_payload.value(QStringLiteral("newPart"))) {
      return fail("detaching should retain every resolved layer value and geometry while clearing component identity");
    }
    controller.undo();
    if (item_by_id(controller, b_title_id).value(QStringLiteral("componentInstanceId")).toString() !=
        b_instance_id) {
      return fail("component detachment should be undoable");
    }
    controller.redo();
    if (!item_by_id(controller, b_title_id).value(QStringLiteral("componentInstanceId")).toString().isEmpty()) {
      return fail("component detachment should be redoable");
    }
    if (!controller.saveDocument(QStringLiteral("Component Lifecycle Proof"))) {
      return fail("updated and detached component states should save");
    }
    CarouselDocumentController reopened;
    if (!reopened.loadDocument(controller.savedDocumentId()) ||
        item_by_id(reopened, b_title_id).value(QStringLiteral("text")) !=
            attached_payload.value(QStringLiteral("title")) ||
        !item_by_id(reopened, b_title_id).value(QStringLiteral("componentDefinitionId")).toString().isEmpty() ||
        item_by_id(reopened, a_title_id).value(QStringLiteral("componentDefinitionVersion")).toInt() != 1) {
      return fail("detached values and untouched older instances should survive save/reopen");
    }
    reopened.setExportManifestEnabled(false);
    const auto lifecycle_export_dir = dir.filePath(QStringLiteral("component-lifecycle-export"));
    QDir().mkpath(lifecycle_export_dir);
    if (!reopened.exportCarouselToDirectory(lifecycle_export_dir) ||
        QImage(QDir(lifecycle_export_dir).filePath(QStringLiteral("carousel-02.png"))).isNull()) {
      return fail("updated and detached component results should remain exportable");
    }
    reopened.deleteUserTemplate(template_id);
  }

  {
    CarouselDocumentController controller;
    controller.createCarouselWithSettings(QVariantMap{
        {QStringLiteral("carouselName"), QStringLiteral("Automatic Text Fit Proof")},
        {QStringLiteral("documentFormat"), QStringLiteral("1x1")},
        {QStringLiteral("slideCount"), 1},
    });
    controller.addTextItem(QStringLiteral("Initial text"));
    controller.setSelectedTextFitSettings(QStringLiteral("fixed"), 18.0, 72.0);
    controller.setSelectedTextFrameSize(300.0, 160.0);
    controller.setSelectedText(
        QStringLiteral("Automatic fitting keeps this deliberately long headline inside its compact frame"));
    auto fixed = controller.selectionDetails();
    if (fixed.value(QStringLiteral("textFitMode")).toString() != QStringLiteral("fixed") ||
        !fixed.value(QStringLiteral("textVerticalOverflow")).toBool() ||
        !fixed.value(QStringLiteral("textFitState")).toString().contains(
            QStringLiteral("preflight"))) {
      return fail("fixed text sizing should preserve the requested size and expose an overflow warning state");
    }
    const auto fixed_width = fixed.value(QStringLiteral("width")).toDouble();
    const auto fixed_height = fixed.value(QStringLiteral("height")).toDouble();
    controller.setSelectedTextFitSettings(QStringLiteral("auto"), 18.0, 72.0);
    auto fitted = controller.selectionDetails();
    const auto fitted_size = fitted.value(QStringLiteral("textPointSize")).toDouble();
    if (fitted.value(QStringLiteral("textFitMode")).toString() != QStringLiteral("auto") ||
        fitted_size < 18.0 || fitted_size >= 72.0 ||
        fitted.value(QStringLiteral("textVerticalOverflow")).toBool() ||
        qAbs(fitted.value(QStringLiteral("width")).toDouble() - fixed_width) > 0.01 ||
        qAbs(fitted.value(QStringLiteral("height")).toDouble() - fixed_height) > 0.01 ||
        !fitted.value(QStringLiteral("textFitState")).toString().startsWith(
            QStringLiteral("Fitted from 72 pt"))) {
      return fail("automatic text fitting should shrink within configured limits without changing the frame");
    }
    controller.setSelectedTextLetterSpacing(2.0);
    fitted = controller.selectionDetails();
    if (fitted.value(QStringLiteral("textVerticalOverflow")).toBool() ||
        fitted.value(QStringLiteral("textPointSize")).toDouble() > fitted_size + 0.1) {
      return fail("automatic text fitting should re-resolve after a typography measurement change");
    }
    controller.setSelectedItemSize(340.0, 180.0);
    fitted = controller.selectionDetails();
    if (qAbs(fitted.value(QStringLiteral("width")).toDouble() - 340.0) > 0.01 ||
        qAbs(fitted.value(QStringLiteral("height")).toDouble() - 180.0) > 0.01 ||
        fitted.value(QStringLiteral("textVerticalOverflow")).toBool()) {
      return fail("resizing an auto-fit text frame should preserve the requested frame and recompute its text size");
    }
    controller.setSelectedText(QStringLiteral("Short"));
    fitted = controller.selectionDetails();
    if (qAbs(fitted.value(QStringLiteral("textPointSize")).toDouble() - 72.0) > 0.1 ||
        !fitted.value(QStringLiteral("textFitState")).toString().contains(
            QStringLiteral("maximum"))) {
      return fail("automatic text fitting should grow short copy back to the configured maximum");
    }
    controller.setSelectedText(
        QStringLiteral("Automatic fitting remains deterministic after saving and reopening this longer headline"));
    if (!controller.saveDocument()) {
      return fail("automatic text fitting settings should save with the document");
    }
    const auto before_reopen = controller.selectionDetails();
    CarouselDocumentController restored;
    if (!restored.loadDocument(controller.savedDocumentId())) {
      return fail("automatic text fitting proof should reopen its saved document");
    }
    const auto after_reopen = restored.selectionDetails();
    if (after_reopen.value(QStringLiteral("textFitMode")).toString() != QStringLiteral("auto") ||
        qAbs(after_reopen.value(QStringLiteral("textFitMinPointSize")).toDouble() - 18.0) > 0.01 ||
        qAbs(after_reopen.value(QStringLiteral("textFitMaxPointSize")).toDouble() - 72.0) > 0.01 ||
        qAbs(after_reopen.value(QStringLiteral("textPointSize")).toDouble() -
             before_reopen.value(QStringLiteral("textPointSize")).toDouble()) > 0.01 ||
        after_reopen.value(QStringLiteral("textVerticalOverflow")).toBool()) {
      return fail("automatic text fitting limits and resolved size should survive save and reopen");
    }
  }

  {
    CarouselDocumentController controller;
    controller.assignPrimaryImage(
        dir.filePath(QStringLiteral("not-an-image.txt")),
        QStringLiteral("Invalid source"));
    auto notices = controller.statusNotices();
    if (notices.size() != 1 ||
        notices.constFirst().toMap().value(QStringLiteral("category")).toString() !=
            QStringLiteral("media") ||
        notices.constFirst().toMap().value(QStringLiteral("actionKey")).toString() !=
            QStringLiteral("relink-media") ||
        notices.constFirst().toMap().value(QStringLiteral("message")).toString().isEmpty()) {
      return fail("media failures should remain in actionable Carousel status history");
    }
    controller.setCurrentSlideName(QStringLiteral("Later successful operation"));
    if (controller.statusNotices().size() != 1) {
      return fail("later status messages should not overwrite retained Carousel failures");
    }
    const auto notice_id = notices.constFirst().toMap().value(QStringLiteral("noticeId")).toInt();
    controller.dismissStatusNotice(notice_id);
    if (!controller.statusNotices().isEmpty()) {
      return fail("individual retained failures should dismiss explicitly");
    }
  }

  {
    CarouselDocumentController brand_author;
    brand_author.setCurrentSlideBackgroundColor(QStringLiteral("#ffffff"));
    brand_author.addTextItemWithRole(QStringLiteral("title"), QStringLiteral("Brand preview title"));
    brand_author.setSelectedTextColor(QStringLiteral("#777777"));
    brand_author.setSelectedTextFontFamily(QStringLiteral("serif"));
    QImage lint_logo(32, 32, QImage::Format_ARGB32);
    lint_logo.fill(QColor(QStringLiteral("#777777")));
    const auto lint_logo_path = write_image(dir, QStringLiteral("lint-brand-logo.png"), lint_logo);
    brand_author.addPhotoItem(lint_logo_path, QStringLiteral("Required Brand Logo"));
    if (!brand_author.saveCurrentBoardAsBrandKit(QStringLiteral("Diff Brand"))) {
      return fail("brand application diff fixture should save");
    }
    const auto diff_brand_id = brand_author.brandKits().constFirst().toMap().value(QStringLiteral("brandId")).toString();
    CarouselDocumentController target;
    target.setCurrentSlideBackgroundColor(QStringLiteral("#000000"));
    target.addTextItemWithRole(QStringLiteral("title"), QStringLiteral("Local title"));
    target.setSelectedTextColor(QStringLiteral("#ffffff"));
    target.setSelectedTextFontFamily(QStringLiteral("system"));
    if (!target.previewBrandKitApplication(diff_brand_id, QStringLiteral("current"), QStringLiteral("retain"))) {
      return fail("brand application should produce a diff before mutation");
    }
    auto diff = target.brandApplicationPreview();
    auto layer_diff = diff.value(QStringLiteral("affectedLayers")).toList().constFirst().toMap();
    if (diff.value(QStringLiteral("boardCount")).toInt() != 1 ||
        diff.value(QStringLiteral("layerCount")).toInt() != 1 ||
        diff.value(QStringLiteral("localOverrideCount")).toInt() != 1 ||
        layer_diff.value(QStringLiteral("overrideAction")).toString() != QStringLiteral("retain") ||
        layer_diff.value(QStringLiteral("result")).toMap().value(QStringLiteral("color")).toString() !=
            QStringLiteral("#ffffff") ||
        diff.value(QStringLiteral("contrastRegressions")).toList().isEmpty()) {
      return fail("brand diff should list exact board/layer changes, retained local values, and contrast regressions");
    }
    if (!target.previewBrandKitApplication(diff_brand_id, QStringLiteral("current"), QStringLiteral("replace"))) {
      return fail("brand application diff should switch override policy without mutation");
    }
    diff = target.brandApplicationPreview();
    layer_diff = diff.value(QStringLiteral("affectedLayers")).toList().constFirst().toMap();
    if (layer_diff.value(QStringLiteral("overrideAction")).toString() != QStringLiteral("replace") ||
        layer_diff.value(QStringLiteral("target")).toMap().value(QStringLiteral("fontFamily")).toString() !=
            QStringLiteral("serif") ||
        !target.applyPreviewedBrandKit() || !target.brandApplicationPreview().isEmpty() ||
        target.selectionDetails().value(QStringLiteral("textColor")).toString() != QStringLiteral("#777777") ||
        target.slides().constFirst().toMap().value(QStringLiteral("backgroundColor")).toString() !=
            QStringLiteral("#ffffff")) {
      return fail("confirmed replace should apply the exact previewed brand values and clear the pending diff");
    }
    target.undo();
    if (target.selectionDetails().value(QStringLiteral("textColor")).toString() != QStringLiteral("#ffffff") ||
        target.slides().constFirst().toMap().value(QStringLiteral("backgroundColor")).toString() !=
            QStringLiteral("#000000")) {
      return fail("previewed brand application should undo as one edit");
    }
    target.previewBrandKitApplication(diff_brand_id, QStringLiteral("current"), QStringLiteral("retain"));
    if (!target.applyPreviewedBrandKit() ||
        target.selectionDetails().value(QStringLiteral("textColor")).toString() != QStringLiteral("#ffffff") ||
        target.slides().constFirst().toMap().value(QStringLiteral("backgroundColor")).toString() !=
            QStringLiteral("#ffffff")) {
      return fail("retain policy should restyle boards while preserving local layer overrides");
    }
    target.addShapeItem(QStringLiteral("rectangle"));
    target.setSelectedShapeFillColor(QStringLiteral("#ff00ff"));
    const auto lint_shape_id = target.selectedItemId();
    target.addTextItemWithRole(QStringLiteral("heading"), QStringLiteral("First heading"));
    target.setSelectedTextFontFamily(QStringLiteral("serif"));
    target.setSelectedTextColor(QStringLiteral("#777777"));
    target.setSelectedTextWeight(QStringLiteral("bold"));
    target.addTextItemWithRole(QStringLiteral("heading"), QStringLiteral("Second heading"));
    target.setSelectedTextFontFamily(QStringLiteral("serif"));
    target.setSelectedTextColor(QStringLiteral("#777777"));
    target.setSelectedTextWeight(QStringLiteral("regular"));
    target.setActiveBrandKitForLint(diff_brand_id);
    auto lint_state = target.brandLintState();
    const auto lint_issues = lint_state.value(QStringLiteral("issues")).toList();
    const auto font_issue = first_preflight_issue(lint_issues, QStringLiteral("unapproved-font"));
    const auto color_issue = first_preflight_issue(lint_issues, QStringLiteral("unapproved-color"), lint_shape_id);
    const auto logo_issue = first_preflight_issue(lint_issues, QStringLiteral("missing-logo"));
    const auto style_issue = first_preflight_issue(lint_issues, QStringLiteral("inconsistent-role-style"));
    const auto contrast_issue = first_preflight_issue(lint_issues, QStringLiteral("minimum-contrast"));
    if (!lint_state.value(QStringLiteral("available")).toBool() || font_issue.isEmpty() ||
        color_issue.isEmpty() || logo_issue.isEmpty() || style_issue.isEmpty() || contrast_issue.isEmpty()) {
      return fail("brand lint should report unapproved fonts/colors, a missing required logo, inconsistent heading styles, and minimum contrast");
    }
    target.focusBrandLintIssue(color_issue.value(QStringLiteral("issueId")).toString());
    if (target.selectedItemId() != lint_shape_id) {
      return fail("brand lint issue navigation should select the exact offending layer");
    }
    const auto ignored_issue_id = style_issue.value(QStringLiteral("issueId")).toString();
    const auto issue_count_before_ignore = lint_state.value(QStringLiteral("issueCount")).toInt();
    target.ignoreBrandLintIssue(ignored_issue_id);
    lint_state = target.brandLintState();
    if (lint_state.value(QStringLiteral("issueCount")).toInt() != issue_count_before_ignore - 1 ||
        lint_state.value(QStringLiteral("ignoredCount")).toInt() != 1 ||
        !first_preflight_issue(lint_state.value(QStringLiteral("issues")).toList(),
                               QStringLiteral("inconsistent-role-style")).isEmpty()) {
      return fail("brand lint ignore should explicitly remove only the chosen stable issue");
    }
    if (!target.saveDocument(QStringLiteral("Brand Lint Persistence"))) {
      return fail("brand lint persistence fixture should save");
    }
    const auto lint_document_id = target.savedDocumentId();
    CarouselDocumentController restored_lint;
    if (!restored_lint.loadDocument(lint_document_id) ||
        restored_lint.brandLintState().value(QStringLiteral("brandId")).toString() != diff_brand_id ||
        restored_lint.brandLintState().value(QStringLiteral("ignoredCount")).toInt() != 1 ||
        !first_preflight_issue(restored_lint.brandLintState().value(QStringLiteral("issues")).toList(),
                               QStringLiteral("inconsistent-role-style")).isEmpty()) {
      return fail("active brand lint and ignored issue identities should survive save and reopen");
    }
    brand_author.deleteBrandKit(diff_brand_id);
  }

  {
    QImage primary_logo(QSize(128, 128), QImage::Format_ARGB32);
    primary_logo.fill(QColor(QStringLiteral("#2563eb")));
    QImage light_logo(QSize(128, 128), QImage::Format_ARGB32);
    light_logo.fill(QColor(QStringLiteral("#ffffff")));
    QImage dark_logo(QSize(128, 128), QImage::Format_ARGB32);
    dark_logo.fill(QColor(QStringLiteral("#101418")));
    QImage monochrome_logo(QSize(128, 128), QImage::Format_ARGB32);
    monochrome_logo.fill(QColor(QStringLiteral("#777777")));
    const auto primary_logo_path =
        write_image(dir, QStringLiteral("logo-rules-primary.png"), primary_logo);
    const auto light_logo_path =
        write_image(dir, QStringLiteral("logo-rules-light.png"), light_logo);
    const auto dark_logo_path =
        write_image(dir, QStringLiteral("logo-rules-dark.png"), dark_logo);
    const auto monochrome_logo_path =
        write_image(dir, QStringLiteral("logo-rules-monochrome.png"), monochrome_logo);
    if (primary_logo_path.isEmpty() || light_logo_path.isEmpty() ||
        dark_logo_path.isEmpty() || monochrome_logo_path.isEmpty()) {
      return fail("logo usage rule fixtures should save");
    }

    CarouselDocumentController controller;
    controller.createCarouselWithSettings(QVariantMap{
        {QStringLiteral("carouselName"), QStringLiteral("Logo Usage Rules Proof")},
        {QStringLiteral("documentFormat"), QStringLiteral("4x5")},
        {QStringLiteral("slideCount"), 2},
        {QStringLiteral("backgroundColor"), QStringLiteral("#ffffff")},
    });
    controller.beginBrandKitEditor(QString{});
    controller.setBrandKitDraftField(QStringLiteral("name"), QStringLiteral("Logo Rules Brand"));
    controller.setBrandKitDraftField(QStringLiteral("logoName"), QStringLiteral("Primary Mark"));
    controller.setBrandKitDraftField(QStringLiteral("logoPath"), primary_logo_path);
    controller.setBrandKitDraftField(QStringLiteral("logoRequired"), false);
    controller.setBrandKitDraftField(QStringLiteral("logoLightPath"), light_logo_path);
    controller.setBrandKitDraftField(QStringLiteral("logoDarkPath"), dark_logo_path);
    controller.setBrandKitDraftField(
        QStringLiteral("logoMonochromePath"), monochrome_logo_path);
    controller.setBrandKitDraftField(QStringLiteral("logoSafeAreaPercent"), 12.0);
    controller.setBrandKitDraftField(QStringLiteral("logoMinimumWidth"), 120.0);
    controller.setBrandKitDraftField(QStringLiteral("logoMinimumHeight"), 50.0);
    controller.setBrandKitDraftField(QStringLiteral("logoMinimumContrast"), 3.0);
    if (!controller.saveBrandKitDraft() || controller.brandKits().isEmpty()) {
      return fail("brand kits should save primary, light, dark, and monochrome logo assets with usage rules");
    }
    const auto logo_brand = controller.brandKits().constLast().toMap();
    const auto logo_brand_id = logo_brand.value(QStringLiteral("brandId")).toString();
    const auto logo_rules = logo_brand.value(QStringLiteral("logoRules")).toMap();
    const auto logo_assets = logo_brand.value(QStringLiteral("logoAssets")).toList();
    QSet<QString> logo_variants;
    for (const auto& value : logo_assets) {
      logo_variants.insert(value.toMap().value(QStringLiteral("variant")).toString());
    }
    if (logo_variants != QSet<QString>{
            QStringLiteral("primary"), QStringLiteral("light"),
            QStringLiteral("dark"), QStringLiteral("monochrome")} ||
        logo_rules.value(QStringLiteral("safeAreaPercent")).toDouble() != 12.0 ||
        logo_rules.value(QStringLiteral("minimumWidthPixels")).toDouble() != 120.0 ||
        logo_rules.value(QStringLiteral("minimumHeightPixels")).toDouble() != 50.0 ||
        logo_rules.value(QStringLiteral("minimumContrast")).toDouble() != 3.0) {
      return fail("logo variant identities and size, clear-space, and contrast rules should persist exactly");
    }
    controller.beginBrandKitEditor(logo_brand_id);
    controller.setBrandKitDraftField(QStringLiteral("logoMonochromePath"), QString{});
    bool warned_for_missing_monochrome = false;
    for (const auto& warning : controller.brandKitValidation().value(
             QStringLiteral("warnings")).toList()) {
      warned_for_missing_monochrome = warned_for_missing_monochrome ||
                                       warning.toString().contains(
                                           QStringLiteral("monochrome"),
                                           Qt::CaseInsensitive);
    }
    controller.cancelBrandKitEditor();
    if (!warned_for_missing_monochrome) {
      return fail("brand-kit validation should clearly warn when a logo variant is missing without blocking save");
    }

    controller.selectSlide(0);
    controller.addPhotoItem(light_logo_path, QStringLiteral("Light Logo Wrong Surface"));
    const auto wrong_logo_id = controller.selectedItemId();
    controller.setSelectedItemSize(40.0, 40.0);
    controller.setSelectedItemCenter(20.0, 20.0);
    controller.selectSlide(1);
    controller.setCurrentSlideBackgroundColor(QStringLiteral("#101418"));
    controller.addPhotoItem(light_logo_path, QStringLiteral("Light Logo Correct Surface"));
    const auto correct_logo_id = controller.selectedItemId();
    controller.setSelectedItemSize(180.0, 180.0);
    controller.setSelectedItemCenter(
        static_cast<double>(controller.documentPixelWidth()) / controller.slideCount() + 540.0,
        675.0);
    const auto stable_logo_state = [](const QVariantMap& item) {
      return QVariantMap{
          {QStringLiteral("sourceUrl"), item.value(QStringLiteral("sourceUrl"))},
          {QStringLiteral("x"), item.value(QStringLiteral("x"))},
          {QStringLiteral("y"), item.value(QStringLiteral("y"))},
          {QStringLiteral("width"), item.value(QStringLiteral("width"))},
          {QStringLiteral("height"), item.value(QStringLiteral("height"))},
          {QStringLiteral("opacity"), item.value(QStringLiteral("opacity"))},
      };
    };
    const auto item_by_id = [](const CarouselDocumentController& document,
                               const QString& item_id) {
      for (const auto& value : document.canvasItems()) {
        const auto item = value.toMap();
        if (item.value(QStringLiteral("itemId")).toString() == item_id) {
          return item;
        }
      }
      return QVariantMap{};
    };
    const auto wrong_before_lint = stable_logo_state(item_by_id(controller, wrong_logo_id));
    const auto correct_before_lint = stable_logo_state(item_by_id(controller, correct_logo_id));
    controller.setActiveBrandKitForLint(logo_brand_id);
    auto lint_state = controller.brandLintState();
    const auto issues = lint_state.value(QStringLiteral("issues")).toList();
    const auto minimum_size_issue = first_preflight_issue(
        issues, QStringLiteral("logo-minimum-size"), wrong_logo_id);
    const auto safe_area_issue = first_preflight_issue(
        issues, QStringLiteral("logo-safe-area"), wrong_logo_id);
    const auto variant_issue = first_preflight_issue(
        issues, QStringLiteral("logo-variant-background"), wrong_logo_id);
    const auto logo_contrast_issue = first_preflight_issue(
        issues, QStringLiteral("logo-background-contrast"), wrong_logo_id);
    bool correct_logo_has_warning = false;
    for (const auto& value : issues) {
      const auto issue = value.toMap();
      correct_logo_has_warning = correct_logo_has_warning ||
                                 (issue.value(QStringLiteral("itemId")).toString() == correct_logo_id &&
                                  issue.value(QStringLiteral("code")).toString().startsWith(
                                      QStringLiteral("logo-")));
    }
    if (minimum_size_issue.isEmpty() || safe_area_issue.isEmpty() ||
        variant_issue.isEmpty() || logo_contrast_issue.isEmpty() ||
        correct_logo_has_warning) {
      return fail("logo lint should warn for minimum size, clear space, wrong surface variant, and low contrast while accepting a correct placement");
    }
    if (stable_logo_state(item_by_id(controller, wrong_logo_id)) != wrong_before_lint ||
        stable_logo_state(item_by_id(controller, correct_logo_id)) != correct_before_lint) {
      return fail("logo usage checks must never resize, move, recolor, swap, or otherwise mutate artwork");
    }
    controller.focusBrandLintIssue(safe_area_issue.value(QStringLiteral("issueId")).toString());
    if (controller.selectedItemId() != wrong_logo_id) {
      return fail("logo usage warnings should jump to the exact offending placement");
    }
    controller.ignoreBrandLintIssue(variant_issue.value(QStringLiteral("issueId")).toString());
    lint_state = controller.brandLintState();
    if (!first_preflight_issue(
             lint_state.value(QStringLiteral("issues")).toList(),
             QStringLiteral("logo-variant-background"), wrong_logo_id).isEmpty()) {
      return fail("logo usage warnings should support an explicit persistent exception");
    }
    if (!controller.saveDocument(QStringLiteral("Logo Usage Rules Proof"))) {
      return fail("logo usage rules and explicit exceptions should save");
    }
    CarouselDocumentController reopened;
    if (!reopened.loadDocument(controller.savedDocumentId()) ||
        reopened.brandLintState().value(QStringLiteral("brandId")).toString() != logo_brand_id ||
        !first_preflight_issue(
             reopened.brandLintState().value(QStringLiteral("issues")).toList(),
             QStringLiteral("logo-variant-background"), wrong_logo_id).isEmpty() ||
        stable_logo_state(item_by_id(reopened, wrong_logo_id)) != wrong_before_lint) {
      return fail("logo rule warnings, ignored exceptions, and unchanged placements should survive save/reopen");
    }
    reopened.deleteBrandKit(logo_brand_id);
  }

  {
    CarouselDocumentController controller;
    controller.createCarouselWithSettings(QVariantMap{
        {QStringLiteral("carouselName"), QStringLiteral("Design Package Proof")},
        {QStringLiteral("documentFormat"), QStringLiteral("4x5")},
        {QStringLiteral("slideCount"), 2},
    });
    QImage private_media(QSize(96, 72), QImage::Format_ARGB32);
    private_media.fill(QColor(QStringLiteral("#1ca67a")));
    const auto private_media_path =
        write_image(dir, QStringLiteral("private-project-media.png"), private_media);
    QImage package_logo(QSize(72, 72), QImage::Format_ARGB32);
    package_logo.fill(QColor(QStringLiteral("#f05d4f")));
    const auto package_logo_path =
        write_image(dir, QStringLiteral("package-brand-logo.png"), package_logo);
    if (private_media_path.isEmpty() || package_logo_path.isEmpty()) {
      return fail("design package media fixtures should save");
    }
    controller.selectSlide(0);
    controller.addPhotoItem(private_media_path, QStringLiteral("Private Project Photo"));
    const auto private_item_id = controller.selectedItemId();
    controller.addTextItemWithRole(
        QStringLiteral("heading"), QStringLiteral("PORTABLE COPY"));
    const auto package_text_id = controller.selectedItemId();
    controller.selectItem(private_item_id);
    controller.addItemToSelection(package_text_id);
    if (!controller.saveSelectionAsTemplate(
            QStringLiteral("Portable Component"), QStringLiteral("Components"))) {
      return fail("design package proof should save a component with private project media");
    }
    QString component_id;
    for (const auto& value : controller.userTemplates()) {
      const auto entry = value.toMap();
      if (entry.value(QStringLiteral("kind")).toString() == QStringLiteral("component") &&
          entry.value(QStringLiteral("name")).toString() == QStringLiteral("Portable Component")) {
        component_id = entry.value(QStringLiteral("templateId")).toString();
        break;
      }
    }
    const auto component_package_path =
        dir.filePath(QStringLiteral("portable-component.stillfold-design.json"));
    if (component_id.isEmpty() ||
        !controller.exportDesignPackage(
            QStringLiteral("component"), component_id, component_package_path)) {
      return fail("custom components should export as versioned design packages");
    }
    QFile component_package_file(component_package_path);
    if (!component_package_file.open(QIODevice::ReadOnly)) {
      return fail("component design package should be readable");
    }
    const auto component_package_bytes = component_package_file.readAll();
    const auto component_package =
        QJsonDocument::fromJson(component_package_bytes).object();
    const auto component_manifest = component_package.value(
        QStringLiteral("assetManifest")).toArray();
    if (component_package.value(QStringLiteral("packageVersion")).toInt() != 1 ||
        component_package.value(QStringLiteral("artifactType")).toString() !=
            QStringLiteral("component") ||
        component_package.value(QStringLiteral("artifactSha256")).toString().size() != 64 ||
        component_package.value(QStringLiteral("packageSha256")).toString().size() != 64 ||
        component_manifest.size() != 1 ||
        component_manifest.at(0).toObject().value(
            QStringLiteral("assetClass")).toString() != QStringLiteral("project-media") ||
        component_manifest.at(0).toObject().value(
            QStringLiteral("embedded")).toBool() ||
        component_manifest.at(0).toObject().value(
            QStringLiteral("exclusionReason")).toString() !=
            QStringLiteral("private-project-media") ||
        !component_package.value(QStringLiteral("embeddedAssets")).toObject().isEmpty() ||
        component_package_bytes.contains(QFile::encodeName(private_media_path))) {
      return fail("component packages should checksum and manifest private media without embedding bytes or absolute paths");
    }
    QFile private_media_file(private_media_path);
    if (!private_media_file.open(QIODevice::ReadOnly) ||
        component_package_bytes.contains(private_media_file.readAll().toBase64())) {
      return fail("component packages must never contain base64 private project media");
    }
    const auto component_count_before_preview = controller.userTemplates().size();
    if (!controller.previewDesignPackageImport(component_package_path)) {
      return fail("valid component packages should pass checksum validation before import");
    }
    auto package_preview = controller.designPackageImportPreview();
    if (!package_preview.value(QStringLiteral("checksumValid")).toBool() ||
        !package_preview.value(QStringLiteral("hasConflict")).toBool() ||
        !package_preview.value(QStringLiteral("idConflict")).toBool() ||
        !package_preview.value(QStringLiteral("nameConflict")).toBool() ||
        package_preview.value(QStringLiteral("excludedPrivateMediaCount")).toInt() != 1 ||
        controller.userTemplates().size() != component_count_before_preview) {
      return fail("component import preview should report checksums, ID/name conflicts, and excluded media without mutation");
    }
    if (!controller.applyDesignPackageImport(QStringLiteral("keep-both")) ||
        controller.userTemplates().size() != component_count_before_preview + 1) {
      return fail("Keep Both should import a conflicting component under a fresh identity");
    }
    QString imported_component_id;
    for (const auto& value : controller.userTemplates()) {
      const auto entry = value.toMap();
      if (entry.value(QStringLiteral("name")).toString() ==
          QStringLiteral("Portable Component Imported")) {
        imported_component_id = entry.value(QStringLiteral("templateId")).toString();
        break;
      }
    }
    controller.selectSlide(1);
    const auto before_imported_component = controller.canvasItems().size();
    controller.applyUserTemplate(imported_component_id, false);
    bool imported_copy_found = false;
    bool imported_media_unresolved = false;
    for (qsizetype index = before_imported_component;
         index < controller.canvasItems().size(); ++index) {
      const auto item = controller.canvasItems().at(index).toMap();
      imported_copy_found = imported_copy_found ||
                            item.value(QStringLiteral("text")).toString() ==
                                QStringLiteral("PORTABLE COPY");
      imported_media_unresolved = imported_media_unresolved ||
                                  (item.value(QStringLiteral("type")).toString() ==
                                       QStringLiteral("image") &&
                                   item.value(QStringLiteral("sourceUrl")).toString().isEmpty());
    }
    if (imported_component_id.isEmpty() || !imported_copy_found ||
        !imported_media_unresolved || controller.missingMediaCount() < 1) {
      return fail("imported components should retain editable copy and surface excluded project media as unresolved");
    }
    const auto count_before_replace = controller.userTemplates().size();
    if (!controller.previewDesignPackageImport(component_package_path) ||
        !controller.applyDesignPackageImport(QStringLiteral("replace")) ||
        controller.userTemplates().size() != count_before_replace) {
      return fail("Replace should explicitly update one conflicting component without creating another copy");
    }

    auto tampered_package = component_package;
    auto tampered_artifact = tampered_package.value(QStringLiteral("artifact")).toObject();
    tampered_artifact.insert(QStringLiteral("name"), QStringLiteral("Tampered Package"));
    tampered_package.insert(QStringLiteral("artifact"), tampered_artifact);
    const auto tampered_path = dir.filePath(QStringLiteral("tampered-design-package.json"));
    if (!write_json(tampered_path, tampered_package) ||
        controller.previewDesignPackageImport(tampered_path) ||
        controller.designPackageImportPreview().value(
            QStringLiteral("checksumValid")).toBool()) {
      return fail("artifact or package checksum tampering should block import before mutation");
    }
    controller.clearDesignPackageImportPreview();

    controller.beginBrandKitEditor(QString{});
    controller.setBrandKitDraftField(QStringLiteral("name"), QStringLiteral("Portable Brand"));
    controller.setBrandKitDraftField(QStringLiteral("logoPath"), package_logo_path);
    controller.setBrandKitDraftField(QStringLiteral("logoRequired"), true);
    if (!controller.saveBrandKitDraft()) {
      return fail("design package proof should save a brand kit with an explicit brand asset");
    }
    QString portable_brand_id;
    for (const auto& value : controller.brandKits()) {
      const auto kit = value.toMap();
      if (kit.value(QStringLiteral("name")).toString() == QStringLiteral("Portable Brand")) {
        portable_brand_id = kit.value(QStringLiteral("brandId")).toString();
        break;
      }
    }
    const auto brand_package_path =
        dir.filePath(QStringLiteral("portable-brand.stillfold-design.json"));
    if (portable_brand_id.isEmpty() ||
        !controller.exportDesignPackage(
            QStringLiteral("brand-kit"), portable_brand_id, brand_package_path)) {
      return fail("brand kits should export as versioned design packages");
    }
    QFile brand_package_file(brand_package_path);
    if (!brand_package_file.open(QIODevice::ReadOnly)) {
      return fail("brand design package should be readable");
    }
    const auto brand_package_bytes = brand_package_file.readAll();
    const auto brand_package = QJsonDocument::fromJson(brand_package_bytes).object();
    const auto brand_manifest = brand_package.value(QStringLiteral("assetManifest")).toArray();
    if (brand_manifest.size() != 1 ||
        !brand_manifest.at(0).toObject().value(
            QStringLiteral("embedded")).toBool() ||
        brand_manifest.at(0).toObject().value(
            QStringLiteral("assetClass")).toString() != QStringLiteral("brand-logo") ||
        brand_package.value(QStringLiteral("embeddedAssets")).toObject().size() != 1 ||
        brand_package_bytes.contains(QFile::encodeName(package_logo_path))) {
      return fail("brand packages should embed checksummed declared brand assets without leaking absolute paths");
    }
    const auto brand_count_before_preview = controller.brandKits().size();
    if (!controller.previewDesignPackageImport(brand_package_path) ||
        controller.brandKits().size() != brand_count_before_preview ||
        controller.designPackageImportPreview().value(
            QStringLiteral("embeddedAssetCount")).toInt() != 1 ||
        !controller.applyDesignPackageImport(QStringLiteral("keep-both")) ||
        controller.brandKits().size() != brand_count_before_preview + 1) {
      return fail("brand package conflict review should remain non-mutating and Keep Both should import checked assets");
    }
    QString imported_brand_id;
    QString imported_logo_path;
    for (const auto& value : controller.brandKits()) {
      const auto kit = value.toMap();
      if (kit.value(QStringLiteral("name")).toString() !=
          QStringLiteral("Portable Brand Imported")) {
        continue;
      }
      imported_brand_id = kit.value(QStringLiteral("brandId")).toString();
      const auto logos = kit.value(QStringLiteral("logoAssets")).toList();
      if (!logos.isEmpty()) {
        imported_logo_path = logos.constFirst().toMap().value(
            QStringLiteral("filePath")).toString();
      }
      break;
    }
    QFile imported_logo_file(imported_logo_path);
    QString imported_logo_checksum;
    if (imported_logo_file.open(QIODevice::ReadOnly)) {
      imported_logo_checksum = QString::fromLatin1(
          QCryptographicHash::hash(
              imported_logo_file.readAll(), QCryptographicHash::Sha256).toHex());
    }
    if (imported_brand_id.isEmpty() || !QFileInfo::exists(imported_logo_path) ||
        imported_logo_checksum != brand_manifest.at(0).toObject().value(
                                      QStringLiteral("sha256")).toString()) {
      return fail("imported brand assets should be restored to a local design-assets path and remain readable");
    }
    controller.deleteUserTemplate(component_id);
    controller.deleteUserTemplate(imported_component_id);
    controller.deleteBrandKit(portable_brand_id);
    controller.deleteBrandKit(imported_brand_id);
  }

  {
    CarouselDocumentController contrast;
    contrast.setCurrentSlideBackgroundGradient(
        QStringLiteral("linear-horizontal"), QStringLiteral("#000000"), QStringLiteral("#ffffff"));
    contrast.addTextItemWithRole(QStringLiteral("body"), QStringLiteral("Live contrast"));
    contrast.setSelectedTextColor(QStringLiteral("#ffffff"));
    contrast.setSelectedTextPointSize(16.0);
    auto check = contrast.textContrastChecks().constFirst().toMap();
    if (check.value(QStringLiteral("backgroundType")).toString() != QStringLiteral("gradient") ||
        check.value(QStringLiteral("sampleCount")).toInt() != 9 ||
        check.value(QStringLiteral("threshold")).toDouble() != 4.5 ||
        check.value(QStringLiteral("minimumRatio")).toDouble() >= 4.5 ||
        check.value(QStringLiteral("maximumRatio")).toDouble() -
                check.value(QStringLiteral("minimumRatio")).toDouble() <= 0.5 ||
        check.value(QStringLiteral("status")).toString() != QStringLiteral("fail")) {
      return fail("live contrast should sample gradients and report the minimum and maximum WCAG-style ratios");
    }
    contrast.setSelectedTextBackgroundColor(QStringLiteral("#000000"));
    contrast.setSelectedTextBackgroundOpacity(1.0);
    check = contrast.textContrastChecks().constFirst().toMap();
    if (!check.value(QStringLiteral("passes")).toBool() ||
        check.value(QStringLiteral("backgroundTreatmentOpacity")).toDouble() != 1.0) {
      return fail("live contrast should composite opaque text background treatments before measurement");
    }
    contrast.setSelectedItemOpacity(0.2);
    contrast.setSelectedTextStrokeColor(QStringLiteral("#ffffff"));
    contrast.setSelectedTextStrokeWidth(3.0);
    check = contrast.textContrastChecks().constFirst().toMap();
    if (check.value(QStringLiteral("passes")).toBool() ||
        check.value(QStringLiteral("textOpacity")).toDouble() != 0.2 ||
        check.value(QStringLiteral("strokeWidth")).toDouble() != 3.0 ||
        check.value(QStringLiteral("minimumStrokeRatio")).toDouble() <= 1.0) {
      return fail("live contrast should account for layer opacity and expose measured stroke treatment contrast");
    }

    const auto contrast_photo_path = write_image(
        dir, QStringLiteral("contrast-region.png"),
        split_image(QSize(240, 160), QColor(QStringLiteral("#000000")), QColor(QStringLiteral("#ffffff"))));
    CarouselDocumentController photographic;
    photographic.assignPrimaryImage(contrast_photo_path, QStringLiteral("Contrast region"));
    photographic.addTextItemWithRole(QStringLiteral("title"), QStringLiteral("On photograph"));
    photographic.setSelectedTextColor(QStringLiteral("#ffffff"));
    const auto photo_check = photographic.textContrastChecks().constFirst().toMap();
    if (photo_check.value(QStringLiteral("backgroundType")).toString() != QStringLiteral("photographic-region") ||
        !photo_check.value(QStringLiteral("uncertain")).toBool() ||
        photo_check.value(QStringLiteral("status")).toString() != QStringLiteral("uncertain") ||
        photo_check.value(QStringLiteral("sampleCount")).toInt() != 9 ||
        photo_check.value(QStringLiteral("uncertaintyReasons")).toStringList().isEmpty()) {
      return fail("live contrast should sample image regions while labeling photographic results as uncertain");
    }
  }

  {
    CarouselDocumentController platform_preview;
    const auto profiles = platform_preview.platformPreviewProfiles();
    QVariantMap feed_grid;
    QVariantMap profile_header;
    QVariantMap reels_style;
    QVariantMap short_video_style;
    for (const auto& value : profiles) {
      const auto profile = value.toMap();
      const auto id = profile.value(QStringLiteral("profileId")).toString();
      if (id == QStringLiteral("feed-grid")) {
        feed_grid = profile;
      } else if (id == QStringLiteral("profile-header")) {
        profile_header = profile;
      } else if (id == QStringLiteral("reels-style")) {
        reels_style = profile;
      } else if (id == QStringLiteral("short-video-style")) {
        short_video_style = profile;
      }
    }
    if (profiles.size() != 4 || feed_grid.value(QStringLiteral("cropKind")).toString() !=
                                  QStringLiteral("center-square") ||
        !profile_header.value(QStringLiteral("showsProfileHeader")).toBool() ||
        profile_header.value(QStringLiteral("avatarObstructionRatio")).toDouble() <= 0.0 ||
        reels_style.value(QStringLiteral("captionBand")).toDouble() <= 0.0 ||
        reels_style.value(QStringLiteral("controlsRail")).toDouble() <= 0.0 ||
        reels_style.value(QStringLiteral("progressEdge")).toString() != QStringLiteral("top") ||
        short_video_style.value(QStringLiteral("progressEdge")).toString() != QStringLiteral("bottom") ||
        short_video_style.value(QStringLiteral("deviceCornerRatio")).toDouble() <= 0.0 ||
        !short_video_style.value(QStringLiteral("referenceOnly")).toBool() ||
        !short_video_style.value(QStringLiteral("disclaimer")).toString().contains(
            QStringLiteral("verify current platform requirements"), Qt::CaseInsensitive) ||
        !short_video_style.value(QStringLiteral("disclaimer")).toString().contains(
            QStringLiteral("never export"), Qt::CaseInsensitive)) {
      return fail("platform previews should define feed, header, caption/control, progress, and device-corner reference geometry with a currentness warning");
    }
  }

  {
    CarouselDocumentController issue_workflow;
    issue_workflow.addTextItemWithRole(QStringLiteral("body"), QStringLiteral("Small delivery note"));
    issue_workflow.setSelectedTextPointSize(10.0);
    issue_workflow.setSelectedTextFontFamily(QStringLiteral("Stillfold Preflight Missing Face"));
    const auto text_item_id = issue_workflow.selectedItemId();
    auto state = issue_workflow.preflightIssueState();
    const auto small_issue = first_preflight_issue(
        state.value(QStringLiteral("issues")).toList(), QStringLiteral("text-too-small"), text_item_id);
    const auto blocking_issue = first_preflight_issue(
        state.value(QStringLiteral("issues")).toList(), QStringLiteral("missing-font"), text_item_id);
    if (small_issue.isEmpty() || blocking_issue.isEmpty() ||
        state.value(QStringLiteral("boardCounts")).toList().constFirst().toMap().value(
            QStringLiteral("totalCount")).toInt() < 2) {
      return fail("preflight workflow should expose per-board counts for blocking and warning issues");
    }
    issue_workflow.setPreflightSeverityFilter(QStringLiteral("warning"));
    state = issue_workflow.preflightIssueState();
    for (const auto& value : state.value(QStringLiteral("issues")).toList()) {
      if (value.toMap().value(QStringLiteral("severity")).toString() != QStringLiteral("warning")) {
        return fail("preflight severity filter should return only the chosen issue class");
      }
    }
    issue_workflow.navigatePreflightIssue(1);
    state = issue_workflow.preflightIssueState();
    if (state.value(QStringLiteral("currentIssueId")).toString().isEmpty() ||
        issue_workflow.currentIndex() != 0) {
      return fail("preflight next navigation should focus a stable issue and its board");
    }
    if (issue_workflow.ignorePreflightIssue(
            small_issue.value(QStringLiteral("issueId")).toString(), QString{}) ||
        issue_workflow.ignorePreflightIssue(
            blocking_issue.value(QStringLiteral("issueId")).toString(), QStringLiteral("Cannot ship yet")) ||
        !issue_workflow.ignorePreflightIssue(
            small_issue.value(QStringLiteral("issueId")).toString(), QStringLiteral("Approved legal footnote"))) {
      return fail("preflight ignore should require a reason and refuse blocking errors");
    }
    state = issue_workflow.preflightIssueState();
    if (state.value(QStringLiteral("ignoredCount")).toInt() != 1 ||
        !first_preflight_issue(state.value(QStringLiteral("issues")).toList(),
                               QStringLiteral("text-too-small"), text_item_id).isEmpty()) {
      return fail("preflight ignore should remove only the reasoned warning from active results");
    }
    issue_workflow.setPreflightSeverityFilter(QStringLiteral("all"));
    issue_workflow.setSelectedTextColor(QStringLiteral("#fbf7f1"));
    if (first_preflight_issue(issue_workflow.preflightIssues(), QStringLiteral("low-contrast-text"), text_item_id).isEmpty()) {
      return fail("preflight should revalidate automatically after a contrast-relevant edit");
    }
    issue_workflow.setSelectedTextColor(QStringLiteral("#1e1a17"));
    if (!first_preflight_issue(issue_workflow.preflightIssues(), QStringLiteral("low-contrast-text"), text_item_id).isEmpty()) {
      return fail("preflight should clear resolved warnings immediately after the relevant edit");
    }
    if (!issue_workflow.saveDocument(QStringLiteral("Preflight Ignore Persistence"))) {
      return fail("preflight ignore persistence fixture should save");
    }
    CarouselDocumentController restored_issues;
    if (!restored_issues.loadDocument(issue_workflow.savedDocumentId()) ||
        restored_issues.preflightIssueState().value(QStringLiteral("ignoredCount")).toInt() != 1 ||
        !first_preflight_issue(restored_issues.preflightIssues(),
                               QStringLiteral("text-too-small"), text_item_id).isEmpty()) {
      return fail("reasoned preflight ignores should persist through save and reopen");
    }
  }

  {
    CarouselDocumentController controller;
    controller.createCarouselWithSettings(QVariantMap{
        {QStringLiteral("carouselName"), QStringLiteral("Board Metadata Carousel")},
        {QStringLiteral("documentFormat"), QStringLiteral("4x5")},
        {QStringLiteral("slideCount"), 1},
    });
    if (!controller.documentDirty() || controller.saveState() != QStringLiteral("Unsaved")) {
      return fail("new carousel documents should expose a truthful unsaved state");
    }
    controller.setCurrentSlideName(QStringLiteral("Opening argument"));
    controller.setCurrentSlideRole(QStringLiteral("hook"));
    controller.setCurrentSlideNotes(
        QStringLiteral("State the core idea without a preamble."),
        QStringLiteral("Use the strongest environmental portrait."),
        QStringLiteral("Caption draft 04; credit the location."));
    controller.setCurrentSlideAccessibilityText(
        QStringLiteral("A person standing in a sunlit industrial workspace."),
        QStringLiteral("The opening board states the campaign's central argument over an environmental portrait."));

    auto slides = controller.slides();
    auto board = slides.constFirst().toMap();
    if (board.value(QStringLiteral("boardName")).toString() != QStringLiteral("Opening argument") ||
        board.value(QStringLiteral("boardRole")).toString() != QStringLiteral("hook") ||
        board.value(QStringLiteral("messageNote")).toString() !=
            QStringLiteral("State the core idea without a preamble.") ||
        board.value(QStringLiteral("mediaIntentNote")).toString() !=
            QStringLiteral("Use the strongest environmental portrait.") ||
        board.value(QStringLiteral("sourceReferenceNote")).toString() !=
            QStringLiteral("Caption draft 04; credit the location.") ||
        board.value(QStringLiteral("altText")).toString() !=
            QStringLiteral("A person standing in a sunlit industrial workspace.") ||
        board.value(QStringLiteral("plainLanguageDescription")).toString().isEmpty()) {
      return fail("board roles, names, production notes, and accessibility metadata should be exposed by the document model");
    }

    controller.duplicateCurrentSlide();
    slides = controller.slides();
    if (slides.size() != 2 ||
        slides.at(1).toMap().value(QStringLiteral("boardName")).toString() != QStringLiteral("Opening argument") ||
        slides.at(1).toMap().value(QStringLiteral("boardRole")).toString() != QStringLiteral("hook") ||
        slides.at(1).toMap().value(QStringLiteral("mediaIntentNote")).toString() !=
            QStringLiteral("Use the strongest environmental portrait.") ||
        slides.at(1).toMap().value(QStringLiteral("altText")).toString().isEmpty()) {
      return fail("duplicating a board should preserve its semantic, production, and accessibility metadata");
    }

    controller.setCurrentSlideName(QStringLiteral("Final action"));
    controller.setCurrentSlideRole(QStringLiteral("cta"));
    controller.setCurrentSlideNotes(
        QStringLiteral("Give one direct action."),
        QStringLiteral("No new media required."),
        QStringLiteral("Use the approved campaign URL."));
    controller.setCurrentSlideAccessibilityText(
        QStringLiteral("A direct call to action with the campaign URL."),
        QStringLiteral("The closing board asks the viewer to visit the approved campaign page."));
    controller.moveSlide(1, 0);
    slides = controller.slides();
    if (slides.at(0).toMap().value(QStringLiteral("boardRole")).toString() != QStringLiteral("cta") ||
        slides.at(1).toMap().value(QStringLiteral("boardRole")).toString() != QStringLiteral("hook")) {
      return fail("reordering boards should move semantic metadata with the board");
    }
    controller.undo();
    slides = controller.slides();
    if (slides.at(0).toMap().value(QStringLiteral("boardRole")).toString() != QStringLiteral("hook") ||
        slides.at(1).toMap().value(QStringLiteral("boardRole")).toString() != QStringLiteral("cta")) {
      return fail("board metadata reordering should undo as one document edit");
    }

    controller.selectSlide(0);
    controller.selectSlideRange(1);
    controller.setSelectedSlidesRole(QStringLiteral("proof"));
    slides = controller.slides();
    if (slides.at(0).toMap().value(QStringLiteral("boardRole")).toString() != QStringLiteral("proof") ||
        slides.at(1).toMap().value(QStringLiteral("boardRole")).toString() != QStringLiteral("proof") ||
        controller.selectedSlideCount() != 2) {
      return fail("storyboard role assignment should update the complete multi-board selection");
    }
    controller.undo();
    slides = controller.slides();
    if (slides.at(0).toMap().value(QStringLiteral("boardRole")).toString() != QStringLiteral("hook") ||
        slides.at(1).toMap().value(QStringLiteral("boardRole")).toString() != QStringLiteral("cta")) {
      return fail("storyboard multi-board role assignment should undo atomically");
    }

    if (!controller.saveDocument()) {
      return fail("carousel documents with semantic board metadata should save");
    }
    if (controller.documentDirty() || controller.saveState() != QStringLiteral("Saved") ||
        controller.lastSavedAt().isEmpty() || !controller.saveError().isEmpty()) {
      return fail("successful carousel saves should expose saved state, time, and no failure detail");
    }
    CarouselDocumentController restored;
    if (!restored.loadDocument(controller.savedDocumentId())) {
      return fail("carousel documents with semantic board metadata should load");
    }
    slides = restored.slides();
    if (slides.size() != 2 ||
        slides.at(0).toMap().value(QStringLiteral("boardName")).toString() != QStringLiteral("Opening argument") ||
        slides.at(0).toMap().value(QStringLiteral("boardRole")).toString() != QStringLiteral("hook") ||
        slides.at(1).toMap().value(QStringLiteral("boardName")).toString() != QStringLiteral("Final action") ||
        slides.at(1).toMap().value(QStringLiteral("sourceReferenceNote")).toString() !=
            QStringLiteral("Use the approved campaign URL.") ||
        slides.at(1).toMap().value(QStringLiteral("altText")).toString() !=
            QStringLiteral("A direct call to action with the campaign URL.")) {
      return fail("saved carousel documents should restore board roles, names, production notes, and accessibility metadata");
    }
    if (restored.documentDirty() || restored.saveState() != QStringLiteral("Saved") ||
        restored.lastSavedAt().isEmpty()) {
      return fail("loaded carousel documents should expose the restored saved state");
    }

    const auto before_notes_dir = dir.filePath(QStringLiteral("export-board-notes-before"));
    const auto after_notes_dir = dir.filePath(QStringLiteral("export-board-notes-after"));
    QDir().mkpath(before_notes_dir);
    QDir().mkpath(after_notes_dir);
    if (!restored.exportCarouselToDirectory(before_notes_dir)) {
      std::cerr << restored.status().toStdString() << '\n';
      return fail("board metadata baseline export should succeed");
    }
    restored.selectSlide(0);
    restored.setCurrentSlideNotes(
        QStringLiteral("A revised internal-only message."),
        QStringLiteral("A revised internal-only media direction."),
        QStringLiteral("A revised internal-only source reference."));
    restored.setCurrentSlideAccessibilityText(
        QStringLiteral("Revised alt text that is metadata only."),
        QStringLiteral("Revised plain-language metadata that is not rendered."));
    if (!restored.documentDirty() || restored.saveState() != QStringLiteral("Unsaved")) {
      return fail("editing a saved carousel should immediately expose an unsaved state");
    }
    if (!restored.exportCarouselToDirectory(after_notes_dir)) {
      return fail("board metadata comparison export should succeed");
    }
    const QImage before_notes_image(QDir(before_notes_dir).filePath(QStringLiteral("carousel-01.png")));
    const QImage after_notes_image(QDir(after_notes_dir).filePath(QStringLiteral("carousel-01.png")));
    if (before_notes_image.isNull() || after_notes_image.isNull() || before_notes_image != after_notes_image) {
      return fail("production and accessibility metadata should save without rendering into exported artwork");
    }
    restored.undo();
    restored.undo();
    if (restored.documentDirty() || restored.saveState() != QStringLiteral("Saved")) {
      return fail("undoing back to the saved carousel content should restore the saved state");
    }
  }

  {
    const auto source_path = write_image(
        dir,
        QStringLiteral("carousel-edit-round-trip.png"),
        split_image(QSize(800, 500), QColor(QStringLiteral("#7a3028")), QColor(QStringLiteral("#285c7a"))));
    if (source_path.isEmpty()) {
      return fail("carousel edit round-trip source should save");
    }

    CarouselDocumentController controller;
    controller.addPhotoItem(source_path, QStringLiteral("round trip"));
    controller.setSelectedItemSize(460.0, 320.0);
    controller.setSelectedItemMediaScale(1.75);
    controller.setSelectedItemMediaOffset(0.35, -0.2);
    controller.setSelectedItemMediaMaskShape(QStringLiteral("ellipse"));
    const auto before = controller.selectionDetails();
    const auto payload = controller.selectedPhotoEditPayload();
    if (payload.value(QStringLiteral("targetKind")).toString() != QStringLiteral("layer") ||
        payload.value(QStringLiteral("filePath")).toString() != source_path ||
        payload.value(QStringLiteral("targetKey")).toString().isEmpty()) {
      return fail("selected Carousel photos should expose an instance-safe Edit payload");
    }

    CarouselDocumentController::RecipeState edited_state;
    edited_state.exposure = 0.65;
    edited_state.temperature = 18.0;
    const auto edited_patch =
        photo_editor::desktop::serialize_adjustment_patch_json(
            edited_state,
            photo_editor::desktop::all_adjustment_groups(),
            photo_editor::desktop::AdjustmentPatchScope::AssetBound);
    const auto preview_revision_before_edit =
        controller.previewDiagnostics().value(QStringLiteral("revision")).toInt();
    if (!controller.applyAdjustmentPatchToPhotoEditTarget(
            payload.value(QStringLiteral("targetKey")).toString(), edited_patch)) {
      return fail("Edit adjustments should apply back to the selected Carousel instance");
    }

    if (!wait_until([&controller, &source_path, preview_revision_before_edit]() {
          const auto items = controller.canvasItems();
          return controller.previewDiagnostics().value(QStringLiteral("revision")).toInt() >
                     preview_revision_before_edit &&
                 items.size() == 1 &&
                 items.constFirst().toMap().value(QStringLiteral("sourceUrl")).toString() !=
                     QUrl::fromLocalFile(source_path).toString();
        })) {
      return fail("round-trip photo edits should publish a refreshed preview");
    }

    const auto after = controller.selectionDetails();
    const auto applied_payload = controller.selectedPhotoEditPayload();
    const auto applied_items = controller.canvasItems();
    const auto applied_adjustments =
        photo_editor::desktop::parse_adjustment_patch_json(
            applied_payload.value(
                QStringLiteral("adjustmentPatchJson"))
                .toString());
    if (!applied_adjustments.ok() ||
        std::abs(
            applied_adjustments.state->exposure -
            edited_state.exposure) > 1e-9 ||
        applied_items.size() != 1 ||
        applied_items.constFirst().toMap().value(QStringLiteral("sourceUrl")).toString() ==
            QUrl::fromLocalFile(source_path).toString() ||
        after.value(QStringLiteral("width")).toDouble() != before.value(QStringLiteral("width")).toDouble() ||
        after.value(QStringLiteral("height")).toDouble() != before.value(QStringLiteral("height")).toDouble() ||
        after.value(QStringLiteral("mediaScale")).toDouble() != before.value(QStringLiteral("mediaScale")).toDouble() ||
        after.value(QStringLiteral("mediaOffsetX")).toDouble() != before.value(QStringLiteral("mediaOffsetX")).toDouble() ||
        after.value(QStringLiteral("mediaOffsetY")).toDouble() != before.value(QStringLiteral("mediaOffsetY")).toDouble() ||
        after.value(QStringLiteral("mediaMaskShape")).toString() != QStringLiteral("ellipse")) {
      return fail("round-trip photo edits should refresh the preview without changing Carousel framing");
    }

    controller.undo();
    if (controller.canvasItems().size() != 1 ||
        !controller.selectedPhotoEditPayload()
             .value(QStringLiteral("adjustmentPatchJson"))
             .toString()
             .isEmpty()) {
      return fail("one Carousel undo should revert the applied Edit recipe without removing the photo");
    }

    CarouselDocumentController stale_controller;
    if (stale_controller.applyAdjustmentPatchToPhotoEditTarget(
            payload.value(QStringLiteral("targetKey")).toString(), edited_patch)) {
      return fail("photo edit target keys should not apply to another Carousel session");
    }

    CarouselDocumentController slot_controller;
    slot_controller.assignPrimaryImage(source_path, QStringLiteral("slot round trip"));
    const auto slot_payload = slot_controller.selectedPhotoEditPayload();
    if (slot_payload.value(QStringLiteral("targetKind")).toString() != QStringLiteral("slot") ||
        !slot_controller.applyAdjustmentPatchToPhotoEditTarget(
            slot_payload.value(QStringLiteral("targetKey")).toString(), edited_patch) ||
        slot_controller.selectedPhotoEditPayload()
            .value(QStringLiteral("adjustmentPatchJson"))
            .toString()
            .isEmpty()) {
      return fail("board-slot photos should use the same Edit and return workflow");
    }
    slot_controller.undo();
    if (!slot_controller.selectedPhotoEditPayload()
             .value(QStringLiteral("adjustmentPatchJson"))
             .toString()
             .isEmpty()) {
      return fail("slot photo round trips should undo as one Carousel edit");
    }
  }

  {
    CarouselDocumentController controller;
    const auto spread_path = write_image(
        dir,
        QStringLiteral("slot-spread.png"),
        split_image(QSize(2160, 1350), QColor(QStringLiteral("#ff453a")), QColor(QStringLiteral("#0a84ff"))));
    if (spread_path.isEmpty()) {
      return fail("linked spread source image should save");
    }

    controller.assignPrimaryImage(spread_path, QStringLiteral("spread"));
    controller.applyTemplateToCurrent(QStringLiteral("linked-horizontal-pair"));

    if (!wait_until([&controller]() {
          const auto continuity = controller.continuityPreview();
          return continuity.value(QStringLiteral("available")).toBool() &&
              !continuity.value(QStringLiteral("leftPreviewSource")).toString().isEmpty() &&
              !continuity.value(QStringLiteral("rightPreviewSource")).toString().isEmpty();
        })) {
      return fail("linked spread board previews should publish asynchronously");
    }
    auto continuity = controller.continuityPreview();
    if (!continuity.value(QStringLiteral("available")).toBool() ||
        continuity.value(QStringLiteral("leftBoardIndex")).toInt() != 0 ||
        continuity.value(QStringLiteral("rightBoardIndex")).toInt() != 1 ||
        continuity.value(QStringLiteral("actualCutX")).toInt() != 1080 ||
        continuity.value(QStringLiteral("boardPixelHeight")).toInt() != 1350 ||
        continuity.value(QStringLiteral("leftPreviewSource")).toString().isEmpty() ||
        continuity.value(QStringLiteral("rightPreviewSource")).toString().isEmpty() ||
        continuity.value(QStringLiteral("hasCriticalSeamContent")).toBool()) {
      return fail("linked spreads should expose exact rendered board previews and their exported cut");
    }

    controller.addTextItem(QStringLiteral("Keep this away from the cut"));
    controller.setSelectedItemCenter(1080.0, 150.0);
    controller.setSelectedItemSize(420.0, 100.0);
    continuity = controller.continuityPreview();
    const auto seam_warnings = continuity.value(QStringLiteral("seamWarnings")).toList();
    if (!continuity.value(QStringLiteral("hasCriticalSeamContent")).toBool() ||
        continuity.value(QStringLiteral("criticalSeamCount")).toInt() != 1 ||
        seam_warnings.size() != 1 ||
        seam_warnings.constFirst().toMap().value(QStringLiteral("itemType")).toString() !=
            QStringLiteral("text") ||
        seam_warnings.constFirst().toMap().value(QStringLiteral("itemId")).toString() !=
            controller.selectedItemId()) {
      return fail("continuity preview should identify the exact editable layer crossing an unsafe seam");
    }

    const auto export_dir_path = dir.filePath(QStringLiteral("export-linked-template"));
    QDir().mkpath(export_dir_path);
    if (!controller.exportCarouselToDirectory(export_dir_path)) {
      return fail("linked template export should succeed");
    }

    const QImage first_slide(QDir(export_dir_path).filePath(QStringLiteral("carousel-01.png")));
    const QImage second_slide(QDir(export_dir_path).filePath(QStringLiteral("carousel-02.png")));
    if (!color_is_dominantly_red(
            first_slide.pixelColor(first_slide.width() / 2,
                                   first_slide.height() / 2)) ||
        !color_is_dominantly_blue(
            second_slide.pixelColor(second_slide.width() / 2,
                                    second_slide.height() / 2))) {
      std::cerr << "linked_template_first=" << first_slide.width() << 'x'
                << first_slide.height() << " center="
                << first_slide
                       .pixelColor(first_slide.width() / 2,
                                   first_slide.height() / 2)
                       .name(QColor::HexArgb)
                       .toStdString()
                << " second=" << second_slide.width() << 'x'
                << second_slide.height() << " center="
                << second_slide
                       .pixelColor(second_slide.width() / 2,
                                   second_slide.height() / 2)
                       .name(QColor::HexArgb)
                       .toStdString()
                << '\n';
      return fail("linked template export should still cut a slot-based spread cleanly across two canvases");
    }
  }

  {
    CarouselDocumentController controller;
    controller.createCarouselWithSettings(QVariantMap{
        {QStringLiteral("carouselName"), QStringLiteral("Neighbor Comparison Carousel")},
        {QStringLiteral("documentFormat"), QStringLiteral("4x5")},
        {QStringLiteral("slideCount"), 3},
    });
    controller.setCurrentSlideName(QStringLiteral("Opening hook"));
    controller.setCurrentSlideRole(QStringLiteral("hook"));
    controller.selectSlide(1);
    controller.setCurrentSlideName(QStringLiteral("Supporting proof"));
    controller.setCurrentSlideRole(QStringLiteral("content"));
    controller.selectSlide(2);
    controller.setCurrentSlideName(QStringLiteral("Final action"));
    controller.setCurrentSlideRole(QStringLiteral("cta"));
    controller.selectSlide(0);
    const auto document_hash = controller.documentContentHash();

    if (!wait_until([&controller]() {
          const auto comparison = controller.neighborComparison();
          return !comparison.value(QStringLiteral("leftPreviewSource")).toString().isEmpty() &&
              !comparison.value(QStringLiteral("rightPreviewSource")).toString().isEmpty();
        })) {
      return fail("neighbor comparison previews should publish asynchronously");
    }
    auto comparison = controller.neighborComparison();
    if (!comparison.value(QStringLiteral("available")).toBool() ||
        !comparison.value(QStringLiteral("temporary")).toBool() ||
        comparison.value(QStringLiteral("exports")).toBool() ||
        comparison.value(QStringLiteral("currentIndex")).toInt() != 0 ||
        comparison.value(QStringLiteral("neighborIndex")).toInt() != 1 ||
        comparison.value(QStringLiteral("leftBoardIndex")).toInt() != 0 ||
        comparison.value(QStringLiteral("rightBoardIndex")).toInt() != 1 ||
        comparison.value(QStringLiteral("leftName")).toString() != QStringLiteral("Opening hook") ||
        comparison.value(QStringLiteral("rightName")).toString() != QStringLiteral("Supporting proof") ||
        comparison.value(QStringLiteral("focusedSide")).toString() != QStringLiteral("left") ||
        comparison.value(QStringLiteral("previousAvailable")).toBool() ||
        !comparison.value(QStringLiteral("nextAvailable")).toBool() ||
        comparison.value(QStringLiteral("leftPreviewSource")).toString().isEmpty() ||
        comparison.value(QStringLiteral("rightPreviewSource")).toString().isEmpty()) {
      return fail("neighbor comparison should expose the current and next rendered boards without becoming export content");
    }
    if (controller.selectAdjacentSlide(-1) || controller.currentIndex() != 0 ||
        !controller.selectAdjacentSlide(1) || controller.currentIndex() != 1 ||
        !controller.selectAdjacentSlide(1) || controller.currentIndex() != 2 ||
        controller.selectAdjacentSlide(1)) {
      return fail("adjacent board navigation should move one board at a time and stop at sequence boundaries");
    }

    comparison = controller.neighborComparison();
    if (comparison.value(QStringLiteral("currentIndex")).toInt() != 2 ||
        comparison.value(QStringLiteral("neighborIndex")).toInt() != 1 ||
        comparison.value(QStringLiteral("leftBoardIndex")).toInt() != 1 ||
        comparison.value(QStringLiteral("rightBoardIndex")).toInt() != 2 ||
        comparison.value(QStringLiteral("focusedSide")).toString() != QStringLiteral("right") ||
        !comparison.value(QStringLiteral("previousAvailable")).toBool() ||
        comparison.value(QStringLiteral("nextAvailable")).toBool() ||
        comparison.value(QStringLiteral("rightName")).toString() != QStringLiteral("Final action") ||
        controller.documentContentHash() != document_hash) {
      return fail("neighbor comparison should flip to the previous board at the sequence end without mutating the document");
    }
  }

  {
    CarouselDocumentController controller;
    controller.createCarouselWithSettings(QVariantMap{
        {QStringLiteral("carouselName"), QStringLiteral("Board Range Commands")},
        {QStringLiteral("documentFormat"), QStringLiteral("4x5")},
        {QStringLiteral("slideCount"), 4},
    });
    for (int index = 0; index < 4; ++index) {
      controller.selectSlide(index);
      controller.addTextItemWithRole(
          QStringLiteral("body"), QStringLiteral("Range board %1").arg(index + 1));
    }
    controller.selectSlide(1);
    controller.selectSlideRange(2);

    const auto current_preview = controller.previewBoardRangeCommand(
        QStringLiteral("background"), QStringLiteral("#112233"), QStringLiteral("current"));
    const auto selected_preview = controller.previewBoardRangeCommand(
        QStringLiteral("background"), QStringLiteral("#112233"), QStringLiteral("selected"));
    const auto following_preview = controller.previewBoardRangeCommand(
        QStringLiteral("background"), QStringLiteral("#112233"), QStringLiteral("following"));
    const auto all_preview = controller.previewBoardRangeCommand(
        QStringLiteral("background"), QStringLiteral("#112233"), QStringLiteral("all"));
    if (current_preview.value(QStringLiteral("affectedBoardCount")).toInt() != 1 ||
        selected_preview.value(QStringLiteral("affectedBoardCount")).toInt() != 2 ||
        selected_preview.value(QStringLiteral("boardNumbers")).toList() != QVariantList{2, 3} ||
        following_preview.value(QStringLiteral("affectedBoardCount")).toInt() != 2 ||
        following_preview.value(QStringLiteral("boardNumbers")).toList() != QVariantList{3, 4} ||
        all_preview.value(QStringLiteral("affectedBoardCount")).toInt() != 4) {
      return fail("board range previews should resolve current, selected, following, and all scopes exactly");
    }

    auto before_hash = controller.documentContentHash();
    if (!controller.applyBoardRangeCommand(
            QStringLiteral("background"), QStringLiteral("#112233"), QStringLiteral("selected"))) {
      return fail("selected-board background range command should apply");
    }
    const auto background_slides = controller.slides();
    if (background_slides.at(0).toMap().value(QStringLiteral("backgroundColor")).toString() ==
            QStringLiteral("#112233") ||
        background_slides.at(1).toMap().value(QStringLiteral("backgroundColor")).toString() !=
            QStringLiteral("#112233") ||
        background_slides.at(2).toMap().value(QStringLiteral("backgroundColor")).toString() !=
            QStringLiteral("#112233") ||
        background_slides.at(3).toMap().value(QStringLiteral("backgroundColor")).toString() ==
            QStringLiteral("#112233")) {
      return fail("selected-board background range command should mutate only the exact previewed boards");
    }
    controller.undo();
    if (controller.documentContentHash() != before_hash) {
      return fail("background range command should undo atomically");
    }

    before_hash = controller.documentContentHash();
    if (!controller.applyBoardRangeCommand(
            QStringLiteral("template"), QStringLiteral("split-pair"), QStringLiteral("following")) ||
        controller.slides().at(2).toMap().value(QStringLiteral("templateKind")).toString() !=
            QStringLiteral("split-pair") ||
        controller.slides().at(3).toMap().value(QStringLiteral("templateKind")).toString() !=
            QStringLiteral("split-pair")) {
      return fail("following-board layout range command should apply to the current board through the sequence end");
    }
    controller.undo();
    if (controller.documentContentHash() != before_hash) {
      return fail("layout range command should undo atomically");
    }

    before_hash = controller.documentContentHash();
    if (!controller.applyBoardRangeCommand(
            QStringLiteral("spacing"), QStringLiteral("airy"), QStringLiteral("all"))) {
      return fail("all-board spacing range command should apply");
    }
    for (const auto& value : controller.slides()) {
      const auto slide = value.toMap();
      if (slide.value(QStringLiteral("layoutMarginX")).toDouble() != 108.0 ||
          slide.value(QStringLiteral("layoutMarginY")).toDouble() != 108.0 ||
          slide.value(QStringLiteral("layoutGutterX")).toDouble() != 36.0 ||
          slide.value(QStringLiteral("layoutGutterY")).toDouble() != 36.0) {
        return fail("spacing range command should apply the complete margin and gutter rule to every board");
      }
    }
    controller.undo();
    if (controller.documentContentHash() != before_hash) {
      return fail("spacing range command should undo atomically");
    }

    before_hash = controller.documentContentHash();
    if (!controller.applyBoardRangeCommand(
            QStringLiteral("text-style"), QStringLiteral("heading"), QStringLiteral("following"))) {
      return fail("following-board text-style range command should apply");
    }
    int heading_layers = 0;
    for (const auto& value : controller.canvasItems()) {
      const auto item = value.toMap();
      const auto board_index = item.value(QStringLiteral("slideIndex")).toInt();
      if (board_index >= 2 && item.value(QStringLiteral("textStyleId")).toString() ==
                                  QStringLiteral("heading")) {
        ++heading_layers;
      }
    }
    if (heading_layers != 2) {
      return fail("text-style range command should restyle every text layer on only the resolved following boards");
    }
    controller.undo();
    if (controller.documentContentHash() != before_hash) {
      return fail("text-style range command should undo atomically");
    }

    controller.selectSlide(0);
    controller.setCurrentSlideBackgroundColor(QStringLiteral("#224466"));
    if (!controller.saveCurrentBoardAsBrandKit(QStringLiteral("Range Command Brand"))) {
      return fail("board range brand fixture should save");
    }
    const auto brand_id = controller.brandKits().constLast().toMap().value(QStringLiteral("brandId")).toString();
    controller.selectSlide(2);
    controller.setCurrentSlideBackgroundColor(QStringLiteral("#fbf7f1"));
    before_hash = controller.documentContentHash();
    if (!controller.applyBoardRangeCommand(
            QStringLiteral("brand"), brand_id, QStringLiteral("current")) ||
        controller.slides().at(2).toMap().value(QStringLiteral("backgroundColor")).toString() !=
            QStringLiteral("#224466")) {
      return fail("current-board brand range command should apply the chosen kit");
    }
    controller.undo();
    if (controller.documentContentHash() != before_hash) {
      return fail("brand range command should undo atomically");
    }
    controller.deleteBrandKit(brand_id);
  }

  {
    CarouselDocumentController controller;
    controller.createCarouselWithSettings(QVariantMap{
        {QStringLiteral("carouselName"), QStringLiteral("Structure Inspector")},
        {QStringLiteral("documentFormat"), QStringLiteral("4x5")},
        {QStringLiteral("slideCount"), 5},
    });
    controller.applyTemplateToCurrent(QStringLiteral("split-pair"));
    controller.addTextItem(QStringLiteral("A repeated campaign sentence"));
    controller.selectSlide(1);
    controller.addTextItem(QStringLiteral("A repeated campaign sentence"));
    controller.selectSlide(4);
    controller.setCurrentSlideBackgroundColor(QStringLiteral("#334455"));

    const auto issue_by_code = [](const QVariantList& issues, const QString& code) {
      for (const auto& value : issues) {
        const auto issue = value.toMap();
        if (issue.value(QStringLiteral("code")).toString() == code) {
          return issue;
        }
      }
      return QVariantMap{};
    };
    auto issues = controller.documentStructureIssues();
    const auto empty_slot = issue_by_code(issues, QStringLiteral("empty-required-slot"));
    const auto duplicate_copy = issue_by_code(issues, QStringLiteral("duplicate-copy"));
    const auto style_outlier = issue_by_code(issues, QStringLiteral("board-style-outlier"));
    if (empty_slot.isEmpty() || duplicate_copy.isEmpty() || style_outlier.isEmpty() ||
        empty_slot.value(QStringLiteral("category")).toString() != QStringLiteral("empty-slots") ||
        duplicate_copy.value(QStringLiteral("relatedItemIds")).toList().size() != 2 ||
        !controller.focusDocumentStructureIssue(
            duplicate_copy.value(QStringLiteral("issueId")).toString()) ||
        controller.selectedItemId() != duplicate_copy.value(QStringLiteral("itemId")).toString()) {
      return fail("document structure inspector should report empty slots, cross-board duplicate copy, style outliers, and jump to the exact layer");
    }

    controller.applySequenceTemplate(QStringLiteral("tutorial"));
    QMap<QString, QStringList> linked_items;
    for (const auto& value : controller.canvasItems()) {
      const auto item = value.toMap();
      const auto link_id = item.value(QStringLiteral("componentLinkId")).toString();
      if (!link_id.isEmpty()) {
        linked_items[link_id].push_back(item.value(QStringLiteral("itemId")).toString());
      }
    }
    QString broken_link_id;
    QString unmatched_link_id;
    for (auto iterator = linked_items.constBegin(); iterator != linked_items.constEnd(); ++iterator) {
      if (iterator.value().size() < 3) {
        continue;
      }
      if (broken_link_id.isEmpty()) {
        broken_link_id = iterator.key();
      } else {
        unmatched_link_id = iterator.key();
        break;
      }
    }
    if (broken_link_id.isEmpty() || unmatched_link_id.isEmpty()) {
      return fail("structure inspector fixture should expose at least two repeated component link groups");
    }
    controller.selectItem(linked_items.value(broken_link_id).constFirst());
    controller.duplicateSelectedItem();
    const auto unmatched_ids = linked_items.value(unmatched_link_id);
    for (int index = 1; index < unmatched_ids.size(); ++index) {
      controller.selectItem(unmatched_ids.at(index));
      controller.removeSelectedItem();
    }
    issues = controller.documentStructureIssues();
    const auto broken_link = issue_by_code(issues, QStringLiteral("broken-component-link"));
    const auto unmatched_component = issue_by_code(issues, QStringLiteral("unmatched-component"));
    if (broken_link.isEmpty() || unmatched_component.isEmpty() ||
        broken_link.value(QStringLiteral("category")).toString() != QStringLiteral("broken-links") ||
        unmatched_component.value(QStringLiteral("category")).toString() !=
            QStringLiteral("unmatched-components") ||
        !controller.focusDocumentStructureIssue(
            unmatched_component.value(QStringLiteral("issueId")).toString()) ||
        controller.selectedItemId() != unmatched_component.value(QStringLiteral("itemId")).toString()) {
      return fail("document structure inspector should distinguish broken repeated links from unmatched component instances and jump to either");
    }
  }

  {
    CarouselDocumentController controller;
    controller.createCarouselWithSettings(QVariantMap{
        {QStringLiteral("carouselName"), QStringLiteral("Board Sections Carousel")},
        {QStringLiteral("documentFormat"), QStringLiteral("4x5")},
        {QStringLiteral("slideCount"), 8},
    });
    for (int index = 0; index < 8; ++index) {
      controller.selectSlide(index);
      controller.setCurrentSlideName(QStringLiteral("Board %1").arg(index + 1));
      controller.addTextItem(QStringLiteral("Section content %1").arg(index + 1));
    }

    controller.selectSlide(1);
    controller.selectSlideRange(3);
    if (!controller.createBoardSection(QStringLiteral("Opening argument"))) {
      return fail("a consecutive multi-board selection should create a named section");
    }
    controller.selectSlide(4);
    controller.selectSlideRange(5);
    if (!controller.createBoardSection(QStringLiteral("Evidence"))) {
      return fail("a second non-overlapping board range should create another section");
    }
    auto sections = controller.boardSections();
    if (sections.size() != 2 ||
        sections.at(0).toMap().value(QStringLiteral("startIndex")).toInt() != 1 ||
        sections.at(0).toMap().value(QStringLiteral("span")).toInt() != 3 ||
        sections.at(1).toMap().value(QStringLiteral("startIndex")).toInt() != 4 ||
        sections.at(1).toMap().value(QStringLiteral("span")).toInt() != 2) {
      return fail("board sections should expose exact ordered ranges and spans");
    }
    const auto opening_id = sections.at(0).toMap().value(QStringLiteral("sectionId")).toString();
    const auto evidence_id = sections.at(1).toMap().value(QStringLiteral("sectionId")).toString();
    if (!controller.renameBoardSection(opening_id, QStringLiteral("Opening chapter")) ||
        !controller.setBoardSectionCollapsed(opening_id, true)) {
      return fail("board sections should support undoable rename and collapse state");
    }
    const auto collapsed_entries = controller.storyboardEntries();
    int collapsed_summary_count = 0;
    for (const auto& value : collapsed_entries) {
      const auto entry = value.toMap();
      if (entry.value(QStringLiteral("sectionId")).toString() == opening_id) {
        ++collapsed_summary_count;
        if (!entry.value(QStringLiteral("sectionCollapsed")).toBool() ||
            entry.value(QStringLiteral("sectionSpan")).toInt() != 3 ||
            entry.value(QStringLiteral("hiddenBoardCount")).toInt() != 2) {
          return fail("collapsed storyboard section summaries should retain the exact hidden board count");
        }
      }
    }
    if (collapsed_entries.size() != 6 || collapsed_summary_count != 1) {
      return fail("collapsing a three-board section should replace its range with one overview entry");
    }

    const auto before_move_hash = controller.documentContentHash();
    if (!controller.moveBoardSection(opening_id, 1)) {
      return fail("a complete section should move later across the neighboring section");
    }
    sections = controller.boardSections();
    QVariantMap moved_opening;
    QVariantMap moved_evidence;
    for (const auto& value : sections) {
      const auto section = value.toMap();
      if (section.value(QStringLiteral("sectionId")).toString() == opening_id) {
        moved_opening = section;
      } else if (section.value(QStringLiteral("sectionId")).toString() == evidence_id) {
        moved_evidence = section;
      }
    }
    const auto moved_slides = controller.slides();
    if (moved_evidence.value(QStringLiteral("startIndex")).toInt() != 1 ||
        moved_opening.value(QStringLiteral("startIndex")).toInt() != 3 ||
        moved_slides.at(3).toMap().value(QStringLiteral("boardName")).toString() !=
            QStringLiteral("Board 2") ||
        moved_slides.at(4).toMap().value(QStringLiteral("boardName")).toString() !=
            QStringLiteral("Board 3") ||
        moved_slides.at(5).toMap().value(QStringLiteral("boardName")).toString() !=
            QStringLiteral("Board 4")) {
      return fail("moving a section should cross the neighboring section while preserving every internal board position");
    }
    for (const auto& value : controller.canvasItems()) {
      const auto item = value.toMap();
      const auto text = item.value(QStringLiteral("text")).toString();
      if (text == QStringLiteral("Section content 2") &&
          item.value(QStringLiteral("slideIndex")).toInt() != 3) {
        return fail("section movement should remap editable layers with their owning boards");
      }
    }
    controller.undo();
    if (controller.documentContentHash() != before_move_hash) {
      return fail("moving an entire board section should restore in one undo step");
    }
    if (!controller.selectBoardSection(opening_id) || controller.selectedSlideCount() != 3 ||
        controller.currentIndex() != 1) {
      return fail("section selection should restore the complete consecutive board range");
    }

    if (!controller.saveDocument(QStringLiteral("Board Sections Persistence"))) {
      return fail("board section persistence fixture should save");
    }
    CarouselDocumentController restored;
    if (!restored.loadDocument(controller.savedDocumentId()) ||
        restored.boardSections().size() != 2 ||
        !restored.boardSections().at(0).toMap().value(QStringLiteral("collapsed")).toBool() ||
        restored.storyboardEntries().size() != 6 ||
        restored.documentContentHash() != controller.documentContentHash()) {
      return fail("section names, membership, collapse state, and ordering should survive save and reopen");
    }
    if (!restored.removeBoardSection(evidence_id) || restored.boardSections().size() != 1 ||
        restored.slideCount() != 8) {
      return fail("ungrouping a section should keep every board in its current order");
    }

    CarouselDocumentController invalid_selection;
    invalid_selection.createCarouselWithSettings(QVariantMap{
        {QStringLiteral("carouselName"), QStringLiteral("Invalid Board Section")},
        {QStringLiteral("slideCount"), 4},
    });
    invalid_selection.selectSlide(0);
    invalid_selection.toggleSlideSelection(2);
    if (invalid_selection.createBoardSection(QStringLiteral("Disjoint"))) {
      return fail("board sections should reject non-consecutive selections");
    }
  }

  {
    const auto item_by_id = [](const CarouselDocumentController& controller, const QString& item_id) {
      for (const auto& value : controller.canvasItems()) {
        const auto item = value.toMap();
        if (item.value(QStringLiteral("itemId")).toString() == item_id) {
          return item;
        }
      }
      return QVariantMap{};
    };
    const auto item_by_component_and_board = [](
                                                  const CarouselDocumentController& controller,
                                                  const QString& component_id,
                                                  int board_index) {
      for (const auto& value : controller.canvasItems()) {
        const auto item = value.toMap();
        if (item.value(QStringLiteral("componentLinkId")).toString() == component_id &&
            item.value(QStringLiteral("slideIndex")).toInt() == board_index) {
          return item;
        }
      }
      return QVariantMap{};
    };

    CarouselDocumentController controller;
    controller.createCarouselWithSettings(QVariantMap{
        {QStringLiteral("carouselName"), QStringLiteral("Repeated Element Proof")},
        {QStringLiteral("documentFormat"), QStringLiteral("4x5")},
        {QStringLiteral("slideCount"), 4},
    });
    controller.selectSlide(0);
    controller.selectSlideRange(2);
    controller.addShapeItem(QStringLiteral("rectangle"));
    const auto source_shape_id = controller.selectedItemId();
    controller.setSelectedItemCenter(540.0, 1120.0);
    controller.setSelectedItemSize(760.0, 140.0);
    controller.setSelectedShapeFillColor(QStringLiteral("#161b22"));
    controller.addTextItemWithRole(QStringLiteral("cta"), QStringLiteral("READ THE FULL STORY"));
    const auto source_text_id = controller.selectedItemId();
    controller.setSelectedItemCenter(540.0, 1120.0);
    controller.setSelectedTextColor(QStringLiteral("#ffffff"));
    controller.selectItem(source_text_id);
    controller.addItemToSelection(source_shape_id);
    if (controller.selectedSlideCount() != 3 ||
        !controller.createRepeatedElementLink(QStringLiteral("Story CTA"), QStringLiteral("selected"))) {
      return fail("selected boards should accept a named multi-layer repeated element");
    }

    auto links = controller.repeatedElementLinks();
    if (links.size() != 1 ||
        links.constFirst().toMap().value(QStringLiteral("name")).toString() !=
            QStringLiteral("Story CTA") ||
        links.constFirst().toMap().value(QStringLiteral("instanceCount")).toInt() != 3 ||
        links.constFirst().toMap().value(QStringLiteral("partCount")).toInt() != 2 ||
        links.constFirst().toMap().value(QStringLiteral("layerCount")).toInt() != 6 ||
        controller.canvasItems().size() != 6) {
      return fail("repeated-element metadata should expose exact boards, parts, and layer counts");
    }
    const auto repeated_link_id =
        links.constFirst().toMap().value(QStringLiteral("linkId")).toString();
    const auto text_component_id =
        item_by_id(controller, source_text_id).value(QStringLiteral("componentLinkId")).toString();
    const auto shape_component_id =
        item_by_id(controller, source_shape_id).value(QStringLiteral("componentLinkId")).toString();
    if (!text_component_id.startsWith(repeated_link_id + QStringLiteral(":")) ||
        !shape_component_id.startsWith(repeated_link_id + QStringLiteral(":")) ||
        text_component_id == shape_component_id ||
        !item_by_component_and_board(controller, text_component_id, 3).isEmpty()) {
      return fail("each repeated-element part should retain one identity on only the chosen boards");
    }

    controller.selectItem(source_text_id);
    controller.setSelectedText(QStringLiteral("OPEN THE CASE STUDY"));
    controller.setSelectedTextColor(QStringLiteral("#00ff99"));
    controller.setSelectedItemCenter(460.0, 1060.0);
    for (int board_index = 0; board_index < 3; ++board_index) {
      const auto item = item_by_component_and_board(controller, text_component_id, board_index);
      if (item.value(QStringLiteral("text")).toString() != QStringLiteral("OPEN THE CASE STUDY") ||
          item.value(QStringLiteral("textColor")).toString() != QStringLiteral("#00ff99") ||
          qAbs(item.value(QStringLiteral("centerX")).toDouble() -
               (board_index * 1080.0 + 460.0)) > 0.001 ||
          qAbs(item.value(QStringLiteral("centerY")).toDouble() - 1060.0) > 0.001) {
        return fail("content, style, and board-relative geometry edits should synchronize automatically");
      }
    }

    controller.selectItem(source_text_id);
    controller.duplicateSelectedItem();
    if (!item_by_id(controller, controller.selectedItemId())
             .value(QStringLiteral("componentLinkId")).toString().isEmpty()) {
      return fail("duplicating a linked layer should create a detached local layer");
    }
    controller.undo();
    controller.selectItem(source_text_id);
    controller.copySelectedItems();
    controller.selectSlide(3);
    controller.pasteItems();
    if (!item_by_id(controller, controller.selectedItemId())
             .value(QStringLiteral("componentLinkId")).toString().isEmpty()) {
      return fail("pasting a linked layer should create a detached local layer");
    }
    controller.undo();

    const auto detached_text_id =
        item_by_component_and_board(controller, text_component_id, 1)
            .value(QStringLiteral("itemId")).toString();
    controller.selectItem(detached_text_id);
    if (!controller.detachSelectedRepeatedElement()) {
      return fail("a repeated element should detach every linked part on the selected board");
    }
    links = controller.repeatedElementLinks();
    int detached_part_count = 0;
    for (const auto& value : controller.canvasItems()) {
      const auto item = value.toMap();
      if (item.value(QStringLiteral("slideIndex")).toInt() == 1 &&
          item.value(QStringLiteral("componentLinkId")).toString().isEmpty()) {
        ++detached_part_count;
      }
    }
    if (links.size() != 1 ||
        links.constFirst().toMap().value(QStringLiteral("instanceCount")).toInt() != 2 ||
        detached_part_count != 2 ||
        controller.selectedRepeatedElementLinkState().value(QStringLiteral("linked")).toBool()) {
      return fail("detaching a board should preserve its layers as an explicit local override");
    }
    controller.setSelectedText(QStringLiteral("LOCAL BOARD OVERRIDE"));
    controller.selectItem(source_text_id);
    controller.setSelectedText(QStringLiteral("GLOBAL LINKED CTA"));
    if (item_by_id(controller, detached_text_id).value(QStringLiteral("text")).toString() !=
            QStringLiteral("LOCAL BOARD OVERRIDE") ||
        item_by_component_and_board(controller, text_component_id, 0)
                .value(QStringLiteral("text")).toString() != QStringLiteral("GLOBAL LINKED CTA") ||
        item_by_component_and_board(controller, text_component_id, 2)
                .value(QStringLiteral("text")).toString() != QStringLiteral("GLOBAL LINKED CTA")) {
      return fail("a deliberate board override should stop receiving later linked edits");
    }
    controller.undo();
    if (item_by_component_and_board(controller, text_component_id, 0)
                .value(QStringLiteral("text")).toString() != QStringLiteral("OPEN THE CASE STUDY") ||
        item_by_component_and_board(controller, text_component_id, 2)
                .value(QStringLiteral("text")).toString() != QStringLiteral("OPEN THE CASE STUDY") ||
        item_by_id(controller, detached_text_id).value(QStringLiteral("text")).toString() !=
            QStringLiteral("LOCAL BOARD OVERRIDE")) {
      return fail("one undo should restore every synchronized instance without changing a detached override");
    }
    controller.redo();

    if (!controller.saveDocument(QStringLiteral("Repeated Element Persistence"))) {
      return fail("the repeated-element persistence fixture should save");
    }
    CarouselDocumentController restored;
    if (!restored.loadDocument(controller.savedDocumentId()) ||
        restored.documentContentHash() != controller.documentContentHash() ||
        restored.repeatedElementLinks().size() != 1 ||
        restored.repeatedElementLinks().constFirst().toMap()
                .value(QStringLiteral("instanceCount")).toInt() != 2 ||
        restored.canvasItems().size() != 6) {
      return fail("repeated-element metadata, instances, and local overrides should survive save and reopen");
    }

    if (!restored.removeRepeatedElementLink(repeated_link_id) ||
        !restored.repeatedElementLinks().isEmpty() || restored.canvasItems().size() != 6) {
      return fail("unlink all should retain every placed layer while removing synchronization metadata");
    }
    for (const auto& value : restored.canvasItems()) {
      if (!value.toMap().value(QStringLiteral("componentLinkId")).toString().isEmpty()) {
        return fail("unlink all should detach every remaining repeated-element instance");
      }
    }
    restored.undo();
    if (restored.repeatedElementLinks().size() != 1) {
      return fail("unlink all should restore atomically in one undo step");
    }
    QStringList source_linked_part_ids;
    for (const auto& value : restored.canvasItems()) {
      const auto item = value.toMap();
      if (item.value(QStringLiteral("slideIndex")).toInt() == 0 &&
          item.value(QStringLiteral("componentLinkId")).toString().startsWith(
              repeated_link_id + QStringLiteral(":"))) {
        source_linked_part_ids.push_back(item.value(QStringLiteral("itemId")).toString());
      }
    }
    restored.selectItem(source_linked_part_ids.constFirst());
    restored.addItemToSelection(source_linked_part_ids.constLast());
    restored.removeSelectedItem();
    if (!restored.repeatedElementLinks().isEmpty()) {
      return fail("removing the penultimate linked board should dissolve an unusable one-board link");
    }
    restored.undo();
    if (restored.repeatedElementLinks().size() != 1 || restored.canvasItems().size() != 6) {
      return fail("automatic repeated-link cleanup should also undo atomically");
    }

    CarouselDocumentController following_scope;
    following_scope.createCarouselWithSettings(QVariantMap{{QStringLiteral("slideCount"), 4}});
    following_scope.selectSlide(1);
    following_scope.addTextItem(QStringLiteral("Following footer"));
    if (!following_scope.createRepeatedElementLink(
            QStringLiteral("Following footer"), QStringLiteral("following")) ||
        following_scope.repeatedElementLinks().constFirst().toMap()
                .value(QStringLiteral("instanceCount")).toInt() != 3) {
      return fail("following scope should link the current board through the final board");
    }
    CarouselDocumentController all_scope;
    all_scope.createCarouselWithSettings(QVariantMap{{QStringLiteral("slideCount"), 4}});
    all_scope.selectSlide(2);
    all_scope.addTextItem(QStringLiteral("All-board number"));
    if (!all_scope.createRepeatedElementLink(
            QStringLiteral("All-board number"), QStringLiteral("all")) ||
        all_scope.repeatedElementLinks().constFirst().toMap()
                .value(QStringLiteral("instanceCount")).toInt() != 4) {
      return fail("all scope should link every board regardless of the source position");
    }

    QImage logo_image(QSize(96, 96), QImage::Format_ARGB32);
    logo_image.fill(QColor(QStringLiteral("#7c5cff")));
    const auto logo_path = write_image(dir, QStringLiteral("repeated-logo.png"), logo_image);
    CarouselDocumentController media_link;
    media_link.createCarouselWithSettings(QVariantMap{{QStringLiteral("slideCount"), 2}});
    media_link.addPhotoItem(logo_path, QStringLiteral("Logo"));
    const auto media_source_id = media_link.selectedItemId();
    if (logo_path.isEmpty() ||
        !media_link.createRepeatedElementLink(QStringLiteral("Logo"), QStringLiteral("all"))) {
      return fail("media layers should support repeated-element links");
    }
    const auto media_component_id =
        item_by_id(media_link, media_source_id).value(QStringLiteral("componentLinkId")).toString();
    media_link.setSelectedItemMediaScale(1.75);
    media_link.setSelectedItemMediaOffset(0.65, -0.35);
    const auto linked_media = item_by_component_and_board(media_link, media_component_id, 1);
    if (qAbs(linked_media.value(QStringLiteral("mediaScale")).toDouble() - 1.75) > 0.001 ||
        qAbs(linked_media.value(QStringLiteral("mediaOffsetX")).toDouble() - 0.65) > 0.001 ||
        qAbs(linked_media.value(QStringLiteral("mediaOffsetY")).toDouble() + 0.35) > 0.001 ||
        linked_media.value(QStringLiteral("sourceUrl")).toUrl().isEmpty()) {
      return fail("linked media should synchronize crop state and retain a visible source on every board");
    }
  }

  {
    CarouselDocumentController controller;
    QImage display_p3_source(QSize(1080, 1350), QImage::Format_ARGB32);
    for (int y = 0; y < display_p3_source.height(); ++y) {
      for (int x = 0; x < display_p3_source.width(); ++x) {
        display_p3_source.setPixelColor(
            x,
            y,
            QColor(
                24 + ((x * 180) / std::max(1, display_p3_source.width() - 1)),
                36 + ((y * 140) / std::max(1, display_p3_source.height() - 1)),
                120));
      }
    }
    display_p3_source.setColorSpace(QColorSpace(QColorSpace::DisplayP3));

    const auto display_p3_path = write_image(
        dir,
        QStringLiteral("display-p3-source.png"),
        display_p3_source);
    if (display_p3_path.isEmpty()) {
      return fail("display-p3 source image should save");
    }

    CarouselDocumentController::RecipeState recipe_state;
    recipe_state.output_color_space = QStringLiteral("Display P3");
    controller.assignPrimaryImage(
        display_p3_path,
        QStringLiteral("display-p3"),
        photo_editor::desktop::serialize_adjustment_patch_json(
            recipe_state,
            photo_editor::desktop::all_adjustment_groups(),
            photo_editor::desktop::AdjustmentPatchScope::AssetBound));
    if (first_preflight_issue(
            controller.preflightIssues(), QStringLiteral("gamut-conversion")).isEmpty()) {
      return fail("sRGB export preflight should warn when Display P3 media will be gamut converted");
    }
    controller.setExportColorSpace(QStringLiteral("Display P3"));
    if (!first_preflight_issue(
             controller.preflightIssues(), QStringLiteral("gamut-conversion")).isEmpty()) {
      return fail("matching Display P3 output should clear the gamut conversion warning");
    }

    const auto export_dir_path = dir.filePath(QStringLiteral("export-display-p3"));
    QDir().mkpath(export_dir_path);
    if (!controller.exportCarouselToDirectory(export_dir_path)) {
      return fail("display-p3 export should succeed");
    }

    const QImage slide(QDir(export_dir_path).filePath(QStringLiteral("carousel-01.png")));
    if (slide.isNull() || !slide.colorSpace().isValid() ||
        slide.colorSpace() != QColorSpace(QColorSpace::DisplayP3)) {
      return fail("carousel export should preserve the requested output color space");
    }
  }

  {
    QGuiApplication::clipboard()->clear();
    CarouselDocumentController controller;
    if (controller.canUndo() || controller.canRedo() || controller.canPaste()) {
      return fail("new carousel history and clipboard should start empty");
    }

    controller.addTextItem(QStringLiteral("HISTORY"));
    if (!controller.canUndo() || controller.canvasItems().size() != 1) {
      return fail("adding a layer should create one undoable history entry");
    }

    controller.undo();
    if (controller.canvasItems().size() != 0 || !controller.canRedo()) {
      return fail("undo should restore the document before a layer was added");
    }
    controller.redo();
    if (controller.canvasItems().size() != 1 || !controller.canUndo()) {
      return fail("redo should restore an undone layer addition");
    }

    controller.copySelectedItems();
    if (!controller.canPaste() || controller.canvasItems().size() != 1) {
      return fail("copy should populate the internal clipboard without mutating layers");
    }
    controller.pasteItems();
    if (controller.canvasItems().size() != 2) {
      return fail("paste should create a new independently identified layer");
    }
    const auto pasted_items = controller.canvasItems();
    if (pasted_items.at(0).toMap().value(QStringLiteral("itemId")) ==
        pasted_items.at(1).toMap().value(QStringLiteral("itemId"))) {
      return fail("pasted layers should receive unique identifiers");
    }

    controller.undo();
    if (controller.canvasItems().size() != 1) {
      return fail("pasting multiple document changes should undo as one step");
    }
    controller.redo();
    if (controller.canvasItems().size() != 2) {
      return fail("redo should restore pasted layers");
    }

    const auto before_move = controller.selectionDetails().value(QStringLiteral("centerX")).toDouble();
    controller.moveSelectedItemByPixelsSilent(10.0, 0.0);
    controller.moveSelectedItemByPixelsSilent(20.0, 0.0);
    controller.moveSelectedItemByPixelsSilent(30.0, 0.0);
    controller.commitTransformState();
    const auto after_move = controller.selectionDetails().value(QStringLiteral("centerX")).toDouble();
    if (after_move <= before_move) {
      return fail("coalesced live transforms should update the selected layer");
    }
    controller.undo();
    const auto restored_center = controller.selectionDetails().value(QStringLiteral("centerX")).toDouble();
    if (qAbs(restored_center - before_move) > 0.01) {
      return fail("one undo should restore an entire coalesced live transform");
    }

    controller.setSelectedItemOpacity(0.9);
    controller.setSelectedItemOpacity(0.8);
    controller.setSelectedItemOpacity(0.7);
    if (qAbs(controller.selectionDetails().value(QStringLiteral("opacity")).toDouble() - 0.7) > 0.01) {
      return fail("rapid style updates should still apply their latest value");
    }
    controller.undo();
    if (qAbs(controller.selectionDetails().value(QStringLiteral("opacity")).toDouble() - 1.0) > 0.01) {
      return fail("rapid style updates should coalesce into one undo step");
    }
  }

  {
    const auto clipboard_mime = QStringLiteral("application/x-stillfold-carousel+json");
    QImage clipboard_media(QSize(320, 240), QImage::Format_ARGB32);
    clipboard_media.fill(QColor(QStringLiteral("#2dca72")));
    const auto clipboard_media_path =
        write_image(dir, QStringLiteral("cross-document-component.png"), clipboard_media);
    if (clipboard_media_path.isEmpty()) {
      return fail("cross-document component fixture should save");
    }

    CarouselDocumentController author;
    author.createCarouselWithSettings(QVariantMap{
        {QStringLiteral("carouselName"), QStringLiteral("Clipboard Component Source")},
        {QStringLiteral("documentFormat"), QStringLiteral("4x5")},
        {QStringLiteral("slideCount"), 1},
    });
    author.addTextItemWithRole(QStringLiteral("title"), QStringLiteral("Portable title"));
    const auto source_text_id = author.selectedItemId();
    author.setSelectedItemSize(360.0, 80.0);
    author.setSelectedItemCenter(320.0, 180.0);
    author.addPhotoItem(clipboard_media_path, QStringLiteral("Portable photo"));
    const auto source_photo_id = author.selectedItemId();
    author.setSelectedItemSize(420.0, 300.0);
    author.setSelectedItemCenter(320.0, 520.0);
    author.addShapeItem(QStringLiteral("rectangle"));
    const auto source_shape_id = author.selectedItemId();
    author.setSelectedShapeFillColor(QStringLiteral("#8844ff"));
    author.setSelectedItemSize(420.0, 80.0);
    author.setSelectedItemCenter(320.0, 880.0);
    author.selectItem(source_text_id);
    author.addItemToSelection(source_photo_id);
    author.addItemToSelection(source_shape_id);
    if (!author.createConstraintStack(QStringLiteral("vertical"), 30.0, 26.0)) {
      return fail("cross-document component source should create an explicit stack");
    }
    author.groupSelectedItems();
    author.copySelectedItems();

    const auto* source_mime = QGuiApplication::clipboard()->mimeData();
    auto payload_document = QJsonDocument::fromJson(source_mime->data(clipboard_mime));
    auto payload = payload_document.object();
    auto payload_items = payload.value(QStringLiteral("items")).toArray();
    for (qsizetype index = 0; index < payload_items.size(); ++index) {
      auto object = payload_items.at(index).toObject();
      if (object.value(QStringLiteral("kind")).toString() == QStringLiteral("text")) {
        object.insert(
            QStringLiteral("textFontFamily"),
            QStringLiteral("Stillfold Clipboard Missing Face"));
        payload_items.replace(index, object);
      }
    }
    payload.insert(QStringLiteral("items"), payload_items);
    auto* tampered_mime = new QMimeData;
    tampered_mime->setData(
        clipboard_mime, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    QGuiApplication::clipboard()->setMimeData(tampered_mime);
    if (!QFile::remove(clipboard_media_path)) {
      return fail("cross-document missing-asset fixture should be removable");
    }

    CarouselDocumentController destination_brand_author;
    destination_brand_author.setCurrentSlideBackgroundColor(QStringLiteral("#101820"));
    destination_brand_author.addTextItemWithRole(
        QStringLiteral("title"), QStringLiteral("Destination brand title"));
    destination_brand_author.setSelectedTextColor(QStringLiteral("#f2f4f8"));
    destination_brand_author.addShapeItem(QStringLiteral("rectangle"));
    destination_brand_author.setSelectedShapeFillColor(QStringLiteral("#ff4f7b"));
    if (!destination_brand_author.saveCurrentBoardAsBrandKit(
            QStringLiteral("Clipboard Destination Brand"))) {
      return fail("destination brand mapping fixture should save");
    }

    CarouselDocumentController consumer;
    consumer.createCarouselWithSettings(QVariantMap{
        {QStringLiteral("carouselName"), QStringLiteral("Clipboard Component Target")},
        {QStringLiteral("documentFormat"), QStringLiteral("1x1")},
        {QStringLiteral("slideCount"), 1},
    });
    if (!consumer.canPaste() || !consumer.previewClipboardPaste()) {
      return fail("a second Carousel document should read the application clipboard payload");
    }
    const auto paste_preview = consumer.clipboardPastePreview();
    if (paste_preview.value(QStringLiteral("kind")).toString() !=
            QStringLiteral("components") ||
        paste_preview.value(QStringLiteral("itemCount")).toInt() != 3 ||
        paste_preview.value(QStringLiteral("missingAssetCount")).toInt() != 1 ||
        paste_preview.value(QStringLiteral("missingFontCount")).toInt() != 1 ||
        paste_preview.value(QStringLiteral("sameDocument")).toBool() ||
        paste_preview.value(QStringLiteral("identityPolicy")).toString() !=
            QStringLiteral("fresh-identities")) {
      return fail("cross-document paste review should report content, missing resources, and identity policy");
    }
    const auto destination_brands = consumer.brandKits();
    if (destination_brands.isEmpty()) {
      return fail("cross-document style mapping proof requires the persisted destination brand kit");
    }
    QVariantMap destination_brand;
    for (const auto& value : destination_brands) {
      const auto candidate = value.toMap();
      if (candidate.value(QStringLiteral("name")).toString() ==
          QStringLiteral("Clipboard Destination Brand")) {
        destination_brand = candidate;
      }
    }
    const auto destination_brand_id =
        destination_brand.value(QStringLiteral("brandId")).toString();
    if (destination_brand_id.isEmpty() || !consumer.applyClipboardPaste(
            QStringLiteral("map-brand"), destination_brand_id) ||
        consumer.canvasItems().size() != 3) {
      return fail("reviewed component paste should apply optional destination brand mapping");
    }
    QString pasted_stack_id;
    QString pasted_group_id;
    QSet<QString> pasted_ids;
    bool mapped_shape = false;
    for (const auto& value : consumer.canvasItems()) {
      const auto item = value.toMap();
      const auto item_id = item.value(QStringLiteral("itemId")).toString();
      if (item_id == source_text_id || item_id == source_photo_id || item_id == source_shape_id ||
          pasted_ids.contains(item_id)) {
        return fail("cross-document pasted layers should receive unique destination identities");
      }
      pasted_ids.insert(item_id);
      const auto stack_id = item.value(QStringLiteral("constraintStackId")).toString();
      const auto group_id = item.value(QStringLiteral("objectGroupId")).toString();
      if ((!pasted_stack_id.isEmpty() && pasted_stack_id != stack_id) ||
          (!pasted_group_id.isEmpty() && pasted_group_id != group_id)) {
        return fail("pasted stack and object-group relationships should be remapped as coherent units");
      }
      pasted_stack_id = stack_id;
      pasted_group_id = group_id;
      if (item.value(QStringLiteral("type")).toString() == QStringLiteral("shape")) {
        mapped_shape = item.value(QStringLiteral("fillColor")).toString() ==
                       destination_brand.value(QStringLiteral("accentColor")).toString();
      }
    }
    if (pasted_stack_id.isEmpty() || pasted_group_id.isEmpty() || !mapped_shape ||
        consumer.preflightIssues().isEmpty()) {
      return fail("component paste should preserve relationships, map brand style, and retain reportable missing resources");
    }
    consumer.undo();
    if (!consumer.canvasItems().isEmpty()) {
      return fail("cross-document component paste should undo atomically");
    }
    consumer.redo();
    if (consumer.canvasItems().size() != 3 ||
        !consumer.saveDocument(QStringLiteral("Clipboard Component Target"))) {
      return fail("cross-document component paste should redo and save");
    }
    CarouselDocumentController reopened_component;
    if (!reopened_component.loadDocument(consumer.savedDocumentId()) ||
        reopened_component.canvasItems().size() != 3) {
      return fail("cross-document component relationships should survive save and reopen");
    }

    QImage board_media(QSize(640, 480), QImage::Format_ARGB32);
    board_media.fill(QColor(QStringLiteral("#ff8a3d")));
    const auto board_media_path =
        write_image(dir, QStringLiteral("cross-document-board.png"), board_media);
    CarouselDocumentController board_author;
    board_author.createCarouselWithSettings(QVariantMap{
        {QStringLiteral("carouselName"), QStringLiteral("Clipboard Board Source")},
        {QStringLiteral("documentFormat"), QStringLiteral("4x5")},
        {QStringLiteral("slideCount"), 2},
    });
    board_author.selectSlide(0);
    board_author.assignPrimaryImage(board_media_path, QStringLiteral("Board one"));
    board_author.addTextItem(QStringLiteral("Board one copy"));
    board_author.selectSlide(1);
    board_author.assignPrimaryImage(board_media_path, QStringLiteral("Board two"));
    board_author.addTextItem(QStringLiteral("Board two copy"));
    board_author.selectSlide(0);
    board_author.selectSlideRange(1);
    if (!board_author.copySelectedBoards()) {
      return fail("selected boards should write a cross-document clipboard package");
    }

    CarouselDocumentController board_consumer;
    board_consumer.createCarouselWithSettings(QVariantMap{
        {QStringLiteral("carouselName"), QStringLiteral("Clipboard Board Target")},
        {QStringLiteral("documentFormat"), QStringLiteral("1x1")},
        {QStringLiteral("slideCount"), 1},
    });
    board_consumer.assignPrimaryImage(board_media_path, QStringLiteral("Existing target board"));
    if (!board_consumer.previewClipboardPaste() ||
        board_consumer.clipboardPastePreview().value(QStringLiteral("boardCount")).toInt() != 2 ||
        board_consumer.clipboardPastePreview().value(QStringLiteral("itemCount")).toInt() != 2 ||
        !board_consumer.applyClipboardPaste(QStringLiteral("preserve"), QString{}) ||
        board_consumer.slideCount() != 3 || board_consumer.selectedSlideCount() != 2) {
      return fail("reviewed board paste should insert the exact ordered board set into another document");
    }
    QSet<QString> board_item_ids;
    for (const auto& value : board_consumer.canvasItems()) {
      const auto item_id = value.toMap().value(QStringLiteral("itemId")).toString();
      if (item_id.isEmpty() || board_item_ids.contains(item_id)) {
        return fail("cross-document board items should receive unique identities");
      }
      board_item_ids.insert(item_id);
    }
    board_consumer.undo();
    if (board_consumer.slideCount() != 1 || !board_consumer.canvasItems().isEmpty()) {
      return fail("cross-document board paste should undo all inserted boards and layers in one step");
    }
    board_consumer.redo();
    const auto board_export_dir = dir.filePath(QStringLiteral("cross-document-board-export"));
    QDir().mkpath(board_export_dir);
    board_consumer.setExportManifestEnabled(false);
    if (board_consumer.slideCount() != 3 ||
        !board_consumer.saveDocument(QStringLiteral("Clipboard Board Target")) ||
        !board_consumer.exportCarouselToDirectory(board_export_dir)) {
      return fail("cross-document board paste should redo, save, and export");
    }
    CarouselDocumentController reopened_boards;
    if (!reopened_boards.loadDocument(board_consumer.savedDocumentId()) ||
        reopened_boards.slideCount() != 3 || reopened_boards.canvasItems().size() != 2) {
      return fail("cross-document pasted boards should survive save and reopen");
    }
  }

  {
    CarouselDocumentController controller;
    controller.setCurrentSlideBackgroundColor(QStringLiteral("#ff0000"));
    controller.addSlide();
    controller.setCurrentSlideBackgroundColor(QStringLiteral("#00ff00"));
    controller.addSlide();
    controller.setCurrentSlideBackgroundColor(QStringLiteral("#0000ff"));

    controller.selectSlide(0);
    const auto moved_board_id = controller.slides().at(0).toMap().value(QStringLiteral("boardId")).toString();
    controller.moveSlide(0, 2);
    auto slides = controller.slides();
    if (controller.currentIndex() != 2 ||
        slides.at(0).toMap().value(QStringLiteral("backgroundColor")).toString() != QStringLiteral("#00ff00") ||
        slides.at(1).toMap().value(QStringLiteral("backgroundColor")).toString() != QStringLiteral("#0000ff") ||
        slides.at(2).toMap().value(QStringLiteral("backgroundColor")).toString() != QStringLiteral("#ff0000") ||
        slides.at(2).toMap().value(QStringLiteral("boardId")).toString() != moved_board_id ||
        !slides.at(2).toMap().value(QStringLiteral("selected")).toBool() ||
        !slides.at(2).toMap().value(QStringLiteral("active")).toBool() ||
        slides.at(0).toMap().value(QStringLiteral("selected")).toBool() ||
        slides.at(0).toMap().value(QStringLiteral("active")).toBool()) {
      return fail("drag-style board reordering should move a board directly to its requested position");
    }
    controller.undo();
    slides = controller.slides();
    if (slides.at(0).toMap().value(QStringLiteral("backgroundColor")).toString() != QStringLiteral("#ff0000") ||
        slides.at(1).toMap().value(QStringLiteral("backgroundColor")).toString() != QStringLiteral("#00ff00") ||
        slides.at(2).toMap().value(QStringLiteral("backgroundColor")).toString() != QStringLiteral("#0000ff") ||
        controller.currentIndex() != 0 ||
        slides.at(0).toMap().value(QStringLiteral("boardId")).toString() != moved_board_id ||
        !slides.at(0).toMap().value(QStringLiteral("selected")).toBool() ||
        !slides.at(0).toMap().value(QStringLiteral("active")).toBool()) {
      return fail("direct board reordering should undo as one history entry");
    }

    controller.selectSlide(0);
    controller.toggleSlideSelection(2);
    if (controller.selectedSlideCount() != 2) {
      return fail("modifier board selection should retain multiple non-adjacent boards");
    }
    controller.duplicateSelectedSlides();
    if (controller.slideCount() != 5 || controller.selectedSlideCount() != 1) {
      return fail("duplicating selected boards should batch the operation and leave one clear active board");
    }
    controller.undo();
    if (controller.slideCount() != 3) {
      return fail("batch board duplication should undo as one history entry");
    }

    controller.selectSlide(0);
    controller.selectSlideRange(1);
    if (controller.selectedSlideCount() != 2) {
      return fail("shift-style board range selection should select every board through the target");
    }
    controller.removeSelectedSlides();
    if (controller.slideCount() != 1) {
      return fail("removing selected boards should preserve the required final board");
    }
    controller.undo();
    if (controller.slideCount() != 3) {
      return fail("batch board removal should undo as one history entry");
    }
  }

  {
    const auto item_for_component_on_board = [](const CarouselDocumentController& controller,
                                                 const QString& component_id,
                                                 int board_index) {
      for (const auto& value : controller.canvasItems()) {
        const auto item = value.toMap();
        if (item.value(QStringLiteral("componentLinkId")).toString() == component_id &&
            item.value(QStringLiteral("slideIndex")).toInt() == board_index) {
          return item;
        }
      }
      return QVariantMap{};
    };

    CarouselDocumentController controller;
    controller.createCarouselWithSettings(QVariantMap{
        {QStringLiteral("carouselName"), QStringLiteral("Guided Tour Variants")},
        {QStringLiteral("documentFormat"), QStringLiteral("4x5")},
        {QStringLiteral("slideCount"), 2},
    });
    controller.selectSlide(0);
    controller.setCurrentSlideName(QStringLiteral("Guided tour"));
    controller.addTextItem(QStringLiteral("July 10–12, 2026"));
    const auto source_date_id = controller.selectedItemId();
    if (!controller.createRepeatedElementLink(QStringLiteral("Tour dates"), QStringLiteral("all"))) {
      return fail("tour date text should be linkable across the original boards");
    }
    QString date_component_id;
    for (const auto& value : controller.canvasItems()) {
      const auto item = value.toMap();
      if (item.value(QStringLiteral("itemId")).toString() == source_date_id) {
        date_component_id = item.value(QStringLiteral("componentLinkId")).toString();
        break;
      }
    }
    const auto original_date_links = controller.repeatedElementLinks();
    if (date_component_id.isEmpty() || original_date_links.size() != 1 ||
        original_date_links.constFirst().toMap()
                .value(QStringLiteral("instanceCount")).toInt() != 2) {
      return fail("the original tour date link should expose both repeated layer instances");
    }

    if (!controller.createBoardVariant()) {
      return fail("Create variant should duplicate and name the current board");
    }
    auto slides = controller.slides();
    const auto variant_content_hash = controller.documentContentHash();
    const bool copied_board_active = controller.currentIndex() == 1 &&
        slides.at(1).toMap().value(QStringLiteral("selected")).toBool() &&
        slides.at(1).toMap().value(QStringLiteral("active")).toBool() &&
        !slides.at(0).toMap().value(QStringLiteral("selected")).toBool() &&
        !slides.at(0).toMap().value(QStringLiteral("active")).toBool();
    controller.selectSlide(0);
    const auto comparison = controller.neighborComparison();
    if (controller.slideCount() != 3 || !copied_board_active || controller.currentIndex() != 0 ||
        slides.at(1).toMap().value(QStringLiteral("boardName")).toString() !=
            QStringLiteral("Guided tour · Variant 1") ||
        comparison.value(QStringLiteral("leftBoardIndex")).toInt() != 0 ||
        comparison.value(QStringLiteral("rightBoardIndex")).toInt() != 1 ||
        comparison.value(QStringLiteral("leftName")).toString() != QStringLiteral("Guided tour") ||
        comparison.value(QStringLiteral("rightName")).toString() !=
            QStringLiteral("Guided tour · Variant 1") ||
        controller.repeatedElementLinks().constFirst().toMap()
                .value(QStringLiteral("instanceCount")).toInt() != 3 ||
        item_for_component_on_board(controller, date_component_id, 1).isEmpty()) {
      return fail("a named variant should become the active comparison neighbor and preserve shared layers");
    }
    controller.undo();
    if (controller.slideCount() != 2 ||
        controller.repeatedElementLinks().constFirst().toMap()
                .value(QStringLiteral("instanceCount")).toInt() != 2) {
      return fail("creating a named variant with linked elements should undo as one operation");
    }
    controller.redo();
    if (controller.slideCount() != 3 ||
        controller.currentIndex() != 0 ||
        controller.documentContentHash() != variant_content_hash ||
        controller.slides().at(1).toMap().value(QStringLiteral("boardName")).toString() !=
            QStringLiteral("Guided tour · Variant 1")) {
      return fail("redo should restore the exact named variant and its independent board content");
    }

    controller.selectItem(source_date_id);
    controller.setSelectedText(QStringLiteral("August 1–3, 2026"));
    const auto linked_variant_date = item_for_component_on_board(controller, date_component_id, 1);
    const auto linked_original_date = item_for_component_on_board(controller, date_component_id, 2);
    if (linked_variant_date.value(QStringLiteral("text")).toString() !=
            QStringLiteral("August 1–3, 2026") ||
        linked_original_date.value(QStringLiteral("text")).toString() !=
            QStringLiteral("August 1–3, 2026")) {
      return fail("editing shared tour dates should update the variant and every linked board");
    }

    controller.selectItem(linked_variant_date.value(QStringLiteral("itemId")).toString());
    if (!controller.detachSelectedRepeatedElement()) {
      return fail("a variant should support a local date override through Detach this board");
    }
    controller.setSelectedText(QStringLiteral("August 8–10, 2026"));
    if (item_for_component_on_board(controller, date_component_id, 0)
                .value(QStringLiteral("text")).toString() != QStringLiteral("August 1–3, 2026") ||
        item_for_component_on_board(controller, date_component_id, 2)
                .value(QStringLiteral("text")).toString() != QStringLiteral("August 1–3, 2026")) {
      return fail("a detached variant date should become a local override without changing the source");
    }
  }

  {
    const auto source = write_image(
        dir, QStringLiteral("tour-date-source.png"),
        split_image(QSize(320, 240), QColor(QStringLiteral("#182b42")), QColor(QStringLiteral("#947044"))));
    photo_editor::desktop::PhotoTextStyle source_style;
    source_style.family = QStringLiteral("Helvetica");
    source_style.size = 32.0;
    source_style.color = QColor(QStringLiteral("#fff4d8"));
    source_style.point_text = true;
    QVariantMap source_text_layer{
        {QStringLiteral("id"), QStringLiteral("tour-dates-native-text")},
        {QStringLiteral("kind"), QStringLiteral("text")},
        {QStringLiteral("text"), QStringLiteral("July 10–12, 2026")},
        {QStringLiteral("locked"), false},
        {QStringLiteral("x"), 72.0},
        {QStringLiteral("y"), 54.0},
        {QStringLiteral("pixelWidth"), 260},
        {QStringLiteral("pixelHeight"), 64},
        {QStringLiteral("textOriginX"), 0.0},
        {QStringLiteral("textOriginY"), 0.0},
        {QStringLiteral("scale"), 1.0},
        {QStringLiteral("rotation"), 0.0},
        {QStringLiteral("textTypography"),
         photo_editor::desktop::encode_photo_text_style(source_style).toVariantMap()},
    };
    if (!photo_editor::desktop::fit_photo_point_text(&source_text_layer)) {
      return fail("Native photo text fixture should have valid fitted geometry");
    }
    CarouselDocumentController::RecipeState base_state;
    base_state.exposure = 0.12;
    base_state.layers = {source_text_layer};
    const auto base_patch = photo_editor::desktop::serialize_adjustment_patch_json(
        base_state, photo_editor::desktop::all_adjustment_groups(),
        photo_editor::desktop::AdjustmentPatchScope::AssetBound);

    CarouselDocumentController controller;
    if (!controller.createBoardsFromEdit(source, QStringLiteral("Guided tour"), base_patch,
            320, 240, 1) || !controller.createBoardVariant()) {
      return fail("Native photo text should be carried into a named variant");
    }
    auto variant_payload = controller.editBoardPayload(1);
    auto variant_decoded = photo_editor::desktop::parse_adjustment_patch_json(
        variant_payload.value(QStringLiteral("adjustmentPatchJson")).toString());
    if (!variant_decoded.ok()) return fail("Variant should retain a valid independent photo recipe");
    auto variant_state = *variant_decoded.state;
    auto variant_layers = variant_state.layers;
    auto variant_text_layer = variant_layers.constFirst().toMap();
    photo_editor::desktop::PhotoTextStyle variant_style = source_style;
    variant_style.family = QStringLiteral("Helvetica");
    variant_style.size = 19.0;
    variant_style.color = QColor(QStringLiteral("#9be1ff"));
    variant_text_layer.insert(QStringLiteral("textTypography"),
        photo_editor::desktop::encode_photo_text_style(variant_style).toVariantMap());
    variant_text_layer.insert(QStringLiteral("x"), 112.0);
    variant_text_layer.insert(QStringLiteral("y"), 92.0);
    variant_text_layer.insert(QStringLiteral("scale"), 1.35);
    variant_text_layer.insert(QStringLiteral("rotation"), 13.0);
    variant_layers[0] = variant_text_layer;
    variant_state.layers = variant_layers;
    variant_state.exposure = 0.73;
    const auto variant_patch = photo_editor::desktop::serialize_adjustment_patch_json(
        variant_state, variant_decoded.groups,
        photo_editor::desktop::AdjustmentPatchScope::AssetBound);
    if (!controller.applyAdjustmentPatchToPhotoEditTarget(
            variant_payload.value(QStringLiteral("targetKey")).toString(), variant_patch)) {
      return fail("Variant-specific text styling should remain independently editable before linking");
    }

    const auto source_board_id = controller.slides().at(0).toMap()
        .value(QStringLiteral("boardId")).toString();
    const auto variant_board_id = controller.slides().at(1).toMap()
        .value(QStringLiteral("boardId")).toString();
    controller.editBoardPayload(0);
    const auto candidates = controller.photoTextLinkCandidates(QStringLiteral("tour-dates-native-text"));
    if (source_board_id.isEmpty() || variant_board_id.isEmpty() ||
        source_board_id == variant_board_id || candidates.size() != 2 ||
        controller.photoTextLinkState(QStringLiteral("tour-dates-native-text"))
                .value(QStringLiteral("linked")).toBool()) {
      return fail("Matching native layer IDs should remain unlinked until an explicit board selection");
    }
    if (!controller.linkEditBoardPhotoText(QStringLiteral("tour-dates-native-text"),
            QStringLiteral("Tour dates"), {variant_board_id})) {
      return fail("An explicit selection should link matching unlocked native text layers");
    }
    variant_payload = controller.editBoardPayload(1);
    variant_decoded = photo_editor::desktop::parse_adjustment_patch_json(
        variant_payload.value(QStringLiteral("adjustmentPatchJson")).toString());
    if (!variant_decoded.ok()) return fail("Linked variant should remain editable before testing atomic sync");
    variant_state = *variant_decoded.state;
    variant_layers = variant_state.layers;
    variant_text_layer = variant_layers.constFirst().toMap();
    variant_text_layer.insert(QStringLiteral("locked"), true);
    variant_layers[0] = variant_text_layer;
    variant_state.layers = variant_layers;
    const auto locked_variant_patch = photo_editor::desktop::serialize_adjustment_patch_json(
        variant_state, variant_decoded.groups,
        photo_editor::desktop::AdjustmentPatchScope::AssetBound);
    if (!controller.applyAdjustmentPatchToPhotoEditTarget(
            variant_payload.value(QStringLiteral("targetKey")).toString(), locked_variant_patch)) {
      return fail("A linked peer should be lockable before validating shared-edit rejection");
    }
    auto source_payload = controller.editBoardPayload(0);
    auto source_decoded = photo_editor::desktop::parse_adjustment_patch_json(
        source_payload.value(QStringLiteral("adjustmentPatchJson")).toString());
    if (!source_decoded.ok()) return fail("Source should retain a valid photo recipe");
    auto shared_source_state = *source_decoded.state;
    auto shared_source_layer = shared_source_state.layers.constFirst().toMap();
    shared_source_layer.insert(QStringLiteral("text"), QStringLiteral("July 18–20, 2026"));
    auto shared_source_layers = shared_source_state.layers;
    shared_source_layers[0] = shared_source_layer;
    shared_source_state.layers = shared_source_layers;
    shared_source_state.exposure = -0.22;
    const auto shared_source_patch = photo_editor::desktop::serialize_adjustment_patch_json(
        shared_source_state, source_decoded.groups,
        photo_editor::desktop::AdjustmentPatchScope::AssetBound);
    const auto content_hash_before_rejected_sync = controller.documentContentHash();
    if (controller.applyAdjustmentPatchToPhotoEditTarget(
            source_payload.value(QStringLiteral("targetKey")).toString(), shared_source_patch) ||
        controller.documentContentHash() != content_hash_before_rejected_sync ||
        !controller.status().contains(QStringLiteral("Unlock the matching text"))) {
      return fail("A locked linked peer should reject the source edit atomically with a clear reason");
    }
    variant_payload = controller.editBoardPayload(1);
    variant_decoded = photo_editor::desktop::parse_adjustment_patch_json(
        variant_payload.value(QStringLiteral("adjustmentPatchJson")).toString());
    if (!variant_decoded.ok()) return fail("Locked linked variant should remain a valid recipe after rejection");
    variant_state = *variant_decoded.state;
    variant_layers = variant_state.layers;
    variant_text_layer = variant_layers.constFirst().toMap();
    variant_text_layer.insert(QStringLiteral("locked"), false);
    variant_layers[0] = variant_text_layer;
    variant_state.layers = variant_layers;
    const auto unlocked_variant_patch = photo_editor::desktop::serialize_adjustment_patch_json(
        variant_state, variant_decoded.groups,
        photo_editor::desktop::AdjustmentPatchScope::AssetBound);
    if (!controller.applyAdjustmentPatchToPhotoEditTarget(
            variant_payload.value(QStringLiteral("targetKey")).toString(), unlocked_variant_patch)) {
      return fail("The linked peer should accept an explicit unlock before a shared edit");
    }
    if (!controller.applyAdjustmentPatchToPhotoEditTarget(
            source_payload.value(QStringLiteral("targetKey")).toString(), shared_source_patch)) {
      return fail("Editing linked source text should commit through the normal Edit return path");
    }

    variant_payload = controller.editBoardPayload(1);
    variant_decoded = photo_editor::desktop::parse_adjustment_patch_json(
        variant_payload.value(QStringLiteral("adjustmentPatchJson")).toString());
    if (!variant_decoded.ok()) return fail("Linked variant should remain a valid photo recipe");
    const auto& linked_variant_state = *variant_decoded.state;
    const auto linked_variant_layer = linked_variant_state.layers.constFirst().toMap();
    const auto link_state = controller.photoTextLinkState(QStringLiteral("tour-dates-native-text"));
    if (linked_variant_layer.value(QStringLiteral("text")).toString() != QStringLiteral("July 18–20, 2026") ||
        linked_variant_layer.value(QStringLiteral("id")).toString() != QStringLiteral("tour-dates-native-text") ||
        linked_variant_layer.value(QStringLiteral("textTypography")).toMap() !=
            photo_editor::desktop::encode_photo_text_style(variant_style).toVariantMap() ||
        linked_variant_layer.value(QStringLiteral("scale")).toDouble() != 1.35 ||
        linked_variant_layer.value(QStringLiteral("rotation")).toDouble() != 13.0 ||
        linked_variant_state.exposure != 0.73 ||
        !link_state.value(QStringLiteral("linked")).toBool() ||
        link_state.value(QStringLiteral("name")).toString() != QStringLiteral("Tour dates") ||
        link_state.value(QStringLiteral("boardNumbers")).toList() != QVariantList{1, 2}) {
      return fail("Shared edits should merge text only while preserving the peer recipe, style, transform, and layer ID");
    }

    variant_payload = controller.editBoardPayload(1);
    variant_decoded = photo_editor::desktop::parse_adjustment_patch_json(
        variant_payload.value(QStringLiteral("adjustmentPatchJson")).toString());
    if (!variant_decoded.ok()) return fail("Linked variant should remain editable after source fanout");
    variant_state = *variant_decoded.state;
    variant_layers = variant_state.layers;
    variant_text_layer = variant_layers.constFirst().toMap();
    variant_text_layer.insert(QStringLiteral("text"), QStringLiteral("July 25–27, 2026"));
    variant_layers[0] = variant_text_layer;
    variant_state.layers = variant_layers;
    const auto linked_variant_patch = photo_editor::desktop::serialize_adjustment_patch_json(
        variant_state, variant_decoded.groups,
        photo_editor::desktop::AdjustmentPatchScope::AssetBound);
    if (!controller.applyAdjustmentPatchToPhotoEditTarget(
            variant_payload.value(QStringLiteral("targetKey")).toString(), linked_variant_patch)) {
      return fail("Editing linked variant text should update the source through the same return path");
    }
    source_payload = controller.editBoardPayload(0);
    source_decoded = photo_editor::desktop::parse_adjustment_patch_json(
        source_payload.value(QStringLiteral("adjustmentPatchJson")).toString());
    variant_payload = controller.editBoardPayload(1);
    variant_decoded = photo_editor::desktop::parse_adjustment_patch_json(
        variant_payload.value(QStringLiteral("adjustmentPatchJson")).toString());
    if (!source_decoded.ok() || !variant_decoded.ok() ||
        source_decoded.state->layers.constFirst().toMap().value(QStringLiteral("text")).toString() !=
            QStringLiteral("July 25–27, 2026") ||
        variant_decoded.state->layers.constFirst().toMap().value(QStringLiteral("text")).toString() !=
            QStringLiteral("July 25–27, 2026") ||
        variant_decoded.state->layers.constFirst().toMap().value(QStringLiteral("textTypography")).toMap() !=
            photo_editor::desktop::encode_photo_text_style(variant_style).toVariantMap() ||
        variant_decoded.state->layers.constFirst().toMap().value(QStringLiteral("scale")).toDouble() != 1.35 ||
        variant_decoded.state->layers.constFirst().toMap().value(QStringLiteral("rotation")).toDouble() != 13.0 ||
        variant_decoded.state->exposure != 0.73) {
      return fail("Variant-originated shared edits should fan out while retaining each board's local styling");
    }
    controller.undo();
    auto undone_source = photo_editor::desktop::parse_adjustment_patch_json(
        controller.editBoardPayload(0).value(QStringLiteral("adjustmentPatchJson")).toString());
    auto undone_variant = photo_editor::desktop::parse_adjustment_patch_json(
        controller.editBoardPayload(1).value(QStringLiteral("adjustmentPatchJson")).toString());
    if (!undone_source.ok() || !undone_variant.ok() ||
        undone_source.state->layers.constFirst().toMap().value(QStringLiteral("text")).toString() !=
            QStringLiteral("July 18–20, 2026") ||
        undone_variant.state->layers.constFirst().toMap().value(QStringLiteral("text")).toString() !=
            QStringLiteral("July 18–20, 2026") ||
        !controller.photoTextLinkState(QStringLiteral("tour-dates-native-text"))
             .value(QStringLiteral("linked")).toBool()) {
      return fail("One undo should restore both sides of a variant text edit while retaining link metadata");
    }
    controller.redo();
    const auto redone_source = photo_editor::desktop::parse_adjustment_patch_json(
        controller.editBoardPayload(0).value(QStringLiteral("adjustmentPatchJson")).toString());
    const auto redone_variant = photo_editor::desktop::parse_adjustment_patch_json(
        controller.editBoardPayload(1).value(QStringLiteral("adjustmentPatchJson")).toString());
    if (!redone_source.ok() || !redone_variant.ok() ||
        redone_source.state->layers.constFirst().toMap().value(QStringLiteral("text")).toString() !=
            QStringLiteral("July 25–27, 2026") ||
        redone_variant.state->layers.constFirst().toMap().value(QStringLiteral("text")).toString() !=
            QStringLiteral("July 25–27, 2026")) {
      return fail("Redo should reapply the linked variant text edit to every member");
    }
    if (!controller.saveDocument(QStringLiteral("Shared tour dates"))) {
      return fail("Linked native text and board recipes should save together");
    }
    CarouselDocumentController reopened;
    if (!reopened.loadDocument(controller.savedDocumentId())) {
      return fail("Linked native text document should reopen");
    }
    reopened.selectSlide(0);
    if (!reopened.photoTextLinkState(QStringLiteral("tour-dates-native-text"))
             .value(QStringLiteral("linked")).toBool()) {
      return fail("Explicit shared text links should survive save and reopen");
    }
    const auto reopened_source = photo_editor::desktop::parse_adjustment_patch_json(
        reopened.editBoardPayload(0).value(QStringLiteral("adjustmentPatchJson")).toString());
    const auto reopened_variant = photo_editor::desktop::parse_adjustment_patch_json(
        reopened.editBoardPayload(1).value(QStringLiteral("adjustmentPatchJson")).toString());
    if (!reopened_source.ok() || !reopened_variant.ok() ||
        reopened_source.state->layers.constFirst().toMap().value(QStringLiteral("text")).toString() !=
            QStringLiteral("July 25–27, 2026") ||
        reopened_variant.state->layers.constFirst().toMap().value(QStringLiteral("text")).toString() !=
            QStringLiteral("July 25–27, 2026") ||
        reopened_variant.state->layers.constFirst().toMap().value(QStringLiteral("id")).toString() !=
            QStringLiteral("tour-dates-native-text") || reopened_variant.state->exposure != 0.73) {
      return fail("Reopen should retain shared text plus the peer's own photo recipe");
    }
    if (!reopened.detachEditBoardPhotoText(QStringLiteral("tour-dates-native-text")) ||
        reopened.photoTextLinkState(QStringLiteral("tour-dates-native-text"))
                .value(QStringLiteral("linked")).toBool()) {
      return fail("A board should detach its native text into a local override");
    }
    auto local_payload = reopened.editBoardPayload(1);
    auto local_decoded = photo_editor::desktop::parse_adjustment_patch_json(
        local_payload.value(QStringLiteral("adjustmentPatchJson")).toString());
    auto local_state = *local_decoded.state;
    auto local_layer = local_state.layers.constFirst().toMap();
    local_layer.insert(QStringLiteral("text"), QStringLiteral("August 1–3, 2026"));
    auto local_layers = local_state.layers;
    local_layers[0] = local_layer;
    local_state.layers = local_layers;
    const auto local_patch = photo_editor::desktop::serialize_adjustment_patch_json(
        local_state, local_decoded.groups,
        photo_editor::desktop::AdjustmentPatchScope::AssetBound);
    if (!reopened.applyAdjustmentPatchToPhotoEditTarget(
            local_payload.value(QStringLiteral("targetKey")).toString(), local_patch)) {
      return fail("Detached native text should accept a board-local override");
    }
    auto detached_source_payload = reopened.editBoardPayload(0);
    auto detached_source_decoded = photo_editor::desktop::parse_adjustment_patch_json(
        detached_source_payload.value(QStringLiteral("adjustmentPatchJson")).toString());
    auto detached_source_state = *detached_source_decoded.state;
    auto detached_source_layer = detached_source_state.layers.constFirst().toMap();
    detached_source_layer.insert(QStringLiteral("text"), QStringLiteral("August 8–10, 2026"));
    auto detached_source_layers = detached_source_state.layers;
    detached_source_layers[0] = detached_source_layer;
    detached_source_state.layers = detached_source_layers;
    const auto detached_source_patch = photo_editor::desktop::serialize_adjustment_patch_json(
        detached_source_state, detached_source_decoded.groups,
        photo_editor::desktop::AdjustmentPatchScope::AssetBound);
    if (!reopened.applyAdjustmentPatchToPhotoEditTarget(
            detached_source_payload.value(QStringLiteral("targetKey")).toString(), detached_source_patch) ||
        !reopened.saveDocument()) {
      return fail("Detached boards should save independently editable text");
    }
    CarouselDocumentController detached_reopened;
    if (!detached_reopened.loadDocument(reopened.savedDocumentId())) {
      return fail("Detached local text should reopen");
    }
    const auto final_source = photo_editor::desktop::parse_adjustment_patch_json(
        detached_reopened.editBoardPayload(0).value(QStringLiteral("adjustmentPatchJson")).toString());
    const auto final_variant = photo_editor::desktop::parse_adjustment_patch_json(
        detached_reopened.editBoardPayload(1).value(QStringLiteral("adjustmentPatchJson")).toString());
    if (!final_source.ok() || !final_variant.ok() ||
        !detached_reopened.linkedPhotoTextGroups().isEmpty() ||
        final_source.state->layers.constFirst().toMap().value(QStringLiteral("text")).toString() !=
            QStringLiteral("August 8–10, 2026") ||
        final_variant.state->layers.constFirst().toMap().value(QStringLiteral("text")).toString() !=
            QStringLiteral("August 1–3, 2026")) {
      return fail("Saved detached text should remain a local override after later source edits");
    }
  }

  {
    const auto reorder_source = write_image(
        dir, QStringLiteral("selected-board-reorder.png"),
        split_image(QSize(320, 240), QColor(QStringLiteral("#263649")), QColor(QStringLiteral("#8a5b3c"))));
    const auto reorder_patch = photo_editor::desktop::serialize_adjustment_patch_json(
        CarouselDocumentController::RecipeState{}, photo_editor::desktop::all_adjustment_groups(),
        photo_editor::desktop::AdjustmentPatchScope::AssetBound);
    CarouselDocumentController controller;
    if (reorder_source.isEmpty() ||
        !controller.createBoardsFromEdit(reorder_source, QStringLiteral("Selected board reorder"),
            reorder_patch, 320, 240, 2) ||
        !controller.addEditBoard(false) || !controller.addEditBoard(false)) {
      return fail("Edit-ready reorder fixture should create four persistent board identities");
    }
    int slides_notifications = 0;
    int selection_notifications = 0;
    QString last_notified_active_board_id;
    QObject::connect(&controller, &CarouselDocumentController::slidesChanged,
        &controller, [&slides_notifications]() { ++slides_notifications; });
    QObject::connect(&controller, &CarouselDocumentController::selectionChanged,
        &controller, [&controller, &selection_notifications, &last_notified_active_board_id]() {
          ++selection_notifications;
          const auto current = controller.currentIndex();
          const auto current_slides = controller.slides();
          if (current >= 0 && current < current_slides.size()) {
            last_notified_active_board_id = current_slides.at(current).toMap()
                .value(QStringLiteral("boardId")).toString();
          }
        });
    QStringList original_board_ids;
    for (const auto& value : controller.slides()) {
      original_board_ids.push_back(value.toMap().value(QStringLiteral("boardId")).toString());
    }
    QSet<QString> unique_board_ids;
    for (const auto& board_id : original_board_ids) unique_board_ids.insert(board_id);
    if (original_board_ids.size() != 4 || unique_board_ids.size() != 4 ||
        std::any_of(original_board_ids.cbegin(), original_board_ids.cend(),
            [](const QString& board_id) { return board_id.isEmpty(); })) {
      return fail("Edit-ready boards should each have a unique stable identity before reordering");
    }
    controller.selectSlide(1);
    controller.selectSlideRange(2);
    const auto selected_notifications_before_move = selection_notifications;
    const auto slide_notifications_before_move = slides_notifications;
    if (selected_notifications_before_move < 2 ||
        last_notified_active_board_id != original_board_ids.at(2)) {
      return fail("Selecting a board range should notify the active board identity before moving it");
    }
    controller.moveSlide(1, 3);
    auto moved_slides = controller.slides();
    if (moved_slides.at(0).toMap().value(QStringLiteral("boardId")).toString() != original_board_ids.at(0) ||
        moved_slides.at(1).toMap().value(QStringLiteral("boardId")).toString() != original_board_ids.at(3) ||
        moved_slides.at(2).toMap().value(QStringLiteral("boardId")).toString() != original_board_ids.at(1) ||
        moved_slides.at(3).toMap().value(QStringLiteral("boardId")).toString() != original_board_ids.at(2) ||
        !moved_slides.at(2).toMap().value(QStringLiteral("selected")).toBool() ||
        !moved_slides.at(3).toMap().value(QStringLiteral("selected")).toBool() ||
        controller.currentIndex() != 3 ||
        selection_notifications <= selected_notifications_before_move ||
        slides_notifications <= slide_notifications_before_move ||
        last_notified_active_board_id != original_board_ids.at(2)) {
      return fail("Moving a selected board range should preserve its order, active identity, and outline");
    }
    const auto selected_notifications_after_move = selection_notifications;
    const auto slide_notifications_after_move = slides_notifications;
    controller.undo();
    const auto undone_slides = controller.slides();
    if (undone_slides.at(1).toMap().value(QStringLiteral("boardId")).toString() != original_board_ids.at(1) ||
        undone_slides.at(2).toMap().value(QStringLiteral("boardId")).toString() != original_board_ids.at(2) ||
        !undone_slides.at(1).toMap().value(QStringLiteral("selected")).toBool() ||
        !undone_slides.at(2).toMap().value(QStringLiteral("selected")).toBool() ||
        controller.currentIndex() != 2 ||
        selection_notifications <= selected_notifications_after_move ||
        slides_notifications <= slide_notifications_after_move ||
        last_notified_active_board_id != original_board_ids.at(2)) {
      return fail("Undo should restore selected board identities after a multi-board reorder");
    }
    const auto selected_notifications_after_undo = selection_notifications;
    const auto slide_notifications_after_undo = slides_notifications;
    controller.redo();
    const auto redone_slides = controller.slides();
    if (redone_slides.at(2).toMap().value(QStringLiteral("boardId")).toString() != original_board_ids.at(1) ||
        redone_slides.at(3).toMap().value(QStringLiteral("boardId")).toString() != original_board_ids.at(2) ||
        !redone_slides.at(2).toMap().value(QStringLiteral("selected")).toBool() ||
        !redone_slides.at(3).toMap().value(QStringLiteral("selected")).toBool() ||
        controller.currentIndex() != 3 ||
        selection_notifications <= selected_notifications_after_undo ||
        slides_notifications <= slide_notifications_after_undo ||
        last_notified_active_board_id != original_board_ids.at(2)) {
      return fail("Redo should restore the selected board identities after a multi-board reorder");
    }
  }

  {
    CarouselDocumentController controller;
    QVariantList stroke_points;
    for (int index = 0; index < 5000; ++index) {
      stroke_points.push_back(QVariantMap{
          {QStringLiteral("x"), 240.0 + static_cast<double>(index)},
          {QStringLiteral("y"), 675.0},
      });
    }
    controller.addBrushStroke(
        stroke_points, QStringLiteral("#ff453a"), 48.0, 1.0, QStringLiteral("marker"), false);
    const auto brush_items = controller.canvasItems();
    if (brush_items.size() != 1 ||
        brush_items.constFirst().toMap().value(QStringLiteral("type")).toString() != QStringLiteral("brush") ||
        brush_items.constFirst().toMap().value(QStringLiteral("brushPoints")).toList().size() > 2048) {
      return fail("brush strokes should be editable bounded vector layers");
    }

    const auto brush_export_path = dir.filePath(QStringLiteral("export-brush-stroke"));
    QDir().mkpath(brush_export_path);
    if (!controller.exportCarouselToDirectory(brush_export_path)) {
      return fail("brush strokes should export");
    }
    QImage brush_export(QDir(brush_export_path).filePath(QStringLiteral("carousel-01.png")));
    if (!color_matches(brush_export.pixelColor(540, 675), QColor(QStringLiteral("#ff453a")))) {
      return fail("brush export should render the selected stroke color");
    }

    QVariantList eraser_points{
        QVariantMap{{QStringLiteral("x"), 240.0}, {QStringLiteral("y"), 675.0}},
        QVariantMap{{QStringLiteral("x"), 840.0}, {QStringLiteral("y"), 675.0}},
    };
    controller.addBrushStroke(
        eraser_points, QStringLiteral("#000000"), 96.0, 1.0, QStringLiteral("marker"), true);
    if (!controller.canvasItems().constLast().toMap().value(QStringLiteral("brushEraser")).toBool()) {
      return fail("eraser strokes should retain their non-destructive eraser role");
    }
    const auto erased_export_path = dir.filePath(QStringLiteral("export-brush-erased"));
    QDir().mkpath(erased_export_path);
    if (!controller.exportCarouselToDirectory(erased_export_path)) {
      return fail("non-destructive eraser strokes should export");
    }
    brush_export = QImage(QDir(erased_export_path).filePath(QStringLiteral("carousel-01.png")));
    if (color_matches(brush_export.pixelColor(540, 675), QColor(QStringLiteral("#ff453a"))) ||
        !color_matches(brush_export.pixelColor(20, 20), QColor(QStringLiteral("#fbf7f1")))) {
      return fail("eraser strokes should remove brush color without erasing unaffected board pixels");
    }
    controller.undo();
    if (controller.canvasItems().size() != 1) {
      return fail("eraser strokes should undo as one non-destructive layer action");
    }
  }

  {
    CarouselDocumentController controller;
    controller.addTextItem(QStringLiteral("creative tools"));
    controller.setSelectedTextUnderline(true);
    controller.setSelectedTextStrikethrough(true);
    controller.setSelectedTextCase(QStringLiteral("uppercase"));
    controller.setSelectedTextPadding(24.0);
    controller.setSelectedTextBackgroundRadius(18.0);
    auto selection = controller.selectionDetails();
    if (!selection.value(QStringLiteral("textUnderline")).toBool() ||
        !selection.value(QStringLiteral("textStrikethrough")).toBool() ||
        selection.value(QStringLiteral("textCase")).toString() != QStringLiteral("uppercase") ||
        qAbs(selection.value(QStringLiteral("textPadding")).toDouble() - 24.0) > 0.01 ||
        qAbs(selection.value(QStringLiteral("textBackgroundRadius")).toDouble() - 18.0) > 0.01) {
      return fail("advanced text styling should be exposed through selection details");
    }

    CarouselDocumentController text_layout_controller;
    text_layout_controller.addTextItem(
        QStringLiteral("A deliberate first paragraph with enough words to wrap.\nA second paragraph also wraps."));
    text_layout_controller.setSelectedTextFrameSize(360.0, 150.0);
    const auto text_layout_baseline_dir = dir.filePath(QStringLiteral("text-layout-baseline"));
    const auto text_layout_configured_dir = dir.filePath(QStringLiteral("text-layout-configured"));
    QDir().mkpath(text_layout_baseline_dir);
    QDir().mkpath(text_layout_configured_dir);
    if (!text_layout_controller.exportCarouselToDirectory(text_layout_baseline_dir)) {
      return fail("text layout baseline should export");
    }
    text_layout_controller.setSelectedTextLineHeight(1.6);
    text_layout_controller.setSelectedTextParagraphSpacing(28.0);
    text_layout_controller.setSelectedTextInset(22.0);
    text_layout_controller.setSelectedTextMaximumLineCount(2);
    selection = text_layout_controller.selectionDetails();
    const auto configured_item = text_layout_controller.canvasItems().constFirst().toMap();
    if (qAbs(selection.value(QStringLiteral("textLineHeight")).toDouble() - 1.6) > 0.001 ||
        qAbs(selection.value(QStringLiteral("textParagraphSpacing")).toDouble() - 28.0) > 0.001 ||
        qAbs(selection.value(QStringLiteral("textInset")).toDouble() - 22.0) > 0.001 ||
        selection.value(QStringLiteral("textMaximumLineCount")).toInt() != 2 ||
        !selection.value(QStringLiteral("textVerticalOverflow")).toBool() ||
        configured_item.value(QStringLiteral("textLayoutLines")).toList().size() != 2) {
      return fail("shared text layout should expose line height, paragraph spacing, inset, maximum lines, and overflow");
    }
    if (!text_layout_controller.exportCarouselToDirectory(text_layout_configured_dir)) {
      return fail("configured text layout should export");
    }
    QFile text_layout_baseline(
        QDir(text_layout_baseline_dir).filePath(QStringLiteral("carousel-01.png")));
    QFile text_layout_configured(
        QDir(text_layout_configured_dir).filePath(QStringLiteral("carousel-01.png")));
    if (!text_layout_baseline.open(QIODevice::ReadOnly) ||
        !text_layout_configured.open(QIODevice::ReadOnly) ||
        text_layout_baseline.readAll() == text_layout_configured.readAll()) {
      return fail("text layout controls should change actual exported pixels");
    }
    text_layout_controller.undo();
    if (text_layout_controller.selectionDetails()
            .value(QStringLiteral("textMaximumLineCount"))
            .toInt() != 0) {
      return fail("maximum line count should undo as one text layout edit");
    }
    text_layout_controller.setSelectedTextMaximumLineCount(2);
    if (!text_layout_controller.saveDocument(QStringLiteral("Text Layout Persistence"))) {
      return fail("carousel documents with shared text layout settings should save");
    }
    CarouselDocumentController restored_text_layout;
    if (!restored_text_layout.loadDocument(text_layout_controller.savedDocumentId())) {
      return fail("carousel documents with shared text layout settings should load");
    }
    const auto restored_text_layout_item = restored_text_layout.canvasItems().constFirst().toMap();
    if (qAbs(restored_text_layout_item.value(QStringLiteral("textLineHeight")).toDouble() - 1.6) > 0.001 ||
        qAbs(restored_text_layout_item.value(QStringLiteral("textParagraphSpacing")).toDouble() - 28.0) > 0.001 ||
        qAbs(restored_text_layout_item.value(QStringLiteral("textInset")).toDouble() - 22.0) > 0.001 ||
        restored_text_layout_item.value(QStringLiteral("textMaximumLineCount")).toInt() != 2 ||
        !restored_text_layout_item.value(QStringLiteral("textVerticalOverflow")).toBool()) {
      return fail("saved carousel documents should restore shared text layout and overflow state");
    }

    CarouselDocumentController text_role_controller;
    text_role_controller.setCurrentSlideBackgroundColor(QStringLiteral("#101418"));
    text_role_controller.addTextItemWithRole(QStringLiteral("title"));
    const auto title_selection = text_role_controller.selectionDetails();
    const auto title_size = title_selection.value(QStringLiteral("textPointSize")).toDouble();
    if (title_selection.value(QStringLiteral("textRole")).toString() != QStringLiteral("title") ||
        title_selection.value(QStringLiteral("text")).toString() != QStringLiteral("Title") ||
        title_selection.value(QStringLiteral("textWeight")).toString() != QStringLiteral("black") ||
        title_selection.value(QStringLiteral("textColor")).toString() != QStringLiteral("#ffffff")) {
      return fail("semantic title insertion should use role and board-aware defaults");
    }
    text_role_controller.addTextItemWithRole(QStringLiteral("caption"));
    auto caption_selection = text_role_controller.selectionDetails();
    if (caption_selection.value(QStringLiteral("textRole")).toString() != QStringLiteral("caption") ||
        caption_selection.value(QStringLiteral("text")).toString() != QStringLiteral("Caption") ||
        caption_selection.value(QStringLiteral("textPointSize")).toDouble() >= title_size) {
      return fail("semantic text roles should use a functional typographic hierarchy");
    }
    text_role_controller.setSelectedTextRole(QStringLiteral("attribution"));
    if (text_role_controller.selectionDetails().value(QStringLiteral("textRole")).toString() !=
        QStringLiteral("attribution")) {
      return fail("selected text layers should support explicit semantic role changes");
    }
    text_role_controller.undo();
    if (text_role_controller.selectionDetails().value(QStringLiteral("textRole")).toString() !=
        QStringLiteral("caption")) {
      return fail("semantic text role changes should undo as one document edit");
    }
    if (!text_role_controller.saveDocument(QStringLiteral("Semantic Text Roles"))) {
      return fail("carousel documents with semantic text roles should save");
    }
    CarouselDocumentController restored_text_roles;
    if (!restored_text_roles.loadDocument(text_role_controller.savedDocumentId())) {
      return fail("carousel documents with semantic text roles should load");
    }
    const auto restored_role_items = restored_text_roles.canvasItems();
    if (restored_role_items.size() != 2 ||
        restored_role_items.at(0).toMap().value(QStringLiteral("textRole")).toString() != QStringLiteral("title") ||
        restored_role_items.at(1).toMap().value(QStringLiteral("textRole")).toString() != QStringLiteral("caption")) {
      return fail("saved carousel documents should restore semantic text roles");
    }

    controller.addShapeItem(QStringLiteral("star"));
    if (controller.selectionDetails().value(QStringLiteral("shapeKind")).toString() != QStringLiteral("star")) {
      return fail("star should be available as an editable vector shape");
    }
    controller.setSelectedShapeKind(QStringLiteral("hexagon"));
    if (controller.selectionDetails().value(QStringLiteral("shapeKind")).toString() != QStringLiteral("hexagon")) {
      return fail("new vector shape kinds should be interchangeable");
    }

    CarouselDocumentController shape_controller;
    shape_controller.addShapeItem(QStringLiteral("star"));
    shape_controller.setSelectedShapeFillColor(QStringLiteral("#ff453a"));
    const auto shape_export_path = dir.filePath(QStringLiteral("export-star-shape"));
    QDir().mkpath(shape_export_path);
    if (!shape_controller.exportCarouselToDirectory(shape_export_path)) {
      return fail("new vector shapes should export successfully");
    }
    const QImage star_export(QDir(shape_export_path).filePath(QStringLiteral("carousel-01.png")));
    if (!color_matches(star_export.pixelColor(star_export.width() / 2, star_export.height() / 2),
                       QColor(QStringLiteral("#ff453a")))) {
      return fail("star shape export should render its filled center");
    }

    CarouselDocumentController template_controller;
    template_controller.applyDesignPreset(QStringLiteral("cover"));
    if (template_controller.canvasItems().size() != 2 || !template_controller.canUndo() ||
        template_controller.canvasItems().at(0).toMap().value(QStringLiteral("textRole")).toString() !=
            QStringLiteral("title") ||
        template_controller.canvasItems().at(1).toMap().value(QStringLiteral("textRole")).toString() !=
            QStringLiteral("subtitle")) {
      return fail("cover template should create editable layers in one undoable action");
    }
    template_controller.undo();
    if (!template_controller.canvasItems().isEmpty()) {
      return fail("one undo should remove every layer created by a design template");
    }
    template_controller.applyDesignPreset(QStringLiteral("cta"));
    if (template_controller.canvasItems().size() != 3) {
      return fail("CTA template should create editable headline, button shape, and label layers");
    }
  }

  {
    CarouselDocumentController catalog;
    const auto sequence_templates = catalog.sequenceTemplates();
    if (sequence_templates.size() != 8) {
      return fail("the authored sequence library should expose eight complete production packs");
    }
    const std::array<QString, 8> template_ids = {
        QStringLiteral("photo-essay"), QStringLiteral("product-launch"),
        QStringLiteral("tutorial"), QStringLiteral("before-after"),
        QStringLiteral("listicle"), QStringLiteral("portfolio-editorial"),
        QStringLiteral("announcement-event"), QStringLiteral("thought-leadership")};
    const std::array<int, 8> expected_board_counts = {7, 8, 8, 6, 8, 7, 8, 8};
    const std::array<int, 8> expected_media_counts = {7, 9, 9, 7, 9, 10, 9, 9};
    const std::array<QString, 8> expected_last_roles = {
        QStringLiteral("cta"), QStringLiteral("cta"), QStringLiteral("cta"),
        QStringLiteral("cta"), QStringLiteral("cta"), QStringLiteral("cta"),
        QStringLiteral("cta"), QStringLiteral("cta")};

    QImage template_media(QSize(96, 96), QImage::Format_ARGB32);
    template_media.fill(QColor(QStringLiteral("#5b8def")));
    const auto template_media_path = write_image(dir, QStringLiteral("sequence-template-media.png"), template_media);
    if (template_media_path.isEmpty()) {
      return fail("sequence template media fixture should save");
    }

    for (std::size_t template_index = 0; template_index < template_ids.size(); ++template_index) {
      const auto metadata = sequence_templates.at(static_cast<qsizetype>(template_index)).toMap();
      if (metadata.value(QStringLiteral("templateId")).toString() != template_ids.at(template_index) ||
          metadata.value(QStringLiteral("boardCount")).toInt() != expected_board_counts.at(template_index) ||
          metadata.value(QStringLiteral("requiredMedia")).toInt() != expected_media_counts.at(template_index) ||
          metadata.value(QStringLiteral("supportedRatios")).toString() !=
              QStringLiteral("4:5 · 1:1 · 9:16 · 5:4") ||
          metadata.value(QStringLiteral("previewRoles")).toList().size() != expected_board_counts.at(template_index)) {
        return fail("sequence cards should expose exact board, media, ratio, purpose, and full-preview metadata");
      }
      if ((template_ids.at(template_index) == QStringLiteral("listicle") &&
           (metadata.value(QStringLiteral("packVersion")).toInt() != 2 ||
            metadata.value(QStringLiteral("changesSincePrevious")).toList().size() != 3)) ||
          (template_ids.at(template_index) != QStringLiteral("listicle") &&
           metadata.value(QStringLiteral("packVersion")).toInt() != 1) ||
          metadata.value(QStringLiteral("updatePolicy")).toString() !=
              QStringLiteral("preview-explicit-preserve-overrides")) {
        return fail("sequence metadata should expose stable pack versions and explicit non-destructive update policy");
      }

      CarouselDocumentController sequence;
      const auto before_hash = sequence.documentContentHash();
      if (!sequence.applySequenceTemplate(template_ids.at(template_index)) ||
          sequence.slideCount() != expected_board_counts.at(template_index) ||
          sequence.canvasItems().size() < expected_board_counts.at(template_index) * 4 ||
          sequence.slides().constLast().toMap().value(QStringLiteral("boardRole")).toString() !=
              expected_last_roles.at(template_index) ||
          sequence.undoText() != QStringLiteral("Apply sequence template")) {
        return fail("each sequence pack should atomically create its complete editable board and role structure");
      }
      const auto authored_hash = sequence.documentContentHash();
      int component_link_count = 0;
      for (const auto& item_value : sequence.canvasItems()) {
        if (!item_value.toMap().value(QStringLiteral("componentLinkId")).toString().isEmpty()) {
          ++component_link_count;
        }
      }
      if (component_link_count < expected_board_counts.at(template_index) * 3) {
        return fail("sequence packs should create persisted identities for their repeated editable components");
      }
      if (template_ids.at(template_index) == QStringLiteral("photo-essay") &&
          (sequence.slides().at(2).toMap().value(QStringLiteral("groupKind")).toString() !=
               QStringLiteral("linked-horizontal-pair") ||
           sequence.slides().at(3).toMap().value(QStringLiteral("groupKind")).toString() !=
               QStringLiteral("linked-horizontal-pair"))) {
        return fail("photo essay should include a real two-board linked media component");
      }
      const auto authored_items = sequence.canvasItems();
      const auto authored_slides = sequence.slides();

      sequence.undo();
      if (sequence.slideCount() != 1 || !sequence.canvasItems().isEmpty() ||
          sequence.documentContentHash() != before_hash) {
        return fail("one undo should restore the entire document replaced by a sequence pack");
      }
      sequence.redo();
      if (sequence.documentContentHash() != authored_hash) {
        const auto redone_items = sequence.canvasItems();
        const auto redone_slides = sequence.slides();
        for (int item_index = 0; item_index < qMin(authored_items.size(), redone_items.size()); ++item_index) {
          if (authored_items.at(item_index).toMap() != redone_items.at(item_index).toMap()) {
            std::cerr << "sequence redo changed item " << item_index << " before="
                      << QJsonDocument::fromVariant(authored_items.at(item_index)).toJson(QJsonDocument::Compact).toStdString()
                      << " after="
                      << QJsonDocument::fromVariant(redone_items.at(item_index)).toJson(QJsonDocument::Compact).toStdString()
                      << '\n';
            break;
          }
        }
        for (int slide_index = 0; slide_index < qMin(authored_slides.size(), redone_slides.size()); ++slide_index) {
          if (authored_slides.at(slide_index).toMap() != redone_slides.at(slide_index).toMap()) {
            std::cerr << "sequence redo changed slide " << slide_index << " before="
                      << QJsonDocument::fromVariant(authored_slides.at(slide_index)).toJson(QJsonDocument::Compact).toStdString()
                      << " after="
                      << QJsonDocument::fromVariant(redone_slides.at(slide_index)).toJson(QJsonDocument::Compact).toStdString()
                      << '\n';
            break;
          }
        }
        return fail("redo should restore the exact atomic sequence template result");
      }

      QVariantList media_selection;
      for (int media_index = 0; media_index < expected_media_counts.at(template_index); ++media_index) {
        media_selection.push_back(template_media_path);
      }
      if (!sequence.previewMediaAutofill(media_selection) ||
          sequence.mediaAutofillPlan().size() != expected_media_counts.at(template_index) ||
          !sequence.applyMediaAutofillPlan() ||
          preflight_issue_count(sequence.preflightIssues(), QStringLiteral("empty-required-slot")) != 0 ||
          sequence.preflightBlockingCount() != 0) {
        return fail("every authored pack should expose an exact, fillable media-placeholder sequence");
      }
      if (!sequence.saveDocument(
              QStringLiteral("Sequence Pack %1").arg(static_cast<qulonglong>(template_index + 1)))) {
        return fail("filled sequence packs should save");
      }
      CarouselDocumentController reopened_sequence;
      if (!reopened_sequence.loadDocument(sequence.savedDocumentId()) ||
          reopened_sequence.documentContentHash() != sequence.documentContentHash() ||
          reopened_sequence.slideCount() != expected_board_counts.at(template_index)) {
        return fail("sequence boards, layers, roles, links, and filled placeholders should survive reopen");
      }
      const auto sequence_export_dir =
          dir.filePath(QStringLiteral("sequence-pack-export-%1").arg(static_cast<qulonglong>(template_index + 1)));
      QDir().mkpath(sequence_export_dir);
      if (!reopened_sequence.exportCarouselToDirectory(sequence_export_dir) ||
          QDir(sequence_export_dir).entryList({QStringLiteral("*.png")}, QDir::Files).size() !=
              expected_board_counts.at(template_index)) {
        return fail("every filled authored pack should export its complete ordered board sequence");
      }
      if (template_ids.at(template_index) == QStringLiteral("listicle")) {
        const QImage first_item_export(
            QDir(sequence_export_dir).filePath(QStringLiteral("carousel-02.png")));
        const QImage final_prompt_export(
            QDir(sequence_export_dir).filePath(QStringLiteral("carousel-08.png")));
        if (first_item_export.isNull() || final_prompt_export.isNull() ||
            !color_matches(first_item_export.pixelColor(100, 1148), QColor(QStringLiteral("#ffb45c"))) ||
            !color_matches(final_prompt_export.pixelColor(400, 850), QColor(QStringLiteral("#17191f")))) {
          return fail("listicle export should render its authored progress and final-prompt components into the ordered PNGs");
        }
      } else if (template_ids.at(template_index) == QStringLiteral("portfolio-editorial")) {
        const QImage opener_export(
            QDir(sequence_export_dir).filePath(QStringLiteral("carousel-01.png")));
        const QImage full_bleed_export(
            QDir(sequence_export_dir).filePath(QStringLiteral("carousel-04.png")));
        if (opener_export.isNull() || full_bleed_export.isNull() ||
            color_is_dominantly_blue(opener_export.pixelColor(100, 100)) ||
            !color_is_dominantly_blue(opener_export.pixelColor(540, 675)) ||
            !color_is_dominantly_blue(full_bleed_export.pixelColor(540, 675))) {
          return fail("portfolio export should render quiet editorial typography while preserving dominant uninterrupted photography");
        }
      } else if (template_ids.at(template_index) == QStringLiteral("announcement-event")) {
        const QImage save_date_export(
            QDir(sequence_export_dir).filePath(QStringLiteral("carousel-01.png")));
        const QImage reserve_export(
            QDir(sequence_export_dir).filePath(QStringLiteral("carousel-08.png")));
        if (save_date_export.isNull() || reserve_export.isNull() ||
            !color_matches(save_date_export.pixelColor(500, 930), QColor(QStringLiteral("#ffd166"))) ||
            !color_matches(reserve_export.pixelColor(400, 910), QColor(QStringLiteral("#ffd166")))) {
          return fail("event export should render its authored date card and reservation action in the ordered PNGs");
        }
      } else if (template_ids.at(template_index) == QStringLiteral("thought-leadership")) {
        const QImage essay_action_export(
            QDir(sequence_export_dir).filePath(QStringLiteral("carousel-08.png")));
        if (essay_action_export.isNull() ||
            !color_matches(essay_action_export.pixelColor(400, 910), QColor(QStringLiteral("#a8d5ff")))) {
          return fail("thought-leadership export should render its authored full-essay action");
        }
      }
    }

    CarouselDocumentController authored_pack_copy;
    authored_pack_copy.applySequenceTemplate(QStringLiteral("photo-essay"));
    bool has_quote_copy = false;
    for (const auto& item_value : authored_pack_copy.canvasItems()) {
      const auto item = item_value.toMap();
      has_quote_copy = has_quote_copy ||
                       item.value(QStringLiteral("text")).toString().contains(QStringLiteral("small details"));
    }
    CarouselDocumentController launch_pack_copy;
    launch_pack_copy.applySequenceTemplate(QStringLiteral("product-launch"));
    CarouselDocumentController tutorial_pack_copy;
    tutorial_pack_copy.applySequenceTemplate(QStringLiteral("tutorial"));
    CarouselDocumentController comparison_pack_copy;
    comparison_pack_copy.applySequenceTemplate(QStringLiteral("before-after"));
    CarouselDocumentController listicle_pack_copy;
    listicle_pack_copy.applySequenceTemplate(QStringLiteral("listicle"));
    CarouselDocumentController portfolio_pack_copy;
    portfolio_pack_copy.applySequenceTemplate(QStringLiteral("portfolio-editorial"));
    CarouselDocumentController event_pack_copy;
    event_pack_copy.applySequenceTemplate(QStringLiteral("announcement-event"));
    CarouselDocumentController thought_pack_copy;
    thought_pack_copy.applySequenceTemplate(QStringLiteral("thought-leadership"));
    const auto comparison_items = comparison_pack_copy.canvasItems();
    const auto contains_copy = [](const QVariantList& items, const QString& fragment) {
      return std::any_of(items.cbegin(), items.cend(), [&fragment](const QVariant& value) {
        return value.toMap().value(QStringLiteral("text")).toString().contains(fragment);
      });
    };
    const bool has_results_disclaimer = std::any_of(
        comparison_items.cbegin(), comparison_items.cend(), [](const QVariant& value) {
          return value.toMap().value(QStringLiteral("text")).toString().contains(QStringLiteral("Results vary"));
        });
    if (!has_quote_copy ||
        !contains_copy(launch_pack_copy.canvasItems(), QStringLiteral("EARLY CUSTOMER")) ||
        !contains_copy(launch_pack_copy.canvasItems(), QStringLiteral("30-DAY TEAM PILOT")) ||
        !contains_copy(launch_pack_copy.canvasItems(), QStringLiteral("VIEW THE SPECS")) ||
        !contains_copy(tutorial_pack_copy.canvasItems(), QStringLiteral("ANNOTATION")) ||
        !contains_copy(tutorial_pack_copy.canvasItems(), QStringLiteral("TIP / WARNING")) ||
        !contains_copy(comparison_items, QStringLiteral("EDIT NOTES")) || !has_results_disclaimer ||
        !contains_copy(listicle_pack_copy.canvasItems(), QStringLiteral("A FIELD GUIDE IN FIVE PARTS")) ||
        !contains_copy(listicle_pack_copy.canvasItems(), QStringLiteral("POINT 5 OF 5")) ||
        !contains_copy(listicle_pack_copy.canvasItems(), QStringLiteral("WHAT WOULD YOU ADD?")) ||
        !contains_copy(portfolio_pack_copy.canvasItems(), QStringLiteral("EDITORIAL PORTFOLIO")) ||
        !contains_copy(portfolio_pack_copy.canvasItems(), QStringLiteral("UNTITLED, 2026")) ||
        !contains_copy(portfolio_pack_copy.canvasItems(), QStringLiteral("PHOTOGRAPHY / YOUR NAME")) ||
        !contains_copy(portfolio_pack_copy.canvasItems(), QStringLiteral("STUDIO@EXAMPLE.COM")) ||
        !contains_copy(event_pack_copy.canvasItems(), QStringLiteral("18 SEPTEMBER 2026")) ||
        !contains_copy(event_pack_copy.canvasItems(), QStringLiteral("NORTH GALLERY")) ||
        !contains_copy(event_pack_copy.canvasItems(), QStringLiteral("RESERVE A PLACE")) ||
        !contains_copy(thought_pack_copy.canvasItems(), QStringLiteral("AN ESSAY BY YOUR NAME")) ||
        !contains_copy(thought_pack_copy.canvasItems(), QStringLiteral("RESEARCHER / SOURCE, YEAR")) ||
        !contains_copy(thought_pack_copy.canvasItems(), QStringLiteral("READ THE FULL ESSAY"))) {
      return fail("the eight packs should retain their authored editorial, campaign, education, proof, listicle, portfolio, event, and thought-leadership content");
    }

    int automatic_item_numbers = 0;
    int item_captions = 0;
    int progress_tracks = 0;
    int progress_fills = 0;
    bool has_numbered_recap = false;
    bool progress_widths_increase = true;
    double previous_progress_width = 0.0;
    for (const auto& value : listicle_pack_copy.canvasItems()) {
      const auto item = value.toMap();
      const auto component = item.value(QStringLiteral("componentLinkId")).toString();
      if (component.endsWith(QStringLiteral(":item-number"))) {
        ++automatic_item_numbers;
        if (item.value(QStringLiteral("textAutoNumberSource")).toString() !=
            QStringLiteral("step")) {
          return fail("listicle item numbers should follow reorder-aware automatic step order");
        }
      } else if (component.endsWith(QStringLiteral(":item-caption"))) {
        ++item_captions;
      } else if (component.endsWith(QStringLiteral(":progress-track"))) {
        ++progress_tracks;
      } else if (component.endsWith(QStringLiteral(":progress-fill"))) {
        ++progress_fills;
        const auto width = item.value(QStringLiteral("width")).toDouble();
        progress_widths_increase = progress_widths_increase && width > previous_progress_width;
        previous_progress_width = width;
      } else if (component.endsWith(QStringLiteral(":recap-list"))) {
        has_numbered_recap = item.value(QStringLiteral("textListStyle")).toString() ==
            QStringLiteral("numbering");
      }
    }
    if (automatic_item_numbers != 5 || item_captions != 5 || progress_tracks != 5 ||
        progress_fills != 5 || !progress_widths_increase || !has_numbered_recap ||
        listicle_pack_copy.slides().at(3).toMap().value(QStringLiteral("templateKind")).toString() !=
            QStringLiteral("split-pair")) {
      return fail("listicle should create five numbered variants, image captions, increasing progress, a comparison pair, and a numbered recap");
    }

    listicle_pack_copy.moveSlide(5, 1);
    QVector<QPair<int, int>> reordered_step_numbers;
    for (const auto& value : listicle_pack_copy.canvasItems()) {
      const auto item = value.toMap();
      if (item.value(QStringLiteral("componentLinkId")).toString().endsWith(
              QStringLiteral(":item-number"))) {
        reordered_step_numbers.push_back({
            item.value(QStringLiteral("slideIndex")).toInt(),
            item.value(QStringLiteral("resolvedText")).toInt(),
        });
      }
    }
    std::sort(reordered_step_numbers.begin(), reordered_step_numbers.end());
    for (int index = 0; index < reordered_step_numbers.size(); ++index) {
      if (reordered_step_numbers.at(index).second != index + 1) {
        return fail("listicle progress numbers should recompute after board reordering");
      }
    }

    CarouselDocumentController tall_listicle;
    tall_listicle.setDocumentFormat(QStringLiteral("9x16"));
    if (!tall_listicle.applySequenceTemplate(QStringLiteral("listicle"))) {
      return fail("listicle should apply at the supported vertical ratio");
    }
    const auto board_width = tall_listicle.documentPixelWidth() / tall_listicle.slideCount();
    const auto board_height = tall_listicle.documentPixelHeight();
    for (const auto& value : tall_listicle.canvasItems()) {
      const auto item = value.toMap();
      const auto board = item.value(QStringLiteral("slideIndex")).toInt();
      const auto x = item.value(QStringLiteral("x")).toDouble();
      const auto y = item.value(QStringLiteral("y")).toDouble();
      const auto right = x + item.value(QStringLiteral("width")).toDouble();
      const auto bottom = y + item.value(QStringLiteral("height")).toDouble();
      if (x < board * board_width - 0.001 || right > (board + 1) * board_width + 0.001 ||
          y < -0.001 || bottom > board_height + 0.001) {
        return fail("listicle layers should remain inside their boards at the supported 9:16 ratio");
      }
    }

    QVariantMap portfolio_metadata;
    for (const auto& value : sequence_templates) {
      const auto metadata = value.toMap();
      if (metadata.value(QStringLiteral("templateId")).toString() ==
          QStringLiteral("portfolio-editorial")) {
        portfolio_metadata = metadata;
        break;
      }
    }
    int editorial_bands = 0;
    int triptych_labels = 0;
    int triptych_numbers = 0;
    bool has_diptych_pair = false;
    bool has_full_bleed_caption = false;
    bool has_pull_quote = false;
    bool has_credit_list = false;
    bool has_contact_details = false;
    bool has_marketing_card_component = false;
    for (const auto& value : portfolio_pack_copy.canvasItems()) {
      const auto item = value.toMap();
      const auto component = item.value(QStringLiteral("componentLinkId")).toString();
      if (component.endsWith(QStringLiteral(":editorial-caption-band"))) {
        ++editorial_bands;
        if (item.value(QStringLiteral("height")).toDouble() >= 216.0) {
          return fail("portfolio editorial bands should stay quiet and leave most of each photograph visible");
        }
      } else if (component.endsWith(QStringLiteral(":diptych-left-label")) ||
                 component.endsWith(QStringLiteral(":diptych-right-label"))) {
        has_diptych_pair = true;
      } else if (component.endsWith(QStringLiteral(":triptych-frame-label"))) {
        ++triptych_labels;
      } else if (component.endsWith(QStringLiteral(":triptych-frame-number"))) {
        ++triptych_numbers;
      } else if (component.endsWith(QStringLiteral(":full-bleed-caption"))) {
        has_full_bleed_caption = true;
      } else if (component.endsWith(QStringLiteral(":pull-quote"))) {
        has_pull_quote = item.value(QStringLiteral("textRole")).toString() == QStringLiteral("quote");
      } else if (component.endsWith(QStringLiteral(":credit-list"))) {
        has_credit_list = true;
      } else if (component.endsWith(QStringLiteral(":contact-details"))) {
        has_contact_details = true;
      }
      has_marketing_card_component = has_marketing_card_component ||
          component.endsWith(QStringLiteral(":headline-panel")) ||
          component.endsWith(QStringLiteral(":cta-button")) ||
          component.endsWith(QStringLiteral(":prompt-button"));
    }
    if (portfolio_metadata.value(QStringLiteral("category")).toString() !=
            QStringLiteral("Photography") ||
        editorial_bands != 7 || !has_diptych_pair || triptych_labels != 3 ||
        triptych_numbers != 3 || !has_full_bleed_caption || !has_pull_quote ||
        !has_credit_list || !has_contact_details || has_marketing_card_component ||
        portfolio_pack_copy.slides().at(1).toMap().value(QStringLiteral("templateKind")).toString() !=
            QStringLiteral("split-pair") ||
        portfolio_pack_copy.slides().at(2).toMap().value(QStringLiteral("templateKind")).toString() !=
            QStringLiteral("stacked-triptych") ||
        portfolio_pack_copy.slides().at(3).toMap().value(QStringLiteral("templateKind")).toString() !=
            QStringLiteral("single")) {
      return fail("portfolio pack should be photography-led with opener, diptych, triptych, full bleed, pull quote, credits, and contact—not marketing cards");
    }

    CarouselDocumentController wide_portfolio;
    wide_portfolio.setDocumentFormat(QStringLiteral("5x4"));
    if (!wide_portfolio.applySequenceTemplate(QStringLiteral("portfolio-editorial"))) {
      return fail("portfolio pack should apply at the supported horizontal ratio");
    }
    const auto portfolio_board_width =
        wide_portfolio.documentPixelWidth() / wide_portfolio.slideCount();
    const auto portfolio_board_height = wide_portfolio.documentPixelHeight();
    for (const auto& value : wide_portfolio.canvasItems()) {
      const auto item = value.toMap();
      const auto board = item.value(QStringLiteral("slideIndex")).toInt();
      const auto x = item.value(QStringLiteral("x")).toDouble();
      const auto y = item.value(QStringLiteral("y")).toDouble();
      const auto right = x + item.value(QStringLiteral("width")).toDouble();
      const auto bottom = y + item.value(QStringLiteral("height")).toDouble();
      if (x < board * portfolio_board_width - 0.001 ||
          right > (board + 1) * portfolio_board_width + 0.001 ||
          y < -0.001 || bottom > portfolio_board_height + 0.001) {
        return fail("portfolio layers should remain inside their boards at the supported 5:4 ratio");
      }
    }

    const auto component_item = [](const CarouselDocumentController& controller,
                                   const QString& suffix) {
      for (const auto& value : controller.canvasItems()) {
        const auto item = value.toMap();
        if (item.value(QStringLiteral("componentLinkId")).toString().endsWith(suffix)) {
          return item;
        }
      }
      return QVariantMap{};
    };
    const auto event_date = component_item(event_pack_copy, QStringLiteral(":event-date"));
    const auto event_location = component_item(event_pack_copy, QStringLiteral(":event-location"));
    const auto event_supporting_point =
        component_item(event_pack_copy, QStringLiteral(":supporting-point"));
    const auto event_program = component_item(event_pack_copy, QStringLiteral(":event-program"));
    const auto event_contributor_left =
        component_item(event_pack_copy, QStringLiteral(":contributor-left"));
    const auto event_contributor_right =
        component_item(event_pack_copy, QStringLiteral(":contributor-right"));
    const auto event_arrival = component_item(event_pack_copy, QStringLiteral(":arrival-details"));
    const auto event_cta = component_item(event_pack_copy, QStringLiteral(":event-cta-label"));
    if (event_date.value(QStringLiteral("textRole")).toString() != QStringLiteral("heading") ||
        !event_location.value(QStringLiteral("text")).toString().contains(QStringLiteral("128 MARKET STREET")) ||
        event_supporting_point.value(QStringLiteral("textRole")).toString() != QStringLiteral("body") ||
        !event_program.value(QStringLiteral("text")).toString().contains(QStringLiteral("20:40")) ||
        event_contributor_left.isEmpty() || event_contributor_right.isEmpty() ||
        !event_arrival.value(QStringLiteral("text")).toString().contains(QStringLiteral("ACCESS /")) ||
        event_cta.value(QStringLiteral("textRole")).toString() != QStringLiteral("cta") ||
        event_pack_copy.slides().at(5).toMap().value(QStringLiteral("templateKind")).toString() !=
            QStringLiteral("split-pair")) {
      return fail("event pack should retain useful headline, date/location/access, supporting point, program, contributor attribution, reminder, and CTA roles");
    }

    const auto thought_byline = component_item(thought_pack_copy, QStringLiteral(":essay-byline"));
    const auto thought_thesis = component_item(thought_pack_copy, QStringLiteral(":thesis-statement"));
    const auto thought_point =
        component_item(thought_pack_copy, QStringLiteral(":essay-supporting-point"));
    const auto thought_evidence = component_item(thought_pack_copy, QStringLiteral(":evidence-source"));
    const auto thought_quote = component_item(thought_pack_copy, QStringLiteral(":thought-pull-quote"));
    const auto thought_source = component_item(thought_pack_copy, QStringLiteral(":thought-quote-source"));
    const auto thought_counterpoint =
        component_item(thought_pack_copy, QStringLiteral(":counterpoint-copy"));
    const auto thought_implications =
        component_item(thought_pack_copy, QStringLiteral(":implications-list"));
    const auto thought_cta = component_item(thought_pack_copy, QStringLiteral(":essay-cta-label"));
    if (thought_byline.value(QStringLiteral("textRole")).toString() != QStringLiteral("attribution") ||
        thought_thesis.value(QStringLiteral("textRole")).toString() != QStringLiteral("heading") ||
        thought_point.value(QStringLiteral("textRole")).toString() != QStringLiteral("body") ||
        !thought_evidence.value(QStringLiteral("text")).toString().contains(QStringLiteral("SOURCE /")) ||
        thought_quote.value(QStringLiteral("textRole")).toString() != QStringLiteral("quote") ||
        thought_source.value(QStringLiteral("textRole")).toString() != QStringLiteral("attribution") ||
        !thought_counterpoint.value(QStringLiteral("text")).toString().contains(QStringLiteral("personal agency")) ||
        thought_implications.value(QStringLiteral("textListStyle")).toString() !=
            QStringLiteral("numbering") ||
        thought_cta.value(QStringLiteral("textRole")).toString() != QStringLiteral("cta") ||
        thought_pack_copy.slides().at(3).toMap().value(QStringLiteral("templateKind")).toString() !=
            QStringLiteral("split-pair")) {
      return fail("thought-leadership pack should retain thesis, supporting point, matched evidence, sourced quote, counterpoint, numbered implications, and CTA roles");
    }

    const auto layers_stay_inside_boards = [](const CarouselDocumentController& controller) {
      const auto board_width = controller.documentPixelWidth() / controller.slideCount();
      const auto board_height = controller.documentPixelHeight();
      for (const auto& value : controller.canvasItems()) {
        const auto item = value.toMap();
        const auto board = item.value(QStringLiteral("slideIndex")).toInt();
        const auto x = item.value(QStringLiteral("x")).toDouble();
        const auto y = item.value(QStringLiteral("y")).toDouble();
        const auto right = x + item.value(QStringLiteral("width")).toDouble();
        const auto bottom = y + item.value(QStringLiteral("height")).toDouble();
        if (x < board * board_width - 0.001 || right > (board + 1) * board_width + 0.001 ||
            y < -0.001 || bottom > board_height + 0.001) {
          return false;
        }
      }
      return true;
    };
    CarouselDocumentController square_event;
    square_event.setDocumentFormat(QStringLiteral("1x1"));
    CarouselDocumentController tall_thought;
    tall_thought.setDocumentFormat(QStringLiteral("9x16"));
    if (!square_event.applySequenceTemplate(QStringLiteral("announcement-event")) ||
        !layers_stay_inside_boards(square_event) ||
        !tall_thought.applySequenceTemplate(QStringLiteral("thought-leadership")) ||
        !layers_stay_inside_boards(tall_thought)) {
      return fail("event and thought-leadership layers should remain in-board at supported square and vertical ratios");
    }
  }

  {
    CarouselDocumentController controller;
    auto item_by_id = [&controller](const QString& item_id) {
      for (const auto& value : controller.canvasItems()) {
        const auto item = value.toMap();
        if (item.value(QStringLiteral("itemId")).toString() == item_id) {
          return item;
        }
      }
      return QVariantMap{};
    };

    controller.addShapeItem(QStringLiteral("rectangle"));
    const auto anchor_id = controller.selectedItemId();
    controller.setSelectedItemCenter(180.0, 260.0);
    controller.setSelectedItemSize(140.0, 180.0);
    controller.setSelectedShapeFillColor(QStringLiteral("#ff453a"));
    controller.setSelectedShapeStrokeColor(QStringLiteral("#34c759"));
    controller.setSelectedShapeStrokeWidth(12.0);
    controller.setSelectedItemOpacity(0.65);

    controller.addShapeItem(QStringLiteral("ellipse"));
    const auto target_id = controller.selectedItemId();
    controller.setSelectedItemCenter(700.0, 820.0);
    controller.setSelectedItemSize(320.0, 240.0);
    controller.setSelectedShapeFillColor(QStringLiteral("#0a84ff"));
    const auto target_original = item_by_id(target_id);

    controller.selectItem(target_id);
    controller.addItemToSelection(anchor_id);
    const auto before_preview_hash = controller.documentContentHash();
    if (!controller.previewSelectionBatchOperation(QStringLiteral("match-size")) ||
        !controller.selectionBatchPreview().value(QStringLiteral("active")).toBool() ||
        controller.selectionBatchPreview().value(QStringLiteral("affectedCount")).toInt() != 1 ||
        controller.documentContentHash() != before_preview_hash ||
        item_by_id(target_id).value(QStringLiteral("width")).toDouble() != 320.0) {
      return fail("match-size preview should list the exact target without mutating document geometry");
    }
    if (!controller.applySelectionBatchOperation()) {
      return fail("match-size preview should apply explicitly");
    }
    auto target = item_by_id(target_id);
    if (target.value(QStringLiteral("width")).toDouble() != 140.0 ||
        target.value(QStringLiteral("height")).toDouble() != 180.0 ||
        !controller.selectionBatchPreview().isEmpty()) {
      return fail("match size should copy both anchor dimensions and clear its preview");
    }
    controller.undo();
    target = item_by_id(target_id);
    if (target.value(QStringLiteral("width")).toDouble() != 320.0 ||
        target.value(QStringLiteral("height")).toDouble() != 240.0) {
      return fail("match size should undo in one step");
    }

    if (!controller.previewSelectionBatchOperation(QStringLiteral("match-width")) ||
        !controller.applySelectionBatchOperation() ||
        item_by_id(target_id).value(QStringLiteral("width")).toDouble() != 140.0 ||
        item_by_id(target_id).value(QStringLiteral("height")).toDouble() != 240.0) {
      return fail("match width should preserve every target height");
    }
    controller.undo();
    if (!controller.previewSelectionBatchOperation(QStringLiteral("match-height")) ||
        !controller.applySelectionBatchOperation() ||
        item_by_id(target_id).value(QStringLiteral("width")).toDouble() != 320.0 ||
        item_by_id(target_id).value(QStringLiteral("height")).toDouble() != 180.0) {
      return fail("match height should preserve every target width");
    }
    controller.undo();

    if (!controller.previewSelectionBatchOperation(QStringLiteral("match-style")) ||
        !controller.applySelectionBatchOperation()) {
      return fail("compatible shape styles should preview and apply");
    }
    target = item_by_id(target_id);
    if (target.value(QStringLiteral("fillColor")).toString() != QStringLiteral("#ff453a") ||
        target.value(QStringLiteral("strokeColor")).toString() != QStringLiteral("#34c759") ||
        target.value(QStringLiteral("strokeWidth")).toDouble() != 12.0 ||
        qAbs(target.value(QStringLiteral("opacity")).toDouble() - 0.65) > 0.001 ||
        target.value(QStringLiteral("shapeKind")).toString() != QStringLiteral("rectangle")) {
      return fail("match style should copy the anchor's complete compatible visual style");
    }
    controller.undo();
    if (item_by_id(target_id).value(QStringLiteral("fillColor")).toString() !=
            target_original.value(QStringLiteral("fillColor")).toString()) {
      return fail("match style should undo atomically");
    }

    const auto anchor_center_before = QPointF(
        item_by_id(anchor_id).value(QStringLiteral("centerX")).toDouble(),
        item_by_id(anchor_id).value(QStringLiteral("centerY")).toDouble());
    const auto target_center_before = QPointF(
        item_by_id(target_id).value(QStringLiteral("centerX")).toDouble(),
        item_by_id(target_id).value(QStringLiteral("centerY")).toDouble());
    if (!controller.previewSelectionBatchOperation(QStringLiteral("swap-position")) ||
        !controller.applySelectionBatchOperation()) {
      return fail("two selected layers should preview and apply position swapping");
    }
    if (QPointF(item_by_id(anchor_id).value(QStringLiteral("centerX")).toDouble(),
                item_by_id(anchor_id).value(QStringLiteral("centerY")).toDouble()) != target_center_before ||
        QPointF(item_by_id(target_id).value(QStringLiteral("centerX")).toDouble(),
                item_by_id(target_id).value(QStringLiteral("centerY")).toDouble()) != anchor_center_before) {
      return fail("swap position should exchange centers without changing sizes or content");
    }
    controller.undo();

    controller.addTextItem(QStringLiteral("Anchor content"));
    const auto source_text_id = controller.selectedItemId();
    controller.setSelectedItemSize(500.0, 180.0);
    controller.addTextItem(QStringLiteral("Destination content"));
    const auto destination_text_id = controller.selectedItemId();
    controller.setSelectedItemSize(360.0, 260.0);
    controller.selectItem(destination_text_id);
    controller.addItemToSelection(source_text_id);
    const auto destination_frame_before = item_by_id(destination_text_id);
    if (!controller.previewSelectionBatchOperation(QStringLiteral("replace-content")) ||
        !controller.applySelectionBatchOperation()) {
      return fail("compatible text content replacement should require an explicit preview apply");
    }
    const auto replaced_text = item_by_id(destination_text_id);
    if (replaced_text.value(QStringLiteral("text")).toString() != QStringLiteral("Anchor content") ||
        replaced_text.value(QStringLiteral("width")).toDouble() !=
            destination_frame_before.value(QStringLiteral("width")).toDouble() ||
        replaced_text.value(QStringLiteral("height")).toDouble() !=
            destination_frame_before.value(QStringLiteral("height")).toDouble()) {
      return fail("replace content should copy authored content while preserving the target frame");
    }
    controller.undo();
    if (item_by_id(destination_text_id).value(QStringLiteral("text")).toString() !=
        QStringLiteral("Destination content")) {
      return fail("replace content should undo in one step");
    }

    controller.selectItem(target_id);
    controller.addItemToSelection(source_text_id);
    controller.addItemToSelection(anchor_id);
    if (!controller.previewSelectionBatchOperation(QStringLiteral("match-style")) ||
        controller.selectionBatchPreview().value(QStringLiteral("affectedCount")).toInt() != 1 ||
        controller.selectionBatchPreview().value(QStringLiteral("skippedCount")).toInt() != 1) {
      return fail("batch preview should explicitly skip incompatible selected layer types");
    }
    controller.selectItem(anchor_id);
    if (!controller.selectionBatchPreview().isEmpty()) {
      return fail("changing the selection should safely discard a stale batch preview");
    }
  }

  {
    CarouselDocumentController controller;
    controller.addSlide();
    controller.selectSlide(1);
    controller.addShapeItem(QStringLiteral("rectangle"));
    const auto first_id = controller.selectedItemId();
    controller.setSelectedItemCenter(1080.0 + 200.0, 300.0);
    controller.setSelectedItemSize(200.0, 100.0);

    if (!controller.applyTransformExpression(
            QStringLiteral("x"), QStringLiteral("300"), QStringLiteral("center"), false) ||
        qAbs(controller.selectionDetails().value(QStringLiteral("centerX")).toDouble() - 1380.0) > 0.001) {
      return fail("absolute X transform expressions should resolve in the active board coordinate space");
    }
    if (!controller.applyTransformExpression(
            QStringLiteral("x"), QStringLiteral("+24"), QStringLiteral("center"), false) ||
        qAbs(controller.selectionDetails().value(QStringLiteral("centerX")).toDouble() - 1404.0) > 0.001) {
      return fail("plus transform expressions should offset the current board-relative value");
    }
    controller.undo();
    if (qAbs(controller.selectionDetails().value(QStringLiteral("centerX")).toDouble() - 1380.0) > 0.001) {
      return fail("a relative transform expression should undo in one step");
    }
    if (!controller.applyTransformExpression(
            QStringLiteral("x"), QStringLiteral("=-10"), QStringLiteral("left"), false) ||
        qAbs(controller.selectionDetails().value(QStringLiteral("x")).toDouble() - 1070.0) > 0.001) {
      return fail("equals-prefixed negative transforms should set an exact board-relative anchor value");
    }
    if (!controller.applyTransformExpression(
            QStringLiteral("y"), QStringLiteral("-10"), QStringLiteral("top"), false) ||
        qAbs(controller.selectionDetails().value(QStringLiteral("y")).toDouble() - 240.0) > 0.001) {
      return fail("minus transform expressions should subtract from the current anchor value");
    }

    if (!controller.applyTransformExpression(
            QStringLiteral("width"), QStringLiteral("*2"), QStringLiteral("center"), false) ||
        qAbs(controller.selectionDetails().value(QStringLiteral("width")).toDouble() - 400.0) > 0.001 ||
        qAbs(controller.selectionDetails().value(QStringLiteral("height")).toDouble() - 100.0) > 0.001) {
      return fail("multiply expressions should resize one explicit dimension when aspect lock is off");
    }
    if (!controller.applyTransformExpression(
            QStringLiteral("height"), QStringLiteral("/2"), QStringLiteral("center"), true) ||
        qAbs(controller.selectionDetails().value(QStringLiteral("width")).toDouble() - 200.0) > 0.001 ||
        qAbs(controller.selectionDetails().value(QStringLiteral("height")).toDouble() - 50.0) > 0.001) {
      return fail("divide expressions should preserve the current selection ratio when aspect lock is on");
    }
    if (!controller.applyTransformExpression(
            QStringLiteral("rotation"), QStringLiteral("+15"), QStringLiteral("center"), false) ||
        controller.selectionDetails().value(QStringLiteral("rotation")).toDouble() != 15.0 ||
        !controller.applyTransformExpression(
            QStringLiteral("rotation"), QStringLiteral("=-45"), QStringLiteral("center"), false) ||
        controller.selectionDetails().value(QStringLiteral("rotation")).toDouble() != -45.0) {
      return fail("rotation fields should accept relative and exact negative degree expressions");
    }

    controller.addShapeItem(QStringLiteral("ellipse"));
    const auto second_id = controller.selectedItemId();
    controller.setSelectedItemCenter(1080.0 + 760.0, 820.0);
    controller.setSelectedItemSize(320.0, 180.0);
    controller.setSelectedItemRotation(20.0);
    controller.selectItem(second_id);
    controller.addItemToSelection(first_id);
    const auto mixed = controller.selectionDetails();
    if (mixed.value(QStringLiteral("transformCoordinateSpace")).toString() != QStringLiteral("board") ||
        !mixed.value(QStringLiteral("transformMixedX")).toBool() ||
        !mixed.value(QStringLiteral("transformMixedY")).toBool() ||
        !mixed.value(QStringLiteral("transformMixedWidth")).toBool() ||
        !mixed.value(QStringLiteral("transformMixedHeight")).toBool() ||
        !mixed.value(QStringLiteral("transformMixedRotation")).toBool()) {
      return fail("multi-selection transform state should expose every mixed coordinate, size, and rotation field");
    }
    if (controller.applyTransformExpression(
            QStringLiteral("width"), QStringLiteral("/0"), QStringLiteral("center"), true) ||
        controller.applyTransformExpression(
            QStringLiteral("unknown"), QStringLiteral("10"), QStringLiteral("center"), true)) {
      return fail("transform expressions should reject divide-by-zero and unknown fields without mutation");
    }
  }

  {
    CarouselDocumentController controller;
    controller.setDocumentName(QStringLiteral("Repeat Grid Proof"));
    controller.addShapeItem(QStringLiteral("rectangle"));
    const auto source_id = controller.selectedItemId();
    controller.setSelectedItemCenter(100.0, 100.0);
    controller.setSelectedItemSize(100.0, 80.0);
    controller.setSelectedShapeFillColor(QStringLiteral("#ff9f0a"));
    const auto before_preview_hash = controller.documentContentHash();
    if (!controller.previewRepeatGrid(2, 3, 10.0, 20.0, QStringLiteral("row-major"))) {
      return fail("repeat grid should preview a valid row-major layout");
    }
    auto preview = controller.repeatGridPreview();
    const auto cells = preview.value(QStringLiteral("cells")).toList();
    if (!preview.value(QStringLiteral("active")).toBool() ||
        preview.value(QStringLiteral("cellCount")).toInt() != 6 ||
        preview.value(QStringLiteral("sourceLayerCount")).toInt() != 1 ||
        preview.value(QStringLiteral("projectedLayerCount")).toInt() != 6 ||
        qAbs(preview.value(QStringLiteral("resultWidth")).toDouble() - 320.0) > 0.001 ||
        qAbs(preview.value(QStringLiteral("resultHeight")).toDouble() - 180.0) > 0.001 ||
        !preview.value(QStringLiteral("detachedEditableResults")).toBool() ||
        cells.size() != 6 ||
        cells.at(1).toMap().value(QStringLiteral("row")).toInt() != 0 ||
        cells.at(1).toMap().value(QStringLiteral("column")).toInt() != 1 ||
        controller.documentContentHash() != before_preview_hash) {
      return fail("repeat-grid preview should truthfully report cells, gaps, projected bounds/layers, ordering, and detached output without mutation");
    }
    if (!controller.previewRepeatGrid(2, 3, 10.0, 20.0, QStringLiteral("column-major"))) {
      return fail("repeat grid should preview column-major ordering");
    }
    preview = controller.repeatGridPreview();
    const auto column_major_cells = preview.value(QStringLiteral("cells")).toList();
    if (preview.value(QStringLiteral("ordering")).toString() != QStringLiteral("column-major") ||
        column_major_cells.at(1).toMap().value(QStringLiteral("row")).toInt() != 1 ||
        column_major_cells.at(1).toMap().value(QStringLiteral("column")).toInt() != 0) {
      return fail("repeat-grid ordering should change the exact cell creation sequence");
    }
    if (!controller.previewRepeatGrid(2, 3, 10.0, 20.0, QStringLiteral("row-major")) ||
        !controller.applyRepeatGrid()) {
      return fail("reviewed repeat grid should apply in one operation");
    }
    const auto grid_items = controller.canvasItems();
    if (grid_items.size() != 6 ||
        controller.selectionDetails().value(QStringLiteral("selectedCount")).toInt() != 6 ||
        !controller.repeatGridPreview().isEmpty()) {
      return fail("repeat grid should select six ordinary result layers and clear its preview");
    }
    QSet<QString> result_ids;
    QSet<QString> result_positions;
    for (const auto& value : grid_items) {
      const auto item = value.toMap();
      result_ids.insert(item.value(QStringLiteral("itemId")).toString());
      result_positions.insert(
          QStringLiteral("%1,%2")
              .arg(item.value(QStringLiteral("x")).toDouble(), 0, 'f', 1)
              .arg(item.value(QStringLiteral("y")).toDouble(), 0, 'f', 1));
      if (!item.value(QStringLiteral("componentLinkId")).toString().isEmpty() ||
          item.value(QStringLiteral("locked")).toBool() ||
          item.value(QStringLiteral("type")).toString() != QStringLiteral("shape")) {
        return fail("repeat-grid results should be detached, unlocked, ordinary editable layers");
      }
    }
    if (result_ids.size() != 6 || result_positions.size() != 6 || !result_ids.contains(source_id)) {
      return fail("repeat-grid results should have unique identities and exact non-overlapping gap positions");
    }
    controller.undo();
    if (controller.canvasItems().size() != 1 || controller.selectedItemId() != source_id) {
      return fail("the complete repeat grid should undo atomically to its source cell");
    }
    controller.redo();
    if (controller.canvasItems().size() != 6 || !controller.saveDocument()) {
      return fail("repeat grid should redo atomically and save as ordinary layers");
    }
    CarouselDocumentController restored;
    if (!restored.loadDocument(controller.savedDocumentId()) || restored.canvasItems().size() != 6) {
      return fail("repeat-grid geometry and unique editable layers should survive save and reopen");
    }
    const auto restored_before_move = restored.canvasItems();
    const auto unmoved_x = restored_before_move.at(0).toMap().value(QStringLiteral("x")).toDouble();
    const auto moved_x = restored_before_move.at(1).toMap().value(QStringLiteral("x")).toDouble();
    restored.selectItem(restored_before_move.at(1).toMap().value(QStringLiteral("itemId")).toString());
    restored.moveSelectedItemByPixels(7.0, 0.0);
    if (restored.canvasItems().at(0).toMap().value(QStringLiteral("x")).toDouble() != unmoved_x ||
        restored.canvasItems().at(1).toMap().value(QStringLiteral("x")).toDouble() != moved_x + 7.0) {
      return fail("reopened repeat-grid cells should remain independently editable");
    }
    if (restored.previewRepeatGrid(0, 2, 0.0, 0.0, QStringLiteral("row-major")) ||
        restored.previewRepeatGrid(11, 10, 0.0, 0.0, QStringLiteral("row-major"))) {
      return fail("repeat grid should reject dimensions outside the declared one-to-ten bounds");
    }
  }

  {
    CarouselDocumentController controller;
    controller.setDocumentFormat(QStringLiteral("5x4"));
    const auto left_path = write_image(
        dir,
        QStringLiteral("left-template.png"),
        QImage(QSize(700, 1080), QImage::Format_ARGB32));
    const auto right_path = write_image(
        dir,
        QStringLiteral("right-template.png"),
        QImage(QSize(700, 1080), QImage::Format_ARGB32));
    if (left_path.isEmpty() || right_path.isEmpty()) {
      return fail("split template source images should save");
    }

    QImage left_image(left_path);
    left_image.fill(QColor(QStringLiteral("#34c759")));
    if (!left_image.save(left_path)) {
      return fail("left split template image should save");
    }

    QImage right_image(right_path);
    right_image.fill(QColor(QStringLiteral("#ffd60a")));
    if (!right_image.save(right_path)) {
      return fail("right split template image should save");
    }

    controller.applyTemplateToCurrent(QStringLiteral("split-pair"));
    controller.assignPrimaryImage(left_path, QStringLiteral("left"));
    controller.assignSecondaryImage(right_path, QStringLiteral("right"));

    const auto export_dir_path = dir.filePath(QStringLiteral("export-split-template"));
    QDir().mkpath(export_dir_path);
    if (!controller.exportCarouselToDirectory(export_dir_path)) {
      return fail("split template export should succeed");
    }

    const QImage slide(QDir(export_dir_path).filePath(QStringLiteral("carousel-01.png")));
    if (!color_is_dominantly_green(
            slide.pixelColor(slide.width() / 4, slide.height() / 2)) ||
        !color_is_dominantly_yellow(
            slide.pixelColor((slide.width() * 3) / 4,
                             slide.height() / 2))) {
      return fail("split template export should preserve left and right slot media");
    }
  }

  {
    CarouselDocumentController controller;
    controller.addTextItem(QStringLiteral("Duplicate me"));
    controller.setSelectedItemCenter(540.0, 220.0);
    controller.duplicateCurrentSlide();

    if (controller.slideCount() != 2 || controller.currentIndex() != 1) {
      return fail("duplicating the current canvas should insert a new selected canvas after it");
    }

    const auto duplicated_items = controller.canvasItems();
    if (duplicated_items.size() != 2) {
      return fail("duplicating a canvas should copy freeform layers that intersect that canvas");
    }

    int copied_selected_items = 0;
    int copied_items_on_second_slide = 0;
    for (const auto& value : duplicated_items) {
      const auto item = value.toMap();
      if (item.value(QStringLiteral("slideIndex")).toInt() == 1) {
        ++copied_items_on_second_slide;
      }
      if (item.value(QStringLiteral("selected")).toBool()) {
        ++copied_selected_items;
      }
    }
    if (copied_items_on_second_slide != 1 || copied_selected_items != 1) {
      return fail("duplicating a canvas should select the copied layers on the new canvas");
    }
  }

  {
    CarouselDocumentController controller;
    controller.setDocumentFormat(QStringLiteral("1x1"));
    if (controller.documentPixelWidth() != 1080 || controller.documentPixelHeight() != 1080) {
      return fail("square carousel format should use a 1080 by 1080 export canvas");
    }

    controller.setDocumentFormat(QStringLiteral("9x16"));
    if (controller.documentPixelWidth() != 1080 || controller.documentPixelHeight() != 1920) {
      return fail("story carousel format should use a 1080 by 1920 export canvas");
    }

    const auto export_dir_path = dir.filePath(QStringLiteral("export-story-format"));
    QDir().mkpath(export_dir_path);
    if (!controller.exportCarouselToDirectory(export_dir_path)) {
      return fail("story format carousel should export");
    }
    const QImage slide(QDir(export_dir_path).filePath(QStringLiteral("carousel-01.png")));
    if (slide.size() != QSize(1080, 1920)) {
      return fail("story format export should write the requested pixel size");
    }
  }

  {
    CarouselDocumentController controller;
    controller.setCurrentSlideBackgroundColor(QStringLiteral("#0a84ff"));

    auto selection = controller.selectionDetails();
    if (selection.value(QStringLiteral("backgroundColor")).toString() != QStringLiteral("#0a84ff")) {
      return fail("board background color should update selection metadata");
    }
    const auto slides = controller.slides();
    if (slides.isEmpty() ||
        slides.at(0).toMap().value(QStringLiteral("backgroundColor")).toString() != QStringLiteral("#0a84ff")) {
      return fail("board background color should update slide metadata");
    }

    const auto export_dir_path = dir.filePath(QStringLiteral("export-background-color"));
    QDir().mkpath(export_dir_path);
    if (!controller.exportCarouselToDirectory(export_dir_path)) {
      return fail("background color carousel should export");
    }

    const QImage slide(QDir(export_dir_path).filePath(QStringLiteral("carousel-01.png")));
    if (!color_matches(slide.pixelColor(4, 4), QColor(QStringLiteral("#0a84ff")))) {
      return fail("empty exported boards should reveal the chosen background color");
    }

    controller.setDocumentName(QStringLiteral("Background Color Carousel"));
    if (!controller.saveDocument()) {
      return fail("background color carousel should save");
    }

    CarouselDocumentController restored;
    if (!restored.loadDocument(controller.savedDocumentId())) {
      return fail("background color carousel should load");
    }
    selection = restored.selectionDetails();
    if (selection.value(QStringLiteral("backgroundColor")).toString() != QStringLiteral("#0a84ff")) {
      return fail("saved carousel documents should restore board background colors");
    }
  }

  {
    CarouselDocumentController controller;
    controller.setCurrentSlideBackgroundGradient(
        QStringLiteral("linear-horizontal"),
        QStringLiteral("#ff0000"),
        QStringLiteral("#0000ff"));

    auto selection = controller.selectionDetails();
    if (selection.value(QStringLiteral("backgroundKind")).toString() != QStringLiteral("linear-horizontal") ||
        selection.value(QStringLiteral("backgroundColor")).toString() != QStringLiteral("#ff0000") ||
        selection.value(QStringLiteral("backgroundSecondaryColor")).toString() != QStringLiteral("#0000ff")) {
      return fail("board gradient controls should update selection metadata");
    }
    const auto slides = controller.slides();
    if (slides.isEmpty() ||
        slides.at(0).toMap().value(QStringLiteral("backgroundKind")).toString() !=
            QStringLiteral("linear-horizontal")) {
      return fail("board gradient controls should update slide metadata");
    }

    const auto export_dir_path = dir.filePath(QStringLiteral("export-background-gradient"));
    QDir().mkpath(export_dir_path);
    if (!controller.exportCarouselToDirectory(export_dir_path)) {
      return fail("gradient background carousel should export");
    }

    const QImage slide(QDir(export_dir_path).filePath(QStringLiteral("carousel-01.png")));
    const QColor left = slide.pixelColor(4, slide.height() / 2);
    const QColor right = slide.pixelColor(slide.width() - 5, slide.height() / 2);
    if (left.red() <= left.blue() || right.blue() <= right.red()) {
      return fail("horizontal gradient backgrounds should render across exported boards");
    }

    controller.setDocumentName(QStringLiteral("Gradient Background Carousel"));
    if (!controller.saveDocument()) {
      return fail("gradient background carousel should save");
    }

    CarouselDocumentController restored;
    if (!restored.loadDocument(controller.savedDocumentId())) {
      return fail("gradient background carousel should load");
    }
    selection = restored.selectionDetails();
    if (selection.value(QStringLiteral("backgroundKind")).toString() != QStringLiteral("linear-horizontal") ||
        selection.value(QStringLiteral("backgroundSecondaryColor")).toString() != QStringLiteral("#0000ff")) {
      return fail("saved carousel documents should restore board gradient settings");
    }
  }

  {
    CarouselDocumentController controller;
    controller.addShapeItem(QStringLiteral("rectangle"));
    controller.setSelectedItemCenter(540.0, 675.0);
    controller.setSelectedItemSize(640.0, 180.0);
    controller.setSelectedShapeFillColor(QStringLiteral("#ff453a"));
    controller.setSelectedShapeStrokeColor(QStringLiteral("#0a84ff"));
    controller.setSelectedShapeStrokeWidth(0.0);
    controller.setSelectedShapeCornerRadius(32.0);
    controller.setSelectedItemOpacity(1.0);

    auto selection = controller.selectionDetails();
    if (!selection.value(QStringLiteral("isShape")).toBool() ||
        selection.value(QStringLiteral("itemType")).toString() != QStringLiteral("shape")) {
      return fail("adding a shape layer should select it as a shape item");
    }
    if (selection.value(QStringLiteral("width")).toDouble() != 640.0 ||
        selection.value(QStringLiteral("height")).toDouble() != 180.0) {
      return fail("shape layers should resize freely without photo aspect locking");
    }
    if (selection.value(QStringLiteral("fillColor")).toString() != QStringLiteral("#ff453a") ||
        selection.value(QStringLiteral("cornerRadius")).toDouble() != 32.0) {
      return fail("shape layer style controls should update selection metadata");
    }

    const auto export_dir_path = dir.filePath(QStringLiteral("export-shape-layer"));
    QDir().mkpath(export_dir_path);
    if (!controller.exportCarouselToDirectory(export_dir_path)) {
      return fail("shape layer export should succeed");
    }
    const QImage slide(QDir(export_dir_path).filePath(QStringLiteral("carousel-01.png")));
    if (!color_matches(slide.pixelColor(540, 675), QColor(QStringLiteral("#ff453a")))) {
      return fail("shape layers should render into exported slides");
    }

    controller.setDocumentName(QStringLiteral("Shape Carousel"));
    if (!controller.saveDocument()) {
      return fail("shape layer carousel should save");
    }

    CarouselDocumentController restored;
    if (!restored.loadDocument(controller.savedDocumentId())) {
      return fail("shape layer carousel should load");
    }
    const auto restored_items = restored.canvasItems();
    if (restored_items.size() != 1 ||
        restored_items.at(0).toMap().value(QStringLiteral("type")).toString() != QStringLiteral("shape") ||
        restored_items.at(0).toMap().value(QStringLiteral("fillColor")).toString() != QStringLiteral("#ff453a")) {
      return fail("saved carousel documents should restore shape layers and style");
    }
  }

  {
    CarouselDocumentController controller;
    const auto focal_source_path = write_image(
        dir,
        QStringLiteral("focal-point-source.png"),
        split_image(QSize(1000, 500), QColor(QStringLiteral("#ff453a")), QColor(QStringLiteral("#0a84ff"))));
    if (focal_source_path.isEmpty()) {
      return fail("focal-point source image should save");
    }
    controller.assignPrimaryImage(focal_source_path, QStringLiteral("focal point source"));
    controller.setCurrentMediaFocalPoint(0.88, 0.5);
    auto selection = controller.selectionDetails();
    if (selection.value(QStringLiteral("cropMode")).toString() != QStringLiteral("smart") ||
        qAbs(selection.value(QStringLiteral("focalX")).toDouble() - 0.88) > 0.001) {
      return fail("slot media should expose a persistent smart focal point");
    }

    const auto portrait_focal_export = dir.filePath(QStringLiteral("focal-smart-portrait"));
    QDir().mkpath(portrait_focal_export);
    if (!controller.exportCarouselToDirectory(portrait_focal_export)) {
      return fail("smart focal portrait crop should export");
    }
    const QImage portrait_focal_image(QDir(portrait_focal_export).filePath(QStringLiteral("carousel-01.png")));
    if (!color_is_dominantly_blue(portrait_focal_image.pixelColor(
            portrait_focal_image.width() / 2,
            portrait_focal_image.height() / 2))) {
      return fail("smart focal crop should keep the requested subject at the center of a portrait ratio");
    }

    controller.setDocumentFormat(QStringLiteral("5x4"));
    const auto landscape_focal_export = dir.filePath(QStringLiteral("focal-smart-landscape"));
    QDir().mkpath(landscape_focal_export);
    if (!controller.exportCarouselToDirectory(landscape_focal_export)) {
      return fail("smart focal landscape crop should export");
    }
    const QImage landscape_focal_image(QDir(landscape_focal_export).filePath(QStringLiteral("carousel-01.png")));
    if (!color_is_dominantly_blue(landscape_focal_image.pixelColor(
            landscape_focal_image.width() / 2,
            landscape_focal_image.height() / 2))) {
      return fail("the same focal point should drive the initial crop after a ratio change");
    }

    controller.setCurrentOffsetX(-1.0);
    selection = controller.selectionDetails();
    if (selection.value(QStringLiteral("cropMode")).toString() != QStringLiteral("manual") ||
        qAbs(selection.value(QStringLiteral("focalX")).toDouble() - 0.88) > 0.001) {
      return fail("manual pan should become authoritative without discarding the saved focal point");
    }
    const auto manual_focal_export = dir.filePath(QStringLiteral("focal-manual-landscape"));
    QDir().mkpath(manual_focal_export);
    if (!controller.exportCarouselToDirectory(manual_focal_export)) {
      return fail("manual focal override should export");
    }
    const QImage manual_focal_image(QDir(manual_focal_export).filePath(QStringLiteral("carousel-01.png")));
    if (!color_is_dominantly_red(manual_focal_image.pixelColor(
            manual_focal_image.width() / 2,
            manual_focal_image.height() / 2))) {
      return fail("manual crop should override the smart focal placement in exported pixels");
    }

    if (!controller.saveDocument(QStringLiteral("Focal Point Persistence"))) {
      return fail("focal-point document should save");
    }
    CarouselDocumentController restored_focal;
    if (!restored_focal.loadDocument(controller.savedDocumentId())) {
      return fail("focal-point document should reopen");
    }
    const auto restored_focal_selection = restored_focal.selectionDetails();
    if (restored_focal_selection.value(QStringLiteral("cropMode")).toString() != QStringLiteral("manual") ||
        qAbs(restored_focal_selection.value(QStringLiteral("focalX")).toDouble() - 0.88) > 0.001 ||
        restored_focal.documentContentHash() != controller.documentContentHash()) {
      return fail("focal point and authoritative manual crop state should survive save and reopen");
    }

    CarouselDocumentController focal_layer;
    focal_layer.addPhotoItem(focal_source_path, QStringLiteral("focal layer"));
    focal_layer.setCurrentMediaFocalPoint(0.2, 0.7);
    const auto focal_layer_map = focal_layer.canvasItems().constFirst().toMap();
    if (qAbs(focal_layer_map.value(QStringLiteral("mediaFocalX")).toDouble() - 0.2) > 0.001 ||
        qAbs(focal_layer_map.value(QStringLiteral("mediaFocalY")).toDouble() - 0.7) > 0.001 ||
        focal_layer_map.value(QStringLiteral("mediaCropMode")).toString() != QStringLiteral("smart")) {
      return fail("freeform media layers should own independent smart focal points");
    }
  }

  {
    CarouselDocumentController controller;
    controller.setCurrentSlideBackgroundColor(QStringLiteral("#ffffff"));
    controller.addShapeItem(QStringLiteral("rectangle"));
    controller.setSelectedItemCenter(540.0, 675.0);
    controller.setSelectedItemSize(640.0, 220.0);
    controller.setSelectedShapeFillColor(QStringLiteral("#ff0000"));
    controller.setSelectedShapeSecondaryFillColor(QStringLiteral("#0000ff"));
    controller.setSelectedShapeFillKind(QStringLiteral("linear-horizontal"));
    controller.setSelectedShapeStrokeWidth(0.0);

    auto selection = controller.selectionDetails();
    if (selection.value(QStringLiteral("fillKind")).toString() != QStringLiteral("linear-horizontal") ||
        selection.value(QStringLiteral("fillSecondaryColor")).toString() != QStringLiteral("#0000ff")) {
      return fail("shape gradient controls should update selection metadata");
    }

    const auto export_dir_path = dir.filePath(QStringLiteral("export-shape-gradient"));
    QDir().mkpath(export_dir_path);
    if (!controller.exportCarouselToDirectory(export_dir_path)) {
      return fail("gradient shape carousel should export");
    }
    const QImage slide(QDir(export_dir_path).filePath(QStringLiteral("carousel-01.png")));
    const QColor left = slide.pixelColor(250, 675);
    const QColor right = slide.pixelColor(830, 675);
    if (left.red() <= left.blue() || right.blue() <= right.red()) {
      return fail("horizontal shape gradients should render across exported shape layers");
    }

    controller.setDocumentName(QStringLiteral("Shape Gradient Carousel"));
    if (!controller.saveDocument()) {
      return fail("shape gradient carousel should save");
    }

    CarouselDocumentController restored;
    if (!restored.loadDocument(controller.savedDocumentId())) {
      return fail("shape gradient carousel should load");
    }
    const auto restored_items = restored.canvasItems();
    if (restored_items.size() != 1 ||
        restored_items.at(0).toMap().value(QStringLiteral("fillKind")).toString() !=
            QStringLiteral("linear-horizontal") ||
        restored_items.at(0).toMap().value(QStringLiteral("fillSecondaryColor")).toString() !=
            QStringLiteral("#0000ff")) {
      return fail("saved carousel documents should restore shape gradient settings");
    }
  }

  {
    CarouselDocumentController controller;
    controller.setCurrentSlideBackgroundColor(QStringLiteral("#ffffff"));
    controller.addShapeItem(QStringLiteral("rectangle"));
    controller.setSelectedItemCenter(540.0, 675.0);
    controller.setSelectedItemSize(520.0, 520.0);
    controller.setSelectedShapeFillColor(QStringLiteral("#ff0000"));
    controller.setSelectedShapeStrokeWidth(0.0);

    controller.addShapeItem(QStringLiteral("rectangle"));
    controller.setSelectedItemCenter(540.0, 675.0);
    controller.setSelectedItemSize(520.0, 520.0);
    controller.setSelectedShapeFillColor(QStringLiteral("#0000ff"));
    controller.setSelectedShapeStrokeWidth(0.0);
    controller.setSelectedItemBlendMode(QStringLiteral("multiply"));

    auto selection = controller.selectionDetails();
    if (selection.value(QStringLiteral("blendMode")).toString() != QStringLiteral("multiply")) {
      return fail("layer blend mode controls should update selection metadata");
    }

    const auto export_dir_path = dir.filePath(QStringLiteral("export-blend-mode"));
    QDir().mkpath(export_dir_path);
    if (!controller.exportCarouselToDirectory(export_dir_path)) {
      return fail("blend mode carousel should export");
    }
    const QImage slide(QDir(export_dir_path).filePath(QStringLiteral("carousel-01.png")));
    if (!color_matches(slide.pixelColor(540, 675), QColor(QStringLiteral("#000000")))) {
      return fail("multiply blend mode should affect exported layer pixels");
    }

    controller.setDocumentName(QStringLiteral("Blend Mode Carousel"));
    if (!controller.saveDocument()) {
      return fail("blend mode carousel should save");
    }

    CarouselDocumentController restored;
    if (!restored.loadDocument(controller.savedDocumentId())) {
      return fail("blend mode carousel should load");
    }
    const auto restored_items = restored.canvasItems();
    if (restored_items.size() != 2 ||
        restored_items.at(1).toMap().value(QStringLiteral("blendMode")).toString() != QStringLiteral("multiply")) {
      return fail("saved carousel documents should restore layer blend modes");
    }
  }

  {
    CarouselDocumentController controller;
    controller.setCurrentSlideBackgroundColor(QStringLiteral("#ffffff"));
    const auto flip_source_path = write_image(
        dir,
        QStringLiteral("flip-source.png"),
        quadrant_image(
            QSize(400, 400),
            QColor(QStringLiteral("#ff453a")),
            QColor(QStringLiteral("#34c759")),
            QColor(QStringLiteral("#0a84ff")),
            QColor(QStringLiteral("#ffcc00"))));
    if (flip_source_path.isEmpty()) {
      return fail("flip source image should save");
    }

    controller.addPhotoItem(flip_source_path, QStringLiteral("flip source"));
    controller.setSelectedItemCenter(540.0, 675.0);
    controller.setSelectedItemSize(400.0, 400.0);
    controller.setSelectedItemFlippedHorizontal(true);
    controller.setSelectedItemFlippedVertical(true);

    auto selection = controller.selectionDetails();
    if (!selection.value(QStringLiteral("flippedHorizontal")).toBool() ||
        !selection.value(QStringLiteral("flippedVertical")).toBool()) {
      return fail("layer flip controls should update selection metadata");
    }

    const auto export_dir_path = dir.filePath(QStringLiteral("export-flipped-layer"));
    QDir().mkpath(export_dir_path);
    if (!controller.exportCarouselToDirectory(export_dir_path)) {
      return fail("flipped layer carousel should export");
    }
    const QImage slide(QDir(export_dir_path).filePath(QStringLiteral("carousel-01.png")));
    if (!color_is_dominantly_yellow(slide.pixelColor(390, 525)) ||
        !color_is_dominantly_red(slide.pixelColor(690, 825))) {
      return fail("flipped layer export should mirror source pixels horizontally and vertically");
    }

    controller.setDocumentName(QStringLiteral("Flipped Layer Carousel"));
    if (!controller.saveDocument()) {
      return fail("flipped layer carousel should save");
    }

    CarouselDocumentController restored;
    if (!restored.loadDocument(controller.savedDocumentId())) {
      return fail("flipped layer carousel should load");
    }
    const auto restored_items = restored.canvasItems();
    if (restored_items.size() != 1 ||
        !restored_items.at(0).toMap().value(QStringLiteral("flippedHorizontal")).toBool() ||
        !restored_items.at(0).toMap().value(QStringLiteral("flippedVertical")).toBool()) {
      return fail("saved carousel documents should restore layer flip state");
    }
  }

  {
    CarouselDocumentController controller;
    controller.setCurrentSlideBackgroundColor(QStringLiteral("#000000"));
    QImage square_source(QSize(400, 400), QImage::Format_ARGB32);
    square_source.fill(QColor(QStringLiteral("#34c759")));
    const auto square_source_path = write_image(dir, QStringLiteral("fit-square-source.png"), square_source);
    QImage wide_source(QSize(800, 400), QImage::Format_ARGB32);
    wide_source.fill(QColor(QStringLiteral("#ff453a")));
    const auto wide_source_path = write_image(dir, QStringLiteral("fit-wide-source.png"), wide_source);
    if (square_source_path.isEmpty() || wide_source_path.isEmpty()) {
      return fail("fit mode source images should save");
    }

    controller.addPhotoItem(square_source_path, QStringLiteral("square source"));
    controller.setSelectedItemCenter(540.0, 675.0);
    controller.setSelectedItemSize(400.0, 400.0);
    controller.replaceSelectedPhotoItem(wide_source_path, QStringLiteral("wide source"), QString{});
    controller.setCurrentFitMode(QStringLiteral("contain"));

    const auto selection = controller.selectionDetails();
    if (selection.value(QStringLiteral("itemFitMode")).toString() != QStringLiteral("contain")) {
      return fail("selected layer fit mode should be exposed in selection metadata");
    }

    const auto export_dir_path = dir.filePath(QStringLiteral("export-layer-fit-mode"));
    QDir().mkpath(export_dir_path);
    if (!controller.exportCarouselToDirectory(export_dir_path)) {
      return fail("layer fit mode carousel should export");
    }
    const QImage slide(QDir(export_dir_path).filePath(QStringLiteral("carousel-01.png")));
    if (!color_matches(slide.pixelColor(540, 525), QColor(QStringLiteral("#000000"))) ||
        !color_is_dominantly_red(slide.pixelColor(540, 675))) {
      return fail("contain fit mode should letterbox replaced media inside a selected layer frame");
    }

    controller.setDocumentName(QStringLiteral("Layer Fit Mode Carousel"));
    if (!controller.saveDocument()) {
      return fail("layer fit mode carousel should save");
    }

    CarouselDocumentController restored;
    if (!restored.loadDocument(controller.savedDocumentId())) {
      return fail("layer fit mode carousel should load");
    }
    const auto restored_items = restored.canvasItems();
    if (restored_items.size() != 1 ||
        restored_items.at(0).toMap().value(QStringLiteral("fitMode")).toString() != QStringLiteral("contain")) {
      return fail("saved carousel documents should restore selected layer fit mode");
    }
  }

  {
    CarouselDocumentController controller;
    controller.setCurrentSlideBackgroundColor(QStringLiteral("#000000"));
    QImage square_source(QSize(400, 400), QImage::Format_ARGB32);
    square_source.fill(QColor(QStringLiteral("#34c759")));
    const auto square_source_path = write_image(dir, QStringLiteral("crop-square-source.png"), square_source);
    const auto wide_source_path = write_image(
        dir,
        QStringLiteral("crop-wide-source.png"),
        split_image(QSize(800, 400), QColor(QStringLiteral("#ff453a")), QColor(QStringLiteral("#0a84ff"))));
    if (square_source_path.isEmpty() || wide_source_path.isEmpty()) {
      return fail("layer crop source images should save");
    }

    controller.addPhotoItem(square_source_path, QStringLiteral("crop square source"));
    controller.setSelectedItemCenter(540.0, 675.0);
    controller.setSelectedItemSize(400.0, 400.0);
    controller.replaceSelectedPhotoItem(wide_source_path, QStringLiteral("crop wide source"), QString{});
    controller.setCurrentFitMode(QStringLiteral("cover"));
    controller.setSelectedItemMediaScale(1.0);
    controller.setSelectedItemMediaOffset(1.0, 0.0);

    auto selection = controller.selectionDetails();
    if (selection.value(QStringLiteral("mediaScale")).toDouble() != 1.0 ||
        selection.value(QStringLiteral("mediaOffsetX")).toDouble() != 1.0 ||
        selection.value(QStringLiteral("mediaOffsetY")).toDouble() != 0.0) {
      return fail("layer media crop controls should update selection metadata");
    }

    const auto export_dir_path = dir.filePath(QStringLiteral("export-layer-media-crop"));
    QDir().mkpath(export_dir_path);
    if (!controller.exportCarouselToDirectory(export_dir_path)) {
      return fail("cropped layer carousel should export");
    }
    const QImage slide(QDir(export_dir_path).filePath(QStringLiteral("carousel-01.png")));
    if (!color_is_dominantly_blue(slide.pixelColor(540, 675))) {
      return fail("layer media pan should shift the exported crop inside the selected frame");
    }

    controller.setDocumentName(QStringLiteral("Layer Media Crop Carousel"));
    if (!controller.saveDocument()) {
      return fail("layer media crop carousel should save");
    }

    CarouselDocumentController restored;
    if (!restored.loadDocument(controller.savedDocumentId())) {
      return fail("layer media crop carousel should load");
    }
    const auto restored_items = restored.canvasItems();
    if (restored_items.size() != 1 ||
        restored_items.at(0).toMap().value(QStringLiteral("mediaScale")).toDouble() != 1.0 ||
        restored_items.at(0).toMap().value(QStringLiteral("mediaOffsetX")).toDouble() != 1.0 ||
        restored_items.at(0).toMap().value(QStringLiteral("mediaOffsetY")).toDouble() != 0.0) {
      return fail("saved carousel documents should restore layer media crop settings");
    }
  }

  {
    CarouselDocumentController controller;
    controller.setCurrentSlideBackgroundColor(QStringLiteral("#ffffff"));
    QImage mask_source(QSize(400, 400), QImage::Format_ARGB32);
    mask_source.fill(QColor(QStringLiteral("#0a84ff")));
    const auto mask_source_path = write_image(dir, QStringLiteral("ellipse-mask-source.png"), mask_source);
    if (mask_source_path.isEmpty()) {
      return fail("ellipse mask source image should save");
    }

    controller.addPhotoItem(mask_source_path, QStringLiteral("ellipse mask source"));
    controller.setSelectedItemCenter(540.0, 675.0);
    controller.setSelectedItemSize(400.0, 400.0);
    controller.setSelectedItemMediaMaskShape(QStringLiteral("ellipse"));

    auto selection = controller.selectionDetails();
    if (selection.value(QStringLiteral("mediaMaskShape")).toString() != QStringLiteral("ellipse")) {
      return fail("layer media mask control should update selection metadata");
    }

    const auto export_dir_path = dir.filePath(QStringLiteral("export-layer-media-mask"));
    QDir().mkpath(export_dir_path);
    if (!controller.exportCarouselToDirectory(export_dir_path)) {
      return fail("masked layer carousel should export");
    }
    const QImage slide(QDir(export_dir_path).filePath(QStringLiteral("carousel-01.png")));
    if (!color_matches(slide.pixelColor(350, 485), QColor(QStringLiteral("#ffffff"))) ||
        !color_is_dominantly_blue(slide.pixelColor(540, 675))) {
      return fail("ellipse media mask should clip exported photo corners without removing the content center");
    }

    controller.setDocumentName(QStringLiteral("Layer Media Mask Carousel"));
    if (!controller.saveDocument()) {
      return fail("layer media mask carousel should save");
    }

    CarouselDocumentController restored;
    if (!restored.loadDocument(controller.savedDocumentId())) {
      return fail("layer media mask carousel should load");
    }
    const auto restored_items = restored.canvasItems();
    if (restored_items.size() != 1 ||
        restored_items.at(0).toMap().value(QStringLiteral("mediaMaskShape")).toString() !=
            QStringLiteral("ellipse")) {
      return fail("saved carousel documents should restore layer media mask settings");
    }
  }

  {
    CarouselDocumentController controller;
    controller.setCurrentSlideBackgroundColor(QStringLiteral("#ffffff"));
    QImage frame_source(QSize(400, 400), QImage::Format_ARGB32);
    frame_source.fill(QColor(QStringLiteral("#0a84ff")));
    const auto frame_source_path = write_image(dir, QStringLiteral("frame-source.png"), frame_source);
    if (frame_source_path.isEmpty()) {
      return fail("frame source image should save");
    }

    controller.addPhotoItem(frame_source_path, QStringLiteral("frame source"));
    controller.setSelectedItemCenter(540.0, 675.0);
    controller.setSelectedItemSize(400.0, 400.0);
    controller.setSelectedItemFrameColor(QStringLiteral("#ff453a"));
    controller.setSelectedItemFrameWidth(40.0);

    auto selection = controller.selectionDetails();
    if (selection.value(QStringLiteral("frameColor")).toString() != QStringLiteral("#ff453a") ||
        selection.value(QStringLiteral("frameWidth")).toDouble() != 40.0) {
      return fail("layer frame controls should update selection metadata");
    }

    const auto export_dir_path = dir.filePath(QStringLiteral("export-layer-frame"));
    QDir().mkpath(export_dir_path);
    if (!controller.exportCarouselToDirectory(export_dir_path)) {
      return fail("framed layer carousel should export");
    }
    const QImage slide(QDir(export_dir_path).filePath(QStringLiteral("carousel-01.png")));
    if (!color_matches(slide.pixelColor(540, 490), QColor(QStringLiteral("#ff453a"))) ||
        !color_is_dominantly_blue(slide.pixelColor(540, 675))) {
      return fail("layer frame should render over exported image edges without covering the content center");
    }

    controller.setDocumentName(QStringLiteral("Layer Frame Carousel"));
    if (!controller.saveDocument()) {
      return fail("layer frame carousel should save");
    }

    CarouselDocumentController restored;
    if (!restored.loadDocument(controller.savedDocumentId())) {
      return fail("layer frame carousel should load");
    }
    const auto restored_items = restored.canvasItems();
    if (restored_items.size() != 1 ||
        restored_items.at(0).toMap().value(QStringLiteral("frameColor")).toString() != QStringLiteral("#ff453a") ||
        restored_items.at(0).toMap().value(QStringLiteral("frameWidth")).toDouble() != 40.0) {
      return fail("saved carousel documents should restore layer frame settings");
    }
  }

  {
    CarouselDocumentController controller;
    controller.setCurrentSlideBackgroundColor(QStringLiteral("#ffffff"));
    QImage radius_source(QSize(400, 400), QImage::Format_ARGB32);
    radius_source.fill(QColor(QStringLiteral("#0a84ff")));
    const auto radius_source_path = write_image(dir, QStringLiteral("radius-source.png"), radius_source);
    if (radius_source_path.isEmpty()) {
      return fail("rounded corner source image should save");
    }

    controller.addPhotoItem(radius_source_path, QStringLiteral("radius source"));
    controller.setSelectedItemCenter(540.0, 675.0);
    controller.setSelectedItemSize(400.0, 400.0);
    controller.setSelectedItemCornerRadius(140.0);

    auto selection = controller.selectionDetails();
    if (selection.value(QStringLiteral("cornerRadius")).toDouble() != 140.0) {
      return fail("layer radius control should update selection metadata");
    }

    const auto export_dir_path = dir.filePath(QStringLiteral("export-layer-radius"));
    QDir().mkpath(export_dir_path);
    if (!controller.exportCarouselToDirectory(export_dir_path)) {
      return fail("rounded layer carousel should export");
    }
    const QImage slide(QDir(export_dir_path).filePath(QStringLiteral("carousel-01.png")));
    if (!color_matches(slide.pixelColor(350, 485), QColor(QStringLiteral("#ffffff"))) ||
        !color_is_dominantly_blue(slide.pixelColor(540, 675))) {
      return fail("rounded layer radius should clip exported photo corners without removing the content center");
    }

    controller.setDocumentName(QStringLiteral("Rounded Layer Carousel"));
    if (!controller.saveDocument()) {
      return fail("rounded layer carousel should save");
    }

    CarouselDocumentController restored;
    if (!restored.loadDocument(controller.savedDocumentId())) {
      return fail("rounded layer carousel should load");
    }
    const auto restored_items = restored.canvasItems();
    if (restored_items.size() != 1 ||
        restored_items.at(0).toMap().value(QStringLiteral("cornerRadius")).toDouble() != 140.0) {
      return fail("saved carousel documents should restore layer radius settings");
    }
  }

  {
    CarouselDocumentController controller;
    controller.setCurrentSlideBackgroundColor(QStringLiteral("#ffffff"));

    controller.addShapeItem(QStringLiteral("rectangle"));
    const auto red_id = controller.selectedItemId();
    controller.setSelectedItemCenter(540.0, 675.0);
    controller.setSelectedItemSize(420.0, 420.0);
    controller.setSelectedShapeFillColor(QStringLiteral("#ff453a"));
    controller.setSelectedShapeStrokeWidth(0.0);

    controller.addShapeItem(QStringLiteral("rectangle"));
    controller.setSelectedItemCenter(540.0, 675.0);
    controller.setSelectedItemSize(420.0, 420.0);
    controller.setSelectedShapeFillColor(QStringLiteral("#0a84ff"));
    controller.setSelectedShapeStrokeWidth(0.0);

    controller.addShapeItem(QStringLiteral("rectangle"));
    const auto green_id = controller.selectedItemId();
    controller.setSelectedItemCenter(540.0, 675.0);
    controller.setSelectedItemSize(420.0, 420.0);
    controller.setSelectedShapeFillColor(QStringLiteral("#34c759"));
    controller.setSelectedShapeStrokeWidth(0.0);

    controller.selectItem(red_id);
    controller.bringSelectedToFront();
    auto items = controller.canvasItems();
    if (items.size() != 3 || items.back().toMap().value(QStringLiteral("itemId")).toString() != red_id) {
      return fail("bring to front should move the selected layer to the top of the layer stack");
    }

    auto export_dir_path = dir.filePath(QStringLiteral("export-arrange-front"));
    QDir().mkpath(export_dir_path);
    if (!controller.exportCarouselToDirectory(export_dir_path)) {
      return fail("front-arranged carousel should export");
    }
    QImage slide(QDir(export_dir_path).filePath(QStringLiteral("carousel-01.png")));
    if (!color_matches(slide.pixelColor(540, 675), QColor(QStringLiteral("#ff453a")))) {
      return fail("bring to front should affect exported layer compositing order");
    }

    controller.sendSelectedToBack();
    items = controller.canvasItems();
    if (items.size() != 3 || items.front().toMap().value(QStringLiteral("itemId")).toString() != red_id) {
      return fail("send to back should move the selected layer to the bottom of the layer stack");
    }

    export_dir_path = dir.filePath(QStringLiteral("export-arrange-back"));
    QDir().mkpath(export_dir_path);
    if (!controller.exportCarouselToDirectory(export_dir_path)) {
      return fail("back-arranged carousel should export");
    }
    slide = QImage(QDir(export_dir_path).filePath(QStringLiteral("carousel-01.png")));
    if (!color_matches(slide.pixelColor(540, 675), QColor(QStringLiteral("#34c759"))) ||
        items.back().toMap().value(QStringLiteral("itemId")).toString() != green_id) {
      return fail("send to back should leave later layers composited above the selected layer");
    }
  }

  {
    CarouselDocumentController controller;
    controller.setCurrentSlideBackgroundColor(QStringLiteral("#ffffff"));
    controller.addShapeItem(QStringLiteral("rectangle"));
    controller.setSelectedItemCenter(540.0, 675.0);
    controller.setSelectedItemSize(300.0, 300.0);
    controller.setSelectedShapeFillColor(QStringLiteral("#0a84ff"));
    controller.setSelectedShapeStrokeWidth(0.0);
    controller.setSelectedItemShadowColor(QStringLiteral("#000000"));
    controller.setSelectedItemShadowOpacity(1.0);
    controller.setSelectedItemShadowOffset(80.0, 80.0);

    auto selection = controller.selectionDetails();
    if (selection.value(QStringLiteral("shadowColor")).toString() != QStringLiteral("#000000") ||
        selection.value(QStringLiteral("shadowOpacity")).toDouble() != 1.0 ||
        selection.value(QStringLiteral("shadowOffsetX")).toDouble() != 80.0 ||
        selection.value(QStringLiteral("shadowOffsetY")).toDouble() != 80.0) {
      return fail("layer shadow controls should update selection metadata");
    }

    const auto export_dir_path = dir.filePath(QStringLiteral("export-layer-shadow"));
    QDir().mkpath(export_dir_path);
    if (!controller.exportCarouselToDirectory(export_dir_path)) {
      return fail("shadowed layer carousel should export");
    }
    const QImage slide(QDir(export_dir_path).filePath(QStringLiteral("carousel-01.png")));
    if (!color_matches(slide.pixelColor(540, 675), QColor(QStringLiteral("#0a84ff"))) ||
        !color_matches(slide.pixelColor(740, 860), QColor(QStringLiteral("#000000")))) {
      return fail("layer shadow should render behind exported content at the configured offset");
    }

    controller.setDocumentName(QStringLiteral("Layer Shadow Carousel"));
    if (!controller.saveDocument()) {
      return fail("layer shadow carousel should save");
    }

    CarouselDocumentController restored;
    if (!restored.loadDocument(controller.savedDocumentId())) {
      return fail("layer shadow carousel should load");
    }
    const auto restored_items = restored.canvasItems();
    if (restored_items.size() != 1 ||
        restored_items.at(0).toMap().value(QStringLiteral("shadowColor")).toString() != QStringLiteral("#000000") ||
        restored_items.at(0).toMap().value(QStringLiteral("shadowOpacity")).toDouble() != 1.0 ||
        restored_items.at(0).toMap().value(QStringLiteral("shadowOffsetX")).toDouble() != 80.0 ||
        restored_items.at(0).toMap().value(QStringLiteral("shadowOffsetY")).toDouble() != 80.0) {
      return fail("saved carousel documents should restore layer shadow settings");
    }
  }

  {
    CarouselDocumentController controller;
    controller.setDocumentFormat(QStringLiteral("5x4"));
    const auto slot_drag_path = write_image(
        dir,
        QStringLiteral("slot-drag-source.png"),
        QImage(QSize(1600, 1080), QImage::Format_ARGB32));
    if (slot_drag_path.isEmpty()) {
      return fail("slot drag source image should save");
    }

    QImage slot_drag_image(slot_drag_path);
    slot_drag_image.fill(QColor(QStringLiteral("#64d2ff")));
    if (!slot_drag_image.save(slot_drag_path)) {
      return fail("slot drag source image should save after fill");
    }

    controller.applyTemplateToCurrent(QStringLiteral("split-pair"));
    controller.assignPrimaryImage(slot_drag_path, QStringLiteral("slot drag"));

    const auto slides = controller.slides();
    if (slides.isEmpty() || !slides.at(0).toMap().value(QStringLiteral("primaryHasMedia")).toBool() ||
        slides.at(0).toMap().value(QStringLiteral("secondaryHasMedia")).toBool()) {
      return fail("slide metadata should expose which template slots currently contain media");
    }

    controller.moveCurrentMediaByPixels(10000.0, 0.0);
    auto selection = controller.selectionDetails();
    if (selection.value(QStringLiteral("offsetX")).toDouble() != -1.0) {
      return fail("slot media dragging should clamp to the right edge of the crop range");
    }

    controller.moveCurrentMediaByPixels(-20000.0, 0.0);
    selection = controller.selectionDetails();
    if (selection.value(QStringLiteral("offsetX")).toDouble() != 1.0) {
      return fail("slot media dragging should clamp to the left edge of the crop range");
    }
  }

  {
    CarouselDocumentController controller;
    const auto video_path = dir.filePath(QStringLiteral("placeholder.mp4"));
    QFile video_file(video_path);
    if (!video_file.open(QIODevice::WriteOnly)) {
      return fail("video placeholder file should open");
    }
    video_file.write("not-a-real-video");
    video_file.close();

    controller.assignPrimaryVideo(video_path, QStringLiteral("clip"));
    const auto selection = controller.selectionDetails();
    if (selection.value(QStringLiteral("mediaType")).toString() != QStringLiteral("video")) {
      return fail("assigning a video should restore video media on the active single canvas");
    }
    if (!selection.value(QStringLiteral("canPlaceVideo")).toBool()) {
      return fail("single template should continue to advertise video support");
    }
  }

  {
    const auto ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
    const auto ffprobe = QStandardPaths::findExecutable(QStringLiteral("ffprobe"));
    if (ffmpeg.isEmpty() || ffprobe.isEmpty()) {
      return fail("video composition proof requires ffmpeg and ffprobe");
    }

    const auto video_path = dir.filePath(QStringLiteral("video-composition-source.mp4"));
    QProcess fixture_process;
    fixture_process.start(
        ffmpeg,
        {QStringLiteral("-y"), QStringLiteral("-f"), QStringLiteral("lavfi"),
         QStringLiteral("-i"), QStringLiteral("color=c=blue:s=320x400:r=24:d=2"),
         QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
         QStringLiteral("sine=frequency=440:sample_rate=44100:duration=2"),
         QStringLiteral("-shortest"), QStringLiteral("-c:v"), QStringLiteral("libx264"),
         QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"), QStringLiteral("-c:a"),
         QStringLiteral("aac"), video_path});
    if (!fixture_process.waitForStarted(5000) || !fixture_process.waitForFinished(30000) ||
        fixture_process.exitCode() != 0) {
      return fail("video composition source fixture should render with deterministic audio");
    }

    CarouselDocumentController controller;
    controller.setDocumentName(QStringLiteral("Video Composition Proof"));
    controller.assignPrimaryVideo(video_path, QStringLiteral("timed clip"));
    auto selection = controller.selectionDetails();
    const auto source_duration = selection.value(QStringLiteral("videoDurationSeconds")).toDouble();
    if (source_duration < 1.9 || source_duration > 2.1 ||
        selection.value(QStringLiteral("videoAudioPolicy")).toString() != QStringLiteral("mute") ||
        !selection.value(QStringLiteral("videoLoopIntent")).toBool() ||
        selection.value(QStringLiteral("videoSourceUrl")).toString().isEmpty()) {
      return fail("assigned board video should expose duration, safe defaults, and a playback source");
    }

    controller.setCurrentVideoTrim(0.35, 1.25);
    controller.setCurrentVideoPosterFrame(0.7);
    controller.setCurrentVideoAudioPolicy(QStringLiteral("preserve"));
    controller.setCurrentVideoLoopIntent(false);
    selection = controller.selectionDetails();
    if (qAbs(selection.value(QStringLiteral("videoTrimInSeconds")).toDouble() - 0.35) > 0.001 ||
        qAbs(selection.value(QStringLiteral("videoTrimOutSeconds")).toDouble() - 1.25) > 0.001 ||
        qAbs(selection.value(QStringLiteral("videoPosterSeconds")).toDouble() - 0.7) > 0.001 ||
        selection.value(QStringLiteral("videoAudioPolicy")).toString() != QStringLiteral("preserve") ||
        selection.value(QStringLiteral("videoLoopIntent")).toBool()) {
      return fail("board video controls should persist exact trim, poster, audio, and loop intent state");
    }
    controller.undo();
    if (!controller.selectionDetails().value(QStringLiteral("videoLoopIntent")).toBool()) {
      return fail("video loop intent should be undoable");
    }
    controller.redo();
    if (controller.selectionDetails().value(QStringLiteral("videoLoopIntent")).toBool()) {
      return fail("video loop intent should be redoable");
    }

    const auto preserve_dir = dir.filePath(QStringLiteral("video-composition-preserve"));
    QDir().mkpath(preserve_dir);
    if (!controller.exportCarouselToDirectory(preserve_dir)) {
      return fail("trimmed board video with preserved audio should export");
    }
    const auto preserved_output = QDir(preserve_dir).filePath(QStringLiteral("carousel-01.mp4"));
    const auto preserved_duration = probe_media_duration(ffprobe, preserved_output);
    if (qAbs(preserved_duration - 0.9) > 0.15 ||
        !media_has_audio_stream(ffprobe, preserved_output)) {
      return fail("video export should enforce the selected trim and preserve audio when requested");
    }

    controller.setCurrentVideoAudioPolicy(QStringLiteral("mute"));
    const auto muted_dir = dir.filePath(QStringLiteral("video-composition-muted"));
    QDir().mkpath(muted_dir);
    if (!controller.exportCarouselToDirectory(muted_dir)) {
      return fail("trimmed muted board video should export");
    }
    const auto muted_output = QDir(muted_dir).filePath(QStringLiteral("carousel-01.mp4"));
    if (qAbs(probe_media_duration(ffprobe, muted_output) - 0.9) > 0.15 ||
        media_has_audio_stream(ffprobe, muted_output)) {
      return fail("mute policy should remove audio without changing the selected trim");
    }

    controller.setCurrentVideoAudioPolicy(QStringLiteral("preserve"));
    controller.addVideoItem(video_path, QStringLiteral("layer clip"));
    controller.setCurrentVideoTrim(0.2, 1.6);
    controller.setCurrentVideoPosterFrame(1.1);
    controller.setCurrentVideoAudioPolicy(QStringLiteral("preserve"));
    controller.setCurrentVideoLoopIntent(false);
    const auto layer_selection = controller.selectionDetails();
    if (!layer_selection.value(QStringLiteral("isVideo")).toBool() ||
        qAbs(layer_selection.value(QStringLiteral("videoTrimInSeconds")).toDouble() - 0.2) > 0.001 ||
        qAbs(layer_selection.value(QStringLiteral("videoTrimOutSeconds")).toDouble() - 1.6) > 0.001 ||
        qAbs(layer_selection.value(QStringLiteral("videoPosterSeconds")).toDouble() - 1.1) > 0.001 ||
        layer_selection.value(QStringLiteral("videoAudioPolicy")).toString() != QStringLiteral("preserve") ||
        layer_selection.value(QStringLiteral("videoLoopIntent")).toBool()) {
      return fail("freeform video layers should use the same composition controls as board video slots");
    }

    if (!controller.saveDocument()) {
      return fail("video composition state should save");
    }
    CarouselDocumentController restored;
    if (!restored.loadDocument(controller.savedDocumentId())) {
      return fail("video composition state should reopen");
    }
    const auto restored_items = restored.canvasItems();
    if (restored_items.size() != 1 ||
        qAbs(restored_items.constFirst().toMap().value(
                 QStringLiteral("videoTrimInSeconds")).toDouble() - 0.2) > 0.001 ||
        qAbs(restored_items.constFirst().toMap().value(
                 QStringLiteral("videoTrimOutSeconds")).toDouble() - 1.6) > 0.001 ||
        qAbs(restored_items.constFirst().toMap().value(
                 QStringLiteral("videoPosterSeconds")).toDouble() - 1.1) > 0.001 ||
        restored_items.constFirst().toMap().value(
            QStringLiteral("videoAudioPolicy")).toString() != QStringLiteral("preserve") ||
        restored_items.constFirst().toMap().value(
            QStringLiteral("videoLoopIntent")).toBool()) {
      return fail("saved documents should restore video-layer trim, poster, audio, and loop state");
    }
    restored.clearSelection();
    const auto restored_slot = restored.selectionDetails();
    if (qAbs(restored_slot.value(QStringLiteral("videoTrimInSeconds")).toDouble() - 0.35) > 0.001 ||
        qAbs(restored_slot.value(QStringLiteral("videoTrimOutSeconds")).toDouble() - 1.25) > 0.001 ||
        qAbs(restored_slot.value(QStringLiteral("videoPosterSeconds")).toDouble() - 0.7) > 0.001 ||
        restored_slot.value(QStringLiteral("videoAudioPolicy")).toString() != QStringLiteral("preserve") ||
        restored_slot.value(QStringLiteral("videoLoopIntent")).toBool()) {
      return fail("saved documents should restore board-video composition state");
    }
  }

  {
    CarouselDocumentController controller;
    const auto video_path = dir.filePath(QStringLiteral("layer-placeholder.mp4"));
    QFile video_file(video_path);
    if (!video_file.open(QIODevice::WriteOnly)) {
      return fail("video layer placeholder file should open");
    }
    video_file.write("not-a-real-video");
    video_file.close();

    controller.addVideoItem(video_path, QStringLiteral("layer clip"));
    auto selection = controller.selectionDetails();
    if (selection.value(QStringLiteral("itemType")).toString() != QStringLiteral("video") ||
        !selection.value(QStringLiteral("isVideo")).toBool()) {
      return fail("adding a video layer should select it as a video item");
    }

    controller.setSelectedItemCenter(540.0, 675.0);
    controller.setSelectedItemSize(420.0, 760.0);

    const auto export_dir_path = dir.filePath(QStringLiteral("export-video-layer"));
    QDir().mkpath(export_dir_path);
    if (!controller.exportCarouselToDirectory(export_dir_path)) {
      return fail("video layer placeholder export should succeed");
    }

    const QImage slide(QDir(export_dir_path).filePath(QStringLiteral("carousel-01.png")));
    if (!region_contains_non_background(
            slide,
            QRect(300, 220, 480, 900),
            QColor(QStringLiteral("#fbf7f1")))) {
      return fail("video layers should render a visible placeholder into exported slides");
    }

    controller.setDocumentName(QStringLiteral("Video Layer Carousel"));
    if (!controller.saveDocument()) {
      return fail("video layer carousel should save");
    }

    CarouselDocumentController restored;
    if (!restored.loadDocument(controller.savedDocumentId())) {
      return fail("video layer carousel should load");
    }

    const auto restored_items = restored.canvasItems();
    if (restored_items.size() != 1 ||
        restored_items.at(0).toMap().value(QStringLiteral("type")).toString() != QStringLiteral("video")) {
      return fail("saved carousel documents should restore video layer items");
    }
  }

  {
    CarouselDocumentController controller;
    controller.addSlide();
    controller.selectSlide(0);

    const auto source_path = write_image(
        dir,
        QStringLiteral("spread.png"),
        split_image(QSize(2160, 1350), QColor(QStringLiteral("#ff2d55")), QColor(QStringLiteral("#1e90ff"))));
    if (source_path.isEmpty()) {
      return fail("spread source image should save");
    }

    controller.addPhotoItem(source_path, QStringLiteral("spread"));
    controller.setSelectedItemCenter(1080.0, 675.0);
    controller.setSelectedItemSize(2160.0, 1350.0);

    const auto export_dir_path = dir.filePath(QStringLiteral("export-spread"));
    QDir().mkpath(export_dir_path);
    if (!controller.exportCarouselToDirectory(export_dir_path)) {
      return fail("spread export should succeed");
    }

    const QImage first_slide(QDir(export_dir_path).filePath(QStringLiteral("carousel-01.png")));
    const QImage second_slide(QDir(export_dir_path).filePath(QStringLiteral("carousel-02.png")));
    if (first_slide.size() != QSize(1080, 1350) || second_slide.size() != QSize(1080, 1350)) {
      return fail("continuous spread export should use the active slide size");
    }
    if (!color_is_dominantly_red(
            first_slide.pixelColor(first_slide.width() / 2,
                                   first_slide.height() / 2)) ||
        !color_is_dominantly_blue(
            second_slide.pixelColor(second_slide.width() / 2,
                                    second_slide.height() / 2))) {
      return fail("continuous spread export should cut a single wide photo cleanly across the slide seam");
    }
    if (controller.documentPixelWidth() != 2160 || controller.slideCount() != 2) {
      return fail("adding a second canvas should extend the document into a continuous two-slide strip");
    }
  }

  {
    CarouselDocumentController controller;
    controller.addSlide();
    const auto green_path = write_image(
        dir,
        QStringLiteral("green.png"),
        QImage(QSize(800, 800), QImage::Format_ARGB32));
    const auto blue_path = write_image(
        dir,
        QStringLiteral("blue.png"),
        QImage(QSize(800, 800), QImage::Format_ARGB32));
    if (green_path.isEmpty() || blue_path.isEmpty()) {
      return fail("photo layer sources should save");
    }

    QImage green_image(green_path);
    green_image.fill(QColor(QStringLiteral("#34c759")));
    if (!green_image.save(green_path)) {
      return fail("green layer source should save");
    }

    QImage blue_image(blue_path);
    blue_image.fill(QColor(QStringLiteral("#0a84ff")));
    if (!blue_image.save(blue_path)) {
      return fail("blue layer source should save");
    }

    controller.selectSlide(0);
    controller.addPhotoItem(green_path, QStringLiteral("green"));
    controller.setSelectedItemCenter(320.0, 720.0);
    controller.setSelectedItemSize(480.0, 480.0);

    controller.selectSlide(1);
    controller.addPhotoItem(blue_path, QStringLiteral("blue"));
    controller.setSelectedItemCenter(1620.0, 720.0);
    controller.setSelectedItemSize(480.0, 480.0);

    controller.selectSlide(0);
    controller.addTextItem(QStringLiteral("HELLO"));
    controller.setSelectedItemCenter(540.0, 180.0);
    controller.setSelectedTextColor(QStringLiteral("#ff453a"));
    controller.setSelectedTextPointSize(120.0);

    const auto export_dir_path = dir.filePath(QStringLiteral("export-layers"));
    QDir().mkpath(export_dir_path);
    if (!controller.exportCarouselToDirectory(export_dir_path)) {
      return fail("multi-layer export should succeed");
    }

    const QImage first_slide(QDir(export_dir_path).filePath(QStringLiteral("carousel-01.png")));
    const QImage second_slide(QDir(export_dir_path).filePath(QStringLiteral("carousel-02.png")));
    if (!color_is_dominantly_green(first_slide.pixelColor(320, 720))) {
      return fail("first canvas should keep its photo layer in place");
    }
    if (!color_is_dominantly_blue(second_slide.pixelColor(540, 720))) {
      return fail("second canvas should keep its photo layer in place");
    }
    if (!region_contains_non_background(
            first_slide,
            QRect(180, 40, 720, 260),
            QColor(QStringLiteral("#fbf7f1")))) {
      return fail("text layers should render into exported slides");
    }
  }

  {
    CarouselDocumentController controller;
    controller.addTextItem(QStringLiteral("Resize me wider because this sentence needs to wrap across multiple lines."));
    controller.setSelectedItemSize(720.0, 180.0);

    auto selection = controller.selectionDetails();
    if (selection.value(QStringLiteral("width")).toDouble() != 720.0) {
      return fail("resizing a text layer should apply the requested width");
    }

    const auto resized_height = selection.value(QStringLiteral("height")).toDouble();
    controller.setSelectedItemSize(320.0, 180.0);
    selection = controller.selectionDetails();
    if (selection.value(QStringLiteral("width")).toDouble() != 320.0) {
      return fail("text layer resize should continue to track subsequent width changes");
    }
    if (selection.value(QStringLiteral("height")).toDouble() <= resized_height) {
      return fail("narrowing a text layer should recompute its wrapped text height");
    }
  }

  {
    CarouselDocumentController controller;
    controller.addTextItem(QStringLiteral("STYLE"));
    controller.setSelectedItemCenter(540.0, 240.0);
    controller.setSelectedItemSize(720.0, 220.0);
    controller.setSelectedTextColor(QStringLiteral("#000000"));
    controller.setSelectedTextPointSize(128.0);
    controller.setSelectedTextFontFamily(QStringLiteral("serif"));
    controller.setSelectedTextWeight(QStringLiteral("black"));
    controller.setSelectedTextItalic(true);

    auto selection = controller.selectionDetails();
    if (selection.value(QStringLiteral("textFontFamily")).toString() != QStringLiteral("serif") ||
        selection.value(QStringLiteral("textWeight")).toString() != QStringLiteral("black") ||
        !selection.value(QStringLiteral("textItalic")).toBool()) {
      return fail("text typography controls should update selection metadata");
    }

    const auto export_dir_path = dir.filePath(QStringLiteral("export-text-typography"));
    QDir().mkpath(export_dir_path);
    if (!controller.exportCarouselToDirectory(export_dir_path)) {
      return fail("text typography carousel should export");
    }
    const QImage slide(QDir(export_dir_path).filePath(QStringLiteral("carousel-01.png")));
    if (!region_contains_non_background(
            slide,
            QRect(180, 100, 720, 300),
            QColor(QStringLiteral("#fbf7f1")))) {
      return fail("styled text should render into exported slides");
    }

    controller.setDocumentName(QStringLiteral("Text Typography Carousel"));
    if (!controller.saveDocument()) {
      return fail("text typography carousel should save");
    }

    CarouselDocumentController restored;
    if (!restored.loadDocument(controller.savedDocumentId())) {
      return fail("text typography carousel should load");
    }
    const auto restored_items = restored.canvasItems();
    if (restored_items.size() != 1 ||
        restored_items.at(0).toMap().value(QStringLiteral("textFontFamily")).toString() != QStringLiteral("serif") ||
        restored_items.at(0).toMap().value(QStringLiteral("textWeight")).toString() != QStringLiteral("black") ||
        !restored_items.at(0).toMap().value(QStringLiteral("textItalic")).toBool()) {
      return fail("saved carousel documents should restore text typography settings");
    }
  }

  {
    CarouselDocumentController controller;
    controller.setCurrentSlideBackgroundColor(QStringLiteral("#ffffff"));
    controller.addTextItem(QStringLiteral("OUTLINE"));
    controller.setSelectedItemCenter(540.0, 675.0);
    controller.setSelectedItemSize(820.0, 260.0);
    controller.setSelectedTextColor(QStringLiteral("#ffffff"));
    controller.setSelectedTextPointSize(150.0);
    controller.setSelectedTextWeight(QStringLiteral("black"));
    controller.setSelectedTextStrokeColor(QStringLiteral("#000000"));
    controller.setSelectedTextStrokeWidth(10.0);

    auto selection = controller.selectionDetails();
    if (selection.value(QStringLiteral("textStrokeColor")).toString() != QStringLiteral("#000000") ||
        selection.value(QStringLiteral("textStrokeWidth")).toDouble() != 10.0) {
      return fail("text stroke controls should update selection metadata");
    }

    const auto export_dir_path = dir.filePath(QStringLiteral("export-text-stroke"));
    QDir().mkpath(export_dir_path);
    if (!controller.exportCarouselToDirectory(export_dir_path)) {
      return fail("text stroke carousel should export");
    }
    const QImage slide(QDir(export_dir_path).filePath(QStringLiteral("carousel-01.png")));
    if (!region_contains_non_background(
            slide,
            QRect(100, 500, 880, 350),
            QColor(QStringLiteral("#ffffff")))) {
      return fail("text stroke should render visible outline pixels into exported slides");
    }

    controller.setDocumentName(QStringLiteral("Text Stroke Carousel"));
    if (!controller.saveDocument()) {
      return fail("text stroke carousel should save");
    }

    CarouselDocumentController restored;
    if (!restored.loadDocument(controller.savedDocumentId())) {
      return fail("text stroke carousel should load");
    }
    const auto restored_items = restored.canvasItems();
    if (restored_items.size() != 1 ||
        restored_items.at(0).toMap().value(QStringLiteral("textStrokeColor")).toString() != QStringLiteral("#000000") ||
        restored_items.at(0).toMap().value(QStringLiteral("textStrokeWidth")).toDouble() != 10.0) {
      return fail("saved carousel documents should restore text stroke settings");
    }
  }

  {
    CarouselDocumentController controller;
    controller.setCurrentSlideBackgroundColor(QStringLiteral("#ffffff"));
    controller.addTextItem(QStringLiteral("BOX"));
    controller.setSelectedItemCenter(540.0, 675.0);
    controller.setSelectedItemSize(720.0, 280.0);
    controller.setSelectedTextColor(QStringLiteral("#000000"));
    controller.setSelectedTextPointSize(150.0);
    controller.setSelectedTextWeight(QStringLiteral("black"));
    controller.setSelectedTextBackgroundColor(QStringLiteral("#ff453a"));
    controller.setSelectedTextBackgroundOpacity(1.0);

    auto selection = controller.selectionDetails();
    if (selection.value(QStringLiteral("textBackgroundColor")).toString() != QStringLiteral("#ff453a") ||
        selection.value(QStringLiteral("textBackgroundOpacity")).toDouble() != 1.0) {
      return fail("text background controls should update selection metadata");
    }

    const auto export_dir_path = dir.filePath(QStringLiteral("export-text-background"));
    QDir().mkpath(export_dir_path);
    if (!controller.exportCarouselToDirectory(export_dir_path)) {
      return fail("text background carousel should export");
    }
    const QImage slide(QDir(export_dir_path).filePath(QStringLiteral("carousel-01.png")));
    if (!color_matches(slide.pixelColor(220, 555), QColor(QStringLiteral("#ff453a"))) ||
        !region_contains_non_background(
            slide,
            QRect(180, 535, 720, 280),
            QColor(QStringLiteral("#ffffff")))) {
      return fail("text background should render behind exported text frame");
    }

    controller.setDocumentName(QStringLiteral("Text Background Carousel"));
    if (!controller.saveDocument()) {
      return fail("text background carousel should save");
    }

    CarouselDocumentController restored;
    if (!restored.loadDocument(controller.savedDocumentId())) {
      return fail("text background carousel should load");
    }
    const auto restored_items = restored.canvasItems();
    if (restored_items.size() != 1 ||
        restored_items.at(0).toMap().value(QStringLiteral("textBackgroundColor")).toString() !=
            QStringLiteral("#ff453a") ||
        restored_items.at(0).toMap().value(QStringLiteral("textBackgroundOpacity")).toDouble() != 1.0) {
      return fail("saved carousel documents should restore text background settings");
    }
  }

  {
    const auto export_text_bounds = [&](double tracking, const QString& export_name) {
      CarouselDocumentController controller;
      controller.setCurrentSlideBackgroundColor(QStringLiteral("#ffffff"));
      controller.addTextItem(QStringLiteral("AAAA"));
      controller.setSelectedItemCenter(540.0, 675.0);
      controller.setSelectedItemSize(860.0, 260.0);
      controller.setSelectedTextColor(QStringLiteral("#000000"));
      controller.setSelectedTextPointSize(160.0);
      controller.setSelectedTextLetterSpacing(tracking);

      const auto export_dir_path = dir.filePath(export_name);
      QDir().mkpath(export_dir_path);
      if (!controller.exportCarouselToDirectory(export_dir_path)) {
        return QRect();
      }
      const QImage slide(QDir(export_dir_path).filePath(QStringLiteral("carousel-01.png")));
      return non_background_bounds(slide, QRect(110, 545, 860, 260), QColor(QStringLiteral("#ffffff")));
    };

    const QRect normal_bounds = export_text_bounds(0.0, QStringLiteral("export-text-tracking-normal"));
    const QRect tracked_bounds = export_text_bounds(36.0, QStringLiteral("export-text-tracking-wide"));
    if (normal_bounds.isNull() || tracked_bounds.isNull() ||
        tracked_bounds.width() <= normal_bounds.width() + 60) {
      return fail("text tracking should increase exported text width");
    }

    CarouselDocumentController controller;
    controller.addTextItem(QStringLiteral("Tracked"));
    controller.setSelectedTextLetterSpacing(24.0);
    auto selection = controller.selectionDetails();
    if (selection.value(QStringLiteral("textLetterSpacing")).toDouble() != 24.0) {
      return fail("text tracking controls should update selection metadata");
    }

    controller.setDocumentName(QStringLiteral("Text Tracking Carousel"));
    if (!controller.saveDocument()) {
      return fail("text tracking carousel should save");
    }

    CarouselDocumentController restored;
    if (!restored.loadDocument(controller.savedDocumentId())) {
      return fail("text tracking carousel should load");
    }
    const auto restored_items = restored.canvasItems();
    if (restored_items.size() != 1 ||
        restored_items.at(0).toMap().value(QStringLiteral("textLetterSpacing")).toDouble() != 24.0) {
      return fail("saved carousel documents should restore text tracking");
    }
  }

  {
    CarouselDocumentController controller;
    controller.setCurrentSlideBackgroundColor(QStringLiteral("#ffffff"));
    controller.addTextItem(QStringLiteral("X"));
    controller.setSelectedItemCenter(540.0, 675.0);
    controller.setSelectedItemSize(720.0, 600.0);
    controller.setSelectedTextColor(QStringLiteral("#000000"));
    controller.setSelectedTextPointSize(180.0);
    controller.setSelectedTextHorizontalAlignment(QStringLiteral("right"));
    controller.setSelectedTextVerticalAlignment(QStringLiteral("bottom"));

    auto selection = controller.selectionDetails();
    if (selection.value(QStringLiteral("textHorizontalAlignment")).toString() != QStringLiteral("right") ||
        selection.value(QStringLiteral("textVerticalAlignment")).toString() != QStringLiteral("bottom")) {
      return fail("text alignment controls should update selection metadata");
    }
    const auto export_dir_path = dir.filePath(QStringLiteral("export-text-alignment"));
    QDir().mkpath(export_dir_path);
    if (!controller.exportCarouselToDirectory(export_dir_path)) {
      return fail("text alignment carousel should export");
    }

    const QImage slide(QDir(export_dir_path).filePath(QStringLiteral("carousel-01.png")));
    const QRect text_frame(180, 375, 720, 600);
    const QRect ink_bounds = non_background_bounds(slide, text_frame, QColor(QStringLiteral("#ffffff")));
    if (ink_bounds.isNull() || ink_bounds.center().x() <= text_frame.center().x() ||
        ink_bounds.center().y() <= text_frame.center().y()) {
      return fail("right and bottom text alignment should affect exported text placement");
    }

    controller.setDocumentName(QStringLiteral("Text Alignment Carousel"));
    if (!controller.saveDocument()) {
      return fail("text alignment carousel should save");
    }

    CarouselDocumentController restored;
    if (!restored.loadDocument(controller.savedDocumentId())) {
      return fail("text alignment carousel should load");
    }
    const auto restored_items = restored.canvasItems();
    if (restored_items.size() != 1 ||
        restored_items.at(0).toMap().value(QStringLiteral("textHorizontalAlignment")).toString() !=
            QStringLiteral("right") ||
        restored_items.at(0).toMap().value(QStringLiteral("textVerticalAlignment")).toString() !=
            QStringLiteral("bottom")) {
      return fail("saved carousel documents should restore text alignment settings");
    }
  }

  {
    CarouselDocumentController controller;
    const auto original_path = write_image(
        dir,
        QStringLiteral("duplicate-original.png"),
        QImage(QSize(800, 800), QImage::Format_ARGB32));
    const auto replacement_path = write_image(
        dir,
        QStringLiteral("duplicate-replacement.png"),
        QImage(QSize(800, 800), QImage::Format_ARGB32));
    if (original_path.isEmpty() || replacement_path.isEmpty()) {
      return fail("duplicate/replace sources should save");
    }

    QImage original_image(original_path);
    original_image.fill(QColor(QStringLiteral("#34c759")));
    if (!original_image.save(original_path)) {
      return fail("original duplicate source should save");
    }

    QImage replacement_image(replacement_path);
    replacement_image.fill(QColor(QStringLiteral("#ff453a")));
    if (!replacement_image.save(replacement_path)) {
      return fail("replacement duplicate source should save");
    }

    controller.addPhotoItem(original_path, QStringLiteral("original"));
    controller.setSelectedItemCenter(320.0, 720.0);
    controller.setSelectedItemSize(320.0, 320.0);
    controller.duplicateSelectedItem();

    const auto duplicated_items = controller.canvasItems();
    if (duplicated_items.size() != 2) {
      return fail("duplicating a selected layer should create a second layer");
    }

    controller.replaceSelectedPhotoItem(replacement_path, QStringLiteral("replacement"), QString{});
    const auto selection = controller.selectionDetails();
    if (selection.value(QStringLiteral("displayLabel")).toString() != QStringLiteral("replacement")) {
      return fail("replacing a selected photo layer should update its media");
    }

    controller.setSelectedItemLocked(true);
    controller.moveSelectedItemByPixels(100.0, 0.0);
    const auto locked_selection = controller.selectionDetails();
    if (locked_selection.value(QStringLiteral("x")).toDouble() != selection.value(QStringLiteral("x")).toDouble()) {
      return fail("locked layers should ignore move commands");
    }

    controller.setSelectedItemHidden(true);
    const auto hidden_selection = controller.selectionDetails();
    if (!hidden_selection.value(QStringLiteral("hidden")).toBool()) {
      return fail("hidden state should be exposed for selected layers");
    }
  }

  {
    CarouselDocumentController controller;
    const auto multi_path = write_image(
        dir,
        QStringLiteral("multi-select.png"),
        QImage(QSize(400, 400), QImage::Format_ARGB32));
    if (multi_path.isEmpty()) {
      return fail("multi-select layer source should save");
    }

    QImage multi_image(multi_path);
    multi_image.fill(QColor(QStringLiteral("#64d2ff")));
    if (!multi_image.save(multi_path)) {
      return fail("multi-select layer source image should save");
    }

    controller.addPhotoItem(multi_path, QStringLiteral("one"));
    const auto first_id = controller.selectedItemId();
    controller.setSelectedItemCenter(120.0, 240.0);
    controller.setSelectedItemSize(120.0, 120.0);

    controller.addPhotoItem(multi_path, QStringLiteral("two"));
    const auto second_id = controller.selectedItemId();
    controller.setSelectedItemCenter(220.0, 240.0);
    controller.setSelectedItemSize(120.0, 120.0);

    controller.addPhotoItem(multi_path, QStringLiteral("three"));
    const auto third_id = controller.selectedItemId();
    controller.setSelectedItemCenter(720.0, 240.0);
    controller.setSelectedItemSize(120.0, 120.0);

    controller.selectItem(first_id);
    controller.addItemToSelection(second_id);
    controller.addItemToSelection(third_id);

    auto selection = controller.selectionDetails();
    if (selection.value(QStringLiteral("selectedCount")).toInt() != 3 ||
        !selection.value(QStringLiteral("hasMultipleSelection")).toBool()) {
      return fail("adding items to the selection should preserve a multi-layer selection set");
    }

    const auto selected_ids = selection.value(QStringLiteral("selectedItemIds")).toList();
    if (selected_ids.size() != 3) {
      return fail("selection details should expose all selected item ids");
    }

    controller.distributeSelectionHorizontally();
    auto items = controller.canvasItems();
    double middle_x = -1.0;
    for (const auto& value : items) {
      const auto item = value.toMap();
      if (item.value(QStringLiteral("itemId")).toString() == second_id) {
        middle_x = item.value(QStringLiteral("x")).toDouble();
      }
    }
    if (middle_x != 360.0) {
      return fail("horizontal distribution should evenly space selected layers");
    }

    controller.alignSelectionLeft();
    items = controller.canvasItems();
    int left_aligned_count = 0;
    int selected_count = 0;
    for (const auto& value : items) {
      const auto item = value.toMap();
      if (item.value(QStringLiteral("selected")).toBool()) {
        ++selected_count;
        if (item.value(QStringLiteral("x")).toDouble() == 0.0) {
          ++left_aligned_count;
        }
      }
    }
    if (selected_count != 3 || left_aligned_count != 3) {
      return fail("aligning left should move every selected layer to the canvas edge");
    }

    controller.groupSelectedItems();
    selection = controller.selectionDetails();
    if (!selection.value(QStringLiteral("isObjectGroup")).toBool() ||
        selection.value(QStringLiteral("objectGroupCount")).toInt() != 3) {
      return fail("grouping should assign one persistent object group to the selected layers");
    }
    const auto original_group_id = selection.value(QStringLiteral("objectGroupId")).toString();
    controller.clearSelection();
    controller.selectItem(second_id);
    selection = controller.selectionDetails();
    if (selection.value(QStringLiteral("selectedCount")).toInt() != 3 ||
        selection.value(QStringLiteral("objectGroupId")).toString() != original_group_id) {
      return fail("selecting one grouped layer should select the complete object group");
    }

    const auto original_group_width = selection.value(QStringLiteral("selectionWidth")).toDouble();
    const auto original_group_height = selection.value(QStringLiteral("selectionHeight")).toDouble();
    controller.setSelectedItemSize(original_group_width * 0.5, original_group_height * 0.5);
    selection = controller.selectionDetails();
    if (selection.value(QStringLiteral("selectionWidth")).toDouble() >= original_group_width ||
        selection.value(QStringLiteral("selectionHeight")).toDouble() >= original_group_height) {
      return fail("resizing a group should scale every member around the group bounds");
    }
    controller.undo();
    selection = controller.selectionDetails();
    if (qAbs(selection.value(QStringLiteral("selectionWidth")).toDouble() - original_group_width) > 0.01) {
      return fail("group resize should undo as one document edit");
    }

    controller.duplicateSelectedItem();
    selection = controller.selectionDetails();
    if (!selection.value(QStringLiteral("isObjectGroup")).toBool() ||
        selection.value(QStringLiteral("objectGroupId")).toString() == original_group_id ||
        controller.canvasItems().size() != 6) {
      return fail("duplicating a group should preserve grouping without sharing identity with the source");
    }
    controller.undo();
    controller.selectItem(second_id);
    controller.ungroupSelectedItems();
    selection = controller.selectionDetails();
    if (selection.value(QStringLiteral("isObjectGroup")).toBool()) {
      return fail("ungrouping should keep layers selected while removing their group identity");
    }
    controller.undo();
    controller.selectItem(second_id);
    if (!controller.selectionDetails().value(QStringLiteral("isObjectGroup")).toBool()) {
      return fail("undo should restore an ungrouped object group");
    }

    controller.toggleItemSelection(second_id);
    selection = controller.selectionDetails();
    if (selection.value(QStringLiteral("selectedCount")).toInt() != 0) {
      return fail("toggling a selected grouped item should remove the complete group from selection");
    }
  }

  {
    CarouselDocumentController controller;
    controller.setDocumentFormat(QStringLiteral("5x4"));

    const auto persisted_left = write_image(
        dir,
        QStringLiteral("persist-left.png"),
        QImage(QSize(700, 1080), QImage::Format_ARGB32));
    const auto persisted_right = write_image(
        dir,
        QStringLiteral("persist-right.png"),
        QImage(QSize(700, 1080), QImage::Format_ARGB32));
    if (persisted_left.isEmpty() || persisted_right.isEmpty()) {
      return fail("persistence source images should save");
    }

    QImage persisted_left_image(persisted_left);
    persisted_left_image.fill(QColor(QStringLiteral("#ff9f0a")));
    if (!persisted_left_image.save(persisted_left)) {
      return fail("left persistence source should save");
    }

    QImage persisted_right_image(persisted_right);
    persisted_right_image.fill(QColor(QStringLiteral("#64d2ff")));
    if (!persisted_right_image.save(persisted_right)) {
      return fail("right persistence source should save");
    }

    controller.applyTemplateToCurrent(QStringLiteral("split-pair"));
    controller.assignPrimaryImage(persisted_left, QStringLiteral("persist-left"));
    controller.assignSecondaryImage(persisted_right, QStringLiteral("persist-right"));
    controller.addSlide();
    controller.selectSlide(1);
    controller.addTextItem(QStringLiteral("SAVE"));
    controller.setSelectedItemCenter(2025.0, 180.0);
    controller.setSelectedTextColor(QStringLiteral("#ff453a"));
    controller.setSelectedTextUnderline(true);
    controller.setSelectedTextCase(QStringLiteral("uppercase"));
    controller.setSelectedTextPadding(18.0);
    controller.setSelectedTextBackgroundRadius(12.0);
    controller.setDocumentName(QStringLiteral("Persisted Carousel"));

    if (!controller.saveDocument()) {
      std::cerr << controller.status().toStdString() << '\n';
      return fail("carousel documents should save successfully");
    }
    if (controller.savedDocumentId().isEmpty()) {
      return fail("saving a carousel should assign a saved document id");
    }

    const auto recent_documents = controller.recentDocuments();
    bool found_saved_document = false;
    for (const auto& value : recent_documents) {
      const auto item = value.toMap();
      if (item.value(QStringLiteral("documentId")).toString() == controller.savedDocumentId() &&
          item.value(QStringLiteral("name")).toString() == QStringLiteral("Persisted Carousel")) {
        found_saved_document = true;
        break;
      }
    }
    if (!found_saved_document) {
      return fail("saved carousel documents should appear in recent documents");
    }

    CarouselDocumentController restored;
    if (!restored.loadDocument(controller.savedDocumentId())) {
      std::cerr << restored.status().toStdString() << '\n';
      return fail("saved carousel documents should load successfully");
    }

    if (restored.documentName() != QStringLiteral("Persisted Carousel") ||
        restored.savedDocumentId() != controller.savedDocumentId()) {
      return fail("loaded carousel documents should restore document identity and naming");
    }
    if (restored.documentFormat() != QStringLiteral("5x4") || restored.slideCount() != 2) {
      return fail("loaded carousel documents should restore the document format and canvas count");
    }

    const auto restored_slides = restored.slides();
    if (restored_slides.size() != 2 ||
        restored_slides.at(0).toMap().value(QStringLiteral("templateKind")).toString() != QStringLiteral("split-pair") ||
        restored_slides.at(1).toMap().value(QStringLiteral("templateKind")).toString() != QStringLiteral("single")) {
      return fail("loaded carousel documents should restore slide templates");
    }

    const auto restored_items = restored.canvasItems();
    if (restored_items.size() != 1 ||
        restored_items.at(0).toMap().value(QStringLiteral("type")).toString() != QStringLiteral("text") ||
        restored_items.at(0).toMap().value(QStringLiteral("slideIndex")).toInt() != 1 ||
        !restored_items.at(0).toMap().value(QStringLiteral("textUnderline")).toBool() ||
        restored_items.at(0).toMap().value(QStringLiteral("textCase")).toString() != QStringLiteral("uppercase") ||
        qAbs(restored_items.at(0).toMap().value(QStringLiteral("textPadding")).toDouble() - 18.0) > 0.01 ||
        qAbs(restored_items.at(0).toMap().value(QStringLiteral("textBackgroundRadius")).toDouble() - 12.0) > 0.01) {
      return fail("loaded carousel documents should restore freeform layer items");
    }

    const auto restored_export_dir = dir.filePath(QStringLiteral("export-restored-document"));
    QDir().mkpath(restored_export_dir);
    if (!restored.exportCarouselToDirectory(restored_export_dir)) {
      return fail("loaded carousel documents should still export successfully");
    }

    const QImage restored_first_slide(QDir(restored_export_dir).filePath(QStringLiteral("carousel-01.png")));
    if (!color_is_dominantly_orange(restored_first_slide.pixelColor(
            restored_first_slide.width() / 4,
            restored_first_slide.height() / 2)) ||
        !color_is_dominantly_cyan(restored_first_slide.pixelColor(
            (restored_first_slide.width() * 3) / 4,
            restored_first_slide.height() / 2))) {
      return fail("loaded carousel documents should preserve assigned slot media");
    }
  }

  {
    CarouselDocumentController controller;
    controller.createCarouselWithSettings(QVariantMap{
        {QStringLiteral("carouselName"), QStringLiteral("Precision Layout Carousel")},
        {QStringLiteral("documentFormat"), QStringLiteral("4x5")},
        {QStringLiteral("slideCount"), 2},
    });

    const auto defaults = controller.layoutSettings();
    if (!defaults.value(QStringLiteral("snappingEnabled")).toBool() ||
        !defaults.value(QStringLiteral("aspectLocked")).toBool() ||
        defaults.value(QStringLiteral("columns")).toInt() != 4) {
      return fail("precision layout settings should expose stable defaults");
    }

    const auto board_layout_baseline_dir = dir.filePath(QStringLiteral("board-layout-baseline-export"));
    const auto board_layout_changed_dir = dir.filePath(QStringLiteral("board-layout-changed-export"));
    QDir().mkpath(board_layout_baseline_dir);
    QDir().mkpath(board_layout_changed_dir);
    if (!controller.exportCarouselToDirectory(board_layout_baseline_dir)) {
      return fail("board-local layout test should export its baseline");
    }

    controller.selectSlide(0);
    controller.setCurrentSlideLayoutTokens(3, 5, 80.0, 90.0, 12.0, 18.0);
    auto board_zero = controller.slides().at(0).toMap();
    auto board_one = controller.slides().at(1).toMap();
    if (board_zero.value(QStringLiteral("layoutColumns")).toInt() != 3 ||
        board_zero.value(QStringLiteral("layoutRows")).toInt() != 5 ||
        qAbs(board_zero.value(QStringLiteral("layoutMarginX")).toDouble() - 80.0) > 0.01 ||
        qAbs(board_zero.value(QStringLiteral("layoutMarginY")).toDouble() - 90.0) > 0.01 ||
        qAbs(board_zero.value(QStringLiteral("layoutGutterX")).toDouble() - 12.0) > 0.01 ||
        qAbs(board_zero.value(QStringLiteral("layoutGutterY")).toDouble() - 18.0) > 0.01 ||
        board_one.value(QStringLiteral("layoutColumns")).toInt() != 4) {
      return fail("board-local columns, rows, margins, and gutters should not leak to another board");
    }
    controller.selectSlide(1);
    controller.setCurrentSlideLayoutTokens(6, 2, 64.0, 56.0, 20.0, 10.0);
    auto selection_layout = controller.selectionDetails();
    board_one = controller.slides().at(1).toMap();
    if (selection_layout.value(QStringLiteral("layoutColumns")).toInt() != 6 ||
        selection_layout.value(QStringLiteral("layoutRows")).toInt() != 2 ||
        qAbs(selection_layout.value(QStringLiteral("layoutMarginX")).toDouble() - 64.0) > 0.01 ||
        qAbs(selection_layout.value(QStringLiteral("layoutMarginY")).toDouble() - 56.0) > 0.01 ||
        qAbs(selection_layout.value(QStringLiteral("layoutGutterX")).toDouble() - 20.0) > 0.01 ||
        qAbs(selection_layout.value(QStringLiteral("layoutGutterY")).toDouble() - 10.0) > 0.01 ||
        board_one.value(QStringLiteral("layoutColumns")).toInt() != 6) {
      return fail("the active board inspector should expose its complete local layout token set");
    }
    controller.undo();
    if (controller.slides().at(1).toMap().value(QStringLiteral("layoutColumns")).toInt() != 4 ||
        controller.slides().at(0).toMap().value(QStringLiteral("layoutColumns")).toInt() != 3) {
      return fail("undoing a board-local layout edit should restore only the edited board");
    }
    controller.redo();
    if (controller.slides().at(1).toMap().value(QStringLiteral("layoutColumns")).toInt() != 6) {
      return fail("redo should restore the board-local layout token edit atomically");
    }
    if (!controller.exportCarouselToDirectory(board_layout_changed_dir)) {
      return fail("board-local layout test should export after editing layout tokens");
    }
    for (int board_index = 1; board_index <= 2; ++board_index) {
      QFile baseline_file(QDir(board_layout_baseline_dir).filePath(
          QStringLiteral("carousel-%1.png").arg(board_index, 2, 10, QLatin1Char('0'))));
      QFile changed_file(QDir(board_layout_changed_dir).filePath(
          QStringLiteral("carousel-%1.png").arg(board_index, 2, 10, QLatin1Char('0'))));
      if (!baseline_file.open(QIODevice::ReadOnly) || !changed_file.open(QIODevice::ReadOnly) ||
          baseline_file.readAll() != changed_file.readAll()) {
        return fail("board-local layout visualization must never change exported pixels or metadata");
      }
    }

    controller.setLayoutSetting(QStringLiteral("gridEnabled"), true);
    controller.setLayoutSetting(QStringLiteral("columns"), 8);
    controller.setLayoutSetting(QStringLiteral("marginX"), 88.0);
    controller.setLayoutSetting(QStringLiteral("nudgeStep"), 2.5);
    controller.addLayoutGuide(QStringLiteral("vertical"), 320.0);
    controller.addLayoutGuide(QStringLiteral("horizontal"), 440.0);
    auto layout = controller.layoutSettings();
    auto guides = layout.value(QStringLiteral("customGuides")).toList();
    if (!layout.value(QStringLiteral("gridEnabled")).toBool() ||
        !layout.value(QStringLiteral("guidesEnabled")).toBool() ||
        layout.value(QStringLiteral("columns")).toInt() != 8 ||
        qAbs(layout.value(QStringLiteral("marginX")).toDouble() - 88.0) > 0.01 ||
        qAbs(layout.value(QStringLiteral("nudgeStep")).toDouble() - 2.5) > 0.01 || guides.size() != 2) {
      return fail("precision layout controls should update the document model");
    }

    const auto first_guide_id = guides.constFirst().toMap().value(QStringLiteral("guideId")).toInt();
    controller.moveLayoutGuide(first_guide_id, 360.0);
    guides = controller.layoutSettings().value(QStringLiteral("customGuides")).toList();
    if (qAbs(guides.constFirst().toMap().value(QStringLiteral("position")).toDouble() - 360.0) > 0.01) {
      return fail("custom layout guides should support precise movement");
    }

    controller.applyLayoutPreset(QStringLiteral("dense"));
    if (controller.layoutSettings().value(QStringLiteral("columns")).toInt() != 12 ||
        controller.selectionDetails().value(QStringLiteral("layoutColumns")).toInt() != 12 ||
        !controller.canUndo()) {
      return fail("layout presets should apply global visibility and board-local tokens as one undoable edit");
    }
    controller.undo();
    if (controller.layoutSettings().value(QStringLiteral("columns")).toInt() != 8 ||
        controller.selectionDetails().value(QStringLiteral("layoutColumns")).toInt() != 6) {
      return fail("undo should restore global guide settings and local board tokens before a preset");
    }

    const auto safe_area_options = controller.safeAreaPresetOptions();
    if (safe_area_options != QStringList{QStringLiteral("instagram-feed"),
                                         QStringLiteral("reels-stories"),
                                         QStringLiteral("tiktok"),
                                         QStringLiteral("device-ui"),
                                         QStringLiteral("custom")}) {
      return fail("safe-area presets should expose the complete supported platform set");
    }

    const auto baseline_export_dir = dir.filePath(QStringLiteral("safe-area-baseline-export"));
    const auto guided_export_dir = dir.filePath(QStringLiteral("safe-area-guided-export"));
    QDir().mkpath(baseline_export_dir);
    QDir().mkpath(guided_export_dir);
    if (!controller.exportCarouselToDirectory(baseline_export_dir)) {
      return fail("precision-layout document should export before enabling safe-area overlays");
    }

    controller.applySafeAreaPreset(QStringLiteral("instagram-feed"));
    layout = controller.layoutSettings();
    if (layout.value(QStringLiteral("safePreset")).toString() != QStringLiteral("instagram-feed") ||
        qAbs(layout.value(QStringLiteral("safeInset")).toDouble() - 0.05) > 0.0001 ||
        !layout.value(QStringLiteral("safeEnabled")).toBool() ||
        !layout.value(QStringLiteral("guidesEnabled")).toBool()) {
      return fail("Instagram Feed safe area should enable a five-percent preview inset");
    }
    controller.applySafeAreaPreset(QStringLiteral("reels-stories"));
    if (qAbs(controller.layoutSettings().value(QStringLiteral("safeInset")).toDouble() - 0.12) > 0.0001) {
      return fail("Reels and Stories safe area should use a twelve-percent preview inset");
    }
    controller.applySafeAreaPreset(QStringLiteral("tiktok"));
    if (qAbs(controller.layoutSettings().value(QStringLiteral("safeInset")).toDouble() - 0.14) > 0.0001) {
      return fail("TikTok safe area should use a fourteen-percent preview inset");
    }
    controller.applySafeAreaPreset(QStringLiteral("device-ui"));
    if (qAbs(controller.layoutSettings().value(QStringLiteral("safeInset")).toDouble() - 0.10) > 0.0001) {
      return fail("generic device UI safe area should use a ten-percent preview inset");
    }
    controller.undo();
    layout = controller.layoutSettings();
    if (layout.value(QStringLiteral("safePreset")).toString() != QStringLiteral("tiktok") ||
        qAbs(layout.value(QStringLiteral("safeInset")).toDouble() - 0.14) > 0.0001) {
      return fail("undo should restore the previous safe-area preset and inset together");
    }
    controller.setLayoutSetting(QStringLiteral("safeInset"), 0.07);
    if (controller.layoutSettings().value(QStringLiteral("safePreset")).toString() != QStringLiteral("custom")) {
      return fail("manually changing the safe inset should switch the preset to custom");
    }
    controller.applySafeAreaPreset(QStringLiteral("tiktok"));

    if (!controller.exportCarouselToDirectory(guided_export_dir)) {
      return fail("precision-layout document should export with a safe-area overlay enabled");
    }
    QFile baseline_export(QDir(baseline_export_dir).filePath(QStringLiteral("carousel-01.png")));
    QFile guided_export(QDir(guided_export_dir).filePath(QStringLiteral("carousel-01.png")));
    if (!baseline_export.open(QIODevice::ReadOnly) || !guided_export.open(QIODevice::ReadOnly) ||
        baseline_export.readAll() != guided_export.readAll()) {
      return fail("safe-area preview overlays must never change exported pixels or metadata");
    }

    const auto precision_layout_hash = controller.documentContentHash();
    if (!controller.saveDocument()) {
      return fail("carousel documents with precision layout settings should save");
    }
    CarouselDocumentController restored;
    if (!restored.loadDocument(controller.savedDocumentId())) {
      return fail("carousel documents with precision layout settings should load");
    }
    layout = restored.layoutSettings();
    guides = layout.value(QStringLiteral("customGuides")).toList();
    if (!layout.value(QStringLiteral("gridEnabled")).toBool() ||
        layout.value(QStringLiteral("columns")).toInt() != 8 ||
        qAbs(layout.value(QStringLiteral("nudgeStep")).toDouble() - 2.5) > 0.01 || guides.size() != 2 ||
        layout.value(QStringLiteral("safePreset")).toString() != QStringLiteral("tiktok") ||
        qAbs(layout.value(QStringLiteral("safeInset")).toDouble() - 0.14) > 0.0001 ||
        !layout.value(QStringLiteral("safeEnabled")).toBool() ||
        restored.documentContentHash() != precision_layout_hash ||
        restored.slides().at(0).toMap().value(QStringLiteral("layoutColumns")).toInt() != 3 ||
        restored.slides().at(1).toMap().value(QStringLiteral("layoutColumns")).toInt() != 6 ||
        qAbs(restored.slides().at(0).toMap().value(QStringLiteral("layoutGutterY")).toDouble() - 18.0) > 0.01 ||
        qAbs(restored.slides().at(1).toMap().value(QStringLiteral("layoutMarginY")).toDouble() - 56.0) > 0.01) {
      return fail("saved carousel documents should restore precision layout settings and guides");
    }

    restored.resetLayoutSettings();
    if (!restored.layoutSettings().value(QStringLiteral("customGuides")).toList().isEmpty() ||
        restored.selectionDetails().value(QStringLiteral("layoutColumns")).toInt() != 4 ||
        qAbs(restored.selectionDetails().value(QStringLiteral("layoutMarginX")).toDouble() - 72.0) > 0.01) {
      return fail("resetting precision layout should clear custom guides and reset the current board tokens");
    }
    restored.undo();
    if (restored.layoutSettings().value(QStringLiteral("customGuides")).toList().size() != 2 ||
        restored.selectionDetails().value(QStringLiteral("layoutColumns")).toInt() != 6 ||
        qAbs(restored.selectionDetails().value(QStringLiteral("layoutMarginX")).toDouble() - 64.0) > 0.01) {
      return fail("precision layout reset should restore global guides and current board tokens in one undo");
    }
  }

  {
    CarouselDocumentController controller;
    controller.createCarouselWithSettings(QVariantMap{
        {QStringLiteral("carouselName"), QStringLiteral("Layout Recipe Proof")},
        {QStringLiteral("documentFormat"), QStringLiteral("4x5")},
        {QStringLiteral("slideCount"), 2},
    });
    controller.selectSlide(0);
    controller.setCurrentSlideLayoutTokens(7, 9, 84.0, 96.0, 22.0, 18.0);
    controller.setLayoutSetting(QStringLiteral("gridEnabled"), true);
    controller.setLayoutSetting(QStringLiteral("rulersEnabled"), true);
    controller.setLayoutSetting(QStringLiteral("snapThreshold"), 14.0);
    controller.setLayoutSetting(QStringLiteral("nudgeStep"), 2.5);
    controller.applySafeAreaPreset(QStringLiteral("tiktok"));
    controller.addLayoutGuide(QStringLiteral("vertical"), 180.0);
    controller.addLayoutGuide(QStringLiteral("horizontal"), 320.0);
    if (!controller.saveCurrentLayoutRecipe(QStringLiteral("Campaign spacing")) ||
        controller.layoutRecipes().size() != 1) {
      return fail("current guides, safe area, grid, and board tokens should save as a layout-only recipe");
    }
    const auto recipe = controller.layoutRecipes().constFirst().toMap();
    const auto recipe_id = recipe.value(QStringLiteral("recipeId")).toString();
    if (recipe_id.isEmpty() || recipe.value(QStringLiteral("columns")).toInt() != 7 ||
        recipe.value(QStringLiteral("rows")).toInt() != 9 ||
        recipe.value(QStringLiteral("guideCount")).toInt() != 2 ||
        recipe.value(QStringLiteral("safePreset")).toString() != QStringLiteral("tiktok")) {
      return fail("saved layout recipes should expose exact style-free layout metadata");
    }

    controller.selectSlide(1);
    controller.setCurrentSlideBackgroundColor(QStringLiteral("#6a1434"));
    controller.addShapeItem(QStringLiteral("rectangle"));
    const auto layout_shape_id = controller.selectedItemId();
    controller.setSelectedShapeFillColor(QStringLiteral("#ffcc33"));
    controller.setSelectedItemSize(420.0, 320.0);
    controller.setSelectedItemCenter(1620.0, 675.0);
    controller.addTextItemWithRole(QStringLiteral("title"), QStringLiteral("Visual style stays"));
    const auto layout_text_id = controller.selectedItemId();
    controller.setSelectedTextColor(QStringLiteral("#ffffff"));
    const auto visual_items_before = controller.canvasItems();
    auto layout_item_by_id = [&controller](const QString& item_id) {
      for (const auto& value : controller.canvasItems()) {
        const auto item = value.toMap();
        if (item.value(QStringLiteral("itemId")).toString() == item_id) {
          return item;
        }
      }
      return QVariantMap{};
    };
    const auto shape_before_recipe = layout_item_by_id(layout_shape_id);
    const auto text_before_recipe = layout_item_by_id(layout_text_id);
    const auto layout_export_before = dir.filePath(QStringLiteral("layout-recipe-before"));
    const auto layout_export_after = dir.filePath(QStringLiteral("layout-recipe-after"));
    QDir().mkpath(layout_export_before);
    QDir().mkpath(layout_export_after);
    controller.setExportManifestEnabled(false);
    controller.setExportBoardScope(QStringLiteral("current"));
    if (!controller.exportCarouselToDirectory(layout_export_before)) {
      return fail("layout recipe pixel fixture should export before preview");
    }
    controller.selectSlide(1);
    const auto visual_hash_before_preview = controller.documentContentHash();
    if (!controller.previewLayoutRecipe(recipe_id)) {
      return fail("layout recipe should preview against the current board without mutating it");
    }
    const auto layout_preview = controller.layoutRecipePreview();
    if (controller.documentContentHash() != visual_hash_before_preview) {
      return fail("layout recipe preview should not mutate the document hash");
    }
    if (layout_preview.value(QStringLiteral("boardIndex")).toInt() != 1 ||
        layout_preview.value(QStringLiteral("styleMutationCount")).toInt() != 0 ||
        layout_preview.value(QStringLiteral("contentMutationCount")).toInt() != 0 ||
        layout_preview.value(QStringLiteral("guideCount")).toInt() != 2 ||
        layout_preview.value(QStringLiteral("afterBoard")).toMap().value(
            QStringLiteral("columns")).toInt() != 7) {
      return fail("layout recipe preview should show exact layout changes and zero visual/content mutations");
    }
    if (!controller.applyPreviewedLayoutRecipe()) {
      return fail("a reviewed layout recipe should apply atomically");
    }
    const auto applied_layout = controller.layoutSettings();
    const auto applied_board = controller.selectionDetails();
    if (!applied_layout.value(QStringLiteral("gridEnabled")).toBool() ||
        applied_layout.value(QStringLiteral("safePreset")).toString() != QStringLiteral("tiktok") ||
        applied_layout.value(QStringLiteral("customGuides")).toList().size() != 2 ||
        qAbs(applied_layout.value(QStringLiteral("nudgeStep")).toDouble() - 2.5) > 0.001 ||
        applied_board.value(QStringLiteral("layoutColumns")).toInt() != 7 ||
        applied_board.value(QStringLiteral("layoutRows")).toInt() != 9 ||
        controller.canvasItems().size() != visual_items_before.size() ||
        layout_item_by_id(layout_shape_id).value(QStringLiteral("fillColor")).toString() !=
            shape_before_recipe.value(QStringLiteral("fillColor")).toString() ||
        layout_item_by_id(layout_shape_id).value(QStringLiteral("x")).toDouble() !=
            shape_before_recipe.value(QStringLiteral("x")).toDouble() ||
        layout_item_by_id(layout_text_id).value(QStringLiteral("textColor")).toString() !=
            text_before_recipe.value(QStringLiteral("textColor")).toString() ||
        layout_item_by_id(layout_text_id).value(QStringLiteral("text")).toString() !=
            text_before_recipe.value(QStringLiteral("text")).toString() ||
        !controller.exportCarouselToDirectory(layout_export_after)) {
      return fail("layout recipe apply should change only saved layout state and keep visual layers/export intact");
    }
    const QImage layout_pixels_before(
        QDir(layout_export_before).filePath(QStringLiteral("carousel-02.png")));
    const QImage layout_pixels_after(
        QDir(layout_export_after).filePath(QStringLiteral("carousel-02.png")));
    if (layout_pixels_before.isNull() || layout_pixels_after.isNull() ||
        layout_pixels_before != layout_pixels_after) {
      return fail("layout recipes should never alter exported visual pixels");
    }
    controller.undo();
    if (controller.selectionDetails().value(QStringLiteral("layoutColumns")).toInt() == 7 ||
        controller.canvasItems().size() != visual_items_before.size() ||
        layout_item_by_id(layout_shape_id).value(QStringLiteral("fillColor")).toString() !=
            shape_before_recipe.value(QStringLiteral("fillColor")).toString() ||
        layout_item_by_id(layout_text_id).value(QStringLiteral("textColor")).toString() !=
            text_before_recipe.value(QStringLiteral("textColor")).toString()) {
      return fail("layout recipe apply should undo independently of visual content");
    }
    controller.redo();
    if (!controller.saveDocument(QStringLiteral("Layout Recipe Proof"))) {
      return fail("documents using a saved layout recipe should save");
    }
    CarouselDocumentController reopened;
    if (!reopened.loadDocument(controller.savedDocumentId())) {
      return fail("documents using a layout recipe should reopen");
    }
    reopened.selectSlide(1);
    if (reopened.selectionDetails().value(QStringLiteral("layoutColumns")).toInt() != 7 ||
        reopened.layoutRecipes().isEmpty() ||
        !reopened.renameLayoutRecipe(recipe_id, QStringLiteral("Campaign spacing v2")) ||
        !reopened.deleteLayoutRecipe(recipe_id) || !reopened.layoutRecipes().isEmpty()) {
      return fail("layout recipes should persist independently and support rename/delete lifecycle");
    }
  }

  {
    CarouselDocumentController controller;
    controller.createCarouselWithSettings(QVariantMap{
        {QStringLiteral("carouselName"), QStringLiteral("Variant Comparison Proof")},
        {QStringLiteral("documentFormat"), QStringLiteral("4x5")},
        {QStringLiteral("slideCount"), 3},
    });
    controller.setExportManifestEnabled(false);
    controller.setExportImageFormat(QStringLiteral("png"));
    controller.setExportBoardScope(QStringLiteral("current"));
    const std::array<QString, 3> initial_backgrounds{
        QStringLiteral("#102030"),
        QStringLiteral("#204060"),
        QStringLiteral("#306090"),
    };
    for (int index = 0; index < 3; ++index) {
      controller.selectSlide(index);
      controller.setCurrentSlideBackgroundColor(
          initial_backgrounds.at(static_cast<std::size_t>(index)));
      controller.addTextItem(
          QStringLiteral("Variant board %1").arg(index + 1));
    }
    controller.selectSlide(0);
    if (!controller.captureVariantComparisonBaseline(QStringLiteral("Original direction"))) {
      return fail("variant comparison should capture the complete left document");
    }
    auto comparison = controller.variantComparisonState();
    const auto left_sources = comparison.value(QStringLiteral("leftPreviewSources")).toList();
    const auto matching_right_sources =
        comparison.value(QStringLiteral("rightPreviewSources")).toList();
    if (!comparison.value(QStringLiteral("active")).toBool() ||
        comparison.value(QStringLiteral("different")).toBool() ||
        comparison.value(QStringLiteral("leftBoardCount")).toInt() != 3 ||
        comparison.value(QStringLiteral("rightBoardCount")).toInt() != 3 ||
        left_sources.size() != 3 || matching_right_sources.size() != 3) {
      return fail("captured and current variants should expose matching rendered previews for every board");
    }
    for (const auto& source : left_sources) {
      const QImage preview(QUrl(source.toString()).toLocalFile());
      if (preview.isNull() || preview.width() > 420 || preview.height() > 520) {
        return fail("variant comparison previews should be real bounded board renders");
      }
    }

    controller.selectSlide(1);
    controller.setCurrentSlideBackgroundColor(QStringLiteral("#b4234d"));
    controller.addTextItem(QStringLiteral("Current direction changed"));
    if (!controller.refreshVariantComparison()) {
      return fail("variant comparison should refresh the live right document");
    }
    comparison = controller.variantComparisonState();
    const auto right_sources = comparison.value(QStringLiteral("rightPreviewSources")).toList();
    controller.setVariantComparisonBoardIndex(1);
    comparison = controller.variantComparisonState();
    const QImage left_board_two(QUrl(left_sources.at(1).toString()).toLocalFile());
    const QImage right_board_two(QUrl(right_sources.at(1).toString()).toLocalFile());
    if (!comparison.value(QStringLiteral("different")).toBool() ||
        comparison.value(QStringLiteral("boardIndex")).toInt() != 1 ||
        left_board_two.isNull() || right_board_two.isNull() || left_board_two == right_board_two) {
      return fail("variant comparison should synchronize navigation and render visible board differences");
    }
    if (!controller.resolveVariantComparison(QStringLiteral("keep-right")) ||
        controller.variantComparisonState().value(QStringLiteral("active")).toBool() ||
        controller.slides().at(1).toMap().value(QStringLiteral("backgroundColor")).toString() !=
            QStringLiteral("#b4234d") ||
        !controller.savedDesignVersions().isEmpty()) {
      return fail("Keep right should retain the current document without creating a hidden version");
    }

    if (!controller.captureVariantComparisonBaseline(QStringLiteral("Restore candidate"))) {
      return fail("variant comparison should support a second decision cycle");
    }
    controller.selectSlide(1);
    controller.setCurrentSlideBackgroundColor(QStringLiteral("#1f9d72"));
    controller.refreshVariantComparison();
    if (!controller.resolveVariantComparison(QStringLiteral("keep-left")) ||
        controller.slides().at(1).toMap().value(QStringLiteral("backgroundColor")).toString() !=
            QStringLiteral("#b4234d")) {
      return fail("Keep left should restore the exact captured document");
    }
    controller.undo();
    if (controller.slides().at(1).toMap().value(QStringLiteral("backgroundColor")).toString() !=
        QStringLiteral("#1f9d72")) {
      return fail("Keep left should undo atomically back to the rejected right document");
    }
    controller.redo();
    if (controller.slides().at(1).toMap().value(QStringLiteral("backgroundColor")).toString() !=
        QStringLiteral("#b4234d")) {
      return fail("Keep left should redo atomically");
    }

    if (!controller.captureVariantComparisonBaseline(QStringLiteral("Approved red"))) {
      return fail("variant comparison should capture the version pair that will be retained");
    }
    controller.selectSlide(1);
    controller.setCurrentSlideBackgroundColor(QStringLiteral("#6f3cc3"));
    controller.refreshVariantComparison();
    if (!controller.resolveVariantComparison(QStringLiteral("keep-both")) ||
        controller.savedDesignVersions().size() != 2 ||
        controller.slides().at(1).toMap().value(QStringLiteral("backgroundColor")).toString() !=
            QStringLiteral("#6f3cc3")) {
      return fail("Keep both should retain the current canvas and create two explicit full-document versions");
    }
    controller.undo();
    if (!controller.savedDesignVersions().isEmpty() ||
        controller.slides().at(1).toMap().value(QStringLiteral("backgroundColor")).toString() !=
            QStringLiteral("#6f3cc3")) {
      return fail("Keep both should add both versions in one undoable action");
    }
    controller.redo();
    if (controller.savedDesignVersions().size() != 2 ||
        !controller.saveDocument(QStringLiteral("Variant Comparison Proof"))) {
      return fail("Keep both should redo atomically and save with the document");
    }

    CarouselDocumentController reopened;
    if (!reopened.loadDocument(controller.savedDocumentId()) ||
        reopened.savedDesignVersions().size() != 2 ||
        reopened.slides().at(1).toMap().value(QStringLiteral("backgroundColor")).toString() !=
            QStringLiteral("#6f3cc3")) {
      return fail("saved design versions and the chosen current canvas should survive reopen");
    }
    reopened.selectSlide(1);
    reopened.setExportManifestEnabled(false);
    reopened.setExportImageFormat(QStringLiteral("png"));
    reopened.setExportBoardScope(QStringLiteral("current"));
    const auto right_export_dir = dir.filePath(QStringLiteral("variant-comparison-right"));
    const auto left_export_dir = dir.filePath(QStringLiteral("variant-comparison-left"));
    QDir().mkpath(right_export_dir);
    QDir().mkpath(left_export_dir);
    if (!reopened.exportCarouselToDirectory(right_export_dir)) {
      return fail("the kept right variant should export normally after reopen");
    }
    const auto saved_versions = reopened.savedDesignVersions();
    const auto left_version_id =
        saved_versions.constFirst().toMap().value(QStringLiteral("versionId")).toString();
    if (left_version_id.isEmpty() || !reopened.restoreSavedDesignVersion(left_version_id) ||
        reopened.slides().at(1).toMap().value(QStringLiteral("backgroundColor")).toString() !=
            QStringLiteral("#b4234d") ||
        !reopened.exportCarouselToDirectory(left_export_dir)) {
      return fail("a saved left version should restore its complete exportable document state");
    }
    const QImage exported_right(
        QDir(right_export_dir).filePath(QStringLiteral("carousel-02.png")));
    const QImage exported_left(
        QDir(left_export_dir).filePath(QStringLiteral("carousel-02.png")));
    if (exported_right.isNull() || exported_left.isNull() || exported_right == exported_left) {
      return fail("restored design versions should produce their distinct rendered outputs");
    }
    reopened.undo();
    if (reopened.slides().at(1).toMap().value(QStringLiteral("backgroundColor")).toString() !=
            QStringLiteral("#6f3cc3") ||
        reopened.savedDesignVersions().size() != 2) {
      return fail("restoring a design version should undo back to the prior current document");
    }
    const auto removable_version_id = reopened.savedDesignVersions().constLast().toMap().value(
        QStringLiteral("versionId")).toString();
    if (!reopened.deleteSavedDesignVersion(removable_version_id) ||
        reopened.savedDesignVersions().size() != 1) {
      return fail("saved design versions should support explicit deletion");
    }
    reopened.undo();
    if (reopened.savedDesignVersions().size() != 2) {
      return fail("deleting a saved design version should be undoable");
    }
  }

  {
    CarouselDocumentController controller;
    controller.createCarouselWithSettings(QVariantMap{
        {QStringLiteral("carouselName"), QStringLiteral("Rich Text Span Proof")},
        {QStringLiteral("documentFormat"), QStringLiteral("4x5")},
        {QStringLiteral("slideCount"), 1},
        {QStringLiteral("backgroundColor"), QStringLiteral("#f7f1e8")},
    });
    const auto copy = QStringLiteral("Powerful ideas need H2O and a source");
    controller.addTextItem(copy);
    const auto rich_text_item_id = controller.selectedItemId();
    controller.setSelectedTextHorizontalAlignment(QStringLiteral("left"));
    controller.setSelectedTextPointSize(82.0);
    controller.setSelectedTextFrameSize(900.0, 420.0);
    controller.setExportManifestEnabled(false);
    controller.setExportImageFormat(QStringLiteral("png"));
    controller.setExportBoardScope(QStringLiteral("current"));
    const auto baseline_dir = dir.filePath(QStringLiteral("rich-text-baseline"));
    const auto styled_dir = dir.filePath(QStringLiteral("rich-text-styled"));
    const auto reopened_dir = dir.filePath(QStringLiteral("rich-text-reopened"));
    QDir().mkpath(baseline_dir);
    QDir().mkpath(styled_dir);
    QDir().mkpath(reopened_dir);
    if (!controller.exportCarouselToDirectory(baseline_dir)) {
      return fail("rich-text proof should export its unformatted baseline");
    }
    const auto powerful_start = static_cast<int>(copy.indexOf(QStringLiteral("Powerful")));
    const auto ideas_start = static_cast<int>(copy.indexOf(QStringLiteral("ideas")));
    const auto superscript_start = static_cast<int>(copy.indexOf(QLatin1Char('2')));
    const auto source_start = static_cast<int>(copy.indexOf(QStringLiteral("source")));
    if (!controller.applySelectedTextSpan(
            powerful_start, 8,
            QVariantMap{{QStringLiteral("emphasis"), true}}) ||
        !controller.applySelectedTextSpan(
            powerful_start, 8,
            QVariantMap{{QStringLiteral("color"), QStringLiteral("#d83a52")}}) ||
        !controller.applySelectedTextSpan(
            ideas_start, 5,
            QVariantMap{{QStringLiteral("weight"), QStringLiteral("black")},
                        {QStringLiteral("italic"), true}}) ||
        !controller.applySelectedTextSpan(
            superscript_start, 1,
            QVariantMap{{QStringLiteral("superscript"), true}}) ||
        !controller.applySelectedTextSpan(
            source_start, 6,
            QVariantMap{{QStringLiteral("link"), QStringLiteral("example.com/reference")}})) {
      return fail("selected ranges should accept emphasis, color, weight, italic, superscript, and links");
    }
    const auto rich_selection = controller.selectionDetails();
    const auto rich_spans = rich_selection.value(QStringLiteral("textSpans")).toList();
    if (rich_spans.size() != 4 ||
        rich_spans.at(0).toMap().value(QStringLiteral("start")).toInt() != powerful_start ||
        !rich_spans.at(0).toMap().value(QStringLiteral("emphasis")).toBool() ||
        rich_spans.at(0).toMap().value(QStringLiteral("color")).toString() !=
            QStringLiteral("#d83a52") ||
        rich_spans.at(1).toMap().value(QStringLiteral("weight")).toString() !=
            QStringLiteral("black") ||
        !rich_spans.at(1).toMap().value(QStringLiteral("italic")).toBool() ||
        !rich_spans.at(2).toMap().value(QStringLiteral("superscript")).toBool() ||
        rich_spans.at(3).toMap().value(QStringLiteral("link")).toString() !=
            QStringLiteral("https://example.com/reference")) {
      return fail("rich-text spans should normalize into deterministic non-overlapping persisted runs");
    }
    bool saw_emphasis = false;
    bool saw_black_italic = false;
    bool saw_superscript = false;
    bool saw_link = false;
    const auto text_lines = controller.canvasItems().constFirst().toMap().value(
        QStringLiteral("textLayoutLines")).toList();
    for (const auto& line_value : text_lines) {
      for (const auto& segment_value :
           line_value.toMap().value(QStringLiteral("segments")).toList()) {
        const auto segment = segment_value.toMap();
        saw_emphasis = saw_emphasis ||
                       (segment.value(QStringLiteral("text")).toString() ==
                            QStringLiteral("Powerful") &&
                        segment.value(QStringLiteral("emphasis")).toBool() &&
                        segment.value(QStringLiteral("color")).toString() ==
                            QStringLiteral("#d83a52"));
        saw_black_italic = saw_black_italic ||
                           (segment.value(QStringLiteral("text")).toString() ==
                                QStringLiteral("ideas") &&
                            segment.value(QStringLiteral("fontWeight")).toString() ==
                                QStringLiteral("black") &&
                            segment.value(QStringLiteral("italic")).toBool());
        saw_superscript = saw_superscript ||
                          (segment.value(QStringLiteral("text")).toString() ==
                               QStringLiteral("2") &&
                           segment.value(QStringLiteral("superscript")).toBool() &&
                           segment.value(QStringLiteral("pointSizeScale")).toDouble() < 1.0);
        saw_link = saw_link ||
                   (segment.value(QStringLiteral("text")).toString() ==
                        QStringLiteral("source") &&
                    segment.value(QStringLiteral("link")).toString() ==
                        QStringLiteral("https://example.com/reference") &&
                    segment.value(QStringLiteral("underline")).toBool());
      }
    }
    if (!saw_emphasis || !saw_black_italic || !saw_superscript || !saw_link ||
        !controller.exportCarouselToDirectory(styled_dir)) {
      return fail("shared rich-text measurement should expose every styled segment and render it for export");
    }
    const QImage baseline_image(
        QDir(baseline_dir).filePath(QStringLiteral("carousel-01.png")));
    const QImage styled_image(
        QDir(styled_dir).filePath(QStringLiteral("carousel-01.png")));
    if (baseline_image.isNull() || styled_image.isNull() || baseline_image == styled_image) {
      return fail("per-span formatting should visibly change the exported pixels");
    }
    controller.undo();
    if (controller.selectionDetails().value(QStringLiteral("textSpans")).toList().size() != 3) {
      return fail("each span-format command should undo independently");
    }
    controller.redo();
    if (controller.selectionDetails().value(QStringLiteral("textSpans")).toList().size() != 4 ||
        !controller.clearSelectedTextSpan(source_start, 6)) {
      return fail("span formatting should redo and selected span formatting should clear explicitly");
    }
    controller.undo();
    if (controller.selectionDetails().value(QStringLiteral("textSpans")).toList().size() != 4) {
      return fail("clearing a text span should be undoable");
    }
    const auto prefixed_copy = QStringLiteral("New ") + copy;
    controller.setSelectedText(prefixed_copy);
    const auto shifted_spans = controller.selectionDetails().value(
        QStringLiteral("textSpans")).toList();
    if (shifted_spans.size() != 4 ||
        shifted_spans.at(0).toMap().value(QStringLiteral("start")).toInt() !=
            powerful_start + 4 ||
        shifted_spans.at(3).toMap().value(QStringLiteral("start")).toInt() !=
            source_start + 4) {
      return fail("rich-text ranges should remap deterministically when surrounding copy changes");
    }
    const auto prefixed_dir = dir.filePath(QStringLiteral("rich-text-prefixed"));
    QDir().mkpath(prefixed_dir);
    if (!controller.exportCarouselToDirectory(prefixed_dir)) {
      return fail("edited copy should export with its remapped rich-text ranges");
    }
    if (!controller.saveDocument(QStringLiteral("Rich Text Span Proof"))) {
      return fail("carousel documents with rich-text spans should save");
    }
    CarouselDocumentController reopened;
    if (!reopened.loadDocument(controller.savedDocumentId())) {
      return fail("carousel documents with rich-text spans should reopen");
    }
    reopened.selectItem(rich_text_item_id);
    if (reopened.selectionDetails().value(QStringLiteral("textSpans")).toList() != shifted_spans ||
        !reopened.exportCarouselToDirectory(reopened_dir)) {
      return fail("rich-text spans should survive save/reopen and remain exportable");
    }
    const QImage reopened_image(
        QDir(reopened_dir).filePath(QStringLiteral("carousel-01.png")));
    const QImage prefixed_before_save(
        QDir(prefixed_dir).filePath(QStringLiteral("carousel-01.png")));
    const auto persisted_lines = reopened.canvasItems().constFirst().toMap().value(
        QStringLiteral("textLayoutLines")).toList();
    if (reopened_image.isNull() || prefixed_before_save.isNull() ||
        persisted_lines.isEmpty() || reopened_image != prefixed_before_save) {
      return fail("reopened rich-text export should reproduce the exact styled document pixels");
    }
  }

  {
    CarouselDocumentController controller;
    controller.createCarouselWithSettings(QVariantMap{
        {QStringLiteral("carouselName"), QStringLiteral("Brand Token Binding Proof")},
        {QStringLiteral("documentFormat"), QStringLiteral("4x5")},
        {QStringLiteral("slideCount"), 1},
        {QStringLiteral("backgroundColor"), QStringLiteral("#f7f3ec")},
    });
    controller.addShapeItem(QStringLiteral("rectangle"));
    const auto token_shape_id = controller.selectedItemId();
    controller.setSelectedShapeFillColor(QStringLiteral("#275dad"));
    controller.setSelectedItemSize(760.0, 520.0);
    controller.setSelectedItemCenter(540.0, 680.0);
    controller.addTextItemWithRole(QStringLiteral("title"), QStringLiteral("FOLLOW THE SYSTEM"));
    const auto token_text_id = controller.selectedItemId();
    controller.setSelectedTextColor(QStringLiteral("#191c24"));
    controller.setSelectedTextFontFamily(QStringLiteral("system"));
    controller.setSelectedTextWeight(QStringLiteral("bold"));
    if (!controller.saveCurrentBoardAsBrandKit(QStringLiteral("Token Binding Brand")) ||
        controller.brandKits().isEmpty()) {
      return fail("brand token proof should create a reusable kit from authored layers");
    }
    const auto brand_id = controller.brandKits().constLast().toMap().value(
        QStringLiteral("brandId")).toString();
    QString accent_binding;
    QString text_color_binding;
    for (const auto& value : controller.brandColorTokens()) {
      const auto token = value.toMap();
      if (token.value(QStringLiteral("brandId")).toString() != brand_id) {
        continue;
      }
      if (token.value(QStringLiteral("tokenId")).toString() == QStringLiteral("accent:0")) {
        accent_binding = token.value(QStringLiteral("bindingId")).toString();
      }
      if (token.value(QStringLiteral("name")).toString() == QStringLiteral("Primary Text")) {
        text_color_binding = token.value(QStringLiteral("bindingId")).toString();
      }
    }
    QString title_type_binding;
    for (const auto& value : controller.brandTypographyTokens()) {
      const auto token = value.toMap();
      if (token.value(QStringLiteral("brandId")).toString() == brand_id &&
          token.value(QStringLiteral("tokenId")).toString() == QStringLiteral("title")) {
        title_type_binding = token.value(QStringLiteral("bindingId")).toString();
        break;
      }
    }
    if (brand_id.isEmpty() || accent_binding.isEmpty() || text_color_binding.isEmpty() ||
        title_type_binding.isEmpty()) {
      return fail("brand kits should expose named color and typography tokens with stable binding identities");
    }
    controller.selectItem(token_shape_id);
    if (!controller.bindSelectedLayerToBrandColorToken(accent_binding) ||
        !controller.selectionDetails().value(QStringLiteral("followsBrandColorToken")).toBool()) {
      return fail("shape layers should bind to a named brand color token");
    }
    controller.selectItem(token_text_id);
    if (!controller.bindSelectedLayerToBrandColorToken(text_color_binding) ||
        !controller.bindSelectedTextToBrandTypographyToken(title_type_binding)) {
      return fail("text layers should independently bind color and typography tokens");
    }
    auto token_text_state = controller.selectionDetails();
    if (!token_text_state.value(QStringLiteral("followsBrandColorToken")).toBool() ||
        !token_text_state.value(QStringLiteral("followsBrandTypographyToken")).toBool() ||
        token_text_state.value(QStringLiteral("brandTypographyTokenId")).toString() !=
            QStringLiteral("title")) {
      return fail("selection metadata should clearly distinguish followed color and type tokens");
    }
    controller.setExportManifestEnabled(false);
    controller.setExportImageFormat(QStringLiteral("png"));
    const auto token_before_dir = dir.filePath(QStringLiteral("brand-token-before"));
    const auto token_after_dir = dir.filePath(QStringLiteral("brand-token-after"));
    const auto token_reopened_dir = dir.filePath(QStringLiteral("brand-token-reopened"));
    QDir().mkpath(token_before_dir);
    QDir().mkpath(token_after_dir);
    QDir().mkpath(token_reopened_dir);
    if (!controller.exportCarouselToDirectory(token_before_dir)) {
      return fail("token-bound layers should export before the source kit changes");
    }

    controller.beginBrandKitEditor(brand_id);
    controller.setBrandKitDraftField(QStringLiteral("accentColor"), QStringLiteral("#f04472"));
    controller.setBrandKitDraftField(QStringLiteral("textColor"), QStringLiteral("#263247"));
    controller.setBrandKitDraftField(QStringLiteral("titleFont"), QStringLiteral("serif"));
    controller.setBrandKitDraftField(QStringLiteral("titleWeight"), QStringLiteral("black"));
    controller.setBrandKitDraftField(
        QStringLiteral("fontLicenseNotes"), QStringLiteral("System and bundled faces verified"));
    if (!controller.saveBrandKitDraft()) {
      return fail("editing a source brand kit should save valid token changes");
    }
    controller.selectItem(token_shape_id);
    const auto first_updated_shape = controller.selectionDetails();
    controller.selectItem(token_text_id);
    token_text_state = controller.selectionDetails();
    if (first_updated_shape.value(QStringLiteral("fillColor")).toString() !=
            QStringLiteral("#f04472") ||
        token_text_state.value(QStringLiteral("textColor")).toString() !=
            QStringLiteral("#263247") ||
        token_text_state.value(QStringLiteral("textFontFamily")).toString() !=
            QStringLiteral("serif") ||
        token_text_state.value(QStringLiteral("textWeight")).toString() !=
            QStringLiteral("black") ||
        !controller.exportCarouselToDirectory(token_after_dir)) {
      return fail("bound layers should immediately follow edited color and typography token values");
    }
    const QImage token_before(
        QDir(token_before_dir).filePath(QStringLiteral("carousel-01.png")));
    const QImage token_after(
        QDir(token_after_dir).filePath(QStringLiteral("carousel-01.png")));
    if (token_before.isNull() || token_after.isNull() || token_before == token_after) {
      return fail("brand token propagation should visibly change exported pixels");
    }

    controller.selectItem(token_shape_id);
    if (!controller.detachSelectedBrandColorToken()) {
      return fail("a followed color token should detach explicitly");
    }
    const auto detached_shape_color = controller.selectionDetails().value(
        QStringLiteral("fillColor")).toString();
    controller.undo();
    if (!controller.selectionDetails().value(QStringLiteral("followsBrandColorToken")).toBool()) {
      return fail("detaching a color token should undo back to the followed state");
    }
    controller.redo();
    if (controller.selectionDetails().value(QStringLiteral("followsBrandColorToken")).toBool() ||
        controller.selectionDetails().value(QStringLiteral("fillColor")).toString() !=
            detached_shape_color) {
      return fail("detaching should retain the resolved color as an ordinary local value");
    }

    controller.selectItem(token_text_id);
    controller.setSelectedTextColor(
        controller.selectionDetails().value(QStringLiteral("textColor")).toString());
    if (controller.selectionDetails().value(QStringLiteral("followsBrandColorToken")).toBool()) {
      return fail("an explicit local text color edit should detach only the color token");
    }
    controller.undo();
    controller.setSelectedTextFontFamily(
        controller.selectionDetails().value(QStringLiteral("textFontFamily")).toString());
    if (controller.selectionDetails().value(QStringLiteral("followsBrandTypographyToken")).toBool()) {
      return fail("an explicit local font edit should detach only the typography token");
    }
    controller.undo();

    controller.beginBrandKitEditor(brand_id);
    controller.setBrandKitDraftField(QStringLiteral("accentColor"), QStringLiteral("#20a66a"));
    controller.setBrandKitDraftField(QStringLiteral("textColor"), QStringLiteral("#4a315f"));
    if (!controller.saveBrandKitDraft()) {
      return fail("a second brand token revision should save");
    }
    controller.selectItem(token_shape_id);
    if (controller.selectionDetails().value(QStringLiteral("fillColor")).toString() !=
            detached_shape_color ||
        controller.selectionDetails().value(QStringLiteral("followsBrandColorToken")).toBool()) {
      return fail("detached layers should remain isolated from later token changes");
    }
    controller.selectItem(token_text_id);
    token_text_state = controller.selectionDetails();
    if (token_text_state.value(QStringLiteral("textColor")).toString() !=
            QStringLiteral("#4a315f") ||
        !token_text_state.value(QStringLiteral("followsBrandColorToken")).toBool() ||
        !token_text_state.value(QStringLiteral("followsBrandTypographyToken")).toBool()) {
      return fail("still-bound text should continue following later kit revisions");
    }
    if (!controller.saveDocument(QStringLiteral("Brand Token Binding Proof"))) {
      return fail("documents with followed and detached token states should save");
    }
    CarouselDocumentController reopened;
    if (!reopened.loadDocument(controller.savedDocumentId())) {
      return fail("documents with brand token bindings should reopen");
    }
    reopened.selectItem(token_shape_id);
    const auto reopened_shape = reopened.selectionDetails();
    reopened.selectItem(token_text_id);
    const auto reopened_text = reopened.selectionDetails();
    if (reopened_shape.value(QStringLiteral("followsBrandColorToken")).toBool() ||
        reopened_shape.value(QStringLiteral("fillColor")).toString() != detached_shape_color ||
        !reopened_text.value(QStringLiteral("followsBrandColorToken")).toBool() ||
        !reopened_text.value(QStringLiteral("followsBrandTypographyToken")).toBool() ||
        reopened_text.value(QStringLiteral("textColor")).toString() != QStringLiteral("#4a315f") ||
        !reopened.exportCarouselToDirectory(token_reopened_dir)) {
      return fail("followed and detached token semantics should survive save/reopen and export");
    }
    const QImage token_reopened(
        QDir(token_reopened_dir).filePath(QStringLiteral("carousel-01.png")));
    if (token_reopened.isNull() || token_reopened == token_after) {
      return fail("reopened export should contain the later followed token revision and detached local shape");
    }
    reopened.deleteBrandKit(brand_id);
  }

  {
    const QHash<QString, int> expected_component_layers{
        {QStringLiteral("logo-lockup"), 4},
        {QStringLiteral("page-number"), 2},
        {QStringLiteral("quote-attribution"), 3},
        {QStringLiteral("stat-card"), 4},
        {QStringLiteral("product-badge"), 3},
        {QStringLiteral("cta-block"), 3},
    };
    const QStringList component_ids{
        QStringLiteral("logo-lockup"), QStringLiteral("page-number"),
        QStringLiteral("quote-attribution"), QStringLiteral("stat-card"),
        QStringLiteral("product-badge"), QStringLiteral("cta-block"),
    };
    CarouselDocumentController controller;
    controller.createCarouselWithSettings(QVariantMap{
        {QStringLiteral("carouselName"), QStringLiteral("Reusable Components Proof")},
        {QStringLiteral("documentFormat"), QStringLiteral("4x5")},
        {QStringLiteral("slideCount"), 7},
    });
    QSet<QString> discovered_components;
    for (const auto& value : controller.componentLibraryEntries()) {
      const auto entry = value.toMap();
      if (entry.value(QStringLiteral("source")).toString() != QStringLiteral("built-in")) {
        continue;
      }
      const auto component_id = entry.value(QStringLiteral("componentId")).toString();
      discovered_components.insert(component_id);
      const auto preview_path = QUrl(
          entry.value(QStringLiteral("previewSource")).toString()).toLocalFile();
      const QImage preview(preview_path);
      if (!expected_component_layers.contains(component_id) || preview.isNull() ||
          preview.size() != QSize(240, 160) ||
          entry.value(QStringLiteral("previewMaximumPixelWidth")).toInt() != 240 ||
          entry.value(QStringLiteral("previewMaximumPixelHeight")).toInt() != 160) {
        return fail("each built-in component should expose an exact bounded preview thumbnail and layer count");
      }
    }
    if (discovered_components != QSet<QString>(component_ids.cbegin(), component_ids.cend())) {
      return fail("component library should contain the six required production component types");
    }

    QSet<QString> placed_instance_ids;
    QString editable_text_id;
    QString editable_shape_id;
    for (int board_index = 0; board_index < component_ids.size(); ++board_index) {
      controller.selectSlide(board_index);
      const auto before_count = controller.canvasItems().size();
      const auto component_id = component_ids.at(board_index);
      if (!controller.applyBuiltInComponent(component_id)) {
        return fail("each built-in component should place directly on the current board");
      }
      const auto items = controller.canvasItems();
      if (items.size() - before_count != expected_component_layers.value(component_id)) {
        return fail("built-in components should create their complete documented editable layer set");
      }
      QString instance_id;
      QSet<QString> part_ids;
      const auto board_width = static_cast<double>(controller.documentPixelWidth()) /
                               controller.slideCount();
      const auto board_height = static_cast<double>(controller.documentPixelHeight());
      const QRectF board_bounds(board_width * board_index, 0.0, board_width, board_height);
      for (qsizetype item_index = before_count; item_index < items.size(); ++item_index) {
        const auto item = items.at(item_index).toMap();
        const auto item_instance = item.value(QStringLiteral("componentInstanceId")).toString();
        if (instance_id.isEmpty()) {
          instance_id = item_instance;
        }
        const QRectF bounds(
            item.value(QStringLiteral("x")).toDouble(),
            item.value(QStringLiteral("y")).toDouble(),
            item.value(QStringLiteral("width")).toDouble(),
            item.value(QStringLiteral("height")).toDouble());
        if (item.value(QStringLiteral("componentDefinitionId")).toString() !=
                QStringLiteral("builtin:%1").arg(component_id) ||
            item_instance.isEmpty() || item_instance != instance_id ||
            item.value(QStringLiteral("componentDefinitionVersion")).toInt() != 1 ||
            item.value(QStringLiteral("componentPartId")).toString().isEmpty() ||
            item.value(QStringLiteral("objectGroupId")).toString() != instance_id ||
            item.value(QStringLiteral("locked")).toBool() || bounds.isEmpty() ||
            !board_bounds.contains(bounds)) {
          return fail("placed components should be unlocked, grouped, versioned ordinary layers contained by their board");
        }
        part_ids.insert(item.value(QStringLiteral("componentPartId")).toString());
        if (editable_text_id.isEmpty() &&
            item.value(QStringLiteral("type")).toString() == QStringLiteral("text")) {
          editable_text_id = item.value(QStringLiteral("itemId")).toString();
        }
        if (editable_shape_id.isEmpty() &&
            item.value(QStringLiteral("type")).toString() == QStringLiteral("shape")) {
          editable_shape_id = item.value(QStringLiteral("itemId")).toString();
        }
      }
      if (part_ids.size() != expected_component_layers.value(component_id) ||
          placed_instance_ids.contains(instance_id)) {
        return fail("each placement should have unique instance identity and stable unique part identities");
      }
      placed_instance_ids.insert(instance_id);
    }
    if (editable_text_id.isEmpty() || editable_shape_id.isEmpty()) {
      return fail("built-in components should expose editable text and shape layers");
    }
    controller.selectItem(editable_text_id);
    controller.setSelectedText(QStringLiteral("EDITED COMPONENT COPY"));
    controller.selectItem(editable_shape_id);
    controller.setSelectedShapeFillColor(QStringLiteral("#ff5d57"));
    controller.selectSlide(0);
    const auto before_duplicate_count = controller.canvasItems().size();
    if (!controller.applyBuiltInComponent(QStringLiteral("logo-lockup")) ||
        controller.canvasItems().size() != before_duplicate_count + 4) {
      return fail("a built-in component should be reusable more than once");
    }
    QString duplicate_instance_id;
    for (qsizetype index = before_duplicate_count; index < controller.canvasItems().size(); ++index) {
      const auto item = controller.canvasItems().at(index).toMap();
      const auto instance_id = item.value(QStringLiteral("componentInstanceId")).toString();
      duplicate_instance_id = duplicate_instance_id.isEmpty() ? instance_id : duplicate_instance_id;
      if (instance_id != duplicate_instance_id || placed_instance_ids.contains(instance_id)) {
        return fail("reusing a component should create a fresh independent instance");
      }
    }
    controller.undo();
    if (controller.canvasItems().size() != before_duplicate_count) {
      return fail("adding a reusable component should undo atomically");
    }
    controller.redo();
    if (controller.canvasItems().size() != before_duplicate_count + 4) {
      return fail("adding a reusable component should redo atomically");
    }
    if (!controller.saveSelectionAsTemplate(
            QStringLiteral("Saved Logo Lockup"), QStringLiteral("Components"))) {
      return fail("a placed component selection should save as a custom reusable definition");
    }
    QString custom_component_id;
    QString custom_preview_path;
    for (const auto& value : controller.componentLibraryEntries()) {
      const auto entry = value.toMap();
      if (entry.value(QStringLiteral("source")).toString() == QStringLiteral("custom") &&
          entry.value(QStringLiteral("name")).toString() == QStringLiteral("Saved Logo Lockup")) {
        custom_component_id = entry.value(QStringLiteral("componentId")).toString();
        custom_preview_path = QUrl(
            entry.value(QStringLiteral("previewSource")).toString()).toLocalFile();
        break;
      }
    }
    const QImage custom_preview(custom_preview_path);
    if (custom_component_id.isEmpty() || custom_preview.isNull() ||
        custom_preview.width() > 320 || custom_preview.height() > 400) {
      return fail("saved custom components should receive an actual bounded thumbnail preview");
    }
    controller.selectSlide(6);
    const auto before_custom_count = controller.canvasItems().size();
    controller.applyUserTemplate(custom_component_id, false);
    if (controller.canvasItems().size() != before_custom_count + 4) {
      return fail("a custom saved component should be reusable on another board");
    }
    QString custom_instance_id;
    for (qsizetype index = before_custom_count; index < controller.canvasItems().size(); ++index) {
      const auto item = controller.canvasItems().at(index).toMap();
      const auto instance_id = item.value(QStringLiteral("componentInstanceId")).toString();
      custom_instance_id = custom_instance_id.isEmpty() ? instance_id : custom_instance_id;
      if (item.value(QStringLiteral("componentDefinitionId")).toString() !=
              QStringLiteral("custom:%1").arg(custom_component_id) ||
          instance_id != custom_instance_id || instance_id.isEmpty()) {
        return fail("custom component placement should keep a fresh definition and instance identity");
      }
    }
    if (!controller.saveDocument(QStringLiteral("Reusable Components Proof"))) {
      return fail("documents containing built-in and custom components should save");
    }
    const auto saved_item_count = controller.canvasItems().size();
    CarouselDocumentController reopened;
    if (!reopened.loadDocument(controller.savedDocumentId()) ||
        reopened.canvasItems().size() != saved_item_count) {
      return fail("component definitions, instances, parts, edits, and geometry should survive save/reopen");
    }
    bool kept_edited_copy = false;
    int identified_layers = 0;
    for (const auto& value : reopened.canvasItems()) {
      const auto item = value.toMap();
      kept_edited_copy = kept_edited_copy ||
                         item.value(QStringLiteral("text")).toString() ==
                             QStringLiteral("EDITED COMPONENT COPY");
      if (!item.value(QStringLiteral("componentDefinitionId")).toString().isEmpty() &&
          !item.value(QStringLiteral("componentInstanceId")).toString().isEmpty() &&
          !item.value(QStringLiteral("componentPartId")).toString().isEmpty()) {
        ++identified_layers;
      }
    }
    if (!kept_edited_copy || identified_layers != saved_item_count) {
      return fail("reopened components should retain ordinary local layer edits and complete identities");
    }
    reopened.setExportManifestEnabled(false);
    reopened.setExportImageFormat(QStringLiteral("png"));
    const auto component_export_dir = dir.filePath(QStringLiteral("reusable-component-export"));
    QDir().mkpath(component_export_dir);
    if (!reopened.exportCarouselToDirectory(component_export_dir)) {
      return fail("documents with reusable components should export");
    }
    for (int index = 0; index < 7; ++index) {
      const auto file_name = QStringLiteral("carousel-%1.png").arg(index + 1, 2, 10, QLatin1Char('0'));
      if (QImage(QDir(component_export_dir).filePath(file_name)).isNull()) {
        return fail("component export should render every edited component board");
      }
    }
    reopened.deleteUserTemplate(custom_component_id);

    for (const auto& format : {QStringLiteral("4x5"), QStringLiteral("1x1"),
                               QStringLiteral("9x16"), QStringLiteral("5x4")}) {
      CarouselDocumentController ratio_controller;
      ratio_controller.createCarouselWithSettings(QVariantMap{
          {QStringLiteral("carouselName"), QStringLiteral("Component Ratio Proof")},
          {QStringLiteral("documentFormat"), format},
          {QStringLiteral("slideCount"), 1},
      });
      for (const auto& component_id : component_ids) {
        const auto before_count = ratio_controller.canvasItems().size();
        if (!ratio_controller.applyBuiltInComponent(component_id)) {
          return fail("every reusable component should adapt to every supported ratio");
        }
        const QRectF board_bounds(
            0.0, 0.0, ratio_controller.documentPixelWidth(),
            ratio_controller.documentPixelHeight());
        for (qsizetype index = before_count; index < ratio_controller.canvasItems().size(); ++index) {
          const auto item = ratio_controller.canvasItems().at(index).toMap();
          const QRectF bounds(
              item.value(QStringLiteral("x")).toDouble(),
              item.value(QStringLiteral("y")).toDouble(),
              item.value(QStringLiteral("width")).toDouble(),
              item.value(QStringLiteral("height")).toDouble());
          if (bounds.isEmpty() || !board_bounds.contains(bounds)) {
            return fail("ratio-adaptive components should remain fully inside the target board");
          }
        }
      }
    }
  }

  {
    CarouselDocumentController controller;
    controller.createCarouselWithSettings(QVariantMap{
        {QStringLiteral("carouselName"), QStringLiteral("Brand Theme Variant Proof")},
        {QStringLiteral("documentFormat"), QStringLiteral("4x5")},
        {QStringLiteral("slideCount"), 4},
        {QStringLiteral("backgroundColor"), QStringLiteral("#f4efe6")},
    });
    QImage theme_media(QSize(180, 120), QImage::Format_ARGB32);
    theme_media.fill(QColor(QStringLiteral("#2b74c8")));
    const auto theme_media_path =
        write_image(dir, QStringLiteral("brand-theme-content.png"), theme_media);
    if (theme_media_path.isEmpty()) {
      return fail("brand theme proof media fixture should save");
    }
    for (int index = 0; index < 4; ++index) {
      controller.selectSlide(index);
      controller.addPhotoItem(
          theme_media_path, QStringLiteral("Theme content %1").arg(index + 1));
      controller.setSelectedItemSize(360.0, 240.0);
      controller.setSelectedItemCenter(540.0, 430.0);
      controller.addShapeItem(QStringLiteral("rectangle"));
      controller.setSelectedShapeFillColor(QStringLiteral("#e36b36"));
      controller.setSelectedItemSize(500.0, 170.0);
      controller.setSelectedItemCenter(540.0, 900.0);
      controller.addTextItemWithRole(
          QStringLiteral("title"),
          QStringLiteral("THEME BOARD %1").arg(index + 1));
      controller.setSelectedTextColor(QStringLiteral("#172033"));
      controller.setSelectedItemSize(720.0, 130.0);
      controller.setSelectedItemCenter(540.0, 700.0);
    }
    const auto content_state = [](const CarouselDocumentController& document) {
      QVariantMap state;
      for (const auto& value : document.canvasItems()) {
        const auto item = value.toMap();
        state.insert(
            item.value(QStringLiteral("itemId")).toString(),
            QVariantMap{
                {QStringLiteral("type"), item.value(QStringLiteral("type"))},
                {QStringLiteral("x"), item.value(QStringLiteral("x"))},
                {QStringLiteral("y"), item.value(QStringLiteral("y"))},
                {QStringLiteral("width"), item.value(QStringLiteral("width"))},
                {QStringLiteral("height"), item.value(QStringLiteral("height"))},
                {QStringLiteral("rotation"), item.value(QStringLiteral("rotation"))},
                {QStringLiteral("text"), item.value(QStringLiteral("text"))},
                {QStringLiteral("textSpans"), item.value(QStringLiteral("textSpans"))},
                {QStringLiteral("sourceUrl"), item.value(QStringLiteral("sourceUrl"))},
                {QStringLiteral("shapeKind"), item.value(QStringLiteral("shapeKind"))},
                {QStringLiteral("objectGroupId"), item.value(QStringLiteral("objectGroupId"))},
                {QStringLiteral("componentLinkId"), item.value(QStringLiteral("componentLinkId"))},
            });
      }
      return state;
    };
    const auto authored_content = content_state(controller);
    controller.selectSlide(0);
    if (!controller.saveCurrentBoardAsBrandKit(QStringLiteral("Variant Brand")) ||
        controller.brandKits().isEmpty()) {
      return fail("brand theme proof should create a reusable source kit");
    }
    const auto brand_id = controller.brandKits().constLast().toMap().value(
        QStringLiteral("brandId")).toString();
    controller.beginBrandKitEditor(brand_id);
    controller.setBrandKitDraftField(
        QStringLiteral("darkBackgroundColor"), QStringLiteral("#111827"));
    controller.setBrandKitDraftField(
        QStringLiteral("darkTextColor"), QStringLiteral("#f9fafb"));
    controller.setBrandKitDraftField(
        QStringLiteral("darkAccentColor"), QStringLiteral("#38bdf8"));
    controller.setBrandKitDraftField(
        QStringLiteral("campaignBackgroundColor"), QStringLiteral("#7c3aed"));
    controller.setBrandKitDraftField(
        QStringLiteral("campaignTextColor"), QStringLiteral("#ffffff"));
    controller.setBrandKitDraftField(
        QStringLiteral("campaignAccentColor"), QStringLiteral("#facc15"));
    if (!controller.saveBrandKitDraft()) {
      return fail("dark and campaign brand theme values should be editable and saveable");
    }
    int variant_count = 0;
    for (const auto& value : controller.brandThemeVariants()) {
      const auto variant = value.toMap();
      if (variant.value(QStringLiteral("brandId")).toString() == brand_id) {
        ++variant_count;
      }
    }
    if (variant_count != 3) {
      return fail("each brand kit should expose exactly light, dark, and campaign variants");
    }

    controller.setExportManifestEnabled(false);
    controller.setExportImageFormat(QStringLiteral("png"));
    const auto base_export_dir = dir.filePath(QStringLiteral("brand-theme-base"));
    const auto dark_export_dir = dir.filePath(QStringLiteral("brand-theme-dark-selected"));
    const auto reopened_export_dir = dir.filePath(QStringLiteral("brand-theme-reopened"));
    QDir().mkpath(base_export_dir);
    QDir().mkpath(dark_export_dir);
    QDir().mkpath(reopened_export_dir);
    if (!controller.exportCarouselToDirectory(base_export_dir)) {
      return fail("authored boards should export before a theme switch");
    }
    const auto base_slides = controller.slides();
    controller.selectSlide(1);
    controller.selectSlideRange(2);
    if (controller.selectedSlideCount() != 2 ||
        !controller.applyBrandThemeVariant(
            brand_id, QStringLiteral("dark"), QStringLiteral("selected"))) {
      return fail("a selected board range should switch to the dark brand theme");
    }
    const auto dark_slides = controller.slides();
    for (int index = 0; index < 4; ++index) {
      const auto slide = dark_slides.at(index).toMap();
      const auto should_be_dark = index == 1 || index == 2;
      if ((slide.value(QStringLiteral("brandThemeKitId")).toString() == brand_id) !=
              should_be_dark ||
          (slide.value(QStringLiteral("brandThemeVariantId")).toString() ==
               QStringLiteral("dark")) != should_be_dark) {
        return fail("selected theme switching should affect exactly the selected board range");
      }
      if (!should_be_dark &&
          slide.value(QStringLiteral("backgroundColor")) !=
              base_slides.at(index).toMap().value(QStringLiteral("backgroundColor"))) {
        return fail("selected theme switching should leave unselected board surfaces untouched");
      }
    }
    if (content_state(controller) != authored_content ||
        !controller.exportCarouselToDirectory(dark_export_dir)) {
      return fail("theme switching should preserve every layer identity, asset, copy, and geometry");
    }
    for (int index = 0; index < 4; ++index) {
      const auto file_name = QStringLiteral("carousel-%1.png").arg(index + 1, 2, 10, QLatin1Char('0'));
      const QImage before(QDir(base_export_dir).filePath(file_name));
      const QImage after(QDir(dark_export_dir).filePath(file_name));
      if (before.isNull() || after.isNull() ||
          ((before != after) != (index == 1 || index == 2))) {
        return fail("selected theme exports should change only the targeted board pixels");
      }
    }

    controller.undo();
    const auto undone_slides = controller.slides();
    for (int index = 0; index < 4; ++index) {
      const auto slide = undone_slides.at(index).toMap();
      if (!slide.value(QStringLiteral("brandThemeKitId")).toString().isEmpty() ||
          !slide.value(QStringLiteral("brandThemeVariantId")).toString().isEmpty() ||
          slide.value(QStringLiteral("backgroundColor")) !=
              base_slides.at(index).toMap().value(QStringLiteral("backgroundColor"))) {
        return fail("a selected-range theme switch should undo atomically in one step");
      }
    }
    controller.redo();
    if (controller.slides().at(1).toMap().value(
            QStringLiteral("brandThemeVariantId")).toString() != QStringLiteral("dark") ||
        controller.slides().at(2).toMap().value(
            QStringLiteral("brandThemeVariantId")).toString() != QStringLiteral("dark") ||
        content_state(controller) != authored_content) {
      return fail("an atomic theme switch should redo without disturbing authored content");
    }

    controller.beginBrandKitEditor(brand_id);
    controller.setBrandKitDraftField(
        QStringLiteral("darkBackgroundColor"), QStringLiteral("#0b1020"));
    controller.setBrandKitDraftField(
        QStringLiteral("darkAccentColor"), QStringLiteral("#fb7185"));
    if (!controller.saveBrandKitDraft() ||
        controller.slides().at(1).toMap().value(
            QStringLiteral("backgroundColor")).toString() != QStringLiteral("#0b1020") ||
        controller.slides().at(2).toMap().value(
            QStringLiteral("backgroundColor")).toString() != QStringLiteral("#0b1020") ||
        content_state(controller) != authored_content) {
      return fail("placed theme variants should follow later source-kit token revisions without losing content");
    }
    if (!controller.applyBrandThemeVariant(
            brand_id, QStringLiteral("campaign"), QStringLiteral("all"))) {
      return fail("a campaign theme should apply across the complete document");
    }
    for (const auto& value : controller.slides()) {
      const auto slide = value.toMap();
      if (slide.value(QStringLiteral("brandThemeKitId")).toString() != brand_id ||
          slide.value(QStringLiteral("brandThemeVariantId")).toString() !=
              QStringLiteral("campaign")) {
        return fail("all-board theme scope should switch every board");
      }
    }
    const auto current_index = controller.currentIndex();
    if (!controller.applyBrandThemeVariant(
            brand_id, QStringLiteral("light"), QStringLiteral("current"))) {
      return fail("the current board should switch independently to the light theme");
    }
    for (int index = 0; index < 4; ++index) {
      const auto variant = controller.slides().at(index).toMap().value(
          QStringLiteral("brandThemeVariantId")).toString();
      if (variant != (index == current_index ? QStringLiteral("light")
                                             : QStringLiteral("campaign"))) {
        return fail("current theme scope should leave every other board variant intact");
      }
    }
    if (content_state(controller) != authored_content ||
        !controller.saveDocument(QStringLiteral("Brand Theme Variant Proof"))) {
      return fail("mixed per-board theme variants should preserve content and save");
    }
    CarouselDocumentController reopened;
    if (!reopened.loadDocument(controller.savedDocumentId()) ||
        content_state(reopened) != authored_content) {
      return fail("theme identities and unchanged authored content should survive save/reopen");
    }
    for (int index = 0; index < 4; ++index) {
      const auto variant = reopened.slides().at(index).toMap().value(
          QStringLiteral("brandThemeVariantId")).toString();
      if (variant != (index == current_index ? QStringLiteral("light")
                                             : QStringLiteral("campaign"))) {
        return fail("save/reopen should retain each board's resolved brand theme variant");
      }
    }
    if (!reopened.exportCarouselToDirectory(reopened_export_dir)) {
      return fail("reopened mixed-theme documents should remain exportable");
    }
    for (int index = 0; index < 4; ++index) {
      const auto file_name = QStringLiteral("carousel-%1.png").arg(index + 1, 2, 10, QLatin1Char('0'));
      if (QImage(QDir(reopened_export_dir).filePath(file_name)).isNull()) {
        return fail("mixed-theme export should produce every resolved board image");
      }
    }
    reopened.deleteBrandKit(brand_id);
  }

  {
    CarouselDocumentController controller;
    controller.createCarouselWithSettings(QVariantMap{
        {QStringLiteral("carouselName"), QStringLiteral("Explicit Constraint Stack")},
        {QStringLiteral("documentFormat"), QStringLiteral("4x5")},
        {QStringLiteral("slideCount"), 1},
    });
    QImage stack_media(QSize(640, 480), QImage::Format_ARGB32);
    stack_media.fill(QColor(QStringLiteral("#2dca72")));
    const auto stack_media_path =
        write_image(dir, QStringLiteral("constraint-stack-media.png"), stack_media);
    if (stack_media_path.isEmpty()) {
      return fail("constraint-stack media fixture should save");
    }

    controller.addTextItem(QStringLiteral("Short title"));
    const auto stack_title_id = controller.selectedItemId();
    controller.setSelectedItemSize(360.0, 80.0);
    controller.setSelectedItemCenter(320.0, 190.0);
    controller.addPhotoItem(stack_media_path, QStringLiteral("Stack media"));
    const auto stack_media_id = controller.selectedItemId();
    controller.setSelectedItemSize(420.0, 315.0);
    controller.setSelectedItemCenter(320.0, 590.0);
    controller.addTextItem(QStringLiteral("Supporting caption"));
    const auto stack_caption_id = controller.selectedItemId();
    controller.setSelectedItemSize(420.0, 54.0);
    controller.setSelectedItemCenter(320.0, 1030.0);

    controller.selectItem(stack_title_id);
    controller.addItemToSelection(stack_media_id);
    controller.addItemToSelection(stack_caption_id);
    if (!controller.createConstraintStack(QStringLiteral("vertical"), 32.0, 28.0)) {
      return fail("multiple unlocked layers on one board should create an explicit vertical stack");
    }
    auto stack_state = controller.selectionDetails();
    if (!stack_state.value(QStringLiteral("isConstraintStack")).toBool() ||
        stack_state.value(QStringLiteral("constraintStackCount")).toInt() != 3 ||
        stack_state.value(QStringLiteral("constraintStackAxis")).toString() !=
            QStringLiteral("vertical") ||
        qAbs(stack_state.value(QStringLiteral("constraintStackPadding")).toDouble() - 32.0) > 0.001 ||
        qAbs(stack_state.value(QStringLiteral("constraintStackGap")).toDouble() - 28.0) > 0.001) {
      return fail("constraint stack state should expose its explicit members, axis, padding, and gap");
    }
    auto item_by_id = [&controller](const QString& item_id) {
      for (const auto& value : controller.canvasItems()) {
        const auto item = value.toMap();
        if (item.value(QStringLiteral("itemId")).toString() == item_id) {
          return item;
        }
      }
      return QVariantMap{};
    };
    auto title_item = item_by_id(stack_title_id);
    auto media_item = item_by_id(stack_media_id);
    auto caption_item = item_by_id(stack_caption_id);
    if (title_item.value(QStringLiteral("constraintStackOrder")).toInt() != 0 ||
        media_item.value(QStringLiteral("constraintStackOrder")).toInt() != 1 ||
        caption_item.value(QStringLiteral("constraintStackOrder")).toInt() != 2 ||
        qAbs(media_item.value(QStringLiteral("y")).toDouble() -
             (title_item.value(QStringLiteral("y")).toDouble() +
              title_item.value(QStringLiteral("height")).toDouble() + 28.0)) > 0.001 ||
        qAbs(caption_item.value(QStringLiteral("y")).toDouble() -
             (media_item.value(QStringLiteral("y")).toDouble() +
              media_item.value(QStringLiteral("height")).toDouble() + 28.0)) > 0.001) {
      return fail("vertical stack creation should preserve deterministic order and exact gaps");
    }

    controller.selectItem(stack_title_id);
    const auto title_height_before = title_item.value(QStringLiteral("height")).toDouble();
    const auto media_y_before = media_item.value(QStringLiteral("y")).toDouble();
    controller.setSelectedText(QStringLiteral(
        "A substantially longer title that wraps across several lines and proves that explicit card spacing follows real measured copy height"));
    title_item = item_by_id(stack_title_id);
    media_item = item_by_id(stack_media_id);
    if (title_item.value(QStringLiteral("height")).toDouble() <= title_height_before ||
        media_item.value(QStringLiteral("y")).toDouble() <= media_y_before ||
        qAbs(media_item.value(QStringLiteral("y")).toDouble() -
             (title_item.value(QStringLiteral("y")).toDouble() +
              title_item.value(QStringLiteral("height")).toDouble() + 28.0)) > 0.001) {
      return fail("content-height copy edits should reflow following stack media by the exact saved gap");
    }
    controller.undo();
    title_item = item_by_id(stack_title_id);
    media_item = item_by_id(stack_media_id);
    if (qAbs(title_item.value(QStringLiteral("height")).toDouble() - title_height_before) > 0.001 ||
        qAbs(media_item.value(QStringLiteral("y")).toDouble() - media_y_before) > 0.001) {
      return fail("copy-driven stack reflow should undo as the original text edit");
    }
    controller.redo();

    if (!controller.updateSelectedConstraintStack(QStringLiteral("vertical"), 40.0, 42.0)) {
      return fail("an explicit stack should allow direct padding and gap edits");
    }
    title_item = item_by_id(stack_title_id);
    media_item = item_by_id(stack_media_id);
    stack_state = controller.selectionDetails();
    if (qAbs(stack_state.value(QStringLiteral("constraintStackPadding")).toDouble() - 40.0) > 0.001 ||
        qAbs(stack_state.value(QStringLiteral("constraintStackGap")).toDouble() - 42.0) > 0.001 ||
        qAbs(media_item.value(QStringLiteral("y")).toDouble() -
             (title_item.value(QStringLiteral("y")).toDouble() +
              title_item.value(QStringLiteral("height")).toDouble() + 42.0)) > 0.001) {
      return fail("stack padding and gaps should be visible state backed by exact geometry");
    }
    if (!controller.updateSelectedConstraintStack(QStringLiteral("horizontal"), 40.0, 42.0)) {
      return fail("an explicit stack should switch axes without a hidden layout mode");
    }
    title_item = item_by_id(stack_title_id);
    media_item = item_by_id(stack_media_id);
    if (qAbs(media_item.value(QStringLiteral("x")).toDouble() -
             (title_item.value(QStringLiteral("x")).toDouble() +
              title_item.value(QStringLiteral("width")).toDouble() + 42.0)) > 0.001) {
      return fail("horizontal stacks should maintain their exact saved gap");
    }
    controller.updateSelectedConstraintStack(QStringLiteral("vertical"), 40.0, 42.0);

    title_item = item_by_id(stack_title_id);
    media_item = item_by_id(stack_media_id);
    const auto title_x_before_move = title_item.value(QStringLiteral("x")).toDouble();
    const auto media_x_before_move = media_item.value(QStringLiteral("x")).toDouble();
    controller.moveSelectedItemByPixels(55.0, 35.0);
    title_item = item_by_id(stack_title_id);
    media_item = item_by_id(stack_media_id);
    controller.reflowSelectedConstraintStack();
    if (qAbs(title_item.value(QStringLiteral("x")).toDouble() - title_x_before_move - 55.0) > 0.001 ||
        qAbs(media_item.value(QStringLiteral("x")).toDouble() - media_x_before_move - 55.0) > 0.001 ||
        qAbs(item_by_id(stack_title_id).value(QStringLiteral("x")).toDouble() -
             title_item.value(QStringLiteral("x")).toDouble()) > 0.001) {
      return fail("dragging one stack member should move the explicit stack and its anchor as one unit");
    }

    controller.selectItem(stack_title_id);
    controller.setSelectedItemLocked(true);
    controller.selectItem(stack_media_id);
    const auto locked_title_geometry = item_by_id(stack_title_id);
    const auto locked_media_geometry = item_by_id(stack_media_id);
    controller.moveSelectedItemByPixels(90.0, 70.0);
    if (item_by_id(stack_title_id).value(QStringLiteral("x")).toDouble() !=
            locked_title_geometry.value(QStringLiteral("x")).toDouble() ||
        item_by_id(stack_media_id).value(QStringLiteral("x")).toDouble() !=
            locked_media_geometry.value(QStringLiteral("x")).toDouble() ||
        controller.updateSelectedConstraintStack(QStringLiteral("vertical"), 44.0, 48.0) ||
        controller.reflowSelectedConstraintStack() || controller.detachSelectedConstraintStack()) {
      return fail("a partially locked stack should reject whole-stack movement, edits, reflow, and detach");
    }
    controller.selectItem(stack_title_id);
    controller.setSelectedItemLocked(false);

    controller.selectItem(stack_title_id);
    controller.addItemToSelection(stack_media_id);
    controller.addItemToSelection(stack_caption_id);
    if (!controller.saveSelectionAsTemplate(
            QStringLiteral("Explicit Stack Card"), QStringLiteral("Components"))) {
      return fail("an explicit stack should save as a reusable component");
    }
    QString stack_component_template_id;
    for (const auto& value : controller.userTemplates()) {
      const auto entry = value.toMap();
      if (entry.value(QStringLiteral("name")).toString() ==
          QStringLiteral("Explicit Stack Card")) {
        stack_component_template_id =
            entry.value(QStringLiteral("templateId")).toString();
      }
    }
    QStringList item_ids_before_template_apply;
    for (const auto& value : controller.canvasItems()) {
      item_ids_before_template_apply.push_back(
          value.toMap().value(QStringLiteral("itemId")).toString());
    }
    if (stack_component_template_id.isEmpty()) {
      return fail("saved stack components should expose a reusable template identity");
    }
    controller.applyUserTemplate(stack_component_template_id, false);
    QString applied_stack_id;
    int applied_stack_members = 0;
    for (const auto& value : controller.canvasItems()) {
      const auto item = value.toMap();
      if (item_ids_before_template_apply.contains(
              item.value(QStringLiteral("itemId")).toString())) {
        continue;
      }
      const auto candidate_stack_id =
          item.value(QStringLiteral("constraintStackId")).toString();
      if (candidate_stack_id.isEmpty() ||
          (!applied_stack_id.isEmpty() && candidate_stack_id != applied_stack_id)) {
        return fail("applied stack components should retain one explicit remapped stack identity");
      }
      applied_stack_id = candidate_stack_id;
      ++applied_stack_members;
    }
    const auto original_stack_id =
        item_by_id(stack_title_id).value(QStringLiteral("constraintStackId")).toString();
    if (applied_stack_members != 3 || applied_stack_id.isEmpty() ||
        applied_stack_id == original_stack_id ||
        !controller.selectionDetails().value(QStringLiteral("isConstraintStack")).toBool()) {
      return fail("reusable stack components should restore three members under a fresh stack identity");
    }

    const auto stack_export_a = dir.filePath(QStringLiteral("constraint-stack-export-a"));
    const auto stack_export_b = dir.filePath(QStringLiteral("constraint-stack-export-b"));
    QDir().mkpath(stack_export_a);
    QDir().mkpath(stack_export_b);
    controller.setExportManifestEnabled(false);
    if (!controller.exportCarouselToDirectory(stack_export_a) ||
        !controller.saveDocument(QStringLiteral("Explicit Constraint Stack"))) {
      return fail("constraint stack document should export and save");
    }
    CarouselDocumentController restored;
    if (!restored.loadDocument(controller.savedDocumentId())) {
      return fail("constraint stack document should reopen");
    }
    restored.selectItem(stack_title_id);
    const auto restored_stack_state = restored.selectionDetails();
    if (!restored_stack_state.value(QStringLiteral("isConstraintStack")).toBool() ||
        restored_stack_state.value(QStringLiteral("constraintStackCount")).toInt() != 3 ||
        restored_stack_state.value(QStringLiteral("constraintStackAxis")).toString() !=
            QStringLiteral("vertical") ||
        qAbs(restored_stack_state.value(QStringLiteral("constraintStackPadding")).toDouble() - 40.0) > 0.001 ||
        qAbs(restored_stack_state.value(QStringLiteral("constraintStackGap")).toDouble() - 42.0) > 0.001 ||
        !restored.exportCarouselToDirectory(stack_export_b)) {
      return fail("constraint membership, axis, padding, gap, and export should survive save and reopen");
    }
    QFile first_stack_export(
        QDir(stack_export_a).filePath(QStringLiteral("carousel-01.png")));
    QFile second_stack_export(
        QDir(stack_export_b).filePath(QStringLiteral("carousel-01.png")));
    if (!first_stack_export.open(QIODevice::ReadOnly) ||
        !second_stack_export.open(QIODevice::ReadOnly) ||
        first_stack_export.readAll() != second_stack_export.readAll()) {
      return fail("reopened constraint stacks should export byte-identical normal layer pixels");
    }
    const auto detached_geometry = restored.selectionDetails();
    if (!restored.detachSelectedConstraintStack() ||
        restored.selectionDetails().value(QStringLiteral("isConstraintStack")).toBool() ||
        qAbs(restored.selectionDetails().value(QStringLiteral("x")).toDouble() -
             detached_geometry.value(QStringLiteral("x")).toDouble()) > 0.001) {
      return fail("detaching a stack should retain current geometry and ordinary editable layers");
    }
    restored.undo();
    if (!restored.selectionDetails().value(QStringLiteral("isConstraintStack")).toBool()) {
      return fail("detaching a constraint stack should undo back to the explicit relationship");
    }
  }

  {
    CarouselDocumentController controller;
    controller.createCarouselWithSettings(QVariantMap{
        {QStringLiteral("carouselName"), QStringLiteral("Preview Invalidation")},
        {QStringLiteral("documentFormat"), QStringLiteral("4x5")},
        {QStringLiteral("slideCount"), 3},
    });
    controller.selectSlide(0);
    controller.addTextItem(QStringLiteral("Board zero text"));
    const auto first_item_id = controller.selectedItemId();
    controller.addTextItem(QStringLiteral("Second selectable layer"));
    const auto second_item_id = controller.selectedItemId();
    controller.selectSlide(1);
    controller.addTextItem(QStringLiteral("Board one text"));
    if (!controller.saveDocument(QStringLiteral("Preview Invalidation"))) {
      return fail("preview invalidation fixture should save before selection-only checks");
    }

    const auto revision_before_selection =
        controller.previewDiagnostics().value(QStringLiteral("revision")).toInt();
    controller.selectItem(first_item_id);
    controller.selectItem(second_item_id);
    if (controller.documentDirty() ||
        controller.previewDiagnostics().value(QStringLiteral("revision")).toInt() !=
            revision_before_selection) {
      return fail("selection chrome changes should neither dirty nor rebuild document previews");
    }

    controller.selectItem(first_item_id);
    controller.setSelectedTextLineHeight(1.45);
    controller.setSelectedTextParagraphSpacing(12.0);
    controller.setSelectedTextInset(8.0);
    if (!wait_until([&controller, revision_before_selection]() {
          return controller.previewDiagnostics().value(QStringLiteral("revision")).toInt() >
                 revision_before_selection;
        }, 3000)) {
      return fail("selected content edits should publish a debounced preview revision");
    }
    const auto diagnostics = controller.previewDiagnostics();
    const auto invalidated_boards =
        diagnostics.value(QStringLiteral("invalidatedBoardIndices")).toList();
    if (diagnostics.value(QStringLiteral("reason")).toString() != QStringLiteral("selected-content") ||
        diagnostics.value(QStringLiteral("boardsRendered")).toInt() != 1 ||
        invalidated_boards != QVariantList{0} ||
        diagnostics.value(QStringLiteral("decodeMs")).toDouble() < 0.0 ||
        diagnostics.value(QStringLiteral("layoutMs")).toDouble() < 0.0 ||
        diagnostics.value(QStringLiteral("renderMs")).toDouble() < 0.0 ||
        diagnostics.value(QStringLiteral("publishMs")).toDouble() < 0.0 ||
        diagnostics.value(QStringLiteral("requestRevision")).toULongLong() < 3 ||
        diagnostics.value(QStringLiteral("staleRequestsDropped")).toInt() < 2 ||
        diagnostics.value(QStringLiteral("cacheBytes")).toLongLong() >
            diagnostics.value(QStringLiteral("cacheBudgetBytes")).toLongLong()) {
      return fail("preview diagnostics should report only the affected board and all four timing stages");
    }
  }

  {
    CarouselDocumentController controller;
    controller.createCarouselWithSettings(QVariantMap{
        {QStringLiteral("carouselName"), QStringLiteral("Preflight Slots")},
        {QStringLiteral("documentFormat"), QStringLiteral("4x5")},
        {QStringLiteral("slideCount"), 1},
    });
    if (controller.preflightBlockingCount() != 0) {
      return fail("a background-only single board should have no blocking preflight issues");
    }

    controller.applyTemplateToCurrent(QStringLiteral("split-pair"));
    auto issues = controller.preflightIssues();
    if (preflight_issue_count(issues, QStringLiteral("empty-required-slot")) != 2 ||
        controller.preflightBlockingCount() != 2) {
      return fail("preflight should identify each empty required split slot as blocking");
    }

    QImage slot_image(QSize(320, 400), QImage::Format_ARGB32);
    slot_image.fill(QColor(QStringLiteral("#246bfe")));
    const auto left_path = write_image(dir, QStringLiteral("preflight-left.png"), slot_image);
    const auto right_path = write_image(dir, QStringLiteral("preflight-right.png"), slot_image);
    if (left_path.isEmpty() || right_path.isEmpty()) {
      return fail("preflight media fixtures should save");
    }
    controller.assignPrimaryImage(left_path, QStringLiteral("left"));
    controller.assignSecondaryImage(right_path, QStringLiteral("right"));
    if (controller.preflightBlockingCount() != 0) {
      return fail("filled split slots with available files should clear blocking preflight issues");
    }

    const auto configured_export_dir = dir.filePath(QStringLiteral("configured-export"));
    QDir().mkpath(configured_export_dir);
    controller.setExportDirectory(configured_export_dir);
    const auto summary = controller.exportSummary();
    if (summary.value(QStringLiteral("boardCount")).toInt() != 1 ||
        summary.value(QStringLiteral("boardOrder")).toString() != QStringLiteral("1") ||
        summary.value(QStringLiteral("ratio")).toString() != QStringLiteral("4:5") ||
        summary.value(QStringLiteral("pixelSize")).toString() != QStringLiteral("1080 × 1350") ||
        summary.value(QStringLiteral("fileTypes")).toString() != QStringLiteral("PNG") ||
        summary.value(QStringLiteral("colorSpace")).toString().isEmpty() ||
        summary.value(QStringLiteral("destination")).toString() != configured_export_dir ||
        summary.value(QStringLiteral("estimatedBytes")).toULongLong() == 0 ||
        summary.value(QStringLiteral("blockingIssueCount")).toInt() != 0) {
      return fail("configured export summary should resolve boards, order, canvas, files, color, destination, estimate, and preflight");
    }
    if (!controller.exportConfiguredCarousel() ||
        !QFileInfo::exists(QDir(configured_export_dir).filePath(QStringLiteral("carousel-01.png")))) {
      return fail("configured export should render to the destination selected by the export sheet model");
    }
    const QImage srgb_export(
        QDir(configured_export_dir).filePath(QStringLiteral("carousel-01.png")));
    if (srgb_export.isNull() || srgb_export.colorSpace() != QColorSpace(QColorSpace::SRgb)) {
      return fail("configured sRGB export should embed a verified sRGB profile");
    }

    controller.setExportColorSpace(QStringLiteral("Display P3"));
    if (controller.exportColorSpace() != QStringLiteral("Display P3") ||
        controller.exportSummary().value(QStringLiteral("colorSpace")).toString() !=
            QStringLiteral("Display P3")) {
      return fail("export color-space selection should be explicit in controller state and summary");
    }
    const auto display_p3_export_dir = dir.filePath(QStringLiteral("configured-display-p3-export"));
    QDir().mkpath(display_p3_export_dir);
    controller.setExportDirectory(display_p3_export_dir);
    if (!controller.exportConfiguredCarousel()) {
      return fail("configured Display P3 export should succeed for image boards");
    }
    const QImage display_p3_export(
        QDir(display_p3_export_dir).filePath(QStringLiteral("carousel-01.png")));
    if (display_p3_export.isNull() ||
        display_p3_export.colorSpace() != QColorSpace(QColorSpace::DisplayP3)) {
      return fail("configured Display P3 export should embed a verified Display P3 profile");
    }
    controller.undo();
    if (controller.exportColorSpace() != QStringLiteral("sRGB")) {
      return fail("export color-space selection should undo with the document");
    }
    controller.redo();
    if (controller.exportColorSpace() != QStringLiteral("Display P3") || !controller.saveDocument()) {
      return fail("export color-space selection should redo and save with the document");
    }
    CarouselDocumentController color_restored;
    if (!color_restored.loadDocument(controller.savedDocumentId()) ||
        color_restored.exportColorSpace() != QStringLiteral("Display P3")) {
      return fail("saved carousel documents should restore explicit export color space");
    }

    if (!QFile::remove(right_path)) {
      return fail("preflight missing-media fixture should be removable");
    }
    issues = controller.preflightIssues();
    const auto missing_issue = first_preflight_issue(issues, QStringLiteral("missing-media"));
    if (missing_issue.isEmpty() || !missing_issue.value(QStringLiteral("blocking")).toBool()) {
      return fail("preflight should block export when assigned slot media is missing");
    }
    const auto blocked_export_dir = dir.filePath(QStringLiteral("preflight-blocked-export"));
    QDir().mkpath(blocked_export_dir);
    if (controller.exportCarouselToDirectory(blocked_export_dir) ||
        !controller.status().startsWith(QStringLiteral("Export blocked:"))) {
      return fail("export should consume the same blocking preflight truth as the inspector");
    }
  }

  {
    CarouselDocumentController controller;
    controller.createCarouselWithSettings(QVariantMap{
        {QStringLiteral("carouselName"), QStringLiteral("Preflight Layers")},
        {QStringLiteral("documentFormat"), QStringLiteral("4x5")},
        {QStringLiteral("slideCount"), 2},
    });
    controller.addTextItem(QStringLiteral("A long line of text that cannot fit inside a tiny text frame"));
    const auto text_id = controller.selectedItemId();
    controller.setSelectedTextColor(QStringLiteral("#ffffff"));
    controller.addShapeItem(QStringLiteral("rectangle"));
    controller.addItemToSelection(text_id);
    controller.setSelectedItemSize(160.0, 36.0);
    controller.setSelectedItemCenter(1080.0, 180.0);
    controller.setSelectedItemHidden(true);

    auto issues = controller.preflightIssues();
    if (first_preflight_issue(issues, QStringLiteral("hidden-layer"), text_id).isEmpty() ||
        first_preflight_issue(issues, QStringLiteral("text-overflow"), text_id).isEmpty() ||
        first_preflight_issue(issues, QStringLiteral("unsafe-seam"), text_id).isEmpty() ||
        first_preflight_issue(issues, QStringLiteral("low-contrast-text"), text_id).isEmpty()) {
      return fail("preflight should flag hidden, clipped, seam-crossing, and low-contrast text layers");
    }

    const auto seam_issue = first_preflight_issue(issues, QStringLiteral("unsafe-seam"), text_id);
    controller.focusPreflightIssue(
        seam_issue.value(QStringLiteral("boardIndex")).toInt(),
        seam_issue.value(QStringLiteral("itemId")).toString());
    if (controller.selectedItemId() != text_id) {
      return fail("preflight issue navigation should focus the affected layer");
    }

    controller.setSelectedItemHidden(false);
    controller.setSelectedItemCenter(-120.0, -120.0);
    issues = controller.preflightIssues();
    if (first_preflight_issue(issues, QStringLiteral("off-canvas-layer"), text_id).isEmpty()) {
      return fail("preflight should flag layers extending outside the export canvas");
    }
  }

  {
    CarouselDocumentController controller;
    controller.createCarouselWithSettings(QVariantMap{
        {QStringLiteral("carouselName"), QStringLiteral("Actionable Text Preflight")},
        {QStringLiteral("documentFormat"), QStringLiteral("4x5")},
        {QStringLiteral("slideCount"), 1},
    });
    controller.addTextItem(QStringLiteral(
        "This intentionally long text must exercise exact overflow measurement and direct resolution actions."));
    const auto item_id = controller.selectedItemId();
    controller.setSelectedTextFrameSize(160.0, 36.0);
    auto overflow = first_preflight_issue(
        controller.preflightIssues(), QStringLiteral("text-overflow"), item_id);
    if (overflow.isEmpty() || overflow.value(QStringLiteral("actions")).toList() != QVariantList{
            QStringLiteral("inspect"),
            QStringLiteral("resize-frame"),
            QStringLiteral("fit-text"),
            QStringLiteral("ignore"),
        }) {
      return fail("text overflow preflight should identify the exact layer and expose all four direct actions");
    }
    controller.selectSlide(0);
    if (!controller.resolveTextPreflightIssue(
            item_id, QStringLiteral("text-overflow"), QStringLiteral("inspect")) ||
        controller.selectedItemId() != item_id) {
      return fail("Inspect should focus the exact affected text layer");
    }
    const auto original_height = controller.canvasItems().constFirst().toMap().value(
        QStringLiteral("height")).toDouble();
    if (!controller.resolveTextPreflightIssue(
            item_id, QStringLiteral("text-overflow"), QStringLiteral("resize-frame")) ||
        !first_preflight_issue(
             controller.preflightIssues(), QStringLiteral("text-overflow"), item_id).isEmpty() ||
        controller.canvasItems().constFirst().toMap().value(QStringLiteral("height")).toDouble() <=
            original_height) {
      return fail("Resize Frame should expand the exact text box until measured content fits");
    }
    controller.undo();
    if (first_preflight_issue(
            controller.preflightIssues(), QStringLiteral("text-overflow"), item_id).isEmpty()) {
      return fail("text overflow frame repair should undo as one edit");
    }

    controller.setSelectedTextFrameSize(420.0, 100.0);
    const auto before_fit_size = controller.selectionDetails().value(
        QStringLiteral("textPointSize")).toDouble();
    if (first_preflight_issue(
            controller.preflightIssues(), QStringLiteral("text-overflow"), item_id).isEmpty() ||
        !controller.resolveTextPreflightIssue(
            item_id, QStringLiteral("text-overflow"), QStringLiteral("fit-text")) ||
        !first_preflight_issue(
             controller.preflightIssues(), QStringLiteral("text-overflow"), item_id).isEmpty() ||
        controller.selectionDetails().value(QStringLiteral("textPointSize")).toDouble() >=
            before_fit_size) {
      return fail("Fit Text should choose the largest fitting size and clear overflow without resizing the frame");
    }

    controller.setSelectedTextPointSize(12.0);
    auto too_small = first_preflight_issue(
        controller.preflightIssues(), QStringLiteral("text-too-small"), item_id);
    if (too_small.isEmpty() ||
        !too_small.value(QStringLiteral("message")).toString().contains(QStringLiteral("18 pt")) ||
        !controller.resolveTextPreflightIssue(
            item_id, QStringLiteral("text-too-small"), QStringLiteral("resize-frame")) ||
        !first_preflight_issue(
             controller.preflightIssues(), QStringLiteral("text-too-small"), item_id).isEmpty() ||
        controller.selectionDetails().value(QStringLiteral("textPointSize")).toDouble() < 18.0) {
      return fail("minimum-readable-size preflight should report its threshold and resize to a readable fitting frame");
    }
    controller.undo();
    if (first_preflight_issue(
            controller.preflightIssues(), QStringLiteral("text-too-small"), item_id).isEmpty() ||
        !controller.resolveTextPreflightIssue(
            item_id, QStringLiteral("text-too-small"), QStringLiteral("ignore")) ||
        !first_preflight_issue(
             controller.preflightIssues(), QStringLiteral("text-too-small"), item_id).isEmpty()) {
      return fail("Ignore should explicitly suppress only the selected layer's readable-size exception");
    }
    if (!controller.saveDocument()) {
      return fail("text preflight ignore should save with the document");
    }
    CarouselDocumentController restored;
    if (!restored.loadDocument(controller.savedDocumentId()) ||
        !first_preflight_issue(
             restored.preflightIssues(), QStringLiteral("text-too-small"), item_id).isEmpty() ||
        !restored.canvasItems().constFirst().toMap().value(
             QStringLiteral("ignoredPreflightCodes")).toStringList().contains(
                 QStringLiteral("text-too-small"))) {
      return fail("explicit text preflight ignores should survive save and reopen without hiding their state");
    }
  }

  {
    const auto reading_order_media_path = write_image(
        dir,
        QStringLiteral("reading-order-media.png"),
        split_image(
            QSize(320, 400),
            QColor(QStringLiteral("#243c5a")),
            QColor(QStringLiteral("#d6a76d"))));
    if (reading_order_media_path.isEmpty()) {
      return fail("reading-order proof should create its media fixture");
    }

    CarouselDocumentController controller;
    controller.createCarouselWithSettings(QVariantMap{
        {QStringLiteral("carouselName"), QStringLiteral("Reading Order Proof")},
        {QStringLiteral("documentFormat"), QStringLiteral("4x5")},
        {QStringLiteral("slideCount"), 1},
    });
    controller.assignPrimaryImage(
        reading_order_media_path,
        QStringLiteral("Editorial portrait"));
    controller.addTextItem(QStringLiteral("Lead with the evidence"));
    const auto lead_text_id = controller.selectedItemId();
    controller.addTextItem(QStringLiteral("Then explain the consequence"));
    const auto consequence_text_id = controller.selectedItemId();
    controller.addTextItem(QStringLiteral("Hidden production note"));
    controller.setSelectedItemHidden(true);

    auto state = controller.readingOrderState();
    auto entries = state.value(QStringLiteral("entries")).toList();
    if (entries.size() != 3 ||
        state.value(QStringLiteral("meaningfulEntryCount")).toInt() != 3) {
      return fail("reading order should include visible text and imagery while excluding hidden text");
    }
    const auto key_for_item = [&entries](const QString& item_id) {
      for (const auto& value : entries) {
        const auto entry = value.toMap();
        if (entry.value(QStringLiteral("itemId")).toString() == item_id) {
          return entry.value(QStringLiteral("key")).toString();
        }
      }
      return QString{};
    };
    const auto lead_key = key_for_item(lead_text_id);
    const auto consequence_key = key_for_item(consequence_text_id);
    if (lead_key.isEmpty() || consequence_key.isEmpty() ||
        entries.constFirst().toMap().value(QStringLiteral("key")).toString() !=
            QStringLiteral("slot:primary")) {
      return fail("reading-order fallback should identify stable slot and layer entries");
    }

    int lead_position = -1;
    for (const auto& value : entries) {
      const auto entry = value.toMap();
      if (entry.value(QStringLiteral("key")).toString() == lead_key) {
        lead_position = entry.value(QStringLiteral("position")).toInt();
      }
    }
    if (lead_position <= 1 || !controller.moveReadingOrderEntry(lead_key, -1)) {
      return fail("reading-order editor should move a visible text entry earlier");
    }
    controller.undo();
    if (controller.readingOrderState().value(QStringLiteral("entries")).toList()
            .constFirst().toMap().value(QStringLiteral("key")).toString() !=
        QStringLiteral("slot:primary")) {
      return fail("reading-order moves should undo as document edits");
    }
    controller.redo();
    state = controller.readingOrderState();
    entries = state.value(QStringLiteral("entries")).toList();
    if (entries.at(lead_position - 2).toMap().value(QStringLiteral("key")).toString() !=
        lead_key) {
      return fail("reading-order moves should redo deterministically");
    }

    while (controller.readingOrderState().value(QStringLiteral("entries")).toList()
               .constFirst().toMap().value(QStringLiteral("key")).toString() != lead_key) {
      if (!controller.moveReadingOrderEntry(lead_key, -1)) {
        return fail("reading-order editor should move text to the first narrative position");
      }
    }
    if (!controller.setReadingOrderEntryMeaningful(
            QStringLiteral("slot:primary"), false) ||
        controller.readingOrderState().value(QStringLiteral("meaningfulEntryCount")).toInt() != 2) {
      return fail("imagery should be explicitly markable as decorative");
    }
    controller.undo();
    if (controller.readingOrderState().value(QStringLiteral("meaningfulEntryCount")).toInt() != 3) {
      return fail("decorative imagery changes should undo");
    }
    controller.redo();
    if (controller.readingOrderState().value(QStringLiteral("meaningfulEntryCount")).toInt() != 2 ||
        !controller.focusReadingOrderEntry(lead_key) ||
        controller.selectedItemId() != lead_text_id ||
        !controller.focusReadingOrderEntry(QStringLiteral("slot:primary")) ||
        !controller.selectedItemId().isEmpty()) {
      return fail("reading-order entries should expose exact canvas focus and reversible meaningful state");
    }

    if (!controller.saveDocument()) {
      return fail("reading-order state should save with the carousel document");
    }
    CarouselDocumentController restored;
    if (!restored.loadDocument(controller.savedDocumentId())) {
      return fail("reading-order state should reopen with the carousel document");
    }
    const auto restored_state = restored.readingOrderState();
    const auto restored_entries = restored_state.value(QStringLiteral("entries")).toList();
    if (restored_entries.size() != 3 ||
        restored_entries.constFirst().toMap().value(QStringLiteral("key")).toString() != lead_key ||
        restored_state.value(QStringLiteral("meaningfulEntryCount")).toInt() != 2) {
      return fail("saved reading order and decorative imagery state should restore exactly");
    }

    const auto export_dir = dir.filePath(QStringLiteral("reading-order-export"));
    QDir().mkpath(export_dir);
    restored.setExportDirectory(export_dir);
    if (!restored.exportConfiguredCarousel()) {
      std::cerr << restored.status().toStdString() << '\n';
      return fail("reading-order manifest proof should export the configured carousel");
    }
    const auto manifests = QDir(export_dir).entryList(
        QStringList{QStringLiteral("*.json")}, QDir::Files);
    if (manifests.size() != 1) {
      return fail("reading-order manifest proof should write one delivery manifest");
    }
    const auto manifest = read_json(QDir(export_dir).filePath(manifests.constFirst()));
    const auto outputs = manifest.value(QStringLiteral("outputs")).toArray();
    const auto exported_order = outputs.size() == 1
                                    ? outputs.at(0).toObject().value(
                                          QStringLiteral("readingOrder")).toArray()
                                    : QJsonArray{};
    if (manifest.value(QStringLiteral("manifestVersion")).toInt() != 2 ||
        manifest.value(QStringLiteral("documentSchemaVersion")).toInt() != 11 ||
        outputs.size() != 1 || exported_order.size() != 2 ||
        exported_order.at(0).toObject().value(QStringLiteral("key")).toString() != lead_key ||
        exported_order.at(0).toObject().value(QStringLiteral("text")).toString() !=
            QStringLiteral("Lead with the evidence")) {
      return fail("delivery manifest v2 should preserve ordered visible text and omit decorative imagery");
    }
    for (const auto& value : exported_order) {
      if (value.toObject().value(QStringLiteral("key")).toString() == QStringLiteral("slot:primary")) {
        return fail("decorative imagery should never enter the exported accessibility reading order");
      }
    }
  }

  {
    CarouselDocumentController controller;
    controller.createCarouselWithSettings(QVariantMap{
        {QStringLiteral("carouselName"), QStringLiteral("Preflight Video")},
        {QStringLiteral("documentFormat"), QStringLiteral("4x5")},
        {QStringLiteral("slideCount"), 1},
    });
    const auto ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
    const auto video_path = dir.filePath(QStringLiteral("preflight-video.mp4"));
    QProcess video_fixture_process;
    video_fixture_process.start(
        ffmpeg,
        {QStringLiteral("-y"), QStringLiteral("-f"), QStringLiteral("lavfi"),
         QStringLiteral("-i"), QStringLiteral("color=c=blue:s=320x400:r=24"),
         QStringLiteral("-t"), QStringLiteral("0.5"), QStringLiteral("-c:v"),
         QStringLiteral("libx264"), QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"), video_path});
    if (ffmpeg.isEmpty() || !video_fixture_process.waitForStarted(5000) ||
        !video_fixture_process.waitForFinished(30000) || video_fixture_process.exitCode() != 0) {
      return fail("video overlay proof requires ffmpeg and a deterministic source fixture");
    }
    controller.assignPrimaryVideo(video_path, QStringLiteral("video"));
    controller.addShapeItem(QStringLiteral("rectangle"));
    controller.setSelectedShapeFillColor(QStringLiteral("#ff0000"));
    controller.setSelectedItemSize(240.0, 240.0);
    controller.addTextItem(QStringLiteral("VIDEO OVERLAY"));
    controller.setSelectedTextColor(QStringLiteral("#ffffff"));
    const auto issue = first_preflight_issue(
        controller.preflightIssues(), QStringLiteral("unsupported-video-composition"));
    if (!issue.isEmpty()) {
      return fail("supported video overlay compositions should not be blocked by preflight");
    }
    const auto video_overlay_export_dir = dir.filePath(QStringLiteral("video-overlay-export"));
    QDir().mkpath(video_overlay_export_dir);
    if (!controller.exportCarouselToDirectory(video_overlay_export_dir)) {
      return fail("video boards with text and shape overlays should export as composited video");
    }
    const auto extracted_frame_path = dir.filePath(QStringLiteral("video-overlay-frame.png"));
    QProcess frame_extract_process;
    frame_extract_process.start(
        ffmpeg,
        {QStringLiteral("-y"), QStringLiteral("-i"),
         QDir(video_overlay_export_dir).filePath(QStringLiteral("carousel-01.mp4")),
         QStringLiteral("-frames:v"), QStringLiteral("1"), extracted_frame_path});
    if (!frame_extract_process.waitForStarted(5000) || !frame_extract_process.waitForFinished(30000) ||
        frame_extract_process.exitCode() != 0) {
      return fail("composited video proof should extract its first frame");
    }
    const QImage composited_frame(extracted_frame_path);
    const auto center_color = composited_frame.pixelColor(
        composited_frame.width() / 2, composited_frame.height() / 2);
    if (composited_frame.isNull() || center_color.red() < 180 || center_color.blue() > 100) {
      return fail("video overlay export should render the red shape over the blue source frames");
    }
  }

  {
    CarouselDocumentController controller;
    controller.createCarouselWithSettings(QVariantMap{
        {QStringLiteral("carouselName"), QStringLiteral("Naming Proof")},
        {QStringLiteral("documentFormat"), QStringLiteral("1x1")},
        {QStringLiteral("slideCount"), 2},
    });
    controller.selectSlide(0);
    controller.setCurrentSlideRole(QStringLiteral("cover"));
    controller.setCurrentSlideAccessibilityText(
        QStringLiteral("A square cover board with the campaign title."),
        QStringLiteral("The cover introduces the naming proof carousel."));
    controller.selectSlide(1);
    controller.setCurrentSlideRole(QStringLiteral("cta"));
    controller.setCurrentSlideAccessibilityText(
        QStringLiteral("A square closing board with a direct action."),
        QStringLiteral("The final board closes the sequence with its call to action."));
    controller.setExportFilenamePattern(
        QStringLiteral("{document}-{board_number}-{board_role}-{ratio}-{version}-{date}"));
    controller.setExportVersion(7);
    const auto naming_dir = dir.filePath(QStringLiteral("tokenized-naming-export"));
    QDir().mkpath(naming_dir);
    controller.setExportDirectory(naming_dir);

    auto preview = controller.exportOutputPreview();
    if (preview.size() != 3 ||
        !preview.at(0).toMap().value(QStringLiteral("resolvedName")).toString().startsWith(
            QStringLiteral("Naming Proof-01-cover-1x1-v7-")) ||
        !preview.at(1).toMap().value(QStringLiteral("resolvedName")).toString().contains(
            QStringLiteral("-02-cta-1x1-v7-"))) {
      return fail("filename tokens should resolve document, board number, role, ratio, version, and date live");
    }

    const auto first_output_path = preview.at(0).toMap().value(QStringLiteral("outputPath")).toString();
    QFile existing_output(first_output_path);
    if (!existing_output.open(QIODevice::WriteOnly) || existing_output.write("existing") <= 0) {
      return fail("collision policy fixture should create an existing output");
    }
    existing_output.close();

    preview = controller.exportOutputPreview();
    if (!preview.at(0).toMap().value(QStringLiteral("collision")).toBool() ||
        preview.at(0).toMap().value(QStringLiteral("action")).toString() != QStringLiteral("rename") ||
        preview.at(0).toMap().value(QStringLiteral("resolvedName")).toString() ==
            preview.at(0).toMap().value(QStringLiteral("proposedName")).toString()) {
      return fail("rename collision policy should preview a unique resolved output name");
    }
    controller.setExportCollisionPolicy(QStringLiteral("skip"));
    if (controller.exportOutputPreview().at(0).toMap().value(QStringLiteral("action")).toString() !=
        QStringLiteral("skip")) {
      return fail("skip collision policy should be explicit in the live output preview");
    }
    controller.setExportCollisionPolicy(QStringLiteral("overwrite"));
    if (controller.exportOutputPreview().at(0).toMap().value(QStringLiteral("action")).toString() !=
        QStringLiteral("overwrite")) {
      return fail("overwrite collision policy should be explicit in the live output preview");
    }

    controller.setExportCollisionPolicy(QStringLiteral("rename"));
    if (!controller.exportConfiguredCarousel()) {
      return fail("rename collision policy should export all boards without overwriting existing files");
    }
    preview = controller.exportOutputPreview();
    if (!QFileInfo::exists(QDir(naming_dir).filePath(
            preview.at(0).toMap().value(QStringLiteral("proposedName")).toString())) ||
        QDir(naming_dir).entryList(QStringList{QStringLiteral("*.png")}, QDir::Files).size() < 3) {
      return fail("tokenized export should preserve the colliding original and write uniquely renamed outputs");
    }

    const auto manifest_files = QDir(naming_dir).entryList(
        QStringList{QStringLiteral("*.json")}, QDir::Files);
    if (manifest_files.size() != 1) {
      return fail("enabled delivery manifest should write exactly one JSON manifest");
    }
    QFile manifest_file(QDir(naming_dir).filePath(manifest_files.constFirst()));
    if (!manifest_file.open(QIODevice::ReadOnly)) {
      return fail("delivery manifest should be readable after export");
    }
    const auto manifest = QJsonDocument::fromJson(manifest_file.readAll()).object();
    const auto manifest_outputs = manifest.value(QStringLiteral("outputs")).toArray();
    if (manifest.value(QStringLiteral("manifestVersion")).toInt() != 2 ||
        manifest.value(QStringLiteral("documentId")).toString().isEmpty() ||
        manifest.value(QStringLiteral("exportVersion")).toInt() != 7 ||
        manifest.value(QStringLiteral("exportedAt")).toString().isEmpty() ||
        manifest_outputs.size() != 2 ||
        manifest_outputs.at(0).toObject().value(QStringLiteral("boardRole")).toString() !=
            QStringLiteral("cover") ||
        manifest_outputs.at(0).toObject().value(QStringLiteral("altText")).toString().isEmpty() ||
        manifest_outputs.at(1).toObject().value(QStringLiteral("plainLanguageDescription")).toString().isEmpty()) {
      return fail("delivery manifest should contain identity, version, timestamp, ordered outputs, roles, and accessibility metadata");
    }
    for (const auto& value : manifest_outputs) {
      const auto output = value.toObject();
      QFile delivered_file(QDir(naming_dir).filePath(output.value(QStringLiteral("fileName")).toString()));
      if (!delivered_file.open(QIODevice::ReadOnly)) {
        return fail("every delivery manifest output should reference a readable file");
      }
      QCryptographicHash hash(QCryptographicHash::Sha256);
      if (!hash.addData(&delivered_file) ||
          QString::fromLatin1(hash.result().toHex()) != output.value(QStringLiteral("sha256")).toString() ||
          output.value(QStringLiteral("width")).toInt() != 1080 ||
          output.value(QStringLiteral("height")).toInt() != 1080 ||
          output.value(QStringLiteral("colorProfile")).toString() != QStringLiteral("sRGB")) {
        return fail("delivery manifest should verify output hashes, dimensions, and color profiles");
      }
    }

    controller.setExportManifestEnabled(false);
    const auto no_manifest_dir = dir.filePath(QStringLiteral("no-manifest-export"));
    QDir().mkpath(no_manifest_dir);
    controller.setExportDirectory(no_manifest_dir);
    if (controller.exportOutputPreview().size() != 2 || !controller.exportConfiguredCarousel() ||
        !QDir(no_manifest_dir).entryList(QStringList{QStringLiteral("*.json")}, QDir::Files).isEmpty()) {
      return fail("delivery manifest should be optional and omitted when disabled");
    }
    controller.setExportManifestEnabled(true);

    controller.setExportFilenamePattern(QStringLiteral("{board_role}"));
    controller.selectSlide(1);
    controller.setCurrentSlideRole(QStringLiteral("cover"));
    if (first_preflight_issue(
            controller.preflightIssues(), QStringLiteral("duplicate-output-name")).isEmpty()) {
      return fail("preflight should flag duplicate names produced by the active filename pattern");
    }
    controller.setCurrentSlideRole(QStringLiteral("cta"));
    controller.setExportFilenamePattern(
        QStringLiteral("{document}-{board_number}-{board_role}-{ratio}-{version}-{date}"));
    if (!controller.saveDocument()) {
      return fail("tokenized export settings should save with the carousel document");
    }
    CarouselDocumentController restored;
    if (!restored.loadDocument(controller.savedDocumentId()) ||
        restored.exportFilenamePattern() != controller.exportFilenamePattern() ||
        restored.exportVersion() != 7 ||
        restored.exportCollisionPolicy() != QStringLiteral("rename")) {
      return fail("saved documents should restore filename pattern, version, and collision policy");
    }
  }

  {
    CarouselDocumentController controller;
    controller.createCarouselWithSettings(QVariantMap{
        {QStringLiteral("carouselName"), QStringLiteral("Image Format Proof")},
        {QStringLiteral("documentFormat"), QStringLiteral("4x5")},
        {QStringLiteral("slideCount"), 1},
    });
    controller.setExportManifestEnabled(false);
    controller.setExportImageQuality(67);
    controller.setExportPreserveTransparency(false);

    const std::array<QString, 3> formats = {
        QStringLiteral("png"), QStringLiteral("jpeg"), QStringLiteral("webp")};
    for (const auto& format : formats) {
      controller.setExportImageFormat(format);
      const auto format_dir = dir.filePath(QStringLiteral("format-%1").arg(format));
      QDir().mkpath(format_dir);
      controller.setExportDirectory(format_dir);
      const auto summary = controller.exportSummary();
      if (summary.value(QStringLiteral("imageFormat")).toString() != format ||
          summary.value(QStringLiteral("imageQuality")).toInt() != 67 ||
          summary.value(QStringLiteral("preserveTransparency")).toBool() ||
          summary.value(QStringLiteral("formatWarning")).toString().isEmpty()) {
        return fail("image export summary should expose format, quality, transparency, and compatibility guidance");
      }
      const auto output_preview = controller.exportOutputPreview();
      if (output_preview.size() != 1 || !controller.exportConfiguredCarousel()) {
        return fail("PNG, JPEG, and WebP image formats should each export through the configured output plan");
      }
      const auto output_path = output_preview.constFirst().toMap().value(QStringLiteral("outputPath")).toString();
      QImageReader reader(output_path);
      const auto detected_format = reader.format().toLower();
      const auto output = reader.read();
      const bool format_matches =
          format == QStringLiteral("jpeg")
              ? detected_format == QByteArrayLiteral("jpg") || detected_format == QByteArrayLiteral("jpeg")
              : detected_format == format.toLatin1();
      if (output.isNull() || output.size() != QSize(1080, 1350) || !format_matches ||
          output.colorSpace() != QColorSpace(QColorSpace::SRgb)) {
        return fail("each image format should reopen with verified pixels, format, and embedded sRGB profile");
      }
    }

    controller.undo();
    if (controller.exportImageFormat() != QStringLiteral("jpeg")) {
      return fail("image format selection should be undoable");
    }
    controller.redo();
    if (controller.exportImageFormat() != QStringLiteral("webp") || !controller.saveDocument()) {
      return fail("image format export settings should save with the document");
    }
    CarouselDocumentController restored;
    if (!restored.loadDocument(controller.savedDocumentId()) ||
        restored.exportImageFormat() != QStringLiteral("webp") ||
        restored.exportImageQuality() != 67 || restored.exportPreserveTransparency()) {
      return fail("saved documents should restore image format, quality, and transparency settings");
    }
  }

  {
    CarouselDocumentController incompatible;
    incompatible.createCarouselWithSettings(QVariantMap{
        {QStringLiteral("carouselName"), QStringLiteral("Incompatible Preset Proof")},
        {QStringLiteral("documentFormat"), QStringLiteral("4x5")},
        {QStringLiteral("slideCount"), 1},
    });
    const auto catalog = incompatible.exportPresets();
    QSet<QString> preset_ids;
    int platform_preset_count = 0;
    int workflow_preset_count = 0;
    for (const auto& value : catalog) {
      const auto preset = value.toMap();
      const auto preset_id = preset.value(QStringLiteral("presetId")).toString();
      if (preset_id.isEmpty() || preset_ids.contains(preset_id) ||
          preset.value(QStringLiteral("presetVersion")).toInt() != 1 ||
          preset.value(QStringLiteral("settings")).toMap().size() != 14 ||
          preset.value(QStringLiteral("guidanceLastReviewed")).toString() !=
              QStringLiteral("2026-07-21") ||
          preset.value(QStringLiteral("guidanceSummary")).toString().isEmpty() ||
          preset.value(QStringLiteral("appChosenDefaults")).toString().isEmpty()) {
        return fail("every built-in export preset should have a unique stable id, version, complete settings, dated guidance, and explicit app defaults");
      }
      preset_ids.insert(preset_id);
      if (preset.value(QStringLiteral("timeSensitive")).toBool()) {
        ++platform_preset_count;
        if (preset.value(QStringLiteral("guidanceSourceUrl")).toString().isEmpty() ||
            preset.value(QStringLiteral("guidanceSourceUpdated")).toString().isEmpty() ||
            preset.value(QStringLiteral("guidanceReviewDue")).toString().isEmpty()) {
          return fail("time-sensitive platform presets should expose a first-party source, source update, and next review date");
        }
      } else {
        ++workflow_preset_count;
      }
    }
    if (catalog.size() != 5 || preset_ids.size() != 5 || platform_preset_count != 3 ||
        workflow_preset_count != 2) {
      return fail("the export catalog should expose three dated platform targets and two versioned workflow targets");
    }

    const auto before_rejected_apply = incompatible.exportPresetState();
    if (incompatible.applyExportPreset(QStringLiteral("tiktok-carousel-square"), false) ||
        incompatible.exportPresetState().value(QStringLiteral("hasActivePreset")).toBool() ||
        incompatible.exportPresetState().value(QStringLiteral("controlledSettings")).toMap() !=
            before_rejected_apply.value(QStringLiteral("controlledSettings")).toMap() ||
        !incompatible.status().contains(QStringLiteral("Requires a 1:1 document"))) {
      return fail("an incompatible delivery preset should reject atomically and never stretch the document");
    }

    CarouselDocumentController controller;
    controller.createCarouselWithSettings(QVariantMap{
        {QStringLiteral("carouselName"), QStringLiteral("Versioned Preset Proof")},
        {QStringLiteral("documentFormat"), QStringLiteral("1x1")},
        {QStringLiteral("slideCount"), 2},
    });
    if (!controller.applyExportPreset(QStringLiteral("tiktok-carousel-square"), false)) {
      return fail("a compatible TikTok square preset should apply");
    }
    auto state = controller.exportPresetState();
    auto summary = controller.exportSummary();
    if (!state.value(QStringLiteral("hasActivePreset")).toBool() ||
        state.value(QStringLiteral("activePresetId")).toString() !=
            QStringLiteral("tiktok-carousel-square") ||
        state.value(QStringLiteral("activePresetVersion")).toInt() != 1 ||
        state.value(QStringLiteral("overrideCount")).toInt() != 0 ||
        state.value(QStringLiteral("resolvedRows")).toList().size() != 16 ||
        controller.exportSizeSettings().value(QStringLiteral("mode")).toString() !=
            QStringLiteral("custom") ||
        controller.exportSizeSettings().value(QStringLiteral("width")).toInt() != 640 ||
        controller.exportSizeSettings().value(QStringLiteral("height")).toInt() != 640 ||
        controller.exportImageFormat() != QStringLiteral("jpeg") ||
        controller.exportImageQuality() != 80 || controller.exportPreserveTransparency() ||
        controller.exportColorSpace() != QStringLiteral("sRGB") ||
        controller.exportFilenamePattern() != QStringLiteral("tiktok-{board_number}") ||
        !controller.exportManifestEnabled() || controller.exportProofSheetEnabled() ||
        summary.value(QStringLiteral("pixelSize")).toString() != QStringLiteral("640 × 640") ||
        summary.value(QStringLiteral("presetId")).toString() !=
            QStringLiteral("tiktok-carousel-square") ||
        summary.value(QStringLiteral("presetVersion")).toInt() != 1) {
      return fail("applying a preset should expose its exact versioned baseline through every resolved export setting");
    }

    controller.undo();
    if (controller.exportPresetState().value(QStringLiteral("hasActivePreset")).toBool() ||
        controller.exportImageFormat() != QStringLiteral("png") ||
        controller.exportSizeSettings().value(QStringLiteral("width")).toInt() != 1080) {
      return fail("one undo should restore the complete pre-preset export state");
    }
    controller.redo();
    if (controller.exportPresetState().value(QStringLiteral("activePresetId")).toString() !=
            QStringLiteral("tiktok-carousel-square") ||
        controller.exportImageQuality() != 80) {
      return fail("one redo should restore the complete versioned preset state");
    }

    controller.setExportImageQuality(73);
    controller.setExportColorSpace(QStringLiteral("Display P3"));
    controller.setExportProofSheetEnabled(true);
    state = controller.exportPresetState();
    QSet<QString> override_keys;
    for (const auto& value : state.value(QStringLiteral("overrides")).toList()) {
      override_keys.insert(value.toMap().value(QStringLiteral("key")).toString());
    }
    if (state.value(QStringLiteral("overrideCount")).toInt() != 3 ||
        override_keys != QSet<QString>{QStringLiteral("imageQuality"),
                                      QStringLiteral("colorSpace"),
                                      QStringLiteral("proofSheetEnabled")}) {
      return fail("preset state should identify every user override by exact controlled setting");
    }

    if (!controller.applyExportPreset(QStringLiteral("youtube-community-square"), true)) {
      return fail("a second compatible preset should apply while preserving explicit overrides");
    }
    state = controller.exportPresetState();
    if (state.value(QStringLiteral("activePresetId")).toString() !=
            QStringLiteral("youtube-community-square") ||
        state.value(QStringLiteral("overrideCount")).toInt() != 3 ||
        controller.exportSizeSettings().value(QStringLiteral("width")).toInt() != 1080 ||
        controller.exportSizeSettings().value(QStringLiteral("height")).toInt() != 1080 ||
        controller.exportFilenamePattern() != QStringLiteral("youtube-{board_number}") ||
        controller.exportImageQuality() != 73 ||
        controller.exportColorSpace() != QStringLiteral("Display P3") ||
        !controller.exportProofSheetEnabled()) {
      return fail("preset switching should apply the new baseline while preserving only the user's prior overrides");
    }

    if (!controller.resetExportPresetOverrides() ||
        controller.exportPresetState().value(QStringLiteral("overrideCount")).toInt() != 0 ||
        controller.exportImageQuality() != 92 ||
        controller.exportColorSpace() != QStringLiteral("sRGB") ||
        controller.exportProofSheetEnabled()) {
      return fail("reset overrides should restore the exact active preset baseline in one operation");
    }
    controller.undo();
    if (controller.exportPresetState().value(QStringLiteral("overrideCount")).toInt() != 3 ||
        controller.exportImageQuality() != 73 ||
        controller.exportColorSpace() != QStringLiteral("Display P3") ||
        !controller.exportProofSheetEnabled()) {
      return fail("resetting preset overrides should undo atomically");
    }
    controller.redo();
    controller.setExportImageQuality(89);
    if (!controller.saveDocument(QStringLiteral("Versioned Preset Proof"))) {
      return fail("versioned preset state should save with the carousel document");
    }
    CarouselDocumentController restored;
    if (!restored.loadDocument(controller.savedDocumentId())) {
      return fail("a document with active export preset state should reopen");
    }
    state = restored.exportPresetState();
    if (state.value(QStringLiteral("activePresetId")).toString() !=
            QStringLiteral("youtube-community-square") ||
        state.value(QStringLiteral("activePresetVersion")).toInt() != 1 ||
        state.value(QStringLiteral("overrideCount")).toInt() != 1 ||
        state.value(QStringLiteral("guidanceLastReviewed")).toString() !=
            QStringLiteral("2026-07-21") ||
        state.value(QStringLiteral("guidanceSourceUrl")).toString().isEmpty() ||
        restored.exportImageQuality() != 89 ||
        restored.exportSizeSettings().value(QStringLiteral("exportResolution")).toString() !=
            QStringLiteral("1080 × 1080")) {
      return fail("save and reopen should preserve preset identity, version, baseline, overrides, guidance, and resolved output");
    }

    const auto preset_export_dir = dir.filePath(QStringLiteral("versioned-preset-export"));
    QDir().mkpath(preset_export_dir);
    restored.setExportDirectory(preset_export_dir);
    if (!restored.exportConfiguredCarousel()) {
      return fail("the reopened preset configuration should drive a real export");
    }
    const auto preset_outputs = QDir(preset_export_dir).entryList(
        {QStringLiteral("*.jpg"), QStringLiteral("*.jpeg")}, QDir::Files, QDir::Name);
    const auto preset_manifests = QDir(preset_export_dir).entryList(
        {QStringLiteral("*.json")}, QDir::Files, QDir::Name);
    if (preset_outputs.size() != 2 || preset_manifests.size() != 1) {
      return fail("the YouTube preset should export two JPEG boards and one separate manifest");
    }
    for (const auto& file_name : preset_outputs) {
      const QImage output(QDir(preset_export_dir).filePath(file_name));
      if (output.isNull() || output.size() != QSize(1080, 1080) ||
          output.colorSpace() != QColorSpace(QColorSpace::SRgb)) {
        return fail("preset output pixels should match the complete resolved dimensions and color profile");
      }
    }
    const auto manifest = read_json(
        QDir(preset_export_dir).filePath(preset_manifests.constFirst()));
    const auto manifest_preset = manifest.value(QStringLiteral("exportPreset")).toObject();
    if (manifest.value(QStringLiteral("documentSchemaVersion")).toInt() != 11 ||
        manifest_preset.value(QStringLiteral("presetId")).toString() !=
            QStringLiteral("youtube-community-square") ||
        manifest_preset.value(QStringLiteral("presetVersion")).toInt() != 1 ||
        manifest_preset.value(QStringLiteral("overrideCount")).toInt() != 1) {
      return fail("the delivery manifest should prove document schema, preset identity, preset version, and override count");
    }
  }

  {
    QImage selected_media(QSize(600, 800), QImage::Format_ARGB32);
    selected_media.fill(QColor(QStringLiteral("#ff7a45")));
    const auto selected_media_path = write_image(
        dir, QStringLiteral("new-carousel-selected-media.png"), selected_media);
    if (selected_media_path.isEmpty()) {
      return fail("new carousel selected-media fixture should save");
    }

    CarouselDocumentController controller;
    controller.createCarouselWithSettings(QVariantMap{
        {QStringLiteral("carouselName"), QStringLiteral("Exact New Carousel")},
        {QStringLiteral("documentFormat"), QStringLiteral("custom")},
        {QStringLiteral("customPixelWidth"), 1234},
        {QStringLiteral("customPixelHeight"), 987},
        {QStringLiteral("slideCount"), 3},
        {QStringLiteral("colorSpace"), QStringLiteral("Display P3")},
        {QStringLiteral("backgroundColor"), QStringLiteral("#112233")},
        {QStringLiteral("templateKind"), QStringLiteral("split-pair")},
        {QStringLiteral("primaryMediaPath"), selected_media_path},
        {QStringLiteral("primaryMediaLabel"), QStringLiteral("Selected source")},
    });
    const auto slides = controller.slides();
    if (controller.documentFormat() != QStringLiteral("custom") ||
        controller.documentPixelWidth() != 3702 || controller.documentPixelHeight() != 987 ||
        controller.slideCount() != 3 || controller.exportColorSpace() != QStringLiteral("Display P3") ||
        slides.size() != 3 ||
        slides.at(0).toMap().value(QStringLiteral("backgroundColor")).toString() != QStringLiteral("#112233") ||
        slides.at(2).toMap().value(QStringLiteral("templateKind")).toString() != QStringLiteral("split-pair") ||
        !slides.at(0).toMap().value(QStringLiteral("primaryHasMedia")).toBool()) {
      return fail("new carousel settings should create exact dimensions, count, profile, background, template, and selected media directly");
    }
    if (!controller.saveDocument()) {
      return fail("custom new carousel settings should save");
    }
    CarouselDocumentController restored;
    if (!restored.loadDocument(controller.savedDocumentId()) ||
        restored.documentFormat() != QStringLiteral("custom") ||
        restored.documentPixelWidth() != 3702 || restored.documentPixelHeight() != 987 ||
        restored.exportColorSpace() != QStringLiteral("Display P3")) {
      return fail("saved custom new carousel settings should reopen without dimension or profile loss");
    }
  }

  {
    const auto missing_family = QStringLiteral("Stillfold Missing Face 9F3A");
    CarouselDocumentController controller;
    controller.createCarouselWithSettings(QVariantMap{
        {QStringLiteral("carouselName"), QStringLiteral("Missing Font Proof")},
        {QStringLiteral("documentFormat"), QStringLiteral("4x5")},
        {QStringLiteral("slideCount"), 1},
    });
    controller.addTextItem(QStringLiteral("First affected headline"));
    controller.setSelectedTextFontFamily(missing_family);
    controller.addTextItem(QStringLiteral("Second affected caption"));
    controller.setSelectedTextFontFamily(missing_family);
    const auto missing_entries = controller.missingFontEntries();
    if (missing_entries.size() != 2 ||
        preflight_issue_count(controller.preflightIssues(), QStringLiteral("missing-font")) != 2 ||
        controller.preflightBlockingCount() != 2) {
      return fail("missing-font detection should identify every exact affected layer and block export");
    }
    for (const auto& value : missing_entries) {
      const auto entry = value.toMap();
      if (entry.value(QStringLiteral("fontFamily")).toString() != missing_family ||
          entry.value(QStringLiteral("itemId")).toString().isEmpty() ||
          entry.value(QStringLiteral("layerLabel")).toString().isEmpty()) {
        return fail("missing-font entries should retain exact original face, layer, and board identity");
      }
    }
    const auto blocked_font_export = dir.filePath(QStringLiteral("missing-font-blocked-export"));
    QDir().mkpath(blocked_font_export);
    controller.setExportDirectory(blocked_font_export);
    if (controller.startConfiguredExport() ||
        !controller.exportProgress().value(QStringLiteral("failureReason")).toString().contains(
            QStringLiteral("Font"))) {
      return fail("export should stay blocked rather than silently substituting an unavailable font");
    }
    bool retained_font_failure = false;
    for (const auto& value : controller.statusNotices()) {
      const auto notice = value.toMap();
      retained_font_failure = retained_font_failure ||
                              (notice.value(QStringLiteral("category")).toString() ==
                                   QStringLiteral("font") &&
                               notice.value(QStringLiteral("actionKey")).toString() ==
                                   QStringLiteral("resolve-fonts"));
    }
    if (!retained_font_failure) {
      return fail("font export failures should retain a direct Resolve Fonts action");
    }

    const auto installed_fonts = controller.installedFontFamilies();
    if (installed_fonts.isEmpty()) {
      return fail("font substitution proof requires an installed replacement family");
    }
    const auto replacement_family = installed_fonts.constFirst();
    if (controller.applyFontFamilyFromBrowser(replacement_family) ||
        controller.selectionDetails().value(QStringLiteral("textFontFamily")).toString() != missing_family) {
      return fail("the installed-font browser must not silently replace a missing authored face");
    }
    if (!controller.previewFontSubstitution(missing_family, replacement_family) ||
        controller.fontSubstitutionPreview().size() != 2) {
      return fail("font substitution should preview every affected layer before applying");
    }
    for (const auto& value : controller.fontSubstitutionPreview()) {
      const auto preview = value.toMap();
      if (preview.value(QStringLiteral("sourceFontFamily")).toString() != missing_family ||
          preview.value(QStringLiteral("replacementFontFamily")).toString() != replacement_family ||
          preview.value(QStringLiteral("itemId")).toString().isEmpty() ||
          preview.value(QStringLiteral("beforeHeight")).toDouble() <= 0.0 ||
          preview.value(QStringLiteral("afterHeight")).toDouble() <= 0.0) {
        return fail("font substitution preview should expose exact layer and before/after layout metrics");
      }
    }
    if (!controller.applyFontSubstitution() || !controller.missingFontEntries().isEmpty() ||
        controller.preflightBlockingCount() != 0) {
      return fail("explicit installed-font substitution should clear only the corresponding export blockers");
    }
    if (!controller.applyFontFamilyFromBrowser(replacement_family) ||
        !controller.recentFontFamilies().contains(replacement_family, Qt::CaseInsensitive)) {
      return fail("choosing an installed browser face should apply it and add it to recent fonts");
    }
    controller.toggleFavoriteFontFamily(replacement_family);
    if (!controller.favoriteFontFamilies().contains(replacement_family, Qt::CaseInsensitive)) {
      return fail("the font browser should support favorite installed faces");
    }
    CarouselDocumentController font_library_reloaded;
    if (!font_library_reloaded.favoriteFontFamilies().contains(replacement_family, Qt::CaseInsensitive) ||
        !font_library_reloaded.recentFontFamilies().contains(replacement_family, Qt::CaseInsensitive)) {
      return fail("font browser favorites and recents should persist in the Carousel design library");
    }
    for (const auto& value : controller.canvasItems()) {
      const auto item = value.toMap();
      if (item.value(QStringLiteral("textFontFamily")).toString() != replacement_family ||
          item.value(QStringLiteral("originalFontFamily")).toString() != missing_family) {
        return fail("font substitution should persist the original missing face identity on every changed layer");
      }
    }
    controller.undo();
    if (controller.missingFontEntries().size() != 2) {
      return fail("font substitution should undo as one document edit");
    }
    if (!controller.previewFontSubstitution(missing_family, replacement_family) ||
        !controller.applyFontSubstitution() || !controller.saveDocument()) {
      return fail("previewed font substitution should reapply and save");
    }
    CarouselDocumentController restored;
    if (!restored.loadDocument(controller.savedDocumentId()) ||
        !restored.missingFontEntries().isEmpty() || restored.canvasItems().size() != 2) {
      return fail("font substitution should survive save and reopen without losing layer count");
    }
    for (const auto& value : restored.canvasItems()) {
      if (value.toMap().value(QStringLiteral("originalFontFamily")).toString() != missing_family) {
        return fail("saved substitution should retain the original font identity for later relink");
      }
    }
  }

  {
    CarouselDocumentController controller;
    controller.createCarouselWithSettings(QVariantMap{
        {QStringLiteral("carouselName"), QStringLiteral("Copy Proof")},
        {QStringLiteral("documentFormat"), QStringLiteral("4x5")},
        {QStringLiteral("slideCount"), 1},
    });
    const auto authored_copy = QStringLiteral("Teh  teh colour tobe , launch");
    controller.addTextItem(authored_copy);
    const auto text_item_id = controller.selectedItemId();
    auto issues = controller.copyProofIssues();
    if (preflight_issue_count(issues, QStringLiteral("spelling")) < 2 ||
        preflight_issue_count(issues, QStringLiteral("repeated-word")) != 1 ||
        preflight_issue_count(issues, QStringLiteral("double-space")) != 1 ||
        preflight_issue_count(issues, QStringLiteral("punctuation-spacing")) != 1 ||
        preflight_issue_count(issues, QStringLiteral("language-variant")) != 1 ||
        controller.copyProofLanguageOptions() !=
            QStringList{QStringLiteral("en-US"), QStringLiteral("en-GB")}) {
      return fail("copy proof should report spelling, repeated words, spacing, punctuation, and language variants");
    }
    controller.setCopyProofLanguage(QStringLiteral("en-GB"));
    if (preflight_issue_count(controller.copyProofIssues(), QStringLiteral("language-variant")) != 0) {
      return fail("copy proof language choice should switch between US and UK spelling rules");
    }
    controller.nextCopyProofIssue();
    if (controller.selectedItemId() != text_item_id) {
      return fail("next copy proof issue should focus the exact affected text layer");
    }

    issues = controller.copyProofIssues();
    QString ignored_issue_id;
    for (const auto& value : issues) {
      const auto issue = value.toMap();
      if (issue.value(QStringLiteral("code")).toString() == QStringLiteral("double-space")) {
        ignored_issue_id = issue.value(QStringLiteral("issueId")).toString();
        break;
      }
    }
    const auto count_before_ignore = issues.size();
    controller.ignoreCopyProofIssue(ignored_issue_id);
    if (controller.copyProofIssues().size() != count_before_ignore - 1 ||
        controller.selectionDetails().value(QStringLiteral("text")).toString() != authored_copy) {
      return fail("ignoring a copy proof issue should hide only that issue without rewriting authored text");
    }
    controller.undo();
    if (controller.copyProofIssues().size() != count_before_ignore ||
        controller.selectionDetails().value(QStringLiteral("text")).toString() != authored_copy) {
      return fail("copy proof ignores should undo without modifying authored text");
    }
    controller.ignoreCopyProofIssue(ignored_issue_id);
    controller.addCopyProofWord(QStringLiteral("tobe"));
    for (const auto& value : controller.copyProofIssues()) {
      const auto issue = value.toMap();
      if (issue.value(QStringLiteral("code")).toString() == QStringLiteral("spelling") &&
          issue.value(QStringLiteral("word")).toString().compare(
              QStringLiteral("tobe"), Qt::CaseInsensitive) == 0) {
        return fail("adding a word to the custom proofing dictionary should remove its spelling issue");
      }
    }
    CarouselDocumentController dictionary_reloaded;
    if (!dictionary_reloaded.customDictionaryWords().contains(QStringLiteral("tobe"), Qt::CaseInsensitive)) {
      return fail("custom copy proof dictionary words should persist in the Carousel library");
    }
    if (!controller.saveDocument()) {
      return fail("copy proof settings should save with the document");
    }
    CarouselDocumentController restored;
    if (!restored.loadDocument(controller.savedDocumentId()) ||
        restored.copyProofLanguage() != QStringLiteral("en-GB") ||
        restored.copyProofIssues().size() != controller.copyProofIssues().size() ||
        restored.canvasItems().constFirst().toMap().value(QStringLiteral("text")).toString() != authored_copy) {
      return fail("copy proof language and ignores should reopen without changing authored text");
    }
  }

  {
    CarouselDocumentController controller;
    controller.addTextItem(QStringLiteral("Keyboard layer one"));
    const auto first_layer_id = controller.selectedItemId();
    controller.addTextItem(QStringLiteral("Keyboard layer two"));
    const auto second_layer_id = controller.selectedItemId();
    controller.addTextItem(QStringLiteral("Keyboard layer three"));
    const auto third_layer_id = controller.selectedItemId();
    controller.selectAdjacentLayer(1);
    if (controller.selectedItemId() != second_layer_id) {
      return fail("keyboard layer navigation should move down the visible layer stack");
    }
    controller.selectAdjacentLayer(-1);
    if (controller.selectedItemId() != third_layer_id) {
      return fail("keyboard layer navigation should move up the visible layer stack");
    }
    controller.selectItem(first_layer_id);
    controller.selectAdjacentLayer(-1);
    if (controller.selectedItemId() != second_layer_id) {
      return fail("keyboard layer navigation should wrap predictably at the stack boundary");
    }
  }

  {
    QImage relink_source(QSize(640, 480), QImage::Format_ARGB32);
    relink_source.fill(QColor(QStringLiteral("#3168d8")));
    const auto source_path = write_image(dir, QStringLiteral("relink-original.png"), relink_source);
    const auto relink_dir_path = dir.filePath(QStringLiteral("relink-candidates"));
    QDir().mkpath(relink_dir_path);
    const auto candidate_path = QDir(relink_dir_path).filePath(QStringLiteral("renamed-source.png"));
    if (source_path.isEmpty() || !QFile::copy(source_path, candidate_path)) {
      return fail("missing-media relink fixtures should save");
    }

    CarouselDocumentController controller;
    controller.createCarouselWithSettings(QVariantMap{
        {QStringLiteral("carouselName"), QStringLiteral("Batch Relink Proof")},
        {QStringLiteral("documentFormat"), QStringLiteral("4x5")},
        {QStringLiteral("slideCount"), 1},
    });
    controller.assignPrimaryImage(source_path, QStringLiteral("Slot source"));
    controller.setCurrentScale(1.65);
    controller.setCurrentOffsetX(0.22);
    controller.setCurrentOffsetY(-0.18);
    controller.addPhotoItem(source_path, QStringLiteral("Layer source"));
    controller.setSelectedItemMediaScale(1.85);
    controller.setSelectedItemMediaOffset(-0.31, 0.27);
    if (!controller.saveDocument() || !QFile::remove(source_path)) {
      return fail("missing-media source should save its identity before becoming unavailable");
    }
    if (controller.missingMediaCount() != 2 ||
        preflight_issue_count(controller.preflightIssues(), QStringLiteral("missing-media")) != 2) {
      return fail("missing-media detection should enumerate slot and freeform instances independently");
    }
    if (!controller.scanRelinkDirectory(relink_dir_path) || controller.relinkPlan().size() != 2) {
      return fail("batch relink scan should compare every missing instance with candidate metadata");
    }
    for (const auto& value : controller.relinkPlan()) {
      const auto entry = value.toMap();
      if (!entry.value(QStringLiteral("safeToApply")).toBool() ||
          entry.value(QStringLiteral("matchState")).toString() != QStringLiteral("verified") ||
          entry.value(QStringLiteral("candidatePath")).toString() != candidate_path ||
          !entry.value(QStringLiteral("matchReason")).toString().contains(
              QStringLiteral("fingerprint"), Qt::CaseInsensitive)) {
        return fail("renamed candidates should relink only when the stored content fingerprint uniquely matches");
      }
    }
    const auto ambiguous_path = QDir(relink_dir_path).filePath(QStringLiteral("duplicate-source.png"));
    if (!QFile::copy(candidate_path, ambiguous_path) || !controller.scanRelinkDirectory(relink_dir_path)) {
      return fail("ambiguous relink fixture should scan");
    }
    for (const auto& value : controller.relinkPlan()) {
      const auto entry = value.toMap();
      if (entry.value(QStringLiteral("safeToApply")).toBool() ||
          entry.value(QStringLiteral("matchState")).toString() != QStringLiteral("ambiguous")) {
        return fail("multiple content-identical candidates should remain ambiguous and never auto-select");
      }
    }
    if (controller.applyRelinkPlan()) {
      return fail("batch relink should refuse a plan without verified unique matches");
    }
    if (!QFile::remove(ambiguous_path) || !controller.scanRelinkDirectory(relink_dir_path) ||
        !controller.applyRelinkPlan() || controller.missingMediaCount() != 0) {
      return fail("verified batch relink should update every safe instance in one operation");
    }
    auto items = controller.canvasItems();
    if (items.size() != 1 ||
        qAbs(items.constFirst().toMap().value(QStringLiteral("mediaScale")).toDouble() - 1.85) > 0.001 ||
        qAbs(items.constFirst().toMap().value(QStringLiteral("mediaOffsetX")).toDouble() + 0.31) > 0.001 ||
        qAbs(items.constFirst().toMap().value(QStringLiteral("mediaOffsetY")).toDouble() - 0.27) > 0.001) {
      return fail("batch relink should preserve each freeform media instance crop");
    }
    controller.setActiveSlot(QStringLiteral("primary"));
    auto selection = controller.selectionDetails();
    if (qAbs(selection.value(QStringLiteral("scale")).toDouble() - 1.65) > 0.001 ||
        qAbs(selection.value(QStringLiteral("offsetX")).toDouble() - 0.22) > 0.001 ||
        qAbs(selection.value(QStringLiteral("offsetY")).toDouble() + 0.18) > 0.001) {
      return fail("batch relink should preserve each slot media instance crop");
    }
    controller.undo();
    if (controller.missingMediaCount() != 2) {
      return fail("batch relink should undo as one document edit");
    }
    if (!controller.scanRelinkDirectory(relink_dir_path) || !controller.applyRelinkPlan() ||
        !controller.saveDocument()) {
      return fail("verified relink should reapply and save");
    }
    CarouselDocumentController restored;
    if (!restored.loadDocument(controller.savedDocumentId()) || restored.missingMediaCount() != 0 ||
        restored.canvasItems().size() != 1 ||
        qAbs(restored.canvasItems().constFirst().toMap().value(
                 QStringLiteral("mediaScale")).toDouble() - 1.85) > 0.001) {
      return fail("relinked paths and preserved crops should survive save and reopen");
    }
  }

  {
    const auto original_documents_path = qgetenv("PHOTO_EDITOR_CAROUSEL_DOCUMENTS_PATH");
    const auto blocked_root = dir.filePath(QStringLiteral("blocked-carousel-root"));
    QFile blocked_root_file(blocked_root);
    if (!blocked_root_file.open(QIODevice::WriteOnly) || blocked_root_file.write("not-a-directory") < 0) {
      return fail("save-failure fixture should create a non-directory documents root");
    }
    blocked_root_file.close();
    qputenv("PHOTO_EDITOR_CAROUSEL_DOCUMENTS_PATH", QFile::encodeName(blocked_root));
    CarouselDocumentController controller;
    controller.createCarouselWithSettings(QVariantMap{
        {QStringLiteral("carouselName"), QStringLiteral("Save State Failure")},
        {QStringLiteral("documentFormat"), QStringLiteral("4x5")},
        {QStringLiteral("slideCount"), 1},
    });
    QStringList observed_states;
    QObject::connect(
        &controller,
        &CarouselDocumentController::documentStateChanged,
        &controller,
        [&controller, &observed_states]() { observed_states.push_back(controller.saveState()); });
    if (controller.saveDocument() || controller.saveState() != QStringLiteral("Save failed") ||
        controller.saveError().isEmpty() || !observed_states.contains(QStringLiteral("Saving"))) {
      return fail("save-state model should expose Saving and a persistent actionable Save failed state");
    }
    bool retained_save_failure = false;
    for (const auto& value : controller.statusNotices()) {
      const auto notice = value.toMap();
      retained_save_failure = retained_save_failure ||
                              (notice.value(QStringLiteral("category")).toString() ==
                                   QStringLiteral("save") &&
                               notice.value(QStringLiteral("actionKey")).toString() ==
                                   QStringLiteral("retry-save"));
    }
    if (!retained_save_failure) {
      return fail("save failures should retain a direct Retry Save action");
    }
    qputenv("PHOTO_EDITOR_CAROUSEL_DOCUMENTS_PATH", original_documents_path);
  }

  {
    QString saved_document_id;
    {
      CarouselDocumentController controller;
      controller.createCarouselWithSettings(QVariantMap{
          {QStringLiteral("carouselName"), QStringLiteral("Autosave Saved Version")},
          {QStringLiteral("documentFormat"), QStringLiteral("4x5")},
          {QStringLiteral("slideCount"), 1},
      });
      controller.addTextItem(QStringLiteral("Last manual save"));
      QStringList observed_states;
      QObject::connect(
          &controller,
          &CarouselDocumentController::documentStateChanged,
          &controller,
          [&controller, &observed_states]() { observed_states.push_back(controller.saveState()); });
      if (!controller.saveDocument()) {
        return fail("autosave recovery fixture should have a current saved version");
      }
      if (!observed_states.contains(QStringLiteral("Saving")) ||
          controller.saveState() != QStringLiteral("Saved") || controller.lastSavedAt().isEmpty()) {
        return fail("successful save should expose Saving, Saved, and the last successful time");
      }
      saved_document_id = controller.savedDocumentId();
      controller.setSelectedText(QStringLiteral("New crash-safe edit"));
      if (!wait_until([&controller]() {
            return controller.saveState() == QStringLiteral("Autosaved");
          }, 6000)) {
        return fail("dirty documents should reach a bounded crash-safe Autosaved state");
      }
    }
    CarouselDocumentController recovery;
    const auto state = recovery.recoveryState();
    if (!state.value(QStringLiteral("available")).toBool() ||
        !state.value(QStringLiteral("hasCurrentSavedVersion")).toBool() ||
        state.value(QStringLiteral("documentId")).toString() != saved_document_id) {
      return fail("startup recovery scan should identify the autosave and its current saved version");
    }
    if (!recovery.openCurrentSavedVersion() || recovery.savedDocumentId() != saved_document_id ||
        recovery.canvasItems().constFirst().toMap().value(QStringLiteral("text")).toString() !=
            QStringLiteral("Last manual save")) {
      return fail("Open Saved Version should discard the snapshot and load the last manual save");
    }
    CarouselDocumentController after_saved_choice;
    if (after_saved_choice.recoveryState().value(QStringLiteral("available")).toBool()) {
      return fail("choosing the current saved version should remove the consumed recovery snapshot");
    }
  }

  {
    {
      CarouselDocumentController controller;
      controller.createCarouselWithSettings(QVariantMap{
          {QStringLiteral("carouselName"), QStringLiteral("Unsaved Recovery")},
          {QStringLiteral("documentFormat"), QStringLiteral("4x5")},
          {QStringLiteral("slideCount"), 1},
      });
      controller.addTextItem(QStringLiteral("Recovered copy content"));
      if (!wait_until([&controller]() {
            return controller.saveState() == QStringLiteral("Autosaved");
          }, 6000)) {
        return fail("unsaved documents should receive crash-safe recovery snapshots");
      }
    }
    CarouselDocumentController recovery;
    if (!recovery.openRecoveryCopy() || !recovery.savedDocumentId().isEmpty() ||
        !recovery.documentName().endsWith(QStringLiteral("Recovery Copy")) ||
        recovery.canvasItems().constFirst().toMap().value(QStringLiteral("text")).toString() !=
            QStringLiteral("Recovered copy content") ||
        recovery.saveState() != QStringLiteral("Autosaved")) {
      return fail("Open As Copy should restore content without reusing the original saved identity");
    }
    if (!recovery.saveDocument() || recovery.savedDocumentId().isEmpty()) {
      return fail("a recovered copy should save as an independent document");
    }
    CarouselDocumentController after_copy_save;
    if (after_copy_save.recoveryState().value(QStringLiteral("available")).toBool()) {
      return fail("saving a recovered copy should remove its consumed recovery files");
    }
  }

  {
    QString autosave_path;
    {
      CarouselDocumentController controller;
      controller.createCarouselWithSettings(QVariantMap{
          {QStringLiteral("carouselName"), QStringLiteral("Last Good Recovery")},
          {QStringLiteral("documentFormat"), QStringLiteral("4x5")},
          {QStringLiteral("slideCount"), 1},
      });
      controller.addTextItem(QStringLiteral("First good snapshot"));
      if (!wait_until([&controller]() {
            return controller.saveState() == QStringLiteral("Autosaved");
          }, 6000)) {
        return fail("first last-good recovery snapshot should complete");
      }
      controller.setSelectedText(QStringLiteral("Second good snapshot"));
      if (!wait_until([&controller]() {
            return controller.saveState() == QStringLiteral("Autosaved");
          }, 6000)) {
        return fail("second autosave should rotate the prior snapshot to last-good");
      }
    }
    const QDir recovery_dir(dir.filePath(QStringLiteral("carousel-documents/recovery")));
    const auto autosaves = recovery_dir.entryInfoList(
        {QStringLiteral("*.autosave.json")}, QDir::Files, QDir::Time);
    if (autosaves.isEmpty()) {
      return fail("autosave should atomically create a recovery artifact");
    }
    autosave_path = autosaves.constFirst().absoluteFilePath();
    QFile corrupt(autosave_path);
    if (!corrupt.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
        corrupt.write("{corrupt") < 0) {
      return fail("primary recovery fixture should be corruptible for last-good proof");
    }
    corrupt.close();
    CarouselDocumentController recovery;
    if (!recovery.recoveryState().value(QStringLiteral("available")).toBool() ||
        !recovery.recoveryState().value(QStringLiteral("usingLastGood")).toBool() ||
        !recovery.restoreRecovery() ||
        recovery.canvasItems().constFirst().toMap().value(QStringLiteral("text")).toString() !=
            QStringLiteral("First good snapshot")) {
      return fail("corrupt primary recovery should fall back to the prior atomically written last-good snapshot");
    }
    recovery.discardRecovery();
    CarouselDocumentController after_discard;
    if (after_discard.recoveryState().value(QStringLiteral("available")).toBool()) {
      return fail("Discard should remove primary and last-good recovery artifacts");
    }
  }

  {
    const auto async_export_dir = dir.filePath(QStringLiteral("async-export"));
    QDir().mkpath(async_export_dir);
    CarouselDocumentController controller;
    controller.createCarouselWithSettings(QVariantMap{
        {QStringLiteral("carouselName"), QStringLiteral("Async Export Proof")},
        {QStringLiteral("documentFormat"), QStringLiteral("4x5")},
        {QStringLiteral("slideCount"), 3},
    });
    controller.setExportManifestEnabled(false);
    controller.setExportDirectory(async_export_dir);
    controller.setExportPartialCleanupPolicy(QStringLiteral("keep"));
    if (!controller.startConfiguredExport() ||
        controller.exportProgress().value(QStringLiteral("state")).toString() !=
            QStringLiteral("running")) {
      return fail("asynchronous configured export should enter a running state");
    }
    if (!wait_until([&controller]() {
          return controller.exportProgress().value(QStringLiteral("completedCount")).toInt() >= 1;
        })) {
      return fail("asynchronous export should report per-board progress");
    }
    controller.cancelExport();
    if (!wait_until([&controller]() {
          return controller.exportProgress().value(QStringLiteral("state")).toString() ==
                 QStringLiteral("cancelled");
        })) {
      return fail("cancelled export should stop between boards");
    }
    auto progress = controller.exportProgress();
    const auto cancelled_count = progress.value(QStringLiteral("completedCount")).toInt();
    if (cancelled_count < 1 || cancelled_count >= 3 ||
        progress.value(QStringLiteral("partialFileCount")).toInt() != cancelled_count ||
        progress.value(QStringLiteral("failureReason")).toString().isEmpty() ||
        !progress.value(QStringLiteral("canRetry")).toBool()) {
      return fail("cancelled export should preserve truthful progress, partial files, reason, and retry action");
    }
    if (!controller.retryFailedExport() ||
        !wait_until([&controller]() {
          return controller.exportProgress().value(QStringLiteral("state")).toString() ==
                 QStringLiteral("completed");
        })) {
      return fail("retry should clean partial files and complete the full export plan");
    }
    progress = controller.exportProgress();
    if (progress.value(QStringLiteral("completedCount")).toInt() != 3 ||
        progress.value(QStringLiteral("totalCount")).toInt() != 3 ||
        progress.value(QStringLiteral("partialFileCount")).toInt() != 0 ||
        qAbs(progress.value(QStringLiteral("fraction")).toDouble() - 1.0) > 0.001 ||
        !progress.value(QStringLiteral("canReveal")).toBool()) {
      return fail("completed export should expose complete progress and reveal output without partial-file state");
    }
    const auto completed_files = QDir(async_export_dir).entryList(
        {QStringLiteral("*.png")}, QDir::Files, QDir::Name);
    if (completed_files.size() != 3) {
      return fail("retry should produce exactly one output per board after partial cleanup");
    }
  }

  {
    const auto cleanup_export_dir = dir.filePath(QStringLiteral("async-remove-partials"));
    QDir().mkpath(cleanup_export_dir);
    CarouselDocumentController controller;
    controller.createCarouselWithSettings(QVariantMap{
        {QStringLiteral("carouselName"), QStringLiteral("Cleanup Proof")},
        {QStringLiteral("documentFormat"), QStringLiteral("4x5")},
        {QStringLiteral("slideCount"), 3},
    });
    controller.setExportManifestEnabled(false);
    controller.setExportDirectory(cleanup_export_dir);
    controller.setExportPartialCleanupPolicy(QStringLiteral("remove"));
    if (!controller.startConfiguredExport() ||
        !wait_until([&controller]() {
          return controller.exportProgress().value(QStringLiteral("completedCount")).toInt() >= 1;
        })) {
      return fail("remove-partial export proof should render its first board");
    }
    controller.cancelExport();
    if (!wait_until([&controller]() {
          return controller.exportProgress().value(QStringLiteral("state")).toString() ==
                 QStringLiteral("cancelled");
        }) ||
        !QDir(cleanup_export_dir).entryList({QStringLiteral("*.png")}, QDir::Files).isEmpty() ||
        controller.exportProgress().value(QStringLiteral("partialFileCount")).toInt() != 0) {
      return fail("remove-on-stop policy should delete only files created by the cancelled export");
    }
  }

  {
    const auto blocked_export_dir = dir.filePath(QStringLiteral("async-blocked"));
    QDir().mkpath(blocked_export_dir);
    CarouselDocumentController controller;
    controller.createCarouselWithSettings(QVariantMap{
        {QStringLiteral("carouselName"), QStringLiteral("Blocked Export Proof")},
        {QStringLiteral("documentFormat"), QStringLiteral("4x5")},
        {QStringLiteral("slideCount"), 1},
        {QStringLiteral("templateKind"), QStringLiteral("split-pair")},
    });
    controller.setExportDirectory(blocked_export_dir);
    if (controller.startConfiguredExport()) {
      return fail("blocking preflight issues should prevent asynchronous export startup");
    }
    const auto progress = controller.exportProgress();
    if (progress.value(QStringLiteral("state")).toString() != QStringLiteral("failed") ||
        !progress.value(QStringLiteral("canRetry")).toBool() ||
        !progress.value(QStringLiteral("failureReason")).toString().startsWith(
            QStringLiteral("Export blocked:"))) {
      return fail("failed export startup should expose a readable reason and retry state");
    }
  }

  {
    CarouselDocumentController controller;
    controller.createCarouselWithSettings(QVariantMap{
        {QStringLiteral("carouselName"), QStringLiteral("Ordered Autofill")},
        {QStringLiteral("documentFormat"), QStringLiteral("4x5")},
        {QStringLiteral("slideCount"), 2},
        {QStringLiteral("templateKind"), QStringLiteral("split-pair")},
    });
    const std::array<QColor, 4> autofill_colors{
        QColor(QStringLiteral("#ff0000")), QColor(QStringLiteral("#00ff00")),
        QColor(QStringLiteral("#0000ff")), QColor(QStringLiteral("#ffff00"))};
    QVariantList autofill_paths;
    for (int index = 0; index < 4; ++index) {
      QImage image(QSize(640, 800), QImage::Format_ARGB32);
      image.fill(autofill_colors.at(static_cast<std::size_t>(index)));
      const auto path = write_image(
          dir, QStringLiteral("autofill-%1.png").arg(index + 1), image);
      if (path.isEmpty()) {
        return fail("ordered autofill media fixtures should save");
      }
      autofill_paths.push_back(path);
    }
    if (!controller.previewMediaAutofill(autofill_paths) ||
        controller.mediaAutofillPlan().size() != 4) {
      return fail("ordered media selection should preview every exact board and slot mapping");
    }
    controller.moveMediaAutofillEntry(3, 0);
    auto autofill_plan = controller.mediaAutofillPlan();
    if (!autofill_plan.constFirst().toMap().value(QStringLiteral("displayLabel")).toString().endsWith(
            QStringLiteral("autofill-4")) ||
        autofill_plan.constFirst().toMap().value(QStringLiteral("boardIndex")).toInt() != 0 ||
        autofill_plan.constFirst().toMap().value(QStringLiteral("slot")).toString() !=
            QStringLiteral("primary")) {
      return fail("reordering autofill sources should preserve the ordered target slot sequence");
    }
    controller.setMediaAutofillEntrySkipped(2, true);
    if (!controller.mediaAutofillPlan().at(2).toMap().value(QStringLiteral("skipped")).toBool()) {
      return fail("autofill mapping preview should support an explicit skip state");
    }
    controller.setMediaAutofillEntrySkipped(2, false);
    if (!controller.applyMediaAutofillPlan() || !controller.mediaAutofillPlan().isEmpty() ||
        !controller.canUndo() || controller.preflightBlockingCount() != 0) {
      return fail("applying a complete autofill mapping should fill every slot as one undoable operation");
    }
    const auto autofill_export_dir = dir.filePath(QStringLiteral("ordered-autofill-export"));
    QDir().mkpath(autofill_export_dir);
    if (!controller.exportCarouselToDirectory(autofill_export_dir)) {
      return fail("ordered autofill result should export");
    }
    const QImage first_board(QDir(autofill_export_dir).filePath(QStringLiteral("carousel-01.png")));
    const QImage second_board(QDir(autofill_export_dir).filePath(QStringLiteral("carousel-02.png")));
    if (!color_is_dominantly_yellow(first_board.pixelColor(270, 675)) ||
        !color_is_dominantly_red(first_board.pixelColor(810, 675)) ||
        !color_is_dominantly_green(second_board.pixelColor(270, 675)) ||
        !color_is_dominantly_blue(second_board.pixelColor(810, 675))) {
      return fail("autofill export should match the reviewed and reordered four-slot mapping");
    }
    controller.undo();
    if (controller.preflightBlockingCount() != 4) {
      return fail("the complete autofill operation should undo atomically to four empty required slots");
    }
    controller.redo();
    if (controller.preflightBlockingCount() != 0 ||
        !controller.saveDocument(QStringLiteral("Ordered Autofill"))) {
      return fail("autofill should redo atomically and save");
    }
    CarouselDocumentController restored;
    if (!restored.loadDocument(controller.savedDocumentId()) || restored.preflightBlockingCount() != 0) {
      return fail("autofilled slot assignments should survive save and reopen");
    }
  }

  {
    QImage source_image(QSize(640, 800), QImage::Format_ARGB32);
    source_image.fill(QColor(QStringLiteral("#4c78ff")));
    QImage unused_image(QSize(640, 800), QImage::Format_ARGB32);
    unused_image.fill(QColor(QStringLiteral("#ffb347")));
    const auto source_path = write_image(
        dir, QStringLiteral("usage-source.png"), source_image);
    const auto unused_path = write_image(
        dir, QStringLiteral("usage-unused.png"), unused_image);
    const auto missing_path = dir.filePath(QStringLiteral("usage-offline.png"));
    if (source_path.isEmpty() || unused_path.isEmpty()) {
      return fail("media usage fixtures should save");
    }

    CarouselDocumentController controller;
    controller.assignPrimaryImage(source_path, QStringLiteral("Repeated source"));
    controller.addSlide();
    controller.assignPrimaryImage(source_path, QStringLiteral("Repeated source"));
    controller.addPhotoItem(source_path, QStringLiteral("Different crop"));
    controller.setSelectedItemMediaScale(1.4);
    const auto layer_id = controller.selectedItemId();
    controller.addSlide();
    controller.assignPrimaryImage(missing_path, QStringLiteral("Offline source"));

    const auto report = controller.mediaUsageReport(QVariantList{source_path, unused_path});
    if (report.value(QStringLiteral("sourceCount")).toInt() != 2 ||
        report.value(QStringLiteral("placementCount")).toInt() != 4 ||
        report.value(QStringLiteral("duplicateSourceCount")).toInt() != 1 ||
        report.value(QStringLiteral("offlineSourceCount")).toInt() != 1 ||
        report.value(QStringLiteral("reusedCropSourceCount")).toInt() != 1 ||
        report.value(QStringLiteral("unusedSelectionCount")).toInt() != 1) {
      return fail("media usage summary should count sources, placements, duplicates, offline dependencies, reused crops, and unused selection");
    }
    QVariantMap repeated_entry;
    QVariantMap offline_entry;
    for (const auto& value : report.value(QStringLiteral("entries")).toList()) {
      const auto entry = value.toMap();
      if (entry.value(QStringLiteral("sourcePath")).toString() == source_path) {
        repeated_entry = entry;
      } else if (entry.value(QStringLiteral("sourcePath")).toString() == missing_path) {
        offline_entry = entry;
      }
    }
    if (repeated_entry.value(QStringLiteral("placementCount")).toInt() != 3 ||
        repeated_entry.value(QStringLiteral("boardNumbersLabel")).toString() != QStringLiteral("1, 2") ||
        repeated_entry.value(QStringLiteral("cropVariantCount")).toInt() != 2 ||
        repeated_entry.value(QStringLiteral("reusedCropCount")).toInt() != 1 ||
        !repeated_entry.value(QStringLiteral("duplicate")).toBool() ||
        !repeated_entry.value(QStringLiteral("hasReusedCrop")).toBool() ||
        !offline_entry.value(QStringLiteral("offline")).toBool()) {
      return fail("per-source media usage should identify exact boards, crop variants, repeated crops, and offline state");
    }
    const auto unused_entries = report.value(QStringLiteral("unusedSelections")).toList();
    if (unused_entries.size() != 1 ||
        unused_entries.constFirst().toMap().value(QStringLiteral("sourcePath")).toString() != unused_path ||
        unused_entries.constFirst().toMap().value(QStringLiteral("reason")).toString() !=
            QStringLiteral("Not used in document")) {
      return fail("media usage should retain the exact unused source selection and reason");
    }
    controller.focusMediaUsagePlacement(1, layer_id);
    if (controller.currentIndex() != 1 || controller.selectedItemId() != layer_id) {
      return fail("media usage placements should jump directly to the dependent board and layer");
    }
    controller.focusMediaUsagePlacement(0, QString{}, QStringLiteral("primary"));
    if (controller.currentIndex() != 0 || !controller.selectedItemId().isEmpty() ||
        controller.selectionDetails().value(QStringLiteral("activeSlot")).toString() !=
            QStringLiteral("primary")) {
      return fail("media usage slot placements should jump directly to the board and slot");
    }

    CarouselDocumentController overflow_controller;
    if (!overflow_controller.previewMediaAutofill(QVariantList{source_path, unused_path})) {
      return fail("autofill overflow should retain valid selected image sources");
    }
    const auto overflow_plan = overflow_controller.mediaAutofillPlan();
    if (overflow_plan.size() != 2 ||
        overflow_plan.at(1).toMap().value(QStringLiteral("boardIndex")).toInt() != -1 ||
        !overflow_plan.at(1).toMap().value(QStringLiteral("skipped")).toBool() ||
        overflow_plan.at(1).toMap().value(QStringLiteral("unusedReason")).toString() !=
            QStringLiteral("No open Carousel target")) {
      return fail("autofill should report selected sources that exceed available document targets");
    }
    overflow_controller.setMediaAutofillEntrySkipped(1, false);
    if (!overflow_controller.mediaAutofillPlan().at(1).toMap().value(
             QStringLiteral("skipped")).toBool()) {
      return fail("an overflow source should stay safely unused until an open target exists");
    }
  }

  {
    CarouselDocumentController controller;
    controller.applyTemplateToCurrent(QStringLiteral("split-pair"));
    controller.addSlide();
    QImage drop_red(QSize(240, 240), QImage::Format_ARGB32);
    QImage drop_blue(QSize(240, 240), QImage::Format_ARGB32);
    QImage drop_green(QSize(240, 240), QImage::Format_ARGB32);
    drop_red.fill(QColor(QStringLiteral("#ff453a")));
    drop_blue.fill(QColor(QStringLiteral("#0a84ff")));
    drop_green.fill(QColor(QStringLiteral("#34c759")));
    const auto drop_red_path = write_image(dir, QStringLiteral("drop-red.png"), drop_red);
    const auto drop_blue_path = write_image(dir, QStringLiteral("drop-blue.png"), drop_blue);
    const auto drop_green_path = write_image(dir, QStringLiteral("drop-green.png"), drop_green);
    if (drop_red_path.isEmpty() || drop_blue_path.isEmpty() || drop_green_path.isEmpty()) {
      return fail("drag-and-drop image fixtures should save");
    }

    if (!controller.dropMediaOnTarget(
            QUrl::fromLocalFile(drop_red_path).toString(),
            QStringLiteral("photo"),
            0,
            QStringLiteral("slot"),
            QStringLiteral("primary"),
            0.25,
            0.5,
            QStringLiteral("Filesystem red")) ||
        controller.undoText() != QStringLiteral("Drop media")) {
      return fail("filesystem URL drops should target an exact board slot in one undo entry");
    }
    controller.undo();
    if (controller.slides().at(0).toMap().value(QStringLiteral("primaryHasMedia")).toBool()) {
      return fail("one undo should remove only the exact slot drop");
    }
    controller.redo();
    if (!controller.dropMediaOnTarget(
            drop_blue_path,
            QStringLiteral("photo"),
            0,
            QStringLiteral("board"),
            QStringLiteral("secondary"),
            0.75,
            0.5,
            QStringLiteral("Library blue")) ||
        !controller.dropMediaOnTarget(
            drop_green_path,
            QStringLiteral("photo"),
            1,
            QStringLiteral("thumbnail"),
            QStringLiteral("primary"),
            0.5,
            0.5,
            QStringLiteral("Thumbnail green"))) {
      return fail("board and thumbnail drops should place media on their exact target boards");
    }
    if (!controller.dropMediaOnTarget(
            drop_red_path,
            QStringLiteral("photo"),
            1,
            QStringLiteral("layer-stack"),
            QStringLiteral("primary"),
            0.25,
            0.75,
            QStringLiteral("Layer red"),
            QStringLiteral("{\"exposure\":0.25}"))) {
      return fail("layer-stack drops should create a freeform media layer");
    }
    const auto dropped_layer = controller.canvasItems().constLast().toMap();
    if (dropped_layer.value(QStringLiteral("slideIndex")).toInt() != 1 ||
        qAbs(dropped_layer.value(QStringLiteral("centerX")).toDouble() - 1350.0) > 1.0 ||
        qAbs(dropped_layer.value(QStringLiteral("centerY")).toDouble() - 1012.5) > 1.0) {
      return fail("layer-stack drop coordinates should map to the exact normalized point on the target board");
    }
    controller.undo();
    if (!controller.canvasItems().isEmpty() ||
        !controller.slides().at(1).toMap().value(QStringLiteral("primaryHasMedia")).toBool()) {
      return fail("one undo should remove the dropped layer without reverting earlier target drops");
    }
    controller.redo();

    const auto drop_export_dir = dir.filePath(QStringLiteral("exact-target-drop-export"));
    QDir().mkpath(drop_export_dir);
    if (controller.preflightBlockingCount() != 0 ||
        !controller.exportCarouselToDirectory(drop_export_dir)) {
      return fail("the exact-target drop result should pass preflight and export");
    }
    const QImage dropped_pair(QDir(drop_export_dir).filePath(QStringLiteral("carousel-01.png")));
    if (!color_is_dominantly_red(dropped_pair.pixelColor(270, 675)) ||
        !color_is_dominantly_blue(dropped_pair.pixelColor(810, 675))) {
      return fail("exported pair pixels should match the exact primary and secondary drop targets");
    }
    if (!controller.saveDocument(QStringLiteral("Exact Target Drops"))) {
      return fail("exact-target drop documents should save");
    }
    CarouselDocumentController reopened_drops;
    if (!reopened_drops.loadDocument(controller.savedDocumentId()) ||
        reopened_drops.documentContentHash() != controller.documentContentHash() ||
        reopened_drops.slideCount() != 2 || reopened_drops.canvasItems().size() != 1) {
      return fail("slot, thumbnail, and layer-stack drop results should survive save and reopen");
    }
  }

  {
    constexpr qint64 kStressBuildBudgetMs = 30000;
    constexpr qint64 kStressSelect100BudgetMs = 750;
    constexpr qint64 kStressEditPublishBudgetMs = 2000;
    constexpr qint64 kStressSaveBudgetMs = 5000;
    constexpr qint64 kStressReopenBudgetMs = 5000;

    QImage high_resolution_image(QSize(4096, 3072), QImage::Format_ARGB32);
    {
      QPainter high_resolution_painter(&high_resolution_image);
      high_resolution_painter.fillRect(QRect(0, 0, 2048, 3072), QColor(QStringLiteral("#ff453a")));
      high_resolution_painter.fillRect(QRect(2048, 0, 2048, 3072), QColor(QStringLiteral("#246bfe")));
    }
    const auto high_resolution_path =
        write_image(dir, QStringLiteral("carousel-stress-4096x3072.png"), high_resolution_image);
    if (high_resolution_path.isEmpty()) {
      return fail("representative carousel stress fixture should create a high-resolution image");
    }
    const auto video_path = dir.filePath(QStringLiteral("carousel-stress-video.mp4"));
    QFile video_file(video_path);
    if (!video_file.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
        video_file.write("representative-video-placeholder") <= 0) {
      return fail("representative carousel stress fixture should create a video source");
    }
    video_file.close();

    QElapsedTimer build_timer;
    build_timer.start();
    CarouselDocumentController controller;
    controller.createCarouselWithSettings(QVariantMap{
        {QStringLiteral("carouselName"), QStringLiteral("Representative Stress v1")},
        {QStringLiteral("documentFormat"), QStringLiteral("4x5")},
        {QStringLiteral("slideCount"), 19},
    });
    controller.setExportColorSpace(QStringLiteral("Display P3"));
    controller.setExportManifestEnabled(false);
    controller.selectSlide(0);
    controller.assignPrimaryImage(high_resolution_path, QStringLiteral("stress spread"));
    controller.applyTemplateToCurrent(QStringLiteral("linked-horizontal-pair"));
    if (controller.slideCount() != 20 ||
        controller.slides().constFirst().toMap().value(QStringLiteral("templateKind")).toString() !=
            QStringLiteral("linked-horizontal-pair")) {
      return fail("representative carousel stress fixture should include a linked two-board spread");
    }

    const auto installed_fonts = controller.installedFontFamilies();
    const auto stress_font = installed_fonts.isEmpty() ? QStringLiteral("Helvetica Neue")
                                                       : installed_fonts.at(installed_fonts.size() / 2);
    QString stress_text_item_id;
    for (int board_index = 0; board_index < 20; ++board_index) {
      controller.selectSlide(board_index);
      controller.setCurrentSlideBackgroundGradient(
          board_index % 2 == 0 ? QStringLiteral("linear-horizontal") : QStringLiteral("linear-vertical"),
          board_index % 2 == 0 ? QStringLiteral("#101820") : QStringLiteral("#f4efe8"),
          board_index % 2 == 0 ? QStringLiteral("#375a7f") : QStringLiteral("#d9a441"));
      if (board_index % 5 == 0) {
        controller.addVideoItem(video_path, QStringLiteral("Stress video"));
      } else {
        controller.addPhotoItem(high_resolution_path, QStringLiteral("Stress high resolution"));
      }
      controller.setSelectedItemCenter(board_index * 1080.0 + 540.0, 675.0);
      controller.setSelectedItemSize(620.0, 460.0);
      controller.setSelectedItemMediaScale(1.8);
      controller.setSelectedItemMediaOffset(0.72, 0.0);

      controller.addTextItemWithRole(
          QStringLiteral("heading"),
          QStringLiteral("Board %1 stress heading").arg(board_index + 1));
      controller.setSelectedTextFontFamily(stress_font);
      controller.setSelectedItemCenter(board_index * 1080.0 + 540.0, 240.0);
      if (stress_text_item_id.isEmpty()) {
        stress_text_item_id = controller.selectedItemId();
      }

      controller.addTextItemWithRole(
          QStringLiteral("caption"),
          QStringLiteral("Mixed media, gradient, custom type, and linked component %1.")
              .arg(board_index + 1));
      controller.setSelectedTextLineHeight(1.35);
      controller.setSelectedItemCenter(board_index * 1080.0 + 540.0, 1080.0);

      controller.addShapeItem(QStringLiteral("rectangle"));
      const auto first_component_id = controller.selectedItemId();
      controller.setSelectedShapeFillColor(QStringLiteral("#ff0000"));
      controller.setSelectedItemCenter(board_index * 1080.0 + 470.0, 910.0);
      controller.setSelectedItemSize(180.0, 120.0);
      controller.addShapeItem(QStringLiteral("ellipse"));
      const auto second_component_id = controller.selectedItemId();
      controller.setSelectedShapeFillColor(QStringLiteral("#0000ff"));
      controller.setSelectedItemBlendMode(QStringLiteral("multiply"));
      controller.setSelectedItemCenter(board_index * 1080.0 + 610.0, 910.0);
      controller.setSelectedItemSize(180.0, 120.0);
      controller.selectItem(first_component_id);
      controller.addItemToSelection(second_component_id);
      controller.groupSelectedItems();
    }
    if (build_timer.elapsed() > kStressBuildBudgetMs) {
      return fail("representative 20-board/100-layer stress fixture exceeded its 30-second build budget");
    }

    const auto stress_items = controller.canvasItems();
    int image_count = 0;
    int video_count = 0;
    int text_count = 0;
    int shape_count = 0;
    int grouped_count = 0;
    for (const auto& value : stress_items) {
      const auto item = value.toMap();
      const auto type = item.value(QStringLiteral("type")).toString();
      image_count += type == QStringLiteral("image") ? 1 : 0;
      video_count += type == QStringLiteral("video") ? 1 : 0;
      text_count += type == QStringLiteral("text") ? 1 : 0;
      shape_count += type == QStringLiteral("shape") ? 1 : 0;
      grouped_count += item.value(QStringLiteral("grouped")).toBool() ? 1 : 0;
    }
    if (stress_items.size() != 100 || image_count != 16 || video_count != 4 ||
        text_count != 40 || shape_count != 40 || grouped_count != 40) {
      return fail("representative stress fixture should contain exactly 100 mixed and linked layers");
    }

    QElapsedTimer select_timer;
    select_timer.start();
    for (const auto& value : stress_items) {
      controller.selectItem(value.toMap().value(QStringLiteral("itemId")).toString());
    }
    if (select_timer.elapsed() > kStressSelect100BudgetMs) {
      return fail("selecting all 100 stress layers exceeded the declared 750 ms interaction budget");
    }

    controller.selectItem(stress_text_item_id);
    const auto revision_before_edit =
        controller.previewDiagnostics().value(QStringLiteral("revision")).toInt();
    QElapsedTimer edit_timer;
    edit_timer.start();
    controller.setSelectedTextParagraphSpacing(18.0);
    if (!wait_until([&controller, revision_before_edit]() {
          return controller.previewDiagnostics().value(QStringLiteral("revision")).toInt() >
                 revision_before_edit;
        }, static_cast<int>(kStressEditPublishBudgetMs)) ||
        edit_timer.elapsed() > kStressEditPublishBudgetMs) {
      return fail("stress-fixture text edit and preview publish exceeded the declared two-second budget");
    }

    QElapsedTimer save_timer;
    save_timer.start();
    const auto authored_content_hash = controller.documentContentHash();
    if (authored_content_hash.size() != 64) {
      return fail("representative stress fixture should expose a canonical SHA-256 document hash");
    }
    if (!controller.saveDocument(QStringLiteral("Representative Stress v1")) ||
        save_timer.elapsed() > kStressSaveBudgetMs) {
      return fail("saving the representative stress fixture exceeded the declared five-second budget");
    }
    QElapsedTimer reopen_timer;
    reopen_timer.start();
    CarouselDocumentController restored;
    const bool stress_reopened = restored.loadDocument(controller.savedDocumentId());
    const auto stress_reopen_elapsed_ms = reopen_timer.elapsed();
    const bool stress_hash_matches = restored.documentContentHash() == authored_content_hash;
    if (!stress_reopened || stress_reopen_elapsed_ms > kStressReopenBudgetMs || restored.slideCount() != 20 ||
        restored.canvasItems().size() != 100 || !stress_hash_matches) {
      const auto diagnostic = QStringLiteral(
                                  "reopening the representative stress fixture failed: loaded=%1 elapsedMs=%2 "
                                  "boards=%3 layers=%4 hashMatches=%5")
                                  .arg(stress_reopened)
                                  .arg(stress_reopen_elapsed_ms)
                                  .arg(restored.slideCount())
                                  .arg(restored.canvasItems().size())
                                  .arg(stress_hash_matches)
                                  .toUtf8();
      return fail(diagnostic.constData());
    }

    const auto first_stress_export_dir = dir.filePath(QStringLiteral("representative-stress-export-a"));
    const auto second_stress_export_dir = dir.filePath(QStringLiteral("representative-stress-export-b"));
    QDir().mkpath(first_stress_export_dir);
    QDir().mkpath(second_stress_export_dir);
    if (!controller.exportCarouselToDirectory(first_stress_export_dir) ||
        !restored.exportCarouselToDirectory(second_stress_export_dir)) {
      return fail("authored and reopened representative stress documents should both export");
    }
    for (int board_index = 0; board_index < 20; ++board_index) {
      const auto file_name = QStringLiteral("carousel-%1.png").arg(board_index + 1, 2, 10, QLatin1Char('0'));
      QFile first_output(QDir(first_stress_export_dir).filePath(file_name));
      QFile second_output(QDir(second_stress_export_dir).filePath(file_name));
      if (!first_output.open(QIODevice::ReadOnly) || !second_output.open(QIODevice::ReadOnly)) {
        return fail("every representative stress output should exist in both export passes");
      }
      const auto first_hash = QCryptographicHash::hash(first_output.readAll(), QCryptographicHash::Sha256);
      const auto second_hash = QCryptographicHash::hash(second_output.readAll(), QCryptographicHash::Sha256);
      if (first_hash != second_hash) {
        return fail("save/reopen should preserve the SHA-256 hash of every representative PNG output");
      }
      const QImage verified_output(QDir(first_stress_export_dir).filePath(file_name));
      if (verified_output.colorSpace().description() != QStringLiteral("Display P3")) {
        return fail("representative deterministic exports should retain the requested Display P3 profile");
      }
    }
    const auto cache_snapshot = photo_editor::desktop::decoded_image_cache_snapshot();
    if (cache_snapshot.entries <= 0 || cache_snapshot.bytes <= 0 ||
        cache_snapshot.bytes > photo_editor::desktop::decoded_image_cache_budget_bytes()) {
      return fail("media-heavy representative exports should use the shared cache within its 512 MB budget");
    }
    const QImage spread_left(
        QDir(first_stress_export_dir).filePath(QStringLiteral("carousel-01.png")));
    const QImage spread_right(
        QDir(first_stress_export_dir).filePath(QStringLiteral("carousel-02.png")));
    if (spread_left.pixelColor(200, 675).red() <= spread_left.pixelColor(200, 675).blue() ||
        spread_right.pixelColor(880, 675).blue() <= spread_right.pixelColor(880, 675).red()) {
      return fail("representative pixel proof should preserve opposite sides of the linked red/blue spread");
    }
    const QImage crop_and_blend_output(
        QDir(first_stress_export_dir).filePath(QStringLiteral("carousel-03.png")));
    const auto blend_pixel = crop_and_blend_output.pixelColor(540, 910);
    if (blend_pixel.red() > 40 || blend_pixel.green() > 40 || blend_pixel.blue() > 40 ||
        !region_contains_non_background(
            crop_and_blend_output, QRect(180, 120, 720, 300), crop_and_blend_output.pixelColor(0, 0))) {
      return fail("representative pixel proof should retain multiply blending and laid-out heading text");
    }
  }

  {
    const auto cutout_source = write_image(
        dir,
        QStringLiteral("cutout-source.png"),
        []() {
          QImage image(QSize(800, 800), QImage::Format_ARGB32);
          image.fill(QColor(QStringLiteral("#e32942")));
          return image;
        }());
    if (cutout_source.isEmpty()) {
      return fail("background-removal source fixture should save");
    }

    qputenv("PHOTO_EDITOR_TEST_FAKE_SEGMENTATION", QByteArrayLiteral("1"));
    CarouselDocumentController layer_controller;
    layer_controller.createCarouselWithSettings(QVariantMap{
        {QStringLiteral("carouselName"), QStringLiteral("Layer Cutout Proof")},
        {QStringLiteral("documentFormat"), QStringLiteral("4x5")},
        {QStringLiteral("slideCount"), 1},
    });
    layer_controller.setCurrentSlideBackgroundTransparent();
    layer_controller.addPhotoItem(cutout_source, QStringLiteral("Layer subject"));
    const auto cutout_layer_id = layer_controller.selectedItemId();
    if (!layer_controller.startSelectedMediaBackgroundRemoval() ||
        layer_controller.selectionDetails().value(QStringLiteral("cutoutState")).toString() !=
            QStringLiteral("processing")) {
      return fail("layer background removal should start asynchronously with visible processing state");
    }
    if (!wait_until([&layer_controller]() {
          return layer_controller.selectionDetails().value(QStringLiteral("cutoutState")).toString() ==
                 QStringLiteral("ready");
        })) {
      return fail("layer background removal should finish with an active deterministic matte");
    }
    auto layer_cutout = layer_controller.selectionDetails();
    if (!layer_cutout.value(QStringLiteral("cutoutEnabled")).toBool() ||
        !layer_cutout.value(QStringLiteral("cutoutHasMask")).toBool()) {
      return fail("layer cutout state should expose the retained matte and active result");
    }
    layer_controller.useSelectedMediaOriginal();
    if (layer_controller.selectionDetails().value(QStringLiteral("cutoutState")).toString() !=
        QStringLiteral("original")) {
      return fail("Use original should disable the result without deleting its matte");
    }
    layer_controller.undo();
    if (layer_controller.selectionDetails().value(QStringLiteral("cutoutState")).toString() !=
        QStringLiteral("ready")) {
      return fail("original fallback should undo back to the cutout");
    }
    layer_controller.redo();
    if (layer_controller.selectionDetails().value(QStringLiteral("cutoutState")).toString() !=
        QStringLiteral("original")) {
      return fail("original fallback should redo without regenerating the matte");
    }
    layer_controller.restoreSelectedMediaCutout();
    layer_controller.setSelectedMediaCutoutRefinement(2.0, 1.0, 35.0);
    layer_controller.setCutoutPreviewMode(QStringLiteral("edges"));
    layer_cutout = layer_controller.selectionDetails();
    if (qAbs(layer_cutout.value(QStringLiteral("cutoutFeather")).toDouble() - 2.0) > 0.001 ||
        qAbs(layer_cutout.value(QStringLiteral("cutoutExpand")).toDouble() - 1.0) > 0.001 ||
        qAbs(layer_cutout.value(QStringLiteral("cutoutEdgeContrast")).toDouble() - 35.0) > 0.001 ||
        layer_cutout.value(QStringLiteral("cutoutPreviewMode")).toString() !=
            QStringLiteral("edges")) {
      return fail("layer edge refinement and inspection mode should update independently");
    }

    const auto layer_export_dir = dir.filePath(QStringLiteral("cutout-layer-export"));
    QDir().mkpath(layer_export_dir);
    layer_controller.setExportManifestEnabled(false);
    layer_controller.setExportImageFormat(QStringLiteral("png"));
    layer_controller.setExportPreserveTransparency(true);
    if (!layer_controller.exportCarouselToDirectory(layer_export_dir)) {
      return fail("layer cutout should export through the normal Carousel renderer");
    }
    const QImage layer_output(
        QDir(layer_export_dir).filePath(QStringLiteral("carousel-01.png")));
    const auto layer_frame = layer_controller.selectionDetails();
    const auto layer_edge_sample = QPoint(
        qRound(layer_frame.value(QStringLiteral("x")).toDouble() + 8.0),
        qRound(layer_frame.value(QStringLiteral("centerY")).toDouble()));
    if (layer_output.isNull() || layer_output.pixelColor(0, 0).alpha() != 0 ||
        layer_output.pixelColor(layer_output.width() / 2, layer_output.height() / 2).alpha() < 250 ||
        layer_output.pixelColor(layer_edge_sample).alpha() >= 200 ||
        !color_is_dominantly_red(
            layer_output.pixelColor(layer_output.width() / 2, layer_output.height() / 2))) {
      const auto diagnostic = QStringLiteral(
                                  "layer cutout alpha proof failed: size=%1x%2 cornerA=%3 centerRGBA=%4,%5,%6,%7 edge=(%8,%9) edgeA=%10")
                                  .arg(layer_output.width())
                                  .arg(layer_output.height())
                                  .arg(layer_output.pixelColor(0, 0).alpha())
                                  .arg(layer_output.pixelColor(layer_output.width() / 2, layer_output.height() / 2).red())
                                  .arg(layer_output.pixelColor(layer_output.width() / 2, layer_output.height() / 2).green())
                                  .arg(layer_output.pixelColor(layer_output.width() / 2, layer_output.height() / 2).blue())
                                  .arg(layer_output.pixelColor(layer_output.width() / 2, layer_output.height() / 2).alpha())
                                  .arg(layer_edge_sample.x())
                                  .arg(layer_edge_sample.y())
                                  .arg(layer_output.pixelColor(layer_edge_sample).alpha())
                                  .toUtf8();
      return fail(diagnostic.constData());
    }
    layer_controller.useSelectedMediaOriginal();
    const auto original_export_dir = dir.filePath(QStringLiteral("cutout-layer-original-export"));
    QDir().mkpath(original_export_dir);
    if (!layer_controller.exportCarouselToDirectory(original_export_dir)) {
      return fail("original fallback should remain exportable");
    }
    const QImage original_output(
        QDir(original_export_dir).filePath(QStringLiteral("carousel-01.png")));
    if (original_output.pixelColor(layer_edge_sample).alpha() < 250) {
      return fail("original fallback should export the unmasked source while retaining transparent board alpha elsewhere");
    }
    layer_controller.restoreSelectedMediaCutout();
    if (!layer_controller.saveDocument(QStringLiteral("Layer Cutout Proof"))) {
      return fail("layer cutout document should save its matte and refinement");
    }
    CarouselDocumentController reopened_layer;
    if (!reopened_layer.loadDocument(layer_controller.savedDocumentId())) {
      return fail("layer cutout document should reopen");
    }
    reopened_layer.selectItem(cutout_layer_id);
    const auto reopened_layer_state = reopened_layer.selectionDetails();
    if (reopened_layer_state.value(QStringLiteral("cutoutState")).toString() !=
            QStringLiteral("ready") ||
        qAbs(reopened_layer_state.value(QStringLiteral("cutoutFeather")).toDouble() - 2.0) > 0.001 ||
        qAbs(reopened_layer_state.value(QStringLiteral("cutoutExpand")).toDouble() - 1.0) > 0.001 ||
        qAbs(reopened_layer_state.value(QStringLiteral("cutoutEdgeContrast")).toDouble() - 35.0) > 0.001) {
      return fail("layer matte and refinements should survive save and reopen");
    }

    CarouselDocumentController slot_controller;
    slot_controller.createCarouselWithSettings(QVariantMap{
        {QStringLiteral("carouselName"), QStringLiteral("Slot Cutout Proof")},
        {QStringLiteral("documentFormat"), QStringLiteral("4x5")},
        {QStringLiteral("slideCount"), 1},
    });
    slot_controller.setCurrentSlideBackgroundTransparent();
    slot_controller.assignPrimaryImage(cutout_source, QStringLiteral("Slot subject"));
    if (!slot_controller.startSelectedMediaBackgroundRemoval() ||
        !wait_until([&slot_controller]() {
          return slot_controller.selectionDetails().value(QStringLiteral("cutoutState")).toString() ==
                 QStringLiteral("ready");
        })) {
      return fail("slot background removal should use the same asynchronous matte path");
    }
    slot_controller.setSelectedMediaCutoutRefinement(1.0, 0.0, 20.0);
    slot_controller.setCutoutPreviewMode(QStringLiteral("matte"));
    slot_controller.setExportManifestEnabled(false);
    slot_controller.setExportImageFormat(QStringLiteral("png"));
    slot_controller.setExportPreserveTransparency(true);
    const auto slot_export_dir = dir.filePath(QStringLiteral("cutout-slot-export"));
    QDir().mkpath(slot_export_dir);
    if (!slot_controller.exportCarouselToDirectory(slot_export_dir)) {
      return fail("slot cutout should export with transparent alpha");
    }
    const auto slot_output_path =
        QDir(slot_export_dir).filePath(QStringLiteral("carousel-01.png"));
    const QImage slot_output(slot_output_path);
    if (slot_output.isNull() || slot_output.pixelColor(0, 0).alpha() > 4 ||
        slot_output.pixelColor(slot_output.width() / 2, slot_output.height() / 2).alpha() < 250 ||
        !color_is_dominantly_red(
            slot_output.pixelColor(slot_output.width() / 2, slot_output.height() / 2))) {
      return fail("matte inspection must stay preview-only while slot export contains subject color and alpha");
    }
    QFile slot_file(slot_output_path);
    if (!slot_file.open(QIODevice::ReadOnly)) {
      return fail("slot cutout output should be readable for deterministic hash proof");
    }
    const auto slot_hash = QCryptographicHash::hash(slot_file.readAll(), QCryptographicHash::Sha256);
    if (!slot_controller.saveDocument(QStringLiteral("Slot Cutout Proof"))) {
      return fail("slot cutout document should save");
    }
    CarouselDocumentController reopened_slot;
    if (!reopened_slot.loadDocument(slot_controller.savedDocumentId()) ||
        reopened_slot.selectionDetails().value(QStringLiteral("cutoutState")).toString() !=
            QStringLiteral("ready") ||
        reopened_slot.selectionDetails().value(QStringLiteral("backgroundKind")).toString() !=
            QStringLiteral("transparent")) {
      return fail("slot matte and transparent board should survive save and reopen");
    }
    reopened_slot.setExportManifestEnabled(false);
    reopened_slot.setExportImageFormat(QStringLiteral("png"));
    reopened_slot.setExportPreserveTransparency(true);
    const auto reopened_slot_export_dir =
        dir.filePath(QStringLiteral("cutout-slot-reopened-export"));
    QDir().mkpath(reopened_slot_export_dir);
    if (!reopened_slot.exportCarouselToDirectory(reopened_slot_export_dir)) {
      return fail("reopened slot cutout should export");
    }
    QFile reopened_slot_file(
        QDir(reopened_slot_export_dir).filePath(QStringLiteral("carousel-01.png")));
    if (!reopened_slot_file.open(QIODevice::ReadOnly) ||
        QCryptographicHash::hash(reopened_slot_file.readAll(), QCryptographicHash::Sha256) !=
            slot_hash) {
      return fail("slot cutout PNG should be byte-deterministic after save and reopen");
    }

    qunsetenv("PHOTO_EDITOR_TEST_FAKE_SEGMENTATION");
    CarouselDocumentController failed_controller;
    failed_controller.createCarouselWithSettings(QVariantMap{
        {QStringLiteral("carouselName"), QStringLiteral("Cutout Failure Proof")},
        {QStringLiteral("documentFormat"), QStringLiteral("4x5")},
        {QStringLiteral("slideCount"), 1},
    });
    failed_controller.addPhotoItem(cutout_source, QStringLiteral("Failure subject"));
    if (!failed_controller.startSelectedMediaBackgroundRemoval() ||
        !wait_until([&failed_controller]() {
          return failed_controller.selectionDetails().value(QStringLiteral("cutoutState")).toString() ==
                 QStringLiteral("error");
        }) ||
        failed_controller.selectionDetails().value(QStringLiteral("cutoutError")).toString().isEmpty() ||
        failed_controller.selectionDetails().value(QStringLiteral("cutoutHasMask")).toBool()) {
      return fail("failed background removal should expose a retryable reason and leave the original untouched");
    }
  }

  {
    CarouselDocumentController controller;
    controller.createCarouselWithSettings(QVariantMap{
        {QStringLiteral("carouselName"), QStringLiteral("Release Workflow v1")},
        {QStringLiteral("documentFormat"), QStringLiteral("4x5")},
        {QStringLiteral("slideCount"), 5},
        {QStringLiteral("colorSpace"), QStringLiteral("sRGB")},
        {QStringLiteral("backgroundColor"), QStringLiteral("#fbf7f1")},
    });
    QVariantList release_media_paths;
    const std::array<QColor, 5> release_colors{
        QColor(QStringLiteral("#ef476f")), QColor(QStringLiteral("#ffd166")),
        QColor(QStringLiteral("#06d6a0")), QColor(QStringLiteral("#118ab2")),
        QColor(QStringLiteral("#073b4c"))};
    for (int index = 0; index < 5; ++index) {
      QImage image(QSize(1600, 2000), QImage::Format_ARGB32);
      image.fill(release_colors.at(static_cast<std::size_t>(index)));
      const auto path = write_image(
          dir, QStringLiteral("release-workflow-media-%1.png").arg(index + 1), image);
      if (path.isEmpty()) {
        return fail("release workflow media fixture should save");
      }
      release_media_paths.push_back(path);
    }
    if (!controller.previewMediaAutofill(release_media_paths) ||
        !controller.applyMediaAutofillPlan() || controller.preflightBlockingCount() != 0) {
      return fail("release workflow should batch-fill all five boards through the reviewed mapping command");
    }

    controller.selectSlide(0);
    controller.applyDesignPreset(QStringLiteral("cover"));
    controller.setSelectedText(QStringLiteral("A COMPLETE RELEASE WORKFLOW"));
    controller.setCurrentSlideRole(QStringLiteral("cover"));
    controller.setCurrentSlideAccessibilityText(
        QStringLiteral("Coral cover board with a release workflow headline."),
        QStringLiteral("The opening board introduces the complete release workflow."));
    if (!controller.saveCurrentBoardAsBrandKit(QStringLiteral("Release Workflow Brand")) ||
        controller.brandKits().isEmpty()) {
      return fail("release workflow should capture its authored board style as a brand kit");
    }
    const auto release_brand_id =
        controller.brandKits().constLast().toMap().value(QStringLiteral("brandId")).toString();
    controller.selectSlide(0);
    controller.selectSlideRange(4);
    controller.applyBrandKit(release_brand_id, QStringLiteral("selected"));

    for (int index = 1; index < 5; ++index) {
      controller.selectSlide(index);
      controller.setCurrentSlideName(QStringLiteral("Release board %1").arg(index + 1));
      controller.setCurrentSlideRole(index == 4 ? QStringLiteral("cta") : QStringLiteral("detail"));
      controller.setCurrentSlideAccessibilityText(
          QStringLiteral("Release media board %1.").arg(index + 1),
          QStringLiteral("Board %1 advances the release sequence.").arg(index + 1));
      controller.addTextItemWithRole(
          index == 4 ? QStringLiteral("cta") : QStringLiteral("caption"),
          index == 4 ? QStringLiteral("SHIP THE WORK")
                     : QStringLiteral("Release detail %1").arg(index + 1));
    }
    controller.moveSlide(4, 1);
    if (controller.slides().at(1).toMap().value(QStringLiteral("boardRole")).toString() !=
        QStringLiteral("cta")) {
      return fail("release workflow cross-board reorder should move the CTA and its content together");
    }

    const auto release_hash = controller.documentContentHash();
    if (!controller.saveDocument(QStringLiteral("Release Workflow v1"))) {
      return fail("release workflow should save");
    }
    CarouselDocumentController restored;
    if (!restored.loadDocument(controller.savedDocumentId()) ||
        restored.documentContentHash() != release_hash || restored.slideCount() != 5 ||
        restored.preflightBlockingCount() != 0) {
      return fail("release workflow should reopen identically and pass blocking preflight");
    }

    const auto release_export_dir = dir.filePath(QStringLiteral("release-workflow-export"));
    QDir().mkpath(release_export_dir);
    restored.setExportDirectory(release_export_dir);
    restored.setExportFilenamePattern(QStringLiteral("{document}-{board_number}-{board_role}"));
    restored.setExportManifestEnabled(true);
    if (!restored.exportConfiguredCarousel()) {
      return fail("release workflow should export its complete ordered delivery");
    }
    const auto release_manifests =
        QDir(release_export_dir).entryList({QStringLiteral("*.json")}, QDir::Files);
    if (release_manifests.size() != 1) {
      return fail("release workflow should produce one delivery manifest");
    }
    QFile release_manifest_file(QDir(release_export_dir).filePath(release_manifests.constFirst()));
    if (!release_manifest_file.open(QIODevice::ReadOnly)) {
      return fail("release workflow delivery manifest should be readable");
    }
    const auto release_manifest =
        QJsonDocument::fromJson(release_manifest_file.readAll()).object();
    const auto release_outputs = release_manifest.value(QStringLiteral("outputs")).toArray();
    if (release_outputs.size() != 5) {
      return fail("release workflow manifest should list every board in exported order");
    }
    for (int index = 0; index < release_outputs.size(); ++index) {
      const auto output = release_outputs.at(index).toObject();
      QFile output_file(QDir(release_export_dir).filePath(output.value(QStringLiteral("fileName")).toString()));
      if (!output_file.open(QIODevice::ReadOnly)) {
        return fail("release workflow manifest should reference every readable output");
      }
      const auto delivered_hash = QString::fromLatin1(
          QCryptographicHash::hash(output_file.readAll(), QCryptographicHash::Sha256).toHex());
      if (delivered_hash != output.value(QStringLiteral("sha256")).toString() ||
          output.value(QStringLiteral("width")).toInt() != 1080 ||
          output.value(QStringLiteral("height")).toInt() != 1350 ||
          output.value(QStringLiteral("colorProfile")).toString() != QStringLiteral("sRGB") ||
          output.value(QStringLiteral("altText")).toString().isEmpty()) {
        return fail("release workflow should verify hash, dimensions, profile, and accessibility data for every output");
      }
    }
    controller.deleteBrandKit(release_brand_id);
  }

  return EXIT_SUCCESS;
}
