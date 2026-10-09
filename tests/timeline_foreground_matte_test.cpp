#include "timeline_foreground_matte.h"

#include <QColor>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QTemporaryDir>
#include <QVariantMap>
#include <QVector>

#include <cmath>
#include <iostream>
#include <string_view>

using namespace photo_editor::desktop;

namespace {

int failures = 0;

void check(bool condition, std::string_view label) {
  if (condition) {
    return;
  }
  std::cerr << "FAIL: " << label << '\n';
  ++failures;
}

QVariantMap metadata_for(const QString& directory) {
  return {
      {QStringLiteral("enabled"), true},
      {QStringLiteral("directory"), directory},
      {QStringLiteral("fps"), 2.0},
      {QStringLiteral("trimStart"), 3.0},
      {QStringLiteral("trimEnd"), 4.5},
      {QStringLiteral("sourceSignature"), QStringLiteral("source-v1")},
      {QStringLiteral("frameCount"), 3},
  };
}

bool write_matte(
    const QString& directory,
    int index,
    const QVector<int>& alpha_values,
    int width,
    int height) {
  QImage image(width, height, QImage::Format_ARGB32);
  if (image.isNull() || alpha_values.size() != width * height) {
    return false;
  }
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const auto alpha = alpha_values.at(y * width + x);
      // The production matte encodes coverage in alpha; white RGB must not
      // affect extraction.
      image.setPixel(x, y, qRgba(255, 255, 255, alpha));
    }
  }
  return image.save(QDir(directory).filePath(
      QStringLiteral("mask_%1.png").arg(index, 6, 10, QLatin1Char('0'))));
}

void test_ready_and_split_trims(const QString& directory) {
  const auto metadata = metadata_for(directory);
  check(timelineForegroundCutoutReady(
            metadata, QStringLiteral("source-v1"), 3.0, 4.5),
        "full analyzed trim is ready");
  check(timelineForegroundCutoutReady(
            metadata, QStringLiteral("source-v1"), 3.5, 4.0),
        "split clip trim inside analyzed coverage is ready");
  check(!timelineForegroundCutoutReady(
            metadata, QStringLiteral("source-v2"), 3.0, 4.5),
        "mismatched source signature is stale");
  check(!timelineForegroundCutoutReady(
            metadata, QStringLiteral("source-v1"), 2.99, 4.0),
        "trim starting before analyzed coverage is rejected");
  check(!timelineForegroundCutoutReady(
            metadata, QStringLiteral("source-v1"), 3.2, 4.51),
        "trim ending after analyzed coverage is rejected");
  check(!timelineForegroundCutoutReady(
            metadata, QStringLiteral("source-v1"), 4.0, 4.0),
        "empty trim is rejected");
}

void test_index_and_missing_frame(const QString& directory) {
  auto metadata = metadata_for(directory);
  check(write_matte(directory, 0, {255}, 1, 1), "write first test matte");
  check(write_matte(directory, 1, {128}, 1, 1), "write second test matte");
  check(write_matte(directory, 2, {0}, 1, 1), "write third test matte");

  QString error;
  const auto first = timelineForegroundMaskPath(
      metadata, QStringLiteral("source-v1"), 3.0, &error);
  check(first.endsWith(QStringLiteral("mask_000000.png")),
        "nonzero source trim maps to mask index zero");
  const auto second = timelineForegroundMaskPath(
      metadata, QStringLiteral("source-v1"), 3.5, &error);
  check(second.endsWith(QStringLiteral("mask_000001.png")),
        "source-time index uses floor relative to nonzero trim");
  const auto third = timelineForegroundMaskPath(
      metadata, QStringLiteral("source-v1"), 4.49, &error);
  check(third.endsWith(QStringLiteral("mask_000002.png")),
        "last covered source time maps to final mask index");

  QFile::remove(QDir(directory).filePath(QStringLiteral("mask_000001.png")));
  error.clear();
  check(timelineForegroundMaskPath(
            metadata, QStringLiteral("source-v1"), 3.5, &error).isEmpty(),
        "missing requested frame mask fails closed");
  check(!error.isEmpty(), "missing requested frame reports a reason");
  error.clear();
  check(timelineForegroundMaskPath(
            metadata, QStringLiteral("source-v1"), 4.5, &error).isEmpty(),
        "time at exclusive analyzed end has no coverage");
}

QImage source_image(QImage::Format format) {
  QImage image(1, 1, format);
  image.setPixelColor(0, 0, QColor(200, 100, 50, 128));
  return image;
}

