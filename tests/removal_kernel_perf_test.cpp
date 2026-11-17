#include "color_mask_runtime.h"
#include "color_blur_runtime.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace photo_editor::pipeline {
[[nodiscard]] std::vector<double> grow_mask_weights(
    const std::vector<double>& mask,
    int width,
    int height,
    int radius);
[[nodiscard]] std::vector<ColorPixel> low_frequency_pixels(
    const std::vector<ColorPixel>& pixels,
    int width,
    int height,
    int radius,
    CancellationToken cancellation = {});
}  // namespace photo_editor::pipeline

namespace {

using namespace photo_editor::pipeline;
using Clock = std::chrono::steady_clock;

constexpr auto kWarmupCount = 1U;
constexpr auto kTrialCount = 5U;
constexpr auto kExclusionRadius = 2;

struct Workload final {
  std::string_view name;
  int width = 0;
  int height = 0;
  std::vector<ColorPixel> source;
  std::vector<double> removal_mask;
};

[[nodiscard]] std::vector<ColorPixel> make_source(int width, int height) {
  auto pixels = std::vector<ColorPixel>(
      static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const auto index = static_cast<std::size_t>(y) *
                             static_cast<std::size_t>(width) +
                         static_cast<std::size_t>(x);
      pixels[index] = ColorPixel{
          .red = static_cast<float>((x * 17 + y * 29) % 401) / 100.0F - 1.0F,
          .green = 0.8F + static_cast<float>((x * 7 + y * 11) % 251) / 100.0F,
          .blue = -0.2F + static_cast<float>((x * 13 + y * 5) % 190) / 100.0F,
          .alpha = 0.15F + static_cast<float>((x + y) % 4) * 0.25F,
      };
    }
  }
  return pixels;
}

[[nodiscard]] std::uint64_t pixel_hash(const std::vector<ColorPixel>& pixels) {
  auto hash = std::uint64_t{14'695'981'039'346'656'037ULL};
  for (const auto& pixel : pixels) {
    const auto channels = std::array{
        std::bit_cast<std::uint32_t>(pixel.red),
        std::bit_cast<std::uint32_t>(pixel.green),
        std::bit_cast<std::uint32_t>(pixel.blue),
        std::bit_cast<std::uint32_t>(pixel.alpha),
    };
    for (const auto channel : channels) {
      hash ^= channel;
      hash *= 1'099'511'628'211ULL;
    }
  }
  return hash;
}

[[nodiscard]] bool same_pixel(const ColorPixel& left, const ColorPixel& right) {
  return std::bit_cast<std::uint32_t>(left.red) ==
             std::bit_cast<std::uint32_t>(right.red) &&
         std::bit_cast<std::uint32_t>(left.green) ==
             std::bit_cast<std::uint32_t>(right.green) &&
         std::bit_cast<std::uint32_t>(left.blue) ==
             std::bit_cast<std::uint32_t>(right.blue) &&
         std::bit_cast<std::uint32_t>(left.alpha) ==
             std::bit_cast<std::uint32_t>(right.alpha);
}

[[nodiscard]] bool same_pixel_vector(
    const std::vector<ColorPixel>& left,
    const std::vector<ColorPixel>& right) {
  if (left.size() != right.size()) return false;
  for (std::size_t index = 0; index < left.size(); ++index) {
    if (!same_pixel(left[index], right[index])) return false;
  }
  return true;
}

[[nodiscard]] std::uint64_t double_hash(const std::vector<double>& values) {
  auto hash = std::uint64_t{14'695'981'039'346'656'037ULL};
  for (const auto value : values) {
    hash ^= std::bit_cast<std::uint64_t>(value);
    hash *= 1'099'511'628'211ULL;
  }
  return hash;
}

[[nodiscard]] bool same_double_vector(
    const std::vector<double>& left,
    const std::vector<double>& right) {
  if (left.size() != right.size()) return false;
  for (std::size_t index = 0; index < left.size(); ++index) {
    if (std::bit_cast<std::uint64_t>(left[index]) !=
        std::bit_cast<std::uint64_t>(right[index])) {
      return false;
    }
  }
  return true;
}

