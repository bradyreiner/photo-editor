#pragma once

#include <QObject>
#include <QStringList>

namespace photo_editor::desktop {

class AppShellController final : public QObject {
  Q_OBJECT
  Q_PROPERTY(int startupWorkspace READ startupWorkspace WRITE setStartupWorkspace NOTIFY preferencesChanged)
  Q_PROPERTY(bool reopenLastWorkspace READ reopenLastWorkspace WRITE setReopenLastWorkspace NOTIFY preferencesChanged)
  Q_PROPERTY(bool defaultTimelineSnapping READ defaultTimelineSnapping WRITE setDefaultTimelineSnapping NOTIFY preferencesChanged)
  Q_PROPERTY(bool defaultTimelineRipple READ defaultTimelineRipple WRITE setDefaultTimelineRipple NOTIFY preferencesChanged)
  Q_PROPERTY(int preferredStartupWorkspace READ preferredStartupWorkspace NOTIFY preferencesChanged)
  Q_PROPERTY(int currentWorkspace READ currentWorkspace WRITE setCurrentWorkspace NOTIFY currentWorkspaceChanged)
  Q_PROPERTY(QStringList workspaceNames READ workspaceNames CONSTANT)
  Q_PROPERTY(QString appDataLocation READ appDataLocation CONSTANT)
  Q_PROPERTY(QString cacheLocation READ cacheLocation CONSTANT)
  Q_PROPERTY(QString logLocation READ logLocation CONSTANT)
  Q_PROPERTY(QString backupLocation READ backupLocation CONSTANT)

 public:
  explicit AppShellController(QObject* parent = nullptr);

  [[nodiscard]] int startupWorkspace() const;
  [[nodiscard]] bool reopenLastWorkspace() const;
  [[nodiscard]] bool defaultTimelineSnapping() const;
  [[nodiscard]] bool defaultTimelineRipple() const;
  [[nodiscard]] int preferredStartupWorkspace() const;
  [[nodiscard]] int currentWorkspace() const;
  [[nodiscard]] QStringList workspaceNames() const;
  [[nodiscard]] QString appDataLocation() const;
  [[nodiscard]] QString cacheLocation() const;
  [[nodiscard]] QString logLocation() const;
  [[nodiscard]] QString backupLocation() const;

  void setStartupWorkspace(int workspace_index);
  void setReopenLastWorkspace(bool reopen_last_workspace);
  void setDefaultTimelineSnapping(bool enabled);
  void setDefaultTimelineRipple(bool enabled);
  void setCurrentWorkspace(int workspace_index);

  Q_INVOKABLE bool revealPath(const QString& path) const;
  Q_INVOKABLE bool openAppDataFolder() const;
  Q_INVOKABLE bool openCacheFolder() const;
  Q_INVOKABLE bool openLogFolder() const;
  Q_INVOKABLE bool openBackupFolder() const;
  Q_INVOKABLE void resetTimelineWorkspaceSettings();
  Q_INVOKABLE void restoreDefaults();

 signals:
  void preferencesChanged();
  void currentWorkspaceChanged();
  void timelineDefaultsChanged();
  void timelineWorkspaceSettingsReset();

 private:
  [[nodiscard]] int sanitizedWorkspaceIndex(int workspace_index) const;
  void writeSetting(const QString& key, const QVariant& value);

  int current_workspace_ = 0;
};

}  // namespace photo_editor::desktop
