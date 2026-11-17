#include "app_bootstrap.h"
#include "export_job_controller.h"
#include "job_runner.h"
#include "job_store.h"
#include "observability.h"

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
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSettings>
#include <QThread>
#include <QVariantMap>

#include <functional>
#include <iostream>

namespace photo_editor::desktop {

struct ExportJobControllerTestAccess final {
  static QVariantMap jobById(const ExportJobController& controller, const QString& id) {
    for (const auto& job : controller.jobs_) {
      if (job.job_id != id) continue;
      return controller.jobMap(job);
    }
    return {};
  }
};

}  // namespace photo_editor::desktop

namespace {

using namespace photo_editor::desktop;

bool wait_until(const std::function<bool()>& ready, int timeout_ms) {
  QElapsedTimer timer;
  timer.start();
  while (!ready() && timer.elapsed() < timeout_ms) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    QThread::msleep(1);
  }
  return ready();
}

bool write_file(const QString& path, const QByteArray& contents) {
  QSaveFile file(path);
  return file.open(QIODevice::WriteOnly) &&
         file.write(contents) == contents.size() && file.commit();
}

QByteArray sha256_file(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) return {};
  QCryptographicHash hash(QCryptographicHash::Sha256);
  while (!file.atEnd()) {
    const auto bytes = file.read(1024 * 1024);
    if (bytes.isEmpty() && file.error() != QFileDevice::NoError) return {};
    hash.addData(bytes);
  }
  return hash.result().toHex();
}

bool write_source_image(const QString& path) {
  QImage image(1280, 960, QImage::Format_RGB32);
  image.setColorSpace(QColorSpace(QColorSpace::SRgb));
  for (int y = 0; y < image.height(); ++y) {
    auto* row = reinterpret_cast<QRgb*>(image.scanLine(y));
    for (int x = 0; x < image.width(); ++x) {
      row[x] = qRgb(
          (x * 17 + y * 3 + 19) & 255,
          (x * 5 + y * 13 + 71) & 255,
          (x * 11 + y * 7 + 137) & 255);
    }
  }
  return image.save(path, "PNG");
}

QString job_status(const ExportJobController& controller, const QString& id) {
  return ExportJobControllerTestAccess::jobById(controller, id)
      .value(QStringLiteral("status")).toString();
}

int hidden_partial_count(const QString& destination) {
  int count = 0;
  QDirIterator iterator(
      destination,
      QDir::Files | QDir::Hidden | QDir::System,
      QDirIterator::Subdirectories);
  while (iterator.hasNext()) {
    iterator.next();
    if (iterator.fileName().contains(QStringLiteral(".partial-"))) ++count;
  }
  return count;
}

