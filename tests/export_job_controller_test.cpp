#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include <QCoreApplication>
#include <QColorSpace>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QThread>
#include <QProcess>
#include <QProcessEnvironment>
#include <QThreadPool>
#include <QtConcurrent>

#include "edit_recipe_state.h"
#include "app_bootstrap.h"
#include "edit_adjustment_patch.h"
#include "bounded_qimage_reader.h"
#include "export_job_controller.h"
#include "job_runner.h"
#include "job_store.h"
#include "worker_queue_registry.h"
#include "photo_render_utils.h"
#include "photo_editor/io/openexr_io.h"
#include "photo_editor/io/hdr_avif_io.h"
#include "photo_editor/pipeline/color_profile_asset.h"
#include "photo_editor/pipeline/color_transform.h"
#include "photo_editor/pipeline/output_rendering.h"

namespace photo_editor::desktop {
struct ExportJobControllerTestAccess {
  static void useSynchronousNotificationBaseline(ExportJobController& controller) {
    QObject::disconnect(jobStoreNotifier(), nullptr, &controller, nullptr);
    QObject::connect(jobStoreNotifier(), &JobStoreNotifier::jobsChanged, &controller,
        [&controller] { controller.loadPersistedJobs(); });
  }
};
}

namespace {

int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

bool wait_for_job(
    photo_editor::desktop::ExportJobController* controller,
    int index,
    int timeout_ms = 15000) {
  // This is a functional watchdog, not a realtime gate: cold native rendering
  // plus asynchronous history observation must both settle. The old 3-second
  // watchdog also fails with synchronous notification replay on this host.
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < timeout_ms) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    const auto jobs = controller->jobs();
    if (index >= 0 && index < jobs.size()) {
      const auto status = jobs.at(index).toMap().value(QStringLiteral("status")).toString();
      if (status == QStringLiteral("Completed") || status == QStringLiteral("Failed")) {
        return true;
      }
    }
  }
  return false;
}

bool wait_for_job_id(
    photo_editor::desktop::ExportJobController* controller,
    const QString& job_id,
    int timeout_ms = 5000) {
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < timeout_ms) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    const auto jobs = controller->jobs();
    for (const auto& value : jobs) {
      const auto job = value.toMap();
      if (job.value(QStringLiteral("jobId")).toString() != job_id) {
        continue;
      }
      const auto status = job.value(QStringLiteral("status")).toString();
      if (status == QStringLiteral("Completed") || status == QStringLiteral("Failed")) {
        return true;
      }
    }
  }
  return false;
}

bool wait_for_persisted_job_status(
    const QString& job_id,
    const QString& expected_status,
    int timeout_ms = 5000) {
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < timeout_ms) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
    const auto records =
        photo_editor::desktop::loadPersistedJobRecords();
    const auto record = std::find_if(
        records.cbegin(),
        records.cend(),
        [&](const auto& candidate) {
          return candidate.job_id == job_id;
        });
    if (record != records.cend() &&
        record->status == expected_status) {
      return true;
    }
  }
  return false;
}

bool issue_code_present(const QVariantList& issues, const QString& code) {
  for (const auto& value : issues) {
    if (value.toMap().value(QStringLiteral("code")).toString() == code) {
      return true;
    }
  }
  return false;
}

int issue_code_count(const QVariantList& issues, const QString& code) {
  auto count = 0;
  for (const auto& value : issues) {
    if (value.toMap().value(QStringLiteral("code")).toString() == code) {
      ++count;
    }
  }
  return count;
}

int max_channel_delta(const QImage& left, const QImage& right) {
  if (left.size() != right.size()) {
    return 255;
  }

  auto max_delta = 0;
  const auto left_argb = left.convertToFormat(QImage::Format_ARGB32);
  const auto right_argb = right.convertToFormat(QImage::Format_ARGB32);
  for (int y = 0; y < left_argb.height(); ++y) {
    for (int x = 0; x < left_argb.width(); ++x) {
      const auto lhs = left_argb.pixelColor(x, y);
      const auto rhs = right_argb.pixelColor(x, y);
      max_delta = std::max(max_delta, std::abs(lhs.red() - rhs.red()));
      max_delta = std::max(max_delta, std::abs(lhs.green() - rhs.green()));
      max_delta = std::max(max_delta, std::abs(lhs.blue() - rhs.blue()));
    }
  }
  return max_delta;
}

struct PfmImage final {
  int width = 0;
  int height = 0;
  std::vector<float> pixels;
};