[[gnu::noinline, nodiscard]] std::vector<ColorPixel> reference_low_frequency(
    const std::vector<ColorPixel>& pixels,
    int width,
    int height,
    int radius) {
  auto red = std::vector<float>(pixels.size());
  auto green = std::vector<float>(pixels.size());
  auto blue = std::vector<float>(pixels.size());
  for (std::size_t index = 0; index < pixels.size(); ++index) {
    red[index] = pixels[index].red;
    green[index] = pixels[index].green;
    blue[index] = pixels[index].blue;
  }
  const auto blurred_red = box_blur(red, width, height, radius);
  const auto blurred_green = box_blur(green, width, height, radius);
  const auto blurred_blue = box_blur(blue, width, height, radius);
  auto result = std::vector<ColorPixel>(pixels.size());
  for (std::size_t index = 0; index < pixels.size(); ++index) {
    result[index] = ColorPixel{
        .red = blurred_red[index],
        .green = blurred_green[index],
        .blue = blurred_blue[index],
        .alpha = pixels[index].alpha,
    };
  }
  return result;
}

[[gnu::noinline, nodiscard]] std::vector<double> reference_grow_mask_weights(
    const std::vector<double>& mask,
    int width,
    int height,
    int radius) {
  struct Row final {
    int y;
    int first_x;
    int last_x;
  };
  const auto radius_one = std::array<Row, 3>{
      {{-1, 0, 0}, {0, -1, 1}, {1, 0, 0}}};
  const auto radius_two = std::array<Row, 5>{
      {{-2, 0, 0}, {-1, -1, 1}, {0, -2, 2}, {1, -1, 1}, {2, 0, 0}}};
  auto result = mask;
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      auto maximum = std::clamp(
          mask[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
               static_cast<std::size_t>(x)],
          0.0,
          1.0);
      const auto include = [&](const Row& row) {
        const auto sample_y = y + row.y;
        if (sample_y < 0 || sample_y >= height) return;
        for (auto offset_x = row.first_x; offset_x <= row.last_x; ++offset_x) {
          const auto sample_x = x + offset_x;
          if (sample_x < 0 || sample_x >= width) continue;
          maximum = std::max(maximum, std::clamp(
              mask[static_cast<std::size_t>(sample_y) * static_cast<std::size_t>(width) +
                   static_cast<std::size_t>(sample_x)],
              0.0,
              1.0));
        }
      };
      if (radius == 1) {
        for (const auto& row : radius_one) include(row);
      } else if (radius == 2) {
        for (const auto& row : radius_two) include(row);
      } else {
        for (int offset_y = -radius; offset_y <= radius; ++offset_y) {
          const auto sample_y = y + offset_y;
          if (sample_y < 0 || sample_y >= height) continue;
          for (int offset_x = -radius; offset_x <= radius; ++offset_x) {
            const auto sample_x = x + offset_x;
            if (sample_x < 0 || sample_x >= width ||
                offset_x * offset_x + offset_y * offset_y > radius * radius) continue;
            maximum = std::max(maximum, std::clamp(
                mask[static_cast<std::size_t>(sample_y) * static_cast<std::size_t>(width) +
                     static_cast<std::size_t>(sample_x)],
                0.0,
                1.0));
          }
        }
      }
      result[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
             static_cast<std::size_t>(x)] = maximum;
    }
  }
  return result;
}

