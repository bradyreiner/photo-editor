#include "photo_editor/pipeline/color_pipeline.h"
#include "photo_editor/pipeline/color_transform.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <barrier>
#include <chrono>
#include <cstring>
#include <iostream>
#include <string_view>
#include <thread>
#include <vector>

namespace pipeline = photo_editor::pipeline;
using Clock = std::chrono::steady_clock;

namespace {
bool same(const std::vector<pipeline::IccRgbaFloat>& a,
    const std::vector<pipeline::IccRgbaFloat>& b) {
  return a.size() == b.size() &&
      std::memcmp(a.data(), b.data(), a.size() * sizeof(a.front())) == 0;
}
}

int main(int argc, char** argv) {
  if (!pipeline::icc_backend_capability().available) return 77;
  const bool benchmark = argc == 2 && std::string_view(argv[1]) == "--benchmark";
  const auto source_profile = pipeline::builtin_rgb_icc_profile_payload(
      pipeline::make_linear_rec2020_color_space());
  const auto srgb = pipeline::builtin_rgb_icc_profile_payload(pipeline::make_srgb_color_space());
  const auto p3 = pipeline::builtin_rgb_icc_profile_payload(pipeline::make_display_p3_color_space());
  std::vector<pipeline::IccRgbaFloat> source(benchmark ? 1024U * 683U : 16384U);
  for (std::size_t i = 0; i < source.size(); ++i) {
    const auto ramp = static_cast<float>((i * 37U) % 1021U) / 1020.0F;
    source[i] = {ramp * 1.6F - .1F, ramp * .7F, 1.4F - ramp,
        static_cast<float>(i % 19U) / 18.0F};
  }
  for (int scenario = 0; scenario < (benchmark ? 2 : 4); ++scenario) {
    const auto transform = scenario < 2
        ? pipeline::compile_icc_transform(source_profile, scenario == 0 ? srgb : p3,
              pipeline::IccTransformOptions{.target = "concurrent-icc-reference"})
        : pipeline::compile_icc_proofing_transform(source_profile, srgb, p3,
              pipeline::IccProofTransformOptions{.gamut_check = scenario == 3,
                  .target = "concurrent-icc-proof"});
    auto expected = source;
    if (!transform.ready() || !transform.apply(expected)) return 1;
    for (std::size_t i = 0; i < source.size(); ++i)
      if (expected[i].alpha != source[i].alpha) return 2;

    for (int threads : {1, 4}) {
      std::vector<double> timings;
      for (int iteration = 0; iteration < (benchmark ? 7 : 8); ++iteration) {
        std::vector<std::vector<pipeline::IccRgbaFloat>> pixels(
            static_cast<std::size_t>(threads), source);
        std::barrier start(threads + 1);
        std::atomic_bool valid{true};
        std::vector<std::thread> workers;
        for (int worker = 0; worker < threads; ++worker) {
          workers.emplace_back([&, worker, retained = transform]() {
            start.arrive_and_wait();
            auto& output = pixels[static_cast<std::size_t>(worker)];
            if (!retained.apply(output)) valid.store(false);
          });
        }
        const auto begin = Clock::now();
        start.arrive_and_wait();
        if (!benchmark && threads > 1) {
          // Evict the original cache entry while workers retain its immutable
          // handle. Compilation and apply must not mutate that live handle.
          for (int key = 0; key < 20; ++key) {
            const auto other = pipeline::compile_icc_transform(source_profile, p3,
                pipeline::IccTransformOptions{
                    .target = "concurrent-icc-eviction-" + std::to_string(key)});
            if (!other.ready()) valid.store(false);
          }
        }
        for (auto& worker : workers) worker.join();
        timings.push_back(std::chrono::duration<double, std::milli>(Clock::now() - begin).count());
        if (!valid.load()) return 3;
        for (const auto& output : pixels) if (!same(output, expected)) return 4;
      }
      std::cout << "{\"scenario\":" << scenario << ",\"threads\":" << threads << ",\"ms\":[";
      for (std::size_t i = 0; i < timings.size(); ++i) std::cout << (i ? "," : "") << timings[i];
      std::cout << "],\"exact\":true}" << std::endl;
    }
  }
  return 0;
}
