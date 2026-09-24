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

  std::cout << "photo_layer_adjustment_recipe_test passed\n";
  return 0;
}
