#include "color_fill_layer_detail_program.h"

#include "color_blur_runtime.h"
#include "color_math_runtime.h"
#include "color_working_space_runtime.h"

#include <algorithm>
#include <cmath>

namespace photo_editor::pipeline {

bool apply_compiled_fill_layer_detail(
    std::vector<ColorPixel>* pixels, const CompiledFillLayerProgram& program,
    std::size_t layer_index) {
  if (pixels == nullptr || layer_index >= program.layer_count ||
      pixels->size() != static_cast<std::size_t>(program.width) * program.height) {
    return false;
  }
  const auto& layer = program.layers[layer_index];
  if (layer.has_spatial_detail) {
    auto context = PipelineColorContext{};
    context.rgb_to_xyz_native = program.rgb_to_xyz_native;
    context.rgb_to_xyz_d65 = program.rgb_to_xyz_d65;
    context.xyz_d65_to_rgb = program.xyz_d65_to_rgb;
    context.output_headroom = program.output_headroom;
    auto luminance = std::vector<float>(pixels->size());
    for (auto index = std::size_t{0U}; index < pixels->size(); ++index) {
      luminance[index] = static_cast<float>(
          luminance_for_pixel((*pixels)[index], context));
    }
    auto texture_blur = std::vector<float>{};
    auto clarity_blur = std::vector<float>{};
    auto workspace = BlurWorkspace{};
    if (std::abs(layer.texture) > 1.0e-9F) {
      guided_luminance_blur_into(
          luminance, static_cast<int>(program.width),
          static_cast<int>(program.height), 2, 0.00045,
          &texture_blur, &workspace);
    }
    guided_luminance_blur_into(
        luminance, static_cast<int>(program.width),
        static_cast<int>(program.height), 8, 0.0018,
        &clarity_blur, &workspace);
    for (auto y = std::uint32_t{0U}; y < program.height; ++y) {
      for (auto x = std::uint32_t{0U}; x < program.width; ++x) {
        const auto index = static_cast<std::size_t>(y) * program.width + x;
        const auto current = static_cast<double>(luminance[index]);
        auto activity = 0.0;
        if (x > 0U) activity = std::max(
            activity, std::abs(current - luminance[index - 1U]));
        if (x + 1U < program.width) activity = std::max(
            activity, std::abs(current - luminance[index + 1U]));
        if (y > 0U) activity = std::max(
            activity, std::abs(current - luminance[index - program.width]));
        if (y + 1U < program.height) activity = std::max(
            activity, std::abs(current - luminance[index + program.width]));
        const auto midtone = bell_weight(current, 0.5, 0.42);
        const auto tone_guard = smoothstep(0.025, 0.12, current) *
            (1.0 - smoothstep(0.74, 0.98, current));
        auto target = current;
        if (!texture_blur.empty()) {
          const auto signal = current - texture_blur[index];
          const auto broad = current - clarity_blur[index];
          const auto guard = 1.0 - smoothstep(0.08, 0.36, std::abs(broad)) * 0.72;
          const auto gain = layer.texture >= 0.0F ? 1.30 : 0.48;
          target += layer.texture * signal * (0.46 + midtone * 0.70) *
              guard * tone_guard * gain;
        }
        if (std::abs(layer.clarity) > 1.0e-9F) {
          const auto signal = current - clarity_blur[index];
          const auto edge = 1.0 - smoothstep(0.24, 0.58, std::abs(signal)) * 0.28;
          const auto structure = smoothstep(0.003, 0.035, activity);
          const auto gain = layer.clarity >= 0.0F ? 1.08 : 0.46;
          target += layer.clarity * signal * (0.36 + midtone * 0.82) *
              tone_guard * edge * structure * gain;
        }
        (*pixels)[index] = scale_pixel_to_luminance_with_headroom(
            (*pixels)[index], target, program.output_headroom, context);
      }
    }
  }
  if (layer.has_fine_detail && !apply_compiled_fine_detail_program(
          pixels, program.fine_detail_programs[layer_index])) return false;
  return true;
}

}  // namespace photo_editor::pipeline
