#include "render_plan_store.h"
#include "edit_recipe_pipeline_bridge.h"
#include "photo_editor/pipeline/render_executor.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <vector>

#include <QCoreApplication>
#include <QString>

namespace {

int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication application(argc, argv);

  photo_editor::desktop::EditRecipeState state;
  state.working_color_profile = QStringLiteral("Linear sRGB");
  state.preview_color_space = QStringLiteral("sRGB");
  state.output_color_space = QStringLiteral("sRGB");
  state.exposure = 0.375;

  auto& store = photo_editor::desktop::RenderPlanStore::shared();
  const auto missing_identity = store.admit(
      {},
      QStringLiteral("render-plan-store-fixture.png"),
      state,
      QSize(64, 48),
      photo_editor::pipeline::RenderPurpose::SettledPreview,
      photo_editor::pipeline::DisplayRenderingTarget::SrgbSimulation);
  if (missing_identity.admitted ||
      missing_identity.error !=
          QStringLiteral("canonical plan requires a stable source asset id")) {
    return fail("RenderPlanStore must fail closed without a source identity");
  }

  const auto missing_dimensions = store.admit(
      QStringLiteral("render-plan-store:invalid-size"),
      QStringLiteral("render-plan-store-fixture.png"),
      state,
      {},
      photo_editor::pipeline::RenderPurpose::SettledPreview,
      photo_editor::pipeline::DisplayRenderingTarget::SrgbSimulation);
  if (missing_dimensions.admitted ||
      missing_dimensions.error !=
          QStringLiteral("canonical plan requires non-zero source dimensions")) {
    return fail("RenderPlanStore must fail closed without source dimensions");
  }

  const auto first = store.admit(
      QStringLiteral("render-plan-store:cache-proof"),
      QStringLiteral("render-plan-store-fixture.png"),
      state,
      QSize(64, 48),
      photo_editor::pipeline::RenderPurpose::SettledPreview,
      photo_editor::pipeline::DisplayRenderingTarget::SrgbSimulation);
  if (!first.admitted || !first.plan.has_value() || first.cache_hit ||
      first.plan_cache_key.isEmpty() || first.recipe_cache_key.isEmpty() ||
      first.serialized_plan.isEmpty() || first.cache_resident_entries <= 0) {
    std::cerr << "first canonical plan admission failed: "
              << first.error.toStdString() << '\n';
    return EXIT_FAILURE;
  }

  const auto second = store.admit(
      QStringLiteral("render-plan-store:cache-proof"),
      QStringLiteral("render-plan-store-fixture.png"),
      state,
      QSize(64, 48),
      photo_editor::pipeline::RenderPurpose::SettledPreview,
      photo_editor::pipeline::DisplayRenderingTarget::SrgbSimulation);
  if (!second.admitted || !second.cache_hit ||
      second.plan_cache_key != first.plan_cache_key ||
      second.recipe_cache_key != first.recipe_cache_key ||
      second.serialized_plan != first.serialized_plan ||
      second.cache_build_count != first.cache_build_count ||
      second.cache_hit_count <= first.cache_hit_count) {
    return fail("identical admissions must reuse one byte-identical cached plan");
  }

  const auto changed_target = store.admit(
      QStringLiteral("render-plan-store:cache-proof"),
      QStringLiteral("render-plan-store-fixture.png"),
      state,
      QSize(64, 48),
      photo_editor::pipeline::RenderPurpose::SettledPreview,
      photo_editor::pipeline::DisplayRenderingTarget::DisplayP3Simulation);
  if (!changed_target.admitted || changed_target.cache_hit ||
      changed_target.plan_cache_key == first.plan_cache_key ||
      changed_target.cache_build_count != first.cache_build_count + 1) {
    return fail("render target identity must produce a distinct canonical plan");
  }

