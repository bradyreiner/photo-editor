#include "app_shell_controller.h"

#include "edit_adjustment_patch.h"

#include <QDesktopServices>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QImage>
#include <QLinearGradient>
#include <QPainter>
#include <QSysInfo>
#include <QSettings>
#include <QSaveFile>
#include <QSet>
#include <QUrl>
#include <QVariant>

#include <algorithm>

#include "app_bootstrap.h"
#include "system_monitor_profile_state.h"

namespace photo_editor::desktop {

namespace {

#ifndef PHOTO_EDITOR_APP_VERSION
#define PHOTO_EDITOR_APP_VERSION "0.0.0"
#endif

#ifndef FRAMEHOUSE_PRODUCT_NAME
#define FRAMEHOUSE_PRODUCT_NAME "Looksy"
#endif

#ifndef FRAMEHOUSE_SUPPORT_URL
#define FRAMEHOUSE_SUPPORT_URL "https://github.com/bradyreiner/photo-editor/issues"
#endif

#ifndef FRAMEHOUSE_PRIVACY_URL
#define FRAMEHOUSE_PRIVACY_URL "https://github.com/bradyreiner/photo-editor/blob/main/site/privacy.html"
#endif

#ifndef FRAMEHOUSE_TERMS_URL
#define FRAMEHOUSE_TERMS_URL "https://github.com/bradyreiner/photo-editor/blob/main/site/terms.html"
#endif

constexpr int kDefaultStartupWorkspace = 0;
constexpr bool kDefaultReopenLastWorkspace = false;
constexpr bool kDefaultTimelineSnapping = true;
constexpr bool kDefaultTimelineRipple = true;
constexpr bool kDefaultCullingAutoAdvance = true;
constexpr bool kDefaultDarkMode = true;
constexpr int kWorkspaceCount = 5;
constexpr int kFirstRunExperienceVersion = 1;

const auto kStartupWorkspaceKey = "app/startupWorkspace";
const auto kReopenLastWorkspaceKey = "app/reopenLastWorkspace";
const auto kLastWorkspaceKey = "app/lastWorkspace";
const auto kDefaultTimelineSnappingKey = "timeline/defaultSnapping";
const auto kDefaultTimelineRippleKey = "timeline/defaultRipple";
const auto kCullingAutoAdvanceKey = "library/cullingAutoAdvance";
const auto kDarkModeKey = "app/darkMode";
const auto kInterfaceScaleKey = "accessibility/interfaceScale";
const auto kReducedMotionKey = "accessibility/reducedMotion";
const auto kHighContrastKey = "accessibility/highContrast";
const auto kColorVisionModeKey = "accessibility/colorVisionMode";
const auto kInterfaceDensityKey = "accessibility/interfaceDensity";
const auto kImportWorkflowProfileKey = "import/workflowProfile";
const auto kImportPresetsKey = "import/presets";
const auto kFirstRunExperienceVersionKey = "app/firstRunExperienceVersion";
const auto kShortcutOverridesKey = "shortcuts/overrides";
const auto kPhotoLibraryLocationKey = "storage/photoLibraryLocation";
const auto kOriginalsBackupLocationKey = "storage/originalsBackupLocation";
const auto kCatalogBackupLocationKey = "storage/catalogBackupLocation";

QString normalized_storage_path(const QString& path_or_url) {
  const auto trimmed = path_or_url.trimmed();
  if (trimmed.isEmpty()) {
    return {};
  }
  const QUrl url(trimmed);
  const auto local_path = url.isLocalFile() ? url.toLocalFile() : trimmed;
  const QFileInfo info(local_path);
  return QDir::cleanPath(info.isAbsolute() ? info.absoluteFilePath()
                                           : QDir::current().absoluteFilePath(local_path));
}

bool ensure_writable_storage_directory(const QString& path) {
  if (path.isEmpty() || (!QFileInfo::exists(path) && !QDir().mkpath(path))) {
    return false;
  }
  const QFileInfo info(path);
  return info.isDir() && info.isWritable();
}

bool storage_paths_overlap(const QString& left, const QString& right) {
  if (left.isEmpty() || right.isEmpty()) {
    return false;
  }
  const auto normalized_left = QDir::cleanPath(QFileInfo(left).absoluteFilePath());
  const auto normalized_right = QDir::cleanPath(QFileInfo(right).absoluteFilePath());
  const auto left_prefix = normalized_left + QDir::separator();
  const auto right_prefix = normalized_right + QDir::separator();
  return normalized_left == normalized_right || normalized_left.startsWith(right_prefix) ||
         normalized_right.startsWith(left_prefix);
}

bool write_sample_image(
    const QString& path,
    const QColor& sky_top,
    const QColor& sky_bottom,
    const QColor& ground,
    const QColor& subject,
    const int variant) {
  constexpr int kWidth = 1600;
  constexpr int kHeight = 1067;
  QImage image(kWidth, kHeight, QImage::Format_RGB32);
  image.fill(sky_bottom);

  QPainter painter(&image);
  painter.setRenderHint(QPainter::Antialiasing, true);
  QLinearGradient sky_gradient(0.0, 0.0, 0.0, 720.0);
  sky_gradient.setColorAt(0.0, sky_top);
  sky_gradient.setColorAt(1.0, sky_bottom);
  painter.fillRect(QRect(0, 0, kWidth, 760), sky_gradient);

  QLinearGradient ground_gradient(0.0, 700.0, 0.0, static_cast<qreal>(kHeight));
  ground_gradient.setColorAt(0.0, ground.lighter(118));
  ground_gradient.setColorAt(1.0, ground.darker(145));
  painter.fillRect(QRect(0, 700, kWidth, kHeight - 700), ground_gradient);

  painter.setPen(Qt::NoPen);
  if (variant == 0) {
    painter.setBrush(QColor(33, 45, 53, 175));
    painter.drawPolygon(QPolygonF{
        QPointF(0.0, 760.0), QPointF(360.0, 410.0), QPointF(620.0, 735.0),
        QPointF(940.0, 330.0), QPointF(1280.0, 720.0), QPointF(1600.0, 500.0),
        QPointF(1600.0, 820.0), QPointF(0.0, 820.0)});
    painter.setBrush(subject);
    painter.drawEllipse(QRectF(1130.0, 170.0, 170.0, 170.0));
  } else if (variant == 1) {
    painter.setBrush(QColor(15, 21, 27, 150));
    painter.drawRect(QRectF(0.0, 0.0, 490.0, static_cast<qreal>(kHeight)));
    painter.setBrush(subject);
    painter.drawEllipse(QRectF(680.0, 250.0, 300.0, 390.0));
    painter.setBrush(subject.darker(145));
    painter.drawRect(QRectF(610.0, 570.0, 440.0, 500.0));
  } else {
    painter.setBrush(QColor(245, 237, 220, 65));
    for (int index = 0; index < 7; ++index) {
      const auto x = 120.0 + static_cast<qreal>(index * 230);
      const auto height = 170.0 + static_cast<qreal>((index % 3) * 85);
      painter.drawRect(QRectF(x, 700.0 - height, 72.0, height));
    }
    painter.setBrush(subject);
    painter.drawEllipse(QRectF(250.0, 180.0, 140.0, 140.0));
  }

  painter.setBrush(Qt::NoBrush);
  painter.setPen(QPen(QColor(255, 255, 255, 55), 3.0));
  painter.drawRect(QRectF(28.0, 28.0, 1544.0, 1011.0));
  painter.end();
  return image.save(path, "PNG");
}

}  // namespace

AppShellController::AppShellController(QObject* parent) : QObject(parent) {
  current_workspace_ = preferredStartupWorkspace();
  checkForUncleanExit();
  photo_library_migration_process_.setProcessChannelMode(QProcess::MergedChannels);
  connect(
      &photo_library_migration_process_,
      &QProcess::finished,
      this,
      &AppShellController::handlePhotoLibraryMigrationFinished);
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

double AppShellController::interfaceScale() const {
  const QSettings settings;
  return std::clamp(settings.value(kInterfaceScaleKey, 1.0).toDouble(), 0.9, 2.0);
}

bool AppShellController::reducedMotion() const {
  const QSettings settings;
  return settings.value(kReducedMotionKey, false).toBool();
}

bool AppShellController::highContrast() const {
  const QSettings settings;
  return settings.value(kHighContrastKey, false).toBool();
}

QString AppShellController::colorVisionMode() const {
  const QSettings settings;
  const auto value = settings.value(kColorVisionModeKey, QStringLiteral("standard")).toString();
  return value == QStringLiteral("protanopia") || value == QStringLiteral("deuteranopia") ||
          value == QStringLiteral("tritanopia") || value == QStringLiteral("grayscale")
      ? value
      : QStringLiteral("standard");
}

QString AppShellController::interfaceDensity() const {
  const QSettings settings;
  const auto value = settings.value(kInterfaceDensityKey, QStringLiteral("pro")).toString();
  return value == QStringLiteral("clean") || value == QStringLiteral("inspect")
      ? value
      : QStringLiteral("pro");
}

QString AppShellController::importWorkflowProfile() const {
  const QSettings settings;
  return settings.value(kImportWorkflowProfileKey, QStringLiteral("personal")).toString() ==
                 QStringLiteral("studio")
             ? QStringLiteral("studio")
             : QStringLiteral("personal");
}

QStringList AppShellController::importPresetNames() const {
  auto names = importPreset(QStringLiteral("__all__")).keys();
  names.sort(Qt::CaseInsensitive);
  return names;
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
QString AppShellController::photoLibraryLocation() const { return AppBootstrap::photoLibraryLocation(); }
QString AppShellController::originalsBackupLocation() const { return AppBootstrap::originalsBackupLocation(); }
bool AppShellController::originalsBackupEnabled() const { return !originalsBackupLocation().isEmpty(); }
QString AppShellController::cacheLocation() const { return AppBootstrap::cacheLocation(); }
QString AppShellController::logLocation() const { return AppBootstrap::logLocation(); }
QString AppShellController::backupLocation() const { return AppBootstrap::backupLocation(); }
QString AppShellController::storageConfigurationStatus() const { return storage_configuration_status_; }
bool AppShellController::photoLibraryMigrationRunning() const {
  return photo_library_migration_process_.state() != QProcess::NotRunning;
}
QString AppShellController::appVersion() const { return QStringLiteral(PHOTO_EDITOR_APP_VERSION); }
QString AppShellController::productName() const { return QStringLiteral(FRAMEHOUSE_PRODUCT_NAME); }
QString AppShellController::supportUrl() const { return QStringLiteral(FRAMEHOUSE_SUPPORT_URL); }
QString AppShellController::privacyUrl() const { return QStringLiteral(FRAMEHOUSE_PRIVACY_URL); }
QString AppShellController::termsUrl() const { return QStringLiteral(FRAMEHOUSE_TERMS_URL); }

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

void AppShellController::setInterfaceScale(double scale) {
  const auto sanitized = std::clamp(scale, 0.9, 2.0);
  if (qFuzzyCompare(interfaceScale(), sanitized)) {
    return;
  }
  writeSetting(QString::fromUtf8(kInterfaceScaleKey), sanitized);
  emit preferencesChanged();
}

void AppShellController::setReducedMotion(bool reduced_motion) {
  if (reducedMotion() == reduced_motion) {
    return;
  }
  writeSetting(QString::fromUtf8(kReducedMotionKey), reduced_motion);
  emit preferencesChanged();
}

void AppShellController::setHighContrast(bool high_contrast) {
  if (highContrast() == high_contrast) {
    return;
  }
  writeSetting(QString::fromUtf8(kHighContrastKey), high_contrast);
  emit preferencesChanged();
}

void AppShellController::setColorVisionMode(const QString& mode) {
  const auto normalized = mode.trimmed().toLower();
  const auto sanitized =
      normalized == QStringLiteral("protanopia") || normalized == QStringLiteral("deuteranopia") ||
          normalized == QStringLiteral("tritanopia") || normalized == QStringLiteral("grayscale")
      ? normalized
      : QStringLiteral("standard");
  if (colorVisionMode() == sanitized) {
    return;
  }
  writeSetting(QString::fromUtf8(kColorVisionModeKey), sanitized);
  emit preferencesChanged();
}

void AppShellController::setInterfaceDensity(const QString& density) {
  const auto normalized = density.trimmed().toLower();
  const auto sanitized = normalized == QStringLiteral("clean") || normalized == QStringLiteral("inspect")
      ? normalized
      : QStringLiteral("pro");
  if (interfaceDensity() == sanitized) {
    return;
  }
  writeSetting(QString::fromUtf8(kInterfaceDensityKey), sanitized);
  emit preferencesChanged();
}

void AppShellController::setImportWorkflowProfile(const QString& profile) {
  const auto normalized = profile.trimmed().toLower() == QStringLiteral("studio")
                              ? QStringLiteral("studio")
                              : QStringLiteral("personal");
  if (importWorkflowProfile() == normalized) {
    return;
  }
  writeSetting(QString::fromUtf8(kImportWorkflowProfileKey), normalized);
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

bool AppShellController::setDisplayCalibration(
    const QString& display_identity,
    const QString& calibration_identity,
    double peak_luminance_nits,
    double black_luminance_nits,
    double reference_white_nits,
    double surround_luminance_nits) {
  const auto calibration =
      DisplayTargetCalibration{
          .display_identity =
              display_identity.trimmed(),
          .calibration_identity =
              calibration_identity.trimmed(),
          .peak_luminance_nits =
              peak_luminance_nits,
          .black_luminance_nits =
              black_luminance_nits,
          .reference_white_nits =
              reference_white_nits,
          .surround_luminance_nits =
              surround_luminance_nits,
      };
  if (!calibration.valid()) {
    return false;
  }
  auto settings = QSettings{};
  settings.setValue(
      QStringLiteral(
          "color/displayCalibration/enabled"),
      true);
  settings.setValue(
      QStringLiteral(
          "color/displayCalibration/displayIdentity"),
      calibration.display_identity);
  settings.setValue(
      QStringLiteral(
          "color/displayCalibration/calibrationIdentity"),
      calibration.calibration_identity);
  settings.setValue(
      QStringLiteral(
          "color/displayCalibration/peakLuminanceNits"),
      calibration.peak_luminance_nits);
  settings.setValue(
      QStringLiteral(
          "color/displayCalibration/blackLuminanceNits"),
      calibration.black_luminance_nits);
  settings.setValue(
      QStringLiteral(
          "color/displayCalibration/referenceWhiteNits"),
      calibration.reference_white_nits);
  settings.setValue(
      QStringLiteral(
          "color/displayCalibration/surroundLuminanceNits"),
      calibration.surround_luminance_nits);
  settings.sync();
  emit preferencesChanged();
  return settings.status() ==
         QSettings::NoError;
}

void AppShellController::clearDisplayCalibration() {
  auto settings = QSettings{};
  settings.remove(
      QStringLiteral(
          "color/displayCalibration"));
  settings.sync();
  emit preferencesChanged();
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

bool AppShellController::openPhotoLibraryFolder() const { return revealPath(photoLibraryLocation()); }

bool AppShellController::openOriginalsBackupFolder() const {
  return originalsBackupEnabled() && revealPath(originalsBackupLocation());
}

bool AppShellController::setPhotoLibraryLocation(const QString& path_or_url) {
  const auto path = normalized_storage_path(path_or_url);
  if (!ensure_writable_storage_directory(path)) {
    storage_configuration_status_ = QStringLiteral("Choose a writable folder for the Photo Library.");
    emit storageLocationsChanged();
    return false;
  }
  if (storage_paths_overlap(path, originalsBackupLocation())) {
    storage_configuration_status_ = QStringLiteral("The Photo Library and originals backup must use separate folders.");
    emit storageLocationsChanged();
    return false;
  }
  const auto current_path = photoLibraryLocation();
  if (QDir::cleanPath(current_path) == QDir::cleanPath(path)) {
    storage_configuration_status_ = QStringLiteral("This folder is already the active Photo Library.");
    emit storageLocationsChanged();
    return true;
  }

  auto current_library_has_files = false;
  if (QFileInfo::exists(current_path)) {
    QDirIterator iterator(current_path, QDir::Files, QDirIterator::Subdirectories);
    current_library_has_files = iterator.hasNext();
  }
  if (current_library_has_files) {
    if (photoLibraryMigrationRunning()) {
      storage_configuration_status_ = QStringLiteral("A Photo Library move is already running.");
      emit storageLocationsChanged();
      return false;
    }
    QDir target(path);
    if (!target.entryList(QDir::AllEntries | QDir::NoDotAndDotDot).isEmpty()) {
      storage_configuration_status_ = QStringLiteral("Choose an empty folder when moving an existing Photo Library.");
      emit storageLocationsChanged();
      return false;
    }
    QProcessEnvironment environment;
    QString environment_error;
    if (!AppBootstrap::prepareProcessEnvironment(&environment, &environment_error)) {
      storage_configuration_status_ = environment_error;
      emit storageLocationsChanged();
      return false;
    }
    pending_photo_library_location_ = path;
    photo_library_migration_process_.setWorkingDirectory(QDir::currentPath());
    photo_library_migration_process_.setProcessEnvironment(environment);
    photo_library_migration_process_.start(
        AppBootstrap::helperProgram(QStringLiteral("catalog_move_library")),
        AppBootstrap::helperArguments(
            QStringLiteral("catalog_move_library"),
            {current_path, path}));
    storage_configuration_status_ = QStringLiteral(
        "Moving the Photo Library with SHA-256 verification. The current library is retained for safety.");
    emit storageLocationsChanged();
    return true;
  }
  writeSetting(QString::fromUtf8(kPhotoLibraryLocationKey), path);
  storage_configuration_status_ = QStringLiteral("Photo Library location saved.");
  emit storageLocationsChanged();
  return true;
}

void AppShellController::handlePhotoLibraryMigrationFinished(
    int exit_code,
    QProcess::ExitStatus exit_status) {
  const auto output = QString::fromUtf8(photo_library_migration_process_.readAllStandardOutput()).trimmed();
  const auto marker = QStringLiteral("FRAMEHOUSE_LIBRARY_MOVE_RESULT=");
  const auto marker_index = output.lastIndexOf(marker);
  if (exit_status == QProcess::NormalExit && exit_code == 0 && marker_index >= 0 &&
      !pending_photo_library_location_.isEmpty()) {
    const auto payload = output.mid(marker_index + marker.size()).section(QLatin1Char('\n'), 0, 0);
    const auto result = QJsonDocument::fromJson(payload.toUtf8()).object();
    writeSetting(QString::fromUtf8(kPhotoLibraryLocationKey), pending_photo_library_location_);
    storage_configuration_status_ = QStringLiteral(
        "Photo Library moved: %1 files verified with SHA-256. The previous library was retained at %2.")
                                        .arg(result.value(QStringLiteral("copiedFiles")).toInt())
                                        .arg(result.value(QStringLiteral("oldRoot")).toString());
  } else {
    storage_configuration_status_ = output.isEmpty()
                                        ? QStringLiteral("Photo Library move failed; the current library remains active.")
                                        : QStringLiteral("Photo Library move failed: %1").arg(output);
  }
  pending_photo_library_location_.clear();
  emit storageLocationsChanged();
}

bool AppShellController::setOriginalsBackupLocation(const QString& path_or_url) {
  const auto path = normalized_storage_path(path_or_url);
  if (!ensure_writable_storage_directory(path)) {
    storage_configuration_status_ = QStringLiteral("Choose a writable folder for the originals backup.");
    emit storageLocationsChanged();
    return false;
  }
  if (storage_paths_overlap(path, photoLibraryLocation())) {
    storage_configuration_status_ = QStringLiteral("The originals backup must be separate from the Photo Library.");
    emit storageLocationsChanged();
    return false;
  }
  writeSetting(QString::fromUtf8(kOriginalsBackupLocationKey), path);
  storage_configuration_status_ = QStringLiteral("Originals backup enabled.");
  emit storageLocationsChanged();
  return true;
}

bool AppShellController::setCatalogBackupLocation(const QString& path_or_url) {
  const auto path = normalized_storage_path(path_or_url);
  if (!ensure_writable_storage_directory(path)) {
    storage_configuration_status_ = QStringLiteral("Choose a writable folder for catalog backups.");
    emit storageLocationsChanged();
    return false;
  }
  writeSetting(QString::fromUtf8(kCatalogBackupLocationKey), path);
  storage_configuration_status_ = QStringLiteral("Catalog backup location saved.");
  emit storageLocationsChanged();
  return true;
}

void AppShellController::disableOriginalsBackup() {
  QSettings settings;
  settings.remove(QString::fromUtf8(kOriginalsBackupLocationKey));
  settings.sync();
  storage_configuration_status_ = QStringLiteral("Originals backup disabled. Existing backup files were not removed.");
  emit storageLocationsChanged();
}

bool AppShellController::saveImportPreset(
    const QString& name,
    const QVariantMap& configuration) {
  const auto normalized_name = name.trimmed();
  if (normalized_name.isEmpty() || configuration.isEmpty()) {
    return false;
  }
  QSettings settings;
  auto presets = settings.value(kImportPresetsKey).toMap();
  presets.insert(normalized_name, configuration);
  settings.setValue(kImportPresetsKey, presets);
  settings.sync();
  if (settings.status() != QSettings::NoError) {
    return false;
  }
  emit preferencesChanged();
  return true;
}

QVariantMap AppShellController::importPreset(const QString& name) const {
  const QSettings settings;
  const auto presets = settings.value(kImportPresetsKey).toMap();
  if (name == QStringLiteral("__all__")) {
    return presets;
  }
  return presets.value(name.trimmed()).toMap();
}

void AppShellController::removeImportPreset(const QString& name) {
  QSettings settings;
  auto presets = settings.value(kImportPresetsKey).toMap();
  if (presets.remove(name.trimmed()) == 0) {
    return;
  }
  settings.setValue(kImportPresetsKey, presets);
  settings.sync();
  emit preferencesChanged();
}
bool AppShellController::openSupportPage() const { return QDesktopServices::openUrl(QUrl(supportUrl())); }
bool AppShellController::openPrivacyPage() const { return QDesktopServices::openUrl(QUrl(privacyUrl())); }
bool AppShellController::openTermsPage() const { return QDesktopServices::openUrl(QUrl(termsUrl())); }

QString AppShellController::createSupportBundle() const {
  const auto timestamp = QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMddTHHmmssZ"));
  const auto bundle_path =
      QDir(backupLocation()).filePath(QStringLiteral("support/FramehouseSupport-%1").arg(timestamp));
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

QString AppShellController::catalogRecoveryStatus() const { return catalog_recovery_status_; }

bool AppShellController::catalogRepairConfirmationPending() const {
  return catalog_repair_confirmation_pending_;
}

bool AppShellController::createCatalogBackup() {
  QString error_message;
  const auto path = AppBootstrap::createCatalogBackup(&error_message);
  catalog_recovery_status_ = path.isEmpty()
      ? (error_message.isEmpty() ? QStringLiteral("Catalog backup failed.") : error_message)
      : QStringLiteral("Catalog backup created: %1").arg(path);
  emit catalogRecoveryChanged();
  return !path.isEmpty();
}

void AppShellController::requestCatalogRepair() {
  catalog_repair_confirmation_pending_ = true;
  catalog_recovery_status_ = QStringLiteral(
      "Repair will create a safety backup, then replace the catalog with the latest verified backup.");
  emit catalogRecoveryChanged();
}

bool AppShellController::confirmCatalogRepair() {
  if (!catalog_repair_confirmation_pending_) {
    catalog_recovery_status_ = QStringLiteral("Confirm catalog repair before restoring a backup.");
    emit catalogRecoveryChanged();
    return false;
  }
  catalog_repair_confirmation_pending_ = false;
  QString error_message;
  const auto backup_path = AppBootstrap::repairCatalogFromLatestBackup(&error_message);
  catalog_recovery_status_ = backup_path.isEmpty()
      ? (error_message.isEmpty() ? QStringLiteral("Catalog repair failed.") : error_message)
      : QStringLiteral("Catalog repaired from %1. Retry loading the Library.").arg(backup_path);
  emit catalogRecoveryChanged();
  return !backup_path.isEmpty();
}

void AppShellController::cancelCatalogRepair() {
  if (!catalog_repair_confirmation_pending_) {
    return;
  }
  catalog_repair_confirmation_pending_ = false;
  catalog_recovery_status_ = QStringLiteral("Catalog repair canceled; no catalog data was changed.");
  emit catalogRecoveryChanged();
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
  settings.remove(QString::fromUtf8(kInterfaceScaleKey));
  settings.remove(QString::fromUtf8(kReducedMotionKey));
  settings.remove(QString::fromUtf8(kHighContrastKey));
  settings.remove(QString::fromUtf8(kColorVisionModeKey));
  settings.remove(QString::fromUtf8(kInterfaceDensityKey));
  settings.remove(QString::fromUtf8(kImportWorkflowProfileKey));
  settings.remove(QString::fromUtf8(kImportPresetsKey));
  settings.remove(QString::fromUtf8(kShortcutOverridesKey));
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
  recovered_project_ = root.value(QStringLiteral("project")).toString();
  const auto adjustment_value =
      root.value(QStringLiteral("adjustmentPatch"));
  if (adjustment_value.isObject()) {
    recovered_adjustment_patch_json_ = QString::fromUtf8(
        QJsonDocument(adjustment_value.toObject())
            .toJson(QJsonDocument::Compact));
  } else {
    // Compatibility is deliberately read-only. The next autosave
    // writes the canonical adjustmentPatch field.
    recovered_adjustment_patch_json_ =
        canonical_asset_bound_adjustment_patch_json(
            root.value(QStringLiteral("recipeJson"))
                .toString());
  }
  recovered_edit_mode_ = root.value(QStringLiteral("editMode")).toString();
  recovered_at_ = root.value(QStringLiteral("updatedAt")).toString();
  session_recovery_available_ = true;
}

bool AppShellController::sessionRecoveryAvailable() const { return session_recovery_available_; }

QString AppShellController::sessionRecoverySummary() const {
  if (!session_recovery_available_) {
    return {};
  }
  const auto workspace = workspaceNames().value(recovered_workspace_, QStringLiteral("workspace"));
  return QStringLiteral("Recovered your %1 session · saved edits and catalog state are intact.")
      .arg(workspace);
}

QStringList AppShellController::sessionRecoveryDetails() const {
  QStringList details;
  if (!session_recovery_available_) {
    return details;
  }
  details.push_back(QStringLiteral("Workspace: %1").arg(
      workspaceNames().value(recovered_workspace_, QStringLiteral("Unknown"))));
  if (!recovered_project_.isEmpty()) {
    details.push_back(QStringLiteral("Project: %1").arg(recovered_project_));
  }
  if (!recovered_asset_id_.isEmpty()) {
    details.push_back(QStringLiteral("Selected photo restored"));
  }
  if (!recovered_timeline_path_.isEmpty()) {
    details.push_back(QStringLiteral("Timeline document restored"));
  }
  if (!recovered_adjustment_patch_json_.isEmpty()) {
    details.push_back(QStringLiteral("Latest committed adjustments restored"));
  }
  if (!recovered_at_.isEmpty()) {
    details.push_back(QStringLiteral("Autosaved: %1").arg(recovered_at_));
  }
  return details;
}

void AppShellController::updateSessionState(
    const QString& active_asset_id,
    const QString& timeline_path,
    const QString& active_project,
    const QString& adjustment_patch_json,
    const QString& edit_mode) {
  const auto path = sessionStatePath();
  QJsonObject root;
  root.insert(QStringLiteral("cleanExit"), false);
  root.insert(QStringLiteral("workspace"), current_workspace_);
  root.insert(QStringLiteral("assetId"), active_asset_id);
  root.insert(QStringLiteral("timelinePath"), timeline_path);
  root.insert(QStringLiteral("project"), active_project);
  const auto canonical_patch =
      canonical_asset_bound_adjustment_patch_json(
          adjustment_patch_json);
  if (!canonical_patch.isEmpty()) {
    root.insert(
        QStringLiteral("adjustmentPatch"),
        QJsonDocument::fromJson(
            canonical_patch.toUtf8())
            .object());
  }
  root.insert(QStringLiteral("editMode"), edit_mode);
  root.insert(QStringLiteral("updatedAt"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
  QSaveFile file(path);
  if (file.open(QIODevice::WriteOnly)) {
    file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    (void)file.commit();
  }
}

void AppShellController::markCleanExit() {
  const auto path = sessionStatePath();
  QJsonObject root;
  root.insert(QStringLiteral("cleanExit"), true);
  QSaveFile file(path);
  if (file.open(QIODevice::WriteOnly)) {
    file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    (void)file.commit();
  }
}

void AppShellController::dismissSessionRecovery() {
  session_recovery_available_ = false;
  emit sessionRecoveryChanged();
}

int AppShellController::recoveredWorkspace() const { return recovered_workspace_; }
QString AppShellController::recoveredAssetId() const { return recovered_asset_id_; }
QString AppShellController::recoveredTimelinePath() const { return recovered_timeline_path_; }
QString AppShellController::recoveredProject() const { return recovered_project_; }
QString AppShellController::recoveredAdjustmentPatchJson() const {
  return recovered_adjustment_patch_json_;
}
QString AppShellController::recoveredEditMode() const { return recovered_edit_mode_; }

bool AppShellController::firstRunExperiencePending() const {
  const QSettings settings;
  return settings.value(kFirstRunExperienceVersionKey, 0).toInt() < kFirstRunExperienceVersion;
}

void AppShellController::completeFirstRunExperience() {
  if (!firstRunExperiencePending()) {
    return;
  }

  writeSetting(QString::fromUtf8(kFirstRunExperienceVersionKey), kFirstRunExperienceVersion);
  emit firstRunExperienceChanged();
}

void AppShellController::restartFirstRunExperience() {
  QSettings settings;
  settings.remove(QString::fromUtf8(kFirstRunExperienceVersionKey));
  settings.sync();
  emit firstRunExperienceChanged();
}

QString AppShellController::prepareSampleProject() const {
  const auto sample_path =
      QDir(appDataLocation()).filePath(QStringLiteral("samples/Quick Start"));
  if (!QDir().mkpath(sample_path)) {
    return {};
  }

  struct SampleDefinition {
    QString file_name;
    QColor sky_top;
    QColor sky_bottom;
    QColor ground;
    QColor subject;
  };
  const QList<SampleDefinition> samples{
      {QStringLiteral("01-mountain-gold.png"), QColor(43, 68, 93), QColor(224, 166, 112), QColor(54, 65, 61), QColor(238, 190, 90)},
      {QStringLiteral("02-studio-portrait.png"), QColor(66, 49, 72), QColor(166, 102, 101), QColor(53, 36, 46), QColor(226, 166, 132)},
      {QStringLiteral("03-city-blue-hour.png"), QColor(19, 40, 69), QColor(72, 105, 139), QColor(25, 31, 39), QColor(232, 142, 85)},
  };

  for (qsizetype index = 0; index < samples.size(); ++index) {
    const auto& sample = samples.at(index);
    const auto file_path = QDir(sample_path).filePath(sample.file_name);
    if (!write_sample_image(
            file_path,
            sample.sky_top,
            sample.sky_bottom,
            sample.ground,
            sample.subject,
            static_cast<int>(index))) {
      return {};
    }
  }
  return sample_path;
}

QVariantMap AppShellController::shortcutOverrides() const {
  const QSettings settings;
  return settings.value(kShortcutOverridesKey).toMap();
}

QString AppShellController::shortcutSequence(
    const QString& shortcut_id,
    const QString& default_sequence) const {
  const auto overrides = shortcutOverrides();
  const auto sequence = overrides.value(shortcut_id.trimmed()).toString().trimmed();
  return sequence.isEmpty() ? default_sequence : sequence;
}

bool AppShellController::setShortcutOverride(
    const QString& shortcut_id,
    const QString& sequence) {
  const auto id = shortcut_id.trimmed();
  const auto normalized_sequence = sequence.trimmed();
  if (id.isEmpty() || normalized_sequence.isEmpty()) {
    return false;
  }
  auto overrides = shortcutOverrides();
  overrides.insert(id, normalized_sequence);
  return setShortcutOverrides(overrides);
}

bool AppShellController::setShortcutOverrides(const QVariantMap& overrides) {
  QSet<QString> used_sequences;
  QVariantMap normalized;
  for (auto iterator = overrides.cbegin(); iterator != overrides.cend(); ++iterator) {
    const auto id = iterator.key().trimmed();
    const auto sequence = iterator.value().toString().trimmed();
    const auto conflict_key = sequence.toLower();
    if (id.isEmpty() || sequence.isEmpty() || used_sequences.contains(conflict_key)) {
      return false;
    }
    used_sequences.insert(conflict_key);
    normalized.insert(id, sequence);
  }
  writeSetting(QString::fromUtf8(kShortcutOverridesKey), normalized);
  emit preferencesChanged();
  return true;
}

void AppShellController::resetShortcutOverride(const QString& shortcut_id) {
  auto overrides = shortcutOverrides();
  if (overrides.remove(shortcut_id.trimmed()) == 0) {
    return;
  }
  writeSetting(QString::fromUtf8(kShortcutOverridesKey), overrides);
  emit preferencesChanged();
}

}  // namespace photo_editor::desktop
