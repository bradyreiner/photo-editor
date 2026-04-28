#include "photo_editor/pipeline/render_graph.h"

namespace photo_editor::pipeline {

RenderGraph::RenderGraph()
    : stages_({
          {"raw_decode", "Decode RAW or raster source into working space."},
          {"input_transform", "Apply input profile, white balance adaptation, and source-to-working transform."},
          {"camera_lens_normalization", "Apply lens correction and geometry normalization."},
          {"global_tone_color", "Apply tone curve, presence, grading, and color mixer controls."},
          {"local_adjustments", "Resolve masks and local adjustment stacks."},
          {"detail", "Apply sharpening, denoise, and fine detail controls."},
          {"crop_transform", "Apply crop and transform after tone and mask edits."},
          {"output_transform", "Convert into display or export target color space."},
      }) {}

const std::vector<RenderStage>& RenderGraph::stages() const noexcept { return stages_; }

}  // namespace photo_editor::pipeline
