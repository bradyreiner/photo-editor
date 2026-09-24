#include "sensor_dust_detection.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

#include <QImage>

namespace {

int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

}  // namespace

int main() {
  using namespace photo_editor::desktop;

  if (!detect_dust_spots(QImage{}, 10).empty() ||
      !detect_dust_spots(QImage(15, 15, QImage::Format_ARGB32), 10).empty()) {
    return fail("dust detection must reject null and undersized images");
  }

  QImage smooth(64, 64, QImage::Format_ARGB32);
  smooth.fill(QColor(210, 210, 210));
  if (!detect_dust_spots(smooth, 8).empty() ||
      !detect_dust_spots(smooth, 0).empty()) {
    return fail("smooth images and zero-capacity requests must not invent dust");
  }

  QImage spotted = smooth;
  for (int y = 31; y <= 33; ++y) {
    for (int x = 31; x <= 33; ++x) {
      spotted.setPixelColor(x, y, QColor(18, 18, 18));
    }
  }
  const auto spots = detect_dust_spots(spotted, 1);
  if (spots.size() != 1 || std::abs(spots.front().x - 32) > 2 ||
      std::abs(spots.front().y - 32) > 2 || spots.front().radius < 2.4 ||
      spots.front().score <= 0.0) {
    return fail("a compact dark defect in a smooth field must produce one bounded candidate");
  }
  const auto repeated = detect_dust_spots(spotted, 1);
  if (repeated.size() != spots.size() || repeated.front().x != spots.front().x ||
      repeated.front().y != spots.front().y ||
      repeated.front().radius != spots.front().radius ||
      repeated.front().score != spots.front().score) {
    return fail("dust candidate detection must be deterministic");
  }

  const auto matte = dust_spot_mask_data(64, 64, spots);
  if (matte.size() != 64 * 64 ||
      static_cast<unsigned char>(matte.at(spots.front().y * 64 + spots.front().x)) !=
          255U ||
      static_cast<unsigned char>(matte.at(0)) != 0U ||
      dust_spot_mask_data(0, 64, spots).size() != 0 ||
      dust_spot_mask_data(64, 0, spots).size() != 0) {
    return fail("dust matte rasterization must be centered, bounded, and fail closed");
  }
  return EXIT_SUCCESS;
}
