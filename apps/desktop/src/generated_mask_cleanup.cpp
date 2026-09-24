#include "generated_mask_cleanup.h"

#include "edit_recipe_mask_schema.h"

#include <algorithm>
#include <cmath>
#include <queue>
#include <utility>
#include <vector>

#include <QPoint>

namespace photo_editor::desktop {

int generated_mask_cleanup_component_limit(
    int width,
    int height,
    double threshold) {
  const auto bounded = qBound(0.0, threshold, kMaxGeneratedMaskCleanupThreshold);
  if (bounded <= 1e-9 || width <= 0 || height <= 0) {
    return 0;
  }
  const auto pixels = static_cast<double>(width) * static_cast<double>(height);
  return std::max(1, static_cast<int>(std::ceil(pixels * bounded)));
}

GeneratedMaskCleanupResult cleanup_generated_mask_components(
    const QByteArray& source_data,
    int width,
    int height,
    double hole_threshold,
    double island_threshold) {
  GeneratedMaskCleanupResult result;
  if (!valid_segmentation_payload(source_data, width, height)) {
    return result;
  }

  result.data = source_data;
  const auto pixel_count = width * height;
  const auto index_for = [width](int x, int y) { return y * width + x; };
  auto selected = std::vector<unsigned char>(
      static_cast<std::size_t>(pixel_count), 0);
  for (int index = 0; index < pixel_count; ++index) {
    selected[static_cast<std::size_t>(index)] =
        static_cast<unsigned char>(source_data.at(index)) > 127U ? 1U : 0U;
  }

  const auto visit_component =
      [&](int start_x,
          int start_y,
          bool target_selected,
          std::vector<unsigned char>* visited) {
        std::vector<int> component;
        component.reserve(64);
        auto touches_edge = false;
        std::queue<QPoint> queue;
        queue.push(QPoint(start_x, start_y));
        (*visited)[static_cast<std::size_t>(index_for(start_x, start_y))] = 1U;
        while (!queue.empty()) {
          const auto point = queue.front();
          queue.pop();
          const auto x = point.x();
          const auto y = point.y();
          component.push_back(index_for(x, y));
          touches_edge = touches_edge || x == 0 || y == 0 ||
                         x == width - 1 || y == height - 1;
          for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
              if (dx == 0 && dy == 0) {
                continue;
              }
              const auto next_x = x + dx;
              const auto next_y = y + dy;
              if (next_x < 0 || next_y < 0 ||
                  next_x >= width || next_y >= height) {
                continue;
              }
              const auto storage_index = static_cast<std::size_t>(
                  index_for(next_x, next_y));
              if ((*visited)[storage_index] != 0U ||
                  (selected[storage_index] != 0U) != target_selected) {
                continue;
              }
              (*visited)[storage_index] = 1U;
              queue.push(QPoint(next_x, next_y));
            }
          }
        }
        return std::pair<std::vector<int>, bool>{
            std::move(component), touches_edge};
      };

  const auto island_limit = generated_mask_cleanup_component_limit(
      width, height, island_threshold);
  if (island_limit > 0) {
    auto visited = std::vector<unsigned char>(
        static_cast<std::size_t>(pixel_count), 0);
    for (int y = 0; y < height; ++y) {
      for (int x = 0; x < width; ++x) {
        const auto storage_index = static_cast<std::size_t>(index_for(x, y));
        if (visited[storage_index] != 0U || selected[storage_index] == 0U) {
          continue;
        }
        auto [component, touches_edge] =
            visit_component(x, y, true, &visited);
        Q_UNUSED(touches_edge);
        if (!component.empty() &&
            static_cast<int>(component.size()) <= island_limit) {
          for (const auto component_index : component) {
            result.data[component_index] = char(0);
            selected[static_cast<std::size_t>(component_index)] = 0U;
          }
          ++result.islands_removed;
          result.pixels_removed += static_cast<int>(component.size());
        }
      }
    }
  }

  const auto hole_limit = generated_mask_cleanup_component_limit(
      width, height, hole_threshold);
  if (hole_limit > 0) {
    auto visited = std::vector<unsigned char>(
        static_cast<std::size_t>(pixel_count), 0);
    for (int y = 0; y < height; ++y) {
      for (int x = 0; x < width; ++x) {
        const auto storage_index = static_cast<std::size_t>(index_for(x, y));
        if (visited[storage_index] != 0U || selected[storage_index] != 0U) {
          continue;
        }
        auto [component, touches_edge] =
            visit_component(x, y, false, &visited);
        if (!touches_edge && !component.empty() &&
            static_cast<int>(component.size()) <= hole_limit) {
          for (const auto component_index : component) {
            result.data[component_index] = char(0xff);
            selected[static_cast<std::size_t>(component_index)] = 1U;
          }
          ++result.holes_filled;
          result.pixels_filled += static_cast<int>(component.size());
        }
      }
    }
  }
  return result;
}

