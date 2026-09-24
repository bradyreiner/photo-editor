#include "segmentation_result_schema.h"

#include "edit_recipe_mask_schema.h"

#include <algorithm>
#include <cmath>

namespace photo_editor::desktop {

QVariantMap segmentation_mask_variant(
    const SegmentationResult& result,
    const QString& kind) {
  if (!valid_segmentation_payload(
          result.mask_data, result.width, result.height)) {
    return {};
  }
  return QVariantMap{
      {QStringLiteral("kind"), normalized_mask_kind(kind)},
      {QStringLiteral("segmentationData"), result.mask_data},
      {QStringLiteral("segmentationWidth"), result.width},
      {QStringLiteral("segmentationHeight"), result.height},
  };
}

bool segmentation_result_has_confidence_map(
    const SegmentationResult& result) {
  return valid_segmentation_payload(
      result.confidence_data,
      result.confidence_width,
      result.confidence_height);
}

void apply_generated_confidence_map(
    QVariantMap* mask,
    const SegmentationResult& result) {
  if (mask == nullptr || !segmentation_result_has_confidence_map(result)) {
    return;
  }
  mask->insert(QStringLiteral("confidenceData"), result.confidence_data);
  mask->insert(QStringLiteral("confidenceWidth"), result.confidence_width);
  mask->insert(QStringLiteral("confidenceHeight"), result.confidence_height);
  mask->insert(QStringLiteral("confidenceMapSchemaVersion"), 1);

  auto metadata =
      mask->value(QStringLiteral("generatedMaskMetadata")).toMap();
  metadata.insert(QStringLiteral("confidenceMapAvailable"), true);
  metadata.insert(QStringLiteral("confidenceMapSchemaVersion"), 1);
  metadata.insert(
      QStringLiteral("confidenceResolution"),
      QStringLiteral("%1x%2")
          .arg(result.confidence_width)
          .arg(result.confidence_height));
  mask->insert(QStringLiteral("generatedMaskMetadata"), metadata);
}

void clear_generated_confidence_map(QVariantMap* mask) {
  if (mask == nullptr) {
    return;
  }
  mask->remove(QStringLiteral("confidenceData"));
  mask->remove(QStringLiteral("confidenceWidth"));
  mask->remove(QStringLiteral("confidenceHeight"));
  mask->remove(QStringLiteral("confidenceMapSchemaVersion"));
  auto metadata =
      mask->value(QStringLiteral("generatedMaskMetadata")).toMap();
  metadata.remove(QStringLiteral("confidenceMapAvailable"));
  metadata.remove(QStringLiteral("confidenceMapSchemaVersion"));
  metadata.remove(QStringLiteral("confidenceResolution"));
  if (!metadata.isEmpty()) {
    mask->insert(QStringLiteral("generatedMaskMetadata"), metadata);
  }
}

SegmentationResult segmentation_result_from_mask_variant(
    const QVariantMap& mask) {
  SegmentationResult result;
  result.mask_data =
      mask.value(QStringLiteral("segmentationData")).toByteArray();
  result.width = mask.value(QStringLiteral("segmentationWidth")).toInt();
  result.height = mask.value(QStringLiteral("segmentationHeight")).toInt();
  result.success = valid_segmentation_payload(
      result.mask_data, result.width, result.height);
  if (!result.success) {
    result.error_message = QStringLiteral("Invalid segmentation payload");
  }
  return result;
}

int segmentation_mask_value_at_normalized(
    const QByteArray& mask_data,
    int width,
    int height,
    double normalized_x,
    double normalized_y) {
  if (!valid_segmentation_payload(mask_data, width, height)) {
    return 0;
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
  return static_cast<int>(
      static_cast<unsigned char>(mask_data.at(y * width + x)));
}

int segmentation_result_nonzero_count(const SegmentationResult& result) {
  if (!valid_segmentation_payload(
          result.mask_data, result.width, result.height)) {
    return 0;
  }
  return static_cast<int>(std::count_if(
      result.mask_data.cbegin(), result.mask_data.cend(), [](char value) {
        return static_cast<unsigned char>(value) != 0U;
      }));
}

double segmentation_result_coverage(const SegmentationResult& result) {
  if (!valid_segmentation_payload(
          result.mask_data, result.width, result.height) ||
      result.width <= 0 || result.height <= 0) {
    return 0.0;
  }
  return static_cast<double>(segmentation_result_nonzero_count(result)) /
         static_cast<double>(result.width * result.height);
}

QRect segmentation_result_bounds(const SegmentationResult& result) {
  if (!valid_segmentation_payload(
          result.mask_data, result.width, result.height)) {
    return {};
  }

  auto min_x = result.width;
  auto min_y = result.height;
  auto max_x = -1;
  auto max_y = -1;
  for (int y = 0; y < result.height; ++y) {
    const auto row_offset = y * result.width;
    for (int x = 0; x < result.width; ++x) {
      if (static_cast<unsigned char>(
              result.mask_data.at(row_offset + x)) < 96U) {
        continue;
      }
      min_x = std::min(min_x, x);
      min_y = std::min(min_y, y);
      max_x = std::max(max_x, x);
      max_y = std::max(max_y, y);
    }
  }
  if (max_x < min_x || max_y < min_y) {
    return {};
  }
  return QRect(min_x, min_y, max_x - min_x + 1, max_y - min_y + 1);
}

bool segmentation_result_is_too_small_to_use(
    const SegmentationResult& result) {
  if (!valid_segmentation_payload(
          result.mask_data, result.width, result.height)) {
    return false;
  }
  const auto bounds = segmentation_result_bounds(result);
  return segmentation_result_nonzero_count(result) < 4 || !bounds.isValid() ||
         bounds.width() <= 1 || bounds.height() <= 1;
}

bool subject_segmentation_result_is_thin_fragment(
    const SegmentationResult& result) {
  if (!valid_segmentation_payload(
          result.mask_data, result.width, result.height) ||
      result.width <= 0 || result.height <= 0) {
    return false;
  }
  const auto bounds = segmentation_result_bounds(result);
  if (!bounds.isValid()) {
    return false;
  }

  const auto bounds_width = static_cast<double>(bounds.width());
  const auto bounds_height = static_cast<double>(bounds.height());
  const auto min_bounds_edge = std::min(bounds_width, bounds_height);
  const auto max_bounds_edge = std::max(bounds_width, bounds_height);
  const auto min_image_edge =
      static_cast<double>(std::min(result.width, result.height));
  const auto thin_edge_ratio =
      min_bounds_edge / std::max(1.0, min_image_edge);
  const auto aspect_ratio =
      max_bounds_edge / std::max(1.0, min_bounds_edge);
  const auto coverage = segmentation_result_coverage(result);
  return aspect_ratio >= 4.0 && thin_edge_ratio <= 0.12 && coverage <= 0.08;
}

}  // namespace photo_editor::desktop