QJsonObject receipt_for(
    const QString& private_root,
    const QString& preferred_path,
    const QString& output_path,
    const QString& source_path,
    const QString& job_id,
    const QVariantMap& job,
    const QString& sentinel_path) {
  const auto metadata = job.value(QStringLiteral("metadata")).toMap();
  const auto integrity = metadata.value(QStringLiteral("outputIntegrity")).toMap();
  const auto sidecar_path = integrity.value(QStringLiteral("sidecarPath")).toString();
  const auto output_exists = QFileInfo::exists(output_path);
  const auto sidecar_file_exists =
      QFileInfo(output_path + QStringLiteral(".integrity.json")).isFile();
  const auto sidecar_directory_preserved =
      QFileInfo(output_path + QStringLiteral(".integrity.json")).isDir();
  const auto partial_count = hidden_partial_count(QFileInfo(output_path).absolutePath());
  QFile sentinel(sentinel_path);
  const auto sentinel_preserved = sentinel.open(QIODevice::ReadOnly) &&
      sentinel.readAll() == QByteArrayLiteral("pre-existing-final-output");
  const auto integrity_contract_passed =
      job.value(QStringLiteral("status")).toString() == QStringLiteral("Failed") &&
      !output_exists && !integrity.value(QStringLiteral("valid")).toBool() &&
      sidecar_path.isEmpty() && !sidecar_file_exists &&
      sidecar_directory_preserved && partial_count == 0 &&
      sentinel_preserved;
  return {
      {QStringLiteral("schema"), QStringLiteral("round13-export-sidecar-failure:v1")},
      {QStringLiteral("qualification"), QStringLiteral(
          "real JobRunner asset export with a private disposable PostgreSQL catalog; synthetic deterministic PNG input; windowless QGuiApplication")},
      {QStringLiteral("jobId"), job_id},
      {QStringLiteral("status"), job.value(QStringLiteral("status")).toString()},
      {QStringLiteral("errorMessage"), job.value(QStringLiteral("errorMessage")).toString()},
      {QStringLiteral("sourceSha256"), QString::fromLatin1(sha256_file(source_path))},
      {QStringLiteral("preferredOutputPath"), preferred_path},
      {QStringLiteral("preferredOutputPreserved"), QFileInfo::exists(preferred_path) && sentinel_preserved},
      {QStringLiteral("outputPath"), output_path},
      {QStringLiteral("outputExists"), output_exists},
      {QStringLiteral("outputSha256"), QString::fromLatin1(sha256_file(output_path))},
      {QStringLiteral("sidecarDirectoryPreserved"), sidecar_directory_preserved},
      {QStringLiteral("integrityMetadataValid"), integrity.value(QStringLiteral("valid")).toBool()},
      {QStringLiteral("integrityFailureReason"), integrity.value(QStringLiteral("failureReason")).toString()},
      {QStringLiteral("integritySidecarPath"), sidecar_path},
      {QStringLiteral("integritySidecarFileExists"), sidecar_file_exists},
      {QStringLiteral("hiddenPartialCount"), partial_count},
      {QStringLiteral("integritySidecarContractPassed"), integrity_contract_passed},
      {QStringLiteral("preexistingSentinelPreserved"), sentinel_preserved},
      {QStringLiteral("privateRoot"), private_root},
  };
}

}  // namespace

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));
  QGuiApplication app(argc, argv);

  const auto private_root = qEnvironmentVariable("PHOTO_EDITOR_ROUND13_PRIVATE_ROOT");
  const auto app_data_root = qEnvironmentVariable("PHOTO_EDITOR_APP_DATA_DIR");
  const auto cache_root = qEnvironmentVariable("PHOTO_EDITOR_CACHE_DIR");
  const auto repository_root = qEnvironmentVariable("PHOTO_EDITOR_REPOSITORY_ROOT");
  const auto database_url = qEnvironmentVariable("PHOTO_EDITOR_DATABASE_URL");
  const auto helper = AppBootstrap::helperProgram(QStringLiteral("catalog_export_jobs"));
  const auto app_data_is_private = app_data_root == private_root ||
      app_data_root.startsWith(private_root + QDir::separator());
  const auto cache_is_private = cache_root == private_root ||
      cache_root.startsWith(private_root + QDir::separator());
  if (!private_root.startsWith(QStringLiteral("/private/tmp/looksy-round13-exports-")) ||
      !QFileInfo(private_root).isDir() ||
      !database_url.contains(QStringLiteral("host=%1/socket").arg(private_root)) ||
      !app_data_is_private || !cache_is_private ||
      repository_root.isEmpty() ||
      !QDir(private_root).exists(QStringLiteral("pg")) ||
      !QDir(private_root).exists(QStringLiteral("socket")) ||
      !QFileInfo(helper).isFile()) {
    std::cerr << "Requires the wrapper-owned round13 private PostgreSQL Unix socket, isolated app/cache directories, and catalog_export_jobs helper.\n";
    return 77;
  }
  QSettings::setDefaultFormat(QSettings::IniFormat);
  QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                     QDir(private_root).filePath(QStringLiteral("preferences")));
  observability::initialize();

  const auto workload_root = QDir(private_root).filePath(QStringLiteral("sidecar-failure"));
  QDir(workload_root).removeRecursively();
  const auto source_dir = QDir(workload_root).filePath(QStringLiteral("source"));
  const auto destination = QDir(workload_root).filePath(QStringLiteral("exports"));
  if (!QDir().mkpath(source_dir) || !QDir().mkpath(destination)) return 1;
  const auto source_path = QDir(source_dir).filePath(QStringLiteral("r13-sidecar-fault.png"));
  if (!write_source_image(source_path)) return 1;

  ExportJobController controller;
  const auto preflight = controller.exportPreflight(
      QStringLiteral("photo"), source_path, {}, {}, QStringLiteral("Web Proof"),
      destination, 0, 0, 0, 0, 0, 0);
  const auto preferred_path = preflight.value(QStringLiteral("outputPath")).toString();
  if (!preflight.value(QStringLiteral("ok")).toBool() || preferred_path.isEmpty() ||
      !write_file(preferred_path, QByteArrayLiteral("pre-existing-final-output"))) {
    std::cerr << "Could not prepare the deterministic preferred-path collision.\n";
    return 1;
  }

  if (!controller.queueExport(
          QStringLiteral("round13-sidecar-fault"), QStringLiteral("photo"), {},
          source_path, {}, {}, QStringLiteral("Web Proof"), destination)) {
    std::cerr << "Could not queue sidecar-failure export: "
              << controller.submissionError().toStdString() << '\n';
    return 1;
  }
  const auto saving_job = controller.jobs().last().toMap();
  const auto job_id = saving_job.value(QStringLiteral("jobId")).toString();
  const auto output_path = saving_job.value(QStringLiteral("outputPath")).toString();
  if (job_id.isEmpty() || output_path.isEmpty() || output_path == preferred_path ||
      QFileInfo::exists(output_path) ||
      !QDir().mkpath(output_path + QStringLiteral(".integrity.json")) ||
      !wait_until([&] { return controller.pendingAdmissionCount() == 0; }, 30000)) {
    std::cerr << "Could not prepare queued collision-resolved sidecar fault.\n";
    return 1;
  }

  JobRunner runner;
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
    std::cerr << "Private JobRunner recovery failed: " << startup_error.toStdString() << '\n';
    return 1;
  }
  controller.initializeFromStore();
  if (!wait_until([&] {
        const auto status = job_status(controller, job_id);
        return status == QStringLiteral("Completed") || status == QStringLiteral("Failed");
      }, 90000)) {
    std::cerr << "The export did not reach a terminal job state.\n";
    return 1;
  }

  const auto job = ExportJobControllerTestAccess::jobById(controller, job_id);
  const auto receipt = receipt_for(
      private_root, preferred_path, output_path, source_path, job_id, job,
      preferred_path);
  const auto receipt_path = qEnvironmentVariable(
      "PHOTO_EDITOR_ROUND13_EXPORT_RECEIPT",
      QDir(private_root).filePath(QStringLiteral("sidecar-failure.json")));
  QSaveFile receipt_file(receipt_path);
  if (!receipt_file.open(QIODevice::WriteOnly)) return 1;
  const auto receipt_bytes = QJsonDocument(receipt).toJson(QJsonDocument::Indented);
  if (receipt_file.write(receipt_bytes) != receipt_bytes.size() || !receipt_file.commit()) return 1;
  std::cout << QJsonDocument(receipt).toJson(QJsonDocument::Compact).toStdString() << '\n';

  const auto require_repair = qEnvironmentVariableIsSet(
      "PHOTO_EDITOR_ROUND13_REQUIRE_SIDECAR_REPAIR");
  if (require_repair &&
      !receipt.value(QStringLiteral("integritySidecarContractPassed")).toBool()) {
    std::cerr << "Verified delivery must fail without its sidecar, preserve the preferred existing file, and clean its stage.\n";
    return 1;
  }
  return 0;
}
