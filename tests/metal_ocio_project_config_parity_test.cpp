#include "photo_editor/pipeline/color_adjustments.h"
#include "photo_editor/pipeline/metal/metal_pipeline_backend.h"
#include "photo_editor/pipeline/ocio_project_config.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#ifndef PHOTO_EDITOR_OCIO_PROJECT_FIXTURE_DIR
#error "PHOTO_EDITOR_OCIO_PROJECT_FIXTURE_DIR must identify the OCIO fixture"
#endif

namespace {

using photo_editor::pipeline::ColorPixel;
using photo_editor::pipeline::OcioProjectProcessor;
using photo_editor::pipeline::OcioProjectRgbaFloat;

[[nodiscard]] bool cpu_apply(
    const OcioProjectProcessor& processor,
    const std::vector<ColorPixel>& source,
    std::vector<ColorPixel>* output) {
  if (output == nullptr) {
    return false;
  }
  std::vector<OcioProjectRgbaFloat> converted;
  converted.reserve(source.size());
  for (const auto& pixel : source) {
    converted.push_back({
        .red = pixel.red,
        .green = pixel.green,
        .blue = pixel.blue,
        .alpha = pixel.alpha,
    });
  }
  if (!processor.apply(converted)) {
    return false;
  }
  output->clear();
  output->reserve(converted.size());
  for (const auto& pixel : converted) {
    output->push_back({
        .red = pixel.red,
        .green = pixel.green,
        .blue = pixel.blue,
        .alpha = pixel.alpha,
    });
  }
  return true;
}

[[nodiscard]] bool pixels_match(
    const std::vector<ColorPixel>& expected,
    const std::vector<ColorPixel>& actual,
    double tolerance,
    double* maximum_error) {
  if (expected.size() != actual.size() || maximum_error == nullptr) {
    return false;
  }
  *maximum_error = 0.0;
  for (std::size_t index = 0U; index < expected.size(); ++index) {
    if (!std::isfinite(actual[index].red) ||
        !std::isfinite(actual[index].green) ||
        !std::isfinite(actual[index].blue) ||
        !std::isfinite(actual[index].alpha) ||
        actual[index].alpha != expected[index].alpha) {
      return false;
    }
    *maximum_error = std::max({
        *maximum_error,
        std::abs(static_cast<double>(actual[index].red - expected[index].red)),
        std::abs(static_cast<double>(actual[index].green - expected[index].green)),
        std::abs(static_cast<double>(actual[index].blue - expected[index].blue)),
    });
  }
  return *maximum_error <= tolerance;
}

}  // namespace

