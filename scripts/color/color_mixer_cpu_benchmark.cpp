#include "photo_editor/pipeline/color_adjustments.h"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <string_view>
#include <vector>

namespace {

using namespace photo_editor::pipeline;
using Clock = std::chrono::steady_clock;
constexpr std::size_t kPixelCount = 131072;
constexpr std::string_view kRgbaHashAlgorithm = "fnv1a64-rgba-f32-le-v1";

[[nodiscard]] float decode_srgb(float value) {
  return value <= 0.04045F
      ? value / 12.92F
      : std::pow((value + 0.055F) / 1.055F, 2.4F);
}

[[nodiscard]] ColorPixel hue_patch(std::size_t index, float scale = 1.0F) {
  constexpr std::array<std::array<float, 3>, 8> encoded{{
      {0.80F, 0.24F, 0.24F}, {0.80F, 0.50F, 0.24F},
      {0.80F, 0.72F, 0.24F}, {0.28F, 0.72F, 0.24F},
      {0.24F, 0.68F, 0.76F}, {0.24F, 0.40F, 0.82F},
      {0.50F, 0.24F, 0.78F}, {0.78F, 0.24F, 0.52F},
  }};
  const auto& rgb = encoded[index % encoded.size()];
  return {decode_srgb(rgb[0]) * scale,
          decode_srgb(rgb[1]) * scale,
          decode_srgb(rgb[2]) * scale,
          0.25F + static_cast<float>(index % 4) * 0.25F};
}

[[nodiscard]] std::vector<ColorPixel> make_pixels(std::string_view sample) {
  std::vector<ColorPixel> pixels;
  pixels.reserve(kPixelCount);
  for (std::size_t index = 0; index < kPixelCount; ++index) {
    if (sample == "neutral") {
      const auto ramp = 0.015F + 0.82F * static_cast<float>(index % 16384U) / 16384.0F;
      const auto perturbation = (index % 6U < 3U ? 0.005F : -0.005F) * ramp;
      auto pixel = ColorPixel{ramp, ramp, ramp, 0.37F};
      if (index % 3U == 0U) pixel.red += perturbation;
      if (index % 3U == 1U) pixel.green += perturbation;
      if (index % 3U == 2U) pixel.blue += perturbation;
      pixels.push_back(pixel);
    } else if (sample == "dark") {
      pixels.push_back(hue_patch(index, 1.0e-5F));
    } else if (sample == "hdr") {
      const auto modulation = 0.9F + static_cast<float>(index % 97U) / 485.0F;
      const auto hue = hue_patch(index);
      pixels.push_back({hue.red * 2.0F * modulation,
                        hue.green * 1.4F * modulation,
                        hue.blue * 1.1F * modulation,
                        hue.alpha});
    } else {
      pixels.push_back(hue_patch(index));
    }
  }
  return pixels;
}

[[nodiscard]] double run_once(
    const CompiledLookStageProgram& program,
    const std::vector<ColorPixel>& input,
    std::vector<ColorPixel>* output) {
  const auto start = Clock::now();
  for (std::size_t index = 0; index < input.size(); ++index) {
    (*output)[index] = apply_compiled_look_stage_program(program, input[index]);
  }
  const auto finish = Clock::now();
  return std::chrono::duration<double, std::milli>(finish - start).count();
}

[[nodiscard]] double checksum(const std::vector<ColorPixel>& pixels) {
  return std::accumulate(
      pixels.begin(), pixels.end(), 0.0,
      [](double total, const ColorPixel& pixel) {
        return total + static_cast<double>(pixel.red) * 0.5 +
               static_cast<double>(pixel.green) * 0.3 +
               static_cast<double>(pixel.blue) * 0.2;
      });
}

[[nodiscard]] std::uint64_t bitwise_rgba_hash(
    const std::vector<ColorPixel>& pixels) {
  // Hash the actual IEEE-754 bit patterns in a defined byte order so the
  // output is stable across host endianness and catches changes to every
  // channel, including alpha and signed/HDR values.
  auto hash = std::uint64_t{14695981039346656037ULL};
  constexpr auto kFnvPrime = std::uint64_t{1099511628211ULL};
  for (const auto& pixel : pixels) {
    const auto rgba = std::array<float, 4>{
        pixel.red, pixel.green, pixel.blue, pixel.alpha};
    for (const auto channel : rgba) {
      const auto bits = std::bit_cast<std::uint32_t>(channel);
      for (auto byte_index = 0U; byte_index < 4U; ++byte_index) {
        const auto byte = static_cast<std::uint8_t>(bits >> (byte_index * 8U));
        hash ^= byte;
        hash *= kFnvPrime;
      }
    }
  }
  return hash;
}

[[nodiscard]] std::size_t changed_pixel_count(
    const std::vector<ColorPixel>& input,
    const std::vector<ColorPixel>& output) {
  auto changed = std::size_t{0};
  for (std::size_t index = 0; index < input.size(); ++index) {
    const auto scale = std::max({
        std::abs(input[index].red),
        std::abs(input[index].green),
        std::abs(input[index].blue),
        1.0e-30F});
    const auto difference = std::max({
        std::abs(output[index].red - input[index].red),
        std::abs(output[index].green - input[index].green),
        std::abs(output[index].blue - input[index].blue)});
    if (difference > scale * 1.0e-6F) {
      ++changed;
    }
  }
  return changed;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "usage: color_mixer_cpu_benchmark <neutral|colored|dark|hdr>\n";
    return EXIT_FAILURE;
  }
  const std::string_view sample(argv[1]);
  if (sample != "neutral" && sample != "colored" && sample != "dark" && sample != "hdr") {
    std::cerr << "unknown color mixer sample class\n";
    return EXIT_FAILURE;
  }

  auto state = ColorAdjustmentState{};
  state.color_pipeline.working_profile = working_color_profile_from_name("Linear sRGB");
  state.color_mixer_channels[0].hue = 50.0;
  state.color_mixer_channels[0].saturation = 25.0;
  const auto program = compile_look_stage_program(state);
  const auto input = make_pixels(sample);
  std::vector<ColorPixel> output(input.size());

  // The first measured call includes one-time selection transfer-table setup
  // in a fresh process. The warm distribution excludes that initialization.
  const auto cold_ms = run_once(program, input, &output);
  std::array<double, 24> warm{};
  for (auto& elapsed : warm) {
    elapsed = run_once(program, input, &output);
  }
  std::sort(warm.begin(), warm.end());
  const auto median_ms = (warm[11] + warm[12]) * 0.5;
  const auto p95_ms = warm[22];
  const auto output_checksum = checksum(output);
  const auto output_hash = bitwise_rgba_hash(output);
  const auto changed_pixels = changed_pixel_count(input, output);
  std::cout << "{\"schema\":\"color-mixer-cpu-benchmark:v1\","
            << "\"sample\":\"" << sample << "\","
            << "\"pixels\":" << input.size() << ','
            << "\"cold_ms\":" << cold_ms << ','
            << "\"warm_median_ms\":" << median_ms << ','
            << "\"warm_p95_ms\":" << p95_ms << ','
            << "\"changed_pixels\":" << changed_pixels << ','
            << "\"checksum\":" << std::setprecision(17) << output_checksum << ','
            << "\"rgba_hash_algorithm\":\"" << kRgbaHashAlgorithm << "\","
            << "\"bitwise_rgba_hash\":\"" << std::hex << std::nouppercase
            << std::setw(16) << std::setfill('0') << output_hash
            << std::dec << std::setfill(' ') << "\"}\n";
  if (!std::isfinite(output_checksum)) {
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
