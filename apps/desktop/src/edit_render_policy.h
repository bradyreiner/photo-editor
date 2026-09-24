#pragma once

#include "edit_recipe_state.h"

#include <algorithm>
#include <cmath>

namespace photo_editor::desktop {

inline constexpr int kDefaultInteractivePreviewEdge = 2560;
inline constexpr int kMinimumInteractivePreviewEdge = 512;
inline constexpr int kMaximumInteractivePreviewEdge = 6144;

[[nodiscard]] inline bool is_local_mask_color_operation(const QString& operation) {
  static const QStringList operations{
      "active_mask_exposure", "active_mask_contrast", "active_mask_highlights",
      "active_mask_shadows", "active_mask_whites", "active_mask_blacks",
      "active_mask_temperature", "active_mask_tint", "active_mask_vibrance",
      "active_mask_saturation", "active_mask_hue_shift", "active_mask_saturation_shift",
      "active_mask_colorize_hue", "active_mask_colorize_strength"};
  return operations.contains(operation);
}

// These controls change rendered coverage, not just the cursor or mask label.
// They need exact settled/full work even when invoked outside a QML gesture.
[[nodiscard]] inline bool is_local_mask_coverage_operation(const QString& operation) {
  static const QStringList operations{
      "active_mask_operation", "active_mask_enabled", "active_mask_solo",
      "active_mask_opacity", "active_mask_density", "active_mask_inverted",
      "active_mask_blend_mode", "active_mask_selection_expand", "active_mask_selection_smooth",
      "active_mask_selection_fine_edge", "active_mask_selection_edge_contrast",
      "active_mask_selection_decontaminate", "active_mask_feather", "active_mask_position",
      "active_mask_rotation", "active_mask_center_x", "active_mask_center_y",
      "active_mask_radius_x", "active_mask_radius_y", "active_mask_luminance_min",
      "active_mask_luminance_max", "active_mask_hue_center", "active_mask_hue_range",
      "active_mask_saturation_min", "active_mask_saturation_max", "active_mask_depth_min",
      "active_mask_depth_max", "active_mask_range_feather", "active_mask_refine_luminance_min",
      "active_mask_refine_luminance_max", "active_mask_refine_hue_center", "active_mask_refine_hue_range",
      "active_mask_refine_saturation_min", "active_mask_refine_saturation_max",
      "active_mask_refine_depth_min", "active_mask_refine_depth_max",
      "active_mask_refine_edge_confidence_min", "active_mask_refine_range_feather",
      "active_generated_mask_hole_fill_threshold", "active_generated_mask_island_removal_threshold"};
  return operations.contains(operation);
}

struct PreviewPresentationMetrics final {
  double content_width = 0.0;
  double content_height = 0.0;
  double device_pixel_ratio = 1.0;
  bool settled_mask_quality = false;
};

[[nodiscard]] inline int clamp_preview_target_edge(int edge) {
  if (edge <= 0) {
    return kDefaultInteractivePreviewEdge;
  }
  return std::clamp(
      edge,
      kMinimumInteractivePreviewEdge,
      kMaximumInteractivePreviewEdge);
}

[[nodiscard]] inline int preview_target_edge_for_presentation(
    const PreviewPresentationMetrics& metrics) {
  const auto finite_width =
      std::isfinite(metrics.content_width)
          ? std::max(0.0, metrics.content_width)
          : 0.0;
  const auto finite_height =
      std::isfinite(metrics.content_height)
          ? std::max(0.0, metrics.content_height)
          : 0.0;
  const auto device_scale =
      std::isfinite(metrics.device_pixel_ratio)
          ? std::max(1.0, metrics.device_pixel_ratio)
          : 1.0;
  const auto quality_floor =
      metrics.settled_mask_quality ? 3072.0 : 2560.0;
  const auto supersample =
      metrics.settled_mask_quality ? 2.5 : 2.0;
  const auto requested = std::clamp(
      std::max(
          quality_floor,
          std::max(finite_width, finite_height) *
              device_scale * supersample),
      static_cast<double>(kMinimumInteractivePreviewEdge),
      static_cast<double>(kMaximumInteractivePreviewEdge));
  return static_cast<int>(std::lround(requested));
}

// A preview-only brush/overlay draft publishes global tone/color feedback while
// the settled request owns exact mask geometry, local adjustments, retouch,
// layers, detail, and lens work. Compile the canonical draft plan from that
// same reduced recipe so large editable mask histories are not serialized and
// compiled into stages the draft executor will immediately discard.
// Photograph-changing local color/coverage controls must not use this plan.
[[nodiscard]] inline EditRecipeState mask_feedback_draft_execution_state(
    const EditRecipeState& state) {
  auto draft = state;
  draft.mask_stack.clear();
  draft.layers.clear();
  draft.active_mask_index = -1;
  draft.linear_mask_enabled = false;
  draft.radial_mask_enabled = false;
  draft.sharpening = 0.0;
  draft.sharpen_radius = 1.0;
  draft.sharpen_detail = 0.0;
  draft.sharpen_masking = 0.0;
  draft.luminance_nr = 0.0;
  draft.color_nr = 0.0;
  draft.lens_distortion = 0.0;
  draft.lens_vignette = 0.0;
  draft.chromatic_aberration = 0.0;
  return draft;
}

// Interactive preview and final export must make the same decision about when
// the 33-cube approximation is insufficient. Keep this policy shared instead
// of letting the two render entry points drift independently.
[[nodiscard]] inline bool render_state_requires_exact_global_tone(
    const EditRecipeState& state) {
  return std::abs(state.exposure) > 1e-6 ||
         std::abs(state.contrast) > 1e-6 ||
         std::abs(state.highlights) > 1e-6 ||
         std::abs(state.shadows) > 1e-6 ||
         std::abs(state.whites) > 1e-6 ||
         std::abs(state.blacks) > 1e-6 ||
         std::abs(state.brightness) > 1e-6 ||
         std::abs(state.filmic_shoulder) > 1e-6 ||
         std::abs(state.filmic_toe) > 1e-6 ||
         std::abs(state.adaptive_contrast) > 1e-6 ||
         std::abs(state.levels_black_point) > 1e-6 ||
         std::abs(state.levels_gamma) > 1e-6 ||
         std::abs(state.levels_white_point) > 1e-6 ||
         std::abs(state.zone_deep_shadows) > 1e-6 ||
         std::abs(state.zone_shadows) > 1e-6 ||
         std::abs(state.zone_midtones) > 1e-6 ||
         std::abs(state.zone_lights) > 1e-6 ||
         std::abs(state.zone_highlights) > 1e-6 ||
         std::abs(state.curve_shadows) > 1e-6 ||
         std::abs(state.curve_darks) > 1e-6 ||
         std::abs(state.curve_lights) > 1e-6 ||
         std::abs(state.curve_highlights) > 1e-6 ||
         !state.master_curve_points.isEmpty() ||
         !state.luma_curve_points.isEmpty() ||
         !state.red_curve_points.isEmpty() ||
         !state.green_curve_points.isEmpty() ||
         !state.blue_curve_points.isEmpty();
}

[[nodiscard]] inline bool render_state_requires_exact_post_color(
    const EditRecipeState& state) {
  const auto has_mixer_channel_adjustments = std::any_of(
      state.mixer_channels.cbegin(),
      state.mixer_channels.cend(),
      [](const auto& channel) {
        return std::abs(channel.hue) > 1e-6 ||
               std::abs(channel.saturation) > 1e-6 ||
               std::abs(channel.luminance) > 1e-6 ||
               std::abs(channel.selective_cyan) > 1e-6 ||
               std::abs(channel.selective_magenta) > 1e-6 ||
               std::abs(channel.selective_yellow) > 1e-6 ||
               std::abs(channel.selective_black) > 1e-6;
      });
  const auto& rgb_mixer = state.rgb_channel_mixer;
  const auto has_rgb_mixer_adjustments =
      std::abs(rgb_mixer.red_from_red - 100.0) > 1e-6 ||
      std::abs(rgb_mixer.red_from_green) > 1e-6 ||
      std::abs(rgb_mixer.red_from_blue) > 1e-6 ||
      std::abs(rgb_mixer.green_from_red) > 1e-6 ||
      std::abs(rgb_mixer.green_from_green - 100.0) > 1e-6 ||
      std::abs(rgb_mixer.green_from_blue) > 1e-6 ||
      std::abs(rgb_mixer.blue_from_red) > 1e-6 ||
      std::abs(rgb_mixer.blue_from_green) > 1e-6 ||
      std::abs(rgb_mixer.blue_from_blue - 100.0) > 1e-6;
  const auto has_black_and_white_mixer_adjustments = std::any_of(
      state.black_and_white_mixer.cbegin(),
      state.black_and_white_mixer.cend(),
      [](double value) { return std::abs(value) > 1e-6; });
  return std::abs(state.saturation) > 1e-6 ||
         std::abs(state.vibrance) > 1e-6 ||
         std::abs(state.hue_adjustment) > 1e-6 ||
         std::abs(state.saturation_adjustment) > 1e-6 ||
         std::abs(state.luminance_adjustment) > 1e-6 ||
         has_mixer_channel_adjustments ||
         has_rgb_mixer_adjustments ||
         state.black_and_white_enabled ||
         has_black_and_white_mixer_adjustments ||
         (state.color_replacement_enabled &&
          std::abs(state.color_replacement_strength) > 1e-6) ||
         (state.skin_tone_protection_enabled &&
          std::abs(state.skin_tone_protection_strength) > 1e-6) ||
         (state.color_harmony_enabled &&
          std::abs(state.color_harmony_strength) > 1e-6) ||
         std::abs(state.grading_shadows_saturation) > 1e-6 ||
         std::abs(state.grading_midtones_saturation) > 1e-6 ||
         std::abs(state.grading_highlights_saturation) > 1e-6 ||
         std::abs(state.grading_shadows_luminance) > 1e-6 ||
         std::abs(state.grading_midtones_luminance) > 1e-6 ||
         std::abs(state.grading_highlights_luminance) > 1e-6 ||
         std::abs(state.grading_amount - 100.0) > 1e-6 ||
         std::abs(state.grading_balance) > 1e-6;
}

}  // namespace photo_editor::desktop
