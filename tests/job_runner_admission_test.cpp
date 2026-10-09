#include "app_bootstrap.h"
#include "job_runner.h"
#include "job_store.h"
#include "worker_queue_registry.h"
#include "photo_cache_budget.h"
#if __has_include("job_admission_policy.h")
#include "job_admission_policy.h"
#define LOOKSY_HAS_JOB_ADMISSION 1
#endif
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSemaphore>
#include <QTemporaryDir>
#include <QThread>
#include <QThreadPool>
#include <QtConcurrent>
#include <QScopeGuard>
#include <iostream>

namespace photo_editor::desktop {
struct JobRunnerTestAccess {
  static auto hold(JobRunner& runner, const PersistentJobRecord& job) {
    runner.ready_ = true;
    auto attempt = runner.beginAttempt(job);
#ifdef LOOKSY_HAS_JOB_ADMISSION
    attempt->resources = inspect_job_resources(job);
#endif
    return attempt;
  }
  static bool idle(const JobRunner& runner) {
    return runner.active_attempts_.isEmpty() && !runner.queue_reading_ && !runner.cancellation_reading_;
  }
  static qsizetype active(const JobRunner& runner) { return runner.active_attempts_.size(); }
};
}
using namespace photo_editor::desktop;

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
#ifdef Q_OS_WIN
  return 77;
