#include "native_window_readiness.h"

#if defined(Q_OS_MACOS)

#import <AppKit/AppKit.h>

#include <QDateTime>
#include <QVariantList>

#include <unistd.h>

namespace photo_editor::desktop {
namespace {

quint32 occlusionState(NSWindow* window) {
  return window == nil ? 0U : static_cast<quint32>(window.occlusionState);
}

QVariantMap windowState(NSWindow* window, NSWindow* main_view_window) {
  const auto state = occlusionState(window);
  return {
      {QStringLiteral("windowNumber"),
       window == nil ? -1 : static_cast<qint64>(window.windowNumber)},
      {QStringLiteral("isMainViewWindow"), window != nil && window == main_view_window},
      {QStringLiteral("isKeyWindow"), window != nil && window.isKeyWindow},
      {QStringLiteral("isMainWindow"), window != nil && window.isMainWindow},
      {QStringLiteral("isVisible"), window != nil && window.isVisible},
      {QStringLiteral("canBecomeKeyWindow"), window != nil && window.canBecomeKeyWindow},
      {QStringLiteral("occlusionState"), state},
      {QStringLiteral("occlusionVisible"),
       (state & static_cast<quint32>(NSWindowOcclusionStateVisible)) != 0U},
      {QStringLiteral("level"), window == nil ? 0 : static_cast<int>(window.level)},
  };
}

}  // namespace

QVariantMap sampleNativeWindowReadiness(QWindow* window) {
  @autoreleasepool {
    NSApplication* application = NSApp;
    NSWindow* native_main_window = nil;
    bool native_main_view_available = false;

    // QWindow::winId() may create a native surface. The lifecycle probe samples
    // an already-visible QQuickWindow, so only read it when Qt already has a
    // platform window handle.
    if (window != nullptr && window->handle() != nullptr) {
      const auto native_id = static_cast<quintptr>(window->winId());
      void* native_view_handle = reinterpret_cast<void*>(native_id);
      NSView* native_main_view = (__bridge NSView*)native_view_handle;
      native_main_view_available = native_main_view != nil;
      native_main_window = native_main_view.window;
    }

    const auto this_process_id = static_cast<qint64>(getpid());
    NSRunningApplication* frontmost_application =
        NSWorkspace.sharedWorkspace.frontmostApplication;
    const bool frontmost_process_available = frontmost_application != nil;
    const auto frontmost_process_id = frontmost_process_available
        ? static_cast<qint64>(frontmost_application.processIdentifier)
        : qint64{-1};

    const auto key_window = application.keyWindow;
    const auto main_window_state = windowState(native_main_window, native_main_window);
    const auto main_window_is_owned =
        application != nil && native_main_window != nil &&
        [application.windows containsObject:native_main_window];

    QVariantList application_windows;
    if (application != nil) {
      for (NSWindow* candidate in application.windows) {
        application_windows.push_back(windowState(candidate, native_main_window));
      }
    }

    return {
        {QStringLiteral("sampledAt"),
         QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
        {QStringLiteral("available"), application != nil},
        {QStringLiteral("thisProcessId"), this_process_id},
        {QStringLiteral("frontmostProcessIdAvailable"), frontmost_process_available},
        {QStringLiteral("frontmostProcessId"), frontmost_process_id},
        {QStringLiteral("frontmostMatchesThisProcess"),
         frontmost_process_available && frontmost_process_id == this_process_id},
        {QStringLiteral("nativeApplicationActive"),
         application != nil && application.isActive},
        {QStringLiteral("nativeMainViewAvailable"), native_main_view_available},
        {QStringLiteral("nativeMainWindowAvailable"), native_main_window != nil},
        {QStringLiteral("nativeMainWindowBelongsToApplication"), main_window_is_owned},
        {QStringLiteral("nativeMainWindow"), main_window_state},
        {QStringLiteral("keyWindowExists"), key_window != nil},
        {QStringLiteral("keyWindowBelongsToMainWindow"),
         key_window != nil && key_window == native_main_window},
        {QStringLiteral("keyWindowNumber"),
         key_window == nil ? -1 : static_cast<qint64>(key_window.windowNumber)},
        {QStringLiteral("applicationWindows"), application_windows},
    };
  }
}

}  // namespace photo_editor::desktop

#endif  // defined(Q_OS_MACOS)
