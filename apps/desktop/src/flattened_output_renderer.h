#pragma once

#include "edit_recipe_state.h"
#include "photo_editor/pipeline/color_adjustments.h"

#include <QColorSpace>
#include <QImage>
#include <QString>

namespace photo_editor::desktop {

struct RenderBackendTelemetry;

[[nodiscard]] QImage render_editable_base_pixels(
    const QString& source_path, EditRecipeState state, QString* error = nullptr);

[[nodiscard]] QImage render_photo_layer_snapshot(
    const QImage& source_image,
    EditRecipeState state,
    const QString& layer_id,
    QString* error = nullptr);

[[nodiscard]] QImage render_flattened_output_file(
    const QString& source_asset_id,
    const QString& source_path,
    const EditRecipeState& state,
    const QColorSpace& target_color_space,
    QImage::Format output_format,
    RenderBackendTelemetry* telemetry = nullptr,
    photo_editor::pipeline::CancellationToken cancellation = {});

}  // namespace photo_editor::desktop
