#include "macos_updater.h"

namespace photo_editor::desktop {

MacosUpdater::MacosUpdater() = default;
MacosUpdater::~MacosUpdater() = default;

bool MacosUpdater::available() const { return controller_ != nullptr; }

void MacosUpdater::checkForUpdates() { controller_ = nullptr; }

}  // namespace photo_editor::desktop