#else
  const QString scenario = argc > 2 ? QString::fromLocal8Bit(argv[2]) : QStringLiteral("copy");
  const bool importing = scenario == QStringLiteral("import");
  QTemporaryDir runtime;
  const auto write = [](const QString& path, const QByteArray& bytes) {
    QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
  };
  const auto helper = runtime.filePath("rust/target/debug/catalog_export_jobs");
  if (!runtime.isValid() || !QDir().mkpath(runtime.filePath("rust/target/debug")) ||
      !write(runtime.filePath("rust/Cargo.toml"), "") || !write(helper, R"PY(#!/usr/bin/env python3
import os,sys,json,fcntl,time
root=os.environ['PHOTO_EDITOR_APP_DATA_DIR']
with open(root+'/store.lock','a') as lock:
 fcntl.flock(lock,fcntl.LOCK_EX)
 with open(root+'/store.json') as f: rows=json.load(f)
 with open(root+'/calls','a') as f: f.write(sys.argv[1]+'\n')
 if sys.argv[1]=='list': print(json.dumps(rows))
 elif sys.argv[1]=='upsert':
  row=json.loads(sys.argv[2]);rows=[r for r in rows if r['job_id']!=row['job_id']]+[row]
  with open(root+'/store.json','w') as f: json.dump(rows,f)
  with open(root+'/updates','a') as f: f.write(json.dumps({'at':time.monotonic(),'record':row})+'\n')
 else: sys.exit(1)
)PY") || !QFile::setPermissions(helper, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner)) return (std::cerr << "Admission fixture failed at line " << __LINE__ << "\n", 1);
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", runtime.path().toUtf8());
  qputenv("PHOTO_EDITOR_CACHE_DIR", runtime.filePath("cache").toUtf8());
  qputenv("PHOTO_EDITOR_REPOSITORY_ROOT", runtime.path().toUtf8());
  qputenv("PHOTO_EDITOR_DATABASE_URL", "postgresql://127.0.0.1:1/admission_proof");
  if (AppBootstrap::helperProgram("catalog_export_jobs") != helper) return (std::cerr << "Admission fixture failed at line " << __LINE__ << "\n", 1);
  if (importing) {
    const auto importer = runtime.filePath("rust/target/debug/import_catalog");
    if (!write(importer, R"PY(#!/usr/bin/env python3
import os,sys,json,shutil
with open(sys.argv[5]) as f: sources=[line.strip() for line in f if line.strip()]
for source in sources:
 for root in [sys.argv[6],sys.argv[7]]:
  if root=='-': continue
  folder=os.path.join(root,sys.argv[2]);os.makedirs(folder,exist_ok=True)
  shutil.copyfile(source,os.path.join(folder,os.path.basename(source)))
with open(os.environ['PHOTO_EDITOR_APP_DATA_DIR']+'/import-finished','w') as f:
 f.write(os.environ.get('PHOTO_EDITOR_IMPORT_METADATA_THREADS','missing'))
print('VIREL_IMPORT_RESULT='+json.dumps({'importedCount':len(sources),'backedUpCount':len(sources),'verificationAlgorithm':'fixture-copy'}))
)PY") || !QFile::setPermissions(importer, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner)) return 1;
    QDir().mkpath(runtime.filePath("card"));
  }
  QDir().mkpath(runtime.filePath("heavy")); QDir().mkpath(runtime.filePath("exports"));
  const QByteArray source(4 * 1024 * 1024, 'q');
  if (!write(runtime.filePath("source.jpg"), source) || !write(runtime.filePath("heavy/document.json"), "{}")) return (std::cerr << "Admission fixture failed at line " << __LINE__ << "\n", 1);
  if (importing && !write(runtime.filePath("card/source.jpg"), source)) return 1;
  QJsonArray samples;
  for (int iteration = 0; iteration < 5; ++iteration) {
    const auto drive = QStringLiteral("drive-") + QString::fromLatin1(QCryptographicHash::hash(
        runtime.filePath("card").toUtf8(), QCryptographicHash::Sha1).toHex().left(12));
    const auto managed = runtime.filePath(QStringLiteral("managed-%1").arg(iteration));
    const auto backup = runtime.filePath(QStringLiteral("backup-%1").arg(iteration));
    const auto output = importing ? QDir(managed).filePath(drive + "/source.jpg") :
        runtime.filePath(QStringLiteral("exports/copy-%1.jpg").arg(iteration));
    QFile::remove(runtime.filePath("import-finished"));
    QJsonObject light{{"job_id", "copy"}, {"job_kind", "asset_export"}, {"status", "Queued"},
        {"asset_id", "source"}, {"media_type", "photo"}, {"preset_name", "Original Copy"},
        {"source_path", runtime.filePath("source.jpg")}, {"destination_path", runtime.filePath("exports")},
        {"output_path", output}, {"metadata_json", QJsonObject{{"queueOrder", 2}}}};
    if (importing) {
      light.insert("job_kind", "catalog_import"); light.insert("source_path", runtime.filePath("card"));
      light.insert("metadata_json", QJsonObject{{"queueOrder", 2}, {"selectionCount", 1},
          {"selectedPaths", QJsonArray{runtime.filePath("card/source.jpg")}}, {"importMode", "managed"},
          {"duplicatePolicy", "skip"}, {"importPreflight", QJsonObject{{"photoLibraryRoot", managed}, {"backupRoot", backup}}}});
    }
    QJsonObject heavy{{"job_id", "heavy"}, {"job_kind", "timeline_render"}, {"status", "Running"},
        {"source_path", runtime.filePath("heavy/document.json")}, {"output_path", runtime.filePath("heavy/render.mp4")}};
    if (scenario == "conflict") heavy.insert("output_path", runtime.filePath("exports/render.mp4"));
    if (scenario == "foreground") WorkerQueueRegistry::publishUserIntent("studio", true, false, true);
    if (scenario == "pressure") set_photo_cache_pressure("critical");
    QJsonArray initial{heavy, light};
    if (scenario == "head-blocked") initial.push_back(QJsonObject{{"job_id", "blocked-head"},
        {"job_kind", "asset_export"}, {"status", "Queued"}, {"media_type", "photo"},
        {"preset_name", "JPEG"}, {"metadata_json", QJsonObject{{"queueOrder", 1}}}});
    if (!write(runtime.filePath("store.json"), QJsonDocument(initial).toJson()) ||
        !write(runtime.filePath("updates"), "")) return (std::cerr << "Admission fixture failed at line " << __LINE__ << "\n", 1);
    JobRunner runner;
    PersistentJobRecord held;
    held.job_id = "heavy"; held.job_kind = "timeline_render";
    held.source_path = heavy.value("source_path").toString();
    held.output_path = heavy.value("output_path").toString();
    auto attempt = JobRunnerTestAccess::hold(runner, held);
    auto gate = std::make_shared<QSemaphore>();
    const auto release = qScopeGuard([gate] { gate->release(); });
    attempt->watcher->setFuture(QtConcurrent::run([gate] {
      gate->acquire(); return QVariantMap{{QStringLiteral("success"), false},
          {QStringLiteral("errorMessage"), QStringLiteral("controlled heavy attempt complete")}};
    }));
    QElapsedTimer elapsed; elapsed.start();
    QTimer heartbeat;
    heartbeat.setInterval(5);
    qint64 last_tick = 0, max_gap = 0, copy_complete_ms = -1;
    qsizetype peak_active = 1;
    QObject::connect(&heartbeat, &QTimer::timeout, [&] {
      max_gap = qMax(max_gap, elapsed.elapsed() - last_tick); last_tick = elapsed.elapsed();
      peak_active = qMax(peak_active, JobRunnerTestAccess::active(runner));
      if (copy_complete_ms < 0 && QFileInfo::exists(importing ? runtime.filePath("import-finished") : output + ".integrity.json")) copy_complete_ms = elapsed.elapsed();
    });
    heartbeat.start();
    QTimer::singleShot(1000, &runner, [gate] { gate->release(); });
    if (scenario == "foreground") QTimer::singleShot(350, &runner, [] { WorkerQueueRegistry::clearUserIntent("studio"); });
    if (scenario == "pressure") QTimer::singleShot(350, &runner, [] { set_photo_cache_pressure("normal"); });
    runner.wake();
    while ((copy_complete_ms < 0 || !JobRunnerTestAccess::idle(runner) || elapsed.elapsed() < (scenario == "head-blocked" ? 1500 : 1000)) && elapsed.elapsed() < 8000) {
      app.processEvents(); QThread::msleep(1);
    }
    const auto total_ms = elapsed.elapsed();
    heartbeat.stop();
    if (!JobRunnerTestAccess::idle(runner) || copy_complete_ms < 0) return (std::cerr << "Admission fixture failed at line " << __LINE__ << "\n", 1);
    QFile delivered(output);
    if (!delivered.open(QIODevice::ReadOnly) || delivered.readAll() != source) return (std::cerr << "Admission fixture failed at line " << __LINE__ << "\n", 1);
    QFile store(runtime.filePath("store.json")); if (!store.open(QIODevice::ReadOnly)) return (std::cerr << "Admission fixture failed at line " << __LINE__ << "\n", 1);
    const auto records = QJsonDocument::fromJson(store.readAll()).array();
    bool copy_done = false, heavy_done = false, blocked_done = false;
    for (const auto& value : records) {
      const auto row = value.toObject();
      if (row.value("job_id") == "copy") copy_done = row.value("status") == "Completed";
      if (row.value("job_id") == "heavy") heavy_done = row.value("status") == "Failed";
      if (row.value("job_id") == "blocked-head") blocked_done = row.value("status") == "Failed";
    }
    if (!copy_done || !heavy_done) return (std::cerr << "Admission fixture failed at line " << __LINE__ << "\n", 1);
    if (importing) {
      QFile threads(runtime.filePath("import-finished")), backed_up(QDir(backup).filePath(drive + "/source.jpg"));
      if (!threads.open(QIODevice::ReadOnly) || threads.readAll() != "2" ||
          !backed_up.open(QIODevice::ReadOnly) || backed_up.readAll() != source) return 1;
    }
    if (scenario == "head-blocked" && (!blocked_done || copy_complete_ms >= 1000 || peak_active != 2)) return 1;
    if (scenario == "conflict" && (peak_active != 1 || copy_complete_ms < 1000)) return 1;
    if ((scenario == "foreground" || scenario == "pressure") && (copy_complete_ms < 350 || copy_complete_ms >= 1000 || peak_active != 2)) return 1;
    samples.push_back(QJsonObject{{"copy_complete_ms", copy_complete_ms}, {"total_ms", total_ms},
        {"heartbeat_max_gap_ms", max_gap}, {"peak_active", peak_active}, {"exact_copy", true}});
    app.processEvents();
  }
  QThreadPool::globalInstance()->waitForDone();
  const auto report = QJsonDocument(QJsonObject{{"scenario", scenario}, {"samples", samples},
      {"source_sha256", QString::fromLatin1(QCryptographicHash::hash(source, QCryptographicHash::Sha256).toHex())},
      {"scope", "Actual scheduler and copy export; fixture store and controlled one-second heavy future"}}).toJson();
  if (argc > 1) return write(QString::fromLocal8Bit(argv[1]), report) ? 0 : 1;
  std::cout << report.constData();
  return 0;
#endif
}
