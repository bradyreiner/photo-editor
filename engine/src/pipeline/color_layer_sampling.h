#pragma once
#include "photo_editor/pipeline/color_adjustments.h"
#include <algorithm>
#include <cmath>
#include <span>

namespace photo_editor::pipeline {

// Layer pixels are straight, scene-linear RGB. Filter associated contributions
// to keep hidden RGB out of cutout edges, then return straight RGB for grading
// and blending. The finite raster has transparent (not edge-clamped) padding.
template <typename Scalar>
ColorPixel sample_layer_bilinear(std::span<const ColorPixel> pixels, int width,
    int height, Scalar x, Scalar y) {
  if (!std::isfinite(x) || !std::isfinite(y) || x <= Scalar{-1} || y <= Scalar{-1} ||
      x >= static_cast<Scalar>(width) || y >= static_cast<Scalar>(height)) return {0, 0, 0, 0};
  const auto x0 = static_cast<int>(std::floor(x)), y0 = static_cast<int>(std::floor(y));
  const auto fx = x - static_cast<Scalar>(x0), fy = y - static_cast<Scalar>(y0);
  // Exact aligned copies stay exact, including straight HDR RGB.
  if (fx == Scalar{0} && fy == Scalar{0})
    return pixels[static_cast<std::size_t>(y0) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x0)];
  Scalar red = 0, green = 0, blue = 0, alpha = 0;
  for (int dy = 0; dy < 2; ++dy) for (int dx = 0; dx < 2; ++dx) {
    const auto sx = x0 + dx, sy = y0 + dy;
    if (sx < 0 || sy < 0 || sx >= width || sy >= height) continue;
    const auto& pixel = pixels[static_cast<std::size_t>(sy) * static_cast<std::size_t>(width) + static_cast<std::size_t>(sx)];
    const auto weight = (dx ? fx : Scalar{1} - fx) * (dy ? fy : Scalar{1} - fy) *
        std::clamp(static_cast<Scalar>(pixel.alpha), Scalar{0}, Scalar{1});
    if (weight <= Scalar{0}) continue;
    red += weight * static_cast<Scalar>(pixel.red);
    green += weight * static_cast<Scalar>(pixel.green);
    blue += weight * static_cast<Scalar>(pixel.blue);
    alpha += weight;
  }
  if (alpha <= Scalar{0}) return {0, 0, 0, 0};
  return {static_cast<float>(red / alpha), static_cast<float>(green / alpha),
      static_cast<float>(blue / alpha), static_cast<float>(alpha)};
}

// Uniform layer minification requires a wider low-pass footprint. Normalize
// against the entire discrete kernel, including transparent padding; clipping
// that normalization to image bounds would make thin cutouts spuriously opaque.
// The editor's minimum scale is 0.05: support is bounded to 20 source pixels.
template <typename Scalar>
ColorPixel sample_layer_filtered(std::span<const ColorPixel> pixels, int width,
    int height, Scalar x, Scalar y, Scalar scale) {
  if (scale >= Scalar{1}) return sample_layer_bilinear(pixels, width, height, x, y);
  const auto radius = Scalar{1} / std::clamp(scale, static_cast<Scalar>(0.05), Scalar{1});
  if (!std::isfinite(x) || !std::isfinite(y) || x <= -radius || y <= -radius ||
      x >= static_cast<Scalar>(width - 1) + radius || y >= static_cast<Scalar>(height - 1) + radius)
    return {0, 0, 0, 0};
  const auto left = static_cast<int>(std::ceil(x-radius)), right = static_cast<int>(std::floor(x+radius));
  const auto top = static_cast<int>(std::ceil(y-radius)), bottom = static_cast<int>(std::floor(y+radius));
  const auto weight = [radius](Scalar distance) { return std::max(Scalar{0}, Scalar{1}-std::abs(distance)/radius); };
  Scalar norm_x = 0, norm_y = 0;
  for (int sx = left; sx <= right; ++sx) norm_x += weight(x-static_cast<Scalar>(sx));
  for (int sy = top; sy <= bottom; ++sy) norm_y += weight(y-static_cast<Scalar>(sy));
  Scalar red = 0, green = 0, blue = 0, alpha = 0;
  for (int sy = std::max(top, 0); sy <= std::min(bottom, height-1); ++sy) {
    const auto wy = weight(y-static_cast<Scalar>(sy));
    for (int sx = std::max(left, 0); sx <= std::min(right, width-1); ++sx) {
      const auto& pixel = pixels[static_cast<std::size_t>(sy)*static_cast<std::size_t>(width)+static_cast<std::size_t>(sx)];
      const auto w = wy*weight(x-static_cast<Scalar>(sx))*std::clamp(static_cast<Scalar>(pixel.alpha), Scalar{0}, Scalar{1});
      if (w <= Scalar{0}) continue;
      red += w*static_cast<Scalar>(pixel.red); green += w*static_cast<Scalar>(pixel.green);
      blue += w*static_cast<Scalar>(pixel.blue); alpha += w;
    }
  }
  if (alpha <= Scalar{0} || norm_x <= Scalar{0} || norm_y <= Scalar{0}) return {0, 0, 0, 0};
  return {static_cast<float>(red/alpha), static_cast<float>(green/alpha),
      static_cast<float>(blue/alpha), static_cast<float>(alpha/(norm_x*norm_y))};
}
}  // namespace photo_editor::pipeline
