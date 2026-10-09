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
#include <QProcess>
#include <QLockFile>
#include <QSaveFile>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <functional>
#include <iostream>
#include <memory>
#include <csignal>
#include <cerrno>

using namespace photo_editor::desktop;
namespace {
QByteArray read_file(const QString& path) {
  QFile input(path);
  return input.open(QIODevice::ReadOnly) ? input.readAll() : QByteArray{};
}
bool write_file(const QString& path, const QByteArray& bytes) {
  QSaveFile output(path);
  return output.open(QIODevice::WriteOnly) && output.write(bytes) == bytes.size() && output.commit();
}
bool wait_until(const std::function<bool()>& ready, int timeout_ms = 6000) {
  QElapsedTimer timer;
  timer.start();
  while (!ready() && timer.elapsed() < timeout_ms) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    QThread::msleep(1);
  }
  return ready();
}
void pump_events(int milliseconds) {
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < milliseconds) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    QThread::msleep(1);
  }
}
int fail(const char* message) { std::cerr << message << '\n'; return 1; }
int helper(const QStringList& arguments) {
  const QDir root(qEnvironmentVariable("PHOTO_EDITOR_MEMBERSHIP_FIXTURE_ROOT"));
  if (arguments.at(1) == "0") {
    std::cout << read_file(root.filePath("snapshot.json")).constData() << '\n';
    return 0;
  }
  if (arguments.size() != 3 || arguments.at(1) != "update-project-memberships") return 2;
  const auto payload = read_file(arguments.at(2));
  int call = 0;
  {
    QLockFile lock(root.filePath("counter.lock"));
    if (!lock.tryLock(2000)) return 2;
    call = read_file(root.filePath("counter")).toInt() + 1;
    if (QFileInfo::exists(root.filePath(QStringLiteral("ignore-termination-%1").arg(call))))
      std::signal(SIGTERM, SIG_IGN);
    if (!write_file(root.filePath(QStringLiteral("request-%1.json").arg(call)), payload) ||
        !write_file(root.filePath(QStringLiteral("pid-%1").arg(call)), QByteArray::number(QCoreApplication::applicationPid())) ||
        !write_file(root.filePath("counter"), QByteArray::number(call))) return 2;
  }
  QElapsedTimer timer;
  timer.start();
  while (!QFileInfo::exists(root.filePath(QStringLiteral("release-%1").arg(call)))) {
    if (!root.exists() || timer.elapsed() > 10000) return 3;
    QThread::msleep(2);
  }
  if (QFileInfo::exists(root.filePath(QStringLiteral("fail-%1").arg(call)))) {
    std::cerr << "Simulated membership helper failure\n";
    return 1;
  }
  const auto real_helper = qEnvironmentVariable("PHOTO_EDITOR_MEMBERSHIP_REAL_HELPER");
  if (!real_helper.isEmpty()) {
    QProcess process;
    process.start(real_helper, {"update-project-memberships", arguments.at(2)});
    if (!process.waitForFinished(10000)) { process.kill(); process.waitForFinished(1000); return 3; }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
      std::cerr << process.readAllStandardError().constData(); return 1;
    }
  }
  return 0;
}
}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  if (qEnvironmentVariableIsSet("PHOTO_EDITOR_MEMBERSHIP_FIXTURE_ROOT") && app.arguments().size() > 1)
    return helper(app.arguments());
  QCoreApplication::setOrganizationName("PhotoEditorTests");
  QCoreApplication::setApplicationName("CatalogProjectMembershipTest");
  QTemporaryDir temporary;
  if (!temporary.isValid()) return fail("Cannot create fixture directory");
  const QDir root(temporary.path());
  const QDir binaries(root.filePath("rust/target/debug"));
  if (!QDir().mkpath(binaries.absolutePath()) || !write_file(root.filePath("rust/Cargo.toml"), "# fixture\n"))
    return fail("Cannot create fixture repository");
  for (const auto& name : {QStringLiteral("catalog_state"), QStringLiteral("catalog_snapshot")})
    if (!QFile::link(app.applicationFilePath(), binaries.filePath(name))) return fail("Cannot install helper fixture");
  qputenv("PHOTO_EDITOR_REPOSITORY_ROOT", root.absolutePath().toUtf8());
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", root.filePath("data").toUtf8());
  qputenv("PHOTO_EDITOR_CACHE_DIR", root.filePath("cache").toUtf8());
  qputenv("PHOTO_EDITOR_DATABASE_URL", "postgresql://fixture.invalid/unused");
  qputenv("PHOTO_EDITOR_MEMBERSHIP_FIXTURE_ROOT", root.absolutePath().toUtf8());
  // Fail before model creation if installed helpers would override the fixture.
  for (const auto& name : {QStringLiteral("catalog_state"), QStringLiteral("catalog_snapshot")})
    if (AppBootstrap::helperProgram(name) != binaries.filePath(name)) return fail("Helper fixture is not authoritative");
  const auto count = [&]() { return read_file(root.filePath("counter")).toInt(); };
  const auto release = [&](int call) { return write_file(root.filePath(QStringLiteral("release-%1").arg(call)), "go"); };
  const auto request = [&](int call) { return QJsonDocument::fromJson(read_file(root.filePath(QStringLiteral("request-%1.json").arg(call)))).object(); };
  const auto outbox_path = root.filePath("data/project-membership-outbox.json");
  const auto pending = [&]() { return QJsonDocument::fromJson(read_file(outbox_path)).object().value("projects").toObject(); };
  QVector<CatalogAssetItem> items;
  QJsonArray assets;
  for (int index = 0; index < 1000; ++index) {
    const auto id = QStringLiteral("asset-%1").arg(index);
    CatalogAssetItem item;
    item.asset_id = id;
    item.file_path = root.filePath(id + ".jpg");
    item.camera_label = "Fixture";
    items.append(item);
    assets.append(QJsonObject{{"asset_id", id}, {"camera_model", "Fixture"},
        {"location", QJsonObject{{"root_path", root.absolutePath()}, {"relative_path", id + ".jpg"}, {"online", false}}}});
  }
  const auto snapshot = [&](int first_member) {
    QJsonArray projects;
    if (first_member >= 0) {
      QJsonArray ids;
      for (int index = first_member; index < 1000; ++index) ids.append(items.at(index).asset_id);
      projects.append(QJsonObject{{"name", "Batch"}, {"asset_ids", ids}});
    }
    return write_file(root.filePath("snapshot.json"), QJsonDocument(QJsonObject{
        {"assets", assets}, {"projects", projects}, {"drives", QJsonArray{}},
        {"review_states", QJsonObject{}}, {"relinked_paths", QJsonObject{}}}).toJson(QJsonDocument::Compact));
  };
  auto model = std::make_unique<CatalogAssetModel>();
  model->setInteractiveRenderingActive(true);
  model->replaceAssetsForTesting(items);
  // Release held fixtures on an assertion failure; never leave a real helper.
  const auto cleanup = qScopeGuard([&]() { for (int call = 1; call < 16; ++call) release(call); });
  int resets = 0, ticks = 0;
  QObject::connect(model.get(), &QAbstractItemModel::modelReset, [&]() { ++resets; });
  QTimer heartbeat;
  heartbeat.setInterval(5);
  QObject::connect(&heartbeat, &QTimer::timeout, [&]() { ++ticks; });
  heartbeat.start();
  model->selectAll();
  QElapsedTimer operation;
  operation.start();
  model->createProjectWithSelectedAssets("  Batch  ");
  const auto batch_ms = operation.elapsed();
  if (batch_ms > 1000 || resets != 1 || model->assetCountForProject("Batch") != 1000 ||
      pending().value("Batch").toObject().size() != 1000 || !wait_until([&]() { return count() == 1; }))
    return fail("Batch operation blocked, reset repeatedly, or lost selection/intent");
  if (request(1).value("added").toArray().size() != 1000 || !request(1).value("removed").toArray().isEmpty())
    return fail("Selection was not sent in one batch");
  const auto previous_ticks = ticks;
  pump_events(100);
  if (count() != 1 || ticks - previous_ticks < 5) return fail("Blocked persistence stalled the model thread or spawned more helpers");
  model->removeAssetFromProject("asset-0", "Batch");
  model->addAssetToProject("asset-0", "Batch");
  model->removeAssetFromProject("asset-0", "Batch");
  if (model->assetInProject("asset-0", "Batch") || pending().value("Batch").toObject().value("asset-0") != false || count() != 1)
    return fail("Newest membership intent was not retained while the helper was active");
  if (!snapshot(-1)) return fail("Cannot write stale snapshot fixture");
  model->refresh();
  if (!wait_until([&]() { return !model->loading(); }) || model->assetInProject("asset-0", "Batch") ||
      model->assetCountForProject("Batch") != 999 || pending().isEmpty()) return fail("Stale snapshot replaced pending memberships");
  if (!release(1) || !wait_until([&]() { return count() == 2; })) return fail("Coalesced follow-up batch did not start");
  if (request(2).value("added").toArray().size() != 999 || request(2).value("removed").toArray() != QJsonArray{"asset-0"})
    return fail("Follow-up batch did not contain latest intent");
  if (!release(2) || !wait_until([&]() { return model->status().startsWith("Saved project"); }) || !snapshot(1))
    return fail("Follow-up batch did not complete");
  model->refresh();
  if (!wait_until([&]() { return !model->loading(); }) || !pending().isEmpty()) return fail("Confirmed snapshot did not retire the journal");

  // Lookup is case-insensitive; persistence must use the existing project name.
  model->removeAssetFromProject("asset-1", "bAtCh");
  if (!wait_until([&]() { return count() == 3; }) || request(3).value("project_name") != "Batch")
    return fail("Mixed-case lookup changed the persistent project identity");
  if (!write_file(root.filePath("fail-3"), "fail") || !release(3) ||
      !wait_until([&]() { return model->status().contains("saved locally"); })) return fail("Failed helper did not surface local recovery");
  pump_events(100);
  if (count() != 3 || pending().isEmpty() || model->assetInProject("asset-1", "Batch")) return fail("Failure retried indefinitely or discarded the edit");
  model->refresh();
  if (!wait_until([&]() { return count() == 4 && !model->loading(); }) || !release(4) ||
      !wait_until([&]() { return model->status().startsWith("Saved project"); }) || !snapshot(2))
    return fail("Explicit refresh did not retry the failed revision");
  model->refresh();
  if (!wait_until([&]() { return !model->loading(); }) || !pending().isEmpty()) return fail("Retried membership did not reconcile");

  model->removeAssetFromProject("asset-2", "Batch");
  if (!wait_until([&]() { return count() == 5; }) || !write_file(root.filePath("fail-5"), "fail") || !release(5) ||
      !wait_until([&]() { return model->status().contains("saved locally"); })) return fail("Cannot prepare restart recovery");
  model.reset();
  model = std::make_unique<CatalogAssetModel>();
  model->setInteractiveRenderingActive(true);
  model->replaceAssetsForTesting(items);
  if (!wait_until([&]() { return count() == 6; }) || request(6).value("removed").toArray() != QJsonArray{"asset-2"} ||
      !release(6) || !wait_until([&]() { return model->status().startsWith("Saved project"); }) || !snapshot(3))
    return fail("Restart did not replay the exact durable membership intent");
  model->refresh();
  if (!wait_until([&]() { return !model->loading(); }) || !pending().isEmpty() ||
      model->assetInProject("asset-2", "Batch") || model->assetCountForProject("Batch") != 997)
    return fail("Recovered membership did not reconcile with confirmed state");
  model.reset();
  // Destroy a model while its owned save helper is blocked. The durable intent
  // must survive, and the worker must not hold process shutdown until timeout.
  model = std::make_unique<CatalogAssetModel>();
  model->setInteractiveRenderingActive(true);
  model->replaceAssetsForTesting(items);
  if (!write_file(root.filePath("ignore-termination-7"), "yes")) return fail("Cannot prepare stubborn helper");
  model->addAssetToProject("asset-10", "Shutdown");
  if (!wait_until([&]() { return count() == 7; })) return fail("Shutdown helper did not start");
  const auto helper_pid = static_cast<pid_t>(read_file(root.filePath("pid-7")).toLongLong());
  if (helper_pid <= 0) return fail("Shutdown helper PID was not recorded");
  const auto shutdown_intent = read_file(outbox_path);
  QElapsedTimer shutdown_timer; shutdown_timer.start();
  model.reset();
  const auto destruction_ms = shutdown_timer.elapsed();
  if (destruction_ms > 100 || read_file(outbox_path) != shutdown_intent)
    return fail("Model destruction blocked or changed the pending membership intent");
  if (!wait_until([&]() { return ::kill(helper_pid, 0) == -1 && errno == ESRCH; }, 2000))
    return fail("Membership helper survived model destruction beyond the shutdown budget");
  const auto helper_exit_ms = shutdown_timer.elapsed();
  model = std::make_unique<CatalogAssetModel>();
  model->setInteractiveRenderingActive(true);
  model->replaceAssetsForTesting(items);
  if (!wait_until([&]() { return count() == 8; }) || request(8) != request(7) ||
      !model->assetInProject("asset-10", "Shutdown") || !release(8) ||
      !wait_until([&]() { return model->status().startsWith("Saved project"); }))
    return fail("Shutdown recovery did not replay the exact durable membership intent");
  std::cout << "Active-save destruction " << destruction_ms << " ms; SIGTERM-ignoring helper gone in "
            << helper_exit_ms << " ms; exact intent replayed.\n";
  model.reset();
  const QByteArray corrupt("{broken-membership-outbox");
  if (!write_file(outbox_path, corrupt)) return fail("Cannot prepare corrupt fixture");
  model = std::make_unique<CatalogAssetModel>();
  model->replaceAssetsForTesting(items);
  model->addAssetToProject("asset-0", "Blocked");
  pump_events(100);
  if (model->errorMessage().isEmpty() || read_file(outbox_path) != corrupt || count() != 8 ||
      model->assetInProject("asset-0", "Blocked")) return fail("Corrupt recovery was overwritten or ignored");
  std::cout << "1000-asset batch returned in " << batch_ms << " ms with one reset and one initial helper; heartbeat, latest intent, stale snapshot, case identity, failure/retry, restart replay, and corrupt recovery checks passed.\n";
  return 0;
}
