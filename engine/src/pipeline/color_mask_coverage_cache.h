#pragma once

#include "color_working_space_runtime.h"
#include <memory>
#include <span>

namespace photo_editor::pipeline {

class MaskCoverageFrame;
struct MaskCoverageEntry;

struct MaskCoverageLease {
  std::shared_ptr<MaskCoverageEntry> entry;
  bool hit = false;
  double* writable = nullptr;
  const double* readable = nullptr;
  double weight(std::size_t index) const { return readable[index]; }
  void record(std::size_t index, double value) const {
    if (writable) writable[index] = value;
  }
};

// Returns null when the bounded cache cannot retain the immutable inputs.
std::shared_ptr<MaskCoverageFrame> acquire_mask_coverage_frame(
    const std::vector<ColorPixel>& pixels, const std::vector<ColorPixel>* edge,
    int width, int height, const PipelineColorContext& context,
    PhotographicRenderingModel model, const LocalAdjustmentCoordinateMapping& mapping,
    CancellationToken cancellation);
const std::vector<ColorPixel>& mask_coverage_edge_pixels(const MaskCoverageFrame& frame);
MaskCoverageLease acquire_mask_coverage(const std::shared_ptr<MaskCoverageFrame>& frame,
    std::span<const LocalMaskSet> prefix, CancellationToken cancellation);
void publish_mask_coverage(const std::shared_ptr<MaskCoverageFrame>& frame,
    const MaskCoverageLease& lease, CancellationToken cancellation);

struct MaskCoverageCacheStats { std::size_t hits, misses, retained_bytes; };
MaskCoverageCacheStats mask_coverage_cache_stats();
// Read-only tests can compare against the original evaluator, without changing
// recipes or introducing a production environment-variable rendering switch.
void reset_mask_coverage_cache_for_testing(bool enabled = true);

}  // namespace photo_editor::pipeline