int main() {
  namespace pipeline = photo_editor::pipeline;
  const auto config_path =
      std::filesystem::path(PHOTO_EDITOR_OCIO_PROJECT_FIXTURE_DIR) /
      "config.ocio";
  const auto config = pipeline::load_ocio_project_config(config_path.string());
  if (!pipeline::ocio_project_backend_available()) {
    std::cout << "SKIP: OpenColorIO project backend unavailable\n";
    return 77;
  }
  const auto matrix_processor = config.compile_processor({
      .kind = pipeline::OcioProjectProcessorKind::ColorSpace,
      .source_color_space = "linear",
      .destination_color_space = "doubled",
  });
  const auto cube_processor = config.compile_processor({
      .kind = pipeline::OcioProjectProcessorKind::ColorSpace,
      .source_color_space = "linear",
      .destination_color_space = "cube_swap",
  });
  const auto matrix_program = matrix_processor.compile_msl_gpu_program();
  const auto cube_program = cube_processor.compile_msl_gpu_program();
  if (!config.ready() || !matrix_processor.ready() || !cube_processor.ready() ||
      !matrix_program.ready() || !cube_program.ready() ||
      matrix_program.description().program_cache_key.size() != 64U ||
      cube_program.description().program_cache_key.size() != 64U ||
      !matrix_program.description().textures.empty() ||
      cube_program.description().textures.size() != 1U) {
    std::cerr << "OCIO MSL program compilation failed before Metal dispatch\n";
    return EXIT_FAILURE;
  }

  pipeline::metal::MetalPipelineBackend metal;
  if (!metal.available()) {
    const auto reason = metal.unavailable_reason();
    if (reason == "no_system_metal_device") {
      std::cout << "SKIP: no system Metal device is available\n";
      return 77;
    }
    std::cerr << "Metal backend failed initialization: " << reason << '\n';
    return EXIT_FAILURE;
  }

  constexpr int kWidth = 17;
  constexpr int kHeight = 13;
  std::vector<ColorPixel> matrix_source;
  matrix_source.reserve(static_cast<std::size_t>(kWidth * kHeight));
  for (int y = 0; y < kHeight; ++y) {
    const auto vertical = static_cast<float>(y) /
                          static_cast<float>(kHeight - 1);
    for (int x = 0; x < kWidth; ++x) {
      const auto horizontal = static_cast<float>(x) /
                              static_cast<float>(kWidth - 1);
      matrix_source.push_back({
          .red = horizontal * 3.0F - 0.5F,
          .green = vertical * 2.0F - 0.25F,
          .blue = (horizontal - vertical) * 1.5F,
          .alpha = 0.125F + horizontal * 0.75F,
      });
    }
  }
  std::vector<ColorPixel> matrix_cpu;
  if (!cpu_apply(matrix_processor, matrix_source, &matrix_cpu)) {
    std::cerr << "OCIO CPU matrix processor failed\n";
    return EXIT_FAILURE;
  }
  auto matrix_gpu = matrix_source;
  pipeline::ColorAdjustmentTelemetry telemetry;
  std::string failure_reason;
  if (!metal.apply_ocio_project_gpu_program(
          &matrix_gpu,
          kWidth,
          kHeight,
          matrix_program,
          &failure_reason,
          &telemetry)) {
    std::cerr << "OCIO Metal matrix dispatch failed: " << failure_reason << '\n';
    return EXIT_FAILURE;
  }
  double matrix_maximum_error = 0.0;
  if (!pixels_match(
          matrix_cpu,
          matrix_gpu,
          2.0e-6,
          &matrix_maximum_error)) {
    std::cerr << "OCIO Metal matrix parity exceeded tolerance: "
              << matrix_maximum_error << '\n';
    return EXIT_FAILURE;
  }

  std::vector<ColorPixel> cube_source;
  cube_source.reserve(matrix_source.size());
  for (int y = 0; y < kHeight; ++y) {
    const auto vertical = static_cast<float>(y) /
                          static_cast<float>(kHeight - 1);
    for (int x = 0; x < kWidth; ++x) {
      const auto horizontal = static_cast<float>(x) /
                              static_cast<float>(kWidth - 1);
      cube_source.push_back({
          .red = horizontal,
          .green = vertical,
          .blue = 0.65F * horizontal + 0.35F * vertical,
          .alpha = 0.2F + 0.6F * vertical,
      });
    }
  }
  std::vector<ColorPixel> cube_cpu;
  if (!cpu_apply(cube_processor, cube_source, &cube_cpu)) {
    std::cerr << "OCIO CPU cube processor failed\n";
    return EXIT_FAILURE;
  }
  auto cube_gpu = cube_source;
  const auto bytes_before_cube = telemetry.gpu_cpu_to_gpu_bytes;
  if (!metal.apply_ocio_project_gpu_program(
          &cube_gpu,
          kWidth,
          kHeight,
          cube_program,
          &failure_reason,
          &telemetry)) {
    std::cerr << "OCIO Metal cube dispatch failed: " << failure_reason << '\n';
    return EXIT_FAILURE;
  }
  double cube_maximum_error = 0.0;
  if (!pixels_match(cube_cpu, cube_gpu, 2.0e-5, &cube_maximum_error)) {
    std::cerr << "OCIO Metal cube parity exceeded tolerance: "
              << cube_maximum_error << '\n';
    return EXIT_FAILURE;
  }
  const auto pixel_bytes = static_cast<std::int64_t>(
      cube_gpu.size() * sizeof(ColorPixel));
  if (telemetry.gpu_cpu_to_gpu_bytes - bytes_before_cube <= pixel_bytes) {
    std::cerr << "first OCIO cube dispatch should report its texture upload\n";
    return EXIT_FAILURE;
  }
  cube_gpu = cube_source;
  const auto bytes_before_reuse = telemetry.gpu_cpu_to_gpu_bytes;
  if (!metal.apply_ocio_project_gpu_program(
          &cube_gpu,
          kWidth,
          kHeight,
          cube_program,
          &failure_reason,
          &telemetry) ||
      telemetry.gpu_cpu_to_gpu_bytes - bytes_before_reuse != pixel_bytes ||
      telemetry.gpu_command_buffer_count != 3 ||
      telemetry.gpu_kernel_dispatch_count != 3 ||
      telemetry.gpu_gpu_to_cpu_bytes != pixel_bytes * 3) {
    std::cerr << "reused OCIO GPU programs should not recompile or re-upload LUTs\n";
    return EXIT_FAILURE;
  }

  std::cout << "Metal OCIO project parity: matrix_max_channel_error="
            << matrix_maximum_error
            << " cube_max_channel_error=" << cube_maximum_error << '\n';
  // Curved and cross-channel LUTs expose interpolation precision and semantic
  // differences that a matrix or identity cube cannot detect. Include values
  // outside the LUT domain to check clamp-to-edge behavior as well.
  auto precision_source = cube_source;
  for (auto& pixel : precision_source) {
    pixel.red = pixel.red * 1.4F - 0.2F;
    pixel.green = pixel.green * 1.4F - 0.2F;
    pixel.blue = pixel.blue * 1.4F - 0.2F;
  }
  for (const auto* fixture : {"precision-curve.clf", "precision-volume.clf"}) {
    const auto processor = config.compile_processor({
        .kind = pipeline::OcioProjectProcessorKind::FileTransform,
        .file_transform_path = (config_path.parent_path() / "luts" / fixture).string(),
    });
    const auto program = processor.compile_msl_gpu_program();
    auto gpu = precision_source;
    std::vector<ColorPixel> cpu;
    double maximum_error = 0.0;
    if (!processor.ready() || !program.ready() ||
        program.description().textures.empty() ||
        !cpu_apply(processor, precision_source, &cpu) ||
        !metal.apply_ocio_project_gpu_program(
            &gpu, kWidth, kHeight, program, &failure_reason) ||
        !pixels_match(cpu, gpu, 2.0e-5, &maximum_error)) {
      std::cerr << fixture << " float interpolation parity failed: "
                << failure_reason << " maximum_error=" << maximum_error << '\n';
      return EXIT_FAILURE;
    }
  }
  return EXIT_SUCCESS;
}
