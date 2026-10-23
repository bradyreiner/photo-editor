#include "photo_paint_surface.h"
#include "photo_text_rasterizer.h"
#include "photo_render_utils.h"
#include "openexr_source_color.h"
#include "brush_matte_rebuild.h"

#include "photo_layer_compiler.h"
#include "photo_layer_effect_codec.h"
#include "photo_extended_effect_bridge.h"
#include "layer_selection_mask_cache.h"
#include "photo_layer_source.h"
#include "linked_image_source.h"
#include "desktop_pipeline_state_compiler.h"
#include "thumbnail_canonical_renderer.h"

#include "color_profile_asset_bridge.h"
#include "app_bootstrap.h"
#include "decoded_image_cache.h"
#include "edit_recipe_pipeline_bridge.h"
#include "edit_recipe_v13_adapter.h"
#include "edit_render_policy.h"
#include "mask_alpha_provider.h"
#include "output_profile_names.h"
#include "preview_output_color_space.h"
#include "render_executor_service.h"
#include "render_plan_store.h"
#include "photo_editor/io/openexr_io.h"
#include "photo_editor/pipeline/color_adjustments.h"
#include "photo_editor/pipeline/color_pipeline.h"
#include "photo_editor/pipeline/render_executor.h"
#include "raw_image_decoder.h"
#include "raw_decode_policy.h"
#include "working_image.h"
#if defined(PHOTO_EDITOR_HAS_METAL_BACKEND) && PHOTO_EDITOR_HAS_METAL_BACKEND
#include "photo_editor/pipeline/metal/metal_pipeline_backend.h"
#endif
#include <QColor>
#include <QColorSpace>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHash>
#include <QImageReader>
#include <QJsonDocument>
#include <QPainter>
#include <QPolygonF>
#include <QSet>
#include <QStringList>
#include <QTransform>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <limits>
#include <initializer_list>
#include <ranges>
#include <span>
#include <utility>
namespace photo_editor::desktop {
namespace {

QImage perspective_warp_text_layer(const QImage& source, double horizontal, double vertical) {
  if (source.isNull() || (std::abs(horizontal) < 1e-6 && std::abs(vertical) < 1e-6))
    return source;
  const auto width = static_cast<double>(source.width());
  const auto height = static_cast<double>(source.height());
  const auto horizontal_offset = width * 0.32 * qBound(-100.0, horizontal, 100.0) / 100.0;
  const auto vertical_offset = height * 0.32 * qBound(-100.0, vertical, 100.0) / 100.0;
  const QPolygonF source_quad{{0.0, 0.0}, {width, 0.0}, {width, height}, {0.0, height}};
  QPolygonF target_quad{
      {horizontal_offset, vertical_offset},
      {width - horizontal_offset, -vertical_offset},
      {width + horizontal_offset, height - vertical_offset},
      {-horizontal_offset, height + vertical_offset}};
  const auto bounds = target_quad.boundingRect();
  if (bounds.width() <= 1e-6 || bounds.height() <= 1e-6) return source;
  for (auto& point : target_quad) {
    point.setX((point.x() - bounds.left()) * width / bounds.width());
    point.setY((point.y() - bounds.top()) * height / bounds.height());
  }
  QTransform warp;
  if (!QTransform::quadToQuad(source_quad, target_quad, warp)) return source;
  QImage result(source.size(), source.format());
  if (result.isNull()) return source;
  if (source.colorSpace().isValid()) result.setColorSpace(source.colorSpace());
  result.setDotsPerMeterX(source.dotsPerMeterX());
  result.setDotsPerMeterY(source.dotsPerMeterY());
  result.fill(Qt::transparent);
  QPainter painter(&result);
  painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
  painter.setTransform(warp);
  painter.drawImage(QPointF(0.0, 0.0), source);
  return result;
}

bool apply_export_color_pipeline_with_executor(
    std::vector<photo_editor::pipeline::ColorPixel>* pixels,
    int width,
    int height,
    photo_editor::pipeline::ColorAdjustmentState&
        pipeline_state,
    photo_editor::pipeline::RemovalQuality removal_quality,
    const photo_editor::pipeline::RenderPlan* canonical_plan,
    bool execute_photographic_output,
    RenderBackendTelemetry* backend_telemetry,
    photo_editor::pipeline::ColorAdjustmentTelemetry*
        engine_telemetry,
    bool* photographic_output_executed,
    bool* publish_executed,
    photo_editor::pipeline::RenderStageCallback
        deferred_decode_stage = {},
    photo_editor::pipeline::CancellationToken cancellation = {}) {
  if (cancellation.cancelled()) {
    return false;
  }
  if (photographic_output_executed != nullptr) {
    *photographic_output_executed = false;
  }
  if (publish_executed != nullptr) {
    *publish_executed = false;
  }
  if (canonical_plan == nullptr) {
    if (deferred_decode_stage) {
      return false;
    }
    const auto applied = photo_editor::pipeline::apply_color_adjustments(
        pixels,
        width,
        height,
        pipeline_state,
        removal_quality,
        cancellation,
        engine_telemetry);
    return applied && !cancellation.cancelled();
  }

  const auto proof_stage =
      std::ranges::find_if(
          canonical_plan->stages,
          [](const auto& stage) {
            return stage.kind ==
                   photo_editor::pipeline::
                       RenderStageKind::Proof;
          });
  const auto device_stage =
      std::ranges::find_if(
          canonical_plan->stages,
          [](const auto& stage) {
            return stage.kind ==
                   photo_editor::pipeline::
                       RenderStageKind::DeviceTransform;
          });
  const auto has_proof_stage =
      proof_stage != canonical_plan->stages.end();
  const auto has_device_stage =
      device_stage != canonical_plan->stages.end();
  auto output_carrier =
      pipeline_state.color_pipeline
          .export_output_profile.color_space;
  output_carrier.transfer_function =
      photo_editor::pipeline::TransferFunction::Linear;
  output_carrier.name =
      "Linear " +
      pipeline_state.color_pipeline
          .export_output_profile.profile_name;
  if (has_device_stage) {
    output_carrier =
        photo_editor::pipeline::
            make_linear_rec2020_color_space();
  }
  auto execution_request =
      photo_editor::pipeline::RenderExecutionRequest{
          .plan = *canonical_plan,
          .backend_version =
              "cpu-reference:color-adjustments-v1",
          .target_identity =
              "export-target:" +
              canonical_plan->plan_cache_key,
          .profile_identity =
              canonical_plan->device_icc_profile.has_value()
                  ? canonical_plan->device_icc_profile->value
                  : "export-device:none",
          .source_color_space_identity =
              pipeline_state.color_pipeline
                  .input_profile.profile_name,
          .working_color_space_identity =
              pipeline_state.color_pipeline
                  .working_profile.profile_name,
          .rendering_target_color_space_identity =
              output_carrier.name,
          .proof_color_space_identity =
              has_proof_stage
                  ? output_carrier.name +
                        " proof simulation"
                  : pipeline_state.color_pipeline
                        .proof_output_profile.profile_name,
          .device_color_space_identity =
              has_device_stage
                  ? (!pipeline_state.color_pipeline
                              .export_icc_profile_asset
                              .reference.display_name.empty()
                         ? pipeline_state.color_pipeline
                               .export_icc_profile_asset
                               .reference.display_name
                         : pipeline_state.color_pipeline
                               .export_icc_profile_asset
                               .reference.asset_id)
                  : pipeline_state.color_pipeline
                        .export_output_profile.profile_name,
          .device_transfer_identity =
              photo_editor::pipeline::display_eotf_name(
                  pipeline_state.color_pipeline
                      .export_display_target.eotf),
          .output_width =
              static_cast<std::uint32_t>(width),
          .output_height =
              static_cast<std::uint32_t>(height),
          .source_area_x =
              pipeline_state.input_transform.source_area_x,
          .source_area_y =
              pipeline_state.input_transform.source_area_y,
          .source_area_width =
              pipeline_state.input_transform.source_area_width,
          .source_area_height =
              pipeline_state.input_transform.source_area_height,
          .bypass_cache = true,
      };
  if (qEnvironmentVariableIntValue("PHOTO_EDITOR_TRACE_RENDER_STAGES") == 1) {
    execution_request.stage_observer = [](const auto& stage, const auto& surface) {
      if (surface.pixels.empty()) return;
      const auto index = std::min(surface.pixels.size() - 1,
          static_cast<std::size_t>(surface.descriptor.height * 0.4) * surface.descriptor.width +
          static_cast<std::size_t>(surface.descriptor.width * 0.35));
      const auto pixel = surface.pixels[index];
      const auto name = photo_editor::pipeline::render_stage_kind_name(stage.kind);
      std::fprintf(stderr, "render-stage %.*s %.9g %.9g %.9g\n", static_cast<int>(name.size()), name.data(),
          static_cast<double>(pixel.red), static_cast<double>(pixel.green), static_cast<double>(pixel.blue));
    };
  }
  if (!deferred_decode_stage) {
    auto decoded_input =
        photo_editor::pipeline::RenderSurface{
            .descriptor =
                photo_editor::pipeline::
                    render_stage_output_descriptor(
                        execution_request,
                        photo_editor::pipeline::
                            RenderStageKind::Decode),
            .pixels = std::move(*pixels),
            .identity =
                "surface:decode:" +
                canonical_plan->recipe_cache_key,
        };
    deferred_decode_stage =
        [decoded_input =
             std::move(decoded_input)](
            const photo_editor::pipeline::
                RenderStageInvocation& invocation,
            std::optional<
                photo_editor::pipeline::RenderSurface>
                input) mutable {
          if (input.has_value()) {
            return photo_editor::pipeline::
                RenderStageExecution{
                    .failure_reason =
                        "export Decode received an unexpected input surface",
                };
          }
          auto output = std::move(decoded_input);
          output.descriptor =
              invocation.expected_output;
          return photo_editor::pipeline::
              RenderStageExecution{
                  .output = std::move(output),
                  .backend = "export-decoder",
              };
        };
  }
  auto output_config =
      pipeline_state.color_pipeline;
  if (has_device_stage) {
    output_config.export_output_profile.color_space =
        output_carrier;
    output_config.export_output_profile.profile_name =
        output_carrier.name;
    output_config.export_display_target.target_name =
        "ACES 2 linear Rec.2020 ICC interchange";
    output_config.export_display_target.color_space =
        output_carrier;
  }
  const auto carrier_payload =
      photo_editor::pipeline::
          builtin_rgb_icc_profile_payload(
              output_carrier);
  if ((has_proof_stage || has_device_stage) &&
      carrier_payload.empty()) {
    return false;
  }
  auto proof_transform =
      std::optional<
          photo_editor::pipeline::
              CompiledIccTransform>{};
  if (has_proof_stage) {
    const auto& proof_asset =
        pipeline_state.color_pipeline
            .proof_icc_profile_asset;
    auto proof_payload =
        std::vector<std::byte>{};
    if (proof_asset.status !=
        photo_editor::pipeline::
            IccProfileAssetStatus::Unconfigured) {
      if (!proof_asset.ready()) {
        return false;
      }
      proof_payload.assign(
          proof_asset.payload_bytes().begin(),
          proof_asset.payload_bytes().end());
    } else {
      proof_payload =
          photo_editor::pipeline::
              builtin_rgb_icc_profile_payload(
                  pipeline_state.color_pipeline
                      .proof_output_profile.color_space);
    }
    proof_transform =
        photo_editor::pipeline::
            compile_icc_proofing_transform(
                carrier_payload,
                proof_payload,
                carrier_payload,
                photo_editor::pipeline::
                    IccProofTransformOptions{
                        .rendering_intent =
                            icc_rendering_intent(
                                pipeline_state.color_pipeline
                                    .render_intent),
                        .proofing_intent =
                            pipeline_state.color_pipeline
                                    .proof_simulate_paper_white
                                ? photo_editor::pipeline::
                                      IccRenderingIntent::
                                          AbsoluteColorimetric
                                : icc_rendering_intent(
                                      pipeline_state.color_pipeline
                                          .proof_render_intent),
                        .black_point_compensation =
                            pipeline_state.color_pipeline
                                .black_point_compensation_enabled &&
                            !pipeline_state.color_pipeline
                                 .proof_simulate_paper_white &&
                            !pipeline_state.color_pipeline
                                 .proof_simulate_ink_black,
                        .gamut_check = false,
                        .pixel_format =
                            photo_editor::pipeline::
                                IccPixelFormat::RgbaFloat32,
                        .target =
                            "export-proof:linear-carrier",
                    });
  }
  auto device_transform =
      std::optional<
          photo_editor::pipeline::
              CompiledIccTransform>{};
  if (has_device_stage) {
    const auto& device_asset =
        pipeline_state.color_pipeline
            .export_icc_profile_asset;
    if (!device_asset.ready()) {
      return false;
    }
    device_transform =
        photo_editor::pipeline::
            compile_icc_transform(
                carrier_payload,
                device_asset.payload_bytes(),
                photo_editor::pipeline::
                    IccTransformOptions{
                        .rendering_intent =
                            icc_rendering_intent(
                                pipeline_state.color_pipeline
                                    .render_intent),
                        .black_point_compensation =
                            pipeline_state.color_pipeline
                                .black_point_compensation_enabled,
                        .pixel_format =
                            photo_editor::pipeline::
                                IccPixelFormat::RgbaFloat32,
                        .target =
                            "export-device:linear-carrier",
                    });
  }
  auto full_result =
      photo_editor::pipeline::execute_color_plan(
          &export_render_executor(),
          execution_request,
          std::move(deferred_decode_stage),
          pipeline_state,
          photo_editor::pipeline::
              ColorPlanExecutionOptions{
                  .removal_quality =
                      removal_quality,
                  .photographic_output_config =
                      &output_config,
                  .gamut_warning_target =
                      photo_editor::pipeline::
                          GamutWarningTarget::
                              ExportOutput,
                  .output_lut_quality =
                      removal_quality ==
                              photo_editor::pipeline::
                                  RemovalQuality::Final
                          ? photo_editor::pipeline::
                                Aces2OutputLutQuality::Final
                          : photo_editor::pipeline::
                                Aces2OutputLutQuality::
                                    Interactive,
                  .proof_transform =
                      proof_transform.has_value()
                          ? &*proof_transform
                          : nullptr,
                  .device_transform =
                      device_transform.has_value()
                          ? &*device_transform
                          : nullptr,
                  .publish_display_target =
                      &pipeline_state.color_pipeline
                           .export_display_target,
                  .stop_before_photographic_output =
                      !execute_photographic_output,
              },
          cancellation,
          engine_telemetry);
  if (!full_result.ok() ||
      !full_result.output.has_value()) {
    if (backend_telemetry != nullptr) {
      const auto stage = QString::fromStdString(
          full_result.failure_stage);
      const auto reason = QString::fromStdString(
          full_result.failure_reason);
      backend_telemetry->fallback_reason =
          stage.isEmpty()
              ? reason
              : QStringLiteral("%1: %2").arg(stage, reason);
    }
    return false;
  }
  if (backend_telemetry != nullptr) {
    backend_telemetry->executor_submission_count =
        1;
  }
  auto completed_stages =
      std::move(full_result.completed_stages);
  const auto completed_kind =
      [&](photo_editor::pipeline::
              RenderStageKind kind) {
        return std::ranges::any_of(
            completed_stages,
            [&](const auto& completed) {
              return completed.kind == kind;
            });
      };
  const auto output_stage_executed =
      completed_kind(
          photo_editor::pipeline::
              RenderStageKind::
                  PhotographicOutput);
  if (photographic_output_executed != nullptr) {
    *photographic_output_executed =
        output_stage_executed;
  }
  const auto publish_stage_executed =
      completed_kind(
          photo_editor::pipeline::
              RenderStageKind::Publish);
  if (publish_executed != nullptr) {
    *publish_executed =
        publish_stage_executed;
  }
  if (backend_telemetry != nullptr) {
    backend_telemetry->completed_render_stages.clear();
    for (const auto& completed : completed_stages) {
      backend_telemetry->completed_render_stages.push_back(
          QString::fromUtf8(
              photo_editor::pipeline::
                  render_stage_kind_name(completed.kind)
                      .data(),
              static_cast<qsizetype>(
                  photo_editor::pipeline::
                      render_stage_kind_name(completed.kind)
                          .size())));
    }
    backend_telemetry->geometry_executor_cache_hit =
        !completed_stages.empty() &&
        completed_stages.front().cache_hit;
  }
  *pixels =
      std::move(full_result.output->pixels);
  if (output_stage_executed ||
      execute_photographic_output) {
    return true;
  }
  if (photo_editor::pipeline::
          photographic_rendering_model_uses_aces2(
              pipeline_state.color_pipeline
                  .photographic_rendering_model) &&
      pipeline_state.color_pipeline.render_intent ==
          photo_editor::pipeline::RenderIntent::Perceptual) {
    return true;
  }
  return photo_editor::pipeline::
      apply_color_compatibility_output_stage(
          pixels,
          width,
          height,
          pipeline_state,
          cancellation,
          engine_telemetry);
}

[[nodiscard]] bool openexr_image_supported(const QString& file_path) {
  return QFileInfo(file_path).suffix().compare(
             QStringLiteral("exr"),
             Qt::CaseInsensitive) == 0 &&
         photo_editor::io::openexr_backend_available();
}

[[nodiscard]] QImage decode_openexr_image(
    const QString& file_path,
    int max_edge,
    photo_editor::pipeline::CancellationToken cancellation) {
  const auto inspection = photo_editor::io::inspect_openexr(
      file_path.toStdString());
  if (!inspection.valid || inspection.width <= 0 || inspection.height <= 0 ||
      inspection.non_finite_sample_count != 0U ||
      inspection.rgba_pixels.size() !=
          static_cast<std::size_t>(inspection.width) *
              static_cast<std::size_t>(inspection.height) * 4U) {
    return {};
  }
  const auto source_transform = openexr_to_linear_srgb_matrix(inspection.chromaticities);
  if (!source_transform || cancellation.cancelled()) return {};
  QImage decoded(
      inspection.width,
      inspection.height,
      QImage::Format_RGBA32FPx4);
  if (decoded.isNull()) {
    return {};
  }
  const auto source_row_bytes = static_cast<std::size_t>(inspection.width) *
                                sizeof(float) * 4U;
  for (int y = 0; y < inspection.height; ++y) {
    if (cancellation.cancelled()) return {};
    std::memcpy(
        decoded.scanLine(y),
        inspection.rgba_pixels.data() +
            static_cast<std::size_t>(y) *
                static_cast<std::size_t>(inspection.width) * 4U,
        source_row_bytes);
    auto* row = reinterpret_cast<float*>(decoded.scanLine(y));
    for (int x = 0; x < inspection.width; ++x) {
      const auto offset = static_cast<std::size_t>(x) * 4;
      const auto rgb = photo_editor::pipeline::multiply_matrix3_vector(*source_transform,
          {row[offset], row[offset + 1], row[offset + 2]});
      for (std::size_t channel = 0; channel < 3; ++channel) {
        if (!std::isfinite(rgb[channel]) || std::abs(rgb[channel]) > std::numeric_limits<float>::max()) return {};
        row[offset + channel] = static_cast<float>(rgb[channel]);
      }
    }
  }
  decoded.setColorSpace(linear_srgb_color_space());
  if (max_edge > 0 &&
      (decoded.width() > max_edge || decoded.height() > max_edge)) {
    decoded = decoded.scaled(
        max_edge,
        max_edge,
        Qt::KeepAspectRatio,
        Qt::SmoothTransformation);
  }
  return decoded;
}

QImage normalize_preview_color_space(QImage image);
QColorSpace preview_target_color_space() {
  return preview_color_space();
}


bool has_extended_range_pixels(const std::vector<photo_editor::pipeline::ColorPixel>& pixels) {
  constexpr auto kTolerance = 1.0e-6F;
  return std::any_of(pixels.cbegin(), pixels.cend(), [](const auto& pixel) {
    return pixel.red > 1.0F + kTolerance || pixel.green > 1.0F + kTolerance ||
           pixel.blue > 1.0F + kTolerance || pixel.red < -kTolerance ||
           pixel.green < -kTolerance || pixel.blue < -kTolerance;
  });
}

QString unsupported_stage_reason(
    const QString& stage,
    const QString& detail) {
  return QStringLiteral(
             "unsupported_stage:%1:%2")
      .arg(stage, detail);
}

QString cpu_required_stage_reason(
    const RenderEditState& state) {
  if (!state.layers.isEmpty()) {
    return unsupported_stage_reason(
        QStringLiteral("Layers"),
        QStringLiteral("photo_layers"));
  }
  for (const auto& control :
       std::initializer_list<
           std::pair<QString, bool>>{
           {QStringLiteral("texture"),
            std::abs(state.texture) >
                1e-6},
           {QStringLiteral("clarity"),
            std::abs(state.clarity) >
                1e-6},
           {QStringLiteral("dehaze"),
            std::abs(state.dehaze) >
                1e-6},
           {QStringLiteral(
                "hdr_local_tone_mapping"),
            std::abs(
                state
                    .hdr_local_tone_mapping) >
                1e-6},
           {QStringLiteral("sharpening"),
            std::abs(state.sharpening) >
                    1e-6 ||
                std::abs(
                    state.sharpen_masking) >
                    1e-6},
           {QStringLiteral(
                "luminance_noise_reduction"),
            std::abs(
                state.luminance_nr) >
                1e-6},
           {QStringLiteral(
                "color_noise_reduction"),
            std::abs(state.color_nr) >
                1e-6},
       }) {
    if (control.second) {
      return unsupported_stage_reason(
          QStringLiteral("Detail"),
          control.first);
    }
  }
  for (const auto& control :
       std::initializer_list<
           std::pair<QString, bool>>{
           {QStringLiteral(
                "lens_distortion"),
            std::abs(
                state.lens_distortion) >
                1e-6},
           {QStringLiteral(
                "lens_vignette"),
            std::abs(
                state.lens_vignette) >
                1e-6},
           {QStringLiteral(
                "chromatic_aberration"),
            std::abs(
                state
                    .chromatic_aberration) >
                1e-6},
           {QStringLiteral(
                "perspective_vertical"),
            std::abs(
                state
                    .perspective_vertical) >
                1e-6},
           {QStringLiteral(
                "perspective_horizontal"),
            std::abs(
                state
                    .perspective_horizontal) >
                1e-6},
           {QStringLiteral(
                "transform_rotation"),
            std::abs(
                state
                    .transform_rotation) >
                1e-6},
           {QStringLiteral(
                "transform_scale"),
            std::abs(
                state.transform_scale -
                1.0) >
                1e-6},
           {QStringLiteral(
                "transform_skew_x"),
            std::abs(
                state.transform_skew_x) >
                1e-6},
           {QStringLiteral(
                "transform_skew_y"),
            std::abs(
                state.transform_skew_y) >
                1e-6},
           {QStringLiteral(
                "liquify_strength"),
            std::abs(
                state.liquify_strength) >
                1e-6},
       }) {
    if (control.second) {
      return unsupported_stage_reason(
          QStringLiteral("Geometry"),
          control.first);
    }
  }
  return {};
}

QString gpu_fallback_reason(
    const RenderEditState& state,
    const photo_editor::pipeline::ColorAdjustmentState& pipeline_state,
    const std::vector<photo_editor::pipeline::ColorPixel>& pixels,
    bool metal_available) {
#if defined(PHOTO_EDITOR_HAS_METAL_BACKEND) && PHOTO_EDITOR_HAS_METAL_BACKEND
  if (!metal_available) {
    return QStringLiteral("metal_unavailable");
  }
  // The compatibility 3-D LUT folds camera characterization into a sampled
  // cube. It is not numerically equivalent to the exact RAW path used once a
  // local mask is present. Keep RAW on the exact path until the accelerated
  // camera-prefix implementation has its own parity evidence.
  if (pipeline_state.input_transform.raw_input_transform_enabled &&
      pipeline_state.color_pipeline.camera_profile.valid) {
    return unsupported_stage_reason(QStringLiteral("CameraCharacterization"),
        QStringLiteral("camera_linear_lut_fidelity"));
  }
  if (!pipeline_state.local_mask_sets.empty()) {
    return unsupported_stage_reason(
        QStringLiteral("Layers"),
        QStringLiteral("local_mask_sets"));
  }
  if (!pipeline_state.retouch_masks.empty()) {
    return unsupported_stage_reason(
        QStringLiteral("Layers"),
        QStringLiteral("retouch_masks"));
  }
  if (!pipeline_state.local_masks.empty() ||
      state.linear_mask_enabled ||
      state.radial_mask_enabled ||
      std::abs(state.linear_mask_exposure) > 1e-6 ||
      std::abs(state.linear_mask_temperature) > 1e-6 ||
      std::abs(state.radial_mask_exposure) > 1e-6 ||
      std::abs(state.radial_mask_temperature) > 1e-6) {
    return unsupported_stage_reason(
        QStringLiteral("Layers"),
        QStringLiteral(
            "legacy_local_masks"));
  }
  if (pixels.size() <= static_cast<std::size_t>(std::numeric_limits<int>::max())) {
    const auto highlight_analysis =
        photo_editor::pipeline::analyze_highlight_reconstruction(
            pixels,
            static_cast<int>(pixels.size()),
            1,
            pipeline_state.input_transform);
    if (highlight_analysis.has_clipping()) {
      return unsupported_stage_reason(
          QStringLiteral("NormalizeRaw"),
          QStringLiteral(
              "camera_linear_highlight_reconstruction"));
    }
  }
  if (has_extended_range_pixels(pixels)) {
    return unsupported_stage_reason(
        QStringLiteral("SceneAdjustments"),
        QStringLiteral(
            "extended_range_pixels"));
  }
  if (render_state_requires_exact_global_tone(state)) {
    return unsupported_stage_reason(
        QStringLiteral("Tone"),
        QStringLiteral(
            "exact_global_tone"));
  }
  if (render_state_requires_exact_post_color(state)) {
    return unsupported_stage_reason(
        QStringLiteral("Look"),
        QStringLiteral(
            "exact_post_color"));
  }
  const auto stage_reason =
      cpu_required_stage_reason(state);
  if (!stage_reason.isEmpty()) {
    return stage_reason;
  }
  return {};
#else
  Q_UNUSED(state);
  Q_UNUSED(pipeline_state);
  Q_UNUSED(pixels);
  Q_UNUSED(metal_available);
  return QStringLiteral("metal_not_compiled");
#endif
}

void apply_output_color_space_transform(QImage* image, const QString& output_color_space_name) {
  if (image == nullptr || image->isNull()) {
    return;
  }

  const auto target_color_space = preview_output_color_space_for_name(output_color_space_name);
  if (!target_color_space.isValid()) {
    return;
  }

  *image = finalize_output_image(std::move(*image), target_color_space);
}

QImage normalize_preview_color_space(QImage image) {
  if (image.isNull()) {
    return image;
  }

  if (!image.colorSpace().isValid()) {
    image.setColorSpace(preview_target_color_space());
  }
  return image;
}

double clamp_unit(double value) {
  return qBound(0.0, value, 1.0);
}

double smoothstep(double edge0, double edge1, double x) {
  if (edge0 >= edge1) {
    return x < edge0 ? 0.0 : 1.0;
  }
  const auto t = clamp_unit((x - edge0) / (edge1 - edge0));
  return t * t * (3.0 - 2.0 * t);
}

double map_double_value(const QVariantMap& map, const QString& key, double fallback) {
  return map.contains(key) ? map.value(key).toDouble() : fallback;
}

bool map_bool_value(const QVariantMap& map, const QString& key, bool fallback) {
  return map.contains(key) ? map.value(key).toBool() : fallback;
}

QString variant_sha1(const QVariant& value) {
  return QString::fromLatin1(
      QCryptographicHash::hash(
          QJsonDocument::fromVariant(value).toJson(QJsonDocument::Compact),
          QCryptographicHash::Sha1)
          .toHex());
}

QStringList normalized_string_list_value(
    const QVariantMap& map,
    const QString& list_key,
    const QString& single_key) {
  QStringList values;
  const auto append_value = [&](const QString& raw_value) {
    const auto value = raw_value.simplified();
    if (!value.isEmpty() && !values.contains(value)) {
      values.push_back(value);
    }
  };

  const auto list_value = map.value(list_key);
  if (list_value.canConvert<QStringList>()) {
    for (const auto& value : list_value.toStringList()) {
      append_value(value);
    }
  } else {
    for (const auto& value : list_value.toList()) {
      append_value(value.toString());
    }
  }
  append_value(map.value(single_key).toString());
  return values;
}

QStringList selection_mask_set_ids_for_layer(const QVariantMap& layer) {
  return normalized_string_list_value(
      layer,
      QStringLiteral("selectionMaskSetIds"),
      QStringLiteral("selectionMaskSetId"));
}

QStringList selection_mask_set_names_for_layer(const QVariantMap& layer) {
  return normalized_string_list_value(
      layer,
      QStringLiteral("selectionMaskSetNames"),
      QStringLiteral("selectionMaskSetName"));
}

using MaskRasterPolicy = LocalMaskRasterPolicy;

double brush_point_weight_for_mask(
    double normalized_x,
    double normalized_y,
    const photo_editor::pipeline::LocalMask& mask,
    const photo_editor::pipeline::BrushPoint& point) {
  const auto brush_size = std::max(0.01, point.size);
  const auto dx = normalized_x - point.x;
  const auto dy = normalized_y - point.y;
  const auto distance = std::sqrt(dx * dx + dy * dy) / brush_size;
  const auto inner_scale = std::clamp(mask.brush_hardness, 0.0, 0.98);
  return clamp_unit(point.flow) * clamp_unit(point.opacity) *
         (1.0 - smoothstep(inner_scale, 1.0, distance));
}

void rebuild_brush_segmentation_at_output_resolution(
    photo_editor::pipeline::LocalMask* mask,
    int width,
    int height) {
  if (mask == nullptr || width <= 0 || height <= 0 || mask->brush_points.empty()) {
    return;
  }

  std::vector<unsigned char> rebuilt(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), 0);
  for (const auto& point : mask->brush_points) {
    const auto radius_x =
        std::max(1, static_cast<int>(std::ceil(std::max(0.01, point.size) * static_cast<double>(std::max(1, width - 1)))));
    const auto radius_y =
        std::max(1, static_cast<int>(std::ceil(std::max(0.01, point.size) * static_cast<double>(std::max(1, height - 1)))));
    const auto center_x = clamp_unit(point.x) * static_cast<double>(std::max(1, width - 1));
    const auto center_y = clamp_unit(point.y) * static_cast<double>(std::max(1, height - 1));
    const auto min_x = std::max(0, static_cast<int>(std::floor(center_x)) - radius_x);
    const auto max_x = std::min(width - 1, static_cast<int>(std::ceil(center_x)) + radius_x);
    const auto min_y = std::max(0, static_cast<int>(std::floor(center_y)) - radius_y);
    const auto max_y = std::min(height - 1, static_cast<int>(std::ceil(center_y)) + radius_y);
    for (int y = min_y; y <= max_y; ++y) {
      const auto normalized_y = height > 1 ? static_cast<double>(y) / static_cast<double>(height - 1) : 0.5;
      for (int x = min_x; x <= max_x; ++x) {
        const auto normalized_x = width > 1 ? static_cast<double>(x) / static_cast<double>(width - 1) : 0.5;
        const auto point_weight = brush_point_weight_for_mask(normalized_x, normalized_y, *mask, point);
        if (point_weight <= 1e-6) {
          continue;
        }
        auto weight = static_cast<double>(rebuilt[static_cast<std::size_t>(y * width + x)]) / 255.0;
        if (point.erase) {
          weight *= (1.0 - point_weight);
        } else {
          weight = mask->brush_buildup ? 1.0 - (1.0 - weight) * (1.0 - point_weight)
                                       : std::max(weight, point_weight);
        }
        rebuilt[static_cast<std::size_t>(y * width + x)] =
            static_cast<unsigned char>(std::lround(clamp_unit(weight) * 255.0));
      }
    }
  }

