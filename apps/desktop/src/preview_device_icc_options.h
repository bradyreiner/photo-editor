#pragma once

#include "photo_editor/pipeline/color_pipeline.h"
#include "photo_editor/pipeline/color_transform.h"

namespace photo_editor::desktop {

// Prewarming, CPU publication, and canonical publication must name the same
// device transform alike. The engine also keys on profile bytes, intent, BPC,
// and pixel format, so sharing this identity never aliases different math.
[[nodiscard]] inline photo_editor::pipeline::IccTransformOptions preview_device_icc_options(
    const photo_editor::pipeline::ColorPipelineConfig& config,
    bool linear_rec2020_interchange) {
  namespace pipeline = photo_editor::pipeline;
  return {
      .rendering_intent = config.render_intent == pipeline::RenderIntent::RelativeColorimetric
                              ? pipeline::IccRenderingIntent::RelativeColorimetric
                              : pipeline::IccRenderingIntent::Perceptual,
      .black_point_compensation = config.black_point_compensation_enabled,
      .pixel_format = pipeline::IccPixelFormat::RgbaFloat32,
      .target = linear_rec2020_interchange
                    ? "preview-output:aces2-rec2020-interchange"
                    : "preview-output",
  };
}

}  // namespace photo_editor::desktop
