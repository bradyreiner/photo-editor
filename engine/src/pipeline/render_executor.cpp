#include "photo_editor/pipeline/render_executor.h"

#include "photo_editor/pipeline/color_transform.h"
#if defined(PHOTO_EDITOR_HAS_METAL_BACKEND) && PHOTO_EDITOR_HAS_METAL_BACKEND
#include "photo_editor/pipeline/metal/metal_pipeline_backend.h"
#endif

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <span>
#include <sstream>

namespace photo_editor::pipeline {

namespace {

[[nodiscard]] std::string sha256_text(
    const std::string& value) {
  return color_payload_sha256(std::as_bytes(std::span(value)));
}

[[nodiscard]] bool valid_source_area(
    const RenderExecutionRequest& request) {
  return request.source_area_x >= 0.0 &&
         request.source_area_y >= 0.0 &&
         request.source_area_width > 0.0 &&
         request.source_area_height > 0.0 &&
         request.source_area_x + request.source_area_width <= 1.0 &&
         request.source_area_y + request.source_area_height <= 1.0;
}

[[nodiscard]] RendererTransitionLook runtime_renderer_transition_look(
    RendererTransitionLookV13 look) {
  switch (look) {
    case RendererTransitionLookV13::PreserveLegacyDirectLuminanceV1:
      return RendererTransitionLook::PreserveLegacyDirectLuminanceV1;
    case RendererTransitionLookV13::None:
      return RendererTransitionLook::None;
  }
  return RendererTransitionLook::None;
}

[[nodiscard]] bool cacheable(RenderStageKind kind) {
  return kind != RenderStageKind::Publish;
}

[[nodiscard]] bool depends_on_rendering_target(
    RenderStageKind kind) {
  return kind == RenderStageKind::PhotographicOutput ||
         kind == RenderStageKind::Proof ||
         kind == RenderStageKind::DeviceTransform ||
         kind == RenderStageKind::Publish;
}

[[nodiscard]] bool depends_on_device_profile(
    RenderStageKind kind) {
  return kind == RenderStageKind::DeviceTransform ||
         kind == RenderStageKind::Publish;
}

[[nodiscard]] bool stale(
    const RenderExecutionRequest& request) {
  return request.latest_generation &&
         request.latest_generation() != request.generation;
}

[[nodiscard]] bool cancelled(
    const RenderExecutionRequest& request) {
  return request.cancelled && request.cancelled();
}

[[nodiscard]] bool has_plan_stage(
    const RenderPlan& plan,
    RenderStageKind kind) {
  return std::ranges::any_of(
      plan.stages,
      [kind](const RenderStagePlan& stage) { return stage.kind == kind; });
}

[[nodiscard]] bool display_referred_text_overlay_is_eligible(
    const RenderExecutionRequest& request,
    const ColorAdjustmentState& state,
    const ColorPlanExecutionOptions& options) {
  if (state.display_referred_text_layers.empty() ||
      state.display_referred_text_color_domain != "display-linear" ||
      state.display_referred_text_eligibility != "eligible-sdr-top-text-suffix" ||
      state.display_referred_text_policy_version !=
          "display-referred-text-suffix-v1" ||
      options.photographic_output_config == nullptr ||
      !has_plan_stage(request.plan, RenderStageKind::PhotographicOutput) ||
      has_plan_stage(request.plan, RenderStageKind::Proof)) {
    return false;
  }
  const auto& config = *options.photographic_output_config;
  if (!photographic_rendering_model_uses_aces2(
          config.photographic_rendering_model) ||
      config.render_intent != RenderIntent::Perceptual ||
      config.soft_proof_enabled) {
    return false;
  }
  const auto preview_target =
      options.gamut_warning_target == GamutWarningTarget::PreviewDisplay;
  const auto has_device_stage =
      has_plan_stage(request.plan, RenderStageKind::DeviceTransform);
  const auto& target = preview_target ? config.preview_display_target
                                      : config.export_display_target;
  const auto& device_profile = preview_target
      ? config.preview_icc_profile_asset
      : config.export_icc_profile_asset;
  const auto photographic_output = std::find_if(
      request.plan.stages.cbegin(), request.plan.stages.cend(),
      [](const RenderStagePlan& stage) {
        return stage.kind == RenderStageKind::PhotographicOutput;
      });
  if (photographic_output == request.plan.stages.cend()) return false;
  const auto photographic_output_index = static_cast<std::size_t>(
      std::distance(request.plan.stages.cbegin(), photographic_output));
  if (has_device_stage) {
    const auto* published_target = options.publish_display_target;
    const auto geometry_prefix = options.stop_before_photographic_output;
    const auto* original_display_target = geometry_prefix
        ? &target
        : published_target;
    if (!preview_target || !request.plan.device_icc_profile.has_value() ||
        request.plan.rendering_target !=
            DisplayRenderingTarget::ActiveDisplay ||
        request.plan.purpose != RenderPurpose::SettledPreview ||
        !device_profile.ready() || original_display_target == nullptr ||
        original_display_target->eotf != DisplayEotf::Srgb ||
        original_display_target->mastering_mode != DisplayMasteringMode::Sdr ||
        original_display_target->color_space.primaries != ColorPrimaries::Srgb ||
        original_display_target->color_space.transfer_function !=
            TransferFunction::Srgb ||
        target.eotf != DisplayEotf::Srgb ||
        target.mastering_mode != DisplayMasteringMode::Sdr ||
        photographic_output_index + 2U >= request.plan.stages.size() ||
        request.plan.stages[photographic_output_index + 1U].kind !=
            RenderStageKind::DeviceTransform ||
        request.plan.stages[photographic_output_index + 2U].kind !=
            RenderStageKind::Publish ||
        photographic_output_index + 3U != request.plan.stages.size() ||
        (geometry_prefix &&
         (published_target != nullptr || options.device_transform != nullptr ||
          target.color_space.primaries != ColorPrimaries::Srgb ||
          target.color_space.transfer_function != TransferFunction::Srgb ||
          config.preview_output_profile.color_space.primaries !=
              ColorPrimaries::Srgb ||
          config.preview_output_profile.color_space.transfer_function !=
              TransferFunction::Srgb)) ||
        (!geometry_prefix &&
         (published_target == nullptr || options.device_transform == nullptr ||
          !options.device_transform->ready() ||
          target.color_space.primaries != ColorPrimaries::Rec2020 ||
          target.color_space.transfer_function != TransferFunction::Linear ||
          config.preview_output_profile.color_space.primaries !=
              ColorPrimaries::Rec2020 ||
          config.preview_output_profile.color_space.transfer_function !=
              TransferFunction::Linear))) {
      return false;
    }

    const auto& profile_reference = device_profile.reference;
    const auto& planned_profile_id =
        request.plan.device_icc_profile->value;
    const auto plan_profile_matches =
        planned_profile_id == profile_reference.asset_id ||
        planned_profile_id ==
            "sha256:" + profile_reference.payload_sha256;
    if (!plan_profile_matches) return false;
    // During the geometry-prefix pass the plan already proves that the
    // settled preview will end in this exact DeviceTransform/Publish suffix,
    // but the transform is deliberately compiled only after geometry. Defer
    // eligible text now so the suffix applies it once, after ACES2 and before
    // the same monitor transform.
    if (geometry_prefix) return true;
    const auto linear_rec2020_payload =
        builtin_rgb_icc_profile_payload(make_linear_rec2020_color_space());
    const auto expected_source_digest =
        color_payload_sha256(linear_rec2020_payload);
    const auto& transform_description =
        options.device_transform->description();
    return transform_description.source_profile_sha256 ==
            expected_source_digest &&
        transform_description.destination_profile_sha256 ==
            profile_reference.payload_sha256 &&
        transform_description.target ==
            "preview-output:aces2-rec2020-interchange" &&
        !transform_description.device_link &&
        !transform_description.soft_proofing;
  }

  return !request.plan.device_icc_profile.has_value() &&
      options.device_transform == nullptr &&
      target.eotf == DisplayEotf::Srgb &&
      target.mastering_mode == DisplayMasteringMode::Sdr &&
      target.color_space.primaries == ColorPrimaries::Srgb &&
      device_profile.status == IccProfileAssetStatus::Unconfigured;
}

[[nodiscard]] bool valid_fusion_evidence(
    const RenderExecutionRequest& request,
    std::size_t stage_index,
    const RenderFusionEvidence& evidence) {
  if (!evidence.unfused_reference_passed ||
      evidence.unfused_reference_identity.empty() ||
      evidence.logical_stages_covered.size() < 2U) {
    return false;
  }
  if (stage_index + evidence.logical_stages_covered.size() >
      request.plan.stages.size()) {
    return false;
  }
  for (auto offset = std::size_t{0};
       offset < evidence.logical_stages_covered.size(); ++offset) {
    if (request.plan.stages[stage_index + offset].kind !=
        evidence.logical_stages_covered[offset]) {
      return false;
    }
  }
  const auto final_index =
      stage_index + evidence.logical_stages_covered.size() - 1U;
  return !request.stop_after_stage_index.has_value() ||
         *request.stop_after_stage_index >= final_index;
}

[[nodiscard]] bool apply_compiled_icc_transform(
    std::vector<ColorPixel>* pixels,
    const CompiledIccTransform& transform,
    CancellationToken cancellation) {
  if (pixels == nullptr || !transform.ready() ||
      cancellation.cancelled()) {
    return false;
  }
  constexpr auto kChunkPixels = std::size_t{16'384};
  std::vector<IccRgbaFloat> chunk;
  chunk.reserve(
      std::min(kChunkPixels, pixels->size()));
  for (std::size_t begin = 0U; begin < pixels->size();
       begin += kChunkPixels) {
    if (cancellation.cancelled()) {
      return false;
    }
    const auto end =
        std::min(begin + kChunkPixels, pixels->size());
    chunk.clear();
    for (auto index = begin; index < end; ++index) {
      const auto& pixel = (*pixels)[index];
      chunk.push_back(IccRgbaFloat{
          .red = pixel.red,
          .green = pixel.green,
          .blue = pixel.blue,
          .alpha = pixel.alpha,
      });
    }
    if (!transform.apply(chunk)) {
      return false;
    }
    for (auto index = begin; index < end; ++index) {
      const auto& transformed = chunk[index - begin];
      (*pixels)[index] = ColorPixel{
          .red = transformed.red,
          .green = transformed.green,
          .blue = transformed.blue,
          .alpha = transformed.alpha,
      };
    }
  }
  return !cancellation.cancelled();
}

}  // namespace

bool RenderSurface::valid() const noexcept {
  const auto pixel_count =
      static_cast<std::uint64_t>(descriptor.width) *
      static_cast<std::uint64_t>(descriptor.height);
  const auto cpu_pixels_valid =
      pixel_count == pixels.size() &&
      (cpu_pixels_validated ||
       std::ranges::all_of(pixels, [](const ColorPixel& pixel) {
         return std::isfinite(pixel.red) &&
                std::isfinite(pixel.green) &&
                std::isfinite(pixel.blue) &&
                std::isfinite(pixel.alpha);
       }));
  const auto backing_valid =
      backing != nullptr && backing->valid_for(descriptor);
  return descriptor.width > 0 && descriptor.height > 0 &&
         (cpu_pixels_valid || backing_valid) &&
         (pixels.empty() || cpu_pixels_valid) && !identity.empty() &&
         (gamut_warning_mask.empty() ||
          gamut_warning_mask.size() == pixel_count);
}

std::uint64_t RenderSurface::byte_size() const noexcept {
  return static_cast<std::uint64_t>(pixels.size()) *
             static_cast<std::uint64_t>(sizeof(ColorPixel)) +
         (backing != nullptr ? backing->byte_size() : 0U) +
         static_cast<std::uint64_t>(
             gamut_warning_mask.size());
}

bool RenderSurface::ensure_cpu_pixels(
    CancellationToken cancellation) {
  if (cancellation.cancelled()) {
    return false;
  }
  const auto pixel_count =
      static_cast<std::uint64_t>(descriptor.width) *
      static_cast<std::uint64_t>(descriptor.height);
  const auto finite_pixels = [](const std::vector<ColorPixel>& candidate) {
    return std::ranges::all_of(candidate, [](const ColorPixel& pixel) {
      return std::isfinite(pixel.red) && std::isfinite(pixel.green) &&
             std::isfinite(pixel.blue) && std::isfinite(pixel.alpha);
    });
  };
  if (pixels.size() == pixel_count &&
      (cpu_pixels_validated || finite_pixels(pixels))) {
    cpu_pixels_validated = true;
    return true;
  }
  if (!pixels.empty() || backing == nullptr || !backing->valid_for(descriptor)) {
    return false;
  }
  auto materialized = std::vector<ColorPixel>{};
  const auto backing_validates_finite =
      backing->copy_pixels_validates_finite();
  if (!backing->copy_pixels(&materialized, cancellation) ||
      cancellation.cancelled() || materialized.size() != pixel_count ||
      (!backing_validates_finite && !finite_pixels(materialized))) {
    return false;
  }
  pixels = std::move(materialized);
  cpu_pixels_validated = true;
  return true;
}

std::string_view render_stage_kind_name(
    RenderStageKind kind) noexcept {
  switch (kind) {
    case RenderStageKind::Decode:
      return "decode";
    case RenderStageKind::NormalizeRaw:
      return "normalize_raw";
    case RenderStageKind::CameraCharacterization:
      return "camera_characterization";
    case RenderStageKind::SceneAdjustments:
      return "scene_adjustments";
    case RenderStageKind::Tone:
      return "tone";
    case RenderStageKind::Curves:
      return "curves";
    case RenderStageKind::Look:
      return "look";
    case RenderStageKind::Layers:
      return "layers";
    case RenderStageKind::Detail:
      return "detail";
    case RenderStageKind::Geometry:
      return "geometry";
    case RenderStageKind::PhotographicOutput:
      return "photographic_output";
    case RenderStageKind::Proof:
      return "proof";
    case RenderStageKind::DeviceTransform:
      return "device_transform";
    case RenderStageKind::Publish:
      return "publish";
  }
  return "unknown";
}

RenderSurfaceDescriptor render_stage_output_descriptor(
    const RenderExecutionRequest& request,
    RenderStageKind kind) {
  auto descriptor = RenderSurfaceDescriptor{
      .pixel_encoding = RenderPixelEncoding::SceneLinearFloat,
      .color_domain = RenderColorDomain::WorkingScene,
      .range = RenderRangeModel::SceneExtended,
      .alpha = RenderAlphaModel::Straight,
      .color_space_identity =
          request.working_color_space_identity,
      .transfer_identity = "linear",
      .width = request.output_width,
      .height = request.output_height,
      .source_area_x = request.source_area_x,
      .source_area_y = request.source_area_y,
      .source_area_width = request.source_area_width,
      .source_area_height = request.source_area_height,
  };
  switch (kind) {
    case RenderStageKind::Decode:
      descriptor.pixel_encoding =
          request.plan.decoder == RawDecoder::LibRaw
              ? RenderPixelEncoding::CameraLinearFloat
              : RenderPixelEncoding::SceneLinearFloat;
      descriptor.color_domain =
          request.plan.decoder == RawDecoder::LibRaw
              ? RenderColorDomain::CameraNative
              : RenderColorDomain::WorkingScene;
      descriptor.color_space_identity =
          request.plan.decoder == RawDecoder::LibRaw
              ? request.source_color_space_identity
              : request.working_color_space_identity;
      return descriptor;
    case RenderStageKind::NormalizeRaw:
      descriptor.pixel_encoding =
          RenderPixelEncoding::CameraLinearFloat;
      descriptor.color_domain = RenderColorDomain::CameraNative;
      descriptor.color_space_identity =
          request.source_color_space_identity;
      return descriptor;
    case RenderStageKind::CameraCharacterization:
    case RenderStageKind::SceneAdjustments:
    case RenderStageKind::Tone:
    case RenderStageKind::Curves:
    case RenderStageKind::Look:
    case RenderStageKind::Layers:
    case RenderStageKind::Detail:
    case RenderStageKind::Geometry:
      return descriptor;
    case RenderStageKind::PhotographicOutput:
      descriptor.pixel_encoding =
          RenderPixelEncoding::DisplayLinearFloat;
      descriptor.color_domain =
          RenderColorDomain::RenderingTarget;
      descriptor.color_space_identity =
          request.rendering_target_color_space_identity;
      descriptor.range =
          request.plan.rendering_target ==
                      DisplayRenderingTarget::HdrPq ||
                  request.plan.rendering_target ==
                      DisplayRenderingTarget::HdrHlg
              ? RenderRangeModel::DisplayExtended
              : RenderRangeModel::DisplayBounded;
      return descriptor;
    case RenderStageKind::Proof:
      descriptor.pixel_encoding =
          RenderPixelEncoding::DisplayLinearFloat;
      descriptor.color_domain =
          RenderColorDomain::ProofSimulation;
      descriptor.color_space_identity =
          request.proof_color_space_identity.empty()
              ? request.rendering_target_color_space_identity
              : request.proof_color_space_identity;
      descriptor.range = RenderRangeModel::DisplayBounded;
      return descriptor;
    case RenderStageKind::DeviceTransform:
    case RenderStageKind::Publish:
      descriptor.pixel_encoding =
          RenderPixelEncoding::DisplayEncodedFloat;
      descriptor.color_domain = RenderColorDomain::Device;
      descriptor.color_space_identity =
          request.device_color_space_identity;
      descriptor.transfer_identity =
          request.device_transfer_identity;
      descriptor.range =
          request.plan.rendering_target ==
                      DisplayRenderingTarget::HdrPq ||
                  request.plan.rendering_target ==
                      DisplayRenderingTarget::HdrHlg
              ? RenderRangeModel::DisplayExtended
              : RenderRangeModel::DisplayBounded;
      return descriptor;
  }
  return descriptor;
}

std::string render_stage_cache_key(
    const RenderExecutionRequest& request,
    const RenderStagePlan& stage,
    const std::optional<RenderSurface>& input) {
  std::ostringstream identity;
  identity.precision(17);
  identity << "render-stage:v2-dependency-scoped"
           << "|source:" << request.plan.source.value
           << "|stage:" << render_stage_kind_name(stage.kind)
           << "|stage-version:" << stage.cache_identity
           << "|backend:" << request.backend_version
           << "|resolution:" << request.output_width << "x"
           << request.output_height
           << "|source-area:" << request.source_area_x << ","
           << request.source_area_y << ","
           << request.source_area_width << ","
           << request.source_area_height
           << "|input:"
           << (input.has_value() ? input->identity : "none");
  if (stage.kind == RenderStageKind::Decode) {
    identity << "|source-revision:" << request.source_revision_identity;
  }
  const auto descriptor =
      render_stage_output_descriptor(
          request,
          stage.kind);
  identity << "|color-space:"
           << descriptor.color_space_identity
           << "|transfer:"
           << descriptor.transfer_identity;
  if (depends_on_rendering_target(stage.kind)) {
    identity << "|target:" << request.target_identity;
  }
  if (depends_on_device_profile(stage.kind)) {
    identity << "|profile:" << request.profile_identity;
  }
  return "sha256:" + sha256_text(identity.str());
}

RenderExecutor::RenderExecutor(
    std::uint64_t maximum_cache_bytes)
    : maximum_cache_bytes_(maximum_cache_bytes) {}

RenderExecutionResult RenderExecutor::execute(
    RenderExecutionRequest request,
    const RenderStageCallback& callback) {
  RenderExecutionResult result;
  result.plan_cache_key = request.plan.plan_cache_key;
  if (request.plan.plan_cache_key.empty() ||
      request.plan.source.value.empty() ||
      request.output_width == 0 ||
      request.output_height == 0 ||
      request.source_color_space_identity.empty() ||
      request.working_color_space_identity.empty() ||
      request.rendering_target_color_space_identity.empty() ||
      request.device_color_space_identity.empty() ||
      request.device_transfer_identity.empty() ||
      request.plan.stages.empty() ||
      request.start_stage_index >= request.plan.stages.size() ||
      (request.start_stage_index > 0 &&
       !request.initial_surface.has_value()) ||
      (request.start_stage_index == 0 &&
       request.initial_surface.has_value()) ||
      (request.stop_after_stage_index.has_value() &&
       (*request.stop_after_stage_index >=
            request.plan.stages.size() ||
        *request.stop_after_stage_index <
            request.start_stage_index)) ||
      !valid_source_area(request) || !callback) {
    result.failure_reason = "invalid render execution request";
    return result;
  }

  std::optional<RenderSurface> current =
      std::move(request.initial_surface);
  if (current.has_value()) {
    const auto expected_input = render_stage_output_descriptor(
        request,
        request.plan.stages[
            request.start_stage_index - 1]
            .kind);
    if (!current->valid() ||
        current->descriptor != expected_input) {
      result.status =
          RenderExecutionStatus::ContractViolation;
      result.failure_stage = std::string(
          render_stage_kind_name(
              request.plan.stages[
                  request.start_stage_index]
                  .kind));
      result.failure_reason =
          "initial surface violates the preceding stage pixel contract";
      return result;
    }
  }
  for (std::size_t index = request.start_stage_index;
       index < request.plan.stages.size();
       ++index) {
    const auto& stage = request.plan.stages[index];
    const auto stage_name =
        std::string(render_stage_kind_name(stage.kind));
    if (cancelled(request)) {
      result.status = RenderExecutionStatus::Cancelled;
      result.failure_stage = stage_name;
      result.failure_reason = "render execution cancelled";
      return result;
    }
    if (stale(request)) {
      result.status = RenderExecutionStatus::Stale;
      result.failure_stage = stage_name;
      result.failure_reason = "render generation is stale";
      return result;
    }
    const auto expected =
        render_stage_output_descriptor(request, stage.kind);
    const auto cache_key =
        render_stage_cache_key(request, stage, current);
    if (cacheable(stage.kind) && !request.bypass_cache) {
      auto cached = find_cached(cache_key);
      if (cached.has_value()) {
        current = std::move(cached);
        const auto completed = RenderCompletedStage{
            .kind = stage.kind,
            .cache_key = cache_key,
            .output_identity = current->identity,
            .backend = "cache",
            .fallback_reason = {},
            .output_descriptor = current->descriptor,
            .cache_hit = true,
            .elapsed_ms = 0,
        };
        result.completed_stages.push_back(completed);
        if (request.stage_observer) {
          request.stage_observer(completed, *current);
        }
        if (request.stop_after_stage_index == index) {
          result.status = RenderExecutionStatus::Completed;
          result.output = std::move(current);
          return result;
        }
        continue;
      }
    }

    const auto stage_started_at = std::chrono::steady_clock::now();
    auto executed = callback(
        RenderStageInvocation{
            .stage = stage,
            .stage_index = index,
            .stage_cache_key = cache_key,
            .expected_output = expected,
        },
        std::move(current));
    const auto stage_elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                     std::chrono::steady_clock::now() - stage_started_at)
                                     .count();
    if (executed.fusion_evidence.has_value() &&
        !valid_fusion_evidence(
            request,
            index,
            *executed.fusion_evidence)) {
      result.status =
          RenderExecutionStatus::ContractViolation;
      result.failure_stage = stage_name;
      result.failure_reason =
          "backend fusion requires a passed unfused reference and a contiguous logical-stage manifest";
      result.in_flight_stage_elapsed_ms = stage_elapsed_ms;
      return result;
    }
    if (!executed.output.has_value()) {
      result.status = cancelled(request)
                          ? RenderExecutionStatus::Cancelled
                          : RenderExecutionStatus::StageFailed;
      result.failure_stage = stage_name;
      result.failure_reason =
          executed.failure_reason.empty()
              ? "stage did not produce an output"
              : executed.failure_reason;
      result.in_flight_stage_elapsed_ms = stage_elapsed_ms;
      return result;
    }
    const auto covered_stage_count =
        executed.fusion_evidence.has_value()
            ? executed.fusion_evidence->logical_stages_covered.size()
            : std::size_t{1};
    const auto final_stage_index = index + covered_stage_count - 1U;
    const auto final_expected = render_stage_output_descriptor(
        request, request.plan.stages[final_stage_index].kind);
    if (!executed.output->valid() ||
        executed.output->descriptor != final_expected) {
      result.status =
          RenderExecutionStatus::ContractViolation;
      result.failure_stage = stage_name;
      result.failure_reason =
          "stage output violates its declared pixel contract";
      result.in_flight_stage_elapsed_ms = stage_elapsed_ms;
      return result;
    }
    current = std::move(executed.output);
    if (cacheable(stage.kind) &&
        !request.bypass_cache &&
        !request.skip_cache_writes &&
        (!request.cache_write_stage_index_exclusive_limit.has_value() ||
         index < *request.cache_write_stage_index_exclusive_limit) &&
        !executed.fusion_evidence.has_value()) {
      insert_cached(cache_key, *current);
    }
    const auto fusion_evidence = executed.fusion_evidence;
    for (auto offset = std::size_t{0}; offset < covered_stage_count; ++offset) {
      const auto completed_index = index + offset;
      const auto completed_kind = request.plan.stages[completed_index].kind;
      const auto completed = RenderCompletedStage{
          .kind = completed_kind,
          .cache_key = offset == 0U
                           ? cache_key
                           : cache_key + ":fused:" +
                                 std::string(render_stage_kind_name(completed_kind)),
          .output_identity = current->identity,
          .backend = executed.backend,
          .fallback_reason = executed.fallback_reason,
          .output_descriptor = render_stage_output_descriptor(request, completed_kind),
          .fusion_evidence = fusion_evidence,
          .cache_hit = false,
          .elapsed_ms = stage_elapsed_ms,
      };
      result.completed_stages.push_back(completed);
      if (request.stage_observer) {
        request.stage_observer(completed, *current);
      }
    }
    index = final_stage_index;
    if (request.stop_after_stage_index == final_stage_index) {
      result.status = RenderExecutionStatus::Completed;
      result.output = std::move(current);
      return result;
    }
  }

