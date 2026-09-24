#pragma once

#include "photo_editor/pipeline/color_adjustments.h"
#include "photo_editor/pipeline/color_transform.h"
#include "photo_editor/pipeline/recipe_compiler.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace photo_editor::pipeline {

enum class RenderPixelEncoding : std::uint8_t {
  SourceEncoded,
  CameraLinearFloat,
  SceneLinearFloat,
  DisplayLinearFloat,
  DisplayEncodedFloat,
};

enum class RenderColorDomain : std::uint8_t {
  Source,
  CameraNative,
  WorkingScene,
  RenderingTarget,
  ProofSimulation,
  Device,
};

enum class RenderRangeModel : std::uint8_t {
  SourceCodeValues,
  SceneExtended,
  DisplayExtended,
  DisplayBounded,
};

enum class RenderAlphaModel : std::uint8_t {
  Opaque,
  Straight,
  Premultiplied,
};

struct RenderSurfaceDescriptor final {
  RenderPixelEncoding pixel_encoding =
      RenderPixelEncoding::SourceEncoded;
  RenderColorDomain color_domain = RenderColorDomain::Source;
  RenderRangeModel range = RenderRangeModel::SourceCodeValues;
  RenderAlphaModel alpha = RenderAlphaModel::Straight;
  std::string color_space_identity;
  std::string transfer_identity;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  double source_area_x = 0.0;
  double source_area_y = 0.0;
  double source_area_width = 1.0;
  double source_area_height = 1.0;

  bool operator==(const RenderSurfaceDescriptor&) const = default;
};

class RenderSurfaceBacking {
 public:
  virtual ~RenderSurfaceBacking() = default;

  [[nodiscard]] virtual std::string_view backend_identity() const noexcept = 0;
  [[nodiscard]] virtual std::uint64_t byte_size() const noexcept = 0;
  [[nodiscard]] virtual bool valid_for(
      const RenderSurfaceDescriptor& descriptor) const noexcept = 0;
  [[nodiscard]] virtual bool copy_pixels(
      std::vector<ColorPixel>* destination,
      CancellationToken cancellation = {}) const = 0;
};

struct RenderSurface final {
  RenderSurfaceDescriptor descriptor;
  std::vector<ColorPixel> pixels;
  std::shared_ptr<const RenderSurfaceBacking> backing;
  std::string identity;
  std::optional<HighlightReconstructionAnalysis>
      highlight_reconstruction;
  std::vector<std::uint8_t> gamut_warning_mask;
  // Set only by an engine stage after that stage has validated every CPU
  // pixel. This lets the executor preserve the finite-pixel contract without
  // rescanning an unchanged multi-megapixel surface at every boundary.
  bool cpu_pixels_validated = false;

  [[nodiscard]] bool valid() const noexcept;
  [[nodiscard]] std::uint64_t byte_size() const noexcept;
  [[nodiscard]] bool ensure_cpu_pixels(
      CancellationToken cancellation = {});
};

struct RenderStageInvocation final {
  RenderStagePlan stage;
  std::size_t stage_index = 0;
  std::string stage_cache_key;
  RenderSurfaceDescriptor expected_output;
};

struct RenderFusionEvidence final {
  bool unfused_reference_passed = false;
  std::string unfused_reference_identity;
  std::vector<RenderStageKind>
      logical_stages_covered;

  bool operator==(
      const RenderFusionEvidence&) const = default;
};

struct RenderStageExecution final {
  std::optional<RenderSurface> output;
  std::string backend;
  std::string fallback_reason;
  std::string failure_reason;
  std::optional<RenderFusionEvidence>
      fusion_evidence;
};

using RenderStageCallback = std::function<RenderStageExecution(
    const RenderStageInvocation&,
    std::optional<RenderSurface>)>;

struct RenderCompletedStage;

struct RenderExecutionRequest final {
  RenderPlan plan;
  std::string backend_version;
  std::string target_identity;
  std::string profile_identity;
  std::string source_color_space_identity;
  std::string working_color_space_identity;
  std::string rendering_target_color_space_identity;
  std::string proof_color_space_identity;
  std::string device_color_space_identity;
  std::string device_transfer_identity;
  std::uint32_t output_width = 0;
  std::uint32_t output_height = 0;
  double source_area_x = 0.0;
  double source_area_y = 0.0;
  double source_area_width = 1.0;
  double source_area_height = 1.0;
  std::uint64_t generation = 0;
  std::function<bool()> cancelled;
  std::function<std::uint64_t()> latest_generation;
  bool bypass_cache = false;
  std::optional<RenderSurface> initial_surface;
  std::size_t start_stage_index = 0;
  std::optional<std::size_t> stop_after_stage_index;
  std::function<void(
      const RenderCompletedStage&,
      const RenderSurface&)> stage_observer;
};

