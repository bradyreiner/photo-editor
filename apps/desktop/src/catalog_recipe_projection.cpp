#include "catalog_recipe_projection.h"

#include "edit_adjustment_patch.h"
#include "edit_recipe_state.h"
#include "edit_recipe_v13_persistence.h"

#include <QJsonDocument>
#include <QJsonObject>

namespace photo_editor::desktop {

CatalogAdjustmentProjectionResult
project_catalog_adjustments_for_desktop(
    const QJsonValue& legacy_recipe,
    const QJsonValue& canonical_recipe,
    const QString& recipe_asset_root) {
  CatalogAdjustmentProjectionResult result;
  result.canonical_present =
      canonical_recipe.isObject();
  if (!result.canonical_present) {
    if (legacy_recipe.isObject()) {
      result.adjustment_patch_json =
          canonical_asset_bound_adjustment_patch_json(
              QString::fromUtf8(
                  QJsonDocument(legacy_recipe.toObject())
                      .toJson(QJsonDocument::Compact)));
    }
    return result;
  }

  const auto canonical_object =
      canonical_recipe.toObject();
  const RecipeAssetStore asset_store(
      recipe_asset_root);
  const auto projection =
      project_canonical_v13_document_to_desktop_state_with_assets(
          CanonicalRecipeV13Document{
              .recipe_payload =
                  canonical_object
                      .value(
                          QStringLiteral(
                              "recipe_payload"))
                      .toString(),
              .cache_key =
                  canonical_object
                      .value(
                          QStringLiteral(
                              "cache_key"))
                      .toString(),
              .provenance_payload =
                  canonical_object
                      .value(
                          QStringLiteral(
                              "provenance_payload"))
                      .toString(),
          },
          asset_store);
  if (!projection.ok()) {
    result.error =
        projection.errors.empty()
            ? QStringLiteral(
                  "Canonical catalog recipe projection failed.")
            : QString::fromStdString(
                  projection.errors.front().message);
    return result;
  }
  result.adjustment_patch_json =
      serialize_adjustment_patch_json(
          *projection.state,
          all_adjustment_groups(),
          AdjustmentPatchScope::AssetBound);
  return result;
}

}  // namespace photo_editor::desktop
