#pragma once

#include <QObject>
#include <QStringList>
#include <QTimer>

namespace photo_editor::desktop {

class AppShellController final : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool sessionRecoveryAvailable READ sessionRecoveryAvailable NOTIFY sessionRecoveryChanged)
  Q_PROPERTY(int startupWorkspace READ startupWorkspace WRITE setStartupWorkspace NOTIFY preferencesChanged)
  Q_PROPERTY(bool reopenLastWorkspace READ reopenLastWorkspace WRITE setReopenLastWorkspace NOTIFY preferencesChanged)
  Q_PROPERTY(bool defaultTimelineSnapping READ defaultTimelineSnapping WRITE setDefaultTimelineSnapping NOTIFY preferencesChanged)
  Q_PROPERTY(bool defaultTimelineRipple READ defaultTimelineRipple WRITE setDefaultTimelineRipple NOTIFY preferencesChanged)
  Q_PROPERTY(bool cullingAutoAdvance READ cullingAutoAdvance WRITE setCullingAutoAdvance NOTIFY preferencesChanged)
  Q_PROPERTY(bool darkMode READ darkMode WRITE setDarkMode NOTIFY preferencesChanged)
  Q_PROPERTY(int preferredStartupWorkspace READ preferredStartupWorkspace NOTIFY preferencesChanged)
  Q_PROPERTY(int currentWorkspace READ currentWorkspace WRITE setCurrentWorkspace NOTIFY currentWorkspaceChanged)
  Q_PROPERTY(QStringList workspaceNames READ workspaceNames CONSTANT)
  Q_PROPERTY(QString appDataLocation READ appDataLocation CONSTANT)
  Q_PROPERTY(QString cacheLocation READ cacheLocation CONSTANT)
  Q_PROPERTY(QString logLocation READ logLocation CONSTANT)
  Q_PROPERTY(QString backupLocation READ backupLocation CONSTANT)
  Q_PROPERTY(QString appVersion READ appVersion CONSTANT)

 public:
  explicit AppShellController(QObject* parent = nullptr);

  [[nodiscard]] int startupWorkspace() const;
  [[nodiscard]] bool reopenLastWorkspace() const;
  [[nodiscard]] bool defaultTimelineSnapping() const;
  [[nodiscard]] bool defaultTimelineRipple() const;
  [[nodiscard]] bool cullingAutoAdvance() const;
  [[nodiscard]] bool darkMode() const;
  [[nodiscard]] int preferredStartupWorkspace() const;
  [[nodiscard]] int currentWorkspace() const;
  [[nodiscard]] QStringList workspaceNames() const;
  [[nodiscard]] QString appDataLocation() const;
  [[nodiscard]] QString cacheLocation() const;
  [[nodiscard]] QString logLocation() const;
  [[nodiscard]] QString backupLocation() const;
  [[nodiscard]] QString appVersion() const;

  void setStartupWorkspace(int workspace_index);
  void setReopenLastWorkspace(bool reopen_last_workspace);
  void setDefaultTimelineSnapping(bool enabled);
  void setDefaultTimelineRipple(bool enabled);
  void setCullingAutoAdvance(bool enabled);
  void setDarkMode(bool dark_mode);
  void setCurrentWorkspace(int workspace_index);

  Q_INVOKABLE bool revealPath(const QString& path) const;
  Q_INVOKABLE bool openAppDataFolder() const;
  Q_INVOKABLE bool openCacheFolder() const;
  Q_INVOKABLE bool openLogFolder() const;
  Q_INVOKABLE bool openBackupFolder() const;
  Q_INVOKABLE void resetTimelineWorkspaceSettings();
  Q_INVOKABLE void restoreDefaults();

  [[nodiscard]] bool sessionRecoveryAvailable() const;
  Q_INVOKABLE void updateSessionState(const QString& active_asset_id, const QString& timeline_path);
  Q_INVOKABLE void markCleanExit();
  Q_INVOKABLE void dismissSessionRecovery();
  Q_INVOKABLE int recoveredWorkspace() const;
  Q_INVOKABLE QString recoveredAssetId() const;
  Q_INVOKABLE QString recoveredTimelinePath() const;

 signals:
  void preferencesChanged();
  void currentWorkspaceChanged();
  void timelineDefaultsChanged();
  void timelineWorkspaceSettingsReset();
  void sessionRecoveryChanged();

 private:
  [[nodiscard]] int sanitizedWorkspaceIndex(int workspace_index) const;
  void writeSetting(const QString& key, const QVariant& value);
  [[nodiscard]] QString sessionStatePath() const;
  void checkForUncleanExit();

  int current_workspace_ = 0;
  bool session_recovery_available_ = false;
  int recovered_workspace_ = 0;
  QString recovered_asset_id_;
  QString recovered_timeline_path_;
};

}  // namespace photo_editor::desktop
