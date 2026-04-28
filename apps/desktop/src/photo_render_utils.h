#pragma once

#include "photo_editor/pipeline/color_pipeline.h"

#include <QColorSpace>
#include <QImage>
#include <QRect>
#include <QRectF>
#include <QSize>
#include <QString>

#include "edit_recipe_state.h"

namespace photo_editor::desktop {

using RenderEditState = EditRecipeState;

[[nodiscard]] bool preview_supported(const QString& file_path);
[[nodiscard]] bool decode_source_image(
    const QString& file_path,
    QImage* image,
    int max_edge = 0,
    const RenderEditState& state = {},
    photo_editor::pipeline::DecodedImageMetadata* metadata = nullptr);
[[nodiscard]] QSize source_image_size(const QString& file_path);
[[nodiscard]] double crop_aspect_ratio(const QString& preset);
[[nodiscard]] bool has_explicit_crop_rect(const RenderEditState& state);
[[nodiscard]] QRectF normalized_crop_rect_for_state(const QSize& size, const RenderEditState& state);
[[nodiscard]] QRect crop_rect_for_image(const QSize& size, const RenderEditState& state);
[[nodiscard]] QImage apply_crop(const QImage& image, const RenderEditState& state);
[[nodiscard]] QImage render_output_image(
    const QImage& image,
    const RenderEditState& state,
    const QColorSpace& target_color_space,
    QImage::Format output_format = QImage::Format_ARGB32,
    const photo_editor::pipeline::DecodedImageMetadata* metadata = nullptr);
void apply_color_adjustments(QImage* image, const RenderEditState& state);

}  // namespace photo_editor::desktop
