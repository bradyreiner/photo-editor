#include "raw_decode_prewarm_queue.h"
#include "raw_image_decoder.h"
#include "catalog_asset_model.h"
#include "photo_render_utils.h"
#include "photo_export_task.h"
#include "source_image_decode.h"
#include "source_image_decode_cache.h"
#include "decoded_image_cache.h"
#include "observability.h"
#include "fixtures/synthetic_dng.h"

#include <atomic>
#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <iostream>
#include <mutex>
#include <optional>
#include <vector>

#include <QCoreApplication>
#include <QByteArrayView>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QImageReader>
#include <QImageWriter>
#include <QColorSpace>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSemaphore>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QThread>
#include <QThreadPool>
#include <QTransform>
#include <QTimer>
#include <QUrl>
#include <QtConcurrent>

namespace {

using namespace photo_editor::desktop;
using photo_editor::pipeline::CancellationToken;

struct DecodedPhoto final {
  QString asset_id;
  QString path;
  QByteArray pixel_sha256;
  QSize size;
  qint64 decode_ms = 0;
};

struct MixedExportResult final {
  bool success = false;
  QByteArray rendered_pixel_sha256;
  QByteArray output_file_sha256;
  QString backend;
  QString canonical_plan_error;
  qint64 canonical_plan_ms = 0;
  qint64 render_ms = 0;
  qint64 encode_and_atomic_publish_ms = 0;
  qint64 bytes = 0;
};

int fail(const char* message) {
  std::cerr << message << '\n';
  return 1;
}

QByteArray pixel_sha256(const QImage& source) {
  const auto image = source.convertToFormat(QImage::Format_RGBA8888);
  if (image.isNull()) return {};
  QCryptographicHash hash(QCryptographicHash::Sha256);
  const auto row_bytes = static_cast<qsizetype>(image.width()) * 4;
  for (int y = 0; y < image.height(); ++y) {
    hash.addData(QByteArrayView(
        reinterpret_cast<const char*>(image.constScanLine(y)), row_bytes));
  }
  return hash.result().toHex();
}

bool write_photo_variants(const QString& fixture, const QString& output_dir, QStringList* paths) {
  if (paths == nullptr) return false;
  QImageReader reader(fixture);
  reader.setAutoTransform(true);
  const auto photo = reader.read();
  if (photo.isNull()) return false;
  const auto enlarged = photo.scaled(4096, 4096, Qt::KeepAspectRatio, Qt::SmoothTransformation);
  if (enlarged.isNull()) return false;

  const auto variants = std::array<QImage, 3>{
      enlarged,
      enlarged.transformed(QTransform::fromScale(-1.0, 1.0)),
      enlarged.transformed(QTransform::fromScale(1.0, -1.0)),
  };
  for (std::size_t index = 0; index < variants.size(); ++index) {
    const auto path = QDir(output_dir).filePath(QStringLiteral("photo-%1.jpg").arg(index));
    QImageWriter writer(path, "JPEG");
    writer.setQuality(94);
    if (!writer.write(variants[index])) return false;
    paths->push_back(path);
  }
  return true;
}

bool write_synthetic_raw(const QString& path) {
  const auto bytes = photo_editor::test_fixtures::synthetic_dng();
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) return false;
  return file.write(reinterpret_cast<const char*>(bytes.data()),
                    static_cast<qint64>(bytes.size())) == static_cast<qint64>(bytes.size());
}

photo_editor::desktop::CatalogAssetItem make_photo_asset(const QString& id, const QString& path) {
  auto item = photo_editor::desktop::CatalogAssetItem{};
  item.asset_id = id;
  item.file_path = path;
  item.media_type = QStringLiteral("photo");
  item.online = true;
  item.recipe_loaded = true;
  return item;
}

QByteArray file_sha256(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) return {};
  QCryptographicHash hash(QCryptographicHash::Sha256);
  while (!file.atEnd()) {
    const auto chunk = file.read(1024 * 1024);
    if (chunk.isEmpty() && !file.atEnd()) return {};
    hash.addData(chunk);
  }
  return hash.result().toHex();
}

