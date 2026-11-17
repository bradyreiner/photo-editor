#include "timeline_fractal_noise.h"

#include <QImage>
#include <QSize>
#include <QString>

#include <cstdint>
#include <iostream>

namespace {

using photo_editor::desktop::timeline_motion::FractalNoiseParameters;
using photo_editor::desktop::timeline_motion::render_fractal_noise_frame;
using photo_editor::desktop::timeline_motion::validate_fractal_noise_parameters;

bool images_equal(const QImage& left, const QImage& right) {
  if (left.size() != right.size() || left.format() != right.format()) return false;
  for (int y = 0; y < left.height(); ++y) {
    const auto* left_row = left.constScanLine(y);
    const auto* right_row = right.constScanLine(y);
    if (left.bytesPerLine() != right.bytesPerLine()) return false;
    for (int index = 0; index < left.bytesPerLine(); ++index) {
      if (left_row[index] != right_row[index]) return false;
    }
  }
  return true;
}

bool images_differ(const QImage& left, const QImage& right) {
  return !images_equal(left, right);
}

bool test_w3c_seed_vector() {
  using photo_editor::desktop::timeline_motion::fractal_noise_detail::
      park_miller_step;
  std::int64_t state = 1;
  for (int index = 0; index < 10000; ++index) {
    state = park_miller_step(state);
  }
  if (state != 1043618065) {
    std::cerr << "Park-Miller seed sequence does not match the W3C test vector.\n";
    return false;
  }
  return true;
}

bool test_stitch_frequency_selection() {
  using photo_editor::desktop::timeline_motion::fractal_noise_detail::
      stitched_frequency;
  const auto selected = stitched_frequency(0.79, 100.0);
  if (selected != 0.79) {
    std::cerr << "Stitching should keep the exactly integral 79-period frequency.\n";
    return false;
  }
  const auto selected_near_high = stitched_frequency(0.78, 100.0);
  if (selected_near_high != 0.78) {
    std::cerr << "Stitching should select the closer 78-period frequency.\n";
    return false;
  }
  const auto selected_near_low = stitched_frequency(0.781, 100.0);
  if (selected_near_low != 0.78) {
    std::cerr << "Stitching should select the lower frequency when relative error is smaller.\n";
    return false;
  }
  return true;
}

bool test_native_source_image() {
  const QSize size(96, 64);
  const FractalNoiseParameters parameters;
  QString error;
  const auto first = render_fractal_noise_frame(size, parameters, &error);
  if (first.isNull() || !error.isEmpty() ||
      first.format() != QImage::Format_RGBA8888) {
    std::cerr << "Native SVG fractal-noise source failed: "
              << error.toStdString() << "\n";
    return false;
  }
  const auto second = render_fractal_noise_frame(size, parameters, &error);
  if (second.isNull() || !images_equal(first, second)) {
    std::cerr << "The same SVG fractal-noise parameters should be deterministic.\n";
    return false;
  }

  auto changed_seed = parameters;
  changed_seed.seed += 1;
  const auto other_seed = render_fractal_noise_frame(size, changed_seed, &error);
  if (other_seed.isNull() || !images_differ(first, other_seed)) {
    std::cerr << "Changing the SVG noise seed should change the generated source.\n";
    return false;
  }

  auto unstitched = parameters;
  unstitched.stitch_tiles = false;
  const auto non_stitched = render_fractal_noise_frame(size, unstitched, &error);
  if (non_stitched.isNull() || !images_differ(first, non_stitched)) {
    std::cerr << "The stitchTiles setting should affect the generated source.\n";
    return false;
  }

  bool has_gray_variation = false;
  bool has_alpha_variation = false;
  auto first_gray = -1;
  auto first_alpha = -1;
  for (int y = 0; y < first.height(); ++y) {
    const auto* row = first.constScanLine(y);
    for (int x = 0; x < first.width(); ++x) {
      const auto offset = x * 4;
      const auto red = row[offset];
      const auto green = row[offset + 1];
      const auto blue = row[offset + 2];
      const auto alpha = row[offset + 3];
      if (red != green || green != blue) {
        std::cerr << "feColorMatrix saturate=0 should produce grayscale RGB.\n";
        return false;
      }
      if (first_gray < 0) first_gray = red;
      if (first_alpha < 0) first_alpha = alpha;
      has_gray_variation = has_gray_variation || red != first_gray;
      has_alpha_variation = has_alpha_variation || alpha != first_alpha;
    }
  }
  if (!has_gray_variation || !has_alpha_variation) {
    std::cerr << "The native source should contain textured gray and alpha channels.\n";
    return false;
  }
  return true;
}

bool test_invalid_parameters_fail_closed() {
  auto parameters = FractalNoiseParameters{};
  parameters.base_frequency_x = -0.1;
  if (validate_fractal_noise_parameters(parameters).isEmpty()) {
    std::cerr << "Negative SVG noise frequencies must be rejected.\n";
    return false;
  }
  parameters = FractalNoiseParameters{};
  parameters.num_octaves = 17;
  if (validate_fractal_noise_parameters(parameters).isEmpty()) {
    std::cerr << "Oversized SVG octave counts must be rejected.\n";
    return false;
  }
  QString error;
  if (!render_fractal_noise_frame(QSize(0, 64), FractalNoiseParameters{}, &error)
           .isNull() || error.isEmpty()) {
    std::cerr << "Invalid dimensions must return an empty image and a reason.\n";
    return false;
  }
  return true;
}

bool test_cancel_stops_rendering() {
  int checks = 0;
  QString error;
  const auto image = render_fractal_noise_frame(
      QSize(96, 64), FractalNoiseParameters{}, &error,
      [&checks]() { return ++checks >= 3; });
  if (!image.isNull() || checks != 3 ||
      !error.contains(QStringLiteral("cancelled"), Qt::CaseInsensitive)) {
    std::cerr << "Cancellation should stop the renderer between scanlines.\n";
    return false;
  }
  return true;
}

}  // namespace

int main() {
  if (!test_w3c_seed_vector() || !test_stitch_frequency_selection() ||
      !test_native_source_image() || !test_invalid_parameters_fail_closed() ||
      !test_cancel_stops_rendering()) {
    return 1;
  }
  return 0;
}
