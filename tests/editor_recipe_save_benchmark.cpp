#include "editor_recipe_save_journal.h"
#include "edit_recipe_mask_schema.h"
#include "photo_paint_surface.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QFile>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <cstring>

namespace {
int paint_checkpoint_benchmark() {
  using namespace photo_editor::desktop;
  QTemporaryDir root;
  if (!root.isValid()) return 1;
  constexpr int tile_count = 192;
  QByteArray raw(kPhotoPaintTileEdge * kPhotoPaintTileEdge * 16, '\0');
  quint32 random = 0x12345678U;
  for (int pixel = 0; pixel < kPhotoPaintTileEdge * kPhotoPaintTileEdge; ++pixel) {
    for (int channel = 0; channel < 4; ++channel) {
      random ^= random << 13; random ^= random >> 17; random ^= random << 5;
      const float value = channel == 3 ? 1.0f : static_cast<float>(random & 0xffffU) / 65535.0f;
      quint32 bits;
      std::memcpy(&bits, &value, sizeof(bits));
      qToLittleEndian(bits, raw.data() + pixel * 16 + channel * 4);
    }
  }
  const auto tile = QString::fromLatin1(qCompress(raw, 1).toBase64());
  EditRecipeState state;
  for (int layer_index = 0; layer_index < 2; ++layer_index) {
    auto payload = empty_photo_paint_tiles(QColorSpace(QColorSpace::SRgbLinear));
    QVariantMap tiles;
    for (int index = 0; index < tile_count; ++index)
      tiles.insert(QStringLiteral("%1,%2").arg(index % 48).arg(index / 48), tile);
    payload["tiles"] = tiles;
    state.layers.append(QVariantMap{{"id", QStringLiteral("cutout-%1").arg(layer_index)},
        {"kind", "pixel"}, {"name", "Cutout"}, {"pixelWidth", 6024}, {"pixelHeight", 4020},
        {"x", 0}, {"paintTiles", payload}});
  }
  state.layers.append(QVariantMap{{"id", "text"}, {"kind", "text"}, {"text", "Trip"}});
  state = parse_edit_recipe_json(serialize_edit_recipe_json(state));
  QString error, path;
  QVector<double> samples;
  double cold_ms = 0;
  {
    EditorRecipeSaveJournal journal;
    EditorRecipeSaveCheckpoint recovered;
    if (!journal.open(root.path(), "paint-checkpoint-benchmark", &recovered, &error)) return 1;
    path = journal.path();
    for (int sample = -1; sample < 7; ++sample) {
      auto layer = state.layers[0].toMap(); layer["x"] = sample + 10; state.layers[0] = layer;
      EditorRecipeSaveCheckpoint checkpoint;
      checkpoint.pending.append({.asset_id = "paint-asset", .file_path = "/benchmark.png", .state = state});
      QElapsedTimer timer; timer.start();
      if (!journal.write(checkpoint, &error)) { std::cerr << error.toStdString(); return 1; }
      const double elapsed = static_cast<double>(timer.nsecsElapsed()) / 1e6;
      if (sample < 0) cold_ms = elapsed; else samples.append(elapsed);
    }
  }
  QFile input(path);
  if (!input.open(QIODevice::ReadOnly)) return 1;
  const auto bytes = input.readAll();
  const auto hash = QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
  input.close();
  EditorRecipeSaveJournal reopened;
  EditorRecipeSaveCheckpoint recovered;
  if (!reopened.open(root.path(), "paint-checkpoint-benchmark", &recovered, &error) ||
      recovered.pending.size() != 1 || recovered.pending[0].state.layers != state.layers) return 1;
  PhotoPaintSurface surface;
  if (!surface.load(recovered.pending[0].state.layers[0].toMap(), &error) ||
      surface.tile(0, 0).isNull()) return 1;
  std::sort(samples.begin(), samples.end());
  std::cout << QJsonDocument(QJsonObject{{"scenario", "two-cutouts-and-text"}, {"samples", 7},
      {"tile_strings", tile_count * 2}, {"tile_string_bytes", tile.size()},
      {"checkpoint_bytes", bytes.size()}, {"cold_ms", cold_ms}, {"median_ms", samples[3]},
      {"max_ms", samples.last()}, {"sha256", hash}, {"reopen_exact", true}, {"tile_decode", true}})
      .toJson(QJsonDocument::Compact).constData() << '\n';
  return 0;
}
}

