#pragma once

#include "photo_editor/pipeline/color_adjustments.h"

#include <array>
#include <cstdint>
#include <vector>

namespace photo_editor::pipeline {

struct CompiledSpatialDetailProgram final {
  static constexpr std::uint32_t kTexture = 1U << 0U;
  static constexpr std::uint32_t kClarity = 1U << 1U;
  static constexpr std::uint32_t kDehaze = 1U << 2U;

  std::array<float, 3> luminance_coefficients{};
  float texture_strength = 0.0F;
  float clarity_strength = 0.0F;
  float dehaze_strength = 0.0F;
  float output_headroom = 1.0F;
  std::uint32_t width = 0U;
  std::uint32_t height = 0U;
  std::uint32_t active_mask = 0U;
  bool valid = false;

  [[nodiscard]] bool ready() const noexcept;
  [[nodiscard]] bool active() const noexcept;
};

struct CompiledFineDetailProgram final {
  static constexpr std::uint32_t kSharpen = 1U << 0U;
  static constexpr std::uint32_t kLuminanceNoiseReduction = 1U << 1U;
  static constexpr std::uint32_t kColorNoiseReduction = 1U << 2U;

  ColorMatrix3 rgb_to_xyz_d65;
  ColorMatrix3 xyz_d65_to_rgb;
  float sharpen_strength = 0.0F;
  float sharpen_gain = 1.9F;
  float sharpen_mask_strength = 0.0F;
  float luminance_nr_strength = 0.0F;
  float color_nr_strength = 0.0F;
  std::uint32_t sharpen_radius = 1U;
  std::uint32_t sharpen_edge_radius = 3U;
  std::uint32_t width = 0U;
  std::uint32_t height = 0U;
  std::uint32_t active_mask = 0U;
  bool valid = false;

  [[nodiscard]] bool ready() const noexcept;
  [[nodiscard]] bool active() const noexcept;
};

[[nodiscard]] CompiledSpatialDetailProgram
compile_interactive_spatial_detail_program(
    const ColorAdjustmentState& state,
    int width,
    int height);

[[nodiscard]] CompiledFineDetailProgram compile_fine_detail_program(
    const ColorAdjustmentState& state,
    int width,
    int height);

[[nodiscard]] bool apply_compiled_interactive_spatial_detail_program(
    std::vector<ColorPixel>* pixels,
    const CompiledSpatialDetailProgram& program);

[[nodiscard]] bool apply_compiled_fine_detail_program(
    std::vector<ColorPixel>* pixels,
    const CompiledFineDetailProgram& program);

}  // namespace photo_editor::pipeline
