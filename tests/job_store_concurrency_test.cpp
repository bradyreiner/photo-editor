#include "app_bootstrap.h"
#include "job_store.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QThread>
#include <QThreadPool>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>
#include <iostream>
#include <memory>
#include <vector>

using namespace photo_editor::desktop;

int real_migration_proof(const QString& root) {
  const QDir directory(root);
  if (!root.startsWith("/private/tmp/job-store-real-") || !directory.exists() ||
      !directory.entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden).isEmpty()) return 1;
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", directory.filePath("runtime").toUtf8());
  qputenv("PHOTO_EDITOR_CACHE_DIR", directory.filePath("cache").toUtf8());
  qunsetenv("PHOTO_EDITOR_DATABASE_URL");
  QString error;
  if (!AppBootstrap::ensureCatalogService(&error)) { std::cerr << error.toStdString() << '\n'; return 1; }
  const auto legacy_path = directory.filePath("runtime/jobs/index.json");
  const QByteArray seed = R"({"jobs":[
    {"job_id":"legacy-paused","job_kind":"asset_export","status":"Paused","metadata_json":{"marker":"preserve","nested":[1,"two"]}},
    {"job_id":"legacy-canceled","job_kind":"catalog_import","status":"Canceled","metadata_json":{"marker":"preserve","nested":[1,"two"]}}
  ]})";
  // Exceed macOS ARG_MAX just as a real job containing image/mask data does.
  const auto large_payload = QString(8 * 1024 * 1024, QLatin1Char('x'));
  auto legacy_jobs = QJsonDocument::fromJson(seed).object().value("jobs").toArray();
  auto large_job = legacy_jobs.first().toObject();
  auto metadata = large_job.value("metadata_json").toObject();
  metadata.insert("large_payload", large_payload);
  large_job.insert("metadata_json", metadata);
  legacy_jobs.replace(0, large_job);
  const auto legacy = QJsonDocument(QJsonObject{{"jobs", legacy_jobs}}).toJson(QJsonDocument::Compact);
  if (!QDir().mkpath(directory.filePath("runtime/jobs"))) return 1;
  QFile file(legacy_path);
  if (!file.open(QIODevice::WriteOnly) || file.write(legacy) != legacy.size()) return 1;
  file.close();
  const auto migrated = loadPersistedJobRecords(&error);
  if (!error.isEmpty() || migrated.size() != 2) { std::cerr << error.toStdString() << '\n'; return 1; }
  if (!file.open(QIODevice::ReadOnly) ||
      !QJsonDocument::fromJson(file.readAll()).object().value("jobs").toArray().isEmpty()) return 1;
  file.close();
  const auto persisted = loadPersistedJobRecords(&error, false);
  if (!error.isEmpty() || persisted.size() != 2) return 1;
  for (const auto& record : persisted) {
    const auto expected = record.job_id == "legacy-paused" ? "Paused" : "Canceled";
    if (record.status != expected || record.metadata_json.value("marker") != "preserve" ||
        record.metadata_json.value("nested").toArray() != QJsonArray{1, "two"}) return 1;
    if (record.job_id == "legacy-paused" &&
        record.metadata_json.value("large_payload").toString() != large_payload) return 1;
  }
  // An interrupted migration can be retried without losing an identical row.
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) || file.write(legacy) != legacy.size()) return 1;
  file.close();
  (void)loadPersistedJobRecords(&error);
  if (!error.isEmpty()) return 1;
  // A newer catalog update is authoritative: old local history must neither
  // overwrite it nor be silently cleared as if the migration succeeded.
  auto newer = *std::find_if(persisted.cbegin(), persisted.cend(), [](const auto& record) {
    return record.job_id == "legacy-paused";
  });
  newer.status = "Completed";
  newer.metadata_json.insert("marker", "newer-catalog-state");
  if (!upsertPersistedJobRecord(newer, &error, true)) return 1;
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) || file.write(legacy) != legacy.size()) return 1;
  file.close();
  (void)loadPersistedJobRecords(&error);
  if (error.isEmpty()) {
    std::cerr << "Migration silently overwrote a newer catalog job\n"; return 1;
  }
  if (!file.open(QIODevice::ReadOnly) || file.readAll() != legacy) return 1;
  file.close();
  const auto after_conflict = loadPersistedJobRecords(&error, false);
  if (!error.isEmpty() || after_conflict.size() != 2) return 1;
  bool preserved_newer = false;
  for (const auto& record : after_conflict) {
    if (record.job_id == newer.job_id)
      preserved_newer = record.status == "Completed" && record.metadata_json == newer.metadata_json;
  }
  if (!preserved_newer || !QFile::copy(legacy_path, directory.filePath("conflicting-history.json"))) return 1;
  // Competing imports of the same previously absent ID must have exactly one
  // winner. Exercise real helper processes, not the local migration lock.
  QString listing;
  if (!AppBootstrap::runCatalogHelper("catalog_export_jobs", {"list"}, &listing, &error)) return 1;
  auto first = QJsonDocument::fromJson(listing.toUtf8()).array().first().toObject();
  first.insert("job_id", "competing-import");
  first.insert("status", "Paused");
  auto second = first;
  second.insert("status", "Canceled");
  const auto import = [](const QJsonObject& payload) {
    QString output, helper_error;
    const bool ok = AppBootstrap::runCatalogHelper("catalog_export_jobs",
        {"import-legacy", QString::fromUtf8(QJsonDocument(payload).toJson(QJsonDocument::Compact))},
        &output, &helper_error);
    if (!ok) return -1;
    const auto accepted = QJsonDocument::fromJson(output.toUtf8()).object().value("accepted");
    return accepted.isBool() ? (accepted.toBool() ? 1 : 0) : -1;
  };
  auto first_import = QtConcurrent::run(import, first);
  auto second_import = QtConcurrent::run(import, second);
  first_import.waitForFinished(); second_import.waitForFinished();
  if (first_import.result() < 0 || second_import.result() < 0 ||
      first_import.result() + second_import.result() != 1) return 1;
  const auto raced = loadPersistedJobRecords(&error, false);
  bool winner_preserved = false;
  for (const auto& record : raced) {
    if (record.job_id == "competing-import")
      winner_preserved = record.status == (first_import.result() == 1 ? "Paused" : "Canceled");
  }
  if (!error.isEmpty() || raced.size() != 3 || !winner_preserved) return 1;
  const QByteArray corrupt("{incomplete");
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) || file.write(corrupt) != corrupt.size()) return 1;
  file.close();
  (void)loadPersistedJobRecords(&error);
  if (error.isEmpty() || !file.open(QIODevice::ReadOnly) || file.readAll() != corrupt) return 1;
  const auto after_failure = loadPersistedJobRecords(&error, false);
  if (!error.isEmpty() || after_failure.size() != 3) return 1;
  std::cout << "Real PostgreSQL migration preserved statuses/metadata, accepted identical retry, rejected conflicting catalog state, selected exactly one competing-import winner, and preserved conflicting/corrupt local history\n";
  return 0;
}

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  if (app.arguments().size() == 3 && app.arguments()[1] == "--real-catalog")
    return real_migration_proof(app.arguments()[2]);
