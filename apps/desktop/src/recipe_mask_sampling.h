#pragma once

#include "desktop_pipeline_state_compiler.h"
#include "edit_recipe_mask_schema.h"
#include "edit_recipe_pipeline_bridge.h"
#include "local_mask_set_settings.h"
#include "mask_alpha_provider.h"
#include "working_image.h"
#include "photo_editor/pipeline/local_mask_sampling.h"
#include <cmath>
#include <utility>
#include <QSet>

namespace photo_editor::desktop {

// Prepared once per overlay/outline request, shared by refined and original
// coverage. Display RGB and approximate white-balance gains are not mask input.
struct RecipeMaskSamplingContext final {
  WorkingImage range;
  std::vector<photo_editor::pipeline::ColorPixel> edge_pixels;
  photo_editor::pipeline::ColorAdjustmentState pipeline;
  photo_editor::pipeline::LocalMaskSet group;
  DesktopSourceGeometry source_geometry;
  bool valid = false;
  photo_editor::pipeline::CancellationToken cancellation;

  [[nodiscard]] photo_editor::pipeline::LocalMask compile_mask(
      const QVariantMap& mask,
      LocalMaskRasterPolicy policy = LocalMaskRasterPolicy::RebuildEditableAtOutputResolution) const {
    return compile_pipeline_local_mask(mask,policy,QSize(range.width,range.height),
        source_geometry.mask_area(),QSizeF(source_geometry.source_size.width()-1,
            source_geometry.source_size.height()-1));
  }

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
    const WorkingImage& decoded,
    const EditRecipeState& state,
    const photo_editor::pipeline::DecodedImageMetadata& metadata,
    const QVariantMap& group_definition,
    const DesktopSourceGeometry& requested_geometry,
    const QSize& output_size = {},
    photo_editor::pipeline::CancellationToken cancellation = {},
    DesktopRenderPurpose purpose = DesktopRenderPurpose::Preview,
    LocalMaskRasterPolicy raster_policy =
        LocalMaskRasterPolicy::RebuildEditableAtOutputResolution) {
  using namespace photo_editor::pipeline;
  RecipeMaskSamplingContext result;
  result.cancellation = cancellation;
  if (decoded.isNull() || cancellation.cancelled()) return result;
  result.pipeline = compile_base_color_adjustment_state(state, &metadata);
  const auto full_pipeline=result.pipeline;
  const QRect full_rect(QPoint(), QSize(decoded.width, decoded.height));
  result.source_geometry = requested_geometry.valid() &&
          requested_geometry.source_size == full_rect.size()
      ? requested_geometry
      : DesktopSourceGeometry{full_rect.size(), full_rect};
  if (requested_geometry.valid() && requested_geometry.source_size != full_rect.size()) {
    return result;
  }
  const auto source_area=result.source_geometry.edge_area();
  result.pipeline.input_transform.source_area_x=source_area.x();
  result.pipeline.input_transform.source_area_y=source_area.y();
  result.pipeline.input_transform.source_area_width=source_area.width();
  result.pipeline.input_transform.source_area_height=source_area.height();
  const auto& pipeline = result.pipeline;
  result.range = crop_working_image(decoded, result.source_geometry.crop_rect);
  auto& range = result.range;
  if (range.isNull()) return result;
  if (output_size.isValid() && !output_size.isEmpty() &&
      (range.width != output_size.width() || range.height != output_size.height())) {
    const auto pixel_count = static_cast<qint64>(output_size.width()) * output_size.height();
    if (output_size.width() <= 0 || output_size.height() <= 0 ||
        pixel_count > 64LL * 1024LL * 1024LL) {
      return result;
    }
    std::vector<ColorPixel> resized(static_cast<std::size_t>(pixel_count));
    for (auto y = 0; y < output_size.height(); ++y) {
      if ((y & 31) == 0 && cancellation.cancelled()) return result;
      const auto source_y = output_size.height() > 1
          ? static_cast<double>(y) * (range.height - 1) / (output_size.height() - 1)
          : 0.0;
      const auto y0 = std::clamp(static_cast<int>(std::floor(source_y)), 0, range.height - 1);
      const auto y1 = std::min(range.height - 1, y0 + 1);
      const auto fy = source_y - y0;
      for (auto x = 0; x < output_size.width(); ++x) {
        const auto source_x = output_size.width() > 1
            ? static_cast<double>(x) * (range.width - 1) / (output_size.width() - 1)
            : 0.0;
        const auto x0 = std::clamp(static_cast<int>(std::floor(source_x)), 0, range.width - 1);
        const auto x1 = std::min(range.width - 1, x0 + 1);
        const auto fx = source_x - x0;
        const auto sample = [&](int sx, int sy) -> const ColorPixel& {
          return range.pixels[static_cast<std::size_t>(sy * range.width + sx)];
        };
        const auto& p00 = sample(x0, y0);
        const auto& p10 = sample(x1, y0);
        const auto& p01 = sample(x0, y1);
        const auto& p11 = sample(x1, y1);
        const auto interpolate = [fx, fy](float a, float b, float c, float d) {
          return static_cast<float>(
              std::lerp(std::lerp(static_cast<double>(a), static_cast<double>(b), fx),
                        std::lerp(static_cast<double>(c), static_cast<double>(d), fx), fy));
        };
        resized[static_cast<std::size_t>(y * output_size.width() + x)] = ColorPixel{
            .red = interpolate(p00.red, p10.red, p01.red, p11.red),
            .green = interpolate(p00.green, p10.green, p01.green, p11.green),
            .blue = interpolate(p00.blue, p10.blue, p01.blue, p11.blue),
            .alpha = interpolate(p00.alpha, p10.alpha, p01.alpha, p11.alpha),
        };
      }
    }
    range.width = output_size.width();
    range.height = output_size.height();
    range.pixels = std::move(resized);
  }
  auto prefix = prepare_color_prefix_stage_context(
      decoded.pixels, decoded.width, decoded.height, full_pipeline);
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
    const auto sets = compile_pipeline_local_mask_sets(state.mask_stack, purpose,
        raster_policy, QSize(range.width, range.height),
        result.source_geometry.mask_area(),QSizeF(decoded.width-1,decoded.height-1));
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

inline RecipeMaskSamplingContext prepare_recipe_mask_sampling(
    const QImage& decoded,
    const EditRecipeState& state,
    const photo_editor::pipeline::DecodedImageMetadata& metadata,
    const QVariantMap& group_definition,
    const QRect& source_crop,
    photo_editor::pipeline::CancellationToken cancellation = {},
    const QSize& output_size = {},
    DesktopRenderPurpose purpose = DesktopRenderPurpose::Preview,
    LocalMaskRasterPolicy raster_policy =
        LocalMaskRasterPolicy::RebuildEditableAtOutputResolution) {
  RecipeMaskSamplingContext result;
  result.cancellation = cancellation;
  if (decoded.isNull() || cancellation.cancelled()) return result;
  const auto pipeline = compile_base_color_adjustment_state(state, &metadata);
  const auto full = working_image_from_qimage(
      decoded,
      state.working_color_profile,
      &pipeline.color_pipeline.input_icc_profile_asset,
      decoded_image_requires_engine_input_transform(metadata));
  if (full.isNull() || cancellation.cancelled()) return result;
  const QRect full_rect(QPoint(), decoded.size());
  const auto crop = source_crop.isValid()
      ? source_crop.intersected(full_rect)
      : full_rect;
  if (source_crop.isValid() && crop.isEmpty()) return result;
  return prepare_recipe_mask_sampling(
      full, state, metadata, group_definition,
      DesktopSourceGeometry{decoded.size(), crop}, output_size, cancellation,
      purpose, raster_policy);
}

}  // namespace photo_editor::desktop
