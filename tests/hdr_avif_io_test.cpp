#include "photo_editor/io/hdr_avif_io.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace {

[[nodiscard]] double pq_signal(double reference_relative, double reference_white_nits) {
  constexpr double m1 = 2610.0 / 16384.0;
  constexpr double m2 = 2523.0 / 32.0;
  constexpr double c1 = 3424.0 / 4096.0;
  constexpr double c2 = 2413.0 / 128.0;
  constexpr double c3 = 2392.0 / 128.0;
  const auto absolute = std::clamp(
      reference_relative * reference_white_nits / 10000.0,
      0.0,
      1.0);
  const auto powered = std::pow(absolute, m1);
  return std::pow((c1 + c2 * powered) / (1.0 + c3 * powered), m2);
}

[[nodiscard]] double hlg_signal(
    double reference_relative,
    double reference_white_nits,
    double peak_luminance_nits) {
  constexpr double a = 0.17883277;
  constexpr double b = 1.0 - 4.0 * a;
  const auto c = 0.5 - a * std::log(4.0 * a);
  const auto target_relative = std::clamp(
      reference_relative * reference_white_nits /
          peak_luminance_nits,
      0.0,
      1.0);
  return target_relative <= (1.0 / 12.0)
             ? std::sqrt(3.0 * target_relative)
             : a * std::log(12.0 * target_relative - b) + c;
}

}  // namespace

int main() {
  if (!photo_editor::io::hdr_avif_backend_available()) {
    std::cout << "SKIP: HDR AVIF backend unavailable\n";
    return 77;
  }
  constexpr int kWidth = 2;
  constexpr int kHeight = 2;
  constexpr double kReferenceWhite = 203.0;
  constexpr double kPeak = 1000.0;
  const auto peak_relative = static_cast<float>(kPeak / kReferenceWhite);
  const std::vector<float> pixels = {
      0.0F, 0.0F, 0.0F, 1.0F,
      0.18F, 0.04F, 0.80F, 1.0F,
      1.0F, 1.0F, 1.0F, 1.0F,
      peak_relative, peak_relative, peak_relative, 1.0F,
  };
  const auto root = std::filesystem::temp_directory_path();
  const auto pq_path = root / "photo-editor-hdr-pq-test.avif";
  const auto hlg_path = root / "photo-editor-hdr-hlg-test.avif";
  std::error_code remove_error;
  std::filesystem::remove(pq_path, remove_error);
  std::filesystem::remove(hlg_path, remove_error);

  const auto write_and_check = [&](const std::filesystem::path& path,
                                   photo_editor::io::HdrAvifTransfer transfer,
                                   int expected_transfer) {
    std::string failure_reason;
    if (!photo_editor::io::write_hdr_avif_rec2020(
            path.string(),
            pixels,
            photo_editor::io::HdrAvifWriteOptions{
                .width = kWidth,
                .height = kHeight,
                .row_stride_floats = static_cast<std::size_t>(kWidth) * 4U,
                .bit_depth = 10,
                .quality = 100,
                .transfer = transfer,
                .peak_luminance_nits = kPeak,
                .black_luminance_nits = 0.0005,
                .reference_white_nits = kReferenceWhite,
                .max_content_light_level = 1000,
                .max_frame_average_light_level = 400,
            },
            &failure_reason)) {
      std::cerr << "HDR AVIF write failed: " << failure_reason << '\n';
      return false;
    }
    const auto inspection = photo_editor::io::inspect_hdr_avif(path.string());
    if (!inspection.valid || inspection.width != kWidth ||
        inspection.height != kHeight || inspection.bit_depth != 10 ||
        inspection.color_primaries != 9 ||
        inspection.transfer_characteristics != expected_transfer ||
        inspection.matrix_coefficients != 9 || !inspection.full_range ||
        inspection.max_content_light_level != 1000 ||
        inspection.max_frame_average_light_level != 400 ||
        !inspection.has_mastering_display ||
        std::abs(inspection.mastering_peak_luminance_nits - kPeak) > 0.1 ||
        std::abs(inspection.mastering_black_luminance_nits - 0.0005) > 0.00011 ||
        inspection.encoded_rgb.size() != 12U) {
      std::cerr << "HDR AVIF metadata/readback contract failed: "
                << inspection.failure_reason << " depth=" << inspection.bit_depth
                << " cp=" << inspection.color_primaries
                << " tc=" << inspection.transfer_characteristics
                << " mc=" << inspection.matrix_coefficients
                << " max=" << inspection.mastering_peak_luminance_nits
                << " min=" << inspection.mastering_black_luminance_nits << '\n';
      return false;
    }
    const auto expected_peak_signal =
        transfer == photo_editor::io::HdrAvifTransfer::Pq
            ? static_cast<float>(pq_signal(peak_relative, kReferenceWhite))
            : 1.0F;
    if (inspection.encoded_rgb[9] <= inspection.encoded_rgb[6] ||
        std::abs(inspection.encoded_rgb[9] - expected_peak_signal) > 0.035F) {
      std::cerr << "HDR AVIF decoded signal must preserve peak above reference white: white="
                << inspection.encoded_rgb[6]
                << " peak=" << inspection.encoded_rgb[9] << '\n';
      return false;
    }
    const std::array<std::array<double, 3>, 4> source_values = {
        std::array<double, 3>{0.0, 0.0, 0.0},
        std::array<double, 3>{0.18, 0.04, 0.80},
        std::array<double, 3>{1.0, 1.0, 1.0},
        std::array<double, 3>{
            static_cast<double>(peak_relative),
            static_cast<double>(peak_relative),
            static_cast<double>(peak_relative)},
    };
    for (std::size_t pixel = 0; pixel < source_values.size(); ++pixel) {
      for (std::size_t channel = 0; channel < 3U; ++channel) {
        const auto expected =
            transfer == photo_editor::io::HdrAvifTransfer::Pq
                ? pq_signal(
                      source_values[pixel][channel],
                      kReferenceWhite)
                : hlg_signal(
                      source_values[pixel][channel],
                      kReferenceWhite,
                      kPeak);
        const auto actual = inspection.encoded_rgb[pixel * 3U + channel];
        if (std::abs(actual - expected) > 0.004) {
          std::cerr
              << "lossless HDR AVIF signal exceeded two-code-value tolerance: pixel="
              << pixel << " channel=" << channel
              << " expected=" << expected << " actual=" << actual << '\n';
          return false;
        }
      }
    }
    if (transfer == photo_editor::io::HdrAvifTransfer::Pq &&
        std::abs(inspection.encoded_rgb[6] -
                 static_cast<float>(pq_signal(1.0, kReferenceWhite))) > 0.035F) {
      std::cerr << "PQ reference-white code did not survive encoded artifact readback\n";
      return false;
    }
    return true;
  };

  if (!write_and_check(pq_path, photo_editor::io::HdrAvifTransfer::Pq, 16) ||
      !write_and_check(hlg_path, photo_editor::io::HdrAvifTransfer::Hlg, 18)) {
    return EXIT_FAILURE;
  }
  std::filesystem::remove(pq_path, remove_error);
  std::filesystem::remove(hlg_path, remove_error);
  return EXIT_SUCCESS;
}
