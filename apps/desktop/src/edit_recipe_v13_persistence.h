#pragma once

#include "edit_recipe_v13_adapter.h"
#include "recipe_asset_store.h"

#include <QString>
#include <QStringList>

#include <optional>
#include <vector>

namespace photo_editor::desktop {

struct CanonicalRecipeV13Document final {
  QString recipe_payload;
  QString cache_key;
  QString provenance_payload;
};

struct CanonicalRecipeV13WriteResult final {
  CanonicalRecipeV13Document document;
  std::vector<DesktopRecipeAdapterError> errors;

  [[nodiscard]] bool ok() const noexcept {
    return errors.empty() && !document.recipe_payload.isEmpty() &&
           !document.cache_key.isEmpty() &&
           !document.provenance_payload.isEmpty();
  }
};

struct CanonicalRecipeV13ProjectionResult final {
  std::optional<EditRecipeState> state;
  std::vector<DesktopRecipeAdapterError> errors;

  [[nodiscard]] bool ok() const noexcept {
    return state.has_value() && errors.empty();
  }
};

struct CanonicalRecipeV13AssetWriteResult final {
  CanonicalRecipeV13Document document;
  std::vector<DesktopRecipeAdapterError> errors;
  int stored_asset_count = 0;

  [[nodiscard]] bool ok() const noexcept {
    return errors.empty() && !document.recipe_payload.isEmpty() &&
           !document.cache_key.isEmpty() &&
           !document.provenance_payload.isEmpty();
  }
};

struct CanonicalRecipeV13SidecarResult final {
  CanonicalRecipeV13Document document;
  std::optional<EditRecipeState> state;
  QString sidecar_path;
  QString error;

  [[nodiscard]] bool ok() const noexcept {
    return error.isEmpty() &&
           !sidecar_path.isEmpty() &&
           (!document.recipe_payload.isEmpty() ||
            state.has_value());
  }
};

[[nodiscard]] CanonicalRecipeV13WriteResult
serialize_desktop_recipe_as_canonical_v13(
    const EditRecipeState& state,
    const DesktopRecipeV13Context& context);

[[nodiscard]] CanonicalRecipeV13AssetWriteResult
serialize_desktop_recipe_as_canonical_v13_with_assets(
    const EditRecipeState& state,
    const DesktopRecipeV13Context& base_context,
    const RecipeAssetStore& asset_store);

[[nodiscard]] CanonicalRecipeV13ProjectionResult
project_canonical_v13_to_desktop_state(
    const photo_editor::pipeline::EditRecipeV13& recipe);

[[nodiscard]] CanonicalRecipeV13ProjectionResult
project_canonical_v13_to_desktop_state_with_assets(
    const photo_editor::pipeline::EditRecipeV13& recipe,
    const RecipeAssetStore& asset_store);

[[nodiscard]] CanonicalRecipeV13ProjectionResult
project_canonical_v13_document_to_desktop_state_with_assets(
    const CanonicalRecipeV13Document& document,
    const RecipeAssetStore& asset_store);

[[nodiscard]] CanonicalRecipeV13SidecarResult
write_canonical_v13_recipe_sidecar(
    const QString& sidecar_path,
    const EditRecipeState& state,
    const DesktopRecipeV13Context& context);

[[nodiscard]] CanonicalRecipeV13SidecarResult
read_canonical_v13_recipe_sidecar(
    const QString& sidecar_path);

[[nodiscard]] QStringList canonical_v13_profile_identities(
    const CanonicalRecipeV13Document& document);

}  // namespace photo_editor::desktop
