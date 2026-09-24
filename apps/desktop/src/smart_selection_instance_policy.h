#pragma once

#include <QByteArray>
#include <QPointF>
#include <QVariantMap>
#include <QVector>

namespace photo_editor::desktop {

[[nodiscard]] QVector<int> smart_select_instance_candidates_from_map(
    const QByteArray& instance_data, int width, int height,
    double normalized_x, double normalized_y);

[[nodiscard]] QVector<int> smart_select_instance_candidates_for_path(
    const QByteArray& instance_data, int width, int height,
    const QVector<QPointF>& points);

[[nodiscard]] QVariantMap segmentation_mask_for_instances(
    const QByteArray& instance_data, int width, int height,
    const QVector<int>& instance_ids);

}  // namespace photo_editor::desktop
