#include "app_bootstrap.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>

#include "observability.h"

namespace photo_editor::desktop {

namespace {

QString ensure_directory(const QString& path) {
  if (path.isEmpty()) {
    return {};
  }

  QDir directory(path);
  if (directory.exists()) {
    const QFileInfo info(directory.absolutePath());
    return info.isWritable() ? directory.absolutePath() : QString{};
  }

  if (!QDir().mkpath(path)) {
    return {};
  }

  const QFileInfo info(path);
  return info.isWritable() ? info.absoluteFilePath() : QString{};
}

QString fallbackRuntimeRoot() {
  return QDir::current().absoluteFilePath(QStringLiteral(".photo-editor"));
}

QString configured_runtime_root() {
  const auto override_root = qEnvironmentVariable("PHOTO_EDITOR_APP_DATA_DIR").trimmed();
  if (override_root.isEmpty()) {
    const auto legacy_override_root = qEnvironmentVariable("PHOTO_EDITOR_APP_DATA_ROOT").trimmed();
    return legacy_override_root.isEmpty() ? QString{} : QDir::cleanPath(legacy_override_root);
  }

  return QDir::cleanPath(override_root);
}

QString app_data_root() {
  const auto explicit_root = configured_runtime_root();
  if (!explicit_root.isEmpty()) {
    const auto ensured_explicit_root = ensure_directory(explicit_root);
    return ensured_explicit_root.isEmpty() ? fallbackRuntimeRoot() : ensured_explicit_root;
  }

  const auto standard_root = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  if (!standard_root.isEmpty()) {
    const auto ensured_standard_root = ensure_directory(standard_root);
    if (!ensured_standard_root.isEmpty()) {
      return ensured_standard_root;
    }
  }

  const auto ensured_fallback_root = ensure_directory(fallbackRuntimeRoot());
  if (!ensured_fallback_root.isEmpty()) {
    return ensured_fallback_root;
  }

  return fallbackRuntimeRoot();
}

QString built_binary_path(const QString& binary_name) {
  return QDir::current().absoluteFilePath(QStringLiteral("rust/target/debug/%1").arg(binary_name));
}

QString installed_binary_path(const QString& binary_name) {
  return QDir(QCoreApplication::applicationDirPath()).filePath(binary_name);
}

bool use_built_binary(const QString& binary_name) {
  const QFileInfo installed_binary(installed_binary_path(binary_name));
  if (installed_binary.exists() && installed_binary.isExecutable()) {
    return true;
  }

  const QFileInfo binary(built_binary_path(binary_name));
  return binary.exists() && binary.isExecutable();
}

QString helper_program(const QString& binary_name) {
  const QFileInfo installed_binary(installed_binary_path(binary_name));
  if (installed_binary.exists() && installed_binary.isExecutable()) {
    return installed_binary.absoluteFilePath();
  }

  return use_built_binary(binary_name) ? built_binary_path(binary_name) : QStringLiteral("cargo");
}

QStringList helper_arguments(const QString& binary_name, const QStringList& arguments) {
  if (use_built_binary(binary_name)) {
    return arguments;
  }

  QStringList cargo_arguments = {
      QStringLiteral("run"),
      QStringLiteral("--quiet"),
      QStringLiteral("--manifest-path"),
      QStringLiteral("rust/Cargo.toml"),
      QStringLiteral("-p"),
      QStringLiteral("photo-catalog"),
      QStringLiteral("--bin"),
      binary_name,
      QStringLiteral("--"),
  };
  cargo_arguments.append(arguments);
  return cargo_arguments;
}

QString friendly_helper_error(const QString& binary_name, const QString& failure_output) {
  if (failure_output.contains(QStringLiteral("Operation not permitted"), Qt::CaseInsensitive) ||
      failure_output.contains(QStringLiteral("PermissionDenied"), Qt::CaseInsensitive)) {
    return QStringLiteral(
               "%1 could not start the local catalog service in the current runtime environment. "
               "The desktop shell will continue to load, but Library-backed features need either "
               "a writable runtime root or an explicit PHOTO_EDITOR_DATABASE_URL.")
        .arg(binary_name);
  }

  return failure_output;
}

QProcessEnvironment base_environment() {
  auto environment = QProcessEnvironment::systemEnvironment();
  environment.insert(QStringLiteral("PHOTO_EDITOR_APP_DATA_DIR"), app_data_root());
  environment.insert(QStringLiteral("PHOTO_EDITOR_LOG_DIR"), QDir(app_data_root()).filePath(QStringLiteral("logs")));
  environment.insert(
      QStringLiteral("PHOTO_EDITOR_BACKUP_DIR"),
      QDir(app_data_root()).filePath(QStringLiteral("backups")));
  if (observability::enabled()) {
    environment.insert(QString::fromLatin1(observability::kEnabledEnv), QStringLiteral("1"));
    environment.insert(
        QString::fromLatin1(observability::kSessionIdEnv),
        observability::sessionId());
    environment.insert(
        QString::fromLatin1(observability::kLogLevelEnv),
        observability::logLevel());
    environment.insert(
        QString::fromLatin1(observability::kRunDirectoryEnv),
        observability::runDirectory());
  }
  return environment;
}

bool run_helper(
    const QString& binary_name,
    const QStringList& arguments,
    QString* output,
    QString* error_message,
    int timeout_ms = 30000) {
  QElapsedTimer timer;
  timer.start();
  observability::logInfo(
      photoEditorCatalogLog(),
      QStringLiteral("catalog.helper.start"),
      {
          {QStringLiteral("binary"), binary_name},
          {QStringLiteral("arguments"), arguments.join(' ')},
          {QStringLiteral("timeout_ms"), timeout_ms},
      });
  QProcess process;
  process.setWorkingDirectory(QDir::currentPath());
  process.setProcessEnvironment(base_environment());
  process.setProcessChannelMode(QProcess::MergedChannels);
  process.start(helper_program(binary_name), helper_arguments(binary_name, arguments));

  if (!process.waitForStarted(5000)) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("Unable to start %1. Build the Rust helpers first.").arg(binary_name);
    }
    observability::logWarning(
        photoEditorCatalogLog(),
        QStringLiteral("catalog.helper.failed_to_start"),
        {
            {QStringLiteral("binary"), binary_name},
            {QStringLiteral("elapsed_ms"), timer.elapsed()},
        });
    return false;
  }

