#pragma once

#include "photo_editor/pipeline/color_adjustments.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace photo_editor::pipeline {

struct CompiledUserLut final {
  ColorLut3D lut;
  float strength = 0.0F;
  ColorMatrix3 working_to_input;
  ColorMatrix3 output_to_working;
  TransferFunction input_transfer = TransferFunction::Linear;
  TransferFunction output_transfer = TransferFunction::Linear;
  std::uint64_t cache_identity = 0U;

  [[nodiscard]] bool ready() const;
};

struct CompiledUserLutStageProgram final {
  std::vector<CompiledUserLut> luts;
  std::size_t requested_count = 0U;
  std::uint64_t cache_identity = 0U;

  [[nodiscard]] bool active() const { return requested_count != 0U; }
  [[nodiscard]] bool ready() const;
};

[[nodiscard]] bool user_lut_state_supports_compiled_execution(
    const ColorAdjustmentState& state);
[[nodiscard]] CompiledUserLutStageProgram compile_user_lut_stage_program(
    const ColorAdjustmentState& state);
[[nodiscard]] ColorPixel apply_compiled_user_lut_stage_program(
    const CompiledUserLutStageProgram& program,
    const ColorPixel& pixel);

}  // namespace photo_editor::pipeline