  mask->segmentation_mask = std::move(rebuilt);
  mask->segmentation_width = width;
  mask->segmentation_height = height;
}

void populate_inpaint_patch(
    photo_editor::pipeline::LocalMask* local_mask,
    const QVariantMap& mask) {
  if (local_mask == nullptr) {
    return;
  }

  const auto patch_data =
      mask.value(QStringLiteral("inpaintPatchData")).toByteArray();
  const auto patch_x = mask.value(QStringLiteral("inpaintPatchX")).toInt();
  const auto patch_y = mask.value(QStringLiteral("inpaintPatchY")).toInt();
  const auto patch_width =
      mask.value(QStringLiteral("inpaintPatchWidth")).toInt();
  const auto patch_height =
      mask.value(QStringLiteral("inpaintPatchHeight")).toInt();
  if (patch_data.isEmpty() || patch_width <= 0 || patch_height <= 0) {
    return;
  }

  auto patch_image = QImage::fromData(patch_data, "PNG");
  if (patch_image.isNull() || patch_image.width() != patch_width ||
      patch_image.height() != patch_height) {
    return;
  }
  if (!patch_image.colorSpace().isValid()) {
    auto patch_color_space = preview_output_color_space_for_name(
        mask.value(
                QStringLiteral("inpaintPatchColorSpace"),
                QStringLiteral("sRGB"))
            .toString());
    if (!patch_color_space.isValid()) {
      patch_color_space = preview_target_color_space();
    }
    patch_image.setColorSpace(patch_color_space);
  }

  const auto working_patch = working_image_from_qimage(patch_image);
  if (working_patch.isNull()) {
    return;
  }
  local_mask->inpaint_patch_pixels = working_patch.pixels;
  local_mask->inpaint_patch_x = patch_x;
  local_mask->inpaint_patch_y = patch_y;
  local_mask->inpaint_patch_width = working_patch.width;
  local_mask->inpaint_patch_height = working_patch.height;
  local_mask->inpaint_model_name =
      mask.value(QStringLiteral("inpaintModelName")).toString().toStdString();
  local_mask->inpaint_blend_amount = qBound(
      0.0,
      mask.value(QStringLiteral("inpaintBlendAmount"), 1.0).toDouble(),
      1.0);
  local_mask->inpaint_seam_feather = qBound(
      0.0,
      mask.value(QStringLiteral("inpaintSeamFeather"), 4.0).toDouble(),
      24.0);
  local_mask->inpaint_color_match = qBound(
      0.0,
      mask.value(QStringLiteral("inpaintColorMatch"), 0.75).toDouble(),
      1.0);
}

photo_editor::pipeline::LocalMask pipeline_local_mask(
    const QVariantMap& mask,
    MaskRasterPolicy raster_policy,
    const QSize& output_size) {
  photo_editor::pipeline::LocalMask local_mask;
  local_mask.id = mask.value(QStringLiteral("id")).toString().toStdString();
  local_mask.parent_id = mask.value(QStringLiteral("parentId")).toString().toStdString();
  local_mask.output_id = mask.value(QStringLiteral("outputId")).toString().toStdString();
  local_mask.name = mask.value(QStringLiteral("name")).toString().toStdString();
  local_mask.kind = normalized_mask_kind(mask.value(QStringLiteral("kind")).toString()).toStdString();
  local_mask.operation =
      normalized_mask_operation(mask.value(QStringLiteral("operation")).toString()).toStdString();
  local_mask.blend_mode =
      normalized_mask_blend_mode(mask.value(QStringLiteral("blendMode")).toString()).toStdString();
  local_mask.retouch_mode = mask.value(QStringLiteral("retouchMode")).toString().trimmed().toLower().toStdString();
  local_mask.retouch_order = std::max(0, mask.value(QStringLiteral("retouchOrder")).toInt());
  local_mask.enabled = mask.contains(QStringLiteral("enabled"))
                           ? mask.value(QStringLiteral("enabled")).toBool()
                           : true;
  local_mask.reusable_output = map_bool_value(mask, QStringLiteral("reusableOutput"), false);
  local_mask.invert = map_bool_value(
      mask,
      QStringLiteral("invert"),
      map_bool_value(mask, QStringLiteral("selectionInvert"), false));
  local_mask.opacity = std::clamp(map_double_value(mask, QStringLiteral("opacity"), 1.0), 0.0, 1.0);
  local_mask.density = std::clamp(map_double_value(mask, QStringLiteral("density"), 1.0), 0.0, 1.0);
  local_mask.exposure = map_double_value(mask, QStringLiteral("exposure"), 0.0);
  local_mask.contrast = map_double_value(mask, QStringLiteral("contrast"), 0.0);
  local_mask.highlights = map_double_value(mask, QStringLiteral("highlights"), 0.0);
  local_mask.shadows = map_double_value(mask, QStringLiteral("shadows"), 0.0);
  local_mask.whites = map_double_value(mask, QStringLiteral("whites"), 0.0);
  local_mask.blacks = map_double_value(mask, QStringLiteral("blacks"), 0.0);
  local_mask.temperature = map_double_value(mask, QStringLiteral("temperature"), 0.0);
  local_mask.tint = map_double_value(mask, QStringLiteral("tint"), 0.0);
  local_mask.vibrance = map_double_value(mask, QStringLiteral("vibrance"), 0.0);
  local_mask.saturation = map_double_value(mask, QStringLiteral("saturation"), 0.0);
  local_mask.hue_shift = map_double_value(mask, QStringLiteral("hueShift"), 0.0);
  local_mask.saturation_shift = map_double_value(mask, QStringLiteral("saturationShift"), 0.0);
  local_mask.colorize_hue = map_double_value(mask, QStringLiteral("colorizeHue"), 0.0);
  local_mask.colorize_strength = map_double_value(mask, QStringLiteral("colorizeStrength"), 0.0);
  local_mask.remove_strength = map_double_value(mask, QStringLiteral("removeStrength"), 0.0);
  local_mask.heal_texture_blend =
      std::clamp(map_double_value(mask, QStringLiteral("healTextureBlend"), 0.68), 0.0, 1.0);
  local_mask.heal_color_blend =
      std::clamp(map_double_value(mask, QStringLiteral("healColorBlend"), 0.46), 0.0, 1.0);
  local_mask.selection_invert = map_bool_value(mask, QStringLiteral("selectionInvert"), local_mask.invert);
  local_mask.selection_expand = map_double_value(mask, QStringLiteral("selectionExpand"), 0.0);
  local_mask.selection_smooth = map_double_value(mask, QStringLiteral("selectionSmooth"), 0.0);
  local_mask.selection_fine_edge = map_double_value(mask, QStringLiteral("selectionFineEdge"), 0.0);
  local_mask.selection_edge_contrast =
      map_double_value(mask, QStringLiteral("selectionEdgeContrast"), 0.0);
  local_mask.selection_decontaminate =
      map_double_value(mask, QStringLiteral("selectionDecontaminate"), 0.0);
  local_mask.selection_grow = map_double_value(mask, QStringLiteral("selectionGrow"), 0.0);
  local_mask.selection_shrink = map_double_value(mask, QStringLiteral("selectionShrink"), 0.0);
  local_mask.selection_feather = map_double_value(mask, QStringLiteral("selectionFeather"), 0.0);
  local_mask.selection_blur = map_double_value(mask, QStringLiteral("selectionBlur"), 0.0);
  local_mask.selection_harden = map_double_value(mask, QStringLiteral("selectionHarden"), 0.0);
  local_mask.selection_despeckle = map_double_value(mask, QStringLiteral("selectionDespeckle"), 0.0);
  local_mask.selection_fill_holes = map_double_value(mask, QStringLiteral("selectionFillHoles"), 0.0);
  local_mask.selection_remove_islands =
      map_double_value(mask, QStringLiteral("selectionRemoveIslands"), 0.0);
  local_mask.refine_luminance_min = map_double_value(mask, QStringLiteral("refineLuminanceMin"), 0.0);
  local_mask.refine_luminance_max = map_double_value(mask, QStringLiteral("refineLuminanceMax"), 1.0);
  local_mask.refine_hue_center = map_double_value(mask, QStringLiteral("refineHueCenter"), 0.0);
  local_mask.refine_hue_range = map_double_value(mask, QStringLiteral("refineHueRange"), 0.5);
  local_mask.refine_saturation_min = map_double_value(mask, QStringLiteral("refineSaturationMin"), 0.0);
  local_mask.refine_saturation_max = map_double_value(mask, QStringLiteral("refineSaturationMax"), 1.0);
  local_mask.refine_depth_min = map_double_value(mask, QStringLiteral("refineDepthMin"), 0.0);
  local_mask.refine_depth_max = map_double_value(mask, QStringLiteral("refineDepthMax"), 1.0);
  local_mask.refine_edge_confidence_min =
      map_double_value(mask, QStringLiteral("refineEdgeConfidenceMin"), 0.0);
  local_mask.refine_range_feather = map_double_value(mask, QStringLiteral("refineRangeFeather"), 0.05);
  local_mask.feather = map_double_value(
      mask,
      QStringLiteral("feather"),
      local_mask.kind == "radial" ? 0.25
      : local_mask.kind == "ellipse" || local_mask.kind == "rectangle" ? 0.08
      : 0.22);
  local_mask.rotation = map_double_value(mask, QStringLiteral("rotation"), 0.0);

  if (local_mask.kind == "radial" || local_mask.kind == "ellipse" ||
      local_mask.kind == "rectangle") {
    local_mask.center_x = map_double_value(mask, QStringLiteral("centerX"), 0.5);
    local_mask.center_y = map_double_value(mask, QStringLiteral("centerY"), 0.5);
    local_mask.radius_x = map_double_value(
        mask, QStringLiteral("radiusX"), local_mask.kind == "rectangle" ? 0.30 : 0.25);
    local_mask.radius_y = map_double_value(
        mask, QStringLiteral("radiusY"), local_mask.kind == "rectangle" ? 0.20 : 0.25);
  } else if (local_mask.kind == "linear") {
    local_mask.position = map_double_value(mask, QStringLiteral("position"), 0.5);
  } else if (local_mask.kind == "brush") {
    local_mask.feather = map_double_value(mask, QStringLiteral("feather"), 0.75);
    local_mask.brush_size = map_double_value(mask, QStringLiteral("brushSize"), 0.08);
    local_mask.brush_flow = map_double_value(mask, QStringLiteral("brushFlow"), 1.0);
    local_mask.brush_hardness = map_double_value(mask, QStringLiteral("brushHardness"), 0.25);
    local_mask.brush_buildup = map_bool_value(mask, QStringLiteral("brushBuildup"), true);
    local_mask.clone_source_set = map_bool_value(mask, QStringLiteral("cloneSourceSet"), false);
    local_mask.clone_source_x = map_double_value(mask, QStringLiteral("cloneSourceX"), 0.5);
    local_mask.clone_source_y = map_double_value(mask, QStringLiteral("cloneSourceY"), 0.5);
    local_mask.clone_offset_x = map_double_value(mask, QStringLiteral("cloneOffsetX"), 0.0);
    local_mask.clone_offset_y = map_double_value(mask, QStringLiteral("cloneOffsetY"), 0.0);
    local_mask.clone_scale = map_double_value(mask, QStringLiteral("cloneScale"), 1.0);
    local_mask.clone_rotation = map_double_value(mask, QStringLiteral("cloneRotation"), 0.0);
    local_mask.clone_aligned = map_bool_value(mask, QStringLiteral("cloneAligned"), true);
    auto raster_mask = mask;
    bool version2 = false;
    for (const auto& stroke : mask.value(QStringLiteral("brushStrokes")).toList()) {
      if (stroke.toMap().value(QStringLiteral("settings")).toMap()
              .value(QStringLiteral("rasterVersion")).toInt() >= 2) { version2 = true; break; }
    }
    if (version2 && raster_policy == MaskRasterPolicy::RebuildEditableAtOutputResolution && output_size.isValid()) {
      const auto rebuilt = mask_with_rebuilt_brush_matte(mask, output_size, QStringLiteral("full-resolution-render"));
      if (!rebuilt.isEmpty()) raster_mask = rebuilt;
    }
    const auto seg_data = raster_mask.value(QStringLiteral("segmentationData")).toByteArray();
    const auto seg_width = raster_mask.value(QStringLiteral("segmentationWidth")).toInt();
    const auto seg_height = raster_mask.value(QStringLiteral("segmentationHeight")).toInt();
    const auto stored_raster_covers_output =
        valid_segmentation_payload(seg_data, seg_width, seg_height) &&
        output_size.isValid() &&
        seg_width >= output_size.width() &&
        seg_height >= output_size.height();
    const auto rebuild_legacy_brush =
        !version2 &&
        raster_policy == MaskRasterPolicy::RebuildEditableAtOutputResolution &&
        output_size.isValid() && !stored_raster_covers_output;
    if (local_mask.retouch_mode == "clone" || rebuild_legacy_brush) {
      const auto points = mask.value(QStringLiteral("brushPoints")).toList();
      local_mask.brush_points.reserve(static_cast<std::size_t>(points.size()));
      for (const auto& point_value : points) {
        const auto point = point_value.toMap();
        local_mask.brush_points.push_back(photo_editor::pipeline::BrushPoint{
            .x = map_double_value(point, QStringLiteral("x"), 0.5),
            .y = map_double_value(point, QStringLiteral("y"), 0.5),
            .size = map_double_value(point, QStringLiteral("size"), local_mask.brush_size),
            .flow = map_double_value(point, QStringLiteral("flow"), local_mask.brush_flow),
            .opacity = map_double_value(point, QStringLiteral("opacity"), 1.0),
            .auto_mask_anchor_x = map_double_value(
                point,
                QStringLiteral("autoMaskAnchorX"),
                map_double_value(point, QStringLiteral("x"), 0.5)),
            .auto_mask_anchor_y = map_double_value(
                point,
                QStringLiteral("autoMaskAnchorY"),
                map_double_value(point, QStringLiteral("y"), 0.5)),
            .pressure = map_double_value(point, QStringLiteral("pressure"), 1.0),
            .tilt_x = map_double_value(point, QStringLiteral("tiltX"), 0.0),
            .tilt_y = map_double_value(point, QStringLiteral("tiltY"), 0.0),
            .erase = map_bool_value(point, QStringLiteral("erase"), false),
        });
      }
    }
    // Freeze the source transform per stroke. Resetting a non-aligned stamp
    // must not move the source of strokes already on the canvas.
    if (local_mask.retouch_mode == "clone") {
      std::size_t offset = 0;
      for (const auto& value : mask.value("brushStrokes").toList()) {
        const auto stroke = value.toMap();
        const auto settings = stroke.value("settings").toMap();
        const auto count = static_cast<std::size_t>(
            stroke.value("samples").toList().size());
        if (offset + count > local_mask.brush_points.size()) {
          local_mask.clone_strokes.clear();
          break;
        }
        photo_editor::pipeline::CloneStroke clone;
        clone.points.assign(
            local_mask.brush_points.begin() + static_cast<std::ptrdiff_t>(offset),
            local_mask.brush_points.begin() +
                static_cast<std::ptrdiff_t>(offset + count));
        clone.source_x = settings.value("cloneSourceX", local_mask.clone_source_x).toDouble();
        clone.source_y = settings.value("cloneSourceY", local_mask.clone_source_y).toDouble();
        clone.offset_x = settings.value("cloneOffsetX", local_mask.clone_offset_x).toDouble();
        clone.offset_y = settings.value("cloneOffsetY", local_mask.clone_offset_y).toDouble();
        clone.scale = settings.value("cloneScale", local_mask.clone_scale).toDouble();
        clone.rotation = settings.value("cloneRotation", local_mask.clone_rotation).toDouble();
        local_mask.clone_strokes.push_back(std::move(clone));
        offset += count;
      }
      if (offset != local_mask.brush_points.size()) {
        local_mask.clone_strokes.clear();
      }
    }
    if (rebuild_legacy_brush && !local_mask.brush_points.empty()) {
      rebuild_brush_segmentation_at_output_resolution(&local_mask, output_size.width(), output_size.height());
    } else if (valid_segmentation_payload(seg_data, seg_width, seg_height)) {
      local_mask.segmentation_mask.resize(static_cast<std::size_t>(seg_data.size()));
      std::memcpy(local_mask.segmentation_mask.data(),
                  seg_data.constData(),
                  static_cast<std::size_t>(seg_data.size()));
      local_mask.segmentation_width = seg_width;
      local_mask.segmentation_height = seg_height;
    }
  } else if (local_mask.kind == "polygon" || local_mask.kind == "lasso") {
    local_mask.feather = map_double_value(mask, QStringLiteral("feather"), 0.06);
    const auto points = mask.value(QStringLiteral("polygonPoints")).toList();
    local_mask.polygon_points.reserve(static_cast<std::size_t>(points.size()));
    for (const auto& point_value : points) {
      const auto point = point_value.toMap();
      local_mask.polygon_points.push_back(photo_editor::pipeline::CurvePoint{
          .x = map_double_value(point, QStringLiteral("x"), 0.5),
          .y = map_double_value(point, QStringLiteral("y"), 0.5),
      });
    }
  } else if (local_mask.kind == "luminance") {
    local_mask.luminance_min = map_double_value(mask, QStringLiteral("luminanceMin"), 0.2);
    local_mask.luminance_max = map_double_value(mask, QStringLiteral("luminanceMax"), 0.8);
    local_mask.range_feather = map_double_value(mask, QStringLiteral("rangeFeather"), 0.1);
  } else if (local_mask.kind == "color") {
    local_mask.hue_center = map_double_value(mask, QStringLiteral("hueCenter"), 0.08);
    local_mask.hue_range = map_double_value(mask, QStringLiteral("hueRange"), 0.12);
    local_mask.saturation_min = map_double_value(mask, QStringLiteral("saturationMin"), 0.1);
    local_mask.saturation_max = map_double_value(mask, QStringLiteral("saturationMax"), 1.0);
    local_mask.range_feather = map_double_value(mask, QStringLiteral("rangeFeather"), 0.08);
  } else if (local_mask.kind == "depth") {
    local_mask.depth_min = map_double_value(mask, QStringLiteral("depthMin"), 0.2);
    local_mask.depth_max = map_double_value(mask, QStringLiteral("depthMax"), 0.8);
    local_mask.range_feather = map_double_value(mask, QStringLiteral("rangeFeather"), 0.1);
    const auto depth_data = mask.value(QStringLiteral("depthData")).toByteArray();
    const auto depth_width = mask.value(QStringLiteral("depthWidth")).toInt();
    const auto depth_height = mask.value(QStringLiteral("depthHeight")).toInt();
    if (valid_segmentation_payload(depth_data, depth_width, depth_height)) {
      local_mask.depth_map.resize(static_cast<std::size_t>(depth_data.size()));
      std::memcpy(local_mask.depth_map.data(),
                  depth_data.constData(),
                  static_cast<std::size_t>(depth_data.size()));
      local_mask.depth_width = depth_width;
      local_mask.depth_height = depth_height;
    }
  } else if (is_raster_mask_kind(QString::fromStdString(local_mask.kind))) {
    const auto seg_data = mask.value(QStringLiteral("segmentationData")).toByteArray();
    const auto seg_width = mask.value(QStringLiteral("segmentationWidth")).toInt();
    const auto seg_height = mask.value(QStringLiteral("segmentationHeight")).toInt();
    if (valid_segmentation_payload(seg_data, seg_width, seg_height)) {
      local_mask.segmentation_mask.resize(static_cast<std::size_t>(seg_data.size()));
      std::memcpy(local_mask.segmentation_mask.data(),
                  seg_data.constData(),
                  static_cast<std::size_t>(seg_data.size()));
      local_mask.segmentation_width = seg_width;
      local_mask.segmentation_height = seg_height;
    }
  }

  populate_inpaint_patch(&local_mask, mask);
  return local_mask;
}

double layer_adjustment_value(const QVariantMap& layer, const QString& key) {
  const auto adjustments = layer.value(QStringLiteral("adjustments")).toMap();
  if (adjustments.contains(key)) {
    return adjustments.value(key).toDouble();
  }
  return layer.value(key).toDouble();
}

QVariantList layer_adjustment_curve_points(const QVariantMap& layer, const QString& key) {
  const auto adjustments = layer.value(QStringLiteral("adjustments")).toMap();
  if (adjustments.contains(key)) {
    return adjustments.value(key).toList();
  }
  return layer.value(key).toList();
}

QVariantMap layer_adjustment_object(const QVariantMap& layer, const QString& key) {
  const auto adjustments = layer.value(QStringLiteral("adjustments")).toMap();
  const auto value = adjustments.contains(key) ? adjustments.value(key) : layer.value(key);
  return value.toMap();
}

double layer_object_double(const QVariantMap& object, const QString& key, double fallback = 0.0) {
  return object.contains(key) ? object.value(key).toDouble() : fallback;
}

bool layer_object_bool(const QVariantMap& object, const QString& key, bool fallback = false) {
  return object.contains(key) ? object.value(key).toBool() : fallback;
}

QString layer_object_string(const QVariantMap& object, const QString& key, const QString& fallback) {
  return object.contains(key) ? object.value(key).toString() : fallback;
}

std::array<double, 3> layer_lut_domain_array(
    const QVariant& value,
    const std::array<double, 3>& fallback) {
  auto domain = fallback;
  const auto list = value.toList();
  if (list.size() >= 3) {
    for (int index = 0; index < 3; ++index) {
      domain[static_cast<std::size_t>(index)] = list.at(index).toDouble();
    }
  }
  return domain;
}

std::array<double, 2> layer_lut_input_range_array(
    const QVariant& value,
    const std::array<double, 2>& fallback) {
  auto range = fallback;
  const auto list = value.toList();
  if (list.size() >= 2) {
    range[0] = list.at(0).toDouble();
    range[1] = list.at(1).toDouble();
  }
  return range;
}

std::vector<photo_editor::pipeline::ColorPixel> layer_lut_table(const QVariant& value) {
  std::vector<photo_editor::pipeline::ColorPixel> table;
  const auto list = value.toList();
  table.reserve(static_cast<std::size_t>(list.size()));
  for (const auto& entry : list) {
    const auto object = entry.toMap();
    if (object.isEmpty()) {
      const auto row = entry.toList();
      if (row.size() >= 3) {
        table.push_back(photo_editor::pipeline::ColorPixel{
            .red = static_cast<float>(row.at(0).toDouble()),
            .green = static_cast<float>(row.at(1).toDouble()),
            .blue = static_cast<float>(row.at(2).toDouble()),
            .alpha = static_cast<float>(row.value(3, 1.0).toDouble()),
        });
      }
      continue;
    }
    table.push_back(photo_editor::pipeline::ColorPixel{
        .red = static_cast<float>(object.value(QStringLiteral("red")).toDouble()),
        .green = static_cast<float>(object.value(QStringLiteral("green")).toDouble()),
        .blue = static_cast<float>(object.value(QStringLiteral("blue")).toDouble()),
        .alpha = static_cast<float>(object.value(QStringLiteral("alpha"), 1.0).toDouble()),
    });
  }
  return table;
}

photo_editor::pipeline::LutAdjustment layer_lut_adjustment_from_object(const QVariantMap& object) {
  return photo_editor::pipeline::LutAdjustment{
      .enabled = layer_object_bool(object, QStringLiteral("enabled"), true),
      .strength = qBound(0.0, layer_object_double(object, QStringLiteral("strength"), 100.0), 100.0),
      .name = object.value(QStringLiteral("name")).toString().toStdString(),
      .table = layer_lut_table(object.value(QStringLiteral("table"))),
      .size = qBound(0, object.value(QStringLiteral("size")).toInt(), 256),
      .domain_min = layer_lut_domain_array(
          object.value(QStringLiteral("domainMin")),
          std::array<double, 3>{0.0, 0.0, 0.0}),
      .domain_max = layer_lut_domain_array(
          object.value(QStringLiteral("domainMax")),
          std::array<double, 3>{1.0, 1.0, 1.0}),
      .input_color_space = object.value(QStringLiteral("inputColorSpace"), QStringLiteral("sRGB"))
                               .toString()
                               .toStdString(),
      .output_color_space = object.value(QStringLiteral("outputColorSpace"), QStringLiteral("sRGB"))
                                .toString()
                                .toStdString(),
      .shaper_table = layer_lut_table(object.value(QStringLiteral("shaperTable"))),
      .shaper_size = qBound(0, object.value(QStringLiteral("shaperSize")).toInt(), 65536),
      .shaper_input_range = layer_lut_input_range_array(
          object.value(QStringLiteral("shaperInputRange")),
          std::array<double, 2>{0.0, 1.0}),
  };
}

std::vector<photo_editor::pipeline::LutAdjustment> layer_lut_stack(const QVariantMap& layer) {
  const auto adjustments = layer.value(QStringLiteral("adjustments")).toMap();
  const auto lut_object = adjustments.contains(QStringLiteral("lut"))
                              ? adjustments.value(QStringLiteral("lut")).toMap()
                              : layer.value(QStringLiteral("lut")).toMap();
  const auto stack_value = adjustments.contains(QStringLiteral("lutStack"))
                               ? adjustments.value(QStringLiteral("lutStack"))
                               : layer.value(QStringLiteral("lutStack"));
  std::vector<photo_editor::pipeline::LutAdjustment> stack;
  for (const auto& value : stack_value.toList()) {
    const auto object = value.toMap();
    if (!object.isEmpty()) {
      stack.push_back(layer_lut_adjustment_from_object(object));
    }
  }
  if (!lut_object.isEmpty()) {
    stack.insert(stack.begin(), layer_lut_adjustment_from_object(lut_object));
  }
  return stack;
}

QString layer_nested_adjustment_string(
    const QVariantMap& layer,
    const QString& object_key,
    const QString& key,
    const QString& flat_key,
    const QString& fallback) {
  const auto object = layer_adjustment_object(layer, object_key);
  if (object.contains(key)) {
    return object.value(key).toString();
  }
  const auto adjustments = layer.value(QStringLiteral("adjustments")).toMap();
  if (adjustments.contains(flat_key)) {
    return adjustments.value(flat_key).toString();
  }
  return layer.value(flat_key, fallback).toString();
}

double layer_nested_adjustment_value(
    const QVariantMap& layer,
    const QString& object_key,
    const QString& key,
    const QString& flat_key,
    double fallback = 0.0) {
  const auto object = layer_adjustment_object(layer, object_key);
  if (object.contains(key)) {
    return object.value(key).toDouble();
  }
  const auto adjustments = layer.value(QStringLiteral("adjustments")).toMap();
  if (adjustments.contains(flat_key)) {
    return adjustments.value(flat_key).toDouble();
  }
  return layer.value(flat_key, fallback).toDouble();
}

std::array<photo_editor::pipeline::ColorMixerChannelAdjustment,
           photo_editor::pipeline::kColorMixerChannelCount>
layer_color_mixer_channels(const QVariantMap& layer) {
  std::array<photo_editor::pipeline::ColorMixerChannelAdjustment,
             photo_editor::pipeline::kColorMixerChannelCount> channels{};
  const auto color_mixer = layer_adjustment_object(layer, QStringLiteral("colorMixer"));
  const auto mixer_channels = color_mixer.value(QStringLiteral("channels")).toMap();
  for (std::size_t index = 0; index < channels.size(); ++index) {
    const auto name = QString::fromUtf8(
        photo_editor::pipeline::kColorMixerChannelNames[index]);
    const auto channel = mixer_channels.value(name).toMap();
    if (channel.isEmpty()) {
      continue;
    }
    channels[index].hue = channel.value(QStringLiteral("hue")).toDouble();
    channels[index].saturation = channel.value(QStringLiteral("saturation")).toDouble();
    channels[index].luminance = channel.value(QStringLiteral("luminance")).toDouble();
    channels[index].selective_cyan = channel.value(QStringLiteral("selectiveCyan")).toDouble();
    channels[index].selective_magenta = channel.value(QStringLiteral("selectiveMagenta")).toDouble();
    channels[index].selective_yellow = channel.value(QStringLiteral("selectiveYellow")).toDouble();
    channels[index].selective_black = channel.value(QStringLiteral("selectiveBlack")).toDouble();
  }
  return channels;
}

photo_editor::pipeline::RgbChannelMixerAdjustment layer_rgb_channel_mixer(const QVariantMap& layer) {
  photo_editor::pipeline::RgbChannelMixerAdjustment mixer;
  const auto color_mixer = layer_adjustment_object(layer, QStringLiteral("colorMixer"));
  const auto object = color_mixer.value(QStringLiteral("rgbChannelMixer")).toMap();
  if (object.isEmpty()) {
    return mixer;
  }
  mixer.red_from_red = layer_object_double(object, QStringLiteral("redFromRed"), 100.0);
  mixer.red_from_green = object.value(QStringLiteral("redFromGreen")).toDouble();
  mixer.red_from_blue = object.value(QStringLiteral("redFromBlue")).toDouble();
  mixer.green_from_red = object.value(QStringLiteral("greenFromRed")).toDouble();
  mixer.green_from_green = layer_object_double(object, QStringLiteral("greenFromGreen"), 100.0);
  mixer.green_from_blue = object.value(QStringLiteral("greenFromBlue")).toDouble();
  mixer.blue_from_red = object.value(QStringLiteral("blueFromRed")).toDouble();
  mixer.blue_from_green = object.value(QStringLiteral("blueFromGreen")).toDouble();
  mixer.blue_from_blue = layer_object_double(object, QStringLiteral("blueFromBlue"), 100.0);
  return mixer;
}

std::array<double, photo_editor::pipeline::kColorMixerChannelCount> layer_black_and_white_mixer(
    const QVariantMap& layer) {
  std::array<double, photo_editor::pipeline::kColorMixerChannelCount> mixer{};
  const auto color_mixer = layer_adjustment_object(layer, QStringLiteral("colorMixer"));
  const auto black_and_white = color_mixer.value(QStringLiteral("blackAndWhite")).toMap();
  const auto channels = black_and_white.value(QStringLiteral("channels")).toMap();
  for (std::size_t index = 0; index < mixer.size(); ++index) {
    const auto name = QString::fromUtf8(
        photo_editor::pipeline::kColorMixerChannelNames[index]);
    mixer[index] = channels.value(name).toDouble();
  }
  return mixer;
}

QVariantMap layer_color_mixer_child(const QVariantMap& layer, const QString& key) {
  return layer_adjustment_object(layer, QStringLiteral("colorMixer")).value(key).toMap();
}

QVariantMap layer_color_grading_child(const QVariantMap& layer, const QString& key) {
  return layer_adjustment_object(layer, QStringLiteral("colorGrading")).value(key).toMap();
}

photo_editor::pipeline::ColorPixel layer_fill_color(
    const QVariantMap& layer, const QString& working_profile) {
  auto color = QColor(layer.value(QStringLiteral("fillColor"), QStringLiteral("#808080")).toString());
  if (!color.isValid()) {
    color = QColor(QStringLiteral("#808080"));
  }
  QImage swatch(1, 1, QImage::Format_RGBA64);
  swatch.setColorSpace(QColorSpace(QColorSpace::SRgb));
  color.setAlphaF(static_cast<float>(qBound(0.0, layer.value(QStringLiteral("fillAlpha"), 1.0).toDouble(), 1.0)));
  swatch.fill(color);
  const auto working = working_image_from_qimage(swatch, working_profile);
  return working.isNull() ? photo_editor::pipeline::ColorPixel{} : working.pixels.front();
}



void populate_photo_layer_mask(photo_editor::pipeline::PhotoLayer* target, const QVariantMap& layer) {
  if (target == nullptr) {
    return;
  }
  const auto mask_data = layer.value(QStringLiteral("maskData")).toByteArray();
  const auto mask_width = layer.value(QStringLiteral("maskWidth")).toInt();
  const auto mask_height = layer.value(QStringLiteral("maskHeight")).toInt();
  const auto expected_size = mask_width * mask_height;
  if (mask_data.isEmpty() || mask_width <= 0 || mask_height <= 0 ||
      mask_data.size() != expected_size) {
    return;
  }
  target->mask_data.assign(mask_data.cbegin(), mask_data.cend());
  target->mask_width = mask_width;
  target->mask_height = mask_height;
  target->mask_inverted = layer.value(QStringLiteral("maskInverted")).toBool();
  target->mask_opacity =
      std::clamp(layer.value(QStringLiteral("maskOpacity"), 1.0).toDouble(), 0.0, 1.0);
  target->mask_density =
      std::clamp(layer.value(QStringLiteral("maskDensity"), 1.0).toDouble(), 0.0, 1.0);
}

QSize selection_layer_mask_raster_size(
    const QVariantList& masks,
    const QVariantMap& layer,
    const QSize& output_size) {
  if (output_size.isValid() && output_size.width() > 0 && output_size.height() > 0) {
    return output_size;
  }

  const auto layer_ids = selection_mask_set_ids_for_layer(layer);
  const auto layer_names = selection_mask_set_names_for_layer(layer);
  for (const auto& value : masks) {
    const auto mask = value.toMap();
    const auto set_id = mask.value(QStringLiteral("maskSetId")).toString().simplified();
    const auto set_name = mask.value(QStringLiteral("maskSetName")).toString().simplified();
    const auto matches = (!set_id.isEmpty() && layer_ids.contains(set_id)) ||
                         (!set_name.isEmpty() && layer_names.contains(set_name));
    if (!matches) {
      continue;
    }
    const auto segmentation_data = mask.value(QStringLiteral("segmentationData")).toByteArray();
    const auto segmentation_width = mask.value(QStringLiteral("segmentationWidth")).toInt();
    const auto segmentation_height = mask.value(QStringLiteral("segmentationHeight")).toInt();
    if (valid_segmentation_payload(segmentation_data, segmentation_width, segmentation_height)) {
      return QSize(segmentation_width, segmentation_height);
    }
  }
  return QSize(1024, 1024);
}

bool selection_layer_mask_set_is_identity(const photo_editor::pipeline::LocalMaskSet& mask_set) {
  return mask_set.enabled &&
         !mask_set.selection_invert &&
         std::abs(mask_set.selection_expand) <= 1e-6 &&
         std::abs(mask_set.selection_smooth) <= 1e-6 &&
         std::abs(mask_set.selection_fine_edge) <= 1e-6 &&
         std::abs(mask_set.selection_edge_contrast) <= 1e-6 &&
         std::abs(mask_set.opacity - 1.0) <= 1e-6 &&
         std::abs(mask_set.density - 1.0) <= 1e-6;
}

bool selection_layer_source_is_stored_raster_passthrough(
    const photo_editor::pipeline::LocalMask& source) {
  const auto expected_size =
      static_cast<std::size_t>(source.segmentation_width) *
      static_cast<std::size_t>(source.segmentation_height);
  const auto source_kind = QString::fromStdString(source.kind);
  return source.enabled &&
         source.operation == "add" &&
         !source.invert &&
         std::abs(source.opacity - 1.0) <= 1e-6 &&
         std::abs(source.density - 1.0) <= 1e-6 &&
         (source.kind == "brush" || is_ai_segmentation_mask_kind(source_kind)) &&
         source.segmentation_width > 0 &&
         source.segmentation_height > 0 &&
         source.segmentation_mask.size() == expected_size;
}

bool populate_photo_layer_selection_mask_from_stored_source(
    photo_editor::pipeline::PhotoLayer* target,
    const QVariantMap& layer,
    const photo_editor::pipeline::LocalMaskSet& mask_set,
    const QSize& raster_size) {
  if (target == nullptr || mask_set.sources.size() != 1 ||
      !selection_layer_mask_set_is_identity(mask_set)) {
    return false;
  }

  const auto& source = mask_set.sources.front();
  if (!selection_layer_source_is_stored_raster_passthrough(source)) {
    return false;
  }

  const QByteArray source_data(
      reinterpret_cast<const char*>(source.segmentation_mask.data()),
      static_cast<qsizetype>(source.segmentation_mask.size()));

  QVariantMap cache_payload;
  cache_payload.insert(
      QStringLiteral("version"),
      QStringLiteral("render-utils-linked-layer-mask-alpha-provider-cache-v1"));
  cache_payload.insert(QStringLiteral("layerId"), layer.value(QStringLiteral("id")).toString());
  cache_payload.insert(QStringLiteral("maskSetId"), QString::fromStdString(mask_set.id));
  cache_payload.insert(QStringLiteral("maskSetName"), QString::fromStdString(mask_set.name));
  cache_payload.insert(QStringLiteral("sourceKind"), QString::fromStdString(source.kind));
  cache_payload.insert(QStringLiteral("sourceOperation"), QString::fromStdString(source.operation));
  cache_payload.insert(QStringLiteral("sourceWidth"), source.segmentation_width);
  cache_payload.insert(QStringLiteral("sourceHeight"), source.segmentation_height);
  cache_payload.insert(QStringLiteral("targetWidth"), raster_size.width());
  cache_payload.insert(QStringLiteral("targetHeight"), raster_size.height());
  cache_payload.insert(
      QStringLiteral("sourceAlphaSha256"),
      QString::fromLatin1(QCryptographicHash::hash(source_data, QCryptographicHash::Sha256).toHex()));
  const auto cache_key = QStringLiteral("sha1:%1").arg(variant_sha1(cache_payload));
  const auto alpha_result = evaluate_mask_alpha_cached(
      cache_key,
      raster_size,
      1,
      [&](int, int, double normalized_x, double normalized_y) {
        const auto source_x = source.segmentation_width > 1
                                  ? normalized_x * static_cast<double>(source.segmentation_width - 1)
                                  : 0.0;
        const auto source_y = source.segmentation_height > 1
                                  ? normalized_y * static_cast<double>(source.segmentation_height - 1)
                                  : 0.0;
        const auto x0 = std::clamp(static_cast<int>(std::floor(source_x)), 0, source.segmentation_width - 1);
        const auto y0 = std::clamp(static_cast<int>(std::floor(source_y)), 0, source.segmentation_height - 1);
        const auto x1 = std::min(source.segmentation_width - 1, x0 + 1);
        const auto y1 = std::min(source.segmentation_height - 1, y0 + 1);
        const auto fx = source_x - static_cast<double>(x0);
        const auto fy = source_y - static_cast<double>(y0);
        const auto sample = [&](int sample_x, int sample_y) {
          return static_cast<double>(
              static_cast<unsigned char>(source_data.at(sample_y * source.segmentation_width + sample_x))) /
                 255.0;
        };
        const auto top = std::lerp(sample(x0, y0), sample(x1, y0), fx);
        const auto bottom = std::lerp(sample(x0, y1), sample(x1, y1), fx);
        return std::lerp(top, bottom, fy);
      });
  if (!alpha_result.valid ||
      !valid_segmentation_payload(alpha_result.alpha_mask, raster_size.width(), raster_size.height())) {
    return false;
  }

  target->mask_data.assign(alpha_result.alpha_mask.cbegin(), alpha_result.alpha_mask.cend());
  target->mask_width = raster_size.width();
  target->mask_height = raster_size.height();
  target->mask_inverted = layer.value(QStringLiteral("maskInverted")).toBool();
  return true;
}

void populate_photo_layer_selection_mask(
    photo_editor::pipeline::PhotoLayer* target,
    const QVariantMap& layer,
    const QVariantList& masks,
    MaskRasterPolicy raster_policy,
    const QSize& output_size,
    std::span<const photo_editor::pipeline::ColorPixel> source_pixels,
    const photo_editor::pipeline::ColorPipelineConfig& color_pipeline,
    DesktopRenderPurpose purpose) {
  if (target == nullptr || !layer.value(QStringLiteral("selectionScoped")).toBool()) {
    return;
  }

  const auto mask_set_ids = selection_mask_set_ids_for_layer(layer);
  const auto mask_set_names = selection_mask_set_names_for_layer(layer);
  if (mask_set_ids.isEmpty() && mask_set_names.isEmpty()) {
    return;
  }

  const auto raster_size = selection_layer_mask_raster_size(masks, layer, output_size);
  if (!raster_size.isValid() || raster_size.width() <= 0 || raster_size.height() <= 0) {
    return;
  }

  const auto mask_sets = compile_pipeline_local_mask_sets(
      masks, purpose, raster_policy, raster_size);
  const auto ordered_count = std::max(mask_set_ids.size(), mask_set_names.size());
  for (auto index = 0; index < ordered_count; ++index) {
    const auto target_id = index < mask_set_ids.size() ? mask_set_ids.at(index) : QString();
    const auto target_name = index < mask_set_names.size() ? mask_set_names.at(index) : QString();
    for (const auto& set : mask_sets) {
      const auto set_id = QString::fromStdString(set.id).simplified();
      const auto set_name = QString::fromStdString(set.name).simplified();
      const auto matches =
          (!target_id.isEmpty() && set_id == target_id) ||
          (target_id.isEmpty() && !target_name.isEmpty() && set_name == target_name);
      if (matches &&
          populate_photo_layer_selection_mask_from_stored_source(target, layer, set, raster_size)) {
        return;
      }
      if (!matches) {
        continue;
      }
      const auto alpha = detail::rasterize_layer_selection_alpha(
          set,
          raster_size.width(),
          raster_size.height(),
          source_pixels,
          color_pipeline);
      if (alpha.size() !=
          static_cast<std::size_t>(raster_size.width()) *
              static_cast<std::size_t>(raster_size.height())) {
        continue;
      }
      target->mask_data = alpha;
      target->mask_width = raster_size.width();
      target->mask_height = raster_size.height();
      target->mask_inverted = layer.value(QStringLiteral("maskInverted")).toBool();
      return;
    }
  }
}

double photo_layer_mask_coverage_at(
    const photo_editor::pipeline::PhotoLayer& mask,
    int x,
    int y,
    int width,
    int height) {
  const auto expected = static_cast<std::size_t>(mask.mask_width) *
      static_cast<std::size_t>(mask.mask_height);
  if (mask.mask_width <= 0 || mask.mask_height <= 0 ||
      mask.mask_data.size() != expected || width <= 0 || height <= 0) return 1.0;
  const auto normalized_x = width > 1 ? static_cast<double>(std::clamp(x, 0, width - 1)) /
      static_cast<double>(width - 1) : 0.0;
  const auto normalized_y = height > 1 ? static_cast<double>(std::clamp(y, 0, height - 1)) /
      static_cast<double>(height - 1) : 0.0;
  const auto mask_x = std::clamp(static_cast<int>(std::lround(
      normalized_x * static_cast<double>(mask.mask_width - 1))), 0, mask.mask_width - 1);
  const auto mask_y = std::clamp(static_cast<int>(std::lround(
      normalized_y * static_cast<double>(mask.mask_height - 1))), 0, mask.mask_height - 1);
  const auto index = static_cast<std::size_t>(mask_y) * static_cast<std::size_t>(mask.mask_width) +
      static_cast<std::size_t>(mask_x);
  auto coverage = static_cast<double>(mask.mask_data[index]) / 255.0;
  if (mask.mask_inverted) coverage = 1.0 - coverage;
  coverage = std::min(coverage, std::clamp(mask.mask_density, 0.0, 1.0));
  return std::clamp(coverage * std::clamp(mask.mask_opacity, 0.0, 1.0), 0.0, 1.0);
}

void intersect_photo_layer_masks(
    photo_editor::pipeline::PhotoLayer* target,
    const photo_editor::pipeline::PhotoLayer& selection_mask) {
  if (target == nullptr || selection_mask.mask_data.empty() ||
      selection_mask.mask_width <= 0 || selection_mask.mask_height <= 0) return;
  if (target->mask_data.empty() || target->mask_width <= 0 || target->mask_height <= 0) {
    *target = selection_mask;
    return;
  }
  const auto width = selection_mask.mask_width;
  const auto height = selection_mask.mask_height;
  std::vector<std::uint8_t> intersection(static_cast<std::size_t>(width) *
      static_cast<std::size_t>(height));
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const auto selection_coverage = photo_layer_mask_coverage_at(
          selection_mask, x, y, width, height);
      const auto target_coverage = photo_layer_mask_coverage_at(*target, x, y, width, height);
      const auto index = static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
          static_cast<std::size_t>(x);
      intersection[index] = static_cast<std::uint8_t>(std::lround(
          std::clamp(selection_coverage * target_coverage, 0.0, 1.0) * 255.0));
    }
  }
  target->mask_data = std::move(intersection);
  target->mask_width = width;
  target->mask_height = height;
  target->mask_inverted = false;
  target->mask_opacity = 1.0;
  target->mask_density = 1.0;
}

