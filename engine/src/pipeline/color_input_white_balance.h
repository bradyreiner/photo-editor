#pragma once

#include "color_working_space_runtime.h"

namespace photo_editor::pipeline {

[[nodiscard]] Matrix3 input_white_balance_matrix(
    double temperature,
    double tint,
    const PipelineColorContext& context);

// Compatibility entry point used by local-adjustment and fill-layer
// compilation. Keep one implementation while those callers migrate to the
// input-specific name.
[[nodiscard]] Matrix3 white_balance_matrix(
    double temperature,
    double tint,
    const PipelineColorContext& context);

[[nodiscard]] Matrix3 input_white_balance_matrix_for_kelvin(
    double kelvin,
    double tint,
    const PipelineColorContext& context);

[[nodiscard]] Matrix3 input_white_balance_matrix_between_whites(
    double source_kelvin,
    double source_tint,
    double target_kelvin,
    double target_tint,
    const PipelineColorContext& context);

}  // namespace photo_editor::pipeline
