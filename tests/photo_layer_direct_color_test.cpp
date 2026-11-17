#include "color_layer_runtime.h"
#include "color_adjustment_kernel_runtime.h"
#include "photo_editor/pipeline/color_adjustments.h"

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
  {
    const auto render_movable_base = [&](const std::vector<PhotoLayer>& layers) {
      std::vector<ColorPixel> result{{0.0F, 0.0F, 1.0F, 1.0F}};
      std::string error;
      if (!apply_standard_photo_layers(&result, 1, 1, context, model, layers, {}, &error)) {
        std::cerr << error << '\n';
        return std::vector<ColorPixel>{};
      }
      return result;
    };
    PhotoLayer base_source;
    base_source.id = "base-source";
    base_source.kind = "pixel";
    base_source.copy_base_image = true;
    base_source.erase_base_image = true;
    base_source.pixel_width = 1;
    base_source.pixel_height = 1;
    base_source.opacity = 0.5;
    base_source.pixels = {{1.0F, 1.0F, 1.0F, 0.0F}};
    PhotoLayer translucent_red;
    translucent_red.kind = "fill";
    translucent_red.fill_color = {1.0F, 0.0F, 0.0F, 0.5F};
    const auto base_above_fill = render_movable_base({translucent_red, base_source});
    const auto base_below_fill = render_movable_base({base_source, translucent_red});
    if (base_above_fill.size() != 1U || base_below_fill.size() != 1U ||
        std::abs(base_above_fill.front().alpha - 0.75F) > 1.0e-6F ||
        std::abs(base_below_fill.front().alpha - 0.75F) > 1.0e-6F ||
        !(base_above_fill.front().blue > base_below_fill.front().blue) ||
        !(base_below_fill.front().red > base_above_fill.front().red)) {
      std::cerr << "Movable Base Image must render once at its stack position over a transparent destination\n";
      return 1;
    }

    base_source.enabled = false;
    const auto hidden_base = render_movable_base({translucent_red, base_source});
    if (hidden_base.size() != 1U || std::abs(hidden_base.front().alpha - 0.5F) > 1.0e-6F ||
        std::abs(hidden_base.front().red - 1.0F) > 1.0e-6F) {
      std::cerr << "A hidden movable Base Image must not remain as an implicit underlay\n";
      return 1;
    }
    base_source.enabled = true;

    base_source.opacity = 1.0;
    base_source.pixels.front().alpha = 0.5F;
    const auto erased_base_above_fill = render_movable_base({translucent_red, base_source});
    if (erased_base_above_fill.size() != 1U ||
        std::abs(erased_base_above_fill.front().alpha - 0.75F) > 1.0e-6F) {
      std::cerr << "Base eraser coverage must modify only the detached source pixels before stack compositing\n";
      return 1;
    }
  }
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

  ColorAdjustmentState extended_layer_lut_state;
  extended_layer_lut_state.color_pipeline.working_profile =
      working_color_profile_from_name("Linear sRGB");
  extended_layer_lut_state.color_pipeline.output_headroom = 4.0;
  LutAdjustment extended_layer_lut;
  extended_layer_lut.enabled = true;
  extended_layer_lut.strength = 100.0;
  extended_layer_lut.name = "Signed HDR layer LUT";
  extended_layer_lut.size = 2;
  extended_layer_lut.input_color_space = "Linear sRGB";
  extended_layer_lut.output_color_space = "Linear sRGB";
  extended_layer_lut.table.assign(8U, ColorPixel{1.25F, -0.25F, 0.50F, 1.25F});
  extended_layer_lut_state.photo_layers = {
      PhotoLayer{
          .id = "signed-hdr-layer-lut",
          .name = "Signed HDR layer LUT",
          .kind = "adjustment",
          .enabled = true,
          .opacity = 1.0,
          .lut_stack = {extended_layer_lut},
      },
  };
  std::vector<ColorPixel> extended_layer_pixels{{0.23F, 0.34F, 0.45F, 0.37F}};
  if (!apply_color_layers_stage(
          &extended_layer_pixels, 1, 1, extended_layer_lut_state, RemovalQuality::Final) ||
      extended_layer_pixels.size() != 1U ||
      std::abs(extended_layer_pixels.front().red - 1.25F) > 1.0e-5F ||
      std::abs(extended_layer_pixels.front().green + 0.25F) > 1.0e-5F ||
      std::abs(extended_layer_pixels.front().blue - 0.50F) > 1.0e-5F ||
      std::abs(extended_layer_pixels.front().alpha - 0.37F) > 1.0e-6F) {
    std::cerr << "Adjustment-layer LUT must preserve signed/HDR RGB and source alpha\n";
    return 1;
  }
  std::cout << "Direct raster colors preserve coverage and affect only layer content\n";
}
