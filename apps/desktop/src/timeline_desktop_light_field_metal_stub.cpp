#include "timeline_desktop_light_field_metal.h"

namespace photo_editor::desktop::timeline_motion {

QImage render_desktop_light_field_metal(
    const QSize& size,
    DesktopLightFieldParameters parameters,
    const std::atomic_bool* cancelled,
    QString* error_message) {
  Q_UNUSED(size);
  Q_UNUSED(parameters);
  Q_UNUSED(cancelled);
  if (error_message != nullptr) {
    *error_message = QStringLiteral("Metal compute is unavailable on this platform.");
  }
  return {};
}

QImage render_transition_veil_field_metal(
    const QSize& size,
    double local_time_seconds,
    double light_time_seconds,
    double gather,
    double field_opacity,
    const std::atomic_bool* cancelled,
    QString* error_message) {
  Q_UNUSED(size);
  Q_UNUSED(local_time_seconds);
  Q_UNUSED(light_time_seconds);
  Q_UNUSED(gather);
  Q_UNUSED(field_opacity);
  Q_UNUSED(cancelled);
  if (error_message != nullptr) {
    *error_message = QStringLiteral("Metal compute is unavailable on this platform.");
  }
  return {};
}

bool desktop_light_field_metal_available() {
  return false;
}

}  // namespace photo_editor::desktop::timeline_motion
