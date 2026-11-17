#include "app_bootstrap.h"
#include "export_job_controller.h"
#include "job_runner.h"
#include "job_store.h"
#include "observability.h"
#include "edit_recipe_state.h"

#include <QByteArrayView>
#include <QColorSpace>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSettings>
#include <QStringList>
#include <QThread>
#include <QThreadPool>
#include <QTimer>
#include <QVariantMap>

#include <mach/mach.h>
#include <mach/task_info.h>
#include <malloc/malloc.h>
#include <sys/resource.h>

#include <algorithm>
#include <functional>
#include <iostream>
#include <QHash>

namespace photo_editor::desktop {

struct ExportJobControllerTestAccess final {
  static QVariantMap jobById(const ExportJobController& controller, const QString& id) {
    for (const auto& job : controller.jobs_) {
      if (job.job_id == id) return controller.jobMap(job);
    }
    return {};
  }

  static QJsonObject retainedAccounting(const ExportJobController& controller) {
    qint64 payload_bytes = 0;
    for (const auto& job : controller.jobs_) {
      payload_bytes += job.job_id.toUtf8().size();
      payload_bytes += job.asset_id.toUtf8().size();
      payload_bytes += job.source_path.toUtf8().size();
      payload_bytes += job.recipe_json.toUtf8().size();
      payload_bytes += job.output_path.toUtf8().size();
      payload_bytes += QJsonDocument(job.metadata_json).toJson(QJsonDocument::Compact).size();
    }
    for (auto it = controller.unconfirmed_jobs_.cbegin();
         it != controller.unconfirmed_jobs_.cend(); ++it) {
      const auto& job = it.value();
      payload_bytes += job.job_id.toUtf8().size();
      payload_bytes += job.asset_id.toUtf8().size();
      payload_bytes += job.source_path.toUtf8().size();
      payload_bytes += job.recipe_json.toUtf8().size();
      payload_bytes += job.output_path.toUtf8().size();
      payload_bytes += QJsonDocument(job.metadata_json).toJson(QJsonDocument::Compact).size();
    }
    return {
        {QStringLiteral("visibleRows"), static_cast<int>(controller.jobs_.size())},
        {QStringLiteral("unconfirmedRows"), static_cast<int>(controller.unconfirmed_jobs_.size())},
        {QStringLiteral("admittingRows"), static_cast<int>(controller.admitting_ids_.size())},
        {QStringLiteral("retainedJobPayloadBytesEstimate"), payload_bytes},
    };
  }
};

struct JobRunnerTestAccess final {
  static qsizetype activeAttemptCount(const JobRunner& runner) {
    return runner.active_attempts_.size();
  }
};

}  // namespace photo_editor::desktop

namespace {

using namespace photo_editor::desktop;

bool wait_until(const std::function<bool()>& ready, int timeout_ms) {
  QElapsedTimer timer;
  timer.start();
  while (!ready() && timer.elapsed() < timeout_ms) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    QThread::msleep(1);
  }
  return ready();
}

QByteArray sha256_file(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) return {};
  QCryptographicHash hash(QCryptographicHash::Sha256);
  while (!file.atEnd()) {
    const auto chunk = file.read(1024 * 1024);
    if (chunk.isEmpty() && file.error() != QFileDevice::NoError) return {};
    hash.addData(chunk);
  }
  return hash.result().toHex();
}

QByteArray decoded_pixel_sha256(const QString& path, QSize* size_out) {
  QImageReader reader(path);
  auto image = reader.read();
  if (image.isNull()) return {};
  if (size_out != nullptr) *size_out = image.size();
  image = image.convertToFormat(QImage::Format_RGBA8888);
  if (image.isNull()) return {};
  QCryptographicHash hash(QCryptographicHash::Sha256);
  for (int y = 0; y < image.height(); ++y) {
    hash.addData(QByteArrayView(reinterpret_cast<const char*>(image.constScanLine(y)), image.bytesPerLine()));
  }
  return hash.result().toHex();
}

