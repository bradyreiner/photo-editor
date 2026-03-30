#include <cstdlib>
#include <iostream>

#include <QApplication>
#include <QColor>
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

  return EXIT_SUCCESS;
}
