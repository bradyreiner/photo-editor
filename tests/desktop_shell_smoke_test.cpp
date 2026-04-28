#include <cstdlib>
#include <iostream>

#include <QApplication>
#include <QCoreApplication>
#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QTime>
#include <qqml.h>
#include <QQuickStyle>
#include <QTemporaryDir>

#include "app_shell_controller.h"
#include "brush_live_preview_item.h"
#include "carousel_document_controller.h"
#include "catalog_asset_model.h"
#include "editor_preview_controller.h"
#include "editor_preview_texture_item.h"
#include "export_job_controller.h"
#include "job_queue_model.h"
#include "job_runner.h"
#include "macos_updater.h"
#include "preview_backend_controller.h"
#include "timeline_document_controller.h"
#include "timeline_playback_controller.h"

namespace {

int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

photo_editor::desktop::CatalogAssetItem make_asset(const QString& asset_id, const QString& file_name) {
  photo_editor::desktop::CatalogAssetItem item;
  item.asset_id = asset_id;
  item.camera_label = QStringLiteral("Test Camera");
  item.file_path = QStringLiteral("/tmp/%1").arg(file_name);
  item.capture_date = QStringLiteral("Apr 8, 2026");
  item.capture_date_value = QDate(2026, 4, 8);
  item.imported_at = QStringLiteral("Apr 8, 2026  9:00 AM");
  item.imported_at_value = QDateTime(QDate(2026, 4, 8), QTime(9, 0));
  item.media_type = QStringLiteral("photo");
  item.online = true;
  return item;
}

}  // namespace

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", QByteArray("offscreen"));

  QApplication app(argc, argv);
  QQuickStyle::setStyle(QStringLiteral("Basic"));
  QCoreApplication::setOrganizationName(QStringLiteral("PhotoEditorTests"));
  QCoreApplication::setApplicationName(QStringLiteral("DesktopShellSmokeTest"));

  QTemporaryDir runtime_root;
  QTemporaryDir cache_root;
  if (!runtime_root.isValid() || !cache_root.isValid()) {
    return fail("temporary runtime directories should be created");
  }

  qputenv("PHOTO_EDITOR_APP_DATA_DIR", runtime_root.path().toUtf8());
  qputenv("PHOTO_EDITOR_CACHE_DIR", cache_root.path().toUtf8());
  qputenv("PHOTO_EDITOR_DATABASE_URL", QByteArray("postgres://example/test"));

  photo_editor::desktop::AppShellController app_shell;
  photo_editor::desktop::CatalogAssetModel catalog_assets;
  photo_editor::desktop::CarouselDocumentController carousel_document;
  photo_editor::desktop::EditorPreviewController editor_preview;
  photo_editor::desktop::JobRunner job_runner;
  photo_editor::desktop::JobQueueModel job_queue;
  photo_editor::desktop::ExportJobController export_jobs;
  photo_editor::desktop::MacosUpdater macos_updater;
  photo_editor::desktop::PreviewBackendController preview_backend;
  photo_editor::desktop::TimelineDocumentController timeline_document;
  photo_editor::desktop::TimelinePlaybackController timeline_playback(&timeline_document);
  qmlRegisterType<photo_editor::desktop::BrushLivePreviewItem>(
      "PhotoEditor",
      1,
      0,
      "BrushLivePreviewItem");
  qmlRegisterType<photo_editor::desktop::EditorPreviewTextureItem>(
      "PhotoEditor",
      1,
      0,
      "EditorPreviewTextureItem");

  QQmlApplicationEngine engine;
  engine.rootContext()->setContextProperty("appShell", &app_shell);
  engine.rootContext()->setContextProperty("catalogAssets", &catalog_assets);
  engine.rootContext()->setContextProperty("carouselDocument", &carousel_document);
  engine.rootContext()->setContextProperty("editorPreview", &editor_preview);
  engine.rootContext()->setContextProperty("exportJobs", &export_jobs);
  engine.rootContext()->setContextProperty("jobQueue", &job_queue);
  engine.rootContext()->setContextProperty("macosUpdater", &macos_updater);
  engine.rootContext()->setContextProperty("previewBackend", &preview_backend);
  engine.rootContext()->setContextProperty("timelineDocument", &timeline_document);
  engine.rootContext()->setContextProperty("timelinePlayback", &timeline_playback);

  const auto main_qml_path =
      QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR)).filePath(QStringLiteral("apps/desktop/qml/Main.qml"));
  engine.load(QUrl::fromLocalFile(main_qml_path));
  QCoreApplication::processEvents();

  if (engine.rootObjects().isEmpty()) {
    return fail("desktop shell should load Main.qml");
  }

  QObject* root = engine.rootObjects().constFirst();
  if (root == nullptr) {
    return fail("desktop shell should expose a root object");
  }

  root->setProperty("currentWorkspace", root->property("editWorkspaceIndex"));
  QCoreApplication::processEvents();

  catalog_assets.replaceAssetsForTesting(
      {make_asset(QStringLiteral("asset-1"), QStringLiteral("IMG_0001.jpg"))});
  catalog_assets.selectIndex(0);
  QCoreApplication::processEvents();
  if (editor_preview.assetId() != QStringLiteral("asset-1") || !editor_preview.hasAsset()) {
    return fail("edit workspace should load the selected catalog asset");
  }

  catalog_assets.replaceAssetsForTesting({});
  QCoreApplication::processEvents();
  if (!editor_preview.assetId().isEmpty() || editor_preview.hasAsset()) {
    return fail("edit workspace should clear the editor when the catalog selection disappears");
  }

  catalog_assets.replaceAssetsForTesting(
      {make_asset(QStringLiteral("asset-2"), QStringLiteral("IMG_0002.jpg"))});
  QCoreApplication::processEvents();
  if (catalog_assets.selectedAssetId() != QStringLiteral("asset-2") ||
      editor_preview.assetId() != QStringLiteral("asset-2")) {
    return fail("edit workspace should follow the next visible asset after the gallery repopulates");
  }

  root->setProperty("currentWorkspace", root->property("libraryWorkspaceIndex"));
  QCoreApplication::processEvents();

  if (!QMetaObject::invokeMethod(root, "beginImportWorkspace")) {
    return fail("menu routing should expose beginImportWorkspace");
  }
  QCoreApplication::processEvents();
  if (root->property("currentWorkspace").toInt() != 0 ||
      root->property("libraryWorkspaceMode").toString() != QStringLiteral("import")) {
    return fail("beginImportWorkspace should route to Library import mode");
  }

  if (!QMetaObject::invokeMethod(root, "createTimelineFromMenu")) {
    return fail("menu routing should expose createTimelineFromMenu");
  }
  QCoreApplication::processEvents();
  if (root->property("currentWorkspace").toInt() != 3 ||
      timeline_document.timelineName().trimmed().isEmpty()) {
    return fail("createTimelineFromMenu should route to Timeline and initialize a document");
  }

  qunsetenv("PHOTO_EDITOR_DATABASE_URL");
  qunsetenv("PHOTO_EDITOR_CACHE_DIR");
  qunsetenv("PHOTO_EDITOR_APP_DATA_DIR");
  return EXIT_SUCCESS;
}
