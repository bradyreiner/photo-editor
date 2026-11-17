#pragma once

#include <QImage>
#include <QSize>
#include <QString>
#include <QVariantMap>

#include <cstdint>
#include <functional>

namespace photo_editor::desktop::timeline_motion {

// Parameters for the SVG Filter Effects feTurbulence fractalNoise primitive.
// The generated image fills an SVG-default filter region (-10%, -10%, 120%,
// 120%) and is clipped to the requested viewport. RGB is desaturated with the
// SVG feColorMatrix saturate=0 matrix; alpha remains the fourth noise channel.
struct FractalNoiseParameters {
  double base_frequency_x = 0.78;
  double base_frequency_y = 0.78;
  int num_octaves = 3;
  std::int64_t seed = 19;
  bool stitch_tiles = true;
  // Optional local-source dimensions. Zero/zero means use the caller's
  // viewport dimensions. Explicit values are serialized only when requested.
  int output_width = 0;
  int output_height = 0;
};

QString validate_fractal_noise_parameters(
    const FractalNoiseParameters& parameters);
QString validate_fractal_noise_frame_size(const QSize& viewport_size);
QString validate_fractal_noise_output_size(
    const QSize& output_size,
    const FractalNoiseParameters& parameters);
[[nodiscard]] QSize resolved_fractal_noise_output_size(
    const QSize& fallback_size,
    const FractalNoiseParameters& parameters);
[[nodiscard]] QVariantMap canonical_fractal_noise_parameters(
    const FractalNoiseParameters& parameters);
[[nodiscard]] bool fractal_noise_parameters_from_variant_map(
    const QVariantMap& values,
    FractalNoiseParameters* parameters,
    QVariantMap* canonical_values = nullptr,
    QString* error_message = nullptr);

// Render a deterministic full-viewport, straight-alpha RGBA noise image.
// Output colors are converted from the SVG filter's default linearRGB space to
// sRGB storage. This follows the W3C reference algorithm; browser-specific
// raster equivalence still requires an image comparison against the source UA.
QImage render_fractal_noise_frame(
    const QSize& viewport_size,
    const FractalNoiseParameters& parameters,
    QString* error_message = nullptr,
    const std::function<bool()>& cancellation_check = {});

namespace fractal_noise_detail {

// Exposed only to let the focused unit target assert the published
// Park-Miller test vector used by the W3C feTurbulence reference algorithm.
std::int64_t park_miller_step(std::int64_t state);

// SVG's stitch frequency selection minimizes relative (not absolute) change.
double stitched_frequency(double frequency, double tile_extent);

}  // namespace fractal_noise_detail
}  // namespace photo_editor::desktop::timeline_motion
