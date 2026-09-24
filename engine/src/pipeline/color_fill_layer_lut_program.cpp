#include "color_fill_layer_lut_program.h"

#include "color_fill_layer_look_program.h"

namespace photo_editor::pipeline {

CompiledUserLutStageProgram compile_retained_layer_user_luts(
    const PhotoLayer& layer, const ColorAdjustmentState& state) {
  return compile_user_lut_stage_program(
      retained_layer_downstream_look_state(layer, state));
}

}  // namespace photo_editor::pipeline
