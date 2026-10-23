#include "catalog_asset_model.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QSemaphore>
#include <QTemporaryDir>
#include <QThread>
#include <QThreadPool>
#include <QTimer>
#include <functional>
#include <iostream>
#include <memory>

namespace photo_editor::desktop {
struct CatalogImportPreflightTestAccess {
  static QThreadPool& pool() { return CatalogAssetModel::importPreflightThreadPool(); }
  static bool active(const CatalogAssetModel& model) { return bool(model.active_import_preflight_); }
  static bool importJobPollInFlight(const CatalogAssetModel& model) {
    return model.import_job_poll_in_flight_;
  }
  static bool importJobPollDirty(const CatalogAssetModel& model) {
    return model.import_job_poll_dirty_;
  }
  static void requestImportJobPoll(CatalogAssetModel& model) {
    model.pollActiveImportJob();
  }
};
}
using namespace photo_editor::desktop;
namespace {
bool wait_until(const std::function<bool()>& ready) {
  QElapsedTimer timer;
  timer.start();
  while (!ready() && timer.elapsed() < 5000) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    QThread::msleep(1);
  }
  return ready();
}
bool write_file(const QString& path) {
  QFile file(path);
  return file.open(QIODevice::WriteOnly) && file.write("fixture", 7) == 7;
}
bool write_bytes(const QString& path, const QByteArray& bytes) {
  QFile file(path);
  return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
int fail(const char* message) { std::cerr << message << '\n'; return 1; }
bool expect(bool condition, const char* message) {
  if (condition) return true;
  std::cerr << message << '\n';
  return false;
}
struct PoolGate {
  QSemaphore started;
  QSemaphore release;
  PoolGate() {
    CatalogImportPreflightTestAccess::pool().start([this] { started.release(); release.acquire(); });
    started.acquire();
  }
  ~PoolGate() {
    release.release();
    CatalogImportPreflightTestAccess::pool().waitForDone();
  }
};
}
int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  app.setOrganizationName("PhotoEditorTests");
  app.setApplicationName("CatalogImportPreflightTest");
  QTemporaryDir temporary;
  if (!temporary.isValid()) return fail("Temporary directory unavailable");
  const QDir root(temporary.path());
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", root.filePath("data").toUtf8());
  qputenv("PHOTO_EDITOR_CACHE_DIR", root.filePath("cache").toUtf8());
  qputenv("PHOTO_EDITOR_MANAGED_MEDIA_DIR", root.filePath("library").toUtf8());
  qputenv("PHOTO_EDITOR_ORIGINALS_BACKUP_DIR", root.filePath("backup").toUtf8());
  qputenv("PHOTO_EDITOR_DATABASE_URL", "postgresql://127.0.0.1:1/preflight_fixture");
