#include "canonical_composite_mask_alpha.h"
#include "mask_range_preview_math.h"
#include "local_mask_set_settings.h"
#include "photo_paint_surface.h"
#include "photo_text_rasterizer.h"
#include "photo_text_dimension_render.h"
#include "photo_render_utils.h"
#include "openexr_source_color.h"
#include "brush_matte_rebuild.h"
#include "mask_edge_feather.h"

#include "photo_layer_compiler.h"
#include "photo_layer_effect_codec.h"
#include "photo_layer_style_compile.h"
#include "photo_layer_blend_if.h"
#include "photo_smart_object.h"
#include "photo_smart_document_expansion.h"
#include "photo_vector_path.h"
#include "photo_vector_mask.h"
#include "photo_vector_mask_coordinates.h"
#include "photo_extended_effect_bridge.h"
#include "layer_selection_mask_cache.h"
#include "photo_layer_source.h"
#include "photo_board_layout.h"
#include "edit_adjustment_patch.h"
#include "linked_image_source.h"
#include "desktop_pipeline_state_compiler.h"
#include "desktop_retouch_source.h"
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
#include "photo_editor/pipeline/brush_edge_math.h"
#include "photo_editor/pipeline/color_pipeline.h"
#include "photo_editor/pipeline/render_executor.h"
#include "raw_image_decoder.h"
#include "raw_decode_policy.h"
#include "working_image.h"
#include "photo_render_profile_for_tests.h"
#if defined(PHOTO_EDITOR_HAS_METAL_BACKEND) && PHOTO_EDITOR_HAS_METAL_BACKEND
#include "photo_editor/pipeline/metal/metal_pipeline_backend.h"
#endif
#include <QColor>
#include <QColorSpace>
#include <QCoreApplication>
#include <QBuffer>
#include <QCryptographicHash>
#include <QCache>
#include <QDateTime>
#include <QScopedValueRollback>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHash>
#include <QImageReader>
#include <QJsonArray>
#include <QMutex>
#include <QMutexLocker>
#include <QJsonDocument>
#include <QPainter>
#include <QPolygonF>
#include <QSet>
#include <QStringList>
#include <QTransform>
#include <QTemporaryFile>
#include <QUrl>
#include <QVariant>
#include <QVariantMap>

