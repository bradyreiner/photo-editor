#include "edit_render_policy.h"

#include <cstdlib>
#include <iostream>
#include <limits>

namespace {

int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

}  // namespace

int main() {
  for (const auto control : {"exposure", "contrast", "highlights", "shadows", "whites", "blacks",
      "temperature", "tint", "vibrance", "saturation", "hue_shift", "saturation_shift",
      "colorize_hue", "colorize_strength"})
    if (!photo_editor::desktop::is_local_mask_color_operation(QString("active_mask_") + control))
      return fail("local color controls must not use the geometry-only mask draft");
  for (const auto operation : {"active_mask_name", "active_mask_locked", "active_mask_selection_expand",
      "selection_brush_start", "smart_selection", "active_mask_position", ""})
    if (photo_editor::desktop::is_local_mask_color_operation(operation))
      return fail("non-color mask operations must not be classified as color edits");
  for (const auto control : {"operation", "enabled", "solo", "opacity", "density", "inverted",
      "blend_mode", "selection_expand", "selection_smooth", "selection_fine_edge",
      "selection_edge_contrast", "selection_decontaminate", "feather", "position", "rotation",
      "center_x", "center_y", "radius_x", "radius_y", "luminance_min", "luminance_max",
      "hue_center", "hue_range", "saturation_min", "saturation_max", "depth_min", "depth_max",
      "range_feather", "refine_luminance_min", "refine_luminance_max", "refine_hue_center",
      "refine_hue_range", "refine_saturation_min", "refine_saturation_max", "refine_depth_min",
      "refine_depth_max", "refine_edge_confidence_min", "refine_range_feather"}) {
    if (!photo_editor::desktop::is_local_mask_coverage_operation(QString("active_mask_") + control))
      return fail("coverage controls must restore exact photograph rendering");
  }
  for (const auto operation : {"active_generated_mask_hole_fill_threshold",
      "active_generated_mask_island_removal_threshold"})
    if (!photo_editor::desktop::is_local_mask_coverage_operation(operation))
      return fail("generated mask cleanup must restore exact photograph rendering");
  for (const auto operation : {"active_mask_name", "active_mask_locked", "active_mask_brush_size",
      "active_mask_brush_flow", "active_mask_brush_preset", "selection_brush_start",
      "active_mask_exposure", ""})
    if (photo_editor::desktop::is_local_mask_coverage_operation(operation))
      return fail("cursor, metadata and color edits must retain their distinct routing");
  using photo_editor::desktop::PreviewPresentationMetrics;
  using photo_editor::desktop::clamp_preview_target_edge;
  using photo_editor::desktop::mask_feedback_draft_execution_state;
  using photo_editor::desktop::preview_target_edge_for_presentation;

  if (clamp_preview_target_edge(0) != 2560 ||
      clamp_preview_target_edge(100) != 512 ||
      clamp_preview_target_edge(1800) != 1800 ||
      clamp_preview_target_edge(9000) != 6144) {
    return fail("preview target clamping should retain the declared default and bounds");
  }

  const auto ordinary = preview_target_edge_for_presentation(
      PreviewPresentationMetrics{
          .content_width = 1000.0,
          .content_height = 800.0,
          .device_pixel_ratio = 2.0,
      });
  const auto settled_mask = preview_target_edge_for_presentation(
      PreviewPresentationMetrics{
          .content_width = 1000.0,
          .content_height = 800.0,
          .device_pixel_ratio = 2.0,
          .settled_mask_quality = true,
      });
  if (ordinary != 4000 || settled_mask != 5000) {
    return fail("presentation policy should own ordinary and settled-mask supersampling");
  }

  const auto ordinary_floor = preview_target_edge_for_presentation(
      PreviewPresentationMetrics{
          .content_width = 320.0,
          .content_height = 200.0,
          .device_pixel_ratio = 2.0,
      });
  const auto mask_floor = preview_target_edge_for_presentation(
      PreviewPresentationMetrics{
          .content_width = 320.0,
          .content_height = 200.0,
          .device_pixel_ratio = 2.0,
          .settled_mask_quality = true,
      });
  if (ordinary_floor != 2560 || mask_floor != 3072) {
    return fail("presentation policy should retain its ordinary and settled-mask quality floors");
  }

  const auto bounded = preview_target_edge_for_presentation(
      PreviewPresentationMetrics{
          .content_width = 5000.0,
          .content_height = 4000.0,
          .device_pixel_ratio = 3.0,
          .settled_mask_quality = true,
      });
  const auto invalid = preview_target_edge_for_presentation(
      PreviewPresentationMetrics{
          .content_width = std::numeric_limits<double>::quiet_NaN(),
          .content_height = -100.0,
          .device_pixel_ratio = std::numeric_limits<double>::infinity(),
      });
  if (bounded != 6144 || invalid != 2560) {
    return fail("presentation policy should bound extreme and non-finite presentation facts");
  }

  photo_editor::desktop::EditRecipeState mask_state;
  mask_state.exposure = 1.25;
  mask_state.saturation = -18.0;
  mask_state.mask_stack = QVariantList{QVariantMap{
      {QStringLiteral("id"), QStringLiteral("mask-1")},
      {QStringLiteral("brushPoints"), QVariantList{QVariantMap{
          {QStringLiteral("x"), 0.5},
          {QStringLiteral("y"), 0.5},
      }}},
  }};
  mask_state.layers = QVariantList{QVariantMap{
      {QStringLiteral("id"), QStringLiteral("layer-1")},
  }};
  mask_state.active_mask_index = 0;
  mask_state.linear_mask_enabled = true;
  mask_state.radial_mask_enabled = true;
  mask_state.sharpening = 40.0;
  mask_state.sharpen_radius = 2.5;
  mask_state.sharpen_detail = 55.0;
  mask_state.sharpen_masking = 35.0;
  mask_state.luminance_nr = 20.0;
  mask_state.color_nr = 15.0;
  mask_state.lens_distortion = 12.0;
  mask_state.lens_vignette = -8.0;
  mask_state.chromatic_aberration = 6.0;
  const auto draft_state =
      mask_feedback_draft_execution_state(mask_state);
  if (!draft_state.mask_stack.isEmpty() ||
      !draft_state.layers.isEmpty() ||
      draft_state.active_mask_index != -1 ||
      draft_state.linear_mask_enabled ||
      draft_state.radial_mask_enabled ||
      draft_state.sharpening != 0.0 ||
      draft_state.sharpen_radius != 1.0 ||
      draft_state.sharpen_detail != 0.0 ||
      draft_state.sharpen_masking != 0.0 ||
      draft_state.luminance_nr != 0.0 ||
      draft_state.color_nr != 0.0 ||
      draft_state.lens_distortion != 0.0 ||
      draft_state.lens_vignette != 0.0 ||
      draft_state.chromatic_aberration != 0.0 ||
      draft_state.exposure != mask_state.exposure ||
      draft_state.saturation != mask_state.saturation) {
    return fail("mask feedback draft policy should remove deferred stages while preserving global tone and color");
  }

  return EXIT_SUCCESS;
}
