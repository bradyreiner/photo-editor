#include <QApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QString>

#include "app_bootstrap.h"
#include "catalog_asset_model.h"
#include "editor_preview_controller.h"

int main(int argc, char* argv[]) {
  QQuickStyle::setStyle(QStringLiteral("Basic"));
  QApplication app(argc, argv);
  QApplication::setApplicationName(photo_editor::desktop::AppBootstrap::application_name);
  QApplication::setOrganizationName(photo_editor::desktop::AppBootstrap::organization_name);

  photo_editor::desktop::CatalogAssetModel catalog_assets;
  photo_editor::desktop::EditorPreviewController editor_preview;
  QQmlApplicationEngine engine;
  engine.rootContext()->setContextProperty("catalogAssets", &catalog_assets);
  engine.rootContext()->setContextProperty("editorPreview", &editor_preview);

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
