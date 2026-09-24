#pragma once

#include "decoder_provenance.h"
#include "edit_recipe_v13_persistence.h"

#include <QString>

namespace photo_editor::desktop {

struct DesktopRecipePersistencePreparation final {
  QString legacy_recipe_json;
  CanonicalRecipeV13Document canonical_document;
  QString error;
  bool legacy_only = false;

  [[nodiscard]] bool ok() const noexcept {
    return error.isEmpty() &&
           !legacy_recipe_json.isEmpty() &&
           (legacy_only || (!canonical_document.recipe_payload.isEmpty() &&
                            !canonical_document.cache_key.isEmpty() &&
                            !canonical_document.provenance_payload.isEmpty()));
  }
};

[[nodiscard]] DesktopRecipePersistencePreparation
prepare_desktop_recipe_persistence(
    const QString& asset_id,
    const EditRecipeState& state,
    const DecoderProvenanceResult& provenance,
    const QString& recipe_asset_root);

}  // namespace photo_editor::desktop
