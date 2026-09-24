#pragma once

#include "vision_segmentation.h"

#include <QRect>
#include <QString>
#include <QVariantMap>

namespace photo_editor::desktop {

[[nodiscard]] QVariantMap segmentation_mask_variant(
    const SegmentationResult& result,
    const QString& kind);

[[nodiscard]] bool segmentation_result_has_confidence_map(
    const SegmentationResult& result);

void apply_generated_confidence_map(
    QVariantMap* mask,
    const SegmentationResult& result);

void clear_generated_confidence_map(QVariantMap* mask);

[[nodiscard]] SegmentationResult segmentation_result_from_mask_variant(
    const QVariantMap& mask);

[[nodiscard]] int segmentation_mask_value_at_normalized(
    const QByteArray& mask_data,
    int width,
    int height,
    double normalized_x,
    double normalized_y);

[[nodiscard]] int segmentation_result_nonzero_count(
    const SegmentationResult& result);

[[nodiscard]] double segmentation_result_coverage(
    const SegmentationResult& result);

[[nodiscard]] QRect segmentation_result_bounds(
    const SegmentationResult& result);

[[nodiscard]] bool segmentation_result_is_too_small_to_use(
    const SegmentationResult& result);

[[nodiscard]] bool subject_segmentation_result_is_thin_fragment(
    const SegmentationResult& result);

}  // namespace photo_editor::desktop
