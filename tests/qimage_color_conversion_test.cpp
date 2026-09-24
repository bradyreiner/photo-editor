#include "qimage_color_conversion.h"
#include "working_image.h"

#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>

#include <QColor>
#include <QColorSpace>
#include <QImage>
#include <QImageReader>
#include <QTemporaryDir>

namespace {

int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

}  // namespace

int main() {
  using photo_editor::desktop::QImageColorConversionPurpose;
  using photo_editor::desktop::convert_tagged_qimage_color_space;

  QImage source(2, 1, QImage::Format_RGBA8888);
  source.setColorSpace(QColorSpace(QColorSpace::DisplayP3));
  source.setPixelColor(
      0,
      0,
      QColor::fromRgbF(1.0F, 0.45F, 0.1F, 1.0F));
  source.setPixelColor(
      1,
      0,
      QColor::fromRgbF(0.1F, 0.65F, 0.95F, 0.5F));

  const auto srgb = QColorSpace(QColorSpace::SRgb);
  const auto output = convert_tagged_qimage_color_space(
      source,
      srgb,
      QImage::Format_RGBA8888,
      QImageColorConversionPurpose::OutputPublication);
  if (output.isNull() || output.colorSpace() != srgb ||
      output.format() != QImage::Format_RGBA8888 ||
      output.pixelColor(1, 0).alphaF() < 0.49) {
    return fail("explicit output publication should preserve format, profile, and alpha");
  }

  const auto monitor_output = convert_tagged_qimage_color_space(
      source,
      srgb,
      QImageColorConversionPurpose::MonitorPublication);
  if (monitor_output.isNull() || monitor_output.colorSpace() != srgb) {
    return fail("explicit monitor publication should produce the requested profile");
  }

  if (!convert_tagged_qimage_color_space(
           QImage{},
           srgb,
           QImageColorConversionPurpose::DecodeNormalization)
           .isNull() ||
      !convert_tagged_qimage_color_space(
           source,
           QColorSpace{},
           QImageColorConversionPurpose::ProofPublication)
           .isNull()) {
    return fail("invalid images and invalid target profiles must fail closed");
  }

  // A source color outside the working gamut needs signed channels. Round-trip
  // all built-in raster profiles through each desktop linear working space so
  // an input clamp cannot hide behind an otherwise correctly tagged image.
  const std::array source_spaces = {
      QColorSpace(QColorSpace::SRgb),
      QColorSpace(QColorSpace::DisplayP3),
      QColorSpace(QColorSpace::AdobeRgb),
      QColorSpace(QColorSpace::ProPhotoRgb),
      photo_editor::desktop::linear_srgb_color_space(),
      photo_editor::desktop::linear_display_p3_color_space(),
      photo_editor::desktop::linear_prophoto_color_space(),
      photo_editor::desktop::linear_rec2020_color_space(),
  };
  const std::array working_names = {
      QStringLiteral("Linear sRGB"),
      QStringLiteral("Linear Display P3"),
      QStringLiteral("Linear ProPhoto RGB"),
      QStringLiteral("Linear Rec.2020"),
  };
  constexpr std::array samples = {
      QRgbaFloat32{1.0F, 0.0F, 0.0F, 1.0F},
      QRgbaFloat32{0.0F, 1.0F, 0.0F, 0.5F},
      QRgbaFloat32{0.0F, 0.0F, 1.0F, 0.75F},
      QRgbaFloat32{0.18F, 0.18F, 0.18F, 1.0F},
  };
  for (const auto& source_space : source_spaces) {
    QImage tagged(static_cast<int>(samples.size()), 1, QImage::Format_RGBA32FPx4);
    tagged.setColorSpace(source_space);
    auto* row = reinterpret_cast<QRgbaFloat32*>(tagged.bits());
    std::copy(samples.begin(), samples.end(), row);
    for (const auto& working_name : working_names) {
      const auto working = photo_editor::desktop::working_image_from_qimage(
          tagged, working_name);
      if (working.isNull()) {
        return fail("every supported raster-to-working boundary must convert");
      }
      QImage packed(tagged.size(), QImage::Format_RGBA32FPx4);
      packed.setColorSpace(photo_editor::desktop::working_color_space(working_name));
      auto* packed_row = reinterpret_cast<QRgbaFloat32*>(packed.bits());
      for (std::size_t index = 0; index < samples.size(); ++index) {
        const auto& pixel = working.pixels[index];
        packed_row[index] = QRgbaFloat32{
            pixel.red, pixel.green, pixel.blue, pixel.alpha};
      }
      const auto restored = packed.convertedToColorSpace(
          source_space, QImage::Format_RGBA32FPx4);
      if (restored.isNull()) {
        return fail("working RGB must remain convertible to its source profile");
      }
      const auto* restored_row = reinterpret_cast<const QRgbaFloat32*>(restored.constBits());
      for (std::size_t index = 0; index < samples.size(); ++index) {
        const auto expected = samples[index];
        const auto actual = restored_row[index];
        // Half-float working conversion plus nonlinear source encoding can
        // amplify small errors near black. Compare in source linear light.
        QImage pair(2, 1, QImage::Format_RGBA32FPx4);
        pair.setColorSpace(source_space);
        auto* pair_row = reinterpret_cast<QRgbaFloat32*>(pair.bits());
        pair_row[0] = expected;
        pair_row[1] = actual;
        auto linear_space = source_space;
        linear_space.setTransferFunction(QColorSpace::TransferFunction::Linear);
        const auto linear = pair.convertedToColorSpace(linear_space);
        const auto* linear_row = reinterpret_cast<const QRgbaFloat32*>(linear.constBits());
        if (std::abs(linear_row[0].r - linear_row[1].r) > 0.003F ||
            std::abs(linear_row[0].g - linear_row[1].g) > 0.003F ||
            std::abs(linear_row[0].b - linear_row[1].b) > 0.003F ||
            std::abs(expected.a - actual.a) > 1.0e-6F) {
          std::cerr << "working profile=" << working_name.toStdString()
                    << " source=" << source_space.description().toStdString() << '\n';
          return fail("raster working conversion must preserve out-of-gamut color and alpha");
        }
      }
    }
  }

  QTemporaryDir fixtures;
  QImage jpeg_source(8, 8, QImage::Format_RGB32);
  jpeg_source.setColorSpace(QColorSpace(QColorSpace::DisplayP3));
  jpeg_source.fill(Qt::red);
  const auto jpeg_path = fixtures.filePath(QStringLiteral("p3-red.jpg"));
  if (!fixtures.isValid() || !jpeg_source.save(jpeg_path, "JPEG", 100)) {
    return fail("the wide-gamut JPEG regression fixture must be writable");
  }
  QImageReader reader(jpeg_path);
  const auto jpeg = reader.read();
  const auto jpeg_working = photo_editor::desktop::working_image_from_qimage(
      jpeg, QStringLiteral("Linear sRGB"));
  if (jpeg.colorSpace() != QColorSpace(QColorSpace::DisplayP3) ||
      jpeg_working.isNull() || jpeg_working.pixels.front().red < 1.1F ||
      jpeg_working.pixels.front().green > -0.03F ||
      jpeg_working.pixels.front().blue > -0.01F) {
    return fail("a P3 JPEG must retain its profile and signed wide-gamut working color");
  }

  for (const auto& working_name : working_names) {
    QImage signed_source(1, 1, QImage::Format_RGBA32FPx4);
    signed_source.setColorSpace(photo_editor::desktop::working_color_space(working_name));
    auto* pixel = reinterpret_cast<QRgbaFloat32*>(signed_source.bits());
    *pixel = QRgbaFloat32{-0.25F, 0.5F, 4.0F, 0.75F};
    const auto signed_working = photo_editor::desktop::working_image_from_qimage(
        signed_source, working_name);
    if (signed_working.isNull() || signed_working.pixels.front().red != -0.25F ||
        signed_working.pixels.front().blue != 4.0F ||
        signed_working.pixels.front().alpha != 0.75F) {
      return fail("signed HDR working samples must survive the raster boundary unchanged");
    }
  }

  return EXIT_SUCCESS;
}
