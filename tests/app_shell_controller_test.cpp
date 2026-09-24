#include <cstdlib>
#include <iostream>

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QSet>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>

#include "app_bootstrap.h"
#include "app_shell_controller.h"
#include "edit_adjustment_patch.h"
#include "preview_backend_controller.h"

namespace {

int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

void clear_settings() {
  QSettings settings;
  settings.clear();
  settings.sync();
}

}  // namespace

int main(int argc, char** argv) {
  QTemporaryDir runtime_root;
  QTemporaryDir settings_root;
  if (!runtime_root.isValid() || !settings_root.isValid()) {
    return fail("temporary runtime and settings roots should be created");
  }
  qputenv("PHOTO_EDITOR_APP_DATA_ROOT", runtime_root.path().toUtf8());
  QCoreApplication app(argc, argv);
  QStandardPaths::setTestModeEnabled(true);
  QSettings::setDefaultFormat(QSettings::IniFormat);
  QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings_root.path());
  QCoreApplication::setOrganizationName(
      QString::fromUtf8(photo_editor::desktop::AppBootstrap::organization_name));
  QCoreApplication::setApplicationName(
      QString::fromUtf8(photo_editor::desktop::AppBootstrap::application_name));
  clear_settings();

  // Seed the exact settings identity and keys written by the pre-Looksy app.
  // The public rename must consume them in place rather than creating a fresh
  // Looksy settings namespace.
  {
    QSettings legacy_settings;
    legacy_settings.setValue(QStringLiteral("app/startupWorkspace"), 3);
    legacy_settings.setValue(QStringLiteral("app/reopenLastWorkspace"), true);
    legacy_settings.setValue(QStringLiteral("app/lastWorkspace"), 4);
    legacy_settings.setValue(QStringLiteral("app/darkMode"), false);
    legacy_settings.setValue(QStringLiteral("app/firstRunExperienceVersion"), 1);
    legacy_settings.setValue(QStringLiteral("preview/backendPreference"), QStringLiteral("CPU"));
    legacy_settings.sync();
    if (legacy_settings.status() != QSettings::NoError) {
      return fail("legacy preference fixture should be writable");
    }
  }

  photo_editor::desktop::AppShellController legacy_controller;
  photo_editor::desktop::PreviewBackendController legacy_preview_backend;
  if (legacy_controller.productName() != QStringLiteral("Looksy") ||
      legacy_controller.startupWorkspace() != 3 ||
      !legacy_controller.reopenLastWorkspace() ||
      legacy_controller.preferredStartupWorkspace() != 4 ||
      legacy_controller.darkMode() ||
      legacy_controller.firstRunExperiencePending() ||
      legacy_preview_backend.preferredBackend() != QStringLiteral("CPU")) {
    return fail("Looksy should reopen legacy workspace, theme, first-run, and preview backend preferences in place");
  }
  clear_settings();

  photo_editor::desktop::AppShellController controller;
  if (controller.productName() != QStringLiteral("Looksy") ||
      controller.appVersion().isEmpty() ||
      !controller.supportUrl().startsWith(QStringLiteral("https://")) ||
      !controller.privacyUrl().startsWith(QStringLiteral("https://")) ||
      !controller.termsUrl().startsWith(QStringLiteral("https://"))) {
    return fail("production product and support metadata should be available to the shell");
  }
  if (controller.startupWorkspace() != 0) {
    return fail("default startup workspace should be Library");
  }
  if (controller.reopenLastWorkspace()) {
    return fail("reopen last workspace should default to false");
  }
  if (!controller.defaultTimelineSnapping() || !controller.defaultTimelineRipple()) {
    return fail("timeline defaults should start enabled");
  }
  if (!controller.cullingAutoAdvance()) {
    return fail("culling auto-advance should default to enabled");
  }
  if (!controller.firstRunExperiencePending()) {
    return fail("a clean profile should open the first-run experience");
  }

  const auto photo_library_path = QDir(runtime_root.path()).filePath(QStringLiteral("user-photo-library"));
  const auto originals_backup_path = QDir(runtime_root.path()).filePath(QStringLiteral("separate-originals-backup"));
  const auto catalog_backup_path = QDir(runtime_root.path()).filePath(QStringLiteral("separate-catalog-backups"));
  if (!controller.setPhotoLibraryLocation(photo_library_path) ||
      controller.setOriginalsBackupLocation(photo_library_path) ||
      !controller.storageConfigurationStatus().contains(QStringLiteral("separate")) ||
      !controller.setOriginalsBackupLocation(originals_backup_path) ||
      !controller.setCatalogBackupLocation(catalog_backup_path)) {
    return fail("storage settings should persist writable distinct photo, originals-backup, and catalog-backup folders");
  }
  photo_editor::desktop::AppShellController storage_relaunch_controller;
  if (QDir::cleanPath(storage_relaunch_controller.photoLibraryLocation()) != QDir::cleanPath(photo_library_path) ||
      QDir::cleanPath(storage_relaunch_controller.originalsBackupLocation()) != QDir::cleanPath(originals_backup_path) ||
      QDir::cleanPath(storage_relaunch_controller.backupLocation()) != QDir::cleanPath(catalog_backup_path) ||
      !storage_relaunch_controller.originalsBackupEnabled()) {
    return fail("storage choices should survive relaunch under the existing application settings identity");
  }
  storage_relaunch_controller.disableOriginalsBackup();
  if (storage_relaunch_controller.originalsBackupEnabled() ||
      !storage_relaunch_controller.storageConfigurationStatus().contains(QStringLiteral("not removed"))) {
    return fail("disabling future originals backups should not claim existing backup files were deleted");
  }

  const auto sample_project = controller.prepareSampleProject();
  if (sample_project.isEmpty() ||
      !QFileInfo::exists(QDir(sample_project).filePath(QStringLiteral("01-mountain-gold.png"))) ||
      !QFileInfo::exists(QDir(sample_project).filePath(QStringLiteral("02-studio-portrait.png"))) ||
      !QFileInfo::exists(QDir(sample_project).filePath(QStringLiteral("03-city-blue-hour.png")))) {
    return fail("first-run sample project should provide three editable images");
  }
  const QImage first_sample(QDir(sample_project).filePath(QStringLiteral("01-mountain-gold.png")));
  QSet<QRgb> sampled_colors;
  for (int y = 80; y < first_sample.height(); y += 180) {
    for (int x = 80; x < first_sample.width(); x += 220) {
      sampled_colors.insert(first_sample.pixel(x, y));
    }
  }
  if (first_sample.isNull() || sampled_colors.size() < 6) {
    return fail("first-run sample photos should contain useful tonal and color variation for editing");
  }

  controller.completeFirstRunExperience();
  if (controller.firstRunExperiencePending()) {
    return fail("completing first run should persist its version");
  }
  photo_editor::desktop::AppShellController completed_first_run_controller;
  if (completed_first_run_controller.firstRunExperiencePending()) {
    return fail("first-run completion should persist across relaunch");
  }
  completed_first_run_controller.restartFirstRunExperience();
  if (!completed_first_run_controller.firstRunExperiencePending()) {
    return fail("first-run guidance should be restartable from Help");
  }

  controller.setStartupWorkspace(3);
  controller.setReopenLastWorkspace(true);
  controller.setDefaultTimelineSnapping(false);
  controller.setDefaultTimelineRipple(false);
  controller.setCullingAutoAdvance(false);
  controller.setCurrentWorkspace(4);
  controller.setInterfaceScale(2.0);
  controller.setReducedMotion(true);
  controller.setHighContrast(true);
  controller.setColorVisionMode(QStringLiteral("deuteranopia"));
  controller.setInterfaceDensity(QStringLiteral("inspect"));
  controller.setImportWorkflowProfile(QStringLiteral("studio"));
  if (!controller.saveImportPreset(
          QStringLiteral("Wedding cards"),
          {
              {QStringLiteral("importMode"), QStringLiteral("managed")},
              {QStringLiteral("duplicatePolicy"), QStringLiteral("skip")},
              {QStringLiteral("organizationTemplate"), QStringLiteral("date-camera")},
              {QStringLiteral("rawJpegPolicy"), QStringLiteral("raw-only")},
          })) {
    return fail("studio import presets should be saveable");
  }
  if (!controller.setShortcutOverrides({
          {QStringLiteral("tool-adjust"), QStringLiteral("Ctrl+Alt+7")},
          {QStringLiteral("tool-crop"), QStringLiteral("Ctrl+Alt+8")},
      }) ||
      controller.setShortcutOverride(QStringLiteral("tool-brush"), QStringLiteral("Ctrl+Alt+7"))) {
    return fail("shortcut customization should persist valid mappings and reject conflicts");
  }

  photo_editor::desktop::AppShellController persisted_controller;
  if (persisted_controller.startupWorkspace() != 3) {
    return fail("startup workspace should persist");
  }
  if (!persisted_controller.reopenLastWorkspace()) {
    return fail("reopen last workspace preference should persist");
  }
  if (persisted_controller.preferredStartupWorkspace() != 4) {
    return fail("preferred startup workspace should reuse the last workspace when enabled");
  }
  if (persisted_controller.defaultTimelineSnapping() || persisted_controller.defaultTimelineRipple()) {
    return fail("timeline preference values should persist");
  }
  if (persisted_controller.cullingAutoAdvance()) {
    return fail("culling auto-advance preference should persist");
  }
  if (!qFuzzyCompare(persisted_controller.interfaceScale(), 2.0) ||
      !persisted_controller.reducedMotion() || !persisted_controller.highContrast() ||
      persisted_controller.colorVisionMode() != QStringLiteral("deuteranopia") ||
      persisted_controller.interfaceDensity() != QStringLiteral("inspect") ||
      persisted_controller.importWorkflowProfile() != QStringLiteral("studio") ||
      persisted_controller.importPresetNames() != QStringList{QStringLiteral("Wedding cards")} ||
      persisted_controller.importPreset(QStringLiteral("Wedding cards"))
              .value(QStringLiteral("rawJpegPolicy"))
              .toString() != QStringLiteral("raw-only") ||
      persisted_controller.shortcutSequence(
          QStringLiteral("tool-adjust"), QStringLiteral("Ctrl+Alt+A")) !=
          QStringLiteral("Ctrl+Alt+7")) {
    return fail("accessibility, density, and shortcut preferences should persist across relaunch");
  }
  if (persisted_controller.setDisplayCalibration(
          QStringLiteral("display-a"),
          QStringLiteral("invalid"),
          100.0,
          101.0,
          100.0,
          5.0) ||
      !persisted_controller.setDisplayCalibration(
          QStringLiteral("display-a"),
          QStringLiteral("meter-run-1"),
          500.0,
          0.002,
          160.0,
          8.0)) {
    return fail(
        "display calibration preferences should reject invalid mastering values and persist a complete calibration");
  }
  {
    const auto settings = QSettings{};
    if (!settings
             .value(
                 QStringLiteral(
                     "color/displayCalibration/enabled"))
             .toBool() ||
        settings
                .value(
                    QStringLiteral(
                        "color/displayCalibration/displayIdentity"))
                .toString() !=
            QStringLiteral("display-a") ||
        settings
                .value(
                    QStringLiteral(
                        "color/displayCalibration/referenceWhiteNits"))
                .toDouble() !=
            160.0) {
      return fail(
          "a valid display calibration should persist under the application preference identity");
    }
  }
  persisted_controller.clearDisplayCalibration();
  {
    const auto settings = QSettings{};
    if (settings.contains(
            QStringLiteral(
                "color/displayCalibration"))) {
      return fail(
          "clearing display calibration should remove all persisted mastering values");
    }
  }

  {
    QSettings settings;
    settings.setValue(QStringLiteral("TimelineWorkspace/sourcePanelCollapsed"), true);
    settings.setValue(QStringLiteral("TimelineWorkspace/sourceRailTab"), QStringLiteral("browser"));
    settings.sync();
  }

  persisted_controller.resetTimelineWorkspaceSettings();
  {
    QSettings settings;
    if (settings.contains(QStringLiteral("TimelineWorkspace/sourcePanelCollapsed")) ||
        settings.contains(QStringLiteral("TimelineWorkspace/sourceRailTab"))) {
      return fail("timeline workspace settings should be removed by reset");
    }
  }

  persisted_controller.restoreDefaults();
  if (persisted_controller.startupWorkspace() != 0 || persisted_controller.reopenLastWorkspace() ||
      !persisted_controller.defaultTimelineSnapping() || !persisted_controller.defaultTimelineRipple() ||
      !persisted_controller.cullingAutoAdvance()) {
    return fail("restore defaults should restore the built-in preference values");
  }
  if (!qFuzzyCompare(persisted_controller.interfaceScale(), 1.0) ||
      persisted_controller.reducedMotion() || persisted_controller.highContrast() ||
      persisted_controller.colorVisionMode() != QStringLiteral("standard") ||
      persisted_controller.interfaceDensity() != QStringLiteral("pro") ||
      persisted_controller.importWorkflowProfile() != QStringLiteral("personal") ||
      !persisted_controller.importPresetNames().isEmpty() ||
      persisted_controller.shortcutOverrides().size() != 0) {
    return fail("restore defaults should reset accessibility, density, and custom shortcuts");
  }

  const auto support_bundle = persisted_controller.createSupportBundle();
  if (support_bundle.isEmpty() ||
      !QFileInfo::exists(QDir(support_bundle).filePath(QStringLiteral("support-manifest.json")))) {
    return fail("support bundle should contain a privacy-scoped manifest");
  }

  controller.setCurrentWorkspace(1);
  photo_editor::desktop::EditRecipeState recovered_state;
  recovered_state.exposure = 0.75;
  const auto recovered_adjustments =
      photo_editor::desktop::serialize_adjustment_patch_json(
          recovered_state,
          photo_editor::desktop::all_adjustment_groups(),
          photo_editor::desktop::
              AdjustmentPatchScope::AssetBound);
  controller.updateSessionState(
      QStringLiteral("asset-recovery-proof"),
      QStringLiteral("/tmp/recovery-timeline.framehouse"),
      QStringLiteral("Recovery Project"),
      recovered_adjustments,
      QStringLiteral("masks"));
  photo_editor::desktop::AppShellController recovered_controller;
  if (!recovered_controller.sessionRecoveryAvailable() ||
      recovered_controller.recoveredWorkspace() != 1 ||
      recovered_controller.recoveredAssetId() != QStringLiteral("asset-recovery-proof") ||
      recovered_controller.recoveredProject() != QStringLiteral("Recovery Project") ||
      recovered_controller.recoveredAdjustmentPatchJson() !=
          recovered_adjustments ||
      recovered_controller.recoveredEditMode() != QStringLiteral("masks") ||
      !recovered_controller.sessionRecoverySummary().contains(QStringLiteral("Edit session")) ||
      recovered_controller.sessionRecoveryDetails().size() < 5) {
    return fail("unclean relaunch should recover the active workspace, project, asset, edit, and timeline");
  }
  recovered_controller.dismissSessionRecovery();
  if (recovered_controller.sessionRecoveryAvailable()) {
    return fail("recovery summary should remain dismissible without changing recovered data");
  }
  recovered_controller.markCleanExit();
  photo_editor::desktop::AppShellController clean_relaunch_controller;
  if (clean_relaunch_controller.sessionRecoveryAvailable()) {
    return fail("a clean shutdown should not report a recovered session");
  }

  if (clean_relaunch_controller.confirmCatalogRepair()) {
    return fail("catalog repair must never run without explicit confirmation");
  }
  clean_relaunch_controller.requestCatalogRepair();
  if (!clean_relaunch_controller.catalogRepairConfirmationPending() ||
      !clean_relaunch_controller.catalogRecoveryStatus().contains(QStringLiteral("safety backup"))) {
    return fail("catalog repair should explain replacement and require confirmation");
  }
  clean_relaunch_controller.cancelCatalogRepair();
  if (clean_relaunch_controller.catalogRepairConfirmationPending() ||
      !clean_relaunch_controller.catalogRecoveryStatus().contains(QStringLiteral("no catalog data was changed"))) {
    return fail("canceling catalog repair should make the unchanged outcome explicit");
  }

  clear_settings();
  return EXIT_SUCCESS;
}
