#include "crash_reporting_controller.h"
#include "update_channel_policy.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUuid>
#include <iostream>
using namespace photo_editor::desktop;
int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  QTemporaryDir settings;
  QStandardPaths::setTestModeEnabled(true);
  QCoreApplication::setOrganizationName(QStringLiteral("LooksyCrashConsentTest"));
  QCoreApplication::setApplicationName(QUuid::createUuid().toString(QUuid::WithoutBraces));
  QSettings::setDefaultFormat(QSettings::IniFormat);
  QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
  const auto root = QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation));
  const QString channel = QStringLiteral(LOOKSY_TEST_RELEASE_CHANNEL);
  const auto queue = root.filePath(QStringLiteral("crash-reports/%1").arg(channel));
  const auto other = root.filePath(QStringLiteral("crash-reports/untouched-proof"));
  const auto seed = [](const QString& path) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write("queued technical data") > 0;
  };
  if (!seed(queue + QStringLiteral("/pending.dmp")) || !seed(other + QStringLiteral("/pending.dmp"))) return 1;
  QSettings().setValue(QStringLiteral("privacy/unrelatedSetting"), true);
  {
    CrashReportingController controller;
    if (controller.enabled() || QFileInfo::exists(queue) || !QFileInfo::exists(other)) return 2;
    if (!seed(queue + QStringLiteral("/pending.dmp"))) return 3;
    controller.setEnabled(false);
    if (QFileInfo::exists(queue) || !QSettings().value(QStringLiteral("privacy/unrelatedSetting")).toBool()) return 4;
    controller.recordEvent(QStringLiteral("untrusted raw token or path"));
  }
  QTemporaryDir protected_files;
  if (!seed(protected_files.filePath(QStringLiteral("must-survive.dmp")))) return 5;
  QDir().mkpath(QFileInfo(queue).absolutePath());
  if (QFile::link(protected_files.path(), queue)) {
    CrashReportingController controller;
    controller.setEnabled(false);
    if (!QFileInfo::exists(protected_files.filePath(QStringLiteral("must-survive.dmp")))) return 7;
  }
  QFile::remove(queue);
  QDir(other).removeRecursively();
  QDir(root.path()).removeRecursively();
  if (!updateChannelAllowed(QStringLiteral("staging"), QStringLiteral("staging")) ||
      updateChannelAllowed(QStringLiteral("staging"), {}) ||
      updateChannelAllowed(QStringLiteral("stable"), QStringLiteral("staging")) ||
      updateChannelAllowed(QStringLiteral("development"), {}) ||
      !updateChannelAllowed(QStringLiteral("beta"), {}) ||
      !updateChannelAllowed(QStringLiteral("stable"), {})) return 8;
  std::cout << "Crash consent queue isolation and update channel policy passed\n";
  return 0;
}
