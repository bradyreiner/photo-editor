#include "color_fill_layer_effect_reference.h"
#include "color_working_space_runtime.h"

#include <algorithm>
#include <cmath>

namespace photo_editor::pipeline {

namespace {

[[nodiscard]] float smoothstep(float edge0, float edge1, float value) {
  const auto t = std::clamp((value - edge0) / (edge1 - edge0), 0.0F, 1.0F);
  return t * t * (3.0F - 2.0F * t);
}

}  // namespace

ColorPixel apply_compiled_fill_layer_point_effects(
    const CompiledFillLayerProgram& program, const CompiledFillLayer& layer,
    std::uint32_t x, std::uint32_t y, const ColorPixel& input) {
  auto output = input;
  if (std::abs(layer.lens_vignette) > 1.0e-6F &&
      program.width > 1U && program.height > 1U) {
    const auto center_x = static_cast<float>(program.width - 1U) * 0.5F;
    const auto center_y = static_cast<float>(program.height - 1U) * 0.5F;
    const auto maximum_radius = std::max(1.0F, std::hypot(center_x, center_y));
    const auto radius = std::hypot(
        static_cast<float>(x) - center_x,
        static_cast<float>(y) - center_y) / maximum_radius;
    const auto gain = std::max(
        0.20F, 1.0F + layer.lens_vignette *
            smoothstep(0.22F, 1.0F, radius) * 0.85F);
    output.red *= gain; output.green *= gain; output.blue *= gain;
  }
  if (std::abs(layer.dehaze) > 1.0e-9F) {
    auto context = PipelineColorContext{};
    context.rgb_to_xyz_d65 = program.rgb_to_xyz_d65;
    context.xyz_d65_to_rgb = program.xyz_d65_to_rgb;
    context.output_headroom = program.output_headroom;
    auto lab = linear_rgb_to_oklab(output, context);
    const auto chroma = std::hypot(lab.a, lab.b);
    if (chroma > 1.0e-6) {
      const auto scale = std::max(0.0, 1.0 + static_cast<double>(layer.dehaze) * 0.10);
      lab.a *= scale; lab.b *= scale;
      output = clamp_rgb_to_output_headroom(
          oklab_to_linear_rgb(lab, output.alpha, context), context);
    }
  }
  output.alpha = input.alpha;
  return output;
}

}  // namespace photo_editor::pipeline
