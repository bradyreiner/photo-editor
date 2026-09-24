#include "photo_layer_adjustment_recipe.h"

namespace photo_editor::desktop {

const QStringList& photo_layer_adjustment_recipe_keys() {
  static const QStringList keys{
      QStringLiteral("exposure"),
      QStringLiteral("contrast"),
      QStringLiteral("highlights"),
      QStringLiteral("shadows"),
      QStringLiteral("whites"),
      QStringLiteral("blacks"),
      QStringLiteral("brightness"),
      QStringLiteral("filmicShoulder"),
      QStringLiteral("filmicToe"),
      QStringLiteral("adaptiveContrast"),
      QStringLiteral("levelsBlackPoint"),
      QStringLiteral("levelsGamma"),
      QStringLiteral("levelsWhitePoint"),
      QStringLiteral("zoneDeepShadows"),
      QStringLiteral("zoneShadows"),
      QStringLiteral("zoneMidtones"),
      QStringLiteral("zoneLights"),
      QStringLiteral("zoneHighlights"),
      QStringLiteral("temperature"),
      QStringLiteral("tint"),
      QStringLiteral("texture"),
      QStringLiteral("clarity"),
      QStringLiteral("dehaze"),
      QStringLiteral("sharpening"),
      QStringLiteral("sharpenRadius"),
      QStringLiteral("sharpenDetail"),
      QStringLiteral("sharpenMasking"),
      QStringLiteral("luminanceNr"),
      QStringLiteral("colorNr"),
      QStringLiteral("lensDistortion"),
      QStringLiteral("lensVignette"),
      QStringLiteral("chromaticAberration"),
      QStringLiteral("perspectiveVertical"),
      QStringLiteral("perspectiveHorizontal"),
      QStringLiteral("transformRotation"),
      QStringLiteral("transformScale"),
      QStringLiteral("transformSkewX"),
      QStringLiteral("transformSkewY"),
      QStringLiteral("liquifyCenterX"),
      QStringLiteral("liquifyCenterY"),
      QStringLiteral("liquifyRadius"),
      QStringLiteral("liquifyStrength"),
      QStringLiteral("contentAwareCropFill"),
      QStringLiteral("vibrance"),
      QStringLiteral("saturation"),
      QStringLiteral("hueShift"),
      QStringLiteral("saturationShift"),
      QStringLiteral("colorizeHue"),
      QStringLiteral("colorizeStrength"),
      QStringLiteral("curveShadows"),
      QStringLiteral("curveDarks"),
      QStringLiteral("curveLights"),
      QStringLiteral("curveHighlights"),
      QStringLiteral("masterCurvePoints"),
      QStringLiteral("lumaCurvePoints"),
      QStringLiteral("redCurvePoints"),
      QStringLiteral("greenCurvePoints"),
      QStringLiteral("blueCurvePoints"),
      QStringLiteral("colorMixer"),
      QStringLiteral("colorGrading"),
      QStringLiteral("lut"),
      QStringLiteral("lutEnabled"),
      QStringLiteral("lutStrength"),
      QStringLiteral("lutName"),
      QStringLiteral("lutTable"),
      QStringLiteral("lutSize"),
      QStringLiteral("lutDomainMin"),
      QStringLiteral("lutDomainMax"),
      QStringLiteral("lutInputColorSpace"),
      QStringLiteral("lutOutputColorSpace"),
      QStringLiteral("lutStack"),
  };
  return keys;
}

QString layer_adjustment_recipe_schema_contract() {
  return QStringLiteral("LayerAdjustmentRecipe:v1|mirrors-global-edit-recipe:true|flat-fields:compat|nested:adjustments.colorMixer,colorGrading,lut|copy-paste-sync-bake:true|future-engine-model:true");
}

QString layer_adjustment_recipe_compatibility_contract() {
  return QStringLiteral("layer-adjustment-recipe-compat:v1|read:legacy-flat-fields+adjustments-map|write:legacy-flat-fields+adjustments-map|preserve-existing-projects:true|migration-target:LayerAdjustmentRecipe");
}

QVariantMap photo_layer_adjustment_recipe(const QVariantMap& layer) {
  (void)layer_adjustment_recipe_schema_contract();
  (void)layer_adjustment_recipe_compatibility_contract();
  QVariantMap recipe;
  for (const auto& key : photo_layer_adjustment_recipe_keys()) {
    if (layer.contains(key)) {
      recipe.insert(key, layer.value(key));
    }
  }
  const auto adjustments = layer.value(QStringLiteral("adjustments")).toMap();
  if (!adjustments.isEmpty()) {
    recipe.insert(QStringLiteral("adjustments"), adjustments);
  }
  return recipe;
}

bool replace_photo_layer_adjustment_recipe(QVariantMap* layer, const QVariantMap& recipe) {
  const auto before = photo_layer_adjustment_recipe(*layer);
  for (const auto& key : photo_layer_adjustment_recipe_keys()) {
    layer->remove(key);
  }
  layer->remove(QStringLiteral("adjustments"));
  for (auto iterator = recipe.cbegin(); iterator != recipe.cend(); ++iterator) {
    layer->insert(iterator.key(), iterator.value());
  }
  return before != photo_layer_adjustment_recipe(*layer);
}

}  // namespace photo_editor::desktop
