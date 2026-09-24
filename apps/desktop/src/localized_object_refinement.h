#pragma once

#include "vision_segmentation.h"

class QImage;

namespace photo_editor::desktop {

[[nodiscard]] SegmentationResult refine_localized_object_mask_by_seed_color(
    const QImage& localized_image,
    const SegmentationResult& local_result,
    double normalized_x,
    double normalized_y);

}  // namespace photo_editor::desktop
