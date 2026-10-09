#include "color_adjustment_kernel_runtime.h"
#include "color_layer_runtime.h"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string_view>
#include <vector>

using namespace photo_editor::pipeline;

static bool identical(const std::vector<ColorPixel>& a, const std::vector<ColorPixel>& b) {
  if (a.size() != b.size()) return false;
  for (std::size_t i = 0; i < a.size(); ++i)
    if (std::bit_cast<std::array<std::uint32_t, 4>>(a[i]) !=
        std::bit_cast<std::array<std::uint32_t, 4>>(b[i])) return false;
  return true;
}

static std::vector<ColorPixel> render(const std::vector<ColorPixel>& source, int width, int height,
    const std::vector<PhotoLayer>& layers, bool optimized, std::size_t* calls = nullptr) {
  ColorAdjustmentState state;
  const auto context = make_pipeline_color_context(state);
  auto pixels = source;
  if (optimized && calls == nullptr) {
    apply_standard_photo_layers(&pixels, width, height, context,
        state.color_pipeline.photographic_rendering_model, layers);
    return pixels;
  }
  apply_photo_layers(&pixels, width, height, context, layers,
      [&](ColorPixel* pixel, const PhotoLayer& layer) {
        if (calls) ++*calls;
        apply_local_adjustment_mask(pixel, context, state.color_pipeline.photographic_rendering_model,
            1.0, layer.exposure, layer.contrast, layer.highlights, layer.shadows, layer.whites,
            layer.blacks, layer.temperature, layer.tint, layer.vibrance, layer.saturation,
            layer.hue_shift, layer.saturation_shift, layer.colorize_hue, layer.colorize_strength);
      }, optimized);
  return pixels;
}

static bool verify_resolved_layer_adjustments() {
  constexpr int width = 257, height = 129;
  std::vector<ColorPixel> source;
  for (int i = 0; i < width * height; ++i) {
    const auto v = static_cast<float>(i % 131) / 200.0F;
    source.push_back({v, v * 0.4F, 0.1F + v * 0.7F, 1.0F});
  }
  PhotoLayer cutout;
  cutout.kind = "pixel";
  cutout.copy_base_image = true;
  cutout.mask_width = 3;
  cutout.mask_height = 1;
  cutout.mask_data = {0, 128, 255};
  PhotoLayer grade;
  grade.kind = "adjustment";
  grade.exposure = 0.4;
  grade.temperature = 18;
  grade.contrast = -7;
  auto second = grade;
  second.exposure = -0.2;
  second.temperature = -12;
  PhotoLayer group;
  group.kind = "group";
  group.id = "group";
  group.exposure = 0.3;
  group.saturation = 12;
  auto child = grade;
  child.parent_id = group.id;
  PhotoLayer fill;
  fill.kind = "fill";
  fill.parent_id = group.id;
  fill.fill_color = {0.2F, 0.4F, 0.7F, 0.6F};
  // Linked cutouts copy the layer vector; groups synthesize an adjustment.
  // Neither callback argument belongs to the original vector. Empty IDs on
  // adjustments also ensure preparation never depends on unique persisted IDs.
  const std::array stacks{std::vector{grade, cutout, second},
      std::vector{fill, child, group, second},
      std::vector{fill, child, group, cutout, second}};
  for (std::size_t i = 0; i < stacks.size(); ++i) {
    const auto reference = render(source, width, height, stacks[i], false);
    for (int repeat = 0; repeat < 3; ++repeat) {
      if (!identical(reference, render(source, width, height, stacks[i], true))) {
        std::cerr << "Resolved layer adjustment changed scalar output in stack " << i << '\n';
        return false;
      }
    }
  }
  std::cout << "Copied cutout stacks and synthesized group adjustments match scalar pixels exactly\n";
  return true;
}

