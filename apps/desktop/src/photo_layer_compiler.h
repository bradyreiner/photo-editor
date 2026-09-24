#pragma once

#include "local_mask_compiler.h"

#include "photo_editor/pipeline/color_adjustments.h"
#include "photo_editor/pipeline/color_pipeline.h"

#include <QSize>
#include <QVariantList>

#include <span>
#include <vector>

namespace photo_editor::desktop {

enum class DesktopRenderPurpose;

inline QString compiled_photo_layer_source_error(
    const std::vector<photo_editor::pipeline::PhotoLayer>& layers) {
  for (const auto& layer : layers) {
    if (layer.enabled && layer.opacity > 0.0 && !layer.content_error.empty())
      return QStringLiteral("Could not render layer '%1': %2")
          .arg(QString::fromStdString(layer.name), QString::fromStdString(layer.content_error));
    if (layer.enabled && layer.opacity > 0.0 && !layer.effect_error.empty())
      return QString::fromStdString(layer.effect_error);
    if (layer.kind == "text" && layer.enabled && layer.opacity > 0.0 &&
        (layer.pixels.empty() || layer.pixel_width <= 0 || layer.pixel_height <= 0))
      return QStringLiteral("Could not render text layer '%1'. Text content could not be prepared.")
          .arg(QString::fromStdString(layer.name));
    if (layer.kind == "source" && layer.enabled && layer.opacity > 0.0 &&
        (layer.pixels.empty() || layer.pixel_width <= 0 || layer.pixel_height <= 0)) {
      return QStringLiteral("Could not decode linked image layer '%1' (%2). Restore the source file or disable the layer.")
          .arg(QString::fromStdString(layer.name), QString::fromStdString(layer.id));
    }
  }
  return {};
}

[[nodiscard]] std::vector<photo_editor::pipeline::PhotoLayer>
compile_pipeline_photo_layers(
    const QVariantList& layers,
    const QVariantList& masks,
    LocalMaskRasterPolicy raster_policy,
    const QSize& output_size,
    std::span<const photo_editor::pipeline::ColorPixel> source_pixels,
    const photo_editor::pipeline::ColorPipelineConfig& color_pipeline,
    DesktopRenderPurpose purpose);

}  // namespace photo_editor::desktop
