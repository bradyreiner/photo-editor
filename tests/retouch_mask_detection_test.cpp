#include "retouch_mask_detection.h"

#include <cstdlib>
#include <iostream>

#include <QColor>
#include <QImage>

namespace {

int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

}  // namespace

int main() {
  using namespace photo_editor::desktop;

  if (detect_red_eye_mask(QImage{}).component_count != 0 ||
      detect_red_eye_mask(QImage(7, 8, QImage::Format_ARGB32))
              .component_count != 0) {
    return fail("red-eye detection must reject null and undersized images");
  }

  QImage eyes(16, 16, QImage::Format_ARGB32);
  eyes.fill(QColor(24, 24, 24));
  for (int y = 7; y <= 9; ++y) {
    for (int x = 7; x <= 9; ++x) {
      eyes.setPixelColor(x, y, QColor(230, 22, 25));
    }
  }
  const auto red_eye = detect_red_eye_mask(eyes);
  if (red_eye.component_count != 1 || red_eye.width != 16 ||
      red_eye.height != 16 || red_eye.mask_data.size() != 16 * 16 ||
      static_cast<unsigned char>(red_eye.mask_data.at(8 * 16 + 8)) != 255U ||
      static_cast<unsigned char>(red_eye.mask_data.at(0)) != 0U) {
    return fail("a compact red pupil component must produce one centered feathered matte");
  }
  QImage oversized = eyes;
  oversized.fill(QColor(24, 24, 24));
  for (int y = 5; y <= 9; ++y) {
    for (int x = 5; x <= 9; ++x) {
      oversized.setPixelColor(x, y, QColor(230, 22, 25));
    }
  }
  if (detect_red_eye_mask(oversized).component_count != 0) {
    return fail("oversized red regions must be rejected as non-pupil components");
  }

  if (detect_skin_cleanup_mask(QImage{}).pixel_count != 0 ||
      detect_skin_cleanup_mask(QImage(3, 4, QImage::Format_ARGB32))
              .pixel_count != 0) {
    return fail("skin cleanup detection must reject null and undersized images");
  }
  QImage skin(4, 4, QImage::Format_ARGB32);
  skin.fill(QColor(190, 130, 105));
  const auto skin_mask = detect_skin_cleanup_mask(skin);
  if (skin_mask.pixel_count != 16 || skin_mask.width != 4 ||
      skin_mask.height != 4 || skin_mask.mask_data.size() != 16 ||
      static_cast<unsigned char>(skin_mask.mask_data.at(0)) != 210U) {
    return fail("admitted warm skin pixels must receive the exact cleanup alpha");
  }
  QImage neutral(4, 4, QImage::Format_ARGB32);
  neutral.fill(QColor(128, 128, 128));
  const auto neutral_mask = detect_skin_cleanup_mask(neutral);
  if (neutral_mask.pixel_count != 0 || !neutral_mask.mask_data.isEmpty() ||
      neutral_mask.width != 0 || neutral_mask.height != 0) {
    return fail("neutral pixels must not manufacture a skin cleanup matte");
  }
  return EXIT_SUCCESS;
}
