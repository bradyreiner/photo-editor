#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "photo_editor/pipeline/color_metrics.h"
#include "photo_editor/pipeline/color_pipeline.h"
#include "photo_editor/pipeline/output_rendering.h"

namespace {

using Clock = std::chrono::steady_clock;

[[nodiscard]] int option_value(
    int argc,
    char** argv,
    std::string_view option,
    int fallback) {
  for (int index = 1; index + 1 < argc; ++index) {
    if (std::string_view(argv[index]) == option) {
      try {
        return std::max(1, std::stoi(argv[index + 1]));
      } catch (...) {
        return fallback;
      }
    }
  }
  return fallback;
}

[[nodiscard]] photo_editor::pipeline::CieLab linear_srgb_to_lab(
    const std::array<double, 3>& rgb) {
  return photo_editor::pipeline::cie_xyz_to_lab(
      {
          .x = 0.4124564 * rgb[0] + 0.3575761 * rgb[1] +
               0.1804375 * rgb[2],
          .y = 0.2126729 * rgb[0] + 0.7151522 * rgb[1] +
               0.0721750 * rgb[2],
          .z = 0.0193339 * rgb[0] + 0.1191920 * rgb[1] +
               0.9503041 * rgb[2],
      },
      photo_editor::pipeline::cie_d65_white());
}

[[nodiscard]] double percentile(
    const std::vector<double>& sorted_values,
    double fraction) {
  if (sorted_values.empty()) {
    return 0.0;
  }
  const auto rank = static_cast<std::size_t>(std::ceil(
      std::clamp(fraction, 0.0, 1.0) *
      static_cast<double>(sorted_values.size())));
  return sorted_values[
      std::min(sorted_values.size() - 1U,
               std::max<std::size_t>(1U, rank) - 1U)];
}

template <typename Render>
[[nodiscard]] std::pair<double, double> benchmark_render(
    const std::vector<std::array<double, 3>>& pixels,
    int iterations,
    Render render) {
  auto checksum = 0.0;
  const auto start = Clock::now();
  for (int iteration = 0; iteration < iterations; ++iteration) {
    for (const auto& pixel : pixels) {
      const auto output = render(pixel);
      checksum += output[0] + output[1] * 0.5 + output[2] * 0.25;
    }
  }
  const auto elapsed = std::chrono::duration<double, std::milli>(
      Clock::now() - start);
  return {elapsed.count() / static_cast<double>(iterations), checksum};
}

}  // namespace