#include <algorithm>
#include <array>
#include <chrono>
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
    const auto profile_apply = photo_render_profile_for_tests.enabled;
    const auto apply_started = profile_apply
        ? std::chrono::steady_clock::now()
        : std::chrono::steady_clock::time_point{};
    const auto applied = photo_editor::pipeline::apply_color_adjustments(
        pixels,
        width,
        height,
        pipeline_state,
        removal_quality,
        cancellation,
        engine_telemetry);
    if (profile_apply) {
      photo_render_profile_for_tests.apply_color_adjustments_ns +=
          std::chrono::duration_cast<std::chrono::nanoseconds>(
              std::chrono::steady_clock::now() - apply_started)
              .count();
    }
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
    backend_telemetry->render_stage_elapsed_ms.clear();
    backend_telemetry->render_stage_backends.clear();
    for (const auto& completed : completed_stages) {
      const auto stage_name = QString::fromUtf8(
          photo_editor::pipeline::render_stage_kind_name(completed.kind).data(),
          static_cast<qsizetype>(
              photo_editor::pipeline::render_stage_kind_name(completed.kind).size()));
      backend_telemetry->completed_render_stages.push_back(stage_name);
      backend_telemetry->render_stage_elapsed_ms.insert(
          stage_name, QVariant::fromValue<qlonglong>(completed.elapsed_ms));
      backend_telemetry->render_stage_backends.insert(
          stage_name, QString::fromStdString(completed.backend));
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
  if (!pipeline_state.post_composite_retouch_masks.empty()) {
    return unsupported_stage_reason(
        QStringLiteral("Layers"),
        QStringLiteral("component_post_composite_retouch"));
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
            pipeline_state.input_transform,
            pipeline_state.color_pipeline.input_transform_recipe.highlight_headroom);
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

// Filter committed alpha as coverage. Minification integrates every source
// pixel cell; magnification retains endpoint-aligned bilinear sampling.
std::vector<unsigned char> resample_committed_brush_matte(
    const QByteArray& alpha,int source_width,int source_height,
    const QSize& output,const QRectF& area) {
  struct Kernel { int first=0; std::vector<double> weights; double normalizer=1.0; };
  const auto kernels=[](int source,int target,double start,double span) {
    std::vector<Kernel> result;
    result.reserve(static_cast<std::size_t>(target));
    const auto source_start=start*static_cast<double>(source-1);
    const auto source_span=span*static_cast<double>(source-1);
    const auto footprint=(source_span+1.0)/static_cast<double>(target);
    for (int destination=0;destination<target;++destination) {
      Kernel kernel;
      if (footprint>1.0) {
        // Keep overlap lengths in destination-denominator units and divide
        // once after integration. Integer pixel crops then retain exact sums
        // and half-alpha rounding even at non-integer reduction ratios.
        const auto begin=source_start*target+static_cast<double>(destination)*(source_span+1.0);
        const auto end=begin+source_span+1.0;
        kernel.normalizer=source_span+1.0;
        kernel.first=static_cast<int>(std::clamp(std::floor(begin/target),0.0,static_cast<double>(source)));
        const auto last=static_cast<int>(std::clamp(std::ceil(end/target),0.0,static_cast<double>(source)));
        for (int cell=kernel.first;cell<last;++cell)
          kernel.weights.push_back(std::max(0.0,std::min(end,static_cast<double>(cell+1)*target)-
              std::max(begin,static_cast<double>(cell)*target)));
      } else {
        const auto center=source_start+(target>1 ? static_cast<double>(destination)/
            static_cast<double>(target-1) : 0.5)*source_span;
        if (center>=0.0 && center<=static_cast<double>(source-1)) {
          kernel.first=static_cast<int>(std::floor(center));
          const auto fraction=center-static_cast<double>(kernel.first);
          kernel.weights.push_back(1.0-fraction);
          if (kernel.first+1<source) kernel.weights.push_back(fraction);
        }
      }
      result.push_back(std::move(kernel));
    }
    return result;
  };
  const auto horizontal=kernels(source_width,output.width(),area.x(),area.width());
  const auto vertical=kernels(source_height,output.height(),area.y(),area.height());
  std::vector<unsigned char> result(static_cast<std::size_t>(output.width())*
      static_cast<std::size_t>(output.height()),0);
  for (int y=0;y<output.height();++y) for (int x=0;x<output.width();++x) {
    const auto& ky=vertical[static_cast<std::size_t>(y)];
    const auto& kx=horizontal[static_cast<std::size_t>(x)];
    auto coverage=0.0;
    for (std::size_t iy=0;iy<ky.weights.size();++iy) {
      const auto row=(ky.first+static_cast<int>(iy))*source_width;
      auto row_coverage=0.0;
      for (std::size_t ix=0;ix<kx.weights.size();++ix)
        row_coverage+=static_cast<unsigned char>(alpha.at(row+kx.first+static_cast<int>(ix)))*kx.weights[ix];
      coverage+=row_coverage*ky.weights[iy];
    }
    result[static_cast<std::size_t>(y*output.width()+x)]=
        static_cast<unsigned char>(std::lround(std::clamp(coverage/(kx.normalizer*ky.normalizer),0.0,255.0)));
  }
  return result;
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
  local_mask->inpaint_source_width = mask.value(QStringLiteral("segmentationWidth")).toInt();
  local_mask->inpaint_source_height = mask.value(QStringLiteral("segmentationHeight")).toInt();
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
    const QSize& output_size,
    const QRectF& coordinate_area,
    const QSizeF& basis_pixel_extent) {
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
  local_mask.component_post_composite_retouch =
      mask.value(QStringLiteral("componentPostCompositeRetouchVersion")).toInt() == 1;
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
    // Freeze the source transform per stroke. Resetting a non-aligned stamp
    // must not move the source of strokes already on the canvas.
    if (local_mask.retouch_mode=="clone") {
      std::size_t offset=0;
      for (const auto& value : mask.value("brushStrokes").toList()) {
        const auto stroke=value.toMap();
        const auto settings=stroke.value("settings").toMap();
        const auto count=static_cast<std::size_t>(stroke.value("samples").toList().size());
        if (offset+count>local_mask.brush_points.size()) { local_mask.clone_strokes.clear(); break; }
        photo_editor::pipeline::CloneStroke clone;
        clone.circular=settings.value("rasterVersion").toInt()>=2;
        clone.hardness=settings.value("hardness",local_mask.brush_hardness).toDouble();
        clone.feather=clone.circular ? settings.value("feather",0.75).toDouble()
                                    : local_mask.feather;
        clone.points.assign(local_mask.brush_points.begin()+static_cast<std::ptrdiff_t>(offset),
            local_mask.brush_points.begin()+static_cast<std::ptrdiff_t>(offset+count));
        clone.source_x=settings.value("cloneSourceX",local_mask.clone_source_x).toDouble();
        clone.source_y=settings.value("cloneSourceY",local_mask.clone_source_y).toDouble();
        clone.offset_x=settings.value("cloneOffsetX",local_mask.clone_offset_x).toDouble();
        clone.offset_y=settings.value("cloneOffsetY",local_mask.clone_offset_y).toDouble();
        clone.scale=settings.value("cloneScale",local_mask.clone_scale).toDouble();
        clone.rotation=settings.value("cloneRotation",local_mask.clone_rotation).toDouble();
        local_mask.clone_strokes.push_back(std::move(clone)); offset+=count;
      }
      if (offset!=local_mask.brush_points.size()) local_mask.clone_strokes.clear();
    }
    auto raster_mask = mask;
    const auto preserve_component_retouch_matte =
        local_mask.component_post_composite_retouch &&
        is_component_post_composite_retouch(mask) &&
        local_mask.kind == "brush" &&
        mask.value(QStringLiteral("toolRole")).toString().trimmed().compare(
            QStringLiteral("retouch"), Qt::CaseInsensitive) == 0;
    // These captured component mattes were clipped against the child's
    // composed alpha when authored. RebuildEditableAtOutputResolution must
    // retain that authoritative coverage and only resample it below.
    const auto feather_authored_matte = local_mask.retouch_mode.empty() && local_mask.selection_feather > 0 &&
        valid_segmentation_payload(mask.value("segmentationData").toByteArray(),
            mask.value("segmentationWidth").toInt(), mask.value("segmentationHeight").toInt());
    if (raster_policy == MaskRasterPolicy::RebuildEditableAtOutputResolution &&
        output_size.isValid() && !preserve_component_retouch_matte && !feather_authored_matte) {
      // The shared replay dispatcher preserves each stroke's original tip:
      // legacy normalized ellipses and v2 pixel circles can coexist in history.
      // Source-aware histories and v1 component repairs retain their accepted
      // captured matte; the latter was gated by authored child alpha and must
      // not be replayed as an unconstrained brush at export resolution.
      const auto rebuilt = mask_with_rebuilt_brush_matte(mask, output_size,
          QStringLiteral("full-resolution-render"), coordinate_area,basis_pixel_extent);
      if (!rebuilt.isEmpty()) raster_mask = rebuilt;
    }
    const auto seg_data = raster_mask.value(QStringLiteral("segmentationData")).toByteArray();
    const auto seg_width = raster_mask.value(QStringLiteral("segmentationWidth")).toInt();
    const auto seg_height = raster_mask.value(QStringLiteral("segmentationHeight")).toInt();
    if (valid_segmentation_payload(seg_data, seg_width, seg_height)) {
      local_mask.segmentation_mask.resize(static_cast<std::size_t>(seg_data.size()));
      std::memcpy(local_mask.segmentation_mask.data(),
                  seg_data.constData(),
                  static_cast<std::size_t>(seg_data.size()));
      local_mask.segmentation_width = seg_width;
      local_mask.segmentation_height = seg_height;
      if (feather_authored_matte) {
        feather_mask_alpha(local_mask.segmentation_mask, seg_width, seg_height,
            local_mask.selection_feather, seg_width, seg_height);
        local_mask.selection_feather = 0.0;
      }
      if (raster_mask == mask && output_size.width()>0 && output_size.height()>0 &&
          (coordinate_area!=QRectF(0.0,0.0,1.0,1.0) ||
           seg_width!=output_size.width() || seg_height!=output_size.height())) {
        // Source-aware alpha remains authoritative at every render size.
        local_mask.segmentation_mask=resample_committed_brush_matte(
            QByteArray(reinterpret_cast<const char*>(local_mask.segmentation_mask.data()),
                static_cast<qsizetype>(local_mask.segmentation_mask.size())),
            seg_width,seg_height,output_size,coordinate_area);
        local_mask.segmentation_width=output_size.width();
        local_mask.segmentation_height=output_size.height();
      }
    } else if (brush_history_requires_source(mask)) {
      // Missing constrained alpha cannot safely fall back to unfiltered dabs.
      local_mask.brush_points.clear();
      local_mask.clone_strokes.clear();
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
    local_mask.color_range_sample_union =
        mask.value(QStringLiteral("colorSampleMode")).toString() == QStringLiteral("union");
    if (local_mask.color_range_sample_union) {
      local_mask.hue_range = std::isfinite(local_mask.hue_range)
                                 ? std::clamp(local_mask.hue_range, 0.02, 0.5)
                                 : 0.12;
      const auto samples = mask.value(QStringLiteral("colorSamples")).toList();
      local_mask.color_range_sample_hues.reserve(static_cast<std::size_t>(samples.size()));
      for (const auto& sample_value : samples) {
        const auto sample = sample_value.toMap();
        if (!sample.contains(QStringLiteral("hue"))) {
          continue;
        }
        const auto hue = sample.value(QStringLiteral("hue")).toDouble();
        if (std::isfinite(hue)) {
          local_mask.color_range_sample_hues.push_back(std::clamp(hue, 0.0, 1.0));
        }
      }
    }
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
  if (local_mask.retouch_mode.empty() && !local_mask.segmentation_mask.empty() &&
      local_mask.selection_feather > 0.0) {
    const auto cropped_brush = local_mask.kind == "brush" &&
        local_mask.segmentation_width == output_size.width() &&
        local_mask.segmentation_height == output_size.height();
    feather_mask_alpha(local_mask.segmentation_mask, local_mask.segmentation_width,
        local_mask.segmentation_height, local_mask.selection_feather,
        cropped_brush && coordinate_area.width() > 0
            ? local_mask.segmentation_width / coordinate_area.width() : local_mask.segmentation_width,
        cropped_brush && coordinate_area.height() > 0
            ? local_mask.segmentation_height / coordinate_area.height() : local_mask.segmentation_height);
    local_mask.selection_feather = 0.0;
    // The filtered values are coverage, rather than SAM confidence. Passing
    // them through the generated-mask threshold would harden the edge again.
    if (is_raster_mask_kind(QString::fromStdString(local_mask.kind))) {
      local_mask.kind = "brush";
      local_mask.brush_points.clear();
    }
  }
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
  } else if (!adjustments.contains(QStringLiteral("lut")) &&
             !layer.contains(QStringLiteral("lut")) &&
             (adjustments.contains(QStringLiteral("lutTable")) ||
              layer.contains(QStringLiteral("lutTable")))) {
    // Old layer recipes keep the primary LUT in flat fields. An explicit
    // structured LUT (including an empty/disabled one) takes precedence.
    const auto value = [&](const QString& key, const QVariant& fallback = {}) {
      return adjustments.value(key, layer.value(key, fallback));
    };
    QVariantMap legacy{
        {QStringLiteral("enabled"), value(QStringLiteral("lutEnabled"), false)},
        {QStringLiteral("strength"), value(QStringLiteral("lutStrength"), 100.0)},
        {QStringLiteral("name"), value(QStringLiteral("lutName"))},
        {QStringLiteral("table"), value(QStringLiteral("lutTable"))},
        {QStringLiteral("size"), value(QStringLiteral("lutSize"))},
        {QStringLiteral("domainMin"), value(QStringLiteral("lutDomainMin"))},
        {QStringLiteral("domainMax"), value(QStringLiteral("lutDomainMax"))},
        {QStringLiteral("inputColorSpace"), value(QStringLiteral("lutInputColorSpace"), QStringLiteral("sRGB"))},
        {QStringLiteral("outputColorSpace"), value(QStringLiteral("lutOutputColorSpace"), QStringLiteral("sRGB"))},
    };
    stack.insert(stack.begin(), layer_lut_adjustment_from_object(legacy));
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
  if (target == nullptr || !layer.value(QStringLiteral("maskEnabled"), true).toBool()) {
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

bool selection_layer_set_uses_color_or_luminance_range(
    const photo_editor::pipeline::LocalMaskSet& mask_set) {
  return std::any_of(mask_set.sources.begin(), mask_set.sources.end(), [](const auto& source) {
    return source.kind == "color" || source.kind == "luminance";
  });
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
    DesktopRenderPurpose purpose,
    const QRectF& mask_source_area,
    const QSizeF& mask_basis_pixel_extent,
    const PhotoLayerSelectionSourceProvider& selection_source_provider) {
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
      masks, purpose, raster_policy, raster_size, mask_source_area, mask_basis_pixel_extent);
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
      auto sampling_pixels = source_pixels;
      std::span<const photo_editor::pipeline::ColorPixel> sampling_edge_pixels;
      if (selection_layer_set_uses_color_or_luminance_range(set)) {
        const auto prefix = selection_source_provider
            ? selection_source_provider(set_id, set_name, raster_size)
            : PhotoLayerSelectionSamplingSource{};
        const auto expected_pixels = static_cast<std::size_t>(raster_size.width()) *
            static_cast<std::size_t>(raster_size.height());
        if (prefix.range_pixels.size() != expected_pixels ||
            prefix.edge_pixels.size() != expected_pixels) {
          target->content_error =
              "Could not prepare the full-source color prefix for a linked range mask.";
          return;
        }
        sampling_pixels = prefix.range_pixels;
        sampling_edge_pixels = prefix.edge_pixels;
      }
      const auto alpha = detail::rasterize_layer_selection_alpha(
          set,
          raster_size.width(),
          raster_size.height(),
          sampling_pixels,
          color_pipeline,
          sampling_edge_pixels);
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
    target->mask_data = selection_mask.mask_data;
    target->mask_width = selection_mask.mask_width;
    target->mask_height = selection_mask.mask_height;
    target->mask_inverted = selection_mask.mask_inverted;
    target->mask_opacity = selection_mask.mask_opacity;
    target->mask_density = selection_mask.mask_density;
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

void apply_photo_vector_mask(photo_editor::pipeline::PhotoLayer* target,
    const QVariantMap& layer, const QVariantList& layers, const QSize& output,
    DesktopSourceGeometry geometry) {
  if (!layer.contains("vectorMask")) return;
  vector_masks::PhotoVectorMask mask; QString error;
  if (!vector_masks::from_variant(layer.value("vectorMask").toMap(), &mask, &error)) {
    target->content_error = error.toStdString(); return;
  }
  if (!mask.enabled) return;
  if (!geometry.valid()) geometry = {output, QRect(QPoint(), output)};
  if (!output.isValid() || static_cast<qint64>(output.width()) * output.height() > 64LL * 1024 * 1024) {
    target->content_error = "Vector mask dimensions exceed the supported raster budget."; return;
  }
  const auto placement = vector_mask_placement(layer, layers, geometry, output);
  if (!placement.valid) { target->content_error = "Vector mask placement is invalid."; return; }
  auto transform = placement.local_to_canvas;
  if (mask.basis == vector_masks::CoordinateBasis::SourceDocument) {
    const auto map = [&](QPointF p) { return placement.source_to_canvas.map(QPointF(p.x() * geometry.source_size.width(), p.y() * geometry.source_size.height())); };
    const auto o = map({0, 0}), x = map({1, 0}) - o, y = map({0, 1}) - o;
    transform = QTransform(x.x(), x.y(), y.x(), y.y(), o.x(), o.y());
  }
  const auto path = vector_paths::to_painter_path(mask.path, QSize(1, 1), &error);
  if (!error.isEmpty()) { target->content_error = error.toStdString(); return; }
  QImage raster(output, QImage::Format_ARGB32_Premultiplied);
  if (raster.isNull()) { target->content_error = "Could not allocate vector mask coverage."; return; }
  raster.fill(Qt::transparent); QPainter painter(&raster); painter.setRenderHint(QPainter::Antialiasing);
  painter.setTransform(transform); painter.fillPath(path, Qt::white); painter.end();
  photo_editor::pipeline::PhotoLayer coverage; coverage.mask_width = output.width(); coverage.mask_height = output.height();
  coverage.mask_data.resize(static_cast<std::size_t>(output.width()) * static_cast<std::size_t>(output.height()));
  for (int y = 0; y < output.height(); ++y) {
    const auto* row = reinterpret_cast<const QRgb*>(raster.constScanLine(y));
    for (int x = 0; x < output.width(); ++x) {
      auto alpha = qAlpha(row[x]) / 255.0; if (mask.inverted) alpha = 1.0 - alpha;
      coverage.mask_data[static_cast<std::size_t>(y) * static_cast<std::size_t>(output.width()) + static_cast<std::size_t>(x)] =
          static_cast<std::uint8_t>(std::lround((1.0 - mask.density * (1.0 - alpha)) * 255.0));
    }
  }
  intersect_photo_layer_masks(target, coverage);
}

photo_editor::pipeline::PhotoLayer compile_photo_layer_selection_mask(
    const QVariantMap& layer,
    const QVariantList& masks,
    MaskRasterPolicy raster_policy,
    const QSize& output_size,
    std::span<const photo_editor::pipeline::ColorPixel> source_pixels,
    const photo_editor::pipeline::ColorPipelineConfig& color_pipeline,
    DesktopRenderPurpose purpose,
    bool preserve_layer_mask_inversion = false,
    const QRectF& mask_source_area = {},
    const QSizeF& mask_basis_pixel_extent = {},
    const PhotoLayerSelectionSourceProvider& selection_source_provider = {}) {
  auto selection_layer = layer;
  selection_layer.insert(QStringLiteral("maskInverted"), preserve_layer_mask_inversion &&
      layer.value(QStringLiteral("maskInverted")).toBool());
  selection_layer.insert(QStringLiteral("maskOpacity"), 1.0);
  selection_layer.insert(QStringLiteral("maskDensity"), 1.0);
  photo_editor::pipeline::PhotoLayer selection_mask;
  populate_photo_layer_selection_mask(&selection_mask, selection_layer, masks,
      raster_policy, output_size, source_pixels, color_pipeline, purpose, mask_source_area,
      mask_basis_pixel_extent, selection_source_provider);
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
        raster_policy, output_size, source_pixels, color_pipeline, purpose, mask_source_area,
        mask_basis_pixel_extent, selection_source_provider);
    if (!additional_mask.content_error.empty()) {
      selection_mask.content_error = additional_mask.content_error;
      return selection_mask;
    }
    intersect_photo_layer_masks(&selection_mask, additional_mask);
  }
  return selection_mask;
}

QImage smart_object_image(const QVariantMap& original, const QSize& requested_size, QString* error) {
  const auto layer = smart_object::resolve_instance_source(original, error);
  if (layer.isEmpty()) return {};
  QImage image;
  const auto bytes = layer.value(QStringLiteral("pixelData")).toByteArray();
  const auto encoding = layer.value(QStringLiteral("pixelDataEncoding")).toString();
  if (encoding == QStringLiteral("source-file")) {
    if (!bytes.isEmpty()) {
      const auto suffix = QFileInfo(layer.value(QStringLiteral("smartObject")).toMap()
          .value(QStringLiteral("fileName")).toString()).suffix();
      QTemporaryFile source(QDir::tempPath() + QStringLiteral("/looksy-smart-XXXXXX.") + suffix);
      if (source.open() && source.write(bytes) == bytes.size() && source.flush()) {
        const auto source_path = source.fileName();
        source.close();
        image = decode_photo_layer_source_image(source_path);
      }
    }
  } else if (!bytes.isEmpty()) {
    image = QImage::fromData(bytes);
  } else {
    image = decode_photo_layer_source_image(layer.value(QStringLiteral("sourcePath")).toString());
  }
  if (image.isNull()) { if (error) *error = QStringLiteral("Smart Object content could not be decoded."); return {}; }
  auto frame_size = requested_size;
  if (!frame_size.isValid()) frame_size = QSize(layer.value(QStringLiteral("pixelWidth")).toInt(), layer.value(QStringLiteral("pixelHeight")).toInt());
  if (!frame_size.isValid() || frame_size == image.size()) return image;
  // Replace Contents retains the existing frame, fitting the new aspect ratio
  // inside it instead of stretching or changing the outer transform. Copy
  // float samples directly so signed and extended-range source values do not
  // pass through QPainter's premultiplied compositing path.
  QImage frame(frame_size, QImage::Format_RGBA32FPx4); frame.fill(Qt::transparent);
  if (image.colorSpace().isValid()) frame.setColorSpace(image.colorSpace());
  const auto scaled = image.scaled(frame_size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
  const auto fitted = scaled.format() == QImage::Format_RGBA32FPx4
      ? scaled : scaled.convertToFormat(QImage::Format_RGBA32FPx4);
  if (frame.isNull() || fitted.isNull()) {
    if (error) *error = QStringLiteral("Smart Object source could not be fitted without losing its float samples.");
    return {};
  }
  const auto offset_x = (frame.width() - fitted.width()) / 2;
  const auto offset_y = (frame.height() - fitted.height()) / 2;
  const auto row_bytes = static_cast<std::size_t>(fitted.width()) * sizeof(QRgbaFloat32);
  for (int y = 0; y < fitted.height(); ++y) {
    std::memcpy(frame.scanLine(offset_y + y) +
                    static_cast<qsizetype>(offset_x) *
                        static_cast<qsizetype>(sizeof(QRgbaFloat32)),
                fitted.constScanLine(y), row_bytes);
  }
  return frame;
}

// Cache source glyph pixels, before placement, masks, materials and layer
// effects. All render threads share one 16 MiB budget; interactive movement
// never retains a second full document or an unbounded history of rasters.
struct FlatTextPixelCache {
  QMutex mutex;
  QCache<QByteArray, std::shared_ptr<const WorkingImage>> entries{16 * 1024};
  quint64 font_revision = 0;

  FlatTextPixelCache() {
    if (auto* application = qobject_cast<QGuiApplication*>(QCoreApplication::instance())) {
      QObject::connect(application, &QGuiApplication::fontDatabaseChanged, application, [this]() {
        const QMutexLocker lock(&mutex);
        ++font_revision;
        entries.clear();
      }, Qt::DirectConnection);
    }
  }
};

FlatTextPixelCache& flat_text_pixel_cache() {
  static FlatTextPixelCache cache;
  return cache;
}

struct PreparedPhotoLayerPixelCacheEntry final {
  std::shared_ptr<const WorkingImage> working;
  QPointF crop_offset;
  bool framed_photo = false;
  double framed_transform_scale = 1.0;
};

struct PreparedPhotoLayerPixelCache final {
  QMutex mutex;
  QCache<QByteArray, std::shared_ptr<const PreparedPhotoLayerPixelCacheEntry>> entries{
      128 * 1024};
};

PreparedPhotoLayerPixelCache& prepared_photo_layer_pixel_cache() {
  static PreparedPhotoLayerPixelCache cache;
  return cache;
}

bool source_recipe_has_external_profile(const EditRecipeState& state) {
  // A declared payload hash does not prove the backing file still exists or
  // matches it. Let the renderer validate external profiles on every request.
  return state.camera_dcp_profile_asset.configured() ||
      state.input_icc_profile_asset.configured() ||
      state.preview_icc_profile_asset.configured() ||
      state.proof_icc_profile_asset.configured() ||
      state.export_icc_profile_asset.configured();
}

bool placed_photo_source_cacheable(const QVariantMap& layer) {
  // These variants render through earlier source overrides, so their pixels
  // can depend on payloads that are not represented by the source-file key.
  if (!layer.value(QStringLiteral("smartObject")).toMap().isEmpty() ||
      !layer.value(QStringLiteral("vectorPath")).toMap().isEmpty() ||
      !layer.value(QStringLiteral("pixelData")).toByteArray().isEmpty()) {
    return false;
  }
  const auto recipe = layer.value(QStringLiteral("sourceRecipeJson")).toString();
  if (recipe.trimmed().isEmpty()) return true;
  const auto parsed = parse_adjustment_payload_or_legacy_recipe(recipe);
  return parsed.ok() && parsed.state->layers.isEmpty() &&
      !source_recipe_has_external_profile(*parsed.state);
}

QByteArray placed_photo_source_pixel_cache_key(
    const QVariantMap& layer,
    const QString& working_profile,
    const QFileInfo& source_file) {
  const auto crop = layer.value(QStringLiteral("boardPhotoCrop")).toMap();
  const bool framed_photo = !crop.isEmpty();
  QVariantMap key{
      {QStringLiteral("sourcePath"), QDir::cleanPath(source_file.absoluteFilePath())},
      {QStringLiteral("sourceFileSize"), source_file.size()},
      {QStringLiteral("sourceModifiedMs"), source_file.lastModified().toMSecsSinceEpoch()},
      {QStringLiteral("sourceRecipeJson"), layer.value(QStringLiteral("sourceRecipeJson"))},
      {QStringLiteral("sourceAssetId"), layer.value(QStringLiteral("sourceAssetId"))},
      {QStringLiteral("workingProfile"), working_profile},
      {QStringLiteral("sourcePixelWidth"), layer.value(QStringLiteral("sourcePixelWidth"))},
      {QStringLiteral("sourcePixelHeight"), layer.value(QStringLiteral("sourcePixelHeight"))},
      {QStringLiteral("pixelWidth"), layer.value(QStringLiteral("pixelWidth"))},
      {QStringLiteral("pixelHeight"), layer.value(QStringLiteral("pixelHeight"))},
      {QStringLiteral("boardPhotoCrop"), crop},
      {QStringLiteral("framedPhoto"), framed_photo},
  };
  if (framed_photo) {
    key.insert(QStringLiteral("placementScale"),
        qBound(0.05, layer.value(QStringLiteral("scale"), 1.0).toDouble(), 32.0));
  }
  return QCryptographicHash::hash(
      QJsonDocument::fromVariant(key).toJson(QJsonDocument::Compact),
      QCryptographicHash::Sha256);
}

QByteArray flat_text_pixel_key(const QVariantMap& layer,
    const QString& working_profile, const QSize& raster_size) {
  QVariantMap key{{"workingProfile", working_profile},
      {"rasterWidth", raster_size.width()}, {"rasterHeight", raster_size.height()}};
  // These are the complete inputs to the flat-text rasterizer and perspective
  // warp. Position/rotation/scale and composite-only fields remain downstream.
  static const QStringList fields{"text", "pixelWidth", "pixelHeight", "fontFamily",
      "fontSize", "fontWeight", "textColor", "textAlignment", "textTypography",
      "textOriginX", "textOriginY", "textPerspectiveHorizontal", "textPerspectiveVertical"};
  // Missing fields use rasterizer defaults; an explicitly null field instead
  // converts to an empty string/zero. Preserve that distinction in the key.
  for (const auto& field : fields)
    if (layer.contains(field)) key.insert(field, layer.value(field));
  return QCryptographicHash::hash(QJsonDocument::fromVariant(key).toJson(QJsonDocument::Compact),
      QCryptographicHash::Sha256);
}

void populate_photo_layer_pixels(photo_editor::pipeline::PhotoLayer* target,
    const QVariantMap& layer, const QString& working_profile, const QSize& text_raster_size = {}) {
  if (target == nullptr) {
    return;
  }
  const auto kind = layer.value(QStringLiteral("kind")).toString().trimmed().toLower();
  auto& profile = photo_render_profile_for_tests;
  const auto profile_kind = photo_render_profile_layer_kind(kind);
  const auto profile_index = static_cast<std::size_t>(profile_kind);
  if (profile.enabled)
    ++profile.populate_photo_layer_pixels_calls_by_kind[profile_index];
  PhotoRenderProfileTimerForTests profile_timer(
      profile.enabled ? &profile.populate_photo_layer_pixels_ns_by_kind[profile_index]
                      : nullptr);
  const auto pixel_data = layer.value(QStringLiteral("pixelData")).toByteArray();
  const auto pixel_width = layer.value(QStringLiteral("pixelWidth")).toInt();
  const auto pixel_height = layer.value(QStringLiteral("pixelHeight")).toInt();
  const bool cacheable_text = kind == QStringLiteral("text") &&
      qobject_cast<QGuiApplication*>(QCoreApplication::instance()) != nullptr &&
      pixel_data.isEmpty() && layer.value("smartObject").toMap().isEmpty() &&
      layer.value("vectorPath").toMap().isEmpty() &&
      layer.value("boardPhotoCrop").toMap().isEmpty() && !photo_text_dimension_is_active(layer);
  const auto text_key = cacheable_text
      ? flat_text_pixel_key(layer, working_profile, text_raster_size) : QByteArray{};
  quint64 text_font_revision = 0;
  if (cacheable_text) {
    auto& cache = flat_text_pixel_cache();
    std::shared_ptr<const WorkingImage> cached;
    {
      const QMutexLocker lock(&cache.mutex);
      text_font_revision = cache.font_revision;
      if (const auto* entry = cache.entries.object(text_key)) cached = *entry;
    }
    if (cached) {
      target->pixels = cached->pixels;
      target->pixel_width = cached->width;
      target->pixel_height = cached->height;
      target->x = layer.value("x").toInt();
      target->y = layer.value("y").toInt();
      target->transform_scale = qBound(0.05, layer.value("scale", 1.0).toDouble(), 32.0);
      target->transform_rotation = qBound(-3600.0, layer.value("rotation").toDouble(), 3600.0);
      return;
    }
  }
  QByteArray source_pixel_key;
  std::shared_ptr<const PreparedPhotoLayerPixelCacheEntry> cached_source_pixels;
  if (kind == QStringLiteral("source") && placed_photo_source_cacheable(layer)) {
    QFileInfo source_file(layer.value(QStringLiteral("sourcePath")).toString());
    source_file.refresh();
    if (source_file.isFile() && source_file.isReadable()) {
      source_pixel_key = placed_photo_source_pixel_cache_key(
          layer, working_profile, source_file);
      auto& cache = prepared_photo_layer_pixel_cache();
      const QMutexLocker lock(&cache.mutex);
      if (const auto* entry = cache.entries.object(source_pixel_key))
        cached_source_pixels = *entry;
    }
  }
  if (cached_source_pixels && cached_source_pixels->working &&
      !cached_source_pixels->working->isNull()) {
    const auto placement_scale =
        qBound(0.05, layer.value(QStringLiteral("scale"), 1.0).toDouble(), 32.0);
    target->pixels = cached_source_pixels->working->pixels;
    target->pixel_width = cached_source_pixels->working->width;
    target->pixel_height = cached_source_pixels->working->height;
    target->x = layer.value(QStringLiteral("x")).toInt() +
        qRound(cached_source_pixels->crop_offset.x() * placement_scale);
    target->y = layer.value(QStringLiteral("y")).toInt() +
        qRound(cached_source_pixels->crop_offset.y() * placement_scale);
    target->transform_scale = cached_source_pixels->framed_photo
        ? cached_source_pixels->framed_transform_scale : placement_scale;
    target->transform_rotation = qBound(-3600.0,
        layer.value(QStringLiteral("rotation")).toDouble(), 3600.0);
    return;
  }
  QImage image;
  if (!layer.value(QStringLiteral("smartObject")).toMap().isEmpty()) {
    QString error; image = smart_object_image(layer, text_raster_size, &error); target->content_error = error.toStdString();
  } else if (!layer.value(QStringLiteral("vectorPath")).toMap().isEmpty()) {
    vector_paths::VectorPathDocument path; QString error;
    if (vector_paths::to_document(layer.value(QStringLiteral("vectorPath")).toMap(), &path, &error)) {
      const auto size = text_raster_size.isValid() ? text_raster_size : QSize(pixel_width, pixel_height);
      image = vector_paths::rasterize_fill(path, size, QColor(layer.value("vectorFillEnabled", true).toBool() ? layer.value(QStringLiteral("vectorFillColor"), QStringLiteral("#4a78ff")).toString() : QStringLiteral("transparent")), &error);
      const auto width = layer.value(QStringLiteral("vectorStrokeWidth"), 0.0).toDouble();
      if (!image.isNull() && width > 0 && layer.value("vectorStrokeEnabled", true).toBool()) {
        const auto stroke = vector_paths::rasterize_stroke(path, size,
            QColor(layer.value(QStringLiteral("vectorStrokeColor"), QStringLiteral("#ffffff")).toString()),
            width * size.width() / std::max(1, pixel_width), &error);
        QPainter painter(&image); painter.drawImage(QPoint(), stroke);
      }
    }
    target->content_error = error.toStdString();
  } else if (kind == QStringLiteral("pixel") && (layer.contains("paintTiles") || layer.contains("paintDraftTiles"))) {
    QString paint_error;
    if (layer.contains(QStringLiteral("compiledPaintRasterRegion"))) {
      PhotoPaintSurface surface;
      if (surface.load(layer, &paint_error)) image = surface.raster_region(text_raster_size,
          layer.value(QStringLiteral("compiledPaintRasterRegion")).toRect(), &paint_error,
          layer.value(QStringLiteral("paintDraftTiles")).toMap());
    } else image = rasterize_photo_paint_layer(layer,text_raster_size,&paint_error);
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
    if (photo_text_dimension_is_active(layer)) {
      auto dimension = render_photo_text_dimension(layer, text_raster_size);
      image = std::move(dimension.image);
      target->effect_padding_x = dimension.padding.x();
      target->effect_padding_y = dimension.padding.y();
      text_error = dimension.error;
    } else {
      image = rasterize_legacy_photo_text(layer, text_raster_size, &text_error);
    }
    target->content_error = text_error.toStdString();
  } else if (kind == QStringLiteral("source")) {
    const auto source_path = layer.value(QStringLiteral("sourcePath")).toString();
    if (source_path.isEmpty()) {
      return;
    }
    image = decode_placed_photo_source_image(layer, working_profile);
    if (image.isNull()) {
      target->content_error = "The placed photo's source or saved recipe could not be rendered.";
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
  if (kind == QStringLiteral("text") && !photo_text_dimension_is_active(layer)) {
    image = perspective_warp_text_layer(
        image,
        layer.value(QStringLiteral("textPerspectiveHorizontal"), 0.0).toDouble(),
        layer.value(QStringLiteral("textPerspectiveVertical"), 0.0).toDouble());
  }
  if (kind == QStringLiteral("pixel") && !layer.contains(QStringLiteral("compiledPaintRasterRegion")) &&
      text_raster_size.isValid() && image.size() != text_raster_size)
    image = image.scaled(text_raster_size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
  QPointF crop_offset;
  const auto placement_scale = qBound(0.05, layer.value(QStringLiteral("scale"), 1.0).toDouble(), 32.0);
  const bool framed_photo = !layer.value(QStringLiteral("boardPhotoCrop")).toMap().isEmpty();
  if (framed_photo) {
    const auto crop = board_photo_crop_bounds(layer, image.size()).intersected(QRectF(image.rect()));
    if (crop.isEmpty()) { target->content_error = "The photo layout crop is empty."; return; }
    crop_offset = crop.topLeft();
    // Rasterize the visible frame at document resolution before styling, so a
    // thin border remains exact even when its source is zoomed in substantially.
    QImage frame(qMax(1, qRound(crop.width() * placement_scale)),
        qMax(1, qRound(crop.height() * placement_scale)), image.format());
    frame.setColorSpace(image.colorSpace()); frame.fill(Qt::transparent);
    {
      QPainter painter(&frame);
      painter.setRenderHint(QPainter::SmoothPixmapTransform);
      painter.setCompositionMode(QPainter::CompositionMode_Source);
      painter.drawImage(QRectF(frame.rect()), image, crop);
    }
    image = std::move(frame);
  }
  if (!image.colorSpace().isValid()) {
    image.setColorSpace(preview_target_color_space());
  }
  // Keep camera-characterized and wide-gamut layer values at float32 precision;
  // a half-float intermediate here can differ from ordinary RAW rendering.
  const auto linear = converted_to_color_space(image, working_color_space(working_profile),
      QImage::Format_RGBA32FPx4);
  auto working = working_image_from_qimage(linear, working_profile, nullptr, true);
  if (working.isNull()) {
    return;
  }
  const auto prepared_source_cost_kib = source_pixel_key.isEmpty()
      ? qint64{0} : static_cast<qint64>((working.byteSize() + 1023) / 1024 + 1);
  if (!source_pixel_key.isEmpty() && prepared_source_cost_kib <= 128 * 1024) {
    auto cached_working = std::make_shared<const WorkingImage>(std::move(working));
    auto cached = std::make_shared<const PreparedPhotoLayerPixelCacheEntry>(
        PreparedPhotoLayerPixelCacheEntry{
            .working = std::move(cached_working),
            .crop_offset = crop_offset,
            .framed_photo = framed_photo,
            .framed_transform_scale = 1.0,
        });
    target->pixels = cached->working->pixels;
    target->pixel_width = cached->working->width;
    target->pixel_height = cached->working->height;
    auto& cache = prepared_photo_layer_pixel_cache();
    const QMutexLocker lock(&cache.mutex);
    cache.entries.insert(source_pixel_key,
        new std::shared_ptr<const PreparedPhotoLayerPixelCacheEntry>(std::move(cached)),
        static_cast<int>(prepared_source_cost_kib));
  } else {
    target->pixels = working.pixels;
    target->pixel_width = working.width;
    target->pixel_height = working.height;
  }
  target->x = layer.value(QStringLiteral("x")).toInt() + qRound(crop_offset.x() * placement_scale);
  target->y = layer.value(QStringLiteral("y")).toInt() + qRound(crop_offset.y() * placement_scale);
  target->transform_scale = framed_photo ? 1.0 : placement_scale;
  target->transform_rotation = qBound(-3600.0, layer.value(QStringLiteral("rotation")).toDouble(), 3600.0);
  if (cacheable_text) {
    auto& cache = flat_text_pixel_cache();
    // Include a small charge for the key and entry metadata; never insert a
    // full-resolution title larger than the entire shared cache budget.
    const auto cost_kib = (working.byteSize() + 1023) / 1024 + 1;
    if (cost_kib <= 16 * 1024) {
      auto pixels = std::make_shared<const WorkingImage>(std::move(working));
      const QMutexLocker lock(&cache.mutex);
      if (cache.font_revision == text_font_revision)
        cache.entries.insert(text_key, new std::shared_ptr<const WorkingImage>(std::move(pixels)),
            static_cast<int>(cost_kib));
    }
  }
}

bool component_range_source_alpha(
    const QVariantMap& mask,
    QByteArray* alpha_data,
    int* alpha_width,
    int* alpha_height) {
  if (alpha_data == nullptr || alpha_width == nullptr || alpha_height == nullptr) {
    return false;
  }
  const auto data = mask.value(QStringLiteral("componentRangeSourceData")).toByteArray();
  const auto width = mask.value(QStringLiteral("componentRangeSourceWidth")).toInt();
  const auto height = mask.value(QStringLiteral("componentRangeSourceHeight")).toInt();
  const auto version = mask.value(QStringLiteral("componentRangeSourceVersion")).toInt();
  if (!valid_component_range_source_snapshot(data, width, height, version)) {
    return false;
  }

  const auto kind = normalized_mask_kind(mask.value(QStringLiteral("kind")).toString());
  const auto key_material = data + QByteArrayLiteral("|") + kind.toUtf8() + QByteArrayLiteral("|") +
      QByteArray::number(mask.value(QStringLiteral("hueCenter")).toDouble(), 'g', 17) + QByteArrayLiteral("|") +
      QByteArray::number(mask.value(QStringLiteral("hueRange")).toDouble(), 'g', 17) + QByteArrayLiteral("|") +
      QByteArray::number(mask.value(QStringLiteral("saturationMin")).toDouble(), 'g', 17) + QByteArrayLiteral("|") +
      QByteArray::number(mask.value(QStringLiteral("saturationMax")).toDouble(), 'g', 17) + QByteArrayLiteral("|") +
      QByteArray::number(mask.value(QStringLiteral("luminanceMin")).toDouble(), 'g', 17) + QByteArrayLiteral("|") +
      QByteArray::number(mask.value(QStringLiteral("luminanceMax")).toDouble(), 'g', 17) + QByteArrayLiteral("|") +
      QByteArray::number(mask.value(QStringLiteral("rangeFeather")).toDouble(), 'g', 17) +
      QByteArrayLiteral("|") + mask.value(QStringLiteral("colorSampleMode")).toString().toUtf8() +
      QByteArrayLiteral("|") + QJsonDocument(QJsonArray::fromVariantList(
          mask.value(QStringLiteral("colorSamples")).toList())).toJson(QJsonDocument::Compact);
  const auto cache_key = QString::fromLatin1(
      QCryptographicHash::hash(key_material, QCryptographicHash::Sha256).toHex());
  struct Cache final {
    QMutex mutex;
    QHash<QString, QByteArray> values;
    QStringList insertion_order;
    qsizetype bytes = 0;
  };
  static Cache cache;
  {
    QMutexLocker locker(&cache.mutex);
    const auto found = cache.values.constFind(cache_key);
    if (found != cache.values.cend()) {
      *alpha_data = found.value();
      *alpha_width = width;
      *alpha_height = height;
      return true;
    }
  }

  QBuffer buffer;
  buffer.setData(data);
  if (!buffer.open(QIODevice::ReadOnly)) {
    return false;
  }
  QImageReader reader(&buffer, "png");
  reader.setAutoTransform(false);
  const auto snapshot = reader.read();
  if (snapshot.isNull() || snapshot.width() != width || snapshot.height() != height) {
    return false;
  }
  const auto working = working_image_from_qimage(snapshot, QStringLiteral("Linear ProPhoto RGB"));
  if (working.isNull()) {
    return false;
  }

  auto sample_definition = mask;
  sample_definition.insert(QStringLiteral("operation"), QStringLiteral("add"));
  sample_definition.insert(QStringLiteral("blendMode"), QStringLiteral("normal"));
  sample_definition.insert(QStringLiteral("enabled"), true);
  sample_definition.insert(QStringLiteral("invert"), false);
  sample_definition.insert(QStringLiteral("opacity"), 1.0);
  sample_definition.insert(QStringLiteral("density"), 1.0);
  sample_definition.insert(QStringLiteral("selectionInvert"), false);
  for (const auto* key : {"selectionExpand", "selectionSmooth", "selectionFineEdge",
                           "selectionEdgeContrast", "selectionDecontaminate", "selectionGrow",
                           "selectionShrink", "selectionFeather", "selectionBlur",
                           "selectionHarden", "selectionDespeckle", "selectionFillHoles",
                           "selectionRemoveIslands", "refineEdgeConfidenceMin"}) {
    sample_definition.insert(QString::fromLatin1(key), 0.0);
  }
  sample_definition.insert(QStringLiteral("refineLuminanceMin"), 0.0);
  sample_definition.insert(QStringLiteral("refineLuminanceMax"), 1.0);
  sample_definition.insert(QStringLiteral("refineHueRange"), 0.5);
  sample_definition.insert(QStringLiteral("refineSaturationMin"), 0.0);
  sample_definition.insert(QStringLiteral("refineSaturationMax"), 1.0);
  sample_definition.insert(QStringLiteral("refineDepthMin"), 0.0);
  sample_definition.insert(QStringLiteral("refineDepthMax"), 1.0);
  const auto local_mask = pipeline_local_mask(
      sample_definition,
      MaskRasterPolicy::UseStoredRaster,
      QSize(width, height),
      QRectF(0.0, 0.0, 1.0, 1.0),
      {});
  photo_editor::pipeline::LocalMaskSet mask_set;
  mask_set.id = "component-range-snapshot";
  mask_set.name = "Component range snapshot";
  mask_set.sources.push_back(local_mask);
  auto sampled_alpha = photo_editor::pipeline::rasterize_local_mask_set_alpha(
      mask_set,
      width,
      height,
      working.pixels,
      photo_editor::pipeline::default_color_pipeline_config());
  if (sampled_alpha.size() != working.pixels.size()) {
    return false;
  }
  QByteArray alpha(static_cast<qsizetype>(sampled_alpha.size()), Qt::Uninitialized);
  for (std::size_t index = 0; index < sampled_alpha.size(); ++index) {
    const auto source_alpha = std::clamp(
        static_cast<double>(working.pixels[index].alpha), 0.0, 1.0);
    alpha[static_cast<qsizetype>(index)] = static_cast<char>(std::lround(
        static_cast<double>(sampled_alpha[index]) * source_alpha));
  }
  {
    QMutexLocker locker(&cache.mutex);
    if (const auto existing = cache.values.find(cache_key); existing != cache.values.end()) {
      *alpha_data = existing.value();
    } else {
      while (!cache.insertion_order.isEmpty() &&
             (cache.values.size() >= 8 || cache.bytes + alpha.size() > 8LL * 1024LL * 1024LL)) {
        const auto oldest = cache.insertion_order.takeFirst();
        cache.bytes -= cache.values.value(oldest).size();
        cache.values.remove(oldest);
      }
      cache.values.insert(cache_key, alpha);
      cache.insertion_order.append(cache_key);
      cache.bytes += alpha.size();
      *alpha_data = std::move(alpha);
    }
  }
  *alpha_width = width;
  *alpha_height = height;
  return true;
}

}  // namespace

void begin_photo_render_stage_profile_for_tests() {
  photo_render_profile_for_tests = {};
  photo_render_profile_for_tests.enabled = true;
}

QVariantMap end_photo_render_stage_profile_for_tests() {
  auto& profile = photo_render_profile_for_tests;
  profile.enabled = false;
  const std::array<QString,
      static_cast<std::size_t>(PhotoRenderProfileLayerKind::count)> layer_kind_names{
      QStringLiteral("text"), QStringLiteral("source"), QStringLiteral("vector"),
      QStringLiteral("pixel"), QStringLiteral("fill"), QStringLiteral("group"),
      QStringLiteral("adjustment"), QStringLiteral("other")};
  QVariantMap populate_ns_by_kind;
  QVariantMap populate_calls_by_kind;
  for (std::size_t index = 0; index < layer_kind_names.size(); ++index) {
    populate_ns_by_kind.insert(
        layer_kind_names[index], profile.populate_photo_layer_pixels_ns_by_kind[index]);
    populate_calls_by_kind.insert(
        layer_kind_names[index], profile.populate_photo_layer_pixels_calls_by_kind[index]);
  }
  return {
      {QStringLiteral("compileExportPipelineStateNs"),
       profile.compile_export_pipeline_state_ns},
      {QStringLiteral("applyColorAdjustmentsNs"),
       profile.apply_color_adjustments_ns},
      {QStringLiteral("expandSmartDocumentsNs"), profile.expand_smart_documents_ns},
      {QStringLiteral("expandSmartDocumentsCalls"), profile.expand_smart_documents_calls},
      {QStringLiteral("expandedSmartDocumentLayers"),
       profile.expanded_smart_document_layers},
      {QStringLiteral("expandedSmartDocumentMasks"),
       profile.expanded_smart_document_masks},
      {QStringLiteral("expansionForPhotoLayerCompileCalls"),
       profile.expansion_for_photo_layer_compile_calls},
      {QStringLiteral("expansionForDisplayTextCalls"),
       profile.expansion_for_display_text_calls},
      {QStringLiteral("compilePipelinePhotoLayersNs"),
       profile.compile_pipeline_photo_layers_ns},
      {QStringLiteral("compilePipelinePhotoLayersCalls"),
       profile.compile_pipeline_photo_layers_calls},
      {QStringLiteral("compiledPhotoLayerCount"), profile.compiled_photo_layer_count},
      {QStringLiteral("populatePhotoLayerPixelsNsByKind"), populate_ns_by_kind},
      {QStringLiteral("populatePhotoLayerPixelsCallsByKind"), populate_calls_by_kind},
  };
}

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

QImage decode_placed_photo_source_image(const QVariantMap& layer, const QString& working_profile) {
  const auto path = layer.value(QStringLiteral("sourcePath")).toString();
  const auto recipe = layer.value(QStringLiteral("sourceRecipeJson")).toString();
  if (recipe.trimmed().isEmpty()) return decode_photo_layer_source_image(path);
  const auto parsed = parse_adjustment_payload_or_legacy_recipe(recipe);
  if (!parsed.ok()) return {};
  static thread_local int depth = 0;
  if (depth >= 8) return {};
  QScopedValueRollback<int> recursion(depth, depth + 1);
  const QFileInfo file(path);
  const auto key = QString::fromLatin1(QCryptographicHash::hash((file.absoluteFilePath() + recipe + working_profile +
      QString::number(file.size()) + QString::number(file.lastModified().toMSecsSinceEpoch())).toUtf8(), QCryptographicHash::Sha256).toHex());
  static thread_local QCache<QString, QImage> cache(128 * 1024);
  // Nested layer files can change independently of the primary photo.
  const auto cacheable = parsed.state->layers.isEmpty() &&
      !source_recipe_has_external_profile(*parsed.state);
  if (cacheable) if (const auto* cached = cache.object(key)) return *cached;
  const auto image = render_flattened_output_file(layer.value(QStringLiteral("sourceAssetId")).toString(), path,
      *parsed.state, working_color_space(working_profile), QImage::Format_RGBA32FPx4);
  if (cacheable && !image.isNull())
    cache.insert(key, new QImage(image), static_cast<int>(qMax<qint64>(1, image.sizeInBytes() / 1024)));
  return image;
}

std::vector<photo_editor::pipeline::PhotoLayer> compile_pipeline_photo_layers(
    const QVariantList& authored_layers, const QVariantList& authored_masks,
    LocalMaskRasterPolicy raster_policy, const QSize& output_size,
    std::span<const photo_editor::pipeline::ColorPixel> source_pixels,
    const photo_editor::pipeline::ColorPipelineConfig& color_pipeline,
    DesktopRenderPurpose purpose,
    const DesktopSourceGeometry& source_geometry,
    const PhotoLayerSelectionSourceProvider& selection_source_provider,
    double document_layer_style_light_angle) {
  auto& profile = photo_render_profile_for_tests;
  if (profile.enabled) ++profile.compile_pipeline_photo_layers_calls;
  PhotoRenderProfileTimerForTests profile_timer(
      profile.enabled ? &profile.compile_pipeline_photo_layers_ns : nullptr);
  if (profile.enabled) ++profile.expansion_for_photo_layer_compile_calls;
  const auto expanded = expand_smart_documents(authored_layers, authored_masks,
      document_layer_style_light_angle);
  if (!expanded.error.isEmpty()) {
    photo_editor::pipeline::PhotoLayer failure;
    failure.kind = "pixel"; failure.content_error = expanded.error.toStdString();
    return {failure};
  }
  const auto& layers = expanded.layers; const auto& masks = expanded.masks;
  const auto mask_basis_pixel_extent = source_geometry.valid()
      ? QSizeF(source_geometry.source_size.width() - 1, source_geometry.source_size.height() - 1) : QSizeF{};
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
    QString blend_if_error;
    const auto blend_if = blend_if_from_variant(layer.value(QStringLiteral("blendIf")).toMap(), &blend_if_error);
    if (blend_if) photo_layer.blend_if = *blend_if;
    else photo_layer.content_error = blend_if_error.toStdString();
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
    if (kind == QStringLiteral("text") && layer.value("textDimension").toMap().value("enabled").toBool())
      photo_layer.text_material.clear();  // Dimensional surfaces own their lighting/materials.
    photo_layer.copy_base_image = layer.value(QStringLiteral("copyBaseImage")).toBool();
    photo_layer.copy_base_image_through_adjustments =
        layer.value(QStringLiteral("copyBaseImageThroughAdjustments")).toBool();
    populate_photo_layer_mask(&photo_layer, layer);
    if (kind == QStringLiteral("group")) {
      const auto component_retouch_masks =
          layer.value(QStringLiteral("_smartDocumentPostCompositeRetouchMasks")).toList();
      static_cast<void>(compile_pipeline_retouch_masks(
          component_retouch_masks, internal_raster_policy, output_size,
          source_geometry.mask_area(), mask_basis_pixel_extent,
          &photo_layer.component_retouch_masks));
      photo_layer.component_retouch_final_quality =
          purpose == DesktopRenderPurpose::Output;
    }
    if (layer.value(QStringLiteral("selectionScoped")).toBool() &&
        layer.value(QStringLiteral("maskEnabled"), true).toBool()) {
      const auto selection_mask = compile_photo_layer_selection_mask(layer, masks,
          internal_raster_policy, output_size, source_pixels, color_pipeline, purpose,
          photo_layer.mask_data.empty(), source_geometry.mask_area(), mask_basis_pixel_extent,
          selection_source_provider);
      if (!selection_mask.content_error.empty()) {
        photo_layer.content_error = selection_mask.content_error;
      }
      if (!selection_mask.mask_data.empty()) {
        if (photo_layer.mask_data.empty()) {
          photo_layer.mask_data = selection_mask.mask_data;
          photo_layer.mask_width = selection_mask.mask_width;
          photo_layer.mask_height = selection_mask.mask_height;
          photo_layer.mask_inverted = selection_mask.mask_inverted;
          // Selection coverage is compiled without owner-level strength so
          // multiple attachments intersect before that strength applies once.
          photo_layer.mask_opacity = std::clamp(
              layer.value(QStringLiteral("maskOpacity"), 1.0).toDouble(), 0.0, 1.0);
          photo_layer.mask_density = std::clamp(
              layer.value(QStringLiteral("maskDensity"), 1.0).toDouble(), 0.0, 1.0);
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
      auto sx = static_cast<double>(output_size.width()) / canvas_width;
      auto sy = static_cast<double>(output_size.height()) / canvas_height;
      auto offset_x = 0.0, offset_y = 0.0;
      const auto basis = layer.value(QStringLiteral("coordinateArea")).toMap();
      const auto current = source_geometry.edge_area();
      if (layer.value(QStringLiteral("coordinateSpace")).toString() == QStringLiteral("source-area") &&
          basis.value("width").toDouble() > 0 && basis.value("height").toDouble() > 0) {
        sx *= basis.value("width").toDouble() / current.width();
        sy *= basis.value("height").toDouble() / current.height();
        offset_x = (basis.value("x").toDouble() - current.x()) / current.width() * output_size.width();
        offset_y = (basis.value("y").toDouble() - current.y()) / current.height() * output_size.height();
      }
      text_raster_size = QSize(qMax(1, qRound(layer.value("pixelWidth").toInt() * sx)),
          qMax(1, qRound(layer.value("pixelHeight").toInt() * sy)));
      raster_layer.insert("x", qRound(layer.value("x").toInt() * sx + offset_x));
      raster_layer.insert("y", qRound(layer.value("y").toInt() * sy + offset_y));
      bool has_active_layer_styles = false;
      const auto style_payload = layer.value(QStringLiteral("layerStyleStack"));
      if (style_payload.metaType().id() == QMetaType::QString) {
        std::string style_error;
        const auto style_stack = photo_editor::pipeline::deserialize_layer_style_stack(
            style_payload.toString().toStdString(), &style_error);
        has_active_layer_styles = style_stack && style_stack->enabled &&
            std::any_of(style_stack->operations.cbegin(),
                style_stack->operations.cend(), [](const auto& operation) {
                  return operation.enabled && operation.opacity > 0.0;
                });
      }
      const auto clip_identity_paint = [&](QVariantMap* content) {
        if (content == nullptr || content->value("scale", 1.0).toDouble() != 1.0 ||
            content->value("rotation").toDouble() != 0.0) return;
        // Layer styles derive gradients, strokes and shadow extents from the
        // authored paint bounds. Keep the full paint frame so a viewport crop
        // cannot shift that content frame before style rasterization.
        if (has_active_layer_styles) return;
        if (!content->contains("paintTiles") && !content->contains("paintDraftTiles")) return;
        const auto origin = QPoint(content->value("x").toInt(), content->value("y").toInt());
        auto visible = QRect(QPoint(), output_size).translated(-origin)
            .intersected(QRect(QPoint(), text_raster_size));
        if (visible == QRect(QPoint(), text_raster_size)) return;
        // Source coordinates can span a large original canvas after cropping.
        // Raster only the visible window, on the same global sample grid.
        if (visible.isEmpty()) visible = QRect(0, 0, 1, 1);
        content->insert("compiledPaintRasterRegion", visible);
        content->insert("x", origin.x() + visible.x());
        content->insert("y", origin.y() + visible.y());
      };
      if (photo_layer.copy_base_image && photo_layer.erase_base_image && !basis.isEmpty()) {
        // Erase coverage edits the developed source BEFORE the movable Base
        // Image row is transformed. Keep that source basis separate from the
        // row's placement, otherwise moving/cropping the row transforms its
        // eraser twice or shifts the developed source itself.
        auto erase = photo_layer;
        erase.copy_base_image = false;
        erase.copy_base_image_through_adjustments = false;
        erase.id += ":source-erase";
        erase.opacity = 1.0;
        erase.mask_data.clear();
        erase.mask_width = 0;
        erase.mask_height = 0;
        erase.mask_inverted = false;
        auto coverage_layer = raster_layer;
        coverage_layer.insert("x", qRound(offset_x));
        coverage_layer.insert("y", qRound(offset_y));
        coverage_layer.insert("scale", 1.0);
        coverage_layer.insert("rotation", 0.0);
        clip_identity_paint(&coverage_layer);
        populate_photo_layer_pixels(&erase, coverage_layer,
            QString::fromStdString(color_pipeline.working_profile.profile_name), text_raster_size);
        photo_layers.push_back(std::move(erase));
        raster_layer.insert("x", qRound(layer.value("x").toInt() * output_size.width() / static_cast<double>(canvas_width)));
        raster_layer.insert("y", qRound(layer.value("y").toInt() * output_size.height() / static_cast<double>(canvas_height)));
        // An empty coverage raster retains the movable Base Image identity;
        // the separate source-erase row above owns its immutable paint tiles.
        raster_layer.remove("paintTiles"); raster_layer.remove("paintDraftTiles");
        raster_layer.remove("pixelData"); raster_layer.remove("paintDraftImage");
        text_raster_size = {};
      } else clip_identity_paint(&raster_layer);
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
      // The movable Base Image row also carries the sparse eraser raster.
      // Compile that coverage before the compositor replaces this row's
      // pixels with the developed source image.
      if (photo_layer.erase_base_image && photo_layer.enabled &&
          photo_layer.opacity > 0.0 && photo_layer.content_error.empty()) {
        populate_photo_layer_pixels(&photo_layer, raster_layer,
            QString::fromStdString(color_pipeline.working_profile.profile_name), text_raster_size);
      }
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
    apply_photo_vector_mask(&photo_layer, layer, layers, output_size, source_geometry);
    const auto frame_style_scale = layer.value(QStringLiteral("boardPhotoCrop")).toMap().isEmpty()
        ? 1.0 : layer.value(QStringLiteral("scale"), 1.0).toDouble();
    compile_photo_layer_styles(&photo_layer, layer, color_pipeline,
        frame_style_scale * (canvas_width > 0 ? static_cast<double>(output_size.width()) / canvas_width : 1.0),
        frame_style_scale * (canvas_height > 0 ? static_cast<double>(output_size.height()) / canvas_height : 1.0));
    QString effect_error;
    if (auto effects = decode_photo_layer_effects(layer, &effect_error)) {
      photo_layer.effects = std::move(*effects);
      if (!bind_photo_effect_maps(&photo_layer.effects, layer, &effect_error))
        photo_layer.effect_error = effect_error.toStdString();
    } else {
      photo_layer.effect_error = effect_error.toStdString();
    }
    if ((!photo_layer.effects.empty() || !photo_layer.style_stack.operations.empty()) && output_size.isValid()) {
      photo_layer.effect_scale_x = canvas_width > 0 ? static_cast<double>(output_size.width()) / canvas_width : 1;
      photo_layer.effect_scale_y = canvas_height > 0 ? static_cast<double>(output_size.height()) / canvas_height : 1;
      std::vector<std::string> mask_refs;
      for (const auto& effect : photo_layer.effects)
        if (effect.enabled && effect.amount != 0 && !effect.mask_ref.empty()) mask_refs.push_back(effect.mask_ref);
      if (photo_layer.style_stack.enabled)
        for (const auto& style : photo_layer.style_stack.operations)
          if (style.enabled && style.opacity > 0 && !style.mask_ref.empty()) mask_refs.push_back(style.mask_ref);
      for (const auto& mask_ref : mask_refs) {
        if (
            std::any_of(photo_layer.effect_masks.begin(), photo_layer.effect_masks.end(),
                [&](const auto& mask) { return mask.id == mask_ref; })) continue;
        photo_editor::pipeline::PhotoLayer mask_layer;
        const QVariantMap mask_target{{QStringLiteral("selectionScoped"), true},
            {QStringLiteral("selectionMaskSetId"), QString::fromStdString(mask_ref)}};
        populate_photo_layer_selection_mask(&mask_layer, mask_target, masks, internal_raster_policy,
            output_size, source_pixels, color_pipeline, purpose, source_geometry.mask_area(),
            mask_basis_pixel_extent, selection_source_provider);
        if (!mask_layer.content_error.empty()) {
          photo_layer.effect_error = mask_layer.content_error;
          break;
        }
        if (mask_layer.mask_data.empty() || mask_layer.mask_width != output_size.width() ||
            mask_layer.mask_height != output_size.height()) {
          photo_layer.effect_error = "Could not resolve layer effect/style mask '" + mask_ref + "'.";
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
        resolved.id = mask_ref;
        resolved.coverage.resize(static_cast<std::size_t>(mask_width) * static_cast<std::size_t>(mask_height));
        const auto radians = photo_layer.transform_rotation * std::acos(-1.0) / 180.0;
        const auto scale = photo_layer.transform_scale;
        for (int y = 0; y < mask_height; ++y) {
          for (int x = 0; x < mask_width; ++x) {
            double doc_x = x, doc_y = y;
            if (raster) {
              const auto dx = (x + 0.5 - mask_width * 0.5) * scale;
              const auto dy = (y + 0.5 - mask_height * 0.5) * scale;
              doc_x = photo_layer.x - photo_layer.effect_padding_x * scale + mask_width * scale * 0.5 + dx * std::cos(radians) - dy * std::sin(radians) - 0.5;
              doc_y = photo_layer.y - photo_layer.effect_padding_y * scale + mask_height * scale * 0.5 + dx * std::sin(radians) + dy * std::cos(radians) - 0.5;
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
  if (profile.enabled)
    profile.compiled_photo_layer_count +=
        static_cast<qint64>(photo_layers.size());
  return photo_layers;
}
namespace {

bool has_linked_live_range_selection(const RenderEditState& state) {
  for (const auto& value : state.mask_stack) {
    const auto mask = value.toMap();
    const auto id = mask.value(QStringLiteral("maskSetId")).toString().simplified();
    const auto name = mask.value(QStringLiteral("maskSetName")).toString().simplified();
    const auto kind = normalized_mask_kind(mask.value(QStringLiteral("kind")).toString());
    if ((kind != QStringLiteral("color") && kind != QStringLiteral("luminance")) ||
        mask.value(QStringLiteral("componentRangeSourceRequired")).toBool()) continue;
    for (const auto& layer_value : state.layers) {
      const auto layer = layer_value.toMap();
      if (!layer.value(QStringLiteral("selectionScoped")).toBool()) continue;
      const auto matches_reference_domain = [&](const QStringList& ids,
                                                const QStringList& names) {
        return (!id.isEmpty() && ids.contains(id)) ||
            (!name.isEmpty() && (id.isEmpty() || ids.isEmpty()) && names.contains(name));
      };
      const auto primary_matches = matches_reference_domain(
          selection_mask_set_ids_for_layer(layer),
          selection_mask_set_names_for_layer(layer));
      const auto intersection_matches = matches_reference_domain(
          normalized_string_list_value(
              layer, QStringLiteral("selectionIntersectionMaskSetIds"), QString()),
          normalized_string_list_value(
              layer, QStringLiteral("selectionIntersectionMaskSetNames"), QString()));
      if (primary_matches || intersection_matches) return true;
    }
  }
  return false;
}

photo_editor::pipeline::ColorAdjustmentState compile_export_pipeline_state(
    const RenderEditState& state,
    const photo_editor::pipeline::DecodedImageMetadata* metadata = nullptr,
    MaskRasterPolicy raster_policy = MaskRasterPolicy::UseStoredRaster,
    const QSize& output_size = {},
    std::span<const photo_editor::pipeline::ColorPixel> source_pixels = {},
    const photo_editor::pipeline::DisplayTarget* export_display_target_override = nullptr,
    const DesktopSourceGeometry& source_geometry = {},
    const QImage* full_source = nullptr,
    photo_editor::pipeline::CancellationToken cancellation = {}) {
  auto color_pipeline_config = compile_color_pipeline_config(state, metadata);
  if (export_display_target_override != nullptr) {
    color_pipeline_config.export_display_target = *export_display_target_override;
    color_pipeline_config.export_output_profile =
        photo_editor::pipeline::output_color_profile_from_name("Rec.2020");
  }
  return compile_desktop_pipeline_state(
      state, metadata, color_pipeline_config,
      DesktopRenderPurpose::Output, raster_policy, output_size,
      source_pixels, source_geometry, full_source, cancellation);
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
    const QSize& output_size,
    const QRectF& source_area,
    const QSizeF& original_basis_pixel_extent) {
  auto area=QRectF(0.0,0.0,1.0,1.0);
  auto basis_pixel_extent=original_basis_pixel_extent;
  const auto coordinate_space=mask.value(QStringLiteral("coordinateSpace")).toString();
  const auto source_coordinates=coordinate_space==QStringLiteral("source") ||
      coordinate_space==QStringLiteral("source-area") ||
      (coordinate_space.isEmpty() && mask.value(QStringLiteral("toolRole")).toString()==QStringLiteral("retouch"));
  if (!source_coordinates) basis_pixel_extent={};
  if (source_coordinates && source_area.width()>=0.0 && source_area.height()>=0.0) {
    auto basis=QRectF(0.0,0.0,1.0,1.0);
    if (coordinate_space==QStringLiteral("source-area")) {
      const auto value=mask.value(QStringLiteral("coordinateArea")).toMap();
      const QRectF recorded(value.value("x").toDouble(),value.value("y").toDouble(),
          value.value("width").toDouble(),value.value("height").toDouble());
      if (recorded.isValid()) basis=recorded;
    }
    area=QRectF((source_area.x()-basis.x())/basis.width(),
        (source_area.y()-basis.y())/basis.height(),
        source_area.width()/basis.width(),source_area.height()/basis.height());
    if (basis_pixel_extent.isValid()) {
      basis_pixel_extent.setWidth(basis_pixel_extent.width()*basis.width());
      basis_pixel_extent.setHeight(basis_pixel_extent.height()*basis.height());
    }
  }
  auto compiled_mask = mask;
  const auto normalized_kind = normalized_mask_kind(mask.value(QStringLiteral("kind")).toString());
  if ((normalized_kind == QStringLiteral("color") ||
       normalized_kind == QStringLiteral("luminance")) &&
      mask.value(QStringLiteral("componentRangeSourceRequired")).toBool()) {
    QByteArray alpha_data;
    auto alpha_width = 0;
    auto alpha_height = 0;
    if (!component_range_source_alpha(mask, &alpha_data, &alpha_width, &alpha_height)) {
      // A component range mask without its captured pixels must never sample
      // the synthetic transparent source or silently change meaning.
      alpha_data = QByteArray(1, '\0');
      alpha_width = 1;
      alpha_height = 1;
    }
    // Brush-raster sampling preserves authored soft coverage literally. The
    // alpha/AI path thresholds around 0.5 and would reshape range feathering.
    compiled_mask.insert(QStringLiteral("kind"), QStringLiteral("brush"));
    compiled_mask.insert(QStringLiteral("segmentationData"), alpha_data);
    compiled_mask.insert(QStringLiteral("segmentationWidth"), alpha_width);
    compiled_mask.insert(QStringLiteral("segmentationHeight"), alpha_height);
    compiled_mask.insert(QStringLiteral("feather"), 0.0);
  }
  auto result = pipeline_local_mask(
      compiled_mask,
      raster_policy == LocalMaskRasterPolicy::RebuildEditableAtOutputResolution
          ? MaskRasterPolicy::RebuildEditableAtOutputResolution
          : MaskRasterPolicy::UseStoredRaster,
      output_size,area,basis_pixel_extent);
  if (area!=QRectF(0.0,0.0,1.0,1.0)) {
    if (!result.inpaint_patch_pixels.empty()) {
      const auto extent_x=basis_pixel_extent.width()>=0.0 ? basis_pixel_extent.width()
          : (output_size.width()>1 && area.width()>0.0
              ? static_cast<double>(output_size.width()-1)/area.width()
              : static_cast<double>(std::max(0,result.inpaint_source_width-1)));
      const auto extent_y=basis_pixel_extent.height()>=0.0 ? basis_pixel_extent.height()
          : (output_size.height()>1 && area.height()>0.0
              ? static_cast<double>(output_size.height()-1)/area.height()
              : static_cast<double>(std::max(0,result.inpaint_source_height-1)));
      // Masks use sample-center endpoints; baked patch rectangles use pixel
      // edges. Recover their physical crop, including a single-pixel axis.
      result.inpaint_source_area_x=area.x()*extent_x/(extent_x+1.0);
      result.inpaint_source_area_y=area.y()*extent_y/(extent_y+1.0);
      result.inpaint_source_area_width=(area.width()*extent_x+1.0)/(extent_x+1.0);
      result.inpaint_source_area_height=(area.height()*extent_y+1.0)/(extent_y+1.0);
    }
    if (result.kind!="brush" ||
        (is_raster_mask_kind(normalized_kind) && mask.value("selectionFeather").toDouble() > 0.0)) {
      result.coordinate_area_x=area.x(); result.coordinate_area_y=area.y();
      result.coordinate_area_width=area.width(); result.coordinate_area_height=area.height();
    }
    const auto point=[&](auto& value) {
      value.x=area.width()>0.0 ? (value.x-area.x())/area.width() :
          0.5+(value.x-area.x())*std::max(1.0,basis_pixel_extent.width());
      value.y=area.height()>0.0 ? (value.y-area.y())/area.height() :
          0.5+(value.y-area.y())*std::max(1.0,basis_pixel_extent.height());
    };
    for (auto& value:result.brush_points) point(value);
    for (auto& stroke:result.clone_strokes) {
      for (auto& value:stroke.points) point(value);
      const auto sx=area.width()>0.0 ? 1.0/area.width() : std::max(1.0,basis_pixel_extent.width());
      const auto sy=area.height()>0.0 ? 1.0/area.height() : std::max(1.0,basis_pixel_extent.height());
      stroke.source_x=(stroke.source_x-area.x())*sx+(area.width()>0.0 ? 0.0 : 0.5);
      stroke.source_y=(stroke.source_y-area.y())*sy+(area.height()>0.0 ? 0.0 : 0.5);
      stroke.offset_x*=sx; stroke.offset_y*=sy;
      stroke.raster_width_scale=sx;
      stroke.raster_height_scale=sy;
    }
    const auto sx=area.width()>0.0 ? 1.0/area.width() : std::max(1.0,basis_pixel_extent.width());
    const auto sy=area.height()>0.0 ? 1.0/area.height() : std::max(1.0,basis_pixel_extent.height());
    result.clone_source_x=(result.clone_source_x-area.x())*sx+(area.width()>0.0 ? 0.0 : 0.5);
    result.clone_source_y=(result.clone_source_y-area.y())*sy+(area.height()>0.0 ? 0.0 : 0.5);
    result.clone_offset_x*=sx; result.clone_offset_y*=sy;
  }
  return result;
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
  const QImage* sampling_source = !mask_options.full_source.isNull()
      ? &mask_options.full_source
      : ((!mask_options.source_geometry.valid() ||
          image.size() == mask_options.source_geometry.source_size)
             ? &image
             : nullptr);
  auto pipeline_state =
      compile_export_pipeline_state(
          state,
          metadata,
          raster_policy,
          QSize(working.width, working.height),
          working.pixels, nullptr, mask_options.source_geometry, sampling_source);
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

QImage prepare_brush_color_source_image(
    const QImage& decoded,const RenderEditState& state,
    const photo_editor::pipeline::DecodedImageMetadata& metadata,
    photo_editor::pipeline::CancellationToken cancellation) {
  if (decoded.isNull() || cancellation.cancelled()) return {};
  if (metadata.is_raw && !photo_editor::pipeline::decoded_image_color_boundary_valid(metadata)) return {};
  const auto pipeline=compile_desktop_source_prefix_state(state,&metadata);
  const QColorSpace srgb_linear(QColorSpace::SRgbLinear);
  if (!photo_editor::pipeline::decoded_image_requires_engine_input_transform(metadata) &&
      pipeline.color_pipeline.input_icc_profile_asset.status==
          photo_editor::pipeline::IccProfileAssetStatus::Unconfigured) {
    if (!decoded.colorSpace().isValid()) return {};
    if (decoded.colorSpace()==srgb_linear) return decoded.convertToFormat(QImage::Format_RGBA32FPx4);
    return convert_tagged_qimage_color_space(decoded,srgb_linear,QImage::Format_RGBA32FPx4,
        QImageColorConversionPurpose::DecodeNormalization);
  }
  auto working=working_image_from_qimage(decoded,state.working_color_profile,
      &pipeline.color_pipeline.input_icc_profile_asset,
      photo_editor::pipeline::decoded_image_requires_engine_input_transform(metadata));
  if (working.isNull()) return {};
  auto prefix=photo_editor::pipeline::prepare_color_prefix_stage_context(
      working.pixels,working.width,working.height,pipeline);
  if (!photo_editor::pipeline::apply_color_normalize_raw_stage(
          &working.pixels,working.width,working.height,pipeline,&prefix,cancellation) ||
      !photo_editor::pipeline::apply_color_camera_characterization_stage(
          &working.pixels,working.width,working.height,pipeline,prefix,cancellation)) return {};
  // Publication packers deliberately clamp signed colors. A mask's guide is
  // an analysis surface, so preserve its actual float samples through the
  // profile conversion, including both above-white and negative channels.
  QImage linear(working.width,working.height,QImage::Format_RGBA32FPx4);
  if (linear.isNull()) return {};
  linear.setColorSpace(working_color_space(state.working_color_profile));
  for (int y=0;y<working.height;++y) {
    if (cancellation.cancelled()) return {};
    auto* row=reinterpret_cast<QRgbaFloat32*>(linear.scanLine(y));
    for (int x=0;x<working.width;++x) {
      const auto& pixel=working.pixels[static_cast<std::size_t>(y*working.width+x)];
      row[x]={pixel.red,pixel.green,pixel.blue,pixel.alpha};
    }
  }
  if (linear.colorSpace()==srgb_linear) return linear;
  return convert_tagged_qimage_color_space(linear,srgb_linear,QImage::Format_RGBA32FPx4,
      QImageColorConversionPurpose::DecodeNormalization);
}

namespace {

QString brush_color_source_cache_key(const QString& path,int max_edge,const RenderEditState& state) {
  // The guide stops at characterization. Keep scene exposure, Look, output,
  // masks and layers out of the key so another stroke reuses the same source.
  RenderEditState source;
  source.input_color_profile=state.input_color_profile;
  source.working_color_profile=state.working_color_profile;
  source.camera_dcp_profile_asset=state.camera_dcp_profile_asset;
  source.camera_profile_rendering_model=state.camera_profile_rendering_model;
  source.input_icc_profile_asset=state.input_icc_profile_asset;
  source.raw_input_transform_enabled=state.raw_input_transform_enabled;
  source.highlight_reconstruction_mode=state.highlight_reconstruction_mode;
  source.highlight_reconstruction_model=state.highlight_reconstruction_model;
  source.chromatic_adaptation_method=state.chromatic_adaptation_method;
  source.raw_demosaic_quality=state.raw_demosaic_quality;
  source.raw_hot_dead_pixel_cleanup=state.raw_hot_dead_pixel_cleanup;
  source.raw_false_color_suppression=state.raw_false_color_suppression;
  source.raw_highlight_recovery=state.raw_highlight_recovery;
  source.raw_white_balance_override=state.raw_white_balance_override;
  source.raw_camera_matching_look=state.raw_camera_matching_look;
  source.raw_white_balance_temperature=state.raw_white_balance_temperature;
  source.raw_white_balance_tint=state.raw_white_balance_tint;
  source.raw_black_level=state.raw_black_level;
  source.raw_white_level=state.raw_white_level;
  source.raw_calibration_hue=state.raw_calibration_hue;
  source.raw_calibration_saturation=state.raw_calibration_saturation;
  source.raw_red_primary_hue=state.raw_red_primary_hue;
  source.raw_red_primary_saturation=state.raw_red_primary_saturation;
  source.raw_green_primary_hue=state.raw_green_primary_hue;
  source.raw_green_primary_saturation=state.raw_green_primary_saturation;
  source.raw_blue_primary_hue=state.raw_blue_primary_hue;
  source.raw_blue_primary_saturation=state.raw_blue_primary_saturation;
  source.temperature=state.temperature;
  source.tint=state.tint;
  auto identity=serialize_edit_recipe_json(source).toUtf8();
  for (const auto& uri:{state.input_icc_profile_asset.source_uri,state.camera_dcp_profile_asset.source_uri}) {
    const QUrl url(uri);
    const QFileInfo profile(url.isLocalFile() ? url.toLocalFile() : uri);
    if (profile.exists()) identity+=decoded_image_cache_key(profile,0).toUtf8();
  }
  return QStringLiteral("brush-guide:v1|")+source_image_decode_cache_key(QFileInfo(path),max_edge,
      openexr_image_supported(path),raw_image_supported(path),source)+QStringLiteral("|")+
      QString::fromLatin1(QCryptographicHash::hash(identity,QCryptographicHash::Sha256).toHex());
}

}  // namespace

bool decode_brush_color_source_image(
    const QString& file_path,QImage* image,int max_edge,const RenderEditState& state,
    photo_editor::pipeline::CancellationToken cancellation) {
  if (!image || cancellation.cancelled()) return false;
  *image={};
  const auto key=max_edge>=0 ? brush_color_source_cache_key(file_path,max_edge,state) : QString{};
  if (!key.isEmpty() && find_decoded_image_cache_entry(key,image)) return !cancellation.cancelled();
  QImage decoded;
  photo_editor::pipeline::DecodedImageMetadata metadata;
  if (!decode_source_image(file_path,&decoded,max_edge,state,&metadata,cancellation)) return false;
  *image=prepare_brush_color_source_image(decoded,state,metadata,cancellation);
  if (image->isNull() || cancellation.cancelled()) return false;
  // Reuse the existing bounded, pressure-aware photo cache. QImage copies share
  // immutable pixels with the active worker instead of duplicating a full RAW.
  if (!key.isEmpty()) insert_decoded_image_cache_entry(key,*image);
  return !cancellation.cancelled();
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
        compiled_pipeline_state = nullptr,
    const QImage& full_source_for_retouch = {});

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
  mask_options.source_geometry = DesktopSourceGeometry{expected_source_size, source_crop};
  mask_options.camera_profile_source_area = mask_options.source_geometry.edge_area();
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
        removal_quality, output_policy, display_target, nullptr, nullptr, cancellation,
        nullptr, decoded);
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

QImage render_editable_base_pixels(
    const QString& source_path,
    EditRecipeState state,
    QString* error,
    bool include_photo_adjustment_layers) {
  QVariantList base_edits;
  for (const auto& value : state.layers) {
    const auto layer = value.toMap();
    if (layer.value(QStringLiteral("baseImageEdit")).toBool() ||
        (include_photo_adjustment_layers &&
         layer.value(QStringLiteral("kind")).toString() == QStringLiteral("adjustment"))) {
      base_edits.push_back(value);
    }
  }
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

QImage render_base_selection_cutout(const QString& source_path, const EditRecipeState& state,
    const QVariantList& masks, QString* error, const std::shared_ptr<std::atomic_bool>& cancelled, QSize output_size) {
  auto base = render_editable_base_pixels(source_path, state, error, true);
  if (base.isNull()) return {};
  if (!output_size.isValid()) output_size = source_image_size(source_path);
  if (output_size.isValid() && base.size() != output_size)
    base = base.scaled(output_size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
  QImage coverage;
  bool geometry_only = masks.size() == 1 && !raw_image_supported(source_path) &&
      state.crop_preset == "Original" && state.crop_zoom == 1.0 &&
      state.crop_rect_width == 0.0 && state.crop_rect_height == 0.0;
  if (geometry_only) {
    QImageReader source_reader(source_path);
    geometry_only = source_reader.canRead() && source_reader.transformation() == QImageIOHandler::TransformationNone;
  }
  auto mask_set = masks.isEmpty() ? photo_editor::pipeline::LocalMaskSet{} : compile_local_mask_set_settings(masks.first().toMap());
  if (geometry_only) {
    const auto mask = compile_pipeline_local_mask(masks.first().toMap(), LocalMaskRasterPolicy::RebuildEditableAtOutputResolution, base.size());
    geometry_only = (mask.kind == "rectangle" || mask.kind == "ellipse" || mask.kind == "radial" || mask.kind == "polygon" || mask.kind == "lasso") &&
        mask.selection_fine_edge == 0.0 && mask.selection_decontaminate == 0.0 && mask_set.selection_fine_edge == 0.0 &&
        mask.refine_luminance_min == 0.0 && mask.refine_luminance_max == 1.0 && mask.refine_hue_range == 0.5 &&
        mask.refine_saturation_min == 0.0 && mask.refine_saturation_max == 1.0 &&
        mask.refine_depth_min == 0.0 && mask.refine_depth_max == 1.0 && mask.refine_edge_confidence_min == 0.0;
    if (geometry_only) mask_set.sources.push_back(mask);
  }
  if (geometry_only) {
    const auto alpha = photo_editor::pipeline::rasterize_local_mask_set_alpha(mask_set, base.width(), base.height());
    if (alpha.empty()) return {};
    coverage = QImage(alpha.data(), base.width(), base.height(), base.width(), QImage::Format_Grayscale8).copy();
  } else {
    QImage source;
    if (!decode_source_image(source_path, &source, 0, state)) {
      if (error) *error = QStringLiteral("Could not prepare the independent cutout selection.");
      return {};
    }
    source = apply_crop(source, state);
    const auto alpha = evaluate_canonical_composite_mask_alpha(source, masks,
        std::pow(2.0, state.exposure),
        preview_white_balance_multipliers(state.temperature, state.tint));
    if (!alpha.valid || alpha.size.isEmpty()) {
      if (error) *error = QStringLiteral("Could not resolve independent cutout coverage.");
      return {};
    }
    coverage = QImage(reinterpret_cast<const uchar*>(alpha.alpha_mask.constData()),
        alpha.size.width(), alpha.size.height(), alpha.size.width(), QImage::Format_Grayscale8);
    coverage = coverage.copy();
  }
  if (coverage.size() != base.size())
    coverage = coverage.scaled(base.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
  if (base.format() != QImage::Format_RGBA32FPx4_Premultiplied)
    base = base.convertToFormat(QImage::Format_RGBA32FPx4_Premultiplied);
  for (int y = 0; y < base.height(); ++y) {
    if (cancelled && cancelled->load()) return {};
    auto* pixels = reinterpret_cast<QRgbaFloat32*>(base.scanLine(y));
    const auto* mask = coverage.constScanLine(y);
    for (int x = 0; x < base.width(); ++x) {
      const auto weight = static_cast<float>(mask[x]) / 255.0F;
      const auto pixel = pixels[x];
      pixels[x] = QRgbaFloat32{pixel.red() * weight, pixel.green() * weight,
          pixel.blue() * weight, pixel.alpha() * weight};
    }
  }
  return base;
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
          .source_geometry = DesktopSourceGeometry{source_size, crop_rect},
      },
      photo_editor::pipeline::RemovalQuality::Final,
      RenderOutputPolicy::DisplayReferred,
      nullptr,
      admission.admitted ? &*admission.plan : nullptr,
      cancellation);
}

namespace {

[[nodiscard]] QImage scene_linear_working_publication(
    const WorkingImage& working,
    const QString& working_profile_name,
    QImage::Format output_format,
    photo_editor::pipeline::CancellationToken cancellation) {
  const auto output_is_float32 =
      output_format == QImage::Format_RGBA32FPx4 ||
      output_format == QImage::Format_RGBA32FPx4_Premultiplied;
  const auto output_is_float16 =
      output_format == QImage::Format_RGBA16FPx4 ||
      output_format == QImage::Format_RGBA16FPx4_Premultiplied;
  if (working.isNull() || (!output_is_float32 && !output_is_float16)) {
    return {};
  }

  const auto output_is_premultiplied =
      output_format == QImage::Format_RGBA32FPx4_Premultiplied ||
      output_format == QImage::Format_RGBA16FPx4_Premultiplied;
  QImage output(working.width, working.height, output_format);
  if (output.isNull()) {
    return {};
  }
  output.setColorSpace(working_color_space(working_profile_name));
  for (int y = 0; y < working.height; ++y) {
    if (cancellation.cancelled()) {
      return {};
    }
    const auto source_offset =
        static_cast<std::size_t>(y) * static_cast<std::size_t>(working.width);
    if (output_is_float32) {
      auto* row = reinterpret_cast<QRgbaFloat32*>(output.scanLine(y));
      for (int x = 0; x < working.width; ++x) {
        const auto& pixel =
            working.pixels[source_offset + static_cast<std::size_t>(x)];
        const auto alpha = static_cast<float>(
            photo_editor::pipeline::clamp_unit(pixel.alpha));
        const auto multiplier = output_is_premultiplied ? alpha : 1.0F;
        row[x] = QRgbaFloat32{
            pixel.red * multiplier,
            pixel.green * multiplier,
            pixel.blue * multiplier,
            alpha,
        };
      }
    } else {
      auto* row = reinterpret_cast<QRgbaFloat16*>(output.scanLine(y));
      for (int x = 0; x < working.width; ++x) {
        const auto& pixel =
            working.pixels[source_offset + static_cast<std::size_t>(x)];
        const auto alpha = static_cast<float>(
            photo_editor::pipeline::clamp_unit(pixel.alpha));
        const auto multiplier = output_is_premultiplied ? alpha : 1.0F;
        row[x] = QRgbaFloat16{
            qfloat16(pixel.red * multiplier),
            qfloat16(pixel.green * multiplier),
            qfloat16(pixel.blue * multiplier),
            qfloat16(alpha),
        };
      }
    }
  }
  return output;
}

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
        compiled_pipeline_state,
    const QImage& full_source_for_retouch) {
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
    auto probe_color_pipeline = compile_color_pipeline_config(state, metadata);
    if (export_display_target_override != nullptr) {
      probe_color_pipeline.export_display_target =
          *export_display_target_override;
      probe_color_pipeline.export_output_profile.color_space =
          export_display_target_override->color_space;
    }
    const auto text_policy = classify_display_referred_text_layers(
        state,
        probe_color_pipeline,
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
      const auto& target = probe_color_pipeline.export_display_target;
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
  auto complete_retouch_source = full_source_for_retouch.isNull()
      ? mask_options.full_source : full_source_for_retouch;
  auto complete_retouch_metadata = metadata == nullptr
      ? photo_editor::pipeline::DecodedImageMetadata{} : *metadata;
  if (complete_retouch_source.isNull() && !image.isNull() &&
      (!mask_options.source_geometry.valid() ||
       image.size() == mask_options.source_geometry.source_size)) {
    complete_retouch_source = image;
  }
  if (compiled_pipeline_state == nullptr && complete_retouch_source.isNull() &&
      deferred_source != nullptr && has_linked_live_range_selection(state)) {
    if (!decode_source_image(deferred_source->file_path, &complete_retouch_source,
            std::max(deferred_source->expected_source_size.width(),
                     deferred_source->expected_source_size.height()),
            state, &complete_retouch_metadata, cancellation) ||
        complete_retouch_source.size() != deferred_source->expected_source_size) {
      if (telemetry != nullptr) {
        telemetry->backend = QStringLiteral("none");
        telemetry->fallback_reason = QStringLiteral(
            "full_source_required_for_linked_range_mask");
      }
      return {};
    }
    const auto& expected_metadata = deferred_source->expected_metadata;
    if (expected_metadata.valid &&
        (!complete_retouch_metadata.valid ||
         complete_retouch_metadata.pixel_encoding != expected_metadata.pixel_encoding ||
         complete_retouch_metadata.decoder_backend != expected_metadata.decoder_backend ||
         complete_retouch_metadata.camera_profile_identity !=
             expected_metadata.camera_profile_identity)) {
      if (telemetry != nullptr) {
        telemetry->backend = QStringLiteral("none");
        telemetry->fallback_reason = QStringLiteral(
            "full_source_metadata_mismatch_for_linked_range_mask");
      }
      return {};
    }
  }
  if (compiled_pipeline_state == nullptr && has_linked_live_range_selection(state) &&
      !complete_retouch_source.isNull() && mask_options.source_geometry.valid() &&
      complete_retouch_source.size() != mask_options.source_geometry.source_size) {
    if (telemetry != nullptr) {
      telemetry->backend = QStringLiteral("none");
      telemetry->fallback_reason = QStringLiteral(
          "full_source_geometry_mismatch_for_linked_range_mask");
    }
    return {};
  }
  auto pipeline_state_storage =
      std::optional<photo_editor::pipeline::ColorAdjustmentState>{};
  if (compiled_pipeline_state == nullptr) {
    const auto profile_compile = photo_render_profile_for_tests.enabled;
    const auto compile_started = profile_compile
        ? std::chrono::steady_clock::now()
        : std::chrono::steady_clock::time_point{};
    pipeline_state_storage.emplace(compile_export_pipeline_state(
        state,
        metadata,
        raster_policy,
        QSize(width, height),
        pixels,
        export_display_target_override, mask_options.source_geometry,
        complete_retouch_source.isNull() ? nullptr : &complete_retouch_source,
        cancellation));
    if (profile_compile) {
      photo_render_profile_for_tests.compile_export_pipeline_state_ns +=
          std::chrono::duration_cast<std::chrono::nanoseconds>(
              std::chrono::steady_clock::now() - compile_started)
              .count();
    }
  }
  auto& pipeline_state = compiled_pipeline_state == nullptr
                             ? *pipeline_state_storage
                             : const_cast<photo_editor::pipeline::ColorAdjustmentState&>(
                                   *compiled_pipeline_state);
  const auto cropped_retouch = !pipeline_state.retouch_masks.empty() &&
      mask_options.source_geometry.valid() &&
      mask_options.source_geometry.crop_rect !=
          QRect(QPoint(), mask_options.source_geometry.source_size);
  // This is deliberately prepared before executor submission. Decode cache
  // hits skip the callback below, but each retouch recipe still needs its own
  // complete-source prefix and masks, including source pixels outside a crop.
  if (compiled_pipeline_state == nullptr && cropped_retouch) {
    if (complete_retouch_source.isNull() && deferred_source != nullptr &&
        !decode_source_image(deferred_source->file_path, &complete_retouch_source, 0, state,
            &complete_retouch_metadata, cancellation)) return {};
    if (deferred_source != nullptr) {
      const auto& expected = deferred_source->expected_metadata;
      if (complete_retouch_source.size() != deferred_source->expected_source_size ||
          (expected.valid && (!complete_retouch_metadata.valid ||
           complete_retouch_metadata.pixel_encoding != expected.pixel_encoding ||
           complete_retouch_metadata.decoder_backend != expected.decoder_backend ||
           complete_retouch_metadata.camera_profile_identity != expected.camera_profile_identity))) return {};
    }
    if (!attach_full_source_retouch_context(&pipeline_state, complete_retouch_source, state,
            complete_retouch_metadata.valid ? &complete_retouch_metadata : metadata,
            DesktopRenderPurpose::Output, raster_policy, mask_options.source_geometry,
            cancellation)) return {};
  }
  const auto retained_retouch_context = pipeline_state.retouch_source_context;
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
            (mask_options.source_geometry.valid()
                 ? mask_options.source_geometry.edge_area()
                 : mask_options.camera_profile_source_area)
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
          if (!complete_retouch_source.isNull()) {
            decoded = complete_retouch_source;
            decoded_metadata = complete_retouch_metadata;
          } else if (!decode_source_image(
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
            auto full_source_input = pipeline_state.input_transform;
            full_source_input.source_area_x = 0.0;
            full_source_input.source_area_y = 0.0;
            full_source_input.source_area_width = 1.0;
            full_source_input.source_area_height = 1.0;
            source_highlights = photo_editor::pipeline::analyze_highlight_reconstruction(
                full_working.pixels, full_working.width, full_working.height, full_source_input,
                pipeline_state.color_pipeline.input_transform_recipe.highlight_headroom);
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
                  export_display_target_override, mask_options.source_geometry,
                  complete_retouch_source.isNull() ? nullptr : &complete_retouch_source,
                  cancellation);
          pipeline_state.retouch_source_context = retained_retouch_context;
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
  } else if (canonical_plan != nullptr && !mask_options.full_source.isNull() &&
      mask_options.source_geometry.valid() &&
      mask_options.source_geometry.crop_rect != QRect(QPoint(), mask_options.source_geometry.source_size) &&
      metadata != nullptr && photo_editor::pipeline::decoded_image_requires_engine_input_transform(*metadata) &&
      pipeline_state.input_transform.highlight_recovery_enabled &&
      pipeline_state.input_transform.highlight_reconstruction_mode != "Disabled") {
    const auto full_working = working_image_from_qimage(mask_options.full_source,
        state.working_color_profile, &input_profile_asset, true);
    if (full_working.isNull() || full_working.width != mask_options.source_geometry.source_size.width() ||
        full_working.height != mask_options.source_geometry.source_size.height() || cancellation.cancelled()) return {};
    auto full_input = pipeline_state.input_transform;
    full_input.source_area_x = 0; full_input.source_area_y = 0;
    full_input.source_area_width = 1; full_input.source_area_height = 1;
    auto source_highlights = photo_editor::pipeline::analyze_highlight_reconstruction(
        full_working.pixels, full_working.width, full_working.height, full_input,
        pipeline_state.color_pipeline.input_transform_recipe.highlight_headroom);
    deferred_decode_stage = [decoded_pixels = std::move(pixels), source_highlights,
                            identity = canonical_plan->recipe_cache_key](const auto& invocation,
                                std::optional<photo_editor::pipeline::RenderSurface> input) mutable {
      if (input.has_value()) return photo_editor::pipeline::RenderStageExecution{
          .failure_reason = "cropped in-memory Decode received an unexpected input surface"};
      return photo_editor::pipeline::RenderStageExecution{
          .output = photo_editor::pipeline::RenderSurface{
              .descriptor = invocation.expected_output,
              .pixels = std::move(decoded_pixels),
              .identity = "surface:decode:" + identity,
              .highlight_reconstruction = source_highlights},
          .backend = "cropped-image-with-full-source-analysis"};
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
  const auto scene_linear_float_output =
      output_policy == RenderOutputPolicy::SceneLinearWorking &&
      (output_format == QImage::Format_RGBA16FPx4 ||
       output_format == QImage::Format_RGBA16FPx4_Premultiplied ||
       output_format == QImage::Format_RGBA32FPx4 ||
       output_format == QImage::Format_RGBA32FPx4_Premultiplied);
  auto output_image = scene_linear_float_output
      ? scene_linear_working_publication(
            working, state.working_color_profile, output_format, cancellation)
      : qimage_from_working_image(
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
  if (output_policy == RenderOutputPolicy::SceneLinearWorking ||
      custom_export_profile_configured || target_color_space.isValid()) {
    return output_image;
  }
  apply_output_color_space_transform(&output_image, state.output_color_space);
  return output_image;
}

}  // namespace

}  // namespace photo_editor::desktop
