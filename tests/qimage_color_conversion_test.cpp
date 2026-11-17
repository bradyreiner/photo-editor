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

  QImage eight_bit_source(2, 1, QImage::Format_ARGB32);
  eight_bit_source.setColorSpace(srgb);
  eight_bit_source.setPixelColor(0, 0, QColor(119, 82, 43, 255));
  eight_bit_source.setPixelColor(1, 0, QColor(36, 157, 203, 255));
  auto float_source = eight_bit_source.convertToFormat(QImage::Format_RGBA32FPx4);
  float_source.setColorSpace(eight_bit_source.colorSpace());
  const auto eight_bit_working = photo_editor::desktop::working_image_from_qimage(
      eight_bit_source, QStringLiteral("Linear ProPhoto RGB"));
  const auto float_working = photo_editor::desktop::working_image_from_qimage(
      float_source, QStringLiteral("Linear ProPhoto RGB"));
  if (eight_bit_working.isNull() || float_working.isNull() ||
      eight_bit_working.pixels.size() != float_working.pixels.size()) {
    return fail("8-bit and float source samples should both reach the working profile");
  }
  for (std::size_t index = 0; index < eight_bit_working.pixels.size(); ++index) {
    const auto& eight_bit = eight_bit_working.pixels[index];
    const auto& floating = float_working.pixels[index];
    if (std::abs(eight_bit.red - floating.red) > 0.001F ||
        std::abs(eight_bit.green - floating.green) > 0.001F ||
        std::abs(eight_bit.blue - floating.blue) > 0.001F ||
        std::abs(eight_bit.alpha - floating.alpha) > 0.001F) {
      return fail("high-precision working conversion must not quantize 8-bit source colors");
    }
  }

  photo_editor::desktop::WorkingImage one_pixel_working_red;
  one_pixel_working_red.width = 1;
  one_pixel_working_red.height = 1;
  one_pixel_working_red.pixels.push_back({1.0F, 0.0F, 0.0F, 1.0F});
  const auto one_pixel_output =
      photo_editor::desktop::qimage_from_working_image(
          one_pixel_working_red,
          srgb,
          QImage::Format_ARGB32,
          QStringLiteral("Linear sRGB"));
  if (one_pixel_output.isNull() ||
      one_pixel_output.format() != QImage::Format_ARGB32) {
    return fail("one-pixel ARGB32 publication should return the requested image format");
  }
  const auto one_pixel_published_red = one_pixel_output.pixelColor(0, 0);
  if (one_pixel_published_red.red() < 250 ||
      one_pixel_published_red.green() > 5 ||
      one_pixel_published_red.blue() > 5) {
    return fail("one-pixel ARGB32 publication should preserve a linear red sample");
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
      photo_editor::desktop::linear_adobe_rgb_color_space(),
      photo_editor::desktop::linear_acescg_color_space(),
      photo_editor::desktop::linear_aces2065_1_color_space(),
  };
  const std::array working_names = {
      QStringLiteral("Linear sRGB"),
      QStringLiteral("Linear Display P3"),
      QStringLiteral("Linear ProPhoto RGB"),
      QStringLiteral("Linear Rec.2020"),
      QStringLiteral("Linear Adobe RGB"),
      QStringLiteral("ACEScg"),
      QStringLiteral("ACES2065-1"),
  };
  struct WorkingAliasCase final {
    QString alias;
    QString canonical_name;
    photo_editor::pipeline::ColorPrimaries primaries;
    photo_editor::pipeline::WorkingTransformKind kind;
  };
  const std::array working_aliases = {
      WorkingAliasCase{
          QStringLiteral("Linear Adobe RGB"), QStringLiteral("Linear Adobe RGB"),
          photo_editor::pipeline::ColorPrimaries::AdobeRgb,
          photo_editor::pipeline::WorkingTransformKind::LinearAdobeRgb},
      WorkingAliasCase{
          QStringLiteral("linear-adobe-rgb"), QStringLiteral("Linear Adobe RGB"),
          photo_editor::pipeline::ColorPrimaries::AdobeRgb,
          photo_editor::pipeline::WorkingTransformKind::LinearAdobeRgb},
      WorkingAliasCase{
          QStringLiteral("AdobeRGBLinear"), QStringLiteral("Linear Adobe RGB"),
          photo_editor::pipeline::ColorPrimaries::AdobeRgb,
          photo_editor::pipeline::WorkingTransformKind::LinearAdobeRgb},
      WorkingAliasCase{
          QStringLiteral("ACEScg"), QStringLiteral("ACEScg"),
          photo_editor::pipeline::ColorPrimaries::AcesAp1,
          photo_editor::pipeline::WorkingTransformKind::LinearAcesCg},
      WorkingAliasCase{
          QStringLiteral("ACEScg/AP1"), QStringLiteral("ACEScg"),
          photo_editor::pipeline::ColorPrimaries::AcesAp1,
          photo_editor::pipeline::WorkingTransformKind::LinearAcesCg},
      WorkingAliasCase{
          QStringLiteral("linearacesap1"), QStringLiteral("ACEScg"),
          photo_editor::pipeline::ColorPrimaries::AcesAp1,
          photo_editor::pipeline::WorkingTransformKind::LinearAcesCg},
      WorkingAliasCase{
          QStringLiteral("ACES2065-1"), QStringLiteral("ACES2065-1"),
          photo_editor::pipeline::ColorPrimaries::AcesAp0,
          photo_editor::pipeline::WorkingTransformKind::LinearAces2065_1},
      WorkingAliasCase{
          QStringLiteral("ACES2065-1/AP0"), QStringLiteral("ACES2065-1"),
          photo_editor::pipeline::ColorPrimaries::AcesAp0,
          photo_editor::pipeline::WorkingTransformKind::LinearAces2065_1},
      WorkingAliasCase{
          QStringLiteral("ACES2065_1"), QStringLiteral("ACES2065-1"),
          photo_editor::pipeline::ColorPrimaries::AcesAp0,
          photo_editor::pipeline::WorkingTransformKind::LinearAces2065_1},
      WorkingAliasCase{
          QStringLiteral("linearacesap0"), QStringLiteral("ACES2065-1"),
          photo_editor::pipeline::ColorPrimaries::AcesAp0,
          photo_editor::pipeline::WorkingTransformKind::LinearAces2065_1},
  };
  for (const auto& alias_case : working_aliases) {
    const auto engine_profile =
        photo_editor::pipeline::working_color_profile_from_name(
            alias_case.alias.toStdString());
    const auto alias_space =
        photo_editor::desktop::working_color_space(alias_case.alias);
    const auto canonical_space =
        photo_editor::desktop::working_color_space(alias_case.canonical_name);
    const auto prophoto_fallback = photo_editor::desktop::linear_prophoto_color_space();
    if (engine_profile.profile_name != alias_case.canonical_name.toStdString() ||
        engine_profile.kind != alias_case.kind ||
        engine_profile.color_space.primaries != alias_case.primaries ||
        engine_profile.color_space.transfer_function !=
            photo_editor::pipeline::TransferFunction::Linear ||
        !alias_space.isValid() || alias_space != canonical_space ||
        alias_space == prophoto_fallback) {
      std::cerr << "working-profile alias=" << alias_case.alias.toStdString()
                << " normalized=" << engine_profile.profile_name
                << " primaries=" << static_cast<int>(engine_profile.color_space.primaries)
                << '\n';
      return fail("Qt working-color aliases must resolve to the engine profile primaries instead of falling back to ProPhoto");
    }

    QImage tagged(1, 1, QImage::Format_RGBA32FPx4);
    tagged.setColorSpace(canonical_space);
    auto* tagged_pixel = reinterpret_cast<QRgbaFloat32*>(tagged.bits());
    *tagged_pixel = QRgbaFloat32{0.1875F, 0.4375F, 0.8125F, 0.625F};
    const auto converted = photo_editor::desktop::working_image_from_qimage(
        tagged, alias_case.alias);
    if (converted.isNull() || converted.pixels.size() != 1U ||
        std::abs(converted.pixels.front().red - 0.1875F) > 1.0e-4F ||
        std::abs(converted.pixels.front().green - 0.4375F) > 1.0e-4F ||
        std::abs(converted.pixels.front().blue - 0.8125F) > 1.0e-4F ||
        std::abs(converted.pixels.front().alpha - 0.625F) > 1.0e-6F) {
      return fail("QImage conversion must retain RGB values and alpha when an alias names the same working primaries");
    }
  }
  if (photo_editor::desktop::working_color_space(QStringLiteral("sRGB")) !=
          QColorSpace(QColorSpace::SRgb) ||
      photo_editor::desktop::working_color_space(QStringLiteral("Display P3")) !=
          QColorSpace(QColorSpace::DisplayP3)) {
    return fail("display-referred sRGB and Display P3 arguments to working_color_space must retain their display-space meaning");
  }
  constexpr std::array samples = {
      QRgbaFloat32{1.0F, 0.0F, 0.0F, 1.0F},
      QRgbaFloat32{0.0F, 1.0F, 0.0F, 0.5F},
      QRgbaFloat32{0.0F, 0.0F, 1.0F, 0.75F},
      QRgbaFloat32{0.18F, 0.18F, 0.18F, 1.0F},
  };
  for (const auto& source_space : source_spaces) {
    if (!source_space.isValid()) {
      return fail("every declared source profile must be valid in the Qt raster boundary");
    }
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
      const auto restored = convert_tagged_qimage_color_space(
          packed, source_space, QImage::Format_RGBA32FPx4,
          QImageColorConversionPurpose::EvidenceMeasurement);
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

  // Published output must retain both its exact profile tag and 16-bit
  // precision for every working-space profile accepted by the editor.
  for (const auto& working_name : working_names) {
    const auto target = photo_editor::desktop::working_color_space(working_name);
    if (!target.isValid()) {
      return fail("every supported working profile must provide a valid Qt color-space tag");
    }
    const auto tagged_16_bit = convert_tagged_qimage_color_space(
        source,
        target,
        QImage::Format_RGBA64,
        QImageColorConversionPurpose::OutputPublication);
    if (tagged_16_bit.isNull() || tagged_16_bit.format() != QImage::Format_RGBA64 ||
        tagged_16_bit.colorSpace() != target ||
        std::abs(tagged_16_bit.pixelColor(1, 0).alphaF() -
                 source.pixelColor(1, 0).alphaF()) > 1.0 / 65535.0) {
      std::cerr << "16-bit output profile=" << working_name.toStdString() << '\n';
      return fail("16-bit output must preserve the selected wide-gamut profile tag and alpha");
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
