#include "editor_preview_controller.h"

#include <QtMath>
#include <QtConcurrent>

#include <array>
#include <cmath>
#include <QCryptographicHash>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QImageWriter>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRect>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <vector>

namespace photo_editor::desktop {

namespace {

constexpr int kInteractivePreviewEdge = 960;

QString preview_cache_root() {
  const auto cache_root = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
  if (cache_root.isEmpty()) {
    return QDir::current().absoluteFilePath(QStringLiteral(".photo-editor-cache/previews"));
  }

  return QDir(cache_root).filePath(QStringLiteral("previews"));
}

QString versioned_file_url(const QString& file_path) {
  const QFileInfo file_info(file_path);
  auto url = QUrl::fromLocalFile(file_info.absoluteFilePath());
  return url.toString();
}

QString with_preview_revision(const QString& source, int revision) {
  if (source.isEmpty()) {
    return {};
  }

  auto url = QUrl(source);
  if (!url.isValid() || url.scheme().isEmpty()) {
    return source;
  }

  QUrlQuery query(url);
  query.removeAllQueryItems(QStringLiteral("rev"));
  query.addQueryItem(QStringLiteral("rev"), QString::number(revision));
  url.setQuery(query);
  return url.toString();
}

bool preview_supported(const QString& file_path) {
  const QFileInfo file_info(file_path);
  const auto suffix = file_info.suffix().toLower().toUtf8();
  return QImageReader::supportedImageFormats().contains(suffix);
}

int clamp_channel(double value) {
  return qBound(0, static_cast<int>(qRound(value)), 255);
}

double clamp_signed_percent(double value) {
  return qBound(-100.0, value, 100.0);
}

double clamp_unit(double value) {
  return qBound(0.0, value, 1.0);
}

struct LinearPixel final {
  double red = 0.0;
  double green = 0.0;
  double blue = 0.0;
  double alpha = 1.0;
};

double srgb_to_linear(double value) {
  const auto clamped = clamp_unit(value);
  if (clamped <= 0.04045) {
    return clamped / 12.92;
  }

  return qPow((clamped + 0.055) / 1.055, 2.4);
}

double linear_to_srgb(double value) {
  const auto clamped = clamp_unit(value);
  if (clamped <= 0.0031308) {
    return clamped * 12.92;
  }

  return 1.055 * qPow(clamped, 1.0 / 2.4) - 0.055;
}

double luminance_for_pixel(const LinearPixel& pixel) {
  return (pixel.red * 0.2126) + (pixel.green * 0.7152) + (pixel.blue * 0.0722);
}

LinearPixel scale_pixel_to_luminance(const LinearPixel& pixel, double target_luminance) {
  const auto clamped_target = clamp_unit(target_luminance);
  const auto current_luminance = luminance_for_pixel(pixel);
  if (current_luminance <= 1e-6) {
    return LinearPixel{
        .red = clamped_target,
        .green = clamped_target,
        .blue = clamped_target,
        .alpha = pixel.alpha,
    };
  }

  const auto scale = clamped_target / current_luminance;
  return LinearPixel{
      .red = qMax(0.0, pixel.red * scale),
      .green = qMax(0.0, pixel.green * scale),
      .blue = qMax(0.0, pixel.blue * scale),
      .alpha = pixel.alpha,
  };
}

double smoothstep(double edge0, double edge1, double x) {
  if (qFuzzyCompare(edge0, edge1)) {
    return x < edge0 ? 0.0 : 1.0;
  }

  const auto t = clamp_unit((x - edge0) / (edge1 - edge0));
  return t * t * (3.0 - 2.0 * t);
}

double bell_weight(double value, double center, double radius) {
  if (radius <= 0.0) {
    return 0.0;
  }

  const auto distance = qAbs(value - center) / radius;
  if (distance >= 1.0) {
    return 0.0;
  }

  const auto t = 1.0 - distance;
  return t * t * (3.0 - 2.0 * t);
}

double apply_sigmoid_contrast(double value, double amount) {
  const auto clamped = clamp_unit(value);
  if (qFuzzyIsNull(amount) || clamped <= 1e-6 || clamped >= 1.0 - 1e-6) {
    return clamped;
  }

  const auto slope = qBound(0.35, 1.0 + amount * 1.8, 2.8);
  const auto logit = std::log(clamped / (1.0 - clamped));
  return clamp_unit(1.0 / (1.0 + std::exp(-logit * slope)));
}

double apply_shadow_adjustment(double value, double amount) {
  const auto mask = 1.0 - smoothstep(0.12, 0.62, value);
  if (amount >= 0.0) {
    return clamp_unit(value + amount * mask * (1.0 - value) * 0.72);
  }

  return clamp_unit(value + amount * mask * (0.35 + value) * 0.58);
}

double apply_highlight_adjustment(double value, double amount) {
  const auto mask = smoothstep(0.38, 0.92, value);
  if (amount >= 0.0) {
    return clamp_unit(value + amount * mask * (1.0 - value) * 0.62);
  }

  return clamp_unit(value + amount * mask * (0.35 + value) * 0.5);
}

double apply_white_adjustment(double value, double amount) {
  const auto mask = smoothstep(0.72, 1.0, value);
  if (amount >= 0.0) {
    return clamp_unit(value + amount * mask * (1.0 - value) * 0.95);
  }

  return clamp_unit(value + amount * mask * value * 0.82);
}

double apply_black_adjustment(double value, double amount) {
  const auto mask = 1.0 - smoothstep(0.0, 0.28, value);
  if (amount >= 0.0) {
    return clamp_unit(value + amount * mask * (1.0 - value) * 0.38);
  }

  return clamp_unit(value + amount * mask * (0.45 + value) * 0.9);
}

std::array<QPointF, 6> tone_curve_control_points(const EditState& state) {
  const auto point = [](double x, double delta) {
    return QPointF{x, clamp_unit(x + (delta / 100.0) * 0.34)};
  };

  return {
      QPointF{0.0, 0.0},
      point(0.20, state.curve_shadows),
      point(0.40, state.curve_darks),
      point(0.65, state.curve_lights),
      point(0.85, state.curve_highlights),
      QPointF{1.0, 1.0},
  };
}

double evaluate_tone_curve(double value, const EditState& state) {
  const auto points = tone_curve_control_points(state);
  const auto clamped_value = clamp_unit(value);

  for (std::size_t index = 1; index < points.size(); ++index) {
    if (clamped_value > points[index].x() && index + 1 < points.size()) {
      continue;
    }

    const auto start = points[index - 1];
    const auto end = points[index];
    const auto span = qMax(1e-6, end.x() - start.x());
    const auto t = clamp_unit((clamped_value - start.x()) / span);
    return clamp_unit(start.y() + (end.y() - start.y()) * t);
  }

  return clamped_value;
}

double apply_global_tone(double value, const EditState& state) {
  auto adjusted = clamp_unit(value);
  adjusted = apply_sigmoid_contrast(adjusted, state.contrast / 100.0);
  adjusted = apply_shadow_adjustment(adjusted, state.shadows / 100.0);
  adjusted = apply_highlight_adjustment(adjusted, state.highlights / 100.0);
  adjusted = apply_white_adjustment(adjusted, state.whites / 100.0);
  adjusted = apply_black_adjustment(adjusted, state.blacks / 100.0);
  return evaluate_tone_curve(adjusted, state);
}

std::array<double, 3> white_balance_multipliers(double temperature, double tint) {
  const auto normalized_temperature = clamp_signed_percent(temperature) / 100.0;
  const auto normalized_tint = clamp_signed_percent(tint) / 100.0;
  const auto red = qPow(2.0, normalized_temperature * 0.42 + normalized_tint * 0.08);
  const auto green = qPow(2.0, -normalized_tint * 0.24);
  const auto blue = qPow(2.0, -normalized_temperature * 0.42 + normalized_tint * 0.08);
  const auto normalization = (red * 0.2126) + (green * 0.7152) + (blue * 0.0722);
  if (normalization <= 1e-6) {
    return {1.0, 1.0, 1.0};
  }

  return {red / normalization, green / normalization, blue / normalization};
}

std::vector<float> box_blur(const std::vector<float>& input, int width, int height, int radius) {
  if (radius <= 0 || input.empty()) {
    return input;
  }

  const auto kernel_width = radius * 2 + 1;
  std::vector<float> horizontal(input.size(), 0.0F);
  std::vector<float> output(input.size(), 0.0F);

  for (int y = 0; y < height; ++y) {
    double sum = 0.0;
    for (int sample = -radius; sample <= radius; ++sample) {
      const auto clamped_x = qBound(0, sample, width - 1);
      sum += input[static_cast<std::size_t>(y * width + clamped_x)];
    }

    for (int x = 0; x < width; ++x) {
      horizontal[static_cast<std::size_t>(y * width + x)] = static_cast<float>(sum / kernel_width);
      const auto remove_x = qBound(0, x - radius, width - 1);
      const auto add_x = qBound(0, x + radius + 1, width - 1);
      sum += input[static_cast<std::size_t>(y * width + add_x)] -
             input[static_cast<std::size_t>(y * width + remove_x)];
    }
  }

  for (int x = 0; x < width; ++x) {
    double sum = 0.0;
    for (int sample = -radius; sample <= radius; ++sample) {
      const auto clamped_y = qBound(0, sample, height - 1);
      sum += horizontal[static_cast<std::size_t>(clamped_y * width + x)];
    }

    for (int y = 0; y < height; ++y) {
      output[static_cast<std::size_t>(y * width + x)] = static_cast<float>(sum / kernel_width);
      const auto remove_y = qBound(0, y - radius, height - 1);
      const auto add_y = qBound(0, y + radius + 1, height - 1);
      sum += horizontal[static_cast<std::size_t>(add_y * width + x)] -
             horizontal[static_cast<std::size_t>(remove_y * width + x)];
    }
  }

  return output;
}

double channel_weight_for_selection(double hue, const QString& hsl_channel) {
  if (hue < 0.0) {
    return 0.0;
  }

  struct Region final {
    const char* name;
    double center;
    double inner_width;
    double outer_width;
  };

  static constexpr Region kRegions[] = {
      {"Reds", 0.0, 0.05, 0.10},
      {"Oranges", 0.09, 0.04, 0.09},
      {"Yellows", 0.15, 0.04, 0.08},
      {"Greens", 0.32, 0.10, 0.18},
      {"Aquas", 0.50, 0.06, 0.10},
      {"Blues", 0.64, 0.08, 0.13},
      {"Purples", 0.78, 0.06, 0.10},
      {"Magentas", 0.88, 0.05, 0.10},
  };

  for (const auto& region : kRegions) {
    if (hsl_channel != QLatin1String(region.name)) {
      continue;
    }

    auto distance = qAbs(hue - region.center);
    distance = qMin(distance, 1.0 - distance);
    if (distance <= region.inner_width) {
      return 1.0;
    }
    if (distance >= region.outer_width) {
      return 0.0;
    }

    return 1.0 - smoothstep(region.inner_width, region.outer_width, distance);
  }

  return 0.0;
}

double crop_aspect_ratio(const QString& preset) {
  if (preset == QStringLiteral("Square")) {
    return 1.0;
  }

  if (preset == QStringLiteral("4x5")) {
    return 4.0 / 5.0;
  }

  if (preset == QStringLiteral("16x9")) {
    return 16.0 / 9.0;
  }

  return 0.0;
}

QRect crop_rect_for_image(const QSize& size, const QString& preset, double crop_zoom) {
  int crop_width = size.width();
  int crop_height = size.height();

  const auto aspect_ratio = crop_aspect_ratio(preset);
  if (aspect_ratio > 0.0) {
    const auto current_ratio = static_cast<double>(size.width()) / static_cast<double>(size.height());
    if (current_ratio > aspect_ratio) {
      crop_width = static_cast<int>(qRound(static_cast<double>(size.height()) * aspect_ratio));
    } else {
      crop_height = static_cast<int>(qRound(static_cast<double>(size.width()) / aspect_ratio));
    }
  }

  crop_width = qMax(1, static_cast<int>(qRound(static_cast<double>(crop_width) / crop_zoom)));
  crop_height = qMax(1, static_cast<int>(qRound(static_cast<double>(crop_height) / crop_zoom)));

  const auto x = qMax(0, (size.width() - crop_width) / 2);
  const auto y = qMax(0, (size.height() - crop_height) / 2);
  return {x, y, crop_width, crop_height};
}

bool has_visible_adjustments(const EditState& state) {
  return !qFuzzyIsNull(state.exposure) ||
         !qFuzzyIsNull(state.contrast) ||
         !qFuzzyIsNull(state.highlights) ||
         !qFuzzyIsNull(state.shadows) ||
         !qFuzzyIsNull(state.whites) ||
         !qFuzzyIsNull(state.blacks) ||
         !qFuzzyIsNull(state.temperature) ||
         !qFuzzyIsNull(state.tint) ||
         !qFuzzyIsNull(state.texture) ||
         !qFuzzyIsNull(state.clarity) ||
         !qFuzzyIsNull(state.dehaze) ||
         !qFuzzyIsNull(state.vibrance) ||
         !qFuzzyIsNull(state.saturation) ||
         !qFuzzyIsNull(state.curve_shadows) ||
         !qFuzzyIsNull(state.curve_darks) ||
         !qFuzzyIsNull(state.curve_lights) ||
         !qFuzzyIsNull(state.curve_highlights) ||
         !qFuzzyIsNull(state.hue_adjustment) ||
         !qFuzzyIsNull(state.saturation_adjustment) ||
         !qFuzzyIsNull(state.luminance_adjustment) ||
         !qFuzzyCompare(state.crop_zoom, 1.0) ||
         state.crop_preset != QStringLiteral("Original") ||
         state.linear_mask_enabled ||
         !qFuzzyIsNull(state.linear_mask_exposure) ||
         !qFuzzyIsNull(state.linear_mask_temperature) ||
         !qFuzzyCompare(state.linear_mask_feather, 0.22) ||
         !qFuzzyCompare(state.linear_mask_position, 0.5) ||
         !qFuzzyIsNull(state.linear_mask_rotation);
}

bool decode_source_image(const QString& file_path, QImage* image, int max_edge) {
  if (!preview_supported(file_path)) {
    return false;
  }

  const QFileInfo source_info(file_path);
  if (!source_info.exists() || !source_info.isReadable()) {
    return false;
  }

  QImageReader reader(file_path);
  if (!reader.canRead()) {
    return false;
  }

  if (max_edge > 0) {
    const auto source_size = reader.size();
    if (source_size.isValid()) {
      reader.setScaledSize(source_size.scaled(max_edge, max_edge, Qt::KeepAspectRatio));
    }
  }

  *image = reader.read().convertToFormat(QImage::Format_ARGB32);
  return !image->isNull();
}

QImage apply_crop(const QImage& image, const EditState& state) {
  return image.copy(crop_rect_for_image(image.size(), state.crop_preset, state.crop_zoom));
}

void apply_color_adjustments(QImage* image, const EditState& state) {
  const auto exposure_gain = qPow(2.0, state.exposure);
  const auto white_balance = white_balance_multipliers(state.temperature, state.tint);
  const auto texture_strength = state.texture / 100.0;
  const auto clarity_strength = state.clarity / 100.0;
  const auto dehaze_strength = state.dehaze / 100.0;
  const auto vibrance_strength = state.vibrance / 100.0;
  const auto saturation_strength = state.saturation / 100.0;
  const auto gradient_radians = qDegreesToRadians(state.linear_mask_rotation);
  const auto gradient_cosine = qCos(gradient_radians);
  const auto gradient_sine = qSin(gradient_radians);
  const auto gradient_feather = qMax(0.05, state.linear_mask_feather);
  const auto width = image->width();
  const auto height = image->height();
  std::vector<LinearPixel> pixels(static_cast<std::size_t>(width * height));

  for (int y = 0; y < height; ++y) {
    const auto* scanline = reinterpret_cast<const QRgb*>(image->constScanLine(y));
    for (int x = 0; x < width; ++x) {
      const QColor color = QColor::fromRgb(scanline[x]);
      auto pixel = LinearPixel{
          .red = srgb_to_linear(color.redF()) * exposure_gain * white_balance[0],
          .green = srgb_to_linear(color.greenF()) * exposure_gain * white_balance[1],
          .blue = srgb_to_linear(color.blueF()) * exposure_gain * white_balance[2],
          .alpha = color.alphaF(),
      };
      pixel = scale_pixel_to_luminance(pixel, apply_global_tone(luminance_for_pixel(pixel), state));
      pixels[static_cast<std::size_t>(y * width + x)] = pixel;
    }
  }

  if (!qFuzzyIsNull(texture_strength) || !qFuzzyIsNull(clarity_strength) || !qFuzzyIsNull(dehaze_strength)) {
    std::vector<float> luminance_buffer(static_cast<std::size_t>(width * height), 0.0F);
    for (std::size_t index = 0; index < pixels.size(); ++index) {
      luminance_buffer[index] = static_cast<float>(luminance_for_pixel(pixels[index]));
    }

    const auto texture_blur = box_blur(luminance_buffer, width, height, 2);
    const auto clarity_blur = box_blur(luminance_buffer, width, height, 8);
    const auto haze_blur = box_blur(luminance_buffer, width, height, 20);

    for (std::size_t index = 0; index < pixels.size(); ++index) {
      const auto luminance = static_cast<double>(luminance_buffer[index]);
      const auto midtone_weight = bell_weight(luminance, 0.5, 0.42);
      auto target_luminance = luminance;
      target_luminance += texture_strength *
                          static_cast<double>(luminance_buffer[index] - texture_blur[index]) *
                          (0.5 + midtone_weight) * 1.15;
      target_luminance += clarity_strength *
                          static_cast<double>(luminance_buffer[index] - clarity_blur[index]) *
                          midtone_weight * 1.55;
      target_luminance += dehaze_strength *
                          static_cast<double>(luminance_buffer[index] - haze_blur[index]) * 1.8;
      pixels[index] = scale_pixel_to_luminance(pixels[index], target_luminance);
    }
  }

  for (std::size_t index = 0; index < pixels.size(); ++index) {
    auto& pixel = pixels[index];
    QColor adjusted;
    adjusted.setRgbF(
        static_cast<float>(linear_to_srgb(pixel.red)),
        static_cast<float>(linear_to_srgb(pixel.green)),
        static_cast<float>(linear_to_srgb(pixel.blue)),
        static_cast<float>(clamp_unit(pixel.alpha)));

    float hue = 0.0F;
    float saturation = 0.0F;
    float lightness = 0.0F;
    float alpha = 0.0F;
    adjusted.getHslF(&hue, &saturation, &lightness, &alpha);

    const auto current_saturation = static_cast<double>(saturation);
    const auto chroma_protection = 0.65 + 0.35 * (1.0 - qAbs(static_cast<double>(lightness) * 2.0 - 1.0));
    saturation = static_cast<float>(
        clamp_unit(current_saturation * (1.0 + saturation_strength * 0.85) +
                   vibrance_strength * (1.0 - current_saturation) * chroma_protection * 0.8 +
                   qMax(0.0, dehaze_strength) * (1.0 - current_saturation) * 0.12));

    const auto channel_weight = channel_weight_for_selection(static_cast<double>(hue), state.hsl_channel);
    if (channel_weight > 0.0) {
      auto adjusted_hue = static_cast<double>(hue) + channel_weight * (state.hue_adjustment / 100.0) * 0.12;
      if (adjusted_hue > 1.0) {
        adjusted_hue -= 1.0;
      } else if (adjusted_hue < 0.0) {
        adjusted_hue += 1.0;
      }
      hue = static_cast<float>(adjusted_hue);
      saturation = static_cast<float>(
          clamp_unit(static_cast<double>(saturation) + channel_weight * (state.saturation_adjustment / 100.0) * 0.45));
      lightness = static_cast<float>(
          clamp_unit(static_cast<double>(lightness) + channel_weight * (state.luminance_adjustment / 100.0) * 0.25));
    }

    adjusted.setHslF(hue, saturation, lightness, alpha);
    pixel.red = srgb_to_linear(adjusted.redF());
    pixel.green = srgb_to_linear(adjusted.greenF());
    pixel.blue = srgb_to_linear(adjusted.blueF());
    pixel.alpha = adjusted.alphaF();
  }

  const auto local_white_balance = white_balance_multipliers(state.linear_mask_temperature, 0.0);
  for (int y = 0; y < height; ++y) {
    auto* scanline = reinterpret_cast<QRgb*>(image->scanLine(y));
    for (int x = 0; x < width; ++x) {
      auto& pixel = pixels[static_cast<std::size_t>(y * width + x)];

      if (state.linear_mask_enabled) {
        const auto normalized_x = width > 1 ? static_cast<double>(x) / static_cast<double>(width - 1) : 0.5;
        const auto normalized_y = height > 1 ? static_cast<double>(y) / static_cast<double>(height - 1) : 0.5;
        const auto axis_coordinate =
            ((normalized_x - 0.5) * gradient_cosine + (normalized_y - 0.5) * gradient_sine) + 0.5;
        const auto mask_weight = clamp_unit(((state.linear_mask_position - axis_coordinate) / gradient_feather) + 0.5);

        if (mask_weight > 0.0) {
          const auto local_exposure_gain = qPow(2.0, state.linear_mask_exposure * mask_weight);
          pixel.red *= 1.0 + (local_white_balance[0] - 1.0) * mask_weight;
          pixel.green *= 1.0 + (local_white_balance[1] - 1.0) * mask_weight;
          pixel.blue *= 1.0 + (local_white_balance[2] - 1.0) * mask_weight;
          pixel.red *= local_exposure_gain;
          pixel.green *= local_exposure_gain;
          pixel.blue *= local_exposure_gain;
        }
      }

      scanline[x] = qRgba(
          clamp_channel(linear_to_srgb(pixel.red) * 255.0),
          clamp_channel(linear_to_srgb(pixel.green) * 255.0),
          clamp_channel(linear_to_srgb(pixel.blue) * 255.0),
          clamp_channel(clamp_unit(pixel.alpha) * 255.0));
    }
  }
}

QString cache_path_for_selection(const QString& file_path, const EditState& state, bool edited, bool full_resolution) {
  const auto fingerprint = QStringLiteral("%1|%2|%3|%4|%5|%6|%7|%8|%9|%10|%11|%12|%13|%14|%15|%16|%17|%18")
                               .arg(file_path)
                               .arg(edited ? QStringLiteral("edited") : QStringLiteral("reference"))
                               .arg(full_resolution ? QStringLiteral("full") : QStringLiteral("interactive"))
                               .arg(state.exposure, 0, 'f', 2)
                               .arg(state.contrast, 0, 'f', 2)
                               .arg(state.highlights, 0, 'f', 2)
                               .arg(state.shadows, 0, 'f', 2)
                               .arg(state.whites, 0, 'f', 2)
                               .arg(state.blacks, 0, 'f', 2)
                               .arg(state.temperature, 0, 'f', 2)
                               .arg(state.tint, 0, 'f', 2)
                               .arg(QStringLiteral("%1:%2:%3").arg(state.texture, 0, 'f', 2).arg(state.clarity, 0, 'f', 2).arg(state.dehaze, 0, 'f', 2))
                               .arg(state.vibrance, 0, 'f', 2)
                               .arg(state.saturation, 0, 'f', 2)
                               .arg(QStringLiteral("%1:%2:%3:%4")
                                        .arg(state.curve_shadows, 0, 'f', 2)
                                        .arg(state.curve_darks, 0, 'f', 2)
                                        .arg(state.curve_lights, 0, 'f', 2)
                                        .arg(state.curve_highlights, 0, 'f', 2))
                               .arg(QStringLiteral("%1:%2:%3:%4")
                                        .arg(state.hsl_channel)
                                        .arg(state.hue_adjustment, 0, 'f', 2)
                                        .arg(state.saturation_adjustment, 0, 'f', 2)
                                        .arg(state.luminance_adjustment, 0, 'f', 2))
                               .arg(QStringLiteral("%1:%2").arg(state.crop_preset).arg(state.crop_zoom, 0, 'f', 2))
                               .arg(QStringLiteral("%1:%2:%3:%4:%5:%6")
                                        .arg(state.linear_mask_enabled ? QStringLiteral("on") : QStringLiteral("off"))
                                        .arg(state.linear_mask_exposure, 0, 'f', 2)
                                        .arg(state.linear_mask_temperature, 0, 'f', 2)
                                        .arg(state.linear_mask_feather, 0, 'f', 2)
                                        .arg(state.linear_mask_position, 0, 'f', 2)
                                        .arg(state.linear_mask_rotation, 0, 'f', 2));
  const auto digest =
      QCryptographicHash::hash(fingerprint.toUtf8(), QCryptographicHash::Sha1).toHex();
  return QDir(preview_cache_root()).filePath(QStringLiteral("%1.jpg").arg(QString::fromLatin1(digest)));
}

RenderResult render_preview_request(const RenderRequest& request) {
  RenderResult result;
  result.full_resolution = request.full_resolution;
  result.generation = request.generation;

  QImage source_image;
  if (!decode_source_image(request.file_path, &source_image, request.max_edge)) {
    result.status = QStringLiteral("Source file unavailable.");
    return result;
  }

  QDir().mkpath(preview_cache_root());

  QImage edited_image = apply_crop(source_image, request.state);
  if (has_visible_adjustments(request.state)) {
    apply_color_adjustments(&edited_image, request.state);
  }

  const auto output_path =
      cache_path_for_selection(request.file_path, request.state, true, request.full_resolution);
  QImageWriter writer(output_path, "jpg");
  writer.setQuality(request.jpeg_quality);
  if (!writer.write(edited_image)) {
    result.status = QStringLiteral("Unable to write preview cache.");
    return result;
  }

  result.preview_source = versioned_file_url(output_path);

  if (request.compare_mode) {
    QImage reference_image = apply_crop(source_image, request.state);
    const auto reference_output_path =
        cache_path_for_selection(request.file_path, request.state, false, request.full_resolution);
    QImageWriter reference_writer(reference_output_path, "jpg");
    reference_writer.setQuality(request.jpeg_quality);
    if (!reference_writer.write(reference_image)) {
      result.status = QStringLiteral("Unable to write reference preview cache.");
      return result;
    }
    result.reference_preview_source = versioned_file_url(reference_output_path);
  }

  result.success = true;
  if (has_visible_adjustments(request.state)) {
    result.status = request.full_resolution ? QStringLiteral("Full-resolution preview ready.")
                                            : QStringLiteral("Interactive preview ready.");
  } else {
    result.status = request.full_resolution ? QStringLiteral("Full-resolution base preview ready.")
                                            : QStringLiteral("Base preview ready.");
  }
  return result;
}

}  // namespace

EditorPreviewController::EditorPreviewController(QObject* parent) : QObject(parent) {
  interactive_render_timer_.setSingleShot(true);
  interactive_render_timer_.setInterval(20);
  connect(&interactive_render_timer_, &QTimer::timeout, this, [this]() { renderPreviewStage(false); });

  full_render_timer_.setSingleShot(true);
  full_render_timer_.setInterval(140);
  connect(&full_render_timer_, &QTimer::timeout, this, [this]() { renderPreviewStage(true); });

  connect(&interactive_render_watcher_, &QFutureWatcher<RenderResult>::finished, this, [this]() {
    handleRenderFinished(false);
  });
  connect(&full_render_watcher_, &QFutureWatcher<RenderResult>::finished, this, [this]() {
    handleRenderFinished(true);
  });

  persist_timer_.setSingleShot(true);
  persist_timer_.setInterval(350);
  connect(&persist_timer_, &QTimer::timeout, this, &EditorPreviewController::flushPersistedState);
  connect(&persist_process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
    setStatus(QStringLiteral("Edit recipe save is unavailable."));
    refreshDebugInfo();
  });
}

