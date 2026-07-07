#pragma once

#include <QLoggingCategory>
#include <QString>
#include <QVariantMap>

Q_DECLARE_LOGGING_CATEGORY(photoEditorAppLog)
Q_DECLARE_LOGGING_CATEGORY(photoEditorCatalogLog)
Q_DECLARE_LOGGING_CATEGORY(photoEditorJobsLog)
Q_DECLARE_LOGGING_CATEGORY(photoEditorPreviewLog)
Q_DECLARE_LOGGING_CATEGORY(photoEditorPerfLog)

namespace photo_editor::desktop::observability {

inline constexpr auto kEnabledEnv = "PHOTO_EDITOR_ENABLE_DEV_OBSERVABILITY";
inline constexpr auto kSessionIdEnv = "PHOTO_EDITOR_SESSION_ID";
inline constexpr auto kLogLevelEnv = "PHOTO_EDITOR_LOG_LEVEL";
inline constexpr auto kLogColorEnv = "PHOTO_EDITOR_LOG_COLOR";
inline constexpr auto kConsoleModeEnv = "PHOTO_EDITOR_OBSERVABILITY_CONSOLE";
inline constexpr auto kRunDirectoryEnv = "PHOTO_EDITOR_OBSERVABILITY_RUN_DIR";

bool enabled();
void initialize();
QString sessionId();
QString logLevel();
QString runDirectory();
QString desktopLogFilePath();
QString colorizedConsoleLineForMessage(QtMsgType type, const QString& category, const QString& line);
QString compactConsoleLineForEvent(const QString& event_name, const QVariantMap& fields = {});
bool consoleModePrintsEvent(
    QtMsgType type,
    const QString& mode,
    const QString& event_name,
    const QVariantMap& fields = {});
QString structuredMessage(const QString& event_name, const QVariantMap& fields = {});
void logInfo(const QLoggingCategory& category, const QString& event_name, const QVariantMap& fields = {});
void logWarning(const QLoggingCategory& category, const QString& event_name, const QVariantMap& fields = {});

}  // namespace photo_editor::desktop::observability