[[nodiscard]] std::vector<std::uint8_t> reference_support(
    const Workload& workload) {
  auto support = std::vector<std::uint8_t>(workload.removal_mask.size(), 0U);
  for (int y = 0; y < workload.height; ++y) {
    for (int x = 0; x < workload.width; ++x) {
      auto covered = false;
      for (int offset_y = -kExclusionRadius;
           offset_y <= kExclusionRadius && !covered;
           ++offset_y) {
        for (int offset_x = -kExclusionRadius;
             offset_x <= kExclusionRadius;
             ++offset_x) {
          if (offset_x * offset_x + offset_y * offset_y >
              kExclusionRadius * kExclusionRadius) {
            continue;
          }
          const auto sample_x = x + offset_x;
          const auto sample_y = y + offset_y;
          if (sample_x < 0 || sample_y < 0 || sample_x >= workload.width ||
              sample_y >= workload.height) {
            continue;
          }
          const auto index = static_cast<std::size_t>(sample_y) *
                                 static_cast<std::size_t>(workload.width) +
                             static_cast<std::size_t>(sample_x);
          if (workload.removal_mask[index] > 1e-4) {
            covered = true;
            break;
          }
        }
      }
      const auto index = static_cast<std::size_t>(y) *
                             static_cast<std::size_t>(workload.width) +
                         static_cast<std::size_t>(x);
      support[index] = covered ? 1U : 0U;
    }
  }
  return support;
}

[[nodiscard]] bool write_pixels(
    const std::string& path,
    const std::vector<ColorPixel>& pixels) {
  auto output = std::ofstream(path, std::ios::binary);
  if (!output) return false;
  for (const auto& pixel : pixels) {
    const auto channels = std::array{
        std::bit_cast<std::uint32_t>(pixel.red),
        std::bit_cast<std::uint32_t>(pixel.green),
        std::bit_cast<std::uint32_t>(pixel.blue),
        std::bit_cast<std::uint32_t>(pixel.alpha),
    };
    output.write(reinterpret_cast<const char*>(channels.data()),
                 static_cast<std::streamsize>(sizeof(channels)));
  }
  return static_cast<bool>(output);
}

[[nodiscard]] bool test_mask_growth_matches_reference() {
  constexpr auto kWidth = 5;
  constexpr auto kHeight = 4;
  const auto mask = std::vector<double>{
      1.7, -0.25, 0.0, 0.15, 0.4,
      0.7, 0.95, 1.2, -0.1, 0.3,
      0.05, 0.55, 0.8, 0.25, 0.0,
      0.45, 0.2, 1.4, 0.6, 0.75,
  };

  for (const auto radius : {1, 2}) {
    auto expected = mask;
    for (int y = 0; y < kHeight; ++y) {
      for (int x = 0; x < kWidth; ++x) {
        auto maximum = std::clamp(
            mask[static_cast<std::size_t>(y * kWidth + x)], 0.0, 1.0);
        for (int offset_y = -radius; offset_y <= radius; ++offset_y) {
          const auto sample_y = y + offset_y;
          if (sample_y < 0 || sample_y >= kHeight) continue;
          for (int offset_x = -radius; offset_x <= radius; ++offset_x) {
            const auto sample_x = x + offset_x;
            if (sample_x < 0 || sample_x >= kWidth) continue;
            if (offset_x * offset_x + offset_y * offset_y > radius * radius) {
              continue;
            }
            maximum = std::max(
                maximum,
                std::clamp(
                    mask[static_cast<std::size_t>(sample_y * kWidth + sample_x)],
                    0.0,
                    1.0));
          }
        }
        expected[static_cast<std::size_t>(y * kWidth + x)] = maximum;
      }
    }

    const auto actual = photo_editor::pipeline::grow_mask_weights(
        mask, kWidth, kHeight, radius);
    if (actual != expected) {
      std::cerr << "mask-growth stencil differed from reference at radius "
                << radius << '\n';
      return false;
    }
  }
  return true;
}

[[nodiscard]] std::int64_t median(std::vector<std::int64_t> values);

