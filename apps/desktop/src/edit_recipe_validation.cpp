#include "edit_recipe_state.h"
#include "photo_layer_effect_codec.h"
#include "photo_paint_format.h"

#include <cmath>

namespace photo_editor::desktop {

EditRecipeValidationResult validate_edit_recipe_json(
    const QString& recipe_json) {
  EditRecipeValidationResult result;
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
  const auto recipe_version = std::isfinite(recipe_version_number) && recipe_version_number >= 1 &&
          recipe_version_number <= kPhotoPaintEditRecipeVersion
      ? static_cast<int>(recipe_version_number) : -1;
  if (recipe_version_value.isUndefined()) {
    // Older flat recipes did not carry a version and remain valid imports.
  } else if (
      !recipe_version_value.isDouble() ||
      !std::isfinite(recipe_version_number) || recipe_version_number < 1.0 ||
      std::floor(recipe_version_number) != recipe_version_number) {
    result.errors.append(
        QStringLiteral("recipeVersion must be a positive integer"));
  } else if (recipe_version_number > kCurrentEditRecipeVersion && recipe_version != kPhotoEffectsEditRecipeVersion && recipe_version != kPhotoTypographyEditRecipeVersion && recipe_version != kPhotoPaintEditRecipeVersion) {
    result.errors.append(
        QStringLiteral("recipeVersion %1 is newer than supported version %2")
            .arg(recipe_version_number)
            .arg(kPhotoPaintEditRecipeVersion));
  }

  for (const auto* collection : {"layers", "photoLayers"}) {
    const auto layers = object.value(QString::fromLatin1(collection)).toArray();
    if (detail::photo_layers_have_effect_stacks(layers.toVariantList()) && recipe_version != kPhotoEffectsEditRecipeVersion && recipe_version != kPhotoTypographyEditRecipeVersion && recipe_version != kPhotoPaintEditRecipeVersion)
      result.errors.append(QStringLiteral("Layer effect stacks require desktop recipeVersion 14."));
    if (detail::photo_layers_have_typography(layers.toVariantList()) && recipe_version != kPhotoTypographyEditRecipeVersion && recipe_version != kPhotoPaintEditRecipeVersion)
      result.errors.append(QStringLiteral("Versioned text requires desktop recipeVersion 15."));
    if (detail::photo_layers_have_paint_tiles(layers.toVariantList()) && recipe_version != kPhotoPaintEditRecipeVersion)
      result.errors.append(QStringLiteral("Paint tiles require desktop recipeVersion 16."));
    for (qsizetype index = 0; index < layers.size(); ++index) {
      const auto layer = layers[index].toObject();
      if (layer.contains("paintTiles")) {
        const auto paint = layer.value("paintTiles").toObject();
        if (paint.value("version").toInt() != 1 || paint.value("format").toString() != QLatin1String(kPhotoPaintFormat))
          result.errors.append(QStringLiteral("Unsupported paint tile format."));
      }
      if (layer.contains("textTypography")) {
        const auto typography = layer.value("textTypography").toObject();
        if (typography.value("version").toDouble(-1) != 1 && typography.value("version").toDouble(-1) != 2)
          result.errors.append(QStringLiteral("%1[%2].textTypography: Unsupported text style version.")
              .arg(QString::fromLatin1(collection)).arg(index));
      }
      QString effect_error;
      if (!decode_photo_layer_effects(layers[index].toObject().toVariantMap(), &effect_error))
        result.errors.append(QStringLiteral("%1[%2].effectStack: %3")
            .arg(QString::fromLatin1(collection)).arg(index).arg(effect_error));
    }
  }

  if (recipe_version == kCurrentEditRecipeVersion || recipe_version == kPhotoEffectsEditRecipeVersion || recipe_version == kPhotoTypographyEditRecipeVersion || recipe_version == kPhotoPaintEditRecipeVersion) {
    const QStringList required_objects = {
        QStringLiteral("rawAdjustments"),
        QStringLiteral("globalAdjustments"),
        QStringLiteral("colorPipeline"),
        QStringLiteral("curves"),
        QStringLiteral("colorMixer"),
        QStringLiteral("colorGrading"),
        QStringLiteral("cropTransform"),
        QStringLiteral("outputTransform"),
        QStringLiteral("exportTransform"),
        QStringLiteral("optics"),
        QStringLiteral("history"),
        QStringLiteral("guideSettings"),
    };
    for (const auto& key : required_objects) {
      if (!object.value(key).isObject()) {
        result.errors.append(QStringLiteral("%1 must be an object").arg(key));
      }
    }

    const QStringList required_arrays = {
        QStringLiteral("maskStack"),
        QStringLiteral("maskSets"),
        QStringLiteral("retouchStack"),
        QStringLiteral("presetApplications"),
        QStringLiteral("layers"),
        QStringLiteral("overlayItems"),
    };
    for (const auto& key : required_arrays) {
      if (!object.value(key).isArray()) {
        result.errors.append(QStringLiteral("%1 must be an array").arg(key));
      }
    }
    if (object.contains(QStringLiteral("photoLayers")) &&
        !object.value(QStringLiteral("photoLayers")).isArray()) {
      result.errors.append(QStringLiteral("photoLayers must be an array"));
    }

    const auto curves = object.value(QStringLiteral("curves")).toObject();
    if (!curves.value(QStringLiteral("parametric")).isObject()) {
      result.errors.append(
          QStringLiteral("curves.parametric must be an object"));
    }
    if (!curves.value(QStringLiteral("channels")).isArray()) {
      result.errors.append(
          QStringLiteral("curves.channels must be an array"));
    }

    const auto raw_adjustments =
        object.value(QStringLiteral("rawAdjustments")).toObject();
    if (!raw_adjustments
             .value(QStringLiteral("highlightReconstructionModel"))
             .isString()) {
      result.errors.append(
          QStringLiteral(
              "rawAdjustments.highlightReconstructionModel must be a string"));
    } else {
      const auto reconstruction_model =
          raw_adjustments
              .value(QStringLiteral("highlightReconstructionModel"))
              .toString();
      if (reconstruction_model !=
              QStringLiteral("legacy-working-rgb-v1") &&
          reconstruction_model !=
              QStringLiteral("camera-linear-opposed-v2")) {
        result.errors.append(
            QStringLiteral(
                "rawAdjustments.highlightReconstructionModel must be legacy-working-rgb-v1 or camera-linear-opposed-v2"));
      }
    }

    const auto output_transform =
        object.value(QStringLiteral("outputTransform")).toObject();
    if (!output_transform
             .value(QStringLiteral("previewColorSpace"))
             .isString()) {
      result.errors.append(
          QStringLiteral(
              "outputTransform.previewColorSpace must be a string"));
    }
    if (output_transform.contains(QStringLiteral("previewMode")) &&
        !output_transform.value(QStringLiteral("previewMode")).isString()) {
      result.errors.append(
          QStringLiteral("outputTransform.previewMode must be a string"));
    } else if (
        output_transform.contains(QStringLiteral("previewMode")) &&
        normalized_preview_output_mode_name(
            output_transform.value(QStringLiteral("previewMode")).toString()) !=
            output_transform.value(QStringLiteral("previewMode")).toString()) {
      result.errors.append(
          QStringLiteral(
              "outputTransform.previewMode must be Monitor, Match Export, Soft Proof, or Reference Display Target"));
    }
    if (output_transform.contains(QStringLiteral("workingColorSpace"))) {
      result.errors.append(
          QStringLiteral(
              "outputTransform.workingColorSpace is legacy; use colorPipeline.workingProfile"));
    }
    if (output_transform.contains(QStringLiteral("colorSpace"))) {
      result.errors.append(
          QStringLiteral(
              "outputTransform.colorSpace is legacy; use exportTransform.colorSpace"));
    }
    if (output_transform.contains(QStringLiteral("renderIntent"))) {
      result.errors.append(
          QStringLiteral(
              "outputTransform.renderIntent is legacy; use colorPipeline.renderIntent"));
    }
    if (output_transform.contains(QStringLiteral("headroom"))) {
      result.errors.append(
          QStringLiteral(
              "outputTransform.headroom is legacy; use colorPipeline.headroom"));
    }

    const auto export_transform =
        object.value(QStringLiteral("exportTransform")).toObject();
    if (!export_transform.value(QStringLiteral("colorSpace")).isString()) {
      result.errors.append(
          QStringLiteral("exportTransform.colorSpace must be a string"));
    }

    const auto color_pipeline =
        object.value(QStringLiteral("colorPipeline")).toObject();
    for (const auto& key : {
             QStringLiteral("inputProfile"),
             QStringLiteral("workingProfile"),
             QStringLiteral("previewOutputProfile"),
             QStringLiteral("exportOutputProfile"),
             QStringLiteral("renderIntent")}) {
      if (!color_pipeline.value(key).isString()) {
        result.errors.append(
            QStringLiteral("colorPipeline.%1 must be a string").arg(key));
      }
    }
    if (!color_pipeline.value(QStringLiteral("proofing")).isObject()) {
      result.errors.append(
          QStringLiteral("colorPipeline.proofing must be an object"));
    } else {
      const auto proofing =
          color_pipeline.value(QStringLiteral("proofing")).toObject();
      if (!proofing.value(QStringLiteral("renderIntent")).isString()) {
        result.errors.append(
            QStringLiteral(
                "colorPipeline.proofing.renderIntent must be a string"));
      }
      if (!proofing.value(QStringLiteral("simulatePaperWhite")).isBool()) {
        result.errors.append(
            QStringLiteral(
                "colorPipeline.proofing.simulatePaperWhite must be a boolean"));
      }
      if (!proofing.value(QStringLiteral("simulateInkBlack")).isBool()) {
        result.errors.append(
            QStringLiteral(
                "colorPipeline.proofing.simulateInkBlack must be a boolean"));
      }
    }
    if (!color_pipeline.value(QStringLiteral("headroom")).isDouble()) {
      result.errors.append(
          QStringLiteral("colorPipeline.headroom must be a number"));
    }
    if (!color_pipeline
             .value(QStringLiteral("gamutMappingEnabled"))
             .isBool()) {
      result.errors.append(
          QStringLiteral(
              "colorPipeline.gamutMappingEnabled must be a boolean"));
    }
    if (!color_pipeline
             .value(QStringLiteral("gamutMappingPolicy"))
             .isString()) {
      result.errors.append(
          QStringLiteral(
              "colorPipeline.gamutMappingPolicy must be a string"));
    }
    if (!color_pipeline
             .value(QStringLiteral("blackPointCompensationEnabled"))
             .isBool()) {
      result.errors.append(
          QStringLiteral(
              "colorPipeline.blackPointCompensationEnabled must be a boolean"));
    }
    if (!color_pipeline.value(QStringLiteral("sourceBlackPoint")).isDouble()) {
      result.errors.append(
          QStringLiteral(
              "colorPipeline.sourceBlackPoint must be a number"));
    }
    if (!color_pipeline.value(QStringLiteral("outputBlackPoint")).isDouble()) {
      result.errors.append(
          QStringLiteral(
              "colorPipeline.outputBlackPoint must be a number"));
    }
    validate_edit_recipe_profile_contract(color_pipeline, &result.errors);
    validate_edit_recipe_content_contract(object, &result.errors);
  }

  result.valid = result.errors.isEmpty();
  return result;
}

}  // namespace photo_editor::desktop