using namespace photo_editor::desktop;
int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  if (app.arguments().contains("--paint-tiles")) return paint_checkpoint_benchmark();
  if (argc == 2) {
    QJsonArray matrix;
    for (const auto* kind : {"brush", "alpha", "subject", "background", "sky", "depth", "linear", "radial"}) {
      for (int variant = 0; variant < 4; ++variant) {
        auto mask = default_mask_definition(QString::fromLatin1(kind));
        mask.insert("id", "codec-parity-mask");
        mask.insert("maskSetId", "codec-parity-set");
        mask.insert("name", QString::fromUtf8("Mask \"\\\n é"));
        mask.insert("segmentationWidth", variant == 2 ? 0 : 9);
        mask.insert("segmentationHeight", 7);
        mask.insert("segmentationData", QByteArray(variant == 1 ? 62 : 63, '\x7f'));
        mask.insert("brushStrokes", QVariantList{QVariantMap{
            {"samples", QVariantList{QVariantMap{{"x", .13}, {"y", .72}, {"pressure", .43}}}}}});
        if (variant == 3) {
          mask.insert("toolRole", "retouch");
          mask.insert("retouchMode", "clone");
          mask.insert("cloneSourceSet", true);
          mask.insert("cloneOffsetX", -.2);
        }
        EditRecipeState state;
        state.mask_stack.append(mask);
        matrix.append(build_edit_recipe_object(state));
      }
    }
    if (!QDir().mkpath(QString::fromLocal8Bit(argv[1]))) return 1;
    QFile output(QDir(QString::fromLocal8Bit(argv[1])).filePath("codec-matrix.json"));
    const auto bytes = QJsonDocument(matrix).toJson(QJsonDocument::Compact);
    if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size()) return 1;
  }
  constexpr int iterations = 24;
  QJsonArray results;
  for (const auto& scenario : {QStringLiteral("small_tone"), QStringLiteral("1mib_tone"),
                             QStringLiteral("1mib_brush"), QStringLiteral("4mib_tone"),
                             QStringLiteral("4mib_brush"), QStringLiteral("4mib_rejected"),
                             QStringLiteral("16mib_tone"), QStringLiteral("16mib_brush"),
                             QStringLiteral("4mib_retouch")}) {
    QTemporaryDir root;
    if (!root.isValid()) return 1;
    const int edge = scenario.startsWith("16mib") ? 4096 : scenario.startsWith("4mib") ? 2048 : scenario.startsWith("small") ? 0 : 1024;
    EditRecipeState state;
    if (edge > 0) {
      QByteArray alpha(edge * edge, '\0');
      quint32 noise = 0x12345678U;
      for (auto& byte : alpha) {
        noise ^= noise << 13; noise ^= noise >> 17; noise ^= noise << 5;
        byte = static_cast<char>(noise & 255U);
      }
      auto mask = default_mask_definition("brush");
      mask.insert("id", "checkpoint-benchmark-mask");
      mask.insert("maskSetId", "checkpoint-benchmark-set");
      if (scenario.endsWith("retouch")) {
        mask.insert("toolRole", "retouch");
        mask.insert("retouchMode", "clone");
      }
      mask.insert("segmentationWidth", edge);
      mask.insert("segmentationHeight", edge);
      mask.insert("segmentationData", alpha);
      mask.insert("localExposure", 0.4);
      state.mask_stack.append(mask);
    }
    // Normalize once, then change real edits on every measured write.
    state = parse_edit_recipe_json(serialize_edit_recipe_json(state));
    const bool retained_rejected = scenario.endsWith("rejected");
    const auto retained_state = state;
    if (retained_rejected) state = EditRecipeState{};
    QVector<double> samples;
    double cold_us = 0;
    qint64 bytes = 0;
    QString error;
    {
      EditorRecipeSaveJournal journal;
      EditorRecipeSaveCheckpoint recovered;
      if (!journal.open(root.path(), "benchmark-catalog", &recovered, &error)) return 1;
      EditorRecipeSaveQueue queue;
      for (int index = -1; index < iterations; ++index) {
        state.exposure = static_cast<double>(index + 2) / 100.0;
        if (scenario.endsWith("brush") || scenario.endsWith("retouch")) {
          auto mask = state.mask_stack.first().toMap();
          auto alpha = mask.value("segmentationData").toByteArray();
          alpha[static_cast<qsizetype>(index + 1)] ^= 0x7f;
          mask.insert("segmentationData", alpha);
          state.mask_stack.first() = mask;
        }
        queue.enqueue({.asset_id = "benchmark-asset", .file_path = "/benchmark.dng", .state = state});
        QElapsedTimer timer; timer.start();
        auto checkpoint = queue.checkpoint();
        if (retained_rejected) checkpoint.rejected.append({.asset_id = "rejected-asset",
            .file_path = "/rejected.dng", .state = retained_state});
        if (!journal.write(checkpoint, &error)) { std::cerr << error.toStdString(); return 1; }
        const auto elapsed = static_cast<double>(timer.nsecsElapsed()) / 1000.0;
        if (index < 0) cold_us = elapsed; else samples.append(elapsed);
      }
      bytes = QFileInfo(journal.path()).size();
      if (argc == 2) {
        if (!QDir().mkpath(QString::fromLocal8Bit(argv[1]))) return 1;
        QFile input(journal.path());
        QFile output(QDir(QString::fromLocal8Bit(argv[1])).filePath(scenario + ".json"));
        if (!input.open(QIODevice::ReadOnly) || !output.open(QIODevice::WriteOnly)) return 1;
        const auto data = input.readAll();
        if (output.write(data) != data.size()) return 1;
      }
    }
    if (edge > 0 && bytes < static_cast<qint64>(edge) * edge) return 1;
    {
      EditorRecipeSaveJournal reopened;
      EditorRecipeSaveCheckpoint recovered;
      if (!reopened.open(root.path(), "benchmark-catalog", &recovered, &error) ||
          recovered.pending.size() != 1 || recovered.active || recovered.retry_migration ||
          serialize_edit_recipe_json(recovered.pending.first().state) != serialize_edit_recipe_json(state) ||
          recovered.rejected.size() != (retained_rejected ? 1 : 0) ||
          (retained_rejected && serialize_edit_recipe_json(recovered.rejected.first().state) !=
                                   serialize_edit_recipe_json(retained_state))) return 1;
    }
    std::sort(samples.begin(), samples.end());
    const auto percentile = [&](double p) { return samples.at(static_cast<qsizetype>(std::ceil(p * samples.size())) - 1); };
    results.append(QJsonObject{{"scenario", scenario}, {"iterations", iterations}, {"checkpoint_bytes", bytes},
        {"cold_us", cold_us}, {"p50_us", percentile(0.5)}, {"p95_us", percentile(0.95)}, {"max_us", samples.last()},
        {"reopen_exact", true}});
  }
  std::cout << QJsonDocument(QJsonObject{{"schema", "recipe-checkpoint-benchmark:v1"},
      {"changed_edit_each_iteration", true}, {"results", results}}).toJson(QJsonDocument::Indented).toStdString();
  return 0;
}
