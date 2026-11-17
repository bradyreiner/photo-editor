#pragma once

#include "edit_recipe_state.h"
#include "editor_preview_texture.h"
#include "output_lut_publication_telemetry.h"
#include "working_image.h"

#include "photo_editor/pipeline/metal/metal_pipeline_backend.h"
#include "photo_editor/pipeline/render_tile_planner.h"

#include <QColorSpace>
#include <QImage>
#include <QRect>
#include <QSize>

#include <memory>
#include <utility>

namespace photo_editor::desktop {

struct NativeOutputSurfacePublication final {
  std::shared_ptr<const photo_editor::pipeline::metal::MetalOutputSurface> surface;
  OutputLutPublicationTelemetry telemetry;
};

struct WorkingPatchPublication final {
  QImage image;
  std::shared_ptr<const photo_editor::pipeline::metal::MetalOutputSurface> surface;
  WorkingImageQImageTelemetry telemetry;
};

[[nodiscard]] inline WorkingImageOutputTransform preview_working_output_transform(
    const photo_editor::pipeline::ColorPipelineConfig* color_pipeline,
    const photo_editor::pipeline::ResolvedIccProfileAsset* encoded_device_profile,
    bool generate_gamut_warning,
    photo_editor::pipeline::Aces2OutputLutQuality quality) {
  return WorkingImageOutputTransform{
      .color_pipeline = color_pipeline,
      .encoded_device_profile = encoded_device_profile,
      .target = photo_editor::pipeline::GamutWarningTarget::PreviewDisplay,
      .generate_proof_gamut_warning_mask = generate_gamut_warning,
      .aces2_lut_quality = quality,
  };
}

[[nodiscard]] inline bool state_supports_native_srgb_surface(
    const EditRecipeState& state,
    const QColorSpace& system_monitor_color_space) {
  return !state.soft_proof_enabled &&
         !state.preview_icc_profile_asset.configured() &&
         normalized_monitor_color_profile_name(state.monitor_color_profile).isEmpty() &&
         preview_color_space_for_name(normalized_output_color_space_name(
             preview_output_color_space_name_for_recipe(state))) ==
             QColorSpace(QColorSpace::SRgb) &&
         (!system_monitor_color_space.isValid() ||
          system_monitor_color_space == QColorSpace(QColorSpace::SRgb));
}

[[nodiscard]] inline NativeOutputSurfacePublication
publish_native_aces_srgb8_surface(
    const WorkingImage& working,
    const photo_editor::pipeline::ColorPipelineConfig& color_pipeline,
    photo_editor::pipeline::Aces2OutputLutQuality quality,
    photo_editor::pipeline::CancellationToken cancellation,
    bool defer_completion) {
  NativeOutputSurfacePublication publication;
#if defined(PHOTO_EDITOR_HAS_METAL_BACKEND) && PHOTO_EDITOR_HAS_METAL_BACKEND
  const auto application =
      photo_editor::pipeline::compile_aces2_output_lut_application(
          color_pipeline,
          photo_editor::pipeline::GamutWarningTarget::PreviewDisplay,
          quality);
  auto metal_telemetry = photo_editor::pipeline::ColorAdjustmentTelemetry{};
  auto& metal =
      photo_editor::pipeline::metal::shared_metal_pipeline_backend();
  const auto retained = std::dynamic_pointer_cast<const
      photo_editor::pipeline::metal::MetalWorkingSurface>(
          working.retainedBacking());
  publication.surface = retained != nullptr
                            ? metal.publish_aces2_output_lut_srgb8_surface(
                                  retained,
                                  application,
                                  &metal_telemetry,
                                  cancellation,
                                  defer_completion)
                            : metal.publish_aces2_output_lut_srgb8_surface(
                                  working.pixels,
                                  working.width,
                                  working.height,
                                  application,
                                  &metal_telemetry,
                                  cancellation,
                                  defer_completion);
  if (publication.surface != nullptr) {
    publication.telemetry = OutputLutPublicationTelemetry{
        .status = QStringLiteral("metal-native-srgb8"),
        .cpu_to_gpu_bytes = metal_telemetry.gpu_cpu_to_gpu_bytes,
        .gpu_to_cpu_bytes = metal_telemetry.gpu_gpu_to_cpu_bytes,
        .command_buffer_count = metal_telemetry.gpu_command_buffer_count,
        .kernel_dispatch_count = metal_telemetry.gpu_kernel_dispatch_count,
    };
  }
#else
  static_cast<void>(working);
  static_cast<void>(color_pipeline);
  static_cast<void>(quality);
  static_cast<void>(cancellation);
  static_cast<void>(defer_completion);
#endif
  return publication;
}

[[nodiscard]] inline NativeOutputSurfacePublication try_publish_native_working_patch(
    const WorkingImage& working,
    const QColorSpace& target_color_space,
    QImage::Format output_format,
    const WorkingImageOutputTransform& output_transform,
    bool allow_native_srgb_surface,
    bool defer_completion,
    photo_editor::pipeline::CancellationToken cancellation) {
  if (!allow_native_srgb_surface || output_transform.color_pipeline == nullptr ||
      output_transform.encoded_device_profile != nullptr ||
      target_color_space != QColorSpace(QColorSpace::SRgb) ||
      output_format != QImage::Format_ARGB32) {
    return {};
  }
  return publish_native_aces_srgb8_surface(
      working,
      *output_transform.color_pipeline,
      output_transform.aces2_lut_quality,
      cancellation,
      defer_completion);
}

[[nodiscard]] inline NativeOutputSurfacePublication
try_publish_native_working_subrect(
    const WorkingImage& working,
    const QRect& source_rect,
    const QColorSpace& target_color_space,
    QImage::Format output_format,
    const WorkingImageOutputTransform& output_transform,
    bool allow_native_srgb_surface,
    photo_editor::pipeline::CancellationToken cancellation) {
  NativeOutputSurfacePublication publication;
#if defined(PHOTO_EDITOR_HAS_METAL_BACKEND) && PHOTO_EDITOR_HAS_METAL_BACKEND
  if (!allow_native_srgb_surface || source_rect.isEmpty() ||
      output_transform.color_pipeline == nullptr ||
      output_transform.encoded_device_profile != nullptr ||
      target_color_space != QColorSpace(QColorSpace::SRgb) ||
      output_format != QImage::Format_ARGB32 ||
      source_rect != source_rect.intersected(
                         QRect(0, 0, working.width, working.height))) {
    return publication;
  }
  const auto retained = std::dynamic_pointer_cast<const
      photo_editor::pipeline::metal::MetalWorkingSurface>(
          working.retainedBacking());
  if (retained == nullptr) return publication;
  const auto application =
      photo_editor::pipeline::compile_aces2_output_lut_application(
          *output_transform.color_pipeline,
          photo_editor::pipeline::GamutWarningTarget::PreviewDisplay,
          output_transform.aces2_lut_quality);
  auto metal_telemetry = photo_editor::pipeline::ColorAdjustmentTelemetry{};
  auto& metal = photo_editor::pipeline::metal::shared_metal_pipeline_backend();
  publication.surface = metal.publish_aces2_output_lut_srgb8_surface(
      retained, source_rect.x(), source_rect.y(), source_rect.width(),
      source_rect.height(), application, &metal_telemetry, cancellation, true);
  if (publication.surface != nullptr) {
    publication.telemetry = OutputLutPublicationTelemetry{
        .status = QStringLiteral("metal-native-srgb8-subrect"),
        .cpu_to_gpu_bytes = metal_telemetry.gpu_cpu_to_gpu_bytes,
        .gpu_to_cpu_bytes = metal_telemetry.gpu_gpu_to_cpu_bytes,
        .command_buffer_count = metal_telemetry.gpu_command_buffer_count,
        .kernel_dispatch_count = metal_telemetry.gpu_kernel_dispatch_count,
    };
  }
#else
  static_cast<void>(working);
  static_cast<void>(source_rect);
  static_cast<void>(target_color_space);
  static_cast<void>(output_format);
  static_cast<void>(output_transform);
  static_cast<void>(allow_native_srgb_surface);
  static_cast<void>(cancellation);
#endif
  return publication;
}

[[nodiscard]] inline WorkingPatchPublication publish_working_patch(
    const WorkingImage& working,
    const QColorSpace& target_color_space,
    QImage::Format output_format,
    const QString& working_profile_name,
    const WorkingImageOutputTransform& output_transform,
    bool allow_native_srgb_surface,
    bool defer_completion,
    photo_editor::pipeline::CancellationToken cancellation) {
  WorkingPatchPublication publication;
  auto native = try_publish_native_working_patch(
      working,
      target_color_space,
      output_format,
      output_transform,
      allow_native_srgb_surface,
      defer_completion,
      cancellation);
  publication.surface = std::move(native.surface);
  publication.telemetry.output_lut = std::move(native.telemetry);
  if (publication.surface != nullptr) {
    return publication;
  }
  publication.image = qimage_from_working_image(
      working,
      target_color_space.isValid() ? target_color_space : QColorSpace{},
      output_format,
      working_profile_name,
      cancellation,
      &publication.telemetry,
      output_transform);
  return publication;
}

template <typename Result>
[[nodiscard]] Result publish_full_working_image(
    const WorkingImage& working,
    const QColorSpace& target_color_space,
    QImage::Format output_format,
    const QString& working_profile_name,
    const WorkingImageOutputTransform& output_transform,
    bool allow_native_srgb_surface,
    photo_editor::pipeline::CancellationToken cancellation) {
  QElapsedTimer timer;
  timer.start();
  auto publication = publish_working_patch(
      working, target_color_space, output_format, working_profile_name,
      output_transform, allow_native_srgb_surface, true, cancellation);
  if (publication.image.isNull() && publication.surface == nullptr) {
    Result result;
    if constexpr (requires(Result& value) { value.failure_reason = QString{}; }) {
      result.failure_reason = publication.telemetry.failure_reason;
    }
    return result;
  }
  Result result;
  result.image = std::move(publication.image);
  result.output_stage_elapsed_ms = timer.elapsed();
  result.output_linear_pack_elapsed_ms = publication.telemetry.linear_pack_ms;
  result.output_color_convert_elapsed_ms = publication.telemetry.color_convert_ms;
  result.output_lut = std::move(publication.telemetry.output_lut);
  result.proof_gamut_warning_mask = std::move(publication.telemetry.gamut_warning_mask);
  if (publication.surface != nullptr) {
    result.updated_rect = QRect(0, 0, working.width, working.height);
    result.pixel_count = static_cast<qint64>(working.width) *
                         static_cast<qint64>(working.height);
    result.patch_surface = std::move(publication.surface);
  }
  return result;
}

template <typename Result>
[[nodiscard]] Result publish_native_tiled_working_patch(
    const WorkingImage& working,
    const std::vector<photo_editor::pipeline::RenderTile>& tiles,
    const QRect& patch_rect,
    const QColorSpace& target_color_space,
    QImage::Format output_format,
    const WorkingImageOutputTransform& output_transform,
    bool allow_native_srgb_surface,
    photo_editor::pipeline::CancellationToken cancellation) {
  if (!allow_native_srgb_surface || patch_rect.isEmpty()) {
    return {};
  }
  QElapsedTimer timer;
  timer.start();
  auto publication = try_publish_native_working_subrect(
      working, patch_rect, target_color_space, output_format,
      output_transform, true, cancellation);
  if (publication.surface == nullptr && !working.isNull()) {
    publication = try_publish_native_working_patch(
        crop_working_image(working, patch_rect), target_color_space,
        output_format, output_transform, true, true, cancellation);
  }
  if (publication.surface == nullptr) {
    return {};
  }
  Result result;
  result.tile_count = static_cast<int>(tiles.size());
  result.updated_rect = patch_rect;
  result.patch_surface = std::move(publication.surface);
  result.texture_patch_base_state_hit = true;
  result.output_stage_elapsed_ms = timer.elapsed();
  result.output_lut = std::move(publication.telemetry);
  for (const auto& tile : tiles) {
    if (result.first_tile_priority.isEmpty()) {
      switch (tile.priority) {
        case photo_editor::pipeline::RenderTilePriority::Visible:
          result.first_tile_priority = QStringLiteral("visible");
          break;
        case photo_editor::pipeline::RenderTilePriority::Adjacent:
          result.first_tile_priority = QStringLiteral("adjacent");
          break;
        case photo_editor::pipeline::RenderTilePriority::Background:
          result.first_tile_priority = QStringLiteral("background");
          break;
      }
    }
    switch (tile.priority) {
      case photo_editor::pipeline::RenderTilePriority::Visible:
        ++result.visible_tile_count;
        break;
      case photo_editor::pipeline::RenderTilePriority::Adjacent:
        ++result.adjacent_tile_count;
        break;
      case photo_editor::pipeline::RenderTilePriority::Background:
        ++result.background_tile_count;
        break;
    }
    result.pixel_count += static_cast<qint64>(tile.output_rect.width) *
                          static_cast<qint64>(tile.output_rect.height);
  }
  return result;
}

[[nodiscard]] inline bool native_surface_matches_patch(
    const std::shared_ptr<const photo_editor::pipeline::metal::MetalOutputSurface>& surface,
    const QRect& patch_rect,
    const QSize& destination_size,
    const QSize& result_size) {
#if defined(PHOTO_EDITOR_HAS_METAL_BACKEND) && PHOTO_EDITOR_HAS_METAL_BACKEND
  return surface != nullptr && patch_rect.isValid() && !patch_rect.isEmpty() &&
         QSize(surface->width(), surface->height()) == patch_rect.size() &&
         destination_size == result_size;
#else
  Q_UNUSED(surface)
  Q_UNUSED(patch_rect)
  Q_UNUSED(destination_size)
  Q_UNUSED(result_size)
  return false;
#endif
}

[[nodiscard]] inline bool upload_native_surface_patch_async(
    EditorPreviewTexture* texture,
    const std::shared_ptr<const photo_editor::pipeline::metal::MetalOutputSurface>& surface,
    const QRect& destination_rect,
    EditorPreviewTexture::NativePatchCommitGuard commit_guard,
    EditorPreviewTexture::NativePatchUploadCompletion completion) {
#if defined(PHOTO_EDITOR_HAS_METAL_BACKEND) && PHOTO_EDITOR_HAS_METAL_BACKEND
  return texture != nullptr && surface != nullptr &&
         texture->uploadNativePatchAsync(
             *surface,
             destination_rect,
             [surface, commit_guard = std::move(commit_guard)]() {
               return surface->producer_completed_successfully() &&
                      (!commit_guard || commit_guard());
             },
             [completion = std::move(completion)](bool published) {
               if (completion) completion(published);
             });
#else
  Q_UNUSED(texture)
  Q_UNUSED(surface)
  Q_UNUSED(destination_rect)
  Q_UNUSED(commit_guard)
  Q_UNUSED(completion)
  return false;
#endif
}

[[nodiscard]] inline bool adopt_native_full_surface_async(
    EditorPreviewTexture* texture,
    const std::shared_ptr<const photo_editor::pipeline::metal::MetalOutputSurface>& surface,
    const QSize& result_size,
    EditorPreviewTexture::NativePatchCommitGuard commit_guard,
    EditorPreviewTexture::NativePatchUploadCompletion completion) {
#if defined(PHOTO_EDITOR_HAS_METAL_BACKEND) && PHOTO_EDITOR_HAS_METAL_BACKEND
  return texture != nullptr && surface != nullptr && result_size.isValid() &&
         QSize(surface->width(), surface->height()) == result_size &&
         texture->adoptNativeSurfaceAsync(
             *surface,
             [surface, commit_guard = std::move(commit_guard)]() {
               return surface->producer_completed_successfully() &&
                      (!commit_guard || commit_guard());
             },
             [completion = std::move(completion)](bool published) {
               if (completion) completion(published);
             });
#else
  Q_UNUSED(texture)
  Q_UNUSED(surface)
  Q_UNUSED(result_size)
  Q_UNUSED(commit_guard)
  Q_UNUSED(completion)
  return false;
#endif
}

template <typename Rendered>
[[nodiscard]] bool rendered_has_image_or_patch(const Rendered& rendered) {
  return !rendered.image.isNull() || !rendered.patch_image.isNull() ||
         rendered.patch_surface != nullptr;
}

template <typename Telemetry, typename Rendered>
void copy_rendered_patch_publication(
    Telemetry* telemetry,
    const Rendered& rendered) {
  if (telemetry == nullptr) {
    return;
  }
  telemetry->texture_upload_image = rendered.patch_image;
  telemetry->texture_upload_surface = rendered.patch_surface;
  telemetry->partial_texture_upload_candidate =
      (!rendered.patch_image.isNull() || rendered.patch_surface != nullptr) &&
      rendered.updated_rect.isValid() && !rendered.updated_rect.isEmpty();
  telemetry->texture_patch_base_state_hit = rendered.texture_patch_base_state_hit;
}

}  // namespace photo_editor::desktop
