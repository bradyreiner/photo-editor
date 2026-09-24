#include <QColor>
#include <QFileInfo>
#include <QStringList>
#include <algorithm>
#include <array>
#include <cmath>

#include "raw_image_decoder.h"
#include "raw_image_decoder_libraw.h"
#include "working_image.h"
#include "capture_camera_metadata.h"

namespace photo_editor::desktop {

QVariantMap read_capture_camera_metadata(const QString& path) {
  return read_libraw_capture_camera_metadata(path);
}

namespace {

[[nodiscard]] bool has_raw_extension(const QString& file_path) {
  static const QStringList kExtensions = {
      QStringLiteral("3fr"), QStringLiteral("arw"), QStringLiteral("cr2"),
      QStringLiteral("cr3"), QStringLiteral("dng"), QStringLiteral("erf"),
      QStringLiteral("iiq"), QStringLiteral("kdc"), QStringLiteral("mef"),
      QStringLiteral("mos"), QStringLiteral("nef"), QStringLiteral("nrw"),
      QStringLiteral("orf"), QStringLiteral("pef"), QStringLiteral("raf"),
      QStringLiteral("raw"), QStringLiteral("rw2"), QStringLiteral("sr2"),
      QStringLiteral("srf"), QStringLiteral("x3f"),
  };
  return kExtensions.contains(QFileInfo(file_path).suffix().toLower());
}

}  // namespace

bool raw_image_supported(const QString& file_path) {
  return raw_camera_linear_decoder_capability().available &&
         has_raw_extension(file_path);
}

bool raw_decode_uses_draft_demosaic(const RawDecodeSettings& settings,
                                    bool scaled_preview) {
  (void)scaled_preview;
  const auto quality = settings.demosaic_quality.trimmed();
  if (quality.compare(QStringLiteral("Preview"), Qt::CaseInsensitive) == 0 ||
      quality.compare(QStringLiteral("Draft"), Qt::CaseInsensitive) == 0) {
    return true;
  }
  if (quality.compare(QStringLiteral("Full"), Qt::CaseInsensitive) == 0 ||
      quality.compare(QStringLiteral("Best"), Qt::CaseInsensitive) == 0) {
    return false;
  }
  // Scaling may reduce output dimensions, but Balanced retains the same
  // demosaic/color semantics as the settled render on every platform.
  return false;
}

double raw_decode_hot_dead_pixel_cleanup_amount(
    const RawDecodeSettings& settings) {
  return std::clamp(settings.hot_dead_pixel_cleanup_amount, 0.0, 1.0);
}

double raw_decode_false_color_suppression_amount(
    const RawDecodeSettings& settings) {
  return std::clamp(settings.false_color_suppression_amount, 0.0, 1.0);
}

bool apply_raw_hot_dead_pixel_cleanup(QImage* image, double amount) {
  amount = std::clamp(amount, 0.0, 1.0);
  if (image == nullptr || image->isNull() || amount <= 0.0 ||
      image->width() < 3 || image->height() < 3) {
    return false;
  }

  const QImage source = image->copy();
  QImage output = image->copy();
  const auto channel_median = [](std::array<double, 8> values) {
    std::sort(values.begin(), values.end());
    return (values[3] + values[4]) * 0.5;
  };
  const auto luma = [](const QColor& color) {
    return color.redF() * 0.2126 + color.greenF() * 0.7152 +
           color.blueF() * 0.0722;
  };

  auto changed = false;
  const auto threshold_scale = 0.45 - amount * 0.37;
  for (int y = 1; y < source.height() - 1; ++y) {
    for (int x = 1; x < source.width() - 1; ++x) {
      std::array<double, 8> red{};
      std::array<double, 8> green{};
      std::array<double, 8> blue{};
      std::array<double, 8> neighbor_luma{};
      std::size_t index = 0;
      for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
          if (dx == 0 && dy == 0) {
            continue;
          }
          const auto neighbor = source.pixelColor(x + dx, y + dy);
          red[index] = neighbor.redF();
          green[index] = neighbor.greenF();
          blue[index] = neighbor.blueF();
          neighbor_luma[index] = luma(neighbor);
          ++index;
        }
      }

      const auto center = source.pixelColor(x, y);
      const auto median_luma = channel_median(neighbor_luma);
      const auto center_luma = luma(center);
      const auto scaled_threshold =
          threshold_scale * std::max(1.0, median_luma);
      if (std::abs(center_luma - median_luma) <= scaled_threshold) {
        continue;
      }

      QColor repaired = center;
      repaired.setRedF(std::clamp(
          static_cast<float>(center.redF() +
                             (channel_median(red) - center.redF()) * amount),
          0.0F, 1.0F));
      repaired.setGreenF(std::clamp(
          static_cast<float>(center.greenF() +
                             (channel_median(green) - center.greenF()) *
                                 amount),
          0.0F, 1.0F));
      repaired.setBlueF(std::clamp(
          static_cast<float>(center.blueF() +
                             (channel_median(blue) - center.blueF()) * amount),
          0.0F, 1.0F));
      output.setPixelColor(x, y, repaired);
      changed = true;
    }
  }

  if (changed) {
    *image = std::move(output);
  }
  return changed;
}