void test_alpha_multiply(const QString& directory) {
  check(write_matte(directory, 10, {128}, 1, 1),
        "write white RGB matte with half alpha");
  const auto path = QDir(directory).filePath(QStringLiteral("mask_000010.png"));
  for (const auto format : {QImage::Format_ARGB32,
                            QImage::Format_ARGB32_Premultiplied}) {
    auto image = source_image(format);
    QString error;
    check(applyTimelineForegroundMask(&image, path, &error),
          "apply alpha-channel matte to straight and premultiplied source");
    check(image.format() == QImage::Format_ARGB32_Premultiplied,
          "matte result is premultiplied for compositing");
    const auto pixel = image.pixel(0, 0);
    check(qAbs(qAlpha(pixel) - 64) <= 1,
          "source alpha is multiplied by matte alpha");
    check(qAbs(qRed(pixel) - 50) <= 2 && qAbs(qGreen(pixel) - 25) <= 2 &&
              qAbs(qBlue(pixel) - 12) <= 2,
          "premultiplied RGB is multiplied by the same matte coverage");
    const auto unpremultiplied = image.pixelColor(0, 0);
    check(qAbs(unpremultiplied.red() - 200) <= 4 &&
              qAbs(unpremultiplied.green() - 100) <= 4 &&
              qAbs(unpremultiplied.blue() - 50) <= 4,
          "straight color is preserved through alpha multiplication");
  }
}

void test_resize_and_invalid_metadata(const QString& directory) {
  check(write_matte(directory, 11, {0, 255}, 2, 1),
        "write two-pixel resize matte");
  QImage resized_source(4, 1, QImage::Format_ARGB32);
  resized_source.fill(Qt::white);
  QString error;
  check(applyTimelineForegroundMask(
            &resized_source,
            QDir(directory).filePath(QStringLiteral("mask_000011.png")), &error),
        "analysis matte resizes to the decoded frame");
  const auto alpha_left = qAlpha(resized_source.pixel(0, 0));
  const auto alpha_middle = qAlpha(resized_source.pixel(1, 0));
  const auto alpha_right = qAlpha(resized_source.pixel(3, 0));
  check(alpha_left == 0 && alpha_right == 255 &&
            alpha_middle > 0 && alpha_middle < 255,
        "smooth matte resize preserves endpoints and interpolates the edge");

  auto malformed = metadata_for(directory);
  malformed.insert(QStringLiteral("fps"), 0.0);
  check(!timelineForegroundCutoutReady(
            malformed, QStringLiteral("source-v1"), 3.0, 4.0),
        "zero fps metadata is rejected");
  malformed = metadata_for(directory);
  malformed.remove(QStringLiteral("frameCount"));
  check(!timelineForegroundCutoutReady(
            malformed, QStringLiteral("source-v1"), 3.0, 4.0),
        "missing frame count is rejected");
  malformed = metadata_for(directory);
  malformed.insert(QStringLiteral("trimEnd"), 3.0);
  check(!timelineForegroundCutoutReady(
            malformed, QStringLiteral("source-v1"), 3.0, 3.0),
        "inverted analyzed range is rejected");
  malformed = metadata_for(directory);
  malformed.insert(QStringLiteral("fps"), 61.0);
  check(!timelineForegroundCutoutReady(
            malformed, QStringLiteral("source-v1"), 3.0, 4.0),
        "analysis fps above the runtime cap is rejected");
  malformed = metadata_for(directory);
  malformed.insert(QStringLiteral("frameCount"), 901);
  check(!timelineForegroundCutoutReady(
            malformed, QStringLiteral("source-v1"), 3.0, 4.0),
        "frame count above the runtime cap is rejected");
  malformed = metadata_for(directory);
  malformed.insert(QStringLiteral("frameCount"), 2);
  check(!timelineForegroundCutoutReady(
            malformed, QStringLiteral("source-v1"), 3.0, 4.0),
        "frame count inconsistent with analyzed duration is rejected");
  malformed = metadata_for(directory);
  malformed.insert(QStringLiteral("trimStart"), -1.0);
  check(!timelineForegroundCutoutReady(
            malformed, QStringLiteral("source-v1"), -1.0, 0.5),
        "negative source trim metadata is rejected");
  malformed = metadata_for(QDir(directory).filePath(QStringLiteral("missing")));
  check(!timelineForegroundCutoutReady(
            malformed, QStringLiteral("source-v1"), 3.0, 4.0),
        "unavailable matte directory is rejected");
}

}  // namespace

int main() {
  QTemporaryDir directory;
  if (!directory.isValid()) {
    std::cerr << "Could not create temporary matte directory.\n";
    return 1;
  }
  test_ready_and_split_trims(directory.path());
  test_index_and_missing_frame(directory.path());
  test_alpha_multiply(directory.path());
  test_resize_and_invalid_metadata(directory.path());
  if (failures != 0) {
    std::cerr << failures << " foreground matte check(s) failed.\n";
    return 1;
  }
  std::cout << "Timeline foreground matte checks passed.\n";
  return 0;
}
