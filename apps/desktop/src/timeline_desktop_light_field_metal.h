#pragma once

#include "timeline_desktop_light_field.h"

#include <QImage>
#include <QString>

#include <atomic>

namespace photo_editor::desktop::timeline_motion {

// Renders the exact editable desktop-light field on the native GPU. A null
// image means Metal is unavailable, the input was invalid, or cancellation was
// requested; callers may then use the deterministic CPU renderer as fallback.
[[nodiscard]] QImage render_desktop_light_field_metal(
    const QSize& size,
    DesktopLightFieldParameters parameters = {},
    const std::atomic_bool* cancelled = nullptr,
    QString* error_message = nullptr);

[[nodiscard]] QImage render_transition_veil_field_metal(
    const QSize& size,
    double local_time_seconds,
    double light_time_seconds,
    double gather = 0.18,
    double field_opacity = 0.42,
    const std::atomic_bool* cancelled = nullptr,
    QString* error_message = nullptr);

[[nodiscard]] bool desktop_light_field_metal_available();

}  // namespace photo_editor::desktop::timeline_motion
