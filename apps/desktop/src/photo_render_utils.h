#pragma once

#include "local_mask_compiler.h"
#include "render_plan_store.h"
#include "flattened_output_renderer.h"
#include "decoder_provenance.h"
#include "photo_editor/pipeline/color_adjustments.h"
#include "photo_editor/pipeline/color_pipeline.h"
#include "photo_editor/pipeline/recipe_compiler.h"
#include "source_image_decode.h"

#include <QColorSpace>
#include <QImage>
#include <QRect>
#include <QRectF>
#include <QSize>
#include <QString>
#include <QStringList>

#include "edit_recipe_state.h"

namespace photo_editor::desktop {

using RenderEditState = EditRecipeState;

enum class RenderBackendPreference {
  Auto,
  CpuOnly,
  GpuOnly,
};

enum class RenderOutputPolicy {
  DisplayReferred,
  SceneLinearWorking,
};

struct RenderBackendTelemetry final {
  QString backend;
  QString fallback_reason;
  QString canonical_plan_cache_key;
  QString canonical_recipe_cache_key;
  QString canonical_plan_error;
  QString decode_preflight;
  QString decode_execution;
  QStringList completed_render_stages;
  int executor_submission_count = 0;
  bool geometry_executor_cache_hit = false;
};

struct GpuPipelineSupport final {
  bool supported = false;
  bool metal_aware_cpu_engine = false;
  QString fallback_reason;
};

struct RenderMaskOptions final {
  bool rebuild_editable_masks_at_output_resolution = false;
  QRectF camera_profile_source_area = QRectF(0.0, 0.0, 1.0, 1.0);
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
        photo_editor::pipeline::RemovalQuality::Final,
    RenderOutputPolicy output_policy =
        RenderOutputPolicy::DisplayReferred,
    const photo_editor::pipeline::RenderPlan*
        canonical_plan = nullptr,
    photo_editor::pipeline::CancellationToken cancellation = {});
[[nodiscard]] QImage render_output_image_with_compiled_state(
    const QImage& image,
    const RenderEditState& state,
    const photo_editor::pipeline::ColorAdjustmentState& compiled_state,
    const QColorSpace& target_color_space,
    QImage::Format output_format,
    const photo_editor::pipeline::RenderPlan& canonical_plan,
    photo_editor::pipeline::CancellationToken cancellation = {});
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
        photo_editor::pipeline::RemovalQuality::Final,
    RenderOutputPolicy output_policy =
        RenderOutputPolicy::DisplayReferred,
    const photo_editor::pipeline::RenderPlan*
        canonical_plan = nullptr,
    photo_editor::pipeline::CancellationToken cancellation = {});
[[nodiscard]] QImage render_output_image_for_display_target(
    const QImage& image,
    const RenderEditState& state,
    const QColorSpace& target_color_space,
    QImage::Format output_format,
    const photo_editor::pipeline::DisplayTarget& display_target,
    const photo_editor::pipeline::DecodedImageMetadata* metadata = nullptr,
    RenderBackendTelemetry* telemetry = nullptr,
    RenderMaskOptions mask_options = {},
    photo_editor::pipeline::RemovalQuality removal_quality =
        photo_editor::pipeline::RemovalQuality::Final,
    const photo_editor::pipeline::RenderPlan*
        canonical_plan = nullptr,
    photo_editor::pipeline::CancellationToken cancellation = {});
[[nodiscard]] QImage render_output_file(
    const QString& file_path,
    const QSize& expected_source_size,
    const QRect& source_crop,
    const RenderEditState& state,
    const QColorSpace& target_color_space,
    QImage::Format output_format,
    const photo_editor::pipeline::DecodedImageMetadata* expected_metadata,
    RenderBackendTelemetry* telemetry = nullptr,
    RenderMaskOptions mask_options = {},
    photo_editor::pipeline::RemovalQuality removal_quality =
        photo_editor::pipeline::RemovalQuality::Final,
    RenderOutputPolicy output_policy =
        RenderOutputPolicy::DisplayReferred,
    const photo_editor::pipeline::DisplayTarget*
        display_target = nullptr,
    const photo_editor::pipeline::RenderPlan*
        canonical_plan = nullptr,
    photo_editor::pipeline::CancellationToken cancellation = {});
}  // namespace photo_editor::desktop
