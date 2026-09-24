#include "desktop_recipe_persistence_service.h"

namespace photo_editor::desktop {

DesktopRecipePersistencePreparation
prepare_desktop_recipe_persistence(
    const QString& asset_id,
    const EditRecipeState& state,
    const DecoderProvenanceResult& provenance,
    const QString& recipe_asset_root) {
  DesktopRecipePersistencePreparation result;
  if (asset_id.trimmed().isEmpty()) {
    result.error =
        QStringLiteral("Canonical recipe persistence requires a stable asset id.");
    return result;
  }
  // Saving an existing legacy edit must not implicitly migrate its renderer.
  // Canonical recipes still require the exact provenance and V13 validation below.
  if (state.photographic_rendering_model == QStringLiteral("legacy-oklch-v1")) {
    result.legacy_recipe_json = serialize_edit_recipe_json(state);
    result.legacy_only = true;
    return result;
  }
  if (!provenance.ok()) {
    result.error =
        provenance.error.isEmpty()
            ? QStringLiteral("Canonical recipe persistence requires exact decoder provenance.")
            : provenance.error;
    return result;
  }
  const RecipeAssetStore asset_store(recipe_asset_root);
  const auto canonical =
      serialize_desktop_recipe_as_canonical_v13_with_assets(
          state,
          DesktopRecipeV13Context{
              .recipe_id =
                  QStringLiteral("catalog-recipe:%1")
                      .arg(asset_id),
              .source_id = asset_id,
              .decoder_provenance =
                  provenance.provenance,
              .raw_source = provenance.raw_source,
          },
          asset_store);
  if (!canonical.ok()) {
    const auto detail =
        canonical.errors.empty()
            ? QStringLiteral("unknown canonical adapter failure")
            : QString::fromStdString(
                  canonical.errors.front().message);
    result.error =
        QStringLiteral("Edit cannot be represented by canonical recipe v13: %1")
            .arg(detail);
    return result;
  }
  result.legacy_recipe_json =
      serialize_edit_recipe_json(state);
  result.canonical_document = canonical.document;
  return result;
}

}  // namespace photo_editor::desktop
