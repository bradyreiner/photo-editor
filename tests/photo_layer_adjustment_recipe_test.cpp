#include "photo_layer_adjustment_recipe.h"
#include "thumbnail_refresh_queue_service.h"

#include <iostream>

namespace {

int fail(const char* message) {
  std::cerr << message << '\n';
  return 1;
}

}  // namespace

int main() {
  using namespace photo_editor::desktop;

  const QVariantList identity_curve{QVariantMap{{"x", 0.0}, {"y", 0.0}},
      QVariantMap{{"x", 0.5}, {"y", 0.5}}, QVariantMap{{"x", 1.0}, {"y", 1.0}}};
  QVariantMap neutral{{"hasNonNeutralAdjustments", true}, {"levelsGamma", 0.0},
      {"levelsWhitePoint", 0.0}, {"transformScale", 1.0}, {"sharpenRadius", 1.0},
      {"sharpenDetail", 50.0}, {"liquifyCenterX", 0.7}, {"liquifyRadius", 0.4},
      {"colorizeHue", 0.4}, {"masterCurvePoints", identity_curve},
      {"adjustments", QVariantMap{{"colorMixer", QVariantMap{{"model", "OKLab"},
          {"rgbChannelMixer", QVariantMap{{"redFromRed", 100.0}, {"greenFromGreen", 100.0},
              {"blueFromBlue", 100.0}}}}},
          {"colorGrading", QVariantMap{{"amount", 100.0}, {"balance", 20.0},
              {"shadows", QVariantMap{{"hue", 0.6}, {"saturation", 0.0}}}}}}}};
  if (photo_layer_has_non_neutral_adjustments(neutral))
    return fail("Neutral persisted defaults and inactive controls must not trigger a photographic develop");
  for (const auto* key : {"exposure", "levelsGamma", "levelsWhitePoint", "curveHighlights"}) {
    auto active = neutral;
    active.insert(QLatin1String(key), 1.0);
    active.insert("hasNonNeutralAdjustments", false);
    if (!photo_layer_has_non_neutral_adjustments(active))
      return fail("Persisted active scalar controls must override missing or stale UI annotations");
  }
  for (const auto& adjustments : QVariantList{
           QVariantMap{{"exposure", 0.5}},
           QVariantMap{{"masterCurvePoints", QVariantList{QVariantMap{{"x", 0.0}, {"y", 0.0}},
               QVariantMap{{"x", 0.5}, {"y", 0.6}}, QVariantMap{{"x", 1.0}, {"y", 1.0}}}}},
           QVariantMap{{"colorMixer", QVariantMap{{"channels", QVariantMap{{"Reds",
               QVariantMap{{"selectiveCyan", 20.0}}}}}}}},
           QVariantMap{{"colorGrading", QVariantMap{{"shadows", QVariantMap{{"saturation", 20.0}}}}}},
           QVariantMap{{"lut", QVariantMap{{"shaperSize", 2},
               {"shaperTable", QVariantList{QVariantList{0.0, 0.0, 0.0},
                   QVariantList{1.0, 0.8, 1.0}}}}}},
           QVariantMap{{"lutEnabled", true}, {"lutStrength", 75.0}, {"lutSize", 2},
               {"lutTable", QVariantList{QVariantList{0.0, 0.0, 0.0}}}}}) {
    if (!photo_layer_has_non_neutral_adjustments({{"adjustments", adjustments}}))
      return fail("Every persisted nested active recipe form must be recognized without UI metadata");
  }

  QVariantMap layer{
      {QStringLiteral("id"), QStringLiteral("layer-a")},
      {QStringLiteral("kind"), QStringLiteral("adjustment")},
      {QStringLiteral("name"), QStringLiteral("Portrait tone")},
      {QStringLiteral("exposure"), 0.75},
      {QStringLiteral("contrast"), 12.0},
      {QStringLiteral("adjustments"),
       QVariantMap{{QStringLiteral("colorGrading"),
                    QVariantMap{{QStringLiteral("balance"), 18.0}}}}},
  };
  const auto recipe = photo_layer_adjustment_recipe(layer);
  if (recipe.size() != 3 || recipe.value(QStringLiteral("exposure")).toDouble() != 0.75 ||
      recipe.contains(QStringLiteral("id")) || recipe.contains(QStringLiteral("name"))) {
    return fail("photo-layer recipe extraction must include only canonical adjustment fields");
  }
  if (!layer_adjustment_recipe_schema_contract().contains(
          QStringLiteral("mirrors-global-edit-recipe:true")) ||
      !layer_adjustment_recipe_compatibility_contract().contains(
          QStringLiteral("read:legacy-flat-fields+adjustments-map"))) {
    return fail("photo-layer recipe owner must preserve schema and legacy-read compatibility contracts");
  }

  const auto recipe_hash = photo_layer_thumbnail_recipe_hash(layer);
  const auto entry_hash = photo_layer_thumbnail_entry_recipe_hash(layer);
  auto changed_recipe_layer = layer;
  changed_recipe_layer.insert(QStringLiteral("exposure"), 1.0);
  if (recipe_hash == photo_layer_thumbnail_recipe_hash(changed_recipe_layer)) {
    return fail("adjustment recipe changes must invalidate the layer thumbnail recipe identity");
  }
  auto changed_composition_layer = layer;
  changed_composition_layer.insert(QStringLiteral("opacity"), 0.5);
  if (recipe_hash != photo_layer_thumbnail_recipe_hash(changed_composition_layer) ||
      entry_hash == photo_layer_thumbnail_entry_recipe_hash(changed_composition_layer)) {
    return fail("composition changes must invalidate only the thumbnail entry identity");
  }

  const QVariantMap replacement{
      {QStringLiteral("shadows"), 24.0},
      {QStringLiteral("adjustments"),
       QVariantMap{{QStringLiteral("lut"),
                    QVariantMap{{QStringLiteral("name"), QStringLiteral("Proof")}}}}},
  };
  if (!replace_photo_layer_adjustment_recipe(&layer, replacement) ||
      photo_layer_adjustment_recipe(layer) != replacement ||
      layer.value(QStringLiteral("id")).toString() != QStringLiteral("layer-a") ||
      layer.contains(QStringLiteral("exposure"))) {
    return fail("recipe replacement must atomically replace adjustment fields while preserving layer identity");
  }
  if (replace_photo_layer_adjustment_recipe(&layer, replacement)) {
    return fail("replacing a photo-layer recipe with identical content must be a no-op");
  }

  for (const auto* kind : {"pixel", "source"}) {
    auto raster = layer;
    raster.insert("kind", QString::fromLatin1(kind));
    if (!photo_layer_supports_adjustment_recipe(raster)) return fail("Raster layers must own color recipes");
    auto changed = raster;
    changed.insert("saturation", -80);
    if (photo_layer_thumbnail_recipe_hash(raster) == photo_layer_thumbnail_recipe_hash(changed))
      return fail("Direct colors must invalidate raster thumbnails");
    raster.insert("paintTiles", QVariantMap{{"tiles", QVariantMap{}}});
    changed.insert("paintTiles", raster.value("paintTiles"));
    if (photo_layer_thumbnail_recipe_hash(raster, 7) == photo_layer_thumbnail_recipe_hash(changed, 7))
      return fail("Direct colors must invalidate thumbnails with an unchanged paint revision");
  }

  const QVariantMap linked_cutout{
      {QStringLiteral("kind"), QStringLiteral("pixel")},
      {QStringLiteral("copyBaseImage"), true},
      {QStringLiteral("copyBaseImageThroughAdjustments"), true},
  };
  if (non_adjustment_photo_layer_thumbnail_recipe_hash(linked_cutout, 11) ==
      non_adjustment_photo_layer_thumbnail_recipe_hash(linked_cutout, 12)) {
    return fail("Linked cutout thumbnail identity must follow changes to its underlying adjustment stack");
  }
  auto legacy_cutout = linked_cutout;
  legacy_cutout.remove(QStringLiteral("copyBaseImageThroughAdjustments"));
  if (non_adjustment_photo_layer_thumbnail_recipe_hash(legacy_cutout, 11) !=
      non_adjustment_photo_layer_thumbnail_recipe_hash(legacy_cutout, 12)) {
    return fail("Markerless saved cutouts must keep their prior thumbnail identity semantics");
  }

  std::cout << "photo_layer_adjustment_recipe_test passed\n";
  return 0;
}
