#include <cstdlib>
#include <iostream>

#include <QCoreApplication>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcessEnvironment>
#include <QTemporaryDir>

#include "app_bootstrap.h"
#include "observability.h"

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

  const auto missing_helper_name = QStringLiteral("definitely_missing_photo_editor_helper");
  const auto missing_helper_arguments =
      photo_editor::desktop::AppBootstrap::helperArguments(missing_helper_name, {QStringLiteral("arg")});
  if (photo_editor::desktop::AppBootstrap::helperProgram(missing_helper_name) != QStringLiteral("cargo") ||
      missing_helper_arguments.size() < 10 ||
      missing_helper_arguments.at(0) != QStringLiteral("run") ||
      missing_helper_arguments.at(7) != missing_helper_name ||
      missing_helper_arguments.last() != QStringLiteral("arg")) {
    return fail("missing helpers should fall back to cargo with the requested binary and arguments");
  }

  qunsetenv("PHOTO_EDITOR_DATABASE_URL");
  qunsetenv("PHOTO_EDITOR_CACHE_DIR");
  qunsetenv("PHOTO_EDITOR_APP_DATA_DIR");
  return EXIT_SUCCESS;
}
