#include "app_bootstrap.h"
#include "catalog_asset_model.h"
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <functional>
#include <iostream>
#include <memory>

using namespace photo_editor::desktop;
namespace {
QByteArray read_file(const QString& path) {
  QFile file(path);
  return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}
bool write_file(const QString& path, const QByteArray& bytes) {
  QSaveFile file(path);
  return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
}
QJsonObject read_object(const QString& path) { return QJsonDocument::fromJson(read_file(path)).object(); }
bool wait_until(const std::function<bool()>& ready, int timeout = 180000) {
  QElapsedTimer elapsed;
  elapsed.start();
  while (!ready() && elapsed.elapsed() < timeout) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    QThread::msleep(1);
  }
  return ready();
}
int helper(const QStringList& args) {
  const QDir root(qEnvironmentVariable("PHOTO_EDITOR_CULLING_FIXTURE_ROOT"));
  if (args.value(1) == "0") {
    std::cout << read_file(root.filePath("snapshot.json")).constData();
    return 0;
  }
  QJsonArray states;
  if (args.value(1) == "set-review-state" && args.size() >= 7) {
    states.append(QJsonObject{{"asset_id", args[2]}, {"rating", args[3].toInt()},
        {"picked", args[4] == "true"}, {"rejected", args[5] == "true"}, {"color_label", args[6]}});
  } else if (args.value(1) == "set-review-states" && args.size() == 3) {
    states = read_object(args[2]).value("states").toArray();
    if (states.isEmpty() || states.size() > 256) return 2;
  } else return 2;
  const int call = read_file(root.filePath("calls")).toInt() + 1;
  if (!write_file(root.filePath(QStringLiteral("request-%1.json").arg(call)), QJsonDocument(states).toJson()) ||
      !write_file(root.filePath("calls"), QByteArray::number(call))) return 2;
  if (QFileInfo::exists(root.filePath("hold")) &&
      !wait_until([&] { return QFileInfo::exists(root.filePath(QStringLiteral("release-%1").arg(call))); }, 15000)) return 3;
  if (QFileInfo::exists(root.filePath("fail"))) return 4;
  auto store = read_object(root.filePath("store.json"));
  QJsonArray applied, missing;
  for (const auto& value : states) {
    const auto row = value.toObject();
    const auto id = row.value("asset_id").toString();
    if (id == qEnvironmentVariable("PHOTO_EDITOR_CULLING_MISSING_ID")) missing.append(id);
    else { store.insert(id, row); applied.append(id); }
  }
  if (!write_file(root.filePath("store.json"), QJsonDocument(store).toJson(QJsonDocument::Compact))) return 2;
  if (QFileInfo::exists(root.filePath("bad-receipt"))) { std::cout << "not a receipt"; return 0; }
  std::cout << QJsonDocument(QJsonObject{{"applied", applied}, {"missing", missing}}).toJson(QJsonDocument::Compact).constData();
  return 0;
}
int fail(const char* message) { std::cerr << message << '\n'; return 1; }
}