QString EditorPreviewController::previewSource() const { return preview_source_; }
QString EditorPreviewController::referencePreviewSource() const { return reference_preview_source_; }
QString EditorPreviewController::debugInfo() const { return debug_info_; }
QString EditorPreviewController::status() const { return status_; }
QString EditorPreviewController::cameraLabel() const { return camera_label_; }
QString EditorPreviewController::captureDate() const { return capture_date_; }
QString EditorPreviewController::lensModel() const { return lens_model_; }
bool EditorPreviewController::hasAsset() const { return !file_path_.isEmpty() || !fallback_thumbnail_source_.isEmpty(); }
bool EditorPreviewController::processing() const { return processing_; }
bool EditorPreviewController::compareMode() const { return compare_mode_; }
double EditorPreviewController::exposure() const { return exposure_; }
double EditorPreviewController::contrast() const { return contrast_; }
double EditorPreviewController::highlights() const { return highlights_; }
double EditorPreviewController::shadows() const { return shadows_; }
double EditorPreviewController::whites() const { return whites_; }
double EditorPreviewController::blacks() const { return blacks_; }
double EditorPreviewController::temperature() const { return temperature_; }
double EditorPreviewController::tint() const { return tint_; }
double EditorPreviewController::texture() const { return texture_; }
double EditorPreviewController::clarity() const { return clarity_; }
double EditorPreviewController::dehaze() const { return dehaze_; }
double EditorPreviewController::vibrance() const { return vibrance_; }
double EditorPreviewController::saturation() const { return saturation_; }
double EditorPreviewController::curveShadows() const { return curve_shadows_; }
double EditorPreviewController::curveDarks() const { return curve_darks_; }
double EditorPreviewController::curveLights() const { return curve_lights_; }
double EditorPreviewController::curveHighlights() const { return curve_highlights_; }
QString EditorPreviewController::hslChannel() const { return hsl_channel_; }
double EditorPreviewController::hueAdjustment() const { return hue_adjustment_; }
double EditorPreviewController::saturationAdjustment() const { return saturation_adjustment_; }
double EditorPreviewController::luminanceAdjustment() const { return luminance_adjustment_; }
double EditorPreviewController::cropZoom() const { return crop_zoom_; }
QString EditorPreviewController::cropPreset() const { return crop_preset_; }
bool EditorPreviewController::whiteBalanceSampling() const { return white_balance_sampling_; }
bool EditorPreviewController::linearMaskEnabled() const { return linear_mask_enabled_; }
double EditorPreviewController::linearMaskExposure() const { return linear_mask_exposure_; }
double EditorPreviewController::linearMaskTemperature() const { return linear_mask_temperature_; }
double EditorPreviewController::linearMaskFeather() const { return linear_mask_feather_; }
double EditorPreviewController::linearMaskPosition() const { return linear_mask_position_; }
double EditorPreviewController::linearMaskRotation() const { return linear_mask_rotation_; }

