#include <QApplication>
#include <QDebug>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QString>
#include <QTimer>

#include "app_bootstrap.h"
#include "app_shell_controller.h"
#include "carousel_document_controller.h"
#include "catalog_asset_model.h"
#include "editor_preview_controller.h"
#include "export_job_controller.h"
#include "macos_updater.h"
#include "preview_backend_controller.h"
#include "timeline_document_controller.h"
#include "timeline_playback_controller.h"

int main(int argc, char* argv[]) {
  QQuickStyle::setStyle(QStringLiteral("Basic"));
  QApplication app(argc, argv);
  QApplication::setApplicationName(photo_editor::desktop::AppBootstrap::application_name);
  QApplication::setOrganizationName(photo_editor::desktop::AppBootstrap::organization_name);
  photo_editor::desktop::AppBootstrap::ensureRuntimeDirectories();
  QString catalog_bootstrap_error;
  if (!photo_editor::desktop::AppBootstrap::ensureCatalogService(&catalog_bootstrap_error) &&
      !catalog_bootstrap_error.isEmpty()) {
    qWarning().noquote() << catalog_bootstrap_error;
  }

  photo_editor::desktop::CatalogAssetModel catalog_assets;
  photo_editor::desktop::AppShellController app_shell;
  photo_editor::desktop::CarouselDocumentController carousel_document;
  photo_editor::desktop::EditorPreviewController editor_preview;
  photo_editor::desktop::ExportJobController export_jobs;
  photo_editor::desktop::MacosUpdater macos_updater;
  photo_editor::desktop::PreviewBackendController preview_backend;
  photo_editor::desktop::TimelineDocumentController timeline_document;
  photo_editor::desktop::TimelinePlaybackController timeline_playback(&timeline_document);
  QQmlApplicationEngine engine;
  engine.rootContext()->setContextProperty("appShell", &app_shell);
  engine.rootContext()->setContextProperty("catalogAssets", &catalog_assets);
  engine.rootContext()->setContextProperty("carouselDocument", &carousel_document);
  engine.rootContext()->setContextProperty("editorPreview", &editor_preview);
  engine.rootContext()->setContextProperty("exportJobs", &export_jobs);
  engine.rootContext()->setContextProperty("previewBackend", &preview_backend);
  engine.rootContext()->setContextProperty("timelineDocument", &timeline_document);
  engine.rootContext()->setContextProperty("timelinePlayback", &timeline_playback);

  QObject::connect(
      &engine,
      &QQmlApplicationEngine::objectCreationFailed,
      &app,
      []() { QCoreApplication::exit(EXIT_FAILURE); },
      Qt::QueuedConnection);
  engine.loadFromModule(QStringLiteral("PhotoEditor"), QStringLiteral("Main"));
  catalog_assets.refresh();

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
  });

  return app.exec();
}
