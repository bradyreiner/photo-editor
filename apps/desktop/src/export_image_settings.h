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
  QString scene_linear_color_space;
  bool preserves_over_range = false;
  bool carries_chromaticities = false;
  bool hdr_delivery = false;
  QString hdr_transfer;
  double peak_luminance_nits = 0.0;
  double black_luminance_nits = 0.0;
  double reference_white_nits = 0.0;
  int max_content_light_level = 0;
  int max_frame_average_light_level = 0;
  QByteArray writer_format = "png";
  QImage::Format image_format = QImage::Format_RGBA64;
};

[[nodiscard]] inline QString export_alpha_behavior(
    const ExportImageSettings& settings) {
  if (settings.writer_format == "pfm") {
    return QStringLiteral("drop-unassociated-alpha");
  }
  return settings.flatten_alpha
             ? QStringLiteral("flatten-to-white-opaque")
             : QStringLiteral("preserve-unassociated-alpha");
}

[[nodiscard]] inline QString export_component_association(
    const ExportImageSettings& settings) {
  return settings.flatten_alpha || settings.writer_format == "pfm"
             ? QStringLiteral("opaque")
             : QStringLiteral("unassociated");
}

[[nodiscard]] inline QString export_orientation_policy() {
  return QStringLiteral("normalized-pixels-no-orientation-tag");
}

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
        .carries_chromaticities = false,
        .writer_format = "pfm",
        .image_format = QImage::Format_RGBA32FPx4,
    };
  }
  if (contains_any({QStringLiteral("openexr"), QStringLiteral("exr")})) {
    const auto full_float =
        lower.contains(QStringLiteral("32-bit")) ||
        lower.contains(QStringLiteral("32 bit")) ||
        lower.contains(QStringLiteral("float"));
    const auto scene_linear_color_space =
        contains_any({
            QStringLiteral("aces2065"),
            QStringLiteral("aces 2065"),
            QStringLiteral("ap0"),
        })
            ? QStringLiteral("ACES2065-1")
            : (contains_any({QStringLiteral("acescg"), QStringLiteral("ap1")})
                   ? QStringLiteral("ACEScg")
                   : QString());
    return {
        .max_edge = 0,
        .jpeg_quality = 100,
        .format_name = QStringLiteral("OpenEXR"),
        .bit_depth = full_float ? 32 : 16,
        .flatten_alpha = false,
        .embed_profile = false,
        .scene_linear = true,
        .scene_linear_color_space = scene_linear_color_space,
        .preserves_over_range = true,
        .carries_chromaticities = true,
        .writer_format = "exr",
        .image_format = QImage::Format_RGBA32FPx4,
    };
  }
  if (contains_any({QStringLiteral("hdr"), QStringLiteral("pq"), QStringLiteral("hlg")}) &&
      contains_any({QStringLiteral("avif")})) {
    const auto hlg = lower.contains(QStringLiteral("hlg"));
    return {
        .max_edge = 0,
        .jpeg_quality = 100,
        .format_name = QStringLiteral("AVIF"),
        .bit_depth = 10,
        .flatten_alpha = true,
        .embed_profile = false,
        .scene_linear = false,
        .preserves_over_range = false,
        .carries_chromaticities = true,
        .hdr_delivery = true,
        .hdr_transfer = hlg ? QStringLiteral("HLG") : QStringLiteral("PQ"),
        .peak_luminance_nits = 1000.0,
        .black_luminance_nits = 0.0005,
        .reference_white_nits = 203.0,
        .max_content_light_level = 1000,
        .max_frame_average_light_level = 400,
        .writer_format = "avif-hdr",
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
  object.insert(QStringLiteral("alphaBehavior"), export_alpha_behavior(settings));
  object.insert(
      QStringLiteral("componentAssociation"),
      export_component_association(settings));
  object.insert(QStringLiteral("orientationPolicy"), export_orientation_policy());
  object.insert(QStringLiteral("embedProfile"), settings.embed_profile);
  object.insert(QStringLiteral("sceneLinear"), settings.scene_linear);
  object.insert(
      QStringLiteral("sceneLinearColorSpace"),
      settings.scene_linear_color_space);
  object.insert(QStringLiteral("preservesOverRange"), settings.preserves_over_range);
  object.insert(QStringLiteral("carriesChromaticities"), settings.carries_chromaticities);
  object.insert(QStringLiteral("hdrDelivery"), settings.hdr_delivery);
  object.insert(QStringLiteral("hdrTransfer"), settings.hdr_transfer);
  object.insert(QStringLiteral("peakLuminanceNits"), settings.peak_luminance_nits);
  object.insert(QStringLiteral("blackLuminanceNits"), settings.black_luminance_nits);
  object.insert(QStringLiteral("referenceWhiteNits"), settings.reference_white_nits);
  object.insert(QStringLiteral("maxContentLightLevel"), settings.max_content_light_level);
  object.insert(
      QStringLiteral("maxFrameAverageLightLevel"),
      settings.max_frame_average_light_level);
  object.insert(
      QStringLiteral("outputEncoding"),
      settings.scene_linear
          ? QStringLiteral("scene-linear-float")
          : (settings.hdr_delivery
                 ? QStringLiteral("rec2100-%1").arg(settings.hdr_transfer.toLower())
                 : QStringLiteral("integer-sdr")));
  object.insert(
      QStringLiteral("componentType"),
      settings.writer_format == "exr"
          ? (settings.bit_depth == 16 ? QStringLiteral("half")
                                      : QStringLiteral("float"))
          : (settings.scene_linear
                 ? QStringLiteral("float")
                 : (settings.hdr_delivery ? QStringLiteral("integer-10")
                                          : QStringLiteral("integer"))));
  object.insert(
      QStringLiteral("colorSpacePolicy"),
      settings.hdr_delivery
          ? QStringLiteral("rec2020-nclx")
          : (settings.carries_chromaticities
          ? (settings.scene_linear_color_space.isEmpty()
                 ? QStringLiteral("working-profile-chromaticities")
                 : QStringLiteral("explicit-scene-linear-chromaticities"))
          : (settings.scene_linear
                 ? QStringLiteral("working-profile-unlabeled")
                 : QStringLiteral("output-profile"))));
  object.insert(
      QStringLiteral("headroomPolicy"),
      settings.preserves_over_range
          ? QStringLiteral("preserve-over-range")
          : (settings.hdr_delivery
                 ? QStringLiteral("tone-map-to-hdr-target")
                 : QStringLiteral("tone-map-to-output-headroom")));
  return object;
}

}  // namespace photo_editor::desktop
