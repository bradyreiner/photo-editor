#include "smart_selection_instance_policy.h"

#include "smart_selection_geometry.h"
#include "smart_selection_prompt_schema.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include <QHash>
#include <QSet>

namespace photo_editor::desktop {
namespace {

QVector<int> sorted_nearby_candidates(
    const QHash<int, int>& best_distance_by_instance,
    const QByteArray& instance_data,
    int width,
    int height) {
  if (best_distance_by_instance.isEmpty()) {
    return {};
  }
  auto best_distance = std::numeric_limits<int>::max();
  for (auto it = best_distance_by_instance.cbegin();
       it != best_distance_by_instance.cend();
       ++it) {
    best_distance = std::min(best_distance, it.value());
  }
  const auto best_radius = std::sqrt(static_cast<double>(best_distance));
  const auto candidate_distance_limit = std::pow(best_radius + 1.0, 2.0);
  QVector<int> candidates;
  for (auto it = best_distance_by_instance.cbegin();
       it != best_distance_by_instance.cend();
       ++it) {
    if (static_cast<double>(it.value()) <= candidate_distance_limit) {
      candidates.append(it.key());
    }
  }
  std::sort(candidates.begin(), candidates.end(), [&](int left, int right) {
    const auto left_coverage =
        instance_pixel_count_in_map(instance_data, width, height, left);
    const auto right_coverage =
        instance_pixel_count_in_map(instance_data, width, height, right);
    if (left_coverage != right_coverage) {
      return left_coverage > right_coverage;
    }
    return left < right;
  });
  return candidates;
}

QVector<int> nearby_candidates_for_seeds(
    const QByteArray& instance_data,
    int width,
    int height,
    const QVector<QPoint>& search_seeds,
    int max_radius) {
  QHash<int, int> best_distance_by_instance;
  auto first_hit_radius = -1;
  for (auto radius = 1; radius <= max_radius; ++radius) {
    for (const auto& seed : search_seeds) {
      for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
          if (std::max(std::abs(dx), std::abs(dy)) != radius) {
            continue;
          }
          const auto instance_id = instance_id_at_map_point(
              instance_data, width, height, seed.x() + dx, seed.y() + dy);
          if (instance_id <= 0) {
            continue;
          }
          const auto distance_sq = dx * dx + dy * dy;
          const auto existing_distance =
              best_distance_by_instance.value(instance_id, std::numeric_limits<int>::max());
          if (distance_sq < existing_distance) {
            best_distance_by_instance.insert(instance_id, distance_sq);
          }
        }
      }
    }
    if (!best_distance_by_instance.isEmpty()) {
      if (first_hit_radius < 0) {
        first_hit_radius = radius;
        continue;
      }
      if (radius >= first_hit_radius + 1) {
        return sorted_nearby_candidates(
            best_distance_by_instance, instance_data, width, height);
      }
    }
  }
  return sorted_nearby_candidates(
      best_distance_by_instance, instance_data, width, height);
}

}  // namespace

QVector<int> smart_select_instance_candidates_from_map(
    const QByteArray& instance_data,
    int width,
    int height,
    double normalized_x,
    double normalized_y) {
  if (instance_data.isEmpty() || width <= 0 || height <= 0) {
    return {};
  }
  const auto direct_instance =
      instance_id_from_map(instance_data, width, height, normalized_x, normalized_y);
  if (direct_instance > 0) {
    return {direct_instance};
  }

  const auto max_radius = std::clamp(
      static_cast<int>(
          std::lround(static_cast<double>(std::min(width, height)) * 0.08)),
      1,
      24);
  const auto seeds =
      smart_select_seed_points(width, height, normalized_x, normalized_y);
  if (!seeds.isEmpty()) {
    const auto primary_candidates = nearby_candidates_for_seeds(
        instance_data, width, height, {seeds.constFirst()}, max_radius);
    if (!primary_candidates.isEmpty()) {
      return primary_candidates;
    }
  }
  if (seeds.size() > 1) {
    return nearby_candidates_for_seeds(
        instance_data, width, height, {seeds.constLast()}, max_radius);
  }
  return {};
}

