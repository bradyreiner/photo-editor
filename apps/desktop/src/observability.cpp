#include "observability.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QStringList>
#include <QThread>
#include <QUuid>

#include <cstdlib>
#include <cstdio>

#if defined(Q_OS_UNIX)
#include <unistd.h>
#endif

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
  QString log_color_mode;
  QString console_mode;
  QString run_directory;
  QString desktop_log_file_path;
  QHash<QString, int> warning_repeat_counts;
  bool colorize_console = false;
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

QString normalized_color_mode(const QString& value) {
  const auto normalized = value.trimmed().toLower();
  if (normalized == QStringLiteral("always") ||
      normalized == QStringLiteral("auto") ||
      normalized == QStringLiteral("never")) {
    return normalized;
  }
  return QStringLiteral("auto");
}

QString normalized_console_mode(const QString& value) {
  const auto normalized = value.trimmed().toLower();
  if (normalized == QStringLiteral("compact") ||
      normalized == QStringLiteral("json") ||
      normalized == QStringLiteral("quiet") ||
      normalized == QStringLiteral("over_budget")) {
    return normalized;
  }
  return QStringLiteral("compact");
}

bool stderr_is_terminal() {
#if defined(Q_OS_UNIX)
  return isatty(fileno(stderr)) == 1;
#else
  return true;
#endif
}

