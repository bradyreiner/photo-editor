#include "edit_recipe_pipeline_bridge.h"
#include "system_monitor_profile_asset_bridge.h"

#include <QFile>
#include <QRegularExpression>

#include <cstdlib>
#include <cmath>
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

QString executable_source(QString source) {
  // Ignore mentions in comments and literals when locating owners/call sites.
  static const QRegularExpression non_code(QStringLiteral(
      R"lex("(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*'|//[^\n]*|/\*.*?\*/)lex"),
      QRegularExpression::DotMatchesEverythingOption);
  auto matches = non_code.globalMatch(source);
  while (matches.hasNext()) {
    const auto match = matches.next();
    source.replace(match.capturedStart(), match.capturedLength(),
                   QString(match.capturedLength(), QLatin1Char(' ')));
  }
  return source;
}

QString function_body(const QString& source, const QString& name) {
  const auto code = executable_source(source);
  const QRegularExpression signature(QStringLiteral("\\b%1\\s*\\(")
      .arg(QRegularExpression::escape(name)));
  auto signatures = signature.globalMatch(code);
  while (signatures.hasNext()) {
    const auto match = signatures.next();
    auto cursor = match.capturedEnd() - 1;
    auto depth = 0;
    for (; cursor < code.size(); ++cursor) {
      if (code[cursor] == QLatin1Char('(')) ++depth;
      if (code[cursor] == QLatin1Char(')') && --depth == 0) {
        ++cursor;
        break;
      }
    }
    while (cursor < code.size() && code[cursor].isSpace()) ++cursor;
    // Calls and forward declarations do not own the implementation.
    if (cursor == code.size() || code[cursor] != QLatin1Char('{')) continue;
    const auto begin = ++cursor;
    depth = 1;
    for (; cursor < code.size(); ++cursor) {
      if (code[cursor] == QLatin1Char('{')) ++depth;
      if (code[cursor] == QLatin1Char('}') && --depth == 0) {
        auto body = code.mid(begin, cursor - begin);
        body.remove(QRegularExpression(QStringLiteral("\\s+")));
        return body;
      }
    }
  }
  return {};
}

bool owns_shared_pipeline(const QString& preview, const QString& output,
                         const QString& compiler, const QString& plan_store) {
  const auto preview_compiler = function_body(preview, "compile_preview_pipeline_state");
  const auto output_compiler = function_body(output, "compile_export_pipeline_state");
  const auto shared_compiler = function_body(compiler, "compile_desktop_pipeline_state");
  const auto preview_renderer = function_body(preview, "render_preview_request");
  const auto export_renderer = function_body(output, "render_flattened_output_file");
  const auto admission = function_body(plan_store, "RenderPlanStore::admit");
  return preview_compiler.contains("returncompile_desktop_pipeline_state(") &&
      preview_compiler.contains("DesktopRenderPurpose::Preview") &&
      output_compiler.contains("returncompile_desktop_pipeline_state(") &&
      output_compiler.contains("DesktopRenderPurpose::Output") &&
      shared_compiler.count("compile_base_color_adjustment_state(") == 1 &&
      !executable_source(preview).contains("compile_base_color_adjustment_state(") &&
      !executable_source(output).contains("compile_base_color_adjustment_state(") &&
      function_body(preview, "compile_desktop_pipeline_state").isEmpty() &&
      function_body(output, "compile_desktop_pipeline_state").isEmpty() &&
      preview_renderer.contains("RenderPlanStore::shared().admit(") &&
      preview_renderer.contains("if(canonical_plan_required&&!admission.admitted)") &&
      export_renderer.contains("RenderPlanStore::shared().admit(") &&
      export_renderer.contains("if(canonical_required&&!admission.admitted)") &&
      export_renderer.contains("admission.admitted?&*admission.plan:nullptr") &&
      admission.contains("compile_desktop_render_plan_with_derived_assets(") &&
      admission.contains("if(!compiled.ok())");
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

  auto raw_state = state;
  raw_state.raw_exposure = 1.9;
  auto raw_metadata = photo_editor::pipeline::DecodedImageMetadata{};
  raw_metadata.valid = true;
  raw_metadata.is_raw = true;
  raw_metadata.pixel_encoding =
      photo_editor::pipeline::DecodedPixelEncoding::CameraLinearRgb;
  raw_metadata.decoder_backend = "LibRaw camera-linear fixture";
  raw_metadata.decoded_color_space_name = "camera RGB";
  raw_metadata.demosaic_applied = true;
  const auto raw_adjustment =
      photo_editor::desktop::compile_base_color_adjustment_state(raw_state, &raw_metadata);
  if (std::abs(raw_adjustment.input_transform.exposure - 3.15) > 1.0e-12 ||
      photo_editor::desktop::compile_base_color_adjustment_state(raw_state)
              .input_transform.exposure != state.exposure) {
    return fail("deferred RAW exposure must add to Light exposure only for engine-owned RAW");
  }
  raw_state.raw_input_transform_enabled = false;
  if (std::abs(photo_editor::desktop::compile_base_color_adjustment_state(raw_state, &raw_metadata)
          .input_transform.exposure - 3.15) > 1.0e-12) {
    return fail("RAW exposure must remain float-engine-owned with input characterization disabled");
  }
  auto raster_metadata = raw_metadata;
  raster_metadata.is_raw = false;
  if (photo_editor::desktop::compile_base_color_adjustment_state(raw_state, &raster_metadata)
          .input_transform.exposure != state.exposure) {
    return fail("RAW exposure must not affect a decoded non-RAW image");
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
      !owns_shared_pipeline(preview_source, export_source, compiler_source,
                            render_plan_store_source) ||
      preview_source.contains(
          QStringLiteral("compile_base_color_adjustment_state(")) ||
      export_source.contains(
          QStringLiteral("compile_base_color_adjustment_state(")) ||
      !preview_source.contains(
          QStringLiteral("RenderPlanStore::shared().admit(")) ||
      !job_runner_source.contains(
          QStringLiteral("RenderPlanStore::shared().admit(")) ||
      !export_source.contains(
          QStringLiteral("RenderPlanStore::shared().admit(")) ||
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

  // More consumers may reuse the sole compiler. Guard ownership and admission,
  // rather than freezing the number of legitimate preview/export call sites.
  const auto extra_consumer = preview_source + QStringLiteral(
      "\nvoid another_preview_consumer() { compile_desktop_pipeline_state(); }\n");
  auto private_preview = preview_source;
  private_preview.replace("compile_desktop_pipeline_state(", "compile_private_pipeline_state(");
  auto bypassed_admission = export_source;
  bypassed_admission.replace("RenderPlanStore::shared().admit(", "compile_export_plan_directly(");
  const auto duplicated_owner = preview_source + QStringLiteral(
      "\nvoid duplicate_pipeline_owner() { compile_base_color_adjustment_state(); }\n");
  if (!owns_shared_pipeline(extra_consumer, export_source, compiler_source, render_plan_store_source) ||
      owns_shared_pipeline(private_preview, export_source, compiler_source, render_plan_store_source) ||
      owns_shared_pipeline(preview_source, bypassed_admission, compiler_source, render_plan_store_source) ||
      owns_shared_pipeline(duplicated_owner, export_source, compiler_source, render_plan_store_source)) {
    return fail("pipeline ownership guard must accept shared reuse and reject private compilers/admission bypasses");
  }

  return EXIT_SUCCESS;
}