#ifndef Q_OS_WIN
  const auto helper_root = root.filePath("fake-helper-root");
  const auto helper_directory = QDir(helper_root).filePath("rust/target/debug");
  const auto jobs_directory = root.filePath("data/jobs");
  const auto helper_path = QDir(helper_directory).filePath("catalog_export_jobs");
  const auto legacy_path = QDir(jobs_directory).filePath("index.json");
  const auto helper_calls_path = root.filePath("data/helper-calls");
  if (!QDir().mkpath(helper_directory) || !QDir().mkpath(jobs_directory) ||
      !write_bytes(QDir(helper_root).filePath("rust/Cargo.toml"), QByteArray{}) ||
      !write_bytes(helper_path,
          "#!/bin/sh\n"
          "set -eu\n"
          "printf '%s\\n' \"$1\" >> \"$PHOTO_EDITOR_APP_DATA_DIR/helper-calls\"\n"
          "case \"$1\" in\n"
          "  list)\n"
          "    sleep 0.25\n"
          "    if [ -f \"$PHOTO_EDITOR_APP_DATA_DIR/catalog-import-job.json\" ]; then\n"
          "      printf '['\n"
          "      cat \"$PHOTO_EDITOR_APP_DATA_DIR/catalog-import-job.json\"\n"
          "      printf ']\\n'\n"
          "    else\n"
          "      printf '[]\\n'\n"
          "    fi\n"
          "    ;;\n"
          "  upsert) printf '%s' \"$2\" > \"$PHOTO_EDITOR_APP_DATA_DIR/catalog-import-job.json\" ;;\n"
          "  delete) rm -f \"$PHOTO_EDITOR_APP_DATA_DIR/catalog-import-job.json\" ;;\n"
          "  import-legacy) printf '{\"accepted\":true}\\n' ;;\n"
          "  *) exit 2 ;;\n"
          "esac\n") ||
      !QFile::setPermissions(helper_path,
          QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner)) return fail("Cannot prepare delayed job helper");
  qputenv("PHOTO_EDITOR_REPOSITORY_ROOT", helper_root.toUtf8());
  const QByteArray legacy_jobs(
      "{\"jobs\":[{\"job_id\":\"legacy-observer-only\",\"job_kind\":\"asset_export\","
      "\"status\":\"Completed\",\"created_at\":\"2020-01-01T00:00:00Z\","
      "\"updated_at\":\"2020-01-01T00:00:00Z\"}]}");
  if (!write_bytes(legacy_path, legacy_jobs)) return fail("Cannot prepare legacy job observer fixture");
  {
    CatalogAssetModel poll_model;
    int poll_heartbeat_ticks = 0;
    QTimer poll_heartbeat;
    poll_heartbeat.setInterval(1);
    QObject::connect(&poll_heartbeat, &QTimer::timeout,
                     [&] { ++poll_heartbeat_ticks; });
    poll_heartbeat.start();
    QElapsedTimer notification_timer;
    notification_timer.start();
    jobStoreNotifier()->notifyJobsChanged();
    if (!wait_until([&] { return CatalogImportPreflightTestAccess::importJobPollInFlight(poll_model); }))
      return fail("Job history poll did not start asynchronously");
    for (int index = 0; index < 30; ++index) jobStoreNotifier()->notifyJobsChanged();
    const auto notification_elapsed = notification_timer.elapsed();
    if (!expect(notification_elapsed < 100,
                "Job notification blocked on the delayed catalog helper") ||
        !expect(CatalogImportPreflightTestAccess::importJobPollDirty(poll_model),
                "Notifications during an import-job poll should request one fresh read") ||
        !wait_until([&] {
          return poll_heartbeat_ticks >= 10 &&
              CatalogImportPreflightTestAccess::importJobPollInFlight(poll_model);
        }) ||
        !wait_until([&] {
          QFile calls(helper_calls_path);
          return calls.open(QIODevice::ReadOnly) &&
              calls.readAll().count('\n') == 2 &&
              !CatalogImportPreflightTestAccess::importJobPollInFlight(poll_model);
        }))
      return fail("Job history polling blocked the UI heartbeat");
  }
  QFile helper_calls(helper_calls_path);
  QFile preserved_legacy(legacy_path);
  if (!expect(helper_calls.open(QIODevice::ReadOnly) && helper_calls.readAll() == "list\nlist\n",
               "Background import-job observation should coalesce to two reads") ||
      !expect(preserved_legacy.open(QIODevice::ReadOnly) && preserved_legacy.readAll() == legacy_jobs,
              "Background import-job observation must not migrate legacy records"))
    return fail("Background job polling changed local job history");
  helper_calls.close();
  {
    auto closing_poll_model = std::make_unique<CatalogAssetModel>();
    CatalogImportPreflightTestAccess::requestImportJobPoll(*closing_poll_model);
    if (!wait_until([&] {
          QFile calls(helper_calls_path);
          return calls.open(QIODevice::ReadOnly) && calls.readAll().count('\n') == 3;
        })) return fail("Delayed observer helper did not start for the closing model");
    QElapsedTimer destruction_timer;
    destruction_timer.start();
    closing_poll_model.reset();
    if (destruction_timer.elapsed() >= 100)
      return fail("Closing a model waited for its background job poll");
  }
  QFile::remove(helper_calls_path);
  QFile::remove(legacy_path);