[[nodiscard]] bool test_growth_sparse_and_dense_masks() {
  constexpr auto kWidth = 17;
  constexpr auto kHeight = 13;
  auto mask = std::vector<double>(kWidth * kHeight, 0.0);
  mask[static_cast<std::size_t>(6 * kWidth + 8)] = 0.35;
  mask[static_cast<std::size_t>(0 * kWidth + 0)] = 0.8;
  mask[static_cast<std::size_t>(12 * kWidth + 16)] = 1.4;
  mask[static_cast<std::size_t>(4 * kWidth + 7)] = -0.5;
  mask[static_cast<std::size_t>(2 * kWidth + 3)] =
      std::numeric_limits<double>::quiet_NaN();
  mask[static_cast<std::size_t>(10 * kWidth + 1)] =
      std::numeric_limits<double>::infinity();
  auto expected = mask;
  for (int y = 0; y < kHeight; ++y) {
    for (int x = 0; x < kWidth; ++x) {
      auto maximum = std::clamp(mask[static_cast<std::size_t>(y * kWidth + x)], 0.0, 1.0);
      for (int offset_y = -3; offset_y <= 3; ++offset_y) {
        for (int offset_x = -3; offset_x <= 3; ++offset_x) {
          if (offset_x * offset_x + offset_y * offset_y > 9) continue;
          const auto sample_x = x + offset_x;
          const auto sample_y = y + offset_y;
          if (sample_x < 0 || sample_y < 0 || sample_x >= kWidth || sample_y >= kHeight) continue;
          maximum = std::max(maximum, std::clamp(
              mask[static_cast<std::size_t>(sample_y * kWidth + sample_x)], 0.0, 1.0));
        }
      }
      expected[static_cast<std::size_t>(y * kWidth + x)] = maximum;
    }
  }
  if (!same_double_vector(grow_mask_weights(mask, kWidth, kHeight, 3), expected)) {
    std::cerr << "sparse and border growth changed radius-three output\n";
    return false;
  }
  const auto dense = std::vector<double>(kWidth * kHeight, 0.4);
  if (!same_double_vector(grow_mask_weights(dense, kWidth, kHeight, 2), dense)) {
    std::cerr << "dense mask growth changed exact output\n";
    return false;
  }
  return true;
}

[[nodiscard]] bool test_growth_special_values() {
  constexpr auto kWidth = 9;
  constexpr auto kHeight = 7;
  auto mask = std::vector<double>(kWidth * kHeight, 0.0);
  mask[0] = -0.0;
  mask[static_cast<std::size_t>(2 * kWidth + 3)] =
      std::bit_cast<double>(std::uint64_t{0x7FF8000000001234ULL});
  mask[static_cast<std::size_t>(4 * kWidth + 7)] =
      std::numeric_limits<double>::infinity();
  mask[static_cast<std::size_t>(6 * kWidth + 8)] =
      -std::numeric_limits<double>::infinity();
  mask[static_cast<std::size_t>(3 * kWidth + 5)] = 0.65;

  for (const auto radius : {1, 2}) {
    const auto expected = reference_grow_mask_weights(mask, kWidth, kHeight, radius);
    const auto actual = grow_mask_weights(mask, kWidth, kHeight, radius);
    if (!same_double_vector(actual, expected)) {
      std::cerr << "mask growth changed signed-zero/NaN/infinity bits at radius "
                << radius << '\n';
      return false;
    }
  }
  return true;
}

[[nodiscard]] bool run_growth_benchmark(
    std::string_view name,
    int width,
    int height,
    bool dense) {
  auto mask = std::vector<double>(
      static_cast<std::size_t>(width) * static_cast<std::size_t>(height), 0.0);
  if (dense) {
    std::fill(mask.begin(), mask.end(), 0.4);
  } else {
    mask[static_cast<std::size_t>(height / 2) * static_cast<std::size_t>(width) +
         static_cast<std::size_t>(width / 2)] = 1.0;
  }
  auto times = std::vector<std::int64_t>{};
  times.reserve(kTrialCount);
  const auto expected = reference_grow_mask_weights(mask, width, height, 2);
  const auto expected_hash = double_hash(expected);
  for (auto iteration = 0U; iteration < kWarmupCount + kTrialCount; ++iteration) {
    const auto start = Clock::now();
    const auto result = grow_mask_weights(mask, width, height, 2);
    const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
                             Clock::now() - start)
                             .count();
    if (result.size() != mask.size() ||
        !same_double_vector(result, expected) || double_hash(result) != expected_hash) {
      std::cerr << "sparse mask growth differs from the previous dense implementation\n";
      return false;
    }
    if (iteration >= kWarmupCount) times.push_back(elapsed);
  }
  std::cout << "mask_growth_benchmark case=" << name
            << " width=" << width << " height=" << height
            << " trials=" << kTrialCount
            << " median_us=" << median(std::move(times))
            << " output_hash=" << expected_hash << '\n';
  return true;
}

