#include "photo_editor/pipeline/photo_effects.h"
#include "photo_editor/pipeline/render_tile_planner.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>

using namespace photo_editor::pipeline;

namespace {
bool close(ColorPixel a, ColorPixel b, double tolerance = 2e-6) {
  return std::max({std::abs(a.red - b.red), std::abs(a.green - b.green),
      std::abs(a.blue - b.blue), std::abs(a.alpha - b.alpha)}) <= tolerance;
}

// Deliberately direct 2D convolution, independent of the separable implementation.
ColorPixel reference(const std::vector<ColorPixel>& input, int width, int height,
    int x, int y, double sigma, double amount, double coverage, bool uniform = false) {
  const auto radius = static_cast<int>(std::ceil(3 * sigma));
  std::array<double, 4> sum{};
  double total = 0;
  for (int dy = -radius; dy <= radius; ++dy) {
    for (int dx = -radius; dx <= radius; ++dx) {
      const auto weight = uniform ? 1.0 : std::exp(-static_cast<double>(dx * dx + dy * dy) / (2 * sigma * sigma));
      total += weight;
      if (x + dx < 0 || x + dx >= width || y + dy < 0 || y + dy >= height) continue;
      const auto pixel = input[static_cast<std::size_t>((y + dy) * width + x + dx)];
      sum[0] += weight * pixel.red * pixel.alpha;
      sum[1] += weight * pixel.green * pixel.alpha;
      sum[2] += weight * pixel.blue * pixel.alpha;
      sum[3] += weight * pixel.alpha;
    }
  }
  for (auto& channel : sum) channel /= total;
  const auto base = input[static_cast<std::size_t>(y * width + x)];
  const auto mix = amount * coverage;
  const auto alpha = base.alpha * (1 - mix) + sum[3] * mix;
  if (alpha == 0) return {0, 0, 0, 0};
  return {static_cast<float>((base.red * base.alpha * (1 - mix) + sum[0] * mix) / alpha),
      static_cast<float>((base.green * base.alpha * (1 - mix) + sum[1] * mix) / alpha),
      static_cast<float>((base.blue * base.alpha * (1 - mix) + sum[2] * mix) / alpha),
      static_cast<float>(alpha)};
}
}

