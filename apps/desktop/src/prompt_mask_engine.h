#pragma once

#include <QByteArray>
#include <QImage>
#include <QRect>
#include <QString>
#include <QVector>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <queue>

namespace photo_editor::desktop::prompt_mask_engine {

struct PromptPoint final {
  double x = 0.5;
  double y = 0.5;
  bool negative = false;
};

struct PromptMaskResult final {
  QByteArray mask_data;
  int width = 0;
  int height = 0;
  bool success = false;
  QString error_message;
};

namespace detail {

struct ColorSample final {
  double red = 0.0;
  double green = 0.0;
  double blue = 0.0;
};

inline double clamp_unit(double value) {
  return std::clamp(value, 0.0, 1.0);
}

inline int clamp_index(int value, int upper_bound) {
  return std::clamp(value, 0, std::max(upper_bound, 0));
}

inline ColorSample sample_color(const QImage& image, int x, int y) {
  const auto clamped_x = clamp_index(x, image.width() - 1);
  const auto clamped_y = clamp_index(y, image.height() - 1);
  const auto pixel = image.pixel(clamped_x, clamped_y);
  return ColorSample{
      .red = static_cast<double>(qRed(pixel)) / 255.0,
      .green = static_cast<double>(qGreen(pixel)) / 255.0,
      .blue = static_cast<double>(qBlue(pixel)) / 255.0,
  };
}

inline double color_distance(const ColorSample& left, const ColorSample& right) {
  const auto delta_red = left.red - right.red;
  const auto delta_green = left.green - right.green;
  const auto delta_blue = left.blue - right.blue;
  return std::sqrt(
      delta_red * delta_red * 0.30 +
      delta_green * delta_green * 0.59 +
      delta_blue * delta_blue * 0.11);
}

inline ColorSample average_color(const QVector<ColorSample>& samples) {
  if (samples.isEmpty()) {
    return {};
  }

  ColorSample average;
  for (const auto& sample : samples) {
    average.red += sample.red;
    average.green += sample.green;
    average.blue += sample.blue;
  }
  const auto scale = 1.0 / static_cast<double>(samples.size());
  average.red *= scale;
  average.green *= scale;
  average.blue *= scale;
  return average;
}

inline double color_spread(
    const QVector<ColorSample>& samples,
    const ColorSample& average) {
  if (samples.isEmpty()) {
    return 0.0;
  }

  auto spread = 0.0;
  for (const auto& sample : samples) {
    spread += color_distance(sample, average);
  }
  return spread / static_cast<double>(samples.size());
}

inline QVector<ColorSample> patch_samples(
    const QImage& image,
    int center_x,
    int center_y,
    int radius) {
  QVector<ColorSample> samples;
  const auto min_x = std::max(0, center_x - radius);
  const auto max_x = std::min(image.width() - 1, center_x + radius);
  const auto min_y = std::max(0, center_y - radius);
  const auto max_y = std::min(image.height() - 1, center_y + radius);
  samples.reserve((max_x - min_x + 1) * (max_y - min_y + 1));
  for (int y = min_y; y <= max_y; ++y) {
    for (int x = min_x; x <= max_x; ++x) {
      samples.append(sample_color(image, x, y));
    }
  }
  return samples;
}

inline QVector<QPoint> positive_prompt_pixels(
    const QVector<PromptPoint>& prompts,
    int width,
    int height) {
  QVector<QPoint> pixels;
  for (const auto& prompt : prompts) {
    if (prompt.negative) {
      continue;
    }
    pixels.append(QPoint(
        clamp_index(
            static_cast<int>(std::lround(clamp_unit(prompt.x) *
                                         static_cast<double>(std::max(width - 1, 0)))),
            width - 1),
        clamp_index(
            static_cast<int>(std::lround(clamp_unit(prompt.y) *
                                         static_cast<double>(std::max(height - 1, 0)))),
            height - 1)));
  }
  return pixels;
}

inline QVector<QPoint> negative_prompt_pixels(
    const QVector<PromptPoint>& prompts,
    int width,
    int height) {
  QVector<QPoint> pixels;
  for (const auto& prompt : prompts) {
    if (!prompt.negative) {
      continue;
    }
    pixels.append(QPoint(
        clamp_index(
            static_cast<int>(std::lround(clamp_unit(prompt.x) *
                                         static_cast<double>(std::max(width - 1, 0)))),
            width - 1),
        clamp_index(
            static_cast<int>(std::lround(clamp_unit(prompt.y) *
                                         static_cast<double>(std::max(height - 1, 0)))),
            height - 1)));
  }
  return pixels;
}

inline QRect prompt_bounds(
    const QVector<QPoint>& positive_pixels,
    int width,
    int height) {
  auto min_x = width - 1;
  auto max_x = 0;
  auto min_y = height - 1;
  auto max_y = 0;
  for (const auto& point : positive_pixels) {
    min_x = std::min(min_x, point.x());
    max_x = std::max(max_x, point.x());
    min_y = std::min(min_y, point.y());
    max_y = std::max(max_y, point.y());
  }

  const auto stroke_width = std::max(1, max_x - min_x + 1);
  const auto stroke_height = std::max(1, max_y - min_y + 1);
  const auto stroke_diagonal = std::hypot(
      static_cast<double>(stroke_width),
      static_cast<double>(stroke_height));
  const auto min_edge = static_cast<double>(std::min(width, height));
  const auto margin = clamp_index(
      static_cast<int>(std::lround(std::clamp(
          std::max(min_edge * 0.14, stroke_diagonal * 0.40 + 24.0),
          42.0,
          min_edge * 0.42))),
      std::max(width, height));

  const auto expanded = QRect(
      std::max(0, min_x - margin),
      std::max(0, min_y - margin),
      std::min(width - 1, max_x + margin) - std::max(0, min_x - margin) + 1,
      std::min(height - 1, max_y + margin) - std::max(0, min_y - margin) + 1);
  return expanded.intersected(QRect(0, 0, width, height));
}

inline QVector<ColorSample> perimeter_samples(const QImage& image, const QRect& bounds) {
  QVector<ColorSample> samples;
  if (!bounds.isValid()) {
    return samples;
  }

  const auto step = std::max(2, std::min(bounds.width(), bounds.height()) / 18);
  for (int x = bounds.left(); x <= bounds.right(); x += step) {
    samples.append(sample_color(image, x, bounds.top()));
    samples.append(sample_color(image, x, bounds.bottom()));
  }
  for (int y = bounds.top(); y <= bounds.bottom(); y += step) {
    samples.append(sample_color(image, bounds.left(), y));
    samples.append(sample_color(image, bounds.right(), y));
  }
  return samples;
}

inline QVector<ColorSample> ring_samples(
    const QImage& image,
    const QPoint& center,
    int radius) {
  QVector<ColorSample> samples;
  if (radius <= 0) {
    return samples;
  }

  constexpr std::array<double, 16> kAngles = {
      0.0, 22.5, 45.0, 67.5, 90.0, 112.5, 135.0, 157.5,
      180.0, 202.5, 225.0, 247.5, 270.0, 292.5, 315.0, 337.5,
  };
  constexpr double kPi = 3.14159265358979323846;
  samples.reserve(static_cast<qsizetype>(kAngles.size()));
  for (const auto angle_degrees : kAngles) {
    const auto radians = angle_degrees * kPi / 180.0;
    const auto sample_x = center.x() + static_cast<int>(std::lround(std::cos(radians) * radius));
    const auto sample_y = center.y() + static_cast<int>(std::lround(std::sin(radians) * radius));
    samples.append(sample_color(image, sample_x, sample_y));
  }
  return samples;
}

inline double minimum_distance_to_samples(
    const ColorSample& sample,
    const QVector<ColorSample>& reference_samples) {
  if (reference_samples.isEmpty()) {
    return std::numeric_limits<double>::max();
  }

  auto best = std::numeric_limits<double>::max();
  for (const auto& reference : reference_samples) {
    best = std::min(best, color_distance(sample, reference));
  }
  return best;
}

inline double local_gradient(const QImage& image, int x, int y) {
  const auto center = sample_color(image, x, y);
  auto gradient = 0.0;
  gradient = std::max(gradient, color_distance(center, sample_color(image, x - 1, y)));
  gradient = std::max(gradient, color_distance(center, sample_color(image, x + 1, y)));
  gradient = std::max(gradient, color_distance(center, sample_color(image, x, y - 1)));
  gradient = std::max(gradient, color_distance(center, sample_color(image, x, y + 1)));
  return gradient;
}

inline QByteArray blur_mask(
    const QByteArray& binary_mask,
    const QRect& bounds,
    int width,
    int height,
    const QVector<QPoint>& seed_points) {
  QByteArray blurred(binary_mask.size(), '\0');
  const auto index_for = [width](int x, int y) {
    return y * width + x;
  };

  for (int y = bounds.top(); y <= bounds.bottom(); ++y) {
    for (int x = bounds.left(); x <= bounds.right(); ++x) {
      auto sum = 0;
      auto count = 0;
      for (int offset_y = -1; offset_y <= 1; ++offset_y) {
        for (int offset_x = -1; offset_x <= 1; ++offset_x) {
          const auto sample_x = std::clamp(x + offset_x, 0, width - 1);
          const auto sample_y = std::clamp(y + offset_y, 0, height - 1);
          sum += static_cast<unsigned char>(binary_mask.at(index_for(sample_x, sample_y)));
          ++count;
        }
      }
      blurred[index_for(x, y)] = static_cast<char>(sum / std::max(count, 1));
    }
  }

  for (const auto& seed : seed_points) {
    if (seed.x() < 0 || seed.y() < 0 || seed.x() >= width || seed.y() >= height) {
      continue;
    }
    blurred[index_for(seed.x(), seed.y())] = static_cast<char>(0xff);
  }
  return blurred;
}

}  // namespace detail

inline PromptMaskResult generate_mask(
    const QImage& source_image,
    const QVector<PromptPoint>& prompts) {
  PromptMaskResult result;
  if (source_image.isNull() || source_image.width() < 2 || source_image.height() < 2) {
    result.error_message = QStringLiteral("Invalid source image");
    return result;
  }

  const auto image = source_image.convertToFormat(QImage::Format_ARGB32);
  const auto width = image.width();
  const auto height = image.height();
  const auto positive_pixels = detail::positive_prompt_pixels(prompts, width, height);
  if (positive_pixels.isEmpty()) {
    result.error_message = QStringLiteral("No smart-selection prompts were provided");
    return result;
  }

  const auto negative_pixels = detail::negative_prompt_pixels(prompts, width, height);
  const auto bounds = detail::prompt_bounds(positive_pixels, width, height);
  if (!bounds.isValid()) {
    result.error_message = QStringLiteral("Smart-selection bounds are invalid");
    return result;
  }

  QByteArray binary_mask(width * height, '\0');
  auto selected_count = 0;
  const auto image_index = [width](int x, int y) {
    return y * width + x;
  };

  for (const auto& seed : positive_pixels) {
    QVector<detail::ColorSample> positive_samples =
        detail::patch_samples(image, seed.x(), seed.y(), 1);
    if (positive_samples.isEmpty()) {
      continue;
    }

    const auto local_bounds =
        detail::prompt_bounds(QVector<QPoint>{seed}, width, height).intersected(bounds);
    if (!local_bounds.isValid()) {
      continue;
    }

    QVector<detail::ColorSample> background_samples =
        detail::perimeter_samples(image, local_bounds);
    const auto prompt_ring_radius = std::clamp(
        std::max(local_bounds.width(), local_bounds.height()) + 8,
        12,
        std::max(width, height) / 3);
    background_samples += detail::ring_samples(image, seed, prompt_ring_radius);
    for (const auto& pixel : negative_pixels) {
      const auto patch = detail::patch_samples(image, pixel.x(), pixel.y(), 2);
      background_samples += patch;
    }
    if (background_samples.isEmpty()) {
      background_samples =
          detail::perimeter_samples(image, QRect(0, 0, width, height));
    }

    const auto average_positive = detail::average_color(positive_samples);
    const auto average_background = detail::average_color(background_samples);
    const auto positive_spread = detail::color_spread(positive_samples, average_positive);
    const auto foreground_threshold =
        std::clamp(0.10 + positive_spread * 1.85, 0.08, 0.24);
    const auto background_bias = std::max(
        0.02,
        detail::color_distance(average_positive, average_background) * 0.16);
    const auto edge_threshold =
        std::clamp(0.14 + positive_spread * 1.75, 0.12, 0.32);
    const auto gradient_threshold =
        std::clamp(0.17 + positive_spread * 1.50, 0.14, 0.34);

    QVector<uint8_t> visited(
        static_cast<qsizetype>(local_bounds.width()) *
            static_cast<qsizetype>(local_bounds.height()),
        0);
    const auto local_index = [&local_bounds](int x, int y) {
      return (y - local_bounds.top()) * local_bounds.width() +
             (x - local_bounds.left());
    };

    std::queue<QPoint> frontier;
    frontier.push(seed);

    while (!frontier.empty()) {
      const auto point = frontier.front();
      frontier.pop();
      if (!local_bounds.contains(point)) {
        continue;
      }

      const auto visit_index = local_index(point.x(), point.y());
      if (visited[visit_index] != 0) {
        continue;
      }
      visited[visit_index] = 1;

      const auto sample = detail::sample_color(image, point.x(), point.y());
      const auto foreground_distance =
          detail::minimum_distance_to_samples(sample, positive_samples);
      const auto background_distance =
          detail::minimum_distance_to_samples(sample, background_samples);
      const auto gradient = detail::local_gradient(image, point.x(), point.y());
      const auto strong_seed_match = foreground_distance <= foreground_threshold * 0.60;
      const auto accepted =
          foreground_distance <= foreground_threshold &&
          foreground_distance + background_bias <= background_distance &&
          (gradient <= gradient_threshold || strong_seed_match);
      if (!accepted) {
        continue;
      }

      const auto mask_index = image_index(point.x(), point.y());
      if (binary_mask[mask_index] == '\0') {
        ++selected_count;
      }
      binary_mask[mask_index] = static_cast<char>(0xff);

      const auto current_color = sample;
      for (int offset_y = -1; offset_y <= 1; ++offset_y) {
        for (int offset_x = -1; offset_x <= 1; ++offset_x) {
          if (offset_x == 0 && offset_y == 0) {
            continue;
          }

          const auto neighbor_x = point.x() + offset_x;
          const auto neighbor_y = point.y() + offset_y;
          if (!local_bounds.contains(neighbor_x, neighbor_y)) {
            continue;
          }

          const auto neighbor_visit_index = local_index(neighbor_x, neighbor_y);
          if (visited[neighbor_visit_index] != 0) {
            continue;
          }

          const auto neighbor_color =
              detail::sample_color(image, neighbor_x, neighbor_y);
          const auto edge_cost =
              detail::color_distance(current_color, neighbor_color);
          const auto neighbor_foreground_distance =
              detail::minimum_distance_to_samples(
                  neighbor_color,
                  positive_samples);
          if (edge_cost <= edge_threshold ||
              neighbor_foreground_distance <= foreground_threshold * 0.70) {
            frontier.push(QPoint(neighbor_x, neighbor_y));
          }
        }
      }
    }
  }

  if (selected_count < 18) {
    result.error_message = QStringLiteral("No selectable object at that point");
    return result;
  }

  for (const auto& negative : negative_pixels) {
    const auto erase_radius = std::max(6, std::min(bounds.width(), bounds.height()) / 18);
    for (int y = negative.y() - erase_radius; y <= negative.y() + erase_radius; ++y) {
      for (int x = negative.x() - erase_radius; x <= negative.x() + erase_radius; ++x) {
        if (x < 0 || y < 0 || x >= width || y >= height) {
          continue;
        }
        const auto dx = static_cast<double>(x - negative.x());
        const auto dy = static_cast<double>(y - negative.y());
        if (std::hypot(dx, dy) > static_cast<double>(erase_radius)) {
          continue;
        }
        binary_mask[image_index(x, y)] = '\0';
      }
    }
  }

  result.mask_data = detail::blur_mask(binary_mask, bounds, width, height, positive_pixels);
  result.width = width;
  result.height = height;
  result.success = result.mask_data.contains(static_cast<char>(0xff)) ||
                   result.mask_data.contains(static_cast<char>(0x7f));
  if (!result.success) {
    result.error_message = QStringLiteral("No selectable object at that point");
  }
  return result;
}

}  // namespace photo_editor::desktop::prompt_mask_engine
