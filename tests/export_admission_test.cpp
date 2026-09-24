#include "export_job_controller.h"
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QJsonDocument>
#include <QSemaphore>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <iostream>
#include <atomic>
#include <memory>

namespace photo_editor::desktop {
struct ExportJobControllerTestAccess {
  static void writer(ExportJobController& controller, std::function<ExportAdmissionResult(PersistentJobRecord)> writer) {
    controller.admission_writer_ = std::move(writer);
    controller.history_reader_ = [] { return ExportHistoryReadResult{}; };
  }
};
}

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  QTemporaryDir root;
  if (!root.isValid()) return 1;
  const auto write = [](const QString& path, const QByteArray& bytes) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
  };
  const auto helper = root.filePath("rust/target/debug/catalog_export_jobs");
  if (!QDir().mkpath(root.filePath("rust/target/debug")) ||
      !write(root.filePath("rust/Cargo.toml"), "") || !write(helper, R"PY(#!/usr/bin/env python3
import json,os,sys,time
args=sys.argv[1:]
if args[0]=='--stdin-json-v1': args=json.load(sys.stdin)['arguments']
time.sleep(.2)
path=os.path.join(os.environ['PHOTO_EDITOR_APP_DATA_DIR'],'records.json')
fail=os.path.join(os.environ['PHOTO_EDITOR_APP_DATA_DIR'],'fail-read')
if args[0]=='list' and os.path.exists(fail):
    print('Catalog temporarily unavailable',file=sys.stderr);sys.exit(1)
rows=json.load(open(path)) if os.path.exists(path) else []
if args[0]=='list': print(json.dumps(rows))
elif args[0]=='upsert':
    row=json.loads(args[1]); rows=[r for r in rows if r['job_id']!=row['job_id']]+[row]
    lost=os.path.join(os.environ['PHOTO_EDITOR_APP_DATA_DIR'],'lost-ack')
    if os.path.exists(lost): row['status']='Completed'
    with open(path+'.tmp','w') as f: json.dump(rows,f)
    os.replace(path+'.tmp',path)
    if os.path.exists(lost): os.remove(lost);sys.exit(1)
else: sys.exit(1)
)PY") || !QFile::setPermissions(helper, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner)) return 1;
  qputenv("PHOTO_EDITOR_REPOSITORY_ROOT", root.path().toUtf8());
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", root.path().toUtf8());
  qputenv("PHOTO_EDITOR_CACHE_DIR", root.filePath("cache").toUtf8());
  qputenv("PHOTO_EDITOR_DATABASE_URL", "postgresql://127.0.0.1:1/export_admission_proof");
  const auto source = root.filePath("source.png");
  QImage image(32, 24, QImage::Format_RGB32); image.fill(Qt::red);
  if (!image.save(source)) return 1;
  photo_editor::desktop::ExportJobController exports;
  const auto wait = [&](const auto& ready) {
    QElapsedTimer deadline; deadline.start();
    while (!ready() && deadline.elapsed() < 10000) { app.processEvents(); QThread::msleep(1); }
    return ready();
  };
  int ticks = 0;
  QTimer heartbeat; heartbeat.setInterval(10);
  QObject::connect(&heartbeat, &QTimer::timeout, [&] { ++ticks; }); heartbeat.start();
  QElapsedTimer timer; timer.start();
  const bool accepted = exports.queueExport("source", "photo", "Proof", source, {}, {}, "Full Resolution", root.filePath("exports"));
  const auto admission_ms = timer.elapsed();
  const auto ticks_at_return = ticks;
  const auto immediate = exports.jobs();
  while ((exports.property("pendingAdmissionCount").toInt() > 0 || exports.loading()) && timer.elapsed() < 10000) {
    app.processEvents(); QThread::msleep(1);
  }
  QFile records(root.filePath("records.json"));
  if (!accepted || !records.open(QIODevice::ReadOnly) || !QJsonDocument::fromJson(records.readAll()).isArray()) return 1;
  std::cout << "admission_ms=" << admission_ms << " ticks_at_return=" << ticks_at_return
            << " total_ticks=" << ticks << " immediate_status="
            << (immediate.isEmpty() ? QString{} : immediate.first().toMap().value("status").toString()).toStdString() << '\n';
  if (!app.arguments().contains("--measure-only") && (admission_ms >= 50 || ticks < 20 ||
      exports.property("pendingAdmissionCount").toInt() != 0)) return 1;
  if (app.arguments().contains("--measure-only")) return 0;
  if (immediate.first().toMap().value("status") != "Saving") return 1;
  records.close();
  if (!write(root.filePath("fail-read"), "offline")) return 1;
  if (!exports.queueExport("failed", "photo", "Failure", source, {}, {}, "Full Resolution", root.filePath("exports"))) return 1;
  const auto failed_id = exports.jobs().last().toMap().value("jobId").toString();
  if (!wait([&] { return exports.pendingAdmissionCount() == 0 && !exports.loading(); }) ||
      exports.admissionError().isEmpty() || QFile::exists(root.filePath("jobs/index.json"))) return 1;
  bool failed_visible = false;
  for (const auto& job : exports.jobs())
    if (job.toMap().value("jobId") == failed_id) failed_visible = job.toMap().value("status") == "Save unconfirmed";
  if (!failed_visible || !QFile::remove(root.filePath("fail-read"))) return 1;
  exports.retryUnconfirmedExports();
  if (!wait([&] { return exports.pendingAdmissionCount() == 0 && !exports.loading(); }) || !exports.admissionError().isEmpty()) return 1;
  int matching_ids = 0;
  for (const auto& job : exports.jobs()) if (job.toMap().value("jobId") == failed_id) ++matching_ids;
  if (matching_ids != 1) return 1;
  if (!write(root.filePath("lost-ack"), "committed without receipt") ||
      !exports.queueExport("lost-ack", "photo", "Lost receipt", source, {}, {}, "Full Resolution", root.filePath("exports"))) return 1;
  const auto ambiguous_id = exports.jobs().last().toMap().value("jobId").toString();
  if (!wait([&] { return exports.pendingAdmissionCount() == 0 && !exports.loading(); })) return 1;
  exports.retryUnconfirmedExports();
  if (!wait([&] { return exports.pendingAdmissionCount() == 0 && !exports.loading(); })) return 1;
  bool preserved_completed = false;
  for (const auto& job : exports.jobs()) if (job.toMap().value("jobId") == ambiguous_id)
    preserved_completed = job.toMap().value("status") == "Completed";
  if (!preserved_completed) return 1;

  // Every accepted task owns its payload independently of the controller.
  // Holding the serial writer also exercises the bounded pending queue.
  struct State { QSemaphore release; std::atomic_int entered{0}; std::atomic_int completed{0}; };
  const auto state = std::make_shared<State>();
  struct ReleaseOnExit { std::shared_ptr<State> state; ~ReleaseOnExit() { state->release.release(); } } release_on_exit{state};
  auto bounded = std::make_unique<photo_editor::desktop::ExportJobController>();
  photo_editor::desktop::ExportJobControllerTestAccess::writer(*bounded, [state](auto record) {
    if (++state->entered == 1) state->release.acquire();
    ++state->completed;
    return photo_editor::desktop::ExportAdmissionResult{record, {}};
  });
  for (int i = 0; i < 64; ++i)
    if (!bounded->queueExport(QString::number(i), "photo", "Bounded", source, {}, {}, "Full Resolution", root.filePath("exports"))) return 1;
  if (bounded->queueExport("overflow", "photo", "Overflow", source, {}, {}, "Full Resolution", root.filePath("exports")) ||
      bounded->pendingAdmissionCount() != 64 || !wait([&] { return state->entered == 1; })) return 1;
  timer.restart(); bounded.reset(); const auto close_ms = timer.elapsed();
  state->release.release();
  if (close_ms >= 50 || !wait([&] { return state->completed == 64; })) return 1;
  std::cout << "Catalog failure preserved an unconfirmed row without fallback; same-ID retry, 64-request bound and detached payload lifetime passed\n";
  return 0;
}
