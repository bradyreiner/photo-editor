#include <cstdlib>
#include <iostream>

#include <QApplication>
#include <QColor>
#include <QDir>
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

QImage horizontal_bands(const QSize& size, const QColor& left, const QColor& right) {
  QImage image(size, QImage::Format_ARGB32);
  image.fill(Qt::black);
  for (int y = 0; y < size.height(); ++y) {
    for (int x = 0; x < size.width(); ++x) {
      image.setPixelColor(x, y, x < (size.width() / 2) ? left : right);
    }
  }
  return image;
}

QImage vertical_bands(const QSize& size, const QColor& top, const QColor& middle, const QColor& bottom) {
  QImage image(size, QImage::Format_ARGB32);
  image.fill(Qt::black);
  const int band_height = size.height() / 3;
  for (int y = 0; y < size.height(); ++y) {
    const QColor color = y < band_height ? top : y < band_height * 2 ? middle : bottom;
    for (int x = 0; x < size.width(); ++x) {
      image.setPixelColor(x, y, color);
    }
  }
  return image;
}

bool color_matches(const QColor& actual, const QColor& expected) {
  return actual.red() == expected.red() && actual.green() == expected.green() &&
         actual.blue() == expected.blue();
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
    const auto source_path = write_image(
        dir,
        QStringLiteral("linked-horizontal.png"),
        horizontal_bands(QSize(2160, 1350), QColor(QStringLiteral("#ff2d55")), QColor(QStringLiteral("#1e90ff"))));
    if (source_path.isEmpty()) {
      return fail("linked horizontal source image should save");
    }

    controller.assignPrimaryImage(source_path, QStringLiteral("pair"));
    controller.applyTemplateToCurrent(QStringLiteral("linked-horizontal-pair"));

    const auto export_dir_path = dir.filePath(QStringLiteral("export-horizontal"));
    QDir().mkpath(export_dir_path);
    if (!controller.exportCarouselToDirectory(export_dir_path)) {
      return fail("linked horizontal export should succeed");
    }

    const QImage first_slide(QDir(export_dir_path).filePath(QStringLiteral("carousel-01.png")));
    const QImage second_slide(QDir(export_dir_path).filePath(QStringLiteral("carousel-02.png")));
    if (first_slide.size() != QSize(1080, 1350) || second_slide.size() != QSize(1080, 1350)) {
      return fail("horizontal linked export should use the 4x5 Instagram size");
    }
    if (!color_matches(first_slide.pixelColor(first_slide.width() / 2, first_slide.height() / 2),
                       QColor(QStringLiteral("#ff2d55"))) ||
        !color_matches(second_slide.pixelColor(second_slide.width() / 2, second_slide.height() / 2),
                       QColor(QStringLiteral("#1e90ff")))) {
      return fail("horizontal linked export should partition the source without seams");
    }

    controller.addSlide();
    controller.selectSlide(2);
    const auto trailing_path = write_image(
        dir,
        QStringLiteral("trailing-slide.png"),
        horizontal_bands(QSize(1080, 1350), QColor(QStringLiteral("#ffffff")), QColor(QStringLiteral("#ffffff"))));
    if (trailing_path.isEmpty()) {
      return fail("trailing slide image should save");
    }
    controller.assignPrimaryImage(trailing_path, QStringLiteral("trailing"));
    controller.selectSlide(0);
    controller.applyTemplateToCurrent(QStringLiteral("linked-horizontal-pair"));

    const auto slides_after_insert = controller.slides();
    if (controller.slideCount() != 3) {
      return fail("rebuilding a linked pair should preserve following slides instead of overwriting them");
    }
    if (slides_after_insert.at(2).toMap().value(QStringLiteral("label")).toString() != QStringLiteral("trailing")) {
      return fail("rebuilding a linked pair should preserve the slide that was already after it");
    }

    controller.clearCurrentGroup();
    if (controller.slideCount() != 2) {
      return fail("clearing a linked pair should collapse it back to one slide");
    }
    const auto slides_after_collapse = controller.slides();
    if (slides_after_collapse.at(1).toMap().value(QStringLiteral("label")).toString() != QStringLiteral("trailing")) {
      return fail("collapsing a linked pair should preserve trailing slides");
    }
  }

  {
    CarouselDocumentController controller;
    controller.setDocumentFormat(QStringLiteral("5x4"));
    const auto source_path = write_image(
        dir,
        QStringLiteral("stacked-triptych.png"),
        vertical_bands(
            QSize(1350, 3240),
            QColor(QStringLiteral("#34c759")),
            QColor(QStringLiteral("#ffd60a")),
            QColor(QStringLiteral("#bf5af2"))));
    if (source_path.isEmpty()) {
      return fail("triptych source image should save");
    }

    controller.assignPrimaryImage(source_path, QStringLiteral("stack"));
    controller.applyTemplateToCurrent(QStringLiteral("linked-vertical-triptych"));

    const auto export_dir_path = dir.filePath(QStringLiteral("export-triptych"));
    QDir().mkpath(export_dir_path);
    if (!controller.exportCarouselToDirectory(export_dir_path)) {
      return fail("triptych export should succeed");
    }

    const QImage top_slide(QDir(export_dir_path).filePath(QStringLiteral("carousel-01.png")));
    const QImage middle_slide(QDir(export_dir_path).filePath(QStringLiteral("carousel-02.png")));
    const QImage bottom_slide(QDir(export_dir_path).filePath(QStringLiteral("carousel-03.png")));
    if (top_slide.size() != QSize(1350, 1080) || middle_slide.size() != QSize(1350, 1080) ||
        bottom_slide.size() != QSize(1350, 1080)) {
      return fail("triptych export should use the 5x4 Instagram size");
    }
    if (!color_matches(top_slide.pixelColor(top_slide.width() / 2, top_slide.height() / 2), QColor(QStringLiteral("#34c759"))) ||
        !color_matches(middle_slide.pixelColor(middle_slide.width() / 2, middle_slide.height() / 2), QColor(QStringLiteral("#ffd60a"))) ||
        !color_matches(bottom_slide.pixelColor(bottom_slide.width() / 2, bottom_slide.height() / 2), QColor(QStringLiteral("#bf5af2")))) {
      return fail("triptych export should preserve the top, middle, and bottom slices in order");
    }
  }

  {
    CarouselDocumentController controller;
    controller.setDocumentFormat(QStringLiteral("5x4"));
    const auto left_path = write_image(
        dir,
        QStringLiteral("left-half.png"),
        QImage(QSize(675, 1080), QImage::Format_ARGB32));
    const auto right_path = write_image(
        dir,
        QStringLiteral("right-half.png"),
        QImage(QSize(675, 1080), QImage::Format_ARGB32));
    if (left_path.isEmpty() || right_path.isEmpty()) {
      return fail("split-pair source images should save");
    }

    QImage left_image(left_path);
    left_image.fill(QColor(QStringLiteral("#ff453a")));
    if (!left_image.save(left_path)) {
      return fail("left split image should save");
    }

    QImage right_image(right_path);
    right_image.fill(QColor(QStringLiteral("#0a84ff")));
    if (!right_image.save(right_path)) {
      return fail("right split image should save");
    }

    controller.applyTemplateToCurrent(QStringLiteral("split-pair"));
    controller.assignPrimaryImage(left_path, QStringLiteral("left"));
    controller.assignSecondaryImage(right_path, QStringLiteral("right"));

    const auto export_dir_path = dir.filePath(QStringLiteral("export-split-pair"));
    QDir().mkpath(export_dir_path);
    if (!controller.exportCarouselToDirectory(export_dir_path)) {
      return fail("split-pair export should succeed");
    }

    const QImage slide(QDir(export_dir_path).filePath(QStringLiteral("carousel-01.png")));
    if (slide.size() != QSize(1350, 1080)) {
      return fail("split-pair export should use the 5x4 Instagram size");
    }
    if (!color_matches(slide.pixelColor(slide.width() / 4, slide.height() / 2), QColor(QStringLiteral("#ff453a"))) ||
        !color_matches(slide.pixelColor((slide.width() * 3) / 4, slide.height() / 2), QColor(QStringLiteral("#0a84ff")))) {
      return fail("split-pair export should divide the canvas exactly in half");
    }
  }

  return EXIT_SUCCESS;
}
