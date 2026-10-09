#include "smart_selection_prompt_schema.h"

#include <algorithm>
#include <cmath>

#include <QStringList>

namespace photo_editor::desktop {
namespace {

void append_unique_normalized_point(
    QVector<QPointF>* points,
    const QPointF& point) {
  if (points == nullptr) {
    return;
  }
  const auto normalized = QPointF(
      std::clamp(point.x(), 0.0, 1.0),
      std::clamp(point.y(), 0.0, 1.0));
  if (!points->isEmpty()) {
    const auto previous = points->constLast();
    const auto delta_x = normalized.x() - previous.x();
    const auto delta_y = normalized.y() - previous.y();
    if ((delta_x * delta_x) + (delta_y * delta_y) < 1e-6) {
      return;
    }
  }
  points->append(normalized);
}

}  // namespace

QString normalized_smart_select_prompt_type(const QVariantMap& point) {
  auto type = point.value(QStringLiteral("type")).toString().trimmed().toLower();
  if (type.isEmpty()) {
    type = point.value(QStringLiteral("kind")).toString().trimmed().toLower();
  }
  if (point.value(QStringLiteral("negative")).toBool() ||
      type == QStringLiteral("negative") ||
      type == QStringLiteral("negative-point") ||
      type == QStringLiteral("exclude")) {
    return QStringLiteral("negative-point");
  }
  if (type == QStringLiteral("box") || type == QStringLiteral("bbox") ||
      type == QStringLiteral("rectangle") ||
      point.contains(QStringLiteral("x1")) ||
      point.contains(QStringLiteral("left")) ||
      (point.contains(QStringLiteral("width")) &&
       point.contains(QStringLiteral("height")))) {
    return QStringLiteral("box");
  }
  if (type == QStringLiteral("brush") ||
      type == QStringLiteral("brush-point") ||
      type == QStringLiteral("rough-brush") ||
      type == QStringLiteral("stroke")) {
    return QStringLiteral("brush");
  }
  return QStringLiteral("positive-point");
}

QVariantMap normalized_smart_select_prompt_map(const QVariant& value) {
  const auto point = value.toMap();
  const auto type = normalized_smart_select_prompt_type(point);
  QVariantMap normalized;
  normalized.insert(QStringLiteral("type"), type);
  normalized.insert(
      QStringLiteral("negative"), type == QStringLiteral("negative-point"));

  if (type == QStringLiteral("box")) {
    auto left = point.contains(QStringLiteral("left"))
                    ? point.value(QStringLiteral("left")).toDouble()
                    : point.value(QStringLiteral("x")).toDouble();
    auto top = point.contains(QStringLiteral("top"))
                   ? point.value(QStringLiteral("top")).toDouble()
                   : point.value(QStringLiteral("y")).toDouble();
    auto right = point.contains(QStringLiteral("right"))
                     ? point.value(QStringLiteral("right")).toDouble()
                     : (point.contains(QStringLiteral("x2"))
                            ? point.value(QStringLiteral("x2")).toDouble()
                            : left + point.value(QStringLiteral("width")).toDouble());
    auto bottom = point.contains(QStringLiteral("bottom"))
                      ? point.value(QStringLiteral("bottom")).toDouble()
                      : (point.contains(QStringLiteral("y2"))
                             ? point.value(QStringLiteral("y2")).toDouble()
                             : top + point.value(QStringLiteral("height")).toDouble());
    if (!point.contains(QStringLiteral("width")) &&
        !point.contains(QStringLiteral("x2")) &&
        !point.contains(QStringLiteral("right"))) {
      right = left;
    }
    if (!point.contains(QStringLiteral("height")) &&
        !point.contains(QStringLiteral("y2")) &&
        !point.contains(QStringLiteral("bottom"))) {
      bottom = top;
    }
    if (right < left) {
      std::swap(left, right);
    }
    if (bottom < top) {
      std::swap(top, bottom);
    }
    left = std::clamp(left, 0.0, 1.0);
    top = std::clamp(top, 0.0, 1.0);
    right = std::clamp(right, 0.0, 1.0);
    bottom = std::clamp(bottom, 0.0, 1.0);
    if ((right - left) < 1e-5 || (bottom - top) < 1e-5) {
      return {};
    }
    normalized.insert(QStringLiteral("left"), left);
    normalized.insert(QStringLiteral("top"), top);
    normalized.insert(QStringLiteral("right"), right);
    normalized.insert(QStringLiteral("bottom"), bottom);
    normalized.insert(QStringLiteral("width"), right - left);
    normalized.insert(QStringLiteral("height"), bottom - top);
    normalized.insert(QStringLiteral("x"), (left + right) * 0.5);
    normalized.insert(QStringLiteral("y"), (top + bottom) * 0.5);
    return normalized;
  }

  if (!point.contains(QStringLiteral("x")) ||
      !point.contains(QStringLiteral("y"))) {
    return {};
  }
  normalized.insert(
      QStringLiteral("x"),
      std::clamp(point.value(QStringLiteral("x")).toDouble(), 0.0, 1.0));
  normalized.insert(
      QStringLiteral("y"),
      std::clamp(point.value(QStringLiteral("y")).toDouble(), 0.0, 1.0));
  return normalized;
}

QVariantList normalized_smart_select_prompt_maps(const QVariantList& points) {
  QVariantList normalized;
  normalized.reserve(points.size());
  for (const auto& value : points) {
    const auto point = normalized_smart_select_prompt_map(value);
    if (!point.isEmpty()) {
      normalized.append(point);
    }
  }
  return normalized;
}

QVector<QPointF> normalized_smart_select_path(const QVariantList& points) {
  QVector<QPointF> normalized_points;
  const auto prompts = normalized_smart_select_prompt_maps(points);
  normalized_points.reserve(prompts.size() * 5);
  for (const auto& value : prompts) {
    const auto point = value.toMap();
    const auto type = point.value(QStringLiteral("type")).toString();
    if (type == QStringLiteral("negative-point")) {
      continue;
    }
    if (type == QStringLiteral("box")) {
      const auto left = point.value(QStringLiteral("left")).toDouble();
      const auto top = point.value(QStringLiteral("top")).toDouble();
      const auto right = point.value(QStringLiteral("right")).toDouble();
      const auto bottom = point.value(QStringLiteral("bottom")).toDouble();
      append_unique_normalized_point(
          &normalized_points,
          QPointF((left + right) * 0.5, (top + bottom) * 0.5));
      append_unique_normalized_point(&normalized_points, QPointF(left, top));
      append_unique_normalized_point(&normalized_points, QPointF(right, top));
      append_unique_normalized_point(
          &normalized_points, QPointF(right, bottom));
      append_unique_normalized_point(
          &normalized_points, QPointF(left, bottom));
      continue;
    }
    append_unique_normalized_point(
        &normalized_points,
        QPointF(
            point.value(QStringLiteral("x")).toDouble(),
            point.value(QStringLiteral("y")).toDouble()));
  }
  return normalized_points;
}

QVariantList quantized_smart_select_preview_path(const QVariantList& points) {
  const auto normalized_points = normalized_smart_select_path(points);
  if (normalized_points.isEmpty()) {
    return {};
  }

  constexpr double kPreviewQuantizationSteps = 512.0;
  QVariantList quantized_points;
  quantized_points.reserve(normalized_points.size());
  for (const auto& point : normalized_points) {
    QVariantMap quantized_point;
    quantized_point.insert(
        QStringLiteral("x"),
        std::clamp(
            std::round(point.x() * kPreviewQuantizationSteps) /
                kPreviewQuantizationSteps,
            0.0,
            1.0));
    quantized_point.insert(
        QStringLiteral("y"),
        std::clamp(
            std::round(point.y() * kPreviewQuantizationSteps) /
                kPreviewQuantizationSteps,
            0.0,
            1.0));
    if (!quantized_points.isEmpty() &&
        quantized_points.constLast().toMap() == quantized_point) {
      continue;
    }
    quantized_points.append(quantized_point);
  }
  return quantized_points;
}

QVector<QPointF> sampled_smart_select_path_points(
    const QVector<QPointF>& points) {
  if (points.isEmpty()) {
    return {};
  }
  QVector<QPointF> sampled_points;
  sampled_points.reserve(points.size() * 4);
  sampled_points.append(points.constFirst());

  constexpr double kMaxPointSpacing = 0.045;
  for (int index = 1; index < points.size(); ++index) {
    const auto previous = points.at(index - 1);
    const auto current = points.at(index);
    const auto dx = current.x() - previous.x();
    const auto dy = current.y() - previous.y();
    const auto distance = std::hypot(dx, dy);
    const auto step_count = std::max(
        1, static_cast<int>(std::ceil(distance / kMaxPointSpacing)));
    for (int step = 1; step <= step_count; ++step) {
      const auto t =
          static_cast<double>(step) / static_cast<double>(step_count);
      sampled_points.append(QPointF(
          std::clamp(previous.x() + dx * t, 0.0, 1.0),
          std::clamp(previous.y() + dy * t, 0.0, 1.0)));
    }
  }
  return sampled_points;
}

QVector<SmartSelectPrompt> smart_select_prompts_for_path(
    const QVariantList& points) {
  QVector<SmartSelectPrompt> prompts;
  const auto prompt_maps = normalized_smart_select_prompt_maps(points);
  prompts.reserve(prompt_maps.size() * 5);
  QVector<QPointF> pending_brush_path;
  const auto flush_brush_path = [&]() {
    if (pending_brush_path.isEmpty()) {
      return;
    }
    const auto sampled_points =
        sampled_smart_select_path_points(pending_brush_path);
    for (const auto& point : sampled_points) {
      prompts.append(SmartSelectPrompt{
          .x = point.x(), .y = point.y(), .negative = false});
    }
    pending_brush_path.clear();
  };

  for (const auto& value : prompt_maps) {
    const auto point = value.toMap();
    const auto type = point.value(QStringLiteral("type")).toString();
    const auto contributes_to_path =
        type == QStringLiteral("brush") ||
        type == QStringLiteral("positive-point");
    if (!contributes_to_path) {
      flush_brush_path();
    }
    if (type == QStringLiteral("negative-point")) {
      prompts.append(SmartSelectPrompt{
          .x = point.value(QStringLiteral("x")).toDouble(),
          .y = point.value(QStringLiteral("y")).toDouble(),
          .negative = true});
      continue;
    }
    if (type == QStringLiteral("box")) {
      const auto left = point.value(QStringLiteral("left")).toDouble();
      const auto top = point.value(QStringLiteral("top")).toDouble();
      const auto right = point.value(QStringLiteral("right")).toDouble();
      const auto bottom = point.value(QStringLiteral("bottom")).toDouble();
      prompts.append(SmartSelectPrompt{
          .x = (left + right) * 0.5,
          .y = (top + bottom) * 0.5,
          .negative = false,
          .box = QRectF(QPointF(left, top), QPointF(right, bottom))});
      continue;
    }
    if (contributes_to_path) {
      append_unique_normalized_point(
          &pending_brush_path,
          QPointF(
              point.value(QStringLiteral("x")).toDouble(),
              point.value(QStringLiteral("y")).toDouble()));
    }
  }
  flush_brush_path();
  return prompts;
}

QVariantMap smart_select_prompt_metadata_for_path(const QVariantList& points) {
  const auto prompt_maps = normalized_smart_select_prompt_maps(points);
  auto positive_count = 0;
  auto negative_count = 0;
  auto box_count = 0;
  auto brush_count = 0;
  for (const auto& value : prompt_maps) {
    const auto type = value.toMap().value(QStringLiteral("type")).toString();
    if (type == QStringLiteral("negative-point")) {
      ++negative_count;
    } else if (type == QStringLiteral("box")) {
      ++box_count;
    } else if (type == QStringLiteral("brush")) {
      ++brush_count;
    } else {
      ++positive_count;
    }
  }

  QStringList types;
  if (positive_count > 0) {
    types.append(QStringLiteral("positive-point"));
  }
  if (negative_count > 0) {
    types.append(QStringLiteral("negative-point"));
  }
  if (box_count > 0) {
    types.append(QStringLiteral("box"));
  }
  if (brush_count > 0) {
    types.append(QStringLiteral("brush"));
  }

  return QVariantMap{
      {QStringLiteral("smartSelectionPromptSchemaVersion"), 1},
      {QStringLiteral("smartSelectionPrompts"), prompt_maps},
      {QStringLiteral("smartSelectionPromptTypes"), types},
      {QStringLiteral("smartSelectionPromptSummary"),
       types.join(QStringLiteral(","))},
      {QStringLiteral("positivePromptCount"), positive_count},
      {QStringLiteral("negativePromptCount"), negative_count},
      {QStringLiteral("boxPromptCount"), box_count},
      {QStringLiteral("brushPromptCount"), brush_count},
  };
}

bool merge_smart_select_prompt_metadata(
    QVariantMap* mask,
    const QVariantList& points) {
  if (mask == nullptr || points.isEmpty()) {
    return false;
  }
  const auto incoming = smart_select_prompt_metadata_for_path(points);
  const auto incoming_prompts =
      incoming.value(QStringLiteral("smartSelectionPrompts")).toList();
  if (incoming_prompts.isEmpty()) {
    return false;
  }

  auto merged_prompts =
      mask->value(QStringLiteral("smartSelectionPrompts")).toList();
  for (const auto& value : incoming_prompts) {
    merged_prompts.append(value);
  }
  const auto merged = smart_select_prompt_metadata_for_path(merged_prompts);
  for (auto it = merged.constBegin(); it != merged.constEnd(); ++it) {
    mask->insert(it.key(), it.value());
  }

  auto generated_metadata =
      mask->value(QStringLiteral("generatedMaskMetadata")).toMap();
  for (auto it = merged.constBegin(); it != merged.constEnd(); ++it) {
    generated_metadata.insert(it.key(), it.value());
  }
  if (!generated_metadata.isEmpty()) {
    mask->insert(QStringLiteral("generatedMaskMetadata"), generated_metadata);
  }
  return true;
}

QRectF bounds_for_normalized_variant_points(const QVariantList& points) {
  if (points.isEmpty()) {
    return {};
  }
  auto left = 1.0;
  auto top = 1.0;
  auto right = 0.0;
  auto bottom = 0.0;
  auto count = 0;
  for (const auto& value : points) {
    const auto point = value.toMap();
    if (!point.contains(QStringLiteral("x")) ||
        !point.contains(QStringLiteral("y"))) {
      continue;
    }
    const auto x = std::clamp(
        point.value(QStringLiteral("x")).toDouble(), 0.0, 1.0);
    const auto y = std::clamp(
        point.value(QStringLiteral("y")).toDouble(), 0.0, 1.0);
    left = std::min(left, x);
    top = std::min(top, y);
    right = std::max(right, x);
    bottom = std::max(bottom, y);
    ++count;
  }
  if (count <= 0 || right <= left || bottom <= top) {
    return {};
  }
  return QRectF(QPointF(left, top), QPointF(right, bottom));
}

}  // namespace photo_editor::desktop
