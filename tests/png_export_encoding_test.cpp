#include "export_image_encoder.h"
#include "photo_export_task.h"

#include <QCoreApplication>
#include <QTemporaryDir>
#include <iostream>

namespace d = photo_editor::desktop;

namespace {
bool same_output(const QImage& expected, const QImage& actual) {
  if (expected.isNull() || actual.isNull() || expected.size() != actual.size() ||
      expected.depth() != actual.depth() || expected.colorSpace() != actual.colorSpace() ||
      expected.colorSpace().iccProfile() != actual.colorSpace().iccProfile() ||
      expected.dotsPerMeterX() != actual.dotsPerMeterX() ||
      expected.dotsPerMeterY() != actual.dotsPerMeterY() || expected.offset() != actual.offset() ||
      expected.textKeys() != actual.textKeys() ||
      expected.convertToFormat(QImage::Format_RGBA64) != actual.convertToFormat(QImage::Format_RGBA64))
    return false;
  for (const auto& key : expected.textKeys())
    if (expected.text(key) != actual.text(key)) return false;
  return true;
}

QImage reference_png(const QImage& image, const QString& description = {}) {
  QByteArray bytes;
  QBuffer buffer(&bytes);
  if (!buffer.open(QIODevice::WriteOnly)) return {};
  QImageWriter writer(&buffer, "png");
  if (!description.isNull()) writer.setText("Description", description);
  if (!writer.write(image)) return {};
  return QImage::fromData(bytes, "PNG");
}
}

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  QTemporaryDir directory;
  if (!directory.isValid()) return 1;
  // Flattening for delivery must preserve print resolution and image metadata,
  // and operate on every source pixel regardless of its display pixel ratio.
  for (const bool scene_linear : {false, true}) {
    QImage image(32, 24, QImage::Format_RGBA64);
    image.fill(QColor::fromRgba64(0, 0, 0, 32768));
    image.setColorSpace(QColorSpace(QColorSpace::SRgb));
    image.setDotsPerMeterX(11811);
    image.setDotsPerMeterY(5906);
    image.setOffset(QPoint(4, 7));
    image.setText("Author", "Delivery metadata");
    image.setDevicePixelRatio(2.0);
    auto settings = d::export_image_settings_for_preset(QStringLiteral("JPEG"));
    settings.scene_linear = scene_linear;
    const auto prepared = d::prepare_image_for_export_encoding(image, settings);
    if (prepared.dotsPerMeterX() != image.dotsPerMeterX() ||
        prepared.dotsPerMeterY() != image.dotsPerMeterY() ||
        prepared.offset() != image.offset() || prepared.text("Author") != image.text("Author") ||
        prepared.colorSpace() != image.colorSpace() ||
        prepared.devicePixelRatio() != image.devicePixelRatio()) {
      std::cerr << "flattened export lost image metadata: scene_linear=" << scene_linear << '\n';
      return 4;
    }
    const auto top_left = prepared.pixelColor(0, 0);
    const auto bottom_right = prepared.pixelColor(prepared.width() - 1, prepared.height() - 1);
    if (top_left != bottom_right || bottom_right.alpha() != 255 ||
        bottom_right.red() < 126 || bottom_right.red() > 129) {
      std::cerr << "flattened export did not composite every physical source pixel\n";
      return 5;
    }
    if (!scene_linear) {
      const auto encoded = d::encode_prepared_standard_export_image(prepared, settings, "JPEG delivery");
      const auto decoded = QImage::fromData(encoded.bytes, "JPEG");
      const auto path = directory.filePath("flattened.jpg");
      auto control = std::make_shared<d::PhotoExportControl>();
      const auto saved = d::run_photo_export([image] { return image; }, path, "jpg", control);
      const auto reopened = QImage(path);
      for (const auto& output : {decoded, reopened}) {
        if (output.isNull() || output.size() != image.size() ||
            // JPEG records integer DPI; permit its half-DPI quantization.
            qAbs(output.dotsPerMeterX() - image.dotsPerMeterX()) > 20 ||
            qAbs(output.dotsPerMeterY() - image.dotsPerMeterY()) > 20 ||
            output.colorSpace() != image.colorSpace() || output.text("Author") != image.text("Author")) {
          std::cerr << "JPEG delivery did not retain print resolution, profile and author: "
                    << output.dotsPerMeterX() << '/' << output.dotsPerMeterY()
                    << " profile=" << (output.colorSpace() == image.colorSpace())
                    << " author=" << output.text("Author").toStdString() << '\n';
          return 6;
        }
      }
      if (!encoded.valid || !saved.success) return 7;
    }
  }
  int cases = 0;
  for (const auto format : {QImage::Format_ARGB32, QImage::Format_RGB888,
           QImage::Format_RGBA8888_Premultiplied, QImage::Format_RGBA64,
           QImage::Format_RGBX64, QImage::Format_Grayscale8, QImage::Format_Grayscale16}) {
    const bool grayscale = format == QImage::Format_Grayscale8 || format == QImage::Format_Grayscale16;
    for (const auto profile : {QColorSpace::SRgb, QColorSpace::DisplayP3}) {
      if (grayscale && profile != QColorSpace::SRgb) continue;
      for (int pattern = 0; pattern < 4; ++pattern) {
        QImage image(129, 97, format);
        // RGB ICC profiles are invalid in grayscale PNG; cover unprofiled gray separately.
        if (!grayscale) image.setColorSpace(QColorSpace(profile));
        image.setDotsPerMeterX(11811); image.setDotsPerMeterY(5906);
        image.setOffset(QPoint(4, 7));
        image.setText("Description", "Original description");
        image.setText("Author", "Encoding contract");
        for (int y = 0; y < image.height(); ++y) {
          for (int x = 0; x < image.width(); ++x) {
            const auto v = static_cast<quint16>(pattern == 0 ? 32768 : pattern == 1
                ? x * 65535 / (image.width() - 1) : pattern == 2
                ? ((x + y) % 17 == 0 ? 65535 : 0) : (x * 4357 + y * 7919) % 65536);
            const auto alpha = static_cast<quint16>((x + y) % 5 * 65535 / 4);
            image.setPixelColor(x, y, QColor::fromRgba64(v, static_cast<quint16>(65535 - v),
                static_cast<quint16>(v / 2), alpha));
          }
        }
        d::ExportImageSettings settings;
        settings.writer_format = "png"; settings.image_format = format;
        const auto prepared = d::prepare_image_for_export_encoding(image, settings);
        const auto encoded = d::encode_prepared_standard_export_image(prepared, settings, "Export description");
        if (!encoded.valid || !same_output(reference_png(prepared, "Export description"),
            QImage::fromData(encoded.bytes, "PNG"))) {
          std::cerr << "queued PNG encoding mismatch: " << static_cast<int>(format) << '/' << pattern << '\n';
          return 2;
        }
        const auto path = directory.filePath("export.png");
        auto control = std::make_shared<d::PhotoExportControl>();
        const auto result = d::run_photo_export([image] { return image; }, path, "png", control);
        if (!result.success || control->phase.load() != d::PhotoExportControl::Phase::Finished ||
            !same_output(reference_png(image), QImage(path))) {
          std::cerr << "atomic PNG encoding mismatch: " << static_cast<int>(format) << '/' << pattern << '\n';
          return 3;
        }
        ++cases;
      }
    }
  }
  std::cout << "PNG cases=" << cases << " queued_and_atomic_pixels_metadata_exact=1\n";
  return 0;
}
