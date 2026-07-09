#include <QApplication>
#include <QDateTime>
#include <QDebug>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QFont>
#include <QFutureWatcher>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QSize>
#include <QString>
#include <QTimer>
#include <QtConcurrent/QtConcurrent>

#include <functional>

#include "app_bootstrap.h"
#include "app_shell_controller.h"
#include "carousel_document_controller.h"
#include "catalog_asset_model.h"
#include "editor_preview_controller.h"
#include "editor_preview_image_provider.h"
#include "export_job_controller.h"
#include "job_queue_model.h"
#include "job_runner.h"
#include "macos_updater.h"
#include "observability.h"
#include "agent_edit_controller.h"
#include "photo_automation_bridge.h"
#include "preview_backend_controller.h"
#include "qml_performance_monitor.h"
#include "timeline_document_controller.h"
#include "timeline_playback_controller.h"

namespace {

void logProofAutorunEvent(
    const QString& event,
    const QString& source_path,
    photo_editor::desktop::EditorPreviewController* editor_preview) {
  const auto native_size = editor_preview->displayPreviewNativeSize();
  photo_editor::desktop::observability::logInfo(
      photoEditorAppLog(),
      event,
      {
          {QStringLiteral("file_path"), source_path},
          {QStringLiteral("processing"), editor_preview->processing()},
          {QStringLiteral("native_available"), editor_preview->displayPreviewNativeAvailable()},
          {QStringLiteral("native_width"), native_size.width()},
          {QStringLiteral("native_height"), native_size.height()},
          {QStringLiteral("native_revision"), editor_preview->displayPreviewNativeRevision()},
          {QStringLiteral("source_width"), editor_preview->sourceImageWidth()},
          {QStringLiteral("source_height"), editor_preview->sourceImageHeight()},
      });
}

void waitForNativeMaxPreview(
    photo_editor::desktop::EditorPreviewController* editor_preview,
    const QString& source_path,
    const QString& label,
    int min_native_revision,
    int attempts_remaining,
    std::function<void()> on_ready) {
  const auto native_size = editor_preview->displayPreviewNativeSize();
  const QSize source_size(editor_preview->sourceImageWidth(), editor_preview->sourceImageHeight());
  const auto ready = editor_preview->displayPreviewNativeAvailable() &&
                     source_size.isValid() &&
                     native_size == source_size &&
                     editor_preview->displayPreviewNativeRevision() > min_native_revision &&
                     !editor_preview->processing();
  if (ready) {
    logProofAutorunEvent(
        QStringLiteral("desktop.proof_autorun.%1_ready").arg(label),
        source_path,
        editor_preview);
    on_ready();
    return;
  }

  if (attempts_remaining <= 0) {
    photo_editor::desktop::observability::logWarning(
        photoEditorAppLog(),
        QStringLiteral("desktop.proof_autorun.%1_timeout").arg(label),
        {
            {QStringLiteral("file_path"), source_path},
            {QStringLiteral("processing"), editor_preview->processing()},
            {QStringLiteral("native_available"), editor_preview->displayPreviewNativeAvailable()},
            {QStringLiteral("native_width"), native_size.width()},
            {QStringLiteral("native_height"), native_size.height()},
            {QStringLiteral("native_revision"), editor_preview->displayPreviewNativeRevision()},
            {QStringLiteral("min_native_revision"), min_native_revision},
            {QStringLiteral("source_width"), source_size.width()},
            {QStringLiteral("source_height"), source_size.height()},
        });
    QTimer::singleShot(1000, qApp, []() { QCoreApplication::quit(); });
    return;
  }

  if (attempts_remaining % 10 == 0) {
    logProofAutorunEvent(
        QStringLiteral("desktop.proof_autorun.%1_waiting").arg(label),
        source_path,
        editor_preview);
  }

  QTimer::singleShot(
      500,
      editor_preview,
      [editor_preview,
       source_path,
       label,
       min_native_revision,
       attempts_remaining,
       on_ready = std::move(on_ready)]() mutable {
        waitForNativeMaxPreview(
            editor_preview,
            source_path,
            label,
            min_native_revision,
            attempts_remaining - 1,
            std::move(on_ready));
      });
}

void waitForLoadedSource(
    photo_editor::desktop::EditorPreviewController* editor_preview,
    const QString& source_path,
    int attempts_remaining,
    std::function<void()> on_ready) {
  const QSize source_size(editor_preview->sourceImageWidth(), editor_preview->sourceImageHeight());
  const auto ready = editor_preview->hasAsset() && source_size.isValid() &&
                     source_size.width() >= 2048 && source_size.height() >= 2048 &&
                     !editor_preview->processing();
  if (ready) {
    logProofAutorunEvent(
        QStringLiteral("desktop.proof_autorun.source_ready"),
        source_path,
        editor_preview);
    on_ready();
    return;
  }

  if (attempts_remaining <= 0) {
    photo_editor::desktop::observability::logWarning(
        photoEditorAppLog(),
        QStringLiteral("desktop.proof_autorun.source_timeout"),
        {
            {QStringLiteral("file_path"), source_path},
            {QStringLiteral("has_asset"), editor_preview->hasAsset()},
            {QStringLiteral("processing"), editor_preview->processing()},
            {QStringLiteral("source_width"), source_size.width()},
            {QStringLiteral("source_height"), source_size.height()},
        });
    QTimer::singleShot(1000, qApp, []() { QCoreApplication::quit(); });
    return;
  }

  if (attempts_remaining % 10 == 0) {
    logProofAutorunEvent(
        QStringLiteral("desktop.proof_autorun.source_waiting"),
        source_path,
        editor_preview);
  }

  QTimer::singleShot(
      500,
      editor_preview,
      [editor_preview,
       source_path,
       attempts_remaining,
       on_ready = std::move(on_ready)]() mutable {
        waitForLoadedSource(
            editor_preview,
            source_path,
            attempts_remaining - 1,
            std::move(on_ready));
      });
}

void waitForFullRenderAndApplyTint(
    photo_editor::desktop::EditorPreviewController* editor_preview,
    const QString& source_path,
    double proof_tint,
    int attempts_remaining) {
  if (editor_preview->fullRenderRunning()) {
    photo_editor::desktop::observability::logInfo(
        photoEditorAppLog(),
        QStringLiteral("desktop.proof_autorun.tint_followup_edit"),
        {{QStringLiteral("file_path"), source_path},
         {QStringLiteral("attempts_remaining"), attempts_remaining}});
    editor_preview->setTint(proof_tint);
    return;
  }

  if (attempts_remaining <= 0) {
    photo_editor::desktop::observability::logWarning(
        photoEditorAppLog(),
        QStringLiteral("desktop.proof_autorun.tint_followup_timeout"),
        {{QStringLiteral("file_path"), source_path}});
    return;
  }

  QTimer::singleShot(10, editor_preview, [editor_preview, source_path, proof_tint, attempts_remaining]() {
    waitForFullRenderAndApplyTint(editor_preview, source_path, proof_tint, attempts_remaining - 1);
  });
}

void scheduleViewportProofEdits(
    photo_editor::desktop::EditorPreviewController* editor_preview,
    const QString& source_path,
    double proof_temperature,
    double proof_tint) {
  photo_editor::desktop::observability::logInfo(
      photoEditorAppLog(),
      QStringLiteral("desktop.proof_autorun.viewport_first"),
      {{QStringLiteral("file_path"), source_path}});
  editor_preview->setPreviewViewport(0.75, 0.75, 0.125, 0.125, 512, true);

  QTimer::singleShot(900, editor_preview, [editor_preview, source_path, proof_temperature]() {
    photo_editor::desktop::observability::logInfo(
        photoEditorAppLog(),
        QStringLiteral("desktop.proof_autorun.temperature_edit"),
        {{QStringLiteral("file_path"), source_path}});
    editor_preview->setTemperature(proof_temperature);
  });

  QTimer::singleShot(1200, editor_preview, [editor_preview, source_path]() {
    photo_editor::desktop::observability::logInfo(
        photoEditorAppLog(),
        QStringLiteral("desktop.proof_autorun.viewport_second"),
        {{QStringLiteral("file_path"), source_path}});
    editor_preview->setPreviewViewport(0.625, 0.625, 0.125, 0.125, 512, true);
  });

  QTimer::singleShot(1210, editor_preview, [editor_preview, source_path, proof_tint]() {
    waitForFullRenderAndApplyTint(editor_preview, source_path, proof_tint, 300);
  });

  QTimer::singleShot(30000, qApp, []() {
    photo_editor::desktop::observability::logInfo(
        photoEditorAppLog(),
        QStringLiteral("desktop.proof_autorun.quit"),
        {});
    QCoreApplication::quit();
  });
}

void scheduleDesktopProofAutorun(photo_editor::desktop::EditorPreviewController* editor_preview) {
  const auto source_path = qEnvironmentVariable("PHOTO_EDITOR_DESKTOP_PROOF_SOURCE_IMAGE").trimmed();
  if (source_path.isEmpty()) {
    return;
  }

  const QFileInfo source_info(source_path);
  if (!source_info.isFile()) {
    photo_editor::desktop::observability::logWarning(
        photoEditorAppLog(),
        QStringLiteral("desktop.proof_autorun.source_missing"),
        {{QStringLiteral("file_path"), source_path}});
    return;
  }

  const auto normalized_source_path = source_info.absoluteFilePath();
  const auto display_name = source_info.completeBaseName();
  photo_editor::desktop::observability::logInfo(
      photoEditorAppLog(),
      QStringLiteral("desktop.proof_autorun.scheduled"),
      {{QStringLiteral("file_path"), normalized_source_path}});

  QTimer::singleShot(500, editor_preview, [editor_preview, normalized_source_path, display_name]() {
    photo_editor::desktop::observability::logInfo(
        photoEditorAppLog(),
        QStringLiteral("desktop.proof_autorun.load_asset"),
        {{QStringLiteral("file_path"), normalized_source_path}});
    editor_preview->loadAsset(
        QStringLiteral("desktop-proof-source"),
        normalized_source_path,
        QString{},
        display_name,
        QString{},
        QString{},
        QString{});
  });

  waitForLoadedSource(
      editor_preview,
      normalized_source_path,
      120,
      [editor_preview, normalized_source_path]() {
        const auto revision_before_texture = editor_preview->displayPreviewNativeRevision();
        const auto proof_texture =
            10.0 +
            static_cast<double>(QDateTime::currentMSecsSinceEpoch() % 3000) / 100.0;
        photo_editor::desktop::observability::logInfo(
            photoEditorAppLog(),
            QStringLiteral("desktop.proof_autorun.working_profile"),
            {{QStringLiteral("file_path"), normalized_source_path},
             {QStringLiteral("working_profile"), QStringLiteral("Linear sRGB")}});
        editor_preview->setWorkingColorProfile(QStringLiteral("Linear sRGB"));
        photo_editor::desktop::observability::logInfo(
            photoEditorAppLog(),
            QStringLiteral("desktop.proof_autorun.texture_edit"),
            {{QStringLiteral("file_path"), normalized_source_path},
             {QStringLiteral("native_revision"), revision_before_texture},
             {QStringLiteral("texture"), proof_texture}});
        editor_preview->setTexture(proof_texture);
        waitForNativeMaxPreview(
            editor_preview,
            normalized_source_path,
            QStringLiteral("adjusted_max_preview"),
            revision_before_texture,
            120,
            [editor_preview, normalized_source_path]() {
              const auto proof_temperature =
                  10.0 +
                  static_cast<double>(QDateTime::currentMSecsSinceEpoch() % 3000) / 100.0;
              const auto proof_tint =
                  15.0 +
                  static_cast<double>(QDateTime::currentMSecsSinceEpoch() % 2500) / 100.0;
              scheduleViewportProofEdits(
                  editor_preview,
                  normalized_source_path,
                  proof_temperature,
                  proof_tint);
            });
      });
}

}  // namespace

