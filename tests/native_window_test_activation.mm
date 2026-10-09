#include "native_window_test_input.h"
#include <QGuiApplication>
#include <QEventLoop>
#include <QTimer>
#include <cstdio>

#import <AppKit/AppKit.h>
#import <ApplicationServices/ApplicationServices.h>

void activate_native_test_application() {
  // CLI smoke runners need to check in with the Process Manager before AppKit
  // can activate them. Keep this legacy compatibility step in test code only.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
  ProcessSerialNumber process{};
  const auto checked_in = GetCurrentProcess(&process);
  const auto transformed = checked_in == noErr
      ? TransformProcessType(&process, kProcessTransformToForegroundApplication) : checked_in;
  const auto foregrounded = transformed == noErr ? SetFrontProcess(&process) : transformed;
#pragma clang diagnostic pop
  std::fprintf(stderr, "Cocoa proof process registration: checkIn=%d transform=%d foreground=%d\n",
      checked_in, transformed, foregrounded);
  // Manual processEvents() smoke fixtures stop NSApp after initialization.
  // Complete foreground activation while Cocoa's native loop is running.
  QEventLoop launch_loop;
  QTimer::singleShot(0, &launch_loop, [] {
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    [NSApp activate];
    [[NSRunningApplication currentApplication]
        activateWithOptions:NSApplicationActivateAllWindows];
    for (NSWindow* window in [NSApp windows]) {
      if ([window isVisible] && [window canBecomeKeyWindow])
        [window makeKeyAndOrderFront:nil];
    }
  });
  QTimer::singleShot(250, &launch_loop, &QEventLoop::quit);
  launch_loop.exec();
  const auto changed = [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
  [NSApp activate];
  const auto activated = [[NSRunningApplication currentApplication]
      activateWithOptions:NSApplicationActivateAllWindows];
  std::fprintf(stderr, "Cocoa proof activation: platform=%s policyChanged=%d activated=%d active=%d running=%d bundle=%s windows=%lu\n",
      QGuiApplication::platformName().toUtf8().constData(), changed, activated,
      [NSApp isActive], [NSApp isRunning],
      [[[NSBundle mainBundle] bundleIdentifier] UTF8String],
      static_cast<unsigned long>([[NSApp windows] count]));
  for (NSWindow* window in [NSApp windows]) {
    std::fprintf(stderr, "Cocoa proof window: title=%s visible=%d key=%d canKey=%d\n",
        [[window title] UTF8String], [window isVisible], [window isKeyWindow],
        [window canBecomeKeyWindow]);
  }
}