  if (!process.waitForFinished(timeout_ms) || process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
    const auto failure_output = QString::fromUtf8(process.readAllStandardOutput()).trimmed();
    if (error_message != nullptr) {
      *error_message = failure_output.isEmpty()
                           ? QStringLiteral("%1 failed to prepare the catalog database.").arg(binary_name)
                           : friendly_helper_error(binary_name, failure_output);
    }
    observability::logWarning(
        photoEditorCatalogLog(),
        QStringLiteral("catalog.helper.failed"),
        {
            {QStringLiteral("binary"), binary_name},
            {QStringLiteral("exit_code"), process.exitCode()},
            {QStringLiteral("exit_status"), static_cast<int>(process.exitStatus())},
            {QStringLiteral("elapsed_ms"), timer.elapsed()},
            {QStringLiteral("output"), failure_output},
        });
    return false;
  }

  if (output != nullptr) {
    *output = QString::fromUtf8(process.readAllStandardOutput()).trimmed();
  }
  observability::logInfo(
      photoEditorCatalogLog(),
      QStringLiteral("catalog.helper.completed"),
      {
          {QStringLiteral("binary"), binary_name},
          {QStringLiteral("elapsed_ms"), timer.elapsed()},
      });
  return true;
}

QString resolve_database_url(QString* error_message) {
  QString output;
  if (!run_helper(
          QStringLiteral("catalog_local_db"),
          {QStringLiteral("connection-url"), QStringLiteral("--ensure-running")},
          &output,
          error_message,
          60000)) {
    return {};
  }

  return output;
}

}  // namespace

QString AppBootstrap::appDataLocation() { return app_data_root(); }

QString AppBootstrap::cacheLocation() {
  const auto override_root = qEnvironmentVariable("PHOTO_EDITOR_CACHE_DIR").trimmed();
  if (!override_root.isEmpty()) {
    const auto ensured_override_root = ensure_directory(override_root);
    return ensured_override_root.isEmpty() ? QDir(app_data_root()).filePath(QStringLiteral("cache")) : ensured_override_root;
  }

  const auto root = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
  if (!root.isEmpty()) {
    const auto ensured_root = ensure_directory(root);
    if (!ensured_root.isEmpty()) {
      return ensured_root;
    }
  }

  return QDir(app_data_root()).filePath(QStringLiteral("cache"));
}

QString AppBootstrap::logLocation() { return QDir(app_data_root()).filePath(QStringLiteral("logs")); }

QString AppBootstrap::backupLocation() { return QDir(app_data_root()).filePath(QStringLiteral("backups")); }

bool AppBootstrap::ensureRuntimeDirectories() {
  return !ensure_directory(appDataLocation()).isEmpty() &&
         !ensure_directory(cacheLocation()).isEmpty() &&
         !ensure_directory(logLocation()).isEmpty() &&
         !ensure_directory(backupLocation()).isEmpty();
}

bool AppBootstrap::ensureCatalogService(QString* error_message) {
  QElapsedTimer timer;
  timer.start();
  if (!ensureRuntimeDirectories()) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("Unable to create the application runtime directories.");
    }
    observability::logWarning(
        photoEditorCatalogLog(),
        QStringLiteral("catalog.bootstrap.failed"),
        {
            {QStringLiteral("reason"), QStringLiteral("runtime_directories")},
            {QStringLiteral("elapsed_ms"), timer.elapsed()},
        });
    return false;
  }

  if (QProcessEnvironment::systemEnvironment().contains(QStringLiteral("PHOTO_EDITOR_DATABASE_URL"))) {
    observability::logInfo(
        photoEditorCatalogLog(),
        QStringLiteral("catalog.bootstrap.external_database"),
        {{QStringLiteral("elapsed_ms"), timer.elapsed()}});
    return true;
  }

  const auto database_url = resolve_database_url(error_message);
  const auto success = !database_url.isEmpty();
  if (success) {
    observability::logInfo(
        photoEditorCatalogLog(),
        QStringLiteral("catalog.bootstrap.ready"),
        {
            {QStringLiteral("elapsed_ms"), timer.elapsed()},
            {QStringLiteral("managed_local_database"), true},
        });
  } else {
    observability::logWarning(
        photoEditorCatalogLog(),
        QStringLiteral("catalog.bootstrap.unavailable"),
        {
            {QStringLiteral("elapsed_ms"), timer.elapsed()},
            {QStringLiteral("managed_local_database"), true},
        });
  }
  return success;
}

bool AppBootstrap::prepareProcessEnvironment(
    QProcessEnvironment* environment,
    QString* error_message) {
  auto resolved_environment = base_environment();

  if (!resolved_environment.contains(QStringLiteral("PHOTO_EDITOR_DATABASE_URL"))) {
    const auto database_url = resolve_database_url(error_message);
    if (database_url.isEmpty()) {
      return false;
    }

    resolved_environment.insert(QStringLiteral("PHOTO_EDITOR_DATABASE_URL"), database_url);
  }

  if (environment != nullptr) {
    *environment = resolved_environment;
  }

  return true;
}

}  // namespace photo_editor::desktop
