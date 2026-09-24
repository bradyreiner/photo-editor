#pragma once
#include <QString>
#include <QVariantMap>

namespace photo_editor::desktop {
// Optical metadata only; no GPS, thumbnails, maker-note payload or image decode.
QVariantMap read_capture_camera_metadata(const QString& path);
QVariantMap read_libraw_capture_camera_metadata(const QString& path);
}
