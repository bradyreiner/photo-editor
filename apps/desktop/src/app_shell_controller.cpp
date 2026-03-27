#include "app_shell_controller.h"

#include <QDesktopServices>
#include <QSettings>
#include <QUrl>
#include <QVariant>

#include "app_bootstrap.h"

namespace photo_editor::desktop {

namespace {

constexpr int kDefaultStartupWorkspace = 0;
constexpr bool kDefaultReopenLastWorkspace = false;
constexpr bool kDefaultTimelineSnapping = true;
constexpr bool kDefaultTimelineRipple = true;
constexpr int kWorkspaceCount = 5;

const auto kStartupWorkspaceKey = "app/startupWorkspace";
const auto kReopenLastWorkspaceKey = "app/reopenLastWorkspace";
const auto kLastWorkspaceKey = "app/lastWorkspace";
const auto kDefaultTimelineSnappingKey = "timeline/defaultSnapping";
const auto kDefaultTimelineRippleKey = "timeline/defaultRipple";

}  // namespace

AppShellController::AppShellController(QObject* parent) : QObject(parent) {
  current_workspace_ = preferredStartupWorkspace();
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

}  // namespace photo_editor::desktop