  state.photographic_rendering_model = QStringLiteral("aces2-ocio-v1");
  state.render_intent = QStringLiteral("Perceptual");
  state.highlights = -28.0;
  state.shadows = 19.0;
  state.vibrance = 14.0;
  state.master_curve_points = {
      QVariantMap{{QStringLiteral("x"), 0.0}, {QStringLiteral("y"), 0.0}},
      QVariantMap{{QStringLiteral("x"), 0.45}, {QStringLiteral("y"), 0.52}},
      QVariantMap{{QStringLiteral("x"), 1.0}, {QStringLiteral("y"), 1.0}},
  };
  constexpr auto kParityWidth = 4;
  constexpr auto kParityHeight = 1;
  const auto raw_metadata = photo_editor::pipeline::DecodedImageMetadata{
      .valid = true,
      .is_raw = true,
      .pixel_encoding = photo_editor::pipeline::DecodedPixelEncoding::SceneLinearRgb,
      .decoder_backend = "fixture-raw-scene-linear-v1",
      .decoded_color_space_name = "Linear sRGB",
      .camera_profile_identity = "fixture-characterized-linear-srgb",
      .demosaic_applied = true,
      .white_balance_applied = true,
      .camera_transform_applied = true,
  };
  const auto preview = store.admit(
      QStringLiteral("render-plan-store:preview-export-identity"),
      QStringLiteral("render-plan-store-fixture.dng"),
      state,
      QSize(kParityWidth, kParityHeight),
      photo_editor::pipeline::RenderPurpose::SettledPreview,
      photo_editor::pipeline::DisplayRenderingTarget::SrgbSimulation,
      std::nullopt,
      &raw_metadata);
  const auto export_render = store.admit(
      QStringLiteral("render-plan-store:preview-export-identity"),
      QStringLiteral("render-plan-store-fixture.dng"),
      state,
      QSize(kParityWidth, kParityHeight),
      photo_editor::pipeline::RenderPurpose::Export,
      photo_editor::pipeline::DisplayRenderingTarget::SrgbSimulation,
      std::nullopt,
      &raw_metadata);
  if (!preview.admitted || !preview.plan.has_value() ||
      !export_render.admitted || !export_render.plan.has_value() ||
      preview.recipe_cache_key != export_render.recipe_cache_key ||
      preview.plan_cache_key == export_render.plan_cache_key) {
    return fail(
        "settled preview and export must share one recipe while retaining distinct publication plans");
  }
  const auto preview_geometry = std::ranges::find_if(
      preview.plan->stages,
      [](const auto& stage) {
        return stage.kind == photo_editor::pipeline::RenderStageKind::Geometry;
      });
  const auto export_geometry = std::ranges::find_if(
      export_render.plan->stages,
      [](const auto& stage) {
        return stage.kind == photo_editor::pipeline::RenderStageKind::Geometry;
      });
  if (preview_geometry == preview.plan->stages.end() ||
      export_geometry == export_render.plan->stages.end()) {
    return fail("canonical preview/export plans must contain Geometry");
  }
  const auto preview_prefix_size = static_cast<std::size_t>(
      std::distance(preview.plan->stages.begin(), preview_geometry)) + 1U;
  const auto export_prefix_size = static_cast<std::size_t>(
      std::distance(export_render.plan->stages.begin(), export_geometry)) + 1U;
  if (preview_prefix_size != export_prefix_size ||
      !std::equal(
          preview.plan->stages.begin(),
          std::next(preview_geometry),
          export_render.plan->stages.begin())) {
    return fail(
        "settled preview and export must compile byte-identical Decode-through-Geometry stages");
  }

