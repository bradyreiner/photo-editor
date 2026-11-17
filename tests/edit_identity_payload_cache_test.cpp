#include "edit_adjustment_patch.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>
#include <future>
#include <iostream>
#include <vector>

using namespace photo_editor::desktop;

namespace {
EditRecipeState fixture(int tile_count) {
  EditRecipeState state;
  for (int layer_index = 0; layer_index < 2; ++layer_index) {
    QVariantMap tiles;
    for (int tile = 0; tile < tile_count; ++tile) {
      QString data(64 * 1024, QChar('A' + layer_index));
      data[0] = QChar('A' + tile % 26);
      tiles.insert(QStringLiteral("%1,0").arg(tile), data);
    }
    state.layers.append(QVariantMap{{"id", QString::number(layer_index)}, {"kind", "pixel"},
        {"name", "Cutout"}, {"x", 0}, {"paintTiles", QVariantMap{{"version", 1}, {"tiles", tiles}}}});
  }
  state.layers.append(QVariantMap{{"id", "text"}, {"kind", "text"}, {"text", "Trip"}});
  return state;
}

EditRecipeState brush_history_fixture(int point_count, int geometry_revision = 0) {
  QVariantList points;
  points.reserve(point_count);
  for (int index = 0; index < point_count; ++index) {
    points.append(QVariantMap{
        {QStringLiteral("x"), index == 0
             ? static_cast<double>(geometry_revision) / 1000.0
             : static_cast<double>(index % 997) / 996.0},
        {QStringLiteral("y"), static_cast<double>(index % 991) / 990.0},
        {QStringLiteral("size"), 0.08},
        {QStringLiteral("flow"), 0.75},
        {QStringLiteral("opacity"), 0.9},
        {QStringLiteral("pressure"), 0.8},
    });
  }
  const QVariantMap settings{
      {QStringLiteral("rasterVersion"), 2},
      {QStringLiteral("feather"), 0.75},
      {QStringLiteral("hardness"), 0.3},
  };
  const QVariantMap stroke{
      {QStringLiteral("operation"), QStringLiteral("paint")},
      {QStringLiteral("settings"), settings},
      {QStringLiteral("samples"), points},
  };
  const QVariantMap mask{
      {QStringLiteral("id"), QStringLiteral("brush-mask")},
      {QStringLiteral("kind"), QStringLiteral("brush")},
      {QStringLiteral("brushPoints"), points},
      {QStringLiteral("brushStrokes"), QVariantList{stroke}},
  };
  EditRecipeState state;
  state.mask_stack.append(mask);
  return state;
}

QVariantMap mask_with_points(
    const EditRecipeState& state,
    const QVariantList& points) {
  auto mask = state.mask_stack.at(0).toMap();
  mask.insert(QStringLiteral("brushPoints"), points);
  return mask;
}

bool check(bool condition, const char* message) {
  if (!condition) std::cerr << message << '\n';
  return condition;
}
}

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  const bool benchmark = app.arguments().contains("--benchmark");
  auto state = fixture(benchmark ? 768 : 2);
  const auto original = state;
  QElapsedTimer timer;
  timer.start();
  const auto identity = adjustment_patch_identity_json(state);
  const double cold_ms = static_cast<double>(timer.nsecsElapsed()) / 1e6;
  if (!check(!identity.isEmpty() && adjustment_patch_identity_json(state) == identity,
             "Repeated reads must retain the exact identity")) return 1;

  auto moved = state;
  auto layer = moved.layers[0].toMap();
  layer.insert("x", 7);
  moved.layers[0] = layer;
  if (!check(adjustment_patch_identity_json(moved) != identity, "Transform must invalidate identity")) return 1;
  auto painted = state;
  layer = painted.layers[0].toMap();
  auto payload = layer.value("paintTiles").toMap();
  auto tiles = payload.value("tiles").toMap();
  auto data = tiles.first().toString();
  data[19] = 'Z';
  tiles[tiles.firstKey()] = data;
  payload["tiles"] = tiles;
  layer["paintTiles"] = payload;
  painted.layers[0] = layer;
  if (!check(adjustment_patch_identity_json(painted) != identity,
             "Pixel edits with unchanged tile keys must invalidate identity")) return 1;
  if (!check(state.layers == original.layers && adjustment_patch_identity_json(state) == identity,
             "Undo and imported snapshots must keep their pixels and identity")) return 1;
  auto text = state;
  layer = text.layers[2].toMap(); layer["text"] = "New trip"; text.layers[2] = layer;
  if (!check(adjustment_patch_identity_json(text) != identity, "Text edits must invalidate identity")) return 1;
  auto reordered = state;
  std::swap(reordered.layers[0], reordered.layers[1]);
  if (!check(adjustment_patch_identity_json(reordered) != identity, "Reordering must invalidate identity")) return 1;
  auto tone = state; tone.exposure = 0.5;
  if (!check(adjustment_patch_identity_json(tone) != identity, "Tone edits must invalidate identity")) return 1;
  auto removed = state; removed.layers.removeAt(0);
  if (!check(adjustment_patch_identity_json(removed) != identity, "Removal must invalidate identity")) return 1;
  if (!benchmark) {
    std::vector<std::future<bool>> reads;
    for (int thread = 0; thread < 4; ++thread) {
      reads.push_back(std::async(std::launch::async, [state, identity, thread]() {
        auto changed = state;
        auto row = changed.layers[0].toMap(); row["x"] = thread + 100; changed.layers[0] = row;
        const auto changed_identity = adjustment_patch_identity_json(changed);
        for (int iteration = 0; iteration < 10; ++iteration) {
          if (adjustment_patch_identity_json(state) != identity ||
              adjustment_patch_identity_json(changed) != changed_identity || changed_identity == identity) return false;
        }
        return true;
      }));
    }
    for (auto& read : reads)
      if (!check(read.get(), "Concurrent preview and UI queries must retain their own identities")) return 1;
  }

  const auto brush_state = brush_history_fixture(10'000);
  const auto brush_identity = adjustment_patch_identity_json(brush_state);
  auto history_cache = brush_history_identity_cache_snapshot();
  if (!check(history_cache.entries == 2 && history_cache.miss_count == 2 &&
                 history_cache.hit_count >= 1 &&
                 history_cache.retained_bytes > 0,
             "Shared point/sample payloads must reuse one exact content digest")) {
    return 1;
  }
  const auto cache_before_scalar = history_cache;
  auto exposure_revision = brush_state;
  exposure_revision.exposure = 0.4;
  auto opacity_revision_mask = exposure_revision.mask_stack.at(0).toMap();
  opacity_revision_mask.insert(QStringLiteral("opacity"), 0.55);
  exposure_revision.mask_stack[0] = opacity_revision_mask;
  const auto exposure_identity = adjustment_patch_identity_json(exposure_revision);
  history_cache = brush_history_identity_cache_snapshot();
  if (!check(exposure_identity != brush_identity &&
                 history_cache.hit_count >= cache_before_scalar.hit_count + 3 &&
                 history_cache.miss_count == cache_before_scalar.miss_count,
             "Scalar revisions must reuse exact cached brush-history digests")) {
    return 1;
  }

  auto changed_points = brush_state;
  auto points = changed_points.mask_stack.at(0).toMap()
                    .value(QStringLiteral("brushPoints")).toList();
  auto changed_point = points.at(5'000).toMap();
  changed_point.insert(QStringLiteral("x"), 0.123456);
  points[5'000] = changed_point;
  auto changed_mask = mask_with_points(changed_points, points);
  auto changed_strokes = changed_mask.value(QStringLiteral("brushStrokes")).toList();
  auto changed_stroke = changed_strokes.at(0).toMap();
  auto changed_samples = changed_stroke.value(QStringLiteral("samples")).toList();
  changed_samples[5'000] = changed_point;
  changed_stroke.insert(QStringLiteral("samples"), changed_samples);
  changed_strokes[0] = changed_stroke;
  changed_mask.insert(QStringLiteral("brushStrokes"), changed_strokes);
  changed_points.mask_stack[0] = changed_mask;
  const auto geometry_identity = adjustment_patch_identity_json(changed_points);
  const auto cache_after_geometry = brush_history_identity_cache_snapshot();
  if (!check(geometry_identity != brush_identity &&
                 cache_after_geometry.miss_count >= history_cache.miss_count + 1,
             "A brush geometry edit must invalidate the compact history digest")) {
    return 1;
  }

  auto changed_settings = brush_state;
  auto settings_stroke = changed_settings.mask_stack.at(0).toMap()
                             .value(QStringLiteral("brushStrokes")).toList().at(0).toMap();
  auto settings = settings_stroke.value(QStringLiteral("settings")).toMap();
  settings.insert(QStringLiteral("hardness"), 0.65);
  settings_stroke.insert(QStringLiteral("settings"), settings);
  auto strokes = QVariantList{settings_stroke};
  auto settings_mask = changed_settings.mask_stack.at(0).toMap();
  settings_mask.insert(QStringLiteral("brushStrokes"), strokes);
  changed_settings.mask_stack[0] = settings_mask;
  const auto settings_identity = adjustment_patch_identity_json(changed_settings);
  const auto cache_after_settings = brush_history_identity_cache_snapshot();
  if (!check(settings_identity != brush_identity &&
                 cache_after_settings.miss_count >= cache_after_geometry.miss_count + 1 &&
                 cache_after_settings.retained_bytes <=
                     kMaxBrushHistoryIdentityCacheMemoryBytes &&
                 cache_after_settings.entries <= kMaxBrushHistoryIdentityCacheEntries,
             "Brush raster-setting changes must invalidate history identity "
             "within the cache budget")) {
    return 1;
  }

  auto reordered_brush = brush_state;
  auto reordered_mask = reordered_brush.mask_stack.at(0).toMap();
  auto reordered_points = reordered_mask.value(QStringLiteral("brushPoints")).toList();
  std::swap(reordered_points[0], reordered_points[1]);
  reordered_mask.insert(QStringLiteral("brushPoints"), reordered_points);
  auto reordered_stroke = reordered_mask.value(QStringLiteral("brushStrokes"))
                              .toList().at(0).toMap();
  auto reordered_samples = reordered_stroke.value(QStringLiteral("samples")).toList();
  std::swap(reordered_samples[0], reordered_samples[1]);
  reordered_stroke.insert(QStringLiteral("samples"), reordered_samples);
  reordered_mask.insert(
      QStringLiteral("brushStrokes"), QVariantList{reordered_stroke});
  reordered_brush.mask_stack[0] = reordered_mask;
  if (!check(adjustment_patch_identity_json(reordered_brush) != brush_identity,
             "Brush sample ordering must remain part of the compact identity")) {
    return 1;
  }

  EditRecipeState byte_payload_a;
  QVariantMap byte_mask{{QStringLiteral("id"), QStringLiteral("byte-mask")}};
  byte_mask.insert(QStringLiteral("brushPoints"), QByteArray(8192, 'A'));
  byte_payload_a.mask_stack.append(byte_mask);
  auto byte_payload_b = byte_payload_a;
  byte_mask.insert(QStringLiteral("brushPoints"), QByteArray(8192, 'B'));
  byte_payload_b.mask_stack[0] = byte_mask;
  if (!check(adjustment_patch_identity_json(byte_payload_a) !=
                 adjustment_patch_identity_json(byte_payload_b),
             "Large scalar payloads must retain their content in compact identity")) {
    return 1;
  }

  for (int revision = 1;
       revision <= kMaxBrushHistoryIdentityCacheEntries / 2 + 1;
       ++revision) {
    const auto history_revision = brush_history_fixture(300, revision);
    (void)adjustment_patch_identity_json(history_revision);
  }
  history_cache = brush_history_identity_cache_snapshot();
  if (!check(history_cache.entries == kMaxBrushHistoryIdentityCacheEntries &&
                 history_cache.eviction_count > 0 &&
                 history_cache.retained_bytes <=
                     kMaxBrushHistoryIdentityCacheMemoryBytes,
             "Brush history identity cache must enforce its bounded LRU budget")) {
    return 1;
  }
  if (benchmark) {
    QVector<double> warm, transforms;
    for (int sample = 0; sample < 7; ++sample) {
      timer.restart();
      if (adjustment_patch_identity_json(state) != identity) return 1;
      warm.append(static_cast<double>(timer.nsecsElapsed()) / 1e6);
      layer = state.layers[0].toMap(); layer["x"] = sample + 20; state.layers[0] = layer;
      timer.restart();
      if (adjustment_patch_identity_json(state) == identity) return 1;
      transforms.append(static_cast<double>(timer.nsecsElapsed()) / 1e6);
      state = original;
    }
    std::sort(warm.begin(), warm.end()); std::sort(transforms.begin(), transforms.end());
    std::cout << QJsonDocument(QJsonObject{{"cold_ms", cold_ms}, {"warm_median_ms", warm[3]},
        {"transform_median_ms", transforms[3]}, {"warm_max_ms", warm.last()},
        {"transform_max_ms", transforms.last()}, {"samples", 7},
        {"paint_payload_ascii_bytes", 2 * 768 * 64 * 1024}, {"correctness", true}})
        .toJson(QJsonDocument::Compact).constData() << '\n';
  } else {
    std::cout << "Identity reuse preserves transforms, pixels, text, order, tone and undo snapshots.\n";
  }
}
