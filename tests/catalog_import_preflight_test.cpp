#include "catalog_asset_model.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>
#include <QSemaphore>
#include <QTemporaryDir>
#include <QThread>
#include <QThreadPool>
#include <QTimer>
#include <QTcpServer>
#include <QHostAddress>
#include <algorithm>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <utility>
#if defined(Q_OS_UNIX)
#include <sys/resource.h>
#endif

namespace photo_editor::desktop {
struct CatalogImportPreflightTestAccess {
  static QThreadPool& pool() { return CatalogAssetModel::importPreflightThreadPool(); }
  static QThreadPool& pollPool() { return CatalogAssetModel::importJobPollThreadPool(); }
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
bool wait_until(const std::function<bool()>& ready, int timeout_ms = 5000) {
  QElapsedTimer timer;
  timer.start();
  while (!ready() && timer.elapsed() < timeout_ms) {
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
qint64 process_peak_rss_bytes() {
#if defined(Q_OS_UNIX)
  struct rusage usage {};
  if (getrusage(RUSAGE_SELF, &usage) == 0) {
#if defined(Q_OS_DARWIN)
    return static_cast<qint64>(usage.ru_maxrss);
#else
    return static_cast<qint64>(usage.ru_maxrss) * 1024;
#endif
  }
#endif
  return 0;
}
bool expect(bool condition, const char* message) {
  if (condition) return true;
  std::cerr << message << '\n';
  return false;
}

bool create_real_file_fixture(const QString& source, int file_count) {
  if (!QDir().mkpath(source)) return false;
  for (int index = 0; index < file_count; ++index) {
    const auto extension = index % 20 == 0 ? QStringLiteral("mp4")
        : index % 20 == 1 ? QStringLiteral("wav")
        : index % 20 == 2 ? QStringLiteral("txt")
                          : QStringLiteral("jpg");
    const auto path = QDir(source).filePath(
        QStringLiteral("asset-%1.%2").arg(index, 6, 10, QLatin1Char('0')).arg(extension));
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return false;
    file.close();
  }
  return true;
}

int run_real_file_preflight_benchmark() {
  QTemporaryDir temporary;
  if (!temporary.isValid()) return fail("Temporary benchmark directory unavailable");
  const QDir root(temporary.path());
  if (!QDir().mkpath(root.filePath("library")) || !QDir().mkpath(root.filePath("backup")))
    return fail("Cannot create isolated benchmark destinations");
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", root.filePath("data").toUtf8());
  qputenv("PHOTO_EDITOR_CACHE_DIR", root.filePath("cache").toUtf8());
  qputenv("PHOTO_EDITOR_MANAGED_MEDIA_DIR", root.filePath("library").toUtf8());
  qputenv("PHOTO_EDITOR_ORIGINALS_BACKUP_DIR", root.filePath("backup").toUtf8());
  qputenv("PHOTO_EDITOR_DATABASE_URL", "postgresql://127.0.0.1:1/preflight_benchmark");

  QJsonArray workloads;
  for (const int file_count : {10000, 100000}) {
    const auto source = root.filePath(QStringLiteral("source-%1").arg(file_count));
    if (!create_real_file_fixture(source, file_count))
      return fail("Cannot create real-file preflight fixture");
    const auto latest_source = root.filePath(QStringLiteral("latest-source-%1").arg(file_count));
    if (!QDir().mkpath(latest_source) ||
        !write_file(QDir(latest_source).filePath(QStringLiteral("latest.jpg"))))
      return fail("Cannot create replacement preflight source");

    CatalogAssetModel model;
    QElapsedTimer heartbeat_clock;
    heartbeat_clock.start();
    qint64 last_heartbeat_ns = 0;
    qint64 max_heartbeat_gap_ns = 0;
    int heartbeat_count = 0;
    QTimer heartbeat;
    heartbeat.setInterval(1);
    QObject::connect(&heartbeat, &QTimer::timeout, [&] {
      const auto now = heartbeat_clock.nsecsElapsed();
      if (last_heartbeat_ns > 0)
        max_heartbeat_gap_ns = qMax(max_heartbeat_gap_ns, now - last_heartbeat_ns);
      last_heartbeat_ns = now;
      ++heartbeat_count;
    });
    heartbeat.start();

    QElapsedTimer cold_timer;
    cold_timer.start();
    model.requestImportPreflight(source, {}, "managed", "skip", "source", "both");
    if (!wait_until([&] { return !model.importPreflightBusy(); }, 120000))
      return fail("Cold real-file preflight exceeded its bound");
    const auto cold_ms = static_cast<double>(cold_timer.nsecsElapsed()) / 1.0e6;
    const auto cold_scanned = model.importPreflightScannedCount();
    const auto cold_result = model.importPreflightResult();
    if (cold_scanned != file_count || cold_result.value("candidateCount").toInt() != file_count)
      return fail("Cold real-file preflight did not account for every candidate");

    QElapsedTimer warm_timer;
    warm_timer.start();
    model.requestImportPreflight(source, {}, "managed", "replace", "date-camera", "raw-only");
    if (!wait_until([&] { return !model.importPreflightBusy(); }, 120000))
      return fail("Warm real-file preflight exceeded its bound");
    const auto warm_ms = static_cast<double>(warm_timer.nsecsElapsed()) / 1.0e6;
    const auto warm_scanned = model.importPreflightScannedCount();
    const auto warm_result = model.importPreflightResult();
    if (warm_scanned != 0 || warm_result.value("candidateCount").toInt() != file_count)
      return fail("Warm policy refresh did not reuse the complete file scan");

    heartbeat.stop();
    QJsonObject cancellation;
    if (file_count == 100000) {
      // Reuse real files to exercise cancellation while the recursive scan is
      // working. The replacement source must win even if the old worker drains
      // after the cancellation request.
      model.cancelImportPreflight();
      model.requestImportPreflight(source, {}, "managed", "skip", "source", "both");
      if (!wait_until([&] {
            return model.importPreflightScannedCount() > 0 || !model.importPreflightBusy();
          }, 120000))
        return fail("Active real-file cancellation fixture did not start");
      const auto scanned_at_cancel = model.importPreflightScannedCount();
      const auto active_at_cancel = model.importPreflightBusy() && scanned_at_cancel < file_count;
      model.cancelImportPreflight();
      QElapsedTimer replacement_timer;
      replacement_timer.start();
      model.requestImportPreflight(latest_source, {}, "referenced", "skip", "source", "both");
      if (!wait_until([&] { return !model.importPreflightBusy(); }, 120000))
        return fail("Replacement preflight did not finish after cancellation");
      const auto replacement_ms = static_cast<double>(replacement_timer.nsecsElapsed()) / 1.0e6;
      const auto replacement = model.importPreflightResult();
      if (replacement.value("sourcePath").toString() != latest_source ||
          replacement.value("candidateCount").toInt() != 1 ||
          model.importPreflightScannedCount() != 1)
        return fail("Canceled recursive scan replaced the latest preflight result");
      cancellation = QJsonObject{
          {"scanned_count_at_cancel", scanned_at_cancel},
          {"active_scan_at_cancel", active_at_cancel},
          {"replacement_request_to_result_ms", replacement_ms},
          {"latest_source_won", true},
      };
    }

    auto workload = QJsonObject{
        {"file_count", file_count},
        {"cold_controller_cache_ms", cold_ms},
        {"cold_scanned_count", cold_scanned},
        {"warm_policy_reuse_ms", warm_ms},
        {"warm_scanned_count", warm_scanned},
        {"candidate_count", warm_result.value("candidateCount").toInt()},
        {"importable_count", warm_result.value("importableCount").toInt()},
        {"heartbeat_ticks", heartbeat_count},
        {"max_heartbeat_gap_ms", static_cast<double>(max_heartbeat_gap_ns) / 1.0e6},
        {"exact_warm_candidate_parity",
         cold_result.value("candidateCount") == warm_result.value("candidateCount")},
    };
    if (!cancellation.isEmpty()) workload.insert("cancellation", cancellation);
    workloads.append(workload);
  }

  std::cout << QJsonDocument(QJsonObject{
      {"scenario", "real-file-preflight-cold-controller-cache-and-warm-policy-reuse"},
      {"storage", "local QTemporaryDir; OS page-cache state unspecified"},
      {"fixture_bytes_per_file", 0},
      {"metrics", "request-to-result latency and 1 ms QTimer gap proxy; fixture creation excluded"},
      {"process_peak_rss_bytes", process_peak_rss_bytes()},
      {"workloads", workloads},
  }).toJson(QJsonDocument::Compact).constData() << '\n';
  return 0;
}

int run_import_file_details_benchmark() {
  QTemporaryDir temporary;
  if (!temporary.isValid()) return fail("Temporary details benchmark directory unavailable");
  const QDir root(temporary.path());
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", root.filePath("data").toUtf8());
  qputenv("PHOTO_EDITOR_CACHE_DIR", root.filePath("cache").toUtf8());
  qputenv("PHOTO_EDITOR_DATABASE_URL", "postgresql://127.0.0.1:1/import_details_benchmark");

  const auto ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
  const auto ffprobe = QStandardPaths::findExecutable(QStringLiteral("ffprobe"));
  if (ffmpeg.isEmpty() || ffprobe.isEmpty())
    return fail("The real-file details benchmark requires ffmpeg and ffprobe");
  const auto generate_video = [&](const QString& path, int duration_seconds) {
    QProcess fixture_writer;
    fixture_writer.start(ffmpeg, {
        QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"),
        QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
        QStringLiteral("color=c=black:s=64x64:r=24:d=%1").arg(duration_seconds),
        QStringLiteral("-an"), QStringLiteral("-c:v"), QStringLiteral("mpeg4"),
        QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"), QStringLiteral("-y"), path});
    return fixture_writer.waitForStarted(5000) && fixture_writer.waitForFinished(30000) &&
        fixture_writer.exitStatus() == QProcess::NormalExit && fixture_writer.exitCode() == 0 &&
        QFileInfo::exists(path);
  };
  const auto video_path = root.filePath("details-benchmark.mp4");
  const auto changed_video_path = root.filePath("details-benchmark-revision.mp4");
  if (!generate_video(video_path, 1) || !generate_video(changed_video_path, 2))
    return fail("ffmpeg could not create the real video fixtures");

  CatalogAssetModel model;
  QElapsedTimer heartbeat_clock;
  heartbeat_clock.start();
  qint64 last_heartbeat_ns = 0;
  qint64 max_heartbeat_gap_ns = 0;
  int heartbeat_count = 0;
  QTimer heartbeat;
  heartbeat.setInterval(1);
  QObject::connect(&heartbeat, &QTimer::timeout, [&] {
    const auto now = heartbeat_clock.nsecsElapsed();
    if (last_heartbeat_ns > 0)
      max_heartbeat_gap_ns = qMax(max_heartbeat_gap_ns, now - last_heartbeat_ns);
    last_heartbeat_ns = now;
    ++heartbeat_count;
  });
  heartbeat.start();

  QElapsedTimer cold_timer;
  cold_timer.start();
  model.requestImportFileDetails(video_path);
  if (!wait_until([&] {
        const auto details = model.importFileDetails();
        return details.value("path").toString() == video_path &&
            !details.value("loading").toBool();
      }, 30000))
    return fail("Cold real-video details probe exceeded its bound");
  const auto cold_ms = static_cast<double>(cold_timer.nsecsElapsed()) / 1.0e6;
  const auto cold_details = model.importFileDetails();
  if (!cold_details.value("available").toBool() ||
      cold_details.value("durationText").toString().isEmpty() ||
      cold_details.value("width").toInt() != 64 ||
      cold_details.value("height").toInt() != 64)
    return fail("Real ffprobe details did not contain expected video metadata");
  const auto original_duration = cold_details.value("durationText").toString();
  const auto original_size = QFileInfo(video_path).size();
  const auto original_modified = QFileInfo(video_path).lastModified();

  QVector<double> warm_samples_ms;
  QJsonArray raw_warm_samples_ms;
  bool exact_details_parity = true;
  for (int sample = 0; sample < 5; ++sample) {
    QElapsedTimer timer;
    timer.start();
    model.requestImportFileDetails(video_path);
    if (!wait_until([&] {
          const auto details = model.importFileDetails();
          return details.value("path").toString() == video_path &&
              !details.value("loading").toBool();
        }, 30000))
      return fail("Repeated real-video details probe exceeded its bound");
    const auto elapsed_ms = static_cast<double>(timer.nsecsElapsed()) / 1.0e6;
    const auto details = model.importFileDetails();
    exact_details_parity = exact_details_parity &&
        details.value("available") == cold_details.value("available") &&
        details.value("durationText") == cold_details.value("durationText") &&
        details.value("width") == cold_details.value("width") &&
        details.value("height") == cold_details.value("height") &&
        details.value("captureTime") == cold_details.value("captureTime");
    warm_samples_ms.append(elapsed_ms);
    raw_warm_samples_ms.append(elapsed_ms);
  }
  heartbeat.stop();
  if (!exact_details_parity)
    return fail("Repeated real-video details changed while the source revision was stable");

  if (!QFile::remove(video_path) || !QFile::copy(changed_video_path, video_path))
    return fail("Cannot replace the real video with a changed revision");
  QFile changed_source(video_path);
  const auto changed_time = QDateTime::currentDateTime().addSecs(2);
  if (!changed_source.open(QIODevice::ReadWrite) ||
      !changed_source.setFileTime(changed_time, QFileDevice::FileModificationTime))
    return fail("Cannot advance the changed media modification time");
  changed_source.close();
  const QFileInfo changed_info(video_path);
  if (changed_info.size() == original_size && changed_info.lastModified() == original_modified)
    return fail("Changed video did not produce a distinct source revision");
  QElapsedTimer invalidation_timer;
  invalidation_timer.start();
  model.requestImportFileDetails(video_path);
  if (!wait_until([&] {
        const auto details = model.importFileDetails();
        return details.value("path").toString() == video_path &&
            !details.value("loading").toBool();
      }, 30000))
    return fail("Changed-revision real-video details probe exceeded its bound");
  const auto invalidation_ms = static_cast<double>(invalidation_timer.nsecsElapsed()) / 1.0e6;
  const auto changed_details = model.importFileDetails();
  if (changed_details.value("durationText").toString().isEmpty() ||
      changed_details.value("durationText").toString() == original_duration)
    return fail("Changed source revision reused stale video details");
  std::sort(warm_samples_ms.begin(), warm_samples_ms.end());
  std::cout << QJsonDocument(QJsonObject{
      {"scenario", "import-selected-video-details-cold-and-repeat"},
      {"storage", "real ffmpeg-generated local QTemporaryDir video; OS page-cache state unspecified"},
      {"ffprobe", ffprobe},
      {"video_bytes", QFileInfo(video_path).size()},
      {"cold_request_to_result_ms", cold_ms},
      {"warm_request_to_result_samples_ms", raw_warm_samples_ms},
      {"warm_p50_ms", warm_samples_ms.at(2)},
      {"warm_p95_ms", warm_samples_ms.last()},
      {"revision_invalidation_ms", invalidation_ms},
      {"original_duration", original_duration},
      {"changed_revision_duration", changed_details.value("durationText").toString()},
      {"revision_invalidation_verified", true},
      {"heartbeat_ticks", heartbeat_count},
      {"max_heartbeat_gap_ms", static_cast<double>(max_heartbeat_gap_ns) / 1.0e6},
      {"exact_details_parity", exact_details_parity},
      {"fixture_generation_excluded", true},
      {"process_peak_rss_bytes", process_peak_rss_bytes()},
  }).toJson(QJsonDocument::Compact).constData() << '\n';
  return 0;
}

int run_import_job_poll_shutdown_child(const QString& root_path, quint16 database_port) {
  const QDir root(root_path);
  const auto helper_root = root.filePath("helper-root");
  const auto helper_directory = QDir(helper_root).filePath("rust/target/debug");
  const auto helper_path = QDir(helper_directory).filePath("catalog_export_jobs");
  const auto data_directory = root.filePath("data");
  if (!QDir().mkpath(helper_directory) || !QDir().mkpath(data_directory) ||
      !write_bytes(QDir(helper_root).filePath("rust/Cargo.toml"), QByteArray{}) ||
      !write_bytes(helper_path,
          "#!/bin/sh\n"
          "set -eu\n"
          "[ \"$1\" = list ]\n"
          "printf 'finished\\n' > \"$PHOTO_EDITOR_APP_DATA_DIR/poll-helper-finished\"\n"
          "printf '[]\\n'\n") ||
      !QFile::setPermissions(helper_path,
          QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner))
    return fail("Cannot prepare the shutdown poll helper");

  qputenv("PHOTO_EDITOR_APP_DATA_DIR", data_directory.toUtf8());
  qputenv("PHOTO_EDITOR_CACHE_DIR", root.filePath("cache").toUtf8());
  qputenv("PHOTO_EDITOR_MANAGED_MEDIA_DIR", root.filePath("library").toUtf8());
  qputenv("PHOTO_EDITOR_ORIGINALS_BACKUP_DIR", root.filePath("backup").toUtf8());
  qputenv("PHOTO_EDITOR_REPOSITORY_ROOT", helper_root.toUtf8());
  qputenv("PHOTO_EDITOR_DATABASE_URL",
          QStringLiteral("postgresql://127.0.0.1:%1/shutdown_fixture").arg(database_port).toUtf8());

  const auto gate_started_path = root.filePath("poll-gate-started");
  const auto release_gate_path = root.filePath("release-poll-gate");
  CatalogImportPreflightTestAccess::pollPool().start(
      [gate_started_path, release_gate_path] {
        (void)write_bytes(gate_started_path, "started");
        QElapsedTimer timer;
        timer.start();
        while (!QFileInfo::exists(release_gate_path) && timer.elapsed() < 10000)
          QThread::msleep(5);
      });
  if (!wait_until([&] { return QFileInfo::exists(gate_started_path); }))
    return fail("The shutdown poll gate did not start");

  QElapsedTimer model_destruction_timer;
  model_destruction_timer.start();
  {
    CatalogAssetModel model;
    CatalogImportPreflightTestAccess::requestImportJobPoll(model);
    if (!wait_until([&] { return CatalogImportPreflightTestAccess::importJobPollInFlight(model); }))
      return fail("The poll request was not queued behind the shutdown gate");
  }
  if (model_destruction_timer.elapsed() >= 100)
    return fail("Closing a model waited for its background job poll");
  if (!write_bytes(root.filePath("child-returning"), "returning"))
    return fail("Cannot signal the shutdown parent");
  return 0;
}

int run_import_job_poll_shutdown_contract(const QString& executable) {
  QTemporaryDir temporary;
  if (!temporary.isValid()) return fail("Shutdown fixture directory unavailable");

  QTcpServer database_listener;
  if (!database_listener.listen(QHostAddress::LocalHost, 0)) {
    std::cerr << "Skipping shutdown poll contract: loopback listen is unavailable: "
              << database_listener.errorString().toStdString() << '\n';
    return 77;
  }

  QProcess child;
  child.start(executable,
      {QStringLiteral("--import-poll-shutdown-child"), temporary.path(),
       QString::number(database_listener.serverPort())});
  const auto stop_child = [&child] {
    if (child.state() == QProcess::NotRunning) return;
    child.kill();
    (void)child.waitForFinished(1000);
  };
  if (!child.waitForStarted(5000)) {
    stop_child();
    return fail("Shutdown poll child did not start");
  }
  const QDir root(temporary.path());
  const auto gate_started_path = root.filePath("poll-gate-started");
  const auto release_gate_path = root.filePath("release-poll-gate");
  const auto returning_path = root.filePath("child-returning");
  const auto app_destroyed_path = root.filePath("core-destroyed");
  const auto helper_finished_path = root.filePath("data/poll-helper-finished");
  if (!wait_until([&] {
        return QFileInfo::exists(gate_started_path) || child.state() == QProcess::NotRunning;
      }) || !QFileInfo::exists(gate_started_path) ||
      !wait_until([&] {
        return QFileInfo::exists(returning_path) || child.state() == QProcess::NotRunning;
      }) || !QFileInfo::exists(returning_path)) {
    (void)write_bytes(release_gate_path, "release");
    stop_child();
    std::cerr << child.readAllStandardError().constData();
    return fail("Shutdown poll child did not reach QCoreApplication teardown");
  }

  // The gate keeps the queued poll from running until the child has returned
  // from main and entered QCoreApplication destruction.
  if (child.waitForFinished(150)) {
    std::cerr << child.readAllStandardError().constData();
    return fail("The application exited before its queued import poll drained");
  }
  if (QFileInfo::exists(app_destroyed_path)) {
    (void)write_bytes(release_gate_path, "release");
    (void)child.waitForFinished(1000);
    stop_child();
    return fail("QCoreApplication destruction returned before its poll pool drained");
  }
  if (!write_bytes(release_gate_path, "release")) {
    stop_child();
    return fail("Cannot release the queued import poll");
  }
  if (!wait_until([&] {
        return QFileInfo::exists(helper_finished_path) || child.state() == QProcess::NotRunning;
      }) || !QFileInfo::exists(helper_finished_path) ||
      !child.waitForFinished(5000) || child.exitStatus() != QProcess::NormalExit ||
      child.exitCode() != 0 || !QFileInfo::exists(app_destroyed_path)) {
    std::cerr << child.readAllStandardError().constData();
    stop_child();
    return fail("The late poll did not finish before QCoreApplication destruction returned");
  }

  std::cout << "Late import poll completed its loopback socket preflight and helper read before QCoreApplication destruction returned\n";
  return 0;
}

class AfterCoreApplicationDestructionWitness final {
 public:
  explicit AfterCoreApplicationDestructionWitness(const QString& path)
      : encoded_path_(QFile::encodeName(path)) {}

  ~AfterCoreApplicationDestructionWitness() {
    std::ofstream marker(encoded_path_.constData(), std::ios::binary);
    marker << "destroyed\n";
  }

 private:
  QByteArray encoded_path_;
};

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
  const auto shutdown_child_mode = argc == 4 &&
      QString::fromLocal8Bit(argv[1]) == QStringLiteral("--import-poll-shutdown-child");
  std::unique_ptr<AfterCoreApplicationDestructionWitness> app_dtor_witness;
  if (shutdown_child_mode) {
    app_dtor_witness = std::make_unique<AfterCoreApplicationDestructionWitness>(
        QDir(QString::fromLocal8Bit(argv[2])).filePath("core-destroyed"));
  }
  QCoreApplication app(argc, argv);
  app.setOrganizationName("PhotoEditorTests");
  app.setApplicationName("CatalogImportPreflightTest");
  if (app.arguments().size() == 4 &&
      app.arguments().at(1) == QStringLiteral("--import-poll-shutdown-child")) {
    bool port_valid = false;
    const auto port = app.arguments().at(3).toUShort(&port_valid);
    if (!port_valid || port == 0) return fail("Invalid shutdown fixture database port");
    return run_import_job_poll_shutdown_child(app.arguments().at(2), port);
  }
  if (app.arguments().contains(QStringLiteral("--import-poll-shutdown-contract")))
    return run_import_job_poll_shutdown_contract(app.applicationFilePath());
  if (app.arguments().contains("--benchmark-cold-warm"))
    return run_real_file_preflight_benchmark();
  if (app.arguments().contains("--benchmark-import-details"))
    return run_import_file_details_benchmark();
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
