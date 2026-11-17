#include "native_window_test_input.h"
#include <QGuiApplication>
#include <QEventLoop>
#include <QElapsedTimer>
#include <QThread>
#include <QTimer>
#include <QWindow>
#include <QVariant>
#include <QVariantMap>
#include <cstdio>
#include <cstdint>
#include <unistd.h>

#import <AppKit/AppKit.h>
#import <ApplicationServices/ApplicationServices.h>

namespace {

bool window_number(CFDictionaryRef info, int64_t* value) {
  if (info == nullptr || value == nullptr) return false;
  const auto raw = CFDictionaryGetValue(info, kCGWindowNumber);
  return raw != nullptr && CFGetTypeID(raw) == CFNumberGetTypeID() &&
      CFNumberGetValue(static_cast<CFNumberRef>(raw), kCFNumberSInt64Type, value);
}

bool window_number_field(CFDictionaryRef info, CFStringRef key, int* value) {
  if (info == nullptr || key == nullptr || value == nullptr) return false;
  const auto raw = CFDictionaryGetValue(info, key);
  return raw != nullptr && CFGetTypeID(raw) == CFNumberGetTypeID() &&
      CFNumberGetValue(static_cast<CFNumberRef>(raw), kCFNumberIntType, value);
}

bool window_alpha(CFDictionaryRef info, double* value) {
  if (info == nullptr || value == nullptr) return false;
  const auto raw = CFDictionaryGetValue(info, kCGWindowAlpha);
  return raw != nullptr && CFGetTypeID(raw) == CFNumberGetTypeID() &&
      CFNumberGetValue(static_cast<CFNumberRef>(raw), kCFNumberDoubleType, value);
}

bool window_bounds(CFDictionaryRef info, CGRect* bounds) {
  if (info == nullptr || bounds == nullptr) return false;
  const auto raw = CFDictionaryGetValue(info, kCGWindowBounds);
  return raw != nullptr && CFGetTypeID(raw) == CFDictionaryGetTypeID() &&
      CGRectMakeWithDictionaryRepresentation(static_cast<CFDictionaryRef>(raw), bounds);
}

QString window_string_field(CFDictionaryRef info, CFStringRef key) {
  if (info == nullptr || key == nullptr) return {};
  const auto raw = CFDictionaryGetValue(info, key);
  if (raw == nullptr || CFGetTypeID(raw) != CFStringGetTypeID()) return {};
  char value[512]{};
  if (!CFStringGetCString(static_cast<CFStringRef>(raw), value,
          static_cast<CFIndex>(sizeof(value)), kCFStringEncodingUTF8))
    return {};
  return QString::fromUtf8(value);
}

QVariantMap rect_variant_map(CGRect bounds) {
  return {
      {QStringLiteral("x"), CGRectGetMinX(bounds)},
      {QStringLiteral("y"), CGRectGetMinY(bounds)},
      {QStringLiteral("width"), CGRectGetWidth(bounds)},
      {QStringLiteral("height"), CGRectGetHeight(bounds)},
  };
}

}  // namespace

void activate_native_test_application() {
  if (QGuiApplication::platformName() != QStringLiteral("cocoa")) return;
  // Retain legacy process-registration diagnostics for CLI test hosts.
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
  // Retry real AppKit activation under a bounded local event loop. This also
  // works when the proof already runs inside QApplication's outer loop.
  auto* application = QCoreApplication::instance();
  if (application == nullptr) return;
  std::fprintf(stderr, "Cocoa initial policy=%ld LSUIElement=%d LSBackgroundOnly=%d\n",
      static_cast<long>([NSApp activationPolicy]),
      [[[NSBundle mainBundle] objectForInfoDictionaryKey:@"LSUIElement"] boolValue],
      [[[NSBundle mainBundle] objectForInfoDictionaryKey:@"LSBackgroundOnly"] boolValue]);
  QEventLoop activation_loop;
  QTimer activation_poll;
  activation_poll.setInterval(100);
  QTimer activation_deadline;
  activation_deadline.setSingleShot(true);
  activation_deadline.setInterval(5000);
  QObject::connect(&activation_poll, &QTimer::timeout, &activation_loop, [&] {
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    [NSApp activate];
    [[NSRunningApplication currentApplication]
        activateWithOptions:NSApplicationActivateAllWindows];
    bool has_key_window = false;
    for (NSWindow* window in [NSApp windows]) {
      if ([window isVisible] && [window canBecomeKeyWindow]) {
        [window makeKeyAndOrderFront:nil];
        has_key_window = has_key_window || [window isKeyWindow];
      }
    }
    if ([NSApp isActive] && has_key_window) activation_loop.quit();
  });
  QObject::connect(&activation_deadline, &QTimer::timeout, &activation_loop,
                   &QEventLoop::quit);
  activation_poll.start();
  activation_deadline.start();
  activation_loop.exec();
  activation_poll.stop();
  activation_deadline.stop();
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
    std::fprintf(stderr, "Cocoa proof window: visible=%d key=%d canKey=%d\n",
        [window isVisible], [window isKeyWindow],
        [window canBecomeKeyWindow]);
  }
}

