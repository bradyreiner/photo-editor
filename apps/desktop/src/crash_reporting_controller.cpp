#include "crash_reporting_controller.h"
#include "customer_platform_config.h"
#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>
#include <QStringList>
#include <QUrl>

#if LOOKSY_HAS_SENTRY
#include <sentry.h>
#endif

namespace photo_editor::desktop {
namespace {
constexpr auto consent_key = "privacy/crashReportsEnabled";
QString databasePath() {
  return QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)).filePath(QStringLiteral("crash-reports"));
}
}
CrashReportingController::CrashReportingController(QObject* parent) : QObject(parent) {
  if (enabled()) start();
  else status_ = available() ? QStringLiteral("Crash reporting is off.") : QStringLiteral("Automatic crash reporting is not available in this version. You can share a support bundle.");
}
CrashReportingController::~CrashReportingController() { stop(); }
bool CrashReportingController::available() const {
  const QUrl dsn(QString::fromUtf8(LOOKSY_SENTRY_DSN));
  return LOOKSY_HAS_SENTRY && dsn.isValid() && dsn.scheme() == QStringLiteral("https") && !dsn.host().isEmpty();
}
bool CrashReportingController::enabled() const { return available() && QSettings().value(consent_key, false).toBool(); }
bool CrashReportingController::start() {
#if LOOKSY_HAS_SENTRY
  if (started_) return true;
  if (!enabled()) return false;
#ifdef Q_OS_WIN
  const auto handler = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("crashpad_handler.exe"));
#else
  const auto handler = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("crashpad_handler"));
#endif
  if (!QFileInfo::exists(handler)) { status_ = QStringLiteral("Crash reporting could not start. The crash handler is missing."); return false; }
  const QFileInfo queue(databasePath());
  if (queue.isSymLink()) { status_ = QStringLiteral("Crash reporting could not start. Its storage location is not a dedicated folder."); return false; }
  qint64 bytes = 0;
  QDirIterator reports(databasePath(), QDir::Files | QDir::NoSymLinks, QDirIterator::Subdirectories);
  while (reports.hasNext()) { reports.next(); bytes += reports.fileInfo().size(); }
  // Enforce a startup ceiling on Crashpad dumps as well as SDK envelope caches.
  if (bytes > 50 * 1024 * 1024 && !QDir(databasePath()).removeRecursively()) {
    status_ = QStringLiteral("Crash reporting could not start. Its report queue is full."); return false;
  }
  QDir().mkpath(databasePath());
  auto* options = sentry_options_new();
  sentry_options_set_dsn(options, LOOKSY_SENTRY_DSN);
  sentry_options_set_release(options, "looksy@" LOOKSY_APP_VERSION);
  sentry_options_set_environment(options, LOOKSY_RELEASE_CHANNEL);
  sentry_options_set_database_path(options, databasePath().toUtf8().constData());
  sentry_options_set_handler_path(options, handler.toUtf8().constData());
  sentry_options_set_require_user_consent(options, 1);
  sentry_options_set_max_breadcrumbs(options, 30);
  sentry_options_set_cache_max_items(options, 20);
  sentry_options_set_cache_max_size(options, 20 * 1024 * 1024);
  sentry_options_set_cache_max_age(options, 7 * 24 * 60 * 60);
  sentry_options_set_shutdown_timeout(options, 1500);
  sentry_options_set_auto_session_tracking(options, 1);
  // Do not attach text logs, catalogs, photos, request bodies, paths or license keys.
  if (sentry_init(options) != 0) { status_ = QStringLiteral("Crash reporting could not start."); return false; }
  started_ = true;
  sentry_user_consent_give();
  setCustomerId(customer_id_);
  status_ = QStringLiteral("Crash reporting is on. Reports include technical details and may include process memory.");
  return true;
#else
  return false;
#endif
}
void CrashReportingController::stop() {
#if LOOKSY_HAS_SENTRY
  if (started_) {
    // Revoke first so shutdown cannot flush a newly disallowed upload.
    if (!enabled()) sentry_user_consent_revoke();
    sentry_close(); started_ = false;
  }
#endif
}
void CrashReportingController::setEnabled(bool value) {
  if (!available()) return;
  QSettings().setValue(consent_key, value);
  if (value) {
    if (!start()) QSettings().setValue(consent_key, false);
  } else {
    stop();
    // This dedicated SDK directory contains only queued reports, never user work.
    const QFileInfo directory(databasePath());
    const bool removed = !directory.exists() ||
        (!directory.isSymLink() && QDir(databasePath()).removeRecursively());
    status_ = removed ? QStringLiteral("Crash reporting is off. Queued reports have been removed.")
                      : QStringLiteral("Crash reporting is off. Some queued reports could not be removed.");
  }
  emit changed();
}
void CrashReportingController::setCustomerId(const QString& customer_id) {
  customer_id_ = customer_id;
#if LOOKSY_HAS_SENTRY
  if (!started_) return;
  if (customer_id.isEmpty()) sentry_remove_user();
  else {
    auto user = sentry_value_new_object();
    sentry_value_set_by_key(user, "id", sentry_value_new_string(customer_id.toUtf8().constData()));
    sentry_set_user(user);
  }
#endif
}
void CrashReportingController::recordEvent(const QString& event_code) {
  static const QStringList allowed{QStringLiteral("app.ready"), QStringLiteral("agent.started"),
      QStringLiteral("agent.finished"), QStringLiteral("export.failed"), QStringLiteral("catalog.failed")};
  if (!started_ || !allowed.contains(event_code)) return;
#if LOOKSY_HAS_SENTRY
  auto breadcrumb = sentry_value_new_breadcrumb("default", event_code.toUtf8().constData());
  sentry_value_set_by_key(breadcrumb, "category", sentry_value_new_string("looksy.workflow"));
  sentry_add_breadcrumb(breadcrumb);
#endif
}
}  // namespace photo_editor::desktop
