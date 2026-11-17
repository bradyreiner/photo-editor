#pragma once

#include "timeline_fractal_noise.h"

#include <QImage>
#include <QString>

#include <cstdint>

namespace native_recreation::svg_filters {

// Small, unregistered pixel prototype for SVG feDisplacementMap. The input
// source is kept in its current color space and interpolated premultiplied.
// The displacement map channel is sampled as a non-premultiplied scalar.
enum class Channel : std::uint8_t { Red = 0, Green = 1, Blue = 2, Alpha = 3 };

struct DisplacementParameters final {
  double scale = 0.0;
  Channel x_channel = Channel::Alpha;
  Channel y_channel = Channel::Alpha;
};

QString validate_displacement_parameters(
    const DisplacementParameters& parameters);

// W3C feDisplacementMap sampling for aligned, equal-sized pixel surfaces:
// P'(x,y) = P(x + scale*(XC-0.5), y + scale*(YC-0.5)). Bilinear interpolation
// is used for the source, with transparent samples beyond its raster bounds.
QImage apply_displacement_map(const QImage& source,
                              const QImage& map,
                              const DisplacementParameters& parameters,
                              QString* error_message = nullptr);

// Exact filter attributes on Canvas.tsx. Omitted SVG attributes mean
// stitchTiles=noStitch and x/yChannelSelector=A/A.
photo_editor::desktop::timeline_motion::FractalNoiseParameters
canvas_brush_noise_parameters();

// Renders the in2 map at Canvas SVG user-space resolution. This prototype
// assumes one raster pixel per user unit (1000x1250 for CampaignPoster).
QImage render_canvas_brush_noise_map(
    const QSize& user_space_size,
    QString* error_message = nullptr);

}  // namespace native_recreation::svg_filters
