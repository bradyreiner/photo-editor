#include "editor_preview_controller.h"
#include "editor_preview_image_sink.h"
#include "photo_editor/pipeline/output_rendering.h"

#include <QGuiApplication>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFile>
#include <QImageWriter>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QSet>
#include <QTemporaryDir>
#include <QThread>
#include <QThreadPool>
#if defined(Q_OS_UNIX)
#include <sys/resource.h>
#include <sys/time.h>
#endif
#if defined(__APPLE__)
#include <pthread.h>
#include <sys/qos.h>
#endif

#include <algorithm>
#include <array>
#include <iostream>

namespace photo_editor::desktop {
RenderResult render_preview_request_for_test(const RenderRequest& request);
void begin_photo_render_stage_profile_for_tests();
QVariantMap end_photo_render_stage_profile_for_tests();
struct EditorPreviewControllerTestAccess {
  static void clear_caches() {
    EditorPreviewController::clearPreviewWorkingCachesForTesting();
  }
  static RenderRequest request(EditorPreviewController& controller, bool full, double exposure) {
    controller.exposure_ = exposure;
    controller.render_reason_ = "edit_change";
    controller.next_interactive_render_draft_ = true;
    controller.last_render_graph_resolution_.dirty_domain = "global_tone_color";
    auto request = controller.currentRenderRequest(full);
    request.use_in_memory_preview = true;
    request.require_cpu_preview_image = true;
    return request;
  }
  static bool idle(const EditorPreviewController& controller) {
    return !controller.processing() && !controller.fullRenderRunning() &&
        !controller.render_scheduler_.interactiveActive() && !controller.render_scheduler_.fullActive() &&
        !controller.render_scheduler_.pendingDispatchActive() &&
        !controller.interactive_request_pending_ && !controller.full_request_pending_ &&
        !controller.interactive_render_result_pending_;
  }
  static QString model(const EditorPreviewController& controller) {
    return controller.currentState().photographic_rendering_model;
  }
};
}  // namespace photo_editor::desktop

namespace {
class PreviewSink final : public photo_editor::desktop::EditorPreviewImageSink {
 public:
  QImage edited;
  void setImage(const QString& key, const QImage& image) override {
    if (key == "edited") edited = image;
  }
  void clearImage(const QString& key) override {
    if (key == "edited") edited = {};
  }
};
QByteArray pixel_hash(const QImage& image) {
  QCryptographicHash hash(QCryptographicHash::Sha256);
  const auto row_bytes = static_cast<qsizetype>(image.width()) * image.depth() / 8;
  for (int y = 0; y < image.height(); ++y) {
    hash.addData(QByteArrayView(reinterpret_cast<const char*>(image.constScanLine(y)), row_bytes));
  }
  return hash.result().toHex();
}

double process_cpu_time_ms() {
#if defined(Q_OS_UNIX)
  struct rusage usage{};
  if (getrusage(RUSAGE_SELF, &usage) != 0) {
    return -1.0;
  }
  const auto timeval_ms = [](const timeval& value) {
    return 1000.0 * static_cast<double>(value.tv_sec) +
           static_cast<double>(value.tv_usec) / 1000.0;
  };
  return timeval_ms(usage.ru_utime) + timeval_ms(usage.ru_stime);
#else
  return -1.0;
#endif
}

int fail(const QString& message) {
  std::cerr << message.toStdString() << '\n';
  return 1;
}

QJsonObject stage_metrics(const photo_editor::desktop::RenderResult& r) {
  return {
      {"decodeMs", r.decode_elapsed_ms}, {"editMs", r.edit_elapsed_ms},
      {"linearMs", r.adjustment_linear_stage_elapsed_ms},
      {"pipelineMs", r.adjustment_pipeline_elapsed_ms},
      {"outputMs", r.adjustment_output_elapsed_ms},
      {"photographicOutputMs", r.photographic_output_stage_elapsed_ms},
      {"histogramScopeMs", r.histogram_scope_elapsed_ms},
      {"geometryMs", r.crop_geometry_elapsed_ms},
      {"packMs", r.output_linear_pack_elapsed_ms},
      {"colorConvertMs", r.output_color_convert_elapsed_ms},
      {"fastGlobalMs", r.adjustment_fast_global_ms},
      {"toneMaskMs", r.adjustment_tone_mask_ms},
      {"finalOutputMs", r.adjustment_final_output_ms},
      {"renderPlanOverheadMs", r.render_plan_overhead_ms},
      {"stages", QJsonObject::fromVariantMap(r.render_stage_elapsed_ms)},
  };
}

QJsonObject output_lut_cache_stats_json(
    const photo_editor::pipeline::Aces2OutputLutCacheStats& stats) {
  return {
      {"requests", static_cast<qint64>(stats.requests)},
      {"hits", static_cast<qint64>(stats.hits)},
      {"misses", static_cast<qint64>(stats.misses)},
      {"builds", static_cast<qint64>(stats.builds)},
      {"evictions", static_cast<qint64>(stats.evictions)},
      {"entries", static_cast<qint64>(stats.entries)},
      {"bytes", static_cast<qint64>(stats.bytes)},
      {"maximumBytes", static_cast<qint64>(stats.maximum_bytes)},
  };
}

QJsonObject output_lut_cache_delta_json(
    const photo_editor::pipeline::Aces2OutputLutCacheStats& before,
    const photo_editor::pipeline::Aces2OutputLutCacheStats& after) {
  return {
      {"requests", static_cast<qint64>(after.requests - before.requests)},
      {"hits", static_cast<qint64>(after.hits - before.hits)},
      {"misses", static_cast<qint64>(after.misses - before.misses)},
      {"builds", static_cast<qint64>(after.builds - before.builds)},
      {"evictions", static_cast<qint64>(after.evictions - before.evictions)},
      {"bytesBefore", static_cast<qint64>(before.bytes)},
      {"bytesAfter", static_cast<qint64>(after.bytes)},
      {"entriesBefore", static_cast<qint64>(before.entries)},
      {"entriesAfter", static_cast<qint64>(after.entries)},
  };
}
}  // namespace

