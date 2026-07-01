#include <algorithm>
#include <cstdlib>
#include <iostream>

#include <QApplication>
#include <QColor>
#include <QColorSpace>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QTemporaryDir>

#include "carousel_document_controller.h"

namespace {

using photo_editor::desktop::CarouselDocumentController;

int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

QString write_image(const QDir& directory, const QString& name, const QImage& image) {
  const auto path = directory.filePath(name);
  if (!image.save(path)) {
    return {};
  }
  return path;
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

}  // namespace

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));
  QApplication app(argc, argv);

  QTemporaryDir workspace;
  if (!workspace.isValid()) {
    return fail("temporary workspace should be created");
  }
  const QDir dir(workspace.path());

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

    const auto export_dir_path = dir.filePath(QStringLiteral("export-linked-template"));
    QDir().mkpath(export_dir_path);
    if (!controller.exportCarouselToDirectory(export_dir_path)) {
      return fail("linked template export should succeed");
    }

    const QImage first_slide(QDir(export_dir_path).filePath(QStringLiteral("carousel-01.png")));
    const QImage second_slide(QDir(export_dir_path).filePath(QStringLiteral("carousel-02.png")));
    if (!color_matches(first_slide.pixelColor(first_slide.width() / 2, first_slide.height() / 2),
                       QColor(QStringLiteral("#ff453a"))) ||
        !color_matches(second_slide.pixelColor(second_slide.width() / 2, second_slide.height() / 2),
                       QColor(QStringLiteral("#0a84ff")))) {
      return fail("linked template export should still cut a slot-based spread cleanly across two canvases");
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
        photo_editor::desktop::serialize_edit_recipe_json(recipe_state));

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
    if (!color_matches(slide.pixelColor(slide.width() / 4, slide.height() / 2), QColor(QStringLiteral("#34c759"))) ||
        !color_matches(slide.pixelColor((slide.width() * 3) / 4, slide.height() / 2), QColor(QStringLiteral("#ffd60a")))) {
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
    if (!color_matches(slide.pixelColor(390, 525), QColor(QStringLiteral("#ffcc00"))) ||
        !color_matches(slide.pixelColor(690, 825), QColor(QStringLiteral("#ff453a")))) {
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
        !color_matches(slide.pixelColor(540, 675), QColor(QStringLiteral("#ff453a")))) {
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
    if (!color_matches(slide.pixelColor(540, 675), QColor(QStringLiteral("#0a84ff")))) {
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
        !color_matches(slide.pixelColor(540, 675), QColor(QStringLiteral("#0a84ff")))) {
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
        !color_matches(slide.pixelColor(540, 675), QColor(QStringLiteral("#0a84ff")))) {
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
        !color_matches(slide.pixelColor(540, 675), QColor(QStringLiteral("#0a84ff")))) {
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
    if (!color_matches(first_slide.pixelColor(first_slide.width() / 2, first_slide.height() / 2),
                       QColor(QStringLiteral("#ff2d55"))) ||
        !color_matches(second_slide.pixelColor(second_slide.width() / 2, second_slide.height() / 2),
                       QColor(QStringLiteral("#1e90ff")))) {
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
    if (!color_matches(first_slide.pixelColor(320, 720), QColor(QStringLiteral("#34c759")))) {
      return fail("first canvas should keep its photo layer in place");
    }
    if (!color_matches(second_slide.pixelColor(540, 720), QColor(QStringLiteral("#0a84ff")))) {
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
      const auto white_path = write_image(
          dir,
          export_name + QStringLiteral("-background.png"),
          QImage(QSize(1080, 1350), QImage::Format_ARGB32));
      if (white_path.isEmpty()) {
        return QRect();
      }

      QImage white_image(white_path);
      white_image.fill(QColor(QStringLiteral("#ffffff")));
      if (!white_image.save(white_path)) {
        return QRect();
      }

      controller.assignPrimaryImage(white_path, QStringLiteral("white"));
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
    const auto white_path = write_image(
        dir,
        QStringLiteral("text-alignment-background.png"),
        QImage(QSize(1080, 1350), QImage::Format_ARGB32));
    if (white_path.isEmpty()) {
      return fail("text alignment background image should save");
    }

    QImage white_image(white_path);
    white_image.fill(QColor(QStringLiteral("#ffffff")));
    if (!white_image.save(white_path)) {
      return fail("text alignment background image should save after fill");
    }

    controller.assignPrimaryImage(white_path, QStringLiteral("white"));
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

    controller.toggleItemSelection(second_id);
    selection = controller.selectionDetails();
    if (selection.value(QStringLiteral("selectedCount")).toInt() != 2) {
      return fail("toggling a selected item should remove it from the selection set");
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
        restored_items.at(0).toMap().value(QStringLiteral("slideIndex")).toInt() != 1) {
      return fail("loaded carousel documents should restore freeform layer items");
    }

    const auto restored_export_dir = dir.filePath(QStringLiteral("export-restored-document"));
    QDir().mkpath(restored_export_dir);
    if (!restored.exportCarouselToDirectory(restored_export_dir)) {
      return fail("loaded carousel documents should still export successfully");
    }

    const QImage restored_first_slide(QDir(restored_export_dir).filePath(QStringLiteral("carousel-01.png")));
    if (!color_matches(restored_first_slide.pixelColor(restored_first_slide.width() / 4, restored_first_slide.height() / 2),
                       QColor(QStringLiteral("#ff9f0a"))) ||
        !color_matches(restored_first_slide.pixelColor((restored_first_slide.width() * 3) / 4, restored_first_slide.height() / 2),
                       QColor(QStringLiteral("#64d2ff")))) {
      return fail("loaded carousel documents should preserve assigned slot media");
    }
  }

  return EXIT_SUCCESS;
}
