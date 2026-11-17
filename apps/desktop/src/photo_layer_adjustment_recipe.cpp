#include "photo_layer_adjustment_recipe.h"

#include <cmath>

namespace photo_editor::desktop {

bool photo_layer_supports_adjustment_recipe(const QVariantMap& layer) {
  const auto kind = layer.value(QStringLiteral("kind")).toString();
  return !layer.value(QStringLiteral("baseImageEdit")).toBool() &&
      (kind == QStringLiteral("adjustment") || kind == QStringLiteral("group") ||
       kind == QStringLiteral("pixel") || kind == QStringLiteral("source"));
}

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

bool photo_layer_has_non_neutral_adjustments(const QVariantMap& layer) {
  auto recipe = photo_layer_adjustment_recipe(layer);
  const auto nested = recipe.take(QStringLiteral("adjustments")).toMap();
  for (auto it = nested.cbegin(); it != nested.cend(); ++it) recipe.insert(it.key(), it.value());
  const auto differs = [](const QVariant& value, double neutral = 0.0) {
    const auto number = value.toDouble();
    return !std::isfinite(number) || std::abs(number - neutral) > 1.0e-9;
  };
  for (const auto& key : photo_layer_adjustment_recipe_keys()) {
    if (!recipe.contains(key)) continue;
    // These parameters only locate/shape an effect enabled by another control.
    if (key == QStringLiteral("sharpenRadius") || key == QStringLiteral("sharpenDetail") ||
        key == QStringLiteral("sharpenMasking") || key.startsWith(QStringLiteral("liquifyCenter")) ||
        key == QStringLiteral("liquifyRadius") || key == QStringLiteral("colorizeHue") ||
        key.startsWith(QStringLiteral("lut")) || key == QStringLiteral("colorMixer") ||
        key == QStringLiteral("colorGrading")) continue;
    if (key.endsWith(QStringLiteral("CurvePoints"))) {
      for (const auto& point_value : recipe.value(key).toList()) {
        const auto point = point_value.toMap();
        if (differs(point.value(QStringLiteral("y")), point.value(QStringLiteral("x")).toDouble()))
          return true;
      }
    } else if (differs(recipe.value(key), key == QStringLiteral("transformScale") ? 1.0 : 0.0)) {
      return true;
    }
  }
  const auto mixer = recipe.value(QStringLiteral("colorMixer")).toMap();
  for (const auto* field : {"hue", "saturation", "luminance"})
    if (differs(mixer.value(QLatin1String(field)))) return true;
  for (const auto* field : {"hueAdjustment", "saturationAdjustment", "luminanceAdjustment"})
    if (differs(recipe.value(QLatin1String(field)))) return true;
  for (const auto& channel_value : mixer.value(QStringLiteral("channels")).toMap()) {
    const auto channel = channel_value.toMap();
    for (const auto* field : {"hue", "saturation", "luminance", "selectiveCyan",
             "selectiveMagenta", "selectiveYellow", "selectiveBlack"})
      if (differs(channel.value(QLatin1String(field)))) return true;
  }
  const auto rgb = mixer.value(QStringLiteral("rgbChannelMixer")).toMap();
  for (const auto* output : {"red", "green", "blue"})
    for (const auto* input : {"Red", "Green", "Blue"}) {
      const auto key = QString::fromLatin1(output) + QStringLiteral("From") + QLatin1String(input);
      const auto neutral = QString::fromLatin1(output).compare(QLatin1String(input), Qt::CaseInsensitive) == 0
          ? 100.0 : 0.0;
      if (differs(rgb.value(key, neutral), neutral)) return true;
    }
  if (mixer.value(QStringLiteral("blackAndWhite")).toMap().value(QStringLiteral("enabled")).toBool())
    return true;
  for (const auto* field : {"colorReplacement", "skinToneProtection", "colorHarmony"}) {
    const auto effect = mixer.value(QLatin1String(field)).toMap();
    if (effect.value(QStringLiteral("enabled")).toBool() &&
        differs(effect.value(QStringLiteral("strength")))) return true;
  }
  const auto grading = recipe.value(QStringLiteral("colorGrading")).toMap();
  if (differs(grading.value(QStringLiteral("amount"), 100.0))) {
    for (const auto* field : {"shadows", "midtones", "highlights"}) {
      const auto band = grading.value(QLatin1String(field)).toMap();
      if (differs(band.value(QStringLiteral("saturation"))) ||
          differs(band.value(QStringLiteral("luminance")))) return true;
    }
  }
  auto luts = recipe.value(QStringLiteral("lutStack")).toList();
  luts.append(recipe.value(QStringLiteral("lut")));
  for (const auto& lut_value : luts) {
    const auto lut = lut_value.toMap();
    if (lut.value(QStringLiteral("enabled"), true).toBool() &&
        lut.value(QStringLiteral("strength"), 100.0).toDouble() > 1.0e-9 &&
        ((lut.value(QStringLiteral("size")).toInt() > 1 &&
          !lut.value(QStringLiteral("table")).toList().isEmpty()) ||
         (lut.value(QStringLiteral("shaperSize")).toInt() > 1 &&
          !lut.value(QStringLiteral("shaperTable")).toList().isEmpty()))) return true;
  }
  if (!recipe.contains(QStringLiteral("lut")) &&
      recipe.value(QStringLiteral("lutEnabled")).toBool() &&
      recipe.value(QStringLiteral("lutStrength"), 100.0).toDouble() > 1.0e-9 &&
      recipe.value(QStringLiteral("lutSize")).toInt() > 1 &&
      !recipe.value(QStringLiteral("lutTable")).toList().isEmpty()) return true;
  return false;
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
