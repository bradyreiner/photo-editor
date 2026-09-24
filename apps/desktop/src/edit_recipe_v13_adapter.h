#pragma once

#include "edit_recipe_state.h"
#include "photo_editor/pipeline/edit_recipe_v13.h"
#include "photo_editor/pipeline/recipe_compiler.h"

#include <QString>

#include <cstdint>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace photo_editor::desktop {

enum class DesktopRecipeAdapterErrorCode {
  MissingContext,
  AmbiguousLegacyIntent,
  UnsupportedLegacyIntent,
  InvalidV13Recipe,
};

struct DesktopRecipeAdapterError final {
  DesktopRecipeAdapterErrorCode code =
      DesktopRecipeAdapterErrorCode::UnsupportedLegacyIntent;
  std::string path;
  std::string message;

  bool operator==(const DesktopRecipeAdapterError&) const = default;
};

struct DesktopRecipeV13Context final {
  QString recipe_id;
  QString source_id;
  QString decoder_provenance;
  bool raw_source = false;
  photo_editor::pipeline::DisplayRenderingTarget rendering_target =
      photo_editor::pipeline::DisplayRenderingTarget::ActiveDisplay;
  std::optional<photo_editor::pipeline::AssetIdentifier> device_icc_profile;
  struct MaskPayloadBinding final {
    QString source_id;
    photo_editor::pipeline::AssetIdentifier payload_asset;
  };
  std::vector<MaskPayloadBinding> mask_payload_assets;
  struct LayerContentBinding final {
    QString layer_id;
    photo_editor::pipeline::AssetIdentifier content_asset;
  };
  std::vector<LayerContentBinding> layer_content_assets;
  struct LutPayloadBinding final {
    QString layer_id;
    std::size_t index = 0;
    photo_editor::pipeline::AssetIdentifier payload_asset;
  };
  std::vector<LutPayloadBinding> lut_payload_assets;
};

struct DesktopRecipeV13AdapterResult final {
  std::optional<photo_editor::pipeline::EditRecipeV13> recipe;
  std::vector<DesktopRecipeAdapterError> errors;

  [[nodiscard]] bool ok() const noexcept {
    return recipe.has_value() && errors.empty();
  }
};

struct DesktopRenderPlanResult final {
  std::optional<photo_editor::pipeline::RenderPlan> plan;
  std::vector<DesktopRecipeAdapterError> adapter_errors;
  std::vector<photo_editor::pipeline::RecipeCompileError> compiler_errors;

  [[nodiscard]] bool ok() const noexcept {
    return plan.has_value() && adapter_errors.empty() &&
           compiler_errors.empty();
  }
};

struct DesktopRenderPlanRequest final {
  QString recipe_id;
  QString recipe_payload_digest;
  QString source_id;
  QString decoder_provenance;
  bool raw_source = false;
  std::uint32_t source_width = 0;
  std::uint32_t source_height = 0;
  photo_editor::pipeline::DisplayRenderingTarget rendering_target =
      photo_editor::pipeline::DisplayRenderingTarget::ActiveDisplay;
  std::optional<photo_editor::pipeline::AssetIdentifier> device_icc_profile;
  photo_editor::pipeline::RenderBackendRequirement backend_requirement =
      photo_editor::pipeline::RenderBackendRequirement::GpuPermitted;
  photo_editor::pipeline::RenderPurpose purpose =
      photo_editor::pipeline::RenderPurpose::SettledPreview;
  photo_editor::pipeline::RenderScopeDomain scope_domain =
      photo_editor::pipeline::RenderScopeDomain::FinalDisplay;
};

[[nodiscard]] DesktopRecipeV13AdapterResult adapt_desktop_recipe_to_v13(
    const EditRecipeState& state,
    const DesktopRecipeV13Context& context);

[[nodiscard]] DesktopRenderPlanResult compile_desktop_render_plan(
    const EditRecipeState& state,
    const DesktopRecipeV13Context& adapter_context,
    const photo_editor::pipeline::RecipeCompileContext& compile_context);

[[nodiscard]] DesktopRenderPlanResult
compile_desktop_render_plan_with_derived_assets(
    const EditRecipeState& state,
    const DesktopRenderPlanRequest& request);

}  // namespace photo_editor::desktop
