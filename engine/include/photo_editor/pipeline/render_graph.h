#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace photo_editor::pipeline {

enum class RenderCachePolicy {
  None,
  Memory,
  Disk,
  MemoryAndDisk,
};

enum class RenderGraphVariant {
  Preview,
  FullResolution,
  Export,
};

struct RenderGraphVariantSettings final {
  RenderGraphVariant variant = RenderGraphVariant::Preview;
  std::string terminal_stage = "output_transform";
  bool full_resolution = false;
  bool encode_artifact = false;
  bool allow_disk_cache = false;
};

struct RenderStage final {
  std::string name;
  std::string description;
  std::vector<std::string> dependencies;
  RenderCachePolicy cache_policy = RenderCachePolicy::None;
  bool cancellable = false;
};

struct RenderDirtyRegion final {
  double x = 0.0;
  double y = 0.0;
  double width = 1.0;
  double height = 1.0;
  bool full_frame = true;
};

struct RenderPixelRect final {
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;
};

enum class RenderTilePriority {
  Visible,
  Adjacent,
  Background,
};

struct RenderTileRequest final {
  int image_width = 0;
  int image_height = 0;
  int tile_size = 512;
  int padding_px = 0;
  int scale_level = 0;
  RenderDirtyRegion dirty_region;
  RenderDirtyRegion visible_region;
  bool prioritize_visible_region = false;
};

struct RenderTile final {
  int tile_x = 0;
  int tile_y = 0;
  int scale_level = 0;
  RenderPixelRect output_rect;
  RenderPixelRect padded_rect;
  RenderTilePriority priority = RenderTilePriority::Background;
};

enum class RenderDirtyReason {
  SourceDecode,
  InputTransform,
  GlobalAdjustment,
  LocalAdjustment,
  MaskEdit,
  BrushStroke,
  DetailAdjustment,
  CropTransform,
  LayerChange,
  OutputTransform,
};

struct RenderInvalidation final {
  RenderDirtyReason reason = RenderDirtyReason::SourceDecode;
  RenderDirtyRegion region;
  std::vector<std::string> affected_stages;
};

struct RenderInvalidationRule final {
  RenderDirtyReason reason = RenderDirtyReason::SourceDecode;
  std::string first_affected_stage = "raw_decode";
  bool full_frame_required = true;
};

struct RenderStageDiagnostics final {
  std::string stage_name;
  double elapsed_ms = 0.0;
  std::uint64_t memory_bytes = 0;
  double gpu_elapsed_ms = 0.0;
  std::uint64_t gpu_memory_bytes = 0;
  bool gpu_used = false;
  bool cache_hit = false;
  std::string fallback_reason;
};

class RenderGraph final {
 public:
  RenderGraph();

  [[nodiscard]] const std::vector<RenderStage>& stages() const noexcept;
  [[nodiscard]] bool contains_stage(std::string_view name) const noexcept;
  [[nodiscard]] const RenderStage* stage(std::string_view name) const noexcept;
  [[nodiscard]] std::vector<std::string> dependencies_for(std::string_view name) const;
  [[nodiscard]] std::vector<std::string> dependents_for(std::string_view name) const;
  [[nodiscard]] std::vector<std::string> downstream_stages_from(std::string_view name) const;
  [[nodiscard]] std::vector<RenderStage> execution_plan_for(std::string_view terminal_stage) const;
  [[nodiscard]] RenderGraphVariantSettings variant_settings(RenderGraphVariant variant) const;
  [[nodiscard]] std::vector<RenderStage> execution_plan_for(RenderGraphVariant variant) const;
  [[nodiscard]] std::string serialized_state(RenderGraphVariant variant) const;
  [[nodiscard]] RenderInvalidation invalidation_for(
      RenderDirtyReason reason,
      const RenderDirtyRegion& changed_region = {}) const;
  [[nodiscard]] std::vector<RenderInvalidationRule> invalidation_rules() const;
  [[nodiscard]] bool stage_cacheable(std::string_view name) const noexcept;
  [[nodiscard]] std::vector<std::string> cacheable_stage_names() const;
  [[nodiscard]] bool stage_cancellable(std::string_view name) const noexcept;
  [[nodiscard]] std::vector<std::string> cancellable_stage_names() const;
  [[nodiscard]] std::vector<std::string> uncancellable_cacheable_stage_names() const;
  [[nodiscard]] std::vector<std::string> invalidated_cache_nodes_for(
      const RenderInvalidation& invalidation) const;
  [[nodiscard]] bool valid() const noexcept;

 private:
  std::vector<RenderStage> stages_;
  bool valid_ = false;
};

[[nodiscard]] RenderPixelRect pixel_rect_for_dirty_region(
    const RenderDirtyRegion& region,
    int image_width,
    int image_height);
[[nodiscard]] RenderPixelRect expand_pixel_rect(
    const RenderPixelRect& rect,
    int padding_px,
    int image_width,
    int image_height);
[[nodiscard]] std::vector<RenderTile> plan_render_tiles(const RenderTileRequest& request);

class RenderGraphDiagnostics final {
 public:
  explicit RenderGraphDiagnostics(const RenderGraph& graph);

  void record_stage(RenderStageDiagnostics diagnostics);

  [[nodiscard]] const std::vector<RenderStageDiagnostics>& stages() const noexcept;
  [[nodiscard]] const RenderStageDiagnostics* stage(std::string_view name) const noexcept;
  [[nodiscard]] double total_elapsed_ms() const noexcept;
  [[nodiscard]] std::uint64_t peak_memory_bytes() const noexcept;
  [[nodiscard]] std::uint64_t peak_gpu_memory_bytes() const noexcept;
  [[nodiscard]] std::vector<std::string> gpu_stage_names() const;
  [[nodiscard]] std::vector<std::string> cache_hit_stage_names() const;
  [[nodiscard]] std::vector<std::string> fallback_stage_names() const;
  [[nodiscard]] bool complete_for(const std::vector<RenderStage>& execution_plan) const noexcept;

 private:
  std::vector<RenderStageDiagnostics> stages_;
};

}  // namespace photo_editor::pipeline
