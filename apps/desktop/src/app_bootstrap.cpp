#include "app_bootstrap.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>

#include <algorithm>

#include "observability.h"
#include "catalog_helper_payload.h"
#include "photo_editor/pipeline/color_profile_asset.h"
#include "photo_editor/pipeline/dcp_profile_asset.h"

namespace photo_editor::desktop {

namespace {

#ifndef PHOTO_EDITOR_APP_VERSION
#define PHOTO_EDITOR_APP_VERSION "0.0.0"
#endif

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

QString configured_storage_location(
    const char* environment_key,
    const char* setting_key,
    const QString& fallback = {}) {
  const auto environment_value = qEnvironmentVariable(environment_key).trimmed();
  if (!environment_value.isEmpty()) {
    return QDir::cleanPath(environment_value);
  }

  const QSettings settings;
  const auto configured_value = settings.value(QString::fromUtf8(setting_key)).toString().trimmed();
  return configured_value.isEmpty() ? fallback : QDir::cleanPath(configured_value);
}

QString executable_name(const QString& binary_name) {
#if defined(Q_OS_WIN)
  return binary_name.endsWith(QStringLiteral(".exe"), Qt::CaseInsensitive) ? binary_name : binary_name + QStringLiteral(".exe");
#else
  return binary_name;
#endif
}

QString repository_root() {
  const auto override_root = qEnvironmentVariable("PHOTO_EDITOR_REPOSITORY_ROOT").trimmed();
  if (!override_root.isEmpty() &&
      QFileInfo::exists(QDir(override_root).filePath(QStringLiteral("rust/Cargo.toml")))) {
    return QDir(override_root).absolutePath();
  }

  const auto source_directory = QFileInfo(QString::fromUtf8(__FILE__)).absoluteDir();
  const auto compiled_root = QDir(source_directory.filePath(QStringLiteral("../../.."))).absolutePath();
  if (QFileInfo::exists(QDir(compiled_root).filePath(QStringLiteral("rust/Cargo.toml")))) {
    return compiled_root;
  }

  auto candidate = QDir::current();
  for (int depth = 0; depth < 8; ++depth) {
    if (QFileInfo::exists(candidate.filePath(QStringLiteral("rust/Cargo.toml")))) {
      return candidate.absolutePath();
    }
    if (!candidate.cdUp()) {
      break;
    }
  }
  return QDir::currentPath();
}

QString debug_binary_path(const QString& binary_name) {
  return QDir(repository_root()).filePath(
      QStringLiteral("rust/target/debug/%1").arg(executable_name(binary_name)));
}

QString release_binary_path(const QString& binary_name) {
  return QDir(repository_root()).filePath(
      QStringLiteral("rust/target/release/%1").arg(executable_name(binary_name)));
}

QString installed_binary_path(const QString& binary_name) {
  return QDir(QCoreApplication::applicationDirPath()).filePath(executable_name(binary_name));
}

QString direct_helper_path(const QString& binary_name) {
  const QFileInfo installed_binary(installed_binary_path(binary_name));
  if (installed_binary.exists() && installed_binary.isExecutable()) {
    return installed_binary.absoluteFilePath();
  }

  const QFileInfo debug_binary(debug_binary_path(binary_name));
  if (debug_binary.exists() && debug_binary.isExecutable()) {
    return debug_binary.absoluteFilePath();
  }

  const QFileInfo release_binary(release_binary_path(binary_name));
  if (release_binary.exists() && release_binary.isExecutable()) {
    return release_binary.absoluteFilePath();
  }

  return {};
}

QStringList helper_arguments(const QString& binary_name, const QStringList& arguments) {
  if (!direct_helper_path(binary_name).isEmpty()) {
    return arguments;
  }

  QStringList cargo_arguments = {
      QStringLiteral("run"),
      QStringLiteral("--quiet"),
      QStringLiteral("--manifest-path"),
      QDir(repository_root()).filePath(QStringLiteral("rust/Cargo.toml")),
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
               "%1 could not start the PostgreSQL catalog service in the current runtime environment. "
               "The desktop shell will continue to load, but Library-backed features need either "
               "a writable runtime root or an explicit PHOTO_EDITOR_DATABASE_URL.")
        .arg(binary_name);
  }

