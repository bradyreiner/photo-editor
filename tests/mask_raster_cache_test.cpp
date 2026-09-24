#include "layer_selection_mask_cache.h"
#include "color_mask_runtime.h"
#include "photo_editor/pipeline/local_mask_sampling.h"

#include <algorithm>
#include <cmath>
#include <chrono>
#include <future>
#include <iostream>
#include <vector>

using namespace photo_editor::pipeline;
using namespace photo_editor::desktop::detail;

// Keep the uncached per-pixel evaluator as an independent oracle.
static bool check(const LocalMaskSet& mask, int width, int height, bool with_source) {
  const auto count = static_cast<std::size_t>(width * height);
  std::vector<ColorPixel> source;
  std::vector<ColorPixel> edge;
  if (with_source) {
    for (std::size_t i = 0; i < count; ++i) {
      const auto value = static_cast<float>(i % 47) / 46.0F;
      source.push_back({value, 1.0F - value, value * 0.4F, 1.0F});
      edge.push_back({1.0F - value, value, 0.2F, 1.0F});
    }
  }
  edge = source;
  ColorAdjustmentState state;
  const auto context = make_pipeline_color_context(state);
  const EdgeAwareMatteContext edge_context{&edge, width, height};
  const ColorPixel neutral{0.5F, 0.5F, 0.5F, 1.0F};
  std::vector<std::uint8_t> expected(count);
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const auto i = static_cast<std::size_t>(y * width + x);
      const auto weight = local_mask_set_weight(
          width > 1 ? double(x) / double(width - 1) : 0.5,
          height > 1 ? double(y) / double(height - 1) : 0.5,
          with_source ? source[i] : neutral, context, mask,
          with_source ? &edge_context : nullptr);
      expected[i] = static_cast<std::uint8_t>(
          std::llround(std::clamp(weight, 0.0, 1.0) * 255.0));
    }
  }
  const auto render = [&] {
    return rasterize_layer_selection_alpha(mask, width, height, source, state.color_pipeline);
  };
  if (render() != expected) return false;
  // Repeat with different source colors to catch an incorrectly admitted cache key.
  for (auto& pixel : source) pixel = {0.02F, 0.8F, 0.1F, 1.0F};
  edge = source;
  const auto actual = render();
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const auto i = static_cast<std::size_t>(y * width + x);
      const auto weight = local_mask_set_weight(
          width > 1 ? double(x) / double(width - 1) : 0.5,
          height > 1 ? double(y) / double(height - 1) : 0.5,
          with_source ? source[i] : neutral, context, mask,
          with_source ? &edge_context : nullptr);
      if (actual[i] != static_cast<std::uint8_t>(
              std::llround(std::clamp(weight, 0.0, 1.0) * 255.0))) return false;
    }
  }
  return true;
}

