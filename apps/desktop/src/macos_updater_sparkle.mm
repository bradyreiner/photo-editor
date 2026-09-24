#include "macos_updater.h"
#include "customer_platform_config.h"

#import <Cocoa/Cocoa.h>
#import <Sparkle/Sparkle.h>

@interface LooksyUpdaterDelegate : NSObject <SPUUpdaterDelegate>
@end
@implementation LooksyUpdaterDelegate
- (NSSet<NSString*>*)allowedChannelsForUpdater:(SPUUpdater*)updater {
  (void)updater;
  return [@LOOKSY_RELEASE_CHANNEL isEqualToString:@"beta"]
      ? [NSSet setWithObject:@"beta"] : [NSSet set];
}
@end

namespace photo_editor::desktop {

MacosUpdater::MacosUpdater(QObject* parent) : QObject(parent) {
  @autoreleasepool {
    LooksyUpdaterDelegate* delegate = [[LooksyUpdaterDelegate alloc] init];
    SPUStandardUpdaterController* controller = [[SPUStandardUpdaterController alloc]
        initWithStartingUpdater:YES
        updaterDelegate:delegate
        userDriverDelegate:nil];
    controller_ = (__bridge_retained void*)controller;
    delegate_ = (__bridge_retained void*)delegate;
  }
}

MacosUpdater::~MacosUpdater() {
  @autoreleasepool {
    if (controller_ != nullptr) {
      CFBridgingRelease(controller_);
      controller_ = nullptr;
    }
    if (delegate_ != nullptr) {
      CFBridgingRelease(delegate_);
      delegate_ = nullptr;
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
