#include "editor_recipe_save_journal.h"
#include "edit_recipe_mask_schema.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <algorithm>
#include <cmath>
#include <iostream>

using namespace photo_editor::desktop;
int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  constexpr int iterations = 24;
  QJsonArray results;
  for (const auto& scenario : {QStringLiteral("small_tone"), QStringLiteral("1mib_tone"),
                             QStringLiteral("1mib_brush"), QStringLiteral("4mib_tone")}) {
    QTemporaryDir root;
    if (!root.isValid()) return 1;
    const int edge = scenario.startsWith("4mib") ? 2048 : scenario.startsWith("small") ? 0 : 1024;
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
      mask.insert("segmentationWidth", edge);
      mask.insert("segmentationHeight", edge);
      mask.insert("segmentationData", alpha);
      mask.insert("localExposure", 0.4);
      state.mask_stack.append(mask);
    }
    // Normalize once, then change real edits on every measured write.
    state = parse_edit_recipe_json(serialize_edit_recipe_json(state));
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
        if (scenario.endsWith("brush")) {
          auto mask = state.mask_stack.first().toMap();
          auto alpha = mask.value("segmentationData").toByteArray();
          alpha[static_cast<qsizetype>(index + 1)] ^= 0x7f;
          mask.insert("segmentationData", alpha);
          state.mask_stack.first() = mask;
        }
        queue.enqueue({.asset_id = "benchmark-asset", .file_path = "/benchmark.dng", .state = state});
        QElapsedTimer timer; timer.start();
        if (!journal.write(queue.checkpoint(), &error)) { std::cerr << error.toStdString(); return 1; }
        const auto elapsed = static_cast<double>(timer.nsecsElapsed()) / 1000.0;
        if (index < 0) cold_us = elapsed; else samples.append(elapsed);
      }
      bytes = QFileInfo(journal.path()).size();
    }
    if (edge > 0 && bytes < static_cast<qint64>(edge) * edge) return 1;
    {
      EditorRecipeSaveJournal reopened;
      EditorRecipeSaveCheckpoint recovered;
      if (!reopened.open(root.path(), "benchmark-catalog", &recovered, &error) ||
          recovered.pending.size() != 1 || recovered.active || recovered.retry_migration ||
          serialize_edit_recipe_json(recovered.pending.first().state) != serialize_edit_recipe_json(state)) return 1;
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
