#pragma once

#include <algorithm>
#include <cmath>

namespace photo_editor::desktop {

// SAM2's mask boundary is the zero-logit contour. Confidence inside an
// object is not transparency. Use the local source-pixel gradient only to
// antialias that contour, keeping solid interiors and clear exteriors exact.
inline int sam2_mask_coverage(double logit, double gradient_x, double gradient_y) {
  if (!std::isfinite(logit) || !std::isfinite(gradient_x) || !std::isfinite(gradient_y)) return 0;
  const auto width = std::hypot(gradient_x, gradient_y);
  if (width < 1e-8) return logit > 0.0 ? 255 : 0;
  return static_cast<int>(std::lround(std::clamp(0.5 + logit / width, 0.0, 1.0) * 255.0));
}

}  // namespace photo_editor::desktop
