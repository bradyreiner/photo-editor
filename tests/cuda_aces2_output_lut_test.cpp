#include "photo_editor/pipeline/color_adjustments.h"

#if defined(PHOTO_EDITOR_HAS_CUDA_BACKEND) && PHOTO_EDITOR_HAS_CUDA_BACKEND
#include "photo_editor/pipeline/cuda/cuda_pipeline_backend.h"
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace {

using photo_editor::pipeline::Aces2OutputLut3D;
using photo_editor::pipeline::ColorPixel;
using photo_editor::pipeline::CompiledAces2OutputLutApplication;

[[nodiscard]] CompiledAces2OutputLutApplication test_application() {
  auto lut = std::make_shared<Aces2OutputLut3D>();
  lut->settings.cube_size = 2;
  lut->settings.shaper_offset = 0.01;
  lut->shaper_maximum = 1.0;
  lut->maximum_physical_rgb = 1.0;
  lut->physical_to_reference_white = 1.0;
  lut->working_to_acescg.values = {
      1.0, 0.0, 0.0,
      0.0, 1.0, 0.0,
      0.0, 0.0, 1.0,
  };
  for (int red = 0; red < 2; ++red) {
    for (int green = 0; green < 2; ++green) {
      for (int blue = 0; blue < 2; ++blue) {
        lut->table.push_back({
            static_cast<float>(red),
            static_cast<float>(green),
            static_cast<float>(blue),
            1.0F,
        });
      }
    }
  }
  lut->cache_key = "cuda-aces2-output-lut-test-v1";
  return {
      .lut = std::move(lut),
      .display_headroom = 1.0,
  };
}

[[nodiscard]] bool close(float left, float right) {
  return std::abs(left - right) <= 2.0e-5F;
}

}  // namespace

int main() {
#if !defined(PHOTO_EDITOR_HAS_CUDA_BACKEND) || !PHOTO_EDITOR_HAS_CUDA_BACKEND
  std::cout << "SKIP: CUDA backend was not compiled\n";
  return 77;
#else
  auto& backend = photo_editor::pipeline::cuda::shared_cuda_pipeline_backend();
  if (!backend.available()) {
    std::cout << "SKIP: no suitable CUDA device is available: "
              << backend.unavailable_reason()
              << '\n';
    return 77;
  }
  const auto application = test_application();
  auto source = std::vector<ColorPixel>{
      {.red = 0.0F, .green = 0.0F, .blue = 0.0F, .alpha = 1.0F},
      {.red = 0.18F, .green = 0.5F, .blue = 1.0F, .alpha = 0.25F},
      {.red = 1.0F, .green = 0.25F, .blue = 0.01F, .alpha = 1.5F},
      {.red = 2.0F, .green = -0.25F, .blue = 0.75F, .alpha = -0.5F},
  };
  auto expected = source;
  photo_editor::pipeline::apply_aces2_output_lut_target_application(
      expected,
      application);
  auto telemetry = photo_editor::pipeline::ColorAdjustmentTelemetry{};
  auto actual = source;
  if (!backend.apply_aces2_output_lut_target(
          &actual, 2, 2, application, &telemetry)) {
    std::cerr << "CUDA output LUT failed: " << telemetry.gpu_fallback_reason
              << '\n';
    return 1;
  }
  for (std::size_t index = 0U; index < expected.size(); ++index) {
    if (!close(actual[index].red, expected[index].red) ||
        !close(actual[index].green, expected[index].green) ||
        !close(actual[index].blue, expected[index].blue) ||
        !close(actual[index].alpha, expected[index].alpha)) {
      std::cerr << "CUDA/CPU parity mismatch at pixel " << index << '\n';
      return 1;
    }
  }
  if (telemetry.gpu_kernel_dispatch_count != 1 ||
      telemetry.gpu_cpu_to_gpu_bytes <= 0 ||
      telemetry.gpu_gpu_to_cpu_bytes <= 0) {
    std::cerr << "CUDA telemetry did not record the device execution\n";
    return 1;
  }
  auto exposure_pixels = source;
  auto exposure_telemetry =
      photo_editor::pipeline::ColorAdjustmentTelemetry{};
  if (!backend.apply_scene_exposure(
          &exposure_pixels,
          2,
          2,
          2.0,
          &exposure_telemetry)) {
    std::cerr << "CUDA scene exposure failed: "
              << exposure_telemetry.gpu_fallback_reason << '\n';
    return 1;
  }
  for (std::size_t index = 0U; index < source.size(); ++index) {
    if (!close(exposure_pixels[index].red, source[index].red * 2.0F) ||
        !close(exposure_pixels[index].green, source[index].green * 2.0F) ||
        !close(exposure_pixels[index].blue, source[index].blue * 2.0F) ||
        exposure_pixels[index].alpha != source[index].alpha) {
      std::cerr << "CUDA scene exposure mismatch at pixel " << index << '\n';
      return 1;
    }
  }
  if (exposure_telemetry.gpu_kernel_dispatch_count != 1 ||
      exposure_telemetry.gpu_cpu_to_gpu_bytes <= 0 ||
      exposure_telemetry.gpu_gpu_to_cpu_bytes <= 0) {
    std::cerr << "CUDA scene exposure telemetry is incomplete\n";
    return 1;
  }
  auto cached_telemetry = photo_editor::pipeline::ColorAdjustmentTelemetry{};
  actual = source;
  if (!backend.apply_aces2_output_lut_target(
          &actual, 2, 2, application, &cached_telemetry)) {
    std::cerr << "CUDA cached output LUT failed\n";
    return 1;
  }
  if (cached_telemetry.gpu_cpu_to_gpu_bytes !=
      static_cast<std::int64_t>(actual.size() * sizeof(ColorPixel))) {
    std::cerr << "CUDA LUT cache did not reuse the device table\n";
    return 1;
  }
  std::cout << "PASS: " << backend.device_name()
            << " CUDA exposure and ACES2 output LUT match CPU references\n";
  return 0;
#endif
}