int main(int argc, char** argv) {
  if (argc > 1 && std::string_view(argv[1]) == "--copied-layer-proof")
    return verify_resolved_layer_adjustments() ? 0 : 1;
  PhotoLayer neutral;
  neutral.kind = "adjustment";
  neutral.mask_width = 3;
  neutral.mask_height = 2;
  neutral.mask_data = {0, 64, 255, 255, 128, 0};
  PhotoLayer fill;
  fill.kind = "fill";
  fill.fill_color = {0.8F, 0.2F, 0.4F, 0.7F};
  fill.clipping_mask = true;
  auto curved = neutral;
  curved.curve_shadows = 12;
  curved.curve_darks = -8;
  curved.curve_lights = 6;
  curved.curve_highlights = -10;
  curved.master_curve_points = {{0, 0}, {0.3, 0.25}, {0.7, 0.8}, {1, 1}};
  curved.luma_curve_points = {{0, 0.02}, {0.5, 0.6}, {1, 0.98}};
  curved.red_curve_points = {{0, 0}, {0.4, 0.5}, {1, 1}};
  curved.green_curve_points = {{0, 0}, {0.6, 0.5}, {1, 1}};
  curved.blue_curve_points = {{0, 0.02}, {0.5, 0.4}, {1, 0.99}};
  if (argc > 1 && std::string_view(argv[1]) == "--advanced-benchmark") {
    auto advanced = neutral;
    advanced.brightness = 12;
    advanced.filmic_toe = 0.2;
    advanced.filmic_shoulder = 0.3;
    advanced.adaptive_contrast = 15;
    advanced.levels_black_point = 0.02;
    advanced.levels_gamma = 1.15;
    advanced.levels_white_point = 0.92;
    advanced.zone_shadows = 12;
    advanced.zone_midtones = -7;
    advanced.zone_highlights = -10;
    const std::vector<ColorPixel> source(1024U * 683U, {0.1F, 0.3F, 0.6F, 0.7F});
    std::vector<double> times;
    std::vector<ColorPixel> reference;
    for (int sample = 0; sample < 5; ++sample) {
      const auto started = std::chrono::steady_clock::now();
      const auto result = render(source, 1024, 683, {advanced}, true);
      times.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now()-started).count());
      if (sample == 0) reference = result;
      else if (!identical(reference, result)) return 1;
    }
    if (!identical(reference, render(source, 1024, 683, {advanced}, false))) return 1;
    std::sort(times.begin(), times.end());
    std::cout << "advanced_layer_p50_ms=" << times[2] << " scalar_parity=exact\n";
    return 0;
  }
  if (argc > 1) {
    const std::vector<ColorPixel> source(2048U * 1365U, {0.1F, 0.3F, 0.6F, 1.0F});
    std::array<std::vector<double>, 2> times;
    for (int i = 0; i < 3; ++i) {
      for (int mode = 0; mode < 2; ++mode) {
        const auto start = std::chrono::steady_clock::now();
        if (!identical(render(source, 2048, 1365, {neutral, neutral}, mode != 0), source)) return 1;
        times[static_cast<std::size_t>(mode)].push_back(std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start).count());
      }
    }
    for (auto& samples : times) std::sort(samples.begin(), samples.end());
    std::cout << "neutral_layers_scalar_p50_ms=" << times[0][1]
              << "\nneutral_layers_optimized_p50_ms=" << times[1][1] << '\n';
    const std::vector<ColorPixel> curve_source(512U * 341U, {0.1F, 0.3F, 0.6F, 0.7F});
    times = {};
    std::vector<ColorPixel> reference;
    for (int i = 0; i < 3; ++i) {
      for (int mode = 0; mode < 2; ++mode) {
        const auto start = std::chrono::steady_clock::now();
        auto result = render(curve_source, 512, 341, {curved}, mode != 0);
        times[static_cast<std::size_t>(mode)].push_back(std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start).count());
        if (mode == 0) reference = std::move(result);
        else if (!identical(reference, result)) return 1;
      }
    }
    for (auto& samples : times) std::sort(samples.begin(), samples.end());
    std::cout << "curve_layer_scalar_p50_ms=" << times[0][1]
              << "\ncurve_layer_optimized_p50_ms=" << times[1][1] << '\n';
    auto common = neutral;
    common.exposure = 0.6;
    common.temperature = 18;
    common.contrast = 12;
    common.saturation = 15;
    times = {};
    for (int i = 0; i < 3; ++i) {
      for (int mode = 0; mode < 2; ++mode) {
        const auto start = std::chrono::steady_clock::now();
        auto result = render(curve_source, 512, 341, {common}, mode != 0);
        times[static_cast<std::size_t>(mode)].push_back(std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start).count());
        if (mode == 0) reference = std::move(result);
        else if (!identical(reference, result)) return 1;
      }
    }
    for (auto& samples : times) std::sort(samples.begin(), samples.end());
    std::cout << "common_layer_scalar_p50_ms=" << times[0][1]
              << "\ncommon_layer_optimized_p50_ms=" << times[1][1] << '\n';
    return 0;
  }

  std::vector<ColorPixel> source;
  for (int i = 0; i < 13 * 9; ++i) {
    const auto v = static_cast<float>(i % 23) / 10.0F - 0.3F;
    source.push_back({v, 1.0F - v, v * 0.4F, static_cast<float>(i % 5) / 4.0F});
  }
  source[0] = {-0.0F, 0.0F, -0.0F, -0.0F};
  std::size_t calls = 0;
  if (!identical(render(source, 13, 9, {neutral, neutral}, true, &calls), source) || calls != 0) return 1;

  std::size_t cases = 0;
  for (const auto mode : {"normal", "multiply", "screen", "color"}) {
    for (bool inverted : {false, true}) {
      for (bool clipped : {false, true}) {
        auto first = neutral;
        first.mask_inverted = inverted;
        first.blend_mode = mode;
        first.clipping_mask = clipped;
        first.mask_opacity = 0.75;
        first.mask_density = 0.65;
        auto adjusted = neutral;
        adjusted.exposure = 0.3;
        adjusted.clipping_mask = true;
        auto hidden = fill;
        hidden.enabled = false;
        PhotoLayer group;
        group.kind = "group";
        group.id = "empty-group";
        PhotoLayer invalid;
        invalid.kind = "pixel";
        for (auto layers : {std::vector{first}, std::vector{first, fill},
                 std::vector{first, hidden, group, invalid, fill, adjusted},
                 std::vector{fill, first, adjusted}}) {
          if (!identical(render(source, 13, 9, layers, false), render(source, 13, 9, layers, true))) {
            std::cerr << "Layer elision changed clipping/blending case " << cases << '\n';
            return 1;
          }
          ++cases;
        }
      }
    }
  }
  for (bool transformed : {false, true}) {
    auto image = fill;
    image.kind = "pixel";
    image.pixel_width = 13;
    image.pixel_height = 9;
    image.pixels = source;
    image.x = -2;
    image.transform_rotation = transformed ? 12.0 : 0.0;
    image.transform_scale = transformed ? 0.7 : 1.0;
    const std::vector layers{neutral, image, fill};
    if (!identical(render(source, 13, 9, layers, false), render(source, 13, 9, layers, true))) return 1;
  }
  for (int variant = 0; variant < 16; ++variant) {
    auto layer = curved;
    layer.clipping_mask = (variant & 1) != 0;
    layer.blend_mode = (variant & 2) != 0 ? "screen" : "normal";
    if ((variant & 4) != 0) {
      layer.master_curve_points = {{1, 1}, {0.3, 0.2}, {0, 0}, {0.3, 0.4}};
      layer.red_curve_points.clear();
    }
    if ((variant & 8) != 0) {
      layer.exposure = -0.7;
      layer.filmic_toe = 0.25;
    }
    const std::vector layers{neutral, layer, fill};
    if (!identical(render(source, 13, 9, layers, false), render(source, 13, 9, layers, true))) {
      std::cerr << "Compiled layer curves changed case " << variant << '\n';
      return 1;
    }
  }
  // Odd dimensions and varying pixels exercise worker row boundaries and
  // clipped alpha writes beyond the tiny composition fixtures above.
  std::vector<ColorPixel> row_source;
  for (int i = 0; i < 257 * 129; ++i) {
    const auto v = static_cast<float>(i % 251) / 100.0F - 0.3F;
    row_source.push_back({v, 1.0F - v, v * 0.4F, static_cast<float>(i % 7) / 6.0F});
  }
  auto clipped_curve = curved;
  clipped_curve.clipping_mask = true;
  const std::vector row_layers{neutral, clipped_curve, fill};
  const auto row_reference = render(row_source, 257, 129, row_layers, false);
  for (int repeat = 0; repeat < 3; ++repeat) {
    if (!identical(row_reference, render(row_source, 257, 129, row_layers, true))) {
      std::cerr << "Parallel curve rows changed clipped output\n";
      return 1;
    }
  }
  for (int variant = 0; variant < 16; ++variant) {
    auto common = neutral;
    common.exposure = 0.6;
    common.contrast = -12;
    common.temperature = 18;
    common.tint = -7;
    common.highlights = -15;
    common.shadows = 20;
    common.whites = 10;
    common.blacks = -8;
    common.vibrance = 15;
    common.saturation = -10;
    if ((variant & 1) != 0) common.hue_shift = 25;
    if ((variant & 2) != 0) { common.colorize_hue = 0.2; common.colorize_strength = 30; }
    if ((variant & 4) != 0) { common.texture = 15; common.clarity = 10; }
    if ((variant & 8) != 0) { common.filmic_toe = 0.2; common.master_curve_points = curved.master_curve_points; }
    const std::vector layers{neutral, common, fill};
    if (!identical(render(row_source, 257, 129, layers, false),
                   render(row_source, 257, 129, layers, true))) {
      std::cerr << "Prepared common adjustment changed variant " << variant << '\n';
      return 1;
    }
  }
  // Every advanced field, signs, HDR values, masks/blends and the spatial
  // prepass compare the prepared production path with the original scalar path.
  const std::array advanced_fields{&PhotoLayer::brightness, &PhotoLayer::filmic_shoulder,
      &PhotoLayer::filmic_toe, &PhotoLayer::adaptive_contrast, &PhotoLayer::levels_black_point,
      &PhotoLayer::levels_gamma, &PhotoLayer::levels_white_point, &PhotoLayer::zone_deep_shadows,
      &PhotoLayer::zone_shadows, &PhotoLayer::zone_midtones, &PhotoLayer::zone_lights,
      &PhotoLayer::zone_highlights};
  for (const auto field : advanced_fields) {
    for (const auto amount : {-65.0, 35.0, 100.0}) {
      for (bool spatial : {false, true}) {
        auto advanced = neutral;
        advanced.*field = amount;
        advanced.opacity = 0.73;
        advanced.clipping_mask = true;
        advanced.blend_mode = spatial ? "screen" : "normal";
        if (spatial) { advanced.texture = 15; advanced.clarity = 12; }
        const std::vector layers{fill, advanced};
        if (!identical(render(source, 13, 9, layers, false), render(source, 13, 9, layers, true))) {
          std::cerr << "Prepared advanced tone changed scalar pixels\n";
          return 1;
        }
      }
    }
  }
  if (!verify_resolved_layer_adjustments()) return 1;
  source[1].red = std::numeric_limits<float>::infinity();
  if (!identical(render(source, 13, 9, {neutral}, false), render(source, 13, 9, {neutral}, true))) return 1;
  std::cout << "Exact scalar/optimized layers: " << cases << " combinations, 16 curve variants, parallel rows, transformed pixels and HDR/alpha edges\n";
}