int main(int argc, char* argv[]) {
  QElapsedTimer startup_timer;
  startup_timer.start();
  QQuickStyle::setStyle(QStringLiteral("Basic"));
  QApplication app(argc, argv);
  QApplication::setFont(QFont(QStringLiteral("Helvetica Neue")));
  QApplication::setApplicationName(photo_editor::desktop::AppBootstrap::application_name);
  QApplication::setOrganizationName(photo_editor::desktop::AppBootstrap::organization_name);
  photo_editor::desktop::AppBootstrap::ensureRuntimeDirectories();
  photo_editor::desktop::observability::initialize();
  photo_editor::desktop::observability::logInfo(
      photoEditorAppLog(),
      QStringLiteral("desktop.startup.begin"),
      {
          {QStringLiteral("session_id"), photo_editor::desktop::observability::sessionId()},
          {QStringLiteral("run_directory"), photo_editor::desktop::observability::runDirectory()},
          {QStringLiteral("desktop_log_file"), photo_editor::desktop::observability::desktopLogFilePath()},
      });
  photo_editor::desktop::CatalogAssetModel catalog_assets;
  photo_editor::desktop::AgentEditController agent_edit;
  photo_editor::desktop::AppShellController app_shell;
  photo_editor::desktop::CarouselDocumentController carousel_document;
  photo_editor::desktop::EditorPreviewController editor_preview;
  photo_editor::desktop::JobRunner job_runner;
  photo_editor::desktop::JobQueueModel job_queue;
  photo_editor::desktop::ExportJobController export_jobs;
  agent_edit.setCatalogModel(&catalog_assets);
  agent_edit.setExportJobController(&export_jobs);
  photo_editor::desktop::PhotoAutomationBridge photo_automation(&agent_edit, &catalog_assets);
  if (photo_automation.start()) {
    agent_edit.setMcpEndpoint(photo_automation.mcpEndpoint(), photo_automation.mcpEndpointKind());
    QTimer::singleShot(0, &agent_edit, [&agent_edit]() {
      agent_edit.refreshMcpTools();
    });
    photo_editor::desktop::observability::logInfo(
        photoEditorAppLog(),
        QStringLiteral("automation.bridge.started"),
        {
            {QStringLiteral("endpoint"), photo_automation.endpointName()},
            {QStringLiteral("mcp_endpoint"), photo_automation.mcpEndpoint()},
            {QStringLiteral("mcp_endpoint_kind"), photo_automation.mcpEndpointKind()},
            {QStringLiteral("session_file"), photo_automation.sessionFilePath()},
        });
  } else {
    photo_editor::desktop::observability::logWarning(
        photoEditorAppLog(),
        QStringLiteral("automation.bridge.failed"),
        {{QStringLiteral("error"), photo_automation.errorMessage()}});
  }
  photo_editor::desktop::MacosUpdater macos_updater;
  photo_editor::desktop::PreviewBackendController preview_backend;
  photo_editor::desktop::QmlPerformanceMonitor qml_performance;
  photo_editor::desktop::TimelineDocumentController timeline_document;
  photo_editor::desktop::TimelinePlaybackController timeline_playback(&timeline_document);
  QQmlApplicationEngine engine;
  auto* editor_preview_image_provider = new photo_editor::desktop::EditorPreviewImageProvider;
  editor_preview.setImageProvider(editor_preview_image_provider);
  engine.addImageProvider(QStringLiteral("editor-preview"), editor_preview_image_provider);
  engine.rootContext()->setContextProperty("appShell", &app_shell);
  engine.rootContext()->setContextProperty("agentEdit", &agent_edit);
  engine.rootContext()->setContextProperty("catalogAssets", &catalog_assets);
  engine.rootContext()->setContextProperty("carouselDocument", &carousel_document);
  engine.rootContext()->setContextProperty("editorPreview", &editor_preview);
  engine.rootContext()->setContextProperty("exportJobs", &export_jobs);
  engine.rootContext()->setContextProperty("jobQueue", &job_queue);
  engine.rootContext()->setContextProperty("macosUpdater", &macos_updater);
  engine.rootContext()->setContextProperty("previewBackend", &preview_backend);
  engine.rootContext()->setContextProperty("qmlPerformance", &qml_performance);
  engine.rootContext()->setContextProperty("timelineDocument", &timeline_document);
  engine.rootContext()->setContextProperty("timelinePlayback", &timeline_playback);

  QObject::connect(
      &engine,
      &QQmlApplicationEngine::objectCreationFailed,
      &app,
      []() { QCoreApplication::exit(EXIT_FAILURE); },
      Qt::QueuedConnection);
  engine.loadFromModule(QStringLiteral("PhotoEditor"), QStringLiteral("Main"));
  auto* catalog_bootstrap_watcher = new QFutureWatcher<QString>(&app);
  QObject::connect(
      catalog_bootstrap_watcher,
      &QFutureWatcher<QString>::finished,
      &app,
      [catalog_bootstrap_watcher, &catalog_assets, &export_jobs, &job_queue, &job_runner]() {
        const auto catalog_bootstrap_error = catalog_bootstrap_watcher->result();
        if (!catalog_bootstrap_error.isEmpty()) {
          photo_editor::desktop::observability::logWarning(
              photoEditorCatalogLog(),
              QStringLiteral("catalog.bootstrap.failed"),
              {{QStringLiteral("error"), catalog_bootstrap_error}});
        } else {
          export_jobs.initializeFromStore();
          job_queue.initializeFromStore();
          job_runner.initializeFromStore();
          catalog_assets.refresh();
        }
        catalog_bootstrap_watcher->deleteLater();
      });
  photo_editor::desktop::observability::logInfo(
      photoEditorCatalogLog(),
      QStringLiteral("catalog.bootstrap.async_started"),
      {{QStringLiteral("elapsed_ms"), startup_timer.elapsed()}});
  catalog_bootstrap_watcher->setFuture(QtConcurrent::run([]() {
    QString catalog_bootstrap_error;
    if (!photo_editor::desktop::AppBootstrap::ensureCatalogService(&catalog_bootstrap_error) &&
        !catalog_bootstrap_error.isEmpty()) {
      return catalog_bootstrap_error;
    }
    return QString();
  }));
  scheduleDesktopProofAutorun(&editor_preview);

  // Periodically write session state for crash recovery
  QTimer session_timer;
  session_timer.setInterval(5000);
  QObject::connect(&session_timer, &QTimer::timeout, [&]() {
    app_shell.updateSessionState(
        catalog_assets.property("selectedAssetId").toString(),
        timeline_document.property("savedDocumentPath").toString());
  });
  session_timer.start();

  QObject::connect(&app, &QCoreApplication::aboutToQuit, [&]() {
    session_timer.stop();
    app_shell.markCleanExit();
    photo_editor::desktop::observability::logInfo(
        photoEditorAppLog(),
        QStringLiteral("desktop.shutdown"),
        {{QStringLiteral("uptime_ms"), startup_timer.elapsed()}});
  });

  photo_editor::desktop::observability::logInfo(
      photoEditorPerfLog(),
      QStringLiteral("desktop.startup.ready"),
      {{QStringLiteral("elapsed_ms"), startup_timer.elapsed()}});
  return app.exec();
}
