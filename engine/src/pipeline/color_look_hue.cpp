#include "color_look_runtime.h"

#include "color_math_runtime.h"

#include <cmath>

namespace photo_editor::pipeline {

namespace {

struct HslColor final {
  double hue = 0.0;
  double saturation = 0.0;
  double lightness = 0.0;
};

[[nodiscard]] double hue_to_rgb(double p, double q, double t) {
  if (t < 0.0) {
    t += 1.0;
  }
  if (t > 1.0) {
    t -= 1.0;
  }
  if (t < 1.0 / 6.0) {
    return p + (q - p) * 6.0 * t;
  }
  if (t < 0.5) {
    return q;
  }
  if (t < 2.0 / 3.0) {
    return p + (q - p) * (2.0 / 3.0 - t) * 6.0;
  }
  return p;
}

[[nodiscard]] ColorPixel hsl_to_rgb(const HslColor& hsl) {
  if (hsl.saturation <= 1e-9) {
    const auto gray = static_cast<float>(clamp_unit(hsl.lightness));
    return ColorPixel{gray, gray, gray, 1.0F};
  }
  const auto q = hsl.lightness < 0.5
                     ? hsl.lightness * (1.0 + hsl.saturation)
                     : hsl.lightness + hsl.saturation - hsl.lightness * hsl.saturation;
  const auto p = 2.0 * hsl.lightness - q;
  return ColorPixel{
      static_cast<float>(clamp_unit(hue_to_rgb(p, q, hsl.hue + 1.0 / 3.0))),
      static_cast<float>(clamp_unit(hue_to_rgb(p, q, hsl.hue))),
      static_cast<float>(clamp_unit(hue_to_rgb(p, q, hsl.hue - 1.0 / 3.0))),
      1.0F};
}

}  // namespace

double oklab_hue_for_rgb_hue(
    double hue_unit,
    const PipelineColorContext& context) {
  const auto target_rgb = hsl_to_rgb(HslColor{
      .hue = wrap_unit(hue_unit),
      .saturation = 1.0,
      .lightness = 0.5,
  });
  const auto target_oklab = linear_rgb_to_oklab(target_rgb, context);
  return wrap_radians(std::atan2(target_oklab.b, target_oklab.a));
}

}  // namespace photo_editor::pipeline