bool apply_generated_mask_cleanup(QVariantMap* mask) {
  if (mask == nullptr ||
      !is_ai_segmentation_mask_kind(
          mask->value(QStringLiteral("kind")).toString())) {
    return false;
  }
  const auto current_data =
      mask->value(QStringLiteral("segmentationData")).toByteArray();
  const auto width = mask->value(QStringLiteral("segmentationWidth")).toInt();
  const auto height = mask->value(QStringLiteral("segmentationHeight")).toInt();
  if (!valid_segmentation_payload(current_data, width, height)) {
    return false;
  }

  auto source_data = mask->value(
      QStringLiteral("generatedMaskCleanupSourceData")).toByteArray();
  const auto source_width = mask->value(
      QStringLiteral("generatedMaskCleanupSourceWidth")).toInt();
  const auto source_height = mask->value(
      QStringLiteral("generatedMaskCleanupSourceHeight")).toInt();
  if (!valid_segmentation_payload(source_data, source_width, source_height) ||
      source_width != width || source_height != height) {
    source_data = current_data;
    mask->insert(QStringLiteral("generatedMaskCleanupSourceData"), source_data);
    mask->insert(QStringLiteral("generatedMaskCleanupSourceWidth"), width);
    mask->insert(QStringLiteral("generatedMaskCleanupSourceHeight"), height);
  }

  const auto hole_threshold = qBound(
      0.0,
      mask->value(
              QStringLiteral("generatedMaskHoleFillThreshold"),
              kDefaultGeneratedMaskHoleFillThreshold)
          .toDouble(),
      kMaxGeneratedMaskCleanupThreshold);
  const auto island_threshold = qBound(
      0.0,
      mask->value(
              QStringLiteral("generatedMaskIslandRemovalThreshold"),
              kDefaultGeneratedMaskIslandRemovalThreshold)
          .toDouble(),
      kMaxGeneratedMaskCleanupThreshold);
  mask->insert(QStringLiteral("generatedMaskHoleFillThreshold"), hole_threshold);
  mask->insert(
      QStringLiteral("generatedMaskIslandRemovalThreshold"), island_threshold);

  const auto cleanup = cleanup_generated_mask_components(
      source_data, width, height, hole_threshold, island_threshold);
  if (!valid_segmentation_payload(cleanup.data, width, height)) {
    return false;
  }
  mask->insert(QStringLiteral("segmentationData"), cleanup.data);
  mask->insert(QStringLiteral("generatedMaskHolesFilled"), cleanup.holes_filled);
  mask->insert(
      QStringLiteral("generatedMaskIslandsRemoved"), cleanup.islands_removed);
  mask->insert(QStringLiteral("generatedMaskPixelsFilled"), cleanup.pixels_filled);
  mask->insert(
      QStringLiteral("generatedMaskPixelsRemoved"), cleanup.pixels_removed);
  auto metadata = mask->value(QStringLiteral("generatedMaskMetadata")).toMap();
  metadata.insert(QStringLiteral("cleanupSchemaVersion"), 1);
  metadata.insert(QStringLiteral("holeFillThreshold"), hole_threshold);
  metadata.insert(QStringLiteral("islandRemovalThreshold"), island_threshold);
  metadata.insert(QStringLiteral("holesFilled"), cleanup.holes_filled);
  metadata.insert(QStringLiteral("islandsRemoved"), cleanup.islands_removed);
  metadata.insert(QStringLiteral("pixelsFilled"), cleanup.pixels_filled);
  metadata.insert(QStringLiteral("pixelsRemoved"), cleanup.pixels_removed);
  mask->insert(QStringLiteral("generatedMaskMetadata"), metadata);
  return true;
}

QString generated_mask_cleanup_summary_for_mask(const QVariantMap& mask) {
  if (!is_ai_segmentation_mask_kind(
          mask.value(QStringLiteral("kind")).toString())) {
    return {};
  }
  const auto hole_threshold = qBound(
      0.0,
      mask.value(
              QStringLiteral("generatedMaskHoleFillThreshold"),
              kDefaultGeneratedMaskHoleFillThreshold)
          .toDouble(),
      kMaxGeneratedMaskCleanupThreshold);
  const auto island_threshold = qBound(
      0.0,
      mask.value(
              QStringLiteral("generatedMaskIslandRemovalThreshold"),
              kDefaultGeneratedMaskIslandRemovalThreshold)
          .toDouble(),
      kMaxGeneratedMaskCleanupThreshold);
  return QStringLiteral(
             "Cleanup: holes <= %1%, islands <= %2% (%3 filled, %4 removed).")
      .arg(hole_threshold * 100.0, 0, 'f', 2)
      .arg(island_threshold * 100.0, 0, 'f', 2)
      .arg(mask.value(QStringLiteral("generatedMaskHolesFilled")).toInt())
      .arg(mask.value(QStringLiteral("generatedMaskIslandsRemoved")).toInt());
}

}  // namespace photo_editor::desktop
