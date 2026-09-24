#pragma once

#include "desktop_pipeline_state_compiler.h"
#include "edit_recipe_mask_schema.h"
#include "edit_recipe_pipeline_bridge.h"
#include "local_mask_set_settings.h"
#include "mask_alpha_provider.h"
#include "working_image.h"
#include "photo_editor/pipeline/local_mask_sampling.h"
#include <QSet>

namespace photo_editor::desktop {

// Prepared once per overlay/outline request, shared by refined and original
// coverage. Display RGB and approximate white-balance gains are not mask input.
struct RecipeMaskSamplingContext final {
  WorkingImage range;
  std::vector<photo_editor::pipeline::ColorPixel> edge_pixels;
  photo_editor::pipeline::ColorAdjustmentState pipeline;
  photo_editor::pipeline::LocalMaskSet group;
  bool valid = false;
  photo_editor::pipeline::CancellationToken cancellation;

  [[nodiscard]] MaskAlphaEvaluationResult evaluate(
      const std::vector<photo_editor::pipeline::LocalMask>& masks,
      bool original_refinement = false) const {
    if (!valid || masks.empty() || cancellation.cancelled()) return {};
    auto set = group;
    set.sources = masks;
    if (original_refinement) {
      set.selection_invert = false;
      set.selection_expand = 0;
      set.selection_smooth = 0;
      set.selection_fine_edge = 0;
      set.selection_edge_contrast = 0;
    }
    const auto alpha = photo_editor::pipeline::rasterize_local_mask_set_alpha_with_context(
        set, range.width, range.height, range.pixels, edge_pixels, pipeline.color_pipeline);
    if (alpha.size() != range.pixels.size() || cancellation.cancelled()) return {};
    auto result = evaluate_mask_alpha(QSize(range.width, range.height), static_cast<int>(masks.size()),
        [&](int x, int y, double, double) {
          return static_cast<double>(alpha[static_cast<std::size_t>(y * range.width + x)]) / 255.0;
        });
    result.cache_key = QStringLiteral("recipe-mask-alpha-v1|%1|%2x%3|%4")
        .arg(result.alpha_hash).arg(range.width).arg(range.height).arg(masks.size());
    return result;
  }
};

inline RecipeMaskSamplingContext prepare_recipe_mask_sampling(
    const QImage& decoded,
    const EditRecipeState& state,
    const photo_editor::pipeline::DecodedImageMetadata& metadata,
    const QVariantMap& group_definition,
    const QRect& source_crop,
    photo_editor::pipeline::CancellationToken cancellation = {}) {
  using namespace photo_editor::pipeline;
  RecipeMaskSamplingContext result;
  result.cancellation = cancellation;
  if (decoded.isNull() || cancellation.cancelled()) return result;
  result.pipeline = compile_base_color_adjustment_state(state, &metadata);
  const auto& pipeline = result.pipeline;
  const auto full = working_image_from_qimage(decoded, state.working_color_profile,
      &pipeline.color_pipeline.input_icc_profile_asset,
      decoded_image_requires_engine_input_transform(metadata));
  if (full.isNull()) return result;
  result.range = crop_working_image(full, source_crop);
  auto& range = result.range;
  if (range.isNull()) return result;
  auto prefix = prepare_color_prefix_stage_context(full.pixels, full.width, full.height, pipeline);
  if (!apply_color_normalize_raw_stage(&range.pixels, range.width, range.height, pipeline, &prefix, cancellation) ||
      !apply_color_camera_characterization_stage(&range.pixels, range.width, range.height, pipeline, prefix, cancellation) ||
      !apply_color_scene_adjustments_stage(&range.pixels, range.width, range.height, pipeline, cancellation) ||
      !apply_color_tone_stage(&range.pixels, range.width, range.height, pipeline, cancellation) ||
      !apply_color_curves_stage(&range.pixels, range.width, range.height, pipeline, cancellation) ||
      !apply_color_look_stage(&range.pixels, range.width, range.height, pipeline, cancellation)) return result;
  result.edge_pixels = range.pixels;
  result.group = compile_local_mask_set_settings(group_definition);
  // Find the target in unfiltered recipe order. Disabled, soloed and bypassed
  // masks retain a defined sampling position even when absent from the render.
  const auto target_id = group_definition.value("maskSetId").toString();
  QVariantList preceding_sources;
  int ordinal = 1;
  bool found = false;
  for (const auto& value : state.mask_stack) {
    const auto mask = normalized_mask_definition(value.toMap(), ordinal++);
    if (mask.value("maskSetId").toString() == target_id && !target_id.isEmpty()) {
      found = true;
      break;
    }
    preceding_sources.append(mask);
  }
  if (found) {
    // Compile the complete stack first so solo filtering includes the target
    // and any later set; truncating the recipe before compiling changes it.
    const auto sets = compile_pipeline_local_mask_sets(state.mask_stack, DesktopRenderPurpose::Preview,
        LocalMaskRasterPolicy::RebuildEditableAtOutputResolution, QSize(range.width, range.height));
    QSet<QString> preceding_ids;
    for (const auto& value : preceding_sources) preceding_ids.insert(value.toMap().value("maskSetId").toString());
    auto local = pipeline;
    local.local_mask_sets.clear();
    local.local_masks.clear();
    local.retouch_masks.clear();
    local.photo_layers.clear();
    local.linear_mask_enabled = false;
    local.radial_mask_enabled = false;
    for (const auto& set : sets) {
      if (preceding_ids.contains(QString::fromStdString(set.id))) local.local_mask_sets.push_back(set);
    }
    if (!apply_local_adjustments_to_look_pixels_with_context(
            &range.pixels, range.width, range.height, local, result.edge_pixels, cancellation)) return result;
  }
  result.valid = !cancellation.cancelled();
  return result;
}

}  // namespace photo_editor::desktop
