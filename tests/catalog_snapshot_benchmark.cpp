#include "catalog_asset_model.h"
#include "edit_adjustment_patch.h"
#include "edit_recipe_state.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>
#include <algorithm>
#include <iostream>
#include <sys/resource.h>

namespace photo_editor::desktop {
struct CatalogSnapshotBenchmarkAccess {
  static void fetch(CatalogAssetModel& model, const QString& path) {
    model.timeline_asset_loading_suppressed_for_testing_ = true;
    model.snapshot_fetch_generation_ = ++model.snapshot_request_generation_;
    model.snapshot_fetch_active_ = true;
    model.setLoading(true);
    model.snapshot_process_.start(QStringLiteral("/bin/cat"), {path});
  }
  static bool valid(const CatalogAssetModel& model, int count, const QString& recipe) {
    if (model.all_assets_.size() != count || model.rowCount() != count) return false;
    for (int i = 0; i < count; ++i) {
      const auto& item = model.all_assets_.at(i);
      if (item.asset_id != QStringLiteral("asset-%1").arg(i) || item.adjustment_patch_json != recipe)
        return false;
    }
    return true;
  }
};
}
using namespace photo_editor::desktop;

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  const auto args = app.arguments();
  if (args.size() != 3) { std::cerr << "usage: catalog_snapshot_benchmark output.json count\n"; return 1; }
  bool valid_count = false;
  const int count = args.at(2).toInt(&valid_count);
  if (!valid_count || count < 1 || count > 100000) return 1;
  QTemporaryDir root;
  if (!root.isValid()) return 1;
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", root.filePath("data").toUtf8());
  qputenv("PHOTO_EDITOR_CACHE_DIR", root.filePath("cache").toUtf8());
  qputenv("PHOTO_EDITOR_DATABASE_URL", "postgresql://127.0.0.1:1/snapshot_fixture");
  QSettings::setDefaultFormat(QSettings::IniFormat);
  QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, root.path());
  QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, root.path());
  app.setOrganizationName("LooksyTests");
  app.setApplicationName("CatalogSnapshotBenchmark");

  EditRecipeState state;
  state.exposure = 0.25;
  const auto patch = serialize_adjustment_patch_json(state, all_adjustment_groups(), AdjustmentPatchScope::AssetBound);
  const auto recipe = QJsonDocument::fromJson(patch.toUtf8()).object();
  const auto expected = canonical_asset_bound_adjustment_patch_json(patch);
  QFile snapshot(root.filePath("snapshot.json"));
  if (!snapshot.open(QIODevice::WriteOnly) || snapshot.write("{\"assets\":[") < 0) return 1;
  for (int i = 0; i < count; ++i) {
    const QJsonObject asset{{"asset_id", QStringLiteral("asset-%1").arg(i)},
        {"capture_year", 2025}, {"capture_month", 6}, {"capture_day", i % 28 + 1},
        {"camera_make", "Fixture"}, {"camera_model", QStringLiteral("Camera %1").arg(i % 20)},
        {"imported_at", "2026-09-01T12:00:00Z"}, {"edit_recipe", recipe},
        {"location", QJsonObject{{"root_path", root.filePath("offline")},
            {"relative_path", QStringLiteral("photo-%1.jpg").arg(i)}, {"drive_id", "fixture"}, {"online", false}}}};
    const auto bytes = QJsonDocument(asset).toJson(QJsonDocument::Compact);
    if ((i > 0 && snapshot.write(",") != 1) || snapshot.write(bytes) != bytes.size()) return 1;
  }
  const QByteArray suffix("] ,\"drives\":[],\"projects\":[],\"review_states\":{},\"relinked_paths\":{}}\n");
  if (snapshot.write(suffix) != suffix.size()) return 1;
  const auto payload_bytes = snapshot.size();
  snapshot.close();

  CatalogAssetModel model;
  model.setInteractiveRenderingActive(true);
  QJsonArray samples;
  for (int sample = 0; sample < 3; ++sample) {
    QEventLoop loop;
    QElapsedTimer elapsed, heartbeat_elapsed;
    QTimer heartbeat, deadline;
    heartbeat.setInterval(5);
    deadline.setSingleShot(true);
    bool timed_out = false;
    qint64 max_gap = 0;
    int ticks = 0, resets = 0;
    QObject::connect(&heartbeat, &QTimer::timeout, [&] {
      max_gap = std::max(max_gap, heartbeat_elapsed.restart()); ++ticks;
    });
    QObject::connect(&deadline, &QTimer::timeout, [&] { timed_out = true; loop.quit(); });
    const auto loading_connection = QObject::connect(&model, &CatalogAssetModel::loadingChanged, &loop, [&] {
      if (!model.loading()) loop.quit();
    });
    const auto reset_connection = QObject::connect(&model, &QAbstractItemModel::modelReset, &loop, [&] { ++resets; });
    heartbeat_elapsed.start(); elapsed.start(); heartbeat.start(); deadline.start(120000);
    CatalogSnapshotBenchmarkAccess::fetch(model, snapshot.fileName());
    loop.exec();
    const auto ready_ms = static_cast<double>(elapsed.nsecsElapsed()) / 1e6;
    max_gap = std::max(max_gap, heartbeat_elapsed.elapsed());
    heartbeat.stop(); deadline.stop();
    QObject::disconnect(loading_connection); QObject::disconnect(reset_connection);
    if (timed_out || !model.errorMessage().isEmpty() || !CatalogSnapshotBenchmarkAccess::valid(model, count, expected)) {
      std::cerr << "Snapshot failed or changed IDs/recipes: " << model.errorMessage().toStdString() << '\n'; return 2;
    }
    samples.append(QJsonObject{{"ready_ms", ready_ms}, {"max_heartbeat_gap_ms", max_gap},
        {"heartbeat_ticks", ticks}, {"model_resets", resets}, {"count", model.rowCount()}});
    std::cout << count << " sample=" << sample << " ready_ms=" << ready_ms << " max_gap=" << max_gap << '\n' << std::flush;
  }
  struct rusage usage{};
  if (getrusage(RUSAGE_SELF, &usage) != 0) return 1;
  auto rss = static_cast<qint64>(usage.ru_maxrss);
#if !defined(__APPLE__)
  rss *= 1024;
#endif
  const QJsonObject report{{"count", count}, {"payload_bytes", payload_bytes}, {"peak_rss_bytes", rss},
      {"samples", samples}, {"exact_ids_and_recipes", true},
      {"scope", "Controller and cat transport; offline synthetic assets; scalar legacy recipes; no database or native frames"}};
  QFile output(args.at(1));
  const auto bytes = QJsonDocument(report).toJson();
  return output.open(QIODevice::WriteOnly) && output.write(bytes) == bytes.size() ? 0 : 1;
}
