#pragma once

#include <QPointF>
#include <QVariantList>
#include <QVector>

namespace photo_editor::desktop {

[[nodiscard]] QVector<int> single_smart_select_candidate_instance_ids(
    const QVector<int>& candidate_instance_ids, int candidate_index);

[[nodiscard]] double polygon_cross(
    const QPointF& origin, const QPointF& left, const QPointF& right);

[[nodiscard]] QVector<QPointF> normalized_polygon_points(
    const QVariantList& points);

[[nodiscard]] bool polygon_points_self_intersect(const QVariantList& points);

[[nodiscard]] QVariantList variant_list_from_polygon_points(
    const QVector<QPointF>& points);

[[nodiscard]] double distance_to_segment(
    const QPointF& point, const QPointF& start, const QPointF& end);

[[nodiscard]] QVector<QPointF> simplified_closed_polygon_points(
    const QVector<QPointF>& points, double tolerance);

[[nodiscard]] QVector<QPointF> smoothed_closed_polygon_points(
    const QVector<QPointF>& points);

}  // namespace photo_editor::desktop
