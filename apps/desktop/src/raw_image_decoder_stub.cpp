#include "raw_image_decoder.h"

namespace photo_editor::desktop {

bool raw_image_supported(const QString& /*file_path*/) { return false; }

QSize raw_image_size(const QString& /*file_path*/) { return {}; }

bool decode_raw_image(
    const QString& /*file_path*/,
    QImage* /*image*/,
    int /*max_edge*/,
    const RawDecodeSettings& /*settings*/) {
  return false;
}

bool decode_raw_linear_image(
    const QString& /*file_path*/,
    QImage* /*image*/,
    int /*max_edge*/,
    const RawDecodeSettings& /*settings*/) {
  return false;
}

bool native_image_supported(const QString& /*file_path*/) { return false; }

QSize native_image_size(const QString& /*file_path*/) { return {}; }

bool decode_native_image(
    const QString& /*file_path*/,
    QImage* /*image*/,
    int /*max_edge*/) {
  return false;
}

bool decode_native_linear_image(
    const QString& /*file_path*/,
    QImage* /*image*/,
    int /*max_edge*/) {
  return false;
}

}  // namespace photo_editor::desktop
