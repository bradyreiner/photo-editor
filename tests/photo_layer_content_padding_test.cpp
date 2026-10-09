#include "photo_editor/pipeline/color_adjustments.h"

#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>

using namespace photo_editor::pipeline;

int main(int argc, char** argv) {
  std::ofstream output;
  if (argc == 2) output.open(argv[1]);
  int cases = 0;
  for (const auto* type : {"gaussian_blur", "box_blur", "unsharp_mask", "motion_blur", "optical_diffusion"}) {
    for (const auto edge : {PhotoEffectEdgeMode::Transparent, PhotoEffectEdgeMode::Extend, PhotoEffectEdgeMode::Reflect}) {
      for (int variant = 0; variant < 12; ++variant) {
        const int width = variant == 11 ? 267 : 39;
        const int height = variant == 11 ? 259 : 31;
        const auto count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
        std::vector<ColorPixel> base(count, {.17F, .23F, .31F, .6F});
        PhotoLayer layer;
        layer.kind = variant == 9 ? "source" : "pixel";
        layer.pixel_width = width; layer.pixel_height = height;
        if (variant == 2) { layer.pixel_width += 6; layer.pixel_height += 10; layer.x = -3; layer.y = -5; }
        if (variant == 3) { layer.pixel_width /= 2; layer.pixel_height /= 2; layer.x = 5; layer.y = 4; }
        if (variant == 4) layer.x = 3;
        if (variant == 5) { layer.transform_rotation = 17; layer.transform_scale = .8; }
        if (variant == 6) { layer.kind = "text"; layer.text_material = "frosted_glass"; layer.text_material_blur = 2; }
        layer.pixels.resize(static_cast<std::size_t>(layer.pixel_width) * static_cast<std::size_t>(layer.pixel_height));
        for (std::size_t i = 0; i < layer.pixels.size(); ++i)
          layer.pixels[i] = {static_cast<float>(i % 19U) / 5 - .2F, .3F,
              static_cast<float>(i % 13U) / 7, static_cast<float>(i % 7U) / 6};
        if (variant == 10) { layer.kind = "fill"; layer.fill_color = {.4F, .3F, 1.7F, .6F}; }
        PhotoEffectInstance effect;
        effect.id = "spatial"; effect.type_id = type; effect.amount = .73;
        effect.gaussian = {2, edge}; effect.box = {2, edge};
        effect.unsharp.sigma = 2; effect.unsharp.edge_mode = edge;
        effect.motion.distance = 6; effect.motion.angle = 27; effect.motion.edge_mode = edge;
        effect.diffusion.radius = 3;
        if (variant == 1 || variant == 11) {
          effect.mask_ref = "coverage";
          std::vector<float> coverage(layer.pixels.size());
          for (std::size_t i = 0; i < coverage.size(); ++i) coverage[i] = static_cast<float>(i % 9U) / 8;
          layer.effect_masks = {{"coverage", coverage}};
          layer.opacity = .63; layer.blend_mode = "screen"; layer.clipping_mask = true;
          layer.mask_width = width; layer.mask_height = height; layer.mask_data.resize(count);
          for (std::size_t i = 0; i < count; ++i) layer.mask_data[i] = static_cast<std::uint8_t>(i % 256U);
        }
        layer.effects = {effect};
        if (variant == 7 || variant == 8) {
          PhotoEffectInstance invert;
          invert.id = "invert"; invert.type_id = "invert_linear_rgb"; invert.amount = .1;
          if (variant == 7) layer.effects.push_back(invert);
          else layer.effects.insert(layer.effects.begin(), invert);
        }
        PhotoLayer lower;
        lower.kind = "fill"; lower.fill_color = {.3F, .4F, .5F, .7F};
        ColorAdjustmentState state;
        state.photo_layers = {lower, layer};
        auto actual = base;
        if (!apply_color_layers_stage(&actual, width, height, state)) return 1;
        // Plain text rasters use the same pixel composition but conservatively
        // retain expanded content. This is an explicit-padding pixel oracle.
        auto reference = state;
        auto& content = reference.photo_layers.back();
        if (content.kind != "text") {
          if (content.kind == "fill") content.pixels.assign(count, content.fill_color);
          content.kind = "text";
          content.text_material.clear();
        }
        auto expected = base;
        if (!apply_color_layers_stage(&expected, width, height, reference) || actual != expected) {
          std::cerr << "Content padding mismatch: " << type << ' ' << static_cast<int>(edge) << ' ' << variant << '\n';
          return 1;
        }
        std::uint64_t hash = 14695981039346656037ULL;
        for (const auto& pixel : actual) {
          for (const auto channel : {pixel.red, pixel.green, pixel.blue, pixel.alpha}) {
            std::uint32_t bits = 0; std::memcpy(&bits, &channel, sizeof(bits));
            for (int byte = 0; byte < 4; ++byte) { hash ^= (bits >> (byte * 8)) & 255U; hash *= 1099511628211ULL; }
          }
        }
        if (output) output << type << ' ' << static_cast<int>(edge) << ' ' << variant << ' ' << hash << '\n';
        ++cases;
      }
    }
  }
  std::cout << cases << " content-padding cases match explicit expanded output\n";
  return argc == 2 && !output.good() ? 1 : 0;
}
