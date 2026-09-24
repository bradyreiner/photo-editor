#include "smart_selection_geometry.h"

#include <algorithm>
#include <cmath>

namespace photo_editor::desktop {
namespace {

bool map_contains_full_raster(
    const QByteArray& instance_data,
    int width,
    int height) {
  return width > 0 && height > 0 &&
         instance_data.size() >= static_cast<qsizetype>(width) * height;
}

}  // namespace

int instance_id_from_map(
    const QByteArray& instance_data,
    int width,
    int height,
    double normalized_x,
    double normalized_y) {
  if (!map_contains_full_raster(instance_data, width, height)) {
    return 0;
  }

  const auto x = std::clamp(
      static_cast<int>(std::lround(
          std::clamp(normalized_x, 0.0, 1.0) *
          static_cast<double>(std::max(width - 1, 0)))),
      0,
      std::max(width - 1, 0));
  const auto sample_instance = [&](double sample_y) {
    const auto y = std::clamp(
        static_cast<int>(std::lround(
            std::clamp(sample_y, 0.0, 1.0) *
            static_cast<double>(std::max(height - 1, 0)))),
        0,
        std::max(height - 1, 0));
    return static_cast<int>(
        static_cast<unsigned char>(instance_data.at(y * width + x)));
  };

  const auto direct_instance = sample_instance(normalized_y);
  const auto flipped_instance = sample_instance(1.0 - normalized_y);
  if (direct_instance == 0) {
    return flipped_instance;
  }
  return direct_instance;
}

int instance_pixel_count_in_map(
    const QByteArray& instance_data,
    int width,
    int height,
    int instance_id) {
  if (instance_data.isEmpty() || width <= 0 || height <= 0 ||
      instance_id <= 0) {
    return 0;
  }

  auto pixel_count = 0;
  const auto total_pixels = std::min<qsizetype>(
      instance_data.size(),
      static_cast<qsizetype>(width) * static_cast<qsizetype>(height));
  for (qsizetype index = 0; index < total_pixels; ++index) {
    if (static_cast<int>(
            static_cast<unsigned char>(instance_data.at(index))) == instance_id) {
      ++pixel_count;
    }
  }
  return pixel_count;
}

int instance_id_at_map_point(
    const QByteArray& instance_data,
    int width,
    int height,
    int x,
    int y) {
  if (!map_contains_full_raster(instance_data, width, height) || x < 0 || y < 0 ||
      x >= width || y >= height) {
    return 0;
  }
  return static_cast<int>(
      static_cast<unsigned char>(instance_data.at(y * width + x)));
}

QVector<QPoint> smart_select_seed_points(
    int width,
    int height,
    double normalized_x,
    double normalized_y) {
  QVector<QPoint> seeds;
  if (width <= 0 || height <= 0) {
    return seeds;
  }

  const auto x = std::clamp(
      static_cast<int>(std::lround(
          std::clamp(normalized_x, 0.0, 1.0) *
          static_cast<double>(std::max(width - 1, 0)))),
      0,
      std::max(width - 1, 0));
  const auto y = std::clamp(
      static_cast<int>(std::lround(
          std::clamp(normalized_y, 0.0, 1.0) *
          static_cast<double>(std::max(height - 1, 0)))),
      0,
      std::max(height - 1, 0));
  seeds.append(QPoint(x, y));

  const auto flipped_y = std::max(height - 1 - y, 0);
  if (flipped_y != y) {
    seeds.append(QPoint(x, flipped_y));
  }
  return seeds;
}

}  // namespace photo_editor::desktop
