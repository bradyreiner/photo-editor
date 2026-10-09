#pragma once

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QString>
#include <QStringList>

namespace photo_editor::desktop {

inline constexpr qsizetype kMaxCatalogHelperPayloadBytes = 256 * 1024 * 1024;
inline constexpr qsizetype kCatalogHelperStdinThresholdBytes = 32 * 1024;

inline bool catalog_helper_payload_uses_stdin(const QString& payload) {
  // QString's UTF-16 code-unit count is a lower bound on Qt's UTF-8 output;
  // invalid surrogate units encode as '?'. Avoid a full payload copy to choose.
  if (payload.size() > kCatalogHelperStdinThresholdBytes) return true;
  return payload.toUtf8().size() > kCatalogHelperStdinThresholdBytes;
}

inline QByteArray catalog_helper_argument_payload(const QStringList& arguments) {
  return QJsonDocument(QJsonObject{
      {QStringLiteral("schema"), QStringLiteral("photo-editor-catalog-arguments:v1")},
      {QStringLiteral("arguments"), QJsonArray::fromStringList(arguments)},
  }).toJson(QJsonDocument::Compact);
}

// The caller prepared and size-checked these exact stdin bytes away from the
// UI thread. This path only queues them to QProcess and closes stdin.
inline bool start_catalog_helper_stdin_payload(
    QProcess* process, const QString& program,
    const QStringList& launcher_arguments, const QByteArray& payload,
    QString* error) {
  if (process == nullptr || process->state() != QProcess::NotRunning) {
    if (error != nullptr) *error = QStringLiteral("Recipe save helper is unavailable or already running.");
    return false;
  }
  if (payload.isEmpty() || payload.size() > kMaxCatalogHelperPayloadBytes) {
    if (error != nullptr) *error = QStringLiteral("Recipe payload exceeds the 256 MiB safety limit or has invalid arguments.");
    return false;
  }
  process->setProperty("recipePayloadBytes", payload.size());
  process->start(program, launcher_arguments);
  const auto written = process->write(payload);
  process->closeWriteChannel();
  if (written != payload.size()) {
    if (error != nullptr) *error = QStringLiteral("Could not queue the complete recipe payload for saving.");
    return false;
  }
  return true;
}

// QProcess buffers writes while starting; EOF is delivered only after buffered
// bytes drain. No waitForStarted/waitForBytesWritten on the UI thread.
inline bool start_catalog_helper_stdin(
    QProcess* process, const QString& program, const QStringList& launcher_arguments,
    const QStringList& payload_arguments, QString* error) {
  if (process == nullptr || process->state() != QProcess::NotRunning) {
    if (error != nullptr) *error = QStringLiteral("Recipe save helper is unavailable or already running.");
    return false;
  }
  const auto payload = catalog_helper_argument_payload(payload_arguments);
  if (payload_arguments.size() > 32 ||
      payload.size() > kMaxCatalogHelperPayloadBytes) {
    if (error != nullptr) *error = QStringLiteral("Recipe payload exceeds the 256 MiB safety limit or has invalid arguments.");
    return false;
  }
  process->setProperty("recipePayloadBytes", payload.size());
  process->start(program, launcher_arguments);
  const auto written = process->write(payload);
  process->closeWriteChannel();
  if (written != payload.size()) {
    if (error != nullptr) *error = QStringLiteral("Could not queue the complete recipe payload for saving.");
    return false;
  }
  return true;
}

}  // namespace photo_editor::desktop
