#include "retouch_mask_detection.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <queue>
#include <vector>

#include <QColor>
#include <QImage>

namespace photo_editor::desktop {
namespace {

double smoothstep(double edge0, double edge1, double value) {
  const auto t = std::clamp((value - edge0) / (edge1 - edge0), 0.0, 1.0);
  return t * t * (3.0 - 2.0 * t);
}

}  // namespace

RedEyeMaskResult detect_red_eye_mask(const QImage& image) {
  RedEyeMaskResult result;
  if (image.isNull() || image.width() < 8 || image.height() < 8) {
    return result;
  }

  const auto source = image.convertToFormat(QImage::Format_ARGB32);
  const auto width = source.width();
  const auto height = source.height();
  result.width = width;
  result.height = height;
  result.mask_data = QByteArray(width * height, '\0');

  std::vector<uint8_t> candidate(
      static_cast<std::size_t>(width * height), 0);
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const auto color = QColor::fromRgb(source.pixel(x, y));
      const auto red = color.redF();
      const auto green = color.greenF();
      const auto blue = color.blueF();
      const auto max_green_blue = std::max(green, blue);
      const auto min_green_blue = std::min(green, blue);
      if (red > 0.34 && red > max_green_blue * 1.55 &&
          red - max_green_blue > 0.16 &&
          max_green_blue - min_green_blue < 0.22) {
        candidate[static_cast<std::size_t>(y * width + x)] = 1;
      }
    }
  }

  std::vector<uint8_t> visited(
      static_cast<std::size_t>(width * height), 0);
  std::queue<int> queue;
  const auto pixel_count = width * height;
  const auto max_component_area = std::max(16, pixel_count / 20);
  for (int index = 0; index < pixel_count; ++index) {
    if (!candidate[static_cast<std::size_t>(index)] ||
        visited[static_cast<std::size_t>(index)]) {
      continue;
    }

    visited[static_cast<std::size_t>(index)] = 1;
    queue.push(index);
    auto area = 0;
    auto min_x = width - 1;
    auto max_x = 0;
    auto min_y = height - 1;
    auto max_y = 0;
    while (!queue.empty()) {
      const auto current = queue.front();
      queue.pop();
      const auto x = current % width;
      const auto y = current / width;
      ++area;
      min_x = std::min(min_x, x);
      max_x = std::max(max_x, x);
      min_y = std::min(min_y, y);
      max_y = std::max(max_y, y);

      const std::array<int, 4> neighbors = {
          current - 1, current + 1, current - width, current + width};
      for (const auto neighbor : neighbors) {
        if (neighbor < 0 || neighbor >= pixel_count) {
          continue;
        }
        const auto neighbor_x = neighbor % width;
        if (std::abs(neighbor_x - x) > 1) {
          continue;
        }
        const auto neighbor_index = static_cast<std::size_t>(neighbor);
        if (!candidate[neighbor_index] || visited[neighbor_index]) {
          continue;
        }
        visited[neighbor_index] = 1;
        queue.push(neighbor);
      }
    }

    const auto box_width = max_x - min_x + 1;
    const auto box_height = max_y - min_y + 1;
    const auto aspect = static_cast<double>(box_width) /
                        static_cast<double>(std::max(1, box_height));
    if (area < 8 || area > max_component_area || aspect < 0.35 ||
        aspect > 2.8) {
      continue;
    }

    const auto center_x =
        (static_cast<double>(min_x) + static_cast<double>(max_x)) * 0.5;
    const auto center_y =
        (static_cast<double>(min_y) + static_cast<double>(max_y)) * 0.5;
    const auto radius_x =
        std::max(2.0, static_cast<double>(box_width) * 0.78);
    const auto radius_y =
        std::max(2.0, static_cast<double>(box_height) * 0.78);
    const auto raster_min_x = std::max(
        0,
        static_cast<int>(std::floor(center_x - radius_x * 1.15)));
    const auto raster_max_x = std::min(
        width - 1,
        static_cast<int>(std::ceil(center_x + radius_x * 1.15)));
    const auto raster_min_y = std::max(
        0,
        static_cast<int>(std::floor(center_y - radius_y * 1.15)));
    const auto raster_max_y = std::min(
        height - 1,
        static_cast<int>(std::ceil(center_y + radius_y * 1.15)));
    for (int y = raster_min_y; y <= raster_max_y; ++y) {
      for (int x = raster_min_x; x <= raster_max_x; ++x) {
        const auto dx = (static_cast<double>(x) - center_x) / radius_x;
        const auto dy = (static_cast<double>(y) - center_y) / radius_y;
        const auto distance = std::sqrt(dx * dx + dy * dy);
        const auto weight = 1.0 - smoothstep(0.72, 1.0, distance);
        if (weight <= 1e-6) {
          continue;
        }
        const auto mask_index = y * width + x;
        result.mask_data[mask_index] = static_cast<char>(std::max(
            static_cast<int>(
                static_cast<unsigned char>(result.mask_data.at(mask_index))),
            static_cast<int>(std::lround(weight * 255.0))));
      }
    }
    ++result.component_count;
  }

  if (result.component_count == 0) {
    result.mask_data.clear();
    result.width = 0;
    result.height = 0;
  }
  return result;
}

SkinMaskResult detect_skin_cleanup_mask(const QImage& image) {
  SkinMaskResult result;
  if (image.isNull() || image.width() < 4 || image.height() < 4) {
    return result;
  }

  const auto source = image.convertToFormat(QImage::Format_ARGB32);
  result.width = source.width();
  result.height = source.height();
  result.mask_data = QByteArray(result.width * result.height, '\0');
  for (int y = 0; y < result.height; ++y) {
    for (int x = 0; x < result.width; ++x) {
      const auto color = QColor::fromRgb(source.pixel(x, y));
      const auto red = color.redF();
      const auto green = color.greenF();
      const auto blue = color.blueF();
      const auto hue = color.hslHueF() < 0.0 ? 0.0 : color.hslHueF();
      const auto saturation = color.hslSaturationF();
      const auto lightness = color.lightnessF();
      const auto warm_hue = hue <= 0.14 || hue >= 0.94;
      const auto skin_ratio = red > blue * 1.10 && green > blue * 0.82 &&
                              red > green * 0.86 && red < green * 1.65;
      if (warm_hue && skin_ratio && saturation >= 0.08 &&
          saturation <= 0.72 && lightness >= 0.18 && lightness <= 0.86) {
        result.mask_data[y * result.width + x] = static_cast<char>(210);
        ++result.pixel_count;
      }
    }
  }
  if (result.pixel_count == 0) {
    result.mask_data.clear();
    result.width = 0;
    result.height = 0;
  }
  return result;
}

}  // namespace photo_editor::desktop