void activate_native_test_window(QWindow* window) {
  if (QGuiApplication::platformName() != QStringLiteral("cocoa") || !window) return;
  activate_native_test_application();
  // Multi-window proofs must select the requested window explicitly; activating
  // the application alone can leave its main editor as the key window.
  auto* view = reinterpret_cast<NSView*>(window->winId());
  NSWindow* native_window = [view window];
  [native_window makeKeyAndOrderFront:nil];
  window->raise();
  window->requestActivate();

  // AppKit can report the requested window as key before the Dock and other
  // transition windows have left its bounds. Let the native sampler qualify
  // the actual foreground state before a held-pointer proof begins. This wait
  // only pumps events and samples state; it does not activate or raise again.
  if (native_window_foreground_sample(window).value(QStringLiteral("qualified")).toBool()) return;
  // A second nested AppKit event loop can outlive its Qt quit timer. Pump a
  // bounded polling loop instead, including when invoked by another proof loop.
  QElapsedTimer settle_elapsed;
  settle_elapsed.start();
  while (settle_elapsed.elapsed() < 1500) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    if (native_window_foreground_sample(window).value(QStringLiteral("qualified")).toBool())
      return;
    QThread::msleep(10);
  }
}

QVariantMap native_window_foreground_sample(QWindow* window) {
  QVariantMap sample{
      {QStringLiteral("sample_available"), false},
      {QStringLiteral("cgwindow_list_available"), false},
      {QStringLiteral("target_window_listed"), false},
      {QStringLiteral("occlusion_available"), false},
      {QStringLiteral("unoccluded"), false},
      {QStringLiteral("qualified"), false},
      {QStringLiteral("cursor_window_level"), -1},
      {QStringLiteral("ignored_cursor_window_count"), 0},
      {QStringLiteral("ignored_cursor_windows"), QVariantList{}},
  };
  if (QGuiApplication::platformName() != QStringLiteral("cocoa") || window == nullptr) return sample;
  const auto cursor_window_level = CGWindowLevelForKey(kCGCursorWindowLevelKey);
  sample.insert(QStringLiteral("cursor_window_level"), cursor_window_level);

  @autoreleasepool {
    auto* view = reinterpret_cast<NSView*>(window->winId());
    NSWindow* native_window = [view window];
    if (native_window == nil) return sample;

    const auto process_id = getpid();
    NSRunningApplication* frontmost = [[NSWorkspace sharedWorkspace] frontmostApplication];
    const auto frontmost_process_id = frontmost != nil ? [frontmost processIdentifier] : -1;
    const auto app_active = [NSApp isActive];
    const auto frontmost_process = frontmost != nil && frontmost_process_id == process_id;
    const auto key_window = [native_window isKeyWindow];
    const auto window_visible = [native_window isVisible];
    const auto window_occlusion_visible =
        ([native_window occlusionState] & NSWindowOcclusionStateVisible) != 0;

    sample.insert(QStringLiteral("sample_available"), true);
    sample.insert(QStringLiteral("sample_source"), QStringLiteral("AppKit+CGWindowList"));
    sample.insert(QStringLiteral("process_id"), static_cast<int>(process_id));
    sample.insert(QStringLiteral("frontmost_process_id"), static_cast<int>(frontmost_process_id));
    sample.insert(QStringLiteral("frontmost_bundle_id"), frontmost != nil &&
            [frontmost bundleIdentifier] != nil
        ? QString::fromUtf8([[frontmost bundleIdentifier] UTF8String])
        : QString());
    sample.insert(QStringLiteral("app_active"), app_active);
    sample.insert(QStringLiteral("frontmost_process"), frontmost_process);
    sample.insert(QStringLiteral("key_window"), key_window);
    sample.insert(QStringLiteral("window_visible"), window_visible);
    sample.insert(QStringLiteral("window_occlusion_visible"), window_occlusion_visible);
    sample.insert(QStringLiteral("window_number"), static_cast<qlonglong>([native_window windowNumber]));

    CFArrayRef windows = CGWindowListCopyWindowInfo(
        kCGWindowListOptionOnScreenOnly, kCGNullWindowID);
    if (windows == nullptr) return sample;

    sample.insert(QStringLiteral("cgwindow_list_available"), true);
    const auto target_window_number = static_cast<int64_t>([native_window windowNumber]);
    CFIndex target_index = -1;
    CGRect target_bounds = CGRectNull;
    const auto window_count = CFArrayGetCount(windows);
    for (CFIndex index = 0; index < window_count; ++index) {
      const auto info = static_cast<CFDictionaryRef>(CFArrayGetValueAtIndex(windows, index));
      int64_t listed_window_number = 0;
      if (window_number(info, &listed_window_number) &&
          listed_window_number == target_window_number) {
        target_index = index;
        static_cast<void>(window_bounds(info, &target_bounds));
        break;
      }
    }

    auto overlapping_window_count = 0;
    auto overlapping_same_process_count = 0;
    auto overlapping_other_process_count = 0;
    auto overlap_area_points2 = 0.0;
    QVariantList overlapping_windows;
    QVariantList ignored_cursor_windows;
    if (target_index >= 0 && !CGRectIsNull(target_bounds) && !CGRectIsEmpty(target_bounds)) {
      for (CFIndex index = 0; index < target_index; ++index) {
        const auto info = static_cast<CFDictionaryRef>(CFArrayGetValueAtIndex(windows, index));
        int64_t listed_window_number = 0;
        int owner_process_id = -1;
        double alpha = 1.0;
        CGRect occluding_bounds = CGRectNull;
        if (!window_number(info, &listed_window_number) ||
            listed_window_number == target_window_number ||
            !window_number_field(info, kCGWindowOwnerPID, &owner_process_id) ||
            !window_alpha(info, &alpha) || alpha <= 0.0 ||
            !window_bounds(info, &occluding_bounds) ||
            CGRectIsNull(occluding_bounds) || CGRectIsEmpty(occluding_bounds)) {
          continue;
        }
        const auto intersection = CGRectIntersection(target_bounds, occluding_bounds);
        if (CGRectIsNull(intersection) || CGRectIsEmpty(intersection)) continue;
        int owner_layer = -1;
        static_cast<void>(window_number_field(info, kCGWindowLayer, &owner_layer));
        const auto overlap_area = static_cast<double>(CGRectGetWidth(intersection)) *
            static_cast<double>(CGRectGetHeight(intersection));
        const QVariantMap window_metadata{
            {QStringLiteral("owner_name"), window_string_field(info, kCGWindowOwnerName)},
            {QStringLiteral("owner_pid"), owner_process_id},
            {QStringLiteral("window_id"), static_cast<qlonglong>(listed_window_number)},
            {QStringLiteral("layer"), owner_layer},
            {QStringLiteral("bounds"), rect_variant_map(occluding_bounds)},
            {QStringLiteral("overlap_bounds"), rect_variant_map(intersection)},
            {QStringLiteral("alpha"), alpha},
            {QStringLiteral("overlap_area_points2"), overlap_area},
        };
        if (owner_layer == cursor_window_level) {
          ignored_cursor_windows.append(window_metadata);
          continue;
        }
        ++overlapping_window_count;
        if (owner_process_id == process_id) {
          ++overlapping_same_process_count;
        } else {
          ++overlapping_other_process_count;
        }
        overlap_area_points2 += overlap_area;
        overlapping_windows.append(window_metadata);
      }
    }

    const auto target_window_listed = target_index >= 0;
    const auto occlusion_available = target_window_listed &&
        !CGRectIsNull(target_bounds) && !CGRectIsEmpty(target_bounds);
    const auto unoccluded = occlusion_available && overlapping_window_count == 0;
    sample.insert(QStringLiteral("target_window_listed"), target_window_listed);
    sample.insert(QStringLiteral("occlusion_available"), occlusion_available);
    sample.insert(QStringLiteral("overlapping_windows_above"), overlapping_window_count);
    sample.insert(QStringLiteral("overlapping_same_process_windows_above"),
        overlapping_same_process_count);
    sample.insert(QStringLiteral("overlapping_other_process_windows_above"),
        overlapping_other_process_count);
    sample.insert(QStringLiteral("overlap_area_points2"), overlap_area_points2);
    sample.insert(QStringLiteral("overlapping_windows"), overlapping_windows);
    sample.insert(QStringLiteral("cursor_window_level"), cursor_window_level);
    sample.insert(QStringLiteral("ignored_cursor_window_count"), ignored_cursor_windows.size());
    sample.insert(QStringLiteral("ignored_cursor_windows"), ignored_cursor_windows);
    sample.insert(QStringLiteral("unoccluded"), unoccluded);
    sample.insert(QStringLiteral("qualified"), app_active && frontmost_process && key_window &&
        window_visible && window_occlusion_visible && unoccluded);
    CFRelease(windows);
  }
  return sample;
}
