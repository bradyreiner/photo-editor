#include "photo_editor/pipeline/color_adjustments.h"
#include "photo_editor/pipeline/color_pipeline.h"
#include "photo_editor/pipeline/color_transform.h"
#include "photo_editor/pipeline/metal/metal_pipeline_backend.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <vector>

namespace {

[[nodiscard]] int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

}  // namespace

int main() {
  namespace pipeline = photo_editor::pipeline;
  const auto destination = pipeline::builtin_rgb_icc_profile_payload(
      pipeline::make_srgb_color_space());
  const auto transform = pipeline::compile_builtin_to_icc_transform(
      pipeline::make_linear_rec2020_color_space(),
      destination,
      pipeline::IccTransformOptions{.target = "metal-icc-lut-parity"});
  if (!pipeline::icc_backend_capability().available) {
    std::cout << "SKIP: Little CMS backend unavailable\n";
    return 77;
  }
  const auto lut = pipeline::compile_icc_transform_lut(
      transform,
      pipeline::icc_transform_lut_settings(
          pipeline::IccTransformLutQuality::Final));
  if (!transform.ready() || lut == nullptr || !lut->valid()) {
    return fail("the measured ICC LUT descriptor should compile before Metal dispatch");
  }

  constexpr int kWidth = 128;
  constexpr int kHeight = 64;
  std::vector<pipeline::IccRgbaFloat> source;
  source.reserve(static_cast<std::size_t>(kWidth * kHeight));
  auto random_state = std::uint32_t{0xabcdef01U};
  auto random_unit = [&]() {
    random_state = random_state * 1664525U + 1013904223U;
    return static_cast<float>(
        static_cast<double>(random_state) /
        static_cast<double>(std::numeric_limits<std::uint32_t>::max()));
  };
  for (int index = 0; index < kWidth * kHeight; ++index) {
    source.push_back({
        .red = random_unit(),
        .green = random_unit(),
        .blue = random_unit(),
        .alpha = random_unit(),
    });
  }
  source[0].red = -0.1F;
  source[1].green = 1.2F;

  auto cpu_values = source;
  auto cpu_stats = pipeline::IccTransformLutApplyStats{};
  if (!pipeline::apply_icc_transform_lut_hybrid(
          *lut,
          transform,
          cpu_values,
          &cpu_stats) ||
      cpu_stats.lut_pixels == 0U || cpu_stats.exact_fallback_pixels == 0U) {
    return fail("the CPU hybrid reference should exercise LUT and exact cells");
  }

  pipeline::metal::MetalPipelineBackend metal;
  if (!metal.available()) {
    const auto reason = metal.unavailable_reason();
    if (reason == "no_system_metal_device") {
      std::cout << "SKIP: " << reason << '\n';
      return 77;
    }
    std::cerr << "Metal ICC backend failed initialization: " << reason << '\n';
    return EXIT_FAILURE;
  }
  auto gpu = source;
  auto gpu_stats = pipeline::IccTransformLutApplyStats{};
  auto first_telemetry = pipeline::ColorAdjustmentTelemetry{};
  if (!metal.apply_icc_transform_lut(
          gpu,
          kWidth,
          kHeight,
          *lut,
          transform,
          &gpu_stats,
          &first_telemetry) ||
      gpu_stats.lut_pixels != cpu_stats.lut_pixels ||
      gpu_stats.exact_fallback_pixels != cpu_stats.exact_fallback_pixels ||
      first_telemetry.gpu_command_buffer_count != 1 ||
      first_telemetry.gpu_kernel_dispatch_count != 1 ||
      first_telemetry.gpu_cpu_to_gpu_bytes <=
          static_cast<std::int64_t>(source.size() * sizeof(pipeline::IccRgbaFloat)) ||
      first_telemetry.gpu_gpu_to_cpu_bytes !=
          static_cast<std::int64_t>(source.size() * sizeof(pipeline::IccRgbaFloat))) {
    return fail("Metal ICC LUT dispatch should publish hybrid routing and upload telemetry");
  }
  auto maximum_channel_error = 0.0;
  for (std::size_t index = 0U; index < gpu.size(); ++index) {
    maximum_channel_error = std::max({
        maximum_channel_error,
        std::abs(static_cast<double>(gpu[index].red) - cpu_values[index].red),
        std::abs(static_cast<double>(gpu[index].green) - cpu_values[index].green),
        std::abs(static_cast<double>(gpu[index].blue) - cpu_values[index].blue),
    });
    if (gpu[index].alpha != source[index].alpha) {
      return fail("Metal ICC LUT dispatch must preserve unassociated alpha exactly");
    }
  }
  if (maximum_channel_error > 2.0e-4) {
    std::cerr << "maximum_channel_error=" << maximum_channel_error << '\n';
    return fail("Metal and CPU ICC LUT interpolation should remain numerically aligned");
  }

  auto gpu_again = source;
  auto second_telemetry = pipeline::ColorAdjustmentTelemetry{};
  if (!metal.apply_icc_transform_lut(
          gpu_again,
          kWidth,
          kHeight,
          *lut,
          transform,
          nullptr,
          &second_telemetry) ||
      second_telemetry.gpu_cpu_to_gpu_bytes !=
          static_cast<std::int64_t>(source.size() * sizeof(pipeline::IccRgbaFloat))) {
    return fail("the second Metal ICC LUT dispatch should reuse cached resources");
  }

  auto non_finite = source;
  non_finite[0].red = std::numeric_limits<float>::quiet_NaN();
  const auto non_finite_before = non_finite;
  if (metal.apply_icc_transform_lut(
          non_finite,
          kWidth,
          kHeight,
          *lut,
          transform) ||
      !std::isnan(non_finite[0].red) ||
      non_finite[1].green != non_finite_before[1].green) {
    return fail("Metal ICC LUT dispatch should reject non-finite input atomically");
  }

  return EXIT_SUCCESS;
}