  return failure_output;
}

enum class TimelinePayloadWriteResult {
  Complete,
  ChildExited,
  TimedOut,
  Failed,
};

int remaining_helper_timeout_ms(const QElapsedTimer& timer, int timeout_ms) {
  if (timeout_ms < 0) {
    return -1;
  }
  const auto elapsed_ms = timer.elapsed();
  if (elapsed_ms >= timeout_ms) {
    return 0;
  }
  return timeout_ms - static_cast<int>(elapsed_ms);
}

template <typename Sink>
bool visit_timeline_stdin_envelope(
    const QStringList& arguments, Sink&& sink, qsizetype* encoded_size) {
  constexpr qsizetype kMaximumStringChunkUtf16 = 1024 * 1024;
  if (encoded_size == nullptr || arguments.size() > 32) {
    return false;
  }

  const auto empty_envelope = QJsonDocument(QJsonObject{
      {QStringLiteral("schema"), QStringLiteral("photo-editor-catalog-arguments:v1")},
      {QStringLiteral("arguments"), QJsonArray{}},
  }).toJson(QJsonDocument::Compact);
  const auto empty_array_marker = empty_envelope.indexOf("[]");
  if (empty_array_marker < 0) {
    return false;
  }
  const auto prefix = empty_envelope.left(empty_array_marker + 1);
  const auto suffix = empty_envelope.mid(empty_array_marker + 1);

  *encoded_size = 0;
  const auto emit_bytes = [&](const QByteArray& bytes) {
    if (bytes.size() > kMaxCatalogHelperPayloadBytes - *encoded_size || !sink(bytes)) {
      return false;
    }
    *encoded_size += bytes.size();
    return true;
  };
  if (!emit_bytes(prefix)) {
    return false;
  }

  for (qsizetype argument_index = 0; argument_index < arguments.size(); ++argument_index) {
    const auto& argument = arguments.at(argument_index);
    qsizetype offset = 0;
    bool first_chunk = true;
    do {
      auto chunk_length = std::min(kMaximumStringChunkUtf16, argument.size() - offset);
      if (chunk_length > 0 && offset + chunk_length < argument.size() &&
          argument.at(offset + chunk_length - 1).isHighSurrogate() &&
          argument.at(offset + chunk_length).isLowSurrogate()) {
        --chunk_length;
      }
      const auto last_chunk = offset + chunk_length == argument.size();
      const auto chunk = argument.mid(offset, chunk_length);
      const auto wrapped_chunk = QJsonDocument(QJsonArray{chunk}).toJson(QJsonDocument::Compact);
      if (wrapped_chunk.size() < 2 || !wrapped_chunk.startsWith("[\"") ||
          !wrapped_chunk.endsWith("\"]")) {
        return false;
      }
      QByteArray encoded_chunk;
      encoded_chunk.reserve(wrapped_chunk.size());
      if (first_chunk) {
        encoded_chunk.append('"');
      }
      encoded_chunk.append(wrapped_chunk.constData() + 2, wrapped_chunk.size() - 4);
      if (last_chunk) {
        encoded_chunk.append('"');
      }
      if (!emit_bytes(encoded_chunk)) {
        return false;
      }
      offset += chunk_length;
      first_chunk = false;
    } while (offset < argument.size());

    if (argument_index + 1 < arguments.size() && !emit_bytes(QByteArrayLiteral(","))) {
      return false;
    }
  }

  return emit_bytes(suffix);
}

bool measure_timeline_stdin_envelope(
    const QStringList& arguments, qsizetype* encoded_size) {
  return visit_timeline_stdin_envelope(
      arguments, [](const QByteArray&) { return true; }, encoded_size);
}

TimelinePayloadWriteResult queue_timeline_payload_chunk(
    QProcess* process, const QByteArray& bytes, int timeout_ms,
    const QElapsedTimer& helper_timer) {
  constexpr qsizetype kWriteChunkBytes = 64 * 1024;
  constexpr qint64 kMaximumQueuedBytes = 256 * 1024;
  qsizetype offset = 0;
  while (offset < bytes.size()) {
    if (process->state() != QProcess::Running) {
      return TimelinePayloadWriteResult::ChildExited;
    }
    if (remaining_helper_timeout_ms(helper_timer, timeout_ms) == 0) {
      return TimelinePayloadWriteResult::TimedOut;
    }
    const auto chunk_bytes = std::min(kWriteChunkBytes, bytes.size() - offset);
    while (process->bytesToWrite() + static_cast<qint64>(chunk_bytes) > kMaximumQueuedBytes) {
      if (process->state() != QProcess::Running) {
        return TimelinePayloadWriteResult::ChildExited;
      }
      const auto remaining_ms = remaining_helper_timeout_ms(helper_timer, timeout_ms);
      if (remaining_ms == 0) {
        return TimelinePayloadWriteResult::TimedOut;
      }
      if (!process->waitForBytesWritten(remaining_ms)) {
        if (process->state() != QProcess::Running) {
          return TimelinePayloadWriteResult::ChildExited;
        }
        return remaining_helper_timeout_ms(helper_timer, timeout_ms) == 0
                   ? TimelinePayloadWriteResult::TimedOut
                   : TimelinePayloadWriteResult::Failed;
      }
    }

    const auto written = process->write(
        bytes.constData() + offset, static_cast<qint64>(chunk_bytes));
    if (written <= 0) {
      return process->state() == QProcess::Running
                 ? TimelinePayloadWriteResult::Failed
                 : TimelinePayloadWriteResult::ChildExited;
    }
    offset += static_cast<qsizetype>(written);
  }
  return TimelinePayloadWriteResult::Complete;
}

TimelinePayloadWriteResult write_timeline_payload_in_bounded_chunks(
    QProcess* process, const QStringList& arguments, qsizetype expected_size,
    int timeout_ms, const QElapsedTimer& helper_timer) {
  auto write_result = TimelinePayloadWriteResult::Complete;
  qsizetype streamed_size = 0;
  const auto streamed = visit_timeline_stdin_envelope(
      arguments,
      [&](const QByteArray& bytes) {
        write_result = queue_timeline_payload_chunk(
            process, bytes, timeout_ms, helper_timer);
        return write_result == TimelinePayloadWriteResult::Complete;
      },
      &streamed_size);
  if (!streamed) {
    return write_result == TimelinePayloadWriteResult::Complete
               ? TimelinePayloadWriteResult::Failed
               : write_result;
  }
  if (streamed_size != expected_size) {
    return TimelinePayloadWriteResult::Failed;
  }

  while (process->bytesToWrite() > 0) {
    if (process->state() != QProcess::Running) {
      return TimelinePayloadWriteResult::ChildExited;
    }
    const auto remaining_ms = remaining_helper_timeout_ms(helper_timer, timeout_ms);
    if (remaining_ms == 0) {
      return TimelinePayloadWriteResult::TimedOut;
    }
    if (!process->waitForBytesWritten(remaining_ms)) {
      if (process->state() != QProcess::Running) {
        return TimelinePayloadWriteResult::ChildExited;
      }
      return remaining_helper_timeout_ms(helper_timer, timeout_ms) == 0
                 ? TimelinePayloadWriteResult::TimedOut
                 : TimelinePayloadWriteResult::Failed;
    }
  }
  if (process->state() != QProcess::Running) {
    return TimelinePayloadWriteResult::ChildExited;
  }
  if (remaining_helper_timeout_ms(helper_timer, timeout_ms) == 0) {
    return TimelinePayloadWriteResult::TimedOut;
  }
  // Closing stdin delivers EOF only after all bounded queued bytes have been
  // written. The worker's later waitForFinished uses the same deadline, so
  // streaming and child execution cannot each consume a fresh full timeout.
  process->closeWriteChannel();
  return TimelinePayloadWriteResult::Complete;
}

void stop_helper_process(QProcess* process) {
  if (process->state() == QProcess::NotRunning) {
    return;
  }
  process->terminate();
  if (!process->waitForFinished(500)) {
    process->kill();
    process->waitForFinished(500);
  }
}

QProcessEnvironment base_environment() {
  auto environment = QProcessEnvironment::systemEnvironment();
  environment.insert(QStringLiteral("PHOTO_EDITOR_APP_VERSION"), QStringLiteral(PHOTO_EDITOR_APP_VERSION));
  if (!environment.contains(QStringLiteral("PHOTO_EDITOR_REQUIRE_PRE_MIGRATION_BACKUP"))) {
    environment.insert(QStringLiteral("PHOTO_EDITOR_REQUIRE_PRE_MIGRATION_BACKUP"), QStringLiteral("1"));
  }
  environment.insert(QStringLiteral("PHOTO_EDITOR_APP_DATA_DIR"), app_data_root());
  environment.insert(
      QStringLiteral("PHOTO_EDITOR_MANAGED_MEDIA_DIR"),
      AppBootstrap::photoLibraryLocation());
  const auto originals_backup_location = AppBootstrap::originalsBackupLocation();
  if (originals_backup_location.isEmpty()) {
    environment.remove(QStringLiteral("PHOTO_EDITOR_ORIGINALS_BACKUP_DIR"));
  } else {
    environment.insert(
        QStringLiteral("PHOTO_EDITOR_ORIGINALS_BACKUP_DIR"),
        originals_backup_location);
  }
  environment.insert(QStringLiteral("PHOTO_EDITOR_LOG_DIR"), QDir(app_data_root()).filePath(QStringLiteral("logs")));
  environment.insert(
      QStringLiteral("PHOTO_EDITOR_BACKUP_DIR"),
      AppBootstrap::backupLocation());
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
    const QProcessEnvironment& environment,
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
          {QStringLiteral("operation"), arguments.value(0)},
          {QStringLiteral("argument_count"), arguments.size()},
          {QStringLiteral("timeout_ms"), timeout_ms},
      });
  QProcess process;
  process.setWorkingDirectory(repository_root());
  process.setProcessEnvironment(environment);
  process.setProcessChannelMode(QProcess::MergedChannels);
  // Large job and timeline payloads can exceed OS argv limits. Leave small
  // legacy CLI calls intact and transport only the large upsert/import bodies
  // through the bounded stdin envelope.
  const auto large_export_job_payload =
      binary_name == QStringLiteral("catalog_export_jobs") &&
      (arguments.value(0) == QStringLiteral("upsert") ||
       arguments.value(0) == QStringLiteral("import-legacy"));
  const auto large_timeline_document_payload =
      binary_name == QStringLiteral("catalog_timeline_documents") &&
      arguments.value(0) == QStringLiteral("upsert");
  const auto use_stdin =
      (large_export_job_payload || large_timeline_document_payload) &&
      catalog_helper_payload_uses_stdin(arguments.value(1));
  const auto stream_timeline_payload = use_stdin && large_timeline_document_payload;
  qsizetype timeline_stdin_payload_size = 0;
  if (stream_timeline_payload) {
    if (!measure_timeline_stdin_envelope(arguments, &timeline_stdin_payload_size)) {
      if (error_message != nullptr) {
        *error_message = QStringLiteral("Recipe payload exceeds the 256 MiB safety limit or has invalid arguments.");
      }
      return false;
    }
    process.setProperty("recipePayloadBytes", timeline_stdin_payload_size);
    process.start(
        AppBootstrap::helperProgram(binary_name),
        AppBootstrap::helperArguments(binary_name, {QStringLiteral("--stdin-json-v1")}));
  } else if (use_stdin) {
    if (!start_catalog_helper_stdin(
            &process, AppBootstrap::helperProgram(binary_name),
            AppBootstrap::helperArguments(binary_name, {QStringLiteral("--stdin-json-v1")}),
            arguments, error_message)) {
      return false;
    }
  } else {
    process.start(AppBootstrap::helperProgram(binary_name), AppBootstrap::helperArguments(binary_name, arguments));
  }

  if (!process.waitForStarted(5000)) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("Unable to start %1: %2").arg(binary_name, process.errorString());
    }
    observability::logWarning(
        photoEditorCatalogLog(),
        QStringLiteral("catalog.helper.failed_to_start"),
        {
            {QStringLiteral("binary"), binary_name},
            {QStringLiteral("operation"), arguments.value(0)},
            {QStringLiteral("error"), process.errorString()},
            {QStringLiteral("elapsed_ms"), timer.elapsed()},
        });
    return false;
  }

  QElapsedTimer helper_timer;
  if (stream_timeline_payload) {
    helper_timer.start();
    const auto write_result = write_timeline_payload_in_bounded_chunks(
        &process, arguments, timeline_stdin_payload_size, timeout_ms, helper_timer);
    if (write_result == TimelinePayloadWriteResult::TimedOut) {
      stop_helper_process(&process);
      if (error_message != nullptr) {
        *error_message = QStringLiteral("%1 timed out; no further helper work is running.")
                             .arg(binary_name);
      }
      observability::logWarning(
          photoEditorCatalogLog(),
          QStringLiteral("catalog.helper.timed_out"),
          {
              {QStringLiteral("binary"), binary_name},
              {QStringLiteral("operation"), arguments.value(0)},
              {QStringLiteral("elapsed_ms"), timer.elapsed()},
          });
      return false;
    }
    if (write_result == TimelinePayloadWriteResult::Failed) {
      stop_helper_process(&process);
      if (error_message != nullptr) {
        *error_message = QStringLiteral("Could not send the complete timeline document payload to %1.")
                             .arg(binary_name);
      }
      observability::logWarning(
          photoEditorCatalogLog(),
          QStringLiteral("catalog.helper.stdin_write_failed"),
          {
              {QStringLiteral("binary"), binary_name},
              {QStringLiteral("operation"), arguments.value(0)},
              {QStringLiteral("elapsed_ms"), timer.elapsed()},
          });
      return false;
    }
    if (write_result == TimelinePayloadWriteResult::ChildExited &&
        process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0) {
      if (error_message != nullptr) {
        *error_message = QStringLiteral("%1 exited before accepting the complete timeline document payload.")
                             .arg(binary_name);
      }
      return false;
    }
  }

  const auto completion_timeout_ms = stream_timeline_payload
                                         ? remaining_helper_timeout_ms(helper_timer, timeout_ms)
                                         : timeout_ms;
  if (process.state() != QProcess::NotRunning &&
      !process.waitForFinished(completion_timeout_ms)) {
    stop_helper_process(&process);
    if (error_message != nullptr) {
      *error_message = QStringLiteral("%1 timed out; no further helper work is running.")
                           .arg(binary_name);
    }
    observability::logWarning(
        photoEditorCatalogLog(),
        QStringLiteral("catalog.helper.timed_out"),
        {
            {QStringLiteral("binary"), binary_name},
            {QStringLiteral("operation"), arguments.value(0)},
            {QStringLiteral("elapsed_ms"), timer.elapsed()},
        });
    return false;
  }

  if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
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
          base_environment(),
          &output,
          error_message,
          60000)) {
    return {};
  }
  return output;
}

}  // namespace