  result.status = RenderExecutionStatus::Completed;
  result.output = std::move(current);
  return result;
}

RenderExecutionResult execute_color_plan(
    RenderExecutor* executor,
    RenderExecutionRequest request,
    RenderStageCallback decode_stage,
    const ColorAdjustmentState& state,
    const ColorPlanExecutionOptions& options,
    CancellationToken cancellation,
    ColorAdjustmentTelemetry* telemetry) {
  RenderExecutionResult invalid;
  invalid.plan_cache_key = request.plan.plan_cache_key;
  if (options.allow_metal_photographic_output) {
    request.backend_version += ":metal-photographic-output-v1";
  }
  if (executor == nullptr) {
    invalid.failure_reason =
        "render executor is required";
    return invalid;
  }
  if (options.start_stage ==
          RenderStageKind::Decode &&
      !decode_stage) {
    invalid.failure_reason =
        "Decode stage callback is required";
    return invalid;
  }
  if (request.plan.stages.empty() ||
      request.plan.stages.front().kind !=
          RenderStageKind::Decode) {
    invalid.failure_reason =
        "canonical plan must begin with Decode";
    return invalid;
  }
  if (options.photographic_output_config != nullptr &&
      options.photographic_output_config->renderer_transition_look !=
          runtime_renderer_transition_look(
              request.plan.renderer_transition_look)) {
    invalid.failure_reason =
        "canonical plan and photographic output transition look differ";
    return invalid;
  }
  const auto defer_display_referred_text =
      display_referred_text_overlay_is_eligible(request, state, options);
  if (defer_display_referred_text) {
    // The Layers stage has different pixels when plain text is deferred to the
    // display-linear suffix. Keep those results in a distinct cache namespace.
    request.backend_version += ":display-referred-text-suffix-v2";
  }
  if (telemetry != nullptr &&
      !state.display_referred_text_layers.empty()) {
    telemetry->display_referred_text_color_domain =
        state.display_referred_text_color_domain;
    telemetry->display_referred_text_eligibility = defer_display_referred_text
        ? state.display_referred_text_eligibility
        : "legacy-working-scene";
    telemetry->display_referred_text_eligibility_reason =
        defer_display_referred_text
            ? "deferred-to-photographic-output"
            : "canonical-plan-requires-working-scene-composition";
    telemetry->display_referred_text_policy_version =
        state.display_referred_text_policy_version;
    telemetry->display_referred_text_layer_count =
        static_cast<std::int64_t>(
            state.display_referred_text_layers.size());
    telemetry->display_referred_text_applied_layer_count = 0;
  }
  const auto normalize_present =
      std::ranges::any_of(
          request.plan.stages,
          [](const RenderStagePlan& stage) {
            return stage.kind ==
                   RenderStageKind::NormalizeRaw;
          });
  auto prefix_context = ColorPrefixStageContext{};
  const auto start =
      std::ranges::find_if(
          request.plan.stages,
          [&](const RenderStagePlan& stage) {
            return stage.kind ==
                   options.start_stage;
          });
  if (start == request.plan.stages.end()) {
    invalid.failure_reason =
        "requested color plan start stage is absent";
    return invalid;
  }
  request.start_stage_index =
      static_cast<std::size_t>(
          std::distance(
              request.plan.stages.begin(),
              start));
  if (options.start_stage ==
      RenderStageKind::Decode) {
    request.initial_surface.reset();
  } else {
    if (options.initial_surface == nullptr) {
      invalid.failure_reason =
          "a suffix color plan requires its predecessor surface";
      return invalid;
    }
    request.initial_surface =
        std::move(*options.initial_surface);
  }
  request.stop_after_stage_index =
      request.plan.stages.size() - 1U;
  if (options.stop_before_photographic_output) {
    if (options.start_stage !=
        RenderStageKind::Decode) {
      invalid.failure_reason =
          "pre-output stop is only valid for a Decode-started color plan";
      return invalid;
    }
    const auto geometry = std::ranges::find_if(
        request.plan.stages,
        [](const RenderStagePlan& stage) {
          return stage.kind ==
                 RenderStageKind::Geometry;
        });
    if (geometry == request.plan.stages.end()) {
      invalid.failure_reason =
          "Geometry is required for a pre-output color plan";
      return invalid;
    }
    request.stop_after_stage_index =
        static_cast<std::size_t>(
            std::distance(
                request.plan.stages.begin(),
                geometry));
  }
  auto fused_photographic_stage_index = std::optional<std::size_t>{};
  auto fused_publish_descriptor = RenderSurfaceDescriptor{};
  auto fused_working_stage_index = std::optional<std::size_t>{};
  auto fused_geometry_descriptor = RenderSurfaceDescriptor{};
  if (options.working_adjustments_geometry_fusion &&
      !defer_display_referred_text) {
    constexpr auto kWorkingStageOrder = std::array{
        RenderStageKind::SceneAdjustments,
        RenderStageKind::Tone,
        RenderStageKind::Curves,
        RenderStageKind::Look,
        RenderStageKind::Layers,
        RenderStageKind::Detail,
        RenderStageKind::Geometry,
    };
    const auto scene = std::ranges::find_if(
        request.plan.stages,
        [](const RenderStagePlan& stage) {
          return stage.kind == RenderStageKind::SceneAdjustments;
        });
    if (scene != request.plan.stages.end()) {
      const auto scene_index = static_cast<std::size_t>(
          std::distance(request.plan.stages.begin(), scene));
      const auto geometry = std::ranges::find_if(
          std::next(scene),
          request.plan.stages.end(),
          [](const RenderStagePlan& stage) {
            return stage.kind == RenderStageKind::Geometry;
          });
      auto canonical_range = geometry != request.plan.stages.end();
      auto previous_rank = std::size_t{0};
      if (canonical_range) {
        for (auto stage = scene; stage != std::next(geometry); ++stage) {
          const auto ordered = std::ranges::find(kWorkingStageOrder, stage->kind);
          if (ordered == kWorkingStageOrder.end()) {
            canonical_range = false;
            break;
          }
          const auto rank = static_cast<std::size_t>(
              std::distance(kWorkingStageOrder.begin(), ordered));
          if (stage != scene && rank <= previous_rank) {
            canonical_range = false;
            break;
          }
          previous_rank = rank;
        }
      }
      if (canonical_range) {
        fused_working_stage_index = scene_index;
        fused_geometry_descriptor = render_stage_output_descriptor(
            request, RenderStageKind::Geometry);
      }
    }
  }
  if (options.photographic_output_publish_fusion &&
      !defer_display_referred_text) {
    const auto photographic = std::ranges::find_if(
        request.plan.stages,
        [](const RenderStagePlan& stage) {
          return stage.kind == RenderStageKind::PhotographicOutput;
        });
    if (photographic != request.plan.stages.end()) {
      const auto photographic_index = static_cast<std::size_t>(
          std::distance(request.plan.stages.begin(), photographic));
      if (photographic_index + 1U < request.plan.stages.size() &&
          request.plan.stages[photographic_index + 1U].kind ==
              RenderStageKind::Publish) {
        fused_photographic_stage_index = photographic_index;
        fused_publish_descriptor = render_stage_output_descriptor(
            request, RenderStageKind::Publish);
      }
    }
  }
  auto fused_device_stage_index = std::optional<std::size_t>{};
  auto fused_device_publish_descriptor = RenderSurfaceDescriptor{};
  if (options.photographic_output_device_publish_fusion &&
      !defer_display_referred_text) {
    const auto photographic = std::ranges::find_if(
        request.plan.stages,
        [](const RenderStagePlan& stage) {
          return stage.kind == RenderStageKind::PhotographicOutput;
        });
    if (photographic != request.plan.stages.end()) {
      const auto photographic_index = static_cast<std::size_t>(
          std::distance(request.plan.stages.begin(), photographic));
      if (photographic_index + 2U < request.plan.stages.size() &&
          request.plan.stages[photographic_index + 1U].kind ==
              RenderStageKind::DeviceTransform &&
          request.plan.stages[photographic_index + 2U].kind ==
              RenderStageKind::Publish) {
        fused_device_stage_index = photographic_index;
        fused_device_publish_descriptor = render_stage_output_descriptor(
            request, RenderStageKind::Publish);
      }
    }
  }
  return executor->execute(
      std::move(request),
      [&, decode_stage =
              std::move(decode_stage)](
          const RenderStageInvocation& invocation,
          std::optional<RenderSurface> input) mutable {
        if (invocation.stage.kind ==
            RenderStageKind::Decode) {
          if (input.has_value()) {
            return RenderStageExecution{
                .failure_reason =
                    "Decode must not consume an existing surface",
            };
          }
          return decode_stage(
              invocation,
              std::nullopt);
        }
        if (!input.has_value()) {
          return RenderStageExecution{
              .failure_reason =
                  "color plan stage requires an input surface",
          };
        }
        if (fused_photographic_stage_index.has_value() &&
            invocation.stage_index == *fused_photographic_stage_index) {
          return options.photographic_output_publish_fusion(
              invocation,
              fused_publish_descriptor,
              std::move(input));
        }
        if (fused_working_stage_index.has_value() &&
            invocation.stage_index == *fused_working_stage_index) {
          return options.working_adjustments_geometry_fusion(
              invocation,
              fused_geometry_descriptor,
              std::move(input));
        }
        if (fused_device_stage_index.has_value() &&
            invocation.stage_index == *fused_device_stage_index) {
          return options.photographic_output_device_publish_fusion(
              invocation,
              fused_device_publish_descriptor,
              std::move(input));
        }
        if (!input->ensure_cpu_pixels(cancellation)) {
          return RenderStageExecution{
              .failure_reason = cancellation.cancelled()
                                    ? "color plan surface materialization cancelled"
                                    : "color plan surface could not materialize CPU pixels",
          };
        }
        const auto input_finite_validated =
            input->cpu_pixels_validated;
        auto local_telemetry =
            ColorAdjustmentTelemetry{};
        auto* stage_telemetry =
            telemetry != nullptr
                ? telemetry
                : &local_telemetry;
        if (input->highlight_reconstruction.has_value()) {
          prefix_context.highlight_reconstruction =
              *input->highlight_reconstruction;
          prefix_context.prepared = true;
        }
        auto gamut_warning_mask =
            std::move(input->gamut_warning_mask);
        auto pixels = std::move(input->pixels);
        auto backend = std::string{"cpu-reference"};
        auto fallback_reason = std::string{};
        auto failure_reason = std::string{};
        auto executed = false;
        switch (invocation.stage.kind) {
          case RenderStageKind::Decode:
            break;
          case RenderStageKind::NormalizeRaw:
            executed = apply_color_normalize_raw_stage(
                &pixels,
                static_cast<int>(
                    invocation.expected_output.width),
                static_cast<int>(
                    invocation.expected_output.height),
                state,
                &prefix_context,
                cancellation,
                stage_telemetry,
                input_finite_validated);
            break;
          case RenderStageKind::CameraCharacterization:
            if (!normalize_present &&
                !apply_color_normalize_raw_stage(
                    &pixels,
                    static_cast<int>(
                        invocation.expected_output.width),
                    static_cast<int>(
                        invocation.expected_output.height),
                    state,
                    &prefix_context,
                    cancellation,
                    stage_telemetry,
                    input_finite_validated)) {
              break;
            }
            executed =
                apply_color_camera_characterization_stage(
                    &pixels,
                    static_cast<int>(
                        invocation.expected_output.width),
                    static_cast<int>(
                        invocation.expected_output.height),
                    state,
                    prefix_context,
                    cancellation,
                    stage_telemetry,
                    input_finite_validated);
            break;
          case RenderStageKind::SceneAdjustments:
            executed = apply_color_scene_adjustments_stage(
                &pixels,
                static_cast<int>(
                    invocation.expected_output.width),
                static_cast<int>(
                    invocation.expected_output.height),
                state,
                cancellation,
                stage_telemetry,
                input_finite_validated);
            break;
          case RenderStageKind::Tone:
            executed = apply_color_tone_stage(
                &pixels,
                static_cast<int>(
                    invocation.expected_output.width),
                static_cast<int>(
                    invocation.expected_output.height),
                state,
                cancellation,
                stage_telemetry,
                input_finite_validated);
            break;
          case RenderStageKind::Curves:
            executed = apply_color_curves_stage(
                &pixels,
                static_cast<int>(
                    invocation.expected_output.width),
                static_cast<int>(
                    invocation.expected_output.height),
                state,
                cancellation,
                stage_telemetry,
                input_finite_validated);
            break;
          case RenderStageKind::Look:
            executed = apply_color_look_stage(
                &pixels,
                static_cast<int>(
                    invocation.expected_output.width),
                static_cast<int>(
                    invocation.expected_output.height),
                state,
                cancellation,
                stage_telemetry,
                input_finite_validated);
            break;
          case RenderStageKind::Layers:
            executed = apply_color_layers_stage(
                &pixels,
                static_cast<int>(
                    invocation.expected_output.width),
                static_cast<int>(
                    invocation.expected_output.height),
                state,
                options.removal_quality,
                cancellation,
                stage_telemetry,
                input_finite_validated,
                !defer_display_referred_text);
            break;
          case RenderStageKind::Detail: {
            const auto dispatches_before =
                stage_telemetry
                    ->gpu_kernel_dispatch_count;
            executed = apply_color_detail_stage(
                &pixels,
                static_cast<int>(
                    invocation.expected_output.width),
                static_cast<int>(
                    invocation.expected_output.height),
                state,
                options.removal_quality,
                cancellation,
                stage_telemetry,
                input_finite_validated);
            if (stage_telemetry
                    ->gpu_kernel_dispatch_count >
                dispatches_before) {
              backend = "metal";
            }
            break;
          }
          case RenderStageKind::Geometry:
            executed = apply_color_geometry_stage(
                &pixels,
                static_cast<int>(
                    invocation.expected_output.width),
                static_cast<int>(
                    invocation.expected_output.height),
                state,
                cancellation,
                stage_telemetry,
                input_finite_validated);
            break;
          case RenderStageKind::PhotographicOutput:
            if (options.photographic_output_config ==
                nullptr) {
              failure_reason =
                  "PhotographicOutput configuration is required";
              break;
            }
#if defined(PHOTO_EDITOR_HAS_METAL_BACKEND) && PHOTO_EDITOR_HAS_METAL_BACKEND
            if (options.allow_metal_photographic_output &&
                !defer_display_referred_text && !cancellation.cancelled()) {
              const auto timer = std::chrono::steady_clock::now();
              const auto application = compile_aces2_output_lut_application(
                  *options.photographic_output_config,
                  options.gamut_warning_target, options.output_lut_quality);
              if (application.ready() && !cancellation.cancelled()) {
                executed = metal::shared_metal_pipeline_backend().apply_aces2_output_lut_target(
                    &pixels, static_cast<int>(invocation.expected_output.width),
                    static_cast<int>(invocation.expected_output.height), application,
                    stage_telemetry);
              }
              if (executed) {
                stage_telemetry->photographic_output_ms +=
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - timer).count();
                backend = "metal-photographic-output";
                executed = !cancellation.cancelled();
                break;
              }
              // The Metal operation only commits pixels after successful
              // completion and validation; failure leaves the CPU input intact.
              fallback_reason = application.ready()
                                    ? stage_telemetry->gpu_fallback_reason
                                    : "photographic-output-lut-unavailable";
            }
#else
            if (options.allow_metal_photographic_output) {
              fallback_reason = "metal-backend-not-compiled";
            }
#endif
            executed =
                apply_color_photographic_output_stage(
                    &pixels,
                    static_cast<int>(
                        invocation.expected_output.width),
                    static_cast<int>(
                        invocation.expected_output.height),
                    *options
                         .photographic_output_config,
                    options.gamut_warning_target,
                    options.output_lut_quality,
                    cancellation,
                    stage_telemetry,
                    state.display_referred_text_layers,
                    defer_display_referred_text);
            break;
          case RenderStageKind::Proof: {
            if (options.proof_transform == nullptr ||
                !options.proof_transform->ready()) {
              failure_reason =
                  "compiled Proof transform is required";
              break;
            }
            const auto timer =
                std::chrono::steady_clock::now();
            if (options
                    .proof_gamut_warning_transform !=
                nullptr) {
              const auto* gamut_transform =
                  options
                      .proof_gamut_warning_transform;
              if (!gamut_transform->ready() ||
                  !gamut_transform->description()
                       .gamut_check) {
                failure_reason =
                    "compiled Proof gamut-warning transform is invalid";
                break;
              }
              auto gamut_pixels =
                  std::vector<IccRgbaFloat>{};
              gamut_pixels.reserve(pixels.size());
              for (const auto& pixel : pixels) {
                gamut_pixels.push_back(
                    IccRgbaFloat{
                        .red = pixel.red,
                        .green = pixel.green,
                        .blue = pixel.blue,
                        .alpha = pixel.alpha,
                    });
              }
              gamut_warning_mask =
                  gamut_transform->gamut_mask(
                      gamut_pixels);
              if (gamut_warning_mask.size() !=
                  pixels.size()) {
                failure_reason =
                    "Proof gamut-warning mask failed";
                break;
              }
            }
            executed = apply_compiled_icc_transform(
                &pixels,
                *options.proof_transform,
                cancellation);
            backend = "littlecms";
            stage_telemetry->proof_ms +=
                std::chrono::duration_cast<
                    std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() -
                    timer)
                    .count();
            break;
          }
          case RenderStageKind::DeviceTransform: {
            if (options.device_transform == nullptr ||
                !options.device_transform->ready()) {
              failure_reason =
                  "compiled DeviceTransform is required";
              break;
            }
            const auto timer =
                std::chrono::steady_clock::now();
            executed = apply_compiled_icc_transform(
                &pixels,
                *options.device_transform,
                cancellation);
            backend = "littlecms";
            stage_telemetry->device_transform_ms +=
                std::chrono::duration_cast<
                    std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() -
                    timer)
                    .count();
            break;
          }
          case RenderStageKind::Publish:
            if (options.publish_display_target ==
                nullptr) {
              failure_reason =
                  "Publish display target is required";
              break;
            }
            if (options.device_transform != nullptr) {
              // A compiled ICC device transform produces destination-encoded
              // device values. Publish remains a real plan boundary, but it
              // must not apply the display transfer function a second time.
              const auto publish_timer =
                  std::chrono::steady_clock::now();
              executed =
                  !cancellation.cancelled() &&
                  std::ranges::all_of(
                      pixels,
                      [](const ColorPixel& pixel) {
                        return
                            std::isfinite(pixel.red) &&
                            std::isfinite(pixel.green) &&
                            std::isfinite(pixel.blue) &&
                            std::isfinite(pixel.alpha);
                      });
              if (executed) {
                backend =
                    "device-encoded-identity";
                stage_telemetry->publish_ms +=
                    std::chrono::duration_cast<
                        std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() -
                        publish_timer)
                        .count();
              }
            } else {
              executed = apply_color_publish_stage(
                  &pixels,
                  static_cast<int>(
                      invocation.expected_output.width),
                  static_cast<int>(
                      invocation.expected_output.height),
                  *options.publish_display_target,
                  cancellation,
                  stage_telemetry);
            }
            break;
        }
        if (!executed) {
          return RenderStageExecution{
              .failure_reason =
                  !failure_reason.empty()
                      ? std::move(failure_reason)
                      : cancellation.cancelled()
                            ? "color plan execution cancelled"
                            : "color plan stage execution failed",
          };
        }
        return RenderStageExecution{
            .output =
                RenderSurface{
                    .descriptor =
                        invocation.expected_output,
                    .pixels = std::move(pixels),
                    .identity =
                        "surface:" +
                        invocation.stage_cache_key,
                    .highlight_reconstruction =
                        invocation.stage.kind ==
                                RenderStageKind::NormalizeRaw
                            ? std::optional<
                                  HighlightReconstructionAnalysis>{
                                  prefix_context
                                      .highlight_reconstruction,
                              }
                            : std::nullopt,
                    .gamut_warning_mask =
                        std::move(gamut_warning_mask),
                    .cpu_pixels_validated = true,
                },
            .backend = std::move(backend),
            .fallback_reason = std::move(fallback_reason),
        };
      });
}