[[nodiscard]] bool run_low_frequency_benchmark() {
  constexpr auto kWidth = 1024;
  constexpr auto kHeight = 768;
  constexpr auto kRadius = 2;
  const auto source = make_source(kWidth, kHeight);
  const auto expected = reference_low_frequency(source, kWidth, kHeight, kRadius);
  const auto expected_hash = pixel_hash(expected);
  const auto cancelled = std::atomic_bool{true};
  if (!low_frequency_pixels(
           source, kWidth, kHeight, kRadius,
           CancellationToken{.cancellation_requested = &cancelled})
           .empty()) {
    std::cerr << "pre-cancelled low-frequency blur returned pixels\n";
    return false;
  }
  auto times = std::vector<std::int64_t>{};
  times.reserve(kTrialCount);
  for (auto iteration = 0U; iteration < kWarmupCount + kTrialCount; ++iteration) {
    const auto start = Clock::now();
    const auto result = low_frequency_pixels(source, kWidth, kHeight, kRadius);
    const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
                             Clock::now() - start)
                             .count();
    if (result.size() != expected.size() || !same_pixel_vector(result, expected) ||
        pixel_hash(result) != expected_hash) {
      std::cerr << "low-frequency fused blur differs from scalar-channel reference\n";
      return false;
    }
    if (iteration >= kWarmupCount) times.push_back(elapsed);
  }
  std::cout << "low_frequency_benchmark case=rgb_1024x768_radius2"
            << " trials=" << kTrialCount
            << " median_us=" << median(std::move(times))
            << " rgba_hash=" << expected_hash << '\n';
  return true;
}

[[nodiscard]] std::int64_t median(std::vector<std::int64_t> values) {
  std::sort(values.begin(), values.end());
  return values.at(values.size() / 2U);
}

[[nodiscard]] bool run_workload(
    const Workload& workload,
    const std::string& dump_prefix) {
  const auto support = reference_support(workload);
  auto times = std::vector<std::int64_t>{};
  times.reserve(kTrialCount);
  auto expected_hash = std::uint64_t{0};
  auto final_pixels = workload.source;
  for (auto iteration = 0U; iteration < kWarmupCount + kTrialCount; ++iteration) {
    auto pixels = workload.source;
    const auto start = Clock::now();
    apply_local_removal(
        &pixels,
        workload.width,
        workload.height,
        workload.removal_mask,
        0.0,
        RemovalQuality::Final,
        1.0,
        1.0);
    const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
                             Clock::now() - start)
                             .count();
    const auto actual_hash = pixel_hash(pixels);
    if (iteration == 0U) {
      expected_hash = actual_hash;
    } else if (actual_hash != expected_hash) {
      std::cerr << "removal result changed across identical trials: "
                << workload.name << '\n';
      return false;
    }
    if (iteration >= kWarmupCount) times.push_back(elapsed);
    final_pixels = std::move(pixels);
  }

  auto changed_pixels = std::size_t{0};
  for (std::size_t index = 0; index < final_pixels.size(); ++index) {
    const auto& input = workload.source[index];
    const auto& output = final_pixels[index];
    if (!std::isfinite(output.red) || !std::isfinite(output.green) ||
        !std::isfinite(output.blue) || !std::isfinite(output.alpha)) {
      std::cerr << "removal produced a non-finite channel: " << workload.name << '\n';
      return false;
    }
    if (std::bit_cast<std::uint32_t>(output.alpha) !=
        std::bit_cast<std::uint32_t>(input.alpha)) {
      std::cerr << "removal changed alpha: " << workload.name << '\n';
      return false;
    }
    if (support[index] == 0U && !same_pixel(input, output)) {
      std::cerr << "removal changed a pixel outside its source-mask support: "
                << workload.name << '\n';
      return false;
    }
    changed_pixels += same_pixel(input, output) ? 0U : 1U;
  }
  if (changed_pixels == 0U) {
    std::cerr << "removal fixture did not exercise any painted pixels: "
              << workload.name << '\n';
    return false;
  }
  if (!dump_prefix.empty() &&
      !write_pixels(dump_prefix + "." + std::string(workload.name) + ".rgba", final_pixels)) {
    std::cerr << "could not write exact pixel dump for " << workload.name << '\n';
    return false;
  }

  std::cout << "removal_benchmark case=" << workload.name
            << " width=" << workload.width
            << " height=" << workload.height
            << " trials=" << kTrialCount
            << " median_us=" << median(std::move(times))
            << " changed_pixels=" << changed_pixels
            << " rgba_hash=" << expected_hash << '\n';
  return true;
}

