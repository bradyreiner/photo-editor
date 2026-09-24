#include "photo_editor/pipeline/photo_effects.h"
#include "photo_editor/pipeline/photo_effect_codec.h"
#include <cmath>
#include <iostream>

using namespace photo_editor::pipeline;
int main() {
  PhotoEffectInstance effect;
  effect.id = "posterize";
  effect.type_id = "posterize_linear_rgb";
  effect.posterize_levels = 3;
  std::vector<ColorPixel> pixels{{0.24F, 0.25F, 0.76F, 1}, {-0.3F, 1.3F, 2.1F, 0.5F}, {0.1F, 0.2F, 0.3F, 0}};
  const auto original = pixels;
  if (!apply_photo_effect(&pixels, 3, 1, effect).ok() ||
      pixels[0] != ColorPixel{0, 0.5F, 1, 1} || pixels[1] != ColorPixel{-0.5F, 1.5F, 2, 0.5F} ||
      pixels[2] != original[2]) return 1;
  effect.amount = 0.5;
  effect.mask_ref = "coverage";
  const std::vector<float> mask{1, 0, 1};
  pixels = original;
  if (!apply_photo_effect(&pixels, 3, 1, effect, {}, mask).ok() ||
      std::abs(pixels[0].red - 0.12F) > 1e-6F || pixels[1] != original[1] || pixels[2] != original[2]) return 1;
  const auto encoded = serialize_photo_effect_stack(std::vector{effect});
  if (!encoded) return 1;
  const auto decoded = deserialize_photo_effect_stack(*encoded);
  if (!decoded || *decoded != std::vector{effect}) return 1;
  effect.posterize_levels = 1;
  effect.enabled = false;
  if (serialize_photo_effect_stack(std::vector{effect})) return 1;
  constexpr int width = 37, height = 29;
  std::vector<ColorPixel> fixture;
  std::vector<float> coverage;
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      fixture.push_back({static_cast<float>((x * 13 + y * 7) % 101) / 50.0F - 0.5F,
          static_cast<float>((x * 3 + y * 17) % 83) / 80.0F,
          static_cast<float>((x * 11 + y) % 61) / 40.0F,
          static_cast<float>((x + y * 3) % 5) / 4.0F});
      coverage.push_back(static_cast<float>((x * 5 + y) % 7) / 6.0F);
    }
  }
  PhotoEffectInstance blur;
  blur.id = "blur";
  blur.gaussian.sigma = 1.3;
  effect.enabled = true;
  effect.posterize_levels = 5;
  const std::vector<PhotoEffectMaskBinding> masks{{"coverage", coverage}};
  for (const auto edge : {PhotoEffectEdgeMode::Transparent, PhotoEffectEdgeMode::Extend, PhotoEffectEdgeMode::Reflect}) {
    blur.gaussian.edge_mode = edge;
    const std::vector stack{blur, effect};
    auto full = fixture;
    auto tiled = fixture;
    if (!apply_photo_effect_stack(&full, width, height, stack, {}, masks).ok() ||
        !apply_photo_effect_stack_tiled(&tiled, width, height, stack, {}, masks, 8).ok()) return 1;
    for (std::size_t i = 0; i < full.size(); ++i) {
      if (std::abs(full[i].red - tiled[i].red) > 1e-6F ||
          std::abs(full[i].green - tiled[i].green) > 1e-6F ||
          std::abs(full[i].blue - tiled[i].blue) > 1e-6F ||
          std::abs(full[i].alpha - tiled[i].alpha) > 1e-6F) {
        std::cerr << "Masked blur/posterize stack must agree across tile boundaries\n";
        return 1;
      }
    }
    auto reversed = fixture;
    if (!apply_photo_effect_stack(&reversed, width, height, std::vector{effect, blur}, {}, masks).ok() || reversed == full)
      return 1;
    auto invalid = effect;
    invalid.posterize_levels = 1;
    auto rejected = fixture;
    if (apply_photo_effect_stack(&rejected, width, height, std::vector{blur, invalid}, {}, masks).ok() ||
        rejected != fixture) return 1;
  }
  std::cout << "Posterize boundaries, HDR, alpha, coverage and codec passed\n";
}
