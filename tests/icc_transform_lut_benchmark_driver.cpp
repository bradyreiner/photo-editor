#include "photo_editor/pipeline/color_pipeline.h"
#include "photo_editor/pipeline/color_transform.h"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <vector>

int main() {
  namespace pipeline = photo_editor::pipeline;
  if (!pipeline::icc_backend_capability().available) {
    std::cout << "backend=unavailable\n";
    return 77;
  }
  const auto destination = pipeline::builtin_rgb_icc_profile_payload(
      pipeline::make_srgb_color_space());
  const auto transform = pipeline::compile_builtin_to_icc_transform(
      pipeline::make_linear_rec2020_color_space(),
      destination,
      pipeline::IccTransformOptions{.target = "icc-lut-benchmark"});
  const auto compile_begin = std::chrono::steady_clock::now();
  const auto lut = pipeline::compile_icc_transform_lut(
      transform,
      pipeline::icc_transform_lut_settings(
          pipeline::IccTransformLutQuality::Final));
  const auto compile_end = std::chrono::steady_clock::now();
  if (!transform.ready() || lut == nullptr) {
    std::cerr << "compile_failure="
              << pipeline::icc_transform_lut_cache_stats().last_failure_reason
              << '\n';
    return EXIT_FAILURE;
  }

  constexpr std::size_t kPixelCount = 512U * 512U;
  constexpr int kIterations = 20;
  std::vector<pipeline::IccRgbaFloat> source;
  source.reserve(kPixelCount);
  auto random_state = std::uint32_t{0x31415926U};
  auto random_unit = [&]() {
    random_state = random_state * 1664525U + 1013904223U;
    return static_cast<float>(
        static_cast<double>(random_state) /
        static_cast<double>(std::numeric_limits<std::uint32_t>::max()));
  };
  for (std::size_t index = 0U; index < kPixelCount; ++index) {
    source.push_back({
        .red = random_unit(),
        .green = random_unit(),
        .blue = random_unit(),
        .alpha = random_unit(),
    });
  }

  auto exact_ms = 0.0;
  auto hybrid_ms = 0.0;
  auto hybrid_stats = pipeline::IccTransformLutApplyStats{};
  for (int iteration = 0; iteration < kIterations; ++iteration) {
    auto exact = source;
    const auto exact_begin = std::chrono::steady_clock::now();
    if (!transform.apply(exact)) {
      return EXIT_FAILURE;
    }
    const auto exact_end = std::chrono::steady_clock::now();
    exact_ms += std::chrono::duration<double, std::milli>(
                    exact_end - exact_begin)
                    .count();

    auto hybrid = source;
    const auto hybrid_begin = std::chrono::steady_clock::now();
    if (!pipeline::apply_icc_transform_lut_hybrid(
            *lut,
            transform,
            hybrid,
            &hybrid_stats)) {
      return EXIT_FAILURE;
    }
    const auto hybrid_end = std::chrono::steady_clock::now();
    hybrid_ms += std::chrono::duration<double, std::milli>(
                     hybrid_end - hybrid_begin)
                     .count();
  }
  const auto compile_ms = std::chrono::duration<double, std::milli>(
                              compile_end - compile_begin)
                              .count();
  std::cout << "schema=icc-transform-lut-benchmark:v1"
            << " cube=" << lut->settings.cube_size
            << " compile_ms=" << compile_ms
            << " exact_apply_ms=" << exact_ms / kIterations
            << " hybrid_apply_ms=" << hybrid_ms / kIterations
            << " lut_pixels=" << hybrid_stats.lut_pixels
            << " exact_fallback_pixels="
            << hybrid_stats.exact_fallback_pixels
            << " unsafe_cells=" << lut->unsafe_cell_count
            << " total_cells=" << lut->unsafe_cells.size()
            << " p99_delta_e_00=" << lut->measured_p99_delta_e_00
            << " max_delta_e_00=" << lut->measured_maximum_delta_e_00
            << '\n';
  return EXIT_SUCCESS;
}