int contracts(const QDir& root, const QVector<CatalogAssetItem>& items) {
  const auto calls = [&] { return read_file(root.filePath("calls")).toInt(); };
  const auto pending_path = root.filePath("data/culling-outbox.json");
  const auto pending = [&] { return read_object(pending_path).value("states").toObject(); };
  const auto release = [&](int n) { return write_file(root.filePath(QStringLiteral("release-%1").arg(n)), "go"); };
  const auto pump = [&](int ms) { QElapsedTimer t; t.start(); wait_until([&] { return t.elapsed() >= ms; }, ms + 1000); };
  QJsonArray assets;
  for (const auto& item : items) assets.append(QJsonObject{{"asset_id", item.asset_id},
      {"location", QJsonObject{{"root_path", root.absolutePath()}, {"relative_path", item.asset_id + ".jpg"}, {"online", false}}}});
  write_file(root.filePath("snapshot.json"), QJsonDocument(QJsonObject{{"assets", assets},
      {"projects", QJsonArray{}}, {"review_states", QJsonObject{}}, {"relinked_paths", QJsonObject{}}}).toJson());
  const auto make_model = [&] {
    auto model = std::make_unique<CatalogAssetModel>();
    model->setInteractiveRenderingActive(true);
    model->replaceAssetsForTesting(items);
    return model;
  };
  auto model = make_model();
  const auto rate = [&](int index, int value) { model->selectAssetById(items[index].asset_id); model->setSelectedRating(value); };
  const auto saved = [&](int index) { return read_object(root.filePath("store.json")).value(items[index].asset_id).toObject(); };
  if (!write_file(root.filePath("hold"), "hold")) return 1;
  rate(0, 1); rate(1, 2); model->toggleSelectedPick(); model->setSelectedColorLabel("red"); rate(2, 3); model->toggleSelectedReject();
  if (!wait_until([&] { return calls() == 1; }, 5000) || pending().size() != 3 ||
      QJsonDocument::fromJson(read_file(root.filePath("request-1.json"))).array().size() != 3) return fail("Burst not checkpointed and batched");
  int ticks = 0;
  QTimer heartbeat;
  heartbeat.setInterval(5);
  QObject::connect(&heartbeat, &QTimer::timeout, [&] { ++ticks; });
  heartbeat.start(); pump(100);
  if (ticks < 10 || calls() != 1) return fail("Held helper blocked or overlapped");
  rate(0, 5); model->clearSelectedCullState();
  pump(40);
  if (pending().value("asset-0").toObject().value("rating") != 0) return fail("Latest clear was not checkpointed");
  release(1);
  if (!wait_until([&] { return calls() == 2; }, 5000) || pending().size() != 1) return fail("Old completion retired newer intent");
  model->refresh();
  if (!wait_until([&] { return !model->loading(); }, 5000) || model->selectedRating() != 0) return fail("Stale snapshot replaced pending clear");
  release(2);
  if (!wait_until([&] { return pending().isEmpty(); }, 5000) || saved(0).value("rating") != 0 ||
      saved(1).value("picked") != true || saved(1).value("color_label") != "red" || saved(2).value("rejected") != true)
    return fail("Batch lost fields or acknowledgement");

  write_file(root.filePath("fail"), "fail"); rate(0, 4);
  if (!wait_until([&] { return calls() == 3; }, 5000)) return fail("Failure fixture did not start");
  release(3); pump(150);
  if (pending().size() != 1 || model->errorMessage().isEmpty() || calls() != 3) return fail("Failed batch was discarded or retried forever");
  QFile::remove(root.filePath("fail")); model->refresh();
  if (!wait_until([&] { return calls() == 4 && !model->loading(); }, 5000) || model->selectedRating() != 4) return fail("Retry/stale snapshot lost pending rating");
  release(4);
  if (!wait_until([&] { return pending().isEmpty(); }, 5000) || saved(0).value("rating") != 4) return fail("Retry failed");

  rate(0, 5);
  if (!wait_until([&] { return calls() == 5; }, 5000)) return fail("Restart fixture did not start");
  model.reset();
  if (pending().size() != 1) return fail("Shutdown lost pending intent");
  QFile::remove(root.filePath("hold"));
  model = make_model();
  if (!wait_until([&] { return calls() == 6 && pending().isEmpty(); }, 5000) || saved(0).value("rating") != 5) return fail("Restart did not replay exact intent");

  qputenv("PHOTO_EDITOR_CULLING_MISSING_ID", "asset-0");
  rate(0, 2); rate(1, 5);
  if (!wait_until([&] { return calls() == 7 && pending().size() == 1; }, 5000) ||
      !pending().contains("asset-0") || saved(1).value("rating") != 5) return fail("Missing asset blocked valid records or lost recovery");
  pump(100); if (calls() != 7) return fail("Missing asset retried indefinitely");
  qunsetenv("PHOTO_EDITOR_CULLING_MISSING_ID"); model->refresh();
  if (!wait_until([&] { return calls() == 8 && pending().isEmpty(); }, 5000) || saved(0).value("rating") != 2) return fail("Missing record did not retry");

  write_file(root.filePath("bad-receipt"), "bad"); rate(0, 3);
  if (!wait_until([&] { return calls() == 9; }, 5000)) return 1;
  pump(100); if (pending().size() != 1) return fail("Malformed receipt acknowledged intent");
  QFile::remove(root.filePath("bad-receipt")); model->refresh();
  if (!wait_until([&] { return calls() == 10 && pending().isEmpty(); }, 5000)) return fail("Malformed receipt recovery failed");

  const auto helper_path = root.filePath("rust/target/debug/catalog_state");
  if (!QFile::rename(helper_path, helper_path + ".held")) return 1;
  if (!write_file(helper_path, "invalid executable") || !QFile::setPermissions(helper_path, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner)) return 1;
  rate(0, 4); pump(200);
  if (calls() != 10 || pending().size() != 1) return fail("FailedToStart lost pending intent");
  QFile::remove(helper_path);
  if (!QFile::rename(helper_path + ".held", helper_path)) return 1;
  model->refresh();
  if (!wait_until([&] { return calls() == 11 && pending().isEmpty(); }, 5000)) return fail("FailedToStart retry failed");

  // A failed atomic checkpoint must prevent dispatch and retain the new state
  // for explicit retry. All paths here belong to this temporary fixture.
  if (!QFile::rename(pending_path, pending_path + ".saved") || !QDir().mkdir(pending_path)) return 1;
  rate(0, 5); pump(150);
  if (calls() != 11 || model->errorMessage().isEmpty()) return fail("Checkpoint failure dispatched unjournaled work");
  if (!QDir().rmdir(pending_path) || !QFile::rename(pending_path + ".saved", pending_path)) return 1;
  model->refresh();
  if (!wait_until([&] { return calls() == 12 && pending().isEmpty(); }, 5000) || saved(0).value("rating") != 5)
    return fail("Checkpoint failure lost the newest intent on retry");
  // Shutdown flushes the coalescing window even before the first helper starts.
  rate(0, 1); model.reset();
  if (calls() != 12 || pending().value("asset-0").toObject().value("rating") != 1)
    return fail("Shutdown did not checkpoint the coalescing window");
  model = make_model();
  if (!wait_until([&] { return calls() == 13 && pending().isEmpty(); }, 5000) || saved(0).value("rating") != 1)
    return fail("Unstarted batch was not replayed after restart");

  model.reset();
  const QByteArray corrupt("{broken-culling-outbox");
  write_file(pending_path, corrupt);
  model = make_model(); rate(0, 1); pump(100);
  if (read_file(pending_path) != corrupt || calls() != 13) return fail("Unreadable recovery was overwritten or dispatched");
  model.reset();
  if (read_file(pending_path) != corrupt) return fail("Shutdown overwrote unreadable recovery");
  std::cout << "Culling contracts passed: burst, bounded single helper, fields, clear/latest intent, heartbeat, stale snapshot, failure/retry, restart, partial missing, receipt validation, failed start, failed checkpoint, coalescing-window shutdown, corrupt recovery.\n";
  return 0;
}

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  if (qEnvironmentVariableIsSet("PHOTO_EDITOR_CULLING_FIXTURE_ROOT") && app.arguments().size() > 1)
    return helper(app.arguments());
  QCoreApplication::setOrganizationName("PhotoEditorTests");
  QCoreApplication::setApplicationName("CatalogCullingPersistenceTest");
  QTemporaryDir temporary;
  if (!temporary.isValid()) return fail("Cannot create fixture");
  const QDir root(temporary.path()), binaries(root.filePath("rust/target/debug"));
  if (!QDir().mkpath(binaries.absolutePath()) || !write_file(root.filePath("rust/Cargo.toml"), "# fixture")) return 1;
  for (const auto& name : {QStringLiteral("catalog_state"), QStringLiteral("catalog_snapshot")})
    if (!QFile::link(app.applicationFilePath(), binaries.filePath(name))) return 1;
  qputenv("PHOTO_EDITOR_REPOSITORY_ROOT", root.absolutePath().toUtf8());
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", root.filePath("data").toUtf8());
  qputenv("PHOTO_EDITOR_CACHE_DIR", root.filePath("cache").toUtf8());
  qputenv("PHOTO_EDITOR_DATABASE_URL", "postgresql://fixture.invalid/unused");
  qputenv("PHOTO_EDITOR_CULLING_FIXTURE_ROOT", root.absolutePath().toUtf8());
  if (AppBootstrap::helperProgram("catalog_state") != binaries.filePath("catalog_state")) return fail("Unsafe helper resolution");
  const int count = app.arguments().size() > 2 ? app.arguments()[2].toInt() : 1000;
  QVector<CatalogAssetItem> items;
  for (int i = 0; i < count; ++i) {
    CatalogAssetItem item;
    item.asset_id = QStringLiteral("asset-%1").arg(i);
    item.file_path = root.filePath(item.asset_id + ".jpg");
    item.camera_label = "Fixture";
    items.append(item);
  }
  if (app.arguments().value(1) == "--contracts") return contracts(root, items);
  CatalogAssetModel model;
  model.setInteractiveRenderingActive(true);
  model.replaceAssetsForTesting(items);
  QElapsedTimer elapsed;
  elapsed.start();
  qint64 last_tick = 0, max_gap = 0;
  QTimer heartbeat;
  heartbeat.setInterval(5);
  QObject::connect(&heartbeat, &QTimer::timeout, [&] {
    const auto now = elapsed.elapsed();
    max_gap = qMax(max_gap, now - last_tick);
    last_tick = now;
  });
  heartbeat.start();
  for (int i = 0; i < count; ++i) {
    model.selectAssetById(items[i].asset_id);
    model.setSelectedRating(i % 5 + 1);
  }
  const auto enqueue_ms = elapsed.elapsed();
  if (!wait_until([&] { return read_object(root.filePath("store.json")).size() == count; })) return fail("Culling queue did not drain");
  const auto drain_ms = elapsed.elapsed();
  const auto store = read_object(root.filePath("store.json"));
  for (int i = 0; i < count; ++i)
    if (store.value(items[i].asset_id).toObject().value("rating").toInt() != i % 5 + 1) return fail("Lost rating");
  const QJsonObject report{{"count", count}, {"helper_launches", read_file(root.filePath("calls")).toInt()},
      {"enqueue_ms", enqueue_ms}, {"drain_ms", drain_ms}, {"heartbeat_max_gap_ms", max_gap}, {"exact", true},
      {"scope", "Actual Qt culling queue with fixture helper and atomic JSON store; no real database"}};
  if (argc > 1 && !write_file(app.arguments()[1], QJsonDocument(report).toJson())) return 1;
  std::cout << QJsonDocument(report).toJson().constData();
  return 0;
}