bool wait_until(const std::function<bool()>& ready, int timeout_ms) {
  QElapsedTimer timer;
  timer.start();
  while (!ready() && timer.elapsed() < timeout_ms) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    QThread::msleep(1);
  }
  return ready();
}

QString thumbnail_local_path(const photo_editor::desktop::CatalogAssetModel& model, int row) {
  const auto source = model.data(
      model.index(row, 0), photo_editor::desktop::CatalogAssetModel::ThumbnailSourceRole).toString();
  return QUrl(source).toLocalFile();
}

bool copy_photo_preflight_sources(const QString& source, const QString& directory, int count) {
  if (!QDir().mkpath(directory)) return false;
  for (int index = 0; index < count; ++index) {
    const auto path = QDir(directory).filePath(QStringLiteral("import-%1.jpg").arg(index, 5, 10, QLatin1Char('0')));
    if (!QFile::copy(source, path)) return false;
  }
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));
  QGuiApplication app(argc, argv);
  using namespace photo_editor::desktop;
#ifndef PHOTO_EDITOR_ROUND10_PHOTO_FIXTURE
  return fail("PHOTO_EDITOR_ROUND10_PHOTO_FIXTURE must point to a bundled photographic JPEG.");
#else
  QTemporaryDir runtime;
  QTemporaryDir photo_dir;
  if (!runtime.isValid() || !photo_dir.isValid()) return fail("Could not create isolated workload directories.");
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", runtime.path().toUtf8());
  qputenv("PHOTO_EDITOR_CACHE_DIR", runtime.filePath("cache").toUtf8());
  qputenv("PHOTO_EDITOR_MANAGED_MEDIA_DIR", runtime.filePath("managed-media").toUtf8());
  qputenv("PHOTO_EDITOR_ORIGINALS_BACKUP_DIR", runtime.filePath("originals-backup").toUtf8());
  qputenv("PHOTO_EDITOR_DATABASE_URL", QByteArrayLiteral("postgresql://127.0.0.1:1/round10_workload"));
  qputenv("PHOTO_EDITOR_ENABLE_DEV_OBSERVABILITY", QByteArrayLiteral("1"));
  const auto log_dir = runtime.filePath(QStringLiteral("logs"));
  qputenv("PHOTO_EDITOR_OBSERVABILITY_RUN_DIR", log_dir.toUtf8());
  observability::initialize();

  QStringList photo_paths;
  if (!write_photo_variants(QStringLiteral(PHOTO_EDITOR_ROUND10_PHOTO_FIXTURE), photo_dir.path(), &photo_paths) ||
      photo_paths.size() != 3) {
    return fail("Could not prepare three decodable JPEG sources from the bundled photo fixture.");
  }

  clear_decoded_image_cache();
  QThreadPool pool;
  pool.setMaxThreadCount(1);
  RawDecodePrewarmQueue queue(&pool);
  QSemaphore first_decode_entered;
  std::atomic_bool first_decode_signaled{false};
  std::atomic_int latest_selected_index{0};
  std::atomic_int canceled_decode_count{0};
  std::atomic_int stale_decode_count{0};
  std::atomic_int completed_decode_count{0};
  std::mutex result_mutex;
  DecodedPhoto published;
  qint64 first_cancel_latency_ms = -1;

  const auto submit_photo = [&](int index) {
    const auto asset_id = QStringLiteral("photo-%1").arg(index);
    const auto path = photo_paths.at(index);
    queue.submit(asset_id, [&, asset_id, path, index](CancellationToken cancellation) {
      auto first_signal_expected = false;
      if (index == 0 && first_decode_signaled.compare_exchange_strong(first_signal_expected, true)) {
        first_decode_entered.release();
      }
      QElapsedTimer decode_timer;
      decode_timer.start();
      QImage decoded;
      const auto success = decode_source_image(path, &decoded, 0, EditRecipeState{}, nullptr, cancellation);
      const auto elapsed_ms = decode_timer.elapsed();
      ++completed_decode_count;
      if (cancellation.cancelled()) {
        ++canceled_decode_count;
        if (index == 0 && first_cancel_latency_ms < 0) first_cancel_latency_ms = elapsed_ms;
        return false;
      }
      if (!success || decoded.isNull()) return false;
      if (latest_selected_index.load(std::memory_order_relaxed) != index) {
        ++stale_decode_count;
        return true;
      }
      std::lock_guard<std::mutex> lock(result_mutex);
      published = DecodedPhoto{asset_id, path, pixel_sha256(decoded), decoded.size(), elapsed_ms};
      return true;
    });
  };
  [[maybe_unused]] const auto join_photo_queue_workers = qScopeGuard([&] {
    queue.cancel();
    if (!pool.waitForDone(30000)) pool.waitForDone();
  });

  submit_photo(0);
  if (!first_decode_entered.tryAcquire(1, 5000)) return fail("The first real-file decode did not start.");
  constexpr int kRapidSelections = 96;
  auto maximum_pending = 0;
  for (int selection = 0; selection < kRapidSelections; ++selection) {
    const auto index = (selection * 7 + 1) % 2;
    latest_selected_index.store(index, std::memory_order_relaxed);
    submit_photo(index);
    maximum_pending = std::max(maximum_pending, queue.snapshot().pending);
  }
  latest_selected_index.store(2, std::memory_order_relaxed);
  submit_photo(2);
  maximum_pending = std::max(maximum_pending, queue.snapshot().pending);
  if (!pool.waitForDone(30000)) return fail("Rapid photo switching did not drain within 30 seconds.");

  QImage expected;
  clear_decoded_image_cache();
  const auto expected_ok = decode_source_image(photo_paths.at(2), &expected, 0, EditRecipeState{});
  const auto expected_hash = pixel_sha256(expected);
  if (!expected_ok || expected.isNull()) return fail("The final selected photo failed its independent decode.");
  {
    std::lock_guard<std::mutex> lock(result_mutex);
    if (published.path != photo_paths.at(2) || published.asset_id != QStringLiteral("photo-2") ||
        published.size != expected.size() || published.pixel_sha256 != expected_hash) {
      return fail("Rapid switching did not publish the exact final photo pixels and identity.");
    }
  }
  const auto photo_snapshot = queue.snapshot();
  if (maximum_pending > 1 || photo_snapshot.pending != 0 || photo_snapshot.failed != 0 ||
      photo_snapshot.started > static_cast<quint64>(kRapidSelections + 2) ||
      canceled_decode_count.load() == 0 || photo_snapshot.replaced == 0) {
    return fail("Rapid switching exceeded the bounded latest-request queue contract.");
  }

  QJsonObject receipt{
      {QStringLiteral("schema"), QStringLiteral("round10-photo-switch-workload:v1")},
      {QStringLiteral("sourceFixture"), QStringLiteral(PHOTO_EDITOR_ROUND10_PHOTO_FIXTURE)},
      {QStringLiteral("sourceKind"), QStringLiteral("bundled-real-photo-jpeg-derived-variants")},
      {QStringLiteral("rapidSelections"), kRapidSelections + 2},
      {QStringLiteral("generatedPhotoSources"), photo_paths.size()},
      {QStringLiteral("finalAssetId"), published.asset_id},
      {QStringLiteral("finalPixelSha256"), QString::fromLatin1(published.pixel_sha256)},
      {QStringLiteral("finalDecodeMs"), published.decode_ms},
      {QStringLiteral("firstCanceledDecodeLatencyMs"), first_cancel_latency_ms},
      {QStringLiteral("completedDecodeCount"), completed_decode_count.load()},
      {QStringLiteral("canceledDecodeCount"), canceled_decode_count.load()},
      {QStringLiteral("staleDecodeCount"), stale_decode_count.load()},
      {QStringLiteral("latestQueueReplacements"), static_cast<qint64>(photo_snapshot.replaced)},
      {QStringLiteral("maximumPending"), maximum_pending},
      {QStringLiteral("realCameraRawPhotoSources"), 0},
      {QStringLiteral("rawPhotoQualification"), QStringLiteral("not-run-no-local-real-raw-input")},
  };

  // Mixed source-derived workload: production thumbnail decode/cache and
  // production import preflight run alongside a changed-recipe render and
  // encoded output file. This is preflight, not committed catalog ingestion.
  constexpr int kPreflightPhotoCopies = 500;
  const auto preflight_root = runtime.filePath(QStringLiteral("preflight-source"));
  if (!copy_photo_preflight_sources(
          QStringLiteral(PHOTO_EDITOR_ROUND10_PHOTO_FIXTURE), preflight_root, kPreflightPhotoCopies)) {
    return fail("Could not prepare source-derived JPEG files for production import preflight.");
  }
  if (!QDir().mkpath(runtime.filePath(QStringLiteral("managed-media"))) ||
      !QDir().mkpath(runtime.filePath(QStringLiteral("originals-backup")))) {
    return fail("Could not prepare isolated import destinations.");
  }

  const auto edited_source_path = photo_paths.at(2);
  const auto source_size = source_image_size(edited_source_path);
  if (!source_size.isValid()) return fail("Could not inspect the export contender source image.");
  QByteArray neutral_hash;
  {
    EditRecipeState neutral_state;
    const auto neutral_plan = RenderPlanStore::shared().admit(
        QStringLiteral("round10-neutral-oracle"), edited_source_path, neutral_state, source_size,
        photo_editor::pipeline::RenderPurpose::Export,
        photo_editor::pipeline::DisplayRenderingTarget::SrgbSimulation);
    if (!neutral_plan.admitted || !neutral_plan.plan.has_value()) {
      std::cerr << "Neutral canonical render plan admission failed: "
                << neutral_plan.error.toStdString() << '\n';
      return fail("The production canonical render plan could not prepare a neutral export oracle.");
    }
    RenderBackendTelemetry telemetry;
    const auto neutral_image = render_output_file(
        edited_source_path, source_size, QRect(QPoint(0, 0), source_size), neutral_state,
        QColorSpace(QColorSpace::SRgb), QImage::Format_RGBA8888, nullptr, &telemetry,
        {}, photo_editor::pipeline::RemovalQuality::Final, RenderOutputPolicy::DisplayReferred,
        nullptr, &*neutral_plan.plan);
    if (neutral_image.isNull()) {
      std::cerr << "Neutral render backend=" << telemetry.backend.toStdString()
                << " fallback=" << telemetry.fallback_reason.toStdString()
                << " canonical=" << telemetry.canonical_plan_error.toStdString() << '\n';
      return fail("The production render path could not prepare a neutral export oracle.");
    }
    neutral_hash = pixel_sha256(neutral_image);
  }

  CatalogAssetModel thumbnail_model;
  QVector<CatalogAssetItem> thumbnail_assets;
  thumbnail_assets.reserve(photo_paths.size());
  for (int index = 0; index < photo_paths.size(); ++index) {
    thumbnail_assets.push_back(make_photo_asset(QStringLiteral("mixed-thumbnail-%1").arg(index), photo_paths.at(index)));
  }
  thumbnail_model.replaceAssetsForTesting(thumbnail_assets);
  thumbnail_model.setVisibleAssetRange(0, static_cast<int>(thumbnail_assets.size() - 1));

  QElapsedTimer combined_drain_timer;
  combined_drain_timer.start();
  QElapsedTimer preflight_timer;
  preflight_timer.start();
  thumbnail_model.requestImportPreflight(
      preflight_root, {}, QStringLiteral("managed"), QStringLiteral("skip"),
      QStringLiteral("source"), QStringLiteral("both"));
  const auto preflight_started = thumbnail_model.importPreflightBusy();

  const auto output_path = runtime.filePath(QStringLiteral("mixed-edited-export.png"));
  EditRecipeState edited_state;
  edited_state.exposure = 0.75;
  const auto export_future = QtConcurrent::run(&pool, [edited_source_path, output_path, source_size, edited_state] {
    MixedExportResult result;
    QImage rendered_pixels;
    QElapsedTimer export_timer;
    export_timer.start();
    auto control = std::make_shared<PhotoExportControl>();
    const auto export_result = run_photo_export([&] {
      RenderBackendTelemetry telemetry;
      QElapsedTimer plan_timer;
      plan_timer.start();
      const auto plan = RenderPlanStore::shared().admit(
          QStringLiteral("round10-edited-export"), edited_source_path, edited_state, source_size,
          photo_editor::pipeline::RenderPurpose::Export,
          photo_editor::pipeline::DisplayRenderingTarget::SrgbSimulation);
      result.canonical_plan_ms = plan_timer.elapsed();
      result.canonical_plan_error = plan.error;
      if (!plan.admitted || !plan.plan.has_value()) return QImage{};
      QElapsedTimer render_timer;
      render_timer.start();
      rendered_pixels = render_output_file(
          edited_source_path, source_size, QRect(QPoint(0, 0), source_size), edited_state,
          QColorSpace(QColorSpace::SRgb), QImage::Format_RGBA8888, nullptr, &telemetry,
          {}, photo_editor::pipeline::RemovalQuality::Final, RenderOutputPolicy::DisplayReferred,
          nullptr, &*plan.plan);
      result.render_ms = render_timer.elapsed();
      result.backend = telemetry.backend;
      return rendered_pixels;
    }, output_path, QByteArrayLiteral("png"), control);
    const auto export_elapsed_ms = export_timer.elapsed();
    result.encode_and_atomic_publish_ms =
        std::max<qint64>(0, export_elapsed_ms - result.canonical_plan_ms - result.render_ms);
    if (!export_result.success || rendered_pixels.isNull()) return result;
    result.rendered_pixel_sha256 = pixel_sha256(rendered_pixels);
    result.output_file_sha256 = file_sha256(output_path);
    result.bytes = QFileInfo(output_path).size();
    result.success = !result.rendered_pixel_sha256.isEmpty() &&
        !result.output_file_sha256.isEmpty() && result.bytes > 0;
    return result;
  });
  [[maybe_unused]] const auto drain_mixed_workload = qScopeGuard([&] {
    thumbnail_model.setInteractiveRenderingActive(true);
    thumbnail_model.cancelImportPreflight();
    const auto catalog_drained = wait_until([&] {
      return !thumbnail_model.importPreflightBusy() && thumbnail_model.activeThumbnailWorkerCount() == 0;
    }, 30000);
    const auto process_drained = pool.waitForDone(30000);
    if (!catalog_drained || !process_drained) {
      std::cerr << "Mixed-workload cleanup timed out: catalog=" << catalog_drained
                << " process=" << process_drained << '\n';
    }
    static_cast<void>(observability::flush());
  });

  auto maximum_active_thumbnail_workers = 0;
  auto maximum_pending_thumbnail_workers = 0;
  auto maximum_import_preflight_scanned = 0;
  int import_thumbnail_overlap_samples = 0;
  int import_thumbnail_export_overlap_samples = 0;
  std::optional<qint64> preflight_completed_ms;
  const auto sample_mixed_workload = [&] {
    const auto active = thumbnail_model.activeThumbnailWorkerCount();
    const auto pending = thumbnail_model.pendingThumbnailWorkerCount();
    maximum_active_thumbnail_workers = std::max(maximum_active_thumbnail_workers, active);
    maximum_pending_thumbnail_workers = std::max(maximum_pending_thumbnail_workers, pending);
    maximum_import_preflight_scanned = std::max(
        maximum_import_preflight_scanned, thumbnail_model.importPreflightScannedCount());
    const auto preflight_active = thumbnail_model.importPreflightBusy();
    if (preflight_active && active > 0) ++import_thumbnail_overlap_samples;
    if (preflight_active && active > 0 && !export_future.isFinished()) {
      ++import_thumbnail_export_overlap_samples;
    }
    if (!preflight_active && !preflight_completed_ms.has_value()) {
      preflight_completed_ms = preflight_timer.elapsed();
    }
  };
  QTimer mixed_workload_sampler;
  mixed_workload_sampler.setInterval(1);
  QObject::connect(&mixed_workload_sampler, &QTimer::timeout, &app, sample_mixed_workload);
  mixed_workload_sampler.start();
  const auto thumbnails_ready = [&] {
    sample_mixed_workload();
    if (thumbnail_model.activeThumbnailWorkerCount() != 0 ||
        thumbnail_model.pendingThumbnailWorkerCount() != 0) return false;
    for (int row = 0; row < thumbnail_assets.size(); ++row) {
      const auto local_path = thumbnail_local_path(thumbnail_model, row);
      if (local_path.isEmpty() || !QFileInfo::exists(local_path)) return false;
    }
    return true;
  };
  if (!wait_until([&] {
        return !thumbnail_model.importPreflightBusy() && export_future.isFinished() && thumbnails_ready();
      }, 30000)) {
    return fail("Mixed preflight/thumbnail/render workload did not reach all terminal states within 30 seconds.");
  }
  mixed_workload_sampler.stop();
  if (!preflight_completed_ms.has_value()) preflight_completed_ms = preflight_timer.elapsed();
  const auto combined_drain_ms = combined_drain_timer.elapsed();
  const auto preflight_ms = *preflight_completed_ms;
  const auto preflight_result = thumbnail_model.importPreflightResult();
  const auto preflight_candidates = preflight_result.value(QStringLiteral("candidateCount")).toInt();
  const auto preflight_eligible = preflight_result.value(QStringLiteral("importableCount")).toInt();
  const auto export_result = export_future.result();
  if (!preflight_started || preflight_candidates != kPreflightPhotoCopies ||
      preflight_eligible != kPreflightPhotoCopies || !preflight_result.value(QStringLiteral("ok")).toBool()) {
    return fail("Production import preflight did not finish with every source JPEG eligible.");
  }
  if (!export_result.success || export_result.rendered_pixel_sha256 == neutral_hash) {
    return fail("The changed recipe did not produce changed pixels and a written output image.");
  }
  if (import_thumbnail_export_overlap_samples <= 0) {
    return fail("The workload did not observe production import preflight, thumbnail decode, and edited export work overlapping.");
  }
  QImage exported_pixels(output_path);
  if (exported_pixels.isNull() || pixel_sha256(exported_pixels) != export_result.rendered_pixel_sha256) {
    return fail("Encoded output pixels do not exactly match the changed-recipe render.");
  }

  if (!observability::flush()) return fail("Could not flush the mixed-workload observability records.");
  QJsonArray thumbnail_samples;
  QFile desktop_log(observability::desktopLogFilePath());
  if (desktop_log.open(QIODevice::ReadOnly)) {
    while (!desktop_log.atEnd()) {
      const auto record = QJsonDocument::fromJson(desktop_log.readLine()).object();
      if (record.value(QStringLiteral("event")).toString() != QStringLiteral("thumbnail.completed")) continue;
      const auto fields = record.value(QStringLiteral("fields")).toObject();
      if (!fields.value(QStringLiteral("asset_id")).toString().startsWith(QStringLiteral("mixed-thumbnail-"))) continue;
      thumbnail_samples.append(QJsonObject{
          {QStringLiteral("assetId"), fields.value(QStringLiteral("asset_id"))},
          {QStringLiteral("queueWaitMs"), fields.value(QStringLiteral("queue_wait_ms"))},
          {QStringLiteral("poolWaitMs"), fields.value(QStringLiteral("pool_wait_ms"))},
          {QStringLiteral("workerElapsedMs"), fields.value(QStringLiteral("elapsed_ms"))},
          {QStringLiteral("deliveryWaitMs"), fields.value(QStringLiteral("delivery_wait_ms"))},
          {QStringLiteral("success"), fields.value(QStringLiteral("success"))},
      });
    }
  }
  if (thumbnail_samples.size() != thumbnail_assets.size()) {
    return fail("Production thumbnail completion telemetry did not cover every mixed-workload source.");
  }
  receipt.insert(QStringLiteral("mixedWorkload"), QJsonObject{
      {QStringLiteral("scenario"), QStringLiteral("source-derived-import-preflight-plus-production-thumbnails-plus-edited-render-and-png-output")},
      {QStringLiteral("importCoverage"), QStringLiteral("production-file-preflight-only; no ingestion-or-catalog-commit")},
      {QStringLiteral("importSourceFiles"), kPreflightPhotoCopies},
      {QStringLiteral("preflightMs"), preflight_ms},
      {QStringLiteral("combinedDrainMs"), combined_drain_ms},
      {QStringLiteral("preflightScannedHighWater"), maximum_import_preflight_scanned},
      {QStringLiteral("preflightCandidateCount"), preflight_candidates},
      {QStringLiteral("importThumbnailOverlapSamples"), import_thumbnail_overlap_samples},
      {QStringLiteral("importThumbnailExportOverlapSamples"), import_thumbnail_export_overlap_samples},
      {QStringLiteral("thumbnailActiveHighWater"), maximum_active_thumbnail_workers},
      {QStringLiteral("thumbnailPendingHighWater"), maximum_pending_thumbnail_workers},
      {QStringLiteral("thumbnailSamples"), thumbnail_samples},
      {QStringLiteral("changedRecipeExposure"), edited_state.exposure},
      {QStringLiteral("neutralRenderPixelSha256"), QString::fromLatin1(neutral_hash)},
      {QStringLiteral("editedRenderPixelSha256"), QString::fromLatin1(export_result.rendered_pixel_sha256)},
      {QStringLiteral("outputFileSha256"), QString::fromLatin1(export_result.output_file_sha256)},
      {QStringLiteral("outputBytes"), export_result.bytes},
      {QStringLiteral("canonicalPlanMs"), export_result.canonical_plan_ms},
      {QStringLiteral("canonicalPlanError"), export_result.canonical_plan_error},
      {QStringLiteral("renderMs"), export_result.render_ms},
      {QStringLiteral("encodeAndAtomicPublishMs"), export_result.encode_and_atomic_publish_ms},
      {QStringLiteral("exportPath"), QStringLiteral("production-run-photo-export-with-atomic-QSaveFile-and-PNG-compression-12")},
      {QStringLiteral("renderBackend"), export_result.backend},
  });

  const auto raw_path = QDir(photo_dir.path()).filePath(QStringLiteral("format-only-synthetic.dng"));
  auto synthetic_raw_decoded = false;
  auto raw_backend_available = raw_camera_linear_decoder_capability().available;
  if (raw_backend_available && write_synthetic_raw(raw_path)) {
    clear_decoded_image_cache();
    RawDecodePrewarmQueue raw_queue(&pool);
    raw_queue.submit(QStringLiteral("synthetic-format-only-raw"), [raw_path](CancellationToken cancellation) {
      return prewarm_source_image_decode_pyramid(raw_path, EditRecipeState{}, 64, 32, cancellation);
    });
    if (!pool.waitForDone(30000)) return fail("Synthetic RAW decode prewarm did not drain.");
    synthetic_raw_decoded = raw_queue.snapshot().failed == 0 &&
        source_image_decode_cache_contains(raw_path, EditRecipeState{}, 64) &&
        source_image_decode_cache_contains(raw_path, EditRecipeState{}, 32);
    receipt.insert(QStringLiteral("syntheticDngFormatOnlyDecodeAndPyramid"), synthetic_raw_decoded);
  }
  receipt.insert(QStringLiteral("rawBackendAvailable"), raw_backend_available);
  if (raw_backend_available && !synthetic_raw_decoded) {
    return fail("The format-only synthetic DNG could not complete the real RAW decoder and cache-pyramid path.");
  }

  const auto report = QJsonDocument(receipt).toJson(QJsonDocument::Compact);
  std::cout << report.constData() << '\n';
  if (argc == 3 && QString::fromLocal8Bit(argv[1]) == QStringLiteral("--report")) {
    QFile output(QString::fromLocal8Bit(argv[2]));
    if (!output.open(QIODevice::WriteOnly | QIODevice::NewOnly) || output.write(report) != report.size()) {
      return fail("Could not write workload report.");
    }
  } else if (argc != 1) {
    return fail("usage: round10_raw_switch_workload_test [--report report.json]");
  }
  return EXIT_SUCCESS;
#endif
}