struct RenderCompletedStage final {
  RenderStageKind kind = RenderStageKind::Decode;
  std::string cache_key;
  std::string output_identity;
  std::string backend;
  std::string fallback_reason;
  RenderSurfaceDescriptor output_descriptor;
  std::optional<RenderFusionEvidence>
      fusion_evidence;
  bool cache_hit = false;

  bool operator==(const RenderCompletedStage&) const = default;
};

enum class RenderExecutionStatus : std::uint8_t {
  Completed,
  Cancelled,
  Stale,
  InvalidRequest,
  StageFailed,
  ContractViolation,
};

struct RenderExecutionResult final {
  RenderExecutionStatus status =
      RenderExecutionStatus::InvalidRequest;
  std::optional<RenderSurface> output;
  std::vector<RenderCompletedStage> completed_stages;
  std::string plan_cache_key;
  std::string failure_stage;
  std::string failure_reason;

  [[nodiscard]] bool ok() const noexcept {
    return status == RenderExecutionStatus::Completed &&
           output.has_value();
  }
};

class RenderExecutor final {
 public:
  explicit RenderExecutor(
      std::uint64_t maximum_cache_bytes =
          256ULL * 1024ULL * 1024ULL);

  [[nodiscard]] RenderExecutionResult execute(
      RenderExecutionRequest request,
      const RenderStageCallback& callback);

  void clear_cache();
  [[nodiscard]] std::uint64_t cache_bytes() const noexcept;
  [[nodiscard]] std::size_t cache_entries() const noexcept;

 private:
  struct CacheEntry final {
    RenderSurface surface;
    std::uint64_t bytes = 0;
    std::list<std::string>::iterator age;
  };

  [[nodiscard]] std::optional<RenderSurface> find_cached(
      const std::string& key);
  void insert_cached(
      const std::string& key,
      const RenderSurface& surface);

  std::uint64_t maximum_cache_bytes_ = 0;
  std::uint64_t cache_bytes_ = 0;
  mutable std::mutex cache_mutex_;
  std::list<std::string> cache_age_;
  std::unordered_map<std::string, CacheEntry> cache_;
};

struct ColorPlanExecutionOptions final {
  RenderStageKind start_stage =
      RenderStageKind::Decode;
  RenderSurface* initial_surface = nullptr;
  RemovalQuality removal_quality =
      RemovalQuality::Interactive;
  const ColorPipelineConfig*
      photographic_output_config = nullptr;
  GamutWarningTarget gamut_warning_target =
      GamutWarningTarget::ExportOutput;
  Aces2OutputLutQuality output_lut_quality =
      Aces2OutputLutQuality::Final;
  const CompiledIccTransform* proof_transform =
      nullptr;
  const CompiledIccTransform*
      proof_gamut_warning_transform = nullptr;
  const CompiledIccTransform* device_transform =
      nullptr;
  const DisplayTarget* publish_display_target =
      nullptr;
  std::function<RenderStageExecution(
      const RenderStageInvocation&,
      const RenderSurfaceDescriptor& publish_output,
      std::optional<RenderSurface>)>
      photographic_output_publish_fusion;
  std::function<RenderStageExecution(
      const RenderStageInvocation&,
      const RenderSurfaceDescriptor& publish_output,
      std::optional<RenderSurface>)>
      photographic_output_device_publish_fusion;
  std::function<RenderStageExecution(
      const RenderStageInvocation&,
      const RenderSurfaceDescriptor& geometry_output,
      std::optional<RenderSurface>)>
      working_adjustments_geometry_fusion;
  bool stop_before_photographic_output = false;
  // Preview-only opt-in. Keep the CPU reference/export route unchanged and
  // materialize pixels for downstream proof, device transforms, and scopes.
  bool allow_metal_photographic_output = false;
};
[[nodiscard]] RenderExecutionResult
execute_color_plan(
    RenderExecutor* executor,
    RenderExecutionRequest request,
    RenderStageCallback decode_stage,
    const ColorAdjustmentState& state,
    const ColorPlanExecutionOptions& options,
    CancellationToken cancellation = {},
    ColorAdjustmentTelemetry* telemetry = nullptr);
[[nodiscard]] RenderSurfaceDescriptor render_stage_output_descriptor(
    const RenderExecutionRequest& request,
    RenderStageKind kind);

[[nodiscard]] std::string render_stage_cache_key(
    const RenderExecutionRequest& request,
    const RenderStagePlan& stage,
    const std::optional<RenderSurface>& input);

[[nodiscard]] std::string_view render_stage_kind_name(
    RenderStageKind kind) noexcept;
}  // namespace photo_editor::pipeline
