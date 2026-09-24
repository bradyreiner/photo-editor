#include "color_mask_coverage_cache.h"
#include <atomic>
#include <cstring>
#include <functional>
#include <iostream>
#include <thread>

using namespace photo_editor::pipeline;
namespace {
constexpr int width = 37, height = 29;
bool equal(const std::vector<ColorPixel>& a, const std::vector<ColorPixel>& b) {
  return a.size() == b.size() && std::memcmp(a.data(), b.data(), a.size() * sizeof(ColorPixel)) == 0;
}
ColorAdjustmentState recipe() {
  ColorAdjustmentState state;
  state.color_pipeline.working_profile = working_color_profile_from_name("Linear sRGB");
  LocalMaskSet set;
  set.temperature = 12.0;
  LocalMask source;
  source.id = "source"; source.kind = "background";
  source.segmentation_width = 9; source.segmentation_height = 7;
  for (int i = 0; i < 63; ++i) source.segmentation_mask.push_back(static_cast<std::uint8_t>((i * 47) % 256));
  source.selection_smooth = 8; source.selection_despeckle = 10;
  source.selection_fine_edge = 15; source.selection_fill_holes = 8;
  set.sources.push_back(source);
  state.local_mask_sets.push_back(set);
  return state;
}
std::vector<ColorPixel> render(std::vector<ColorPixel> pixels, const ColorAdjustmentState& state,
    LocalAdjustmentCoordinateMapping mapping = {0, 0, width, height}) {
  if (!apply_local_adjustments_to_look_pixels(&pixels, width, height, state, mapping)) return {};
  return pixels;
}
}
int main() {
  std::vector<ColorPixel> source;
  for (int i = 0; i < width * height; ++i)
    source.push_back({float(i % 23) / 19.0F, float(i % 17) / 13.0F, float(i % 31) / 27.0F, float(i % 5) / 4.0F});
  int checks = 0;
  using Mutation = std::function<void(ColorAdjustmentState&, std::vector<ColorPixel>&, LocalAdjustmentCoordinateMapping&)>;
  const auto check = [&](const Mutation& mutate, bool expected_hit) {
    auto before = recipe(), after = before;
    auto pixels = source;
    LocalAdjustmentCoordinateMapping mapping{0, 0, width, height};
    mutate(after, pixels, mapping);
    reset_mask_coverage_cache_for_testing(false);
    const auto reference = render(pixels, after, mapping);
    reset_mask_coverage_cache_for_testing();
    if (render(source, before).empty()) return false;
    if (mask_coverage_cache_stats().hits != 0 || render(source, before).empty()) return false;
    const auto stats = mask_coverage_cache_stats();
    const auto actual = render(pixels, after, mapping);
    const auto final = mask_coverage_cache_stats();
    ++checks;
    if (reference.empty() || !equal(reference, actual) || (final.hits > stats.hits) != expected_hit) {
      std::cerr << "coverage invalidation case " << checks << " failed, reference=" << reference.size()
                << " equal=" << equal(reference, actual) << " hits=" << (final.hits - stats.hits)
                << " expected_hit=" << expected_hit << '\n'; return false;
    }
    return true;
  };
  if (!check([](auto&, auto&, auto&) {}, true) ||
      !check([](auto& s, auto&, auto&) { s.local_mask_sets[0].temperature = 25; }, true)) return 1;
  for (const auto field : {&LocalMaskSet::opacity, &LocalMaskSet::density, &LocalMaskSet::selection_expand,
      &LocalMaskSet::selection_smooth, &LocalMaskSet::selection_fine_edge, &LocalMaskSet::selection_edge_contrast})
    if (!check([field](auto& s, auto&, auto&) { s.local_mask_sets[0].*field += .15; }, false)) return 1;
  for (const auto field : {&LocalMask::feather, &LocalMask::selection_smooth, &LocalMask::selection_grow,
      &LocalMask::selection_shrink, &LocalMask::selection_blur, &LocalMask::selection_harden,
      &LocalMask::selection_despeckle, &LocalMask::selection_fill_holes, &LocalMask::selection_remove_islands,
      &LocalMask::selection_decontaminate, &LocalMask::refine_luminance_min, &LocalMask::refine_hue_range,
      &LocalMask::refine_saturation_min, &LocalMask::refine_depth_min, &LocalMask::refine_edge_confidence_min})
    if (!check([field](auto& s, auto&, auto&) { s.local_mask_sets[0].sources[0].*field += .15; }, false)) return 1;
  if (!check([](auto& s, auto&, auto&) { s.local_mask_sets[0].sources[0].segmentation_mask.back() ^= 1; }, false) ||
      !check([](auto& s, auto&, auto&) { s.local_mask_sets[0].sources[0].selection_invert = true; }, false) ||
      !check([](auto& s, auto&, auto&) { s.local_mask_sets[0].sources[0].operation = "subtract"; }, false) ||
      !check([](auto&, auto& p, auto&) { p.back().red += .01F; }, false) ||
      !check([](auto&, auto& p, auto&) { p.front().alpha = -0.0F; }, false) ||
      !check([](auto&, auto&, auto& m) { ++m.source_x; ++m.source_width; }, false) ||
      !check([](auto& s, auto&, auto&) { s.color_pipeline.working_profile = working_color_profile_from_name("Linear ProPhoto RGB"); }, false) ||
      !check([](auto& s, auto&, auto&) { s.color_pipeline.photographic_rendering_model = PhotographicRenderingModel::LegacyOklchV1; }, false)) return 1;
  if (!check([](auto& s, auto&, auto&) { s.local_mask_sets[0].selection_invert = true; }, false) ||
      !check([](auto& s, auto&, auto&) { s.local_mask_sets[0].sources[0].enabled = false; }, false) ||
      !check([](auto& s, auto&, auto&) { s.local_mask_sets[0].sources[0].parent_id = "parent"; }, false) ||
      !check([](auto& s, auto&, auto&) { auto& m = s.local_mask_sets[0].sources[0]; m.segmentation_width = 7; m.segmentation_height = 9; }, false) ||
      !check([](auto& s, auto&, auto&) { auto& m = s.local_mask_sets[0].sources[0]; m.depth_width = 1; m.depth_height = 1; m.depth_map = {127}; }, false) ||
      !check([](auto& s, auto&, auto&) { s.local_mask_sets[0].sources[0].brush_points.push_back({}); }, false) ||
      !check([](auto& s, auto&, auto&) { s.input_transform.chromatic_adaptation_method = "CAT16"; }, false)) return 1;

  for (const auto profile : {"Linear sRGB", "Linear Display P3", "Linear Rec.2020", "Linear ProPhoto RGB"})
    for (const auto model : {PhotographicRenderingModel::LegacyOklchV1,
        PhotographicRenderingModel::Aces2HellwigJmhV1, PhotographicRenderingModel::Aces2OcioV1}) {
      auto state = recipe();
      state.color_pipeline.working_profile = working_color_profile_from_name(profile);
      state.color_pipeline.photographic_rendering_model = model;
      auto signed_source = source;
      signed_source[17].red = -.04F;
      state.local_mask_sets[0].temperature = 25;
      reset_mask_coverage_cache_for_testing(false);
      const auto expected = render(signed_source, state);
      reset_mask_coverage_cache_for_testing();
      state.local_mask_sets[0].temperature = 12;
      if (render(signed_source, state).empty()) return 1;
      if (render(signed_source, state).empty()) return 1;
      state.local_mask_sets[0].temperature = 25;
      if (!equal(expected, render(signed_source, state)) || mask_coverage_cache_stats().hits != 1) return 1;
    }

  // A downstream luminance range sees the preceding edit, not the original.
  auto before = recipe();
  auto range = before.local_mask_sets.front();
  range.sources[0].kind = "luminance";
  range.sources[0].luminance_min = .3; range.sources[0].luminance_max = .6;
  before.local_mask_sets.push_back(range);
  auto after = before;
  after.local_mask_sets[0].exposure = 1.5;
  reset_mask_coverage_cache_for_testing(false);
  const auto reference = render(source, after);
  reset_mask_coverage_cache_for_testing();
  if (render(source, before).empty()) return 1;
  if (render(source, before).empty()) return 1;
  if (!equal(reference, render(source, after))) return 1;
  const auto stats = mask_coverage_cache_stats();
  if (stats.hits != 1 || stats.misses != 5) return 1;
  std::atomic_bool parallel_ok = true;
  std::vector<std::thread> threads;
  for (int i = 0; i < 4; ++i) threads.emplace_back([&] {
    if (!equal(reference, render(source, after))) parallel_ok = false;
  });
  for (auto& thread : threads) thread.join();
  if (!parallel_ok) return 1;

  // Canceled partial coverage must never be published, even for matching keys.
  reset_mask_coverage_cache_for_testing();
  const auto context = make_pipeline_color_context(before);
  auto edge = source; edge.back().green += .01F;
  auto frame = acquire_mask_coverage_frame(source, &edge, width, height, context,
      before.color_pipeline.photographic_rendering_model, {0, 0, width, height}, {});
  if (acquire_mask_coverage(frame, before.local_mask_sets, {}).entry) return 1;
  auto lease = acquire_mask_coverage(frame, before.local_mask_sets, {});
  if (!lease.entry) return 1;
  lease.record(0, .5);  // Deliberately unfinished coverage.
  std::atomic_bool canceled = true;
  publish_mask_coverage(frame, lease, {.cancellation_requested = &canceled});
  if (acquire_mask_coverage(frame, before.local_mask_sets, {}).hit) return 1;
  if (acquire_mask_coverage_frame(source, &edge, width, height, context,
      before.color_pipeline.photographic_rendering_model, {0, 0, width, height}, {}) != frame) return 1;
  edge.back().blue += .01F;
  if (acquire_mask_coverage_frame(source, &edge, width, height, context,
      before.color_pipeline.photographic_rendering_model, {0, 0, width, height}, {}) == frame) return 1;
  lease = {}; frame.reset();
  reset_mask_coverage_cache_for_testing();
  // Pin old in-flight snapshots: replacement must respect the shared budget.
  std::vector<ColorPixel> big(1024 * 1024);
  std::vector<std::shared_ptr<MaskCoverageFrame>> pinned;
  bool limited = false;
  for (int i = 0; i < 26; ++i) {
    big[0].red = float(i);
    auto next = acquire_mask_coverage_frame(big, nullptr, 1024, 1024, context,
        before.color_pipeline.photographic_rendering_model, {0, 0, 1024, 1024}, {});
    if (!next) { limited = true; break; }
    pinned.push_back(next);
  }
  if (!limited || mask_coverage_cache_stats().retained_bytes > 384U * 1024U * 1024U) return 1;
  pinned.clear(); reset_mask_coverage_cache_for_testing();
  if (mask_coverage_cache_stats().retained_bytes != 0) return 1;
  std::cout << checks << " bit-exact invalidation cases, prior-mask ranges, concurrency, cancellation and budget pass\n";
}
