#include "photo_editor/pipeline/render_graph.h"

namespace photo_editor::pipeline {

RenderGraph::RenderGraph()
    : stages_({
          {"raw_decode", "Decode RAW or raster source into working space."},
          {"camera_lens_normalization", "Apply camera profile, lens correction, and geometry normalization."},
          {"global_tone_color", "Apply exposure, white balance, curve, presence, and color mixer controls."},
          {"local_adjustments", "Resolve masks and local adjustment stacks."},
          {"detail", "Apply sharpening, denoise, and fine detail controls."},
          {"crop_transform", "Apply crop and transform after tone and mask edits."},
          {"output_transform", "Convert into display or export target color space."},
      }) {}

const std::vector<RenderStage>& RenderGraph::stages() const noexcept { return stages_; }

}  // namespace photo_editor::pipeline
