#include "localized_object_refinement.h"

#include "edit_recipe_mask_schema.h"
#include "segmentation_result_schema.h"

#include <algorithm>
#include <cmath>
#include <queue>

#include <QImage>
#include <QPoint>
#include <QVector>

namespace photo_editor::desktop {
namespace {

struct LocalColorSample final {
  double red = 0.0;
  double green = 0.0;
  double blue = 0.0;
};

LocalColorSample local_color_sample(const QImage& image, int x, int y) {
  if (image.isNull()) {
    return {};
  }
  const auto clamped_x = qBound(0, x, image.width() - 1);
  const auto clamped_y = qBound(0, y, image.height() - 1);
  const auto pixel = image.pixel(clamped_x, clamped_y);
  return LocalColorSample{
      .red = static_cast<double>(qRed(pixel)) / 255.0,
      .green = static_cast<double>(qGreen(pixel)) / 255.0,
      .blue = static_cast<double>(qBlue(pixel)) / 255.0,
  };
}

double local_color_distance(const LocalColorSample& left, const LocalColorSample& right) {
  const auto delta_red = left.red - right.red;
  const auto delta_green = left.green - right.green;
  const auto delta_blue = left.blue - right.blue;
  return std::sqrt(
      delta_red * delta_red * 0.30 +
      delta_green * delta_green * 0.59 +
      delta_blue * delta_blue * 0.11);
}

LocalColorSample average_local_patch_color(
    const QImage& image,
    int center_x,
    int center_y,
    int radius) {
  if (image.isNull()) {
    return {};
  }

  auto red = 0.0;
  auto green = 0.0;
  auto blue = 0.0;
  auto count = 0.0;
  for (int y = center_y - radius; y <= center_y + radius; ++y) {
    for (int x = center_x - radius; x <= center_x + radius; ++x) {
      const auto sample = local_color_sample(image, x, y);
      red += sample.red;
      green += sample.green;
      blue += sample.blue;
      count += 1.0;
    }
  }
  if (count <= 0.0) {
    return {};
  }
  return LocalColorSample{
      .red = red / count,
      .green = green / count,
      .blue = blue / count,
  };
}

LocalColorSample average_local_perimeter_color(const QImage& image) {
  if (image.isNull()) {
    return {};
  }

  auto red = 0.0;
  auto green = 0.0;
  auto blue = 0.0;
  auto count = 0.0;
  const auto step = std::max(1, std::min(image.width(), image.height()) / 20);
  for (int x = 0; x < image.width(); x += step) {
    const auto top = local_color_sample(image, x, 0);
    const auto bottom = local_color_sample(image, x, image.height() - 1);
    red += top.red + bottom.red;
    green += top.green + bottom.green;
    blue += top.blue + bottom.blue;
    count += 2.0;
  }
  for (int y = 0; y < image.height(); y += step) {
    const auto left = local_color_sample(image, 0, y);
    const auto right = local_color_sample(image, image.width() - 1, y);
    red += left.red + right.red;
    green += left.green + right.green;
    blue += left.blue + right.blue;
    count += 2.0;
  }
  if (count <= 0.0) {
    return {};
  }
  return LocalColorSample{
      .red = red / count,
      .green = green / count,
      .blue = blue / count,
  };
}


}  // namespace

SegmentationResult refine_localized_object_mask_by_seed_color(
    const QImage& localized_image,
    const SegmentationResult& local_result,
    double normalized_x,
    double normalized_y) {
  if (localized_image.isNull() || localized_image.width() < 2 || localized_image.height() < 2) {
    return local_result;
  }

  const auto has_local_mask =
      valid_segmentation_payload(local_result.mask_data, local_result.width, local_result.height) &&
      local_result.width == localized_image.width() &&
      local_result.height == localized_image.height();
  const auto search_mask =
      has_local_mask
          ? local_result.mask_data
          : QByteArray(localized_image.width() * localized_image.height(), static_cast<char>(0xff));

  const auto seed_x = qBound(
      0,
      static_cast<int>(std::lround(std::clamp(normalized_x, 0.0, 1.0) *
                                   static_cast<double>(std::max(localized_image.width() - 1, 0)))),
      std::max(localized_image.width() - 1, 0));
  const auto seed_y = qBound(
      0,
      static_cast<int>(std::lround(std::clamp(normalized_y, 0.0, 1.0) *
                                   static_cast<double>(std::max(localized_image.height() - 1, 0)))),
      std::max(localized_image.height() - 1, 0));
  const auto seed_color = average_local_patch_color(localized_image, seed_x, seed_y, 1);
  const auto perimeter_color = average_local_perimeter_color(localized_image);
  const auto seed_background_distance =
      local_color_distance(seed_color, perimeter_color);
  const auto color_threshold =
      qBound(0.05, seed_background_distance * 0.55, 0.20);

  QByteArray refined(localized_image.width() * localized_image.height(), '\0');
  QVector<uchar> visited(
      static_cast<qsizetype>(localized_image.width() * localized_image.height()),
      0);
  std::queue<QPoint> queue;
  queue.push(QPoint(seed_x, seed_y));

  auto refined_count = 0;
  while (!queue.empty()) {
    const auto point = queue.front();
    queue.pop();
    if (point.x() < 0 || point.y() < 0 ||
        point.x() >= localized_image.width() || point.y() >= localized_image.height()) {
      continue;
    }

    const auto index = point.y() * localized_image.width() + point.x();
    if (visited[static_cast<qsizetype>(index)] != 0) {
      continue;
    }
    visited[static_cast<qsizetype>(index)] = 1;

    const auto mask_value =
        static_cast<unsigned char>(search_mask.at(index));
    if (mask_value < 24) {
      continue;
    }

    const auto sample = local_color_sample(localized_image, point.x(), point.y());
    const auto distance_to_seed = local_color_distance(sample, seed_color);
    if (distance_to_seed > color_threshold &&
        !(mask_value > 160 && distance_to_seed <= color_threshold * 1.35)) {
      continue;
    }

    refined[index] = static_cast<char>(0xff);
    ++refined_count;
    queue.push(QPoint(point.x() - 1, point.y()));
    queue.push(QPoint(point.x() + 1, point.y()));
    queue.push(QPoint(point.x(), point.y() - 1));
    queue.push(QPoint(point.x(), point.y() + 1));
  }

  if (refined_count <= 0) {
    return local_result;
  }
  if (has_local_mask &&
      refined_count >= segmentation_result_nonzero_count(local_result)) {
    return local_result;
  }

  return SegmentationResult{
      .mask_data = refined,
      .width = localized_image.width(),
      .height = localized_image.height(),
      .success = true,
      .error_message = QString(),
  };
}


}  // namespace photo_editor::desktop
