#include "edit_recipe_pipeline_bridge.h"
#include "system_monitor_profile_asset_bridge.h"

#include <QFile>

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

int fail(const std::string& message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

QString read_source(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return {};
  }
  return QString::fromUtf8(file.readAll());
}

}  // namespace

int main() {
  photo_editor::desktop::EditRecipeState state;
  state.input_color_profile = QStringLiteral("Embedded");
  state.working_color_profile = QStringLiteral("Linear ProPhoto RGB");
  state.preview_color_space = QStringLiteral("Display P3");
  state.output_color_space = QStringLiteral("Adobe RGB");
  state.soft_proof_profile = QStringLiteral("sRGB");
  state.photographic_rendering_model =
      QStringLiteral("aces2-ocio-v1");
  state.render_intent = QStringLiteral("Perceptual");
  state.proof_render_intent =
      QStringLiteral("Relative Colorimetric");
  state.output_headroom = 2.0;
  state.output_black_point = 0.002;
  state.exposure = 1.25;
  state.temperature = 8.0;
  state.tint = -3.0;
  state.highlights = -18.0;
  state.shadows = 22.0;
  state.vibrance = 14.0;
  state.grading_balance = -12.0;
  state.master_curve_points = {
      QVariantMap{{QStringLiteral("x"), 0.0},
                  {QStringLiteral("y"), 0.1}},
      QVariantMap{{QStringLiteral("x"), 1.0},
                  {QStringLiteral("y"), 0.9}},
  };

  const auto first =
      photo_editor::desktop::compile_color_pipeline_config(state);
  const auto second =
      photo_editor::desktop::compile_color_pipeline_config(state);
  if (first.input_profile.profile_name !=
          second.input_profile.profile_name ||
      first.input_profile.kind != second.input_profile.kind ||
      first.working_profile.profile_name !=
          second.working_profile.profile_name ||
      first.working_profile.kind != second.working_profile.kind ||
      first.preview_output_profile.profile_name !=
          second.preview_output_profile.profile_name ||
      first.preview_output_profile.kind !=
          second.preview_output_profile.kind ||
      first.export_output_profile.profile_name !=
          second.export_output_profile.profile_name ||
      first.export_output_profile.kind !=
          second.export_output_profile.kind ||
      first.proof_output_profile.profile_name !=
          second.proof_output_profile.profile_name ||
      first.proof_output_profile.kind !=
          second.proof_output_profile.kind ||
      first.photographic_rendering_model !=
          second.photographic_rendering_model ||
      first.output_headroom != 2.0 ||
      first.output_black_point != 0.002 ||
      photo_editor::desktop::preview_output_color_space_name_for_recipe(
          state) != QStringLiteral("Display P3")) {
    return fail("shared color pipeline compilation should be deterministic and target-complete");
  }

  const auto adjustment =
      photo_editor::desktop::compile_base_color_adjustment_state(
          state, nullptr, &first);
  if (adjustment.input_transform.exposure != 1.25 ||
      adjustment.input_transform.temperature != 8.0 ||
      adjustment.input_transform.tint != -3.0 ||
      adjustment.highlights != -18.0 ||
      adjustment.shadows != 22.0 ||
      adjustment.vibrance != 14.0 ||
      adjustment.grading_balance != -12.0 ||
      adjustment.master_curve_points.size() != 2U ||
      adjustment.master_curve_points.front().y != 0.1 ||
      adjustment.master_curve_points.back().y != 0.9 ||
      adjustment.color_pipeline.output_headroom != 2.0) {
    return fail("shared base adjustment compilation should preserve canonical desktop intent");
  }

  const auto monitor_color_space =
      QColorSpace(QColorSpace::SRgb);
  const auto monitor_snapshot =
      photo_editor::desktop::SystemMonitorProfileSnapshot{
          .color_space = monitor_color_space,
          .display_name = QStringLiteral("Test sRGB Monitor"),
          .identity = QStringLiteral("test-display|icc-sha256:stable"),
          .display_identity = QStringLiteral("test-display"),
          .discovery_backend = QStringLiteral("test"),
      };
  const auto monitor_asset =
      photo_editor::desktop::resolve_system_monitor_profile_asset(
          monitor_snapshot, 1024 * 1024);
  const auto repeated_monitor_asset =
      photo_editor::desktop::resolve_system_monitor_profile_asset(
          monitor_snapshot, 1024 * 1024);
  if (!monitor_asset.ready() || !repeated_monitor_asset.ready() ||
      monitor_asset.reference.asset_id !=
          repeated_monitor_asset.reference.asset_id ||
      monitor_asset.reference.payload_sha256 !=
          repeated_monitor_asset.reference.payload_sha256 ||
      monitor_asset.reference.source_kind != "system-monitor") {
    return fail(
        "system monitor ICC resolution should be immutable and cache-stable");
  }

  state.preview_color_space.clear();
  if (photo_editor::desktop::preview_output_color_space_name_for_recipe(
          state) != QStringLiteral("Adobe RGB")) {
    return fail("empty preview target should resolve through the shared output fallback");
  }
  state.preview_color_space = QStringLiteral("sRGB");
  state.monitor_color_profile = QStringLiteral("Display P3");
  if (photo_editor::desktop::preview_output_color_space_name_for_recipe(
          state) != QStringLiteral("Display P3")) {
    return fail(
        "a manual monitor profile should replace the intermediate preview target");
  }
  state.soft_proof_enabled = true;
  if (photo_editor::desktop::preview_output_color_space_name_for_recipe(
          state) != QStringLiteral("sRGB")) {
    return fail(
        "soft proofing should retain its declared preview target before the monitor transform");
  }

  const auto preview_source =
      read_source(QStringLiteral(PHOTO_EDITOR_PREVIEW_CONTROLLER_PATH));
  const auto export_source =
      read_source(QStringLiteral(PHOTO_EDITOR_PHOTO_RENDER_UTILS_PATH));
  const auto compiler_source =
      read_source(QStringLiteral(PHOTO_EDITOR_DESKTOP_PIPELINE_COMPILER_PATH));
  const auto render_plan_store_source =
      read_source(QStringLiteral(PHOTO_EDITOR_RENDER_PLAN_STORE_PATH));
  const auto job_runner_source =
      read_source(QStringLiteral(PHOTO_EDITOR_JOB_RUNNER_PATH));
  const auto catalog_source =
      read_source(QStringLiteral(
          PHOTO_EDITOR_CATALOG_ASSET_MODEL_PATH));
  const auto carousel_source =
      read_source(QStringLiteral(PHOTO_EDITOR_CAROUSEL_CONTROLLER_PATH));
  const auto carousel_canonical_render =
      carousel_source.indexOf(QStringLiteral(
          "RenderPlanStore::shared().admit("));
  const auto carousel_crop_after_render =
      carousel_source.indexOf(
          QStringLiteral("image->copy(carousel_crop_rect_for_image"),
          carousel_canonical_render);
  if (preview_source.isEmpty() || export_source.isEmpty() ||
      compiler_source.isEmpty() ||
      render_plan_store_source.isEmpty() ||
      job_runner_source.isEmpty() ||
      catalog_source.isEmpty() ||
      carousel_source.isEmpty() ||
      !preview_source.contains(
          QStringLiteral("#include \"desktop_pipeline_state_compiler.h\"")) ||
      !export_source.contains(
          QStringLiteral("#include \"desktop_pipeline_state_compiler.h\"")) ||
      !compiler_source.contains(
          QStringLiteral("#include \"edit_recipe_pipeline_bridge.h\"")) ||
      compiler_source.count(
          QStringLiteral("compile_base_color_adjustment_state(")) != 1 ||
      preview_source.count(
          QStringLiteral("compile_desktop_pipeline_state(")) != 1 ||
      export_source.count(
          QStringLiteral("compile_desktop_pipeline_state(")) != 1 ||
      preview_source.contains(
          QStringLiteral("compile_base_color_adjustment_state(")) ||
      export_source.contains(
          QStringLiteral("compile_base_color_adjustment_state(")) ||
      !preview_source.contains(
          QStringLiteral("RenderPlanStore::shared().admit(")) ||
      !job_runner_source.contains(
          QStringLiteral("RenderPlanStore::shared().admit(")) ||
      export_source.count(
          QStringLiteral("RenderPlanStore::shared().admit(")) != 1 ||
      render_plan_store_source.count(
          QStringLiteral("compile_desktop_render_plan_with_derived_assets(")) != 1 ||
      render_plan_store_source.contains(
          QStringLiteral("render_output_image(")) ||
      !export_source.contains(
          QStringLiteral("QImage render_flattened_output_file(")) ||
      carousel_source.count(
          QStringLiteral("RenderPlanStore::shared().admit(")) != 1 ||
      preview_source.contains(
          QStringLiteral("compile_desktop_render_plan_with_derived_assets(")) ||
      job_runner_source.contains(
          QStringLiteral("compile_desktop_render_plan_with_derived_assets(")) ||
      !carousel_source.contains(
          QStringLiteral("#include \"photo_render_utils.h\"")) ||
      catalog_source.count(
          QStringLiteral(
              "RenderPlanStore::shared().admit(")) != 1 ||
      !catalog_source.contains(
          QStringLiteral("RenderPurpose::Thumbnail")) ||
      !catalog_source.contains(
          QStringLiteral("render_output_image(")) ||
      !catalog_source.contains(
          QStringLiteral("&*admission.plan")) ||
      !carousel_source.contains(QStringLiteral("decode_source_image(")) ||
      !carousel_source.contains(QStringLiteral("render_output_image(")) ||
      !carousel_source.contains(QStringLiteral(
          "decode_state.raw_input_transform_enabled = false;")) ||
      !carousel_source.contains(
          QStringLiteral("apply_output_color_space_transform(")) ||
      !carousel_source.contains(QStringLiteral(
          "&*admission.plan))")) ||
      carousel_canonical_render < 0 || carousel_crop_after_render < 0 ||
      carousel_source.contains(
          QStringLiteral("compile_carousel_pipeline_state(")) ||
      carousel_source.contains(
          QStringLiteral("compile_base_color_adjustment_state(")) ||
      preview_source.contains(
          QStringLiteral("color_pipeline_config_for_state(")) ||
      export_source.contains(
          QStringLiteral("color_pipeline_config_for_state(")) ||
      preview_source.contains(QStringLiteral("to_pipeline_state(")) ||
      export_source.contains(QStringLiteral("to_pipeline_state(")) ||
      carousel_source.contains(QStringLiteral("to_pipeline_state("))) {
    return fail(
        "preview, export, and carousel must use the sole shared desktop pipeline compiler");
  }

  return EXIT_SUCCESS;
}
