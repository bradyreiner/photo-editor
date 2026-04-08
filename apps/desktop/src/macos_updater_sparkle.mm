#include "macos_updater.h"

#import <Cocoa/Cocoa.h>
#import <Sparkle/Sparkle.h>

namespace photo_editor::desktop {

MacosUpdater::MacosUpdater(QObject* parent) : QObject(parent) {
  @autoreleasepool {
    SPUStandardUpdaterController* controller = [[SPUStandardUpdaterController alloc]
        initWithStartingUpdater:YES
        updaterDelegate:nil
        userDriverDelegate:nil];
    controller_ = (__bridge_retained void*)controller;
  }
}

MacosUpdater::~MacosUpdater() {
  @autoreleasepool {
    if (controller_ != nullptr) {
      CFBridgingRelease(controller_);
      controller_ = nullptr;
    }
  }
}

bool MacosUpdater::available() const { return controller_ != nullptr; }

void MacosUpdater::checkForUpdates() {
  @autoreleasepool {
    if (controller_ == nullptr) {
      return;
    }

    SPUStandardUpdaterController* controller =
        (__bridge SPUStandardUpdaterController*)controller_;
    [controller checkForUpdates:nil];
  }
}

}  // namespace photo_editor::desktop
