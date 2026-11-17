#include "edit_recipe_state.h"
#include "edit_recipe_component_storage.h"
#include "photo_layer_effect_codec.h"
#include "photo_paint_format.h"
#include "photo_text_style.h"
#include "photo_layer_style_validation.h"

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

  auto object = document.object();
  const auto recipe_version_value =
      object.value(QStringLiteral("recipeVersion"));
  const auto recipe_version_number =
      recipe_version_value.isUndefined()
          ? 1.0
          : recipe_version_value.toDouble(-1.0);
  const auto recipe_version = std::isfinite(recipe_version_number) && recipe_version_number >= 1 &&
          recipe_version_number <= kLatestSupportedEditRecipeVersion
      ? static_cast<int>(recipe_version_number) : -1;
  if (recipe_version_value.isUndefined()) {
    // Older flat recipes did not carry a version and remain valid imports.
  } else if (
      !recipe_version_value.isDouble() ||
      !std::isfinite(recipe_version_number) || recipe_version_number < 1.0 ||
      std::floor(recipe_version_number) != recipe_version_number) {
    result.errors.append(
        QStringLiteral("recipeVersion must be a positive integer"));
  } else if (recipe_version_number > kCurrentEditRecipeVersion && !is_supported_structured_edit_recipe_version(recipe_version)) {
    result.errors.append(
        QStringLiteral("recipeVersion %1 is newer than supported version %2")
            .arg(recipe_version_number)
            .arg(kLatestSupportedEditRecipeVersion));
  }

  QString component_error;
  if (!expand_recipe_component_objects(&object, recipe_version, &component_error)) {
    result.errors.append(component_error);
    return result;
  }

  const auto document_light_value = object.value(
      QStringLiteral("documentLayerStyleLightAngle"));
  if (!document_light_value.isUndefined()) {
    const auto angle = document_light_value.toDouble(-99999.0);
    if (!document_light_value.isDouble() || !std::isfinite(angle) ||
        angle < -3600.0 || angle > 3600.0) {
      result.errors.append(QStringLiteral(
          "documentLayerStyleLightAngle must be finite and within 3600 degrees"));
    }
  }
  auto adjustment_state_object = object;
  adjustment_state_object.remove(QStringLiteral("recipeVersion"));
  const auto lighting_state =
      parse_edit_adjustment_state_object(adjustment_state_object);
  const bool requires_document_light =
      detail::recipe_requires_document_layer_style_light(lighting_state);
  if ((requires_document_light || !document_light_value.isUndefined()) &&
      recipe_version < kPhotoDocumentLayerStyleLightEditRecipeVersion) {
    result.errors.append(QStringLiteral(
        "Document layer style lighting requires desktop recipeVersion 20."));
  }
  if (requires_document_light && document_light_value.isUndefined()) {
    result.errors.append(QStringLiteral(
        "Document layer style lighting requires documentLayerStyleLightAngle."));
  }

  bool has_text_dimensions = false;
  for (const auto* collection : {"layers", "photoLayers"}) {
    const auto layers = object.value(QString::fromLatin1(collection)).toArray();
    has_text_dimensions = has_text_dimensions ||
        detail::photo_layers_have_text_dimensions(layers.toVariantList());
  }
  if (has_text_dimensions &&
      recipe_version < kPhotoTextDimensionEditRecipeVersion) {
    result.errors.append(QStringLiteral(
        "Dimensional text requires desktop recipeVersion 21."));
  }
  if (recipe_version == kPhotoTextDimensionEditRecipeVersion &&
      !has_text_dimensions) {
    result.errors.append(QStringLiteral(
        "Desktop recipeVersion 21 requires a textDimension layer."));
  }

  QVariantList style_masks;
  if (detail::photo_layers_have_styles(object.value(QStringLiteral("layers")).toArray().toVariantList()) ||
      detail::photo_layers_have_styles(object.value(QStringLiteral("photoLayers")).toArray().toVariantList())) {
    auto adjustment_object = object;
    adjustment_object.remove(QStringLiteral("recipeVersion"));
    style_masks = parse_edit_adjustment_state_object(adjustment_object).mask_stack;
  }
  for (const auto* collection : {"layers", "photoLayers"}) {
    const auto layers = object.value(QString::fromLatin1(collection)).toArray();
    QString text_dimension_error;
    if (!validate_photo_text_dimensions_in_layers(
            layers.toVariantList(), &text_dimension_error)) {
      result.errors.append(QStringLiteral("%1: %2")
          .arg(QString::fromLatin1(collection), text_dimension_error));
    }
    if (detail::photo_layers_have_effect_stacks(layers.toVariantList()) && recipe_version < kPhotoEffectsEditRecipeVersion)
      result.errors.append(QStringLiteral("Layer effect stacks require desktop recipeVersion 14."));
    if (detail::photo_layers_have_typography(layers.toVariantList()) && recipe_version < kPhotoTypographyEditRecipeVersion)
      result.errors.append(QStringLiteral("Versioned text requires desktop recipeVersion 15."));
    if (detail::photo_layers_have_paint_tiles(layers.toVariantList()) && recipe_version < kPhotoPaintEditRecipeVersion)
      result.errors.append(QStringLiteral("Paint tiles require desktop recipeVersion 16."));
    if (detail::photo_layers_have_components(layers.toVariantList()) && recipe_version < kPhotoComponentEditRecipeVersion)
      result.errors.append(QStringLiteral("Editable components require desktop recipeVersion 17."));
    if (detail::photo_layers_have_styles(layers.toVariantList()) && recipe_version < kPhotoLayerStyleEditRecipeVersion)
      result.errors.append(QStringLiteral("Shared layer styles require desktop recipeVersion 19."));
    QString style_error;
    if (!validate_photo_layer_style_storage(layers.toVariantList(), &style_error, 0, &style_masks))
      result.errors.append(style_error);
    for (qsizetype index = 0; index < layers.size(); ++index) {
      const auto layer = layers[index].toObject();
      if (layer.contains("paintTiles")) {
        const auto paint = layer.value("paintTiles").toObject();
        if (paint.value("version").toInt() != 1 || paint.value("format").toString() != QLatin1String(kPhotoPaintFormat))
          result.errors.append(QStringLiteral("Unsupported paint tile format."));
      }
      if (layer.contains("textTypography")) {
        const auto typography = layer.value("textTypography").toObject();
        QString style_error;
        const auto decoded_style = decode_photo_text_style(typography, &style_error);
        if (!decoded_style) {
          result.errors.append(QStringLiteral("%1[%2].textTypography: %3")
              .arg(QString::fromLatin1(collection)).arg(index).arg(style_error));
        } else if (!photo_text_runs_fit_content(decoded_style->runs,
                       layer.value("text").toString(QStringLiteral("Text")))) {
          result.errors.append(QStringLiteral("%1[%2].textTypography: character style ranges must fit the text and may not split a Unicode character or CRLF line break")
              .arg(QString::fromLatin1(collection)).arg(index));
        }
      }
      QString effect_error;
      if (!decode_photo_layer_effects(layers[index].toObject().toVariantMap(), &effect_error))
        result.errors.append(QStringLiteral("%1[%2].effectStack: %3")
            .arg(QString::fromLatin1(collection)).arg(index).arg(effect_error));
    }
  }

  if (is_supported_structured_edit_recipe_version(recipe_version)) {
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