#ifdef Q_OS_WIN
  return 77;
#else
  if (app.arguments().size() == 3 && app.arguments()[1] == "--write-job") {
    PersistentJobRecord record;
    record.job_id = app.arguments()[2];
    record.metadata_json.insert("payload", QString(100000, QLatin1Char('x')));
    QString error;
    if (upsertPersistedJobRecord(record, &error)) return 0;
    std::cerr << record.job_id.toStdString() << ": " << error.toStdString() << '\n';
    return 1;
  }
  QTemporaryDir runtime;
  if (!runtime.isValid()) return 1;
  const auto write = [](const QString& path, const QByteArray& bytes) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
  };
  const auto read = [&](const QString& name) {
    QFile file(runtime.filePath(name));
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
  };
  const auto helper = runtime.filePath("rust/target/debug/catalog_export_jobs");
  if (!QDir().mkpath(runtime.filePath("rust/target/debug")) ||
      !write(runtime.filePath("rust/Cargo.toml"), "") || !write(helper, R"SH(#!/bin/sh
if [ "$1" = --stdin-json-v1 ]; then
  operation=$(python3 -c 'import json,sys; print(json.load(sys.stdin)["arguments"][0])') || exit 1
  set -- "$operation"
fi
mode=$(cat "$PHOTO_EDITOR_APP_DATA_DIR/mode")
if [ "$mode" = offline ]; then exit 1; fi
if [ "$mode" = hold ] && [ "$1" = import-legacy ]; then
  touch "$PHOTO_EDITOR_APP_DATA_DIR/entered"
  while [ ! -f "$PHOTO_EDITOR_APP_DATA_DIR/release" ]; do sleep 0.01; done
fi
if [ "$1" = import-legacy ]; then printf '{"accepted":true}\n'; exit 0; fi
printf '[]\n'
)SH") || !QFile::setPermissions(helper, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner)) return 1;
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", runtime.path().toUtf8());
  qputenv("PHOTO_EDITOR_CACHE_DIR", runtime.filePath("cache").toUtf8());
  qputenv("PHOTO_EDITOR_REPOSITORY_ROOT", runtime.path().toUtf8());
  qputenv("PHOTO_EDITOR_DATABASE_URL", "postgresql://127.0.0.1:1/job_store_proof");
  if (AppBootstrap::helperProgram("catalog_export_jobs") != helper) return 1;
  (void)jobStoreNotifier();
  QThreadPool pool;
  pool.setMaxThreadCount(2);
  const auto fail = [](const char* message) { std::cerr << message << '\n'; return 1; };
  if (!write(runtime.filePath("mode"), "offline")) return 1;
  PersistentJobRecord seed;
  seed.job_id = "seed";
  if (!upsertPersistedJobRecord(seed) || !write(runtime.filePath("mode"), "hold")) return 1;
  auto migration = QtConcurrent::run(&pool, [] { QString error; (void)loadPersistedJobRecords(&error); return error; });
  QElapsedTimer deadline; deadline.start();
  while (!QFile::exists(runtime.filePath("entered")) && deadline.elapsed() < 5000) {
    app.processEvents(); QThread::msleep(1);
  }
  const bool entered = QFile::exists(runtime.filePath("entered"));
  auto competing_migration = QtConcurrent::run(&pool, [] { QString error; (void)loadPersistedJobRecords(&error); return error; });
  deadline.restart();
  while (!competing_migration.isFinished() && deadline.elapsed() < 1000) {
    app.processEvents(); QThread::msleep(1);
  }
  const bool competing_rejected = competing_migration.isFinished();
  bool mutations_saved = false;
  if (entered && write(runtime.filePath("mode"), "offline")) {
    PersistentJobRecord added; added.job_id = "added";
    seed.status = "Canceled";
    mutations_saved = upsertPersistedJobRecord(added) && upsertPersistedJobRecord(seed);
  }
  // Always release a held helper, including assertion failures.
  if (!write(runtime.filePath("release"), "1")) return 1;
  migration.waitForFinished();
  competing_migration.waitForFinished();
  if (!entered || !mutations_saved) return fail("held migration fixture failed");
  if (!competing_rejected || competing_migration.result().isEmpty()) return fail("overlapping migration must retry without uploading a stale snapshot");
  auto jobs = QJsonDocument::fromJson(read("jobs/index.json")).object().value("jobs").toArray();
  QMap<QString, QString> statuses;
  for (const auto& value : jobs) statuses.insert(value.toObject().value("job_id").toString(), value.toObject().value("status").toString());
  if (statuses != QMap<QString, QString>{{"seed", "Canceled"}, {"added", "Queued"}})
    return fail("migration erased a concurrent append or same-ID update");

  std::vector<std::unique_ptr<QProcess>> writers;
  for (int i = 0; i < 12; ++i) {
    auto child = std::make_unique<QProcess>();
    child->start(app.applicationFilePath(), {"--write-job", QStringLiteral("writer-%1").arg(i)});
    if (!child->waitForStarted(3000)) return 1;
    writers.push_back(std::move(child));
  }
  bool writers_ok = true;
  for (auto& child : writers) {
    if (!child->waitForFinished(15000) || child->exitStatus() != QProcess::NormalExit || child->exitCode() != 0) {
      std::cerr << child->readAllStandardError().toStdString();
      writers_ok = false;
    }
  }
  if (!writers_ok) return fail("concurrent fallback writer failed");
  jobs = QJsonDocument::fromJson(read("jobs/index.json")).object().value("jobs").toArray();
  if (jobs.size() != 14) return fail("concurrent fallback writes lost records");
  const auto before_delete = jobs.size();
  if (!removePersistedJobRecord("writer-0")) return 1;
  jobs = QJsonDocument::fromJson(read("jobs/index.json")).object().value("jobs").toArray();
  if (jobs.size() != before_delete - 1) return fail("fallback delete must preserve other jobs");
  // A successful pass prunes unchanged uploaded versions but leaves a newly
  // appended job for the next pass, with an explicit incomplete-recovery error.
  if (!QFile::remove(runtime.filePath("entered")) || !QFile::remove(runtime.filePath("release")) ||
      !write(runtime.filePath("mode"), "hold")) return 1;
  migration = QtConcurrent::run(&pool, [] { QString error; (void)loadPersistedJobRecords(&error); return error; });
  deadline.restart();
  while (!QFile::exists(runtime.filePath("entered")) && deadline.elapsed() < 5000) {
    app.processEvents(); QThread::msleep(1);
  }
  PersistentJobRecord late; late.job_id = "late";
  const bool late_saved = QFile::exists(runtime.filePath("entered")) &&
      write(runtime.filePath("mode"), "offline") && upsertPersistedJobRecord(late);
  if (!write(runtime.filePath("mode"), "online") || !write(runtime.filePath("release"), "1")) return 1;
  migration.waitForFinished();
  jobs = QJsonDocument::fromJson(read("jobs/index.json")).object().value("jobs").toArray();
  if (!late_saved || jobs.size() != 1 || jobs.first().toObject().value("job_id") != "late" ||
      migration.result().isEmpty()) return fail("successful snapshot pruning must retain late work and report incomplete migration");
  QString final_error;
  (void)loadPersistedJobRecords(&final_error);
  if (!final_error.isEmpty() || !QJsonDocument::fromJson(read("jobs/index.json")).object().value("jobs").toArray().isEmpty())
    return fail("next migration pass must retire the remaining acknowledged snapshot");
  if (!write(runtime.filePath("mode"), "offline")) return 1;
  {
    QLockFile held(runtime.filePath("jobs/index.lock"));
    if (!held.tryLock(0)) return 1;
    const auto before_timeout = read("jobs/index.json");
    deadline.restart();
    QString lock_error;
    if (upsertPersistedJobRecord(seed, &lock_error) || lock_error.isEmpty() ||
        deadline.elapsed() > 2000 || read("jobs/index.json") != before_timeout)
      return fail("held-lock timeout must report failure and preserve history within the bound");
  }
  if (!upsertPersistedJobRecord(seed)) return fail("writer must recover after the lock is released");
  for (const auto& invalid : {QByteArray("{truncated"), QByteArray("{}"), QByteArray(R"({"jobs":[null]})"),
                             QByteArray(R"({"jobs":[{"status":"Queued"}]})")}) {
    if (!write(runtime.filePath("jobs/index.json"), invalid)) return 1;
    QString error;
    if (upsertPersistedJobRecord(seed, &error) || error.isEmpty() || read("jobs/index.json") != invalid)
      return fail("fallback upsert must not overwrite unreadable history");
    if (removePersistedJobRecord("seed", &error) || error.isEmpty() || read("jobs/index.json") != invalid)
      return fail("fallback delete must not overwrite unreadable history");
  }
  std::cout << "Single-flight migration, concurrent append/update, snapshot pruning/retry, 12 process writers, delete preservation and corrupt-history guards passed\n";
  return 0;
#endif
}
