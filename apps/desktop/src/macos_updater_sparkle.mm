#include "macos_updater.h"
#include "customer_platform_config.h"
#include "update_channel_policy.h"

#import <Cocoa/Cocoa.h>
#import <Sparkle/Sparkle.h>

@interface LooksyUpdaterDelegate : NSObject <SPUUpdaterDelegate>
@end
@implementation LooksyUpdaterDelegate
- (BOOL)updater:(SPUUpdater*)updater shouldProceedWithUpdate:(SUAppcastItem*)item
    updateCheck:(SPUUpdateCheck)updateCheck error:(NSError**)error {
  (void)updater;
  (void)updateCheck;
  const bool allowed = photo_editor::desktop::updateChannelAllowed(
      QString::fromUtf8(LOOKSY_RELEASE_CHANNEL), QString::fromUtf8(item.channel.UTF8String));
  if (!allowed && error != nullptr) {
    *error = [NSError errorWithDomain:@"com.virel.studio.update" code:1
        userInfo:@{NSLocalizedDescriptionKey: @"This update belongs to a different release channel."}];
  }
  return allowed ? YES : NO;
}
- (NSSet<NSString*>*)allowedChannelsForUpdater:(SPUUpdater*)updater {
  (void)updater;
  if ([@LOOKSY_RELEASE_CHANNEL isEqualToString:@"staging"])
    return [NSSet setWithObject:@"staging"];
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
