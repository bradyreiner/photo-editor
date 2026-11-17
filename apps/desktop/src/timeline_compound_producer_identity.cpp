#include "timeline_compound_producer_identity.h"

#include <cmath>
#include <cerrno>
#include <cstring>
#include <limits>

#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QMetaType>
#include <QProcess>
#include <QSaveFile>
#include <QStandardPaths>
#include <QThread>
#include <QVariantList>
#include <QVector>
#include <QUuid>

#if defined(Q_OS_MACOS)
#include <mach/mach_time.h>
#include <libproc.h>
#include <sys/proc_info.h>
#include <sys/sysctl.h>
#include <sys/time.h>
#include <signal.h>
#include <unistd.h>
#endif

namespace photo_editor::desktop::timeline_compound::producer {
namespace {

bool fail(QString* error_message, const QString& message) {
  if (error_message != nullptr) *error_message = message;
  return false;
}

QStringList strict_string_list(const QVariant& value) {
  if (value.metaType().id() == QMetaType::QStringList) return value.toStringList();
  if (value.metaType().id() != QMetaType::QVariantList) return {};
  const auto values = value.toList();
  QStringList result;
  result.reserve(values.size());
  for (const auto& item : values) {
    if (item.metaType().id() != QMetaType::QString) return {};
    result.push_back(item.toString());
  }
  return result;
}

bool canonical_nonexistent_path(const QString& path, QString* parent_path) {
  const QFileInfo info(path);
  if (path.trimmed().isEmpty() || !info.isAbsolute() || info.fileName().isEmpty() ||
      info.exists() || info.isSymLink()) {
    return false;
  }
  const auto parent = info.dir().canonicalPath();
  if (parent.isEmpty() || QDir(parent).filePath(info.fileName()) != path) return false;
  if (parent_path != nullptr) *parent_path = parent;
  return true;
}

bool canonical_owned_sibling(
    const QString& path,
    const QString& expected_name,
    const QString& expected_parent,
    bool require_existing_file) {
  const QFileInfo info(path);
  if (path.trimmed().isEmpty() || !info.isAbsolute() || info.fileName() != expected_name ||
      info.dir().canonicalPath() != expected_parent ||
      QDir(expected_parent).filePath(info.fileName()) != path || info.isSymLink()) {
    return false;
  }
  if (require_existing_file) {
    return info.isFile() && info.canonicalFilePath() == path;
  }
  return !info.exists();
}

bool valid_producer_paths(
    const QString& child_timeline_id,
    const QString& document_path,
    const QString& render_path,
    bool receipt_may_exist,
    bool render_may_exist) {
  const auto normalized_id = child_timeline_id.trimmed().toLower();
  const QUuid parsed_id(normalized_id);
  if (parsed_id.isNull() || parsed_id.toString(QUuid::WithoutBraces).toLower() != normalized_id) {
    return false;
  }
  const QFileInfo document_info(document_path);
  const auto parent = document_info.dir().canonicalPath();
  if (parent.isEmpty() ||
      !canonical_owned_sibling(document_path,
                               QStringLiteral("compound-%1.json").arg(normalized_id), parent, true)) {
    return false;
  }
  const auto final_name = QStringLiteral("compound-%1.mov").arg(normalized_id);
  const QFileInfo final_info(render_path);
  const auto final_path_valid = render_may_exist
      ? (final_info.isAbsolute() && final_info.fileName() == final_name &&
         final_info.dir().canonicalPath() == parent &&
         QDir(parent).filePath(final_info.fileName()) == render_path &&
         !final_info.isSymLink() &&
         (!final_info.exists() || (final_info.isFile() && final_info.canonicalFilePath() == render_path)))
      : canonical_owned_sibling(render_path, final_name, parent, false);
  if (!final_path_valid) return false;
  const auto partial = partial_render_path(render_path);
  const auto graph = filter_graph_sidecar_path(render_path);
  const auto producer_receipt = receipt_path(render_path);
  const auto valid_running_sibling = [parent](const QString& path, const QString& name) {
    const QFileInfo info(path);
    return info.isAbsolute() && info.fileName() == name && info.dir().canonicalPath() == parent &&
           QDir(parent).filePath(info.fileName()) == path && !info.isSymLink() &&
           (!info.exists() || (info.isFile() && info.canonicalFilePath() == path));
  };
  if (!valid_running_sibling(partial, QStringLiteral("compound-%1.partial.mov").arg(normalized_id)) ||
      !valid_running_sibling(graph, QStringLiteral("compound-%1.ffgraph").arg(normalized_id))) {
    return false;
  }
  if (receipt_may_exist && QFileInfo::exists(producer_receipt)) {
    return canonical_owned_sibling(
        producer_receipt,
        QStringLiteral("compound-%1.producer.json").arg(normalized_id),
        parent,
        true);
  }
  return canonical_owned_sibling(
      producer_receipt,
      QStringLiteral("compound-%1.producer.json").arg(normalized_id),
      parent,
      false);
}

QString receipt_hash(const QVariantMap& receipt) {
  auto body = receipt;
  body.remove(QStringLiteral("receiptSha256"));
  const auto bytes = QJsonDocument::fromVariant(body).toJson(QJsonDocument::Compact);
  return QString::fromLatin1(
      QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

#if defined(Q_OS_MACOS)
struct ProcessIdentity final {
  pid_t pid = -1;
  uid_t uid = static_cast<uid_t>(-1);
  pid_t process_group_id = -1;
  quint64 start_seconds = 0;
  quint64 start_microseconds = 0;
  QString boot_session_id;
  QString executable_path;
  QStringList argv;
};

QString boot_session_id(QString* error_message) {
  mach_timebase_info_data_t timebase{};
  struct timeval wall_time {};
  if (mach_timebase_info(&timebase) != KERN_SUCCESS || timebase.denom == 0 ||
      gettimeofday(&wall_time, nullptr) != 0) {
    fail(error_message, QStringLiteral("The system boot-session identity is unavailable."));
    return {};
  }
  const auto wall_seconds = static_cast<qint64>(wall_time.tv_sec);
  const auto uptime_seconds = static_cast<qint64>(
      static_cast<long double>(mach_absolute_time()) * timebase.numer /
      static_cast<long double>(timebase.denom) / 1000000000.0L);
  if (wall_seconds <= uptime_seconds) {
    fail(error_message, QStringLiteral("The system boot-session identity is invalid."));
    return {};
  }
  // Minute granularity absorbs the small sampling skew between wall and
  // monotonic clocks while still distinguishing independent macOS boots.
  return QStringLiteral("macos-boot-minute:%1").arg((wall_seconds - uptime_seconds) / 60);
}

bool read_process_arguments(pid_t pid, QStringList* arguments, QString* error_message) {
  if (arguments == nullptr || pid <= 0) {
    return fail(error_message, QStringLiteral("The producer argument request is invalid."));
  }
  int mib[3] = {CTL_KERN, KERN_PROCARGS2, pid};
  size_t buffer_size = 0;
  if (sysctl(mib, 3, nullptr, &buffer_size, nullptr, 0) != 0 || buffer_size == 0 ||
      buffer_size > 1024U * 1024U) {
    if (errno == ESRCH) return fail(error_message, QStringLiteral("The producer process exited."));
    return fail(error_message, QStringLiteral("The kernel did not expose producer arguments."));
  }
  QByteArray buffer(static_cast<qsizetype>(buffer_size), Qt::Uninitialized);
  if (sysctl(mib, 3, buffer.data(), &buffer_size, nullptr, 0) != 0 ||
      buffer_size < sizeof(int)) {
    if (errno == ESRCH) return fail(error_message, QStringLiteral("The producer process exited."));
    return fail(error_message, QStringLiteral("The kernel producer argument read failed."));
  }

  int argument_count = 0;
  std::memcpy(&argument_count, buffer.constData(), sizeof(argument_count));
  if (argument_count <= 0 || argument_count > 4096) {
    return fail(error_message, QStringLiteral("The kernel returned an invalid producer argument count."));
  }
  auto offset = static_cast<qsizetype>(sizeof(argument_count));
  const auto* data = buffer.constData();
  const auto length = static_cast<qsizetype>(buffer_size);
  while (offset < length && data[offset] != '\0') ++offset;
  if (offset >= length) {
    return fail(error_message, QStringLiteral("The kernel producer executable argument is malformed."));
  }
  while (offset < length && data[offset] == '\0') ++offset;
  QStringList parsed;
  parsed.reserve(argument_count);
  for (int index = 0; index < argument_count; ++index) {
    if (offset >= length) {
      return fail(error_message, QStringLiteral("The kernel producer argument vector is truncated."));
    }
    const auto end = buffer.indexOf('\0', offset);
    if (end < 0) {
      return fail(error_message, QStringLiteral("The kernel producer argument vector is truncated."));
    }
    parsed.push_back(QString::fromLocal8Bit(data + offset, end - offset));
    offset = end + 1;
  }
  *arguments = parsed;
  return true;
}

bool read_process_identity(pid_t pid, ProcessIdentity* identity, QString* error_message) {
  if (identity == nullptr || pid <= 0) {
    return fail(error_message, QStringLiteral("The producer identity request is invalid."));
  }
  struct proc_bsdinfo info {};
  errno = 0;
  const auto info_size = proc_pidinfo(
      pid, PROC_PIDTBSDINFO, 0, &info, static_cast<int>(sizeof(info)));
  if (info_size != static_cast<int>(sizeof(info))) {
    if (errno == ESRCH) return fail(error_message, QStringLiteral("The producer process exited."));
    return fail(error_message, QStringLiteral("The kernel did not expose producer process identity."));
  }
  if (info.pbi_pid != static_cast<uint32_t>(pid)) {
    return fail(error_message, QStringLiteral("The producer PID identity changed during inspection."));
  }

  QByteArray executable_buffer(4096, Qt::Uninitialized);
  const auto executable_size = proc_pidpath(pid, executable_buffer.data(),
                                             static_cast<uint32_t>(executable_buffer.size()));
  if (executable_size <= 0 || executable_size >= executable_buffer.size()) {
    if (errno == ESRCH) return fail(error_message, QStringLiteral("The producer process exited."));
    return fail(error_message, QStringLiteral("The kernel did not expose the producer executable path."));
  }
  executable_buffer[executable_size] = '\0';
  QStringList argv;
  if (!read_process_arguments(pid, &argv, error_message)) return false;

  // Read the kernel identity a second time. This rejects a PID that exited and
  // was reused while path/argv were being collected.
  struct proc_bsdinfo confirmed {};
  errno = 0;
  if (proc_pidinfo(pid, PROC_PIDTBSDINFO, 0, &confirmed,
                   static_cast<int>(sizeof(confirmed))) != static_cast<int>(sizeof(confirmed))) {
    if (errno == ESRCH) return fail(error_message, QStringLiteral("The producer process exited."));
    return fail(error_message, QStringLiteral("Producer identity could not be confirmed."));
  }
  if (confirmed.pbi_pid != info.pbi_pid || confirmed.pbi_uid != info.pbi_uid ||
      confirmed.pbi_start_tvsec != info.pbi_start_tvsec ||
      confirmed.pbi_start_tvusec != info.pbi_start_tvusec) {
    return fail(error_message, QStringLiteral("The producer PID was reused during identity inspection."));
  }

  identity->pid = pid;
  identity->uid = info.pbi_uid;
  identity->process_group_id = static_cast<pid_t>(info.pbi_pgid);
  identity->start_seconds = info.pbi_start_tvsec;
  identity->start_microseconds = info.pbi_start_tvusec;
  identity->boot_session_id = boot_session_id(error_message);
  if (identity->boot_session_id.isEmpty()) return false;
  identity->executable_path = QFileInfo(QString::fromLocal8Bit(executable_buffer.constData()))
                                  .canonicalFilePath();
  identity->argv = argv;
  if (identity->executable_path.isEmpty()) {
    return fail(error_message, QStringLiteral("The producer executable path is not canonical."));
  }
  return true;
}

bool receipt_matches_process(const QVariantMap& receipt, const ProcessIdentity& identity) {
  const auto expected_arguments = strict_string_list(receipt.value(QStringLiteral("argv")));
  return receipt.value(QStringLiteral("pid")).toLongLong() == identity.pid &&
         receipt.value(QStringLiteral("uid")).toUInt() == identity.uid &&
         receipt.value(QStringLiteral("processGroupId")).toLongLong() == identity.process_group_id &&
         receipt.value(QStringLiteral("startTimeSeconds")).toULongLong() == identity.start_seconds &&
         receipt.value(QStringLiteral("startTimeMicroseconds")).toULongLong() == identity.start_microseconds &&
         receipt.value(QStringLiteral("bootSessionId")).toString() == identity.boot_session_id &&
         receipt.value(QStringLiteral("executablePath")).toString() == identity.executable_path &&
         expected_arguments == identity.argv;
}
#endif

}  // namespace

bool validate_task_output_paths(
    const QString& child_timeline_id,
    const QString& document_path,
    const QString& render_path,
    QString* error_message) {
  if (error_message != nullptr) error_message->clear();
  const auto normalized_id = child_timeline_id.trimmed().toLower();
  const QUuid id(normalized_id);
  const auto canonical_id = id.isNull()
      ? QString{}
      : id.toString(QUuid::WithoutBraces).toLower();
  if (canonical_id.isEmpty() || canonical_id != normalized_id) {
    return fail(error_message,
                QStringLiteral("A task-owned compound needs a canonical child timeline ID."));
  }

  QString document_parent;
  QString render_parent;
  const QFileInfo document_info(document_path);
  const QFileInfo render_info(render_path);
  if (!canonical_nonexistent_path(document_path, &document_parent) ||
      !canonical_nonexistent_path(render_path, &render_parent) ||
      document_parent != render_parent ||
      document_info.fileName() != QStringLiteral("compound-%1.json").arg(canonical_id) ||
      render_info.fileName() != QStringLiteral("compound-%1.mov").arg(canonical_id)) {
    return fail(error_message,
                QStringLiteral("Compound document and render paths must be new canonical files with the admitted child ID in their names."));
  }

  const auto partial_path = partial_render_path(render_path);
  const auto graph_path = filter_graph_sidecar_path(render_path);
  QString partial_parent;
  QString graph_parent;
  if (partial_path.isEmpty() ||
      !canonical_nonexistent_path(partial_path, &partial_parent) ||
      partial_parent != document_parent || graph_path.isEmpty() ||
      !canonical_nonexistent_path(graph_path, &graph_parent) ||
      graph_parent != document_parent) {
    return fail(error_message,
                QStringLiteral("The compound staging and graph paths must be absent in the same admitted output directory."));
  }
  QString receipt_parent;
  const auto producer_receipt_file = receipt_path(render_path);
  if (producer_receipt_file.isEmpty() ||
      !canonical_nonexistent_path(producer_receipt_file, &receipt_parent) ||
      receipt_parent != document_parent) {
    return fail(error_message,
                QStringLiteral("The compound producer receipt path must be a new canonical sibling in the admitted output directory."));
  }
  return true;
}

QString partial_render_path(const QString& render_path) {
  const QFileInfo info(render_path);
  const auto suffix = info.suffix().toLower();
  if (!info.isAbsolute() || info.fileName().isEmpty() || suffix != QStringLiteral("mov")) {
    return {};
  }
  return QDir(info.dir().absolutePath()).filePath(
      info.completeBaseName() + QStringLiteral(".partial.") + suffix);
}

QString filter_graph_sidecar_path(const QString& render_path) {
  const QFileInfo info(render_path);
  if (!info.isAbsolute() || info.fileName().isEmpty() ||
      info.suffix().compare(QStringLiteral("mov"), Qt::CaseInsensitive) != 0) {
    return {};
  }
  return QDir(info.dir().absolutePath()).filePath(
      info.completeBaseName() + QStringLiteral(".ffgraph"));
}

QString receipt_path(const QString& render_path) {
  const QFileInfo info(render_path);
  if (!info.isAbsolute() || info.fileName().isEmpty() ||
      info.suffix().compare(QStringLiteral("mov"), Qt::CaseInsensitive) != 0) {
    return {};
  }
  return QDir(info.dir().absolutePath()).filePath(
      info.completeBaseName() + QStringLiteral(".producer.json"));
}

bool write_receipt(
    const QString& receipt_path,
    const QVariantMap& receipt,
    QString* error_message) {
  if (error_message != nullptr) error_message->clear();
  const auto render_path = receipt.value(QStringLiteral("renderPath")).toString();
  const auto expected_path = photo_editor::desktop::timeline_compound::producer::receipt_path(render_path);
  const QFileInfo destination(receipt_path);
  if (receipt_path != expected_path || receipt_path.trimmed().isEmpty() ||
      !destination.isAbsolute() || destination.exists() || destination.isSymLink() ||
      destination.dir().canonicalPath().isEmpty() ||
      QDir(destination.dir().canonicalPath()).filePath(destination.fileName()) != receipt_path ||
      receipt.value(QStringLiteral("receiptSha256")).toString() != receipt_hash(receipt) ||
      receipt.value(QStringLiteral("filterGraphPath")).toString() != filter_graph_sidecar_path(render_path) ||
      receipt.value(QStringLiteral("partialPath")).toString() != partial_render_path(render_path)) {
    return fail(error_message, QStringLiteral("The compound producer receipt path or integrity check failed."));
  }
  const auto process_state = inspect_receipt(receipt, error_message);
  if (process_state != IdentityState::Matching &&
      process_state != IdentityState::NotRunning) {
    return false;
  }
  QSaveFile file(receipt_path);
  const auto bytes = QJsonDocument::fromVariant(receipt).toJson(QJsonDocument::Indented);
  if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
    return fail(error_message, QStringLiteral("The exact compound producer receipt could not be saved."));
  }
  const QFileInfo written(receipt_path);
  if (!written.isFile() || written.isSymLink() || written.canonicalFilePath() != receipt_path) {
    return fail(error_message, QStringLiteral("The saved compound producer receipt is not a canonical regular file."));
  }
  return true;
}

QVariantMap read_receipt(
    const QString& receipt_path,
    QString* error_message) {
  if (error_message != nullptr) error_message->clear();
  const QFileInfo info(receipt_path);
  if (!info.isFile() || info.isSymLink() || !info.isReadable() ||
      info.canonicalFilePath() != receipt_path || info.size() <= 0 || info.size() > 1024 * 1024) {
    fail(error_message, QStringLiteral("The compound producer receipt is not a bounded canonical file."));
    return {};
  }
  QFile file(receipt_path);
  if (!file.open(QIODevice::ReadOnly)) {
    fail(error_message, QStringLiteral("The compound producer receipt could not be read."));
    return {};
  }
  QJsonParseError parse_error;
  const auto document = QJsonDocument::fromJson(file.readAll(), &parse_error);
  if (parse_error.error != QJsonParseError::NoError || !document.isObject()) {
    fail(error_message, QStringLiteral("The compound producer receipt JSON is invalid."));
    return {};
  }
  const auto receipt = document.object().toVariantMap();
  const auto render_path = receipt.value(QStringLiteral("renderPath")).toString();
  const auto child_id = receipt.value(QStringLiteral("childTimelineId")).toString();
  const auto operation_id = receipt.value(QStringLiteral("operationId")).toString();
  const QUuid child_uuid(child_id);
  const QUuid operation_uuid(operation_id);
  if (receipt_path != photo_editor::desktop::timeline_compound::producer::receipt_path(render_path) ||
      receipt.value(QStringLiteral("schema")).toString() != QStringLiteral("looksy.timeline.compound-producer/v1") ||
      receipt.value(QStringLiteral("receiptSha256")).toString() != receipt_hash(receipt) ||
      child_uuid.isNull() || child_uuid.toString(QUuid::WithoutBraces).toLower() != child_id ||
      operation_uuid.isNull() || operation_uuid.toString(QUuid::WithoutBraces).toLower() != operation_id ||
      !valid_producer_paths(child_id, receipt.value(QStringLiteral("documentPath")).toString(), render_path,
                            true, true) ||
      receipt.value(QStringLiteral("partialPath")).toString() != partial_render_path(render_path) ||
      receipt.value(QStringLiteral("filterGraphPath")).toString() != filter_graph_sidecar_path(render_path)) {
    fail(error_message, QStringLiteral("The compound producer receipt did not match its exact canonical task paths."));
    return {};
  }
  return receipt;
}

QVariantMap capture_receipt(
    qint64 process_id,
    const QString& executable_path,
    const QStringList& arguments,
    const QString& operation_id,
    const QString& child_timeline_id,
    const QString& document_path,
    const QString& render_path,
    QString* error_message) {
  if (error_message != nullptr) error_message->clear();
  const auto canonical_executable = QFileInfo(executable_path).canonicalFilePath();
  const auto normalized_operation = operation_id.trimmed().toLower();
  const auto normalized_child = child_timeline_id.trimmed().toLower();
  const QUuid operation_uuid(normalized_operation);
  const QUuid child_uuid(normalized_child);
  const auto canonical_operation = operation_uuid.isNull()
      ? QString{}
      : operation_uuid.toString(QUuid::WithoutBraces).toLower();
  const auto canonical_child = child_uuid.isNull()
      ? QString{}
      : child_uuid.toString(QUuid::WithoutBraces).toLower();
  if (canonical_executable.isEmpty() || canonical_operation != normalized_operation ||
      canonical_child != normalized_child ||
      !valid_producer_paths(child_timeline_id, document_path, render_path, false, false)) {
    fail(error_message, QStringLiteral("The compound producer receipt inputs are invalid."));
    return {};
  }

#if defined(Q_OS_MACOS)
  if (process_id <= 0 || process_id > std::numeric_limits<pid_t>::max()) {
    fail(error_message, QStringLiteral("The compound producer PID is invalid."));
    return {};
  }
  ProcessIdentity identity;
  if (!read_process_identity(static_cast<pid_t>(process_id), &identity, error_message)) return {};
  QStringList expected_argv{canonical_executable};
  expected_argv.append(arguments);
  if (identity.uid != geteuid() || identity.executable_path != canonical_executable ||
      identity.argv != expected_argv) {
    fail(error_message,
         QStringLiteral("Producer mismatch uid=%1/%2 exe=%3/%4 argv=%5 expected=%6")
             .arg(identity.uid)
             .arg(static_cast<uint>(geteuid()))
             .arg(identity.executable_path, canonical_executable,
                  identity.argv.join(QLatin1Char('|')), expected_argv.join(QLatin1Char('|'))));
    return {};
  }
  QVariantMap receipt{
      {QStringLiteral("schema"), QStringLiteral("looksy.timeline.compound-producer/v1")},
      {QStringLiteral("operationId"), canonical_operation},
      {QStringLiteral("childTimelineId"), canonical_child},
      {QStringLiteral("pid"), static_cast<qlonglong>(identity.pid)},
      {QStringLiteral("uid"), static_cast<uint>(identity.uid)},
      {QStringLiteral("processGroupId"), static_cast<qlonglong>(identity.process_group_id)},
      {QStringLiteral("startTimeSeconds"), QVariant::fromValue<qulonglong>(identity.start_seconds)},
      {QStringLiteral("startTimeMicroseconds"), QVariant::fromValue<qulonglong>(identity.start_microseconds)},
      {QStringLiteral("bootSessionId"), identity.boot_session_id},
      {QStringLiteral("executablePath"), identity.executable_path},
      {QStringLiteral("argv"), QVariant::fromValue(identity.argv)},
      {QStringLiteral("documentPath"), document_path},
      {QStringLiteral("partialPath"), partial_render_path(render_path)},
      {QStringLiteral("renderPath"), render_path},
      {QStringLiteral("filterGraphPath"), filter_graph_sidecar_path(render_path)},
      {QStringLiteral("producerReceiptPath"), receipt_path(render_path)}};
  receipt.insert(QStringLiteral("receiptSha256"), receipt_hash(receipt));
  return receipt;
#else
  Q_UNUSED(process_id);
  Q_UNUSED(arguments);
  fail(error_message, QStringLiteral("Durable compound producer identity is supported only on macOS."));
  return {};
#endif
}

QVariantMap make_launch_specification(
    const QString& executable_path,
    const QStringList& arguments,
    const QString& operation_id,
    const QString& child_timeline_id,
    const QString& document_path,
    const QString& render_path,
    QString* error_message) {
  if (error_message != nullptr) error_message->clear();
  const auto canonical_executable = QFileInfo(executable_path).canonicalFilePath();
  const auto normalized_operation = operation_id.trimmed().toLower();
  const auto normalized_child = child_timeline_id.trimmed().toLower();
  const QUuid operation_uuid(normalized_operation);
  const QUuid child_uuid(normalized_child);
  const auto partial_path = partial_render_path(render_path);
  const auto operation_canonical = operation_uuid.isNull()
      ? QString{}
      : operation_uuid.toString(QUuid::WithoutBraces).toLower();
  const auto child_canonical = child_uuid.isNull()
      ? QString{}
      : child_uuid.toString(QUuid::WithoutBraces).toLower();
  if (canonical_executable.isEmpty() || operation_canonical != normalized_operation ||
      child_canonical != normalized_child || partial_path.isEmpty() ||
      !valid_producer_paths(child_timeline_id, document_path, render_path, false, false)) {
    fail(error_message, QStringLiteral("The compound producer launch specification inputs are invalid."));
    return {};
  }

#if defined(Q_OS_MACOS)
  QString boot_error;
  const auto session = boot_session_id(&boot_error);
  if (session.isEmpty()) {
    if (error_message != nullptr) *error_message = boot_error;
    return {};
  }
  QStringList argv{canonical_executable};
  argv.append(arguments);
  if (!argv.contains(partial_path)) {
    fail(error_message, QStringLiteral("The launch argument vector must name its unique task-owned partial path."));
    return {};
  }
  QVariantMap specification{
      {QStringLiteral("schema"), QStringLiteral("looksy.timeline.compound-launch/v1")},
      {QStringLiteral("operationId"), normalized_operation},
      {QStringLiteral("childTimelineId"), normalized_child},
      {QStringLiteral("uid"), static_cast<uint>(geteuid())},
      {QStringLiteral("bootSessionId"), session},
      {QStringLiteral("executablePath"), canonical_executable},
      {QStringLiteral("argv"), QVariant::fromValue(argv)},
      {QStringLiteral("documentPath"), document_path},
      {QStringLiteral("partialPath"), partial_path},
      {QStringLiteral("renderPath"), render_path},
      {QStringLiteral("filterGraphPath"), filter_graph_sidecar_path(render_path)},
      {QStringLiteral("producerReceiptPath"), receipt_path(render_path)}};
  specification.insert(QStringLiteral("launchSpecSha256"), receipt_hash(specification));
  return specification;
#else
  Q_UNUSED(arguments);
  fail(error_message, QStringLiteral("Durable compound producer identity is supported only on macOS."));
  return {};
#endif
}

bool validate_launch_specification(
    const QVariantMap& launch_spec,
    QString* error_message) {
  if (error_message != nullptr) error_message->clear();
  const auto child_id = launch_spec.value(QStringLiteral("childTimelineId")).toString();
  const auto operation_id = launch_spec.value(QStringLiteral("operationId")).toString();
  const auto document_path = launch_spec.value(QStringLiteral("documentPath")).toString();
  const auto render_path = launch_spec.value(QStringLiteral("renderPath")).toString();
  const auto argv = strict_string_list(launch_spec.value(QStringLiteral("argv")));
  const auto executable_path = launch_spec.value(QStringLiteral("executablePath")).toString();
  const auto boot_id = launch_spec.value(QStringLiteral("bootSessionId")).toString();
  const QUuid child_uuid(child_id);
  const QUuid operation_uuid(operation_id);
  auto body = launch_spec;
  const auto supplied_hash = body.take(QStringLiteral("launchSpecSha256")).toString();
  if (launch_spec.value(QStringLiteral("schema")).toString() != QStringLiteral("looksy.timeline.compound-launch/v1") ||
      supplied_hash.isEmpty() || supplied_hash != receipt_hash(body) ||
      child_uuid.isNull() || child_uuid.toString(QUuid::WithoutBraces).toLower() != child_id ||
      operation_uuid.isNull() || operation_uuid.toString(QUuid::WithoutBraces).toLower() != operation_id ||
      QFileInfo(executable_path).canonicalFilePath() != executable_path || argv.size() < 2 ||
      argv.first() != executable_path ||
      !argv.contains(partial_render_path(render_path)) || boot_id.isEmpty() ||
      launch_spec.value(QStringLiteral("partialPath")).toString() != partial_render_path(render_path) ||
      launch_spec.value(QStringLiteral("filterGraphPath")).toString() != filter_graph_sidecar_path(render_path) ||
      launch_spec.value(QStringLiteral("producerReceiptPath")).toString() != receipt_path(render_path) ||
      // A sealed launch spec remains valid after its own writer creates the
      // receipt/final sibling. This validator is also used for recovery and
      // inspection; freshness is enforced before prepare/start, while here we
      // accept only absent or canonical regular task-owned siblings.
      !valid_producer_paths(child_id, document_path, render_path, true, true)) {
    return fail(error_message, QStringLiteral("The compound producer launch specification failed validation."));
  }
#if defined(Q_OS_MACOS)
  if (launch_spec.value(QStringLiteral("uid")).toUInt() != static_cast<uint>(geteuid())) {
    return fail(error_message, QStringLiteral("The compound producer launch UID differs from the current user."));
  }
  return true;
#else
  return fail(error_message, QStringLiteral("Durable compound producer identity is supported only on macOS."));
#endif
}

CandidateState find_unique_candidate(
    const QVariantMap& launch_spec,
    QVariantMap* producer_receipt,
    QString* error_message) {
  if (producer_receipt != nullptr) producer_receipt->clear();
  if (error_message != nullptr) error_message->clear();
  const auto child_id = launch_spec.value(QStringLiteral("childTimelineId")).toString();
  const auto operation_id = launch_spec.value(QStringLiteral("operationId")).toString();
  const auto document_path = launch_spec.value(QStringLiteral("documentPath")).toString();
  const auto render_path = launch_spec.value(QStringLiteral("renderPath")).toString();
  const auto argv = strict_string_list(launch_spec.value(QStringLiteral("argv")));
  const auto executable_path = launch_spec.value(QStringLiteral("executablePath")).toString();
  const auto boot_id = launch_spec.value(QStringLiteral("bootSessionId")).toString();
  const QUuid child_uuid(child_id);
  const QUuid operation_uuid(operation_id);
  const auto partial_path = partial_render_path(render_path);
  auto specification_body = launch_spec;
  const auto spec_hash = specification_body.take(QStringLiteral("launchSpecSha256")).toString();
  const auto canonical_executable = QFileInfo(executable_path).canonicalFilePath();
  if (launch_spec.value(QStringLiteral("schema")).toString() != QStringLiteral("looksy.timeline.compound-launch/v1") ||
      spec_hash.isEmpty() || spec_hash != receipt_hash(specification_body) ||
      child_uuid.isNull() || child_uuid.toString(QUuid::WithoutBraces).toLower() != child_id ||
      operation_uuid.isNull() || operation_uuid.toString(QUuid::WithoutBraces).toLower() != operation_id ||
      canonical_executable != executable_path || argv.size() < 2 || argv.first() != executable_path ||
      !argv.contains(partial_path) ||
      launch_spec.value(QStringLiteral("producerReceiptPath")).toString() != receipt_path(render_path) ||
      launch_spec.value(QStringLiteral("filterGraphPath")).toString() != filter_graph_sidecar_path(render_path) ||
      !valid_producer_paths(child_id, document_path, render_path, false, false)) {
    fail(error_message, QStringLiteral("The compound producer launch specification failed validation."));
    return CandidateState::Unavailable;
  }

#if defined(Q_OS_MACOS)
  if (launch_spec.value(QStringLiteral("uid")).toUInt() != static_cast<uint>(geteuid())) {
    fail(error_message, QStringLiteral("The compound producer launch UID differs from the current user."));
    return CandidateState::Unavailable;
  }
  constexpr int kProcessListBufferBytes = 1024 * 1024;
  QByteArray process_ids(kProcessListBufferBytes, Qt::Uninitialized);
  const auto bytes = proc_listpids(
      PROC_ALL_PIDS, 0, process_ids.data(), kProcessListBufferBytes);
  if (bytes <= 0 || static_cast<qsizetype>(bytes) >= process_ids.size() ||
      bytes % static_cast<int>(sizeof(pid_t)) != 0) {
    fail(error_message, QStringLiteral("The kernel process list is incomplete; candidate search is not safe."));
    return CandidateState::Unavailable;
  }
  const auto* pids = reinterpret_cast<const pid_t*>(process_ids.constData());
  const auto pid_count = bytes / static_cast<int>(sizeof(pid_t));
  auto matched = QVariantMap{};
  auto match_count = 0;
  for (int index = 0; index < pid_count; ++index) {
    const auto pid = pids[index];
    if (pid <= 0) continue;
    struct proc_bsdinfo basic_info {};
    errno = 0;
    if (proc_pidinfo(pid, PROC_PIDTBSDINFO, 0, &basic_info,
                     static_cast<int>(sizeof(basic_info))) != static_cast<int>(sizeof(basic_info))) {
      continue;
    }
    if (basic_info.pbi_uid != static_cast<uid_t>(launch_spec.value(QStringLiteral("uid")).toUInt())) {
      continue;
    }
    ProcessIdentity identity;
    QString inspect_error;
    if (!read_process_identity(pid, &identity, &inspect_error)) {
      // We only fail closed on an unreadable process when the kernel says it
      // could be the same executable. Otherwise it cannot match the launch.
      QByteArray path_buffer(4096, Qt::Uninitialized);
      const auto path_length = proc_pidpath(pid, path_buffer.data(), static_cast<uint32_t>(path_buffer.size()));
      if (path_length <= 0) continue;
      path_buffer[path_length] = '\0';
      const auto candidate_path = QFileInfo(QString::fromLocal8Bit(path_buffer.constData())).canonicalFilePath();
      if (candidate_path == executable_path) {
        if (error_message != nullptr) *error_message = inspect_error;
        return CandidateState::Unavailable;
      }
      continue;
    }
    if (identity.uid != launch_spec.value(QStringLiteral("uid")).toUInt() ||
        identity.executable_path != executable_path || identity.argv != argv ||
        identity.boot_session_id != boot_id) {
      continue;
    }
    QString capture_error;
    const auto candidate_receipt = capture_receipt(
        pid, executable_path, argv.mid(1), operation_id, child_id,
        document_path, render_path, &capture_error);
    if (candidate_receipt.isEmpty()) {
      if (error_message != nullptr) *error_message = capture_error;
      return CandidateState::Unavailable;
    }
    matched = candidate_receipt;
    ++match_count;
    if (match_count > 1) {
      fail(error_message, QStringLiteral("More than one live process matches the unique compound partial path."));
      return CandidateState::Ambiguous;
    }
  }
  if (match_count == 0) return CandidateState::NoMatch;
  if (producer_receipt != nullptr) *producer_receipt = matched;
  return CandidateState::UniqueMatch;
#else
  Q_UNUSED(boot_id);
  fail(error_message, QStringLiteral("Durable compound producer identity is supported only on macOS."));
  return CandidateState::Unavailable;
#endif
}

IdentityState inspect_receipt(
    const QVariantMap& receipt,
    QString* error_message) {
  if (error_message != nullptr) error_message->clear();
  const auto schema = receipt.value(QStringLiteral("schema")).toString();
  const auto child_id = receipt.value(QStringLiteral("childTimelineId")).toString();
  const auto document_path = receipt.value(QStringLiteral("documentPath")).toString();
  const auto render_path = receipt.value(QStringLiteral("renderPath")).toString();
  if (schema != QStringLiteral("looksy.timeline.compound-producer/v1") ||
      receipt.value(QStringLiteral("receiptSha256")).toString() != receipt_hash(receipt) ||
      receipt.value(QStringLiteral("partialPath")).toString() != partial_render_path(render_path) ||
      receipt.value(QStringLiteral("filterGraphPath")).toString() != filter_graph_sidecar_path(render_path) ||
      receipt.value(QStringLiteral("producerReceiptPath")).toString() != receipt_path(render_path) ||
      !valid_producer_paths(child_id, document_path, render_path, true, true)) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("The compound producer receipt failed its schema, hash, or exact-path check.");
    }
    return IdentityState::Unavailable;
  }

#if defined(Q_OS_MACOS)
  const auto process_id = receipt.value(QStringLiteral("pid")).toLongLong();
  if (process_id <= 0 || process_id > std::numeric_limits<pid_t>::max()) {
    if (error_message != nullptr) *error_message = QStringLiteral("The compound producer receipt PID is invalid.");
    return IdentityState::Unavailable;
  }
  ProcessIdentity identity;
  QString inspect_error;
  if (!read_process_identity(static_cast<pid_t>(process_id), &identity, &inspect_error)) {
    if (inspect_error == QStringLiteral("The producer process exited.")) {
      return IdentityState::NotRunning;
    }
    if (error_message != nullptr) *error_message = inspect_error;
    return IdentityState::Unavailable;
  }
  if (!receipt_matches_process(receipt, identity)) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("The producer PID is present but UID, executable, argv, or start identity differs from the journal receipt.");
    }
    return IdentityState::Mismatch;
  }
  return IdentityState::Matching;
