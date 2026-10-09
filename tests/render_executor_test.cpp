#include "photo_editor/pipeline/render_executor.h"
#if defined(PHOTO_EDITOR_HAS_METAL_BACKEND) && PHOTO_EDITOR_HAS_METAL_BACKEND
#include "photo_editor/pipeline/metal/metal_pipeline_backend.h"
#endif

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <utility>

namespace {

int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

photo_editor::pipeline::RenderExecutionRequest request_fixture() {
  using namespace photo_editor::pipeline;
  return RenderExecutionRequest{
      .plan =
          RenderPlan{
              .recipe_cache_key = "recipe:test",
              .source = AssetIdentifier{"source:test"},
              .rendering_target =
                  DisplayRenderingTarget::DisplayP3Simulation,
              .source_width = 2,
              .source_height = 1,
              .stages =
                  {
                      RenderStagePlan{
                          .kind = RenderStageKind::Decode,
                          .cache_identity = "decode:v1",
                      },
                      RenderStagePlan{
                          .kind =
                              RenderStageKind::CameraCharacterization,
                          .cache_identity = "characterize:v1",
                      },
                      RenderStagePlan{
                          .kind =
                              RenderStageKind::PhotographicOutput,
                          .cache_identity = "output:v1",
                      },
                      RenderStagePlan{
                          .kind = RenderStageKind::DeviceTransform,
                          .cache_identity = "device:v1",
                      },
                      RenderStagePlan{
                          .kind = RenderStageKind::Publish,
                          .cache_identity = "publish:v1",
                      },
                  },
              .plan_cache_key = "plan:test",
          },
      .backend_version = "cpu-reference:v1",
      .target_identity = "display-p3:100-nits",
      .profile_identity = "icc:display-p3:test",
      .source_color_space_identity =
          "camera-native:test",
      .working_color_space_identity =
          "Linear ProPhoto RGB",
      .rendering_target_color_space_identity =
          "Display P3",
      .proof_color_space_identity = "proof:cmyk:test",
      .device_color_space_identity =
          "Display P3 device",
      .device_transfer_identity = "sRGB",
      .output_width = 2,
      .output_height = 1,
      .generation = 7,
  };
}

class FakeRenderSurfaceBacking final
    : public photo_editor::pipeline::RenderSurfaceBacking {
 public:
  FakeRenderSurfaceBacking(
      photo_editor::pipeline::RenderSurfaceDescriptor descriptor,
      std::vector<photo_editor::pipeline::ColorPixel> pixels)
      : descriptor_(std::move(descriptor)), pixels_(std::move(pixels)) {}

  [[nodiscard]] std::string_view backend_identity() const noexcept override {
    return "fake-native";
  }
  [[nodiscard]] std::uint64_t byte_size() const noexcept override {
    return static_cast<std::uint64_t>(pixels_.size()) *
           sizeof(photo_editor::pipeline::ColorPixel);
  }
  [[nodiscard]] bool valid_for(
      const photo_editor::pipeline::RenderSurfaceDescriptor& descriptor)
      const noexcept override {
    return descriptor == descriptor_;
  }
  [[nodiscard]] bool copy_pixels(
      std::vector<photo_editor::pipeline::ColorPixel>* destination,
      photo_editor::pipeline::CancellationToken cancellation) const override {
    if (destination == nullptr || cancellation.cancelled()) {
      return false;
    }
    *destination = pixels_;
    ++copy_count_;
    return true;
  }
  [[nodiscard]] int copy_count() const noexcept { return copy_count_; }

 private:
  photo_editor::pipeline::RenderSurfaceDescriptor descriptor_;
  std::vector<photo_editor::pipeline::ColorPixel> pixels_;
  mutable int copy_count_ = 0;
};

}  // namespace

