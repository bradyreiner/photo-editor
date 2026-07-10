#include "app_shell_controller.h"

#include <QDesktopServices>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSysInfo>
#include <QSettings>
#include <QUrl>
#include <QVariant>

#include "app_bootstrap.h"

namespace photo_editor::desktop {

namespace {

#ifndef PHOTO_EDITOR_APP_VERSION
#define PHOTO_EDITOR_APP_VERSION "0.0.0"
#endif

#ifndef VIREL_PRODUCT_NAME
#define VIREL_PRODUCT_NAME "Virel Studio"
#endif

#ifndef VIREL_SUPPORT_URL
#define VIREL_SUPPORT_URL "https://virel.studio/support"
#endif

#ifndef VIREL_PRIVACY_URL
#define VIREL_PRIVACY_URL "https://virel.studio/privacy"
#endif

#ifndef VIREL_TERMS_URL
#define VIREL_TERMS_URL "https://virel.studio/terms"
#endif

constexpr int kDefaultStartupWorkspace = 0;
constexpr bool kDefaultReopenLastWorkspace = false;
constexpr bool kDefaultTimelineSnapping = true;
constexpr bool kDefaultTimelineRipple = true;
constexpr bool kDefaultCullingAutoAdvance = true;
constexpr bool kDefaultDarkMode = true;
constexpr int kWorkspaceCount = 5;

const auto kStartupWorkspaceKey = "app/startupWorkspace";
const auto kReopenLastWorkspaceKey = "app/reopenLastWorkspace";
const auto kLastWorkspaceKey = "app/lastWorkspace";
const auto kDefaultTimelineSnappingKey = "timeline/defaultSnapping";
const auto kDefaultTimelineRippleKey = "timeline/defaultRipple";
const auto kCullingAutoAdvanceKey = "library/cullingAutoAdvance";
const auto kDarkModeKey = "app/darkMode";

}  // namespace

AppShellController::AppShellController(QObject* parent) : QObject(parent) {
  current_workspace_ = preferredStartupWorkspace();
  checkForUncleanExit();
}

int AppShellController::startupWorkspace() const {
  const QSettings settings;
  return sanitizedWorkspaceIndex(settings.value(kStartupWorkspaceKey, kDefaultStartupWorkspace).toInt());
}

bool AppShellController::reopenLastWorkspace() const {
  const QSettings settings;
  return settings.value(kReopenLastWorkspaceKey, kDefaultReopenLastWorkspace).toBool();
}

bool AppShellController::defaultTimelineSnapping() const {
  const QSettings settings;
  return settings.value(kDefaultTimelineSnappingKey, kDefaultTimelineSnapping).toBool();
}

bool AppShellController::defaultTimelineRipple() const {
  const QSettings settings;
  return settings.value(kDefaultTimelineRippleKey, kDefaultTimelineRipple).toBool();
}

bool AppShellController::cullingAutoAdvance() const {
  const QSettings settings;
  return settings.value(kCullingAutoAdvanceKey, kDefaultCullingAutoAdvance).toBool();
}

bool AppShellController::darkMode() const {
  const QSettings settings;
  return settings.value(kDarkModeKey, kDefaultDarkMode).toBool();
}

int AppShellController::preferredStartupWorkspace() const {
  if (!reopenLastWorkspace()) {
    return startupWorkspace();
  }

  const QSettings settings;
  return sanitizedWorkspaceIndex(settings.value(kLastWorkspaceKey, startupWorkspace()).toInt());
}

int AppShellController::currentWorkspace() const { return current_workspace_; }

QStringList AppShellController::workspaceNames() const {
  return {
      QStringLiteral("Library"),
      QStringLiteral("Edit"),
      QStringLiteral("Carousel"),
      QStringLiteral("Timeline"),
      QStringLiteral("Export"),
  };
}

QString AppShellController::appDataLocation() const { return AppBootstrap::appDataLocation(); }
QString AppShellController::cacheLocation() const { return AppBootstrap::cacheLocation(); }
QString AppShellController::logLocation() const { return AppBootstrap::logLocation(); }
QString AppShellController::backupLocation() const { return AppBootstrap::backupLocation(); }
QString AppShellController::appVersion() const { return QStringLiteral(PHOTO_EDITOR_APP_VERSION); }
QString AppShellController::productName() const { return QStringLiteral(VIREL_PRODUCT_NAME); }
QString AppShellController::supportUrl() const { return QStringLiteral(VIREL_SUPPORT_URL); }
QString AppShellController::privacyUrl() const { return QStringLiteral(VIREL_PRIVACY_URL); }
QString AppShellController::termsUrl() const { return QStringLiteral(VIREL_TERMS_URL); }

bool AppShellController::localDiagnosticsEnabled() const {
#if defined(PHOTO_EDITOR_ENABLE_LOCAL_DIAGNOSTICS) && PHOTO_EDITOR_ENABLE_LOCAL_DIAGNOSTICS
  return true;
#else
  return false;
#endif
}

void AppShellController::setStartupWorkspace(int workspace_index) {
  const auto sanitized = sanitizedWorkspaceIndex(workspace_index);
  if (startupWorkspace() == sanitized) {
    return;
  }

  writeSetting(QString::fromUtf8(kStartupWorkspaceKey), sanitized);
  emit preferencesChanged();
}

void AppShellController::setReopenLastWorkspace(bool reopen_last_workspace) {
  if (reopenLastWorkspace() == reopen_last_workspace) {
    return;
  }

  writeSetting(QString::fromUtf8(kReopenLastWorkspaceKey), reopen_last_workspace);
  emit preferencesChanged();
}

void AppShellController::setDefaultTimelineSnapping(bool enabled) {
  if (defaultTimelineSnapping() == enabled) {
    return;
  }

  writeSetting(QString::fromUtf8(kDefaultTimelineSnappingKey), enabled);
  emit preferencesChanged();
  emit timelineDefaultsChanged();
}

void AppShellController::setDefaultTimelineRipple(bool enabled) {
  if (defaultTimelineRipple() == enabled) {
    return;
  }

  writeSetting(QString::fromUtf8(kDefaultTimelineRippleKey), enabled);
  emit preferencesChanged();
  emit timelineDefaultsChanged();
}

void AppShellController::setCullingAutoAdvance(bool enabled) {
  if (cullingAutoAdvance() == enabled) {
    return;
  }

  writeSetting(QString::fromUtf8(kCullingAutoAdvanceKey), enabled);
  emit preferencesChanged();
}

void AppShellController::setDarkMode(bool dark_mode) {
  if (darkMode() == dark_mode) {
    return;
  }

  writeSetting(QString::fromUtf8(kDarkModeKey), dark_mode);
  emit preferencesChanged();
}

void AppShellController::setCurrentWorkspace(int workspace_index) {
  const auto sanitized = sanitizedWorkspaceIndex(workspace_index);
  if (current_workspace_ == sanitized) {
    return;
  }

  current_workspace_ = sanitized;
  writeSetting(QString::fromUtf8(kLastWorkspaceKey), current_workspace_);
  emit currentWorkspaceChanged();
}

bool AppShellController::revealPath(const QString& path) const {
  if (path.trimmed().isEmpty()) {
    return false;
  }

  return QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

bool AppShellController::openAppDataFolder() const { return revealPath(appDataLocation()); }
bool AppShellController::openCacheFolder() const { return revealPath(cacheLocation()); }
bool AppShellController::openLogFolder() const { return revealPath(logLocation()); }
bool AppShellController::openBackupFolder() const { return revealPath(backupLocation()); }
bool AppShellController::openSupportPage() const { return QDesktopServices::openUrl(QUrl(supportUrl())); }
bool AppShellController::openPrivacyPage() const { return QDesktopServices::openUrl(QUrl(privacyUrl())); }
bool AppShellController::openTermsPage() const { return QDesktopServices::openUrl(QUrl(termsUrl())); }

QString AppShellController::createSupportBundle() const {
  const auto timestamp = QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMddTHHmmssZ"));
  const auto bundle_path =
      QDir(backupLocation()).filePath(QStringLiteral("support/VirelSupport-%1").arg(timestamp));
  if (!QDir().mkpath(bundle_path)) {
    return {};
  }

  QJsonObject manifest{
      {QStringLiteral("product"), productName()},
      {QStringLiteral("version"), appVersion()},
      {QStringLiteral("createdAt"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
      {QStringLiteral("os"), QSysInfo::prettyProductName()},
      {QStringLiteral("kernelType"), QSysInfo::kernelType()},
      {QStringLiteral("kernelVersion"), QSysInfo::kernelVersion()},
      {QStringLiteral("cpuArchitecture"), QSysInfo::currentCpuArchitecture()},
      {QStringLiteral("localDiagnosticsOnly"), true},
      {QStringLiteral("containsPhotos"), false},
      {QStringLiteral("privacyNotice"),
       QStringLiteral("Review the included logs before sharing. They may contain local file paths but never include photo bytes.")},
  };
  QFile manifest_file(QDir(bundle_path).filePath(QStringLiteral("support-manifest.json")));
  if (!manifest_file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    QDir(bundle_path).removeRecursively();
    return {};
  }
  manifest_file.write(QJsonDocument(manifest).toJson(QJsonDocument::Indented));
  manifest_file.close();

  const QDir runs_root(QDir(logLocation()).filePath(QStringLiteral("runs")));
  const auto runs = runs_root.entryInfoList(
      QDir::Dirs | QDir::NoDotAndDotDot,
      QDir::Time);
  const auto included_run_count = qMin(3, runs.size());
  for (int run_index = 0; run_index < included_run_count; ++run_index) {
    const auto& run = runs.at(run_index);
    const auto destination = QDir(bundle_path).filePath(QStringLiteral("logs/%1").arg(run.fileName()));
    QDir().mkpath(destination);
    const QDir source(run.absoluteFilePath());
    const auto files = source.entryInfoList(QDir::Files | QDir::Readable, QDir::Name);
    for (const auto& file : files) {
      if (file.size() <= 10 * 1024 * 1024) {
        QFile::copy(file.absoluteFilePath(), QDir(destination).filePath(file.fileName()));
      }
    }
  }

  revealPath(bundle_path);
  return bundle_path;
}

void AppShellController::resetTimelineWorkspaceSettings() {
  QSettings settings;
  settings.beginGroup(QStringLiteral("TimelineWorkspace"));
  settings.remove(QString());
  settings.endGroup();
  settings.sync();
  emit timelineWorkspaceSettingsReset();
}

void AppShellController::restoreDefaults() {
  QSettings settings;
  settings.remove(QString::fromUtf8(kStartupWorkspaceKey));
  settings.remove(QString::fromUtf8(kReopenLastWorkspaceKey));
  settings.remove(QString::fromUtf8(kLastWorkspaceKey));
  settings.remove(QString::fromUtf8(kDefaultTimelineSnappingKey));
  settings.remove(QString::fromUtf8(kDefaultTimelineRippleKey));
  settings.remove(QString::fromUtf8(kCullingAutoAdvanceKey));
  settings.sync();

  const auto preferred_workspace = preferredStartupWorkspace();
  if (current_workspace_ != preferred_workspace) {
    current_workspace_ = preferred_workspace;
    emit currentWorkspaceChanged();
  }

  emit preferencesChanged();
  emit timelineDefaultsChanged();
  resetTimelineWorkspaceSettings();
}

int AppShellController::sanitizedWorkspaceIndex(int workspace_index) const {
  if (workspace_index < 0 || workspace_index >= kWorkspaceCount) {
    return kDefaultStartupWorkspace;
  }

  return workspace_index;
}

void AppShellController::writeSetting(const QString& key, const QVariant& value) {
  QSettings settings;
  settings.setValue(key, value);
  settings.sync();
}

QString AppShellController::sessionStatePath() const {
  return QDir(AppBootstrap::appDataLocation()).filePath(QStringLiteral("session.json"));
}

void AppShellController::checkForUncleanExit() {
  const auto path = sessionStatePath();
  QFile file(path);
  if (!file.exists()) {
    return;
  }

  if (!file.open(QIODevice::ReadOnly)) {
    file.remove();
    return;
  }

  const auto document = QJsonDocument::fromJson(file.readAll());
  file.close();
  if (document.isNull() || !document.isObject()) {
    QFile::remove(path);
    return;
  }

  const auto root = document.object();
  if (root.value(QStringLiteral("cleanExit")).toBool(false)) {
    QFile::remove(path);
    return;
  }

  recovered_workspace_ = sanitizedWorkspaceIndex(root.value(QStringLiteral("workspace")).toInt(0));
  recovered_asset_id_ = root.value(QStringLiteral("assetId")).toString();
  recovered_timeline_path_ = root.value(QStringLiteral("timelinePath")).toString();
  session_recovery_available_ = true;
  QFile::remove(path);
}

bool AppShellController::sessionRecoveryAvailable() const { return session_recovery_available_; }

void AppShellController::updateSessionState(const QString& active_asset_id, const QString& timeline_path) {
  const auto path = sessionStatePath();
  QJsonObject root;
  root.insert(QStringLiteral("cleanExit"), false);
  root.insert(QStringLiteral("workspace"), current_workspace_);
  root.insert(QStringLiteral("assetId"), active_asset_id);
  root.insert(QStringLiteral("timelinePath"), timeline_path);
  QFile file(path);
  if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
  }
}

void AppShellController::markCleanExit() {
  const auto path = sessionStatePath();
  QJsonObject root;
  root.insert(QStringLiteral("cleanExit"), true);
  QFile file(path);
  if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
  }
}

void AppShellController::dismissSessionRecovery() {
  session_recovery_available_ = false;
  emit sessionRecoveryChanged();
}

int AppShellController::recoveredWorkspace() const { return recovered_workspace_; }
QString AppShellController::recoveredAssetId() const { return recovered_asset_id_; }
QString AppShellController::recoveredTimelinePath() const { return recovered_timeline_path_; }

}  // namespace photo_editor::desktop
