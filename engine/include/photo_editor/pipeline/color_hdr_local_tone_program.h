#pragma once

#include "photo_editor/pipeline/color_adjustments.h"

#include <array>
#include <cstdint>
#include <vector>

namespace photo_editor::pipeline {

struct CompiledHdrLocalToneProgram final {
  std::array<float, 3> luminance_coefficients{};
  float strength = 0.0F;
  float output_headroom = 1.0F;
  std::uint32_t radius = 1U;
  std::uint32_t width = 0U;
  std::uint32_t height = 0U;
  bool valid = false;

  [[nodiscard]] bool ready() const noexcept;
  [[nodiscard]] bool active() const noexcept;
};

[[nodiscard]] CompiledHdrLocalToneProgram compile_hdr_local_tone_program(
    const ColorAdjustmentState& state,
    int width,
    int height);

[[nodiscard]] bool apply_compiled_hdr_local_tone_program(
    std::vector<ColorPixel>* pixels,
    const CompiledHdrLocalToneProgram& program);

}  // namespace photo_editor::pipeline