int main() {
  using namespace photo_editor::pipeline;

  auto native_request = request_fixture();
  native_request.stop_after_stage_index = 0U;
  auto native_callback_count = 0;
  auto native_backing = std::shared_ptr<FakeRenderSurfaceBacking>{};
  RenderExecutor native_executor(1024);
  const auto native_callback = [&](const RenderStageInvocation& invocation,
                                   std::optional<RenderSurface>) {
    ++native_callback_count;
    native_backing = std::make_shared<FakeRenderSurfaceBacking>(
        invocation.expected_output,
        std::vector<ColorPixel>{ColorPixel{0.1F, 0.2F, 0.3F, 1.0F},
                                ColorPixel{0.4F, 0.5F, 0.6F, 1.0F}});
    return RenderStageExecution{
        .output = RenderSurface{
            .descriptor = invocation.expected_output,
            .backing = native_backing,
            .identity = "surface:fake-native",
        },
        .backend = "fake-native",
    };
  };
  const auto native_first = native_executor.execute(native_request, native_callback);
  native_callback_count = 0;
  const auto native_cached = native_executor.execute(native_request, native_callback);
  if (!native_first.ok() || !native_cached.ok() || native_callback_count != 0 ||
      native_cached.output->pixels.size() != 0U ||
      native_cached.output->backing != native_backing ||
      native_executor.cache_bytes() != native_backing->byte_size()) {
    return fail("executor must validate, account, and cache native-only surfaces");
  }
  auto materialized_native = *native_cached.output;
  if (!materialized_native.ensure_cpu_pixels() ||
      materialized_native.pixels.size() != 2U || native_backing->copy_count() != 1 ||
      materialized_native.byte_size() != native_backing->byte_size() * 2U) {
    return fail("native surfaces must materialize exact CPU pixels only on demand");
  }
  auto cancelled_native = *native_cached.output;
  const auto cancellation_requested = std::atomic_bool{true};
  if (cancelled_native.ensure_cpu_pixels(
          CancellationToken{.cancellation_requested = &cancellation_requested}) ||
      !cancelled_native.pixels.empty() || native_backing->copy_count() != 1) {
    return fail("surface materialization must preserve an empty destination on failure");
  }

  RenderExecutor executor(1024);
  auto request = request_fixture();
  auto callback_count = 0;
  const auto callback =
      [&](const RenderStageInvocation& invocation,
          std::optional<RenderSurface> input) {
        ++callback_count;
        auto pixels =
            input.has_value()
                ? std::move(input->pixels)
                : std::vector<ColorPixel>{
                      ColorPixel{.red = 0.25F, .green = 0.5F, .blue = 1.5F},
                      ColorPixel{.red = 0.75F, .green = 0.4F, .blue = 0.1F},
                  };
        return RenderStageExecution{
            .output =
                RenderSurface{
                    .descriptor = invocation.expected_output,
                    .pixels = std::move(pixels),
                    .identity =
                        "surface:" + invocation.stage_cache_key,
                },
            .backend = "cpu-reference",
        };
      };

  const auto first = executor.execute(request, callback);
  if (!first.ok() ||
      first.completed_stages.size() != request.plan.stages.size() ||
      callback_count != static_cast<int>(request.plan.stages.size()) ||
      first.plan_cache_key != request.plan.plan_cache_key ||
      first.completed_stages.front().output_descriptor.pixel_encoding !=
          RenderPixelEncoding::CameraLinearFloat ||
      first.completed_stages.front().output_descriptor
              .color_space_identity !=
          "camera-native:test" ||
      first.completed_stages.at(1).output_descriptor.color_domain !=
          RenderColorDomain::WorkingScene ||
      first.completed_stages.at(1).output_descriptor
              .color_space_identity !=
          "Linear ProPhoto RGB" ||
      first.completed_stages.at(2).output_descriptor.color_domain !=
          RenderColorDomain::RenderingTarget ||
      first.completed_stages.at(2).output_descriptor
              .color_space_identity !=
          "Display P3" ||
      first.completed_stages.back().output_descriptor.color_domain !=
          RenderColorDomain::Device ||
      first.completed_stages.back().output_descriptor
              .transfer_identity !=
          "sRGB") {
    return fail("executor should enforce the typed v13 stage-domain sequence");
  }

  callback_count = 0;
  const auto repeated = executor.execute(request, callback);
  if (!repeated.ok() || callback_count != 1 ||
      repeated.completed_stages.back().kind !=
          RenderStageKind::Publish ||
      repeated.completed_stages.back().cache_hit ||
      !repeated.completed_stages.front().cache_hit ||
      executor.cache_entries() != request.plan.stages.size() - 1 ||
      executor.cache_bytes() > 1024) {
    return fail("executor should reuse bounded intermediates but always republish");
  }

  auto different_target = request;
  different_target.target_identity = "srgb:100-nits";
  const auto decode_key =
      render_stage_cache_key(
          request,
          request.plan.stages.front(),
          std::nullopt);
  if (decode_key !=
          render_stage_cache_key(
          different_target,
          different_target.plan.stages.front(),
          std::nullopt) ||
      render_stage_cache_key(
          request,
          request.plan.stages.at(2),
          std::nullopt) ==
          render_stage_cache_key(
              different_target,
              different_target.plan.stages.at(2),
              std::nullopt)) {
    return fail(
        "target changes must preserve decode identity and invalidate photographic output");
  }
  auto different_device_profile = request;
  different_device_profile.profile_identity = "icc:display-p3:replacement";
  if (render_stage_cache_key(
          request,
          request.plan.stages.at(2),
          std::nullopt) !=
          render_stage_cache_key(
              different_device_profile,
              different_device_profile.plan.stages.at(2),
              std::nullopt) ||
      render_stage_cache_key(
          request,
          request.plan.stages.at(3),
          std::nullopt) ==
          render_stage_cache_key(
              different_device_profile,
              different_device_profile.plan.stages.at(3),
              std::nullopt)) {
    return fail(
        "device profile changes must preserve photographic output and invalidate only the device boundary");
  }
  auto different_plan_key = request;
  different_plan_key.plan.plan_cache_key = "plan:downstream-only-change";
  if (decode_key !=
      render_stage_cache_key(
          different_plan_key,
          different_plan_key.plan.stages.front(),
          std::nullopt)) {
    return fail(
        "whole-plan identity must not invalidate an unchanged upstream stage");
  }
  auto different_source_area = request;
  different_source_area.source_area_x = 0.25;
  different_source_area.source_area_width = 0.75;
  if (decode_key ==
      render_stage_cache_key(
          different_source_area,
          different_source_area.plan.stages.front(),
          std::nullopt) ||
      render_stage_output_descriptor(
          different_source_area,
          RenderStageKind::Decode)
              .source_area_x != 0.25) {
    return fail(
        "source-area policy must participate in stage identity and typed descriptors");
  }

  auto text_transform_request = request_fixture();
  text_transform_request.plan.plan_cache_key = "plan:text-position-before";
  text_transform_request.plan.stages = {
      RenderStagePlan{.kind = RenderStageKind::Decode,
                      .cache_identity = "decode:text-source"},
      RenderStagePlan{.kind = RenderStageKind::CameraCharacterization,
                      .cache_identity = "camera:text-source"},
      RenderStagePlan{.kind = RenderStageKind::SceneAdjustments,
                      .cache_identity = "scene:text-source"},
      RenderStagePlan{.kind = RenderStageKind::Tone,
                      .cache_identity = "tone:text-source"},
      RenderStagePlan{.kind = RenderStageKind::Curves,
                      .cache_identity = "curves:text-source"},
      RenderStagePlan{.kind = RenderStageKind::Look,
                      .cache_identity = "look:text-source"},
      RenderStagePlan{.kind = RenderStageKind::Layers,
                      .cache_identity = "layers:text-position-before"},
      RenderStagePlan{.kind = RenderStageKind::Detail,
                      .cache_identity = "detail:text-position-before"},
      RenderStagePlan{.kind = RenderStageKind::Geometry,
                      .cache_identity = "geometry:text-position-before"},
      RenderStagePlan{.kind = RenderStageKind::PhotographicOutput,
                      .cache_identity = "output:text-position-before"},
      RenderStagePlan{.kind = RenderStageKind::DeviceTransform,
                      .cache_identity = "device:text-position-before"},
      RenderStagePlan{.kind = RenderStageKind::Publish,
                      .cache_identity = "publish:text-position-before"},
  };
  constexpr auto kLayersStageIndex = std::size_t{6};
  text_transform_request.cache_write_stage_index_exclusive_limit =
      kLayersStageIndex;
  auto moved_text_transform_request = text_transform_request;
  moved_text_transform_request.plan.plan_cache_key =
      "plan:text-position-after";
  moved_text_transform_request.plan.stages[6].cache_identity =
      "layers:text-position-after";
  moved_text_transform_request.plan.stages[7].cache_identity =
      "detail:text-position-after";
  moved_text_transform_request.plan.stages[8].cache_identity =
      "geometry:text-position-after";
  moved_text_transform_request.plan.stages[9].cache_identity =
      "output:text-position-after";
  moved_text_transform_request.plan.stages[10].cache_identity =
      "device:text-position-after";
  moved_text_transform_request.plan.stages[11].cache_identity =
      "publish:text-position-after";

  RenderExecutor text_transform_executor(1024);
  auto text_transform_callback_count = 0;
  const auto text_transform_callback =
      [&](const RenderStageInvocation& invocation,
          std::optional<RenderSurface> input) {
        ++text_transform_callback_count;
        auto pixels = input.has_value()
                          ? std::move(input->pixels)
                          : std::vector<ColorPixel>{
                                ColorPixel{0.2F, 0.4F, 0.6F, 1.0F},
                                ColorPixel{0.6F, 0.4F, 0.2F, 1.0F}};
        return RenderStageExecution{
            .output = RenderSurface{
                .descriptor = invocation.expected_output,
                .pixels = std::move(pixels),
                .identity = "surface:" + invocation.stage_cache_key,
            },
            .backend = "cpu-reference",
        };
      };
  const auto before_transform = text_transform_executor.execute(
      text_transform_request, text_transform_callback);
  if (!before_transform.ok() ||
      text_transform_callback_count !=
          static_cast<int>(text_transform_request.plan.stages.size()) ||
      text_transform_executor.cache_entries() != kLayersStageIndex ||
      std::any_of(
          before_transform.completed_stages.cbegin() +
              static_cast<std::ptrdiff_t>(kLayersStageIndex),
          before_transform.completed_stages.cend(),
          [](const auto& stage) { return stage.cache_hit; })) {
    return fail(
        "exclusive stage limit should cache only the pre-Layers prefix");
  }

  text_transform_callback_count = 0;
  const auto after_transform = text_transform_executor.execute(
      moved_text_transform_request, text_transform_callback);
  if (!after_transform.ok() ||
      text_transform_callback_count !=
          static_cast<int>(moved_text_transform_request.plan.stages.size() -
                           kLayersStageIndex) ||
      after_transform.plan_cache_key == before_transform.plan_cache_key ||
      text_transform_executor.cache_entries() != kLayersStageIndex) {
    return fail(
        "text movement should reuse only unchanged prefix stages without caching Layers or its suffix");
  }
  for (auto index = std::size_t{0};
       index < moved_text_transform_request.plan.stages.size(); ++index) {
    const auto expected_cache_hit = index < kLayersStageIndex;
    if (after_transform.completed_stages[index].cache_hit !=
        expected_cache_hit) {
      return fail(
          "text movement should hit every unchanged pre-Layers stage and miss Layers onward");
    }
  }

  auto cancelled_request = request;
  cancelled_request.cancelled = []() { return true; };
  const auto cancelled = executor.execute(cancelled_request, callback);
  if (cancelled.status != RenderExecutionStatus::Cancelled ||
      !cancelled.completed_stages.empty()) {
    return fail("executor should cancel before invoking the next stage");
  }

  auto stale_request = request;
  stale_request.latest_generation = []() {
    return std::uint64_t{8};
  };
  const auto stale = executor.execute(stale_request, callback);
  if (stale.status != RenderExecutionStatus::Stale ||
      !stale.completed_stages.empty()) {
    return fail("executor should reject stale generations before publication");
  }

  RenderExecutor contract_executor;
  const auto violation = contract_executor.execute(
      request,
      [](const RenderStageInvocation& invocation,
         std::optional<RenderSurface>) {
        auto wrong = invocation.expected_output;
        wrong.color_domain = RenderColorDomain::Device;
        return RenderStageExecution{
            .output =
                RenderSurface{
                    .descriptor = wrong,
                    .pixels =
                        {
                            ColorPixel{},
                            ColorPixel{},
                        },
                    .identity = "invalid-contract",
                },
            .backend = "test",
        };
      });
  if (violation.status !=
          RenderExecutionStatus::ContractViolation ||
      violation.failure_stage != "decode") {
    return fail("executor should fail closed on a stage pixel-contract violation");
  }
  const auto non_finite = contract_executor.execute(
      request,
      [](const RenderStageInvocation& invocation,
         std::optional<RenderSurface>) {
        return RenderStageExecution{
            .output =
                RenderSurface{
                    .descriptor = invocation.expected_output,
                    .pixels =
                        {
                            ColorPixel{
                                .red =
                                    std::numeric_limits<float>::infinity(),
                            },
                            ColorPixel{},
                        },
                    .identity = "non-finite-contract",
                },
            .backend = "test",
        };
      });
  if (non_finite.status !=
          RenderExecutionStatus::ContractViolation ||
      non_finite.failure_stage != "decode") {
    return fail(
        "executor should reject non-finite pixels before cache or publication");
  }

  auto suffix_request = request;
  suffix_request.bypass_cache = true;
  suffix_request.start_stage_index = 2;
  suffix_request.stop_after_stage_index = 2;
  suffix_request.initial_surface =
      RenderSurface{
          .descriptor = render_stage_output_descriptor(
              suffix_request,
              RenderStageKind::CameraCharacterization),
          .pixels =
              {
                  ColorPixel{
                      .red = 0.25F,
                      .green = 0.5F,
                      .blue = 1.5F,
                  },
                  ColorPixel{
                      .red = 0.75F,
                      .green = 0.4F,
                      .blue = 0.1F,
                  },
              },
          .identity = "surface:characterized-input",
      };
  callback_count = 0;
  const auto suffix =
      executor.execute(suffix_request, callback);
  if (!suffix.ok() || callback_count != 1 ||
      suffix.completed_stages.size() != 1 ||
      suffix.completed_stages.front().kind !=
          RenderStageKind::PhotographicOutput ||
      suffix.output->descriptor.color_domain !=
          RenderColorDomain::RenderingTarget) {
    return fail(
        "executor should resume from a validated identity-bearing intermediate surface");
  }
  auto move_owned_suffix_request = suffix_request;
  const auto* expected_move_owned_pixels =
      move_owned_suffix_request.initial_surface->pixels.data();
  const ColorPixel* received_move_owned_pixels = nullptr;
  const auto move_owned_suffix = executor.execute(
      std::move(move_owned_suffix_request),
      [&](const RenderStageInvocation& invocation,
          std::optional<RenderSurface> input) {
        received_move_owned_pixels = input->pixels.data();
        auto pixels = std::move(input->pixels);
        return RenderStageExecution{
            .output =
                RenderSurface{
                    .descriptor = invocation.expected_output,
                    .pixels = std::move(pixels),
                    .identity =
                        "move-owned:" +
                        invocation.stage_cache_key,
                },
            .backend = "cpu-reference",
        };
      });
  if (!move_owned_suffix.ok() ||
      received_move_owned_pixels !=
          expected_move_owned_pixels ||
      move_owned_suffix.output->pixels.data() !=
          expected_move_owned_pixels) {
    return fail(
        "move-owned stage execution should transfer a full surface without copying its pixel allocation");
  }
  auto invalid_suffix = suffix_request;
  invalid_suffix.initial_surface->descriptor.color_domain =
      RenderColorDomain::Device;
  const auto invalid_suffix_result =
      executor.execute(invalid_suffix, callback);
  if (invalid_suffix_result.status !=
          RenderExecutionStatus::ContractViolation ||
      invalid_suffix_result.failure_stage !=
          "photographic_output") {
    return fail(
        "executor should fail closed when a resumed surface violates the preceding stage contract");
  }

  auto fallback_request = request;
  fallback_request.bypass_cache = true;
  const auto fallback = executor.execute(
      fallback_request,
      [](const RenderStageInvocation& invocation,
         std::optional<RenderSurface> input) {
        auto pixels =
            input.has_value()
                ? std::move(input->pixels)
                : std::vector<ColorPixel>{
                      ColorPixel{},
                      ColorPixel{},
                  };
        const auto fallback_stage =
            invocation.stage.kind ==
            RenderStageKind::CameraCharacterization;
        return RenderStageExecution{
            .output =
                RenderSurface{
                    .descriptor = invocation.expected_output,
                    .pixels = std::move(pixels),
                    .identity =
                        "fallback-surface:" + invocation.stage_cache_key,
                },
            .backend = fallback_stage ? "cpu-reference" : "metal",
            .fallback_reason =
                fallback_stage
                    ? "camera-characterization-not-supported-by-metal"
                    : "",
        };
      });
  const auto fallback_manifest =
      std::ranges::find_if(
          fallback.completed_stages,
          [](const auto& stage) {
            return !stage.fallback_reason.empty();
          });
  if (!fallback.ok() ||
      fallback_manifest == fallback.completed_stages.cend() ||
      fallback_manifest->kind !=
          RenderStageKind::CameraCharacterization ||
      fallback_manifest->backend != "cpu-reference" ||
      fallback_manifest->fallback_reason !=
          "camera-characterization-not-supported-by-metal") {
    return fail(
        "successful backend fallback must identify the exact logical stage and reason");
  }

  auto fusion_request = request_fixture();
  fusion_request.bypass_cache = true;
  fusion_request.stop_after_stage_index = 1U;
  const auto fusion_callback =
      [&](bool reference_passed) {
        return [=](
                   const RenderStageInvocation&
                       invocation,
                   std::optional<RenderSurface>
                       input) {
          auto pixels =
              input.has_value()
                  ? std::move(input->pixels)
                  : std::vector<ColorPixel>{
                        ColorPixel{
                            .red = 0.18F,
                            .green = 0.18F,
                            .blue = 0.18F,
                            .alpha = 1.0F,
                        },
                        ColorPixel{
                            .red = 0.72F,
                            .green = 0.44F,
                            .blue = 0.21F,
                            .alpha = 1.0F,
                        },
                    };
          return RenderStageExecution{
              .output =
                  RenderSurface{
                      .descriptor =
                          render_stage_output_descriptor(
                              fusion_request,
                              RenderStageKind::
                                  CameraCharacterization),
                      .pixels =
                          std::move(pixels),
                      .identity =
                          "surface:fused:" +
                          invocation
                              .stage_cache_key,
                  },
              .backend = "metal-fused",
              .fusion_evidence =
                  RenderFusionEvidence{
                      .unfused_reference_passed =
                          reference_passed,
                      .unfused_reference_identity =
                          reference_passed
                              ? "cpu-reference:fixture-pass"
                              : "",
                      .logical_stages_covered =
                          {
                              RenderStageKind::
                                  Decode,
                              RenderStageKind::
                                  CameraCharacterization,
                          },
                  },
          };
        };
      };
  RenderExecutor fusion_executor(4096);
  const auto rejected_fusion =
      fusion_executor.execute(
          fusion_request,
          fusion_callback(false));
  const auto admitted_fusion =
      fusion_executor.execute(
          fusion_request,
          fusion_callback(true));
  if (rejected_fusion.status !=
          RenderExecutionStatus::
              ContractViolation ||
      rejected_fusion.failure_reason.find(
          "passed unfused reference") ==
          std::string::npos ||
      !admitted_fusion.ok() ||
      admitted_fusion.completed_stages.size() !=
          2U ||
      !std::ranges::all_of(
          admitted_fusion.completed_stages,
          [](const auto& completed) {
            return completed
                       .fusion_evidence
                       .has_value() &&
                   completed
                           .fusion_evidence
                           ->logical_stages_covered
                           .size() == 2U &&
                   !completed.cache_hit;
          })) {
    return fail(
        "backend fusion must fail closed without an unfused reference and report every covered logical stage when admitted");
  }

  const auto linear_srgb_payload =
      builtin_rgb_icc_profile_payload(
          make_linear_srgb_color_space());
  const auto encoded_srgb_payload =
      builtin_rgb_icc_profile_payload(
          output_color_profile_from_name("sRGB")
              .color_space);
  const auto proof_transform =
      compile_icc_proofing_transform(
          linear_srgb_payload,
          encoded_srgb_payload,
          linear_srgb_payload,
          IccProofTransformOptions{
              .rendering_intent =
                  IccRenderingIntent::Perceptual,
              .proofing_intent =
                  IccRenderingIntent::RelativeColorimetric,
              .black_point_compensation = true,
              .gamut_check = false,
              .pixel_format =
                  IccPixelFormat::RgbaFloat32,
              .target = "executor-proof-test",
          });
  const auto device_transform =
      compile_icc_transform(
          linear_srgb_payload,
          encoded_srgb_payload,
          IccTransformOptions{
              .rendering_intent =
                  IccRenderingIntent::Perceptual,
              .black_point_compensation = true,
              .pixel_format =
                  IccPixelFormat::RgbaFloat32,
              .target = "executor-device-test",
          });
  const auto gamut_transform =
      compile_icc_proofing_transform(
          linear_srgb_payload,
          encoded_srgb_payload,
          linear_srgb_payload,
          IccProofTransformOptions{
              .rendering_intent =
                  IccRenderingIntent::Perceptual,
              .proofing_intent =
                  IccRenderingIntent::RelativeColorimetric,
              .black_point_compensation = true,
              .gamut_check = true,
              .pixel_format =
                  IccPixelFormat::RgbaFloat32,
              .target =
                  "executor-proof-gamut-mask-test",
          });
  auto full_request = request_fixture();
  full_request.plan.plan_cache_key =
      "plan:single-submission";
  full_request.plan.recipe_cache_key =
      "recipe:single-submission";
  full_request.plan.stages = {
      RenderStagePlan{
          .kind = RenderStageKind::Decode,
          .cache_identity = "decode:single",
      },
      RenderStagePlan{
          .kind =
              RenderStageKind::NormalizeRaw,
          .cache_identity =
              "normalize-raw:single",
      },
      RenderStagePlan{
          .kind =
              RenderStageKind::CameraCharacterization,
          .cache_identity = "camera:single",
      },
      RenderStagePlan{
          .kind = RenderStageKind::SceneAdjustments,
          .cache_identity = "scene:single",
      },
      RenderStagePlan{
          .kind = RenderStageKind::Tone,
          .cache_identity = "tone:single",
      },
      RenderStagePlan{
          .kind = RenderStageKind::Look,
          .cache_identity = "look:single",
      },
      RenderStagePlan{
          .kind = RenderStageKind::Layers,
          .cache_identity = "layers:single",
      },
      RenderStagePlan{
          .kind = RenderStageKind::Detail,
          .cache_identity = "detail:single",
      },
      RenderStagePlan{
          .kind = RenderStageKind::Geometry,
          .cache_identity = "geometry:single",
      },
      RenderStagePlan{
          .kind = RenderStageKind::PhotographicOutput,
          .cache_identity = "output:single",
      },
      RenderStagePlan{
          .kind = RenderStageKind::Proof,
          .cache_identity = "proof:single",
      },
      RenderStagePlan{
          .kind =
              RenderStageKind::DeviceTransform,
          .cache_identity = "device:single",
      },
      RenderStagePlan{
          .kind = RenderStageKind::Publish,
          .cache_identity = "publish:single",
      },
  };
  auto full_state = ColorAdjustmentState{};
  full_state.color_pipeline =
      default_color_pipeline_config();
  const auto full_target =
      make_sdr_display_target(
          output_color_profile_from_name("sRGB"));
  full_state.color_pipeline
      .export_output_profile =
      output_color_profile_from_name("sRGB");
  full_state.color_pipeline
      .export_display_target = full_target;
  RenderExecutor full_executor(4096);
  auto full_decode_calls = 0;
  const auto full_result = execute_color_plan(
      &full_executor,
      full_request,
      [&](const RenderStageInvocation& invocation,
          std::optional<RenderSurface> input) {
        ++full_decode_calls;
        if (input.has_value()) {
          return RenderStageExecution{
              .failure_reason =
                  "single-submission Decode received input",
          };
        }
        return RenderStageExecution{
            .output =
                RenderSurface{
                    .descriptor =
                        invocation.expected_output,
                    .pixels =
                        {
                            ColorPixel{
                                .red = 0.18F,
                                .green = 0.18F,
                                .blue = 0.18F,
                                .alpha = 1.0F,
                            },
                            ColorPixel{
                                .red = 0.72F,
                                .green = 0.44F,
                                .blue = 0.21F,
                                .alpha = 1.0F,
                            },
                        },
                    .identity =
                        "surface:decode:single",
                },
            .backend = "fixture-decoder",
        };
      },
      full_state,
      ColorPlanExecutionOptions{
          .removal_quality =
              RemovalQuality::Final,
          .photographic_output_config =
              &full_state.color_pipeline,
          .gamut_warning_target =
              GamutWarningTarget::ExportOutput,
          .output_lut_quality =
              Aces2OutputLutQuality::Final,
          .proof_transform =
              &proof_transform,
          .proof_gamut_warning_transform =
              &gamut_transform,
          .device_transform =
              &device_transform,
          .publish_display_target =
              &full_target,
      });
  if (!full_result.ok() ||
      !full_result.output.has_value() ||
      full_decode_calls != 1 ||
      full_result.completed_stages.size() !=
          full_request.plan.stages.size() ||
      full_result.completed_stages.front().kind !=
          RenderStageKind::Decode ||
      full_result.completed_stages.back().kind !=
          RenderStageKind::Publish ||
      full_result.completed_stages.back().backend !=
          "device-encoded-identity" ||
      full_result.output->gamut_warning_mask.size() !=
          full_result.output->pixels.size()) {
    return fail(
        "one executor submission should own the complete Decode-through-Publish plan without double-encoding ICC device values");
  }
  auto mismatched_transition_config = full_state.color_pipeline;
  mismatched_transition_config.renderer_transition_look =
      RendererTransitionLook::PreserveLegacyDirectLuminanceV1;
  auto mismatch_decode_calls = 0;
  const auto mismatched_transition_result = execute_color_plan(
      &full_executor,
      full_request,
      [&](const RenderStageInvocation&,
          std::optional<RenderSurface>) {
        ++mismatch_decode_calls;
        return RenderStageExecution{};
      },
      full_state,
      ColorPlanExecutionOptions{
          .photographic_output_config =
              &mismatched_transition_config,
      });
  if (mismatched_transition_result.ok() ||
      mismatch_decode_calls != 0 ||
      mismatched_transition_result.failure_reason !=
          "canonical plan and photographic output transition look differ") {
    return fail(
        "executor must fail closed before decode when plan and output transition identities differ");
  }
  auto fused_working_request = full_request;
  fused_working_request.plan.plan_cache_key = "plan:fused-working-stages";
  fused_working_request.bypass_cache = true;
  auto fused_working_callback_count = 0;
  auto fused_working_received_characterized_input = false;
  const auto fused_working_result = execute_color_plan(
      &full_executor,
      fused_working_request,
      [&](const RenderStageInvocation& invocation,
          std::optional<RenderSurface> input) {
        if (input.has_value()) {
          return RenderStageExecution{
              .failure_reason = "fused-working Decode received input",
          };
        }
        return RenderStageExecution{
            .output = RenderSurface{
                .descriptor = invocation.expected_output,
                .pixels = {
                    ColorPixel{.red = 0.18F, .green = 0.18F, .blue = 0.18F, .alpha = 1.0F},
                    ColorPixel{.red = 0.72F, .green = 0.44F, .blue = 0.21F, .alpha = 1.0F},
                },
                .identity = "surface:decode:fused-working",
            },
            .backend = "fixture-decoder",
        };
      },
      full_state,
      ColorPlanExecutionOptions{
          .removal_quality = RemovalQuality::Final,
          .photographic_output_config = &full_state.color_pipeline,
          .gamut_warning_target = GamutWarningTarget::ExportOutput,
          .output_lut_quality = Aces2OutputLutQuality::Final,
          .proof_transform = &proof_transform,
          .proof_gamut_warning_transform = &gamut_transform,
          .device_transform = &device_transform,
          .publish_display_target = &full_target,
          .working_adjustments_geometry_fusion =
              [&](const RenderStageInvocation& invocation,
                  const RenderSurfaceDescriptor& geometry_output,
                  std::optional<RenderSurface> input) {
                ++fused_working_callback_count;
                fused_working_received_characterized_input =
                    invocation.stage.kind == RenderStageKind::SceneAdjustments &&
                    input.has_value() && !input->pixels.empty();
                auto pixels = input.has_value()
                                  ? std::move(input->pixels)
                                  : std::vector<ColorPixel>{};
                return RenderStageExecution{
                    .output = RenderSurface{
                        .descriptor = geometry_output,
                        .pixels = std::move(pixels),
                        .identity = "surface:geometry:fused-working",
                    },
                    .backend = "fixture-working-fusion",
                    .fusion_evidence = RenderFusionEvidence{
                        .unfused_reference_passed = true,
                        .unfused_reference_identity = "fixture-working-stage-parity:v1",
                        .logical_stages_covered = {
                            RenderStageKind::SceneAdjustments,
                            RenderStageKind::Tone,
                            RenderStageKind::Look,
                            RenderStageKind::Layers,
                            RenderStageKind::Detail,
                            RenderStageKind::Geometry,
                        },
                    },
                };
              },
      });
  const auto fused_working_stage_count = std::ranges::count_if(
      fused_working_result.completed_stages,
      [](const RenderCompletedStage& completed) {
        return completed.backend == "fixture-working-fusion" &&
               completed.fusion_evidence.has_value();
      });
  if (!fused_working_result.ok() ||
      fused_working_callback_count != 1 ||
      !fused_working_received_characterized_input ||
      fused_working_stage_count != 6) {
    std::cerr << "working fusion status="
              << static_cast<int>(fused_working_result.status)
              << " callback=" << fused_working_callback_count
              << " characterized=" << fused_working_received_characterized_input
              << " stages=" << fused_working_stage_count
              << " failure=" << fused_working_result.failure_reason << '\n';
    return fail(
        "the canonical working-stage fusion must consume the characterized surface once and retain every logical working-stage diagnostic");
  }
  const auto repeated_full_result =
      execute_color_plan(
          &full_executor,
          full_request,
          [&](const RenderStageInvocation&
                  invocation,
              std::optional<RenderSurface>
                  input) {
            ++full_decode_calls;
            if (input.has_value()) {
              return RenderStageExecution{
                  .failure_reason =
                      "repeated full-plan Decode received input",
              };
            }
            return RenderStageExecution{
                .output =
                    RenderSurface{
                        .descriptor =
                            invocation
                                .expected_output,
                        .pixels =
                            {
                                ColorPixel{
                                    .red = 0.18F,
                                    .green = 0.18F,
                                    .blue = 0.18F,
                                    .alpha = 1.0F,
                                },
                                ColorPixel{
                                    .red = 0.72F,
                                    .green = 0.44F,
                                    .blue = 0.21F,
                                    .alpha = 1.0F,
                                },
                            },
                        .identity =
                            "surface:decode:single",
                    },
                .backend =
                    "fixture-decoder",
            };
          },
          full_state,
          ColorPlanExecutionOptions{
              .removal_quality =
                  RemovalQuality::Final,
              .photographic_output_config =
                  &full_state.color_pipeline,
              .gamut_warning_target =
                  GamutWarningTarget::
                      ExportOutput,
              .output_lut_quality =
                  Aces2OutputLutQuality::Final,
              .proof_transform =
                  &proof_transform,
              .proof_gamut_warning_transform =
                  &gamut_transform,
              .device_transform =
                  &device_transform,
              .publish_display_target =
                  &full_target,
          });
  if (!repeated_full_result.ok() ||
      full_decode_calls != 1 ||
      !repeated_full_result.completed_stages
           .front()
           .cache_hit ||
      !repeated_full_result.completed_stages
           .at(1)
           .cache_hit) {
    return fail(
        "the unified plan should reuse Decode and NormalizeRaw without invoking the decoder again");
  }
  auto publication_suffix_request =
      full_request;
  publication_suffix_request.plan.plan_cache_key =
      "plan:publication-suffix";
  auto geometry_surface =
      RenderSurface{
          .descriptor =
              render_stage_output_descriptor(
                  publication_suffix_request,
                  RenderStageKind::Geometry),
          .pixels =
              {
                  ColorPixel{
                      .red = 0.18F,
                      .green = 0.18F,
                      .blue = 0.18F,
                      .alpha = 1.0F,
                  },
                  ColorPixel{
                      .red = 0.72F,
                      .green = 0.44F,
                      .blue = 0.21F,
                      .alpha = 1.0F,
                  },
              },
          .identity =
              "surface:geometry:suffix",
      };
  const auto suffix_geometry_reference = geometry_surface;
  const auto suffix_result = execute_color_plan(
      &full_executor,
      publication_suffix_request,
      {},
      full_state,
      ColorPlanExecutionOptions{
          .start_stage =
              RenderStageKind::
                  PhotographicOutput,
          .initial_surface =
              &geometry_surface,
          .removal_quality =
              RemovalQuality::Final,
          .photographic_output_config =
              &full_state.color_pipeline,
          .gamut_warning_target =
              GamutWarningTarget::PreviewDisplay,
          .output_lut_quality =
              Aces2OutputLutQuality::Final,
          .proof_transform =
              &proof_transform,
          .proof_gamut_warning_transform =
              &gamut_transform,
          .device_transform =
              &device_transform,
          .publish_display_target =
              &full_target,
      });
  if (!suffix_result.ok() ||
      !suffix_result.output.has_value() ||
      suffix_result.completed_stages.size() != 4U ||
      suffix_result.completed_stages.front().kind !=
          RenderStageKind::PhotographicOutput ||
      suffix_result.completed_stages.back().kind !=
          RenderStageKind::Publish ||
      suffix_result.completed_stages.back().backend !=
          "device-encoded-identity" ||
      suffix_result.output->gamut_warning_mask.size() !=
          suffix_result.output->pixels.size() ||
      !geometry_surface.pixels.empty()) {
    return fail(
        "an adjusted-working cache hit should resume the publication suffix in one executor submission");
  }

  auto accelerated_geometry = suffix_geometry_reference;
  auto accelerated_request = publication_suffix_request;
  auto observed_cpu_surfaces = 0U;
  accelerated_request.stage_observer = [&](const auto&, const auto& surface) {
    if (surface.pixels.size() == 2U) ++observed_cpu_surfaces;
  };
  ColorAdjustmentTelemetry accelerated_telemetry;
  const auto accelerated_result = execute_color_plan(
      &full_executor, accelerated_request, {}, full_state,
      ColorPlanExecutionOptions{
          .start_stage = RenderStageKind::PhotographicOutput,
          .initial_surface = &accelerated_geometry,
          .removal_quality = RemovalQuality::Final,
          .photographic_output_config = &full_state.color_pipeline,
          .gamut_warning_target = GamutWarningTarget::PreviewDisplay,
          .output_lut_quality = Aces2OutputLutQuality::Final,
          .proof_transform = &proof_transform,
          .proof_gamut_warning_transform = &gamut_transform,
          .device_transform = &device_transform,
          .publish_display_target = &full_target,
          .allow_metal_photographic_output = true,
      }, {}, &accelerated_telemetry);
  if (!accelerated_result.ok() || !accelerated_result.output.has_value() ||
      observed_cpu_surfaces != 4U ||
      accelerated_result.completed_stages.front().cache_hit ||
      accelerated_result.completed_stages.front().cache_key ==
          suffix_result.completed_stages.front().cache_key ||
      accelerated_result.output->gamut_warning_mask != suffix_result.output->gamut_warning_mask) {
    return fail("optional Metal output must preserve CPU scope/proof surfaces and separate reference cache entries");
  }
  for (std::size_t index = 0; index < suffix_result.output->pixels.size(); ++index) {
    const auto& cpu = suffix_result.output->pixels[index];
    const auto& accelerated = accelerated_result.output->pixels[index];
    if (std::abs(cpu.red - accelerated.red) > 2.0e-5F ||
        std::abs(cpu.green - accelerated.green) > 2.0e-5F ||
        std::abs(cpu.blue - accelerated.blue) > 2.0e-5F || cpu.alpha != accelerated.alpha) {
      return fail("optional Metal output must match the CPU reference through proof and device publication");
    }
  }
#if defined(PHOTO_EDITOR_HAS_METAL_BACKEND) && PHOTO_EDITOR_HAS_METAL_BACKEND
  if (metal::shared_metal_pipeline_backend().available() &&
      (accelerated_result.completed_stages.front().backend != "metal-photographic-output" ||
       accelerated_telemetry.gpu_kernel_dispatch_count != 1)) {
    return fail("available Metal output must execute and report its dispatch rather than silently fall back");
  }
#endif

  auto fused_suffix_request = publication_suffix_request;
  fused_suffix_request.plan.plan_cache_key = "plan:fused-publication-suffix";
  std::erase_if(
      fused_suffix_request.plan.stages,
      [](const RenderStagePlan& stage) {
        return stage.kind == RenderStageKind::Proof ||
               stage.kind == RenderStageKind::DeviceTransform;
      });
  fused_suffix_request.bypass_cache = true;
  const auto fused_geometry_descriptor = render_stage_output_descriptor(
      fused_suffix_request, RenderStageKind::Geometry);
  const auto fused_input_pixels = std::vector<ColorPixel>{
      ColorPixel{.red = -0.1F, .green = 0.18F, .blue = 1.4F, .alpha = 1.0F},
      ColorPixel{.red = 0.72F, .green = 0.44F, .blue = 0.21F, .alpha = 0.5F},
  };
  const auto fused_input_backing = std::make_shared<FakeRenderSurfaceBacking>(
      fused_geometry_descriptor, fused_input_pixels);
  auto fused_geometry_surface = RenderSurface{
      .descriptor = fused_geometry_descriptor,
      .backing = fused_input_backing,
      .identity = "surface:geometry:fused-native",
  };
  auto fused_callback_count = 0;
  auto fused_callback_received_native_input = false;
  const auto fused_suffix_result = execute_color_plan(
      &full_executor,
      fused_suffix_request,
      {},
      full_state,
      ColorPlanExecutionOptions{
          .start_stage = RenderStageKind::PhotographicOutput,
          .initial_surface = &fused_geometry_surface,
          .removal_quality = RemovalQuality::Final,
          .photographic_output_config = &full_state.color_pipeline,
          .gamut_warning_target = GamutWarningTarget::PreviewDisplay,
          .output_lut_quality = Aces2OutputLutQuality::Final,
          .publish_display_target = &full_target,
          .photographic_output_publish_fusion =
              [&](const RenderStageInvocation& invocation,
                  const RenderSurfaceDescriptor& publish_output,
                  std::optional<RenderSurface> input) {
                ++fused_callback_count;
                fused_callback_received_native_input =
                    invocation.stage.kind == RenderStageKind::PhotographicOutput &&
                    input.has_value() && input->pixels.empty() &&
                    input->backing == fused_input_backing;
                return RenderStageExecution{
                    .output = RenderSurface{
                        .descriptor = publish_output,
                        .pixels = fused_input_pixels,
                        .identity = "surface:publish:fused-native",
                    },
                    .backend = "fixture-native-output-fusion",
                    .fusion_evidence = RenderFusionEvidence{
                        .unfused_reference_passed = true,
                        .unfused_reference_identity = "fixture-output-parity:v1",
                        .logical_stages_covered = {
                            RenderStageKind::PhotographicOutput,
                            RenderStageKind::Publish,
                        },
                    },
                };
              },
      });
  if (!fused_suffix_result.ok() || !fused_suffix_result.output.has_value() ||
      fused_callback_count != 1 || !fused_callback_received_native_input ||
      fused_input_backing->copy_count() != 0 ||
      fused_suffix_result.completed_stages.size() != 2U ||
      fused_suffix_result.completed_stages.front().kind !=
          RenderStageKind::PhotographicOutput ||
      fused_suffix_result.completed_stages.back().kind != RenderStageKind::Publish ||
      fused_suffix_result.completed_stages.front().backend !=
          "fixture-native-output-fusion" ||
      !fused_suffix_result.completed_stages.front().fusion_evidence.has_value() ||
      fused_suffix_result.output->descriptor !=
          render_stage_output_descriptor(fused_suffix_request, RenderStageKind::Publish)) {
    return fail(
        "the canonical PhotographicOutput-Publish fusion must consume retained input once without materialization and report both logical stages");
  }

  auto fused_device_request = publication_suffix_request;
  fused_device_request.plan.plan_cache_key = "plan:fused-device-publication-suffix";
  std::erase_if(
      fused_device_request.plan.stages,
      [](const RenderStagePlan& stage) {
        return stage.kind == RenderStageKind::Proof;
      });
  fused_device_request.bypass_cache = true;
  const auto fused_device_geometry_descriptor = render_stage_output_descriptor(
      fused_device_request, RenderStageKind::Geometry);
  const auto fused_device_input_backing =
      std::make_shared<FakeRenderSurfaceBacking>(
          fused_device_geometry_descriptor, fused_input_pixels);
  auto fused_device_geometry_surface = RenderSurface{
      .descriptor = fused_device_geometry_descriptor,
      .backing = fused_device_input_backing,
      .identity = "surface:geometry:fused-device-native",
  };
  auto fused_device_callback_count = 0;
  const auto fused_device_result = execute_color_plan(
      &full_executor,
      fused_device_request,
      {},
      full_state,
      ColorPlanExecutionOptions{
          .start_stage = RenderStageKind::PhotographicOutput,
          .initial_surface = &fused_device_geometry_surface,
          .removal_quality = RemovalQuality::Final,
          .photographic_output_config = &full_state.color_pipeline,
          .gamut_warning_target = GamutWarningTarget::PreviewDisplay,
          .output_lut_quality = Aces2OutputLutQuality::Final,
          .device_transform = &device_transform,
          .publish_display_target = &full_target,
          .photographic_output_device_publish_fusion =
              [&](const RenderStageInvocation& invocation,
                  const RenderSurfaceDescriptor& publish_output,
                  std::optional<RenderSurface> input) {
                ++fused_device_callback_count;
                if (invocation.stage.kind != RenderStageKind::PhotographicOutput ||
                    !input.has_value() || !input->pixels.empty() ||
                    input->backing != fused_device_input_backing) {
                  return RenderStageExecution{
                      .failure_reason = "fixture retained device input mismatch",
                  };
                }
                return RenderStageExecution{
                    .output = RenderSurface{
                        .descriptor = publish_output,
                        .pixels = fused_input_pixels,
                        .identity = "surface:publish:fused-device-native",
                    },
                    .backend = "fixture-native-device-output-fusion",
                    .fusion_evidence = RenderFusionEvidence{
                        .unfused_reference_passed = true,
                        .unfused_reference_identity = "fixture-device-output-parity:v1",
                        .logical_stages_covered = {
                            RenderStageKind::PhotographicOutput,
                            RenderStageKind::DeviceTransform,
                            RenderStageKind::Publish,
                        },
                    },
                };
              },
      });
  if (!fused_device_result.ok() || !fused_device_result.output.has_value() ||
      fused_device_callback_count != 1 ||
      fused_device_input_backing->copy_count() != 0 ||
      fused_device_result.completed_stages.size() != 3U ||
      fused_device_result.completed_stages.at(0).kind !=
          RenderStageKind::PhotographicOutput ||
      fused_device_result.completed_stages.at(1).kind !=
          RenderStageKind::DeviceTransform ||
      fused_device_result.completed_stages.at(2).kind != RenderStageKind::Publish ||
      !std::ranges::all_of(
          fused_device_result.completed_stages,
          [](const RenderCompletedStage& completed) {
            return completed.backend == "fixture-native-device-output-fusion" &&
                   completed.fusion_evidence.has_value() && !completed.cache_hit;
          })) {
    return fail(
        "the canonical PhotographicOutput-DeviceTransform-Publish fusion must retain input and report all three logical stages");
  }

  executor.clear_cache();
  if (executor.cache_entries() != 0 || executor.cache_bytes() != 0) {
    return fail("executor cache should clear deterministically");
  }

  return EXIT_SUCCESS;
}
