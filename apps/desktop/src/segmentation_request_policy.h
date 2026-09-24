#pragma once

#include "vision_segmentation.h"

#include <QString>

namespace photo_editor::desktop {

[[nodiscard]] QString normalized_generated_mask_request_kind(
    const QString& kind);

[[nodiscard]] bool is_unsupported_landscape_semantic_mask_kind(
    const QString& kind);

[[nodiscard]] QString unsupported_landscape_semantic_label(
    const QString& kind);

[[nodiscard]] bool is_unsupported_people_part_semantic_mask_kind(
    const QString& kind);

[[nodiscard]] QString unsupported_people_part_semantic_label(
    const QString& kind);

[[nodiscard]] QString actionable_segmentation_failure_status(
    const QString& runtime_label,
    const QString& target_label,
    const SegmentationResult& result,
    bool has_prompt);

[[nodiscard]] QString segmentation_failure_reason_code(
    const SegmentationResult& result,
    bool has_prompt);

}  // namespace photo_editor::desktop
