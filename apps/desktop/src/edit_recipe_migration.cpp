#include "edit_recipe_state.h"

#include <cmath>

namespace photo_editor::desktop {

EditRecipeMigrationResult migrate_edit_recipe_json(
    const QString& recipe_json,
    int target_version) {
  EditRecipeMigrationResult result;
  result.target_version = target_version == 0 ? kCurrentEditRecipeVersion : target_version;
  if (target_version != 0 && target_version != kCurrentEditRecipeVersion && target_version != kPhotoEffectsEditRecipeVersion && target_version != kPhotoTypographyEditRecipeVersion && target_version != kPhotoPaintEditRecipeVersion) {
    result.errors.append(
        QStringLiteral(
            "target recipeVersion %1 is not supported; current version is %2")
            .arg(target_version)
            .arg(kPhotoPaintEditRecipeVersion));
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
          recipe_version_number <= kPhotoPaintEditRecipeVersion
      ? static_cast<int>(recipe_version_number) : -1;
  result.source_version = source_version;
  if (!recipe_version_value.isUndefined() &&
      (!recipe_version_value.isDouble() || !std::isfinite(recipe_version_number) || recipe_version_number < 1.0 ||
       std::floor(recipe_version_number) != recipe_version_number)) {
    result.errors.append(
        QStringLiteral("recipeVersion must be a positive integer"));
    return result;
  }
  if (recipe_version_number > kCurrentEditRecipeVersion && source_version != kPhotoEffectsEditRecipeVersion && source_version != kPhotoTypographyEditRecipeVersion && source_version != kPhotoPaintEditRecipeVersion) {
    result.errors.append(
        QStringLiteral("recipeVersion %1 is newer than supported version %2")
            .arg(recipe_version_number)
            .arg(kPhotoPaintEditRecipeVersion));
    return result;
  }

  if (source_version == kCurrentEditRecipeVersion || source_version == kPhotoEffectsEditRecipeVersion || source_version == kPhotoTypographyEditRecipeVersion || source_version == kPhotoPaintEditRecipeVersion) {
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
  if (target_version == 0)
    target_version = has_paint || source_version == kPhotoPaintEditRecipeVersion
        ? kPhotoPaintEditRecipeVersion : has_typography || source_version == kPhotoTypographyEditRecipeVersion
        ? kPhotoTypographyEditRecipeVersion : has_effects || source_version == kPhotoEffectsEditRecipeVersion
        ? kPhotoEffectsEditRecipeVersion : kCurrentEditRecipeVersion;
  result.target_version = target_version;
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
  auto migrated = build_edit_recipe_object(state);
  migrated.insert(QStringLiteral("recipeVersion"), target_version);
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
