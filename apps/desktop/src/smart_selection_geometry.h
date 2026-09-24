#pragma once

#include <QByteArray>
#include <QPoint>
#include <QVector>

namespace photo_editor::desktop {

[[nodiscard]] int instance_id_from_map(
    const QByteArray& instance_data,
    int width,
    int height,
    double normalized_x,
    double normalized_y);

[[nodiscard]] int instance_pixel_count_in_map(
    const QByteArray& instance_data,
    int width,
    int height,
    int instance_id);

[[nodiscard]] int instance_id_at_map_point(
    const QByteArray& instance_data,
    int width,
    int height,
    int x,
    int y);

[[nodiscard]] QVector<QPoint> smart_select_seed_points(
    int width,
    int height,
    double normalized_x,
    double normalized_y);

}  // namespace photo_editor::desktop
