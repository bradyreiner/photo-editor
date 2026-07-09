#pragma once

#include <QByteArray>
#include <QImage>
#include <QJsonObject>
#include <QString>

#include <initializer_list>

namespace photo_editor::desktop {

struct ExportImageSettings final {
  int max_edge = 0;
  int jpeg_quality = 95;
  QString format_name = QStringLiteral("PNG");
  int bit_depth = 16;
  bool flatten_alpha = false;
  bool embed_profile = true;
  bool scene_linear = false;
  bool preserves_over_range = false;
  QByteArray writer_format = "png";
  QImage::Format image_format = QImage::Format_RGBA64;
};

[[nodiscard]] inline ExportImageSettings export_image_settings_for_preset(const QString& preset) {
  const auto normalized = preset.trimmed();
  const auto lower = normalized.toLower();
  const auto contains_any = [&](std::initializer_list<QString> needles) {
    for (const auto& needle : needles) {
      if (lower.contains(needle)) {
        return true;
      }
    }
    return false;
  };
  const auto requests_8_bit =
      lower.contains(QStringLiteral("8-bit")) ||
      lower.contains(QStringLiteral("8 bit")) ||
      lower.contains(QStringLiteral("8bpc"));
  const auto requests_16_bit =
      lower.contains(QStringLiteral("16-bit")) ||
      lower.contains(QStringLiteral("16 bit")) ||
      lower.contains(QStringLiteral("16bpc"));

  if (contains_any({QStringLiteral("pfm")}) ||
      normalized.compare(QStringLiteral("Scene Linear"), Qt::CaseInsensitive) == 0 ||
      normalized.compare(QStringLiteral("Scene Linear PFM"), Qt::CaseInsensitive) == 0) {
    return {
        .max_edge = 0,
        .jpeg_quality = 100,
        .format_name = QStringLiteral("PFM"),
        .bit_depth = 32,
        .flatten_alpha = false,
        .embed_profile = false,
        .scene_linear = true,
        .preserves_over_range = true,
        .writer_format = "pfm",
        .image_format = QImage::Format_RGBA32FPx4,
    };
  }
  if (contains_any({QStringLiteral("jpeg"), QStringLiteral("jpg")})) {
    return {
        .max_edge = 0,
        .jpeg_quality = 95,
        .format_name = QStringLiteral("JPEG"),
        .bit_depth = 8,
        .flatten_alpha = true,
        .embed_profile = true,
        .writer_format = "jpg",
        .image_format = QImage::Format_ARGB32,
    };
  }
  if (contains_any({QStringLiteral("tiff"), QStringLiteral("tif")})) {
    const auto bit_depth = requests_8_bit && !requests_16_bit ? 8 : 16;
    return {
        .max_edge = 0,
        .jpeg_quality = 100,
        .format_name = QStringLiteral("TIFF"),
        .bit_depth = bit_depth,
        .flatten_alpha = false,
        .embed_profile = true,
        .writer_format = "tif",
        .image_format = bit_depth == 8 ? QImage::Format_RGBA8888 : QImage::Format_RGBA64,
    };
  }
  if (contains_any({QStringLiteral("png")})) {
    const auto bit_depth = requests_8_bit && !requests_16_bit ? 8 : 16;
    return {
        .max_edge = 0,
        .jpeg_quality = 100,
        .format_name = QStringLiteral("PNG"),
        .bit_depth = bit_depth,
        .flatten_alpha = false,
        .embed_profile = true,
        .writer_format = "png",
        .image_format = bit_depth == 8 ? QImage::Format_RGBA8888 : QImage::Format_RGBA64,
    };
  }
  if (normalized.compare(QStringLiteral("Quick Web"), Qt::CaseInsensitive) == 0) {
    return {
        .max_edge = 1440,
        .jpeg_quality = 88,
        .format_name = QStringLiteral("JPEG"),
        .bit_depth = 8,
        .flatten_alpha = true,
        .embed_profile = true,
        .writer_format = "jpg",
        .image_format = QImage::Format_ARGB32,
    };
  }
  if (normalized.compare(QStringLiteral("Client Preview"), Qt::CaseInsensitive) == 0) {
    return {
        .max_edge = 2048,
        .jpeg_quality = 92,
        .format_name = QStringLiteral("JPEG"),
        .bit_depth = 8,
        .flatten_alpha = true,
        .embed_profile = true,
        .writer_format = "jpg",
        .image_format = QImage::Format_ARGB32,
    };
  }
  if (normalized.compare(QStringLiteral("Web Proof"), Qt::CaseInsensitive) == 0) {
    return {
        .max_edge = 2560,
        .jpeg_quality = 94,
        .format_name = QStringLiteral("JPEG"),
        .bit_depth = 8,
        .flatten_alpha = true,
        .embed_profile = true,
        .writer_format = "jpg",
        .image_format = QImage::Format_ARGB32,
    };
  }
  if (normalized.compare(QStringLiteral("Archive TIFF"), Qt::CaseInsensitive) == 0) {
    return {
        .max_edge = 0,
        .jpeg_quality = 100,
        .format_name = QStringLiteral("TIFF"),
        .bit_depth = 16,
        .flatten_alpha = false,
        .embed_profile = true,
        .writer_format = "tif",
        .image_format = QImage::Format_RGBA64,
    };
  }

  return {};
}

[[nodiscard]] inline QJsonObject export_image_settings_metadata(
    const ExportImageSettings& settings) {
  QJsonObject object;
  object.insert(QStringLiteral("format"), settings.format_name);
  object.insert(QStringLiteral("bitDepth"), settings.bit_depth);
  object.insert(QStringLiteral("writerFormat"), QString::fromLatin1(settings.writer_format));
  object.insert(QStringLiteral("jpegQuality"), settings.jpeg_quality);
  object.insert(QStringLiteral("flattenAlpha"), settings.flatten_alpha);
  object.insert(QStringLiteral("embedProfile"), settings.embed_profile);
  object.insert(QStringLiteral("sceneLinear"), settings.scene_linear);
  object.insert(QStringLiteral("preservesOverRange"), settings.preserves_over_range);
  object.insert(
      QStringLiteral("outputEncoding"),
      settings.scene_linear ? QStringLiteral("scene-linear-float")
                            : QStringLiteral("integer-sdr"));
  object.insert(
      QStringLiteral("headroomPolicy"),
      settings.preserves_over_range ? QStringLiteral("preserve-over-range")
                                    : QStringLiteral("tone-map-to-output-headroom"));
  return object;
}

}  // namespace photo_editor::desktop
