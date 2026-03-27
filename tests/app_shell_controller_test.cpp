#include <cstdlib>
#include <iostream>

#include <QCoreApplication>
#include <QSettings>
#include <QStandardPaths>

#include "app_shell_controller.h"

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
  QCoreApplication app(argc, argv);
  QStandardPaths::setTestModeEnabled(true);
  QCoreApplication::setOrganizationName(QStringLiteral("PhotoEditorTests"));
  QCoreApplication::setApplicationName(QStringLiteral("AppShellControllerTest"));
  clear_settings();

  photo_editor::desktop::AppShellController controller;
  if (controller.startupWorkspace() != 0) {
    return fail("default startup workspace should be Library");
  }
  if (controller.reopenLastWorkspace()) {
    return fail("reopen last workspace should default to false");
  }
  if (!controller.defaultTimelineSnapping() || !controller.defaultTimelineRipple()) {
    return fail("timeline defaults should start enabled");
  }

  controller.setStartupWorkspace(3);
  controller.setReopenLastWorkspace(true);
  controller.setDefaultTimelineSnapping(false);
  controller.setDefaultTimelineRipple(false);
  controller.setCurrentWorkspace(4);

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
      !persisted_controller.defaultTimelineSnapping() || !persisted_controller.defaultTimelineRipple()) {
    return fail("restore defaults should restore the built-in preference values");
  }

  clear_settings();
  return EXIT_SUCCESS;
}
