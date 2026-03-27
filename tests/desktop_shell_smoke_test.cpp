#include <cstdlib>
#include <iostream>

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QTemporaryDir>

#include "app_shell_controller.h"
#include "carousel_document_controller.h"
#include "catalog_asset_model.h"
#include "editor_preview_controller.h"
#include "export_job_controller.h"
#include "timeline_document_controller.h"
#include "timeline_playback_controller.h"

namespace {

int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
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
  photo_editor::desktop::ExportJobController export_jobs;
  photo_editor::desktop::TimelineDocumentController timeline_document;
  photo_editor::desktop::TimelinePlaybackController timeline_playback(&timeline_document);

  QQmlApplicationEngine engine;
  engine.rootContext()->setContextProperty("appShell", &app_shell);
  engine.rootContext()->setContextProperty("catalogAssets", &catalog_assets);
  engine.rootContext()->setContextProperty("carouselDocument", &carousel_document);
  engine.rootContext()->setContextProperty("editorPreview", &editor_preview);
  engine.rootContext()->setContextProperty("exportJobs", &export_jobs);
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
