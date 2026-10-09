#include "app_bootstrap.h"
#include "catalog_asset_model.h"
#include "edit_adjustment_patch.h"
#include "edit_recipe_state.h"
#include "edit_recipe_v13_persistence.h"
#include "catalog_recipe_projection.h"
#include "photo_editor/pipeline/cpu_work_scheduler.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSettings>
#include <QTemporaryDir>
#include <QThread>
#include <functional>
#include <iostream>
#include <memory>
#include <atomic>
#include <algorithm>

namespace photo_editor::desktop {
struct CatalogSnapshotContractAccess {
  static void isolate(CatalogAssetModel& model) { model.timeline_asset_loading_suppressed_for_testing_ = true; }
  static bool parsing(const CatalogAssetModel& model) { return model.snapshot_parse_active_; }
  static auto future(const CatalogAssetModel& model) { return model.snapshot_parse_watcher_.future(); }
  static auto items(const CatalogAssetModel& model) { return model.all_assets_; }
};
}
using namespace photo_editor::desktop;
namespace {
bool write(const QString& path, const QByteArray& bytes) {
  QSaveFile file(path);
  return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
}
QByteArray read(const QString& path) {
  QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}
bool wait(const std::function<bool()>& ready) {
  QElapsedTimer timer; timer.start();
  while (!ready() && timer.elapsed() < 15000) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 5); QThread::msleep(1);
  }
  return ready();
}
int fail(const char* message) { std::cerr << message << '\n'; return 1; }
QByteArray snapshot(int count, const QString& prefix, QStringList* expected = nullptr) {
  QJsonArray assets;
  for (int i = 0; i < count; ++i) {
    EditRecipeState state;
    state.exposure = static_cast<double>(i % 31) / 10.0 - 1.5;
    state.contrast = static_cast<double>(i % 17) / 100.0;
    const auto patch = serialize_adjustment_patch_json(state, all_adjustment_groups(), AdjustmentPatchScope::AssetBound);
    QJsonObject asset{{"asset_id", prefix + QString::number(i)},
        {"edit_recipe", QJsonDocument::fromJson(patch.toUtf8()).object()},
        {"location", QJsonObject{{"root_path", "/isolated/offline"},
            {"relative_path", QStringLiteral("%1.jpg").arg(i)}, {"online", false}}}};
    if (expected && i % 2 == 1) {
      DesktopRecipeV13Context context;
      context.recipe_id = "recipe:" + prefix + QString::number(i);
      context.source_id = "asset:" + prefix + QString::number(i);
      context.decoder_provenance = "raster:test-v1";
      const auto saved = serialize_desktop_recipe_as_canonical_v13(state, context);
      if (!saved.ok()) return {};
      asset.insert("canonical_edit_recipe_v13", QJsonObject{
          {"recipe_payload", saved.document.recipe_payload}, {"cache_key", saved.document.cache_key},
          {"provenance_payload", saved.document.provenance_payload}});
    }
    if (expected) {
      const auto projected = project_catalog_adjustments_for_desktop(asset.value("edit_recipe"),
          asset.value("canonical_edit_recipe_v13"), QDir(AppBootstrap::appDataLocation()).filePath("recipe-assets"));
      if (!projected.ok()) return {};
      expected->append(projected.adjustment_patch_json);
    }
    assets.append(asset);
  }
  return QJsonDocument(QJsonObject{{"assets", assets}}).toJson(QJsonDocument::Compact);
}
}

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  if (app.arguments().size() > 1 && qEnvironmentVariableIsSet("LOOKSY_SNAPSHOT_CONTRACT_ROOT")) {
    const QDir root(qEnvironmentVariable("LOOKSY_SNAPSHOT_CONTRACT_ROOT"));
    const auto calls = read(root.filePath("calls")).toInt() + 1;
    if (!write(root.filePath("calls"), QByteArray::number(calls))) return 2;
    std::cout << read(root.filePath("snapshot.json")).constData() << std::flush;
    return root.exists("fail") ? 1 : 0;
  }
  QTemporaryDir temporary;
  if (!temporary.isValid()) return 1;
  const QDir root(temporary.path());
  const QDir binaries(root.filePath("rust/target/debug"));
  if (!QDir().mkpath(binaries.path()) || !write(root.filePath("rust/Cargo.toml"), "# isolated fixture\n") ||
      !QFile::link(app.applicationFilePath(), binaries.filePath("catalog_snapshot"))) return 1;
  qputenv("LOOKSY_SNAPSHOT_CONTRACT_ROOT", root.path().toUtf8());
  qputenv("PHOTO_EDITOR_REPOSITORY_ROOT", root.path().toUtf8());
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", root.filePath("data").toUtf8());
  qputenv("PHOTO_EDITOR_CACHE_DIR", root.filePath("cache").toUtf8());
  qputenv("PHOTO_EDITOR_DATABASE_URL", "postgresql://127.0.0.1:1/snapshot_fixture");
  QSettings::setDefaultFormat(QSettings::IniFormat);
  QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, root.path());
  QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, root.path());
  app.setOrganizationName("LooksyTests"); app.setApplicationName("SnapshotContracts");
  if (AppBootstrap::helperProgram("catalog_snapshot") != binaries.filePath("catalog_snapshot"))
    return fail("Fixture helper resolution failed; refusing other helpers");
  auto model = std::make_unique<CatalogAssetModel>();
  CatalogSnapshotContractAccess::isolate(*model);
  model->setInteractiveRenderingActive(true);
  QStringList expected;
  const auto small = snapshot(257, "current-", &expected);
  const auto large = snapshot(10000, "obsolete-");
  if (small.isEmpty() || large.isEmpty() || expected.size() != 257) return fail("Cannot build recipe fixture");
  const auto path = root.filePath("snapshot.json");
  if (!write(path, small)) return 1;
  model->refresh();
  if (!wait([&] { return !model->loading(); }) || !model->errorMessage().isEmpty()) return fail("Initial snapshot failed");
  const auto exact = [&] {
    const auto items = CatalogSnapshotContractAccess::items(*model);
    if (items.size() != expected.size()) return false;
    for (qsizetype i = 0; i < items.size(); ++i)
      if (items.at(i).asset_id != "current-" + QString::number(i) || items.at(i).adjustment_patch_json != expected.at(i))
        return false;
    return true;
  };
  if (!exact()) return fail("Parallel projection changed order or recipe fields");
  if (!write(path, large)) return 1;
  model->refresh();
  if (!wait([&] { return CatalogSnapshotContractAccess::parsing(*model); })) return fail("Parse stage never started");
  const auto obsolete = CatalogSnapshotContractAccess::future(*model);
  if (obsolete.isFinished()) return fail("Contention fixture finished before the probe");
  // Probe throughout decoding and recipe preparation, without dispatching the
  // publication event. A single early probe could miss the shared-pool phase.
  qint64 probe_ms = 0;
  int probes = 0;
  QElapsedTimer probe_deadline; probe_deadline.start();
  while (!obsolete.isFinished() && probe_deadline.elapsed() < 10000) {
    std::atomic_int probe_items{0};
    QElapsedTimer probe; probe.start();
    photo_editor::pipeline::run_cpu_parallel_for(16, [&](int) { probe_items.fetch_add(1); });
    probe_ms = std::max(probe_ms, probe.elapsed());
    if (probe_items != 16) return fail("Shared CPU probe lost work");
    ++probes;
    QThread::msleep(10);
  }
  std::cout << "shared_pool_probe_ms=" << probe_ms << '\n' << std::flush;
  if (probes == 0 || probe_ms > 100 || !obsolete.isFinished()) return fail("Catalog preparation starved shared CPU work");
  if (!write(path, small)) return 1;
  for (int i = 0; i < 5; ++i) model->refresh();
  if (!wait([&] { return !model->loading(); }) || !obsolete.isFinished() || !exact() ||
      read(root.filePath("calls")).toInt() != 3) return fail("Refresh did not coalesce or stale projection published");
  // Also supersede an in-flight worker, before waiting for its completion.
  if (!write(path, large)) return 1;
  model->refresh();
  if (!wait([&] { return CatalogSnapshotContractAccess::parsing(*model); })) return fail("Cancellation parse never started");
  const auto cancelled = CatalogSnapshotContractAccess::future(*model);
  if (!write(path, small)) return 1;
  for (int i = 0; i < 5; ++i) model->refresh();
  if (!wait([&] { return !model->loading(); }) || !cancelled.isFinished() || !exact() ||
      read(root.filePath("calls")).toInt() != 5) return fail("Active cancellation did not coalesce safely");
  for (const bool helper_failure : {false, true}) {
    if (!write(path, "{not valid JSON")) return 1;
    if (helper_failure && !write(root.filePath("fail"), "yes")) return 1;
    model->refresh();
    if (!wait([&] { return !model->loading(); }) || model->errorMessage().isEmpty() || !exact())
      return fail("Malformed or failed snapshot replaced accepted catalog");
  }
  QFile::remove(root.filePath("fail"));
  if (!write(path, large)) return 1;
  model->refresh();
  if (!wait([&] { return CatalogSnapshotContractAccess::parsing(*model); })) return fail("Shutdown parse never started");
  const auto pending = CatalogSnapshotContractAccess::future(*model);
  QElapsedTimer destruction; destruction.start();
  model.reset();
  const auto destruction_ms = destruction.elapsed();
  if (destruction_ms > 100 || !wait([&] { return pending.isFinished(); })) return fail("Shutdown blocked or abandoned projection");
  std::cout << "Exact mixed recipes/order, coalesced refresh, stale rejection, malformed/failed snapshot retention, and active-parse shutdown passed; destruction_ms="
            << destruction_ms << '\n';
  return 0;
}