photo_editor::pipeline::PhotoLayer compile_photo_layer_selection_mask(
    const QVariantMap& layer,
    const QVariantList& masks,
    MaskRasterPolicy raster_policy,
    const QSize& output_size,
    std::span<const photo_editor::pipeline::ColorPixel> source_pixels,
    const photo_editor::pipeline::ColorPipelineConfig& color_pipeline,
    DesktopRenderPurpose purpose,
    bool preserve_layer_mask_inversion = false) {
  auto selection_layer = layer;
  selection_layer.insert(QStringLiteral("maskInverted"), preserve_layer_mask_inversion &&
      layer.value(QStringLiteral("maskInverted")).toBool());
  selection_layer.insert(QStringLiteral("maskOpacity"), 1.0);
  selection_layer.insert(QStringLiteral("maskDensity"), 1.0);
  photo_editor::pipeline::PhotoLayer selection_mask;
  populate_photo_layer_selection_mask(&selection_mask, selection_layer, masks,
      raster_policy, output_size, source_pixels, color_pipeline, purpose);
  const auto intersection_ids = layer.value(QStringLiteral("selectionIntersectionMaskSetIds")).toStringList();
  const auto intersection_names = layer.value(QStringLiteral("selectionIntersectionMaskSetNames")).toStringList();
  for (auto index = 0; index < std::max(intersection_ids.size(), intersection_names.size()); ++index) {
    const auto id = index < intersection_ids.size() ? intersection_ids.at(index) : QString();
    const auto name = index < intersection_names.size() ? intersection_names.at(index) : QString();
    if (id.isEmpty() && name.isEmpty()) continue;
    auto intersection_layer = selection_layer;
    intersection_layer.insert(QStringLiteral("maskInverted"), false);
    intersection_layer.insert(QStringLiteral("selectionMaskSetId"), id);
    intersection_layer.insert(QStringLiteral("selectionMaskSetName"), name);
    intersection_layer.insert(QStringLiteral("selectionMaskSetIds"), QVariantList{id});
    intersection_layer.insert(QStringLiteral("selectionMaskSetNames"), QVariantList{name});
    photo_editor::pipeline::PhotoLayer additional_mask;
    populate_photo_layer_selection_mask(&additional_mask, intersection_layer, masks,
        raster_policy, output_size, source_pixels, color_pipeline, purpose);
    intersect_photo_layer_masks(&selection_mask, additional_mask);
  }
  return selection_mask;
}