  const auto source_pixels = std::vector<photo_editor::pipeline::ColorPixel>{
      {.red = -0.08F, .green = 0.02F, .blue = 0.18F, .alpha = 0.25F},
      {.red = 0.04F, .green = 0.18F, .blue = 0.72F, .alpha = 0.5F},
      {.red = 1.4F, .green = 0.64F, .blue = 0.21F, .alpha = 0.75F},
      {.red = 8.0F, .green = 2.0F, .blue = -0.12F, .alpha = 1.0F},
  };
  const auto pipeline_state =
      photo_editor::desktop::compile_base_color_adjustment_state(
          state, &raw_metadata);
  const auto execute_pre_output =
      [&](const photo_editor::pipeline::RenderPlan& plan) {
        auto executor = photo_editor::pipeline::RenderExecutor{1024U * 1024U};
        return photo_editor::pipeline::execute_color_plan(
            &executor,
            photo_editor::pipeline::RenderExecutionRequest{
                .plan = plan,
                .backend_version = "cpu-reference:gate2",
                .target_identity = "pre-output:gate2",
                .profile_identity = "builtin:srgb",
                .source_color_space_identity = "linear-srgb",
                .working_color_space_identity = "Linear sRGB",
                .rendering_target_color_space_identity = "sRGB",
                .device_color_space_identity = "sRGB",
                .device_transfer_identity = "sRGB",
                .output_width = kParityWidth,
                .output_height = kParityHeight,
                .generation = 1U,
            },
            [&](const photo_editor::pipeline::RenderStageInvocation& invocation,
                std::optional<photo_editor::pipeline::RenderSurface> input) {
              if (input.has_value()) {
                return photo_editor::pipeline::RenderStageExecution{
                    .failure_reason = "Decode received an unexpected input surface",
                };
              }
              return photo_editor::pipeline::RenderStageExecution{
                  .output = photo_editor::pipeline::RenderSurface{
                      .descriptor = invocation.expected_output,
                      .pixels = source_pixels,
                      .identity = "surface:preview-export-gate2",
                  },
                  .backend = "fixture-decoder",
              };
            },
            pipeline_state,
            photo_editor::pipeline::ColorPlanExecutionOptions{
                .removal_quality = photo_editor::pipeline::RemovalQuality::Final,
                .stop_before_photographic_output = true,
            });
      };
  const auto preview_result = execute_pre_output(*preview.plan);
  const auto export_result = execute_pre_output(*export_render.plan);
  const auto pixels_match_exactly =
      preview_result.output.has_value() && export_result.output.has_value() &&
      preview_result.output->pixels.size() == export_result.output->pixels.size() &&
      std::equal(
          preview_result.output->pixels.begin(),
          preview_result.output->pixels.end(),
          export_result.output->pixels.begin(),
          export_result.output->pixels.end(),
          [](const auto& left, const auto& right) {
            return left.red == right.red && left.green == right.green &&
                   left.blue == right.blue && left.alpha == right.alpha;
          });
  if (!preview_result.ok() || !preview_result.output.has_value() ||
      !export_result.ok() || !export_result.output.has_value() ||
      preview_result.output->descriptor != export_result.output->descriptor ||
      !pixels_match_exactly ||
      preview_result.completed_stages.size() != export_result.completed_stages.size()) {
    std::cerr << "preview_status=" << static_cast<int>(preview_result.status)
              << " preview_failure=" << preview_result.failure_reason
              << " export_status=" << static_cast<int>(export_result.status)
              << " export_failure=" << export_result.failure_reason
              << " preview_stages=" << preview_result.completed_stages.size()
              << " export_stages=" << export_result.completed_stages.size()
              << " pixels_exact=" << pixels_match_exactly << '\n';
    return fail(
        "settled preview and export must produce identical pre-output pixels from their admitted plans");
  }
  for (std::size_t index = 0; index < preview_result.completed_stages.size(); ++index) {
    if (preview_result.completed_stages[index].kind !=
            export_result.completed_stages[index].kind ||
        preview_result.completed_stages[index].backend !=
            export_result.completed_stages[index].backend) {
      return fail(
          "settled preview and export must execute the same pre-output stage order and backends");
    }
  }

  return EXIT_SUCCESS;
}
