#include "brush_input_schema.h"

#include "edit_recipe_mask_schema.h"

#include <algorithm>
#include <cmath>

#include <QString>
#include <QVariantList>

namespace photo_editor::desktop {
namespace {

double clamp_unit(double value) { return std::clamp(value, 0.0, 1.0); }

double map_double_value(const QVariantMap& map, const QString& key, double fallback) {
  return map.contains(key) ? map.value(key).toDouble() : fallback;
}

bool map_bool_value(const QVariantMap& map, const QString& key, bool fallback) {
  return map.contains(key) ? map.value(key).toBool() : fallback;
}

}  // namespace

bool ensure_brush_raster_mask(
    QVariantMap* mask,
    const QSize& raster_size) {
  if (mask == nullptr || !raster_size.isValid() || raster_size.width() <= 1 ||
      raster_size.height() <= 1) {
    return false;
  }

  const auto existing_data = mask->value(QStringLiteral("segmentationData")).toByteArray();
  const auto existing_width = mask->value(QStringLiteral("segmentationWidth")).toInt();
  const auto existing_height = mask->value(QStringLiteral("segmentationHeight")).toInt();
  if (valid_segmentation_payload(existing_data, existing_width, existing_height)) {
    return true;
  }

  mask->insert(
      QStringLiteral("segmentationData"),
      QByteArray(raster_size.width() * raster_size.height(), '\0'));
  mask->insert(QStringLiteral("segmentationWidth"), raster_size.width());
  mask->insert(QStringLiteral("segmentationHeight"), raster_size.height());
  return true;
}

QVariantMap brush_point_variant(
    double normalized_x,
    double normalized_y,
    double brush_size,
    double brush_flow,
    double brush_opacity,
    double auto_mask_anchor_x,
    double auto_mask_anchor_y,
    double pressure,
    double tilt_x,
    double tilt_y,
    bool erase) {
  const auto clamped_pressure = clamp_unit(pressure);
  const auto clamped_tilt_x = std::clamp(tilt_x, -1.0, 1.0);
  const auto clamped_tilt_y = std::clamp(tilt_y, -1.0, 1.0);
  QVariantMap point;
  point.insert(QStringLiteral("x"), normalized_x);
  point.insert(QStringLiteral("y"), normalized_y);
  point.insert(QStringLiteral("size"), std::max(0.01, brush_size));
  point.insert(QStringLiteral("flow"), clamp_unit(brush_flow));
  point.insert(QStringLiteral("opacity"), clamp_unit(brush_opacity));
  point.insert(QStringLiteral("autoMaskAnchorX"), clamp_unit(auto_mask_anchor_x));
  point.insert(QStringLiteral("autoMaskAnchorY"), clamp_unit(auto_mask_anchor_y));
  point.insert(QStringLiteral("pressure"), clamped_pressure);
  point.insert(QStringLiteral("tiltX"), clamped_tilt_x);
  point.insert(QStringLiteral("tiltY"), clamped_tilt_y);
  if (erase) {
    point.insert(QStringLiteral("erase"), true);
  }
  return point;
}

QVariantMap brush_point_variant(const BrushPreviewPoint& point) {
  return brush_point_variant(
      point.x,
      point.y,
      point.size,
      point.flow,
      point.opacity,
      point.auto_mask_anchor_x,
      point.auto_mask_anchor_y,
      point.pressure,
      point.tilt_x,
      point.tilt_y,
      point.erase);
}

QVariantMap brush_stroke_variant(
    const QVariantMap& mask,
    const QVector<BrushPreviewPoint>& samples,
    int created_at_order) {
  QVariantList sample_values;
  sample_values.reserve(samples.size());
  for (const auto& sample : samples) {
    sample_values.append(brush_point_variant(sample));
  }

  QVariantMap settings;
  if (mask.value(QStringLiteral("toolRole")).toString() != QStringLiteral("retouch"))
    settings.insert(QStringLiteral("rasterVersion"), 2);
  settings.insert(QStringLiteral("preset"), mask.value(QStringLiteral("brushPreset")).toString());
  settings.insert(QStringLiteral("size"), mask.value(QStringLiteral("brushSize")).toDouble());
  settings.insert(QStringLiteral("flow"), mask.value(QStringLiteral("brushFlow")).toDouble());
  settings.insert(QStringLiteral("hardness"), mask.value(QStringLiteral("brushHardness")).toDouble());
  settings.insert(QStringLiteral("spacing"), mask.value(QStringLiteral("brushSpacing")).toDouble());
  settings.insert(QStringLiteral("smoothing"), mask.value(QStringLiteral("brushSmoothing")).toDouble());
  settings.insert(QStringLiteral("pressureSize"), mask.value(QStringLiteral("brushPressureSize")).toDouble());
  settings.insert(QStringLiteral("pressureFlow"), mask.value(QStringLiteral("brushPressureFlow")).toDouble());
  settings.insert(
      QStringLiteral("pressureOpacity"),
      mask.value(QStringLiteral("brushPressureOpacity")).toDouble());
  settings.insert(QStringLiteral("buildup"), mask.value(QStringLiteral("brushBuildup")).toBool());
  settings.insert(QStringLiteral("eraseMode"), mask.value(QStringLiteral("brushEraseMode")).toBool());
  settings.insert(QStringLiteral("autoMask"), mask.value(QStringLiteral("brushAutoMask")).toBool());
  settings.insert(
      QStringLiteral("autoMaskTolerance"),
      mask.value(QStringLiteral("brushAutoMaskTolerance")).toDouble());
  settings.insert(
      QStringLiteral("protectEdges"),
      mask.value(QStringLiteral("brushProtectEdges")).toBool());
  settings.insert(QStringLiteral("feather"), mask.value(QStringLiteral("feather")).toDouble());

  if (mask.value("retouchMode").toString()=="clone" && !samples.isEmpty()) {
    auto offset_x=mask.value("cloneOffsetX").toDouble();
    auto offset_y=mask.value("cloneOffsetY").toDouble();
    if (created_at_order<=1 || !mask.value("cloneAligned",true).toBool()) {
      offset_x=mask.value("cloneSourceX",0.5).toDouble()-samples.front().x;
      offset_y=mask.value("cloneSourceY",0.5).toDouble()-samples.front().y;
    }
    settings.insert("cloneSourceX",mask.value("cloneSourceX",0.5));
    settings.insert("cloneSourceY",mask.value("cloneSourceY",0.5));
    settings.insert("cloneOffsetX",offset_x); settings.insert("cloneOffsetY",offset_y);
    settings.insert("cloneScale",mask.value("cloneScale",1.0));
    settings.insert("cloneRotation",mask.value("cloneRotation",0.0));
  }
  const auto order = std::max(1, created_at_order);
  const auto erase_stroke = !samples.isEmpty() &&
                             std::all_of(samples.cbegin(), samples.cend(), [](const auto& sample) {
                               return sample.erase;
                             });
  QVariantMap stroke;
  stroke.insert(QStringLiteral("id"), QStringLiteral("stroke-%1").arg(order));
  stroke.insert(QStringLiteral("createdAtOrder"), order);
  stroke.insert(
      QStringLiteral("operation"),
      erase_stroke ? QStringLiteral("erase")
                   : normalized_mask_operation(mask.value(QStringLiteral("operation")).toString()));
  stroke.insert(QStringLiteral("sampleCount"), sample_values.size());
  stroke.insert(QStringLiteral("settings"), settings);
  stroke.insert(QStringLiteral("samples"), sample_values);
  return stroke;
}

double pressure_response_multiplier(double pressure, double response) {
  return std::lerp(1.0, clamp_unit(pressure), clamp_unit(response));
}

BrushPreviewPoint brush_preview_point(
    double normalized_x,
    double normalized_y,
    double brush_size,
    double brush_flow,
    double pressure_size_response,
    double pressure_flow_response,
    double pressure_opacity_response,
    double pressure,
    double tilt_x,
    double tilt_y,
    bool erase) {
  const auto clamped_pressure = clamp_unit(pressure);
  const auto clamped_tilt_x = std::clamp(tilt_x, -1.0, 1.0);
  const auto clamped_tilt_y = std::clamp(tilt_y, -1.0, 1.0);
  const auto pressure_size = pressure_response_multiplier(clamped_pressure, pressure_size_response);
  const auto pressure_flow = pressure_response_multiplier(clamped_pressure, pressure_flow_response);
  const auto pressure_opacity = pressure_response_multiplier(clamped_pressure, pressure_opacity_response);
  const auto tilt_magnitude =
      std::min(1.0, std::hypot(clamped_tilt_x, clamped_tilt_y));
  const auto tilt_size = std::lerp(1.0, 1.35, tilt_magnitude);
  return BrushPreviewPoint{
      .x = normalized_x,
      .y = normalized_y,
      .size = std::max(0.01, brush_size * pressure_size * tilt_size),
      .flow = clamp_unit(brush_flow) * pressure_flow,
      .opacity = pressure_opacity,
      .auto_mask_anchor_x = clamp_unit(normalized_x),
      .auto_mask_anchor_y = clamp_unit(normalized_y),
      .pressure = clamped_pressure,
      .tilt_x = clamped_tilt_x,
      .tilt_y = clamped_tilt_y,
      .erase = erase,
  };
}

BrushPreviewPoint brush_preview_point_from_variant(
    const QVariant& value,
    double fallback_x,
    double fallback_y,
    double fallback_size,
    double fallback_flow) {
  const auto point = value.toMap();
  return BrushPreviewPoint{
      .x = map_double_value(point, QStringLiteral("x"), fallback_x),
      .y = map_double_value(point, QStringLiteral("y"), fallback_y),
      .size = map_double_value(point, QStringLiteral("size"), fallback_size),
      .flow = map_double_value(point, QStringLiteral("flow"), fallback_flow),
      .opacity = map_double_value(point, QStringLiteral("opacity"), 1.0),
      .auto_mask_anchor_x = map_double_value(
          point,
          QStringLiteral("autoMaskAnchorX"),
          map_double_value(point, QStringLiteral("x"), fallback_x)),
      .auto_mask_anchor_y = map_double_value(
          point,
          QStringLiteral("autoMaskAnchorY"),
          map_double_value(point, QStringLiteral("y"), fallback_y)),
      .pressure = map_double_value(point, QStringLiteral("pressure"), 1.0),
      .tilt_x = map_double_value(point, QStringLiteral("tiltX"), 0.0),
      .tilt_y = map_double_value(point, QStringLiteral("tiltY"), 0.0),
      .erase = map_bool_value(point, QStringLiteral("erase"), false),
  };
}

QVector<BrushPreviewPoint> brush_points_for_target_raster_replay(
    const QVariantMap& mask) {
  QVector<BrushPreviewPoint> points;
  const auto fallback_size = map_double_value(mask, QStringLiteral("brushSize"), 0.08);
  const auto fallback_flow = map_double_value(mask, QStringLiteral("brushFlow"), 1.0);
  const auto brush_points = mask.value(QStringLiteral("brushPoints")).toList();
  points.reserve(brush_points.size());
  for (const auto& point_value : brush_points) {
    const auto point = point_value.toMap();
    if (!point.isEmpty()) {
      points.append(brush_preview_point_from_variant(
          point_value, 0.5, 0.5, fallback_size, fallback_flow));
    }
  }
  if (!points.isEmpty()) {
    return points;
  }

  const auto strokes = mask.value(QStringLiteral("brushStrokes")).toList();
  for (const auto& stroke_value : strokes) {
    const auto stroke = stroke_value.toMap();
    const auto erase_stroke =
        stroke.value(QStringLiteral("operation")).toString().trimmed().toLower() ==
        QStringLiteral("erase");
    const auto samples = stroke.value(QStringLiteral("samples")).toList();
    points.reserve(points.size() + samples.size());
    for (const auto& sample_value : samples) {
      if (sample_value.toMap().isEmpty()) {
        continue;
      }
      auto point = brush_preview_point_from_variant(
          sample_value, 0.5, 0.5, fallback_size, fallback_flow);
      if (erase_stroke) {
        point.erase = true;
      }
      points.append(point);
    }
  }
  return points;
}

QRectF dirty_region_for_brush_points(const QVector<BrushPreviewPoint>& points) {
  if (points.isEmpty()) {
    return {};
  }

  auto left = 1.0;
  auto top = 1.0;
  auto right = 0.0;
  auto bottom = 0.0;
  for (const auto& point : points) {
    const auto radius = std::clamp(point.size, 0.01, 1.0) + 0.01;
    left = std::min(left, point.x - radius);
    top = std::min(top, point.y - radius);
    right = std::max(right, point.x + radius);
    bottom = std::max(bottom, point.y + radius);
  }

  left = std::clamp(left, 0.0, 1.0);
  top = std::clamp(top, 0.0, 1.0);
  right = std::clamp(right, 0.0, 1.0);
  bottom = std::clamp(bottom, 0.0, 1.0);
  if (right <= left || bottom <= top) {
    return {};
  }
  return QRectF(left, top, right - left, bottom - top);
}

int rounded_brush_decode_edge(double edge) {
  constexpr auto kBrushAutoMaskDecodeStep = 64.0;
  return static_cast<int>(
      std::ceil(std::max(1.0, edge) / kBrushAutoMaskDecodeStep) *
      kBrushAutoMaskDecodeStep);
}

int auto_mask_source_decode_edge_for_brush_commit(
    const QVector<BrushPreviewPoint>& points,
    const QRectF& dirty_region,
    int existing_width,
    int existing_height,
    bool existing_segmentation_valid) {
  constexpr auto kMaxBrushAutoMaskDecodeEdge = 1024;
  constexpr auto kMinBrushAutoMaskDecodeEdge = 512;
  if (existing_segmentation_valid && existing_width > 0 && existing_height > 0) {
    return std::clamp(
        std::max(existing_width, existing_height),
        kMinBrushAutoMaskDecodeEdge,
        kMaxBrushAutoMaskDecodeEdge);
  }

  auto max_radius = 0.0;
  for (const auto& point : points) {
    max_radius = std::max(max_radius, std::clamp(point.size, 0.01, 1.0));
  }
  const auto footprint = dirty_region.isValid()
                             ? std::max(dirty_region.width(), dirty_region.height())
                             : max_radius * 2.0;
  const auto desired_edge = std::max(footprint * 1536.0, max_radius * 4096.0);
  return std::clamp(
      rounded_brush_decode_edge(desired_edge),
      kMinBrushAutoMaskDecodeEdge,
      kMaxBrushAutoMaskDecodeEdge);
}



}  // namespace photo_editor::desktop
