#pragma once

#include "desktop_pipeline_state_compiler.h"
#include "working_image.h"

#include <QSize>
#include <QVariantMap>

namespace photo_editor::desktop {

inline constexpr int kComponentPostCompositeRetouchVersion = 1;

// Component repair masks are evaluated against the visible child composite,
// after its editable layer tree has rendered. The versioned marker is stored
// with the mask so saved/reopened component documents keep that behavior while
// ordinary photo retouch retains its original pre-layer semantics.
inline bool tag_component_post_composite_retouch(
    QVariantMap* mask, const QSize& canvas_size) {
  if (mask == nullptr || !canvas_size.isValid() || canvas_size.isEmpty() ||
      static_cast<qint64>(canvas_size.width()) * canvas_size.height() >
          64LL * 1000LL * 1000LL) return false;
  mask->insert(QStringLiteral("componentPostCompositeRetouchVersion"),
               kComponentPostCompositeRetouchVersion);
  mask->insert(QStringLiteral("componentRetouchCanvasWidth"), canvas_size.width());
  mask->insert(QStringLiteral("componentRetouchCanvasHeight"), canvas_size.height());
  return true;
}

inline bool is_component_post_composite_retouch(const QVariantMap& mask) {
  const auto width = mask.value(QStringLiteral("componentRetouchCanvasWidth")).toInt();
  const auto height = mask.value(QStringLiteral("componentRetouchCanvasHeight")).toInt();
  return mask.value(QStringLiteral("componentPostCompositeRetouchVersion")).toInt() ==
          kComponentPostCompositeRetouchVersion &&
      width > 0 && height > 0 &&
      static_cast<qint64>(width) * height <= 64LL * 1000LL * 1000LL;
}

// Retouch operates before output geometry. A physical crop must not remove its
// clone/heal/removal source pixels. Prepare the same working/local-adjustment
// prefix for the complete decoded source, retaining its original mask basis.
inline bool attach_full_source_retouch_context(
    photo_editor::pipeline::ColorAdjustmentState* target,
    const QImage& full_source,
    const EditRecipeState& state,
    const photo_editor::pipeline::DecodedImageMetadata* metadata,
    DesktopRenderPurpose purpose,
    LocalMaskRasterPolicy raster_policy,
    const DesktopSourceGeometry& geometry,
    photo_editor::pipeline::CancellationToken cancellation = {}) {
  using namespace photo_editor::pipeline;
  if (target == nullptr) return false;
  target->retouch_source_context.reset();
  if (target->retouch_masks.empty() || !geometry.valid() ||
      geometry.crop_rect == QRect(QPoint(), geometry.source_size)) return true;
  if (full_source.isNull() || full_source.size() != geometry.source_size || cancellation.cancelled()) return false;
  auto working = working_image_from_qimage(full_source, state.working_color_profile,
      &target->color_pipeline.input_icc_profile_asset,
      metadata != nullptr && decoded_image_requires_engine_input_transform(*metadata));
  if (working.isNull() || cancellation.cancelled()) return false;
  auto sampling_state = state;
  sampling_state.layers.clear();
  auto pipeline = compile_desktop_pipeline_state(sampling_state, metadata,
      target->color_pipeline, purpose, raster_policy, full_source.size(), working.pixels,
      DesktopSourceGeometry{full_source.size(), QRect(QPoint(), full_source.size())});
  auto context = std::make_shared<RetouchSourceContext>();
  context->masks = std::move(pipeline.retouch_masks);
  pipeline.retouch_masks.clear();
  pipeline.photo_layers.clear();
  pipeline.display_referred_text_layers.clear();
  auto prefix = prepare_color_prefix_stage_context(working.pixels, working.width, working.height, pipeline);
  if (!apply_color_normalize_raw_stage(&working.pixels, working.width, working.height, pipeline, &prefix, cancellation) ||
      !apply_color_camera_characterization_stage(&working.pixels, working.width, working.height, pipeline, prefix, cancellation) ||
      !apply_color_scene_adjustments_stage(&working.pixels, working.width, working.height, pipeline, cancellation) ||
      !apply_color_tone_stage(&working.pixels, working.width, working.height, pipeline, cancellation) ||
      !apply_color_curves_stage(&working.pixels, working.width, working.height, pipeline, cancellation) ||
      !apply_color_look_stage(&working.pixels, working.width, working.height, pipeline, cancellation) ||
      !apply_color_layers_stage(&working.pixels, working.width, working.height, pipeline,
          purpose == DesktopRenderPurpose::Output ? RemovalQuality::Final : RemovalQuality::Interactive,
          cancellation) || cancellation.cancelled()) return false;
  context->pixels = std::move(working.pixels);
  context->width = working.width;
  context->height = working.height;
  context->crop_x = geometry.crop_rect.x(); context->crop_y = geometry.crop_rect.y();
  context->crop_width = geometry.crop_rect.width(); context->crop_height = geometry.crop_rect.height();
  target->retouch_source_context = std::move(context);
  return true;
}

}  // namespace photo_editor::desktop
