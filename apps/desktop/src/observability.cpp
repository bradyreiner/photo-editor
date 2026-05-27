#include "observability.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QMutex>
#include <QMutexLocker>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QStringList>
#include <QThread>
#include <QUuid>

#include <cstdlib>
#include <cstdio>

#include "app_bootstrap.h"

Q_LOGGING_CATEGORY(photoEditorAppLog, "photo_editor.app")
Q_LOGGING_CATEGORY(photoEditorCatalogLog, "photo_editor.catalog")
Q_LOGGING_CATEGORY(photoEditorJobsLog, "photo_editor.jobs")
Q_LOGGING_CATEGORY(photoEditorPreviewLog, "photo_editor.preview")
Q_LOGGING_CATEGORY(photoEditorPerfLog, "photo_editor.perf")

namespace photo_editor::desktop::observability {

namespace {

#if defined(PHOTO_EDITOR_ENABLE_DEV_OBSERVABILITY) && PHOTO_EDITOR_ENABLE_DEV_OBSERVABILITY
constexpr bool kDevObservabilityEnabled = true;
#else
constexpr bool kDevObservabilityEnabled = false;
#endif

struct ObservabilityState final {
  QMutex mutex;
  QFile log_file;
  QString session_id;
  QString log_level;
  QString run_directory;
  QString desktop_log_file_path;
  bool initialized = false;
};

ObservabilityState g_state;

QString normalized_level(const QString& value) {
  const auto normalized = value.trimmed().toLower();
  if (normalized == QStringLiteral("trace") ||
      normalized == QStringLiteral("debug") ||
      normalized == QStringLiteral("info") ||
      normalized == QStringLiteral("warning") ||
      normalized == QStringLiteral("error")) {
    return normalized;
  }
  return QStringLiteral("info");
}

QString session_id_from_environment() {
  const auto session_id = qEnvironmentVariable(kSessionIdEnv).trimmed();
  return session_id.isEmpty()
             ? QUuid::createUuid().toString(QUuid::WithoutBraces)
             : session_id;
}

QString log_level_from_environment() {
  const auto level = qEnvironmentVariable(kLogLevelEnv).trimmed();
  return normalized_level(level.isEmpty() ? QStringLiteral("info") : level);
}

QString default_run_directory(const QString& session_id) {
  const auto timestamp =
      QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMddTHHmmss.zzzZ"));
  return QDir(AppBootstrap::logLocation())
      .filePath(QStringLiteral("runs/%1-%2").arg(timestamp, session_id.left(8)));
}

void apply_logging_rules(const QString& level) {
  QStringList rules = {
      QStringLiteral("qt.qml.binding.removal.info=false"),
      QStringLiteral("photo_editor.*.debug=false"),
      QStringLiteral("photo_editor.*.info=true"),
      QStringLiteral("photo_editor.*.warning=true"),
  };
  if (level == QStringLiteral("trace") || level == QStringLiteral("debug")) {
    rules[1] = QStringLiteral("photo_editor.*.debug=true");
  }
  if (level == QStringLiteral("warning")) {
    rules[2] = QStringLiteral("photo_editor.*.info=false");
  }
  if (level == QStringLiteral("error")) {
    rules[2] = QStringLiteral("photo_editor.*.info=false");
    rules[3] = QStringLiteral("photo_editor.*.warning=false");
  }
  QLoggingCategory::setFilterRules(rules.join('\n'));
}

QString severity_name(QtMsgType type) {
  switch (type) {
    case QtDebugMsg:
      return QStringLiteral("debug");
    case QtInfoMsg:
      return QStringLiteral("info");
    case QtWarningMsg:
      return QStringLiteral("warning");
    case QtCriticalMsg:
      return QStringLiteral("error");
    case QtFatalMsg:
      return QStringLiteral("fatal");
  }
  return QStringLiteral("info");
}

QString thread_id_string() {
  return QStringLiteral("0x%1")
      .arg(reinterpret_cast<quintptr>(QThread::currentThreadId()), 0, 16);
}

QString workflow_for_event(const QString& event_name) {
  if (event_name.startsWith(QStringLiteral("preview."))) {
    return QStringLiteral("preview");
  }
  if (event_name.startsWith(QStringLiteral("thumbnail.")) ||
      event_name.startsWith(QStringLiteral("benchmark.thumbnail"))) {
    return QStringLiteral("thumbnail");
  }
  if (event_name.startsWith(QStringLiteral("catalog.")) ||
      event_name.startsWith(QStringLiteral("catalog_"))) {
    return QStringLiteral("catalog");
  }
  if (event_name.startsWith(QStringLiteral("job."))) {
    return QStringLiteral("jobs");
  }
  if (event_name.startsWith(QStringLiteral("desktop.startup"))) {
    return QStringLiteral("startup");
  }
  if (event_name.startsWith(QStringLiteral("benchmark."))) {
    return QStringLiteral("benchmark");
  }
  return {};
}

void add_if_missing(QJsonObject* fields, const QString& key, const QJsonValue& value) {
  if (fields == nullptr || fields->contains(key) || value.isUndefined() || value.isNull()) {
    return;
  }
  if (value.isString() && value.toString().trimmed().isEmpty()) {
    return;
  }
  fields->insert(key, value);
}

void add_standard_event_fields(const QString& event_name, QJsonObject* fields) {
  if (fields == nullptr) {
    return;
  }

  add_if_missing(fields, QStringLiteral("workflow"), workflow_for_event(event_name));
  add_if_missing(fields, QStringLiteral("thread"), thread_id_string());
  add_if_missing(fields, QStringLiteral("reason"), fields->value(QStringLiteral("render_reason")));

  if (!fields->contains(QStringLiteral("interactive")) &&
      fields->contains(QStringLiteral("full_resolution"))) {
    fields->insert(
        QStringLiteral("interactive"),
        !fields->value(QStringLiteral("full_resolution")).toBool());
  }
  add_if_missing(fields, QStringLiteral("interactive"), fields->value(QStringLiteral("draft_interactive")));

  add_if_missing(fields, QStringLiteral("cache_hit"), fields->value(QStringLiteral("decoded_cache_hit")));
  add_if_missing(fields, QStringLiteral("cache_hit"), fields->value(QStringLiteral("linear_cache_hit")));
  add_if_missing(fields, QStringLiteral("cache_hit"), fields->value(QStringLiteral("decoded_cache_lookup")));
}

void cleanup_old_run_directories(const QString& logs_root_path) {
  QDir runs_root(QDir(logs_root_path).filePath(QStringLiteral("runs")));
  if (!runs_root.exists()) {
    return;
  }

  const auto run_entries = runs_root.entryInfoList(
      QDir::Dirs | QDir::NoDotAndDotDot,
      QDir::Time | QDir::Reversed);
  constexpr int kMaxRunDirectories = 20;
  for (int index = 0; index < run_entries.size() - kMaxRunDirectories; ++index) {
    QDir(run_entries.at(index).absoluteFilePath()).removeRecursively();
  }
}

void write_session_metadata_file(const QString& file_path) {
  QJsonObject metadata{
      {QStringLiteral("session_id"), g_state.session_id},
      {QStringLiteral("log_level"), g_state.log_level},
      {QStringLiteral("run_directory"), g_state.run_directory},
      {QStringLiteral("desktop_log_file"), g_state.desktop_log_file_path},
      {QStringLiteral("pid"), static_cast<qint64>(QCoreApplication::applicationPid())},
      {QStringLiteral("application"), QCoreApplication::applicationName()},
      {QStringLiteral("organization"), QCoreApplication::organizationName()},
      {QStringLiteral("started_at"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
  };
  QFile metadata_file(file_path);
  if (!metadata_file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    return;
  }
  metadata_file.write(QJsonDocument(metadata).toJson(QJsonDocument::Indented));
  metadata_file.close();
}

void observability_message_handler(
    QtMsgType type,
    const QMessageLogContext& context,
    const QString& message) {
  QString console_line;
  QByteArray log_line;

  {
    QMutexLocker locker(&g_state.mutex);
    console_line = qFormatLogMessage(type, context, message);

    QJsonObject record{
        {QStringLiteral("timestamp"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
        {QStringLiteral("severity"), severity_name(type)},
        {QStringLiteral("category"), QString::fromUtf8(context.category == nullptr ? "" : context.category)},
        {QStringLiteral("session_id"), g_state.session_id},
        {QStringLiteral("pid"), static_cast<qint64>(QCoreApplication::applicationPid())},
        {QStringLiteral("thread_id"), thread_id_string()},
    };
    if (context.file != nullptr) {
      record.insert(QStringLiteral("file"), QString::fromUtf8(context.file));
    }
    if (context.line > 0) {
      record.insert(QStringLiteral("line"), context.line);
    }
    if (context.function != nullptr) {
      record.insert(QStringLiteral("function"), QString::fromUtf8(context.function));
    }

    const auto trimmed_message = message.trimmed();
    QJsonParseError parse_error;
    const auto parsed_message =
        QJsonDocument::fromJson(trimmed_message.toUtf8(), &parse_error);
    if (parse_error.error == QJsonParseError::NoError && parsed_message.isObject()) {
      auto fields = parsed_message.object();
      const auto event_name = fields.value(QStringLiteral("event")).toString().trimmed();
      if (!event_name.isEmpty()) {
        record.insert(QStringLiteral("event"), event_name);
      }
      add_standard_event_fields(event_name, &fields);
      record.insert(QStringLiteral("fields"), fields);
    } else {
      record.insert(QStringLiteral("message"), message);
    }

    log_line = QJsonDocument(record).toJson(QJsonDocument::Compact);
    log_line.append('\n');
    if (g_state.log_file.isOpen()) {
      g_state.log_file.write(log_line);
      g_state.log_file.flush();
    }
  }

  std::fputs(console_line.toUtf8().constData(), stderr);
  std::fputc('\n', stderr);
  std::fflush(stderr);

  if (type == QtFatalMsg) {
    std::abort();
  }
}

void ensure_initialized_locked() {
  if (!kDevObservabilityEnabled) {
    return;
  }
  if (g_state.initialized) {
    return;
  }

  g_state.session_id = session_id_from_environment();
  g_state.log_level = log_level_from_environment();
  g_state.run_directory = qEnvironmentVariable(kRunDirectoryEnv).trimmed();
  if (g_state.run_directory.isEmpty()) {
    g_state.run_directory = default_run_directory(g_state.session_id);
  }
  g_state.run_directory = QDir::cleanPath(g_state.run_directory);
  g_state.desktop_log_file_path =
      QDir(g_state.run_directory).filePath(QStringLiteral("desktop.jsonl"));

  QDir().mkpath(g_state.run_directory);
  cleanup_old_run_directories(AppBootstrap::logLocation());

  g_state.log_file.setFileName(g_state.desktop_log_file_path);
  if (g_state.log_file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
    write_session_metadata_file(
        QDir(g_state.run_directory).filePath(QStringLiteral("session.json")));
  }

  qputenv(kEnabledEnv, QByteArrayLiteral("1"));
  qputenv(kSessionIdEnv, g_state.session_id.toUtf8());
  qputenv(kLogLevelEnv, g_state.log_level.toUtf8());
  qputenv(kRunDirectoryEnv, g_state.run_directory.toUtf8());

  qSetMessagePattern(
      QStringLiteral("%{time yyyy-MM-ddTHH:mm:ss.zzzZ} [%{type}] %{category}: %{message}"));
  apply_logging_rules(g_state.log_level);
  qInstallMessageHandler(observability_message_handler);
  g_state.initialized = true;
}

}  // namespace

bool enabled() { return kDevObservabilityEnabled; }

void initialize() {
  if (!kDevObservabilityEnabled) {
    qunsetenv(kEnabledEnv);
    qunsetenv(kSessionIdEnv);
    qunsetenv(kLogLevelEnv);
    qunsetenv(kRunDirectoryEnv);
    return;
  }
  QMutexLocker locker(&g_state.mutex);
  ensure_initialized_locked();
}

QString sessionId() {
  if (!kDevObservabilityEnabled) {
    return {};
  }
  QMutexLocker locker(&g_state.mutex);
  ensure_initialized_locked();
  return g_state.session_id;
}

QString logLevel() {
  if (!kDevObservabilityEnabled) {
    return {};
  }
  QMutexLocker locker(&g_state.mutex);
  ensure_initialized_locked();
  return g_state.log_level;
}

QString runDirectory() {
  if (!kDevObservabilityEnabled) {
    return {};
  }
  QMutexLocker locker(&g_state.mutex);
  ensure_initialized_locked();
  return g_state.run_directory;
}

QString desktopLogFilePath() {
  if (!kDevObservabilityEnabled) {
    return {};
  }
  QMutexLocker locker(&g_state.mutex);
  ensure_initialized_locked();
  return g_state.desktop_log_file_path;
}

QString structuredMessage(const QString& event_name, const QVariantMap& fields) {
  QJsonObject message_fields;
  message_fields.insert(QStringLiteral("event"), event_name);
  for (auto iterator = fields.cbegin(); iterator != fields.cend(); ++iterator) {
    message_fields.insert(iterator.key(), QJsonValue::fromVariant(iterator.value()));
  }
  add_standard_event_fields(event_name, &message_fields);
  return QString::fromUtf8(QJsonDocument(message_fields).toJson(QJsonDocument::Compact));
}

void logInfo(const QLoggingCategory& category, const QString& event_name, const QVariantMap& fields) {
  if (!kDevObservabilityEnabled) {
    return;
  }
  qCInfo(category).noquote() << structuredMessage(event_name, fields);
}

void logWarning(const QLoggingCategory& category, const QString& event_name, const QVariantMap& fields) {
  if (!kDevObservabilityEnabled) {
    return;
  }
  qCWarning(category).noquote() << structuredMessage(event_name, fields);
}

}  // namespace photo_editor::desktop::observability
