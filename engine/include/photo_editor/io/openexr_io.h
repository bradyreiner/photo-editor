#pragma once

#include "photo_editor/pipeline/color_space.h"

#include <cstddef>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace photo_editor::io {

struct OpenExrChromaticities final {
  float red_x = 0.0F;
  float red_y = 0.0F;
  float green_x = 0.0F;
  float green_y = 0.0F;
  float blue_x = 0.0F;
  float blue_y = 0.0F;
  float white_x = 0.0F;
  float white_y = 0.0F;
};

struct OpenExrWriteOptions final {
  int width = 0;
  int height = 0;
  int data_window_min_x = 0;
  int data_window_min_y = 0;
  std::size_t row_stride_floats = 0U;
  bool half_float = true;
  bool include_alpha = true;
  pipeline::RgbColorSpace color_space;
  float white_luminance_nits = 100.0F;
  std::string rendering_model;
  std::string transform_provenance;
  std::function<bool()> cancellation_requested;
};

struct OpenExrInspection final {
  bool valid = false;
  int width = 0;
  int height = 0;
  bool has_alpha = false;
  std::string channel_type;
  std::string compression;
  OpenExrChromaticities chromaticities;
  float white_luminance_nits = 0.0F;
  std::string color_space_name;
  std::string transfer_function;
  std::string rendering_model;
  std::string transform_provenance;
  std::size_t non_finite_sample_count = 0U;
  std::vector<float> rgba_pixels;
  std::string failure_reason;
};

[[nodiscard]] bool openexr_backend_available();
[[nodiscard]] std::string_view openexr_backend_version();
[[nodiscard]] OpenExrChromaticities openexr_chromaticities_for_color_space(
    const pipeline::RgbColorSpace& color_space);
[[nodiscard]] bool write_openexr_rgba(
    std::string_view file_path,
    std::span<const float> rgba_pixels,
    const OpenExrWriteOptions& options,
    std::string* failure_reason = nullptr);
[[nodiscard]] OpenExrInspection inspect_openexr(
    std::string_view file_path,
    bool read_pixels = true);

}  // namespace photo_editor::io
