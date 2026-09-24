#pragma once

#include <QByteArray>

class QImage;

namespace photo_editor::desktop {

struct RedEyeMaskResult final {
  QByteArray mask_data;
  int width = 0;
  int height = 0;
  int component_count = 0;
};

struct SkinMaskResult final {
  QByteArray mask_data;
  int width = 0;
  int height = 0;
  int pixel_count = 0;
};

[[nodiscard]] RedEyeMaskResult detect_red_eye_mask(const QImage& image);

[[nodiscard]] SkinMaskResult detect_skin_cleanup_mask(const QImage& image);

}  // namespace photo_editor::desktop
