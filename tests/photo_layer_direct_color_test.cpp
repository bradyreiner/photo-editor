#include "color_layer_runtime.h"
#include "color_adjustment_kernel_runtime.h"

#include <cmath>
#include <iostream>

using namespace photo_editor::pipeline;

int main() {
  constexpr int width = 9, height = 5;
  ColorAdjustmentState state;
  const auto context = make_pipeline_color_context(state);
  const auto model = state.color_pipeline.photographic_rendering_model;
  const std::vector<ColorPixel> background(width * height, {0.12F, 0.2F, 0.3F, 1.0F});
  const auto render = [&](const std::vector<PhotoLayer>& layers) {
    auto result = background;
    std::string error;
    if (!apply_standard_photo_layers(&result, width, height, context, model, layers, {}, &error)) {
      std::cerr << error << '\n';
      result.clear();
    }
    return result;
  };
  for (const auto* kind : {"pixel", "source"}) {
    for (bool transformed : {false, true}) {
      for (bool grouped : {false, true}) {
        PhotoLayer cutout;
        cutout.kind = kind;
        cutout.id = "cutout";
        cutout.pixel_width = 4;
        cutout.pixel_height = 3;
        cutout.x = 2;
        cutout.y = 1;
        cutout.opacity = 0.7;
        cutout.mask_width = 2;
        cutout.mask_height = 1;
        cutout.mask_data = {128, 255};
        cutout.transform_scale = transformed ? 1.2 : 1.0;
        cutout.transform_rotation = transformed ? 10 : 0;
        for (int i = 0; i < 12; ++i)
          cutout.pixels.push_back({0.3F, 0.12F, 0.08F, static_cast<float>(i % 4) / 3});
        const auto neutral = render({cutout});
        cutout.exposure = 0.4;
        cutout.temperature = -18;
        cutout.saturation = -25;
        cutout.colorize_hue = 0.6;
        cutout.colorize_strength = 35;
        auto expected_cutout = cutout;
        for (auto& pixel : expected_cutout.pixels) {
          const auto alpha = pixel.alpha;
          apply_local_adjustment_mask(&pixel, context, model, 1.0,
              cutout.exposure, 0, 0, 0, 0, 0, cutout.temperature, 0,
              0, cutout.saturation, 0, 0, cutout.colorize_hue, cutout.colorize_strength);
          pixel.alpha = alpha;
        }
        expected_cutout.exposure = 0;
        expected_cutout.temperature = 0;
        expected_cutout.saturation = 0;
        expected_cutout.colorize_strength = 0;
        PhotoLayer group;
        group.kind = "group";
        group.id = "group";
        if (grouped) {
          cutout.parent_id = group.id;
          expected_cutout.parent_id = group.id;
        }
        const auto actual = render(grouped ? std::vector{group, cutout} : std::vector{cutout});
        const auto expected = render(grouped ? std::vector{group, expected_cutout} : std::vector{expected_cutout});
        if (actual.empty() || actual.size() != expected.size()) return 1;
        bool changed = false;
        for (std::size_t i = 0; i < actual.size(); ++i) {
          const auto& a = actual[i];
          const auto& b = expected[i];
          changed = changed || std::abs(a.red - neutral[i].red) > 1e-6 ||
              std::abs(a.green - neutral[i].green) > 1e-6 || std::abs(a.blue - neutral[i].blue) > 1e-6;
          if (std::abs(a.red - b.red) > 1e-6 || std::abs(a.green - b.green) > 1e-6 ||
              std::abs(a.blue - b.blue) > 1e-6 || a.alpha != b.alpha) {
            std::cerr << "Direct cutout colors must precede masks, opacity, placement and group compositing\n";
            return 1;
          }
        }
        if (!changed || actual.front().red != background.front().red || actual.front().alpha != 1.0F) return 1;
      }
    }
  }
  std::cout << "Direct raster colors preserve coverage and affect only layer content\n";
}
