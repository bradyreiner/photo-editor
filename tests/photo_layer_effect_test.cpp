#include "photo_editor/pipeline/color_adjustments.h"
#include "photo_editor/pipeline/color_fill_layer_program.h"
#include "photo_editor/pipeline/photo_effects.h"
#include "photo_editor/pipeline/cpu_work_scheduler.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <iostream>
#include <cstring>
#include <fstream>
#include <limits>

using namespace photo_editor::pipeline;

int emit_stream_cases(const char* path) {
  std::ofstream output(path);
  if (!output) return 1;
  constexpr int w = 529, h = 271;
  std::vector<ColorPixel> source(static_cast<std::size_t>(w) * h);
  std::vector<float> coverage(source.size());
  for (std::size_t i = 0; i < source.size(); ++i) {
    source[i] = {static_cast<float>(i % 19U) / 5 - .2F, .3F, 1.7F,
        static_cast<float>(i % 7U) / 6};
    coverage[i] = static_cast<float>(i % 9U) / 8;
  }
  for (const auto* type : {"gaussian_blur", "optical_diffusion"})
    for (const auto* blend : {"normal", "multiply", "screen", "overlay"})
      for (const bool masked : {false, true})
        for (const bool clipped : {false, true}) {
          PhotoEffectInstance effect;
          effect.id = "matrix"; effect.type_id = type;
          effect.input_scope = PhotoEffectInputScope::CompositeBelow;
          effect.gaussian.sigma = 2;
          effect.diffusion.radius = 8;
          effect.amount = .73;
          if (masked) effect.mask_ref = "coverage";
          PhotoLayer adjustment;
          adjustment.id = "matrix-layer";
          adjustment.kind = "adjustment";
          adjustment.opacity = .63;
          adjustment.blend_mode = blend;
          adjustment.clipping_mask = clipped;
          adjustment.effects = {effect};
          if (masked) {
            adjustment.effect_masks = {{"coverage", coverage}};
            adjustment.mask_width = w; adjustment.mask_height = h;
            adjustment.mask_data.resize(source.size());
            for (std::size_t i = 0; i < source.size(); ++i)
              adjustment.mask_data[i] = static_cast<uint8_t>(i % 256U);
          }
          PhotoLayer backing;
          backing.kind = "fill"; backing.fill_color = {.4F, .5F, .6F, .35F};
          ColorAdjustmentState state;
          state.photo_layers = clipped ? std::vector<PhotoLayer>{backing, adjustment}
                                       : std::vector<PhotoLayer>{adjustment};
          auto pixels = source;
          if (!apply_color_layers_stage(&pixels, w, h, state)) return 1;
          std::uint64_t hash = 14695981039346656037ULL;
          for (const auto& pixel : pixels)
            for (const auto channel : {pixel.red, pixel.green, pixel.blue, pixel.alpha}) {
              std::uint32_t bits = 0;
              std::memcpy(&bits, &channel, sizeof(bits));
              for (int byte = 0; byte < 4; ++byte) {
                hash ^= (bits >> (byte * 8)) & 255U;
                hash *= 1099511628211ULL;
              }
            }
          output << type << ' ' << blend << ' ' << masked << ' ' << clipped << ' ' << hash << '\n';
        }
  return output.good() ? 0 : 1;
}

int large_stream_contract() {
  constexpr int width = 7500, height = 6000;
  const ColorPixel value{.4F, .2F, .7F, .5F};
  PhotoEffectInstance effect;
  effect.id = "45mp-invert"; effect.type_id = "invert_linear_rgb";
  effect.input_scope = PhotoEffectInputScope::CompositeBelow;
  std::vector<ColorPixel> expected{value};
  const auto oracle = apply_photo_effect(&expected, 1, 1, effect);
  if (!oracle.ok()) {
    std::cerr << "45 MP oracle effect failed: " << oracle.error << '\n';
    return 1;
  }
  PhotoLayer layer;
  layer.kind = "adjustment"; layer.effects = {effect};
  ColorAdjustmentState state;
  state.photo_layers = {layer};
  std::vector<ColorPixel> pixels(static_cast<std::size_t>(width) * height, value);
  if (!apply_color_layers_stage(&pixels, width, height, state) ||
      !std::all_of(pixels.begin(), pixels.end(), [&](const auto& pixel) { return pixel == expected[0]; })) {
    std::cerr << "45 MP adjustment effect did not preserve exact expected pixels\n";
    return 1;
  }
  std::cout << "45 MP adjustment effect passed with default tile scratch budget\n";
  return 0;
}