int main(int argc, char** argv) {
  const auto width = option_value(argc, argv, "--width", 256);
  const auto height = option_value(argc, argv, "--height", 256);
  const auto iterations = option_value(argc, argv, "--iterations", 3);
  std::vector<std::array<double, 3>> pixels;
  pixels.reserve(
      static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
  for (int y = 0; y < height; ++y) {
    const auto yn = static_cast<double>(y) /
                    static_cast<double>(std::max(1, height - 1));
    for (int x = 0; x < width; ++x) {
      const auto xn = static_cast<double>(x) /
                      static_cast<double>(std::max(1, width - 1));
      const auto highlight = std::exp2(7.0 * xn) - 1.0;
      pixels.push_back({
          highlight * (0.05 + 0.95 * yn),
          highlight * (0.02 + 0.65 * (1.0 - yn)),
          highlight * (0.01 + 0.45 * std::abs(0.5 - yn)),
      });
    }
  }

  const auto config = photo_editor::pipeline::default_color_pipeline_config();
  const auto exact =
      photo_editor::pipeline::make_aces2_output_rendering_parameters(
          config.working_profile.color_space,
          config.preview_display_target);
  photo_editor::pipeline::clear_aces2_output_processor_cache_for_testing();
  const auto compile_start = Clock::now();
  const auto compiled = photo_editor::pipeline::compile_aces2_output_processor(
      config.working_profile.color_space,
      config.preview_display_target);
  const auto compile_ms = std::chrono::duration<double, std::milli>(
                              Clock::now() - compile_start)
                              .count();
  const auto cache_hit_start = Clock::now();
  const auto reused = photo_editor::pipeline::compile_aces2_output_processor(
      config.working_profile.color_space,
      config.preview_display_target);
  const auto cache_hit_ms = std::chrono::duration<double, std::milli>(
                                Clock::now() - cache_hit_start)
                                .count();
  if (compiled == nullptr || reused.get() != compiled.get()) {
    std::cerr << "compiled output processor was not cached\n";
    return EXIT_FAILURE;
  }

  const auto exact_result = benchmark_render(
      pixels,
      iterations,
      [&](const std::array<double, 3>& pixel) {
        return photo_editor::pipeline::aces2_render_to_linear_display_rgb(
            pixel,
            exact);
      });
  const auto compiled_result = benchmark_render(
      pixels,
      iterations,
      [&](const std::array<double, 3>& pixel) {
        return photo_editor::pipeline::aces2_render_to_linear_display_rgb(
            pixel,
            compiled->parameters);
      });

  photo_editor::pipeline::clear_aces2_output_lut_cache_for_testing();
  const auto interactive_lut_compile_start = Clock::now();
  const auto interactive_lut =
      photo_editor::pipeline::compile_aces2_output_lut(
          config.working_profile.color_space,
          config.preview_display_target,
          photo_editor::pipeline::aces2_output_lut_settings(
              photo_editor::pipeline::Aces2OutputLutQuality::Interactive));
  const auto interactive_lut_compile_ms =
      std::chrono::duration<double, std::milli>(
          Clock::now() - interactive_lut_compile_start)
          .count();
  const auto final_lut_compile_start = Clock::now();
  const auto final_lut = photo_editor::pipeline::compile_aces2_output_lut(
      config.working_profile.color_space,
      config.preview_display_target,
      photo_editor::pipeline::aces2_output_lut_settings(
          photo_editor::pipeline::Aces2OutputLutQuality::Final));
  const auto final_lut_compile_ms =
      std::chrono::duration<double, std::milli>(
          Clock::now() - final_lut_compile_start)
          .count();
  const auto final_lut_cache_hit_start = Clock::now();
  const auto reused_final_lut =
      photo_editor::pipeline::compile_aces2_output_lut(
          config.working_profile.color_space,
          config.preview_display_target,
          photo_editor::pipeline::aces2_output_lut_settings(
              photo_editor::pipeline::Aces2OutputLutQuality::Final));
  const auto final_lut_cache_hit_ms =
      std::chrono::duration<double, std::milli>(
          Clock::now() - final_lut_cache_hit_start)
          .count();

  std::pair<double, double> final_lut_result{};
  std::vector<double> final_lut_delta_e;
  if (final_lut != nullptr) {
    if (interactive_lut == nullptr ||
        reused_final_lut.get() != final_lut.get()) {
      std::cerr << "output LUT tiers were not both available and cached\n";
      return EXIT_FAILURE;
    }
    final_lut_result = benchmark_render(
        pixels,
        iterations,
        [&](const std::array<double, 3>& pixel) {
          return photo_editor::pipeline::aces2_output_lut_apply(
              *final_lut,
              pixel);
        });
    final_lut_delta_e.reserve(pixels.size());
    for (const auto& pixel : pixels) {
      const auto reference =
          photo_editor::pipeline::aces2_render_to_linear_display_rgb(
              pixel,
              compiled->parameters);
      const auto approximation =
          photo_editor::pipeline::aces2_output_lut_apply(*final_lut, pixel);
      final_lut_delta_e.push_back(photo_editor::pipeline::delta_e_2000(
          linear_srgb_to_lab(reference),
          linear_srgb_to_lab(approximation)));
    }
    std::sort(final_lut_delta_e.begin(), final_lut_delta_e.end());
  }

  std::vector<double> delta_e;
  delta_e.reserve(pixels.size());
  for (const auto& pixel : pixels) {
    const auto reference =
        photo_editor::pipeline::aces2_render_to_linear_display_rgb(
            pixel,
            exact);
    const auto accelerated =
        photo_editor::pipeline::aces2_render_to_linear_display_rgb(
            pixel,
            compiled->parameters);
    delta_e.push_back(photo_editor::pipeline::delta_e_2000(
        linear_srgb_to_lab(reference),
        linear_srgb_to_lab(accelerated)));
  }
  std::sort(delta_e.begin(), delta_e.end());
  const auto stats =
      photo_editor::pipeline::aces2_output_processor_cache_stats();
  const auto lut_stats =
      photo_editor::pipeline::aces2_output_lut_cache_stats();
  std::cout << "pixels=" << pixels.size() << '\n'
            << "iterations=" << iterations << '\n'
            << "compile_ms=" << compile_ms << '\n'
            << "cache_hit_ms=" << cache_hit_ms << '\n'
            << "exact_avg_ms=" << exact_result.first << '\n'
            << "compiled_avg_ms=" << compiled_result.first << '\n'
            << "speedup=" << exact_result.first / compiled_result.first
            << '\n'
            << "delta_e_00_p99=" << percentile(delta_e, 0.99) << '\n'
            << "delta_e_00_max=" << delta_e.back() << '\n'
            << "exact_checksum=" << exact_result.second << '\n'
            << "compiled_checksum=" << compiled_result.second << '\n'
            << "cache_entries=" << stats.entries << '\n'
            << "cache_hits=" << stats.hits << '\n'
            << "cache_misses=" << stats.misses << '\n'
            << "output_lut_available=" << (final_lut != nullptr ? 1 : 0)
            << '\n'
            << "interactive_lut_compile_ms=" << interactive_lut_compile_ms
            << '\n'
            << "final_lut_compile_ms=" << final_lut_compile_ms << '\n'
            << "final_lut_cache_hit_ms=" << final_lut_cache_hit_ms << '\n'
            << "output_lut_cache_entries=" << lut_stats.entries << '\n'
            << "output_lut_cache_bytes=" << lut_stats.bytes << '\n';
  if (final_lut != nullptr) {
    std::cout << "final_lut_avg_ms=" << final_lut_result.first << '\n'
              << "final_lut_speedup="
              << compiled_result.first / final_lut_result.first << '\n'
              << "final_lut_delta_e_00_p99="
              << percentile(final_lut_delta_e, 0.99) << '\n'
              << "final_lut_delta_e_00_max=" << final_lut_delta_e.back()
              << '\n'
              << "final_lut_checksum=" << final_lut_result.second << '\n';
  }
  return EXIT_SUCCESS;
}
