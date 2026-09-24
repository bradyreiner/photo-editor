#pragma once

#include "vision_segmentation.h"

#include <QPointF>
#include <QRectF>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

namespace photo_editor::desktop {

[[nodiscard]] QString normalized_smart_select_prompt_type(
    const QVariantMap& point);

[[nodiscard]] QVariantMap normalized_smart_select_prompt_map(
    const QVariant& value);

[[nodiscard]] QVariantList normalized_smart_select_prompt_maps(
    const QVariantList& points);

[[nodiscard]] QVector<QPointF> normalized_smart_select_path(
    const QVariantList& points);

[[nodiscard]] QVariantList quantized_smart_select_preview_path(
    const QVariantList& points);

[[nodiscard]] QVector<QPointF> sampled_smart_select_path_points(
    const QVector<QPointF>& points);

[[nodiscard]] QVector<SmartSelectPrompt> smart_select_prompts_for_path(
    const QVariantList& points);

[[nodiscard]] QVariantMap smart_select_prompt_metadata_for_path(
    const QVariantList& points);

[[nodiscard]] bool merge_smart_select_prompt_metadata(
    QVariantMap* mask,
    const QVariantList& points);

[[nodiscard]] QRectF bounds_for_normalized_variant_points(
    const QVariantList& points);

}  // namespace photo_editor::desktop
