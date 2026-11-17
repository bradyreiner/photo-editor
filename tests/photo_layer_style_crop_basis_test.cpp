#include "edit_recipe_state.h"
#include "photo_paint_surface.h"
#include "photo_render_utils.h"
#include "photo_layer_style_codec.h"
#include "photo_editor/pipeline/layer_styles.h"

#include <QColorSpace>
#include <QCoreApplication>
#include <QImage>
#include <QVariantMap>
#include <QtGui/qrgbafloat.h>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace photo_editor::desktop;
using namespace photo_editor::pipeline;

namespace {

void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

QImage make_source(QSize size) {
  QImage image(size, QImage::Format_ARGB32);
  image.setColorSpace(QColorSpace(QColorSpace::SRgb));
  for (int y = 0; y < size.height(); ++y) {
    for (int x = 0; x < size.width(); ++x) {
      image.setPixelColor(x, y, QColor(30 + x * 2, 35 + y * 2,
          95 + ((x + y) % 48)));
    }
  }
  return image;
}

QVariantMap make_authored_paint_layer(QSize canvas_size) {
  auto layer = QVariantMap{
      {QStringLiteral("id"), QStringLiteral("crop-style-authored-paint")},
      {QStringLiteral("name"), QStringLiteral("Authored source-area paint")},
      {QStringLiteral("kind"), QStringLiteral("pixel")},
      {QStringLiteral("enabled"), true},
      {QStringLiteral("opacity"), 1.0},
      {QStringLiteral("x"), 0},
      {QStringLiteral("y"), 0},
      {QStringLiteral("scale"), 1.0},
      {QStringLiteral("rotation"), 0.0},
      {QStringLiteral("pixelWidth"), canvas_size.width()},
      {QStringLiteral("pixelHeight"), canvas_size.height()},
      {QStringLiteral("canvasWidth"), canvas_size.width()},
      {QStringLiteral("canvasHeight"), canvas_size.height()},
      {QStringLiteral("coordinateSpace"), QStringLiteral("source-area")},
      {QStringLiteral("coordinateArea"), QVariantMap{
          {QStringLiteral("x"), 0.0}, {QStringLiteral("y"), 0.0},
          {QStringLiteral("width"), 1.0}, {QStringLiteral("height"), 1.0}}},
      {QStringLiteral("paintTiles"), empty_photo_paint_tiles(
          QColorSpace(QColorSpace::SRgbLinear))}};

  PhotoPaintSurface surface;
  QString error;
  require(surface.load(layer, &error), "Load the small authored paint-tile fixture.");
  auto tile = surface.tile(0, 0, &error);
  require(!tile.isNull(), "Decode the authored paint tile.");
  for (int y = 15; y < 57; ++y) {
    auto* row = reinterpret_cast<QRgbaFloat32*>(tile.scanLine(y));
    for (int x = 20; x < 80; ++x)
      row[x] = QRgbaFloat32{0.72F, 0.18F, 0.055F, 1.0F};
  }
  surface.store(0, 0, tile);
  layer.insert(QStringLiteral("paintTiles"), surface.snapshot());
  return layer;
}

QString encode_stack(const LayerStyleStack& stack) {
  std::string error;
  const auto encoded = serialize_layer_style_stack(stack, &error);
  require(encoded.has_value(), "Serialize the deterministic layer-style stack.");
  return QString::fromStdString(*encoded);
}

LayerStyleStack gradient_stack() {
  LayerStyleOperation operation;
  require(layer_style_codec::create_default_operation(QStringLiteral("gradient_overlay"),
              QStringLiteral("crop-gradient"), &operation, nullptr),
          "Create the gradient-overlay operation.");
  auto& gradient = std::get<LayerStyleGradientOverlay>(operation.parameters).gradient;
  gradient.angle_degrees = 0.0;
  gradient.stops = {
      {0.0, LayerStyleColor{.red = 0.98, .green = 0.08, .blue = 0.03, .alpha = 1.0}},
      {1.0, LayerStyleColor{.red = 0.04, .green = 0.16, .blue = 0.96, .alpha = 1.0}}};
  return LayerStyleStack{1, true, {operation}};
}

LayerStyleStack stroke_stack() {
  LayerStyleOperation operation;
  require(layer_style_codec::create_default_operation(QStringLiteral("stroke"),
              QStringLiteral("crop-stroke"), &operation, nullptr),
          "Create the stroke operation.");
  auto& stroke = std::get<LayerStyleStroke>(operation.parameters);
  stroke.width = 12.0;
  stroke.alignment = LayerStyleStrokeAlignment::Outside;
  stroke.fill = LayerStyleColor{.red = 0.08, .green = 0.96, .blue = 0.18, .alpha = 1.0};
  return LayerStyleStack{1, true, {operation}};
}

QImage render(const QImage& input, const QImage& full_source,
              const DesktopSourceGeometry& geometry,
              const QVariantMap& paint_layer) {
  EditRecipeState state;
  auto layer = paint_layer;
  state.layers = {layer};
  RenderMaskOptions options;
  options.source_geometry = geometry;
  options.full_source = full_source;
  const auto output = render_output_image_with_backend(input, state,
      QColorSpace(QColorSpace::SRgb), QImage::Format_ARGB32,
      RenderBackendPreference::CpuOnly, nullptr, nullptr, options);
  require(!output.isNull(), "Render the authored source-area layer through the CPU photo pipeline.");
  require(output.size() == input.size(), "Keep the render on the physical source or crop canvas.");
  return output;
}

struct CropDifference final {
  int maximum_channel_error = 0;
  qsizetype changed_pixels = 0;
};

CropDifference compare_crop(const QImage& full, const QImage& cropped,
                            const QRect& crop) {
  require(!full.isNull() && !cropped.isNull() && cropped.size() == crop.size(),
          "Compare the same physical crop from full and cropped renders.");
  CropDifference difference;
  for (int y = 0; y < crop.height(); ++y) {
    for (int x = 0; x < crop.width(); ++x) {
      const auto reference = full.pixelColor(x + crop.x(), y + crop.y());
      const auto candidate = cropped.pixelColor(x, y);
      const auto error = std::max({
          std::abs(reference.red() - candidate.red()),
          std::abs(reference.green() - candidate.green()),
          std::abs(reference.blue() - candidate.blue()),
          std::abs(reference.alpha() - candidate.alpha())});
      difference.maximum_channel_error =
          std::max(difference.maximum_channel_error, error);
      if (error > 1) ++difference.changed_pixels;
    }
  }
  return difference;
}

CropDifference verify_style_basis(const QImage& source, const QRect& crop,
                                  const LayerStyleStack& stack,
                                  const char* label) {
  auto layer = make_authored_paint_layer(source.size());
  layer.insert(QStringLiteral("layerStyleStack"), encode_stack(stack));
  const DesktopSourceGeometry full_geometry{source.size(), source.rect()};
  const DesktopSourceGeometry crop_geometry{source.size(), crop};
  const auto full = render(source, source, full_geometry, layer);
  const auto cropped_source = source.copy(crop);
  const auto cropped = render(cropped_source, source, crop_geometry, layer);
  const auto difference = compare_crop(full, cropped, crop);
  std::cout << label << " crop_basis max_channel_error="
            << difference.maximum_channel_error << " changed_pixels="
            << difference.changed_pixels << '\n';
  return difference;
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  try {
    const QSize size(96, 72);
    const QRect crop(16, 12, 64, 48);
    const auto source = make_source(size);

    const auto gradient_difference = verify_style_basis(
        source, crop, gradient_stack(), "gradient_overlay");
    const bool gradient_basis_preserved =
        gradient_difference.maximum_channel_error <= 1;

    // This independent probe checks style distances in document pixels. A
    // source-area paint tile retains its full source raster when a physical
    // crop is rendered; its stroke must therefore keep the same authored
    // width around the visible content boundary.
    const auto stroke_difference = verify_style_basis(
        source, crop, stroke_stack(), "stroke_distance");
    const bool stroke_basis_preserved =
        stroke_difference.maximum_channel_error <= 1;
    require(gradient_basis_preserved,
            "Gradient-overlay frame and authored style bounds must survive a physical source crop.");
    require(stroke_basis_preserved,
            "Stroke distance must retain its authored physical-pixel basis after a source crop.");
  } catch (const std::exception& error) {
    std::cerr << "Layer-style crop-basis regression: " << error.what() << '\n';
    return 1;
  }
  return 0;
}
