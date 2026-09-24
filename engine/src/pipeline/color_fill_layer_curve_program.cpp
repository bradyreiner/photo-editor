#include "color_fill_layer_curve_program.h"

#include <bit>
#include <cmath>
#include <ranges>

namespace photo_editor::pipeline {

namespace {

constexpr auto kCurveCount = std::size_t{6U};

[[nodiscard]] auto curve_vectors(CompiledCurveStageLuts& curves) {
  return std::array{
      &curves.parametric, &curves.master, &curves.luma,
      &curves.red, &curves.green, &curves.blue};
}

[[nodiscard]] auto curve_vectors(const CompiledCurveStageLuts& curves) {
  return std::array{
      &curves.parametric, &curves.master, &curves.luma,
      &curves.red, &curves.green, &curves.blue};
}

}  // namespace

bool retained_layer_curves_active(const PhotoLayer& layer) {
  return std::ranges::any_of(
             std::array{layer.curve_shadows, layer.curve_darks,
                        layer.curve_lights, layer.curve_highlights},
             [](double value) { return std::abs(value) > 1.0e-9; }) ||
         !layer.master_curve_points.empty() || !layer.luma_curve_points.empty() ||
         !layer.red_curve_points.empty() || !layer.green_curve_points.empty() ||
         !layer.blue_curve_points.empty();
}

ColorAdjustmentState retained_layer_curve_state(
    const PhotoLayer& layer, const ColorAdjustmentState& state) {
  auto curve_state = ColorAdjustmentState{};
  curve_state.color_pipeline = state.color_pipeline;
  curve_state.curve_shadows = layer.curve_shadows;
  curve_state.curve_darks = layer.curve_darks;
  curve_state.curve_lights = layer.curve_lights;
  curve_state.curve_highlights = layer.curve_highlights;
  curve_state.master_curve_points = layer.master_curve_points;
  curve_state.luma_curve_points = layer.luma_curve_points;
  curve_state.red_curve_points = layer.red_curve_points;
  curve_state.green_curve_points = layer.green_curve_points;
  curve_state.blue_curve_points = layer.blue_curve_points;
  return curve_state;
}

bool retained_layer_curves_supported(
    const PhotoLayer& layer, const ColorAdjustmentState& state) {
  if (!retained_layer_curves_active(layer)) return true;
  const auto controls = std::array{
      layer.curve_shadows, layer.curve_darks,
      layer.curve_lights, layer.curve_highlights};
  const auto finite_points = [](const std::vector<CurvePoint>& points) {
    return std::ranges::all_of(points, [](const CurvePoint& point) {
      return std::isfinite(point.x) && std::isfinite(point.y);
    });
  };
  if (!std::ranges::all_of(controls, [](double value) {
        return std::isfinite(value);
      }) ||
      !finite_points(layer.master_curve_points) ||
      !finite_points(layer.luma_curve_points) ||
      !finite_points(layer.red_curve_points) ||
      !finite_points(layer.green_curve_points) ||
      !finite_points(layer.blue_curve_points)) {
    return false;
  }
  const auto curves =
      compile_curve_stage_luts(retained_layer_curve_state(layer, state));
  return curves.ready() && std::ranges::all_of(
      curve_vectors(curves), [](const auto* samples) {
        return std::ranges::all_of(*samples, [](float value) {
          return std::isfinite(value);
        });
      });
}

void append_retained_layer_curves(
    const PhotoLayer& layer, const ColorAdjustmentState& state,
    std::size_t index, CompiledFillLayerProgram* program) {
  if (program == nullptr || !retained_layer_curves_active(layer)) return;
  auto curves = compile_curve_stage_luts(retained_layer_curve_state(layer, state));
  auto& compiled = program->layers[index];
  compiled.has_curves = curves.ready();
  const auto vectors = curve_vectors(curves);
  for (auto curve = std::size_t{0U}; curve < kCurveCount; ++curve) {
    if (vectors[curve]->empty()) continue;
    compiled.curve_active_mask |= std::uint32_t{1U} << curve;
    compiled.curve_offsets[curve] =
        static_cast<std::uint32_t>(program->curve_float_count);
    program->curve_float_count += vectors[curve]->size();
  }
  program->curve_luts[index] = std::move(curves);
}

bool retained_layer_curves_ready(
    const CompiledFillLayerProgram& program, const CompiledFillLayer& layer,
    std::size_t index) {
  if (!layer.has_curves) return true;
  if (layer.kind != 2U || program.curve_identity == 0U ||
      !program.curve_luts[index].ready() || layer.curve_active_mask == 0U) {
    return false;
  }
  const auto vectors = curve_vectors(program.curve_luts[index]);
  for (auto curve = std::size_t{0U}; curve < kCurveCount; ++curve) {
    const auto active = (layer.curve_active_mask & (std::uint32_t{1U} << curve)) != 0U;
    if (active != !vectors[curve]->empty()) return false;
    if (active && static_cast<std::uint64_t>(layer.curve_offsets[curve]) +
                      vectors[curve]->size() > program.curve_float_count) return false;
  }
  return true;
}

void finish_retained_layer_curve_identity(CompiledFillLayerProgram* program) {
  if (program == nullptr || program->curve_float_count == 0U) return;
  auto identity = std::uint64_t{14695981039346656037ULL};
  for (auto index = std::size_t{0U}; index < program->layer_count; ++index) {
    if (!program->layers[index].has_curves) continue;
    identity ^= program->curve_luts[index].cache_identity;
    identity *= 1099511628211ULL;
  }
  program->curve_identity = identity != 0U ? identity : 1U;
}

}  // namespace photo_editor::pipeline