int main() {
  const auto fail = [](const char* reason) { std::cerr << reason << '\n'; return 1; };
  PhotoEffectInstance effect;
  effect.gaussian.sigma = 0.75;
  effect.amount = 0.6;
  std::vector<ColorPixel> source{
      {2, -0.5F, 0.2F, 1}, {100, 0, 100, 0}, {0, 1, 0.5F, 0.3F},
      {0.2F, 0.4F, 1, 0.5F}, {0, 0, 0, 0}, {1, 2, 3, 1}};
  const std::vector<float> mask{1, 0.2F, 0, 0.5F, 1, 1};
  auto actual = source;
  if (!apply_photo_effect(&actual, 3, 2, effect, {}, mask).ok()) return fail("Gaussian failed");
  for (int y = 0; y < 2; ++y) {
    for (int x = 0; x < 3; ++x) {
      const auto i = static_cast<std::size_t>(y * 3 + x);
      if (!close(actual[i], reference(source, 3, 2, x, y, effect.gaussian.sigma, effect.amount, mask[i])))
        return fail("Gaussian disagrees with independent masked premultiplied 2D reference");
    }
  }
  if (actual[2] != source[2]) return fail("Zero mask coverage must preserve exact input");

  for (const auto mode : {PhotoEffectEdgeMode::Extend, PhotoEffectEdgeMode::Reflect}) {
    effect.gaussian.edge_mode = mode;
    effect.gaussian.sigma = 4;
    actual.assign(6, {-2, 8, 0.5F, 0.35F});
    if (!apply_photo_effect(&actual, 2, 3, effect).ok()) return fail("Constant blur failed");
    for (const auto pixel : actual)
      if (!close(pixel, {-2, 8, 0.5F, 0.35F})) return fail("Constant HDR field changed");
    actual.assign(1, {2, 3, 4, 0.3F});
    if (!apply_photo_effect(&actual, 1, 1, effect).ok() || !close(actual[0], {2, 3, 4, 0.3F}))
      return fail("One-pixel boundary behavior failed");
  }

  effect.gaussian.edge_mode = PhotoEffectEdgeMode::Transparent;
  effect.gaussian.sigma = 1;
  effect.amount = 1;
  actual.assign(81, {999, -999, 999, 0});
  actual[40] = {2, 0.25F, -0.5F, 1};
  if (!apply_photo_effect(&actual, 9, 9, effect).ok()) return fail("Impulse blur failed");
  double energy = 0;
  for (std::size_t i = 0; i < actual.size(); ++i) {
    const auto pixel = actual[i];
    energy += pixel.alpha;
    if (pixel.alpha > 0 && (!close(pixel, {2, 0.25F, -0.5F, pixel.alpha}) ||
        !close(pixel, actual[actual.size() - i - 1]))) return fail("Impulse fringe or asymmetry");
  }
  if (std::abs(energy - 1) > 1e-6) return fail("Gaussian loses normalized kernel energy");

  PhotoEffectSupport support;
  PhotoEffectRenderOptions options;
  options.scale_x = 0.5;
  options.scale_y = 2;
  if (!photo_effect_support(effect, options, &support).ok() || support.x != 2 || support.y != 6)
    return fail("Support ignores anisotropic preview scale");

  // A padded crop must produce the same output as full-frame processing.
  constexpr int width = 19, height = 13;
  source.clear();
  for (int i = 0; i < width * height; ++i) {
    source.push_back({static_cast<float>(i % 11) / 3, static_cast<float>(i % 5) - 1,
        0.2F, static_cast<float>(i % 7) / 6});
  }
  for (const auto mode : {PhotoEffectEdgeMode::Transparent, PhotoEffectEdgeMode::Extend, PhotoEffectEdgeMode::Reflect}) {
    effect.gaussian.edge_mode = mode;
    actual = source;
    if (!apply_photo_effect(&actual, width, height, effect).ok()) return fail("Full blur failed");
    const auto tiles = plan_render_tiles({.image_width = width, .image_height = height,
        .tile_size = 5, .padding_px = 3});
    for (const auto& tile : tiles) {
      const auto& p = tile.padded_rect;
      const auto& o = tile.output_rect;
      std::vector<ColorPixel> crop;
      for (int y = p.y; y < p.y + p.height; ++y)
        for (int x = p.x; x < p.x + p.width; ++x)
          crop.push_back(source[static_cast<std::size_t>(y * width + x)]);
      if (!apply_photo_effect(&crop, p.width, p.height, effect).ok()) return fail("Tile blur failed");
      for (int y = o.y; y < o.y + o.height; ++y)
        for (int x = o.x; x < o.x + o.width; ++x)
          if (!close(actual[static_cast<std::size_t>(y * width + x)],
                  crop[static_cast<std::size_t>((y - p.y) * p.width + x - p.x)]))
            return fail("Tile boundary differs from full-frame Gaussian");
    }
  }

  const auto unchanged = source;
  effect.gaussian.sigma = 0;
  if (apply_photo_effect(&source, width, height, effect).status != PhotoEffectStatus::Identity || source != unchanged)
    return fail("Zero radius is not exact identity");
  effect.type_id = "future_effect";
  if (apply_photo_effect(&source, width, height, effect).status != PhotoEffectStatus::Unsupported || source != unchanged)
    return fail("Unknown active effect was silently omitted");
  effect.enabled = false;
  if (!apply_photo_effect(&source, width, height, effect).ok() || source != unchanged)
    return fail("Disabled unknown effect must remain harmless");
  effect = {};
  options = {};
  options.max_scratch_bytes = 1;
  if (apply_photo_effect(&source, width, height, effect, options).status != PhotoEffectStatus::ResourceLimit || source != unchanged)
    return fail("Scratch memory budget ignored");
  options = {};
  std::atomic_bool cancel{true};
  options.cancellation.cancellation_requested = &cancel;
  if (apply_photo_effect(&source, width, height, effect, options).status != PhotoEffectStatus::Cancelled || source != unchanged)
    return fail("Cancellation changed input");
  effect.gaussian.sigma = std::numeric_limits<double>::quiet_NaN();
  if (apply_photo_effect(&source, width, height, effect).status != PhotoEffectStatus::Invalid || source != unchanged)
    return fail("Nonfinite sigma accepted");
  effect = {};
  effect.mask_ref = "unresolved-mask";
  if (apply_photo_effect(&source, width, height, effect).status != PhotoEffectStatus::Invalid || source != unchanged)
    return fail("Missing mask silently ignored");
  effect.enabled = false;
  if (!apply_photo_effect(&source, width, height, effect).ok() || source != unchanged)
    return fail("Disabled effect must not require its mask dependency");

  std::vector<PhotoEffectInstance> effects(2);
  effects[0].id = "blur-a";
  effects[0].gaussian.sigma = 1;
  effects[1].id = "blur-b";
  effects[1].gaussian.sigma = 2;
  effects[1].gaussian.edge_mode = PhotoEffectEdgeMode::Extend;
  if (!photo_effect_stack_support(effects, {}, &support).ok() || support.x != 9 || support.y != 9)
    return fail("Stack support does not accumulate downstream halos");
  actual = source;
  if (!apply_photo_effect_stack(&actual, width, height, effects).ok()) return fail("Stack failed");
  auto ordered = source;
  for (const auto& entry : effects)
    if (!apply_photo_effect(&ordered, width, height, entry).ok()) return fail("Ordered reference failed");
  if (actual != ordered) return fail("Stack does not execute in array order");
  std::reverse(effects.begin(), effects.end());
  auto reversed = source;
  if (!apply_photo_effect_stack(&reversed, width, height, effects).ok() || reversed == actual)
    return fail("Effect reorder did not change edge behavior");
  effects[1].mask_ref = "missing";
  if (apply_photo_effect_stack(&source, width, height, effects).status != PhotoEffectStatus::Invalid || source != unchanged)
    return fail("Later missing mask published a partially executed stack");
  effects[1].mask_ref.clear();
  effects[1].algorithm_version = 99;
  if (apply_photo_effect_stack(&source, width, height, effects).status != PhotoEffectStatus::Unsupported || source != unchanged)
    return fail("Later unsupported effect modified input");
  effects[1].algorithm_version = 1;
  effects[1].id = effects[0].id;
  if (apply_photo_effect_stack(&source, width, height, effects).status != PhotoEffectStatus::Invalid)
    return fail("Duplicate effect ID accepted");
  effects[1].id = "unique";
  effects[0].mask_ref = "zero";
  effects[1].enabled = false;
  const std::vector<float> zero_mask(source.size(), 0);
  const std::array bindings{PhotoEffectMaskBinding{"zero", zero_mask}};
  if (!apply_photo_effect_stack(&source, width, height, effects, {}, bindings).ok() || source != unchanged)
    return fail("Named effect mask was not resolved");
  // Exercise the production tile executor with two ordered operators and spatial
  // mask variation crossing tile boundaries, including the image edges.
  effects[0].mask_ref = "coverage";
  effects[0].gaussian.sigma = 0.7;
  effects[1].enabled = true;
  effects[1].gaussian.sigma = 1.2;
  std::vector<float> coverage(source.size());
  for (std::size_t i = 0; i < coverage.size(); ++i)
    coverage[i] = static_cast<float>(i % 11) / 10.0F;
  const std::array tile_bindings{PhotoEffectMaskBinding{"coverage", coverage}};
  for (const auto mode : {PhotoEffectEdgeMode::Transparent, PhotoEffectEdgeMode::Extend,
           PhotoEffectEdgeMode::Reflect}) {
    effects[0].gaussian.edge_mode = mode;
    auto full = source;
    auto tiled = source;
    if (!apply_photo_effect_stack(&full, width, height, effects, {}, tile_bindings).ok() ||
        !apply_photo_effect_stack_tiled(&tiled, width, height, effects, {}, tile_bindings, 4).ok())
      return fail("Automatic tiled stack failed");
    for (std::size_t i = 0; i < full.size(); ++i)
      if (!close(full[i], tiled[i])) return fail("Automatic tiled masked stack has seams");
  }
  effects[1].mask_ref = "missing";
  if (apply_photo_effect_stack_tiled(&source, width, height, effects, {}, tile_bindings, 4).status !=
          PhotoEffectStatus::Invalid || source != unchanged)
    return fail("Tiled failure published partial pixels");
  effects[1].mask_ref.clear();
  PhotoEffectRenderOptions limited;
  limited.max_scratch_bytes = source.size() * sizeof(ColorPixel);
  if (apply_photo_effect_stack_tiled(&source, width, height, effects, limited, tile_bindings, 4).status !=
          PhotoEffectStatus::ResourceLimit || source != unchanged)
    return fail("Tiled scratch limit changed input");
  PhotoEffectInstance invert;
  PhotoEffectInstance box;
  box.id = "box";
  box.type_id = "box_blur";
  box.box.radius = 1;
  std::vector<ColorPixel> box_impulse(25, {0, 0, 0, 0});
  box_impulse[12] = {1, 0.5F, 0.25F, 1};
  if (!apply_photo_effect(&box_impulse, 5, 5, box).ok()) return fail("Box blur failed");
  for (int y = 0; y < 5; ++y) for (int x = 0; x < 5; ++x) {
    const auto alpha = x >= 1 && x <= 3 && y >= 1 && y <= 3 ? 1.0F / 9.0F : 0.0F;
    if (std::abs(box_impulse[static_cast<std::size_t>(y * 5 + x)].alpha - alpha) > 1e-6F)
      return fail("Box impulse must have uniform square support");
  }
  box.box.radius = 2;
  {
    auto bounded = source;
    PhotoEffectRenderOptions exact_budget;
    exact_budget.max_scratch_bytes = source.size() * 2 * sizeof(ColorPixel);
    if (!apply_photo_effect(&bounded, width, height, box, exact_budget).ok())
      return fail("Running box blur should fit exactly two scratch rasters");
    bounded = source;
    --exact_budget.max_scratch_bytes;
    if (apply_photo_effect(&bounded, width, height, box, exact_budget).status != PhotoEffectStatus::ResourceLimit ||
        bounded != source) return fail("Box scratch budget must reject without changing input");
  }
  box.amount = 0.7;
  box.mask_ref = "coverage";
  for (const auto mode : {PhotoEffectEdgeMode::Transparent, PhotoEffectEdgeMode::Extend,
           PhotoEffectEdgeMode::Reflect}) {
    box.box.edge_mode = mode;
    const std::vector box_stack{box};
    auto box_full = source;
    auto box_tiled = source;
    if (!apply_photo_effect_stack(&box_full, width, height, box_stack, {}, tile_bindings).ok() ||
        !apply_photo_effect_stack_tiled(&box_tiled, width, height, box_stack, {}, tile_bindings, 4).ok())
      return fail("Masked box stack failed");
    for (std::size_t i = 0; i < box_full.size(); ++i)
      if (!close(box_full[i], box_tiled[i])) return fail("Masked box blur has tile seams");
    if (mode == PhotoEffectEdgeMode::Transparent)
      for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
        const auto i = static_cast<std::size_t>(y * width + x);
        if (!close(box_full[i], (coverage[i] == 0 ? source[i] : reference(source, width, height, x, y, 2.0 / 3.0, box.amount, coverage[i], true))))
          return fail("Running box sum differs from independent direct convolution");
      }
  }
  invert.id = "invert";
  invert.type_id = "invert_linear_rgb";
  std::vector<ColorPixel> colors{{0.25F, 2, -1, 0.5F}, {4, -2, 1, 0}};
  const auto original_colors = colors;
  if (!apply_photo_effect(&colors, 2, 1, invert).ok() ||
      colors[0] != ColorPixel{0.75F, -1, 2, 0.5F} || colors[1] != original_colors[1])
    return fail("Linear invert must preserve HDR range, alpha and hidden transparent RGB");
  if (!apply_photo_effect(&colors, 2, 1, invert).ok() || colors != original_colors)
    return fail("Two full linear inversions must restore these exact fixture values");
  invert.amount = 0.5;
  if (!apply_photo_effect(&colors, 2, 1, invert).ok() || colors[0] != ColorPixel{0.5F, 0.5F, 0.5F, 0.5F})
    return fail("Linear invert amount must interpolate in working RGB");
  invert.amount = 1;
  PhotoEffectInstance mixed_blur;
  mixed_blur.id = "mixed-blur";
  mixed_blur.gaussian.sigma = 1;
  std::vector<PhotoEffectInstance> mixed_stack{invert, mixed_blur};
  auto mixed_full = source;
  auto mixed_tiles = source;
  if (!apply_photo_effect_stack(&mixed_full, width, height, mixed_stack).ok() ||
      !apply_photo_effect_stack_tiled(&mixed_tiles, width, height, mixed_stack, {}, {}, 4).ok())
    return fail("Mixed invert/blur stack must render");
  for (std::size_t i = 0; i < mixed_full.size(); ++i)
    if (!close(mixed_full[i], mixed_tiles[i])) return fail("Mixed filter stack has tile seams");
  std::cout << "Gaussian reference, alpha, HDR, scale, tile, identity and failure checks passed\n";
}
