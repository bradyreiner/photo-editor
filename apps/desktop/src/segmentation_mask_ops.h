#pragma once

#include <QByteArray>

namespace photo_editor::desktop {

[[nodiscard]] int selected_pixel_count_for_mask_data(
    const QByteArray& mask_data);

[[nodiscard]] QByteArray union_segmentation_masks(
    const QByteArray& existing_data,
    int existing_width,
    int existing_height,
    const QByteArray& incoming_data,
    int incoming_width,
    int incoming_height);

[[nodiscard]] QByteArray subtract_segmentation_mask(
    const QByteArray& existing_data,
    int existing_width,
    int existing_height,
    const QByteArray& incoming_data,
    int incoming_width,
    int incoming_height);

[[nodiscard]] QByteArray resample_segmentation_mask(
    const QByteArray& source_data,
    int source_width,
    int source_height,
    int target_width,
    int target_height);

}  // namespace photo_editor::desktop
