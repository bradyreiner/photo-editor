#include "photo_editor/pipeline/render_executor.h"
#include "photo_editor/pipeline/output_rendering.h"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <thread>
#include <utility>
#include <vector>

namespace {

using namespace photo_editor::pipeline;

int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

[[nodiscard]] std::vector<ColorPixel> source_fixture(
    std::size_t width,
    std::size_t height) {
  auto pixels = std::vector<ColorPixel>(width * height);
  constexpr auto kAlpha = std::array<float, 5>{
      0.0F, 0.25F, 0.5F, 0.75F, 1.0F};
  for (std::size_t y = 0U; y < height; ++y) {
    for (std::size_t x = 0U; x < width; ++x) {
      const auto index = y * width + x;
      pixels[index] = ColorPixel{
          .red = static_cast<float>(x % width) /
                 static_cast<float>(width - 1U),
          .green = static_cast<float>(y % height) /
                   static_cast<float>(height - 1U),
          .blue = static_cast<float>((x * 17U + y * 31U) % 1009U) / 1008.0F,
          .alpha = kAlpha[(x + y) % kAlpha.size()],
      };
    }
  }
  return pixels;
}

[[nodiscard]] bool same_float(float left, float right) {
  return std::bit_cast<std::uint32_t>(left) ==
         std::bit_cast<std::uint32_t>(right);
}

[[nodiscard]] bool same_pixels(
    const std::vector<ColorPixel>& left,
    const std::vector<ColorPixel>& right) {
  if (left.size() != right.size()) return false;
  for (std::size_t index = 0U; index < left.size(); ++index) {
    if (!same_float(left[index].red, right[index].red) ||
        !same_float(left[index].green, right[index].green) ||
        !same_float(left[index].blue, right[index].blue) ||
        !same_float(left[index].alpha, right[index].alpha)) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] std::int64_t median(std::vector<std::int64_t> values) {
  std::sort(values.begin(), values.end());
  return values.at(values.size() / 2U);
}

}  // namespace

int main() {
  const auto source_profile =
      builtin_rgb_icc_profile_payload(make_linear_srgb_color_space());
  const auto destination_profile = builtin_rgb_icc_profile_payload(
      output_color_profile_from_name("sRGB").color_space);
  const auto transform = compile_icc_transform(
      source_profile,
      destination_profile,
      IccTransformOptions{
          .rendering_intent = IccRenderingIntent::Perceptual,
          .black_point_compensation = true,
          .pixel_format = IccPixelFormat::RgbaFloat32,
          .target = "render-executor-icc-parallel-test",
      });
  if (source_profile.empty() || destination_profile.empty() ||
      !transform.ready() || transform.description().device_link ||
      transform.description().soft_proofing ||
      transform.description().gamut_check ||
      transform.description().source_profile_sha256.empty() ||
      transform.description().destination_profile_sha256.empty() ||
      transform.description().pixel_format != IccPixelFormat::RgbaFloat32) {
    return fail("The ICC test transform must be a ready ordinary RGBA float device transform.");
  }

  constexpr auto kWidth = std::size_t{1024};
  constexpr auto kHeight = std::size_t{670};
  const auto source = source_fixture(kWidth, kHeight);
  const auto parallel_expected =
      std::thread::hardware_concurrency() > 2U;
  auto serial = source;
  auto parallel = source;
  auto serial_used_parallel = true;
  auto parallel_used_parallel = false;
  if (!apply_cpu_icc_transform(
          &serial, transform, {}, false, &serial_used_parallel) ||
      serial_used_parallel ||
      !apply_cpu_icc_transform(
          &parallel, transform, {}, true, &parallel_used_parallel) ||
      parallel_used_parallel != parallel_expected) {
    return fail("The large CPU ICC request did not select the expected serial and bounded-parallel paths.");
  }
  if (!same_pixels(serial, parallel)) {
    return fail("Parallel CPU ICC output must match serial RGBA output bit for bit.");
  }
  for (std::size_t index = 0U; index < source.size(); ++index) {
    if (!same_float(source[index].alpha, serial[index].alpha) ||
        !same_float(source[index].alpha, parallel[index].alpha)) {
      return fail("CPU ICC processing must preserve every source alpha value exactly.");
    }
  }

  auto small = std::vector<ColorPixel>(source.begin(), source.begin() + 4096);
  auto small_reference = small;
  auto small_parallel_used = true;
  if (!apply_cpu_icc_transform(
          &small, transform, {}, true, &small_parallel_used) ||
      small_parallel_used ||
      !apply_cpu_icc_transform(
          &small_reference, transform, {}, false) ||
      !same_pixels(small, small_reference)) {
    return fail("Small images must keep the serial route when parallel mode is opted in.");
  }

  auto cancelled_pixels = source;
  auto cancelled_parallel_used = true;
  const auto cancellation_requested = std::atomic_bool{true};
  if (apply_cpu_icc_transform(
          &cancelled_pixels,
          transform,
          CancellationToken{
              .cancellation_requested = &cancellation_requested,
          },
          true,
          &cancelled_parallel_used) ||
      cancelled_parallel_used || !same_pixels(cancelled_pixels, source)) {
    return fail("A cancelled CPU ICC request must fail closed before changing its source pixels.");
  }

  auto unready_pixels = source;
  auto unready_parallel_used = true;
  const auto unready_transform = CompiledIccTransform{};
  if (apply_cpu_icc_transform(
          &unready_pixels,
          unready_transform,
          {},
          true,
          &unready_parallel_used) ||
      unready_parallel_used || !same_pixels(unready_pixels, source)) {
    return fail("An unready profile must fail closed without selecting or partially applying the parallel path.");
  }

  constexpr auto kWarmupCount = std::size_t{2};
  constexpr auto kMeasurementCount = std::size_t{7};
  auto serial_times = std::vector<std::int64_t>{};
  auto parallel_times = std::vector<std::int64_t>{};
  serial_times.reserve(kMeasurementCount);
  parallel_times.reserve(kMeasurementCount);
  for (auto iteration = std::size_t{0};
       iteration < kWarmupCount + kMeasurementCount;
       ++iteration) {
    for (const auto allow_parallel : {false, true}) {
      auto pixels = source;
      const auto start = std::chrono::steady_clock::now();
      auto used_parallel = false;
      const auto succeeded = apply_cpu_icc_transform(
          &pixels, transform, {}, allow_parallel, &used_parallel);
      const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
                               std::chrono::steady_clock::now() - start)
                               .count();
      if (!succeeded ||
          used_parallel != (allow_parallel && parallel_expected)) {
        return fail("The timed ICC route did not execute the requested path.");
      }
      if (iteration >= kWarmupCount) {
        (allow_parallel ? parallel_times : serial_times).push_back(elapsed);
      }
    }
  }
  const auto serial_median_us = median(std::move(serial_times));
  const auto parallel_median_us = median(std::move(parallel_times));
  const auto improvement_percent = serial_median_us > 0
      ? 100.0 * static_cast<double>(serial_median_us - parallel_median_us) /
            static_cast<double>(serial_median_us)
      : 0.0;
  std::cout << "CPU ICC transform benchmark: pixels=" << source.size()
            << " serial_median_us=" << serial_median_us
            << " parallel_median_us=" << parallel_median_us
            << " improvement_percent=" << improvement_percent << '\n';
  return EXIT_SUCCESS;
}
