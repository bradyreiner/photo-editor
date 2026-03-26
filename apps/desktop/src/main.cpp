#include <QApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QString>

#include "app_bootstrap.h"
#include "carousel_document_controller.h"
#include "catalog_asset_model.h"
#include "editor_preview_controller.h"
#include "timeline_document_controller.h"
#include "timeline_playback_controller.h"

int main(int argc, char* argv[]) {
  QQuickStyle::setStyle(QStringLiteral("Basic"));
  QApplication app(argc, argv);
  QApplication::setApplicationName(photo_editor::desktop::AppBootstrap::application_name);
  QApplication::setOrganizationName(photo_editor::desktop::AppBootstrap::organization_name);

  photo_editor::desktop::CatalogAssetModel catalog_assets;
  photo_editor::desktop::CarouselDocumentController carousel_document;
  photo_editor::desktop::EditorPreviewController editor_preview;
  photo_editor::desktop::TimelineDocumentController timeline_document;
  photo_editor::desktop::TimelinePlaybackController timeline_playback(&timeline_document);
  QQmlApplicationEngine engine;
  engine.rootContext()->setContextProperty("catalogAssets", &catalog_assets);
  engine.rootContext()->setContextProperty("carouselDocument", &carousel_document);
  engine.rootContext()->setContextProperty("editorPreview", &editor_preview);
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

  return app.exec();
}