QVector<int> smart_select_instance_candidates_for_path(
    const QByteArray& instance_data,
    int width,
    int height,
    const QVector<QPointF>& points) {
  const auto sampled_points = sampled_smart_select_path_points(points);
  if (sampled_points.isEmpty()) {
    return {};
  }

  QHash<int, int> touch_count_by_instance;
  QHash<int, int> first_seen_by_instance;
  for (int point_index = 0; point_index < sampled_points.size(); ++point_index) {
    const auto point = sampled_points.at(point_index);
    const auto point_candidates = smart_select_instance_candidates_from_map(
        instance_data, width, height, point.x(), point.y());
    for (const auto instance_id : point_candidates) {
      if (instance_id <= 0) {
        continue;
      }
      touch_count_by_instance.insert(
          instance_id, touch_count_by_instance.value(instance_id, 0) + 1);
      if (!first_seen_by_instance.contains(instance_id)) {
        first_seen_by_instance.insert(instance_id, point_index);
      }
    }
  }
  if (touch_count_by_instance.isEmpty()) {
    return {};
  }

  auto max_touch_count = 0;
  for (auto it = touch_count_by_instance.cbegin();
       it != touch_count_by_instance.cend();
       ++it) {
    max_touch_count = std::max(max_touch_count, it.value());
  }
  const auto inclusion_threshold = std::max(
      1,
      static_cast<int>(std::ceil(static_cast<double>(max_touch_count) * 0.5)));
  QVector<int> candidates;
  for (auto it = touch_count_by_instance.cbegin();
       it != touch_count_by_instance.cend();
       ++it) {
    if (it.value() >= inclusion_threshold) {
      candidates.append(it.key());
    }
  }
  std::sort(candidates.begin(), candidates.end(), [&](int left, int right) {
    const auto left_touch_count = touch_count_by_instance.value(left, 0);
    const auto right_touch_count = touch_count_by_instance.value(right, 0);
    if (left_touch_count != right_touch_count) {
      return left_touch_count > right_touch_count;
    }
    const auto left_coverage =
        instance_pixel_count_in_map(instance_data, width, height, left);
    const auto right_coverage =
        instance_pixel_count_in_map(instance_data, width, height, right);
    if (left_coverage != right_coverage) {
      return left_coverage > right_coverage;
    }
    const auto left_first_seen =
        first_seen_by_instance.value(left, std::numeric_limits<int>::max());
    const auto right_first_seen =
        first_seen_by_instance.value(right, std::numeric_limits<int>::max());
    if (left_first_seen != right_first_seen) {
      return left_first_seen < right_first_seen;
    }
    return left < right;
  });
  return candidates;
}

QVariantMap segmentation_mask_for_instances(
    const QByteArray& instance_data,
    int width,
    int height,
    const QVector<int>& instance_ids) {
  if (instance_data.isEmpty() || width <= 0 || height <= 0 || instance_ids.isEmpty()) {
    return {};
  }
  QSet<int> normalized_ids;
  for (const auto instance_id : instance_ids) {
    if (instance_id > 0) {
      normalized_ids.insert(instance_id);
    }
  }
  if (normalized_ids.isEmpty()) {
    return {};
  }

  QByteArray mask_data(width * height, char(0));
  auto has_pixels = false;
  for (int index = 0; index < instance_data.size() && index < mask_data.size(); ++index) {
    const auto instance_id =
        static_cast<int>(static_cast<unsigned char>(instance_data.at(index)));
    if (normalized_ids.contains(instance_id)) {
      mask_data[index] = static_cast<char>(0xff);
      has_pixels = true;
    }
  }
  if (!has_pixels) {
    return {};
  }

  QVariantMap mask;
  mask.insert(QStringLiteral("kind"), QStringLiteral("object"));
  mask.insert(QStringLiteral("segmentationData"), mask_data);
  mask.insert(QStringLiteral("segmentationWidth"), width);
  mask.insert(QStringLiteral("segmentationHeight"), height);
  return mask;
}

}  // namespace photo_editor::desktop
