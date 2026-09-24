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

}  // namespace photo_editor::desktop
