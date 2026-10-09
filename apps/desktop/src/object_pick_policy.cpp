#include "object_pick_policy.h"

#include "edit_recipe_mask_schema.h"
#include "segmentation_result_schema.h"

#include <algorithm>
#include <cmath>

#include <QImage>

namespace photo_editor::desktop {

bool should_prefer_prompt_object_pick_result(
    const SegmentationResult& instance_result,
    const SegmentationResult& prompt_result,
    double normalized_x,
    double normalized_y) {
  if (!valid_segmentation_payload(
          prompt_result.mask_data, prompt_result.width, prompt_result.height)) {
    return false;
  }
  if (!valid_segmentation_payload(
          instance_result.mask_data,
          instance_result.width,
          instance_result.height)) {
    return true;
  }

  const auto instance_center_value = segmentation_mask_value_at_normalized(
      instance_result.mask_data,
      instance_result.width,
      instance_result.height,
      normalized_x,
      normalized_y);
  const auto prompt_center_value = segmentation_mask_value_at_normalized(
      prompt_result.mask_data,
      prompt_result.width,
      prompt_result.height,
      normalized_x,
      normalized_y);
  if (prompt_center_value >= 96 && instance_center_value < 96) {
    return true;
  }
  if (instance_center_value >= 96 && prompt_center_value < 96) {
    return false;
  }

  const auto instance_coverage = segmentation_result_coverage(instance_result);
  const auto prompt_coverage = segmentation_result_coverage(prompt_result);
  if (prompt_coverage <= 0.0) {
    return false;
  }
  if (instance_coverage <= 0.0) {
    return true;
  }

  const auto prompt_coverage_ratio = prompt_coverage / instance_coverage;
  if (prompt_coverage_ratio < 0.78 && prompt_coverage_ratio > 0.02 &&
      prompt_center_value >= 64) {
    return true;
  }
  if (prompt_coverage_ratio > 2.5 && instance_center_value >= 64) {
    return false;
  }
  return prompt_center_value > instance_center_value + 48 &&
         prompt_coverage_ratio <= 1.5;
}

QRect localized_object_pick_bounds(
    const QSize& image_size,
    double normalized_x,
    double normalized_y) {
  if (!image_size.isValid() || image_size.width() < 2 ||
      image_size.height() < 2) {
    return {};
  }

  const auto min_edge = std::min(image_size.width(), image_size.height());
  const auto crop_extent = std::clamp(
      static_cast<int>(
          std::lround(static_cast<double>(min_edge) * 0.26)),
      48,
      320);
  const auto crop_width = std::min(image_size.width(), crop_extent);
  const auto crop_height = std::min(image_size.height(), crop_extent);
  const auto center_x = std::clamp(
      static_cast<int>(std::lround(
          std::clamp(normalized_x, 0.0, 1.0) *
          static_cast<double>(std::max(image_size.width() - 1, 0)))),
      0,
      std::max(image_size.width() - 1, 0));
  const auto center_y = std::clamp(
      static_cast<int>(std::lround(
          std::clamp(normalized_y, 0.0, 1.0) *
          static_cast<double>(std::max(image_size.height() - 1, 0)))),
      0,
      std::max(image_size.height() - 1, 0));
  const auto left = std::clamp(
      center_x - crop_width / 2, 0, image_size.width() - crop_width);
  const auto top = std::clamp(
      center_y - crop_height / 2, 0, image_size.height() - crop_height);
  return QRect(left, top, crop_width, crop_height);
}

bool segmentation_result_is_localized_square_failure(
    const QImage& source_image,
    const SegmentationResult& result,
    double normalized_x,
    double normalized_y) {
  if (source_image.isNull() ||
      !valid_segmentation_payload(
          result.mask_data, result.width, result.height) ||
      result.width <= 0 || result.height <= 0) {
    return false;
  }

  const auto bounds = segmentation_result_bounds(result);
  const auto localized_bounds = localized_object_pick_bounds(
      source_image.size(), normalized_x, normalized_y);
  if (!bounds.isValid() || !localized_bounds.isValid()) {
    return false;
  }

  const auto bounds_area =
      static_cast<double>(bounds.width()) * bounds.height();
  if (bounds_area <= 0.0) {
    return false;
  }
  const auto fill_ratio =
      static_cast<double>(segmentation_result_nonzero_count(result)) /
      bounds_area;
  const auto covers_localized_crop =
      bounds.width() >=
          static_cast<int>(std::lround(localized_bounds.width() * 0.88)) &&
      bounds.height() >=
          static_cast<int>(std::lround(localized_bounds.height() * 0.88)) &&
      std::abs(bounds.center().x() - localized_bounds.center().x()) <=
          std::max(4, localized_bounds.width() / 12) &&
      std::abs(bounds.center().y() - localized_bounds.center().y()) <=
          std::max(4, localized_bounds.height() / 12);
  return covers_localized_crop && fill_ratio >= 0.72;
}

SegmentationResult remap_localized_segmentation_result(
    const SegmentationResult& local_result,
    const QRect& bounds,
    const QSize& target_size) {
  if (!valid_segmentation_payload(
          local_result.mask_data, local_result.width, local_result.height) ||
      !bounds.isValid() || !target_size.isValid() ||
      bounds.x() < 0 || bounds.y() < 0 ||
      bounds.right() >= target_size.width() ||
      bounds.bottom() >= target_size.height()) {
    return {};
  }

  SegmentationResult remapped;
  remapped.width = target_size.width();
  remapped.height = target_size.height();
  remapped.mask_data = QByteArray(remapped.width * remapped.height, '\0');
  const auto copy_width = std::min(bounds.width(), local_result.width);
  const auto copy_height = std::min(bounds.height(), local_result.height);
  for (int y = 0; y < copy_height; ++y) {
    const auto source_offset = y * local_result.width;
    const auto target_offset =
        (bounds.y() + y) * remapped.width + bounds.x();
    for (int x = 0; x < copy_width; ++x) {
      remapped.mask_data[target_offset + x] =
          local_result.mask_data.at(source_offset + x);
    }
  }
  remapped.success = valid_segmentation_payload(
      remapped.mask_data, remapped.width, remapped.height);
  if (!remapped.success) {
    remapped.error_message =
        QStringLiteral("Failed to remap localized object mask");
  }
  return remapped;
}

bool single_point_prompt_result_is_plausible(
    const SegmentationResult& result,
    double normalized_x,
    double normalized_y) {
  if (!valid_segmentation_payload(
          result.mask_data, result.width, result.height)) {
    return false;
  }
  const auto center_value = segmentation_mask_value_at_normalized(
      result.mask_data,
      result.width,
      result.height,
      normalized_x,
      normalized_y);
  if (center_value < 96) {
    return false;
  }
  const auto coverage = segmentation_result_coverage(result);
  if (!(coverage > 0.0 && coverage <= 0.92)) {
    return false;
  }
  const auto bounds = segmentation_result_bounds(result);
  return bounds.isValid() && bounds.width() > 1 && bounds.height() > 1;
}

bool smart_select_result_honors_prompts(
    const SegmentationResult& result,
    const QVector<SmartSelectPrompt>& prompts,
    bool allow_multiple_prompt_components) {
  if (!valid_segmentation_payload(
          result.mask_data, result.width, result.height) || prompts.isEmpty()) {
    return false;
  }
  auto has_positive_prompt = false;
  auto has_box_prompt = false;
  QVector<int> positive_prompt_indices;
  positive_prompt_indices.reserve(prompts.size());
  for (const auto& prompt : prompts) {
    if (prompt.box.isValid()) {
      // Box corners (and the center of hollow objects) can be background.
      // Require a meaningful selected region inside the requested bounds.
      const auto bounds = QRectF(
          prompt.box.x() * result.width, prompt.box.y() * result.height,
          prompt.box.width() * result.width, prompt.box.height() * result.height)
                              .toAlignedRect().intersected(QRect(0, 0, result.width, result.height));
      auto selected = 0;
      for (int y = bounds.top(); y <= bounds.bottom() && selected < 2; ++y) {
        for (int x = bounds.left(); x <= bounds.right() && selected < 2; ++x) {
          if (static_cast<unsigned char>(result.mask_data.at(y * result.width + x)) >= 96) ++selected;
        }
      }
      if (selected < 2) return false;
      has_box_prompt = true;
      has_positive_prompt = true;
      continue;
    }
    const auto value = segmentation_mask_value_at_normalized(
        result.mask_data, result.width, result.height, prompt.x, prompt.y);
    if (prompt.negative) {
      if (value >= 96) {
        return false;
      }
    } else {
      has_positive_prompt = true;
      if (value < 96) {
        return false;
      }
      const auto x = std::clamp(
          static_cast<int>(std::lround(
              std::clamp(prompt.x, 0.0, 1.0) *
              static_cast<double>(result.width - 1))),
          0,
          result.width - 1);
      const auto y = std::clamp(
          static_cast<int>(std::lround(
              std::clamp(prompt.y, 0.0, 1.0) *
              static_cast<double>(result.height - 1))),
          0,
          result.height - 1);
      positive_prompt_indices.append(y * result.width + x);
    }
  }
  if (!has_positive_prompt) {
    return false;
  }
  if (positive_prompt_indices.isEmpty()) return has_box_prompt;

  // A dragged prompt path may intentionally cross several subjects. Accept
  // multiple meaningful components while rejecting isolated mask specks.
  QVector<int> component_ids(result.mask_data.size(), -1);
  QVector<int> component_sizes;
  QVector<int> pending;
  for (auto index = 0; index < result.mask_data.size(); ++index) {
    if (static_cast<unsigned char>(result.mask_data.at(index)) < 96 ||
        component_ids.at(index) >= 0) {
      continue;
    }
    const auto component_id = static_cast<int>(component_sizes.size());
    component_sizes.append(0);
    pending.clear();
    pending.append(index);
    component_ids[index] = component_id;
    while (!pending.isEmpty()) {
      const auto current = pending.takeLast();
      ++component_sizes[component_id];
      const auto x = current % result.width;
      const auto y = current / result.width;
      for (auto offset_y = -1; offset_y <= 1; ++offset_y) {
        for (auto offset_x = -1; offset_x <= 1; ++offset_x) {
          if (offset_x == 0 && offset_y == 0) {
            continue;
          }
          const auto neighbor_x = x + offset_x;
          const auto neighbor_y = y + offset_y;
          if (neighbor_x < 0 || neighbor_x >= result.width || neighbor_y < 0 ||
              neighbor_y >= result.height) {
            continue;
          }
          const auto neighbor = neighbor_y * result.width + neighbor_x;
          if (component_ids.at(neighbor) >= 0 ||
              static_cast<unsigned char>(result.mask_data.at(neighbor)) < 96) {
            continue;
          }
          component_ids[neighbor] = component_id;
          pending.append(neighbor);
        }
      }
    }
  }
  auto first_positive_component_id = -1;
  for (const auto index : positive_prompt_indices) {
    const auto component_id = component_ids.at(index);
    if (component_id < 0 || component_sizes.at(component_id) < 2) {
      return false;
    }
    if (first_positive_component_id < 0) {
      first_positive_component_id = component_id;
    } else if (!allow_multiple_prompt_components &&
               component_id != first_positive_component_id) {
      return false;
    }
  }
  return true;
}

}  // namespace photo_editor::desktop