bool write_source_image(
    const QString& path, int seed, QSize size = QSize(640, 480)) {
  QImage image(size, QImage::Format_RGB32);
  image.setColorSpace(QColorSpace(QColorSpace::SRgb));
  for (int y = 0; y < image.height(); ++y) {
    auto* row = reinterpret_cast<QRgb*>(image.scanLine(y));
    for (int x = 0; x < image.width(); ++x) {
      row[x] = qRgb((x * 13 + y * 7 + seed * 23) & 255,
                    (x * 5 + y * 17 + seed * 41) & 255,
                    (x * 19 + y * 3 + seed * 67) & 255);
    }
  }
  return image.save(path, "PNG");
}

int hidden_partial_count(const QString& root) {
  int count = 0;
  QDirIterator iterator(root, QDir::Files | QDir::Hidden | QDir::System,
                        QDirIterator::Subdirectories);
  while (iterator.hasNext()) {
    iterator.next();
    if (iterator.fileName().contains(QStringLiteral(".partial-"))) ++count;
  }
  return count;
}

QJsonObject memory_snapshot() {
  QJsonObject result;
  mach_task_basic_info_data_t resident_info{};
  mach_msg_type_number_t resident_count = MACH_TASK_BASIC_INFO_COUNT;
  if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO,
                reinterpret_cast<task_info_t>(&resident_info),
                &resident_count) == KERN_SUCCESS) {
    result.insert(QStringLiteral("rssBytes"),
                  static_cast<qint64>(resident_info.resident_size));
  }
  malloc_statistics_t allocation{};
  malloc_zone_statistics(nullptr, &allocation);
  result.insert(QStringLiteral("mallocLiveBytes"),
                static_cast<qint64>(allocation.size_in_use));
  result.insert(QStringLiteral("mallocReservedBytes"),
                static_cast<qint64>(allocation.size_allocated));
  result.insert(QStringLiteral("mallocLiveBlocks"),
                static_cast<qint64>(allocation.blocks_in_use));
  task_vm_info_data_t vm_info{};
  mach_msg_type_number_t vm_count = TASK_VM_INFO_COUNT;
  if (task_info(mach_task_self(), TASK_VM_INFO,
                reinterpret_cast<task_info_t>(&vm_info), &vm_count) == KERN_SUCCESS) {
    result.insert(QStringLiteral("physicalFootprintBytes"),
                  static_cast<qint64>(vm_info.phys_footprint));
    result.insert(QStringLiteral("mediaFootprintBytes"),
                  static_cast<qint64>(vm_info.ledger_tag_media_footprint));
    result.insert(QStringLiteral("graphicsFootprintBytes"),
                  static_cast<qint64>(vm_info.ledger_tag_graphics_footprint));
  }
  thread_act_array_t threads = nullptr;
  mach_msg_type_number_t thread_count = 0;
  if (task_threads(mach_task_self(), &threads, &thread_count) == KERN_SUCCESS) {
    result.insert(QStringLiteral("threadCount"), static_cast<qint64>(thread_count));
    for (mach_msg_type_number_t index = 0; index < thread_count; ++index) {
      static_cast<void>(mach_port_deallocate(mach_task_self(), threads[index]));
    }
    static_cast<void>(vm_deallocate(
        mach_task_self(), reinterpret_cast<vm_address_t>(threads),
        static_cast<vm_size_t>(thread_count) * sizeof(thread_t)));
  }
  result.insert(QStringLiteral("globalPoolActiveThreads"),
                QThreadPool::globalInstance()->activeThreadCount());
  result.insert(QStringLiteral("globalPoolMaxThreads"),
                QThreadPool::globalInstance()->maxThreadCount());
  return result;
}

QString job_status(const ExportJobController& controller, const QString& id) {
  return ExportJobControllerTestAccess::jobById(controller, id)
      .value(QStringLiteral("status")).toString();
}