[[nodiscard]] Workload tiny_mark_workload() {
  constexpr auto kWidth = 1600;
  constexpr auto kHeight = 1200;
  auto mask = std::vector<double>(
      static_cast<std::size_t>(kWidth) * static_cast<std::size_t>(kHeight), 0.0);
  mask[static_cast<std::size_t>(kHeight / 2) * static_cast<std::size_t>(kWidth) +
       static_cast<std::size_t>(kWidth / 2)] = 1.0;
  return Workload{
      .name = "tiny_mark_final_removal",
      .width = kWidth,
      .height = kHeight,
      .source = make_source(kWidth, kHeight),
      .removal_mask = std::move(mask),
  };
}

[[nodiscard]] Workload compact_mark_workload() {
  constexpr auto kWidth = 640;
  constexpr auto kHeight = 512;
  auto mask = std::vector<double>(
      static_cast<std::size_t>(kWidth) * static_cast<std::size_t>(kHeight), 0.0);
  for (int y = kHeight / 2 - 8; y < kHeight / 2 + 8; ++y) {
    for (int x = kWidth / 2 - 8; x < kWidth / 2 + 8; ++x) {
      mask[static_cast<std::size_t>(y) * static_cast<std::size_t>(kWidth) +
           static_cast<std::size_t>(x)] = 1.0;
    }
  }
  return Workload{
      .name = "compact_mark_final_removal",
      .width = kWidth,
      .height = kHeight,
      .source = make_source(kWidth, kHeight),
      .removal_mask = std::move(mask),
  };
}

}  // namespace

int main(int argc, char** argv) {
  auto dump_prefix = std::string{};
  if (argc == 3 && std::string_view(argv[1]) == "--dump-prefix") {
    dump_prefix = argv[2];
  } else if (argc != 1) {
    std::cerr << "Usage: removal_kernel_perf_test [--dump-prefix PATH]\n";
    return EXIT_FAILURE;
  }

  if (!test_mask_growth_matches_reference()) return EXIT_FAILURE;
  if (!test_growth_sparse_and_dense_masks() ||
      !test_growth_special_values() ||
      !run_growth_benchmark("tiny_1600x1200", 1600, 1200, false) ||
      !run_growth_benchmark("dense_640x512", 640, 512, true) ||
      !run_low_frequency_benchmark()) {
    return EXIT_FAILURE;
  }

  const auto cancelled = std::atomic_bool{true};
  {
    const auto cancellation_fixture = tiny_mark_workload();
    auto unchanged = cancellation_fixture.source;
    apply_local_removal(
        &unchanged,
        cancellation_fixture.width,
        cancellation_fixture.height,
        cancellation_fixture.removal_mask,
        0.0,
        RemovalQuality::Final,
        1.0,
        1.0,
        CancellationToken{.cancellation_requested = &cancelled});
    if (unchanged != cancellation_fixture.source) {
      std::cerr << "pre-cancelled removal modified input pixels\n";
      return EXIT_FAILURE;
    }
  }

  if (!run_workload(tiny_mark_workload(), dump_prefix) ||
      !run_workload(compact_mark_workload(), dump_prefix)) {
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