int main(int argc, char**) {
  if (argc > 1) {
    LocalMaskSet mask;
    mask.sources.resize(2);
    mask.sources[1].kind = "radial";
    mask.sources[1].operation = "subtract";
    std::vector<double> samples;
    std::vector<std::uint8_t> reference;
    for (int i = 0; i < 6; ++i) {
      const auto start = std::chrono::steady_clock::now();
      const auto alpha = rasterize_layer_selection_alpha(
          mask, 1536, 1024, {}, ColorAdjustmentState{}.color_pipeline);
      const auto elapsed = std::chrono::duration<double, std::milli>(
          std::chrono::steady_clock::now() - start).count();
      if (i == 0) reference = alpha;
      else {
        if (alpha != reference) return 1;
        samples.push_back(elapsed);
      }
    }
    std::sort(samples.begin(), samples.end());
    std::cout << "repeated_mask_p50_ms=" << samples[2] << '\n';
    return 0;
  }
  MaskRasterCache cache;
  LocalMaskSet key;
  key.sources.resize(1);
  const std::vector<std::uint8_t> large(17U * 1024U * 1024U, 17);
  for (int i = 0; i < 8; ++i) {
    key.id = std::to_string(i);
    cache.insert(key, 1024, 1024, large);
    if (cache.retained_bytes() > MaskRasterCache::kByteLimit || !cache.find(key, 1024, 1024)) return 1;
  }
  if (cache.find(key, 1023, 1024)) return 1;
  key.id = "0";
  if (cache.find(key, 1024, 1024)) return 1;
  key.id = "7";
  key.sources[0].position += 0.1;
  if (cache.find(key, 1024, 1024)) return 1;

  LocalMask linear;
  linear.id = "gradient";
  linear.rotation = 31.0;
  LocalMask radial;
  radial.kind = "radial";
  radial.operation = "subtract";
  radial.center_x = 0.37;
  LocalMask range;
  range.kind = "luminance";
  range.operation = "intersect";
  LocalMask segmentation;
  segmentation.kind = "subject";
  segmentation.segmentation_width = 3;
  segmentation.segmentation_height = 2;
  segmentation.segmentation_mask = {0, 64, 255, 220, 128, 0};
  segmentation.selection_fine_edge = 0.4;
  segmentation.selection_feather = 0.2;
  std::vector<LocalMaskSet> cases(5);
  cases[1].sources = {linear, radial, range};
  cases[2].sources = {segmentation};
  cases[2].selection_invert = true;
  cases[2].opacity = 0.7;
  cases[3] = cases[1];
  cases[3].enabled = false;
  range.parent_id = linear.id;
  cases[4].sources = {linear, range, segmentation};

  LocalMask brush;
  brush.kind = "brush";
  brush.segmentation_width = 3;
  brush.segmentation_height = 2;
  brush.segmentation_mask = {0, 64, 255, 220, 128, 0};
  LocalMaskSet painted;
  painted.sources = {linear, brush};
  cases.push_back(painted);
  for (auto field : {&LocalMask::selection_fine_edge, &LocalMask::selection_feather,
                    &LocalMask::selection_blur, &LocalMask::refine_luminance_min,
                    &LocalMask::refine_saturation_min, &LocalMask::refine_depth_min}) {
    auto changed = painted;
    changed.sources[1].*field = 0.5;
    if (mask_raster_source_independent(changed)) return 1;
    cases.push_back(changed);
  }
  for (const auto kind : {"color", "luminance", "depth", "subject", "unknown"}) {
    auto changed = painted;
    changed.sources[1].kind = kind;
    if (mask_raster_source_independent(changed)) return 1;
    cases.push_back(changed);
  }
  for (int i = 0; i < 5; ++i) {
    auto changed = painted;
    changed.sources[1].segmentation_mask[0] = static_cast<std::uint8_t>(30 * i);
    changed.sources[1].operation = i % 2 == 0 ? "subtract" : "intersect";
    changed.selection_invert = i % 2 == 0;
    changed.opacity = 0.2 * i;
    cases.push_back(changed);
  }
  for (const auto& mask : cases) {
    for (const auto dimensions : {std::pair{1, 1}, std::pair{19, 7}, std::pair{61, 37}}) {
      for (const bool with_source : {false, true}) {
        if (!check(mask, dimensions.first, dimensions.second, with_source)) {
          std::cerr << "Cached mask differs from serial oracle\n";
          return 1;
        }
      }
    }
  }
  if (!rasterize_local_mask_set_alpha(cases[1], 0, 12, {}).empty()) return 1;
  std::vector<std::future<bool>> workers;
  for (int i = 0; i < 4; ++i)
    workers.push_back(std::async(std::launch::async, [painted] { return check(painted, 83, 67, true); }));
  for (auto& worker : workers) if (!worker.get()) return 1;
  std::cout << "Mask cache matches scalar oracle across " << cases.size()
            << " mask variants, source changes, sizes, eviction and concurrent workers\n";
}
