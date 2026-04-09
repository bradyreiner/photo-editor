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
