#pragma once

#include <QSize>
#include <QString>
#include <QVariantMap>

#include "edit_recipe_mask_schema.h"
#include "mask_alpha_provider.h"

namespace photo_editor::desktop {

inline QSize raster_size_for_mask_trace(const QVariantMap& mask) {
  const auto width = mask.value(QStringLiteral("segmentationWidth")).toInt();
  const auto height = mask.value(QStringLiteral("segmentationHeight")).toInt();
  return width > 0 && height > 0 ? QSize(width, height) : QSize{};
}

inline bool is_selection_mask(const QVariantMap& mask) {
  return normalized_mask_tool_role(mask.value(QStringLiteral("toolRole")).toString()) ==
         QStringLiteral("selection");
}

inline bool mask_has_valid_segmentation_payload(const QVariantMap& mask) {
  return valid_segmentation_payload(
      mask.value(QStringLiteral("segmentationData")).toByteArray(),
      mask.value(QStringLiteral("segmentationWidth")).toInt(),
      mask.value(QStringLiteral("segmentationHeight")).toInt());
}

inline bool brush_mask_has_painted_content(const QVariantMap& mask) {
  return !mask.value(QStringLiteral("brushPoints")).toList().isEmpty() ||
         !mask.value(QStringLiteral("brushStrokes")).toList().isEmpty() ||
         mask_has_valid_segmentation_payload(mask);
}

inline QVariantMap mask_alpha_availability_fields(
    const QVariantMap& normalized,
    const QSize& trace_size) {
  const auto kind = normalized_mask_kind(normalized.value(QStringLiteral("kind")).toString());
  const auto has_stroke_replay =
      kind == QStringLiteral("brush") &&
      (!normalized.value(QStringLiteral("brushPoints")).toList().isEmpty() ||
       !normalized.value(QStringLiteral("brushStrokes")).toList().isEmpty());
  const auto has_raster_payload = mask_has_valid_segmentation_payload(normalized);
  const auto procedural =
      kind == QStringLiteral("linear") || kind == QStringLiteral("radial") ||
      kind == QStringLiteral("ellipse") || kind == QStringLiteral("rectangle") ||
      kind == QStringLiteral("polygon") || kind == QStringLiteral("lasso") ||
      kind == QStringLiteral("luminance") || kind == QStringLiteral("color") ||
      kind == QStringLiteral("depth") || kind == QStringLiteral("luminance_range") ||
      kind == QStringLiteral("color_range") || kind == QStringLiteral("depth_range");
  QString provider;
  if (has_stroke_replay) {
    provider = QStringLiteral("brush-stroke-replay");
  } else if (has_raster_payload || kind == QStringLiteral("alpha") ||
             is_raster_mask_kind(kind)) {
    provider = has_raster_payload ? QStringLiteral("raster-payload")
                                  : QStringLiteral("raster-missing");
  } else if (procedural) {
    provider = QStringLiteral("procedural-geometry");
  } else {
    provider = QStringLiteral("unavailable");
  }
  const auto available = provider != QStringLiteral("unavailable") &&
                         provider != QStringLiteral("raster-missing");
  return shared_mask_alpha_provider_fields(provider, available, trace_size);
}

inline bool selection_mask_is_empty_or_invalid(
    const QVariantMap& candidate,
    int ordinal) {
  const auto mask = normalized_mask_definition(candidate, ordinal);
  if (!is_selection_mask(mask)) {
    return false;
  }
  const auto kind = normalized_mask_kind(mask.value(QStringLiteral("kind")).toString());
  if (kind == QStringLiteral("brush")) {
    return !brush_mask_has_painted_content(mask);
  }
  if (kind == QStringLiteral("polygon") || kind == QStringLiteral("lasso")) {
    return mask.value(QStringLiteral("polygonPoints")).toList().size() < 3;
  }
  if (kind == QStringLiteral("alpha") || is_raster_mask_kind(kind)) {
    return !mask_has_valid_segmentation_payload(mask);
  }
  return false;
}

inline QString mask_source_badge_label(const QString& kind_name) {
  const auto kind = normalized_mask_kind(kind_name);
  if (kind == QStringLiteral("linear")) return QStringLiteral("Linear");
  if (kind == QStringLiteral("radial")) return QStringLiteral("Radial");
  if (kind == QStringLiteral("ellipse")) return QStringLiteral("Ellipse");
  if (kind == QStringLiteral("rectangle")) return QStringLiteral("Rectangle");
  if (kind == QStringLiteral("brush")) return QStringLiteral("Brush");
  if (kind == QStringLiteral("polygon")) return QStringLiteral("Polygon");
  if (kind == QStringLiteral("lasso")) return QStringLiteral("Lasso");
  if (kind == QStringLiteral("luminance")) return QStringLiteral("Luma");
  if (kind == QStringLiteral("color")) return QStringLiteral("Color");
  if (kind == QStringLiteral("depth")) return QStringLiteral("Depth");
  if (kind == QStringLiteral("alpha")) return QStringLiteral("Alpha");
  return is_ai_segmentation_mask_kind(kind) ? ai_segmentation_mask_label(kind)
                                             : QStringLiteral("Source");
}

inline QString mask_operation_badge_label(const QString& operation_name) {
  const auto operation = normalized_mask_operation(operation_name);
  if (operation == QStringLiteral("subtract")) return QStringLiteral("Subtract");
  if (operation == QStringLiteral("intersect")) return QStringLiteral("Intersect");
  if (operation == QStringLiteral("replace")) return QStringLiteral("Replace");
  if (operation == QStringLiteral("exclude")) return QStringLiteral("Exclude");
  return QStringLiteral("Add");
}

}  // namespace photo_editor::desktop
