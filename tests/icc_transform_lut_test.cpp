#include "photo_editor/pipeline/color_pipeline.h"
#include "photo_editor/pipeline/color_transform.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <future>
#include <iostream>
#include <limits>
#include <thread>
#include <vector>

namespace {

[[nodiscard]] int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

[[nodiscard]] bool same_pixel(
    const photo_editor::pipeline::IccRgbaFloat& left,
    const photo_editor::pipeline::IccRgbaFloat& right) {
  return left.red == right.red && left.green == right.green &&
         left.blue == right.blue && left.alpha == right.alpha;
}

}  // namespace

int main() {
  namespace pipeline = photo_editor::pipeline;
  if (!pipeline::icc_backend_capability().available) {
    std::cout << "SKIP: Little CMS backend unavailable\n";
    return 77;
  }

  const auto destination = pipeline::builtin_rgb_icc_profile_payload(
      pipeline::make_srgb_color_space());
  const auto transform = pipeline::compile_builtin_to_icc_transform(
      pipeline::make_linear_rec2020_color_space(),
      destination,
      pipeline::IccTransformOptions{
          .rendering_intent = pipeline::IccRenderingIntent::Perceptual,
          .black_point_compensation = true,
          .pixel_format = pipeline::IccPixelFormat::RgbaFloat32,
          .target = "icc-lut-test",
      });
  if (!transform.ready()) {
    return fail("the exact ICC reference processor should compile");
  }

  pipeline::clear_icc_transform_lut_cache_for_testing();
  const auto interactive = pipeline::compile_icc_transform_lut(
      transform,
      pipeline::icc_transform_lut_settings(
          pipeline::IccTransformLutQuality::Interactive));
  const auto final = pipeline::compile_icc_transform_lut(
      transform,
      pipeline::icc_transform_lut_settings(
          pipeline::IccTransformLutQuality::Final));
  if (interactive == nullptr || final == nullptr || !interactive->valid() ||
      !final->valid() || interactive->settings.cube_size != 33 ||
      final->settings.cube_size != 97 ||
      interactive->transform_cache_key != transform.description().cache_key ||
      final->transform_cache_key != transform.description().cache_key ||
      interactive->cache_key == final->cache_key ||
      interactive->validation_sample_count < 8'000U ||
      final->validation_sample_count < 8'000U ||
      interactive->measured_p99_delta_e_00 >
          interactive->settings.p99_delta_e_00_limit ||
      interactive->measured_maximum_delta_e_00 >
          interactive->settings.maximum_delta_e_00_limit ||
      final->measured_p99_delta_e_00 >
          final->settings.p99_delta_e_00_limit ||
      final->measured_maximum_delta_e_00 >
          final->settings.maximum_delta_e_00_limit) {
    const auto stats = pipeline::icc_transform_lut_cache_stats();
    std::cerr << "ICC LUT compile failure: " << stats.last_failure_reason << '\n';
    return fail("measured ICC LUT quality tiers should compile within their gates");
  }

  std::vector<pipeline::IccRgbaFloat> inputs;
  inputs.reserve(16'384U);
  auto random_state = std::uint32_t{0x12345678U};
  auto random_unit = [&]() {
    random_state = random_state * 1664525U + 1013904223U;
    return static_cast<float>(
        static_cast<double>(random_state) /
        static_cast<double>(std::numeric_limits<std::uint32_t>::max()));
  };
  for (std::size_t index = 0U; index < 16'384U; ++index) {
    inputs.push_back({
        .red = random_unit(),
        .green = random_unit(),
        .blue = random_unit(),
        .alpha = random_unit(),
    });
  }
  auto exact = inputs;
  auto approximate = inputs;
  auto hybrid_stats = pipeline::IccTransformLutApplyStats{};
  if (!transform.apply(exact) ||
      !pipeline::apply_icc_transform_lut_hybrid(
          *final,
          transform,
          approximate,
          &hybrid_stats) ||
      hybrid_stats.lut_pixels == 0U ||
      hybrid_stats.exact_fallback_pixels == 0U ||
      hybrid_stats.lut_pixels + hybrid_stats.exact_fallback_pixels !=
          inputs.size()) {
    return fail("exact and LUT ICC processors should apply to bounded float pixels");
  }
  std::vector<double> errors;
  errors.reserve(inputs.size() * 3U);
  for (std::size_t index = 0U; index < inputs.size(); ++index) {
    if (exact[index].alpha != inputs[index].alpha ||
        approximate[index].alpha != inputs[index].alpha) {
      return fail("exact and LUT ICC processors must preserve unassociated alpha exactly");
    }
    errors.push_back(std::abs(
        static_cast<double>(approximate[index].red) - exact[index].red));
    errors.push_back(std::abs(
        static_cast<double>(approximate[index].green) - exact[index].green));
    errors.push_back(std::abs(
        static_cast<double>(approximate[index].blue) - exact[index].blue));
  }
  std::sort(errors.begin(), errors.end());
  const auto p99_index = static_cast<std::size_t>(
      std::ceil(0.99 * static_cast<double>(errors.size()))) - 1U;
  if (errors[p99_index] > 0.02 || errors.back() > 0.08) {
    return fail("an independent seeded corpus should remain channel-bounded");
  }

  std::array<pipeline::IccRgbaFloat, 2> extended = {{
      {.red = -0.01F, .green = 0.2F, .blue = 0.3F, .alpha = 0.4F},
      {.red = 0.1F, .green = 1.01F, .blue = 0.3F, .alpha = 0.5F},
  }};
  const auto extended_before = extended;
  if (pipeline::apply_icc_transform_lut(*final, extended) ||
      !same_pixel(extended[0], extended_before[0]) ||
      !same_pixel(extended[1], extended_before[1])) {
    return fail("signed or over-range ICC inputs should reject the LUT atomically");
  }
  std::array<pipeline::IccRgbaFloat, 1> non_finite = {{
      {.red = std::numeric_limits<float>::quiet_NaN(),
       .green = 0.2F,
       .blue = 0.3F,
       .alpha = 0.4F},
  }};
  if (pipeline::apply_icc_transform_lut(*final, non_finite) ||
      !std::isnan(non_finite[0].red)) {
    return fail("non-finite ICC inputs should reject the LUT without mutation");
  }

  const auto final_again = pipeline::compile_icc_transform_lut(
      transform,
      pipeline::icc_transform_lut_settings(
          pipeline::IccTransformLutQuality::Final));
  const auto cache_stats = pipeline::icc_transform_lut_cache_stats();
  if (final_again != final || cache_stats.hits < 1U ||
      cache_stats.builds != 2U || cache_stats.entries != 2U ||
      cache_stats.capacity != 2U ||
      cache_stats.maximum_bytes != 32U * 1024U * 1024U ||
      cache_stats.bytes > cache_stats.maximum_bytes ||
      cache_stats.bytes != interactive->byte_size() + final->byte_size()) {
    return fail("ICC LUTs should use a complete-identity bounded cache");
  }

  const auto device_transform = pipeline::compile_builtin_to_icc_transform(
      pipeline::make_linear_srgb_color_space(),
      destination,
      pipeline::IccTransformOptions{
          .rendering_intent = pipeline::IccRenderingIntent::Perceptual,
          .black_point_compensation = true,
          .pixel_format = pipeline::IccPixelFormat::RgbaFloat32,
          .target = "icc-native-device-hybrid-lut-test",
      });
  const auto device_lut = pipeline::compile_icc_transform_lut(
      device_transform,
      pipeline::icc_transform_lut_settings(
          pipeline::IccTransformLutQuality::Final));
  if (!device_transform.ready() || device_lut == nullptr ||
      !device_lut->valid() || device_lut->unsafe_cell_count == 0U ||
      std::ranges::none_of(
          device_lut->unsafe_cells,
          [](std::uint8_t value) { return value != 0U; })) {
    return fail(
        "device LUT validation must retain cells that require exact correction");
  }

  std::atomic<bool> concurrent_ok = true;
  std::vector<std::thread> workers;
  for (int worker = 0; worker < 8; ++worker) {
    workers.emplace_back([&]() {
      auto sample = std::array<pipeline::IccRgbaFloat, 1>{{
          {.red = 0.25F, .green = 0.5F, .blue = 0.75F, .alpha = 0.33F},
      }};
      for (int iteration = 0; iteration < 100; ++iteration) {
        auto value = sample;
        if (!pipeline::apply_icc_transform_lut(*final, value) ||
            value[0].alpha != sample[0].alpha) {
          concurrent_ok.store(false);
          return;
        }
      }
    });
  }
  for (auto& worker : workers) {
    worker.join();
  }
  if (!concurrent_ok.load()) {
    return fail("compiled ICC LUTs should be immutable and thread safe");
  }

  pipeline::clear_icc_transform_lut_cache_for_testing();
  std::array<std::future<std::shared_ptr<const pipeline::IccTransformLut3D>>, 8>
      concurrent_compiles;
  for (auto& future : concurrent_compiles) {
    future = std::async(std::launch::async, [&]() {
      return pipeline::compile_icc_transform_lut(
          transform,
          pipeline::icc_transform_lut_settings(
              pipeline::IccTransformLutQuality::Final));
    });
  }
  std::shared_ptr<const pipeline::IccTransformLut3D> concurrent_identity;
  for (auto& future : concurrent_compiles) {
    const auto compiled = future.get();
    if (compiled == nullptr) {
      return fail("concurrent ICC LUT compilation should succeed");
    }
    if (concurrent_identity == nullptr) {
      concurrent_identity = compiled;
    } else if (concurrent_identity != compiled) {
      return fail("concurrent ICC LUT requests should converge on one immutable object");
    }
  }
  const auto concurrent_cache_stats =
      pipeline::icc_transform_lut_cache_stats();
  if (concurrent_cache_stats.builds != 1U ||
      concurrent_cache_stats.entries != 1U ||
      concurrent_cache_stats.hits < 7U) {
    return fail("ICC LUT construction should deduplicate in-flight requests");
  }

  const auto invalid = pipeline::compile_icc_transform_lut(
      transform,
      pipeline::IccTransformLutSettings{
          .cube_size = 1,
          .input_shaper_exponent = 1.0 / 3.0,
          .p99_delta_e_00_limit = 0.0,
          .maximum_delta_e_00_limit = 0.0,
      });
  if (invalid != nullptr ||
      pipeline::icc_transform_lut_cache_stats().last_failure_reason !=
          "invalid_icc_transform_lut_settings") {
    return fail("invalid ICC LUT settings should fail closed with a stable reason");
  }

  return EXIT_SUCCESS;
}
