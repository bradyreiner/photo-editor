#include "preview_device_icc_options.h"

#include <chrono>
#include <cstring>
#include <iostream>
#include <string_view>
#include <vector>
#if defined(__APPLE__) || defined(__unix__)
#include <sys/resource.h>
#endif

namespace pipeline = photo_editor::pipeline;
namespace desktop = photo_editor::desktop;

int main(int argc, char** argv) {
  if (!pipeline::icc_backend_capability().available) return 77;
  const bool benchmark = argc == 2;
  const bool before = benchmark && std::string_view(argv[1]) == "before";
  auto config = pipeline::ColorPipelineConfig{};
  config.render_intent = pipeline::RenderIntent::Perceptual;
  config.black_point_compensation_enabled = true;
  const auto source = pipeline::builtin_rgb_icc_profile_payload(pipeline::make_linear_rec2020_color_space());
  const auto destination = pipeline::builtin_rgb_icc_profile_payload(pipeline::make_srgb_color_space());
  const auto options = desktop::preview_device_icc_options(config, true);
  const auto prewarm = pipeline::compile_icc_transform(source, destination, options);
  auto old_options = options;
  old_options.target = "preview-device:linear-carrier";
  const auto canonical = pipeline::compile_icc_transform(source, destination, before ? old_options : options);
  // This is the unchanged CPU publication identity in working_image.h.
  auto cpu_options = options;
  cpu_options.target = "preview-output:aces2-rec2020-interchange";
  const auto cpu = pipeline::compile_icc_transform(source, destination, cpu_options);
  if (!prewarm.ready() || !canonical.ready() || !cpu.ready()) return 1;
  const auto interactive = pipeline::icc_transform_lut_settings(pipeline::IccTransformLutQuality::Interactive);
  const auto final = pipeline::icc_transform_lut_settings(pipeline::IccTransformLutQuality::Final);
  pipeline::clear_icc_transform_lut_cache_for_testing();
  if (!pipeline::compile_icc_transform_lut(prewarm, interactive) ||
      !pipeline::compile_icc_transform_lut(prewarm, final)) return 1;
  const auto warm = pipeline::icc_transform_lut_cache_stats();
  std::size_t misses = 0;
  const auto start = std::chrono::steady_clock::now();
  for (int edit = 0; edit < 3; ++edit) {
    for (int stage = 0; stage < 3; ++stage) {
      const auto& transform = stage == 2 ? cpu : canonical;
      const auto& settings = stage == 0 ? interactive : final;
      if (!pipeline::find_cached_icc_transform_lut(transform, settings)) {
        ++misses;
        // Join the prewarm before advancing to make eviction reproducible.
        if (!pipeline::compile_icc_transform_lut(transform, settings)) return 1;
      }
    }
  }
  const auto ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
  const auto stats = pipeline::icc_transform_lut_cache_stats();
  long peak_rss = 0;
#if defined(__APPLE__) || defined(__unix__)
  rusage usage{};
  if (getrusage(RUSAGE_SELF, &usage) == 0) peak_rss = usage.ru_maxrss;
#endif
  std::cout << "{\"policy\":\"" << (before ? "before" : "candidate")
            << "\",\"requests\":9,\"lookup_misses\":" << misses
            << ",\"rebuilds\":" << stats.builds - warm.builds
            << ",\"evictions\":" << stats.evictions - warm.evictions
            << ",\"cache_bytes\":" << stats.bytes << ",\"cache_capacity\":" << stats.capacity
            << ",\"cache_maximum_bytes\":" << stats.maximum_bytes
            << ",\"elapsed_ms\":" << ms << ",\"peak_rss_native_units\":" << peak_rss << "}\n";
  if (!before && (misses != 0 || stats.builds != warm.builds || stats.evictions != warm.evictions)) return 1;
  if (stats.capacity != 2 || stats.maximum_bytes != 32U * 1024U * 1024U || stats.bytes > stats.maximum_bytes) return 1;
  if (benchmark) return 0;

  const auto old = pipeline::compile_icc_transform(source, destination, old_options);
  std::vector<pipeline::IccRgbaFloat> samples;
  for (int i = 0; i < 4096; ++i)
    samples.push_back({static_cast<float>(i % 71) / 70, static_cast<float>(i % 29) / 28,
                       static_cast<float>(i % 43) / 42, static_cast<float>(i % 13) / 12});
  auto expected = samples;
  auto actual = samples;
  if (!old.apply(expected) || !canonical.apply(actual) ||
      std::memcmp(expected.data(), actual.data(), actual.size() * sizeof(actual.front())) != 0) return 1;
  for (const auto settings : {interactive, final}) {
    const auto a = pipeline::compile_icc_transform_lut(old, settings);
    const auto b = pipeline::compile_icc_transform_lut(canonical, settings);
    if (!a || !b || a->table.size() != b->table.size() || a->unsafe_cells != b->unsafe_cells ||
        std::memcmp(a->table.data(), b->table.data(), a->table.size() * sizeof(a->table.front())) != 0) return 1;
  }
  const auto identity = canonical.description().cache_key;
  for (int variant = 0; variant < 4; ++variant) {
    auto changed_config = config;
    if (variant == 0) changed_config.black_point_compensation_enabled = false;
    if (variant == 1) changed_config.render_intent = pipeline::RenderIntent::RelativeColorimetric;
    const auto changed_options = desktop::preview_device_icc_options(changed_config, variant != 2);
    const auto changed_destination = variant == 3 ? source : destination;
    const auto changed = pipeline::compile_icc_transform(source, changed_destination, changed_options);
    if (!changed.ready() || changed.description().cache_key == identity) return 1;
  }
  std::cout << "Exact processor pixels and both LUT tables match; profile, intent, BPC, and carrier identities remain distinct\n";
  return 0;
}
