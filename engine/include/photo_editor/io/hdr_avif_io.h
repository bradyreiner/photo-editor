#pragma once

#include <cstddef>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace photo_editor::io {

enum class HdrAvifTransfer {
  Pq,
  Hlg,
};

struct HdrAvifWriteOptions final {
  int width = 0;
  int height = 0;
  std::size_t row_stride_floats = 0U;
  int bit_depth = 10;
  int quality = 90;
  HdrAvifTransfer transfer = HdrAvifTransfer::Pq;
  double peak_luminance_nits = 1000.0;
  double black_luminance_nits = 0.0005;
  double reference_white_nits = 203.0;
  int max_content_light_level = 1000;
  int max_frame_average_light_level = 400;
  std::function<bool()> cancellation_requested;
};

struct HdrAvifInspection final {
  bool valid = false;
  int width = 0;
  int height = 0;
  int bit_depth = 0;
  int color_primaries = 0;
  int transfer_characteristics = 0;
  int matrix_coefficients = 0;
  bool full_range = false;
  int max_content_light_level = 0;
  int max_frame_average_light_level = 0;
  bool has_mastering_display = false;
  double mastering_peak_luminance_nits = 0.0;
  double mastering_black_luminance_nits = 0.0;
  std::vector<float> encoded_rgb;
  std::string failure_reason;
};

[[nodiscard]] bool hdr_avif_backend_available();
[[nodiscard]] std::string_view hdr_avif_backend_version();
[[nodiscard]] bool write_hdr_avif_rec2020(
    std::string_view file_path,
    std::span<const float> linear_reference_relative_rgba,
    const HdrAvifWriteOptions& options,
    std::string* failure_reason = nullptr);
[[nodiscard]] HdrAvifInspection inspect_hdr_avif(
    std::string_view file_path,
    bool read_pixels = true);

}  // namespace photo_editor::io
