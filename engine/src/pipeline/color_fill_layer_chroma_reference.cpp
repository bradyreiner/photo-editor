#include "color_fill_layer_chroma_reference.h"

#include <algorithm>
#include <cmath>

namespace photo_editor::pipeline {

namespace {

constexpr auto kTwoPi = 6.28318530717958647692;

[[nodiscard]] double smoothstep(double edge0, double edge1, double value) {
  const auto unit = std::clamp((value - edge0) / (edge1 - edge0), 0.0, 1.0);
  return unit * unit * (3.0 - 2.0 * unit);
}

[[nodiscard]] double wrap_radians(double value) {
  value = std::fmod(value, kTwoPi);
  return value < 0.0 ? value + kTwoPi : value;
}

[[nodiscard]] double shortest_delta(double from, double to) {
  auto delta = std::fmod(to - from, kTwoPi);
  if (delta > kTwoPi * 0.5) delta -= kTwoPi;
  if (delta < -kTwoPi * 0.5) delta += kTwoPi;
  return delta;
}

[[nodiscard]] std::array<double, 3> multiply(
    const ColorMatrix3& matrix, const std::array<double, 3>& value) {
  return {
      matrix.values[0] * value[0] + matrix.values[1] * value[1] +
          matrix.values[2] * value[2],
      matrix.values[3] * value[0] + matrix.values[4] * value[1] +
          matrix.values[5] * value[2],
      matrix.values[6] * value[0] + matrix.values[7] * value[1] +
          matrix.values[8] * value[2],
  };
}

}  // namespace

ColorPixel apply_compiled_fill_layer_chroma(
    const CompiledFillLayerProgram& program,
    std::size_t layer_index,
    const ColorPixel& input) {
  if (layer_index >= program.layer_count ||
      !program.layers[layer_index].has_local_chroma) return input;
  const auto& layer = program.layers[layer_index];
  const auto& look = program.look_programs[layer_index];
  const auto xyz = multiply(
      look.rgb_to_xyz_d65, {input.red, input.green, input.blue});
  const auto l = 0.8189330101 * xyz[0] + 0.3618667424 * xyz[1] -
                 0.1288597137 * xyz[2];
  const auto m = 0.0329845436 * xyz[0] + 0.9293118715 * xyz[1] +
                 0.0361456387 * xyz[2];
  const auto s = 0.0482003018 * xyz[0] + 0.2643662691 * xyz[1] +
                 0.6338517070 * xyz[2];
  const auto l_root = std::cbrt(l);
  const auto m_root = std::cbrt(m);
  const auto s_root = std::cbrt(s);
  const auto lightness = 0.2104542553 * l_root + 0.7936177850 * m_root -
                         0.0040720468 * s_root;
  const auto a = 1.9779984951 * l_root - 2.4285922050 * m_root +
                 0.4505937099 * s_root;
  const auto b = 0.0259040371 * l_root + 0.7827717662 * m_root -
                 0.8086757660 * s_root;
  auto chroma = std::hypot(a, b);
  auto hue = wrap_radians(std::atan2(b, a));
  if (chroma <= 1.0e-6) {
    hue = layer.colorize_hue;
  } else {
    const auto low_chroma_weight = 1.0 - smoothstep(0.035, 0.24, chroma);
    chroma *= std::max(
        0.0, 1.0 + static_cast<double>(look.vibrance_strength) *
                       low_chroma_weight * 0.82);
    chroma *= look.saturation_strength >= 0.0F
        ? std::max(0.0, 1.0 + static_cast<double>(look.saturation_strength) * 0.85)
        : std::max(0.0, 1.0 + static_cast<double>(look.saturation_strength));
    chroma *= std::clamp(
        1.0 - 0.20 * smoothstep(0.78, 0.98, lightness) *
                         smoothstep(0.035, 0.22, chroma),
        0.78, 1.0);
  }
  hue = wrap_radians(hue + layer.hue_shift);
  chroma *= std::max(0.0, 1.0 + static_cast<double>(layer.saturation_shift));
  if (layer.colorize_amount > 1.0e-6F) {
    hue = wrap_radians(
        hue + shortest_delta(hue, layer.colorize_hue) * layer.colorize_amount);
    chroma = std::lerp(
        chroma, std::max(chroma, 0.18),
        static_cast<double>(layer.colorize_amount));
  }
  const auto next_a = chroma * std::cos(hue);
  const auto next_b = chroma * std::sin(hue);
  const auto next_l = lightness + 0.3963377774 * next_a + 0.2158037573 * next_b;
  const auto next_m = lightness - 0.1055613458 * next_a - 0.0638541728 * next_b;
  const auto next_s = lightness - 0.0894841775 * next_a - 1.2914855480 * next_b;
  const auto rgb = multiply(
      look.xyz_d65_to_rgb,
      {1.2270138511 * next_l * next_l * next_l -
           0.5577999807 * next_m * next_m * next_m +
           0.2812561490 * next_s * next_s * next_s,
       -0.0405801784 * next_l * next_l * next_l +
           1.1122568696 * next_m * next_m * next_m -
           0.0716766787 * next_s * next_s * next_s,
       -0.0763812845 * next_l * next_l * next_l -
           0.4214819784 * next_m * next_m * next_m +
           1.5861632204 * next_s * next_s * next_s});
  return {static_cast<float>(rgb[0]), static_cast<float>(rgb[1]),
          static_cast<float>(rgb[2]), input.alpha};
}

}  // namespace photo_editor::pipeline