void EditorPreviewController::setCompareMode(bool compare_mode) {
  if (compare_mode_ == compare_mode) {
    return;
  }
  compare_mode_ = compare_mode;
  emit compareModeChanged();
  if (compare_mode_ && hasAsset()) {
    renderPreview();
  }
}

void EditorPreviewController::setExposure(double exposure) {
  exposure = qBound(-2.0, exposure, 2.0);
  if (qFuzzyCompare(exposure_, exposure)) {
    return;
  }
  exposure_ = exposure;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setContrast(double contrast) {
  contrast = clamp_signed_percent(contrast);
  if (qFuzzyCompare(contrast_ + 1.0, contrast + 1.0)) {
    return;
  }
  contrast_ = contrast;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setHighlights(double highlights) {
  highlights = clamp_signed_percent(highlights);
  if (qFuzzyCompare(highlights_ + 1.0, highlights + 1.0)) {
    return;
  }
  highlights_ = highlights;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setShadows(double shadows) {
  shadows = clamp_signed_percent(shadows);
  if (qFuzzyCompare(shadows_ + 1.0, shadows + 1.0)) {
    return;
  }
  shadows_ = shadows;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setWhites(double whites) {
  whites = clamp_signed_percent(whites);
  if (qFuzzyCompare(whites_ + 1.0, whites + 1.0)) {
    return;
  }
  whites_ = whites;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setBlacks(double blacks) {
  blacks = clamp_signed_percent(blacks);
  if (qFuzzyCompare(blacks_ + 1.0, blacks + 1.0)) {
    return;
  }
  blacks_ = blacks;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setTemperature(double temperature) {
  temperature = clamp_signed_percent(temperature);
  if (qFuzzyCompare(temperature_ + 1.0, temperature + 1.0)) {
    return;
  }
  temperature_ = temperature;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setTint(double tint) {
  tint = clamp_signed_percent(tint);
  if (qFuzzyCompare(tint_ + 1.0, tint + 1.0)) {
    return;
  }
  tint_ = tint;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setTexture(double texture) {
  texture = clamp_signed_percent(texture);
  if (qFuzzyCompare(texture_ + 1.0, texture + 1.0)) {
    return;
  }
  texture_ = texture;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setClarity(double clarity) {
  clarity = clamp_signed_percent(clarity);
  if (qFuzzyCompare(clarity_ + 1.0, clarity + 1.0)) {
    return;
  }
  clarity_ = clarity;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setDehaze(double dehaze) {
  dehaze = clamp_signed_percent(dehaze);
  if (qFuzzyCompare(dehaze_ + 1.0, dehaze + 1.0)) {
    return;
  }
  dehaze_ = dehaze;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setVibrance(double vibrance) {
  vibrance = clamp_signed_percent(vibrance);
  if (qFuzzyCompare(vibrance_ + 1.0, vibrance + 1.0)) {
    return;
  }
  vibrance_ = vibrance;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setSaturation(double saturation) {
  saturation = clamp_signed_percent(saturation);
  if (qFuzzyCompare(saturation_ + 1.0, saturation + 1.0)) {
    return;
  }
  saturation_ = saturation;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setCurveShadows(double curve_shadows) {
  curve_shadows = clamp_signed_percent(curve_shadows);
  if (qFuzzyCompare(curve_shadows_ + 1.0, curve_shadows + 1.0)) {
    return;
  }
  curve_shadows_ = curve_shadows;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setCurveDarks(double curve_darks) {
  curve_darks = clamp_signed_percent(curve_darks);
  if (qFuzzyCompare(curve_darks_ + 1.0, curve_darks + 1.0)) {
    return;
  }
  curve_darks_ = curve_darks;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setCurveLights(double curve_lights) {
  curve_lights = clamp_signed_percent(curve_lights);
  if (qFuzzyCompare(curve_lights_ + 1.0, curve_lights + 1.0)) {
    return;
  }
  curve_lights_ = curve_lights;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setCurveHighlights(double curve_highlights) {
  curve_highlights = clamp_signed_percent(curve_highlights);
  if (qFuzzyCompare(curve_highlights_ + 1.0, curve_highlights + 1.0)) {
    return;
  }
  curve_highlights_ = curve_highlights;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setHslChannel(const QString& hsl_channel) {
  const auto normalized = hsl_channel.isEmpty() ? QStringLiteral("Reds") : hsl_channel;
  if (hsl_channel_ == normalized) {
    return;
  }
  hsl_channel_ = normalized;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setHueAdjustment(double hue_adjustment) {
  hue_adjustment = clamp_signed_percent(hue_adjustment);
  if (qFuzzyCompare(hue_adjustment_ + 1.0, hue_adjustment + 1.0)) {
    return;
  }
  hue_adjustment_ = hue_adjustment;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setSaturationAdjustment(double saturation_adjustment) {
  saturation_adjustment = clamp_signed_percent(saturation_adjustment);
  if (qFuzzyCompare(saturation_adjustment_ + 1.0, saturation_adjustment + 1.0)) {
    return;
  }
  saturation_adjustment_ = saturation_adjustment;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setLuminanceAdjustment(double luminance_adjustment) {
  luminance_adjustment = clamp_signed_percent(luminance_adjustment);
  if (qFuzzyCompare(luminance_adjustment_ + 1.0, luminance_adjustment + 1.0)) {
    return;
  }
  luminance_adjustment_ = luminance_adjustment;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setCropZoom(double crop_zoom) {
  crop_zoom = qBound(1.0, crop_zoom, 3.0);
  if (qFuzzyCompare(crop_zoom_, crop_zoom)) {
    return;
  }
  crop_zoom_ = crop_zoom;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setCropPreset(const QString& crop_preset) {
  const auto normalized =
      crop_preset.isEmpty() ? QStringLiteral("Original") : crop_preset;
  if (crop_preset_ == normalized) {
    return;
  }
  crop_preset_ = normalized;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setLinearMaskEnabled(bool enabled) {
  if (linear_mask_enabled_ == enabled) {
    return;
  }
  linear_mask_enabled_ = enabled;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setLinearMaskExposure(double local_exposure) {
  local_exposure = qBound(-2.0, local_exposure, 2.0);
  if (qFuzzyCompare(linear_mask_exposure_, local_exposure)) {
    return;
  }
  linear_mask_exposure_ = local_exposure;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setLinearMaskTemperature(double local_temperature) {
  local_temperature = clamp_signed_percent(local_temperature);
  if (qFuzzyCompare(linear_mask_temperature_ + 1.0, local_temperature + 1.0)) {
    return;
  }
  linear_mask_temperature_ = local_temperature;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setLinearMaskFeather(double local_feather) {
  local_feather = qBound(0.05, local_feather, 1.0);
  if (qFuzzyCompare(linear_mask_feather_, local_feather)) {
    return;
  }
  linear_mask_feather_ = local_feather;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setLinearMaskPosition(double local_position) {
  local_position = qBound(0.0, local_position, 1.0);
  if (qFuzzyCompare(linear_mask_position_, local_position)) {
    return;
  }
  linear_mask_position_ = local_position;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setLinearMaskRotation(double local_rotation) {
  local_rotation = qBound(-180.0, local_rotation, 180.0);
  if (qFuzzyCompare(linear_mask_rotation_ + 360.0, local_rotation + 360.0)) {
    return;
  }
  linear_mask_rotation_ = local_rotation;
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::loadAsset(
    const QString& asset_id,
    const QString& file_path,
    const QString& thumbnail_source,
    const QString& camera_label,
    const QString& capture_date,
    const QString& lens_model,
    const QString& recipe_json) {
  persistCurrentState();
  asset_id_ = asset_id;
  file_path_ = file_path;
  fallback_thumbnail_source_ = thumbnail_source;
  camera_label_ = camera_label;
  capture_date_ = capture_date;
  lens_model_ = lens_model;
  restoreStateForAsset(asset_id_, recipe_json);
  setWhiteBalanceSampling(false);
  qInfo().noquote() << "EditorPreviewController::loadAsset"
                    << "asset_id=" << asset_id_
                    << "file_path=" << file_path_
                    << "thumbnail=" << fallback_thumbnail_source_;
  refreshDebugInfo();
  emit assetChanged();
  renderPreview();
}

void EditorPreviewController::resetAdjustments() {
  exposure_ = 0.0;
  contrast_ = 0.0;
  highlights_ = 0.0;
  shadows_ = 0.0;
  whites_ = 0.0;
  blacks_ = 0.0;
  temperature_ = 0.0;
  tint_ = 0.0;
  texture_ = 0.0;
  clarity_ = 0.0;
  dehaze_ = 0.0;
  vibrance_ = 0.0;
  saturation_ = 0.0;
  curve_shadows_ = 0.0;
  curve_darks_ = 0.0;
  curve_lights_ = 0.0;
  curve_highlights_ = 0.0;
  hsl_channel_ = QStringLiteral("Reds");
  hue_adjustment_ = 0.0;
  saturation_adjustment_ = 0.0;
  luminance_adjustment_ = 0.0;
  crop_zoom_ = 1.0;
  crop_preset_ = QStringLiteral("Original");
  persistCurrentState();
  emit controlsChanged();
  renderPreview();
}

void EditorPreviewController::setWhiteBalanceSampling(bool enabled) {
  if (white_balance_sampling_ == enabled) {
    return;
  }
  white_balance_sampling_ = enabled;
  emit whiteBalanceSamplingChanged();
}

void EditorPreviewController::sampleWhiteBalanceAt(double normalized_x, double normalized_y) {
  if (!white_balance_sampling_) {
    return;
  }

  QImage image;
  if (!decodeSourceImage(&image, 0)) {
    setStatus(QStringLiteral("Unable to decode source for white balance sampling."));
    setWhiteBalanceSampling(false);
    return;
  }

  image = applyCrop(image);

  const auto sample_x = qBound(0, static_cast<int>(qRound(normalized_x * (image.width() - 1))), image.width() - 1);
  const auto sample_y = qBound(0, static_cast<int>(qRound(normalized_y * (image.height() - 1))), image.height() - 1);
  const QColor sampled(image.pixel(sample_x, sample_y));

  const auto red = srgb_to_linear(sampled.redF());
  const auto green = srgb_to_linear(sampled.greenF());
  const auto blue = srgb_to_linear(sampled.blueF());
  const auto average_rb = qMax(1e-6, (red + blue) * 0.5);
  const auto red_blue_balance = std::log2((blue + 1e-6) / (red + 1e-6));
  const auto green_balance = std::log2((green + 1e-6) / average_rb);

  setTemperature(temperature_ + red_blue_balance * 55.0);
  setTint(tint_ + green_balance * 60.0);
  setWhiteBalanceSampling(false);
  setStatus(QStringLiteral("White balance sampled from preview."));
}

void EditorPreviewController::renderPreview() {
  ++render_generation_;
  qInfo().noquote() << "EditorPreviewController::renderPreview"
                    << "file_path=" << file_path_
                    << "has_adjustments=" << hasVisibleAdjustments();
  if (!preview_supported(file_path_)) {
    preview_source_ = fallback_thumbnail_source_;
    reference_preview_source_ = compare_mode_ ? fallback_thumbnail_source_ : QString();
    setStatus(hasAsset() ? QStringLiteral("Preview fallback active.") : QStringLiteral("No asset selected."));
    interactive_render_timer_.stop();
    full_render_timer_.stop();
    interactive_request_pending_ = false;
    full_request_pending_ = false;
    updateProcessingState();
    refreshDebugInfo();
    emit previewChanged();
    return;
  }

  const QFileInfo source_info(file_path_);
  if (!source_info.exists() || !source_info.isReadable()) {
    preview_source_ = fallback_thumbnail_source_;
    reference_preview_source_ = compare_mode_ ? fallback_thumbnail_source_ : QString();
    setStatus(QStringLiteral("Source file unavailable."));
    interactive_render_timer_.stop();
    full_render_timer_.stop();
    interactive_request_pending_ = false;
    full_request_pending_ = false;
    updateProcessingState();
    refreshDebugInfo();
    emit previewChanged();
    return;
  }

  setStatus(hasVisibleAdjustments() ? QStringLiteral("Rendering interactive preview...") : QStringLiteral("Rendering base preview..."));
  interactive_render_timer_.start();
  full_render_timer_.start();
  updateProcessingState();
}

void EditorPreviewController::renderPreviewStage(bool full_resolution) {
  const auto request = currentRenderRequest(full_resolution);
  if (request.file_path.isEmpty()) {
    updateProcessingState();
    return;
  }

  qInfo().noquote() << "EditorPreviewController::renderPreviewStage"
                    << (full_resolution ? "full" : "interactive")
                    << "file_path=" << request.file_path
                    << "generation=" << request.generation;

  auto& watcher = full_resolution ? full_render_watcher_ : interactive_render_watcher_;
  auto& pending_request = full_resolution ? pending_full_request_ : pending_interactive_request_;
  auto& pending = full_resolution ? full_request_pending_ : interactive_request_pending_;

  if (watcher.isRunning()) {
    pending_request = request;
    pending = true;
    updateProcessingState();
    return;
  }

  startRenderStage(full_resolution, request);
}

void EditorPreviewController::handleRenderFinished(bool full_resolution) {
  auto& watcher = full_resolution ? full_render_watcher_ : interactive_render_watcher_;
  const auto result = watcher.result();

  if (result.generation == render_generation_) {
    if (!result.success) {
      preview_source_ = fallback_thumbnail_source_;
      reference_preview_source_ = compare_mode_ ? fallback_thumbnail_source_ : QString();
      setStatus(result.status);
    } else {
      ++preview_revision_;
      preview_source_ = with_preview_revision(result.preview_source, preview_revision_);
      reference_preview_source_ =
          with_preview_revision(result.reference_preview_source, preview_revision_);
      setStatus(result.status);
    }
    refreshDebugInfo();
    emit previewChanged();
  }

  auto& pending_request = full_resolution ? pending_full_request_ : pending_interactive_request_;
  auto& pending = full_resolution ? full_request_pending_ : interactive_request_pending_;
  if (pending) {
    pending = false;
    startRenderStage(full_resolution, pending_request);
    return;
  }

  updateProcessingState();
}

void EditorPreviewController::startRenderStage(bool full_resolution, const RenderRequest& request) {
  auto& watcher = full_resolution ? full_render_watcher_ : interactive_render_watcher_;
  watcher.setFuture(QtConcurrent::run([request]() { return render_preview_request(request); }));
  updateProcessingState();
}

void EditorPreviewController::updateProcessingState() {
  const auto busy = interactive_render_timer_.isActive() ||
                    full_render_timer_.isActive() ||
                    interactive_render_watcher_.isRunning() ||
                    full_render_watcher_.isRunning() ||
                    interactive_request_pending_ ||
                    full_request_pending_;
  setProcessing(busy);
}

RenderRequest EditorPreviewController::currentRenderRequest(bool full_resolution) const {
  return RenderRequest{
      .file_path = file_path_,
      .fallback_thumbnail_source = fallback_thumbnail_source_,
      .state = currentState(),
      .compare_mode = compare_mode_,
      .full_resolution = full_resolution,
      .max_edge = full_resolution ? 0 : kInteractivePreviewEdge,
      .jpeg_quality = full_resolution ? 90 : 78,
      .generation = render_generation_,
  };
}

void EditorPreviewController::refreshDebugInfo() {
  const QFileInfo source_info(file_path_);
  const auto info = QStringLiteral("status=%1 | file=%2 | exists=%3 | readable=%4 | thumb=%5 | preview=%6 | reference=%7")
                        .arg(status_)
                        .arg(file_path_.isEmpty() ? QStringLiteral("<none>") : file_path_)
                        .arg(source_info.exists() ? QStringLiteral("yes") : QStringLiteral("no"))
                        .arg(source_info.isReadable() ? QStringLiteral("yes") : QStringLiteral("no"))
                        .arg(fallback_thumbnail_source_.isEmpty() ? QStringLiteral("<none>") : fallback_thumbnail_source_)
                        .arg(preview_source_.isEmpty() ? QStringLiteral("<none>") : preview_source_)
                        .arg(reference_preview_source_.isEmpty() ? QStringLiteral("<none>") : reference_preview_source_);
  if (debug_info_ == info) {
    return;
  }
  debug_info_ = info;
  emit debugInfoChanged();
}

void EditorPreviewController::setStatus(const QString& status) {
  if (status_ == status) {
    return;
  }
  status_ = status;
  emit statusChanged();
}

void EditorPreviewController::setProcessing(bool processing) {
  if (processing_ == processing) {
    return;
  }
  processing_ = processing;
  emit processingChanged();
}

QString EditorPreviewController::cachePathForSelection(bool edited, bool full_resolution) const {
  return cache_path_for_selection(file_path_, currentState(), edited, full_resolution);
}

void EditorPreviewController::persistCurrentState() {
  if (asset_id_.isEmpty()) {
    return;
  }
  asset_states_.insert(asset_id_, currentState());
  persist_timer_.start();
}

void EditorPreviewController::restoreStateForAsset(const QString& asset_id, const QString& recipe_json) {
  if (asset_id.isEmpty()) {
    applyState(EditState{});
    return;
  }

  if (asset_states_.contains(asset_id)) {
    applyState(asset_states_.value(asset_id));
    return;
  }

  applyState(stateFromRecipeJson(recipe_json));
}

void EditorPreviewController::applyState(const EditState& state) {
  exposure_ = state.exposure;
  contrast_ = state.contrast;
  highlights_ = state.highlights;
  shadows_ = state.shadows;
  whites_ = state.whites;
  blacks_ = state.blacks;
  temperature_ = state.temperature;
  tint_ = state.tint;
  texture_ = state.texture;
  clarity_ = state.clarity;
  dehaze_ = state.dehaze;
  vibrance_ = state.vibrance;
  saturation_ = state.saturation;
  curve_shadows_ = state.curve_shadows;
  curve_darks_ = state.curve_darks;
  curve_lights_ = state.curve_lights;
  curve_highlights_ = state.curve_highlights;
  hsl_channel_ = state.hsl_channel;
  hue_adjustment_ = state.hue_adjustment;
  saturation_adjustment_ = state.saturation_adjustment;
  luminance_adjustment_ = state.luminance_adjustment;
  crop_zoom_ = state.crop_zoom;
  crop_preset_ = state.crop_preset;
  linear_mask_enabled_ = state.linear_mask_enabled;
  linear_mask_exposure_ = state.linear_mask_exposure;
  linear_mask_temperature_ = state.linear_mask_temperature;
  linear_mask_feather_ = state.linear_mask_feather;
  linear_mask_position_ = state.linear_mask_position;
  linear_mask_rotation_ = state.linear_mask_rotation;
  emit controlsChanged();
}

EditState EditorPreviewController::currentState() const {
  return EditState{
      .exposure = exposure_,
      .contrast = contrast_,
      .highlights = highlights_,
      .shadows = shadows_,
      .whites = whites_,
      .blacks = blacks_,
      .temperature = temperature_,
      .tint = tint_,
      .texture = texture_,
      .clarity = clarity_,
      .dehaze = dehaze_,
      .vibrance = vibrance_,
      .saturation = saturation_,
      .curve_shadows = curve_shadows_,
      .curve_darks = curve_darks_,
      .curve_lights = curve_lights_,
      .curve_highlights = curve_highlights_,
      .hsl_channel = hsl_channel_,
      .hue_adjustment = hue_adjustment_,
      .saturation_adjustment = saturation_adjustment_,
      .luminance_adjustment = luminance_adjustment_,
      .crop_zoom = crop_zoom_,
      .crop_preset = crop_preset_,
      .linear_mask_enabled = linear_mask_enabled_,
      .linear_mask_exposure = linear_mask_exposure_,
      .linear_mask_temperature = linear_mask_temperature_,
      .linear_mask_feather = linear_mask_feather_,
      .linear_mask_position = linear_mask_position_,
      .linear_mask_rotation = linear_mask_rotation_,
  };
}

EditState EditorPreviewController::stateFromRecipeJson(const QString& recipe_json) const {
  if (recipe_json.trimmed().isEmpty()) {
    return EditState{};
  }

  const auto json_document = QJsonDocument::fromJson(recipe_json.toUtf8());
  if (!json_document.isObject()) {
    return EditState{};
  }

  const auto object = json_document.object();
  EditState state;
  state.exposure = object.value(QStringLiteral("exposure")).toDouble();
  state.contrast = object.value(QStringLiteral("contrast")).toDouble();
  state.highlights = object.value(QStringLiteral("highlights")).toDouble();
  state.shadows = object.value(QStringLiteral("shadows")).toDouble();
  state.whites = object.value(QStringLiteral("whites")).toDouble();
  state.blacks = object.value(QStringLiteral("blacks")).toDouble();
  state.temperature = object.value(QStringLiteral("temperature")).toDouble();
  state.tint = object.value(QStringLiteral("tint")).toDouble();
  state.texture = object.value(QStringLiteral("texture")).toDouble();
  state.clarity = object.value(QStringLiteral("clarity")).toDouble();
  state.dehaze = object.value(QStringLiteral("dehaze")).toDouble();
  state.vibrance = object.value(QStringLiteral("vibrance")).toDouble();
  state.saturation = object.value(QStringLiteral("saturation")).toDouble();
  state.curve_shadows = object.value(QStringLiteral("curveShadows")).toDouble();
  state.curve_darks = object.value(QStringLiteral("curveDarks")).toDouble();
  state.curve_lights = object.value(QStringLiteral("curveLights")).toDouble();
  state.curve_highlights = object.value(QStringLiteral("curveHighlights")).toDouble();
  state.hsl_channel = object.value(QStringLiteral("hslChannel")).toString(QStringLiteral("Reds"));
  state.hue_adjustment = object.value(QStringLiteral("hueAdjustment")).toDouble();
  state.saturation_adjustment = object.value(QStringLiteral("saturationAdjustment")).toDouble();
  state.luminance_adjustment = object.value(QStringLiteral("luminanceAdjustment")).toDouble();
  state.crop_zoom = object.value(QStringLiteral("cropZoom")).toDouble(1.0);
  state.crop_preset = object.value(QStringLiteral("cropPreset")).toString(QStringLiteral("Original"));
  state.linear_mask_enabled = object.value(QStringLiteral("linearMaskEnabled")).toBool(false);
  state.linear_mask_exposure = object.value(QStringLiteral("linearMaskExposure")).toDouble();
  state.linear_mask_temperature = object.value(QStringLiteral("linearMaskTemperature")).toDouble();
  state.linear_mask_feather = object.value(QStringLiteral("linearMaskFeather")).toDouble(0.22);
  state.linear_mask_position = object.value(QStringLiteral("linearMaskPosition")).toDouble(0.5);
  state.linear_mask_rotation = object.value(QStringLiteral("linearMaskRotation")).toDouble();
  return state;
}

QString EditorPreviewController::recipeJsonForState(const EditState& state) const {
  QJsonObject object;
  object.insert(QStringLiteral("exposure"), state.exposure);
  object.insert(QStringLiteral("contrast"), state.contrast);
  object.insert(QStringLiteral("highlights"), state.highlights);
  object.insert(QStringLiteral("shadows"), state.shadows);
  object.insert(QStringLiteral("whites"), state.whites);
  object.insert(QStringLiteral("blacks"), state.blacks);
  object.insert(QStringLiteral("temperature"), state.temperature);
  object.insert(QStringLiteral("tint"), state.tint);
  object.insert(QStringLiteral("texture"), state.texture);
  object.insert(QStringLiteral("clarity"), state.clarity);
  object.insert(QStringLiteral("dehaze"), state.dehaze);
  object.insert(QStringLiteral("vibrance"), state.vibrance);
  object.insert(QStringLiteral("saturation"), state.saturation);
  object.insert(QStringLiteral("curveShadows"), state.curve_shadows);
  object.insert(QStringLiteral("curveDarks"), state.curve_darks);
  object.insert(QStringLiteral("curveLights"), state.curve_lights);
  object.insert(QStringLiteral("curveHighlights"), state.curve_highlights);
  object.insert(QStringLiteral("hslChannel"), state.hsl_channel);
  object.insert(QStringLiteral("hueAdjustment"), state.hue_adjustment);
  object.insert(QStringLiteral("saturationAdjustment"), state.saturation_adjustment);
  object.insert(QStringLiteral("luminanceAdjustment"), state.luminance_adjustment);
  object.insert(QStringLiteral("cropZoom"), state.crop_zoom);
  object.insert(QStringLiteral("cropPreset"), state.crop_preset);
  object.insert(QStringLiteral("linearMaskEnabled"), state.linear_mask_enabled);
  object.insert(QStringLiteral("linearMaskExposure"), state.linear_mask_exposure);
  object.insert(QStringLiteral("linearMaskTemperature"), state.linear_mask_temperature);
  object.insert(QStringLiteral("linearMaskFeather"), state.linear_mask_feather);
  object.insert(QStringLiteral("linearMaskPosition"), state.linear_mask_position);
  object.insert(QStringLiteral("linearMaskRotation"), state.linear_mask_rotation);
  return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

void EditorPreviewController::flushPersistedState() {
  if (asset_id_.isEmpty()) {
    return;
  }

  if (persist_process_.state() != QProcess::NotRunning) {
    persist_timer_.start();
    return;
  }

  const auto recipe_json = recipeJsonForState(currentState());
  persist_process_.start(saveRecipeProgram(), saveRecipeArguments(asset_id_, recipe_json));
}

bool EditorPreviewController::hasVisibleAdjustments() const {
  return has_visible_adjustments(currentState());
}

bool EditorPreviewController::decodeSourceImage(QImage* image, int max_edge) const {
  return decode_source_image(file_path_, image, max_edge);
}

QImage EditorPreviewController::applyCrop(const QImage& image) const {
  return apply_crop(image, currentState());
}

void EditorPreviewController::applyColorAdjustments(QImage* image) const {
  apply_color_adjustments(image, currentState());
}

bool EditorPreviewController::channelMatchesSelection(const QColor& color) const {
  return channel_weight_for_selection(color.hslHueF(), hsl_channel_) > 0.0;
}

QString EditorPreviewController::saveRecipeProgram() const {
  if (useBuiltSaveRecipeBinary()) {
    return builtBinaryPath(QStringLiteral("save_edit_recipe"));
  }

  return QStringLiteral("cargo");
}

QStringList EditorPreviewController::saveRecipeArguments(const QString& asset_id, const QString& recipe_json) const {
  if (useBuiltSaveRecipeBinary()) {
    return {asset_id, recipe_json};
  }

  return {
      QStringLiteral("run"),
      QStringLiteral("--quiet"),
      QStringLiteral("--manifest-path"),
      QStringLiteral("rust/Cargo.toml"),
      QStringLiteral("-p"),
      QStringLiteral("photo-catalog"),
      QStringLiteral("--bin"),
      QStringLiteral("save_edit_recipe"),
      QStringLiteral("--"),
      asset_id,
      recipe_json,
  };
}

bool EditorPreviewController::useBuiltSaveRecipeBinary() const {
  const QFileInfo binary(builtBinaryPath(QStringLiteral("save_edit_recipe")));
  return binary.exists() && binary.isExecutable();
}

QString EditorPreviewController::builtBinaryPath(const QString& binary_name) const {
  return QDir::current().absoluteFilePath(QStringLiteral("rust/target/debug/%1").arg(binary_name));
}

}  // namespace photo_editor::desktop
