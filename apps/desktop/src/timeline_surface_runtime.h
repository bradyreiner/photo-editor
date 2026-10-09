#pragma once

#include <QVariantList>
#include <QVariantMap>
#include <QString>

#include <atomic>
#include <functional>

namespace photo_editor::desktop {

// Availability is local-only: it never installs packages or downloads weights.
[[nodiscard]] bool timelineSurfaceAvailable(QString* reason = nullptr);

// Runs the optional OpenCV planar tracker synchronously. Call from a worker
// thread. Frames are numbered 000000.png onward and must be 2..900 images,
// each no larger than 960 pixels on its longest side.
[[nodiscard]] QVariantMap trackTimelineSurface(
    const QString& frames_directory,
    const QVariantList& source_times_seconds,
    const QVariantMap& seed,
    std::atomic_bool* cancelled = nullptr,
    std::function<void(int completed, int total)> progress = {});

}  // namespace photo_editor::desktop
