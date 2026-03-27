#pragma once

#include <QProcess>
#include <QProcessEnvironment>
#include <QString>

namespace photo_editor::desktop {

struct AppBootstrap final {
  static constexpr const char* application_name = "PhotoEditor";
  static constexpr const char* organization_name = "PhotoEditor";

  static QString appDataLocation();
  static QString cacheLocation();
  static QString logLocation();
  static QString backupLocation();
  static bool ensureRuntimeDirectories();
  static bool ensureCatalogService(QString* error_message = nullptr);
  static bool prepareProcessEnvironment(QProcessEnvironment* environment, QString* error_message = nullptr);
};

}  // namespace photo_editor::desktop
