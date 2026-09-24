#include "photo_editor/pipeline/ocio_project_config.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#ifndef PHOTO_EDITOR_OCIO_PROJECT_FIXTURE_DIR
#error "PHOTO_EDITOR_OCIO_PROJECT_FIXTURE_DIR must identify the OCIO fixture"
#endif

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

[[nodiscard]] bool has_option(
    int argc,
    char** argv,
    std::string_view option) {
  for (int index = 1; index < argc; ++index) {
    if (std::string_view(argv[index]) == option) {
      return true;
    }
  }
  return false;
}

template <typename Operation>
[[nodiscard]] double measure_average_ms(int iterations, Operation operation) {
  const auto start = Clock::now();
  for (int iteration = 0; iteration < iterations; ++iteration) {
    if (!operation()) {
      return -1.0;
    }
  }
  return std::chrono::duration<double, std::milli>(Clock::now() - start)
             .count() /
         static_cast<double>(iterations);
}

}  // namespace

int main(int argc, char** argv) {
  namespace pipeline = photo_editor::pipeline;
  const auto config_iterations =
      option_value(argc, argv, "--config-iterations", 50);
  const auto processor_iterations =
      option_value(argc, argv, "--processor-iterations", 500);
  const auto apply_iterations =
      option_value(argc, argv, "--apply-iterations", 10);
  const auto pixel_count = option_value(argc, argv, "--pixels", 262144);
  const auto config_path =
      std::filesystem::path(PHOTO_EDITOR_OCIO_PROJECT_FIXTURE_DIR) /
      "config.ocio";

  if (!pipeline::ocio_project_backend_available()) {
    std::cout << "{\"schema\":\"ocio-project-benchmark:v1\","
                 "\"status\":\"backend-unavailable\"}\n";
    return 77;
  }

  const auto cold_start = Clock::now();
  const auto config = pipeline::load_ocio_project_config(config_path.string());
  const auto cold_config_ms =
      std::chrono::duration<double, std::milli>(Clock::now() - cold_start)
          .count();
  if (!config.ready()) {
    std::cerr << "unable to load benchmark config\n";
    return EXIT_FAILURE;
  }

  const auto repeated_config_ms = measure_average_ms(
      config_iterations,
      [&]() { return pipeline::load_ocio_project_config(config_path.string()).ready(); });
  const pipeline::OcioProjectProcessorRequest request{
      .kind = pipeline::OcioProjectProcessorKind::ColorSpace,
      .source_color_space = "linear",
      .destination_color_space = has_option(argc, argv, "--cube")
                                     ? "cube_swap"
                                     : "clf_scaled",
  };
  const auto cold_processor_start = Clock::now();
  const auto processor = config.compile_processor(request);
  const auto cold_processor_ms =
      std::chrono::duration<double, std::milli>(
          Clock::now() - cold_processor_start)
          .count();
  if (!processor.ready()) {
    std::cerr << "unable to compile benchmark processor\n";
    return EXIT_FAILURE;
  }
  if (has_option(argc, argv, "--dump-msl")) {
    const auto gpu_program = processor.compile_msl_gpu_program();
    if (!gpu_program.ready()) {
      std::cerr << gpu_program.description().failure_reason << '\n';
      return EXIT_FAILURE;
    }
    std::cout << gpu_program.description().shader_text;
    return EXIT_SUCCESS;
  }
  const auto repeated_processor_ms = measure_average_ms(
      processor_iterations,
      [&]() { return config.compile_processor(request).ready(); });

  std::vector<pipeline::OcioProjectRgbaFloat> pixels(
      static_cast<std::size_t>(pixel_count));
  for (std::size_t index = 0U; index < pixels.size(); ++index) {
    const auto unit = static_cast<float>(index % 1024U) / 1023.0F;
    pixels[index] = {
        .red = unit * 1.5F - 0.25F,
        .green = 1.0F - unit,
        .blue = unit * unit * 2.0F,
        .alpha = 0.375F,
    };
  }
  const auto original = pixels;
  const auto apply_ms = measure_average_ms(apply_iterations, [&]() {
    pixels = original;
    return processor.apply(pixels);
  });
  double checksum = 0.0;
  for (const auto& pixel : pixels) {
    checksum += static_cast<double>(pixel.red) +
                static_cast<double>(pixel.green) * 0.5 +
                static_cast<double>(pixel.blue) * 0.25 +
                static_cast<double>(pixel.alpha) * 0.125;
  }

  std::cout << "{\n"
            << "  \"schema\": \"ocio-project-benchmark:v1\",\n"
            << "  \"status\": \"ready\",\n"
            << "  \"backend_version\": \""
            << pipeline::ocio_project_backend_version() << "\",\n"
            << "  \"pixels\": " << pixel_count << ",\n"
            << "  \"cold_config_ms\": " << cold_config_ms << ",\n"
            << "  \"repeated_config_ms\": " << repeated_config_ms << ",\n"
            << "  \"cold_processor_ms\": " << cold_processor_ms << ",\n"
            << "  \"repeated_processor_ms\": " << repeated_processor_ms
            << ",\n"
            << "  \"apply_ms\": " << apply_ms << ",\n"
            << "  \"checksum\": " << checksum << "\n"
            << "}\n";
  return EXIT_SUCCESS;
}