QString AppBootstrap::appDataLocation() { return app_data_root(); }

QString AppBootstrap::photoLibraryLocation() {
  return configured_storage_location(
      "PHOTO_EDITOR_MANAGED_MEDIA_DIR",
      "storage/photoLibraryLocation",
      QDir(app_data_root()).filePath(QStringLiteral("managed")));
}

QString AppBootstrap::originalsBackupLocation() {
  return configured_storage_location(
      "PHOTO_EDITOR_ORIGINALS_BACKUP_DIR",
      "storage/originalsBackupLocation");
}

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

QString AppBootstrap::backupLocation() {
  return configured_storage_location(
      "PHOTO_EDITOR_BACKUP_DIR",
      "storage/catalogBackupLocation",
      QDir(app_data_root()).filePath(QStringLiteral("backups")));
}

QString AppBootstrap::iccProfileAssetLocation() {
  return QDir(app_data_root()).filePath(QStringLiteral("color-assets/icc"));
}

QString AppBootstrap::dcpProfileAssetLocation() {
  return QDir(app_data_root()).filePath(QStringLiteral("color-assets/dcp"));
}

bool AppBootstrap::ensureRuntimeDirectories() {
  const auto originals_backup_location = originalsBackupLocation();
  const auto runtime_ready = !ensure_directory(appDataLocation()).isEmpty() &&
                             !ensure_directory(photoLibraryLocation()).isEmpty() &&
                             (originals_backup_location.isEmpty() ||
                              !ensure_directory(originals_backup_location).isEmpty()) &&
                             !ensure_directory(cacheLocation()).isEmpty() &&
                             !ensure_directory(logLocation()).isEmpty() &&
                             !ensure_directory(backupLocation()).isEmpty() &&
                             !ensure_directory(iccProfileAssetLocation()).isEmpty() &&
                             !ensure_directory(dcpProfileAssetLocation()).isEmpty();
  if (runtime_ready) {
    photo_editor::pipeline::default_icc_profile_asset_registry().set_storage_root(
        iccProfileAssetLocation().toStdString());
    photo_editor::pipeline::default_dcp_profile_asset_registry().set_storage_root(
        dcpProfileAssetLocation().toStdString());
  }
  return runtime_ready;
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
            {QStringLiteral("backend"), QStringLiteral("postgresql")},
            {QStringLiteral("managed_local_database"), true},
        });
  } else {
    observability::logWarning(
        photoEditorCatalogLog(),
        QStringLiteral("catalog.bootstrap.unavailable"),
        {
            {QStringLiteral("elapsed_ms"), timer.elapsed()},
            {QStringLiteral("backend"), QStringLiteral("postgresql")},
            {QStringLiteral("managed_local_database"), true},
        });
  }
  return success;
}

