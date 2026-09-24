#include "brush_input_schema.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

#include <QByteArray>
#include <QVariantList>

namespace {

int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

bool close(double actual, double expected) {
  return std::abs(actual - expected) < 1e-12;
}

}  // namespace

int main() {
  using namespace photo_editor::desktop;

  QVariantMap mask;
  if (ensure_brush_raster_mask(nullptr, QSize(4, 3)) ||
      ensure_brush_raster_mask(&mask, QSize(1, 3)) ||
      !ensure_brush_raster_mask(&mask, QSize(4, 3)) ||
      mask.value(QStringLiteral("segmentationWidth")).toInt() != 4 ||
      mask.value(QStringLiteral("segmentationHeight")).toInt() != 3 ||
      mask.value(QStringLiteral("segmentationData")).toByteArray() !=
          QByteArray(12, char(0))) {
    return fail("brush raster admission must reject invalid requests and create exact zero alpha");
  }
  if (!ensure_brush_raster_mask(&mask, QSize(8, 8)) ||
      mask.value(QStringLiteral("segmentationWidth")).toInt() != 4 ||
      mask.value(QStringLiteral("segmentationHeight")).toInt() != 3) {
    return fail("brush raster admission must retain an existing valid segmentation payload");
  }

  const auto raw_point = brush_point_variant(
      0.25, 0.75, 0.0, 1.4, -0.2, -1.0, 2.0, 1.4, -2.0, 2.0, true);
  if (!close(raw_point.value(QStringLiteral("size")).toDouble(), 0.01) ||
      !close(raw_point.value(QStringLiteral("flow")).toDouble(), 1.0) ||
      !close(raw_point.value(QStringLiteral("opacity")).toDouble(), 0.0) ||
      !close(raw_point.value(QStringLiteral("autoMaskAnchorX")).toDouble(), 0.0) ||
      !close(raw_point.value(QStringLiteral("autoMaskAnchorY")).toDouble(), 1.0) ||
      !close(raw_point.value(QStringLiteral("pressure")).toDouble(), 1.0) ||
      !close(raw_point.value(QStringLiteral("tiltX")).toDouble(), -1.0) ||
      !close(raw_point.value(QStringLiteral("tiltY")).toDouble(), 1.0) ||
      !raw_point.value(QStringLiteral("erase")).toBool()) {
    return fail("brush point serialization must apply exact physical-input bounds");
  }

  const auto preview_point = brush_preview_point(
      0.2, 0.3, 0.2, 0.8, 1.0, 1.0, 1.0, 0.5, 1.0, 0.0, true);
  if (!close(preview_point.size, 0.135) || !close(preview_point.flow, 0.4) ||
      !close(preview_point.opacity, 0.5) ||
      !close(preview_point.auto_mask_anchor_x, 0.2) ||
      !close(preview_point.auto_mask_anchor_y, 0.3) || !preview_point.erase) {
    return fail("brush preview input must combine pressure and tilt deterministically");
  }
  const auto round_trip = brush_preview_point_from_variant(
      brush_point_variant(preview_point), 0.0, 0.0, 0.01, 0.0);
  if (!close(round_trip.x, preview_point.x) ||
      !close(round_trip.y, preview_point.y) ||
      !close(round_trip.size, preview_point.size) ||
      !close(round_trip.flow, preview_point.flow) ||
      !close(round_trip.opacity, preview_point.opacity) ||
      !close(round_trip.pressure, preview_point.pressure) ||
      !close(round_trip.tilt_x, preview_point.tilt_x) ||
      !close(round_trip.tilt_y, preview_point.tilt_y) ||
      round_trip.erase != preview_point.erase) {
    return fail("brush point variants must round-trip every preview input field");
  }

  QVariantMap replay_mask;
  replay_mask.insert(QStringLiteral("brushSize"), 0.2);
  replay_mask.insert(QStringLiteral("brushFlow"), 0.3);
  QVariantMap flat_point;
  flat_point.insert(QStringLiteral("x"), 0.1);
  flat_point.insert(QStringLiteral("y"), 0.2);
  replay_mask.insert(QStringLiteral("brushPoints"), QVariantList{flat_point});
  QVariantMap ignored_sample;
  ignored_sample.insert(QStringLiteral("x"), 0.9);
  QVariantMap ignored_stroke;
  ignored_stroke.insert(QStringLiteral("samples"), QVariantList{ignored_sample});
  replay_mask.insert(QStringLiteral("brushStrokes"), QVariantList{ignored_stroke});
  const auto flat_replay = brush_points_for_target_raster_replay(replay_mask);
  if (flat_replay.size() != 1 || !close(flat_replay.constFirst().x, 0.1) ||
      !close(flat_replay.constFirst().y, 0.2) ||
      !close(flat_replay.constFirst().size, 0.2) ||
      !close(flat_replay.constFirst().flow, 0.3)) {
    return fail("flat brush points must take precedence and inherit mask fallbacks during replay");
  }
  replay_mask.remove(QStringLiteral("brushPoints"));
  ignored_stroke.insert(QStringLiteral("operation"), QStringLiteral(" ERASE "));
  replay_mask.insert(QStringLiteral("brushStrokes"), QVariantList{ignored_stroke});
  const auto stroke_replay = brush_points_for_target_raster_replay(replay_mask);
  if (stroke_replay.size() != 1 || !close(stroke_replay.constFirst().x, 0.9) ||
      !close(stroke_replay.constFirst().y, 0.5) || !stroke_replay.constFirst().erase ||
      !brush_points_for_target_raster_replay({}).isEmpty()) {
    return fail("stroke replay must normalize erase operations, fallbacks, and empty input");
  }

  const QVector<BrushPreviewPoint> dirty_points{{
      .x = 0.5,
      .y = 0.5,
      .size = 0.1,
  }};
  const auto dirty = dirty_region_for_brush_points(dirty_points);
  if (dirty_region_for_brush_points({}).isValid() || !close(dirty.left(), 0.39) ||
      !close(dirty.top(), 0.39) || !close(dirty.width(), 0.22) ||
      !close(dirty.height(), 0.22)) {
    return fail("brush dirty regions must be exact, padded, and empty-safe");
  }

  QVariantMap stroke_mask;
  stroke_mask.insert(QStringLiteral("brushPreset"), QStringLiteral("Soft"));
  QVector<BrushPreviewPoint> erase_samples{preview_point};
  const auto stroke = brush_stroke_variant(stroke_mask, erase_samples, 0);
  if (stroke.value(QStringLiteral("id")).toString() != QStringLiteral("stroke-1") ||
      stroke.value(QStringLiteral("createdAtOrder")).toInt() != 1 ||
      stroke.value(QStringLiteral("operation")).toString() != QStringLiteral("erase") ||
      stroke.value(QStringLiteral("sampleCount")).toInt() != 1 ||
      stroke.value(QStringLiteral("settings")).toMap().value(QStringLiteral("preset")).toString() !=
          QStringLiteral("Soft") ||
      stroke.value(QStringLiteral("samples")).toList().size() != 1) {
    return fail("brush stroke serialization must preserve order, operation, settings, and samples");
  }

  if (rounded_brush_decode_edge(1.0) != 64 ||
      rounded_brush_decode_edge(65.0) != 128 ||
      auto_mask_source_decode_edge_for_brush_commit({}, {}, 300, 200, true) != 512 ||
      auto_mask_source_decode_edge_for_brush_commit({}, {}, 2000, 1000, true) != 1024) {
    return fail("brush decode sizing must quantize and clamp existing segmentation dimensions");
  }
  auto small = BrushPreviewPoint{};
  small.size = 0.1;
  auto large = BrushPreviewPoint{};
  large.size = 0.3;
  if (auto_mask_source_decode_edge_for_brush_commit({small}, dirty, 0, 0, false) != 512 ||
      auto_mask_source_decode_edge_for_brush_commit({large}, {}, 0, 0, false) != 1024) {
    return fail("brush decode sizing must bound new source work from stroke footprint");
  }

  return EXIT_SUCCESS;
}
