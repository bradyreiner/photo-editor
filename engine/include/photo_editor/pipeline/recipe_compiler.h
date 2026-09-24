#pragma once

#include "photo_editor/pipeline/edit_recipe_v13.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace photo_editor::pipeline {

enum class RenderStageKind : std::uint8_t {
  Decode,
  NormalizeRaw,
  CameraCharacterization,
  SceneAdjustments,
  Tone,
  Curves,
  Look,
  Layers,
  Detail,
  Geometry,
  PhotographicOutput,
  Proof,
  DeviceTransform,
  Publish,
};

enum class RenderBackendRequirement : std::uint8_t {
  ExactCpuReference,
  GpuPermitted,
};

enum class RenderPurpose : std::uint8_t {
  InteractivePreview,
  SettledPreview,
  Export,
  Thumbnail,
  Scope,
  Carousel,
};

enum class RenderScopeDomain : std::uint8_t {
  SceneLinear,
  Working,
  PreOutput,
  Proof,
  FinalDisplay,
};

struct RecipeCompileContext final {
  std::uint32_t source_width = 0;
  std::uint32_t source_height = 0;
  std::vector<AssetIdentifier> available_profile_assets;
  std::vector<AssetIdentifier> available_content_assets;
  // Explicit executor admission, e.g. "gaussian_blur:1". Empty means no photo
  // effect implementation has been connected to this render path yet.
  std::vector<std::string> available_photo_effect_algorithms;
  RenderBackendRequirement backend_requirement =
      RenderBackendRequirement::GpuPermitted;
  RenderPurpose purpose = RenderPurpose::SettledPreview;
  RenderScopeDomain scope_domain =
      RenderScopeDomain::FinalDisplay;

  bool operator==(const RecipeCompileContext&) const = default;
};

struct RenderStagePlan final {
  RenderStageKind kind = RenderStageKind::Decode;
  std::string cache_identity;

  bool operator==(const RenderStagePlan&) const = default;
};

struct RenderPlan final {
  std::uint32_t plan_version = 2;
  std::string recipe_cache_key;
  AssetIdentifier source;
  RawDecoder decoder = RawDecoder::LibRaw;
  CameraProfileMode camera_profile_mode = CameraProfileMode::Embedded;
  std::optional<AssetIdentifier> camera_profile;
  PhotographicRenderer renderer = PhotographicRenderer::Aces2OcioV1;
  RendererTransitionLookV13 renderer_transition_look =
      RendererTransitionLookV13::None;
  DisplayRenderingTarget rendering_target =
      DisplayRenderingTarget::ActiveDisplay;
  std::optional<AssetIdentifier> device_icc_profile;
  ProofMode proof_mode = ProofMode::Disabled;
  std::optional<AssetIdentifier> proof_profile;
  RenderBackendRequirement backend_requirement =
      RenderBackendRequirement::GpuPermitted;
  RenderPurpose purpose = RenderPurpose::SettledPreview;
  RenderScopeDomain scope_domain =
      RenderScopeDomain::FinalDisplay;
  std::uint32_t source_width = 0;
  std::uint32_t source_height = 0;
  std::vector<RenderStagePlan> stages;
  std::string plan_cache_key;

  bool operator==(const RenderPlan&) const = default;
};

enum class RecipeCompileErrorCode : std::uint8_t {
  InvalidRecipe,
  InvalidSourceGeometry,
  ProfileUnavailable,
  AssetUnavailable,
  UnsupportedColorSpace,
  UnsupportedEffect,
};

struct RecipeCompileError final {
  RecipeCompileErrorCode code = RecipeCompileErrorCode::InvalidRecipe;
  std::string path;
  std::string message;

  bool operator==(const RecipeCompileError&) const = default;
};

struct RecipeCompileResult final {
  std::optional<RenderPlan> plan;
  std::vector<RecipeCompileError> errors;

  [[nodiscard]] bool ok() const noexcept {
    return plan.has_value() && errors.empty();
  }
};

class RecipeCompiler final {
 public:
  [[nodiscard]] RecipeCompileResult compile(
      const EditRecipeV13& recipe,
      const RecipeCompileContext& context) const;
};

[[nodiscard]] std::string serialize_render_plan(const RenderPlan& plan);
[[nodiscard]] std::string_view render_scope_domain_name(
    RenderScopeDomain domain) noexcept;
[[nodiscard]] RenderStageKind render_scope_domain_stop_stage(
    RenderScopeDomain domain) noexcept;

}  // namespace photo_editor::pipeline
