#include "edit_adjustment_patch.h"

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