#endif
  const auto source = root.filePath("source");
  const auto other = root.filePath("other");
  if (!QDir().mkpath(source) || !QDir().mkpath(other)) return fail("Cannot create sources");
  const auto raw = QDir(source).filePath("pair.dng");
  const auto jpeg = QDir(source).filePath("pair.jpg");
  const auto video = QDir(source).filePath("clip.mp4");
  const auto unsupported = QDir(source).filePath("note.txt");
  const auto other_photo = QDir(other).filePath("new.jpg");
  for (const auto& path : {raw, jpeg, video, unsupported, other_photo})
    if (!write_file(path)) return fail("Cannot write source fixture");
  CatalogAssetModel model;
  if (!expect(
          QDir::cleanPath(model.commonDirectoryForFiles({raw, jpeg})) == QDir::cleanPath(source),
          "Native common-directory resolution failed") ||
      !expect(
          QDir::cleanPath(model.localPathForUrl(model.localFileUrlForPath(raw))) ==
              QDir::cleanPath(raw),
          "Native file URL round trip failed") ||
      !expect(model.localPathIsWithinRoot(raw, source),
              "Native child path should be accepted within its source root") ||
      !expect(!model.localPathIsWithinRoot(other_photo, source),
              "Native sibling path should be rejected outside its source root")) {
    return 1;
  }
  model.setInteractiveRenderingActive(true);
  CatalogAssetItem duplicate;
  duplicate.asset_id = "known-photo";
  duplicate.file_path = raw;
  model.replaceAssetsForTesting({duplicate});
  const QStringList selected{raw, raw, jpeg, video, unsupported, QDir(source).filePath("missing.jpg")};
  auto expected = model.importPreflight(source, selected, "managed", "skip", "source", "both");
  expected.remove("eligiblePaths");
  int ticks = 0;
  QTimer heartbeat;
  heartbeat.setInterval(1);
  QObject::connect(&heartbeat, &QTimer::timeout, [&] { ++ticks; });
  heartbeat.start();
  {
    PoolGate gate;
    model.requestImportPreflight(source, selected, "managed", "skip", "source", "both");
    if (!model.importPreflightBusy() || !model.importPreflightResult().isEmpty() ||
        !wait_until([&] { return ticks >= 10 && CatalogImportPreflightTestAccess::active(model); }))
      return fail("Review blocked the event loop or published before worker completion");
  }
  if (!wait_until([&] { return !model.importPreflightBusy(); }) || model.importPreflightResult() != expected)
    return fail("Async review did not match synchronous preflight");
  if (model.importPreflightScannedCount() != selected.size()) return fail("Scan did not visit selected candidates");
  // The file disappears after the completed review. Policy changes reuse the
  // immutable review facts; confirmation must re-read current facts instead.
  if (!QFile::remove(video)) return fail("Cannot change source fixture");
  model.requestImportPreflight(source, selected, "referenced", "replace", "source", "raw-only");
  if (!wait_until([&] { return !model.importPreflightBusy(); }) ||
      model.importPreflightResult().value("videoCount").toInt() != 1 ||
      model.importPreflightScannedCount() != 0 ||
      model.importPreflightResult().value("policySkippedCount").toInt() != 1)
    return fail("Policy update did not reuse the completed file scan");
  {
    PoolGate gate;
    model.requestImportPreflight(source, selected, "managed", "skip", "source", "both");
    if (!wait_until([&] { return CatalogImportPreflightTestAccess::active(model); })) return fail("Worker did not queue");
    for (int index = 0; index < 30; ++index)
      model.requestImportPreflight(other, {}, "referenced", "skip", "source", "both");
  }
  if (!wait_until([&] { return !model.importPreflightBusy(); }) ||
      model.importPreflightResult().value("sourcePath").toString() != other ||
      model.importPreflightResult().value("candidateCount").toInt() != 1)
    return fail("Superseded source replaced the latest report");
  // A finished task whose callback has not been delivered must also be stale.
  model.requestImportPreflight(source, selected, "managed", "skip", "source", "both");
  if (!wait_until([&] { return CatalogImportPreflightTestAccess::active(model); })) return fail("Worker did not start");
  CatalogImportPreflightTestAccess::pool().waitForDone();
  model.cancelImportPreflight();
  model.requestImportPreflight(other, {}, "referenced", "skip", "source", "both");
  if (!wait_until([&] { return !model.importPreflightBusy(); }) ||
      model.importPreflightResult().value("sourcePath").toString() != other)
    return fail("Late completion replaced the newer review");
  if (!QFile::remove(other_photo)) return fail("Cannot remove reviewed file");
  model.importSelectionWithOptions(other, {}, {}, "referenced", "skip", "source", "both");
  if (!model.busy() || !model.importPreflightBusy()) return fail("Admission was not asynchronous");
  // Closing the view after confirmation cannot cancel the submitted request.
  model.cancelImportPreflight();
  if (!wait_until([&] { return !model.importPreflightBusy(); }) || model.busy() ||
      model.importing() || model.errorMessage().isEmpty() ||
      model.importPreflightResult().value("importableCount").toInt() != 0)
    return fail("Confirmation reused stale file facts or failed to release busy state");
  if (!write_file(other_photo)) return fail("Cannot restore admission fixture");
  {
    PoolGate gate;
    model.importSelectionWithOptions(other, {}, {}, "referenced", "skip", "source", "both");
    if (!wait_until([&] { return CatalogImportPreflightTestAccess::active(model); })) return fail("Admission did not queue");
    qputenv("PHOTO_EDITOR_MANAGED_MEDIA_DIR", root.filePath("changed-library").toUtf8());
  }
  if (!wait_until([&] { return !model.importPreflightBusy(); }) || model.importing() || model.busy())
    return fail("Admission ignored a destination change during scanning");
  qputenv("PHOTO_EDITOR_MANAGED_MEDIA_DIR", root.filePath("library").toUtf8());
  model.importSelectionWithOptions(other, {}, {}, "referenced", "skip", "source", "both");
  if (!wait_until([&] { return !model.importPreflightBusy(); }) || !model.importing())
    return fail("Fresh successful admission did not queue the import");
  bool found = false;
  for (const auto& job : loadPersistedJobRecords()) {
    if (job.job_kind != "catalog_import" || job.source_path != other) continue;
    found = job.metadata_json.value("selectionCount").toInt() == 1 &&
        job.metadata_json.value("selectedPaths").toArray().at(0).toString() == other_photo &&
        job.metadata_json.value("importPreflight").toObject().value("importableCount").toInt() == 1;
    if (!removePersistedJobRecord(job.job_id, nullptr)) return fail("Cannot remove fixture job");
    break;
  }
  if (!found) return fail("Admission did not persist the freshly scanned selection");
  {
    PoolGate gate;
    auto closing = std::make_unique<CatalogAssetModel>();
    closing->requestImportPreflight(source, selected, "managed", "skip", "source", "both");
    if (!wait_until([&] { return CatalogImportPreflightTestAccess::active(*closing); })) return fail("Closing worker did not start");
    closing.reset();
  }
  std::cout << "Async parity, heartbeat, cached policy changes, stale completion, fresh admission, and destruction passed\n";
  return 0;
}
