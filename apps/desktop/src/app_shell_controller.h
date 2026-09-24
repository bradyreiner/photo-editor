#pragma once

#include <QObject>
#include <QFontDatabase>
#include <QProcess>
#include <QStringList>
#include <QTimer>
#include <QVariantMap>

namespace photo_editor::desktop {

class AppShellController final : public QObject {
  Q_OBJECT
  Q_PROPERTY(QString systemUiFontFamily READ systemUiFontFamily CONSTANT)
  Q_PROPERTY(bool sessionRecoveryAvailable READ sessionRecoveryAvailable NOTIFY sessionRecoveryChanged)
  Q_PROPERTY(QString sessionRecoverySummary READ sessionRecoverySummary NOTIFY sessionRecoveryChanged)
  Q_PROPERTY(QStringList sessionRecoveryDetails READ sessionRecoveryDetails NOTIFY sessionRecoveryChanged)
  Q_PROPERTY(bool firstRunExperiencePending READ firstRunExperiencePending NOTIFY firstRunExperienceChanged)
  Q_PROPERTY(int startupWorkspace READ startupWorkspace WRITE setStartupWorkspace NOTIFY preferencesChanged)
  Q_PROPERTY(bool reopenLastWorkspace READ reopenLastWorkspace WRITE setReopenLastWorkspace NOTIFY preferencesChanged)
  Q_PROPERTY(bool defaultTimelineSnapping READ defaultTimelineSnapping WRITE setDefaultTimelineSnapping NOTIFY preferencesChanged)
  Q_PROPERTY(bool defaultTimelineRipple READ defaultTimelineRipple WRITE setDefaultTimelineRipple NOTIFY preferencesChanged)
  Q_PROPERTY(bool cullingAutoAdvance READ cullingAutoAdvance WRITE setCullingAutoAdvance NOTIFY preferencesChanged)
  Q_PROPERTY(bool darkMode READ darkMode WRITE setDarkMode NOTIFY preferencesChanged)
  Q_PROPERTY(double interfaceScale READ interfaceScale WRITE setInterfaceScale NOTIFY preferencesChanged)
  Q_PROPERTY(bool reducedMotion READ reducedMotion WRITE setReducedMotion NOTIFY preferencesChanged)
  Q_PROPERTY(bool highContrast READ highContrast WRITE setHighContrast NOTIFY preferencesChanged)
  Q_PROPERTY(QString colorVisionMode READ colorVisionMode WRITE setColorVisionMode NOTIFY preferencesChanged)
  Q_PROPERTY(QString interfaceDensity READ interfaceDensity WRITE setInterfaceDensity NOTIFY preferencesChanged)
  Q_PROPERTY(QString importWorkflowProfile READ importWorkflowProfile WRITE setImportWorkflowProfile NOTIFY preferencesChanged)
  Q_PROPERTY(QStringList importPresetNames READ importPresetNames NOTIFY preferencesChanged)
  Q_PROPERTY(int preferredStartupWorkspace READ preferredStartupWorkspace NOTIFY preferencesChanged)
  Q_PROPERTY(int currentWorkspace READ currentWorkspace WRITE setCurrentWorkspace NOTIFY currentWorkspaceChanged)
  Q_PROPERTY(QStringList workspaceNames READ workspaceNames CONSTANT)
  Q_PROPERTY(QString appDataLocation READ appDataLocation CONSTANT)
  Q_PROPERTY(QString photoLibraryLocation READ photoLibraryLocation NOTIFY storageLocationsChanged)
  Q_PROPERTY(QString originalsBackupLocation READ originalsBackupLocation NOTIFY storageLocationsChanged)
  Q_PROPERTY(bool originalsBackupEnabled READ originalsBackupEnabled NOTIFY storageLocationsChanged)
  Q_PROPERTY(QString cacheLocation READ cacheLocation CONSTANT)
  Q_PROPERTY(QString logLocation READ logLocation CONSTANT)
  Q_PROPERTY(QString backupLocation READ backupLocation NOTIFY storageLocationsChanged)
  Q_PROPERTY(QString storageConfigurationStatus READ storageConfigurationStatus NOTIFY storageLocationsChanged)
  Q_PROPERTY(bool photoLibraryMigrationRunning READ photoLibraryMigrationRunning NOTIFY storageLocationsChanged)
  Q_PROPERTY(QString appVersion READ appVersion CONSTANT)
  Q_PROPERTY(QString productName READ productName CONSTANT)
  Q_PROPERTY(QString supportUrl READ supportUrl CONSTANT)
  Q_PROPERTY(QString privacyUrl READ privacyUrl CONSTANT)
  Q_PROPERTY(QString termsUrl READ termsUrl CONSTANT)
  Q_PROPERTY(bool localDiagnosticsEnabled READ localDiagnosticsEnabled CONSTANT)
  Q_PROPERTY(QString catalogRecoveryStatus READ catalogRecoveryStatus NOTIFY catalogRecoveryChanged)
  Q_PROPERTY(bool catalogRepairConfirmationPending READ catalogRepairConfirmationPending NOTIFY catalogRecoveryChanged)