bool apply_raw_false_color_suppression(QImage* image, double amount) {
  amount = std::clamp(amount, 0.0, 1.0);
  if (image == nullptr || image->isNull() || amount <= 0.0 ||
      image->width() < 3 || image->height() < 3) {
    return false;
  }

  const QImage source = image->copy();
  QImage output = image->copy();
  const auto channel_median = [](std::array<double, 8> values) {
    std::sort(values.begin(), values.end());
    return (values[3] + values[4]) * 0.5;
  };
  const auto luma = [](const QColor& color) {
    return color.redF() * 0.2126 + color.greenF() * 0.7152 +
           color.blueF() * 0.0722;
  };

  auto changed = false;
  const auto threshold = 0.18 - amount * 0.12;
  for (int y = 1; y < source.height() - 1; ++y) {
    for (int x = 1; x < source.width() - 1; ++x) {
      std::array<double, 8> red{};
      std::array<double, 8> green{};
      std::array<double, 8> blue{};
      std::array<double, 8> neighbor_luma{};
      std::size_t index = 0;
      for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
          if (dx == 0 && dy == 0) {
            continue;
          }
          const auto neighbor = source.pixelColor(x + dx, y + dy);
          red[index] = neighbor.redF();
          green[index] = neighbor.greenF();
          blue[index] = neighbor.blueF();
          neighbor_luma[index] = luma(neighbor);
          ++index;
        }
      }

      const auto center = source.pixelColor(x, y);
      const auto center_luma = luma(center);
      const auto median_luma = channel_median(neighbor_luma);
      const auto median_red = channel_median(red);
      const auto median_green = channel_median(green);
      const auto median_blue = channel_median(blue);
      const auto chroma_error = std::max({
          std::abs((center.redF() - center_luma) - (median_red - median_luma)),
          std::abs((center.greenF() - center_luma) -
                   (median_green - median_luma)),
          std::abs((center.blueF() - center_luma) -
                   (median_blue - median_luma)),
      });
      if (chroma_error <= threshold) {
        continue;
      }

      QColor repaired = center;
      repaired.setRedF(std::clamp(
          static_cast<float>(center.redF() + (center_luma + median_red -
                                              median_luma - center.redF()) *
                                                 amount),
          0.0F, 1.0F));
      repaired.setGreenF(std::clamp(
          static_cast<float>(center.greenF() + (center_luma + median_green -
                                                median_luma - center.greenF()) *
                                                   amount),
          0.0F, 1.0F));
      repaired.setBlueF(std::clamp(
          static_cast<float>(center.blueF() + (center_luma + median_blue -
                                               median_luma - center.blueF()) *
                                                  amount),
          0.0F, 1.0F));
      output.setPixelColor(x, y, repaired);
      changed = true;
    }
  }

  if (changed) {
    *image = std::move(output);
  }
  return changed;
}

QSize raw_image_size(const QString& file_path) {
  return has_raw_extension(file_path)
             ? libraw_camera_linear_image_size(file_path)
             : QSize{};
}

bool decode_raw_image(const QString& file_path, QImage* image, int max_edge,
                      const RawDecodeSettings& settings,
                      photo_editor::pipeline::DecodedImageMetadata* metadata) {
  auto rendered_settings = settings;
  rendered_settings.input_transform_enabled = false;
  QImage linear;
  if (!decode_raw_linear_image(file_path, &linear, max_edge, rendered_settings,
                               metadata)) {
    return false;
  }
  *image = finalize_output_image(std::move(linear), preview_color_space());
  return !image->isNull();
}

bool decode_raw_linear_image(
    const QString& file_path, QImage* image, int max_edge,
    const RawDecodeSettings& settings,
    photo_editor::pipeline::DecodedImageMetadata* metadata) {
  if (!has_raw_extension(file_path)) {
    return false;
  }
  const auto camera_linear =
      raw_decode_requests_camera_linear_backend(settings);
  const auto decoded =
      camera_linear ? decode_libraw_camera_linear_image(
                          file_path, image, max_edge, settings, metadata)
                    : decode_libraw_scene_linear_image(
                          file_path, image, max_edge, settings, metadata);
  if (!decoded) {
    return false;
  }
  apply_raw_hot_dead_pixel_cleanup(
      image, raw_decode_hot_dead_pixel_cleanup_amount(settings));
  apply_raw_false_color_suppression(
      image, raw_decode_false_color_suppression_amount(settings));
  return true;
}

bool read_raw_decoded_metadata(
    const QString& file_path,
    photo_editor::pipeline::DecodedImageMetadata* metadata,
    const RawDecodeSettings& settings) {
  if (!has_raw_extension(file_path)) {
    return false;
  }
  return raw_decode_requests_camera_linear_backend(settings)
             ? read_libraw_camera_linear_metadata(file_path, metadata, settings)
             : read_libraw_scene_linear_metadata(file_path, metadata, settings);
}

bool native_image_supported(const QString& /*file_path*/) { return false; }

QSize native_image_size(const QString& /*file_path*/) { return {}; }

bool decode_native_image(const QString& /*file_path*/, QImage* /*image*/,
                         int /*max_edge*/) {
  return false;
}

bool decode_native_linear_image(const QString& /*file_path*/, QImage* /*image*/,
                                int /*max_edge*/) {
  return false;
}

}  // namespace photo_editor::desktop
