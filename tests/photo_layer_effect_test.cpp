#include "photo_editor/pipeline/color_adjustments.h"
#include "photo_editor/pipeline/color_fill_layer_program.h"
#include "photo_editor/pipeline/photo_effects.h"

#include <algorithm>
#include <cmath>
#include <iostream>

using namespace photo_editor::pipeline;

int main() {
  constexpr int width = 19, height = 17;
  std::vector<ColorPixel> input(static_cast<std::size_t>(width * height), {0, 0, 0, 0});
  input[static_cast<std::size_t>(8 * width + 9)] = {2, 0.5F, -0.1F, 1};
  PhotoEffectInstance effect;
  effect.id = "blur";
  effect.gaussian.sigma = 1;
  effect.input_scope = PhotoEffectInputScope::CompositeBelow;
  PhotoLayer layer;
  layer.id = "composite";
  layer.kind = "adjustment";
  layer.effects = {effect};
  ColorAdjustmentState state;
  state.photo_layers = {layer};
  auto expected = input;
  if (!apply_photo_effect(&expected, width, height, effect).ok()) return 1;
  auto actual = input;
  if (!apply_color_layers_stage(&actual, width, height, state)) return 1;
  const auto matches = [](const auto& a, const auto& b) {
    for (std::size_t i = 0; i < a.size(); ++i) {
      if (std::max({std::abs(a[i].red - b[i].red), std::abs(a[i].green - b[i].green),
              std::abs(a[i].blue - b[i].blue), std::abs(a[i].alpha - b[i].alpha)}) > 1e-5F)
        return false;
    }
    return true;
  };
  if (!matches(actual, expected) || actual == input) {
    std::cerr << "Composite blur was elided or changed alpha/color semantics\n";
    return 1;
  }
  if (compile_fill_layer_program(state, width, height).ready()) {
    std::cerr << "Retained renderer admitted an unsupported spatial effect\n";
    return 1;
  }
  // A one-pixel cutout must spread beyond its original extent without moving.
  {
    auto masked_layer = layer;
    masked_layer.effects[0].mask_ref = "coverage";
    masked_layer.effects[0].amount = 0.6;
    std::vector<float> coverage(input.size());
    for (int y = 0; y < height; ++y)
      for (int x = 0; x < width; ++x)
        coverage[static_cast<std::size_t>(y * width + x)] = x < 9 ? 0.0F : 0.5F;
    masked_layer.effect_masks = {{"coverage", coverage}};
    state.photo_layers = {masked_layer};
    actual = input;
    auto masked_expected = input;
    if (!apply_photo_effect(&masked_expected, width, height, masked_layer.effects[0], {}, coverage).ok() ||
        !apply_color_layers_stage(&actual, width, height, state) || !matches(actual, masked_expected)) {
      std::cerr << "Composite effect mask or partial amount was ignored\n";
      return 1;
    }
    state.photo_layers[0].effect_masks.clear();
    actual = input;
    if (apply_color_layers_stage(&actual, width, height, state) || actual != input) {
      std::cerr << "Missing composite effect mask published pixels\n";
      return 1;
    }
  }
  layer.kind = "pixel";
  layer.x = 9;
  layer.y = 8;
  layer.pixel_width = layer.pixel_height = 1;
  layer.pixels = {{2, 0.5F, -0.1F, 1}};
  layer.effects[0].input_scope = PhotoEffectInputScope::LayerContent;
  state.photo_layers = {layer};
  actual.assign(input.size(), {0, 0, 0, 0});
  if (!apply_color_layers_stage(&actual, width, height, state) || !matches(actual, expected)) {
    std::cerr << "Cutout blur clipped its halo or shifted its position\n";
    return 1;
  }
  for (int failure = 0; failure < 3; ++failure) {
    state.photo_layers = {layer};
    auto& invalid = state.photo_layers[0].effects[0];
    if (failure == 0) invalid.type_id = "future";
    if (failure == 1) invalid.mask_ref = "missing";
    if (failure == 2) invalid.input_scope = PhotoEffectInputScope::CompositeBelow;
    actual = input;
    if (apply_color_layers_stage(&actual, width, height, state) || actual != input) {
      std::cerr << "Invalid layer effect succeeded or published partial pixels\n";
      return 1;
    }
  }
  layer.kind = "adjustment";
  layer.effects[0].input_scope = PhotoEffectInputScope::CompositeBelow;
  layer.effects[0].gaussian.sigma = 0;
  state.photo_layers = {layer};
  actual = input;
  actual[0] = {4, -2, 1, 0};
  const auto identity_input = actual;
  if (!apply_color_layers_stage(&actual, width, height, state) || actual != identity_input) {
    std::cerr << "Zero blur changed hidden RGB or visible pixels\n";
    return 1;
  }
  layer.effects[0].type_id = "invert_linear_rgb";
  state.photo_layers = {layer};
  actual.assign(input.size(), {0.25F, 0.5F, 2, 1});
  if (!apply_color_layers_stage(&actual, width, height, state) ||
      actual[0] != ColorPixel{0.75F, 0.5F, -1, 1}) {
    std::cerr << "Composite linear invert failed\n";
    return 1;
  }
  layer.kind = "pixel";
  layer.effects[0].input_scope = PhotoEffectInputScope::LayerContent;
  layer.pixel_width = 2;
  layer.pixel_height = 1;
  layer.pixels = {{0.25F, 0.5F, 2, 0.5F}, {4, -2, 1, 0}};
  state.photo_layers = {layer};
  actual.assign(input.size(), {0, 0, 0, 0});
  if (!apply_color_layers_stage(&actual, width, height, state) ||
      actual[static_cast<std::size_t>(8 * width + 9)] != ColorPixel{0.75F, 0.5F, -1, 0.5F} ||
      actual[static_cast<std::size_t>(8 * width + 10)].alpha != 0) {
    std::cerr << "Raster invert changed placement, alpha or HDR values\n";
    return 1;
  }
  layer.effects[0].type_id = "box_blur";
  layer.effects[0].box.radius = 1;
  layer.effects[0].box.edge_mode = PhotoEffectEdgeMode::Extend;
  layer.pixel_width = layer.pixel_height = 1;
  layer.pixels = {{0.25F, 0.5F, 2, 1}};
  state.photo_layers = {layer};
  actual.assign(input.size(), {0, 0, 0, 0});
  if (!apply_color_layers_stage(&actual, width, height, state) ||
      actual[static_cast<std::size_t>(8 * width + 9)] != layer.pixels[0] ||
      actual[static_cast<std::size_t>(8 * width + 8)].alpha != 0) {
    std::cerr << "Box extend mode must retain constant cutout content without expanding bounds\n";
    return 1;
  }
  std::cout << "Composite and cutout blur, fast-path admission and rollback passed\n";
}
