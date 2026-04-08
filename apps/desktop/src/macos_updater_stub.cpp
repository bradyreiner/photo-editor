#include "macos_updater.h"

namespace photo_editor::desktop {

MacosUpdater::MacosUpdater(QObject* parent) : QObject(parent) {}
MacosUpdater::~MacosUpdater() = default;

bool MacosUpdater::available() const { return controller_ != nullptr; }

void MacosUpdater::checkForUpdates() { controller_ = nullptr; }

}  // namespace photo_editor::desktop
