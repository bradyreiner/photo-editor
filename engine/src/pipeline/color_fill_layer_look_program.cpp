#include "color_fill_layer_look_program.h"

#include "color_layer_runtime.h"
#include "color_lut_runtime.h"

#include <cmath>
#include <ranges>

namespace photo_editor::pipeline {

namespace {

[[nodiscard]] bool finite_program(const CompiledLookStageProgram& program) {
  const auto finite = [](auto value) { return std::isfinite(value); };
  const auto finite_array = [&](const auto& values) {
    return std::ranges::all_of(values, finite);
  };
  return finite_array(program.rgb_to_xyz_d65.values) &&
         finite_array(program.xyz_d65_to_rgb.values) &&
         finite_array(program.luminance_coefficients) &&
         finite_array(program.rgb_mixer) &&
         finite_array(program.black_and_white_mixer) &&
         finite_array(program.grading_hue) &&
         finite_array(program.grading_saturation) &&
         finite_array(program.grading_luminance) &&
         finite_array(program.mixer_hue) &&
         finite_array(program.mixer_saturation) &&
         finite_array(program.mixer_luminance) &&
         finite_array(program.selective_cyan) &&
         finite_array(program.selective_magenta) &&
         finite_array(program.selective_yellow) &&
         finite_array(program.selective_black) &&
         finite_array(program.harmony_anchors) &&
         finite(program.saturation_strength) && finite(program.vibrance_strength) &&
         finite(program.grading_amount) && finite(program.grading_balance) &&
         finite(program.harmony_strength) && finite(program.output_headroom) &&
         finite(program.replacement_source_hue) &&
         finite(program.replacement_target_oklab_hue) &&
         finite(program.replacement_range) && finite(program.replacement_softness) &&
         finite(program.replacement_strength) && finite(program.skin_hue) &&
         finite(program.skin_range) && finite(program.skin_strength);
}

}  // namespace

ColorAdjustmentState retained_layer_downstream_look_state(
    const PhotoLayer& layer, const ColorAdjustmentState& state) {
  auto look_state = photo_layer_color_adjustment_state(layer);
  look_state.color_pipeline = state.color_pipeline;
  return look_state;
}

bool retained_layer_downstream_look_supported(
    const PhotoLayer& layer, const ColorAdjustmentState& state,
    const PipelineColorContext& context) {
  (void)context;
  const auto look_state = retained_layer_downstream_look_state(layer, state);
  if (!user_lut_state_supports_compiled_execution(look_state)) {
    return false;
  }
  const auto program = compile_look_stage_program(look_state);
  return program.active_mask == 0U || (program.ready() && finite_program(program));
}

void append_retained_layer_downstream_look(
    const PhotoLayer& layer, const ColorAdjustmentState& state,
    std::size_t index, CompiledFillLayerProgram* program) {
  if (program == nullptr) return;
  auto look = compile_look_stage_program(
      retained_layer_downstream_look_state(layer, state));
  if (look.active_mask == 0U) return;
  program->layers[index].has_downstream_look = look.ready();
  program->downstream_look_programs[index] = std::move(look);
}

bool retained_layer_downstream_look_ready(
    const CompiledFillLayerProgram& program, const CompiledFillLayer& layer,
    std::size_t index) {
  return !layer.has_downstream_look ||
         (layer.kind == 2U && program.downstream_look_identity != 0U &&
          program.downstream_look_programs[index].ready() &&
          finite_program(program.downstream_look_programs[index]));
}

void finish_retained_layer_downstream_look_identity(
    CompiledFillLayerProgram* program) {
  if (program == nullptr) return;
  auto identity = std::uint64_t{14695981039346656037ULL};
  identity ^= program->layer_count;
  identity *= 1099511628211ULL;
  auto active = false;
  for (auto index = std::size_t{0U}; index < program->layer_count; ++index) {
    if (!program->layers[index].has_downstream_look) continue;
    active = true;
    identity ^= program->downstream_look_programs[index].cache_identity;
    identity *= 1099511628211ULL;
    identity ^= index;
    identity *= 1099511628211ULL;
  }
  if (active) program->downstream_look_identity = identity != 0U ? identity : 1U;
}

}  // namespace photo_editor::pipeline
