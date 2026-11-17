#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace photo_editor::desktop {

// Three separable box passes approximate a Gaussian in linear work. Keep the
// authored alpha untouched; only the compiled render copy is filtered.
inline void feather_mask_alpha(std::vector<std::uint8_t>& alpha, int width, int height,
    double amount, double full_width, double full_height) {
  if (width <= 0 || height <= 0 || alpha.size() != static_cast<std::size_t>(width) * static_cast<std::size_t>(height) ||
      !std::isfinite(amount) || amount <= 0) return;
  if (!std::isfinite(full_width) || !std::isfinite(full_height)) return;
  const auto sigma = std::clamp(std::clamp(amount / 100.0, 0.0, 1.0) *
      std::min(full_width, full_height) * 0.025, 0.0, static_cast<double>(std::max(width, height)));
  // Blend neighboring box radii by variance, so small slider moves do not
  // jump immediately to a three-pixel blur or plateau at integer radii.
  const int radius = static_cast<int>(std::floor((std::sqrt(1.0 + 4.0 * sigma * sigma) - 1.0) / 2.0));
  const auto blend = std::clamp((sigma * sigma - radius * (radius + 1.0)) / (2.0 * (radius + 1.0)), 0.0, 1.0);
  std::vector<float> pixels(alpha.begin(), alpha.end()), temporary(pixels.size());
  const auto pass = [&](bool horizontal) {
    const int lines = horizontal ? height : width;
    const int length = horizontal ? width : height;
    const auto index = [&](int line, int p) {
      return static_cast<std::size_t>(horizontal ? line * width + p : p * width + line);
    };
    for (int line = 0; line < lines; ++line) {
      double sum = 0;
      for (int p = -radius; p <= radius; ++p)
        sum += pixels[index(line, std::clamp(p, 0, length - 1))];
      double outer_sum = sum + pixels[index(line, 0)] + pixels[index(line, std::min(radius + 1, length - 1))];
      for (int p = 0; p < length; ++p) {
        temporary[index(line, p)] = static_cast<float>(std::lerp(
            sum / (2.0 * radius + 1.0), outer_sum / (2.0 * radius + 3.0), blend));
        sum += pixels[index(line, std::clamp(p + radius + 1, 0, length - 1))] -
            pixels[index(line, std::clamp(p - radius, 0, length - 1))];
        outer_sum += pixels[index(line, std::clamp(p + radius + 2, 0, length - 1))] -
            pixels[index(line, std::clamp(p - radius - 1, 0, length - 1))];
      }
    }
    pixels.swap(temporary);
  };
  for (int i = 0; i < 3; ++i) { pass(true); pass(false); }
  for (std::size_t i = 0; i < alpha.size(); ++i)
    alpha[i] = static_cast<std::uint8_t>(std::lround(std::clamp(pixels[i], 0.0F, 255.0F)));
}

}  // namespace photo_editor::desktop