void populate_photo_layer_pixels(photo_editor::pipeline::PhotoLayer* target,
    const QVariantMap& layer, const QString& working_profile, const QSize& text_raster_size = {}) {
  if (target == nullptr) {
    return;
  }
  const auto kind = layer.value(QStringLiteral("kind")).toString().trimmed().toLower();
  const auto pixel_data = layer.value(QStringLiteral("pixelData")).toByteArray();
  const auto pixel_width = layer.value(QStringLiteral("pixelWidth")).toInt();
  const auto pixel_height = layer.value(QStringLiteral("pixelHeight")).toInt();
  QImage image;
  if (kind == QStringLiteral("pixel") && (layer.contains("paintTiles") || layer.contains("paintDraftTiles"))) {
    QString paint_error;
    image = rasterize_photo_paint_layer(layer,text_raster_size,&paint_error);
    target->content_error = paint_error.toStdString();
  } else if (kind == QStringLiteral("pixel") && layer.value("paintDraftImage").canConvert<QImage>()) {
    image = layer.value("paintDraftImage").value<QImage>();
  } else if (!pixel_data.isEmpty() && pixel_width > 0 && pixel_height > 0) {
    image = QImage::fromData(pixel_data, "PNG");
    if (image.isNull() || image.width() != pixel_width || image.height() != pixel_height) {
      return;
    }
  } else if (kind == QStringLiteral("text")) {
    QString text_error;
    image = rasterize_legacy_photo_text(layer, text_raster_size, &text_error);
    target->content_error = text_error.toStdString();
  } else if (kind == QStringLiteral("source")) {
    const auto source_path = layer.value(QStringLiteral("sourcePath")).toString();
    if (source_path.isEmpty()) {
      return;
    }
    image = decode_photo_layer_source_image(source_path);
    if (image.isNull()) {
      return;
    }
    image = converted_to_color_space(image, working_color_space(working_profile), QImage::Format_RGBA32FPx4);
    if (image.isNull()) return;
    const auto target_width = layer.value(QStringLiteral("pixelWidth")).toInt();
    const auto target_height = layer.value(QStringLiteral("pixelHeight")).toInt();
    if (target_width > 0 && target_height > 0 &&
        (image.width() != target_width || image.height() != target_height)) {
      image = image.scaled(target_width, target_height, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }
  } else {
    return;
  }
  if (kind == QStringLiteral("text")) {
    image = perspective_warp_text_layer(
        image,
        layer.value(QStringLiteral("textPerspectiveHorizontal"), 0.0).toDouble(),
        layer.value(QStringLiteral("textPerspectiveVertical"), 0.0).toDouble());
  }
  if (kind == QStringLiteral("pixel") && text_raster_size.isValid() && image.size() != text_raster_size)
    image = image.scaled(text_raster_size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
  if (!image.colorSpace().isValid()) {
    image.setColorSpace(preview_target_color_space());
  }
  // Keep camera-characterized and wide-gamut layer values at float32 precision;
  // a half-float intermediate here can differ from ordinary RAW rendering.
  const auto linear = converted_to_color_space(image, working_color_space(working_profile),
      QImage::Format_RGBA32FPx4);
  const auto working = working_image_from_qimage(linear, working_profile, nullptr, true);
  if (working.isNull()) {
    return;
  }
  target->pixels = working.pixels;
  target->pixel_width = working.width;
  target->pixel_height = working.height;
  target->x = layer.value(QStringLiteral("x")).toInt();
  target->y = layer.value(QStringLiteral("y")).toInt();
  target->transform_scale = qBound(0.05, layer.value(QStringLiteral("scale"), 1.0).toDouble(), 32.0);
  target->transform_rotation = qBound(-3600.0, layer.value(QStringLiteral("rotation")).toDouble(), 3600.0);
}

}  // namespace

QImage decode_photo_layer_source_image(const QString& path, int max_edge) {
  EditRecipeState source_state;
  QImage decoded;
  photo_editor::pipeline::DecodedImageMetadata metadata;
  if (!decode_source_image(path, &decoded, max_edge, source_state, &metadata)) return {};
  if (!photo_editor::pipeline::decoded_image_requires_engine_input_transform(metadata)) return decoded;

  // Camera RGB is not an ICC-tagged display image. Apply only the canonical
  // input prefix using THIS file's decode contract, never the document's camera.
  auto working = working_image_from_qimage(decoded, source_state.working_color_profile, nullptr, true);
  if (working.isNull()) return {};
  const auto config = compile_color_pipeline_config(source_state, &metadata);
  const auto state = compile_desktop_pipeline_state(source_state, &metadata, config,
      DesktopRenderPurpose::Output, LocalMaskRasterPolicy::UseStoredRaster,
      decoded.size(), working.pixels);
  auto prefix = photo_editor::pipeline::prepare_color_prefix_stage_context(
      working.pixels, working.width, working.height, state);
  if (!photo_editor::pipeline::apply_color_normalize_raw_stage(
          &working.pixels, working.width, working.height, state, &prefix) ||
      !photo_editor::pipeline::apply_color_camera_characterization_stage(
          &working.pixels, working.width, working.height, state, prefix)) return {};
  QImage characterized(working.width, working.height, QImage::Format_RGBA32FPx4);
  if (characterized.isNull()) return {};
  characterized.setColorSpace(working_color_space(source_state.working_color_profile));
  for (int y = 0; y < working.height; ++y) {
    auto* row = reinterpret_cast<QRgbaFloat32*>(characterized.scanLine(y));
    for (int x = 0; x < working.width; ++x) {
      const auto& pixel = working.pixels[static_cast<std::size_t>(y) *
          static_cast<std::size_t>(working.width) + static_cast<std::size_t>(x)];
      row[x] = QRgbaFloat32{pixel.red, pixel.green, pixel.blue, pixel.alpha};
    }
  }
  return characterized;
}

std::vector<photo_editor::pipeline::PhotoLayer> compile_pipeline_photo_layers(
    const QVariantList& layers, const QVariantList& masks,
    LocalMaskRasterPolicy raster_policy, const QSize& output_size,
    std::span<const photo_editor::pipeline::ColorPixel> source_pixels,
    const photo_editor::pipeline::ColorPipelineConfig& color_pipeline,
    DesktopRenderPurpose purpose) {
  const auto internal_raster_policy =
      raster_policy == LocalMaskRasterPolicy::RebuildEditableAtOutputResolution
      ? MaskRasterPolicy::RebuildEditableAtOutputResolution : MaskRasterPolicy::UseStoredRaster;
  std::vector<photo_editor::pipeline::PhotoLayer> photo_layers;
  photo_layers.reserve(static_cast<std::size_t>(layers.size()));
  auto solo_active = false;
  for (const auto& value : layers) {
    const auto layer = value.toMap();
    const auto kind = layer.value(QStringLiteral("kind")).toString().trimmed().toLower();
    if (kind != QStringLiteral("group") && layer.value(QStringLiteral("solo")).toBool()) {
      solo_active = true;
      break;
    }
  }
  for (const auto& value : layers) {
    const auto layer = value.toMap();
    const auto kind = layer.value(QStringLiteral("kind")).toString().trimmed().toLower();
    if (solo_active && !layer.value(QStringLiteral("baseImageEdit")).toBool() && kind != QStringLiteral("group") && !layer.value(QStringLiteral("solo")).toBool()) {
      continue;
    }
    auto photo_layer = photo_editor::pipeline::PhotoLayer{
        .erase_base_image = layer.value(QStringLiteral("baseImageEdit")).toBool(),
        .id = layer.value(QStringLiteral("id")).toString().toStdString(),
        .parent_id = layer.value(QStringLiteral("parentId")).toString().trimmed().toStdString(),
        .name = layer.value(QStringLiteral("name")).toString().toStdString(),
        .kind = (kind.isEmpty() ? QStringLiteral("adjustment") : kind).toStdString(),
        .blend_mode =
            normalized_mask_blend_mode(layer.value(QStringLiteral("blendMode")).toString()).toStdString(),
        .enabled = map_bool_value(layer, QStringLiteral("enabled"), true),
        .opacity = qBound(0.0, layer.value(QStringLiteral("opacity"), 1.0).toDouble(), 1.0),
        .clipping_mask = layer.value(QStringLiteral("clippingMask")).toBool(),
        .exposure = layer_adjustment_value(layer, QStringLiteral("exposure")),
        .contrast = layer_adjustment_value(layer, QStringLiteral("contrast")),
        .highlights = layer_adjustment_value(layer, QStringLiteral("highlights")),
        .shadows = layer_adjustment_value(layer, QStringLiteral("shadows")),
        .whites = layer_adjustment_value(layer, QStringLiteral("whites")),
        .blacks = layer_adjustment_value(layer, QStringLiteral("blacks")),
        .temperature = layer_adjustment_value(layer, QStringLiteral("temperature")),
        .tint = layer_adjustment_value(layer, QStringLiteral("tint")),
        .texture = layer_adjustment_value(layer, QStringLiteral("texture")),
        .clarity = layer_adjustment_value(layer, QStringLiteral("clarity")),
        .dehaze = layer_adjustment_value(layer, QStringLiteral("dehaze")),
        .sharpening = layer_adjustment_value(layer, QStringLiteral("sharpening")),
        .sharpen_radius = layer.contains(QStringLiteral("sharpenRadius")) ||
                                  layer.value(QStringLiteral("adjustments")).toMap().contains(
                                      QStringLiteral("sharpenRadius"))
                              ? layer_adjustment_value(layer, QStringLiteral("sharpenRadius"))
                              : 1.0,
        .sharpen_detail = layer_adjustment_value(layer, QStringLiteral("sharpenDetail")),
        .sharpen_masking = layer_adjustment_value(layer, QStringLiteral("sharpenMasking")),
        .luminance_nr = layer_adjustment_value(layer, QStringLiteral("luminanceNr")),
        .color_nr = layer_adjustment_value(layer, QStringLiteral("colorNr")),
        .lens_distortion = layer_adjustment_value(layer, QStringLiteral("lensDistortion")),
        .lens_vignette = layer_adjustment_value(layer, QStringLiteral("lensVignette")),
        .chromatic_aberration =
            layer_adjustment_value(layer, QStringLiteral("chromaticAberration")),
        .perspective_vertical = layer_adjustment_value(layer, QStringLiteral("perspectiveVertical")),
        .perspective_horizontal =
            layer_adjustment_value(layer, QStringLiteral("perspectiveHorizontal")),
        .global_transform_rotation =
            layer_adjustment_value(layer, QStringLiteral("transformRotation")),
        .global_transform_scale = layer.contains(QStringLiteral("transformScale")) ||
                                          layer.value(QStringLiteral("adjustments")).toMap().contains(
                                              QStringLiteral("transformScale"))
                                      ? layer_adjustment_value(layer, QStringLiteral("transformScale"))
                                      : 1.0,
        .transform_skew_x = layer_adjustment_value(layer, QStringLiteral("transformSkewX")),
        .transform_skew_y = layer_adjustment_value(layer, QStringLiteral("transformSkewY")),
        .liquify_center_x = layer.contains(QStringLiteral("liquifyCenterX")) ||
                                    layer.value(QStringLiteral("adjustments")).toMap().contains(
                                        QStringLiteral("liquifyCenterX"))
                                ? layer_adjustment_value(layer, QStringLiteral("liquifyCenterX"))
                                : 0.5,
        .liquify_center_y = layer.contains(QStringLiteral("liquifyCenterY")) ||
                                    layer.value(QStringLiteral("adjustments")).toMap().contains(
                                        QStringLiteral("liquifyCenterY"))
                                ? layer_adjustment_value(layer, QStringLiteral("liquifyCenterY"))
                                : 0.5,
        .liquify_radius = layer.contains(QStringLiteral("liquifyRadius")) ||
                                  layer.value(QStringLiteral("adjustments")).toMap().contains(
                                      QStringLiteral("liquifyRadius"))
                              ? layer_adjustment_value(layer, QStringLiteral("liquifyRadius"))
                              : 0.25,
        .liquify_strength = layer_adjustment_value(layer, QStringLiteral("liquifyStrength")),
        .content_aware_crop_fill =
            layer.value(QStringLiteral("adjustments")).toMap().contains(
                QStringLiteral("contentAwareCropFill"))
                ? layer.value(QStringLiteral("adjustments")).toMap()
                      .value(QStringLiteral("contentAwareCropFill"))
                      .toBool()
                : layer.value(QStringLiteral("contentAwareCropFill")).toBool(),
        .vibrance = layer_adjustment_value(layer, QStringLiteral("vibrance")),
        .saturation = layer_adjustment_value(layer, QStringLiteral("saturation")),
        .hue_shift = layer_adjustment_value(layer, QStringLiteral("hueShift")),
        .saturation_shift = layer_adjustment_value(layer, QStringLiteral("saturationShift")),
        .colorize_hue = layer_adjustment_value(layer, QStringLiteral("colorizeHue")),
        .colorize_strength = layer_adjustment_value(layer, QStringLiteral("colorizeStrength")),
        .brightness = layer_adjustment_value(layer, QStringLiteral("brightness")),
        .filmic_shoulder = layer_adjustment_value(layer, QStringLiteral("filmicShoulder")),
        .filmic_toe = layer_adjustment_value(layer, QStringLiteral("filmicToe")),
        .adaptive_contrast = layer_adjustment_value(layer, QStringLiteral("adaptiveContrast")),
        .levels_black_point = layer_adjustment_value(layer, QStringLiteral("levelsBlackPoint")),
        .levels_gamma = layer_adjustment_value(layer, QStringLiteral("levelsGamma")),
        .levels_white_point = layer_adjustment_value(layer, QStringLiteral("levelsWhitePoint")),
        .zone_deep_shadows = layer_adjustment_value(layer, QStringLiteral("zoneDeepShadows")),
        .zone_shadows = layer_adjustment_value(layer, QStringLiteral("zoneShadows")),
        .zone_midtones = layer_adjustment_value(layer, QStringLiteral("zoneMidtones")),
        .zone_lights = layer_adjustment_value(layer, QStringLiteral("zoneLights")),
        .zone_highlights = layer_adjustment_value(layer, QStringLiteral("zoneHighlights")),
        .curve_shadows = layer_adjustment_value(layer, QStringLiteral("curveShadows")),
        .curve_darks = layer_adjustment_value(layer, QStringLiteral("curveDarks")),
        .curve_lights = layer_adjustment_value(layer, QStringLiteral("curveLights")),
        .curve_highlights = layer_adjustment_value(layer, QStringLiteral("curveHighlights")),
        .master_curve_points =
            compile_pipeline_curve_points(layer_adjustment_curve_points(layer, QStringLiteral("masterCurvePoints"))),
        .luma_curve_points =
            compile_pipeline_curve_points(layer_adjustment_curve_points(layer, QStringLiteral("lumaCurvePoints"))),
        .red_curve_points =
            compile_pipeline_curve_points(layer_adjustment_curve_points(layer, QStringLiteral("redCurvePoints"))),
        .green_curve_points =
            compile_pipeline_curve_points(layer_adjustment_curve_points(layer, QStringLiteral("greenCurvePoints"))),
        .blue_curve_points =
            compile_pipeline_curve_points(layer_adjustment_curve_points(layer, QStringLiteral("blueCurvePoints"))),
        .color_mixer_model = layer_nested_adjustment_string(
            layer,
            QStringLiteral("colorMixer"),
            QStringLiteral("model"),
            QStringLiteral("colorMixerModel"),
            QStringLiteral("OKLab")).toStdString(),
        .color_mixer_channel = layer_nested_adjustment_string(
            layer,
            QStringLiteral("colorMixer"),
            QStringLiteral("channel"),
            QStringLiteral("hslChannel"),
            QStringLiteral("Reds")).toStdString(),
        .hue_adjustment = layer_nested_adjustment_value(
            layer, QStringLiteral("colorMixer"), QStringLiteral("hue"), QStringLiteral("hueAdjustment")),
        .saturation_adjustment = layer_nested_adjustment_value(
            layer, QStringLiteral("colorMixer"), QStringLiteral("saturation"), QStringLiteral("saturationAdjustment")),
        .luminance_adjustment = layer_nested_adjustment_value(
            layer, QStringLiteral("colorMixer"), QStringLiteral("luminance"), QStringLiteral("luminanceAdjustment")),
        .color_mixer_channels = layer_color_mixer_channels(layer),
        .rgb_channel_mixer = layer_rgb_channel_mixer(layer),
        .black_and_white_enabled = layer_color_mixer_child(
            layer, QStringLiteral("blackAndWhite")).value(QStringLiteral("enabled")).toBool(),
        .black_and_white_mixer = layer_black_and_white_mixer(layer),
        .color_replacement_enabled = layer_color_mixer_child(
            layer, QStringLiteral("colorReplacement")).value(QStringLiteral("enabled")).toBool(),
        .color_replacement_source_hue = layer_color_mixer_child(
            layer, QStringLiteral("colorReplacement")).value(QStringLiteral("sourceHue")).toDouble(),
        .color_replacement_target_hue = layer_object_double(layer_color_mixer_child(
            layer, QStringLiteral("colorReplacement")), QStringLiteral("targetHue"), 0.33),
        .color_replacement_range = layer_object_double(layer_color_mixer_child(
            layer, QStringLiteral("colorReplacement")), QStringLiteral("range"), 35.0),
        .color_replacement_softness = layer_object_double(layer_color_mixer_child(
            layer, QStringLiteral("colorReplacement")), QStringLiteral("softness"), 100.0),
        .color_replacement_strength = layer_color_mixer_child(
            layer, QStringLiteral("colorReplacement")).value(QStringLiteral("strength")).toDouble(),
        .color_replacement_preserve_luminance = layer_object_bool(layer_color_mixer_child(
            layer, QStringLiteral("colorReplacement")), QStringLiteral("preserveLuminance"), true),
        .color_replacement_preserve_saturation = layer_object_bool(layer_color_mixer_child(
            layer, QStringLiteral("colorReplacement")), QStringLiteral("preserveSaturation"), true),
        .skin_tone_protection_enabled = layer_color_mixer_child(
            layer, QStringLiteral("skinToneProtection")).value(QStringLiteral("enabled")).toBool(),
        .skin_tone_line_hue = layer_object_double(layer_color_mixer_child(
            layer, QStringLiteral("skinToneProtection")), QStringLiteral("lineHue"), 0.08),
        .skin_tone_protection_range = layer_object_double(layer_color_mixer_child(
            layer, QStringLiteral("skinToneProtection")), QStringLiteral("range"), 30.0),
        .skin_tone_protection_strength = layer_color_mixer_child(
            layer, QStringLiteral("skinToneProtection")).value(QStringLiteral("strength")).toDouble(),
        .color_harmony_enabled = layer_color_mixer_child(
            layer, QStringLiteral("colorHarmony")).value(QStringLiteral("enabled")).toBool(),
        .color_harmony_mode = layer_object_string(layer_color_mixer_child(
            layer, QStringLiteral("colorHarmony")), QStringLiteral("mode"), QStringLiteral("Complementary")).toStdString(),
        .color_harmony_base_hue = layer_color_mixer_child(
            layer, QStringLiteral("colorHarmony")).value(QStringLiteral("baseHue")).toDouble(),
        .color_harmony_strength = layer_color_mixer_child(
            layer, QStringLiteral("colorHarmony")).value(QStringLiteral("strength")).toDouble(),
        .grading_shadows_hue = layer_color_grading_child(
            layer, QStringLiteral("shadows")).value(QStringLiteral("hue")).toDouble(),
        .grading_shadows_saturation = layer_color_grading_child(
            layer, QStringLiteral("shadows")).value(QStringLiteral("saturation")).toDouble(),
        .grading_shadows_luminance = layer_color_grading_child(
            layer, QStringLiteral("shadows")).value(QStringLiteral("luminance")).toDouble(),
        .grading_midtones_hue = layer_color_grading_child(
            layer, QStringLiteral("midtones")).value(QStringLiteral("hue")).toDouble(),
        .grading_midtones_saturation = layer_color_grading_child(
            layer, QStringLiteral("midtones")).value(QStringLiteral("saturation")).toDouble(),
        .grading_midtones_luminance = layer_color_grading_child(
            layer, QStringLiteral("midtones")).value(QStringLiteral("luminance")).toDouble(),
        .grading_highlights_hue = layer_color_grading_child(
            layer, QStringLiteral("highlights")).value(QStringLiteral("hue")).toDouble(),
        .grading_highlights_saturation = layer_color_grading_child(
            layer, QStringLiteral("highlights")).value(QStringLiteral("saturation")).toDouble(),
        .grading_highlights_luminance = layer_color_grading_child(
            layer, QStringLiteral("highlights")).value(QStringLiteral("luminance")).toDouble(),
        .grading_amount = layer_object_double(layer_adjustment_object(
            layer, QStringLiteral("colorGrading")), QStringLiteral("amount"), 100.0),
        .grading_balance = layer_adjustment_object(
            layer, QStringLiteral("colorGrading")).value(QStringLiteral("balance")).toDouble(),
        .lut_stack = layer_lut_stack(layer),
        .fill_color = layer_fill_color(layer, QString::fromStdString(color_pipeline.working_profile.profile_name)),
    };
    if (kind == QStringLiteral("text")) {
      const auto typography = layer.value(QStringLiteral("textTypography")).toMap();
      photo_layer.text_material = typography.value(QStringLiteral("material")).toString().toStdString();
      if (!photo_layer.text_material.empty() && photo_layer.text_material != "none") {
        photo_layer.text_material_tint = layer_fill_color(
            {{QStringLiteral("fillColor"), typography.value(QStringLiteral("materialTint"), QStringLiteral("#ff9eb8c8"))}},
            QString::fromStdString(color_pipeline.working_profile.profile_name));
        photo_layer.text_material_blur = qBound(0.0, typography.value(QStringLiteral("materialBlur"), 6.0).toDouble(), 64.0);
        photo_layer.text_material_transmission = qBound(0.0,
            typography.value(QStringLiteral("materialTransmission"), 0.68).toDouble(), 1.0);
        photo_layer.text_material_edge_softness = qBound(0.0,
            typography.value(QStringLiteral("materialEdgeSoftness"), 1.0).toDouble(), 16.0);
        photo_layer.text_material_depth = qBound(0.0,
            typography.value(QStringLiteral("materialDepth"), 4.0).toDouble(), 32.0);
        photo_layer.text_material_light_angle = qBound(-180.0,
            typography.value(QStringLiteral("materialLightAngle"), -45.0).toDouble(), 180.0);
      }
    }
    photo_layer.copy_base_image = layer.value(QStringLiteral("copyBaseImage")).toBool();
    populate_photo_layer_mask(&photo_layer, layer);
    if (layer.value(QStringLiteral("selectionScoped")).toBool()) {
      const auto selection_mask = compile_photo_layer_selection_mask(layer, masks,
          internal_raster_policy, output_size, source_pixels, color_pipeline, purpose,
          photo_layer.mask_data.empty());
      if (!selection_mask.mask_data.empty()) {
        if (photo_layer.mask_data.empty()) {
          photo_layer.mask_data = selection_mask.mask_data;
          photo_layer.mask_width = selection_mask.mask_width;
          photo_layer.mask_height = selection_mask.mask_height;
          photo_layer.mask_inverted = selection_mask.mask_inverted;
          photo_layer.mask_opacity = selection_mask.mask_opacity;
          photo_layer.mask_density = selection_mask.mask_density;
        } else {
          intersect_photo_layer_masks(&photo_layer, selection_mask);
        }
      }
    }
    auto raster_layer = layer;
    QSize text_raster_size;
    const auto canvas_width = layer.value(QStringLiteral("canvasWidth")).toInt();
    const auto canvas_height = layer.value(QStringLiteral("canvasHeight")).toInt();
    if (kind == QStringLiteral("text") && output_size.isValid() && canvas_width > 0 && canvas_height > 0) {
      const auto sx = static_cast<double>(output_size.width()) / canvas_width;
      const auto sy = static_cast<double>(output_size.height()) / canvas_height;
      if (!photo_layer.text_material.empty() && photo_layer.text_material != "none") {
        const auto material_scale = std::sqrt(sx * sy);
        photo_layer.text_material_blur *= material_scale;
        photo_layer.text_material_edge_softness *= material_scale;
        photo_layer.text_material_depth *= material_scale;
      }
      const auto scaled_width = qBound(16, layer.value("pixelWidth", 512).toInt(), 8192) * sx;
      const auto scaled_height = qBound(16, layer.value("pixelHeight", 128).toInt(), 8192) * sy;
      if (scaled_width <= 16384 && scaled_height <= 16384) {
        text_raster_size = QSize(qMax(1, static_cast<int>(std::lround(scaled_width))),
            qMax(1, static_cast<int>(std::lround(scaled_height))));
        raster_layer.insert("x", static_cast<int>(std::clamp(std::round(layer.value("x").toInt() * sx),
            static_cast<double>(std::numeric_limits<int>::min()), static_cast<double>(std::numeric_limits<int>::max()))));
        raster_layer.insert("y", static_cast<int>(std::clamp(std::round(layer.value("y").toInt() * sy),
            static_cast<double>(std::numeric_limits<int>::min()), static_cast<double>(std::numeric_limits<int>::max()))));
      } else {
        photo_layer.content_error = "Text raster dimensions exceed the supported size or memory budget.";
      }
    }
    if (kind == QStringLiteral("pixel") && output_size.isValid() && canvas_width > 0 && canvas_height > 0) {
      const auto sx = static_cast<double>(output_size.width()) / canvas_width;
      const auto sy = static_cast<double>(output_size.height()) / canvas_height;
      text_raster_size = QSize(qMax(1, qRound(layer.value("pixelWidth").toInt() * sx)),
          qMax(1, qRound(layer.value("pixelHeight").toInt() * sy)));
      raster_layer.insert("x", qRound(layer.value("x").toInt() * sx));
      raster_layer.insert("y", qRound(layer.value("y").toInt() * sy));
    }
    if (kind == QStringLiteral("source") && output_size.isValid() &&
        canvas_width > 0 && canvas_height > 0) {
      const auto scale_x = static_cast<double>(output_size.width()) / canvas_width;
      const auto scale_y = static_cast<double>(output_size.height()) / canvas_height;
      raster_layer.insert("x", static_cast<int>(std::lround(layer.value("x").toInt() * scale_x)));
      raster_layer.insert("y", static_cast<int>(std::lround(layer.value("y").toInt() * scale_y)));
      const auto width = layer.value("pixelWidth").toInt();
      const auto height = layer.value("pixelHeight").toInt();
      if (width > 0 && height > 0) {
        raster_layer.insert("pixelWidth", qMax(1, static_cast<int>(std::lround(width * scale_x))));
        raster_layer.insert("pixelHeight", qMax(1, static_cast<int>(std::lround(height * scale_y))));
      }
    }
    if (photo_layer.copy_base_image) {
      photo_layer.x = raster_layer.value("x").toInt();
      photo_layer.y = raster_layer.value("y").toInt();
      photo_layer.transform_scale = qBound(0.05, layer.value("scale", 1.0).toDouble(), 32.0);
      photo_layer.transform_rotation = qBound(-3600.0, layer.value("rotation").toDouble(), 3600.0);
      // The pixels are resolved from the developed base by the layer stage.
      photo_layer.pixel_width = output_size.width();
      photo_layer.pixel_height = output_size.height();
    } else if (photo_layer.enabled && photo_layer.opacity > 0.0 && photo_layer.content_error.empty()) {
      populate_photo_layer_pixels(&photo_layer, raster_layer, QString::fromStdString(color_pipeline.working_profile.profile_name), text_raster_size);
    }
    if (kind == QStringLiteral("group")) {
      const auto source_width = canvas_width > 0 ? canvas_width : output_size.width();
      const auto source_height = canvas_height > 0 ? canvas_height : output_size.height();
      const auto scale_x = source_width > 0 && output_size.width() > 0
          ? static_cast<double>(output_size.width()) / source_width : 1.0;
      const auto scale_y = source_height > 0 && output_size.height() > 0
          ? static_cast<double>(output_size.height()) / source_height : 1.0;
      const auto scaled_offset = [](int offset, double scale) {
        const auto value = std::round(static_cast<double>(offset) * scale);
        return static_cast<int>(std::clamp(
            value,
            static_cast<double>(std::numeric_limits<int>::lowest()),
            static_cast<double>(std::numeric_limits<int>::max())));
      };
      photo_layer.x = scaled_offset(layer.value(QStringLiteral("x")).toInt(), scale_x);
      photo_layer.y = scaled_offset(layer.value(QStringLiteral("y")).toInt(), scale_y);
      photo_layer.transform_scale = qBound(0.05, layer.value(QStringLiteral("scale"), 1.0).toDouble(), 32.0);
      photo_layer.transform_rotation = qBound(-3600.0, layer.value(QStringLiteral("rotation")).toDouble(), 3600.0);
    }
    QString effect_error;
    if (auto effects = decode_photo_layer_effects(layer, &effect_error)) {
      photo_layer.effects = std::move(*effects);
      if (!bind_photo_effect_maps(&photo_layer.effects, layer, &effect_error))
        photo_layer.effect_error = effect_error.toStdString();
    } else {
      photo_layer.effect_error = effect_error.toStdString();
    }
    if (!photo_layer.effects.empty() && output_size.isValid()) {
      photo_layer.effect_scale_x = canvas_width > 0 ? static_cast<double>(output_size.width()) / canvas_width : 1;
      photo_layer.effect_scale_y = canvas_height > 0 ? static_cast<double>(output_size.height()) / canvas_height : 1;
      for (const auto& effect : photo_layer.effects) {
        if (!effect.enabled || effect.amount == 0 || effect.mask_ref.empty() ||
            std::any_of(photo_layer.effect_masks.begin(), photo_layer.effect_masks.end(),
                [&](const auto& mask) { return mask.id == effect.mask_ref; })) continue;
        photo_editor::pipeline::PhotoLayer mask_layer;
        const QVariantMap mask_target{{QStringLiteral("selectionScoped"), true},
            {QStringLiteral("selectionMaskSetId"), QString::fromStdString(effect.mask_ref)}};
        populate_photo_layer_selection_mask(&mask_layer, mask_target, masks, internal_raster_policy,
            output_size, source_pixels, color_pipeline, purpose);
        if (mask_layer.mask_data.empty() || mask_layer.mask_width != output_size.width() ||
            mask_layer.mask_height != output_size.height()) {
          photo_layer.effect_error = "Could not resolve photo effect mask '" + effect.mask_ref + "'.";
          break;
        }
        const bool raster = photo_layer.kind == "source" || photo_layer.kind == "pixel" || photo_layer.kind == "text";
        const int mask_width = raster ? photo_layer.pixel_width : output_size.width();
        const int mask_height = raster ? photo_layer.pixel_height : output_size.height();
        if (mask_width <= 0 || mask_height <= 0) {
          photo_layer.effect_error = "Photo effect mask target has no content dimensions.";
          break;
        }
        photo_editor::pipeline::PhotoEffectMaskRaster resolved;
        resolved.id = effect.mask_ref;
        resolved.coverage.resize(static_cast<std::size_t>(mask_width) * static_cast<std::size_t>(mask_height));
        const auto radians = photo_layer.transform_rotation * std::acos(-1.0) / 180.0;
        const auto scale = photo_layer.transform_scale;
        for (int y = 0; y < mask_height; ++y) {
          for (int x = 0; x < mask_width; ++x) {
            double doc_x = x, doc_y = y;
            if (raster) {
              const auto dx = (x + 0.5 - mask_width * 0.5) * scale;
              const auto dy = (y + 0.5 - mask_height * 0.5) * scale;
              doc_x = photo_layer.x + mask_width * scale * 0.5 + dx * std::cos(radians) - dy * std::sin(radians) - 0.5;
              doc_y = photo_layer.y + mask_height * scale * 0.5 + dx * std::sin(radians) + dy * std::cos(radians) - 0.5;
            }
            const auto x0 = static_cast<int>(std::floor(doc_x)), y0 = static_cast<int>(std::floor(doc_y));
            const auto sample = [&](int sx, int sy) {
              if (sx < 0 || sy < 0 || sx >= output_size.width() || sy >= output_size.height()) return 0.0;
              return mask_layer.mask_data[static_cast<std::size_t>(sy) * static_cast<std::size_t>(output_size.width()) +
                  static_cast<std::size_t>(sx)] / 255.0;
            };
            resolved.coverage[static_cast<std::size_t>(y) * static_cast<std::size_t>(mask_width) + static_cast<std::size_t>(x)] =
                static_cast<float>(std::lerp(std::lerp(sample(x0, y0), sample(x0 + 1, y0), doc_x - x0),
                    std::lerp(sample(x0, y0 + 1), sample(x0 + 1, y0 + 1), doc_x - x0), doc_y - y0));
          }
        }
        photo_layer.effect_masks.push_back(std::move(resolved));
      }
    }
    photo_layers.push_back(std::move(photo_layer));
  }
  // Desktop rows are top first; the engine accumulates the bottom first.
  std::reverse(photo_layers.begin(), photo_layers.end());
  return photo_layers;
}
namespace {

photo_editor::pipeline::ColorAdjustmentState compile_export_pipeline_state(
    const RenderEditState& state,
    const photo_editor::pipeline::DecodedImageMetadata* metadata = nullptr,
    MaskRasterPolicy raster_policy = MaskRasterPolicy::UseStoredRaster,
    const QSize& output_size = {},
    std::span<const photo_editor::pipeline::ColorPixel> source_pixels = {},
    const photo_editor::pipeline::DisplayTarget* export_display_target_override = nullptr) {
  auto color_pipeline_config = compile_color_pipeline_config(state, metadata);
  if (export_display_target_override != nullptr) {
    color_pipeline_config.export_display_target = *export_display_target_override;
    color_pipeline_config.export_output_profile =
        photo_editor::pipeline::output_color_profile_from_name("Rec.2020");
  }
  return compile_desktop_pipeline_state(
      state, metadata, color_pipeline_config,
      DesktopRenderPurpose::Output, raster_policy, output_size,
      source_pixels);
}

#if defined(PHOTO_EDITOR_HAS_METAL_BACKEND) && PHOTO_EDITOR_HAS_METAL_BACKEND
photo_editor::pipeline::metal::MetalPipelineBackend& metal_backend() {
  return photo_editor::pipeline::metal::
      shared_metal_pipeline_backend();
}
#endif
}  // namespace
bool source_image_supported(const QString& file_path) { return preview_supported(file_path); }
photo_editor::pipeline::LocalMask compile_pipeline_local_mask(
    const QVariantMap& mask,
    LocalMaskRasterPolicy raster_policy,
    const QSize& output_size) {
  return pipeline_local_mask(
      mask,
      raster_policy == LocalMaskRasterPolicy::RebuildEditableAtOutputResolution
          ? MaskRasterPolicy::RebuildEditableAtOutputResolution
          : MaskRasterPolicy::UseStoredRaster,
      output_size);
}
QImage render_canonical_thumbnail_image(
    const QImage& image,
    const EditRecipeState& state,
    const QColorSpace& target_color_space,
    const photo_editor::pipeline::RenderPlan* canonical_plan) {
  return render_output_image(
      image,
      state,
      target_color_space,
      QImage::Format_ARGB32,
      nullptr,
      nullptr,
      {},
      photo_editor::pipeline::RemovalQuality::Final,
      RenderOutputPolicy::DisplayReferred,
      canonical_plan);
}

QString render_backend_unavailable_reason(RenderBackendPreference backend_preference) {
  switch (backend_preference) {
    case RenderBackendPreference::Auto:
    case RenderBackendPreference::CpuOnly:
      return {};
    case RenderBackendPreference::GpuOnly:
#if defined(PHOTO_EDITOR_HAS_METAL_BACKEND) && PHOTO_EDITOR_HAS_METAL_BACKEND
      return metal_backend().available()
                 ? QString{}
                 : QString::fromStdString(metal_backend().unavailable_reason());
#else
      return QStringLiteral("metal_not_compiled");
#endif
  }
  return QStringLiteral("unknown_backend_preference");
}

bool preview_supported(const QString& file_path) {
  const QFileInfo file_info(file_path);
  const auto suffix = file_info.suffix().toLower().toUtf8();
  return QImageReader::supportedImageFormats().contains(suffix) ||
         openexr_image_supported(file_path) || raw_image_supported(file_path) ||
         native_image_supported(file_path);
}

bool render_backend_available(RenderBackendPreference backend_preference) {
  switch (backend_preference) {
    case RenderBackendPreference::Auto:
    case RenderBackendPreference::CpuOnly:
      return true;
    case RenderBackendPreference::GpuOnly:
#if defined(PHOTO_EDITOR_HAS_METAL_BACKEND) && PHOTO_EDITOR_HAS_METAL_BACKEND
      return metal_backend().available();
#else
      return false;
#endif
  }
  return false;
}

GpuPipelineSupport evaluate_gpu_pipeline_support(
    const QImage& image,
    const RenderEditState& state,
    const photo_editor::pipeline::DecodedImageMetadata* metadata,
    RenderMaskOptions mask_options,
    bool assume_metal_available) {
  if (image.isNull()) {
    return GpuPipelineSupport{
        .supported = false,
        .metal_aware_cpu_engine = false,
        .fallback_reason = QStringLiteral("empty_source"),
    };
  }

  const auto requested_input_profile =
      photo_editor::pipeline::input_color_profile_from_name(
          state.input_color_profile.toStdString());
  const auto dcp_profile_asset =
      resolve_dcp_profile_asset(state.camera_dcp_profile_asset);
  const auto input_profile_asset = resolve_icc_profile_asset(state.input_icc_profile_asset);
  if (requested_input_profile.kind ==
      photo_editor::pipeline::InputTransformKind::CameraDcpProfile) {
    const auto camera_linear_boundary =
        metadata != nullptr &&
        photo_editor::pipeline::decoded_image_requires_engine_input_transform(*metadata);
    if (!camera_linear_boundary || !dcp_profile_asset.ready()) {
      return GpuPipelineSupport{
          .supported = false,
          .metal_aware_cpu_engine = false,
          .fallback_reason =
              !camera_linear_boundary
                  ? QStringLiteral("dcp_profile_requires_camera_linear_raw")
                  : QString::fromStdString(
                        dcp_profile_asset.failure_reason.empty()
                            ? "dcp_profile_asset_required"
                            : dcp_profile_asset.failure_reason),
      };
    }
  }
  if (requested_input_profile.kind ==
      photo_editor::pipeline::InputTransformKind::CameraIccProfile) {
    const auto camera_linear_boundary =
        metadata != nullptr &&
        photo_editor::pipeline::decoded_image_requires_engine_input_transform(*metadata);
    if (!camera_linear_boundary || !input_profile_asset.ready()) {
      return GpuPipelineSupport{
          .supported = false,
          .metal_aware_cpu_engine = false,
          .fallback_reason =
              !camera_linear_boundary
                  ? QStringLiteral("camera_icc_profile_requires_camera_linear_raw")
                  : QString::fromStdString(
                        input_profile_asset.failure_reason.empty()
                            ? "camera_icc_profile_asset_required"
                            : input_profile_asset.failure_reason),
      };
    }
  }

  if (metadata != nullptr && state.input_icc_profile_asset.configured() &&
      !photo_editor::pipeline::decoded_image_accepts_custom_input_profile(*metadata)) {
    return GpuPipelineSupport{
        .supported = false,
        .metal_aware_cpu_engine = false,
        .fallback_reason = QStringLiteral(
            "icc_input_profile_requires_camera_linear_raw"),
    };
  }

  const auto working = working_image_from_qimage(
      image,
      state.working_color_profile,
      &input_profile_asset,
      metadata != nullptr &&
          photo_editor::pipeline::decoded_image_requires_engine_input_transform(
              *metadata));
  if (working.isNull()) {
    return GpuPipelineSupport{
        .supported = false,
        .metal_aware_cpu_engine = false,
        .fallback_reason =
            input_profile_asset.status == photo_editor::pipeline::IccProfileAssetStatus::Unconfigured
                ? QStringLiteral("linearization_failed")
                : QString::fromStdString(
                      input_profile_asset.failure_reason.empty()
                          ? "icc_input_profile_transform_failed"
                          : input_profile_asset.failure_reason),
    };
  }

  const auto raster_policy = mask_options.rebuild_editable_masks_at_output_resolution
                                 ? MaskRasterPolicy::RebuildEditableAtOutputResolution
                                 : MaskRasterPolicy::UseStoredRaster;
  auto pipeline_state =
      compile_export_pipeline_state(
          state,
          metadata,
          raster_policy,
          QSize(working.width, working.height),
          working.pixels);
  if (const auto error = compiled_photo_layer_source_error(pipeline_state.photo_layers);
      !error.isEmpty()) {
    return GpuPipelineSupport{.supported = false, .metal_aware_cpu_engine = false,
        .fallback_reason = error};
  }
  const auto reason = gpu_fallback_reason(
      state,
      pipeline_state,
      working.pixels,
      assume_metal_available);
  return GpuPipelineSupport{
      .supported = reason.isEmpty(),
      .metal_aware_cpu_engine =
          reason ==
          unsupported_stage_reason(
              QStringLiteral("Layers"),
              QStringLiteral(
                  "local_mask_sets")),
      .fallback_reason = reason,
  };
}

QString source_image_decode_cache_key(
    const QFileInfo& source_info,
    int max_edge,
    bool openexr_supported,
    bool raw_supported,
    const RenderEditState& state) {
  auto key = decoded_image_cache_key(source_info, max_edge);
  if (openexr_supported || raw_supported) {
    key += openexr_supported
               ? QStringLiteral("|") + QString::fromLatin1(kOpenExrSourceColorContract) + QStringLiteral("|") +
                     QString::fromStdString(std::string(
                         photo_editor::io::openexr_backend_version()))
               : QStringLiteral("|raw|") +
                     raw_decode_settings_cache_key(
                         raw_decode_settings_for_state(state));
  }
  return key +
         QStringLiteral("|publication=source-linear-v2-camera-characterized-in-engine");
}

bool decode_source_image(
    const QString& file_path,
    QImage* image,
    int max_edge,
    const RenderEditState& state,
    photo_editor::pipeline::DecodedImageMetadata* metadata,
    photo_editor::pipeline::CancellationToken cancellation,
    SourceDecodeTelemetry* telemetry) {
  if (image == nullptr || cancellation.cancelled() ||
      !preview_supported(file_path)) {
    return false;
  }
  if (metadata != nullptr) {
    *metadata = {};
  }

  const QFileInfo source_info(file_path);
  if (!source_info.exists() || !source_info.isReadable()) {
    return false;
  }

  const auto native_supported = native_image_supported(file_path);
  const auto openexr_supported = openexr_image_supported(file_path);
  const auto raw_supported = raw_image_supported(file_path);
  const auto should_cache = max_edge >= 0;
  const auto publish_cache_telemetry = [telemetry](
                                           const DecodedImageCacheSnapshot& snapshot) {
    if (telemetry == nullptr) {
      return;
    }
    telemetry->cache_entries = snapshot.entries;
    telemetry->cache_bytes = snapshot.bytes;
  };
  if (telemetry != nullptr) {
    *telemetry = SourceDecodeTelemetry{
        .cache_lookup = should_cache,
        .cache_hit = false,
        .backend = native_supported
                       ? QStringLiteral("native")
                       : openexr_supported
                             ? QStringLiteral("openexr")
                             : raw_supported ? QStringLiteral("raw")
                                             : QStringLiteral("qt"),
    };
    publish_cache_telemetry(decoded_image_cache_snapshot());
  }
  const auto cache_key = should_cache
                             ? source_image_decode_cache_key(
                                   source_info,
                                   max_edge,
                                   openexr_supported,
                                   raw_supported,
                                   state)
                             : QString{};
  if (should_cache) {
    const auto raw_decode = raw_supported;
    auto cache_snapshot = DecodedImageCacheSnapshot{};
    const auto cache_hit = raw_decode && metadata != nullptr
                               ? find_decoded_image_cache_entry_with_metadata(
                                     cache_key, image, metadata, &cache_snapshot)
                               : find_decoded_image_cache_entry(
                                     cache_key, image, &cache_snapshot);
    publish_cache_telemetry(cache_snapshot);
    if (cache_hit) {
      if (telemetry != nullptr) {
        telemetry->cache_hit = true;
      }
      return !cancellation.cancelled() && !image->isNull();
    }
  }

  if (native_supported) {
    auto decoded = QImage{};
    if (!decode_native_linear_image(
            file_path, &decoded, max_edge, cancellation)) {
      return false;
    }

    if (cancellation.cancelled()) {
      return false;
    }
    *image = normalize_preview_color_space(std::move(decoded));
    if (should_cache && !image->isNull() && !cancellation.cancelled()) {
      auto cache_snapshot = DecodedImageCacheSnapshot{};
      insert_decoded_image_cache_entry(cache_key, *image, &cache_snapshot);
      publish_cache_telemetry(cache_snapshot);
    }
    return !cancellation.cancelled() && !image->isNull();
  }

  if (openexr_supported) {
    *image = decode_openexr_image(file_path, max_edge, cancellation);
    if (cancellation.cancelled()) {
      return false;
    }
    if (should_cache && !image->isNull()) {
      auto cache_snapshot = DecodedImageCacheSnapshot{};
      insert_decoded_image_cache_entry(cache_key, *image, &cache_snapshot);
      publish_cache_telemetry(cache_snapshot);
    }
    return !image->isNull();
  }

  if (raw_supported) {
    auto decoded = QImage{};
    if (!decode_raw_linear_image(
            file_path,
            &decoded,
            max_edge,
            raw_decode_settings_for_state(state),
            metadata,
            cancellation)) {
      return false;
    }

    // RAW decode metadata owns the camera-color interpretation. In
    // particular, camera-linear samples must remain untagged until the engine
    // applies the recorded camera characterization; assigning the generic
    // preview sRGB tag here would falsely label camera RGB as display RGB.
    *image = std::move(decoded);
    if (should_cache && !image->isNull() && !cancellation.cancelled()) {
      auto cache_snapshot = DecodedImageCacheSnapshot{};
      if (metadata != nullptr) {
        insert_decoded_image_cache_entry_with_metadata(
            cache_key, *image, *metadata, &cache_snapshot);
      } else {
        insert_decoded_image_cache_entry(cache_key, *image, &cache_snapshot);
      }
      publish_cache_telemetry(cache_snapshot);
    }
    return !cancellation.cancelled() && !image->isNull();
  }

  QImageReader reader(file_path);
  reader.setAutoTransform(true);
  if (!reader.canRead()) {
    return false;
  }

  if (max_edge > 0) {
    const auto source_size = reader.size();
    if (source_size.isValid() &&
        (source_size.width() > max_edge ||
         source_size.height() > max_edge)) {
      reader.setScaledSize(source_size.scaled(max_edge, max_edge, Qt::KeepAspectRatio));
    }
  }

  *image = normalize_preview_color_space(reader.read());
  if (cancellation.cancelled()) {
    return false;
  }
  if (should_cache && !image->isNull()) {
    auto cache_snapshot = DecodedImageCacheSnapshot{};
    insert_decoded_image_cache_entry(cache_key, *image, &cache_snapshot);
    publish_cache_telemetry(cache_snapshot);
  }

  return !image->isNull();
}

bool prewarm_source_image_decode_pyramid(
    const QString& file_path,
    const RenderEditState& state,
    int settled_edge,
    int draft_edge,
    photo_editor::pipeline::CancellationToken cancellation) {
  if (!raw_image_supported(file_path) || settled_edge <= 0 || draft_edge <= 0 ||
      draft_edge > settled_edge || cancellation.cancelled()) {
    return false;
  }

  auto settled = QImage{};
  auto metadata = photo_editor::pipeline::DecodedImageMetadata{};
  if (!decode_source_image(
          file_path,
          &settled,
          settled_edge,
          state,
          &metadata,
          cancellation) ||
      settled.isNull() || cancellation.cancelled()) {
    return false;
  }

  if (std::max(settled.width(), settled.height()) <= draft_edge) {
    return true;
  }
  auto draft = settled.scaled(
      draft_edge,
      draft_edge,
      Qt::KeepAspectRatio,
      Qt::SmoothTransformation);
  if (draft.isNull() || cancellation.cancelled()) {
    return false;
  }

  const auto source_info = QFileInfo(file_path);
  const auto draft_key = source_image_decode_cache_key(
      source_info,
      draft_edge,
      false,
      true,
      state);
  insert_decoded_image_cache_entry_with_metadata(
      draft_key,
      draft,
      metadata);
  return !cancellation.cancelled();
}

bool find_source_image_decode_cache(
    const QString& file_path, const RenderEditState& state, int max_edge, QImage* image) {
  if (!image || max_edge < 0) return false;
  const QFileInfo source(file_path);
  if (!source.exists() || !source.isReadable()) return false;
  return find_decoded_image_cache_entry(
      source_image_decode_cache_key(source, max_edge,
                                    openexr_image_supported(file_path),
                                    raw_image_supported(file_path), state), image);
}

bool source_image_decode_cache_contains(
    const QString& file_path,
    const RenderEditState& state,
    int max_edge) {
  if (!raw_image_supported(file_path) || max_edge <= 0) {
    return false;
  }
  const auto source_info = QFileInfo(file_path);
  if (!source_info.exists() || !source_info.isReadable()) {
    return false;
  }
  const auto key = source_image_decode_cache_key(
      source_info,
      max_edge,
      false,
      true,
      state);
  auto image = QImage{};
  auto metadata = photo_editor::pipeline::DecodedImageMetadata{};
  return find_decoded_image_cache_entry_with_metadata(
      key,
      &image,
      &metadata);
}

QSize source_image_size(const QString& file_path) {
  if (!preview_supported(file_path)) {
    return {};
  }

  // QImageReader may surface the embedded JPEG in a RAW container. Prefer the
  // RAW decoder's sensor dimensions so full-resolution exports and previews
  // cannot silently inherit proxy geometry.
  if (raw_image_supported(file_path)) {
    const auto raw_size = raw_image_size(file_path);
    if (raw_size.isValid()) {
      return raw_size;
    }
  }

  if (native_image_supported(file_path)) {
    return native_image_size(file_path);
  }

  if (openexr_image_supported(file_path)) {
    const auto inspection = photo_editor::io::inspect_openexr(
        file_path.toStdString(),
        false);
    return inspection.valid ? QSize(inspection.width, inspection.height) : QSize{};
  }

  QImageReader reader(file_path);
  if (reader.canRead()) {
    return reader.size();
  }

  return {};
}

QImage render_output_image(
    const QImage& image,
    const RenderEditState& state,
    const QColorSpace& target_color_space,
    QImage::Format output_format,
    const photo_editor::pipeline::DecodedImageMetadata* metadata,
    RenderBackendTelemetry* telemetry,
    RenderMaskOptions mask_options,
    photo_editor::pipeline::RemovalQuality removal_quality,
    RenderOutputPolicy output_policy,
    const photo_editor::pipeline::RenderPlan*
        canonical_plan,
    photo_editor::pipeline::CancellationToken cancellation) {
  if (canonical_plan == nullptr &&
      normalized_photographic_rendering_model_name(
          state.photographic_rendering_model) ==
          QStringLiteral("aces2-ocio-v1")) {
    if (telemetry != nullptr) {
      telemetry->backend = QStringLiteral("none");
      telemetry->fallback_reason =
          QStringLiteral("canonical_plan_required_for_aces2");
      telemetry->canonical_plan_error =
          QStringLiteral("canonical_plan_required_for_aces2");
    }
    return {};
  }
  return render_output_image_with_backend(
      image,
      state,
      target_color_space,
      output_format,
      RenderBackendPreference::Auto,
      metadata,
      telemetry,
      mask_options,
      removal_quality,
      output_policy,
      canonical_plan,
      cancellation);
}

namespace {

struct DeferredRenderSource final {
  QString file_path;
  QSize expected_source_size;
  QRect source_crop;
  photo_editor::pipeline::DecodedImageMetadata
      expected_metadata;
};

QImage render_output_image_with_backend_impl(
    const QImage& image,
    const RenderEditState& state,
    const QColorSpace& target_color_space,
    QImage::Format output_format,
    RenderBackendPreference backend_preference,
    const photo_editor::pipeline::DecodedImageMetadata* metadata,
    RenderBackendTelemetry* telemetry,
    RenderMaskOptions mask_options,
    photo_editor::pipeline::RemovalQuality removal_quality,
    RenderOutputPolicy output_policy,
    const photo_editor::pipeline::DisplayTarget*
        export_display_target_override,
    const photo_editor::pipeline::RenderPlan*
        canonical_plan,
    const DeferredRenderSource* deferred_source = nullptr,
    photo_editor::pipeline::CancellationToken cancellation = {},
    const photo_editor::pipeline::ColorAdjustmentState*
        compiled_pipeline_state = nullptr);

}  // namespace

QImage render_output_image_with_compiled_state(
    const QImage& image,
    const RenderEditState& state,
    const photo_editor::pipeline::ColorAdjustmentState& compiled_state,
    const QColorSpace& target_color_space,
    QImage::Format output_format,
    const photo_editor::pipeline::RenderPlan& canonical_plan,
    photo_editor::pipeline::CancellationToken cancellation) {
  return render_output_image_with_backend_impl(
      image,
      state,
      target_color_space,
      output_format,
      RenderBackendPreference::Auto,
      nullptr,
      nullptr,
      RenderMaskOptions{.rebuild_editable_masks_at_output_resolution = true},
      photo_editor::pipeline::RemovalQuality::Interactive,
      RenderOutputPolicy::DisplayReferred,
      nullptr,
      &canonical_plan,
      nullptr,
      cancellation,
      &compiled_state);
}

QImage render_output_image_with_backend(
    const QImage& image,
    const RenderEditState& state,
    const QColorSpace& target_color_space,
    QImage::Format output_format,
    RenderBackendPreference backend_preference,
    const photo_editor::pipeline::DecodedImageMetadata* metadata,
    RenderBackendTelemetry* telemetry,
    RenderMaskOptions mask_options,
    photo_editor::pipeline::RemovalQuality removal_quality,
    RenderOutputPolicy output_policy,
    const photo_editor::pipeline::RenderPlan*
        canonical_plan,
    photo_editor::pipeline::CancellationToken cancellation) {
  return render_output_image_with_backend_impl(
      image,
      state,
      target_color_space,
      output_format,
      backend_preference,
      metadata,
      telemetry,
      mask_options,
      removal_quality,
      output_policy,
      nullptr,
      canonical_plan,
      nullptr,
      cancellation);
}

QImage render_output_image_for_display_target(
    const QImage& image,
    const RenderEditState& state,
    const QColorSpace& target_color_space,
    QImage::Format output_format,
    const photo_editor::pipeline::DisplayTarget& display_target,
    const photo_editor::pipeline::DecodedImageMetadata* metadata,
    RenderBackendTelemetry* telemetry,
    RenderMaskOptions mask_options,
    photo_editor::pipeline::RemovalQuality removal_quality,
    const photo_editor::pipeline::RenderPlan*
        canonical_plan,
    photo_editor::pipeline::CancellationToken cancellation) {
  if (canonical_plan == nullptr &&
      normalized_photographic_rendering_model_name(
          state.photographic_rendering_model) ==
          QStringLiteral("aces2-ocio-v1")) {
    if (telemetry != nullptr) {
      telemetry->backend = QStringLiteral("none");
      telemetry->fallback_reason =
          QStringLiteral("canonical_plan_required_for_aces2");
      telemetry->canonical_plan_error =
          QStringLiteral("canonical_plan_required_for_aces2");
    }
    return {};
  }
  return render_output_image_with_backend_impl(
      image,
      state,
      target_color_space,
      output_format,
      RenderBackendPreference::Auto,
      metadata,
      telemetry,
      mask_options,
      removal_quality,
      RenderOutputPolicy::DisplayReferred,
      &display_target,
      canonical_plan,
      nullptr,
      cancellation);
}

QImage render_output_file(
    const QString& file_path,
    const QSize& expected_source_size,
    const QRect& source_crop,
    const RenderEditState& state,
    const QColorSpace& target_color_space,
    QImage::Format output_format,
    const photo_editor::pipeline::DecodedImageMetadata*
        expected_metadata,
    RenderBackendTelemetry* telemetry,
    RenderMaskOptions mask_options,
    photo_editor::pipeline::RemovalQuality
        removal_quality,
    RenderOutputPolicy output_policy,
    const photo_editor::pipeline::DisplayTarget*
        display_target,
    const photo_editor::pipeline::RenderPlan*
        canonical_plan,
    photo_editor::pipeline::CancellationToken cancellation) {
  if (canonical_plan == nullptr &&
      normalized_photographic_rendering_model_name(state.photographic_rendering_model) ==
          QStringLiteral("legacy-oklch-v1")) {
    // Legacy recipes have no canonical Decode stage. Materialize their source
    // here, then use the same compatibility renderer as an in-memory export.
    QImage decoded;
    photo_editor::pipeline::DecodedImageMetadata decoded_metadata;
    if (!decode_source_image(file_path, &decoded, 0, state, &decoded_metadata, cancellation) ||
        decoded.size() != expected_source_size || !source_crop.isValid() ||
        !decoded.rect().contains(source_crop) ||
        (expected_metadata != nullptr && expected_metadata->valid &&
         (!decoded_metadata.valid || decoded_metadata.pixel_encoding != expected_metadata->pixel_encoding ||
          decoded_metadata.decoder_backend != expected_metadata->decoder_backend ||
          decoded_metadata.camera_profile_identity != expected_metadata->camera_profile_identity))) {
      return {};
    }
    return render_output_image_with_backend_impl(
        decoded.copy(source_crop), state, target_color_space, output_format,
        RenderBackendPreference::Auto, &decoded_metadata, telemetry, mask_options,
        removal_quality, output_policy, display_target, nullptr, nullptr, cancellation);
  }
  const auto deferred_source =
      DeferredRenderSource{
          .file_path = file_path,
          .expected_source_size =
              expected_source_size,
          .source_crop = source_crop,
          .expected_metadata =
              expected_metadata == nullptr
                  ? photo_editor::pipeline::
                        DecodedImageMetadata{}
                  : *expected_metadata,
      };
  return render_output_image_with_backend_impl(
      {},
      state,
      target_color_space,
      output_format,
      RenderBackendPreference::Auto,
      expected_metadata,
      telemetry,
      mask_options,
      removal_quality,
      output_policy,
      display_target,
      canonical_plan,
      &deferred_source,
      cancellation);
}

QImage render_editable_base_pixels(const QString& source_path, EditRecipeState state, QString* error) {
  QVariantList base_edits;
  for (const auto& value : state.layers)
    if (value.toMap().value(QStringLiteral("baseImageEdit")).toBool()) base_edits.push_back(value);
  state.layers = base_edits;
  const auto size = source_image_size(source_path);
  photo_editor::pipeline::DecodedImageMetadata metadata;
  if (raw_image_supported(source_path) && !read_raw_decoded_metadata(
          source_path, &metadata, raw_decode_settings_for_state(state))) return {};
  const auto admission = RenderPlanStore::shared().admit(
      QStringLiteral("editable-base:%1").arg(source_path), source_path, state, size,
      photo_editor::pipeline::RenderPurpose::Export,
      photo_editor::pipeline::DisplayRenderingTarget::SrgbSimulation, std::nullopt,
      metadata.valid ? &metadata : nullptr);
  if (!admission.admitted) { if (error) *error = admission.error; return {}; }
  RenderBackendTelemetry telemetry;
  auto result = render_output_file(source_path, size, crop_rect_for_image(size, state), state,
      working_color_space(state.working_color_profile), QImage::Format_RGBA32FPx4_Premultiplied,
      metadata.valid ? &metadata : nullptr, &telemetry, {},
      photo_editor::pipeline::RemovalQuality::Final, RenderOutputPolicy::SceneLinearWorking,
      nullptr, &*admission.plan);
  if (result.isNull() && error) *error = telemetry.canonical_plan_error + " " + telemetry.fallback_reason;
  return result;
}

QImage render_photo_layer_snapshot(
    const QImage& source_image,
    EditRecipeState state,
    const QString& layer_id,
    QString* error) {
  if (source_image.isNull() || layer_id.trimmed().isEmpty()) {
    if (error) *error = QStringLiteral("The content-layer snapshot request is incomplete.");
    return {};
  }
  QHash<QString, QVariantMap> layers_by_id;
  for (const auto& value : state.layers) {
    const auto layer = value.toMap();
    const auto id = layer.value(QStringLiteral("id")).toString().simplified();
    if (!id.isEmpty()) layers_by_id.insert(id, layer);
  }
  if (!layers_by_id.contains(layer_id)) {
    if (error) *error = QStringLiteral("The source layer no longer exists.");
    return {};
  }
  QSet<QString> isolated_ids{layer_id};
  auto parent_id = layers_by_id.value(layer_id).value(QStringLiteral("parentId")).toString().simplified();
  while (!parent_id.isEmpty()) {
    if (isolated_ids.contains(parent_id) || !layers_by_id.contains(parent_id)) {
      if (error) *error = QStringLiteral("The source layer has an invalid group hierarchy.");
      return {};
    }
    isolated_ids.insert(parent_id);
    parent_id = layers_by_id.value(parent_id).value(QStringLiteral("parentId")).toString().simplified();
  }
  QVariantList isolated_layers;
  for (const auto& value : state.layers) {
    const auto layer = value.toMap();
    if (isolated_ids.contains(layer.value(QStringLiteral("id")).toString().simplified()))
      isolated_layers.append(layer);
  }
  state.layers = std::move(isolated_layers);
  state.crop_rect_x = 0.0;
  state.crop_rect_y = 0.0;
  state.crop_rect_width = 0.0;
  state.crop_rect_height = 0.0;
  state.crop_zoom = 1.0;
  state.crop_preset = QStringLiteral("Original");
  state.transform_rotation = 0.0;
  state.transform_scale = 1.0;
  state.transform_skew_x = 0.0;
  state.transform_skew_y = 0.0;
  state.perspective_vertical = 0.0;
  state.perspective_horizontal = 0.0;
  auto transparent = source_image.convertToFormat(QImage::Format_RGBA8888);
  for (int y = 0; y < transparent.height(); ++y) {
    auto* row = transparent.scanLine(y);
    for (int x = 0; x < transparent.width(); ++x) row[x * 4 + 3] = 0;
  }
  RenderBackendTelemetry telemetry;
  auto output = render_output_image_with_backend(transparent, state,
      working_color_space(state.working_color_profile),
      QImage::Format_RGBA32FPx4_Premultiplied, RenderBackendPreference::CpuOnly,
      nullptr, &telemetry, {}, photo_editor::pipeline::RemovalQuality::Final,
      RenderOutputPolicy::SceneLinearWorking);
  if (output.isNull() && error) {
    *error = telemetry.canonical_plan_error + QLatin1Char(' ') + telemetry.fallback_reason;
    if (error->trimmed().isEmpty()) *error = QStringLiteral("The selected content layer could not be rasterized.");
  }
  return output;
}

QImage render_flattened_output_file(
    const QString& source_asset_id,
    const QString& source_path,
    const RenderEditState& state,
    const QColorSpace& target_color_space,
    QImage::Format output_format,
    RenderBackendTelemetry* telemetry,
    photo_editor::pipeline::CancellationToken cancellation) {
  const auto source_size = source_image_size(source_path);
  if (!source_size.isValid() || cancellation.cancelled()) {
    return {};
  }
  auto decoded_metadata =
      photo_editor::pipeline::DecodedImageMetadata{};
  const auto source_is_raw = raw_image_supported(source_path);
  if (source_is_raw &&
      !read_raw_decoded_metadata(
          source_path,
          &decoded_metadata,
          raw_decode_settings_for_state(state))) {
    return {};
  }
  const auto crop_rect = crop_rect_for_image(source_size, state);
  if (!crop_rect.isValid() || crop_rect.isEmpty()) {
    return {};
  }
  const auto camera_profile_source_area = QRectF(
      static_cast<double>(crop_rect.x()) /
          static_cast<double>(source_size.width()),
      static_cast<double>(crop_rect.y()) /
          static_cast<double>(source_size.height()),
      static_cast<double>(crop_rect.width()) /
          static_cast<double>(source_size.width()),
      static_cast<double>(crop_rect.height()) /
          static_cast<double>(source_size.height()));
  auto export_profile =
      std::optional<photo_editor::pipeline::AssetIdentifier>{};
  if (!state.export_icc_profile_asset.asset_id.trimmed().isEmpty()) {
    export_profile = photo_editor::pipeline::AssetIdentifier{
        state.export_icc_profile_asset.asset_id.trimmed().toStdString()};
  } else if (!state.export_icc_profile_asset.payload_sha256
                  .trimmed()
                  .isEmpty()) {
    export_profile = photo_editor::pipeline::AssetIdentifier{
        "sha256:" +
        state.export_icc_profile_asset.payload_sha256
            .trimmed()
            .toStdString()};
  }
  const auto output_name = state.output_color_space.trimmed().toLower();
  const auto rendering_target =
      output_name.contains(QStringLiteral("display p3"))
          ? photo_editor::pipeline::DisplayRenderingTarget::
                DisplayP3Simulation
          : photo_editor::pipeline::DisplayRenderingTarget::
                SrgbSimulation;
  const auto stable_source_id =
      source_asset_id.trimmed().isEmpty()
          ? QStringLiteral("flattened-export:%1")
                .arg(QFileInfo(source_path).absoluteFilePath())
          : source_asset_id.trimmed();
  const auto admission = RenderPlanStore::shared().admit(
      stable_source_id,
      source_path,
      state,
      source_size,
      photo_editor::pipeline::RenderPurpose::Export,
      rendering_target,
      export_profile,
      decoded_metadata.valid ? &decoded_metadata : nullptr);
  if (telemetry != nullptr) {
    telemetry->canonical_plan_cache_key = admission.plan_cache_key;
    telemetry->canonical_recipe_cache_key = admission.recipe_cache_key;
    telemetry->canonical_plan_error = admission.error;
  }
  const auto canonical_required =
      normalized_photographic_rendering_model_name(
          state.photographic_rendering_model) ==
      QStringLiteral("aces2-ocio-v1");
  if (canonical_required && !admission.admitted) {
    return {};
  }
  return render_output_file(
      source_path,
      source_size,
      crop_rect,
      state,
      target_color_space,
      output_format,
      decoded_metadata.valid ? &decoded_metadata : nullptr,
      telemetry,
      RenderMaskOptions{
          .rebuild_editable_masks_at_output_resolution = true,
          .camera_profile_source_area = camera_profile_source_area,
      },
      photo_editor::pipeline::RemovalQuality::Final,
      RenderOutputPolicy::DisplayReferred,
      nullptr,
      admission.admitted ? &*admission.plan : nullptr,
      cancellation);
}

namespace {

QImage render_output_image_with_backend_impl(
    const QImage& image,
    const RenderEditState& state,
    const QColorSpace& target_color_space,
    QImage::Format output_format,
    RenderBackendPreference backend_preference,
    const photo_editor::pipeline::DecodedImageMetadata* metadata,
    RenderBackendTelemetry* telemetry,
    RenderMaskOptions mask_options,
    photo_editor::pipeline::RemovalQuality removal_quality,
    RenderOutputPolicy output_policy,
    const photo_editor::pipeline::DisplayTarget*
        export_display_target_override,
    const photo_editor::pipeline::RenderPlan*
        canonical_plan,
    const DeferredRenderSource* deferred_source,
    photo_editor::pipeline::CancellationToken cancellation,
    const photo_editor::pipeline::ColorAdjustmentState*
        compiled_pipeline_state) {
  if (telemetry != nullptr) {
    *telemetry = {};
  }
  if (cancellation.cancelled() ||
      (image.isNull() && deferred_source == nullptr)) {
    if (telemetry != nullptr) {
      telemetry->backend = QStringLiteral("none");
      telemetry->fallback_reason = QStringLiteral("empty_source");
    }
    return {};
  }
  if (const auto error = linked_photo_layer_source_file_error(state.layers);
      !error.isEmpty()) {
    if (telemetry != nullptr) {
      telemetry->backend = QStringLiteral("none");
      telemetry->fallback_reason = error;
    }
    return {};
  }
  if (deferred_source != nullptr &&
      (canonical_plan == nullptr ||
       deferred_source->file_path.trimmed().isEmpty() ||
       !deferred_source->expected_source_size.isValid() ||
       !deferred_source->source_crop.isValid() ||
       deferred_source->source_crop.isEmpty() ||
       !QRect(
            QPoint(0, 0),
            deferred_source->expected_source_size)
            .contains(deferred_source->source_crop))) {
    if (telemetry != nullptr) {
      telemetry->backend = QStringLiteral("none");
      telemetry->fallback_reason =
          QStringLiteral("invalid_deferred_decode_source");
    }
    return {};
  }
  if (compiled_pipeline_state != nullptr && deferred_source != nullptr) {
    return {};
  }

  std::optional<photo_editor::pipeline::RenderPlan> auto_admitted_plan;
  if (canonical_plan == nullptr && deferred_source == nullptr &&
      !image.isNull() && output_policy == RenderOutputPolicy::DisplayReferred &&
      normalized_photographic_rendering_model_name(
          state.photographic_rendering_model) ==
          QStringLiteral("aces2-ocio-v1")) {
    auto probe_state = compile_export_pipeline_state(
        state,
        metadata,
        MaskRasterPolicy::UseStoredRaster,
        image.size());
    if (export_display_target_override != nullptr) {
      probe_state.color_pipeline.export_display_target =
          *export_display_target_override;
      probe_state.color_pipeline.export_output_profile.color_space =
          export_display_target_override->color_space;
    }
    const auto text_policy = classify_display_referred_text_layers(
        state,
        probe_state.color_pipeline,
        DesktopRenderPurpose::Output,
        image.size());
    if (!text_policy.eligible_layer_ids.isEmpty()) {
      auto device_profile =
          std::optional<photo_editor::pipeline::AssetIdentifier>{};
      if (!state.export_icc_profile_asset.asset_id.trimmed().isEmpty()) {
        device_profile = photo_editor::pipeline::AssetIdentifier{
            state.export_icc_profile_asset.asset_id.trimmed().toStdString()};
      } else if (!state.export_icc_profile_asset.payload_sha256
                      .trimmed()
                      .isEmpty()) {
        device_profile = photo_editor::pipeline::AssetIdentifier{
            "sha256:" + state.export_icc_profile_asset.payload_sha256
                              .trimmed()
                              .toStdString()};
      }
      const auto& target = probe_state.color_pipeline.export_display_target;
      const auto rendering_target = target.eotf ==
              photo_editor::pipeline::DisplayEotf::Pq
          ? photo_editor::pipeline::DisplayRenderingTarget::HdrPq
          : target.eotf == photo_editor::pipeline::DisplayEotf::Hlg
              ? photo_editor::pipeline::DisplayRenderingTarget::HdrHlg
              : target.color_space.primaries ==
                        photo_editor::pipeline::ColorPrimaries::DisplayP3
                    ? photo_editor::pipeline::DisplayRenderingTarget::DisplayP3Simulation
                    : photo_editor::pipeline::DisplayRenderingTarget::SrgbSimulation;
      const auto source_identity = QStringLiteral("in-memory-qimage:%1")
                                       .arg(QString::number(image.cacheKey(), 16));
      const auto admission = RenderPlanStore::shared().admit(
          source_identity,
          QStringLiteral("in-memory-render.png"),
          state,
          image.size(),
          photo_editor::pipeline::RenderPurpose::Export,
          rendering_target,
          device_profile,
          metadata);
      if (!admission.admitted || !admission.plan.has_value()) {
        if (telemetry != nullptr) {
          telemetry->canonical_plan_cache_key = admission.plan_cache_key;
          telemetry->canonical_recipe_cache_key = admission.recipe_cache_key;
          telemetry->canonical_plan_error = admission.error;
          telemetry->backend = QStringLiteral("none");
          telemetry->fallback_reason =
              QStringLiteral("canonical_plan_admission_failed_for_display_text");
        }
        return {};
      }
      auto_admitted_plan = admission.plan;
      canonical_plan = &*auto_admitted_plan;
      if (telemetry != nullptr) {
        telemetry->canonical_plan_cache_key = admission.plan_cache_key;
        telemetry->canonical_recipe_cache_key = admission.recipe_cache_key;
        telemetry->canonical_plan_error.clear();
      }
    }
  }

  const auto requested_input_profile =
      photo_editor::pipeline::input_color_profile_from_name(
          state.input_color_profile.toStdString());
  const auto dcp_profile_asset =
      resolve_dcp_profile_asset(state.camera_dcp_profile_asset);
  const auto input_profile_asset = resolve_icc_profile_asset(state.input_icc_profile_asset);
  if (requested_input_profile.kind ==
      photo_editor::pipeline::InputTransformKind::CameraDcpProfile) {
    const auto camera_linear_boundary =
        metadata != nullptr &&
        photo_editor::pipeline::decoded_image_requires_engine_input_transform(*metadata);
    if (!camera_linear_boundary || !dcp_profile_asset.ready()) {
      if (telemetry != nullptr) {
        telemetry->backend = QStringLiteral("none");
        telemetry->fallback_reason =
            !camera_linear_boundary
                ? QStringLiteral("dcp_profile_requires_camera_linear_raw")
                : QString::fromStdString(
                      dcp_profile_asset.failure_reason.empty()
                          ? "dcp_profile_asset_required"
                          : dcp_profile_asset.failure_reason);
      }
      return {};
    }
  }
  if (requested_input_profile.kind ==
      photo_editor::pipeline::InputTransformKind::CameraIccProfile) {
    const auto camera_linear_boundary =
        metadata != nullptr &&
        photo_editor::pipeline::decoded_image_requires_engine_input_transform(*metadata);
    if (!camera_linear_boundary || !input_profile_asset.ready()) {
      if (telemetry != nullptr) {
        telemetry->backend = QStringLiteral("none");
        telemetry->fallback_reason =
            !camera_linear_boundary
                ? QStringLiteral("camera_icc_profile_requires_camera_linear_raw")
                : QString::fromStdString(
                      input_profile_asset.failure_reason.empty()
                          ? "camera_icc_profile_asset_required"
                          : input_profile_asset.failure_reason);
      }
      return {};
    }
  }

  if (metadata != nullptr && state.input_icc_profile_asset.configured() &&
      !photo_editor::pipeline::decoded_image_accepts_custom_input_profile(*metadata)) {
    if (telemetry != nullptr) {
      telemetry->backend = QStringLiteral("none");
      telemetry->fallback_reason = QStringLiteral(
          "icc_input_profile_requires_camera_linear_raw");
    }
    return {};
  }

  auto working =
      deferred_source == nullptr
          ? working_image_from_qimage(
                image,
                state.working_color_profile,
                &input_profile_asset,
                metadata != nullptr &&
                    photo_editor::pipeline::
                        decoded_image_requires_engine_input_transform(
                            *metadata))
          : WorkingImage{
                .width =
                    deferred_source->source_crop.width(),
                .height =
                    deferred_source->source_crop.height(),
                .pixels = {},
            };
  if (deferred_source == nullptr && working.isNull()) {
    if (telemetry != nullptr) {
      telemetry->backend = QStringLiteral("none");
      telemetry->fallback_reason =
          input_profile_asset.status == photo_editor::pipeline::IccProfileAssetStatus::Unconfigured
              ? QStringLiteral("linearization_failed")
              : QString::fromStdString(
                    input_profile_asset.failure_reason.empty()
                        ? "icc_input_profile_transform_failed"
                        : input_profile_asset.failure_reason);
    }
    return {};
  }

  auto& pixels = working.pixels;
  const auto width = working.width;
  const auto height = working.height;

  const auto raster_policy = mask_options.rebuild_editable_masks_at_output_resolution
                                 ? MaskRasterPolicy::RebuildEditableAtOutputResolution
                                 : MaskRasterPolicy::UseStoredRaster;
  auto pipeline_state_storage =
      std::optional<photo_editor::pipeline::ColorAdjustmentState>{};
  if (compiled_pipeline_state == nullptr) {
    pipeline_state_storage.emplace(compile_export_pipeline_state(
        state,
        metadata,
        raster_policy,
        QSize(width, height),
        pixels,
        export_display_target_override));
  }
  auto& pipeline_state = compiled_pipeline_state == nullptr
                             ? *pipeline_state_storage
                             : const_cast<photo_editor::pipeline::ColorAdjustmentState&>(
                                   *compiled_pipeline_state);
  if (const auto error = compiled_photo_layer_source_error(pipeline_state.photo_layers);
      !error.isEmpty()) {
    if (telemetry != nullptr) {
      telemetry->backend = QStringLiteral("none");
      telemetry->fallback_reason = error;
    }
    return {};
  }
  const auto apply_camera_profile_source_area =
      [&](photo_editor::pipeline::
              ColorAdjustmentState* target) {
        auto source_area =
            mask_options.camera_profile_source_area
                .intersected(
                    QRectF(0.0, 0.0, 1.0, 1.0));
        if (!source_area.isValid() ||
            source_area.width() <= 0.0 ||
            source_area.height() <= 0.0) {
          source_area =
              QRectF(0.0, 0.0, 1.0, 1.0);
        }
        target->input_transform.source_area_x =
            source_area.x();
        target->input_transform.source_area_y =
            source_area.y();
        target->input_transform.source_area_width =
            source_area.width();
        target->input_transform.source_area_height =
            source_area.height();
      };
  if (compiled_pipeline_state == nullptr) {
    apply_camera_profile_source_area(&pipeline_state);
  }
  const auto display_referred_output =
      output_policy == RenderOutputPolicy::DisplayReferred;
  const auto custom_export_profile_configured =
      display_referred_output &&
      pipeline_state.color_pipeline.export_icc_profile_asset.status !=
      photo_editor::pipeline::IccProfileAssetStatus::Unconfigured;
  const auto execute_canonical_photographic_output =
      canonical_plan != nullptr &&
      display_referred_output &&
      (custom_export_profile_configured ||
       pipeline_state.color_pipeline.soft_proof_enabled ||
       qt_supports_exact_target_linear_profile(
           pipeline_state.color_pipeline
               .export_output_profile.color_space
               .primaries));
  auto photographic_output_executed = false;
  auto publish_executed = false;
  auto deferred_decode_stage =
      photo_editor::pipeline::RenderStageCallback{};
  if (deferred_source != nullptr) {
    deferred_decode_stage =
        [&](const photo_editor::pipeline::
                RenderStageInvocation& invocation,
            std::optional<
                photo_editor::pipeline::RenderSurface>
                input) {
          if (input.has_value()) {
            return photo_editor::pipeline::
                RenderStageExecution{
                    .failure_reason =
                        "export Decode received an unexpected input surface",
                };
          }
          auto decoded = QImage{};
          auto decoded_metadata =
              photo_editor::pipeline::
                  DecodedImageMetadata{};
          if (!decode_source_image(
                  deferred_source->file_path,
                  &decoded,
                  0,
                  state,
                  &decoded_metadata,
                  cancellation)) {
            return photo_editor::pipeline::
                RenderStageExecution{
                    .failure_reason =
                        "export Decode could not read the source image",
                };
          }
          const auto& expected_metadata =
              deferred_source->expected_metadata;
          if (decoded.size() !=
                  deferred_source
                      ->expected_source_size ||
              (expected_metadata.valid &&
               (!decoded_metadata.valid ||
                decoded_metadata.pixel_encoding !=
                    expected_metadata.pixel_encoding ||
                decoded_metadata.decoder_backend !=
                    expected_metadata.decoder_backend ||
                decoded_metadata
                        .camera_profile_identity !=
                    expected_metadata
                        .camera_profile_identity))) {
            return photo_editor::pipeline::
                RenderStageExecution{
                    .failure_reason =
                        "export Decode did not match the admitted source boundary",
                };
          }
          // Highlight recovery estimates scene chrominance from sensor data.
          // Keep that estimate tied to the complete source, not the crop chosen
          // for this export; otherwise cropping can recolor retained highlights.
          auto source_highlights = std::optional<
              photo_editor::pipeline::HighlightReconstructionAnalysis>{};
          if (decoded_metadata.valid &&
              photo_editor::pipeline::decoded_image_requires_engine_input_transform(decoded_metadata) &&
              pipeline_state.input_transform.highlight_recovery_enabled &&
              pipeline_state.input_transform.highlight_reconstruction_mode != "Disabled" &&
              pipeline_state.input_transform.highlight_reconstruction_model == "camera-linear-opposed-v2" &&
              deferred_source->source_crop != QRect(QPoint(0, 0), decoded.size())) {
            const auto full_working = working_image_from_qimage(
                decoded, state.working_color_profile, &input_profile_asset, true);
            if (full_working.isNull() || cancellation.cancelled()) {
              return photo_editor::pipeline::RenderStageExecution{
                  .failure_reason = "export could not analyze full-source RAW highlights"};
            }
            source_highlights = photo_editor::pipeline::analyze_highlight_reconstruction(
                full_working.pixels, full_working.width, full_working.height, pipeline_state.input_transform);
          }
          decoded =
              decoded.copy(
                  deferred_source->source_crop);
          auto decoded_working =
              working_image_from_qimage(
                  decoded,
                  state.working_color_profile,
                  &input_profile_asset,
                  decoded_metadata.valid &&
                      photo_editor::pipeline::
                          decoded_image_requires_engine_input_transform(
                              decoded_metadata));
          if (decoded_working.isNull() ||
              decoded_working.width != width ||
              decoded_working.height != height) {
            return photo_editor::pipeline::
                RenderStageExecution{
                    .failure_reason =
                        "export Decode could not materialize the admitted linear surface",
                };
          }
          if (telemetry != nullptr) {
            telemetry->decode_execution =
                QStringLiteral(
                    "file-read-and-linearize-in-executor-decode");
          }
          pixels =
              std::move(decoded_working.pixels);
          pipeline_state =
              compile_export_pipeline_state(
                  state,
                  decoded_metadata.valid
                      ? &decoded_metadata
                      : metadata,
                  raster_policy,
                  QSize(width, height),
                  pixels,
                  export_display_target_override);
          if (const auto error = compiled_photo_layer_source_error(pipeline_state.photo_layers);
              !error.isEmpty()) {
            return photo_editor::pipeline::RenderStageExecution{
                .failure_reason = error.toStdString()};
          }
          apply_camera_profile_source_area(
              &pipeline_state);
          return photo_editor::pipeline::
              RenderStageExecution{
                  .output =
                      photo_editor::pipeline::
                          RenderSurface{
                              .descriptor =
                                  invocation
                                      .expected_output,
                              .pixels =
                                  std::move(pixels),
                              .identity =
                                  "surface:decode:" +
                                  canonical_plan
                                      ->recipe_cache_key,
                              .highlight_reconstruction = source_highlights,
                          },
                  .backend =
                      "export-file-decoder",
              };
        };
  }
#if defined(PHOTO_EDITOR_HAS_METAL_BACKEND) && PHOTO_EDITOR_HAS_METAL_BACKEND
  auto& metal = metal_backend();
  const auto fallback_reason = gpu_fallback_reason(state, pipeline_state, pixels, metal.available());
  const auto gpu_supported = fallback_reason.isEmpty();
  if (qEnvironmentVariableIntValue("PHOTO_EDITOR_TRACE_RENDER_STAGES") == 1) {
    std::fprintf(stderr, "render-route gpu=%d fallback=%s canonical=%d raw=%d camera=%d\n", gpu_supported,
        fallback_reason.toUtf8().constData(), canonical_plan != nullptr,
        pipeline_state.input_transform.raw_input_transform_enabled, pipeline_state.color_pipeline.camera_profile.valid);
  }
  const auto requires_canonical_geometry_executor =
      canonical_plan != nullptr;
  photo_editor::pipeline::ColorAdjustmentTelemetry engine_telemetry;
  if (backend_preference ==
          RenderBackendPreference::GpuOnly &&
      requires_canonical_geometry_executor) {
    if (telemetry != nullptr) {
      telemetry->backend = QStringLiteral("none");
      telemetry->fallback_reason =
          unsupported_stage_reason(
              QStringLiteral("Geometry"),
              QStringLiteral(
                  "canonical_cpu_reference_required"));
    }
    return {};
  }
  if (backend_preference == RenderBackendPreference::GpuOnly &&
      !gpu_supported) {
    if (telemetry != nullptr) {
      telemetry->backend = QStringLiteral("none");
      telemetry->fallback_reason = fallback_reason;
    }
    return {};
  }
  if (backend_preference != RenderBackendPreference::CpuOnly &&
      gpu_supported &&
      !requires_canonical_geometry_executor) {
    if (telemetry != nullptr) {
      telemetry->backend = QStringLiteral("metal");
      telemetry->fallback_reason.clear();
    }
    const auto lut = photo_editor::pipeline::build_color_lut(pipeline_state);
    if (!metal.apply_masks_and_color_lut(
            &pixels,
            width,
            height,
            pipeline_state,
            lut,
            &engine_telemetry,
            cancellation)) {
      const auto fused_failure_reason =
          QString::fromStdString(
              engine_telemetry.gpu_fallback_reason);
      if (backend_preference ==
          RenderBackendPreference::GpuOnly) {
        if (telemetry != nullptr) {
          telemetry->backend = QStringLiteral("none");
          telemetry->fallback_reason =
              fused_failure_reason;
        }
        return {};
      }
      if (telemetry != nullptr) {
        telemetry->backend = QStringLiteral("cpu");
        telemetry->fallback_reason =
            fused_failure_reason;
      }
      engine_telemetry = {};
      if (!apply_export_color_pipeline_with_executor(
              &pixels,
              width,
              height,
              pipeline_state,
              removal_quality,
              canonical_plan,
              execute_canonical_photographic_output,
              telemetry,
              &engine_telemetry,
              &photographic_output_executed,
              &publish_executed,
              std::move(
                  deferred_decode_stage),
              cancellation)) {
        return {};
      }
    }
  } else {
    if (telemetry != nullptr) {
      telemetry->backend = QStringLiteral("cpu");
      telemetry->fallback_reason =
          backend_preference == RenderBackendPreference::CpuOnly
              ? QStringLiteral("cpu_requested")
              : fallback_reason;
    }
    if (!apply_export_color_pipeline_with_executor(
        &pixels,
        width,
        height,
        pipeline_state,
        removal_quality,
        canonical_plan,
        execute_canonical_photographic_output,
        telemetry,
        &engine_telemetry,
        &photographic_output_executed,
        &publish_executed,
        std::move(
            deferred_decode_stage),
        cancellation)) {
      return {};
    }
    if (telemetry != nullptr &&
        telemetry->backend == QStringLiteral("cpu") &&
        engine_telemetry.gpu_command_buffer_count > 0 &&
        engine_telemetry.gpu_kernel_dispatch_count > 0) {
      telemetry->backend = QStringLiteral("metal");
      telemetry->fallback_reason.clear();
    }
  }
#else
  if (backend_preference == RenderBackendPreference::GpuOnly) {
    if (telemetry != nullptr) {
      telemetry->backend = QStringLiteral("none");
      telemetry->fallback_reason = QStringLiteral("metal_not_compiled");
    }
    return {};
  }
  if (telemetry != nullptr) {
    telemetry->backend = QStringLiteral("cpu");
    telemetry->fallback_reason =
        backend_preference == RenderBackendPreference::CpuOnly
            ? QStringLiteral("cpu_requested")
            : QStringLiteral("metal_not_compiled");
  }
  photo_editor::pipeline::ColorAdjustmentTelemetry
      engine_telemetry;
  if (!apply_export_color_pipeline_with_executor(
      &pixels,
      width,
      height,
      pipeline_state,
      removal_quality,
      canonical_plan,
      execute_canonical_photographic_output,
      telemetry,
      &engine_telemetry,
      &photographic_output_executed,
      &publish_executed,
      std::move(deferred_decode_stage),
      cancellation)) {
    return {};
  }
#endif

  WorkingImageQImageTelemetry output_transform_telemetry;
  const auto output_primaries =
      pipeline_state.color_pipeline.export_output_profile.color_space.primaries;
  const auto linear_output_profile =
      linear_output_profile_name(output_primaries, state.working_color_profile);
  const auto published_output_profile =
      encoded_output_profile_name(output_primaries, state.working_color_profile);
  if (cancellation.cancelled()) {
    return {};
  }
  if (publish_executed &&
      custom_export_profile_configured &&
      pipeline_state.color_pipeline
          .export_icc_profile_asset.ready()) {
    const auto profile_bytes =
        pipeline_state.color_pipeline
            .export_icc_profile_asset.payload_bytes();
    const auto device_color_space =
        QColorSpace::fromIccProfile(
            QByteArray(
                reinterpret_cast<const char*>(
                    profile_bytes.data()),
                static_cast<qsizetype>(
                    profile_bytes.size())));
    if (!device_color_space.isValid()) {
      return {};
    }
    QImage device_image(
        working.width,
        working.height,
        working_publication_format(
            output_format, working.width, working.height));
    if (device_image.isNull() ||
        !pack_working_rows_for_publication(
            working,
            QRect(
                0,
                0,
                working.width,
                working.height),
            &device_image,
            cancellation)) {
      return {};
    }
    device_image.setColorSpace(device_color_space);
    return device_image.format() == output_format
               ? device_image
               : device_image.convertToFormat(
                     output_format);
  }
  auto output_image = qimage_from_working_image(
      working,
      target_color_space.isValid() ? target_color_space : preview_target_color_space(),
      output_format,
      publish_executed
          ? published_output_profile
          : (photographic_output_executed
                 ? linear_output_profile
                 : state.working_color_profile),
      cancellation,
      &output_transform_telemetry,
      WorkingImageOutputTransform{
          .color_pipeline =
              display_referred_output &&
                      !photographic_output_executed
                                ? &pipeline_state.color_pipeline
                                : nullptr,
          .target = photo_editor::pipeline::GamutWarningTarget::ExportOutput,
          .allow_metal_output_lut = backend_preference != RenderBackendPreference::CpuOnly,
          .allow_cuda_output_lut = backend_preference != RenderBackendPreference::CpuOnly,
      });
  if (cancellation.cancelled() || output_image.isNull()) {
    if (telemetry != nullptr && !output_transform_telemetry.failure_reason.isEmpty()) {
      telemetry->fallback_reason = output_transform_telemetry.failure_reason;
    }
    return {};
  }
  if (custom_export_profile_configured || target_color_space.isValid()) {
    return output_image;
  }
  apply_output_color_space_transform(&output_image, state.output_color_space);
  return output_image;
}

}  // namespace

}  // namespace photo_editor::desktop