 public:
  [[nodiscard]] QString systemUiFontFamily() const {
    return QFontDatabase::systemFont(QFontDatabase::GeneralFont).family();
  }
  explicit AppShellController(QObject* parent = nullptr);

  [[nodiscard]] int startupWorkspace() const;
  [[nodiscard]] bool reopenLastWorkspace() const;
  [[nodiscard]] bool defaultTimelineSnapping() const;
  [[nodiscard]] bool defaultTimelineRipple() const;
  [[nodiscard]] bool cullingAutoAdvance() const;
  [[nodiscard]] bool darkMode() const;
  [[nodiscard]] double interfaceScale() const;
  [[nodiscard]] bool reducedMotion() const;
  [[nodiscard]] bool highContrast() const;
  [[nodiscard]] QString colorVisionMode() const;
  [[nodiscard]] QString interfaceDensity() const;
  [[nodiscard]] QString importWorkflowProfile() const;
  [[nodiscard]] QStringList importPresetNames() const;
  [[nodiscard]] int preferredStartupWorkspace() const;
  [[nodiscard]] int currentWorkspace() const;
  [[nodiscard]] QStringList workspaceNames() const;
  [[nodiscard]] QString appDataLocation() const;
  [[nodiscard]] QString photoLibraryLocation() const;
  [[nodiscard]] QString originalsBackupLocation() const;
  [[nodiscard]] bool originalsBackupEnabled() const;
  [[nodiscard]] QString cacheLocation() const;
  [[nodiscard]] QString logLocation() const;
  [[nodiscard]] QString backupLocation() const;
  [[nodiscard]] QString storageConfigurationStatus() const;
  [[nodiscard]] bool photoLibraryMigrationRunning() const;
  [[nodiscard]] QString appVersion() const;
  [[nodiscard]] QString productName() const;
  [[nodiscard]] QString supportUrl() const;
  [[nodiscard]] QString privacyUrl() const;
  [[nodiscard]] QString termsUrl() const;
  [[nodiscard]] bool localDiagnosticsEnabled() const;

  void setStartupWorkspace(int workspace_index);
  void setReopenLastWorkspace(bool reopen_last_workspace);
  void setDefaultTimelineSnapping(bool enabled);
  void setDefaultTimelineRipple(bool enabled);
  void setCullingAutoAdvance(bool enabled);
  void setDarkMode(bool dark_mode);
  void setInterfaceScale(double scale);
  void setReducedMotion(bool reduced_motion);
  void setHighContrast(bool high_contrast);
  void setColorVisionMode(const QString& mode);
  void setInterfaceDensity(const QString& density);
  void setImportWorkflowProfile(const QString& profile);
  void setCurrentWorkspace(int workspace_index);

