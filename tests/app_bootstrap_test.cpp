#include <cstdlib>
#include <iostream>

#include <QCoreApplication>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QTemporaryDir>

#include "app_bootstrap.h"

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

  QTemporaryDir runtime_root;
  QTemporaryDir cache_root;
  if (!runtime_root.isValid() || !cache_root.isValid()) {
    return fail("temporary directories should be created");
  }

  qputenv("PHOTO_EDITOR_APP_DATA_DIR", runtime_root.path().toUtf8());
  qputenv("PHOTO_EDITOR_CACHE_DIR", cache_root.path().toUtf8());
  qputenv("PHOTO_EDITOR_DATABASE_URL", QByteArray("postgres://example/test"));

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

  if (!QFileInfo(photo_editor::desktop::AppBootstrap::logLocation()).isDir() ||
      !QFileInfo(photo_editor::desktop::AppBootstrap::backupLocation()).isDir()) {
    return fail("log and backup directories should exist after bootstrap");
  }

  QProcessEnvironment environment;
  if (!photo_editor::desktop::AppBootstrap::prepareProcessEnvironment(&environment, nullptr)) {
    return fail("prepareProcessEnvironment should succeed when database url is supplied");
  }

  if (environment.value(QStringLiteral("PHOTO_EDITOR_APP_DATA_DIR")) != app_data_location ||
      environment.value(QStringLiteral("PHOTO_EDITOR_LOG_DIR")) != photo_editor::desktop::AppBootstrap::logLocation() ||
      environment.value(QStringLiteral("PHOTO_EDITOR_BACKUP_DIR")) != photo_editor::desktop::AppBootstrap::backupLocation() ||
      environment.value(QStringLiteral("PHOTO_EDITOR_DATABASE_URL")) != QStringLiteral("postgres://example/test")) {
    return fail("prepared process environment should include runtime and database settings");
  }

  qunsetenv("PHOTO_EDITOR_DATABASE_URL");
  qunsetenv("PHOTO_EDITOR_CACHE_DIR");
  qunsetenv("PHOTO_EDITOR_APP_DATA_DIR");
  return EXIT_SUCCESS;
}
