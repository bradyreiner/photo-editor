#include "desktop_pipeline_state_compiler.h"
#include "edit_recipe_pipeline_bridge.h"
#include "edit_recipe_v13_persistence.h"
#include "photo_paint_surface.h"
#include "photo_pixel_stroke.h"

#include <QCoreApplication>
#include <QTemporaryDir>
#include <algorithm>
#include <cmath>
#include <iostream>

using namespace photo_editor::desktop;
using namespace photo_editor::pipeline;

bool verify(bool erase, bool movable_base, bool transformed, QRect creation_crop, QRect execution_crop) {
  const QSize source_size(512, 320);
  const DesktopSourceGeometry original{source_size, creation_crop};
  const auto area = original.edge_area();
  QVariantMap layer{{"id", "paint-source-basis"}, {"kind", "pixel"},
      {"pixelWidth", creation_crop.width()}, {"pixelHeight", creation_crop.height()},
      {"canvasWidth", creation_crop.width()}, {"canvasHeight", creation_crop.height()},
      {"coordinateSpace", "source-area"},
      {"coordinateArea", QVariantMap{{"x", area.x()}, {"y", area.y()}, {"width", area.width()}, {"height", area.height()}}},
      {"paintTiles", empty_photo_paint_tiles(QColorSpace(QColorSpace::SRgbLinear))},
      {"baseImageEdit", erase}, {"copyBaseImage", movable_base}};
  PhotoPixelStroke stroke;
  stroke.color = QColor(230, 80, 30, 170);
  stroke.diameter = 59.0; stroke.hardness = 0.75;
  stroke.opacity = 0.65; stroke.pressure_size = false;
  if (!stroke.begin(layer)) return false;
  stroke.point({80.5, 70.5}, 1.0); stroke.point({180.5, 110.5}, 1.0); stroke.finish();
  layer.insert("paintTiles", stroke.commit());
  if (transformed) {
    layer.insert("x", 17); layer.insert("y", -8);
    layer.insert("scale", 0.8); layer.insert("rotation", 23.0);
  }
  QTemporaryDir store_directory;
  RecipeAssetStore assets(store_directory.path());
  DesktopRecipeV13Context context;
  context.recipe_id = QStringLiteral("paint-source-basis-roundtrip");
  context.source_id = QStringLiteral("paint-source-basis-source");
  context.decoder_provenance = QStringLiteral("QtImageReader:paint-source-basis");
  EditRecipeState saved_state; saved_state.layers = {layer};
  const auto saved = serialize_desktop_recipe_as_canonical_v13_with_assets(saved_state, context, assets);
  if (!saved.ok()) { std::cerr << "Canonical paint source basis save failed\n"; return false; }
  const auto restored = project_canonical_v13_document_to_desktop_state_with_assets(saved.document, assets);
  if (!restored.ok() || restored.state->layers.size() != 1 ||
      restored.state->layers.front().toMap().value("coordinateArea").toMap() != layer.value("coordinateArea").toMap() ||
      restored.state->layers.front().toMap().value("coordinateSpace") != layer.value("coordinateSpace")) {
    std::cerr << "Canonical paint source basis was lost on reopen\n"; return false;
  }
  const auto render = [&](QRect crop) {
    const DesktopSourceGeometry geometry{source_size, crop};
    EditRecipeState recipe = *restored.state;
    recipe = parse_edit_recipe_json(serialize_edit_recipe_json(recipe));
    if (recipe.layers.size() != 1 || recipe.layers.front().toMap().value("coordinateArea").toMap() !=
        layer.value("coordinateArea").toMap()) return std::vector<ColorPixel>{};
    const auto config = compile_color_pipeline_config(recipe);
    std::vector<ColorPixel> pixels(static_cast<std::size_t>(crop.width()) * static_cast<std::size_t>(crop.height()),
        ColorPixel{0.1F, 0.2F, 0.3F, 1.0F});
    const auto pipeline = compile_desktop_pipeline_state(recipe, nullptr, config,
        DesktopRenderPurpose::Preview, LocalMaskRasterPolicy::UseStoredRaster, crop.size(), pixels, geometry);
    if (!apply_color_layers_stage(&pixels, crop.width(), crop.height(), pipeline)) pixels.clear();
    return pixels;
  };
  const auto reference = render(QRect(QPoint(), source_size));
  const auto cropped = render(execution_crop);
  if (reference.empty() || cropped.empty()) return false;
  double maximum = 0;
  for (int y = 0; y < execution_crop.height(); ++y) {
    for (int x = 0; x < execution_crop.width(); ++x) {
      const auto& a = cropped[static_cast<std::size_t>(y * execution_crop.width() + x)];
      const auto& b = reference[static_cast<std::size_t>((y + execution_crop.y()) * source_size.width() + x + execution_crop.x())];
      maximum = std::max({maximum, std::abs(static_cast<double>(a.red - b.red)),
          std::abs(static_cast<double>(a.green - b.green)), std::abs(static_cast<double>(a.blue - b.blue)),
          std::abs(static_cast<double>(a.alpha - b.alpha))});
    }
  }
  if (maximum > 2.0e-6) {
    std::cerr << "Paint source basis moved after crop: erase=" << erase << " movable=" << movable_base
              << " transform=" << transformed << " max=" << maximum << '\n';
    return false;
  }
  return true;
}

int main(int argc, char** argv) {
  QCoreApplication application(argc, argv);
  for (const auto creation : {QRect(0, 0, 512, 320), QRect(64, 32, 384, 256)}) {
    for (bool erase : {false, true}) {
      if (!verify(erase, false, false, creation, QRect(96, 48, 320, 224))) return 1;
      if (!erase && !verify(false, false, true, creation, QRect(96, 48, 320, 224))) return 1;
      if (erase && !verify(true, true, false, creation, QRect(96, 48, 320, 224))) return 1;
    }
  }
  std::cout << "Paint and Base Image erasing retain source pixels across crop changes and layer transforms.\n";
  return 0;
}