  Q_INVOKABLE bool revealPath(const QString& path) const;
  Q_INVOKABLE bool openAppDataFolder() const;
  Q_INVOKABLE bool openCacheFolder() const;
  Q_INVOKABLE bool openLogFolder() const;
  Q_INVOKABLE bool openBackupFolder() const;
  Q_INVOKABLE bool openPhotoLibraryFolder() const;
  Q_INVOKABLE bool openOriginalsBackupFolder() const;
  Q_INVOKABLE bool setPhotoLibraryLocation(const QString& path_or_url);
  Q_INVOKABLE bool setOriginalsBackupLocation(const QString& path_or_url);
  Q_INVOKABLE bool setCatalogBackupLocation(const QString& path_or_url);
  Q_INVOKABLE void disableOriginalsBackup();
  Q_INVOKABLE bool saveImportPreset(const QString& name, const QVariantMap& configuration);
  Q_INVOKABLE QVariantMap importPreset(const QString& name) const;
  Q_INVOKABLE void removeImportPreset(const QString& name);
  Q_INVOKABLE bool openSupportPage() const;
  Q_INVOKABLE bool openPrivacyPage() const;
  Q_INVOKABLE bool openTermsPage() const;
  Q_INVOKABLE QString createSupportBundle() const;
  [[nodiscard]] QString catalogRecoveryStatus() const;
  [[nodiscard]] bool catalogRepairConfirmationPending() const;
  Q_INVOKABLE bool createCatalogBackup();
  Q_INVOKABLE void requestCatalogRepair();
  Q_INVOKABLE bool confirmCatalogRepair();
  Q_INVOKABLE void cancelCatalogRepair();
  Q_INVOKABLE void resetTimelineWorkspaceSettings();
  Q_INVOKABLE bool setDisplayCalibration(
      const QString& display_identity,
      const QString& calibration_identity,
      double peak_luminance_nits,
      double black_luminance_nits,
      double reference_white_nits,
      double surround_luminance_nits);
  Q_INVOKABLE void clearDisplayCalibration();
  Q_INVOKABLE void restoreDefaults();

  [[nodiscard]] bool sessionRecoveryAvailable() const;
  [[nodiscard]] QString sessionRecoverySummary() const;
  [[nodiscard]] QStringList sessionRecoveryDetails() const;
  Q_INVOKABLE void updateSessionState(
      const QString& active_asset_id,
      const QString& timeline_path,
      const QString& active_project = {},
      const QString& adjustment_patch_json = {},
      const QString& edit_mode = {});
  Q_INVOKABLE void markCleanExit();
  Q_INVOKABLE void dismissSessionRecovery();
  Q_INVOKABLE int recoveredWorkspace() const;
  Q_INVOKABLE QString recoveredAssetId() const;
  Q_INVOKABLE QString recoveredTimelinePath() const;
  Q_INVOKABLE QString recoveredProject() const;
  Q_INVOKABLE QString recoveredAdjustmentPatchJson() const;
  Q_INVOKABLE QString recoveredEditMode() const;
  [[nodiscard]] bool firstRunExperiencePending() const;
  Q_INVOKABLE void completeFirstRunExperience();
  Q_INVOKABLE void restartFirstRunExperience();
  Q_INVOKABLE QString prepareSampleProject() const;
  Q_INVOKABLE QVariantMap shortcutOverrides() const;
  Q_INVOKABLE QString shortcutSequence(
      const QString& shortcut_id,
      const QString& default_sequence) const;
  Q_INVOKABLE bool setShortcutOverride(
      const QString& shortcut_id,
      const QString& sequence);
  Q_INVOKABLE bool setShortcutOverrides(const QVariantMap& overrides);
  Q_INVOKABLE void resetShortcutOverride(const QString& shortcut_id);

 signals:
  void preferencesChanged();
  void currentWorkspaceChanged();
  void timelineDefaultsChanged();
  void timelineWorkspaceSettingsReset();
  void sessionRecoveryChanged();
  void firstRunExperienceChanged();
  void catalogRecoveryChanged();
  void storageLocationsChanged();

 private:
  [[nodiscard]] int sanitizedWorkspaceIndex(int workspace_index) const;
  void writeSetting(const QString& key, const QVariant& value);
  [[nodiscard]] QString sessionStatePath() const;
  void checkForUncleanExit();
  void handlePhotoLibraryMigrationFinished(int exit_code, QProcess::ExitStatus exit_status);

  int current_workspace_ = 0;
  bool session_recovery_available_ = false;
  int recovered_workspace_ = 0;
  QString recovered_asset_id_;
  QString recovered_timeline_path_;
  QString recovered_project_;
  QString recovered_adjustment_patch_json_;
  QString recovered_edit_mode_;
  QString recovered_at_;
  QString catalog_recovery_status_;
  QString storage_configuration_status_;
  QString pending_photo_library_location_;
  QProcess photo_library_migration_process_;
  bool catalog_repair_confirmation_pending_ = false;
};

}  // namespace photo_editor::desktop
