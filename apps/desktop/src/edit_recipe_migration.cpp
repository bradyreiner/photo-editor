#include "edit_recipe_state.h"
#include "edit_recipe_component_storage.h"

#include <cmath>

namespace photo_editor::desktop {

EditRecipeMigrationResult migrate_edit_recipe_json(
    const QString& recipe_json,
    int target_version) {
  EditRecipeMigrationResult result;
  result.target_version = target_version == 0 ? kCurrentEditRecipeVersion : target_version;
  if (target_version != 0 && !is_supported_structured_edit_recipe_version(target_version)) {
    result.errors.append(
        QStringLiteral(
            "target recipeVersion %1 is not supported; current version is %2")
            .arg(target_version)
            .arg(kLatestSupportedEditRecipeVersion));
    return result;
  }

  const auto trimmed = recipe_json.trimmed();
  if (trimmed.isEmpty()) {
    result.errors.append(QStringLiteral("recipe JSON is empty"));
    return result;
  }

  QJsonParseError parse_error;
  const auto document =
      QJsonDocument::fromJson(trimmed.toUtf8(), &parse_error);
  if (parse_error.error != QJsonParseError::NoError) {
    result.errors.append(
        QStringLiteral("recipe JSON parse error at offset %1: %2")
            .arg(parse_error.offset)
            .arg(parse_error.errorString()));
    return result;
  }
  if (!document.isObject()) {
    result.errors.append(QStringLiteral("recipe root must be a JSON object"));
    return result;
  }

  const auto object = document.object();
  const auto recipe_version_value =
      object.value(QStringLiteral("recipeVersion"));
  const auto recipe_version_number =
      recipe_version_value.isUndefined()
          ? 1.0
          : recipe_version_value.toDouble(-1.0);
  const auto source_version = std::isfinite(recipe_version_number) && recipe_version_number >= 1 &&
          recipe_version_number <= kLatestSupportedEditRecipeVersion
      ? static_cast<int>(recipe_version_number) : -1;
  result.source_version = source_version;
  if (!recipe_version_value.isUndefined() &&
      (!recipe_version_value.isDouble() || !std::isfinite(recipe_version_number) || recipe_version_number < 1.0 ||
       std::floor(recipe_version_number) != recipe_version_number)) {
    result.errors.append(
        QStringLiteral("recipeVersion must be a positive integer"));
    return result;
  }
  if (recipe_version_number > kCurrentEditRecipeVersion && !is_supported_structured_edit_recipe_version(source_version)) {
    result.errors.append(
        QStringLiteral("recipeVersion %1 is newer than supported version %2")
            .arg(recipe_version_number)
            .arg(kLatestSupportedEditRecipeVersion));
    return result;
  }

  if (is_supported_structured_edit_recipe_version(source_version)) {
    const auto validation = validate_edit_recipe_json(trimmed);
    if (!validation.valid) {
      result.errors = validation.errors;
      return result;
    }
  }
  const auto state = parse_edit_recipe_json(trimmed);
  const bool has_effects = detail::photo_layers_have_effect_stacks(state.layers);
  const bool has_typography = detail::photo_layers_have_typography(state.layers);
  const bool has_paint = detail::photo_layers_have_paint_tiles(state.layers);
  const bool has_styles = detail::photo_layers_have_styles(state.layers);
  const bool has_document_light =
      detail::recipe_requires_document_layer_style_light(state);
  const bool has_text_dimensions =
      detail::photo_layers_have_text_dimensions(state.layers);
  if (target_version == 0)
    target_version = has_text_dimensions || source_version == kPhotoTextDimensionEditRecipeVersion
        ? kPhotoTextDimensionEditRecipeVersion
        : has_document_light || source_version == kPhotoDocumentLayerStyleLightEditRecipeVersion
        ? kPhotoDocumentLayerStyleLightEditRecipeVersion
        : has_styles || source_version == kPhotoLayerStyleEditRecipeVersion ? kPhotoLayerStyleEditRecipeVersion : detail::photo_layers_have_component_objects(state.layers) || source_version == kPhotoComponentObjectTableEditRecipeVersion
        ? kPhotoComponentObjectTableEditRecipeVersion : source_version == kPhotoComponentEditRecipeVersion
        ? kPhotoComponentEditRecipeVersion : has_paint || source_version == kPhotoPaintEditRecipeVersion
        ? kPhotoPaintEditRecipeVersion : has_typography || source_version == kPhotoTypographyEditRecipeVersion
        ? kPhotoTypographyEditRecipeVersion : has_effects || source_version == kPhotoEffectsEditRecipeVersion
        ? kPhotoEffectsEditRecipeVersion : kCurrentEditRecipeVersion;
  result.target_version = target_version;
  if (has_styles && target_version < kPhotoLayerStyleEditRecipeVersion) {
    result.errors.append(QStringLiteral("Cannot downgrade a recipe with shared layer styles below version 19."));
    return result;
  }
  if (has_document_light &&
      target_version < kPhotoDocumentLayerStyleLightEditRecipeVersion) {
    result.errors.append(QStringLiteral(
        "Cannot downgrade a recipe with document layer style lighting below version 20."));
    return result;
  }
  if (has_text_dimensions &&
      target_version < kPhotoTextDimensionEditRecipeVersion) {
    result.errors.append(QStringLiteral(
        "Cannot downgrade a recipe with dimensional text below version 21."));
    return result;
  }
  if (detail::photo_layers_have_components(state.layers) && target_version < kPhotoComponentEditRecipeVersion) {
    result.errors.append(QStringLiteral("Cannot downgrade a recipe with editable components below version 17.")); return result;
  }
  if (has_paint && target_version < kPhotoPaintEditRecipeVersion) {
    result.errors.append(QStringLiteral("Cannot downgrade a recipe with paint tiles below version 16."));
    return result;
  }
  if (has_typography && target_version < kPhotoTypographyEditRecipeVersion) {
    result.errors.append(QStringLiteral("Cannot downgrade a recipe with versioned typography below version 15."));
    return result;
  }
  if (has_effects && target_version < kPhotoEffectsEditRecipeVersion) {
    result.errors.append(QStringLiteral("Cannot downgrade a recipe with photo effects to version 12."));
    return result;
  }
  auto migrated = build_edit_adjustment_state_object(state);
  migrated.insert(QStringLiteral("recipeVersion"), target_version);
  if (target_version >= kPhotoComponentObjectTableEditRecipeVersion) {
    QString component_error;
    if (!pack_recipe_component_objects(&migrated, &component_error, true)) {
      result.errors.append(component_error);
      return result;
    }
  }
  result.recipe_json = QString::fromUtf8(QJsonDocument(migrated).toJson(QJsonDocument::Compact));
  const auto validation = validate_edit_recipe_json(result.recipe_json);
  if (!validation.valid) {
    result.errors = validation.errors;
    result.recipe_json.clear();
    return result;
  }
  result.migrated = source_version != target_version;
  return result;
}

}  // namespace photo_editor::desktop