bool should_colorize_console(const QString& mode) {
  if (mode == QStringLiteral("never") || !qEnvironmentVariable("NO_COLOR").isEmpty()) {
    return false;
  }
  if (mode == QStringLiteral("always")) {
    return true;
  }
  const auto term = qEnvironmentVariable("TERM").trimmed().toLower();
  return stderr_is_terminal() && term != QStringLiteral("dumb");
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

QString ansi_color_for_message(QtMsgType type, const QString& category) {
  switch (type) {
    case QtDebugMsg:
      return QStringLiteral("\x1b[2;37m");
    case QtInfoMsg:
      if (category == QStringLiteral("photo_editor.perf")) {
        return QStringLiteral("\x1b[36m");
      }
      if (category == QStringLiteral("photo_editor.jobs")) {
        return QStringLiteral("\x1b[35m");
      }
      if (category == QStringLiteral("photo_editor.preview")) {
        return QStringLiteral("\x1b[34m");
      }
      return QStringLiteral("\x1b[32m");
    case QtWarningMsg:
      return QStringLiteral("\x1b[33m");
    case QtCriticalMsg:
    case QtFatalMsg:
      return QStringLiteral("\x1b[31;1m");
  }
  return {};
}

QString colorized_console_line(
    QtMsgType type,
    const QString& category,
    const QString& line,
    bool colorize) {
  if (!colorize || line.isEmpty()) {
    return line;
  }
  const auto color = ansi_color_for_message(type, category);
  if (color.isEmpty()) {
    return line;
  }
  return color + line + QStringLiteral("\x1b[0m");
}

QJsonObject fields_object_from_variant_map(const QVariantMap& fields) {
  QJsonObject object;
  for (auto iterator = fields.cbegin(); iterator != fields.cend(); ++iterator) {
    object.insert(iterator.key(), QJsonValue::fromVariant(iterator.value()));
  }
  return object;
}

QString string_field(const QJsonObject& fields, const QString& key) {
  const auto value = fields.value(key);
  if (value.isString()) {
    return value.toString().trimmed();
  }
  if (value.isBool()) {
    return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
  }
  if (value.isDouble()) {
    return QString::number(value.toDouble(), 'f', 0);
  }
  return {};
}

double number_field(const QJsonObject& fields, const QString& key, double fallback = -1.0) {
  const auto value = fields.value(key);
  if (value.isDouble()) {
    return value.toDouble();
  }
  if (value.isString()) {
    bool ok = false;
    const auto parsed = value.toString().trimmed().toDouble(&ok);
    if (ok) {
      return parsed;
    }
  }
  return fallback;
}

bool bool_field(const QJsonObject& fields, const QString& key, bool fallback = false) {
  const auto value = fields.value(key);
  if (value.isBool()) {
    return value.toBool();
  }
  if (value.isString()) {
    const auto normalized = value.toString().trimmed().toLower();
    if (normalized == QStringLiteral("true") || normalized == QStringLiteral("1")) {
      return true;
    }
    if (normalized == QStringLiteral("false") || normalized == QStringLiteral("0")) {
      return false;
    }
  }
  return fallback;
}

QString whole_ms(double value) {
  if (value < 0.0) {
    return {};
  }
  return QStringLiteral("%1ms").arg(qRound64(value));
}

void append_field(QStringList* parts, const QString& label, const QString& value) {
  if (parts == nullptr || value.isEmpty()) {
    return;
  }
  parts->append(QStringLiteral("%1=%2").arg(label, value));
}

void append_ms_field(QStringList* parts, const QString& label, double value) {
  append_field(parts, label, whole_ms(value));
}

QString first_non_empty_field(const QJsonObject& fields, std::initializer_list<QString> keys) {
  for (const auto& key : keys) {
    const auto value = string_field(fields, key);
    if (!value.isEmpty()) {
      return value;
    }
  }
  return {};
}

QString compact_cache_summary(const QJsonObject& fields) {
  const auto hits = number_field(fields, QStringLiteral("cache_hit_count"));
  const auto lookups = number_field(fields, QStringLiteral("cache_lookup_count"));
  if (hits >= 0.0 && lookups >= 0.0) {
    return QStringLiteral("%1/%2").arg(qRound64(hits)).arg(qRound64(lookups));
  }
  if (fields.contains(QStringLiteral("cache_hit"))) {
    return bool_field(fields, QStringLiteral("cache_hit")) ? QStringLiteral("hit")
                                                          : QStringLiteral("miss");
  }
  return {};
}

QString compact_budget_summary(const QJsonObject& fields) {
  const auto budget = number_field(fields, QStringLiteral("render_latency_budget_ms"));
  if (budget < 0.0) {
    return {};
  }
  const auto within_budget = bool_field(
      fields,
      QStringLiteral("render_within_latency_budget"),
      true);
  return QStringLiteral("%1/%2")
      .arg(within_budget ? QStringLiteral("ok") : QStringLiteral("OVER"))
      .arg(whole_ms(budget));
}

QString compact_console_line_for_fields(const QString& event_name, const QJsonObject& fields) {
  QStringList parts{event_name};
  append_field(&parts, QStringLiteral("stage"), string_field(fields, QStringLiteral("stage")));
  append_field(
      &parts,
      QStringLiteral("mode"),
      first_non_empty_field(
          fields,
          {QStringLiteral("render_mode"), QStringLiteral("interaction_mode")}));
  append_field(
      &parts,
      QStringLiteral("reason"),
      first_non_empty_field(
          fields,
          {QStringLiteral("reason"), QStringLiteral("render_reason"), QStringLiteral("cancellation_reason")}));
  append_ms_field(
      &parts,
      QStringLiteral("elapsed"),
      number_field(
          fields,
          QStringLiteral("elapsed_ms"),
          number_field(fields, QStringLiteral("total_elapsed_ms"))));
  append_ms_field(&parts, QStringLiteral("queue"), number_field(fields, QStringLiteral("queue_wait_ms")));
  append_field(&parts, QStringLiteral("budget"), compact_budget_summary(fields));
  append_field(&parts, QStringLiteral("backend"), string_field(fields, QStringLiteral("backend")));
  append_field(&parts, QStringLiteral("fallback"), string_field(fields, QStringLiteral("fallback_reason")));
  append_field(&parts, QStringLiteral("dirty"), string_field(fields, QStringLiteral("dirty_domain")));
  append_field(&parts, QStringLiteral("cache"), compact_cache_summary(fields));
  append_field(
      &parts,
      QStringLiteral("failure"),
      first_non_empty_field(
          fields,
          {QStringLiteral("failure_stage"), QStringLiteral("async_job_stage")}));
  append_field(&parts, QStringLiteral("gen"), string_field(fields, QStringLiteral("generation")));
  return parts.join(' ');
}

bool event_is_over_budget(const QJsonObject& fields) {
  if (!fields.contains(QStringLiteral("render_within_latency_budget"))) {
    return false;
  }
  return !bool_field(fields, QStringLiteral("render_within_latency_budget"), true);
}

bool event_is_attention_worthy(const QString& event_name, const QJsonObject& fields) {
  return event_is_over_budget(fields) ||
         event_name.endsWith(QStringLiteral(".failed")) ||
         event_name.endsWith(QStringLiteral(".failure")) ||
         event_name.endsWith(QStringLiteral(".cancelled")) ||
         event_name.endsWith(QStringLiteral(".canceled")) ||
         event_name.contains(QStringLiteral("failed")) ||
         event_name.contains(QStringLiteral("cancel"));
}

bool console_mode_prints_event_for_fields(
    QtMsgType type,
    const QString& mode,
    const QString& event_name,
    const QJsonObject& fields) {
  if (type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg) {
    return true;
  }
  if (mode == QStringLiteral("quiet")) {
    return false;
  }
  if (mode == QStringLiteral("over_budget")) {
    return !event_name.isEmpty() && event_is_attention_worthy(event_name, fields);
  }
  return true;
}

QString formatted_console_line(
    const QString& timestamp,
    QtMsgType type,
    const QString& category,
    const QString& payload) {
  return QStringLiteral("%1 [%2] %3: %4")
      .arg(timestamp, severity_name(type), category, payload);
}

bool should_rate_limit_warning(QtMsgType type, const QString& category) {
  return type == QtWarningMsg && category.startsWith(QStringLiteral("qt."));
}

bool should_print_warning_repeat_count(int count) {
  return count == 1 || count == 5 || count == 25 || count == 100 || count % 500 == 0;
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
      {QStringLiteral("log_color_mode"), g_state.log_color_mode},
      {QStringLiteral("console_mode"), g_state.console_mode},
      {QStringLiteral("console_color_enabled"), g_state.colorize_console},
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
  bool print_console_line = true;
  QByteArray log_line;

  {
    QMutexLocker locker(&g_state.mutex);
    const auto timestamp = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    console_line = qFormatLogMessage(type, context, message);

    const auto category =
        QString::fromUtf8(context.category == nullptr ? "" : context.category);
    QJsonObject record{
        {QStringLiteral("timestamp"), timestamp},
        {QStringLiteral("severity"), severity_name(type)},
        {QStringLiteral("category"), category},
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
    QString event_name;
    QJsonObject fields;
    if (parse_error.error == QJsonParseError::NoError && parsed_message.isObject()) {
      fields = parsed_message.object();
      event_name = fields.value(QStringLiteral("event")).toString().trimmed();
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

    if (g_state.console_mode != QStringLiteral("json")) {
      print_console_line = console_mode_prints_event_for_fields(
          type, g_state.console_mode, event_name, fields);
      if (print_console_line && !event_name.isEmpty()) {
        console_line = formatted_console_line(
            timestamp,
            type,
            category,
            compact_console_line_for_fields(event_name, fields));
      }
    }
    if (print_console_line && should_rate_limit_warning(type, category)) {
      const auto warning_key = category + QChar('\n') + message;
      const auto repeat_count = g_state.warning_repeat_counts.value(warning_key, 0) + 1;
      g_state.warning_repeat_counts.insert(warning_key, repeat_count);
      print_console_line = should_print_warning_repeat_count(repeat_count);
      if (print_console_line && repeat_count > 1) {
        console_line = formatted_console_line(
            timestamp,
            type,
            category,
            QStringLiteral("repeated %1 times: %2").arg(repeat_count).arg(message));
      }
    }
    console_line = colorized_console_line(
        type, category, console_line, g_state.colorize_console);
  }

  if (print_console_line) {
    std::fputs(console_line.toUtf8().constData(), stderr);
    std::fputc('\n', stderr);
    std::fflush(stderr);
  }

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
  g_state.log_color_mode =
      normalized_color_mode(qEnvironmentVariable(kLogColorEnv).trimmed());
  g_state.console_mode =
      normalized_console_mode(qEnvironmentVariable(kConsoleModeEnv).trimmed());
  g_state.colorize_console = should_colorize_console(g_state.log_color_mode);
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
  qputenv(kLogColorEnv, g_state.log_color_mode.toUtf8());
  qputenv(kConsoleModeEnv, g_state.console_mode.toUtf8());
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
    qunsetenv(kLogColorEnv);
    qunsetenv(kConsoleModeEnv);
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

QString colorizedConsoleLineForMessage(
    QtMsgType type,
    const QString& category,
    const QString& line) {
  return colorized_console_line(type, category, line, true);
}

QString compactConsoleLineForEvent(const QString& event_name, const QVariantMap& fields) {
  return compact_console_line_for_fields(event_name, fields_object_from_variant_map(fields));
}

bool consoleModePrintsEvent(
    QtMsgType type,
    const QString& mode,
    const QString& event_name,
    const QVariantMap& fields) {
  return console_mode_prints_event_for_fields(
      type,
      normalized_console_mode(mode),
      event_name,
      fields_object_from_variant_map(fields));
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
