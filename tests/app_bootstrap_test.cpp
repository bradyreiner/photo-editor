#include <cstdlib>
#include <iostream>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>

#include "app_bootstrap.h"
#include "observability.h"
#include "photo_editor/pipeline/color_profile_asset.h"
#include "photo_editor/pipeline/dcp_profile_asset.h"
#include "qml_performance_monitor.h"
#include "worker_queue_registry.h"

namespace {

int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  QCoreApplication::setOrganizationName(QStringLiteral("PhotoEditorTests"));
  QCoreApplication::setApplicationName(QStringLiteral("AppBootstrapTest"));

  if (QString::fromUtf8(photo_editor::desktop::AppBootstrap::application_name) !=
          QStringLiteral("PhotoEditor") ||
      QString::fromUtf8(photo_editor::desktop::AppBootstrap::organization_name) !=
          QStringLiteral("PhotoEditor")) {
    return fail("Looksy must retain the private-alpha application identity so existing data and preferences stay in place");
  }

  QTemporaryDir runtime_root;
  QTemporaryDir cache_root;
  if (!runtime_root.isValid() || !cache_root.isValid()) {
    return fail("temporary directories should be created");
  }

  const QHash<QString, QByteArray> legacy_runtime_files{
      {QStringLiteral("postgres/state.json"),
       QByteArrayLiteral("{\"version\":4,\"database_name\":\"photo_editor\"}")},
      {QStringLiteral("postgres/data/PG_VERSION"), QByteArrayLiteral("16\n")},
      {QStringLiteral("presets/legacy-look.json"),
       QByteArrayLiteral("{\"name\":\"Legacy Look\",\"exposure\":0.25}")},
      {QStringLiteral("jobs/legacy-queued.json"),
       QByteArrayLiteral("{\"id\":\"legacy-export\",\"state\":\"queued\"}")},
      {QStringLiteral("backups/legacy-catalog.dump"), QByteArrayLiteral("legacy-catalog-backup")},
      {QStringLiteral("agent/models.json"),
       QByteArrayLiteral("{\"selectedModel\":\"legacy-local-model\"}")},
  };
  for (auto iterator = legacy_runtime_files.constBegin(); iterator != legacy_runtime_files.constEnd(); ++iterator) {
    const auto path = QDir(runtime_root.path()).filePath(iterator.key());
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
      return fail("legacy runtime fixture directories should be created");
    }
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(iterator.value()) != iterator.value().size()) {
      return fail("legacy runtime fixture files should be written");
    }
  }

  qputenv("PHOTO_EDITOR_APP_DATA_DIR", runtime_root.path().toUtf8());
  qputenv("PHOTO_EDITOR_CACHE_DIR", cache_root.path().toUtf8());
  qputenv("PHOTO_EDITOR_DATABASE_URL", QByteArray("postgres://example/test"));
  qputenv("PHOTO_EDITOR_REQUIRE_PRE_MIGRATION_BACKUP", QByteArray("0"));

  const auto app_data_location = photo_editor::desktop::AppBootstrap::appDataLocation();
  const auto cache_location = photo_editor::desktop::AppBootstrap::cacheLocation();
  if (QFileInfo(app_data_location).absoluteFilePath() != QFileInfo(runtime_root.path()).absoluteFilePath()) {
    return fail("app data location should honor PHOTO_EDITOR_APP_DATA_DIR");
  }
  if (QFileInfo(cache_location).absoluteFilePath() != QFileInfo(cache_root.path()).absoluteFilePath()) {
    return fail("cache location should honor PHOTO_EDITOR_CACHE_DIR");
  }

  if (!photo_editor::desktop::AppBootstrap::ensureRuntimeDirectories()) {
    return fail("runtime directories should be created successfully");
  }

  for (auto iterator = legacy_runtime_files.constBegin(); iterator != legacy_runtime_files.constEnd(); ++iterator) {
    QFile file(QDir(runtime_root.path()).filePath(iterator.key()));
    if (!file.open(QIODevice::ReadOnly) || file.readAll() != iterator.value()) {
      return fail("Looksy bootstrap should preserve existing catalog, preset, job, backup, and agent data in place");
    }
  }

  if (!QFileInfo(photo_editor::desktop::AppBootstrap::logLocation()).isDir() ||
      !QFileInfo(photo_editor::desktop::AppBootstrap::backupLocation()).isDir() ||
      !QFileInfo(photo_editor::desktop::AppBootstrap::iccProfileAssetLocation()).isDir() ||
      !QFileInfo(photo_editor::desktop::AppBootstrap::dcpProfileAssetLocation()).isDir()) {
    return fail("log, backup, and durable ICC/DCP profile directories should exist after bootstrap");
  }
  if (QFileInfo(QString::fromStdString(
                    photo_editor::pipeline::default_icc_profile_asset_registry().storage_root()))
          .absoluteFilePath() !=
      QFileInfo(photo_editor::desktop::AppBootstrap::iccProfileAssetLocation())
          .absoluteFilePath()) {
    return fail("bootstrap should configure the default ICC registry with durable app storage");
  }
  if (QFileInfo(QString::fromStdString(
                    photo_editor::pipeline::default_dcp_profile_asset_registry().storage_root()))
          .absoluteFilePath() !=
      QFileInfo(photo_editor::desktop::AppBootstrap::dcpProfileAssetLocation())
          .absoluteFilePath()) {
    return fail("bootstrap should configure the default DCP registry with durable app storage");
  }

  QProcessEnvironment environment;
  if (!photo_editor::desktop::AppBootstrap::prepareProcessEnvironment(&environment, nullptr)) {
    return fail("prepareProcessEnvironment should succeed when database url is supplied");
  }

  if (environment.value(QStringLiteral("PHOTO_EDITOR_APP_DATA_DIR")) != app_data_location ||
      environment.value(QStringLiteral("PHOTO_EDITOR_LOG_DIR")) != photo_editor::desktop::AppBootstrap::logLocation() ||
      environment.value(QStringLiteral("PHOTO_EDITOR_BACKUP_DIR")) != photo_editor::desktop::AppBootstrap::backupLocation() ||
      environment.value(QStringLiteral("PHOTO_EDITOR_DATABASE_URL")) !=
          QStringLiteral("postgres://example/test") ||
      environment.value(QStringLiteral("PHOTO_EDITOR_REQUIRE_PRE_MIGRATION_BACKUP")) != QStringLiteral("0")) {
    return fail("prepared process environment should preserve the explicit PostgreSQL authority");
  }

  const auto observability_message = photo_editor::desktop::observability::structuredMessage(
      QStringLiteral("preview.render.completed"),
      {
          {QStringLiteral("render_reason"), QStringLiteral("slider_drag")},
          {QStringLiteral("full_resolution"), false},
          {QStringLiteral("decoded_cache_hit"), true},
      });
  const auto observability_json = QJsonDocument::fromJson(observability_message.toUtf8());
  if (!observability_json.isObject()) {
    return fail("structured observability messages should be JSON objects");
  }
  const auto observability_fields = observability_json.object();
  if (observability_fields.value(QStringLiteral("event")).toString() !=
          QStringLiteral("preview.render.completed") ||
      observability_fields.value(QStringLiteral("workflow")).toString() != QStringLiteral("preview") ||
      observability_fields.value(QStringLiteral("reason")).toString() != QStringLiteral("slider_drag") ||
      !observability_fields.value(QStringLiteral("interactive")).toBool() ||
      !observability_fields.value(QStringLiteral("cache_hit")).toBool() ||
      observability_fields.value(QStringLiteral("thread")).toString().isEmpty()) {
    return fail("structured observability messages should expose standard performance fields");
  }

  const auto qml_route_fields =
      photo_editor::desktop::QmlPerformanceMonitor::routeReadyFields(
          QStringLiteral("Masks"),
          1,
          42,
          17,
          false,
          QStringLiteral("qml-route:masks"));
  if (qml_route_fields.value(QStringLiteral("workflow")).toString() != QStringLiteral("qml_route") ||
      qml_route_fields.value(QStringLiteral("workflow_id")).toString() != QStringLiteral("qml-route:masks") ||
      qml_route_fields.value(QStringLiteral("surface")).toString() != QStringLiteral("masks") ||
      qml_route_fields.value(QStringLiteral("route")).toString() != QStringLiteral("masks") ||
      qml_route_fields.value(QStringLiteral("route_index")).toInt() != 1 ||
      qml_route_fields.value(QStringLiteral("elapsed_ms")).toLongLong() != 42 ||
      qml_route_fields.value(QStringLiteral("component_creation_ms")).toLongLong() != 17 ||
      qml_route_fields.value(QStringLiteral("prewarmed")).toBool()) {
    return fail("QML route telemetry should expose workflow, route, elapsed, and component timing fields");
  }

  const auto qml_frame_fields =
      photo_editor::desktop::QmlPerformanceMonitor::frameSampleFields(
          QStringLiteral("Edit"),
          1,
          41.5,
          true,
          false);
  if (qml_frame_fields.value(QStringLiteral("workflow")).toString() != QStringLiteral("qml_frame") ||
      qml_frame_fields.value(QStringLiteral("workflow_id")).toString() != QStringLiteral("qml-frame:edit") ||
      qml_frame_fields.value(QStringLiteral("surface")).toString() != QStringLiteral("edit") ||
      qml_frame_fields.value(QStringLiteral("frame_interval_ms")).toDouble() < 41.0 ||
      !qml_frame_fields.value(QStringLiteral("janky")).toBool() ||
      qml_frame_fields.value(QStringLiteral("dropped")).toBool() ||
      qml_frame_fields.value(QStringLiteral("jank_frame_count")).toInt() != 1 ||
      qml_frame_fields.value(QStringLiteral("dropped_frame_count")).toInt() != 0) {
    return fail("QML frame sample telemetry should expose frame interval and jank fields");
  }

  const auto qml_frame_summary =
      photo_editor::desktop::QmlPerformanceMonitor::frameSummaryFields(
          QStringLiteral("Timeline"),
          3,
          120,
          52.0,
          4,
          1);
  if (qml_frame_summary.value(QStringLiteral("surface")).toString() != QStringLiteral("timeline") ||
      qml_frame_summary.value(QStringLiteral("sample_count")).toInt() != 120 ||
      qml_frame_summary.value(QStringLiteral("max_frame_interval_ms")).toDouble() != 52.0 ||
      qml_frame_summary.value(QStringLiteral("jank_frame_count")).toInt() != 4 ||
      qml_frame_summary.value(QStringLiteral("dropped_frame_count")).toInt() != 1) {
    return fail("QML frame summary telemetry should expose sample, max interval, jank, and dropped counts");
  }
  const auto qml_frame_compact_line =
      photo_editor::desktop::observability::compactConsoleLineForEvent(
          QStringLiteral("qml.frame.summary"),
          qml_frame_summary);
  const QStringList expected_qml_frame_tokens{
      QStringLiteral("surface=timeline"),
      QStringLiteral("samples=120"),
      QStringLiteral("max=52ms"),
      QStringLiteral("jank=4"),
      QStringLiteral("dropped=1"),
  };
  for (const auto& token : expected_qml_frame_tokens) {
    if (!qml_frame_compact_line.contains(token)) {
      return fail("compact QML frame summaries should include useful frame diagnostics");
    }
  }

  const auto qml_preview_failure =
      photo_editor::desktop::QmlPerformanceMonitor::previewFailureFields(
          QStringLiteral("Edit"),
          QStringLiteral("Selection preview unavailable"),
          QStringLiteral("mask_rasterization"),
          QStringLiteral("Mask source could not be rasterized."),
          QStringLiteral("failed|blocking:false|stale:false|retry:true|stage:mask_rasterization|detail:true"),
          false,
          QStringLiteral("render_failure_state"));
  if (qml_preview_failure.value(QStringLiteral("workflow")).toString() != QStringLiteral("preview") ||
      qml_preview_failure.value(QStringLiteral("workflow_id")).toString() != QStringLiteral("preview-failure:edit") ||
      qml_preview_failure.value(QStringLiteral("surface")).toString() != QStringLiteral("edit") ||
      qml_preview_failure.value(QStringLiteral("status")).toString() != QStringLiteral("Selection preview unavailable") ||
      qml_preview_failure.value(QStringLiteral("failure_stage")).toString() != QStringLiteral("mask_rasterization") ||
      !qml_preview_failure.value(QStringLiteral("failure_detail_present")).toBool() ||
      qml_preview_failure.value(QStringLiteral("presentation_signature")).toString().isEmpty() ||
      qml_preview_failure.value(QStringLiteral("processing")).toBool() ||
      qml_preview_failure.value(QStringLiteral("source")).toString() != QStringLiteral("render_failure_state")) {
    return fail("QML preview failure telemetry should expose visible failure source, status, stage, and presentation fields");
  }

  const auto colorized_perf_line =
      photo_editor::desktop::observability::colorizedConsoleLineForMessage(
          QtInfoMsg,
          QStringLiteral("photo_editor.perf"),
          QStringLiteral("preview.render.completed 42 ms"));
  if (!colorized_perf_line.startsWith(QStringLiteral("\x1b[36m")) ||
      !colorized_perf_line.endsWith(QStringLiteral("\x1b[0m"))) {
    return fail("perf console lines should be colorized cyan with a reset suffix");
  }

  const auto colorized_warning_line =
      photo_editor::desktop::observability::colorizedConsoleLineForMessage(
          QtWarningMsg,
          QStringLiteral("qt.imageformats.tiff"),
          QStringLiteral("TIFF tag warning"));
  if (!colorized_warning_line.startsWith(QStringLiteral("\x1b[33m")) ||
      !colorized_warning_line.endsWith(QStringLiteral("\x1b[0m"))) {
    return fail("warning console lines should be colorized yellow with a reset suffix");
  }

  const QVariantMap compact_fields{
      {QStringLiteral("stage"), QStringLiteral("interactive")},
      {QStringLiteral("render_mode"), QStringLiteral("live_preview")},
      {QStringLiteral("render_reason"), QStringLiteral("slider_drag")},
      {QStringLiteral("elapsed_ms"), 142},
      {QStringLiteral("queue_wait_ms"), 9},
      {QStringLiteral("render_latency_budget_ms"), 100},
      {QStringLiteral("render_within_latency_budget"), false},
      {QStringLiteral("backend"), QStringLiteral("metal")},
      {QStringLiteral("dirty_domain"), QStringLiteral("layers_composite")},
      {QStringLiteral("async_job_stage"), QStringLiteral("render_request")},
      {QStringLiteral("cache_hit_count"), 2},
      {QStringLiteral("cache_lookup_count"), 6},
      {QStringLiteral("generation"), 33},
  };
  const auto compact_line =
      photo_editor::desktop::observability::compactConsoleLineForEvent(
          QStringLiteral("preview.render.completed"),
          compact_fields);
  const QStringList expected_compact_tokens{
      QStringLiteral("preview.render.completed"),
      QStringLiteral("stage=interactive"),
      QStringLiteral("mode=live_preview"),
      QStringLiteral("reason=slider_drag"),
      QStringLiteral("elapsed=142ms"),
      QStringLiteral("queue=9ms"),
      QStringLiteral("budget=OVER/100ms"),
      QStringLiteral("backend=metal"),
      QStringLiteral("dirty=layers_composite"),
      QStringLiteral("cache=2/6"),
      QStringLiteral("job=render_request"),
      QStringLiteral("gen=33"),
  };
  for (const auto& token : expected_compact_tokens) {
    if (!compact_line.contains(token)) {
      return fail("compact observability console line should include key performance tokens");
    }
  }
  if (compact_line.contains(QStringLiteral("failure=render_request"))) {
    return fail("compact observability console line should not label async job stage as failure");
  }

  const auto registered_worker_pools =
      photo_editor::desktop::WorkerQueueRegistry::registeredPoolIds();
  const QStringList expected_worker_pools{
      QStringLiteral("preview-render"),
      QStringLiteral("overlay-render"),
      QStringLiteral("histogram-scope"),
      QStringLiteral("thumbnail"),
      QStringLiteral("mask-commit"),
      QStringLiteral("smart-selection"),
      QStringLiteral("inpaint"),
      QStringLiteral("export"),
      QStringLiteral("catalog-import"),
      QStringLiteral("metadata-extraction"),
      QStringLiteral("timeline-media"),
      QStringLiteral("carousel-render"),
  };
  for (const auto& pool_id : expected_worker_pools) {
    if (!registered_worker_pools.contains(pool_id) ||
        !photo_editor::desktop::WorkerQueueRegistry::isRegisteredPool(pool_id)) {
      return fail("worker queue registry should expose every expected worker pool");
    }
  }

  const auto worker_sample =
      photo_editor::desktop::WorkerQueueRegistry::sampleFields(
          QStringLiteral("thumbnail"),
          QStringLiteral("thumbnail"),
          QStringLiteral("library"),
          2,
          4,
          8,
          6,
          3,
          1);
  if (worker_sample.value(QStringLiteral("event")).isValid() ||
      worker_sample.value(QStringLiteral("worker_pool")).toString() != QStringLiteral("thumbnail") ||
      worker_sample.value(QStringLiteral("pool_id")).toString() != QStringLiteral("thumbnail") ||
      worker_sample.value(QStringLiteral("workflow")).toString() != QStringLiteral("thumbnail") ||
      worker_sample.value(QStringLiteral("surface")).toString() != QStringLiteral("library") ||
      worker_sample.value(QStringLiteral("active_worker_count")).toInt() != 2 ||
      worker_sample.value(QStringLiteral("pending_worker_count")).toInt() != 4 ||
      worker_sample.value(QStringLiteral("worker_limit")).toInt() != 8 ||
      worker_sample.value(QStringLiteral("queue_wait_ms")).toLongLong() != 6 ||
      worker_sample.value(QStringLiteral("completed_count")).toInt() != 3 ||
      worker_sample.value(QStringLiteral("cancelled_count")).toInt() != 1 ||
      !worker_sample.value(QStringLiteral("registered_pool")).toBool()) {
    return fail("worker queue samples should expose pool, workflow, counts, limits, and queue timing");
  }

  const auto worker_summary =
      photo_editor::desktop::WorkerQueueRegistry::summaryFields(
          QStringLiteral("preview-render"),
          QStringLiteral("preview"),
          QStringLiteral("edit"),
          1,
          0,
          2,
          7,
          42,
          1,
          0);
  if (worker_summary.value(QStringLiteral("worker_pool")).toString() != QStringLiteral("preview-render") ||
      worker_summary.value(QStringLiteral("worker_elapsed_ms")).toLongLong() != 42 ||
      worker_summary.value(QStringLiteral("queue_wait_ms")).toLongLong() != 7 ||
      worker_summary.value(QStringLiteral("completed_count")).toInt() != 1 ||
      worker_summary.value(QStringLiteral("cancelled_count")).toInt() != 0) {
    return fail("worker queue summaries should expose elapsed time and completion counts");
  }

  photo_editor::desktop::WorkerQueueRegistry::publishUserIntent(
      QStringLiteral("timeline"),
      true,
      true,
      true,
      {QStringLiteral("clip-visible"), QStringLiteral("clip-focus")},
      QStringLiteral("clip-focus"));
  const auto playback_decision =
      photo_editor::desktop::WorkerQueueRegistry::priorityDecision(
          QStringLiteral("playback"), QStringLiteral("timeline"),
          QStringLiteral("clip-focus"));
  const auto focused_proxy_decision =
      photo_editor::desktop::WorkerQueueRegistry::priorityDecision(
          QStringLiteral("proxy"), QStringLiteral("timeline"),
          QStringLiteral("clip-focus"), QStringLiteral("high"));
  const auto visible_thumbnail_decision =
      photo_editor::desktop::WorkerQueueRegistry::priorityDecision(
          QStringLiteral("thumbnail"), QStringLiteral("timeline"),
          QStringLiteral("clip-visible"));
  const auto final_export_decision =
      photo_editor::desktop::WorkerQueueRegistry::priorityDecision(
          QStringLiteral("asset_export"), QStringLiteral("export"),
          QStringLiteral("clip-focus"));
  if (playback_decision.value(QStringLiteral("effectivePriority")).toString() !=
          QStringLiteral("critical") ||
      playback_decision.value(QStringLiteral("deferred")).toBool() ||
      !focused_proxy_decision.value(QStringLiteral("deferred")).toBool() ||
      focused_proxy_decision.value(QStringLiteral("effectivePriority")).toString() !=
          QStringLiteral("deferred") ||
      !focused_proxy_decision.value(QStringLiteral("reason")).toString().contains(
          QStringLiteral("playback"), Qt::CaseInsensitive) ||
      !visible_thumbnail_decision.value(QStringLiteral("deferred")).toBool() ||
      final_export_decision.value(QStringLiteral("deferred")).toBool()) {
    return fail("latency-sensitive playback should outrank even manually high proxy, thumbnail, analysis, and preview work without silently deferring final delivery");
  }

  photo_editor::desktop::WorkerQueueRegistry::publishUserIntent(
      QStringLiteral("timeline"),
      true,
      false,
      false,
      {QStringLiteral("clip-visible"), QStringLiteral("clip-focus")},
      QStringLiteral("clip-focus"));
  const auto focused_idle_decision =
      photo_editor::desktop::WorkerQueueRegistry::priorityDecision(
          QStringLiteral("proxy"), QStringLiteral("timeline"),
          QStringLiteral("clip-focus"));
  const auto visible_idle_decision =
      photo_editor::desktop::WorkerQueueRegistry::priorityDecision(
          QStringLiteral("proxy"), QStringLiteral("timeline"),
          QStringLiteral("clip-visible"));
  const auto background_idle_decision =
      photo_editor::desktop::WorkerQueueRegistry::priorityDecision(
          QStringLiteral("proxy"), QStringLiteral("timeline"),
          QStringLiteral("clip-offscreen"));
  if (focused_idle_decision.value(QStringLiteral("deferred")).toBool() ||
      focused_idle_decision.value(QStringLiteral("effectiveRank")).toInt() <=
          visible_idle_decision.value(QStringLiteral("effectiveRank")).toInt() ||
      visible_idle_decision.value(QStringLiteral("effectiveRank")).toInt() <=
          background_idle_decision.value(QStringLiteral("effectiveRank")).toInt()) {
    return fail("idle background work should order focused, visible, then offscreen items deterministically");
  }
  photo_editor::desktop::WorkerQueueRegistry::clearUserIntent(
      QStringLiteral("timeline"));
  if (photo_editor::desktop::WorkerQueueRegistry::userIntentSnapshot()
          .value(QStringLiteral("workspaceVisible")).toBool()) {
    return fail("closing an editing surface should clear its latency-sensitive scheduling intent");
  }
  QProcess bounded_background_process;
  photo_editor::desktop::WorkerQueueRegistry::configureBackgroundProcess(
      &bounded_background_process, QStringLiteral("proxy"));
  if (!bounded_background_process.property("backgroundPriorityBounded").toBool() ||
      bounded_background_process.property("backgroundWorkflow").toString() !=
          QStringLiteral("proxy")) {
    return fail("background subprocesses should carry the shared OS-priority contract before they start");
  }

  auto failed_compact_fields = compact_fields;
  failed_compact_fields.insert(QStringLiteral("failure_stage"), QStringLiteral("mask_alpha"));
  const auto failed_compact_line =
      photo_editor::desktop::observability::compactConsoleLineForEvent(
          QStringLiteral("preview.render.failure_presented"),
          failed_compact_fields);
  if (!failed_compact_line.contains(QStringLiteral("failure=mask_alpha")) ||
      failed_compact_line.contains(QStringLiteral("failure=render_request"))) {
    return fail("compact observability console line should print only the resolved failure stage");
  }

  if (!photo_editor::desktop::observability::consoleModePrintsEvent(
          QtInfoMsg,
          QStringLiteral("over_budget"),
          QStringLiteral("preview.render.completed"),
          compact_fields)) {
    return fail("over-budget console mode should print slow structured events");
  }

  const QVariantMap within_budget_fields{
      {QStringLiteral("elapsed_ms"), 42},
      {QStringLiteral("render_latency_budget_ms"), 100},
      {QStringLiteral("render_within_latency_budget"), true},
  };
  if (photo_editor::desktop::observability::consoleModePrintsEvent(
          QtInfoMsg,
          QStringLiteral("over_budget"),
          QStringLiteral("preview.render.completed"),
          within_budget_fields)) {
    return fail("over-budget console mode should suppress normal info events");
  }
  if (photo_editor::desktop::observability::consoleModePrintsEvent(
          QtInfoMsg,
          QStringLiteral("quiet"),
          QStringLiteral("preview.render.completed"),
          compact_fields)) {
    return fail("quiet console mode should suppress info events");
  }
  if (!photo_editor::desktop::observability::consoleModePrintsEvent(
          QtWarningMsg,
          QStringLiteral("quiet"),
          QStringLiteral("preview.render.failed"),
          {})) {
    return fail("quiet console mode should still print warnings");
  }

  const auto missing_helper_name = QStringLiteral("definitely_missing_photo_editor_helper");
  const auto missing_helper_arguments =
      photo_editor::desktop::AppBootstrap::helperArguments(missing_helper_name, {QStringLiteral("arg")});
  if (photo_editor::desktop::AppBootstrap::helperProgram(missing_helper_name) != QStringLiteral("cargo") ||
      missing_helper_arguments.size() < 10 ||
      missing_helper_arguments.at(0) != QStringLiteral("run") ||
      !QFileInfo(missing_helper_arguments.at(3)).isAbsolute() ||
      !missing_helper_arguments.at(3).endsWith(QStringLiteral("rust/Cargo.toml")) ||
      missing_helper_arguments.at(7) != missing_helper_name ||
      missing_helper_arguments.last() != QStringLiteral("arg")) {
    return fail("missing helpers should fall back to cargo with the requested binary and arguments");
  }

  QProcessEnvironment postgres_environment;
  if (!photo_editor::desktop::AppBootstrap::prepareProcessEnvironment(
          &postgres_environment, nullptr) ||
      postgres_environment.value(QStringLiteral("PHOTO_EDITOR_DATABASE_URL")) !=
          QStringLiteral("postgres://example/test") ||
      postgres_environment.value(QStringLiteral("PHOTO_EDITOR_APP_DATA_DIR")) !=
          app_data_location) {
    return fail("desktop helpers should retain the configured PostgreSQL connection contract");
  }
  QString postgres_bootstrap_error;
  if (!photo_editor::desktop::AppBootstrap::ensureCatalogService(
          &postgres_bootstrap_error) ||
      QFileInfo(QDir(runtime_root.path()).filePath(QStringLiteral("catalog.sqlite3")))
          .exists()) {
    std::cerr << postgres_bootstrap_error.toStdString() << '\n';
    return fail("application bootstrap should honor PostgreSQL without creating a SQLite authority");
  }
  qunsetenv("PHOTO_EDITOR_DATABASE_URL");
  qunsetenv("PHOTO_EDITOR_CACHE_DIR");
  qunsetenv("PHOTO_EDITOR_APP_DATA_DIR");
  return EXIT_SUCCESS;
}