bool read_pfm_image(const QString& path, PfmImage* image) {
  QFile file(path);
  if (image == nullptr || !file.open(QIODevice::ReadOnly)) {
    return false;
  }
  const auto data = file.readAll();
  qsizetype cursor = 0;
  const auto read_line = [&]() -> QByteArray {
    if (cursor >= data.size()) {
      return {};
    }
    const auto start = cursor;
    while (cursor < data.size() && data.at(cursor) != '\n') {
      ++cursor;
    }
    const auto line = data.mid(start, cursor - start);
    if (cursor < data.size() && data.at(cursor) == '\n') {
      ++cursor;
    }
    return line;
  };

  if (read_line().trimmed() != QByteArrayLiteral("PF")) {
    return false;
  }
  const auto dimensions = read_line().simplified().split(' ');
  if (dimensions.size() != 2) {
    return false;
  }
  const auto width = dimensions.at(0).toInt();
  const auto height = dimensions.at(1).toInt();
  const auto scale = read_line().trimmed().toFloat();
  if (width <= 0 || height <= 0 || scale >= 0.0F) {
    return false;
  }

  const auto expected_values = static_cast<qsizetype>(width) * height * 3;
  const auto expected_bytes = expected_values * static_cast<qsizetype>(sizeof(float));
  if (data.size() - cursor != expected_bytes) {
    return false;
  }

  image->width = width;
  image->height = height;
  image->pixels.resize(static_cast<std::size_t>(expected_values));
  for (qsizetype index = 0; index < expected_values; ++index) {
    float value = 0.0F;
    std::memcpy(
        &value,
        data.constData() + cursor + index * static_cast<qsizetype>(sizeof(float)),
        sizeof(float));
    image->pixels[static_cast<std::size_t>(index)] = value;
  }
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  QTemporaryDir temp_dir(QDir::tempPath() + QStringLiteral("/export-controller-proof-XXXXXX"));
  if (!temp_dir.isValid()) {
    return fail("temporary export test directory should be created");
  }

  qputenv("HOME", temp_dir.path().toUtf8());
  qputenv("XDG_DATA_HOME", temp_dir.filePath(QStringLiteral("data")).toUtf8());
  qputenv("XDG_CACHE_HOME", temp_dir.filePath(QStringLiteral("cache")).toUtf8());
  qputenv("PHOTO_EDITOR_APP_DATA_ROOT", temp_dir.filePath(QStringLiteral("app-data")).toUtf8());
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", temp_dir.filePath(QStringLiteral("app-data")).toUtf8());
  qputenv("PHOTO_EDITOR_CACHE_DIR", temp_dir.filePath(QStringLiteral("cache")).toUtf8());
  qunsetenv("PHOTO_EDITOR_DATABASE_URL");

  QCoreApplication app(argc, argv);
  QCoreApplication::setOrganizationName(QStringLiteral("PhotoEditorTests"));
  QCoreApplication::setApplicationName(QStringLiteral("ExportJobControllerTest"));
  if (app.arguments().contains(QStringLiteral("--keep-artifacts"))) temp_dir.setAutoRemove(false);
  QElapsedTimer suite_timer;
  suite_timer.start();
  const auto phase = [&](const char* name) {
    std::cerr << "export-proof phase=" << name << " elapsed_ms=" << suite_timer.elapsed() << '\n';
  };
  std::cerr << "export-proof root=" << temp_dir.path().toStdString() << '\n';
  struct StopOwnedTestCatalog {
    ~StopOwnedTestCatalog() {
      QThreadPool::globalInstance()->waitForDone(10000);
      QProcess process;
      auto environment = QProcessEnvironment::systemEnvironment();
      environment.remove(QStringLiteral("PHOTO_EDITOR_DATABASE_URL"));
      process.setProcessEnvironment(environment);
      process.start(photo_editor::desktop::AppBootstrap::helperProgram(QStringLiteral("catalog_local_db")),
          photo_editor::desktop::AppBootstrap::helperArguments(QStringLiteral("catalog_local_db"), {QStringLiteral("stop")}));
      if (!process.waitForFinished(10000)) { process.kill(); process.waitForFinished(1000); }
    }
  } stop_owned_test_catalog;

  const auto large_tiff_limit =
      photo_editor::desktop::
          bounded_qimage_allocation_limit_megabytes(
              QSize(7328, 5496), 8);
  const auto previous_allocation_limit =
      QImageReader::allocationLimit();
  {
    const photo_editor::desktop::
        ScopedQImageReaderAllocationLimit allocation_limit(
            QSize(7328, 5496), 8);
    if (!large_tiff_limit.has_value() ||
        *large_tiff_limit <= 256 ||
        !allocation_limit.valid() ||
        allocation_limit.required_limit_megabytes() !=
            *large_tiff_limit ||
        (previous_allocation_limit > 0 &&
         QImageReader::allocationLimit() <
             *large_tiff_limit)) {
      return fail(
          "large 16-bit export verification must receive a bounded allocation allowance");
    }
  }
  if (QImageReader::allocationLimit() !=
          previous_allocation_limit ||
      photo_editor::desktop::
          bounded_qimage_allocation_limit_megabytes(
              QSize(50000, 50000), 8)
          .has_value()) {
    return fail(
        "bounded image-reader allocation must restore global state and reject excessive decodes");
  }

  const auto source_path = temp_dir.filePath(QStringLiteral("source.png"));
  const auto export_dir = temp_dir.filePath(QStringLiteral("exports"));
  QDir().mkpath(export_dir);

  QImage source(80, 40, QImage::Format_ARGB32);
  source.fill(Qt::black);
  for (int y = 0; y < source.height(); ++y) {
    for (int x = 0; x < source.width(); ++x) {
      source.setPixelColor(x, y, x < 40 ? QColor(240, 32, 32) : QColor(24, 48, 240));
    }
  }
  if (!source.save(source_path)) {
    return fail("source image should be written for export testing");
  }

  photo_editor::desktop::EditRecipeState crop_state;
  crop_state.crop_rect_x = 0.5;
  crop_state.crop_rect_y = 0.0;
  crop_state.crop_rect_width = 0.5;
  crop_state.crop_rect_height = 1.0;
  crop_state.output_color_space = QStringLiteral("Display P3");
  const auto recipe_json = photo_editor::desktop::serialize_edit_recipe_json(crop_state);

  auto runner = std::make_unique<photo_editor::desktop::JobRunner>();
  photo_editor::desktop::ExportJobController controller;
  // Match app startup: history observation alone does not open dispatch. The
  // runner must finish durable recovery before this test creates new work.
  bool startup_finished = false;
  QString startup_error;
  QObject::connect(runner.get(), &photo_editor::desktop::JobRunner::startupRecoveryFinished,
      &app, [&](const QString& error) { startup_finished = true; startup_error = error; });
  runner->initializeFromStore();
  QElapsedTimer startup_timer;
  startup_timer.start();
  while (!startup_finished && startup_timer.elapsed() < 15000) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
    QThread::msleep(1);
  }
  if (!startup_finished || !startup_error.isEmpty()) {
    std::cerr << "Export test startup recovery: " << startup_error.toStdString() << '\n';
    return fail("export tests require successful runner startup recovery");
  }
  phase("startup-ready");
  if (app.arguments().contains(QStringLiteral("--sync-history-baseline")))
    photo_editor::desktop::ExportJobControllerTestAccess::useSynchronousNotificationBaseline(controller);
  controller.initializeFromStore();
  photo_editor::desktop::WorkerQueueRegistry::publishUserIntent(
      QStringLiteral("timeline"), true, true, true,
      {QStringLiteral("asset-preview-deferred")},
      QStringLiteral("asset-preview-deferred"));
  photo_editor::desktop::PersistentJobRecord deferred_preview_job;
  deferred_preview_job.job_id = QStringLiteral("intent-deferred-export-preview");
  deferred_preview_job.job_kind = QStringLiteral("asset_export");
  deferred_preview_job.asset_id = QStringLiteral("asset-preview-deferred");
  deferred_preview_job.media_type = QStringLiteral("photo");
  deferred_preview_job.display_label = QStringLiteral("Deferred Export Preview");
  deferred_preview_job.source_path = source_path;
  deferred_preview_job.preset_name = QStringLiteral("Original Copy");
  deferred_preview_job.destination_path = export_dir;
  deferred_preview_job.status = QStringLiteral("Queued");
  deferred_preview_job.metadata_json.insert(
      QStringLiteral("purpose"), QStringLiteral("export-preview"));
  deferred_preview_job.created_at =
      QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
  deferred_preview_job.updated_at = deferred_preview_job.created_at;
  if (!photo_editor::desktop::upsertPersistedJobRecord(
          deferred_preview_job, nullptr)) {
    return fail("export-preview priority fixture should enter the persistent queue");
  }
  QElapsedTimer deferred_preview_timer;
  deferred_preview_timer.start();
  while (deferred_preview_timer.elapsed() < 350) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
  }
  auto preview_remained_queued = false;
  for (const auto& job : photo_editor::desktop::loadPersistedJobRecords()) {
    if (job.job_id == deferred_preview_job.job_id) {
      preview_remained_queued = job.status == QStringLiteral("Queued");
      break;
    }
  }
  if (!preview_remained_queued) {
    return fail("persistent export-preview work should not start while Timeline playback or direct manipulation is active");
  }
  photo_editor::desktop::WorkerQueueRegistry::clearUserIntent(
      QStringLiteral("timeline"));
  runner->wake();
  if (!wait_for_job_id(&controller, deferred_preview_job.job_id, 5000)) {
    return fail("deferred export-preview work should resume automatically after Timeline interaction ends");
  }
  auto preview_completed_after_resume = false;
  for (const auto& value : controller.jobs()) {
    const auto job = value.toMap();
    if (job.value(QStringLiteral("jobId")).toString() == deferred_preview_job.job_id) {
      preview_completed_after_resume =
          job.value(QStringLiteral("status")).toString() == QStringLiteral("Completed");
      break;
    }
  }
  if (!preview_completed_after_resume) {
    return fail("resumed export-preview work should complete through the ordinary persistent runner");
  }
  controller.clearFinishedJobs();

  const auto cancellation_source_path =
      temp_dir.filePath(QStringLiteral("cancellation-source.png"));
  QImage cancellation_source(
      2048, 2048, QImage::Format_ARGB32);
  for (int y = 0; y < cancellation_source.height(); ++y) {
    auto* row = reinterpret_cast<QRgb*>(
        cancellation_source.scanLine(y));
    for (int x = 0; x < cancellation_source.width(); ++x) {
      row[x] = qRgb(
          (x * 13 + y * 7) & 255,
          (x * 3 + y * 17) & 255,
          (x * 19 + y * 5) & 255);
    }
  }
  if (!cancellation_source.save(cancellation_source_path)) {
    return fail("cooperative cancellation source should be written");
  }
  photo_editor::desktop::EditRecipeState cancellation_state;
  cancellation_state.clarity = 80.0;
  cancellation_state.dehaze = 60.0;
  cancellation_state.sharpening = 100.0;
  cancellation_state.sharpen_radius = 3.0;
  const auto cancellation_asset_id =
      QStringLiteral("cooperative-export-cancellation");
  if (!controller.queueExport(
          cancellation_asset_id,
          QStringLiteral("photo"),
          QStringLiteral("Cancel Cooperative Export"),
          cancellation_source_path,
          QString(),
          photo_editor::desktop::serialize_edit_recipe_json(
              cancellation_state),
          QStringLiteral("PNG 16-bit"),
          export_dir)) {
    return fail("cooperative cancellation export should queue");
  }
  QString cancellation_job_id;
  for (const auto& value : controller.jobs()) {
    const auto job = value.toMap();
    if (job.value(QStringLiteral("assetId")).toString() ==
        cancellation_asset_id) {
      cancellation_job_id =
          job.value(QStringLiteral("jobId")).toString();
      break;
    }
  }
  if (cancellation_job_id.isEmpty()) {
    return fail("cooperative cancellation export should expose its job id");
  }
  if (!wait_for_persisted_job_status(
          cancellation_job_id,
          QStringLiteral("Running"),
          5000)) {
    return fail("cooperative cancellation export should enter Running");
  }
  QString cancellation_output_path;
  for (const auto& record :
       photo_editor::desktop::loadPersistedJobRecords()) {
    if (record.job_id == cancellation_job_id) {
      cancellation_output_path = record.output_path;
      break;
    }
  }
  if (!photo_editor::desktop::cancelPersistedJobRecord(
          cancellation_job_id, nullptr) ||
      !wait_for_persisted_job_status(
          cancellation_job_id,
          QStringLiteral("Canceled"),
          12000)) {
    return fail("live export cancellation should finish cooperatively");
  }
  if (cancellation_output_path.isEmpty() ||
      QFileInfo::exists(cancellation_output_path) ||
      QFileInfo::exists(
          cancellation_output_path +
          QStringLiteral(".integrity.json"))) {
    return fail("canceled export must not publish a delivery or integrity sidecar");
  }
  const auto partial_exports = QDir(export_dir).entryList(
      {QStringLiteral(".*.partial-*")},
      QDir::Files | QDir::Hidden);
  if (!partial_exports.isEmpty()) {
    return fail("canceled export must remove its hidden staging artifact");
  }
  controller.clearFinishedJobs();

  photo_editor::desktop::EditRecipeState preflight_state;
  preflight_state.output_color_space = QStringLiteral("Display P3");
  preflight_state.soft_proof_enabled = true;
  preflight_state.soft_proof_profile = QStringLiteral("sRGB");
  preflight_state.crop_rect_width = 0.5;
  preflight_state.crop_rect_height = 1.0;
  const auto preflight = controller.exportPreflight(
      QStringLiteral("photo"),
      source_path,
      QString(),
      photo_editor::desktop::serialize_edit_recipe_json(preflight_state),
      QStringLiteral("Web Proof"),
      export_dir,
      100,
      3,
      4,
      80,
      1,
      0);
  const auto preflight_issues = preflight.value(QStringLiteral("issues")).toList();
  const auto preflight_parity = preflight.value(QStringLiteral("previewExportParity")).toMap();
  const auto preflight_parity_groups =
      preflight_parity.value(QStringLiteral("highRiskGroups")).toStringList();
  const auto preflight_color_provenance =
      preflight.value(QStringLiteral("colorTransformProvenance")).toMap();
  if (!preflight.value(QStringLiteral("ok")).toBool() ||
      preflight.value(QStringLiteral("warningCount")).toInt() < 5 ||
      preflight.value(QStringLiteral("outputColorSpace")).toString() != QStringLiteral("Display P3") ||
      std::abs(preflight.value(QStringLiteral("outputHeadroom")).toDouble() - 1.0) > 1.0e-9 ||
      preflight.value(QStringLiteral("softProofProfile")).toString() != QStringLiteral("sRGB") ||
      preflight.value(QStringLiteral("estimatedPixelWidth")).toInt() != 40 ||
      !issue_code_present(preflight_issues, QStringLiteral("profile-web-wide-gamut")) ||
      !issue_code_present(preflight_issues, QStringLiteral("soft-proof-mismatch")) ||
      !issue_code_present(preflight_issues, QStringLiteral("preview-export-parity-required")) ||
      !preflight_parity.value(QStringLiteral("required")).toBool() ||
      preflight_parity.value(QStringLiteral("status")).toString() != QStringLiteral("required-at-export") ||
      preflight_parity.value(QStringLiteral("expectedTolerance")).toInt() != 10 ||
      !preflight_parity_groups.contains(QStringLiteral("crop-transform-optics")) ||
      !preflight_parity_groups.contains(QStringLiteral("output-profile-proofing")) ||
      preflight_color_provenance.value(QStringLiteral("schema")).toString() !=
          QStringLiteral("color-transform-provenance:v10") ||
      preflight_color_provenance.value(QStringLiteral("executionBackend")).toString() !=
          QStringLiteral("OpenColorIO ACES 2") ||
      !preflight_color_provenance.value(QStringLiteral("processorVersion"))
           .toString()
           .contains(QStringLiteral("ocio-aces2-output-transform")) ||
      preflight_color_provenance.value(QStringLiteral("sourceProfile")).toString() !=
          QStringLiteral("Linear ProPhoto RGB") ||
      preflight_color_provenance.value(QStringLiteral("destinationProfile")).toString() !=
          QStringLiteral("Display P3") ||
      preflight_color_provenance.value(QStringLiteral("chromaticAdaptation")).toString() !=
          QStringLiteral("Bradford") ||
      preflight_color_provenance.value(QStringLiteral("highlightReconstructionModel")).toString() !=
          QStringLiteral("camera-linear-opposed-v2") ||
      preflight_color_provenance.value(QStringLiteral("iccBackend")).toString().isEmpty() ||
      !issue_code_present(preflight_issues, QStringLiteral("preview-clipping")) ||
      !issue_code_present(preflight_issues, QStringLiteral("raw-clipping"))) {
    std::cerr
        << QJsonDocument::fromVariant(preflight)
               .toJson(QJsonDocument::Compact)
               .toStdString()
        << '\n';
    return fail("export preflight should catch profile, soft-proof, parity, clipping, and size risks before queueing");
  }

  photo_editor::desktop::EditRecipeState filename_only_dcp_state;
  filename_only_dcp_state.input_color_profile =
      QStringLiteral("Camera DCP: Filename Only.dcp");
  const auto filename_only_dcp_preflight = controller.exportPreflight(
      QStringLiteral("photo"),
      source_path,
      QString(),
      photo_editor::desktop::serialize_edit_recipe_json(filename_only_dcp_state),
      QStringLiteral("TIFF 16-bit"),
      export_dir,
      0,
      0,
      0,
      0,
      0,
      0);
  const auto filename_only_dcp_issues =
      filename_only_dcp_preflight.value(QStringLiteral("issues")).toList();
  const auto filename_only_dcp_provenance =
      filename_only_dcp_preflight.value(QStringLiteral("colorTransformProvenance")).toMap();
  if (filename_only_dcp_preflight.value(QStringLiteral("ok")).toBool() ||
      !issue_code_present(
          filename_only_dcp_issues,
          QStringLiteral("dcp-raw-color-boundary-unavailable")) ||
      filename_only_dcp_provenance.value(QStringLiteral("requestedCameraProfile")).toString() !=
          QStringLiteral("Camera DCP: Filename Only.dcp") ||
      filename_only_dcp_provenance
              .value(QStringLiteral("cameraProfileResolutionStatus"))
              .toString() != QStringLiteral("unavailable") ||
      filename_only_dcp_provenance
              .value(QStringLiteral("cameraProfileSelectionSource"))
              .toString() != QStringLiteral("explicit-user-dcp") ||
      filename_only_dcp_provenance
              .value(QStringLiteral("cameraProfileSelectionPrecedenceRank"))
              .toInt() != 1 ||
      filename_only_dcp_provenance
              .value(QStringLiteral("cameraProfileFallbackUsed"))
              .toBool()) {
    return fail("export preflight must reject filename-only DCP requests instead of inventing a transform");
  }

  const auto preflight_profile_payload =
      photo_editor::pipeline::builtin_rgb_icc_profile_payload(
          photo_editor::pipeline::make_display_p3_color_space());
  const auto preflight_profile_hash =
      photo_editor::pipeline::color_payload_sha256(preflight_profile_payload);
  const photo_editor::pipeline::IccProfileAssetReference preflight_profile_reference{
      .asset_id = "icc:export-preflight-display-p3",
      .payload_sha256 = preflight_profile_hash,
      .display_name = "Preflight Display P3",
      .source_uri = {},
      .source_kind = "test-generated",
      .byte_size = preflight_profile_payload.size(),
  };
  if (!photo_editor::pipeline::default_icc_profile_asset_registry()
           .register_payload(
               preflight_profile_reference,
               preflight_profile_payload,
               "memory:export-preflight-display-p3")
           .ready()) {
    return fail("custom export preflight profile should register");
  }
  photo_editor::desktop::EditRecipeState custom_profile_preflight_state;
  custom_profile_preflight_state.output_color_space = QStringLiteral("Display P3");
  custom_profile_preflight_state.export_icc_profile_asset =
      photo_editor::desktop::IccProfileAssetState{
          .asset_id = QString::fromStdString(preflight_profile_reference.asset_id),
          .payload_sha256 = QString::fromStdString(preflight_profile_reference.payload_sha256),
          .display_name = QString::fromStdString(preflight_profile_reference.display_name),
          .source_uri = {},
          .source_kind = QStringLiteral("test-generated"),
          .byte_size = static_cast<qint64>(preflight_profile_reference.byte_size),
      };
  const auto custom_profile_preflight = controller.exportPreflight(
      QStringLiteral("photo"),
      source_path,
      QString(),
      photo_editor::desktop::serialize_edit_recipe_json(custom_profile_preflight_state),
      QStringLiteral("TIFF 16-bit"),
      export_dir,
      0,
      0,
      0,
      0,
      0,
      0);
  const auto custom_profile_provenance =
      custom_profile_preflight.value(QStringLiteral("colorTransformProvenance")).toMap();
  if (!custom_profile_preflight.value(QStringLiteral("ok")).toBool() ||
      custom_profile_provenance.value(QStringLiteral("executionBackend")).toString() !=
          QStringLiteral("OpenColorIO ACES 2 + Little CMS") ||
      custom_profile_provenance.value(QStringLiteral("profileAssetStatus")).toString() !=
          QStringLiteral("ready") ||
      custom_profile_provenance.value(QStringLiteral("profileAssetId")).toString() !=
          QString::fromStdString(preflight_profile_reference.asset_id) ||
      custom_profile_provenance.value(QStringLiteral("profilePayloadSha256")).toString() !=
          QString::fromStdString(preflight_profile_reference.payload_sha256) ||
      custom_profile_provenance.value(QStringLiteral("transformCacheKey")).toString().isEmpty() ||
      !custom_profile_provenance.value(QStringLiteral("transformFailureReason")).toString().isEmpty()) {
    return fail("export preflight should prove the exact custom ICC asset and compiled processor");
  }

  photo_editor::desktop::EditRecipeState aces_builtin_preflight_state;
  aces_builtin_preflight_state.photographic_rendering_model =
      QStringLiteral("aces2-ocio-v1");
  const auto aces_builtin_preflight = controller.exportPreflight(
      QStringLiteral("photo"),
      source_path,
      QString(),
      photo_editor::desktop::serialize_edit_recipe_json(
          aces_builtin_preflight_state),
      QStringLiteral("TIFF 16-bit"),
      export_dir,
      0,
      0,
      0,
      0,
      0,
      0);
  const auto aces_builtin_provenance =
      aces_builtin_preflight.value(QStringLiteral("colorTransformProvenance"))
          .toMap();
  const auto expected_aces_backend = QString::fromStdString(std::string(
      photo_editor::pipeline::aces2_output_renderer_backend_name()));
  const auto expected_aces_version = QString::fromStdString(std::string(
      photo_editor::pipeline::aces2_output_renderer_version()));
  if (!aces_builtin_preflight.value(QStringLiteral("ok")).toBool() ||
      aces_builtin_provenance.value(QStringLiteral("executionBackend"))
              .toString() != expected_aces_backend ||
      aces_builtin_provenance.value(QStringLiteral("processorVersion"))
              .toString() != expected_aces_version ||
      aces_builtin_provenance.value(QStringLiteral("transformCacheKey"))
          .toString()
          .isEmpty()) {
    return fail("ACES 2 export preflight should report the actual cached output processor");
  }

  auto aces_custom_preflight_state = custom_profile_preflight_state;
  aces_custom_preflight_state.photographic_rendering_model =
      QStringLiteral("aces2-ocio-v1");
  const auto aces_custom_preflight = controller.exportPreflight(
      QStringLiteral("photo"),
      source_path,
      QString(),
      photo_editor::desktop::serialize_edit_recipe_json(
          aces_custom_preflight_state),
      QStringLiteral("TIFF 16-bit"),
      export_dir,
      0,
      0,
      0,
      0,
      0,
      0);
  const auto aces_custom_provenance =
      aces_custom_preflight.value(QStringLiteral("colorTransformProvenance"))
          .toMap();
  if (!aces_custom_preflight.value(QStringLiteral("ok")).toBool() ||
      issue_code_present(
          aces_custom_preflight.value(QStringLiteral("issues")).toList(),
          QStringLiteral("aces2-custom-icc-output-not-supported")) ||
      aces_custom_provenance.value(QStringLiteral("executionBackend"))
              .toString() !=
          QStringLiteral("%1 + Little CMS").arg(expected_aces_backend) ||
      aces_custom_provenance.value(QStringLiteral("photographicRenderingStatus"))
              .toString() != QStringLiteral("active") ||
      !aces_custom_provenance.value(QStringLiteral("transformCacheKey"))
           .toString()
           .contains(QStringLiteral("->")) ||
      !aces_custom_provenance.value(QStringLiteral("transformCacheKey"))
           .toString()
           .contains(QStringLiteral("aces2-rec2020-interchange"))) {
    return fail("perceptual ACES 2 export should compose the photographic renderer with the custom ICC processor");
  }

  aces_custom_preflight_state.render_intent =
      QStringLiteral("Relative Colorimetric");
  const auto aces_relative_icc_preflight = controller.exportPreflight(
      QStringLiteral("photo"),
      source_path,
      QString(),
      photo_editor::desktop::serialize_edit_recipe_json(
          aces_custom_preflight_state),
      QStringLiteral("TIFF 16-bit"),
      export_dir,
      0,
      0,
      0,
      0,
      0,
      0);
  const auto aces_relative_icc_provenance =
      aces_relative_icc_preflight
          .value(QStringLiteral("colorTransformProvenance"))
          .toMap();
  if (!aces_relative_icc_preflight.value(QStringLiteral("ok")).toBool() ||
      issue_code_present(
          aces_relative_icc_preflight.value(QStringLiteral("issues")).toList(),
          QStringLiteral("aces2-custom-icc-output-not-supported")) ||
      aces_relative_icc_provenance.value(QStringLiteral("executionBackend"))
              .toString() != QStringLiteral("Little CMS") ||
      aces_relative_icc_provenance
              .value(QStringLiteral("photographicRenderingStatus"))
              .toString() != QStringLiteral("bypassed-relative-colorimetric")) {
    return fail("relative-colorimetric custom ICC export should bypass photographic rendering and use Little CMS");
  }

  auto missing_profile_preflight_state = custom_profile_preflight_state;
  missing_profile_preflight_state.export_icc_profile_asset.asset_id =
      QStringLiteral("icc:preflight-missing");
  missing_profile_preflight_state.export_icc_profile_asset.payload_sha256 =
      QString(64, QLatin1Char('0'));
  missing_profile_preflight_state.export_icc_profile_asset.byte_size = 128;
  const auto missing_profile_preflight = controller.exportPreflight(
      QStringLiteral("photo"),
      source_path,
      QString(),
      photo_editor::desktop::serialize_edit_recipe_json(missing_profile_preflight_state),
      QStringLiteral("TIFF 16-bit"),
      export_dir,
      0,
      0,
      0,
      0,
      0,
      0);
  if (missing_profile_preflight.value(QStringLiteral("ok")).toBool() ||
      !issue_code_present(
          missing_profile_preflight.value(QStringLiteral("issues")).toList(),
          QStringLiteral("icc-profile-asset-unavailable"))) {
    return fail("export preflight must block a missing requested ICC profile");
  }

  auto proof_profile_preflight_state = custom_profile_preflight_state;
  proof_profile_preflight_state.export_icc_profile_asset = {};
  proof_profile_preflight_state.proof_icc_profile_asset =
      custom_profile_preflight_state.export_icc_profile_asset;
  proof_profile_preflight_state.soft_proof_enabled = true;
  const auto proof_profile_preflight = controller.exportPreflight(
      QStringLiteral("photo"),
      source_path,
      QString(),
      photo_editor::desktop::serialize_edit_recipe_json(proof_profile_preflight_state),
      QStringLiteral("TIFF 16-bit"),
      export_dir,
      0,
      0,
      0,
      0,
      0,
      0);
  if (!proof_profile_preflight.value(QStringLiteral("ok")).toBool() ||
      issue_code_present(
          proof_profile_preflight.value(QStringLiteral("issues")).toList(),
          QStringLiteral("icc-proof-profile-execution-unsupported"))) {
    return fail("export preflight should accept an executable preview-only custom proof profile");
  }

  photo_editor::desktop::EditRecipeState threshold_state;
  threshold_state.output_color_space = QStringLiteral("sRGB");
  threshold_state.output_headroom = 2.0;
  threshold_state.raw_gamut_mapping = false;
  const auto threshold_preflight = controller.exportPreflight(
      QStringLiteral("photo"),
      source_path,
      QString(),
      photo_editor::desktop::serialize_edit_recipe_json(threshold_state),
      QStringLiteral("Full Resolution"),
      export_dir,
      8,
      0,
      0,
      8,
      0,
      0);
  const auto threshold_issues = threshold_preflight.value(QStringLiteral("issues")).toList();
  if (!threshold_preflight.value(QStringLiteral("ok")).toBool() ||
      threshold_preflight.value(QStringLiteral("outputColorSpace")).toString() != QStringLiteral("sRGB") ||
      std::abs(threshold_preflight.value(QStringLiteral("outputHeadroom")).toDouble() - 2.0) > 1.0e-9 ||
      threshold_preflight.value(QStringLiteral("preservesOverRange")).toBool() ||
      threshold_preflight.value(QStringLiteral("outputEncoding")).toString() != QStringLiteral("integer-sdr") ||
      threshold_preflight.value(QStringLiteral("headroomPolicy")).toString() !=
          QStringLiteral("tone-map-to-output-headroom") ||
      issue_code_count(threshold_issues, QStringLiteral("gamut-mapping-off")) != 1 ||
      !issue_code_present(threshold_issues, QStringLiteral("output-headroom-sdr-export")) ||
      issue_code_present(threshold_issues, QStringLiteral("profile-web-wide-gamut")) ||
      issue_code_present(threshold_issues, QStringLiteral("preview-clipping")) ||
      issue_code_present(threshold_issues, QStringLiteral("raw-clipping"))) {
    return fail(
        "export preflight should gate gamut warnings on sRGB output and preserve configured output headroom");
  }

  photo_editor::desktop::EditRecipeState scene_linear_preflight_state;
  scene_linear_preflight_state.output_color_space = QStringLiteral("sRGB");
  scene_linear_preflight_state.output_headroom = 2.0;
  const auto scene_linear_preflight = controller.exportPreflight(
      QStringLiteral("photo"),
      source_path,
      QString(),
      photo_editor::desktop::serialize_edit_recipe_json(scene_linear_preflight_state),
      QStringLiteral("Scene Linear PFM"),
      export_dir,
      8,
      0,
      0,
      8,
      0,
      0);
  const auto scene_linear_issues =
      scene_linear_preflight.value(QStringLiteral("issues")).toList();
  const auto scene_linear_settings =
      scene_linear_preflight.value(QStringLiteral("exportImageSettings")).toMap();
  if (!scene_linear_preflight.value(QStringLiteral("ok")).toBool() ||
      scene_linear_preflight.value(QStringLiteral("format")).toString() != QStringLiteral("PFM") ||
      scene_linear_preflight.value(QStringLiteral("bitDepth")).toInt() != 32 ||
      scene_linear_preflight.value(QStringLiteral("embedProfile")).toBool() ||
      !scene_linear_preflight.value(QStringLiteral("sceneLinear")).toBool() ||
      !scene_linear_preflight.value(QStringLiteral("preservesOverRange")).toBool() ||
      scene_linear_preflight.value(QStringLiteral("outputEncoding")).toString() !=
          QStringLiteral("scene-linear-float") ||
      scene_linear_preflight.value(QStringLiteral("headroomPolicy")).toString() !=
          QStringLiteral("preserve-over-range") ||
      !scene_linear_settings.value(QStringLiteral("sceneLinear")).toBool() ||
      !scene_linear_settings.value(QStringLiteral("preservesOverRange")).toBool() ||
      issue_code_present(scene_linear_issues, QStringLiteral("output-headroom-sdr-export"))) {
    return fail("scene-linear export preflight should expose float headroom preservation without SDR warnings");
  }

  threshold_state.output_color_space = QStringLiteral("Display P3");
  const auto wide_gamut_threshold_preflight = controller.exportPreflight(
      QStringLiteral("photo"),
      source_path,
      QString(),
      photo_editor::desktop::serialize_edit_recipe_json(threshold_state),
      QStringLiteral("Full Resolution"),
      export_dir,
      0,
      3,
      4,
      0,
      2,
      1);
  const auto wide_gamut_threshold_issues =
      wide_gamut_threshold_preflight.value(QStringLiteral("issues")).toList();
  if (issue_code_present(wide_gamut_threshold_issues, QStringLiteral("gamut-mapping-off")) ||
      issue_code_present(wide_gamut_threshold_issues, QStringLiteral("preview-clipping")) ||
      issue_code_present(wide_gamut_threshold_issues, QStringLiteral("raw-clipping"))) {
    return fail(
        "export preflight clipping warnings should require sampled pixels and sRGB-specific gamut risk");
  }

  const auto missing_source_preflight = controller.exportPreflight(
      QStringLiteral("photo"),
      temp_dir.filePath(QStringLiteral("missing.png")),
      QString(),
      recipe_json,
      QStringLiteral("Full Resolution"),
      export_dir,
      0,
      0,
      0,
      0,
      0,
      0);
  if (missing_source_preflight.value(QStringLiteral("ok")).toBool() ||
      !issue_code_present(
          missing_source_preflight.value(QStringLiteral("issues")).toList(),
          QStringLiteral("source-missing"))) {
    return fail("export preflight should block missing rendered photo sources");
  }

  const auto missing_destination_preflight = controller.exportPreflight(
      QStringLiteral("photo"),
      source_path,
      QString(),
      recipe_json,
      QStringLiteral("Full Resolution"),
      QString(),
      0,
      0,
      0,
      0,
      0,
      0);
  if (missing_destination_preflight.value(QStringLiteral("ok")).toBool() ||
      !issue_code_present(
          missing_destination_preflight.value(QStringLiteral("issues")).toList(),
          QStringLiteral("destination-missing"))) {
    return fail("export preflight should block missing destinations");
  }

  photo_editor::desktop::EditRecipeState stale_mask_state;
  QVariantMap stale_generated_mask;
  stale_generated_mask.insert(QStringLiteral("kind"), QStringLiteral("sky"));
  stale_generated_mask.insert(QStringLiteral("name"), QStringLiteral("Sky Refresh"));
  stale_generated_mask.insert(QStringLiteral("generatedMaskStale"), true);
  stale_generated_mask.insert(QStringLiteral("generatedMaskMetadata"), QVariantMap{
      {QStringLiteral("schemaVersion"), 1},
      {QStringLiteral("sourceSchemaVersion"), 1},
      {QStringLiteral("rasterSchemaVersion"), 1},
      {QStringLiteral("generatedOutputSchemaVersion"), 1},
      {QStringLiteral("kind"), QStringLiteral("sky")},
      {QStringLiteral("promptType"), QStringLiteral("semantic")},
      {QStringLiteral("sourceWidth"), 80},
      {QStringLiteral("sourceHeight"), 40},
      {QStringLiteral("sourceResolution"), QStringLiteral("80x40")},
      {QStringLiteral("cropX"), 0.0},
      {QStringLiteral("cropY"), 0.0},
      {QStringLiteral("cropW"), 1.0},
      {QStringLiteral("cropH"), 1.0},
      {QStringLiteral("cropZoom"), 1.0},
      {QStringLiteral("cropPreset"), QStringLiteral("Current")},
      {QStringLiteral("transformRotation"), 0.0},
      {QStringLiteral("runtime"), QStringLiteral("Local Smart Select")},
      {QStringLiteral("modelName"), QStringLiteral("local")},
      {QStringLiteral("modelVersion"), QStringLiteral("test")},
      {QStringLiteral("dependencies"), QVariantMap{}},
  });
  QVariantMap preview_inpaint_patch;
  preview_inpaint_patch.insert(QStringLiteral("kind"), QStringLiteral("brush"));
  preview_inpaint_patch.insert(QStringLiteral("toolRole"), QStringLiteral("retouch"));
  preview_inpaint_patch.insert(QStringLiteral("retouchMode"), QStringLiteral("remove"));
  preview_inpaint_patch.insert(QStringLiteral("name"), QStringLiteral("Remove Cable"));
  preview_inpaint_patch.insert(QStringLiteral("committed"), true);
  preview_inpaint_patch.insert(QStringLiteral("inpaintPatchData"), QByteArray(4, '\x7f'));
  preview_inpaint_patch.insert(QStringLiteral("inpaintPatchX"), 0);
  preview_inpaint_patch.insert(QStringLiteral("inpaintPatchY"), 0);
  preview_inpaint_patch.insert(QStringLiteral("inpaintPatchWidth"), 2);
  preview_inpaint_patch.insert(QStringLiteral("inpaintPatchHeight"), 2);
  preview_inpaint_patch.insert(QStringLiteral("inpaintQualityMode"), QStringLiteral("preview"));
  preview_inpaint_patch.insert(QStringLiteral("inpaintFinalRequired"), true);
  stale_mask_state.mask_stack = QVariantList{stale_generated_mask, preview_inpaint_patch};
  const auto stale_mask_preflight = controller.exportPreflight(
      QStringLiteral("photo"),
      source_path,
      QString(),
      photo_editor::desktop::serialize_edit_recipe_json(stale_mask_state),
      QStringLiteral("Full Resolution"),
      export_dir,
      0,
      0,
      0,
      0,
      0,
      0);
  const auto stale_mask_issues = stale_mask_preflight.value(QStringLiteral("issues")).toList();
  if (!stale_mask_preflight.value(QStringLiteral("ok")).toBool()) {
    return fail("stale generated mask/final inpaint warnings should not block export preflight");
  }
  if (!issue_code_present(stale_mask_issues, QStringLiteral("stale-generated-mask"))) {
    return fail("export preflight should warn about stale generated masks");
  }
  if (!issue_code_present(stale_mask_issues, QStringLiteral("final-inpaint-required"))) {
    return fail("export preflight should warn about preview-only final inpaint patches");
  }
  if (stale_mask_preflight.value(QStringLiteral("warningCount")).toInt() < 2) {
    return fail("export preflight should count stale generated mask and final inpaint warnings");
  }

  photo_editor::desktop::EditRecipeState missing_retouch_state;
  QVariantMap missing_heal_source;
  missing_heal_source.insert(QStringLiteral("kind"), QStringLiteral("brush"));
  missing_heal_source.insert(QStringLiteral("toolRole"), QStringLiteral("retouch"));
  missing_heal_source.insert(QStringLiteral("retouchMode"), QStringLiteral("heal"));
  missing_heal_source.insert(QStringLiteral("name"), QStringLiteral("Heal Without Brush"));
  QVariantMap missing_remove_source;
  missing_remove_source.insert(QStringLiteral("kind"), QStringLiteral("object"));
  missing_remove_source.insert(QStringLiteral("toolRole"), QStringLiteral("retouch"));
  missing_remove_source.insert(QStringLiteral("retouchMode"), QStringLiteral("remove"));
  missing_remove_source.insert(QStringLiteral("name"), QStringLiteral("Remove Without Inpaint"));
  missing_retouch_state.mask_stack = QVariantList{missing_heal_source, missing_remove_source};
  const auto missing_retouch_preflight = controller.exportPreflight(
      QStringLiteral("photo"),
      source_path,
      QString(),
      photo_editor::desktop::serialize_edit_recipe_json(missing_retouch_state),
      QStringLiteral("Full Resolution"),
      export_dir,
      0,
      0,
      0,
      0,
      0,
      0);
  const auto missing_retouch_issues =
      missing_retouch_preflight.value(QStringLiteral("issues")).toList();
  if (!missing_retouch_preflight.value(QStringLiteral("ok")).toBool()) {
    return fail("missing retouch provenance should warn without blocking export preflight");
  }
  if (!issue_code_present(
          missing_retouch_issues,
          QStringLiteral("retouch-provenance-missing"))) {
    return fail("export preflight should warn when retouch provenance is missing");
  }

  const auto crop_job_index = controller.jobCount();
  if (!controller.queueExport(
          QStringLiteral("asset-crop"),
          QStringLiteral("photo"),
          QStringLiteral("Crop Check"),
          source_path,
          QString(),
          recipe_json,
          QStringLiteral("Full Resolution"),
          export_dir)) {
    return fail("photo export should queue successfully");
  }

  if (!wait_for_job(&controller, crop_job_index)) {
    std::cerr << "Export history at timeout: " << QJsonDocument::fromVariant(controller.jobs()).toJson().constData()
              << "loading=" << controller.loading() << " error=" << controller.historyError().toStdString() << '\n';
    for (const auto& record : photo_editor::desktop::loadPersistedJobRecords())
      std::cerr << "Persisted job " << record.job_id.toStdString() << " status=" << record.status.toStdString() << '\n';
    return fail("rendered photo export should finish");
  }

  const auto rendered_job = controller.jobs().at(crop_job_index).toMap();
  if (rendered_job.value(QStringLiteral("status")).toString() != QStringLiteral("Completed")) {
    std::cerr
        << "rendered photo export status="
        << rendered_job.value(QStringLiteral("status"))
               .toString()
               .toStdString()
        << " error="
        << rendered_job.value(QStringLiteral("errorMessage"))
               .toString()
               .toStdString()
        << '\n';
    return fail("rendered photo export should complete successfully");
  }

  const auto rendered_path = rendered_job.value(QStringLiteral("outputPath")).toString();
  const auto rendered_export_job =
      rendered_job.value(QStringLiteral("exportRenderJob")).toMap();
  const auto rendered_export_preview_job =
      rendered_job.value(QStringLiteral("exportPreviewJob")).toMap();
  const auto rendered_export_job_from_metadata =
      rendered_job.value(QStringLiteral("metadata"))
          .toMap()
          .value(QStringLiteral("exportRenderJob"))
          .toMap();
  const auto rendered_export_preview_job_from_metadata =
      rendered_job.value(QStringLiteral("metadata"))
          .toMap()
          .value(QStringLiteral("exportPreviewJob"))
          .toMap();
  const auto rendered_metadata =
      rendered_job.value(QStringLiteral("metadata")).toMap();
  const auto completed_render_stages =
      rendered_metadata
          .value(
              QStringLiteral(
                  "completedRenderStages"))
          .toList();
  if (rendered_metadata.value(QStringLiteral("canonicalPlanCacheKey"))
          .toString()
          .isEmpty() ||
      rendered_metadata.value(QStringLiteral("canonicalRecipeCacheKey"))
          .toString()
          .isEmpty() ||
      !rendered_metadata.value(QStringLiteral("canonicalPlanError"))
           .toString()
           .isEmpty() ||
      rendered_metadata
              .value(
                  QStringLiteral(
                      "decodePreflight"))
              .toString() !=
          QStringLiteral(
              "size-only-before-plan") ||
      rendered_metadata
              .value(
                  QStringLiteral(
                      "decodeExecution"))
              .toString() !=
          QStringLiteral(
              "file-read-and-linearize-in-executor-decode") ||
      rendered_metadata
              .value(
                  QStringLiteral(
                      "executorSubmissionCount"))
              .toInt() != 1 ||
      completed_render_stages.size() != 9 ||
      completed_render_stages.front().toString() !=
          QStringLiteral("decode") ||
      completed_render_stages.at(1).toString() !=
          QStringLiteral("camera_characterization") ||
      completed_render_stages.at(2).toString() !=
          QStringLiteral("scene_adjustments") ||
      completed_render_stages.at(3).toString() !=
          QStringLiteral("tone") ||
      completed_render_stages.at(4).toString() !=
          QStringLiteral("look") ||
      completed_render_stages.at(5).toString() !=
          QStringLiteral("detail") ||
      completed_render_stages.at(6).toString() !=
          QStringLiteral("geometry") ||
      completed_render_stages.at(7).toString() !=
          QStringLiteral("photographic_output") ||
      completed_render_stages.back().toString() !=
          QStringLiteral("publish") ||
      rendered_metadata
          .value(
              QStringLiteral(
                  "geometryExecutorCacheHit"))
          .toBool()) {
    return fail(
        "rendered photo export should persist its admitted plan identity and executed Decode-through-Publish manifest");
  }
  if (rendered_export_job.value(QStringLiteral("jobKind")).toString() !=
          QStringLiteral("full_resolution_export") ||
      rendered_export_job.value(QStringLiteral("stage")).toString() !=
          QStringLiteral("export_render") ||
      !rendered_export_job.value(QStringLiteral("jobId"))
           .toString()
           .startsWith(QStringLiteral("export_render-%1-").arg(
               rendered_job.value(QStringLiteral("jobId")).toString())) ||
      rendered_export_job.value(QStringLiteral("assetId")).toString() !=
          QStringLiteral("asset-crop") ||
      rendered_export_job.value(QStringLiteral("sourcePath")).toString() != source_path ||
      rendered_export_job.value(QStringLiteral("outputPath")).toString() != rendered_path ||
      rendered_export_job.value(QStringLiteral("preset")).toString() !=
          QStringLiteral("Full Resolution") ||
      !rendered_export_job.value(QStringLiteral("renderPhoto")).toBool() ||
      !rendered_export_job.value(QStringLiteral("stateSignature"))
           .toString()
           .startsWith(QStringLiteral("sha1:")) ||
      rendered_export_job_from_metadata.value(QStringLiteral("jobId")).toString() !=
          rendered_export_job.value(QStringLiteral("jobId")).toString()) {
    return fail("rendered photo export should expose a stable full-resolution export render job identity");
  }
  const QFileInfo rendered_info(rendered_path);
  if (rendered_info.suffix().toLower() != QStringLiteral("png")) {
    return fail("full-resolution photo export should write a png");
  }
  if (rendered_export_preview_job.value(QStringLiteral("jobKind")).toString() !=
          QStringLiteral("full_resolution_export_preview") ||
      rendered_export_preview_job.value(QStringLiteral("stage")).toString() !=
          QStringLiteral("export_preview_diff") ||
      !rendered_export_preview_job.value(QStringLiteral("jobId"))
           .toString()
           .startsWith(QStringLiteral("export_preview-%1-").arg(
               rendered_job.value(QStringLiteral("jobId")).toString())) ||
      rendered_export_preview_job.value(QStringLiteral("assetId")).toString() !=
          QStringLiteral("asset-crop") ||
      rendered_export_preview_job.value(QStringLiteral("sourcePath")).toString() != source_path ||
      rendered_export_preview_job.value(QStringLiteral("outputPath")).toString() != rendered_path ||
      rendered_export_preview_job.value(QStringLiteral("preset")).toString() !=
          QStringLiteral("Full Resolution") ||
      !rendered_export_preview_job.value(QStringLiteral("renderPhoto")).toBool() ||
      rendered_export_preview_job.value(QStringLiteral("tolerance")).toInt() != 1 ||
      rendered_export_preview_job.value(QStringLiteral("exportRenderJobId")).toString() !=
          rendered_export_job.value(QStringLiteral("jobId")).toString() ||
      !rendered_export_preview_job.value(QStringLiteral("stateSignature"))
           .toString()
           .startsWith(QStringLiteral("sha1:")) ||
      rendered_export_preview_job_from_metadata.value(QStringLiteral("jobId")).toString() !=
          rendered_export_preview_job.value(QStringLiteral("jobId")).toString()) {
    return fail("rendered photo export should expose a stable export preview parity job identity");
  }

  QImage rendered_image(rendered_path);
  if (rendered_image.isNull()) {
    return fail("rendered photo export should produce a readable image");
  }
  if (rendered_image.width() != 40 || rendered_image.height() != 40) {
    return fail("rendered photo export should honor the crop dimensions");
  }
  if (!rendered_image.colorSpace().isValid()) {
    return fail("rendered photo export should preserve an output color profile");
  }
  if (rendered_image.colorSpace() != QColorSpace(QColorSpace::DisplayP3)) {
    return fail("rendered photo export should embed the requested Display P3 output profile");
  }
  if (rendered_image.format() != QImage::Format_RGBA64) {
    return fail("full-resolution rendered PNG export should preserve a 16-bit image format");
  }

  const auto sample = rendered_image.pixelColor(rendered_image.width() / 2, rendered_image.height() / 2);
  if (sample.blue() < 160 || sample.red() > 120) {
    return fail("rendered photo export should contain the cropped right-half content");
  }

  const auto missing_identity_admission =
      photo_editor::desktop::RenderPlanStore::shared().admit(
          QString(),
          source_path,
          photo_editor::desktop::parse_edit_recipe_json(recipe_json),
          source.size(),
          photo_editor::pipeline::RenderPurpose::Export,
          photo_editor::pipeline::DisplayRenderingTarget::
              DisplayP3Simulation);
  if (missing_identity_admission.admitted ||
      !missing_identity_admission.error.contains(
          QStringLiteral("stable source asset id")) ||
      !missing_identity_admission.plan_cache_key.isEmpty()) {
    return fail("ACES export must fail closed when canonical source identity is unavailable");
  }
  const auto canonical_state =
      photo_editor::desktop::parse_edit_recipe_json(recipe_json);
  auto uncharacterized_raw_metadata =
      photo_editor::pipeline::DecodedImageMetadata{
          .valid = true,
          .is_raw = true,
          .pixel_encoding =
              photo_editor::pipeline::DecodedPixelEncoding::CameraLinearRgb,
          .decoder_backend = "LibRaw camera-linear fixture",
          .decoded_color_space_name = "Camera RGB",
          .camera_profile_identity = "fixture:camera",
          .demosaic_applied = true,
          .white_balance_applied = true,
          .camera_transform_applied = false,
          .camera_make = "Fixture",
          .camera_model = "Uncharacterized",
      };
  const auto uncharacterized_raw_admission =
      photo_editor::desktop::RenderPlanStore::shared().admit(
          QStringLiteral("asset-uncharacterized-raw"),
          source_path,
          canonical_state,
          source.size(),
          photo_editor::pipeline::RenderPurpose::Export,
          photo_editor::pipeline::DisplayRenderingTarget::
              DisplayP3Simulation,
          std::nullopt,
          &uncharacterized_raw_metadata);
  if (uncharacterized_raw_admission.admitted ||
      !uncharacterized_raw_admission.error.contains(
          QStringLiteral("camera_linear_characterization_required"))) {
    return fail(
        "canonical RAW export must fail closed instead of applying an identity camera transform");
  }
  uncharacterized_raw_metadata.decoder_camera_to_xyz_valid = true;
  uncharacterized_raw_metadata.decoder_camera_to_xyz = {
      0.62, 0.24, 0.14,
      0.18, 0.72, 0.10,
      0.02, 0.12, 0.86,
  };
  uncharacterized_raw_metadata.decoder_camera_to_xyz_reference_white =
      "D65";
  const auto characterized_raw_admission =
      photo_editor::desktop::RenderPlanStore::shared().admit(
          QStringLiteral("asset-characterized-raw"),
          source_path,
          canonical_state,
          source.size(),
          photo_editor::pipeline::RenderPurpose::Export,
          photo_editor::pipeline::DisplayRenderingTarget::
              DisplayP3Simulation,
          std::nullopt,
          &uncharacterized_raw_metadata);
  if (!characterized_raw_admission.admitted ||
      characterized_raw_admission.plan_cache_key.isEmpty()) {
    return fail(
        "canonical RAW export must admit an explicit decoder camera matrix");
  }

  constexpr auto
      kConcurrentPlanRequestCount = 8;
  std::vector<
      QFuture<
          photo_editor::desktop::
              CanonicalRenderPlanAdmission>>
      concurrent_plan_requests;
  concurrent_plan_requests.reserve(
      kConcurrentPlanRequestCount);
  for (auto index = 0;
       index <
       kConcurrentPlanRequestCount;
       ++index) {
    concurrent_plan_requests.push_back(
        QtConcurrent::run(
            [source_path,
             canonical_state,
             source_size =
                 source.size()]() {
              return photo_editor::desktop::
                  RenderPlanStore::shared().admit(
                      QStringLiteral(
                          "asset-plan-cache-concurrency-proof"),
                      source_path,
                      canonical_state,
                      source_size,
                      photo_editor::pipeline::
                          RenderPurpose::
                              SettledPreview,
                      photo_editor::pipeline::
                          DisplayRenderingTarget::
                              DisplayP3Simulation);
            }));
  }
  auto plan_cache_hit_count = 0;
  auto plan_cache_miss_count = 0;
  QString concurrent_plan_key;
  quint64 concurrent_build_count = 0;
  for (auto& future :
       concurrent_plan_requests) {
    future.waitForFinished();
    const auto admission =
        future.result();
    if (!admission.admitted ||
        admission.plan_cache_key.isEmpty() ||
        admission.cache_resident_entries <=
            0 ||
        admission.cache_resident_entries >
            64) {
      return fail(
          "canonical plan cache must return an admitted plan from a bounded resident store");
    }
    if (concurrent_plan_key.isEmpty()) {
      concurrent_plan_key =
          admission.plan_cache_key;
      concurrent_build_count =
          admission.cache_build_count;
    } else if (
        admission.plan_cache_key !=
            concurrent_plan_key ||
        admission.cache_build_count !=
            concurrent_build_count) {
      return fail(
          "identical concurrent plan requests must share one plan identity and one build generation");
    }
    if (admission.cache_hit) {
      ++plan_cache_hit_count;
    } else {
      ++plan_cache_miss_count;
    }
  }
  if (plan_cache_miss_count != 1 ||
      plan_cache_hit_count !=
          kConcurrentPlanRequestCount - 1) {
    return fail(
        "eight identical concurrent plan requests must produce one build and seven cache hits");
  }
  const auto changed_target_admission =
      photo_editor::desktop::
          RenderPlanStore::shared().admit(
              QStringLiteral(
                  "asset-plan-cache-concurrency-proof"),
              source_path,
              canonical_state,
              source.size(),
              photo_editor::pipeline::
                  RenderPurpose::
                      SettledPreview,
              photo_editor::pipeline::
                  DisplayRenderingTarget::
                      SrgbSimulation);
  if (!changed_target_admission.admitted ||
      changed_target_admission.cache_hit ||
      changed_target_admission
              .plan_cache_key ==
          concurrent_plan_key ||
      changed_target_admission
              .cache_build_count !=
          concurrent_build_count + 1) {
    return fail(
        "a changed output target must compile one new canonical plan without reusing the prior target entry");
  }
  auto last_resident_plan_count = 0;
  for (auto index = 0;
       index < 65;
       ++index) {
    const auto admission =
        photo_editor::desktop::
            RenderPlanStore::shared().admit(
                QStringLiteral(
                    "asset-plan-cache-eviction-%1")
                    .arg(index),
                source_path,
                canonical_state,
                source.size(),
                photo_editor::pipeline::
                    RenderPurpose::
                        SettledPreview,
                photo_editor::pipeline::
                    DisplayRenderingTarget::
                        DisplayP3Simulation);
    if (!admission.admitted ||
        admission.cache_hit ||
        admission.cache_resident_entries >
            64) {
      return fail(
          "unique canonical plans must build once while the resident cache stays bounded to 64 entries");
    }
    last_resident_plan_count =
        admission.cache_resident_entries;
  }
  const auto evicted_plan_admission =
      photo_editor::desktop::
          RenderPlanStore::shared().admit(
              QStringLiteral(
                  "asset-plan-cache-concurrency-proof"),
              source_path,
              canonical_state,
              source.size(),
              photo_editor::pipeline::
                  RenderPurpose::
                      SettledPreview,
              photo_editor::pipeline::
                  DisplayRenderingTarget::
                      DisplayP3Simulation);
  if (last_resident_plan_count != 64 ||
      !evicted_plan_admission.admitted ||
      evicted_plan_admission.cache_hit ||
      evicted_plan_admission
              .cache_resident_entries !=
          64) {
    return fail(
        "the 64-entry canonical plan cache must evict and rebuild its least-recently-used identity");
  }

  const auto deleted_source_path = temp_dir.filePath(QStringLiteral("deleted-before-render.png"));
  if (!source.save(deleted_source_path)) {
    return fail("deleted-source export fixture should be written before queueing");
  }
  const auto deleted_source_job_index = controller.jobCount();
  if (!controller.queueExport(
          QStringLiteral("asset-deleted-source"),
          QStringLiteral("photo"),
          QStringLiteral("Deleted Source Export"),
          deleted_source_path,
          QString(),
          recipe_json,
          QStringLiteral("Full Resolution"),
          export_dir)) {
    return fail("deleted-source export should queue before the source disappears");
  }
  if (!QFile::remove(deleted_source_path)) {
    return fail("deleted-source export fixture should be removable before rendering");
  }
  if (!wait_for_job(&controller, deleted_source_job_index)) {
    return fail("deleted-source export job should settle");
  }
  const auto deleted_source_job = controller.jobs().at(deleted_source_job_index).toMap();
  const auto deleted_source_export_render_job =
      deleted_source_job.value(QStringLiteral("exportRenderJob")).toMap();
  const auto deleted_source_export_render_job_from_metadata =
      deleted_source_job.value(QStringLiteral("metadata"))
          .toMap()
          .value(QStringLiteral("exportRenderJob"))
          .toMap();
  if (deleted_source_job.value(QStringLiteral("status")).toString() != QStringLiteral("Failed") ||
      !deleted_source_job.value(QStringLiteral("errorMessage"))
           .toString()
           .contains(QStringLiteral("Unable to render")) ||
      deleted_source_export_render_job.value(QStringLiteral("jobKind")).toString() !=
          QStringLiteral("full_resolution_export") ||
      deleted_source_export_render_job.value(QStringLiteral("stage")).toString() !=
          QStringLiteral("export_render") ||
      deleted_source_export_render_job.value(QStringLiteral("assetId")).toString() !=
          QStringLiteral("asset-deleted-source") ||
      !deleted_source_export_render_job.value(QStringLiteral("stateSignature"))
           .toString()
           .startsWith(QStringLiteral("sha1:")) ||
      deleted_source_export_render_job_from_metadata.value(QStringLiteral("jobId")).toString() !=
          deleted_source_export_render_job.value(QStringLiteral("jobId")).toString()) {
    return fail("failed photo exports should keep stage-specific export_render job metadata");
  }

  const auto crop_bounds_source_path = temp_dir.filePath(QStringLiteral("crop-bounds-source.png"));
  QImage crop_bounds_source(11, 7, QImage::Format_ARGB32);
  crop_bounds_source.setColorSpace(QColorSpace(QColorSpace::SRgb));
  for (int y = 0; y < crop_bounds_source.height(); ++y) {
    for (int x = 0; x < crop_bounds_source.width(); ++x) {
      crop_bounds_source.setPixelColor(
          x,
          y,
          QColor(17 + x * 19, 23 + y * 27, 31 + x * 7 + y * 9, 255));
    }
  }
  if (!crop_bounds_source.save(crop_bounds_source_path)) {
    return fail("crop bounds source image should be written for export testing");
  }
  photo_editor::desktop::EditRecipeState crop_bounds_state;
  crop_bounds_state.crop_rect_x = 2.0 / 11.0;
  crop_bounds_state.crop_rect_y = 1.0 / 7.0;
  crop_bounds_state.crop_rect_width = 6.0 / 11.0;
  crop_bounds_state.crop_rect_height = 4.0 / 7.0;
  crop_bounds_state.output_color_space = QStringLiteral("sRGB");
  const auto expected_crop_rect =
      photo_editor::desktop::crop_rect_for_image(crop_bounds_source.size(), crop_bounds_state);
  const auto expected_crop_preview = photo_editor::desktop::render_output_image_with_backend(
      photo_editor::desktop::apply_crop(crop_bounds_source, crop_bounds_state),
      crop_bounds_state,
      QColorSpace(QColorSpace::SRgb),
      QImage::Format_RGBA64,
      photo_editor::desktop::RenderBackendPreference::Auto);
  if (expected_crop_rect != QRect(2, 1, 6, 4) || expected_crop_preview.size() != expected_crop_rect.size()) {
    return fail("preview crop bounds proof should establish the exact expected crop rectangle");
  }
  const auto crop_bounds_job_index = controller.jobCount();
  if (!controller.queueExport(
          QStringLiteral("asset-crop-bounds"),
          QStringLiteral("photo"),
          QStringLiteral("Crop Bounds Check"),
          crop_bounds_source_path,
          QString(),
          photo_editor::desktop::serialize_edit_recipe_json(crop_bounds_state),
          QStringLiteral("Full Resolution"),
          export_dir)) {
    return fail("crop bounds export should queue successfully");
  }
  if (!wait_for_job(&controller, crop_bounds_job_index)) {
    return fail("crop bounds export should finish");
  }
  const auto crop_bounds_job = controller.jobs().at(crop_bounds_job_index).toMap();
  const QImage crop_bounds_exported(crop_bounds_job.value(QStringLiteral("outputPath")).toString());
  if (crop_bounds_exported.size() != expected_crop_rect.size()) {
    return fail("export crop bounds should match the preview crop rectangle size exactly");
  }
  const auto crop_bounds_channel_delta =
      max_channel_delta(
          expected_crop_preview,
          crop_bounds_exported);
  if (crop_bounds_channel_delta > 1) {
    std::cerr << "crop bounds max channel delta="
              << crop_bounds_channel_delta
              << " expected0="
              << expected_crop_preview.pixelColor(0, 0).rgba64().red()
              << ','
              << expected_crop_preview.pixelColor(0, 0).rgba64().green()
              << ','
              << expected_crop_preview.pixelColor(0, 0).rgba64().blue()
              << " exported0="
              << crop_bounds_exported.pixelColor(0, 0).rgba64().red()
              << ','
              << crop_bounds_exported.pixelColor(0, 0).rgba64().green()
              << ','
              << crop_bounds_exported.pixelColor(0, 0).rgba64().blue()
              << '\n';
    return fail("export crop result should match the preview crop bounds and pixels exactly");
  }
  const auto crop_bounds_diff = crop_bounds_job.value(QStringLiteral("previewExportDiff")).toMap();
  const auto crop_bounds_preview_job =
      crop_bounds_job.value(QStringLiteral("exportPreviewJob")).toMap();
  if (!crop_bounds_diff.value(QStringLiteral("available")).toBool()) {
    return fail("export crop proof should publish preview/export diff metadata");
  }
  if (crop_bounds_preview_job.value(QStringLiteral("jobKind")).toString() !=
          QStringLiteral("full_resolution_export_preview") ||
      crop_bounds_diff.value(QStringLiteral("exportPreviewJobId")).toString() !=
          crop_bounds_preview_job.value(QStringLiteral("jobId")).toString()) {
    return fail("export crop proof should bind preview/export diff metadata to the preview job identity");
  }
  if (crop_bounds_diff.value(QStringLiteral("width")).toInt() != expected_crop_rect.width() ||
      crop_bounds_diff.value(QStringLiteral("height")).toInt() != expected_crop_rect.height()) {
    return fail("export crop proof diff dimensions should match the exact preview crop bounds");
  }
  if (!crop_bounds_diff.value(QStringLiteral("withinTolerance")).toBool() ||
      crop_bounds_diff.value(QStringLiteral("maxChannelDelta")).toInt() > 1 ||
      crop_bounds_diff.value(QStringLiteral("tolerance")).toInt() != 1) {
    return fail("export crop proof diff should verify written pixels are within lossless tolerance");
  }
  const auto crop_bounds_metadata = crop_bounds_job.value(QStringLiteral("metadata")).toMap();
  if (crop_bounds_metadata.value(QStringLiteral("previewExportDiff")).toMap().isEmpty()) {
    return fail("export crop proof diff should persist in job metadata");
  }

  const auto original_constraint_job_index = controller.jobCount();
  if (!controller.queueExportWithCropConstraint(
          QStringLiteral("asset-original-constraint"),
          QStringLiteral("photo"),
          QStringLiteral("Original Constraint"),
          source_path,
          QString(),
          recipe_json,
          QStringLiteral("Full Resolution"),
          export_dir,
          QStringLiteral("Original"),
          0.0,
          0.0,
          0.0,
          0.0)) {
    return fail("export crop constraints should queue successfully");
  }
  const auto original_constraint_metadata =
      controller.jobs().at(original_constraint_job_index).toMap().value(QStringLiteral("exportCrop")).toMap();
  if (original_constraint_metadata.value(QStringLiteral("preset")).toString() != QStringLiteral("Original")) {
    return fail("export crop constraints should be visible in queued job metadata");
  }
  if (!wait_for_job(&controller, original_constraint_job_index)) {
    return fail("export crop constraint job should finish");
  }
  const QImage original_constraint_image(
      controller.jobs()
          .at(original_constraint_job_index)
          .toMap()
          .value(QStringLiteral("outputPath"))
          .toString());
  if (original_constraint_image.width() != 80 || original_constraint_image.height() != 40) {
    return fail("export crop constraints should be able to clear the recipe crop for one export");
  }

  const auto square_constraint_job_index = controller.jobCount();
  if (!controller.queueExportWithCropConstraint(
          QStringLiteral("asset-square-constraint"),
          QStringLiteral("photo"),
          QStringLiteral("Square Constraint"),
          source_path,
          QString(),
          QString(),
          QStringLiteral("Full Resolution"),
          export_dir,
          QStringLiteral("1:1"),
          0.0,
          0.0,
          0.0,
          0.0)) {
    return fail("square export crop constraints should queue successfully");
  }
  if (!wait_for_job(&controller, square_constraint_job_index)) {
    return fail("square export crop constraint job should finish");
  }
  const QImage square_constraint_image(
      controller.jobs()
          .at(square_constraint_job_index)
          .toMap()
          .value(QStringLiteral("outputPath"))
          .toString());
  if (square_constraint_image.width() != 40 || square_constraint_image.height() != 40) {
    return fail("export crop constraints should apply an export-only crop preset");
  }

  const auto archive_job_index = controller.jobCount();
  if (!controller.queueExport(
          QStringLiteral("asset-archive"),
          QStringLiteral("photo"),
          QStringLiteral("Archive Check"),
          source_path,
          QString(),
          recipe_json,
          QStringLiteral("Archive TIFF"),
          export_dir)) {
    return fail("archive TIFF export should queue successfully");
  }
  if (!wait_for_job(&controller, archive_job_index)) {
    return fail("archive TIFF export should finish");
  }
  const auto archive_job = controller.jobs().at(archive_job_index).toMap();
  if (archive_job.value(QStringLiteral("status")).toString() != QStringLiteral("Completed")) {
    const auto artifact =
        archive_job.value(
            QStringLiteral(
                "artifactVerification"))
            .toMap();
    std::cerr
        << "archive TIFF status="
        << archive_job
               .value(
                   QStringLiteral("status"))
               .toString()
               .toStdString()
        << " error="
        << archive_job
               .value(
                   QStringLiteral(
                       "errorMessage"))
               .toString()
               .toStdString()
        << " artifactFailure="
        << artifact
               .value(
                   QStringLiteral(
                       "failureReason"))
               .toString()
               .toStdString()
        << '\n';
    return fail("archive TIFF export should complete successfully");
  }
  const auto archive_path = archive_job.value(QStringLiteral("outputPath")).toString();
  if (QFileInfo(archive_path).suffix().toLower() != QStringLiteral("tif")) {
    return fail("archive export should write a TIFF artifact");
  }
  const QImage archive_image(archive_path);
  if (archive_image.isNull()) {
    return fail("archive TIFF export should produce a readable image");
  }
  if (archive_image.format() != QImage::Format_RGBA64) {
    return fail("archive TIFF export should preserve a 16-bit image format");
  }
  if (archive_image.colorSpace() != QColorSpace(QColorSpace::DisplayP3)) {
    return fail("archive TIFF export should embed the requested output profile");
  }

  const auto export_settings_preflight = controller.exportPreflight(
      QStringLiteral("photo"),
      source_path,
      QString(),
      recipe_json,
      QStringLiteral("PNG 8-bit"),
      export_dir,
      0,
      0,
      0,
      0,
      0,
      0);
  const auto preflight_export_settings =
      export_settings_preflight.value(QStringLiteral("exportImageSettings")).toMap();
  if (export_settings_preflight.value(QStringLiteral("format")).toString() !=
          QStringLiteral("PNG") ||
      export_settings_preflight.value(QStringLiteral("bitDepth")).toInt() != 8 ||
      export_settings_preflight.value(QStringLiteral("flattenAlpha")).toBool() ||
      !export_settings_preflight.value(QStringLiteral("embedProfile")).toBool() ||
      preflight_export_settings.value(QStringLiteral("format")).toString() !=
          QStringLiteral("PNG") ||
      preflight_export_settings.value(QStringLiteral("bitDepth")).toInt() != 8 ||
      preflight_export_settings.value(QStringLiteral("alphaBehavior")).toString() !=
          QStringLiteral("preserve-unassociated-alpha") ||
      preflight_export_settings.value(QStringLiteral("componentAssociation")).toString() !=
          QStringLiteral("unassociated") ||
      preflight_export_settings.value(QStringLiteral("orientationPolicy")).toString() !=
          QStringLiteral("normalized-pixels-no-orientation-tag")) {
    return fail("export preflight should expose explicit format, bit-depth, alpha, and profile settings");
  }

  QImage alpha_source(18, 10, QImage::Format_ARGB32);
  alpha_source.setColorSpace(QColorSpace(QColorSpace::SRgb));
  alpha_source.fill(Qt::transparent);
  for (int y = 0; y < alpha_source.height(); ++y) {
    for (int x = alpha_source.width() / 3; x < alpha_source.width(); ++x) {
      alpha_source.setPixelColor(
          x,
          y,
          x < (alpha_source.width() * 2) / 3
              ? QColor(208, 44, 132, 96)
              : QColor(32, 160, 220, 255));
    }
  }
  const auto alpha_source_path = temp_dir.filePath(QStringLiteral("alpha-source.png"));
  if (!alpha_source.save(alpha_source_path)) {
    return fail("alpha export source should be written");
  }
  photo_editor::desktop::EditRecipeState alpha_state;
  alpha_state.output_color_space = QStringLiteral("sRGB");
  const auto alpha_recipe_json = photo_editor::desktop::serialize_edit_recipe_json(alpha_state);
  auto queue_recipe_export = [&](const QString& asset_id,
                                 const QString& preset,
                                 const QString& source,
                                 const QString& recipe) -> QVariantMap {
    const auto index = controller.jobCount();
    if (!controller.queueExport(
            asset_id,
            QStringLiteral("photo"),
            asset_id,
            source,
            QString(),
            recipe,
            preset,
            export_dir)) {
      return {};
    }
    if (!wait_for_job(&controller, index)) {
      return {};
    }
    return controller.jobs().at(index).toMap();
  };
  auto queue_format_export = [&](const QString& asset_id, const QString& preset) -> QVariantMap {
    return queue_recipe_export(asset_id, preset, alpha_source_path, alpha_recipe_json);
  };

  const auto png8_job = queue_format_export(QStringLiteral("PNG 8-bit Check"), QStringLiteral("PNG 8-bit"));
  const auto png16_job = queue_format_export(QStringLiteral("PNG 16-bit Check"), QStringLiteral("PNG 16-bit"));
  const auto tiff8_job = queue_format_export(QStringLiteral("TIFF 8-bit Check"), QStringLiteral("TIFF 8-bit"));
  const auto tiff16_job = queue_format_export(QStringLiteral("TIFF 16-bit Check"), QStringLiteral("TIFF 16-bit"));
  const auto jpeg8_job = queue_format_export(QStringLiteral("JPEG 8-bit Check"), QStringLiteral("JPEG 8-bit"));
  if (png8_job.isEmpty() || png16_job.isEmpty() || tiff8_job.isEmpty() ||
      tiff16_job.isEmpty() || jpeg8_job.isEmpty()) {
    return fail("explicit format/bit-depth exports should queue and finish");
  }
  const auto require_export_settings = [&](const QVariantMap& job,
                                           const QString& suffix,
                                           const QString& format,
                                           int bit_depth,
                                           bool flatten_alpha) {
    const auto output_path = job.value(QStringLiteral("outputPath")).toString();
    const auto render_job = job.value(QStringLiteral("exportRenderJob")).toMap();
    const auto settings = render_job.value(QStringLiteral("exportImageSettings")).toMap();
    const auto artifact = job.value(QStringLiteral("artifactVerification")).toMap();
    const auto expected_alpha_behavior =
        flatten_alpha ? QStringLiteral("flatten-to-white-opaque")
                      : QStringLiteral("preserve-unassociated-alpha");
    return job.value(QStringLiteral("status")).toString() == QStringLiteral("Completed") &&
           QFileInfo(output_path).suffix().compare(suffix, Qt::CaseInsensitive) == 0 &&
           render_job.value(QStringLiteral("format")).toString() == format &&
           render_job.value(QStringLiteral("bitDepth")).toInt() == bit_depth &&
           render_job.value(QStringLiteral("flattenAlpha")).toBool() == flatten_alpha &&
           settings.value(QStringLiteral("format")).toString() == format &&
           settings.value(QStringLiteral("bitDepth")).toInt() == bit_depth &&
           settings.value(QStringLiteral("flattenAlpha")).toBool() == flatten_alpha &&
           artifact.value(QStringLiteral("schema")).toString() ==
               QStringLiteral("export-artifact-verification:v2") &&
           artifact.value(QStringLiteral("valid")).toBool() &&
           artifact.value(QStringLiteral("bitDepth")).toInt() == bit_depth &&
           artifact.value(QStringLiteral("pixelsVerified")).toBool() &&
           artifact.value(QStringLiteral("profileVerified")).toBool() &&
           artifact.value(QStringLiteral("transferFunctionVerified")).toBool() &&
           artifact.value(QStringLiteral("metadataApplicable")).toBool() &&
           artifact.value(QStringLiteral("metadataVerified")).toBool() &&
           !artifact.value(QStringLiteral("masteringFieldsApplicable")).toBool() &&
           artifact.value(QStringLiteral("masteringFieldsVerified")).toBool() &&
           artifact.value(QStringLiteral("alphaVerified")).toBool() &&
           artifact.value(QStringLiteral("orientationVerified")).toBool() &&
           artifact.value(QStringLiteral("componentAssociationVerified")).toBool() &&
           artifact.value(QStringLiteral("alphaBehavior")).toString() ==
               expected_alpha_behavior;
  };
  if (!require_export_settings(png8_job, QStringLiteral("png"), QStringLiteral("PNG"), 8, false) ||
      !require_export_settings(png16_job, QStringLiteral("png"), QStringLiteral("PNG"), 16, false) ||
      !require_export_settings(tiff8_job, QStringLiteral("tif"), QStringLiteral("TIFF"), 8, false) ||
      !require_export_settings(tiff16_job, QStringLiteral("tif"), QStringLiteral("TIFF"), 16, false) ||
      !require_export_settings(jpeg8_job, QStringLiteral("jpg"), QStringLiteral("JPEG"), 8, true)) {
    for (const auto& job : {png8_job, png16_job, tiff8_job, tiff16_job, jpeg8_job}) {
      const auto artifact = job.value(QStringLiteral("artifactVerification")).toMap();
      std::cerr << job.value(QStringLiteral("assetId")).toString().toStdString()
                << " status=" << job.value(QStringLiteral("status")).toString().toStdString()
                << " error=" << job.value(QStringLiteral("errorMessage")).toString().toStdString()
                << " artifactValid=" << artifact.value(QStringLiteral("valid")).toBool()
                << " artifactFailure="
                << artifact.value(QStringLiteral("failureReason")).toString().toStdString()
                << " maxPixelDelta="
                << artifact.value(QStringLiteral("maximumPixelDelta")).toInt()
                << " meanPixelDelta="
                << artifact.value(QStringLiteral("meanPixelDelta")).toDouble()
                << '\n';
    }
    return fail("export render jobs should persist explicit format, bit-depth, and alpha policy");
  }
  const auto png8_signature =
      png8_job.value(QStringLiteral("exportRenderJob")).toMap().value(QStringLiteral("stateSignature")).toString();
  const auto png16_signature =
      png16_job.value(QStringLiteral("exportRenderJob")).toMap().value(QStringLiteral("stateSignature")).toString();
  const auto tiff8_signature =
      tiff8_job.value(QStringLiteral("exportRenderJob")).toMap().value(QStringLiteral("stateSignature")).toString();
  const auto tiff16_signature =
      tiff16_job.value(QStringLiteral("exportRenderJob")).toMap().value(QStringLiteral("stateSignature")).toString();
  if (png8_signature.isEmpty() || png16_signature.isEmpty() ||
      tiff8_signature.isEmpty() || tiff16_signature.isEmpty() ||
      png8_signature == png16_signature ||
      tiff8_signature == tiff16_signature) {
    return fail("export render signatures should include format and bit-depth settings");
  }
  const QImage png8_image(png8_job.value(QStringLiteral("outputPath")).toString());
  const QImage png16_image(png16_job.value(QStringLiteral("outputPath")).toString());
  const QImage tiff8_image(tiff8_job.value(QStringLiteral("outputPath")).toString());
  const QImage tiff16_image(tiff16_job.value(QStringLiteral("outputPath")).toString());
  const QImage jpeg8_image(jpeg8_job.value(QStringLiteral("outputPath")).toString());
  if (png8_image.isNull() || png16_image.isNull() || tiff8_image.isNull() ||
      tiff16_image.isNull() || jpeg8_image.isNull()) {
    return fail("explicit format/bit-depth export artifacts should be readable");
  }
  if (png8_image.depth() > 32 || png16_image.format() != QImage::Format_RGBA64 ||
      tiff8_image.depth() > 32 || tiff16_image.format() != QImage::Format_RGBA64) {
    return fail("PNG/TIFF exports should honor requested 8-bit or 16-bit storage");
  }
  if (jpeg8_image.hasAlphaChannel() ||
      jpeg8_image.pixelColor(1, jpeg8_image.height() / 2).red() < 220 ||
      jpeg8_image.pixelColor(1, jpeg8_image.height() / 2).green() < 220 ||
      jpeg8_image.pixelColor(1, jpeg8_image.height() / 2).blue() < 220) {
    return fail("JPEG 8-bit export should flatten transparent pixels onto white and drop alpha");
  }

  const auto require_profile_export = [&](const QString& asset_id,
                                          const QString& preset,
                                          const QString& output_profile,
                                          const QColorSpace& expected_profile) -> bool {
    photo_editor::desktop::EditRecipeState profile_state;
    profile_state.output_color_space = output_profile;
    const auto profile_recipe_json = photo_editor::desktop::serialize_edit_recipe_json(profile_state);
    const auto job = queue_recipe_export(asset_id, preset, alpha_source_path, profile_recipe_json);
    if (job.value(QStringLiteral("status")).toString() != QStringLiteral("Completed")) {
      const auto artifact =
          job.value(
                 QStringLiteral(
                     "artifactVerification"))
              .toMap();
      std::cerr
          << asset_id.toStdString()
          << " status="
          << job.value(
                    QStringLiteral("status"))
                 .toString()
                 .toStdString()
          << " error="
          << job.value(
                    QStringLiteral(
                        "errorMessage"))
                 .toString()
                 .toStdString()
          << " artifactFailure="
          << artifact
                 .value(
                     QStringLiteral(
                         "failureReason"))
                 .toString()
                 .toStdString()
          << '\n';
      return false;
    }
    const QImage decoded(job.value(QStringLiteral("outputPath")).toString());
    return !decoded.isNull() && decoded.colorSpace() == expected_profile;
  };
  if (!require_profile_export(
          QStringLiteral("sRGB PNG Profile Check"),
          QStringLiteral("PNG 16-bit"),
          QStringLiteral("sRGB"),
          QColorSpace(QColorSpace::SRgb)) ||
      !require_profile_export(
          QStringLiteral("Display P3 PNG Profile Check"),
          QStringLiteral("PNG 16-bit"),
          QStringLiteral("Display P3"),
          QColorSpace(QColorSpace::DisplayP3)) ||
      !require_profile_export(
          QStringLiteral("ProPhoto TIFF Profile Check"),
          QStringLiteral("TIFF 16-bit"),
          QStringLiteral("ProPhoto RGB"),
          QColorSpace(QColorSpace::ProPhotoRgb))) {
    return fail("exports should preserve embedded output profile metadata for sRGB, Display P3, and wide gamut");
  }

  QImage headroom_source(2, 1, QImage::Format_ARGB32);
  headroom_source.setColorSpace(QColorSpace(QColorSpace::SRgb));
  headroom_source.fill(Qt::white);
  const auto headroom_source_path = temp_dir.filePath(QStringLiteral("headroom-source.png"));
  if (!headroom_source.save(headroom_source_path)) {
    return fail("headroom export source should be written");
  }
  photo_editor::desktop::EditRecipeState headroom_state;
  headroom_state.output_color_space = QStringLiteral("sRGB");
  headroom_state.output_headroom = 2.0;
  headroom_state.exposure = 3.0;
  const auto headroom_recipe_json =
      photo_editor::desktop::serialize_edit_recipe_json(headroom_state);
  const auto headroom_png_job = queue_recipe_export(
      QStringLiteral("Headroom PNG SDR Check"),
      QStringLiteral("PNG 16-bit"),
      headroom_source_path,
      headroom_recipe_json);
  const auto headroom_pfm_job = queue_recipe_export(
      QStringLiteral("Headroom Scene Linear Check"),
      QStringLiteral("Scene Linear PFM"),
      headroom_source_path,
      headroom_recipe_json);
  if (headroom_png_job.value(QStringLiteral("status")).toString() != QStringLiteral("Completed") ||
      headroom_pfm_job.value(QStringLiteral("status")).toString() != QStringLiteral("Completed")) {
    return fail("headroom PNG and scene-linear PFM exports should complete");
  }
  const auto headroom_pfm_artifact =
      headroom_pfm_job
          .value(
              QStringLiteral(
                  "artifactVerification"))
          .toMap();
  if (headroom_pfm_artifact
              .value(
                  QStringLiteral("schema"))
              .toString() !=
          QStringLiteral(
              "export-artifact-verification:v2") ||
      !headroom_pfm_artifact
           .value(
               QStringLiteral("valid"))
           .toBool() ||
      !headroom_pfm_artifact
           .value(
               QStringLiteral(
                   "pixelsVerified"))
           .toBool() ||
      !headroom_pfm_artifact
           .value(
               QStringLiteral(
                   "transferFunctionVerified"))
           .toBool() ||
      headroom_pfm_artifact
          .value(
              QStringLiteral(
                  "metadataApplicable"))
          .toBool() ||
      headroom_pfm_artifact
          .value(
              QStringLiteral(
                  "masteringFieldsApplicable"))
          .toBool()) {
    return fail(
        "PFM artifact verification must prove linear float pixels and mark container metadata and mastering fields inapplicable");
  }
  const auto headroom_png_render_job =
      headroom_png_job.value(QStringLiteral("exportRenderJob")).toMap();
  const auto headroom_pfm_render_job =
      headroom_pfm_job.value(QStringLiteral("exportRenderJob")).toMap();
  if (headroom_png_render_job.value(QStringLiteral("preservesOverRange")).toBool() ||
      headroom_png_render_job.value(QStringLiteral("headroomPolicy")).toString() !=
          QStringLiteral("tone-map-to-output-headroom") ||
      !headroom_pfm_render_job.value(QStringLiteral("sceneLinear")).toBool() ||
      !headroom_pfm_render_job.value(QStringLiteral("preservesOverRange")).toBool() ||
      headroom_pfm_render_job.value(QStringLiteral("outputEncoding")).toString() !=
          QStringLiteral("scene-linear-float") ||
      headroom_pfm_render_job.value(QStringLiteral("headroomPolicy")).toString() !=
          QStringLiteral("preserve-over-range")) {
    return fail("export render metadata should distinguish SDR and scene-linear headroom policy");
  }
  const QImage headroom_png(headroom_png_job.value(QStringLiteral("outputPath")).toString());
  PfmImage headroom_pfm;
  if (headroom_png.isNull() ||
      !read_pfm_image(headroom_pfm_job.value(QStringLiteral("outputPath")).toString(), &headroom_pfm) ||
      headroom_pfm.width != 2 || headroom_pfm.height != 1 || headroom_pfm.pixels.size() < 3) {
    return fail("headroom export artifacts should be readable as PNG and PFM");
  }
  if (headroom_png.pixelColor(0, 0).redF() > 1.0001F ||
      headroom_png.pixelColor(0, 0).greenF() > 1.0001F ||
      headroom_png.pixelColor(0, 0).blueF() > 1.0001F) {
    return fail("integer SDR headroom export should not decode to over-range pixel values");
  }
  if (headroom_pfm.pixels[0] < 7.95F || headroom_pfm.pixels[0] > 8.05F ||
      std::abs(headroom_pfm.pixels[0] - headroom_pfm.pixels[1]) > 1.0e-4F ||
      std::abs(headroom_pfm.pixels[1] - headroom_pfm.pixels[2]) > 1.0e-4F) {
    std::cerr << "PFM headroom pixel=" << headroom_pfm.pixels[0] << ","
              << headroom_pfm.pixels[1] << "," << headroom_pfm.pixels[2]
              << '\n';
    return fail("scene-linear PFM export should preserve neutral over-range headroom");
  }

  const auto openexr_preflight = controller.exportPreflight(
      QStringLiteral("photo"),
      headroom_source_path,
      QString(),
      headroom_recipe_json,
      QStringLiteral("OpenEXR Half"),
      export_dir,
      2,
      0,
      0,
      2,
      0,
      0);
  const auto openexr_preflight_issues =
      openexr_preflight.value(QStringLiteral("issues")).toList();
  if (!photo_editor::io::openexr_backend_available()) {
    if (openexr_preflight.value(QStringLiteral("ok")).toBool() ||
        !issue_code_present(
            openexr_preflight_issues,
            QStringLiteral("openexr-backend-unavailable"))) {
      return fail("OpenEXR preflight must fail explicitly when its backend is unavailable");
    }
  } else {
    const auto openexr_settings =
        openexr_preflight.value(QStringLiteral("exportImageSettings")).toMap();
    const auto openexr_provenance =
        openexr_preflight.value(QStringLiteral("colorTransformProvenance")).toMap();
    if (!openexr_preflight.value(QStringLiteral("ok")).toBool() ||
        openexr_preflight.value(QStringLiteral("format")).toString() !=
            QStringLiteral("OpenEXR") ||
        openexr_preflight.value(QStringLiteral("bitDepth")).toInt() != 16 ||
        !openexr_preflight.value(QStringLiteral("sceneLinear")).toBool() ||
        !openexr_preflight.value(QStringLiteral("preservesOverRange")).toBool() ||
        !openexr_preflight.value(QStringLiteral("carriesChromaticities")).toBool() ||
        openexr_preflight.value(QStringLiteral("displayTransformApplied")).toBool() ||
        openexr_preflight.value(QStringLiteral("outputColorSpace")).toString() !=
            QStringLiteral("Linear ProPhoto RGB") ||
        openexr_settings.value(QStringLiteral("componentType")).toString() !=
            QStringLiteral("half") ||
        openexr_settings.value(QStringLiteral("colorSpacePolicy")).toString() !=
            QStringLiteral("working-profile-chromaticities") ||
        openexr_provenance.value(QStringLiteral("executionBackend")).toString() !=
            QStringLiteral("scene-linear-working") ||
        openexr_provenance.value(QStringLiteral("photographicRenderingStatus")).toString() !=
            QStringLiteral("bypassed-scene-linear-export")) {
      return fail("OpenEXR preflight must describe a linear working-space artifact without a display transform");
    }

    const auto openexr_half_job = queue_recipe_export(
        QStringLiteral("OpenEXR Half Check"),
        QStringLiteral("OpenEXR Half"),
        headroom_source_path,
        headroom_recipe_json);
    const auto openexr_float_job = queue_recipe_export(
        QStringLiteral("OpenEXR Float Check"),
        QStringLiteral("OpenEXR 32-bit Float"),
        headroom_source_path,
        headroom_recipe_json);
    if (openexr_half_job.value(QStringLiteral("status")).toString() !=
            QStringLiteral("Completed") ||
        openexr_float_job.value(QStringLiteral("status")).toString() !=
            QStringLiteral("Completed") ||
        QFileInfo(openexr_half_job.value(QStringLiteral("outputPath")).toString())
                .suffix()
                .compare(QStringLiteral("exr"), Qt::CaseInsensitive) != 0) {
      return fail("OpenEXR half/float exports should complete with an EXR artifact");
    }
    for (const auto& job :
         {openexr_half_job,
          openexr_float_job}) {
      const auto artifact =
          job.value(
                 QStringLiteral(
                     "artifactVerification"))
              .toMap();
      if (artifact
                  .value(
                      QStringLiteral("schema"))
                  .toString() !=
              QStringLiteral(
                  "export-artifact-verification:v2") ||
          !artifact
               .value(
                   QStringLiteral("valid"))
               .toBool() ||
          !artifact
               .value(
                   QStringLiteral(
                       "pixelsVerified"))
               .toBool() ||
          !artifact
               .value(
                   QStringLiteral(
                       "profileVerified"))
               .toBool() ||
          !artifact
               .value(
                   QStringLiteral(
                       "transferFunctionVerified"))
               .toBool() ||
          !artifact
               .value(
                   QStringLiteral(
                       "metadataApplicable"))
               .toBool() ||
          !artifact
               .value(
                   QStringLiteral(
                       "metadataVerified"))
               .toBool() ||
          artifact
              .value(
                  QStringLiteral(
                      "masteringFieldsApplicable"))
              .toBool()) {
        return fail(
            "OpenEXR artifact verification must prove decoded scene-linear pixels, chromaticity metadata, and transfer");
      }
    }
    const auto half_inspection = photo_editor::io::inspect_openexr(
        openexr_half_job.value(QStringLiteral("outputPath")).toString().toStdString());
    const auto float_inspection = photo_editor::io::inspect_openexr(
        openexr_float_job.value(QStringLiteral("outputPath")).toString().toStdString());
    if (!half_inspection.valid || !float_inspection.valid ||
        half_inspection.width != 2 || half_inspection.height != 1 ||
        half_inspection.channel_type != "half" ||
        float_inspection.channel_type != "float" ||
        !half_inspection.has_alpha || !float_inspection.has_alpha ||
        half_inspection.color_space_name != "Linear ProPhoto RGB" ||
        half_inspection.transfer_function != "linear" ||
        half_inspection.rendering_model != "scene-linear-working-v1" ||
        half_inspection.transform_provenance.find("display-transform:bypassed") ==
            std::string::npos ||
        half_inspection.rgba_pixels.size() < 4U ||
        float_inspection.rgba_pixels.size() < 4U ||
        half_inspection.rgba_pixels[0] <= 1.0F ||
        float_inspection.rgba_pixels[0] <= 1.0F ||
        std::abs(half_inspection.rgba_pixels[0] -
                 float_inspection.rgba_pixels[0]) > 0.01F) {
      return fail("OpenEXR artifact readback must preserve linear working-space metadata, alpha, and extended highlights");
    }

    const auto half_output_path =
        openexr_half_job.value(QStringLiteral("outputPath")).toString();
    QImage decoded_openexr;
    photo_editor::pipeline::DecodedImageMetadata decoded_openexr_metadata;
    if (!photo_editor::desktop::preview_supported(half_output_path) ||
        photo_editor::desktop::source_image_size(half_output_path) != QSize(2, 1) ||
        !photo_editor::desktop::decode_source_image(
            half_output_path,
            &decoded_openexr,
            0,
            photo_editor::desktop::RenderEditState{},
            &decoded_openexr_metadata) ||
        decoded_openexr.size() != QSize(2, 1) ||
        decoded_openexr.format() != QImage::Format_RGBA32FPx4 ||
        !decoded_openexr.colorSpace().isValid()) {
      return fail("exported OpenEXR artifact must re-enter the normal source decode path as linear float RGBA");
    }
    const auto* const decoded_openexr_pixels =
        reinterpret_cast<const float*>(decoded_openexr.constScanLine(0));
    if (decoded_openexr_pixels == nullptr || decoded_openexr_pixels[0] <= 1.0F ||
        std::abs(decoded_openexr_pixels[0] - half_inspection.rgba_pixels[0]) >
            1.0e-5F) {
      return fail("OpenEXR import must preserve exported scene-linear over-range samples");
    }

    const auto acescg_preflight = controller.exportPreflight(
        QStringLiteral("photo"),
        headroom_source_path,
        QString(),
        headroom_recipe_json,
        QStringLiteral("ACEScg OpenEXR"),
        export_dir,
        2,
        0,
        0,
        2,
        0,
        0);
    const auto acescg_settings =
        acescg_preflight.value(QStringLiteral("exportImageSettings")).toMap();
    const auto acescg_provenance =
        acescg_preflight.value(QStringLiteral("colorTransformProvenance")).toMap();
    if (!acescg_preflight.value(QStringLiteral("ok")).toBool() ||
        acescg_preflight.value(QStringLiteral("outputColorSpace")).toString() !=
            QStringLiteral("ACEScg") ||
        acescg_settings.value(QStringLiteral("sceneLinearColorSpace")).toString() !=
            QStringLiteral("ACEScg") ||
        acescg_settings.value(QStringLiteral("colorSpacePolicy")).toString() !=
            QStringLiteral("explicit-scene-linear-chromaticities") ||
        acescg_provenance.value(QStringLiteral("executionBackend")).toString() !=
            QStringLiteral("builtin-matrix") ||
        acescg_provenance.value(QStringLiteral("processorVersion")).toString() !=
            QStringLiteral("builtin-matrix:v1") ||
        acescg_provenance.value(QStringLiteral("destinationProfile")).toString() !=
            QStringLiteral("ACEScg") ||
        acescg_preflight.value(QStringLiteral("displayTransformApplied")).toBool()) {
      return fail("ACEScg OpenEXR preflight must expose an explicit scene-linear interchange transform");
    }

    const auto aces2065_job = queue_recipe_export(
        QStringLiteral("ACES2065-1 Interchange Check"),
        QStringLiteral("ACES2065-1 OpenEXR"),
        headroom_source_path,
        headroom_recipe_json);
    const auto acescg_job = queue_recipe_export(
        QStringLiteral("ACEScg Interchange Check"),
        QStringLiteral("ACEScg OpenEXR 32-bit Float"),
        headroom_source_path,
        headroom_recipe_json);
    if (aces2065_job.value(QStringLiteral("status")).toString() !=
            QStringLiteral("Completed") ||
        acescg_job.value(QStringLiteral("status")).toString() !=
            QStringLiteral("Completed")) {
      return fail("ACES2065-1 and ACEScg OpenEXR exports should complete");
    }
    const auto aces2065_inspection = photo_editor::io::inspect_openexr(
        aces2065_job.value(QStringLiteral("outputPath")).toString().toStdString());
    const auto acescg_inspection = photo_editor::io::inspect_openexr(
        acescg_job.value(QStringLiteral("outputPath")).toString().toStdString());
    const auto expected_ap0_chromaticities =
        photo_editor::io::openexr_chromaticities_for_color_space(
            photo_editor::pipeline::make_aces2065_1_color_space());
    const auto expected_ap1_chromaticities =
        photo_editor::io::openexr_chromaticities_for_color_space(
            photo_editor::pipeline::make_acescg_color_space());
    if (!aces2065_inspection.valid || !acescg_inspection.valid ||
        aces2065_inspection.color_space_name != "ACES2065-1" ||
        acescg_inspection.color_space_name != "ACEScg" ||
        aces2065_inspection.channel_type != "half" ||
        acescg_inspection.channel_type != "float" ||
        aces2065_inspection.rendering_model != "scene-linear-interchange-v1" ||
        acescg_inspection.rendering_model != "scene-linear-interchange-v1" ||
        acescg_inspection.transform_provenance.find(
            "source-profile:Linear ProPhoto RGB|destination-profile:ACEScg") ==
            std::string::npos ||
        std::abs(aces2065_inspection.chromaticities.red_x -
                 expected_ap0_chromaticities.red_x) > 1.0e-6F ||
        std::abs(aces2065_inspection.chromaticities.white_x -
                 expected_ap0_chromaticities.white_x) > 1.0e-6F ||
        std::abs(acescg_inspection.chromaticities.red_x -
                 expected_ap1_chromaticities.red_x) > 1.0e-6F ||
        std::abs(acescg_inspection.chromaticities.white_x -
                 expected_ap1_chromaticities.white_x) > 1.0e-6F ||
        acescg_inspection.rgba_pixels.size() != float_inspection.rgba_pixels.size()) {
      return fail("ACES OpenEXR readback must preserve AP0/AP1 identity, precision, and transform provenance");
    }
    const auto working_to_acescg =
        photo_editor::pipeline::compile_builtin_color_transform(
            photo_editor::pipeline::make_linear_prophoto_rgb_color_space(),
            photo_editor::pipeline::make_acescg_color_space());
    for (std::size_t pixel_index = 0U;
         pixel_index < acescg_inspection.rgba_pixels.size();
         pixel_index += 4U) {
      const auto expected = photo_editor::pipeline::multiply_matrix3_vector(
          working_to_acescg.matrix,
          {
              float_inspection.rgba_pixels[pixel_index],
              float_inspection.rgba_pixels[pixel_index + 1U],
              float_inspection.rgba_pixels[pixel_index + 2U],
          });
      if (std::abs(acescg_inspection.rgba_pixels[pixel_index] - expected[0]) >
              2.0e-5 ||
          std::abs(acescg_inspection.rgba_pixels[pixel_index + 1U] - expected[1]) >
              2.0e-5 ||
          std::abs(acescg_inspection.rgba_pixels[pixel_index + 2U] - expected[2]) >
              2.0e-5 ||
          std::abs(acescg_inspection.rgba_pixels[pixel_index + 3U] -
                   float_inspection.rgba_pixels[pixel_index + 3U]) >
              1.0e-7F) {
        return fail("ACEScg OpenEXR pixels must equal the declared working-to-AP1 transform and preserve alpha");
      }
    }
    QImage decoded_acescg;
    if (!photo_editor::desktop::decode_source_image(
            acescg_job.value(QStringLiteral("outputPath")).toString(),
            &decoded_acescg,
            0,
            photo_editor::desktop::RenderEditState{}) ||
        decoded_acescg.size() != QSize(2, 1) ||
        decoded_acescg.format() != QImage::Format_RGBA32FPx4 ||
        !decoded_acescg.colorSpace().isValid()) {
      return fail("ACEScg OpenEXR must import as a tagged linear floating-point source");
    }
    const auto* decoded_acescg_pixels =
        reinterpret_cast<const float*>(decoded_acescg.constScanLine(0));
    if (decoded_acescg_pixels == nullptr ||
        std::abs(decoded_acescg_pixels[0] - acescg_inspection.rgba_pixels[0]) >
            1.0e-6F ||
        std::abs(decoded_acescg_pixels[3] - acescg_inspection.rgba_pixels[3]) >
            1.0e-7F) {
      return fail("ACEScg OpenEXR import must preserve AP1 samples and unassociated alpha");
    }
  }

  const auto hdr_pq_preflight = controller.exportPreflight(
      QStringLiteral("photo"),
      headroom_source_path,
      QString(),
      headroom_recipe_json,
      QStringLiteral("HDR PQ AVIF"),
      export_dir,
      2,
      0,
      0,
      2,
      0,
      0);
  const auto hdr_pq_issues =
      hdr_pq_preflight.value(QStringLiteral("issues")).toList();
  if (!photo_editor::io::hdr_avif_backend_available()) {
    if (hdr_pq_preflight.value(QStringLiteral("ok")).toBool() ||
        !issue_code_present(
            hdr_pq_issues,
            QStringLiteral("hdr-avif-backend-unavailable"))) {
      return fail("HDR AVIF preflight must fail explicitly when its backend is unavailable");
    }
  } else {
    const auto hdr_settings =
        hdr_pq_preflight.value(QStringLiteral("exportImageSettings")).toMap();
    const auto hdr_provenance =
        hdr_pq_preflight.value(QStringLiteral("colorTransformProvenance")).toMap();
    if (!hdr_pq_preflight.value(QStringLiteral("ok")).toBool() ||
        hdr_pq_preflight.value(QStringLiteral("format")).toString() !=
            QStringLiteral("AVIF") ||
        hdr_pq_preflight.value(QStringLiteral("bitDepth")).toInt() != 10 ||
        !hdr_pq_preflight.value(QStringLiteral("hdrDelivery")).toBool() ||
        hdr_pq_preflight.value(QStringLiteral("hdrTransfer")).toString() !=
            QStringLiteral("PQ") ||
        hdr_pq_preflight.value(QStringLiteral("outputColorSpace")).toString() !=
            QStringLiteral("Rec.2020") ||
        hdr_pq_preflight.value(QStringLiteral("outputEncoding")).toString() !=
            QStringLiteral("rec2100-pq") ||
        hdr_pq_preflight.value(QStringLiteral("headroomPolicy")).toString() !=
            QStringLiteral("tone-map-to-hdr-target") ||
        std::abs(hdr_pq_preflight.value(QStringLiteral("outputHeadroom")).toDouble() -
                 (1000.0 / 203.0)) > 1.0e-9 ||
        hdr_settings.value(QStringLiteral("peakLuminanceNits")).toDouble() != 1000.0 ||
        hdr_settings.value(QStringLiteral("referenceWhiteNits")).toDouble() != 203.0 ||
        hdr_settings.value(QStringLiteral("jpegQuality")).toInt() != 100 ||
        hdr_settings.value(QStringLiteral("colorSpacePolicy")).toString() !=
            QStringLiteral("rec2020-nclx") ||
        !hdr_provenance.value(QStringLiteral("executionBackend")).toString().contains(
            QStringLiteral("libheif")) ||
        hdr_provenance.value(QStringLiteral("destinationProfile")).toString() !=
            QStringLiteral("Rec.2100 PQ")) {
      return fail("HDR AVIF preflight must state its Rec.2100 target, luminance contract, and delivery backend");
    }

    const auto hdr_pq_job = queue_recipe_export(
        QStringLiteral("HDR PQ AVIF Check"),
        QStringLiteral("HDR PQ AVIF"),
        headroom_source_path,
        headroom_recipe_json);
    const auto hdr_hlg_job = queue_recipe_export(
        QStringLiteral("HDR HLG AVIF Check"),
        QStringLiteral("HDR HLG AVIF"),
        headroom_source_path,
        headroom_recipe_json);
    if (hdr_pq_job.value(QStringLiteral("status")).toString() !=
            QStringLiteral("Completed") ||
        hdr_hlg_job.value(QStringLiteral("status")).toString() !=
            QStringLiteral("Completed")) {
      return fail("PQ and HLG AVIF exports should complete through the real encoder");
    }
    for (const auto& job :
         {hdr_pq_job, hdr_hlg_job}) {
      const auto artifact =
          job.value(
                 QStringLiteral(
                     "artifactVerification"))
              .toMap();
      if (artifact
                  .value(
                      QStringLiteral("schema"))
                  .toString() !=
              QStringLiteral(
                  "export-artifact-verification:v2") ||
          !artifact
               .value(
                   QStringLiteral("valid"))
               .toBool() ||
          !artifact
               .value(
                   QStringLiteral(
                       "pixelsVerified"))
               .toBool() ||
          !artifact
               .value(
                   QStringLiteral(
                       "profileVerified"))
               .toBool() ||
          !artifact
               .value(
                   QStringLiteral(
                       "transferFunctionVerified"))
               .toBool() ||
          !artifact
               .value(
                   QStringLiteral(
                       "metadataApplicable"))
               .toBool() ||
          !artifact
               .value(
                   QStringLiteral(
                       "metadataVerified"))
               .toBool() ||
          !artifact
               .value(
                   QStringLiteral(
                       "masteringFieldsApplicable"))
               .toBool() ||
          !artifact
               .value(
                   QStringLiteral(
                       "masteringFieldsVerified"))
               .toBool()) {
        return fail(
            "HDR AVIF artifact verification must prove encoded pixels, Rec.2100 transfer metadata, and mastering fields");
      }
    }
    const auto pq_inspection = photo_editor::io::inspect_hdr_avif(
        hdr_pq_job.value(QStringLiteral("outputPath")).toString().toStdString());
    const auto hlg_inspection = photo_editor::io::inspect_hdr_avif(
        hdr_hlg_job.value(QStringLiteral("outputPath")).toString().toStdString());
    if (!pq_inspection.valid || !hlg_inspection.valid ||
        pq_inspection.width != 2 || pq_inspection.height != 1 ||
        pq_inspection.bit_depth != 10 || hlg_inspection.bit_depth != 10 ||
        pq_inspection.color_primaries != 9 || hlg_inspection.color_primaries != 9 ||
        pq_inspection.transfer_characteristics != 16 ||
        hlg_inspection.transfer_characteristics != 18 ||
        pq_inspection.matrix_coefficients != 9 ||
        hlg_inspection.matrix_coefficients != 9 ||
        pq_inspection.max_content_light_level != 1000 ||
        pq_inspection.max_frame_average_light_level != 400 ||
        !pq_inspection.has_mastering_display ||
        !hlg_inspection.has_mastering_display ||
        std::abs(pq_inspection.mastering_peak_luminance_nits - 1000.0) > 0.1 ||
        pq_inspection.encoded_rgb.empty() || hlg_inspection.encoded_rgb.empty()) {
      return fail("HDR AVIF artifact readback must prove 10-bit Rec.2100 PQ/HLG and mastering metadata");
    }
  }

  const auto copy_job_index = controller.jobCount();
  if (!controller.queueExport(
          QStringLiteral("asset-copy"),
          QStringLiteral("photo"),
          QStringLiteral("Original Copy"),
          source_path,
          QString(),
          QString(),
          QStringLiteral("Original Copy"),
          export_dir)) {
    return fail("original copy export should queue successfully");
  }

  if (!wait_for_job(&controller, copy_job_index)) {
    return fail("original copy export should finish");
  }

  const auto copied_job = controller.jobs().at(copy_job_index).toMap();
  if (copied_job.value(QStringLiteral("status")).toString() != QStringLiteral("Completed")) {
    return fail("original copy export should complete successfully");
  }

  const auto copied_path = copied_job.value(QStringLiteral("outputPath")).toString();
  const QFileInfo copied_info(copied_path);
  if (copied_info.suffix().toLower() != QStringLiteral("png")) {
    return fail("original copy export should preserve the source file extension");
  }

  QFile source_file(source_path);
  QFile copied_file(copied_path);
  if (!source_file.open(QIODevice::ReadOnly) || !copied_file.open(QIODevice::ReadOnly)) {
    return fail("exported original copy should be readable");
  }
  if (source_file.readAll() != copied_file.readAll()) {
    return fail("original copy export should preserve the original file bytes");
  }
  const auto copied_integrity = copied_job.value(QStringLiteral("metadata"))
                                    .toMap()
                                    .value(QStringLiteral("outputIntegrity"))
                                    .toMap();
  const auto copied_integrity_sidecar =
      copied_integrity.value(QStringLiteral("sidecarPath")).toString();
  if (!copied_integrity.value(QStringLiteral("valid")).toBool() ||
      copied_integrity.value(QStringLiteral("schema")).toString() !=
          QStringLiteral("delivery-integrity:v1") ||
      copied_integrity.value(QStringLiteral("algorithm")).toString() !=
          QStringLiteral("sha256-full-v1") ||
      copied_integrity.value(QStringLiteral("sha256")).toString().size() != 64 ||
      copied_integrity.value(QStringLiteral("fileBytes")).toLongLong() != copied_info.size() ||
      !QFileInfo(copied_integrity_sidecar).isReadable()) {
    return fail("completed delivery should persist a full SHA-256 and adjacent integrity manifest");
  }
  QFile copied_integrity_manifest(copied_integrity_sidecar);
  if (!copied_integrity_manifest.open(QIODevice::ReadOnly) ||
      QJsonDocument::fromJson(copied_integrity_manifest.readAll())
              .object()
              .value(QStringLiteral("sha256")).toString() !=
          copied_integrity.value(QStringLiteral("sha256")).toString()) {
    return fail("delivery job metadata and the exported integrity manifest should agree");
  }
  copied_file.close();
  QFile tampered_copy(copied_path);
  if (!tampered_copy.open(QIODevice::Append) || tampered_copy.write("x", 1) != 1) {
    return fail("the delivery-integrity fixture should permit a post-export tamper");
  }
  tampered_copy.close();
  if (!tampered_copy.open(QIODevice::ReadOnly) ||
      QString::fromLatin1(
          QCryptographicHash::hash(tampered_copy.readAll(), QCryptographicHash::Sha256).toHex()) ==
          copied_integrity.value(QStringLiteral("sha256")).toString()) {
    return fail("the persisted delivery checksum should expose a post-export byte change");
  }

  controller.clearFinishedJobs();
  if (controller.jobCount() != 0) {
    return fail("clearFinishedJobs should empty the queue before recovery testing");
  }
  phase("restart-recovery");
  // Simulate a restart, not two simultaneously active dispatchers observing
  // one catalog. Every preceding job has reached its terminal state.
  runner.reset();

  photo_editor::desktop::PersistentJobRecord recovered_record;
  recovered_record.job_id = QStringLiteral("recovered-export");
  recovered_record.job_kind = QStringLiteral("asset_export");
  recovered_record.asset_id = QStringLiteral("asset-recovered");
  recovered_record.media_type = QStringLiteral("photo");
  recovered_record.display_label = QStringLiteral("Recovered Export");
  recovered_record.source_path = source_path;
  recovered_record.preset_name = QStringLiteral("Original Copy");
  recovered_record.destination_path = export_dir;
  recovered_record.status = QStringLiteral("Running");
  recovered_record.created_at = QStringLiteral("2026-03-31T00:00:00Z");
  recovered_record.updated_at = recovered_record.created_at;
  if (!photo_editor::desktop::upsertPersistedJobRecord(recovered_record, nullptr)) {
    return fail("recovered export job should be persisted");
  }

  photo_editor::desktop::JobRunner recovered_runner;
  photo_editor::desktop::ExportJobController recovered_controller;
  bool recovery_finished = false;
  QString recovery_error;
  QObject::connect(&recovered_runner, &photo_editor::desktop::JobRunner::startupRecoveryFinished,
      &app, [&](const QString& error) { recovery_finished = true; recovery_error = error; });
  recovered_runner.initializeFromStore();
  QElapsedTimer recovered_startup_timer;
  recovered_startup_timer.start();
  while (!recovery_finished && recovered_startup_timer.elapsed() < 15000) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
    QThread::msleep(1);
  }
  if (!recovery_finished || !recovery_error.isEmpty()) {
    std::cerr << "Restart recovery: " << recovery_error.toStdString() << '\n';
    return fail("restarted export runner must recover before publishing job history");
  }
  recovered_controller.initializeFromStore();
  QElapsedTimer recovery_history_timer;
  recovery_history_timer.start();
  while (recovered_controller.loading() && recovery_history_timer.elapsed() < 10000) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    QThread::msleep(1);
  }
  if (recovered_controller.loading() || !recovered_controller.historyError().isEmpty()) {
    std::cerr << "Recovery history loading=" << recovered_controller.loading()
              << " error=" << recovered_controller.historyError().toStdString()
              << " elapsed=" << recovery_history_timer.elapsed() << " count=" << recovered_controller.jobCount() << '\n';
    return fail("recovered export history should finish loading without error");
  }
  if (recovered_controller.jobCount() < 1) {
    return fail("recovered export jobs should load into the queue");
  }
  QVariantMap recovered_job;
  for (const auto& value : recovered_controller.jobs()) {
    const auto job = value.toMap();
    if (job.value(QStringLiteral("jobId")).toString() == QStringLiteral("recovered-export")) {
      recovered_job = job;
      break;
    }
  }
  const auto recovery_metadata = recovered_job.value(QStringLiteral("metadata")).toMap();
  if (recovered_job.value(QStringLiteral("status")).toString() != QStringLiteral("Paused") ||
      !recovery_metadata.value(QStringLiteral("requiresRecoveryConfirmation")).toBool() ||
      recovery_metadata.value(QStringLiteral("recoveryAction")).toString() !=
          QStringLiteral("awaiting-confirmation")) {
    return fail("recovered export should pause and require confirmation before another external write");
  }
  QString recovered_resume_error;
  if (!photo_editor::desktop::resumePersistedJobRecord(
          QStringLiteral("recovered-export"), &recovered_resume_error)) {
    return fail("recovered export should resume after explicit confirmation");
  }
  if (!wait_for_job_id(&recovered_controller, QStringLiteral("recovered-export"), 8000)) {
    return fail("confirmed recovered export should complete successfully");
  }

  recovered_job.clear();
  for (const auto& value : recovered_controller.jobs()) {
    const auto job = value.toMap();
    if (job.value(QStringLiteral("jobId")).toString() == QStringLiteral("recovered-export")) {
      recovered_job = job;
      break;
    }
  }
  if (recovered_job.isEmpty()) {
    return fail("recovered export should remain visible in the controller");
  }
  if (recovered_job.value(QStringLiteral("status")).toString() != QStringLiteral("Completed")) {
    return fail("recovered export should complete successfully");
  }
  const auto resumed_metadata = recovered_job.value(QStringLiteral("metadata")).toMap();
  if (resumed_metadata.value(QStringLiteral("requiresRecoveryConfirmation")).toBool() ||
      resumed_metadata.value(QStringLiteral("recoveryAction")).toString() !=
          QStringLiteral("resumed-after-confirmation")) {
    return fail("explicit resume should clear the recovery write confirmation and record the action");
  }
  if (!QFileInfo::exists(recovered_job.value(QStringLiteral("outputPath")).toString())) {
    return fail("recovered export should write its output file");
  }
  phase("restart-recovery-verified");

  const auto subject_source_path = temp_dir.filePath(QStringLiteral("subject-source.png"));
  QImage subject_source(64, 32, QImage::Format_ARGB32);
  subject_source.fill(QColor(96, 96, 96));
  if (!subject_source.save(subject_source_path)) {
    return fail("subject source image should be written for export testing");
  }

  photo_editor::desktop::EditRecipeState subject_state;
  QVariantMap subject_mask;
  subject_mask.insert(QStringLiteral("id"), QStringLiteral("subject-export"));
  subject_mask.insert(QStringLiteral("name"), QStringLiteral("AI Subject"));
  subject_mask.insert(QStringLiteral("kind"), QStringLiteral("subject"));
  subject_mask.insert(QStringLiteral("operation"), QStringLiteral("add"));
  subject_mask.insert(QStringLiteral("enabled"), true);
  subject_mask.insert(QStringLiteral("exposure"), 1.0);
  subject_mask.insert(QStringLiteral("temperature"), 0.0);
  subject_mask.insert(QStringLiteral("feather"), 0.05);
  subject_mask.insert(QStringLiteral("segmentationData"), QByteArray::fromHex("ff00"));
  subject_mask.insert(QStringLiteral("segmentationWidth"), 2);
  subject_mask.insert(QStringLiteral("segmentationHeight"), 1);
  subject_state.mask_stack = {subject_mask};
  const auto subject_recipe_json = photo_editor::desktop::serialize_edit_recipe_json(subject_state);

  const auto subject_job_index = controller.jobCount();
  if (!controller.queueExport(
          QStringLiteral("asset-subject"),
          QStringLiteral("photo"),
          QStringLiteral("Subject Export"),
          subject_source_path,
          QString(),
          subject_recipe_json,
          QStringLiteral("Full Resolution"),
          export_dir)) {
    return fail("subject mask export should queue successfully");
  }

  if (!wait_for_job(&controller, subject_job_index)) {
    return fail("subject mask export should finish");
  }

  const auto subject_job = controller.jobs().at(subject_job_index).toMap();
  if (subject_job.value(QStringLiteral("status")).toString() != QStringLiteral("Completed")) {
    return fail("subject mask export should complete successfully");
  }
  const auto subject_export_render_job =
      subject_job.value(QStringLiteral("exportRenderJob")).toMap();
  const auto subject_export_preview_job =
      subject_job.value(QStringLiteral("exportPreviewJob")).toMap();
  const auto subject_metadata = subject_job.value(QStringLiteral("metadata")).toMap();
  const auto subject_export_mask_trace =
      subject_export_render_job.value(QStringLiteral("maskAlphaTrace")).toMap();
  const auto subject_export_mask_trace_from_metadata =
      subject_metadata
          .value(QStringLiteral("exportRenderJob"))
          .toMap()
          .value(QStringLiteral("maskAlphaTrace"))
          .toMap();
  const auto subject_export_preview_job_from_metadata =
      subject_metadata.value(QStringLiteral("exportPreviewJob")).toMap();
  const auto subject_preview_export_diff =
      subject_job.value(QStringLiteral("previewExportDiff")).toMap();
  const auto subject_preview_export_diff_from_metadata =
      subject_metadata.value(QStringLiteral("previewExportDiff")).toMap();
  if (subject_export_render_job.value(QStringLiteral("jobKind")).toString() !=
          QStringLiteral("full_resolution_export") ||
      subject_export_render_job.value(QStringLiteral("stage")).toString() !=
          QStringLiteral("export_render") ||
      subject_export_preview_job.value(QStringLiteral("jobKind")).toString() !=
          QStringLiteral("full_resolution_export_preview") ||
      subject_export_preview_job.value(QStringLiteral("stage")).toString() !=
          QStringLiteral("export_preview_diff") ||
      subject_export_preview_job.value(QStringLiteral("exportRenderJobId")).toString() !=
          subject_export_render_job.value(QStringLiteral("jobId")).toString() ||
      !subject_export_render_job.value(QStringLiteral("stateSignature"))
           .toString()
           .startsWith(QStringLiteral("sha1:")) ||
      !subject_export_preview_job.value(QStringLiteral("stateSignature"))
           .toString()
           .startsWith(QStringLiteral("sha1:")) ||
      subject_export_preview_job_from_metadata.value(QStringLiteral("jobId")).toString() !=
          subject_export_preview_job.value(QStringLiteral("jobId")).toString() ||
      subject_preview_export_diff.value(QStringLiteral("exportPreviewJobId")).toString() !=
          subject_export_preview_job.value(QStringLiteral("jobId")).toString() ||
      subject_preview_export_diff.value(QStringLiteral("exportPreviewStateSignature")).toString() !=
          subject_export_preview_job.value(QStringLiteral("stateSignature")).toString()) {
    return fail("subject mask export should bind render, preview, and diff jobs to one state signature");
  }
  if (!subject_preview_export_diff.value(QStringLiteral("available")).toBool() ||
      !subject_preview_export_diff.value(QStringLiteral("withinTolerance")).toBool() ||
      subject_preview_export_diff.value(QStringLiteral("status")).toString() !=
          QStringLiteral("matched") ||
      subject_preview_export_diff.value(QStringLiteral("maxChannelDelta")).toInt() > 1 ||
      subject_preview_export_diff.value(QStringLiteral("tolerance")).toInt() != 1 ||
      subject_preview_export_diff_from_metadata.value(QStringLiteral("status")).toString() !=
          QStringLiteral("matched")) {
    return fail("subject mask export preview should match the final encoded export within tolerance");
  }
  if (subject_export_mask_trace.value(QStringLiteral("mask_source_count")).toInt() != 1 ||
      subject_export_mask_trace.value(QStringLiteral("mask_source_ids")).toList().value(0).toString() !=
          QStringLiteral("subject-export") ||
      !subject_export_mask_trace.value(QStringLiteral("mask_source_hash"))
           .toString()
           .startsWith(QStringLiteral("sha1:")) ||
      !subject_export_mask_trace.value(QStringLiteral("mask_alpha_trace_hash"))
           .toString()
           .startsWith(QStringLiteral("sha1:")) ||
      !subject_export_mask_trace.value(QStringLiteral("mask_alpha_trace_signature"))
           .toString()
           .contains(QStringLiteral("export-render")) ||
      !subject_export_mask_trace.value(QStringLiteral("mask_alpha_trace_signature"))
           .toString()
           .contains(QStringLiteral("provider:export-render")) ||
      subject_export_mask_trace.value(QStringLiteral("mask_alpha_provider_contract")).toString() !=
          QStringLiteral("shared-mask-alpha-provider-v1") ||
      subject_export_mask_trace.value(QStringLiteral("mask_alpha_provider")).toString() !=
          QStringLiteral("export-render") ||
      !subject_export_mask_trace
           .value(QStringLiteral("mask_alpha_provider_signature"))
           .toString()
           .contains(QStringLiteral("provider:export-render")) ||
      subject_export_mask_trace.value(QStringLiteral("mask_alpha_full_resolution_available")).toBool() !=
          true ||
      subject_export_mask_trace
              .value(QStringLiteral("mask_alpha_component_providers"))
              .toList()
              .value(0)
              .toString() != QStringLiteral("raster-payload") ||
      !subject_export_mask_trace.value(QStringLiteral("mask_source_revision_hash"))
           .toString()
           .startsWith(QStringLiteral("sha1:")) ||
      subject_export_mask_trace.value(QStringLiteral("mask_alpha_raster_width")).toInt() != 2 ||
      subject_export_mask_trace.value(QStringLiteral("mask_alpha_raster_height")).toInt() != 1 ||
      subject_export_mask_trace_from_metadata.value(QStringLiteral("mask_alpha_trace_hash")).toString() !=
          subject_export_mask_trace.value(QStringLiteral("mask_alpha_trace_hash")).toString()) {
    return fail("subject mask export should publish exact mask alpha trace metadata in the export render job");
  }

  QImage subject_rendered_image(subject_job.value(QStringLiteral("outputPath")).toString());
  if (subject_rendered_image.isNull()) {
    return fail("subject mask export should produce a readable image");
  }

  const auto left_sample = subject_rendered_image.pixelColor(subject_rendered_image.width() / 4, subject_rendered_image.height() / 2);
  const auto right_sample = subject_rendered_image.pixelColor(subject_rendered_image.width() * 3 / 4, subject_rendered_image.height() / 2);
  if (!(left_sample.red() > right_sample.red() + 20 &&
        left_sample.green() > right_sample.green() + 20 &&
        left_sample.blue() > right_sample.blue() + 20)) {
    return fail("subject mask export should apply local adjustments to the segmented region");
  }

  const auto removal_source_path = temp_dir.filePath(QStringLiteral("removal-source.png"));
  QImage removal_source(33, 33, QImage::Format_ARGB32);
  removal_source.fill(QColor(96, 96, 96));
  removal_source.setPixelColor(16, 16, QColor(240, 32, 32));
  if (!removal_source.save(removal_source_path)) {
    return fail("removal source image should be written for export testing");
  }

  photo_editor::desktop::EditRecipeState removal_state;
  QVariantMap removal_mask;
  removal_mask.insert(QStringLiteral("id"), QStringLiteral("remove-export"));
  removal_mask.insert(QStringLiteral("name"), QStringLiteral("Remove Object"));
  removal_mask.insert(QStringLiteral("kind"), QStringLiteral("object"));
  removal_mask.insert(QStringLiteral("operation"), QStringLiteral("add"));
  removal_mask.insert(QStringLiteral("enabled"), true);
  removal_mask.insert(QStringLiteral("removeStrength"), 100.0);
  removal_mask.insert(QStringLiteral("feather"), 0.05);
  QByteArray removal_segmentation(33 * 33, '\0');
  removal_segmentation[16 * 33 + 16] = static_cast<char>(0xff);
  removal_mask.insert(QStringLiteral("segmentationData"), removal_segmentation);
  removal_mask.insert(QStringLiteral("segmentationWidth"), 33);
  removal_mask.insert(QStringLiteral("segmentationHeight"), 33);
  removal_state.mask_stack = {removal_mask};
  const auto removal_recipe_json = photo_editor::desktop::serialize_edit_recipe_json(removal_state);

  const auto removal_job_index = controller.jobCount();
  if (!controller.queueExport(
          QStringLiteral("asset-remove"),
          QStringLiteral("photo"),
          QStringLiteral("Removal Export"),
          removal_source_path,
          QString(),
          removal_recipe_json,
          QStringLiteral("Full Resolution"),
          export_dir)) {
    return fail("removal export should queue successfully");
  }

  if (!wait_for_job(&controller, removal_job_index)) {
    return fail("removal export should finish");
  }

  const auto removal_job = controller.jobs().at(removal_job_index).toMap();
  if (removal_job.value(QStringLiteral("status")).toString() != QStringLiteral("Completed")) {
    return fail("removal export should complete successfully");
  }

  QImage removal_rendered_image(removal_job.value(QStringLiteral("outputPath")).toString());
  if (removal_rendered_image.isNull()) {
    return fail("removal export should produce a readable image");
  }

  const auto removal_center = removal_rendered_image.pixelColor(16, 16);
  if (!(removal_center.green() > 60 && removal_center.blue() > 60 &&
        removal_center.red() < 180 &&
        std::abs(removal_center.red() - removal_center.green()) < 12 &&
        std::abs(removal_center.green() - removal_center.blue()) < 12)) {
    std::cerr << "removal center=" << removal_center.red() << ","
              << removal_center.green() << "," << removal_center.blue()
              << '\n';
    return fail("removal export should fill the removed object from surrounding pixels");
  }

  controller.clearFinishedJobs();
  const auto delivery_source_dir = temp_dir.filePath(QStringLiteral("delivery-sources"));
  phase("delivery-preflight");
  const auto delivery_export_dir = temp_dir.filePath(QStringLiteral("delivery-exports"));
  if (!QDir().mkpath(delivery_source_dir) || !QDir().mkpath(delivery_export_dir)) {
    return fail("delivery export proof folders should be created");
  }

  QVariantList delivery_assets;
  QVariantList ready_delivery_assets;
  photo_editor::desktop::EditRecipeState delivery_recipe_state;
  delivery_recipe_state.output_color_space = QStringLiteral("sRGB");
  const auto delivery_adjustment_payload =
      photo_editor::desktop::serialize_adjustment_patch_json(
          delivery_recipe_state,
          photo_editor::desktop::all_adjustment_groups(),
          photo_editor::desktop::AdjustmentPatchScope::AssetBound);
  for (int index = 1; index <= 20; ++index) {
    const auto base_name = QStringLiteral("DELIVERY_%1").arg(index, 2, 10, QLatin1Char('0'));
    const auto source = QDir(delivery_source_dir).filePath(base_name + QStringLiteral(".png"));
    if (index < 20) {
      QImage delivery_image(32, 24, QImage::Format_RGB32);
      delivery_image.fill(QColor(24 + index * 5, 48 + index * 3, 96 + index * 2));
      if (!delivery_image.save(source)) {
        return fail("delivery source fixtures should be written");
      }
    }
    const QVariantMap asset{
        {QStringLiteral("assetId"), QStringLiteral("delivery-%1").arg(index)},
        {QStringLiteral("mediaType"), QStringLiteral("photo")},
        {QStringLiteral("filePath"), source},
        {QStringLiteral("adjustmentPatchJson"), delivery_adjustment_payload},
        {QStringLiteral("displayLabel"), base_name},
    };
    delivery_assets.push_back(asset);
    if (index < 20) {
      ready_delivery_assets.push_back(asset);
    }
  }

  const auto existing_collision_path =
      QDir(delivery_export_dir).filePath(QStringLiteral("DELIVERY_01-web-proof.jpg"));
  QFile existing_collision(existing_collision_path);
  if (!existing_collision.open(QIODevice::WriteOnly) ||
      existing_collision.write("preserve-existing-delivery") <= 0) {
    return fail("delivery collision fixture should be created");
  }
  existing_collision.close();

  const auto delivery_preflight = controller.batchExportPreflight(
      delivery_assets, QStringLiteral("Web Proof"), delivery_export_dir);
  const auto delivery_preflight_items =
      delivery_preflight.value(QStringLiteral("items")).toList();
  if (delivery_preflight.value(QStringLiteral("schema")).toString() !=
          QStringLiteral("export-batch-preflight:v1") ||
      delivery_preflight.value(QStringLiteral("assetCount")).toInt() != 20 ||
      delivery_preflight.value(QStringLiteral("readyCount")).toInt() != 19 ||
      delivery_preflight.value(QStringLiteral("blockedCount")).toInt() != 1 ||
      delivery_preflight.value(QStringLiteral("missingOriginalCount")).toInt() != 1 ||
      delivery_preflight.value(QStringLiteral("collisionCount")).toInt() != 1 ||
      delivery_preflight_items.size() != 20 ||
      delivery_preflight_items.at(0).toMap().value(QStringLiteral("outputFileName")).toString() !=
          QStringLiteral("DELIVERY_01-web-proof-2.jpg") ||
      !delivery_preflight_items.at(0).toMap().value(QStringLiteral("collision")).toBool()) {
    return fail("20-photo delivery preflight should identify the missing original and preserve a colliding output with an exact numbered filename");
  }

  const auto unavailable_destination =
      temp_dir.filePath(QStringLiteral("temporarily-unavailable-destination"));
  QFile unavailable_destination_file(unavailable_destination);
  if (!unavailable_destination_file.open(QIODevice::WriteOnly) ||
      unavailable_destination_file.write("temporarily unavailable") <= 0) {
    return fail("temporary unavailable destination fixture should be created");
  }
  unavailable_destination_file.close();
  const auto unavailable_preflight = controller.batchExportPreflight(
      delivery_assets, QStringLiteral("Web Proof"), unavailable_destination);
  if (unavailable_preflight.value(QStringLiteral("unavailableDestinationCount")).toInt() != 20 ||
      unavailable_preflight.value(QStringLiteral("blockedCount")).toInt() != 20) {
    return fail("batch preflight should block every delivery item when the destination is unavailable");
  }

  if (controller.queueBatchExport(
          ready_delivery_assets,
          QStringLiteral("Web Proof"),
          delivery_export_dir) != 19) {
    return fail("the 19 ready delivery items should queue after the missing original is removed");
  }

  phase("delivery-submitted");
  // queueBatchExport now acknowledges in-memory submission. Preserve the
  // previous delivery timer boundary (all rows durably admitted), and give
  // admission its own bounded assertion rather than mistaking Saving for Queued.
  QElapsedTimer admission_wait;
  admission_wait.start();
  while (controller.pendingAdmissionCount() > 0 && admission_wait.elapsed() < 30000) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
  }
  if (controller.pendingAdmissionCount() != 0 || !controller.admissionError().isEmpty()) {
    std::cerr << "delivery admission pending=" << controller.pendingAdmissionCount()
              << " error=" << controller.admissionError().toStdString() << '\n';
    return fail("all delivery submissions should receive durable catalog confirmation within 30 seconds");
  }
  QElapsedTimer delivery_wait;
  phase("delivery-queued");
  delivery_wait.start();
  while (delivery_wait.elapsed() < 60000) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    auto terminal = 0;
    for (const auto& value : controller.jobs()) {
      const auto status = value.toMap().value(QStringLiteral("status")).toString();
      if (status == QStringLiteral("Completed") || status == QStringLiteral("Failed")) {
        ++terminal;
      }
    }
    if (terminal == 19) {
      break;
    }
  }
  if (controller.jobCount() != 19) {
    return fail("delivery batch should retain one visible row per queued item");
  }
  for (const auto& value : controller.jobs()) {
    const auto job = value.toMap();
    const auto metadata = job.value(QStringLiteral("metadata")).toMap();
    const auto summary = metadata.value(QStringLiteral("deliverySummary")).toMap();
    const auto output_integrity = metadata.value(QStringLiteral("outputIntegrity")).toMap();
    if (job.value(QStringLiteral("status")).toString() != QStringLiteral("Completed") ||
        !QFileInfo::exists(job.value(QStringLiteral("outputPath")).toString()) ||
        summary.value(QStringLiteral("schema")).toString() !=
            QStringLiteral("export-delivery-summary:v1") ||
        summary.value(QStringLiteral("format")).toString() != QStringLiteral("JPEG") ||
        summary.value(QStringLiteral("bitDepth")).toInt() != 8 ||
        summary.value(QStringLiteral("width")).toInt() != 32 ||
        summary.value(QStringLiteral("height")).toInt() != 24 ||
        summary.value(QStringLiteral("outputProfile")).toString() != QStringLiteral("sRGB") ||
        !summary.value(QStringLiteral("profileVerified")).toBool() ||
        !summary.value(QStringLiteral("artifactVerified")).toBool() ||
        !output_integrity.value(QStringLiteral("valid")).toBool() ||
        output_integrity.value(QStringLiteral("sha256")).toString().size() != 64 ||
        !QFileInfo(output_integrity.value(QStringLiteral("sidecarPath")).toString()).isReadable() ||
        !summary.value(QStringLiteral("metadataPolicy")).toString().contains(QStringLiteral("camera metadata is not copied")) ||
        summary.value(QStringLiteral("sharpeningPolicy")).toString() !=
            QStringLiteral("No additional output sharpening.")) {
      std::cerr << "delivery verification asset=" << job.value(QStringLiteral("assetId")).toString().toStdString()
                << " status=" << job.value(QStringLiteral("status")).toString().toStdString()
                << " error=" << job.value(QStringLiteral("errorMessage")).toString().toStdString()
                << " summary=" << QJsonDocument(QJsonObject::fromVariantMap(summary)).toJson(QJsonDocument::Compact).toStdString()
                << '\n';
      return fail("every successful delivery should report verified format, profile, dimensions, metadata policy, and sharpening policy");
    }
  }
  QFile preserved_collision(existing_collision_path);
  if (!preserved_collision.open(QIODevice::ReadOnly) ||
      preserved_collision.readAll() != QByteArrayLiteral("preserve-existing-delivery")) {
    return fail("collision handling should preserve the pre-existing output bytes");
  }

  phase("delivery-verified");
  photo_editor::desktop::PersistentJobRecord transient_destination_job;
  transient_destination_job.job_id = QStringLiteral("delivery-transient-destination");
  transient_destination_job.job_kind = QStringLiteral("asset_export");
  transient_destination_job.asset_id = QStringLiteral("delivery-20-retry");
  transient_destination_job.media_type = QStringLiteral("photo");
  transient_destination_job.display_label = QStringLiteral("DELIVERY_20_RETRY");
  transient_destination_job.source_path =
      ready_delivery_assets.first().toMap().value(QStringLiteral("filePath")).toString();
  transient_destination_job.recipe_json = delivery_adjustment_payload;
  transient_destination_job.preset_name = QStringLiteral("Web Proof");
  transient_destination_job.destination_path = unavailable_destination;
  transient_destination_job.status = QStringLiteral("Queued");
  transient_destination_job.created_at =
      QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
  transient_destination_job.updated_at = transient_destination_job.created_at;
  if (!photo_editor::desktop::upsertPersistedJobRecord(transient_destination_job, nullptr) ||
      !wait_for_job_id(&controller, transient_destination_job.job_id, 8000)) {
    return fail("temporarily unavailable destination should produce an inspectable failed export");
  }
  QVariantMap transient_failed_job;
  for (const auto& value : controller.jobs()) {
    if (value.toMap().value(QStringLiteral("jobId")).toString() == transient_destination_job.job_id) {
      transient_failed_job = value.toMap();
      break;
    }
  }
  if (transient_failed_job.value(QStringLiteral("status")).toString() != QStringLiteral("Failed") ||
      !transient_failed_job.value(QStringLiteral("errorMessage")).toString().contains(QStringLiteral("destination"))) {
    return fail("runtime destination loss should fail only that export with a useful destination error");
  }

  if (!QFile::remove(unavailable_destination) || !QDir().mkpath(unavailable_destination)) {
    return fail("temporary destination should become available for retry");
  }
  controller.retryJob(transient_destination_job.job_id);
  if (!wait_for_job_id(&controller, transient_destination_job.job_id, 12000)) {
    return fail("the failed destination export should finish after retry");
  }
  QVariantMap retried_destination_job;
  for (const auto& value : controller.jobs()) {
    const auto job = value.toMap();
    if (job.value(QStringLiteral("jobId")).toString() == transient_destination_job.job_id) {
      retried_destination_job = job;
      continue;
    }
    if (job.value(QStringLiteral("metadata")).toMap().value(QStringLiteral("retryCount")).toInt() != 0) {
      return fail("retry-failed should not rerun successful delivery items");
    }
  }
  if (retried_destination_job.value(QStringLiteral("status")).toString() != QStringLiteral("Completed") ||
      retried_destination_job.value(QStringLiteral("metadata")).toMap().value(QStringLiteral("retryCount")).toInt() != 1 ||
      controller.revealTarget(transient_destination_job.job_id) !=
          retried_destination_job.value(QStringLiteral("outputPath")).toString()) {
    return fail("retry should run only the failed item and expose its completed file as the Finder reveal target");
  }

  phase("all-assertions-passed");
  return EXIT_SUCCESS;
}
