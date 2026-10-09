#include "photo_editor/pipeline/photo_effects.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#if defined(__unix__) || defined(__APPLE__)
#include <sys/resource.h>
#endif

using namespace photo_editor::pipeline;
using Clock = std::chrono::steady_clock;

std::uint64_t pixel_hash(const std::vector<ColorPixel>& pixels) {
  auto hash = std::uint64_t{14695981039346656037ULL};
  for (const auto& pixel : pixels) {
    for (const auto value : {pixel.red, pixel.green, pixel.blue, pixel.alpha}) {
      std::uint32_t bits = 0;
      std::memcpy(&bits, &value, sizeof(bits));
      for (int byte = 0; byte < 4; ++byte) {
        hash ^= (bits >> (byte * 8)) & 255U;
        hash *= 1099511628211ULL;
      }
    }
  }
  return hash;
}

int main(int argc, char** argv) {
  if (argc < 2) { std::cerr << "usage: photo_effect_kernel_benchmark output.json [width] [repeats]\n"; return 1; }
  const int width = argc > 2 ? std::stoi(argv[2]) : 1024;
  const int height = width * 2 / 3 + 1;
  const int repeats = argc > 3 ? std::stoi(argv[3]) : 3;
  if (width < 16 || width > 4096 || repeats < 1 || repeats > 20) return 1;
  const auto count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
  std::vector<ColorPixel> source(count);
  std::vector<float> mask(count);
  for (std::size_t i = 0; i < count; ++i) {
    const auto ramp = static_cast<float>((i * 37U) % 1021U) / 1020.0F;
    source[i] = {ramp * 4.0F - 0.2F, ramp * 0.7F, 1.4F - ramp, static_cast<float>(i % 17U) / 16.0F};
    mask[i] = static_cast<float>(i % 9U) / 8.0F;
  }
  std::ofstream output(argv[1]);
  if (!output) return 1;
  output << "{\"width\":" << width << ",\"height\":" << height << ",\"cases\":[\n";
  bool first = true;
  for (const auto* name : {"gaussian_blur", "unsharp_mask", "box_blur", "motion_blur", "dragged_shutter", "optical_diffusion", "imperfect_print"}) {
    for (const auto mode : {PhotoEffectEdgeMode::Transparent, PhotoEffectEdgeMode::Extend, PhotoEffectEdgeMode::Reflect}) {
      if ((std::string(name) == "optical_diffusion" || std::string(name) == "imperfect_print") && mode != PhotoEffectEdgeMode::Extend) continue;
      PhotoEffectInstance effect;
      effect.id = "bench"; effect.type_id = name; effect.amount = .73; effect.mask_ref = "selection";
      effect.gaussian = {12.0, mode}; effect.unsharp = {3.0, 1.2, .01, mode};
      effect.box = {24, mode}; effect.motion = {24.0, 27.0, mode};
      effect.dragged_shutter = {24.0, 27.0, .65, .4, mode, false};
      effect.diffusion.radius = 24.0;
      effect.imperfect_print.red_offset_x = 1.5;
      for (const bool tiled : {false, true}) {
        if (tiled && std::string(name) != "gaussian_blur" && std::string(name) != "optical_diffusion") continue;
        std::uint64_t expected_hash = 0;
        std::vector<double> times;
        for (int repeat = 0; repeat < repeats; ++repeat) {
          auto pixels = source;
          const auto start = Clock::now();
          PhotoEffectResult result;
          if (tiled) {
            const std::vector effects{effect};
            const std::vector bindings{PhotoEffectMaskBinding{"selection", mask}};
            result = apply_photo_effect_stack_tiled(&pixels, width, height, effects, {}, bindings, 256);
          } else {
            result = apply_photo_effect(&pixels, width, height, effect, {}, mask);
          }
          const auto ms = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
          if (!result.ok()) { std::cerr << name << ": " << result.error << '\n'; return 2; }
          const auto hash = pixel_hash(pixels);
          if (repeat > 0 && hash != expected_hash) return 3;
          expected_hash = hash; times.push_back(ms);
        }
        if (!first) output << ",\n";
        first = false;
        output << "{\"effect\":\"" << name << "\",\"edge\":" << static_cast<int>(mode)
               << ",\"tiled\":" << (tiled ? "true" : "false") << ",\"hash\":\"" << expected_hash << "\",\"ms\":[";
        for (std::size_t i = 0; i < times.size(); ++i) output << (i ? "," : "") << times[i];
        output << "]}";
        output.flush();
        std::cout << name << ' ' << static_cast<int>(mode) << " tiled=" << tiled << " last_ms=" << times.back() << '\n';
      }
    }
  }
  long rss = 0;
#if defined(__unix__) || defined(__APPLE__)
  struct rusage usage{};
  if (getrusage(RUSAGE_SELF, &usage) == 0) rss = usage.ru_maxrss;
#if !defined(__APPLE__)
  rss *= 1024;
#endif
#endif
  output << "\n],\"peakRssBytes\":" << rss << "}\n";
  return output.good() ? 0 : 1;
}