void RenderExecutor::clear_cache() {
  const std::scoped_lock lock(cache_mutex_);
  cache_.clear();
  cache_age_.clear();
  cache_bytes_ = 0;
}

std::uint64_t RenderExecutor::cache_bytes() const noexcept {
  const std::scoped_lock lock(cache_mutex_);
  return cache_bytes_;
}

std::size_t RenderExecutor::cache_entries() const noexcept {
  const std::scoped_lock lock(cache_mutex_);
  return cache_.size();
}

std::optional<RenderSurface> RenderExecutor::find_cached(
    const std::string& key) {
  const std::scoped_lock lock(cache_mutex_);
  const auto found = cache_.find(key);
  if (found == cache_.end()) {
    return std::nullopt;
  }
  cache_age_.splice(
      cache_age_.end(),
      cache_age_,
      found->second.age);
  return found->second.surface;
}

void RenderExecutor::insert_cached(
    const std::string& key,
    const RenderSurface& surface) {
  const auto bytes = surface.byte_size();
  if (maximum_cache_bytes_ == 0 ||
      bytes > maximum_cache_bytes_) {
    return;
  }
  const std::scoped_lock lock(cache_mutex_);
  if (const auto existing = cache_.find(key);
      existing != cache_.end()) {
    cache_bytes_ -= existing->second.bytes;
    cache_age_.erase(existing->second.age);
    cache_.erase(existing);
  }
  while (!cache_age_.empty() &&
         cache_bytes_ + bytes > maximum_cache_bytes_) {
    const auto oldest = cache_age_.front();
    cache_age_.pop_front();
    const auto found = cache_.find(oldest);
    if (found != cache_.end()) {
      cache_bytes_ -= found->second.bytes;
      cache_.erase(found);
    }
  }
  cache_age_.push_back(key);
  const auto age = std::prev(cache_age_.end());
  cache_.emplace(
      key,
      CacheEntry{
          .surface = surface,
          .bytes = bytes,
          .age = age,
      });
  cache_bytes_ += bytes;
}

}  // namespace photo_editor::pipeline
