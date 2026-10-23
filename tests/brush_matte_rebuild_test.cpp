#include "brush_matte_rebuild.h"
#include "scaled_mask_matte_service.h"

#include <cstdlib>
#include <iostream>

#include <QByteArray>
#include <QVariantList>

namespace {

int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

QVariantMap point(double x, double y, bool erase = false) {
  QVariantMap value;
  value.insert(QStringLiteral("x"), x);
  value.insert(QStringLiteral("y"), y);
  value.insert(QStringLiteral("size"), 0.25);
  value.insert(QStringLiteral("flow"), 1.0);
  value.insert(QStringLiteral("opacity"), 1.0);
  if (erase) {
    value.insert(QStringLiteral("erase"), true);
  }
  return value;
}

unsigned char alpha_at(const QVariantMap& mask, int x, int y) {
  const auto width = mask.value(QStringLiteral("segmentationWidth")).toInt();
  const auto data = mask.value(QStringLiteral("segmentationData")).toByteArray();
  return static_cast<unsigned char>(data.at(y * width + x));
}

}  // namespace

int main() {
  using namespace photo_editor::desktop;

  QVariantMap non_brush;
  non_brush.insert(QStringLiteral("kind"), QStringLiteral("subject"));
  QVariantMap empty_brush;
  empty_brush.insert(QStringLiteral("kind"), QStringLiteral("brush"));
  if (!mask_with_rebuilt_brush_matte(non_brush, QSize(9, 9), QStringLiteral("preview"))
           .isEmpty() ||
      !mask_with_rebuilt_brush_matte(empty_brush, QSize(9, 9), QStringLiteral("preview"))
           .isEmpty() ||
      !mask_with_rebuilt_brush_matte(empty_brush, QSize(1, 9), QStringLiteral("preview"))
           .isEmpty()) {
    return fail("brush matte rebuild must reject non-brush, empty, and degenerate requests");
  }

  QVariantMap brush;
  brush.insert(QStringLiteral("kind"), QStringLiteral("brush"));
  brush.insert(QStringLiteral("segmentationWidth"), 4);
  brush.insert(QStringLiteral("segmentationHeight"), 3);
  brush.insert(QStringLiteral("feather"), 0.05);
  brush.insert(QStringLiteral("brushPoints"), QVariantList{point(0.5, 0.5)});
  const auto rebuilt = mask_with_rebuilt_brush_matte(
      brush, QSize(9, 9), QStringLiteral("full-resolution-render"));
  if (rebuilt.value(QStringLiteral("segmentationWidth")).toInt() != 9 ||
      rebuilt.value(QStringLiteral("segmentationHeight")).toInt() != 9 ||
      alpha_at(rebuilt, 4, 4) != 255U || alpha_at(rebuilt, 0, 0) != 0U ||
      rebuilt.value(QStringLiteral("scaledMattePurpose")).toString() !=
          QStringLiteral("full-resolution-render") ||
      rebuilt.value(QStringLiteral("scaledMatteSourceWidth")).toInt() != 4 ||
      rebuilt.value(QStringLiteral("scaledMatteSourceHeight")).toInt() != 3 ||
      rebuilt.value(QStringLiteral("scaledMatteWidth")).toInt() != 9 ||
      rebuilt.value(QStringLiteral("scaledMatteHeight")).toInt() != 9 ||
      !rebuilt.value(QStringLiteral("scaledMatteRebuiltFromBrushStrokes")).toBool() ||
      rebuilt.value(QStringLiteral("scaledMatteBrushReplayPointCount")).toInt() != 1 ||
      rebuilt.value(QStringLiteral("scaled_matte_rebuild_pixels")).toLongLong() != 81 ||
      rebuilt.value(QStringLiteral("full_resolution_matte_rebuild_pixels")).toLongLong() != 81 ||
      !rebuilt.contains(QStringLiteral("scaled_matte_rebuild_ms")) ||
      !rebuilt.contains(QStringLiteral("full_resolution_matte_rebuild_ms"))) {
    return fail("brush matte rebuild must replay exact alpha and publish complete size telemetry");
  }

  brush.insert(
      QStringLiteral("brushPoints"),
      QVariantList{point(0.5, 0.5), point(0.5, 0.5, true)});
  const auto erased =
      mask_with_rebuilt_brush_matte(brush, QSize(9, 9), QStringLiteral("preview"));
  if (alpha_at(erased, 4, 4) != 0U ||
      erased.value(QStringLiteral("scaledMatteBrushReplayPointCount")).toInt() != 2 ||
      erased.contains(QStringLiteral("full_resolution_matte_rebuild_pixels"))) {
    return fail("brush matte replay must preserve point order and scope full-resolution telemetry");
  }

  auto cached_brush = brush;
  cached_brush.insert(QStringLiteral("segmentationData"), QByteArray(4 * 3, '\x19'));
  cached_brush.insert(QStringLiteral("segmentationWidth"), 4);
  cached_brush.insert(QStringLiteral("segmentationHeight"), 3);
  const auto committed_before_cache =
      cached_brush.value(QStringLiteral("segmentationData")).toByteArray();
  clear_scaled_mask_matte_cache();
  const auto expected_cached_matte = mask_with_rebuilt_brush_matte(
      cached_brush, QSize(9, 9), QStringLiteral("preview"));
  const auto first_cached_matte = mask_with_scaled_segmentation_matte(
      cached_brush, QSize(9, 9), QStringLiteral("preview"));
  auto cache_state = scaled_mask_matte_cache_snapshot();
  if (first_cached_matte.value(QStringLiteral("segmentationData")).toByteArray() !=
          expected_cached_matte.value(QStringLiteral("segmentationData")).toByteArray() ||
      cache_state.brush_replay_entries != 1 ||
      cache_state.brush_replay_bytes != 9 * 9 ||
      cache_state.brush_replay_miss_count != 1 ||
      cache_state.brush_replay_hit_count != 0 ||
      cache_state.total_bytes > kMaxScaledMaskMatteCacheMemoryBytes ||
      cache_state.total_entries > kMaxScaledMaskMatteCacheEntries ||
      cached_brush.value(QStringLiteral("segmentationData")).toByteArray() !=
          committed_before_cache) {
    return fail(
        "first brush replay cache fill must match exact replay without mutating the source matte");
  }

  const auto second_cached_matte = mask_with_scaled_segmentation_matte(
      cached_brush, QSize(9, 9), QStringLiteral("preview"));
  cache_state = scaled_mask_matte_cache_snapshot();
  if (second_cached_matte.value(QStringLiteral("segmentationData")).toByteArray() !=
          expected_cached_matte.value(QStringLiteral("segmentationData")).toByteArray() ||
      second_cached_matte.value(QStringLiteral("scaledMatteSourceWidth")).toInt() != 4 ||
      second_cached_matte.value(QStringLiteral("scaledMatteSourceHeight")).toInt() != 3 ||
      cache_state.brush_replay_hit_count != 1 ||
      cache_state.brush_replay_miss_count != 1 ||
      cached_brush.value(QStringLiteral("segmentationData")).toByteArray() !=
          committed_before_cache) {
    return fail(
        "brush replay cache hits must preserve byte-exact output metadata and source immutability");
  }

  auto changed_settings = cached_brush;
  changed_settings.insert(QStringLiteral("feather"), 0.8);
  (void)mask_with_scaled_segmentation_matte(
      changed_settings, QSize(9, 9), QStringLiteral("preview"));
  auto changed_purpose = cached_brush;
  (void)mask_with_scaled_segmentation_matte(
      changed_purpose, QSize(9, 9), QStringLiteral("thumbnail"));
  (void)mask_with_scaled_segmentation_matte(
      cached_brush, QSize(10, 9), QStringLiteral("preview"));
  auto changed_geometry = cached_brush;
  changed_geometry.insert(QStringLiteral("brushPoints"), QVariantList{point(0.25, 0.5)});
  (void)mask_with_scaled_segmentation_matte(
      changed_geometry, QSize(9, 9), QStringLiteral("preview"));
  auto signed_brush = cached_brush;
  signed_brush.insert(QStringLiteral("brushAlphaSourceSignature"), QStringLiteral("revision-a"));
  (void)mask_with_scaled_segmentation_matte(
      signed_brush, QSize(9, 9), QStringLiteral("preview"));
  signed_brush.insert(QStringLiteral("brushAlphaSourceSignature"), QStringLiteral("revision-b"));
  (void)mask_with_scaled_segmentation_matte(
      signed_brush, QSize(9, 9), QStringLiteral("preview"));
  cache_state = scaled_mask_matte_cache_snapshot();
  if (cache_state.brush_replay_miss_count != 7 ||
      cache_state.brush_replay_entries != 7 ||
      cache_state.total_bytes > kMaxScaledMaskMatteCacheMemoryBytes) {
    return fail(
        "brush replay cache keys must distinguish settings, purpose, and source signatures");
  }

  auto source_dependent_brush = cached_brush;
  source_dependent_brush.insert(QStringLiteral("brushProtectEdges"), true);
  (void)mask_with_scaled_segmentation_matte(
      source_dependent_brush, QSize(9, 9), QStringLiteral("preview"));
  source_dependent_brush = cached_brush;
  source_dependent_brush.insert(QStringLiteral("brushAutoMask"), true);
  (void)mask_with_scaled_segmentation_matte(
      source_dependent_brush, QSize(9, 9), QStringLiteral("preview"));
  source_dependent_brush = cached_brush;
  QVariantMap protected_stroke_settings;
  protected_stroke_settings.insert(QStringLiteral("protectEdges"), true);
  QVariantMap protected_stroke;
  protected_stroke.insert(QStringLiteral("samples"), QVariantList{point(0.5, 0.5)});
  protected_stroke.insert(QStringLiteral("settings"), protected_stroke_settings);
  source_dependent_brush.insert(QStringLiteral("brushStrokes"), QVariantList{protected_stroke});
  (void)mask_with_scaled_segmentation_matte(
      source_dependent_brush, QSize(9, 9), QStringLiteral("preview"));
  const auto after_source_dependent = scaled_mask_matte_cache_snapshot();
  if (after_source_dependent.brush_replay_miss_count != 7 ||
      after_source_dependent.brush_replay_entries != 7 ||
      cached_brush.value(QStringLiteral("segmentationData")).toByteArray() !=
          committed_before_cache) {
    return fail(
        "source-dependent brush cases must bypass replay caching and preserve the committed matte");
  }
  clear_scaled_mask_matte_cache();
  for (int index = 0; index < kMaxScaledMaskMatteCacheEntries + 3; ++index) {
    auto bounded_brush = cached_brush;
    bounded_brush.insert(
        QStringLiteral("brushAlphaSourceSignature"),
        QStringLiteral("bounded-%1").arg(index));
    (void)mask_with_scaled_segmentation_matte(
        bounded_brush, QSize(2, 2), QStringLiteral("preview"));
  }
  cache_state = scaled_mask_matte_cache_snapshot();
  if (cache_state.total_entries != kMaxScaledMaskMatteCacheEntries ||
      cache_state.brush_replay_entries != kMaxScaledMaskMatteCacheEntries ||
      cache_state.brush_replay_eviction_count < 3 ||
      cache_state.total_bytes > kMaxScaledMaskMatteCacheMemoryBytes) {
    return fail(
        "brush replay cache must evict least-recently-used entries at its shared capacity ceiling");
  }
  clear_scaled_mask_matte_cache();
  cache_state = scaled_mask_matte_cache_snapshot();
  if (cache_state.total_entries != 0 || cache_state.total_bytes != 0 ||
      cache_state.brush_replay_hit_count != 0 || cache_state.brush_replay_miss_count != 0) {
    return fail("clearing scaled matte caches must release brush replay data and reset counters");
  }

  return EXIT_SUCCESS;
}