QString AppBootstrap::createCatalogBackup(QString* error_message) {
  QString output;
  if (!run_helper(
          QStringLiteral("catalog_local_db"),
          {QStringLiteral("backup")},
          base_environment(),
          &output,
          error_message,
          120000)) {
    return {};
  }
  return output;
}

QString AppBootstrap::repairCatalogFromLatestBackup(QString* error_message) {
  QString output;
  if (!run_helper(
          QStringLiteral("catalog_local_db"),
          {QStringLiteral("restore-latest")},
          base_environment(),
          &output,
          error_message,
          180000)) {
    return {};
  }
  return output;
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

bool AppBootstrap::runCatalogHelper(
    const QString& binary_name,
    const QStringList& arguments,
    QString* output,
    QString* error_message,
    int timeout_ms) {
  QProcessEnvironment environment;
  if (!prepareProcessEnvironment(&environment, error_message)) {
    return false;
  }
  return run_helper(
      binary_name,
      arguments,
      environment,
      output,
      error_message,
      timeout_ms);
}

QString AppBootstrap::helperProgram(const QString& binary_name) {
  const auto helper_path = direct_helper_path(binary_name);
  return helper_path.isEmpty() ? QStringLiteral("cargo") : helper_path;
}

QStringList AppBootstrap::helperArguments(const QString& binary_name, const QStringList& arguments) {
  return helper_arguments(binary_name, arguments);
}

}  // namespace photo_editor::desktop