QJsonObject verify_output(const QVariantMap& job) {
  const auto path = job.value(QStringLiteral("outputPath")).toString();
  const auto metadata = job.value(QStringLiteral("metadata")).toMap();
  const auto integrity = metadata.value(QStringLiteral("outputIntegrity")).toMap();
  const auto sidecar_path = integrity.value(QStringLiteral("sidecarPath")).toString();
  QFile sidecar(sidecar_path);
  QJsonObject manifest;
  if (sidecar.open(QIODevice::ReadOnly) && sidecar.size() <= 64 * 1024) {
    manifest = QJsonDocument::fromJson(sidecar.readAll()).object();
  }
  QSize output_size;
  const auto output_sha = sha256_file(path);
  const auto pixels_sha = decoded_pixel_sha256(path, &output_size);
  const auto sidecar_sha = QString::fromLatin1(output_sha);
  const auto passed =
      job.value(QStringLiteral("status")).toString() == QStringLiteral("Completed") &&
      !output_sha.isEmpty() && !pixels_sha.isEmpty() && output_size == QSize(640, 480) &&
      integrity.value(QStringLiteral("valid")).toBool() &&
      integrity.value(QStringLiteral("sha256")).toString().toLatin1() == output_sha &&
      QFileInfo(sidecar_path).isFile() &&
      manifest.value(QStringLiteral("valid")).toBool() &&
      manifest.value(QStringLiteral("sha256")).toString() == sidecar_sha &&
      manifest.value(QStringLiteral("outputPath")).toString() == path;
  return {
      {QStringLiteral("passed"), passed},
      {QStringLiteral("outputPath"), path},
      {QStringLiteral("outputSha256"), QString::fromLatin1(output_sha)},
      {QStringLiteral("decodedPixelSha256"), QString::fromLatin1(pixels_sha)},
      {QStringLiteral("width"), output_size.width()},
      {QStringLiteral("height"), output_size.height()},
      {QStringLiteral("sidecarPath"), sidecar_path},
      {QStringLiteral("sidecarShaMatchesOutput"),
       manifest.value(QStringLiteral("sha256")).toString() == sidecar_sha},
  };
}

QJsonArray persisted_job_ids() {
  QJsonArray ids;
  const auto records = loadPersistedJobRecords();
  for (const auto& record : records) ids.append(record.job_id);
  return ids;
}

