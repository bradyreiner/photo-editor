#include "color_look_runtime.h"

#include "color_math_runtime.h"

#include <array>
#include <cmath>
#include <cstddef>

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
    double hue_unit) {
  // UI hues are turns around the sRGB/Qt.hsla display wheel. Convert one
  // canonical display color through linear sRGB before measuring its OKLab
  // angle; interpreting the turn itself as an OKLab angle gives a different
  // visible color and makes the same control mean different things by gamut.
  static const auto context = [] {
    ColorAdjustmentState state;
    state.color_pipeline.working_profile = working_color_profile_from_name("Linear sRGB");
    return make_pipeline_color_context(state);
  }();
  struct CacheEntry final {
    double display_hue = 0.0;
    double oklab_hue = 0.0;
    bool valid = false;
  };
  // Post-tone controls share a small set of fixed wheel angles for every pixel
  // in an image. Keep their exact normalized mappings per render thread so we
  // do not repeat RGB-to-OKLab conversion for each sample.
  static thread_local std::array<CacheEntry, 16> cache{};
  static thread_local auto replacement_index = std::size_t{0};
  const auto display_hue = wrap_unit(hue_unit);
  for (const auto& entry : cache) {
    if (entry.valid && entry.display_hue == display_hue) {
      return entry.oklab_hue;
    }
  }
  auto target_rgb = hsl_to_rgb(HslColor{
      .hue = display_hue,
      .saturation = 1.0,
      .lightness = 0.5,
  });
  target_rgb.red = static_cast<float>(srgb_to_linear(target_rgb.red));
  target_rgb.green = static_cast<float>(srgb_to_linear(target_rgb.green));
  target_rgb.blue = static_cast<float>(srgb_to_linear(target_rgb.blue));
  const auto target_oklab = linear_rgb_to_oklab(target_rgb, context);
  const auto oklab_hue = wrap_radians(std::atan2(target_oklab.b, target_oklab.a));
  cache[replacement_index] = CacheEntry{
      .display_hue = display_hue,
      .oklab_hue = oklab_hue,
      .valid = true,
  };
  replacement_index = (replacement_index + 1U) % cache.size();
  return oklab_hue;
}

}  // namespace photo_editor::pipeline