#else
  if (error_message != nullptr) {
    *error_message = QStringLiteral("Durable compound producer identity is supported only on macOS.");
  }
  return IdentityState::Unavailable;
#endif
}

bool stop_verified_producer(
    const QVariantMap& receipt,
    int graceful_timeout_ms,
    bool* process_stopped,
    QString* error_message) {
  if (process_stopped != nullptr) *process_stopped = false;
  if (error_message != nullptr) error_message->clear();
  const auto state = inspect_receipt(receipt, error_message);
  if (state == IdentityState::NotRunning) {
    if (process_stopped != nullptr) *process_stopped = true;
    return true;
  }
  if (state != IdentityState::Matching) return false;

#if defined(Q_OS_MACOS)
  const auto process_id = static_cast<pid_t>(receipt.value(QStringLiteral("pid")).toLongLong());
  QString verify_error;
  if (inspect_receipt(receipt, &verify_error) != IdentityState::Matching) {
    if (error_message != nullptr) *error_message = verify_error;
    return false;
  }
  if (kill(process_id, SIGTERM) != 0 && errno != ESRCH) {
    if (error_message != nullptr) *error_message = QStringLiteral("The verified compound producer could not receive SIGTERM.");
    return false;
  }

  const auto deadline_ms = qMax(0, graceful_timeout_ms);
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() <= deadline_ms) {
    QString poll_error;
    const auto current = inspect_receipt(receipt, &poll_error);
    if (current == IdentityState::NotRunning) {
      if (process_stopped != nullptr) *process_stopped = true;
      return true;
    }
    if (current != IdentityState::Matching) {
      if (error_message != nullptr) *error_message = poll_error;
      return false;
    }
    QThread::msleep(20);
  }

  QString verify_before_kill;
  if (inspect_receipt(receipt, &verify_before_kill) != IdentityState::Matching) {
    if (error_message != nullptr) *error_message = verify_before_kill;
    return false;
  }
  if (kill(process_id, SIGKILL) != 0 && errno != ESRCH) {
    if (error_message != nullptr) *error_message = QStringLiteral("The verified compound producer could not receive SIGKILL.");
    return false;
  }
  QElapsedTimer kill_timer;
  kill_timer.start();
  while (kill_timer.elapsed() <= 2000) {
    QString poll_error;
    const auto current = inspect_receipt(receipt, &poll_error);
    if (current == IdentityState::NotRunning) {
      if (process_stopped != nullptr) *process_stopped = true;
      return true;
    }
    if (current != IdentityState::Matching) {
      if (error_message != nullptr) *error_message = poll_error;
      return false;
    }
    QThread::msleep(20);
  }
  if (error_message != nullptr) *error_message = QStringLiteral("The verified compound producer remained present after SIGKILL.");
  return false;
#else
  if (error_message != nullptr) {
    *error_message = QStringLiteral("Verified producer termination is supported only on macOS.");
  }
  return false;
#endif
}

}  // namespace photo_editor::desktop::timeline_compound::producer
