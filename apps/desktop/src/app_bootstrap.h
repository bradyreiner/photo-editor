#pragma once

#include <QProcess>
#include <QProcessEnvironment>
#include <QString>
#include <QStringList>

namespace photo_editor::desktop {

struct AppBootstrap final {
  // These storage identifiers intentionally remain stable so existing private-alpha
  // catalogs and preferences survive the public product-name transition.
  static constexpr const char* application_name = "PhotoEditor";
  static constexpr const char* organization_name = "PhotoEditor";

  static QString appDataLocation();
  static QString photoLibraryLocation();
  static QString originalsBackupLocation();
  static QString cacheLocation();
  static QString logLocation();
  static QString backupLocation();
  static QString iccProfileAssetLocation();
  static QString dcpProfileAssetLocation();
  static bool ensureRuntimeDirectories();
  static bool ensureCatalogService(QString* error_message = nullptr);
  static QString createCatalogBackup(QString* error_message = nullptr);
  static QString repairCatalogFromLatestBackup(QString* error_message = nullptr);
  static bool prepareProcessEnvironment(QProcessEnvironment* environment, QString* error_message = nullptr);
  static bool runCatalogHelper(
      const QString& binary_name,
      const QStringList& arguments,
      QString* output = nullptr,
      QString* error_message = nullptr,
      int timeout_ms = 30000);
  static QString helperProgram(const QString& binary_name);
  static QStringList helperArguments(const QString& binary_name, const QStringList& arguments);
};

}  // namespace photo_editor::desktop
