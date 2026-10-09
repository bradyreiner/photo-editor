#pragma once

#include "vision_segmentation.h"

#include <QRect>
#include <QSize>

class QImage;

namespace photo_editor::desktop {

[[nodiscard]] bool should_prefer_prompt_object_pick_result(
    const SegmentationResult& instance_result,
    const SegmentationResult& prompt_result,
    double normalized_x,
    double normalized_y);

[[nodiscard]] QRect localized_object_pick_bounds(
    const QSize& image_size,
    double normalized_x,
    double normalized_y);

[[nodiscard]] bool segmentation_result_is_localized_square_failure(
    const QImage& source_image,
    const SegmentationResult& result,
    double normalized_x,
    double normalized_y);

[[nodiscard]] SegmentationResult remap_localized_segmentation_result(
    const SegmentationResult& local_result,
    const QRect& bounds,
    const QSize& target_size);

[[nodiscard]] bool single_point_prompt_result_is_plausible(
    const SegmentationResult& result,
    double normalized_x,
    double normalized_y);

[[nodiscard]] bool smart_select_result_honors_prompts(
    const SegmentationResult& result,
    const QVector<SmartSelectPrompt>& prompts,
    bool allow_multiple_prompt_components = false);

}  // namespace photo_editor::desktop