int main(int argc, char** argv) {
  using namespace photo_editor::desktop;
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication app(argc, argv);
#if defined(__APPLE__)
  const auto qos_set_result =
      pthread_set_qos_class_self_np(QOS_CLASS_USER_INITIATED, 0);
  if (qos_set_result != 0) {
    return fail("could not set benchmark main thread to user-initiated QoS");
  }
  constexpr const char* kQosClass = "QOS_CLASS_USER_INITIATED";
#else
  const int qos_set_result = -1;
  constexpr const char* kQosClass = "unsupported";
  std::cerr << "preview CPU benchmark requires macOS thread QoS\n";
  return 77;
#endif
  if (argc != 3) return fail("usage: preview_cpu_pipeline_benchmark_test PHOTO OUTPUT.json");
  QTemporaryDir runtime;
  if (!runtime.isValid()) return fail("Unable to create isolated runtime.");
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", runtime.path().toUtf8());
  qputenv("PHOTO_EDITOR_CACHE_DIR", runtime.filePath("cache").toUtf8());
  qputenv("PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE", "1");
  qputenv("PHOTO_EDITOR_DISABLE_METAL_PREVIEW", "1");
  qputenv("PHOTO_EDITOR_DATABASE_URL", "postgresql://127.0.0.1:1/round11_preview");
  QSettings::setDefaultFormat(QSettings::IniFormat);
  QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, runtime.filePath("settings"));
  const QImage source(QString::fromLocal8Bit(argv[1]));
  if (source.isNull()) return fail("Cannot decode photo fixture.");
  const auto enlarged = source.scaled(2048, 2048, Qt::KeepAspectRatio, Qt::SmoothTransformation);
  const auto photo = runtime.filePath("photo.jpg");
  QImageWriter writer(photo, "JPEG");
  writer.setQuality(94);
  if (!writer.write(enlarged)) return fail(writer.errorString());

  PreviewSink sink;
  const auto setup_cpu_start_ms = process_cpu_time_ms();
  const auto constructor_cpu_start_ms = process_cpu_time_ms();
  QElapsedTimer setup_timer;
  setup_timer.start();
  EditorPreviewController controller;
  const auto constructor_ms = static_cast<double>(setup_timer.nsecsElapsed()) / 1e6;
  const auto constructor_cpu_end_ms = process_cpu_time_ms();
  const auto constructor_cpu_ms =
      constructor_cpu_start_ms < 0.0 || constructor_cpu_end_ms < 0.0
          ? -1.0
          : constructor_cpu_end_ms - constructor_cpu_start_ms;
  controller.setImageProvider(&sink);
  controller.setCatalogRecipePersistenceEnabled(false);
  controller.setPreviewTargetEdge(2048);
  // Keep background prewarm ordering deterministic while charging all its CPU
  // and wall time to setup, before the public-load measurement.
  if (!QThreadPool::globalInstance()->waitForDone(60000))
    return fail("Constructor background prewarm did not settle.");
  const auto output_lut_cache_before_load =
      photo_editor::pipeline::aces2_output_lut_cache_stats();
  const auto public_load_cpu_start_ms = process_cpu_time_ms();
  QElapsedTimer warmup;
  warmup.start();
  controller.loadAsset("round11-preview-cpu", photo, {}, {}, {}, {}, {});
  while ((!EditorPreviewControllerTestAccess::idle(controller) || sink.edited.isNull()) &&
         warmup.elapsed() < 60000) {
    QCoreApplication::processEvents();
    QThread::msleep(2);
  }
  if (!EditorPreviewControllerTestAccess::idle(controller) || sink.edited.isNull()) {
    return fail("Public asset loading did not finish isolated preview initialization.");
  }
  const auto public_load_cpu_end_ms = process_cpu_time_ms();
  const auto load_to_settled_ms = static_cast<double>(warmup.nsecsElapsed()) / 1e6;
  const auto public_load_cpu_ms =
      public_load_cpu_start_ms < 0.0 || public_load_cpu_end_ms < 0.0
          ? -1.0
          : public_load_cpu_end_ms - public_load_cpu_start_ms;
  const auto setup_to_settled_ms = static_cast<double>(setup_timer.nsecsElapsed()) / 1e6;
  const auto setup_cpu_end_ms = process_cpu_time_ms();
  const auto setup_cpu_ms =
      setup_cpu_start_ms < 0.0 || setup_cpu_end_ms < 0.0
          ? -1.0
          : setup_cpu_end_ms - setup_cpu_start_ms;
  const auto output_lut_cache_after_load =
      photo_editor::pipeline::aces2_output_lut_cache_stats();

  QJsonObject receipt{{"schema", "preview-cpu-pipeline:v1"},
      {"scope", "controller-derived request including monitor ICC; CPU only; no queue/import/native presentation"},
      {"sourceWidth", enlarged.width()}, {"sourceHeight", enlarged.height()},
      {"outputLutCacheResetByBenchmark", false}};
  receipt.insert("benchmarkQosClass", kQosClass);
  receipt.insert("benchmarkQosSetResult", qos_set_result);
  receipt.insert("cpuClockMethod", "getrusage_ru_utime_plus_ru_stime");
  receipt.insert("controllerConstructorMs", constructor_ms);
  receipt.insert("controllerConstructorCpuMs", constructor_cpu_ms);
  receipt.insert("publicLoadToSettledMs", load_to_settled_ms);
  receipt.insert("publicLoadToSettledCpuMs", public_load_cpu_ms);
  receipt.insert("controllerSetupToSettledMs", setup_to_settled_ms);
  receipt.insert("controllerSetupToSettledCpuMs", setup_cpu_ms);
  receipt.insert("setupMetric", "constructor/configuration/background-prewarm/cache-snapshot barrier/public load to settled image sink; windowless, not physical presentation");
  receipt.insert("photographicModel", EditorPreviewControllerTestAccess::model(controller));
  receipt.insert("outputLutCacheBeforeLoad",
                 output_lut_cache_stats_json(output_lut_cache_before_load));
  receipt.insert("outputLutCacheAfterLoad",
                 output_lut_cache_stats_json(output_lut_cache_after_load));
  receipt.insert("outputLutCacheLoadDelta",
                 output_lut_cache_delta_json(
                     output_lut_cache_before_load,
                     output_lut_cache_after_load));
  const std::array<double, 7> exposures{-0.24, -0.12, 0.1, 0.25, 0.4, 0.63, 0.79};
  for (const auto& scenario : {QStringLiteral("interactive"), QStringLiteral("full"),
                              QStringLiteral("cold-full")}) {
    EditorPreviewControllerTestAccess::clear_caches();
    auto request = EditorPreviewControllerTestAccess::request(controller, scenario != "interactive", 0.05);
    const auto warm_cpu_start_ms = process_cpu_time_ms();
    auto warm = render_preview_request_for_test(request);
    const auto warm_cpu_end_ms = process_cpu_time_ms();
    const auto warm_cpu_ms =
        warm_cpu_start_ms < 0.0 || warm_cpu_end_ms < 0.0
            ? -1.0
            : warm_cpu_end_ms - warm_cpu_start_ms;
    if (!warm.success || warm.preview_image.isNull()) return fail(warm.status);
    warm = {};
    QJsonArray times, cpu_times, hashes, stages, formats, dimensions;
    QSet<QByteArray> distinct_hashes;
    const int profile_rounds = std::clamp(qEnvironmentVariableIntValue("PHOTO_EDITOR_PREVIEW_PROFILE_ROUNDS"), 1, 20);
    for (int sample = 0; sample < profile_rounds * static_cast<int>(exposures.size()); ++sample) {
      if (scenario == "cold-full") EditorPreviewControllerTestAccess::clear_caches();
      const auto exposure = exposures[static_cast<std::size_t>(sample) % exposures.size()] +
          0.013 * static_cast<double>(static_cast<std::size_t>(sample) / exposures.size());
      request = EditorPreviewControllerTestAccess::request(controller, scenario != "interactive", exposure);
      const auto output_lut_cache_before =
          photo_editor::pipeline::aces2_output_lut_cache_stats();
      begin_photo_render_stage_profile_for_tests();
      const auto render_cpu_start_ms = process_cpu_time_ms();
      QElapsedTimer timer;
      timer.start();
      const auto rendered = render_preview_request_for_test(request);
      const auto elapsed_ms = static_cast<double>(timer.nsecsElapsed()) / 1e6;
      const auto render_cpu_end_ms = process_cpu_time_ms();
      const auto render_cpu_ms =
          render_cpu_start_ms < 0.0 || render_cpu_end_ms < 0.0
              ? -1.0
              : render_cpu_end_ms - render_cpu_start_ms;
      const auto output_lut_cache_after =
          photo_editor::pipeline::aces2_output_lut_cache_stats();
      const auto profile = end_photo_render_stage_profile_for_tests();
      if (!rendered.success || rendered.cancelled || rendered.preview_image.isNull() ||
          rendered.rendered_cache_hit || rendered.backend != "cpu" ||
          !rendered.canonical_plan_error.isEmpty()) {
        return fail(QStringLiteral("%1: success=%2 backend=%3 cache=%4 status=%5 plan=%6")
            .arg(scenario).arg(rendered.success).arg(rendered.backend)
            .arg(rendered.rendered_cache_hit).arg(rendered.status, rendered.canonical_plan_error));
      }
      auto metrics = stage_metrics(rendered);
      metrics.insert("exportProfile", QJsonObject::fromVariantMap(profile));
      metrics.insert("outputLutCacheBefore",
                     output_lut_cache_stats_json(output_lut_cache_before));
      metrics.insert("outputLutCacheAfter",
                     output_lut_cache_stats_json(output_lut_cache_after));
      metrics.insert("outputLutCacheDelta",
                     output_lut_cache_delta_json(
                         output_lut_cache_before,
                         output_lut_cache_after));
      times.append(elapsed_ms);
      cpu_times.append(render_cpu_ms);
      const auto hash = pixel_hash(rendered.preview_image);
      if (distinct_hashes.contains(hash)) return fail("Changing exposure did not change preview pixels.");
      distinct_hashes.insert(hash);
      hashes.append(QString::fromLatin1(hash));
      stages.append(metrics);
      formats.append(static_cast<int>(rendered.preview_image.format()));
      dimensions.append(QJsonArray{rendered.preview_image.width(), rendered.preview_image.height()});
    }
    receipt.insert(scenario, QJsonObject{{"ms", times}, {"cpuMs", cpu_times},
        {"warmupCpuMs", warm_cpu_ms}, {"pixelHashes", hashes},
        {"stages", stages}, {"formats", formats}, {"dimensions", dimensions},
        {"monitorProfile", request.state.preview_icc_profile_asset.payload_sha256},
        {"draft", request.draft_interactive}, {"maxEdge", request.max_edge}});
  }
  qint64 rss = 0;
#if defined(Q_OS_UNIX)
  struct rusage usage{};
  if (getrusage(RUSAGE_SELF, &usage) != 0) return fail("Peak RSS unavailable.");
#ifdef Q_OS_MACOS
  rss = static_cast<qint64>(usage.ru_maxrss);
#else
  rss = static_cast<qint64>(usage.ru_maxrss) * 1024;
#endif
#endif
  receipt.insert("peakRssBytes", rss);
  receipt.insert("peakRssAvailable", rss > 0);
  QFile output(QString::fromLocal8Bit(argv[2]));
  if (!output.open(QIODevice::WriteOnly)) return fail(output.errorString());
  const auto bytes = QJsonDocument(receipt).toJson(QJsonDocument::Indented);
  if (output.write(bytes) != bytes.size()) return fail(output.errorString());
  return 0;
}