bool write_receipt(const QString& path, const QJsonObject& receipt) {
  QSaveFile file(path);
  if (!file.open(QIODevice::WriteOnly)) return false;
  const auto bytes = QJsonDocument(receipt).toJson(QJsonDocument::Indented);
  return file.write(bytes) == bytes.size() && file.commit();
}

}  // namespace

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));
  QGuiApplication app(argc, argv);
  const auto private_root = qEnvironmentVariable("PHOTO_EDITOR_ROUND13_PRIVATE_ROOT");
  const auto repository_root = qEnvironmentVariable("PHOTO_EDITOR_REPOSITORY_ROOT");
  const auto database_url = qEnvironmentVariable("PHOTO_EDITOR_DATABASE_URL");
  const auto app_data_root = qEnvironmentVariable("PHOTO_EDITOR_APP_DATA_DIR");
  const auto cache_root = qEnvironmentVariable("PHOTO_EDITOR_CACHE_DIR");
  const auto helper = AppBootstrap::helperProgram(QStringLiteral("catalog_export_jobs"));
  if (!private_root.startsWith(QStringLiteral("/private/tmp/looksy-round13-exports-")) ||
      !QFileInfo(private_root).isDir() ||
      !database_url.contains(QStringLiteral("host=%1/socket").arg(private_root)) ||
      !app_data_root.startsWith(private_root + QDir::separator()) ||
      !cache_root.startsWith(private_root + QDir::separator()) ||
      repository_root.isEmpty() || !QFileInfo(helper).isExecutable() ||
      !QDir(private_root).exists(QStringLiteral("pg")) ||
      !QDir(private_root).exists(QStringLiteral("socket"))) {
    std::cerr << "Requires only the wrapper-owned Round13 private PostgreSQL socket and isolated runtime/cache directories.\n";
    return 77;
  }
  QSettings::setDefaultFormat(QSettings::IniFormat);
  QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                     QDir(private_root).filePath(QStringLiteral("preferences")));
  observability::initialize();

  const auto work_root = QDir(private_root).filePath(QStringLiteral("sustained"));
  QDir(work_root).removeRecursively();
  const auto source_dir = QDir(work_root).filePath(QStringLiteral("sources"));
  const auto export_root = QDir(work_root).filePath(QStringLiteral("exports"));
  if (!QDir().mkpath(source_dir) || !QDir().mkpath(export_root)) return 1;

  QVariantList assets;
  QJsonArray sources;
  for (int index = 0; index < 4; ++index) {
    const auto source_path = QDir(source_dir).filePath(QStringLiteral("photo-%1.png").arg(index));
    if (!write_source_image(source_path, index + 1)) return 1;
    assets.push_back(QVariantMap{
        {QStringLiteral("assetId"), QStringLiteral("r13-photo-%1").arg(index)},
        {QStringLiteral("mediaType"), QStringLiteral("photo")},
        {QStringLiteral("filePath"), source_path},
        {QStringLiteral("displayLabel"), QStringLiteral("Photo-%1").arg(index)},
    });
    sources.append(QJsonObject{
        {QStringLiteral("path"), source_path},
        {QStringLiteral("sha256"), QString::fromLatin1(sha256_file(source_path))},
    });
  }

  JobRunner runner;
  ExportJobController controller;
  bool startup_finished = false;
  QString startup_error;
  QObject::connect(&runner, &JobRunner::startupRecoveryFinished, &app,
                   [&](const QString& error) {
                     startup_finished = true;
                     startup_error = error;
                   });
  runner.initializeFromStore();
  if (!wait_until([&] { return startup_finished; }, 30000) ||
      !startup_error.isEmpty()) {
    std::cerr << "JobRunner recovery failed: " << startup_error.toStdString() << '\n';
    return 1;
  }
  controller.initializeFromStore();
  if (!wait_until([&] { return !controller.loading(); }, 30000) ||
      controller.jobCount() != 0) {
    std::cerr << "Private catalog should begin with an empty export history.\n";
    return 1;
  }

  qsizetype peak_active_attempts = 0;
  int peak_global_pool_active_threads = 0;
  QTimer resource_sampler;
  resource_sampler.setInterval(10);
  QObject::connect(&resource_sampler, &QTimer::timeout, &app, [&] {
    peak_active_attempts = qMax(
        peak_active_attempts, JobRunnerTestAccess::activeAttemptCount(runner));
    peak_global_pool_active_threads = qMax(
        peak_global_pool_active_threads,
        QThreadPool::globalInstance()->activeThreadCount());
  });
  resource_sampler.start();

  QJsonArray batches;
  QJsonArray memory_samples;
  QHash<QString, QString> first_pixels_by_asset;
  memory_samples.append(QJsonObject{
      {QStringLiteral("phase"), QStringLiteral("initial")},
      {QStringLiteral("memory"), memory_snapshot()},
      {QStringLiteral("retained"), ExportJobControllerTestAccess::retainedAccounting(controller)},
      {QStringLiteral("activeAttempts"), JobRunnerTestAccess::activeAttemptCount(runner)},
  });

  for (int batch = 0; batch < 4; ++batch) {
    const auto destination = QDir(export_root).filePath(QStringLiteral("batch-%1").arg(batch));
    if (!QDir().mkpath(destination) ||
        controller.queueBatchExport(assets, QStringLiteral("Web Proof"), destination) != 4) {
      std::cerr << "Batch submission failed: " << controller.submissionError().toStdString() << '\n';
      return 1;
    }
    if (!wait_until([&] {
          return controller.pendingAdmissionCount() == 0 &&
                 controller.jobCount() == 4;
        }, 30000) || !controller.admissionError().isEmpty()) {
      std::cerr << "Batch admission failed: " << controller.admissionError().toStdString() << '\n';
      return 1;
    }

    QStringList job_ids;
    for (const auto& value : controller.jobs()) {
      const auto job = value.toMap();
      if (job.value(QStringLiteral("destinationDirectory")).toString() == destination) {
        job_ids.push_back(job.value(QStringLiteral("jobId")).toString());
      }
    }
    if (job_ids.size() != 4 ||
        !wait_until([&] {
          return std::all_of(job_ids.cbegin(), job_ids.cend(), [&](const QString& id) {
            const auto status = job_status(controller, id);
            return status == QStringLiteral("Completed") || status == QStringLiteral("Failed");
          });
        }, 90000)) {
      std::cerr << "Batch did not reach terminal status.\n";
      return 1;
    }

    QJsonArray batch_outputs;
    bool batch_ok = true;
    for (const auto& id : job_ids) {
      const auto job = ExportJobControllerTestAccess::jobById(controller, id);
      const auto verified = verify_output(job);
      const auto asset_id = job.value(QStringLiteral("assetId")).toString();
      const auto pixel_sha = verified.value(QStringLiteral("decodedPixelSha256")).toString();
      if (first_pixels_by_asset.contains(asset_id) &&
          first_pixels_by_asset.value(asset_id) != pixel_sha) {
        batch_ok = false;
      } else if (!first_pixels_by_asset.contains(asset_id)) {
        first_pixels_by_asset.insert(asset_id, pixel_sha);
      }
      batch_ok = batch_ok && verified.value(QStringLiteral("passed")).toBool();
      batch_outputs.append(QJsonObject{
          {QStringLiteral("jobId"), id},
          {QStringLiteral("status"), job.value(QStringLiteral("status")).toString()},
          {QStringLiteral("metadataBytes"),
           QJsonDocument(QJsonObject::fromVariantMap(job.value(QStringLiteral("metadata")).toMap()))
               .toJson(QJsonDocument::Compact).size()},
          {QStringLiteral("verification"), verified},
      });
    }
    if (!batch_ok) {
      std::cerr << "At least one batch export failed output or integrity verification.\n";
      return 1;
    }

    const auto before_clear = QJsonObject{
        {QStringLiteral("phase"), QStringLiteral("batch-%1-before-clear").arg(batch)},
        {QStringLiteral("memory"), memory_snapshot()},
        {QStringLiteral("retained"), ExportJobControllerTestAccess::retainedAccounting(controller)},
        {QStringLiteral("activeAttempts"), JobRunnerTestAccess::activeAttemptCount(runner)},
    };
    controller.clearFinishedJobs();
    if (!wait_until([&] { return !controller.loading() && controller.jobCount() == 0; }, 30000)) {
      std::cerr << "clearFinishedJobs did not remove the completed history rows.\n";
      return 1;
    }
    const auto records_after_clear = loadPersistedJobRecords();
    bool outputs_retained = true;
    for (const auto& output : batch_outputs) {
      const auto verify = output.toObject().value(QStringLiteral("verification")).toObject();
      const auto output_path = verify.value(QStringLiteral("outputPath")).toString();
      outputs_retained = outputs_retained &&
          sha256_file(output_path) == verify.value(QStringLiteral("outputSha256")).toString().toLatin1() &&
          QFileInfo(verify.value(QStringLiteral("sidecarPath")).toString()).isFile();
    }
    const auto after_clear = QJsonObject{
        {QStringLiteral("phase"), QStringLiteral("batch-%1-after-clear").arg(batch)},
        {QStringLiteral("memory"), memory_snapshot()},
        {QStringLiteral("retained"), ExportJobControllerTestAccess::retainedAccounting(controller)},
        {QStringLiteral("persistedRows"), static_cast<int>(records_after_clear.size())},
        {QStringLiteral("deliveredOutputsRetained"), outputs_retained},
        {QStringLiteral("partialFiles"), hidden_partial_count(work_root)},
        {QStringLiteral("activeAttempts"), JobRunnerTestAccess::activeAttemptCount(runner)},
    };
    if (!records_after_clear.isEmpty() || !outputs_retained ||
        hidden_partial_count(work_root) != 0 ||
        ExportJobControllerTestAccess::retainedAccounting(controller)
            .value(QStringLiteral("visibleRows")).toInt() != 0) {
      std::cerr << "Completed history cleanup must release rows while preserving exact delivered outputs.\n";
      return 1;
    }
    batches.append(QJsonObject{
        {QStringLiteral("batchIndex"), batch},
        {QStringLiteral("jobIds"), QJsonArray::fromStringList(job_ids)},
        {QStringLiteral("outputs"), batch_outputs},
        {QStringLiteral("beforeClear"), before_clear},
        {QStringLiteral("afterClear"), after_clear},
    });
    memory_samples.append(before_clear);
    memory_samples.append(after_clear);
  }

  // Deterministically exercise a cancellation after the final file has been
  // atomically published but before JobRunner processes its queued watcher.
  const auto cancel_source = QDir(source_dir).filePath(QStringLiteral("late-cancel.png"));
  if (!write_source_image(cancel_source, 29, QSize(2048, 2048))) return 1;
  EditRecipeState cancel_state;
  cancel_state.clarity = 80.0;
  cancel_state.dehaze = 60.0;
  cancel_state.sharpening = 100.0;
  cancel_state.sharpen_radius = 3.0;
  const auto cancel_destination = QDir(export_root).filePath(QStringLiteral("late-cancel"));
  if (!QDir().mkpath(cancel_destination) ||
      !controller.queueExport(QStringLiteral("round13-late-cancel"),
                              QStringLiteral("photo"),
                              QStringLiteral("Late Cancel"), cancel_source, {},
                              serialize_edit_recipe_json(cancel_state),
                              QStringLiteral("Web Proof"), cancel_destination)) {
    std::cerr << "Late-cancel export could not be queued.\n";
    return 1;
  }
  QString cancel_job_id;
  QString cancel_output_path;
  if (!wait_until([&] {
        for (const auto& value : controller.jobs()) {
          const auto job = value.toMap();
          if (job.value(QStringLiteral("assetId")).toString() ==
              QStringLiteral("round13-late-cancel")) {
            cancel_job_id = job.value(QStringLiteral("jobId")).toString();
            cancel_output_path = job.value(QStringLiteral("outputPath")).toString();
            return !cancel_job_id.isEmpty() && !cancel_output_path.isEmpty();
          }
        }
        return false;
      }, 30000) ||
      !wait_until([&] {
        return JobRunnerTestAccess::activeAttemptCount(runner) > 0 &&
               job_status(controller, cancel_job_id) == QStringLiteral("Running");
      }, 30000)) {
    std::cerr << "Late-cancel export did not enter a real running attempt.\n";
    return 1;
  }

  QElapsedTimer publication_wait;
  publication_wait.start();
  const auto cancel_sidecar_path = cancel_output_path + QStringLiteral(".integrity.json");
  while ((!QFileInfo(cancel_output_path).isFile() ||
          !QFileInfo(cancel_sidecar_path).isFile()) &&
         publication_wait.elapsed() < 90000) {
    QThread::msleep(1);
  }
  if (!QFileInfo(cancel_output_path).isFile() ||
      !QFileInfo(cancel_sidecar_path).isFile()) {
    std::cerr << "Late-cancel export did not publish both final output and integrity manifest.\n";
    return 1;
  }
  const auto published_output_sha = sha256_file(cancel_output_path);
  QFile published_sidecar(cancel_sidecar_path);
  QJsonObject published_manifest;
  if (published_sidecar.open(QIODevice::ReadOnly) && published_sidecar.size() <= 64 * 1024) {
    published_manifest = QJsonDocument::fromJson(published_sidecar.readAll()).object();
  }
  const auto complete_pair_was_verified =
      !published_output_sha.isEmpty() &&
      published_manifest.value(QStringLiteral("valid")).toBool() &&
      published_manifest.value(QStringLiteral("sha256")).toString() ==
          QString::fromLatin1(published_output_sha) &&
      published_manifest.value(QStringLiteral("outputPath")).toString() == cancel_output_path;
  if (!complete_pair_was_verified) {
    std::cerr << "Late-cancel fixture must verify the published output and sidecar pair before cancellation.\n";
    return 1;
  }
  QString cancellation_error;
  QElapsedTimer cancel_api_timer;
  cancel_api_timer.start();
  const auto cancel_requested = cancelPersistedJobRecord(cancel_job_id, &cancellation_error);
  const auto cancel_api_elapsed_ms = cancel_api_timer.elapsed();
  if (!cancel_requested) {
    std::cerr << "Late cancel request failed: " << cancellation_error.toStdString() << '\n';
    return 1;
  }
  if (!wait_until([&] {
        return job_status(controller, cancel_job_id) == QStringLiteral("Canceled") &&
               JobRunnerTestAccess::activeAttemptCount(runner) == 0;
      }, 30000)) {
    std::cerr << "Late-published cancellation did not reach a terminal canceled state.\n";
    return 1;
  }
  const auto cancel_job = ExportJobControllerTestAccess::jobById(controller, cancel_job_id);
  const auto canceled_integrity = cancel_job.value(QStringLiteral("metadata")).toMap()
      .value(QStringLiteral("outputIntegrity")).toMap();
  const auto cancel_output_removed = !QFileInfo::exists(cancel_output_path);
  const auto cancel_sidecar_removed = !QFileInfo::exists(cancel_sidecar_path);
  const bool late_cancel_passed = !(published_output_sha.isEmpty() || !cancel_output_removed || !cancel_sidecar_removed ||
      canceled_integrity.value(QStringLiteral("valid")).toBool() ||
      !canceled_integrity.value(QStringLiteral("sidecarPath")).toString().isEmpty() ||
      !canceled_integrity.value(QStringLiteral("failureReason")).toString().contains(
          QStringLiteral("revoked because the job was canceled")) ||
      hidden_partial_count(work_root) != 0);
  if (!late_cancel_passed)
    std::cerr << "Late cancel must remove only the just-published attempt output and sidecar.\n";
  const auto cancel_receipt = QJsonObject{
      {QStringLiteral("jobId"), cancel_job_id},
      {QStringLiteral("status"), cancel_job.value(QStringLiteral("status")).toString()},
      {QStringLiteral("finalOutputShaBeforeCancellation"), QString::fromLatin1(published_output_sha)},
      {QStringLiteral("completePairVerifiedBeforeCancellation"), complete_pair_was_verified},
      {QStringLiteral("waitForPublishedOutputMs"), publication_wait.elapsed()},
      {QStringLiteral("cancelApiElapsedMs"), cancel_api_elapsed_ms},
      {QStringLiteral("outputIntegrityInvalidated"),
       !canceled_integrity.value(QStringLiteral("valid")).toBool() &&
           canceled_integrity.value(QStringLiteral("sidecarPath")).toString().isEmpty()},
      {QStringLiteral("outputIntegrityFailureReason"),
       canceled_integrity.value(QStringLiteral("failureReason")).toString()},
      {QStringLiteral("outputRemoved"), cancel_output_removed},
      {QStringLiteral("sidecarRemoved"), cancel_sidecar_removed},
      {QStringLiteral("partialFilesAfterCancel"), hidden_partial_count(work_root)},
  };
  controller.clearFinishedJobs();
  if (!wait_until([&] { return !controller.loading() && controller.jobCount() == 0; }, 30000) ||
      !loadPersistedJobRecords().isEmpty()) {
    std::cerr << "Cancellation history row was not removed by clearFinishedJobs.\n";
    return 1;
  }
  resource_sampler.stop();

  memory_samples.append(QJsonObject{
      {QStringLiteral("phase"), QStringLiteral("final-after-cancel-clear")},
      {QStringLiteral("memory"), memory_snapshot()},
      {QStringLiteral("retained"), ExportJobControllerTestAccess::retainedAccounting(controller)},
      {QStringLiteral("activeAttempts"), JobRunnerTestAccess::activeAttemptCount(runner)},
  });
  const auto receipt = QJsonObject{
      {QStringLiteral("schema"), QStringLiteral("round13-export-sustained-batch:v1")},
      {QStringLiteral("qualification"), QStringLiteral(
          "Real ExportJobController batch submission and JobRunner processing against a private disposable PostgreSQL catalog; four repeated four-photo Web Proof batches; late published-file cancellation while the GUI completion callback is held; windowless QGuiApplication. Synthetic deterministic PNG photos; no throughput/concurrency claim." )},
      {QStringLiteral("batchCount"), static_cast<int>(batches.size())},
      {QStringLiteral("photosPerBatch"), static_cast<int>(assets.size())},
      {QStringLiteral("sourceManifest"), sources},
      {QStringLiteral("batches"), batches},
      {QStringLiteral("cancelAfterPublish"), cancel_receipt},
      {QStringLiteral("lateCancelContractPassed"), late_cancel_passed},
      {QStringLiteral("memorySamples"), memory_samples},
      {QStringLiteral("finalPersistedJobIds"), persisted_job_ids()},
      {QStringLiteral("hiddenPartialFilesFinal"), hidden_partial_count(work_root)},
      {QStringLiteral("finalRetained"), ExportJobControllerTestAccess::retainedAccounting(controller)},
      {QStringLiteral("peakActiveRunnerAttempts"), static_cast<qint64>(peak_active_attempts)},
      {QStringLiteral("peakGlobalPoolActiveThreads"), peak_global_pool_active_threads},
      {QStringLiteral("privateRoot"), private_root},
  };
  const auto receipt_path = qEnvironmentVariable(
      "PHOTO_EDITOR_ROUND13_EXPORT_RECEIPT",
      QDir(private_root).filePath(QStringLiteral("sustained-batch.json")));
  if (!write_receipt(receipt_path, receipt)) return 1;
  std::cout << QJsonDocument(receipt).toJson(QJsonDocument::Compact).toStdString() << '\n';
  return late_cancel_passed ? 0 : 1;
}
