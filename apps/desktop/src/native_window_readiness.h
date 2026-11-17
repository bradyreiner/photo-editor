#pragma once

#include <QVariantMap>
#include <QWindow>

#if defined(Q_OS_MACOS)
namespace photo_editor::desktop {

// Captures a point-in-time AppKit view of the Qt window and this process's
// native windows. This is diagnostic data only; it does not activate windows.
QVariantMap sampleNativeWindowReadiness(QWindow* window);

}  // namespace photo_editor::desktop
#endif