int parallel_content_contracts() {
  constexpr int width = 384, height = 192; // Above the parallel content threshold.
  std::vector<ColorPixel> source(static_cast<std::size_t>(width) * height);
  for (std::size_t i = 0; i < source.size(); ++i)
    source[i] = {static_cast<float>(i % 17U) / 5 - .2F, .4F, 1.3F,
        static_cast<float>(i % 11U) / 10};
  for (const auto* kind : {"pixel", "source", "text"}) {
    for (const bool transformed : {false, true}) {
      PhotoLayer content;
      content.kind = kind; content.pixels = source;
      content.pixel_width = width; content.pixel_height = height;
      content.x = -7; content.y = 5;
      content.opacity = .63; content.blend_mode = "screen";
      content.clipping_mask = true;
      content.mask_width = width; content.mask_height = height;
      content.mask_data.resize(source.size());
      for (std::size_t i = 0; i < source.size(); ++i)
        content.mask_data[i] = static_cast<std::uint8_t>(i % 256U);
      if (transformed) { content.transform_scale = .73; content.transform_rotation = 17; }
      if (content.kind == "text") {
        content.text_material = "frosted_glass";
        content.text_material_blur = 3;
      }
      PhotoLayer lower;
      lower.kind = "fill"; lower.fill_color = {.2F, .3F, .4F, .7F};
      ColorAdjustmentState state;
      state.photo_layers = {lower, content};
      auto expected = source;
      if (!apply_color_layers_stage(&expected, width, height, state)) return 1;
      std::atomic_bool exact{true};
      // Nested calls run rows inline on the shared worker, exercising the
      // serial fallback concurrently against the ordinary parallel result.
      run_cpu_parallel_for(16, [&](int) {
        auto pixels = source;
        if (!apply_color_layers_stage(&pixels, width, height, state) || pixels != expected)
          exact.store(false);
      });
      if (!exact.load()) {
        std::cerr << "Parallel content differs from nested serial composition\n";
        return 1;
      }
    }
  }
  return 0;
}

int main(int argc, char** argv) {
  if (argc == 2 && std::string(argv[1]) == "--large-stream") return large_stream_contract();
  if (argc == 2) return emit_stream_cases(argv[1]);
  if (parallel_content_contracts() != 0) return 1;
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
  // A later tile fails after earlier tiles have written into the private
  // compositor transaction. None of those writes may reach the caller.
  {
    constexpr int late_width = 769, late_height = 17;
    std::vector<ColorPixel> original(static_cast<std::size_t>(late_width) * late_height,
        {0.4F, 0.3F, 0.2F, 1});
    original.back().red = std::numeric_limits<float>::max();
    auto work = original;
    PhotoEffectInstance diffusion;
    diffusion.id = "late-failure";
    diffusion.type_id = "optical_diffusion";
    diffusion.input_scope = PhotoEffectInputScope::CompositeBelow;
    diffusion.diffusion.radius = 4;
    diffusion.diffusion.warmth = 1;
    PhotoLayer adjustment;
    adjustment.kind = "adjustment";
    adjustment.effects = {diffusion};
    ColorAdjustmentState failure_state;
    failure_state.photo_layers = {adjustment};
    if (apply_color_layers_stage(&work, late_width, late_height, failure_state) || work != original) {
      std::cerr << "Late tile failure leaked partial layer output\n";
      return 1;
    }
  }
  // Content effects can modify their prepared private rows before a later
  // band fails. Neither live content nor the composited caller may change.
  {
    constexpr int w = 17, h = 769;
    std::vector<ColorPixel> original(static_cast<std::size_t>(w) * h, {.4F, .3F, .2F, 1});
    PhotoLayer content;
    content.kind = "pixel"; content.pixel_width = w; content.pixel_height = h;
    content.pixels = original;
    content.pixels.back().red = std::numeric_limits<float>::max();
    PhotoEffectInstance diffusion;
    diffusion.id = "late-content-failure"; diffusion.type_id = "optical_diffusion";
    diffusion.diffusion.radius = 4; diffusion.diffusion.warmth = 1;
    content.effects = {diffusion};
    ColorAdjustmentState failure_state;
    failure_state.photo_layers = {content};
    auto work = original;
    if (apply_color_layers_stage(&work, w, h, failure_state) || work != original ||
        failure_state.photo_layers.front().pixels != content.pixels) {
      std::cerr << "Late content failure leaked private effect rows\n";
      return 1;
    }
  }
  std::cout << "Composite and cutout blur, fast-path admission and rollback passed\n";
}
