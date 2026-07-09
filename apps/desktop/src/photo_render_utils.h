#pragma once

#include "photo_editor/pipeline/color_adjustments.h"
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

enum class RenderBackendPreference {
  Auto,
  CpuOnly,
  GpuOnly,
};

struct RenderBackendTelemetry final {
  QString backend;
  QString fallback_reason;
};

struct GpuPipelineSupport final {
  bool supported = false;
  bool metal_aware_cpu_engine = false;
  QString fallback_reason;
};

struct RenderMaskOptions final {
  bool rebuild_editable_masks_at_output_resolution = false;
};

[[nodiscard]] bool preview_supported(const QString& file_path);
[[nodiscard]] bool render_backend_available(RenderBackendPreference backend_preference);
[[nodiscard]] QString render_backend_unavailable_reason(RenderBackendPreference backend_preference);
[[nodiscard]] GpuPipelineSupport evaluate_gpu_pipeline_support(
    const QImage& image,
    const RenderEditState& state,
    const photo_editor::pipeline::DecodedImageMetadata* metadata = nullptr,
    RenderMaskOptions mask_options = {},
    bool assume_metal_available = true);
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
    const photo_editor::pipeline::DecodedImageMetadata* metadata = nullptr,
    RenderBackendTelemetry* telemetry = nullptr,
    RenderMaskOptions mask_options = {},
    photo_editor::pipeline::RemovalQuality removal_quality =
        photo_editor::pipeline::RemovalQuality::Final);
[[nodiscard]] QImage render_output_image_with_backend(
    const QImage& image,
    const RenderEditState& state,
    const QColorSpace& target_color_space,
    QImage::Format output_format,
    RenderBackendPreference backend_preference,
    const photo_editor::pipeline::DecodedImageMetadata* metadata = nullptr,
    RenderBackendTelemetry* telemetry = nullptr,
    RenderMaskOptions mask_options = {},
    photo_editor::pipeline::RemovalQuality removal_quality =
        photo_editor::pipeline::RemovalQuality::Final);
void apply_color_adjustments(QImage* image, const RenderEditState& state);

}  // namespace photo_editor::desktop
