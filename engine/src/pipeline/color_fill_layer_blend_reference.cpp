#include "color_fill_layer_blend_reference.h"

#include "color_working_space_runtime.h"

#include <algorithm>
#include <cmath>

namespace photo_editor::pipeline {
namespace {

[[nodiscard]] float blend_channel(
    float base, float source, float headroom, std::uint32_t mode) {
  if (mode == 0U) return source;
  const auto b = std::max(0.0F, base);
  const auto s = std::max(0.0F, source);
  if (mode == 1U) return b * s;
  if (mode == 2U) return headroom - (headroom - b) * (headroom - s) / headroom;
  if (mode == 3U) return std::abs(b - s);
  if (mode == 4U) return std::max(b, s);
  if (mode == 5U) return std::min(b, s);
  const auto bn = std::clamp(b / headroom, 0.0F, 1.0F);
  const auto sn = std::clamp(s / headroom, 0.0F, 1.0F);
  const auto overlay = [](float base_value, float source_value) {
    return base_value <= 0.5F
        ? 2.0F * base_value * source_value
        : 1.0F - 2.0F * (1.0F - base_value) * (1.0F - source_value);
  };
  if (mode == 6U) return overlay(bn, sn) * headroom;
  if (mode == 7U) {
    const auto d = bn <= 0.25F
        ? ((16.0F * bn - 12.0F) * bn + 4.0F) * bn
        : std::sqrt(bn);
    return (sn <= 0.5F
        ? bn - (1.0F - 2.0F * sn) * bn * (1.0F - bn)
        : bn + (2.0F * sn - 1.0F) * (d - bn)) * headroom;
  }
  if (mode == 8U) return overlay(sn, bn) * headroom;
  return std::clamp(b + 2.0F * s - headroom, 0.0F, headroom);
}

}  // namespace

ColorPixel apply_retained_layer_blend(
    const CompiledFillLayerProgram& program, const ColorPixel& base,
    const ColorPixel& source, std::uint32_t mode) {
  if (mode < 10U) {
    return {
        blend_channel(base.red, source.red, program.blend_headroom, mode),
        blend_channel(base.green, source.green, program.blend_headroom, mode),
        blend_channel(base.blue, source.blue, program.blend_headroom, mode),
        source.alpha};
  }
  auto context = PipelineColorContext{};
  context.render_intent = program.render_intent;
  context.output_gamut_mapping_enabled = program.gamut_mapping_enabled;
  context.output_gamut_mapping_policy = program.gamut_mapping_policy;
  context.output_headroom = program.blend_headroom;
  context.layer_blend_headroom = program.blend_headroom;
  context.rgb_to_xyz_d65 = program.rgb_to_xyz_d65;
  context.xyz_d65_to_rgb = program.xyz_d65_to_rgb;
  const auto base_oklab = linear_rgb_to_oklab(base, context);
  const auto source_oklab = linear_rgb_to_oklab(source, context);
  auto lightness = clamp_oklab_lightness(base_oklab.lightness, context);
  auto chroma = std::hypot(base_oklab.a, base_oklab.b);
  auto hue = std::atan2(base_oklab.b, base_oklab.a);
  const auto source_chroma = std::hypot(source_oklab.a, source_oklab.b);
  const auto source_hue = source_chroma > 1.0e-6
      ? std::atan2(source_oklab.b, source_oklab.a) : hue;
  if (mode == 10U) {
    hue = source_hue;
  } else if (mode == 11U) {
    chroma = source_chroma;
  } else if (mode == 12U) {
    hue = source_hue;
    chroma = source_chroma;
  } else {
    lightness = clamp_oklab_lightness(source_oklab.lightness, context);
  }
  auto blended = gamut_fit_oklch(
      lightness, chroma, hue, source.alpha, context);
  blended.alpha = source.alpha;
  return blended;
}

}  // namespace photo_editor::pipeline
