#pragma once

#include <QByteArray>
#include <QVariantList>
#include <QVariantMap>

namespace photo_editor::desktop {

[[nodiscard]] QVariantList hull_points_for_segmentation_mask(
    const QVariantMap& mask);

[[nodiscard]] QVariantList bounding_box_points_for_segmentation_mask(
    const QVariantMap& mask);

[[nodiscard]] QVariantList traced_outline_paths_for_alpha_mask(
    const QByteArray& data, int width, int height);

[[nodiscard]] QVariantList traced_outline_paths_for_alpha_mask_with_edge_padding(
    const QByteArray& data, int width, int height);

}  // namespace photo_editor::desktop
