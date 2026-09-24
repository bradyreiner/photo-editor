#include "photo_editor/pipeline/color_pipeline.h"
#include "photo_editor/pipeline/ocio_project_config.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <thread>
#include <vector>

#ifndef PHOTO_EDITOR_OCIO_PROJECT_FIXTURE_DIR
#error "PHOTO_EDITOR_OCIO_PROJECT_FIXTURE_DIR must identify the OCIO fixture"
#endif

#ifndef PHOTO_EDITOR_OCIO_PROJECT_CONFIG_SHA256
#error "PHOTO_EDITOR_OCIO_PROJECT_CONFIG_SHA256 must identify the OCIO config"
#endif

#ifndef PHOTO_EDITOR_OCIO_PROJECT_CLF_SHA256
#error "PHOTO_EDITOR_OCIO_PROJECT_CLF_SHA256 must identify the CLF fixture"
#endif

#ifndef PHOTO_EDITOR_OCIO_PROJECT_CTF_SHA256
#error "PHOTO_EDITOR_OCIO_PROJECT_CTF_SHA256 must identify the CTF fixture"
#endif

namespace {

using photo_editor::pipeline::OcioProjectProcessorKind;
using photo_editor::pipeline::OcioProjectProcessorRequest;
using photo_editor::pipeline::OcioProjectRgbaFloat;
using photo_editor::pipeline::OcioProjectStatus;

[[nodiscard]] bool close(float left, float right, float tolerance = 2.0e-6F) {
  return std::abs(left - right) <= tolerance;
}

[[nodiscard]] bool pixel_matches(
    const OcioProjectRgbaFloat& pixel,
    std::array<float, 4> expected,
    float tolerance = 2.0e-6F) {
  return close(pixel.red, expected[0], tolerance) &&
         close(pixel.green, expected[1], tolerance) &&
         close(pixel.blue, expected[2], tolerance) &&
         pixel.alpha == expected[3];
}

[[nodiscard]] int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

void set_process_environment(const char* name, const char* value) {
#if defined(_WIN32)
  static_cast<void>(_putenv_s(name, value));
#else
  static_cast<void>(setenv(name, value, 1));
#endif
}

void clear_process_environment(const char* name) {
#if defined(_WIN32)
  static_cast<void>(_putenv_s(name, ""));
#else
  static_cast<void>(unsetenv(name));
#endif
}

}  // namespace

int main() {
  namespace fs = std::filesystem;
  const fs::path fixture_root(PHOTO_EDITOR_OCIO_PROJECT_FIXTURE_DIR);
  const auto config_path = fixture_root / "config.ocio";
  const auto context_config_path = fixture_root / "config-context.ocio";
  const auto clf_path = fixture_root / "luts" / "scale.clf";
  const auto ctf_path = fixture_root / "luts" / "offset.ctf";

  const auto empty = photo_editor::pipeline::load_ocio_project_config("");
  const auto unsupported =
      photo_editor::pipeline::load_ocio_project_config("project-config.txt");
  if (empty.ready() ||
      empty.description().status != OcioProjectStatus::PathEmpty ||
      unsupported.ready() ||
      unsupported.description().status != OcioProjectStatus::UnsupportedSource) {
    return fail("OCIO project config loading should reject empty and unsupported sources");
  }

  const auto photographic_before =
      photo_editor::pipeline::default_color_pipeline_config();
  const auto config = photo_editor::pipeline::load_ocio_project_config(
      config_path.string());
  if (!photo_editor::pipeline::ocio_project_backend_available()) {
    const auto unavailable_gpu_program =
        photo_editor::pipeline::OcioProjectProcessor{}
            .compile_msl_gpu_program();
    if (config.ready() ||
        config.description().status != OcioProjectStatus::BackendUnavailable ||
        unavailable_gpu_program.ready() ||
        unavailable_gpu_program.description().status !=
            OcioProjectStatus::BackendUnavailable) {
      return fail("builds without OpenColorIO should fail project configs closed");
    }
    std::cout << "SKIP: OpenColorIO project-config backend unavailable\n";
    return 77;
  }

  const auto& description = config.description();
  if (!config.ready() || description.schema != "ocio-project-config:v1" ||
      description.status != OcioProjectStatus::Ready ||
      description.backend != "OpenColorIO" || description.backend_version.empty() ||
      description.source_kind != "ocio-file" ||
      description.source_sha256 != PHOTO_EDITOR_OCIO_PROJECT_CONFIG_SHA256 ||
      description.config_name != "photo-editor-ocio-project-v1" ||
      description.config_cache_id.empty() ||
      description.context_cache_id.empty() ||
      description.default_display != "Test Display" ||
      !description.strict_parsing ||
      !description.process_environment_ignored ||
      !description.photographic_pipeline_isolated ||
      !description.context_variables.empty() ||
      description.roles.size() != 2U || description.color_spaces.size() != 4U ||
      description.views.size() != 2U || description.looks.size() != 1U ||
      description.file_rules.size() != 2U) {
    return fail("OCIO project config inspection should publish complete immutable provenance");
  }

  const auto* const previous_context_environment = std::getenv("LUT_NAME");
  const auto previous_context_value =
      previous_context_environment != nullptr
          ? std::string(previous_context_environment)
          : std::string{};
  const auto had_previous_context_environment =
      previous_context_environment != nullptr;
  set_process_environment("LUT_NAME", "offset.ctf");
  const auto default_context_config =
      photo_editor::pipeline::load_ocio_project_config(
          context_config_path.string());
  if (had_previous_context_environment) {
    set_process_environment("LUT_NAME", previous_context_value.c_str());
  } else {
    clear_process_environment("LUT_NAME");
  }
  const auto override_context_config =
      photo_editor::pipeline::load_ocio_project_config(
          context_config_path.string(),
          photo_editor::pipeline::OcioProjectLoadOptions{
              .context_overrides = {{
                  .name = "LUT_NAME",
                  .value = "offset.ctf",
              }},
          });
  const auto default_context_processor =
      default_context_config.compile_processor({
          .kind = OcioProjectProcessorKind::ColorSpace,
          .source_color_space = "linear",
          .destination_color_space = "context_scaled",
      });
  const auto override_context_processor =
      override_context_config.compile_processor({
          .kind = OcioProjectProcessorKind::ColorSpace,
          .source_color_space = "linear",
          .destination_color_space = "context_scaled",
      });
  std::array<OcioProjectRgbaFloat, 1> default_context_pixel = {{
      {.red = 0.4F, .green = 0.4F, .blue = 0.4F, .alpha = 0.66F},
  }};
  auto override_context_pixel = default_context_pixel;
  const auto default_context_gpu =
      default_context_processor.compile_msl_gpu_program();
  const auto override_context_gpu =
      override_context_processor.compile_msl_gpu_program();
  const auto duplicate_context =
      photo_editor::pipeline::load_ocio_project_config(
          context_config_path.string(),
          photo_editor::pipeline::OcioProjectLoadOptions{
              .context_overrides = {
                  {.name = "LUT_NAME", .value = "scale.clf"},
                  {.name = "LUT_NAME", .value = "offset.ctf"},
              },
          });
  if (!default_context_config.ready() || !override_context_config.ready() ||
      !default_context_config.description().process_environment_ignored ||
      default_context_config.description().context_cache_id.empty() ||
      default_context_config.description().context_variables.size() != 1U ||
      default_context_config.description().context_variables[0].name !=
          "LUT_NAME" ||
      default_context_config.description().context_variables[0].value !=
          "scale.clf" ||
      default_context_config.description().context_variables[0].source !=
          "config-default" ||
      override_context_config.description().context_variables.size() != 1U ||
      override_context_config.description().context_variables[0].value !=
          "offset.ctf" ||
      override_context_config.description().context_variables[0].source !=
          "explicit-override" ||
      default_context_config.description().context_cache_id ==
          override_context_config.description().context_cache_id ||
      default_context_config.description().config_cache_id ==
          override_context_config.description().config_cache_id ||
      !default_context_processor.apply(default_context_pixel) ||
      !override_context_processor.apply(override_context_pixel) ||
      !pixel_matches(default_context_pixel[0], {0.5F, 0.3F, 0.2F, 0.66F}) ||
      !pixel_matches(override_context_pixel[0], {0.5F, 0.6F, 0.7F, 0.66F}) ||
      default_context_processor.description().external_files.size() != 1U ||
      default_context_processor.description().external_files[0].sha256 !=
          PHOTO_EDITOR_OCIO_PROJECT_CLF_SHA256 ||
      override_context_processor.description().external_files.size() != 1U ||
      override_context_processor.description().external_files[0].sha256 !=
          PHOTO_EDITOR_OCIO_PROJECT_CTF_SHA256 ||
      !default_context_gpu.ready() || !override_context_gpu.ready() ||
      default_context_gpu.description().program_cache_key ==
          override_context_gpu.description().program_cache_key ||
      duplicate_context.ready() ||
      duplicate_context.description().status != OcioProjectStatus::InvalidRequest ||
      duplicate_context.description().failure_reason !=
          "ocio_context_variable_duplicate") {
    std::cerr << "default context ready=" << default_context_config.ready()
              << " context-cache="
              << default_context_config.description().context_cache_id
              << " config-cache="
              << default_context_config.description().config_cache_id
              << " reason="
              << default_context_config.description().failure_reason
              << " variables="
              << default_context_config.description().context_variables.size()
              << " processor=" << default_context_processor.ready()
              << " pixel=" << default_context_pixel[0].red << ','
              << default_context_pixel[0].green << ','
              << default_context_pixel[0].blue << " gpu="
              << default_context_gpu.ready() << " gpu-reason="
              << default_context_gpu.description().failure_reason << '\n'
              << "override context ready=" << override_context_config.ready()
              << " context-cache="
              << override_context_config.description().context_cache_id
              << " config-cache="
              << override_context_config.description().config_cache_id
              << " reason="
              << override_context_config.description().failure_reason
              << " variables="
              << override_context_config.description().context_variables.size()
              << " processor=" << override_context_processor.ready()
              << " pixel=" << override_context_pixel[0].red << ','
              << override_context_pixel[0].green << ','
              << override_context_pixel[0].blue << " gpu="
              << override_context_gpu.ready() << " gpu-reason="
              << override_context_gpu.description().failure_reason << '\n'
              << "duplicate status="
              << photo_editor::pipeline::ocio_project_status_name(
                     duplicate_context.description().status)
              << " reason=" << duplicate_context.description().failure_reason
              << '\n';
    return fail("OCIO contexts should use declared defaults plus explicit overrides and ignore ambient process state");
  }
  if (description.roles[0].name != "default" ||
      description.roles[0].color_space != "linear" ||
      description.color_spaces[0].name != "linear" ||
      description.color_spaces[0].encoding != "scene-linear" ||
      description.color_spaces[0].reference_space != "scene" ||
      description.views[0].display != "Test Display" ||
      description.views[0].name != "Doubled" ||
      !description.views[0].is_default ||
      description.looks[0].name != "warm" ||
      description.looks[0].process_space != "linear" ||
      !description.looks[0].has_forward_transform) {
    return fail("OCIO project config should retain roles, encodings, views, and look semantics");
  }
  const auto photographic_after =
      photo_editor::pipeline::default_color_pipeline_config();
  if (photographic_before.input_profile.profile_name !=
          photographic_after.input_profile.profile_name ||
      photographic_before.working_profile.profile_name !=
          photographic_after.working_profile.profile_name ||
      photographic_before.preview_output_profile.profile_name !=
          photographic_after.preview_output_profile.profile_name ||
      photographic_before.export_output_profile.profile_name !=
          photographic_after.export_output_profile.profile_name ||
      photographic_before.photographic_rendering_model !=
          photographic_after.photographic_rendering_model) {
    return fail("loading a project config must not mutate the photographic color pipeline");
  }

  const auto exr_match = config.match_file_rule("shot/plate.0001.exr");
  const auto default_match = config.match_file_rule("shot/reference.png");
  if (exr_match.status != OcioProjectStatus::Ready ||
      exr_match.rule_name != "Linear EXR" || exr_match.color_space != "linear" ||
      exr_match.only_default_rule ||
      default_match.status != OcioProjectStatus::Ready ||
      default_match.rule_name != "Default" ||
      default_match.color_space != "linear" ||
      !default_match.only_default_rule) {
    return fail("OCIO file rules should resolve the first matching rule and report default-only matches");
  }

  const auto color_processor = config.compile_processor({
      .kind = OcioProjectProcessorKind::ColorSpace,
      .source_color_space = "linear",
      .destination_color_space = "doubled",
  });
  const auto color_processor_again = config.compile_processor({
      .kind = OcioProjectProcessorKind::ColorSpace,
      .source_color_space = "linear",
      .destination_color_space = "doubled",
  });
  std::array<OcioProjectRgbaFloat, 1> color_pixel = {{
      {.red = 0.1F, .green = -0.2F, .blue = 1.5F, .alpha = 0.37F},
  }};
  if (!color_processor.ready() ||
      color_processor.description().processor_cache_id.empty() ||
      color_processor.description().processor_cache_id !=
          color_processor_again.description().processor_cache_id ||
      color_processor.description().no_op ||
      !color_processor.apply(color_pixel) ||
      !pixel_matches(color_pixel[0], {0.2F, -0.4F, 3.0F, 0.37F})) {
    return fail("OCIO color-space processors should be deterministic, extended-range safe, and alpha preserving");
  }
  const auto color_gpu_program = color_processor.compile_msl_gpu_program();
  const auto color_gpu_program_again =
      color_processor.compile_msl_gpu_program();
  if (!color_gpu_program.ready() ||
      color_gpu_program.description().schema !=
          "ocio-project-gpu-program:v1" ||
      color_gpu_program.description().language != "msl-2.0" ||
      color_gpu_program.description().function_name !=
          "photo_editor_ocio_transform" ||
      color_gpu_program.description().config_cache_id !=
          color_processor.description().config_cache_id ||
      color_gpu_program.description().processor_cache_id !=
          color_processor.description().processor_cache_id ||
      color_gpu_program.description().gpu_processor_cache_id.empty() ||
      color_gpu_program.description().shader_cache_id.empty() ||
      color_gpu_program.description().shader_sha256.size() != 64U ||
      color_gpu_program.description().resource_sha256.size() != 64U ||
      color_gpu_program.description().program_cache_key.size() != 64U ||
      color_gpu_program.description().program_cache_key !=
          color_gpu_program_again.description().program_cache_key ||
      color_gpu_program.description().shader_text.find(
          "photo_editor_ocio_transform") == std::string::npos ||
      color_gpu_program.description().uniform_count != 0U ||
      color_gpu_program.description().uniform_buffer_bytes != 0U ||
      !color_gpu_program.description().preserves_unassociated_alpha) {
    std::cerr << "GPU program failure: "
              << color_gpu_program.description().failure_reason << '\n';
    return fail("OCIO processors should compile immutable, identity-pinned MSL programs");
  }

  const auto inverse_processor = config.compile_processor({
      .kind = OcioProjectProcessorKind::ColorSpace,
      .source_color_space = "linear",
      .destination_color_space = "doubled",
      .inverse = true,
  });
  if (!inverse_processor.apply(color_pixel) ||
      !pixel_matches(color_pixel[0], {0.1F, -0.2F, 1.5F, 0.37F})) {
    return fail("OCIO project color-space processors should support explicit inversion");
  }

  const auto display_processor = config.compile_processor({
      .kind = OcioProjectProcessorKind::DisplayView,
      .source_color_space = "linear",
      .display = "Test Display",
      .view = "Doubled",
  });
  std::array<OcioProjectRgbaFloat, 1> display_pixel = {{
      {.red = 0.25F, .green = 0.5F, .blue = 0.75F, .alpha = 0.61F},
  }};
  if (!display_processor.apply(display_pixel) ||
      !pixel_matches(display_pixel[0], {0.5F, 1.0F, 1.5F, 0.61F})) {
    return fail("OCIO display/view processors should execute only when explicitly requested");
  }

  const auto look_processor = config.compile_processor({
      .kind = OcioProjectProcessorKind::Look,
      .source_color_space = "linear",
      .destination_color_space = "linear",
      .looks = "+warm",
  });
  std::array<OcioProjectRgbaFloat, 1> look_pixel = {{
      {.red = 0.4F, .green = 0.5F, .blue = 0.6F, .alpha = 0.42F},
  }};
  if (!look_processor.ready() ||
      look_processor.description().resolved_looks.size() != 1U ||
      look_processor.description().resolved_looks[0] != "warm" ||
      !look_processor.apply(look_pixel) ||
      !pixel_matches(look_pixel[0], {0.44F, 0.5F, 0.54F, 0.42F})) {
    return fail("OCIO look processors should resolve process-space semantics and provenance");
  }

  const auto clf_processor = config.compile_processor({
      .kind = OcioProjectProcessorKind::FileTransform,
      .file_transform_path = clf_path.string(),
  });
  std::array<OcioProjectRgbaFloat, 1> clf_pixel = {{
      {.red = 0.4F, .green = 0.4F, .blue = 0.4F, .alpha = 0.73F},
  }};
  if (!clf_processor.ready() ||
      clf_processor.description().external_files.size() != 1U ||
      clf_processor.description().external_files[0].sha256 !=
          PHOTO_EDITOR_OCIO_PROJECT_CLF_SHA256 ||
      clf_processor.description().external_files[0].provenance_scope !=
          "file-content" ||
      !clf_processor.apply(clf_pixel) ||
      !pixel_matches(clf_pixel[0], {0.5F, 0.3F, 0.2F, 0.73F})) {
    return fail("OCIO CLF processors should execute with content-hashed external provenance");
  }

  const auto ctf_processor = config.compile_processor({
      .kind = OcioProjectProcessorKind::FileTransform,
      .file_transform_path = ctf_path.string(),
  });
  std::array<OcioProjectRgbaFloat, 1> ctf_pixel = {{
      {.red = -0.1F, .green = 0.0F, .blue = 1.2F, .alpha = 0.19F},
  }};
  if (!ctf_processor.ready() ||
      ctf_processor.description().external_files.size() != 1U ||
      ctf_processor.description().external_files[0].sha256 !=
          PHOTO_EDITOR_OCIO_PROJECT_CTF_SHA256 ||
      !ctf_processor.apply(ctf_pixel) ||
      !pixel_matches(ctf_pixel[0], {0.0F, 0.2F, 1.5F, 0.19F})) {
    return fail("OCIO CTF processors should preserve extended range and content provenance");
  }

  const auto config_clf_processor = config.compile_processor({
      .kind = OcioProjectProcessorKind::ColorSpace,
      .source_color_space = "linear",
      .destination_color_space = "clf_scaled",
  });
  std::array<OcioProjectRgbaFloat, 1> config_clf_pixel = {{
      {.red = 0.8F, .green = 0.8F, .blue = 0.8F, .alpha = 0.91F},
  }};
  if (!config_clf_processor.apply(config_clf_pixel) ||
      !pixel_matches(config_clf_pixel[0], {1.0F, 0.6F, 0.4F, 0.91F})) {
    return fail("OCIO config search paths should resolve project CLF transforms");
  }

  const auto cube_processor = config.compile_processor({
      .kind = OcioProjectProcessorKind::ColorSpace,
      .source_color_space = "linear",
      .destination_color_space = "cube_swap",
  });
  const auto cube_gpu_program = cube_processor.compile_msl_gpu_program();
  std::array<OcioProjectRgbaFloat, 1> cube_pixel = {{
      {.red = 0.2F, .green = 0.4F, .blue = 0.8F, .alpha = 0.57F},
  }};
  if (!cube_processor.ready() || !cube_processor.apply(cube_pixel) ||
      !pixel_matches(cube_pixel[0], {0.8F, 0.4F, 0.2F, 0.57F}) ||
      !cube_gpu_program.ready() ||
      cube_gpu_program.description().textures.size() != 1U ||
      cube_gpu_program.description().textures[0].kind !=
          photo_editor::pipeline::OcioProjectGpuTextureKind::ThreeDimensional ||
      cube_gpu_program.description().textures[0].width != 2U ||
      cube_gpu_program.description().textures[0].height != 2U ||
      cube_gpu_program.description().textures[0].depth != 2U ||
      cube_gpu_program.description().textures[0].channels != 3U ||
      cube_gpu_program.description().textures[0].values.size() != 24U ||
      cube_gpu_program.description().texture_bytes != 24U * sizeof(float) ||
      cube_gpu_program.description().resource_sha256.size() != 64U ||
      cube_gpu_program.description().program_cache_key.size() != 64U) {
    std::cerr << "3D GPU program failure: "
              << cube_gpu_program.description().failure_reason
              << " pixel=" << cube_pixel[0].red << ','
              << cube_pixel[0].green << ',' << cube_pixel[0].blue << ','
              << cube_pixel[0].alpha
              << " textures=" << cube_gpu_program.description().textures.size()
              << " bytes=" << cube_gpu_program.description().texture_bytes;
    if (!cube_gpu_program.description().textures.empty()) {
      const auto& texture = cube_gpu_program.description().textures[0];
      std::cerr << " dimensions=" << texture.width << 'x' << texture.height
                << 'x' << texture.depth << " channels=" << texture.channels
                << " values=" << texture.values.size();
    }
    std::cerr << '\n';
    return fail("OCIO GPU programs should retain bounded immutable 3D LUT resources");
  }

  const auto invalid_processor = config.compile_processor({
      .kind = OcioProjectProcessorKind::ColorSpace,
      .source_color_space = "missing",
      .destination_color_space = "doubled",
  });
  const auto invalid_gpu_program = invalid_processor.compile_msl_gpu_program();
  std::array<OcioProjectRgbaFloat, 1> invalid_pixel = {{
      {.red = 0.1F, .green = 0.2F, .blue = 0.3F, .alpha = 0.4F},
  }};
  if (invalid_processor.ready() || invalid_processor.apply(invalid_pixel) ||
      invalid_gpu_program.ready() ||
      invalid_gpu_program.description().status !=
          OcioProjectStatus::InvalidRequest ||
      invalid_processor.description().status !=
          OcioProjectStatus::ProcessorCompileFailed ||
      !pixel_matches(invalid_pixel[0], {0.1F, 0.2F, 0.3F, 0.4F})) {
    return fail("invalid OCIO selections should fail closed without touching pixels");
  }
  std::array<OcioProjectRgbaFloat, 1> non_finite_pixel = {{
      {.red = std::numeric_limits<float>::quiet_NaN(),
       .green = 0.2F,
       .blue = 0.3F,
       .alpha = 0.4F},
  }};
  if (color_processor.apply(non_finite_pixel) ||
      !std::isnan(non_finite_pixel[0].red)) {
    return fail("OCIO processors should reject non-finite output without publishing partial results");
  }

  std::atomic<bool> concurrency_ok = true;
  std::vector<std::thread> workers;
  for (int worker = 0; worker < 8; ++worker) {
    workers.emplace_back([&]() {
      for (int iteration = 0; iteration < 100; ++iteration) {
        std::array<OcioProjectRgbaFloat, 1> pixel = {{
            {.red = 0.25F, .green = -0.5F, .blue = 2.0F, .alpha = 0.33F},
        }};
        if (!color_processor.apply(pixel) ||
            !pixel_matches(pixel[0], {0.5F, -1.0F, 4.0F, 0.33F})) {
          concurrency_ok.store(false);
          return;
        }
      }
    });
  }
  for (auto& worker : workers) {
    worker.join();
  }
  if (!concurrency_ok.load()) {
    return fail("compiled OCIO project processors should be immutable and thread safe");
  }

  const auto unique = std::to_string(
      std::chrono::steady_clock::now().time_since_epoch().count());
  const auto temp_root =
      fs::temp_directory_path() / ("photo-editor-ocio-project-" + unique);
  fs::create_directories(temp_root);
  const auto mutable_root = temp_root / "mutable-project";
  fs::copy(
      fixture_root,
      mutable_root,
      fs::copy_options::recursive);
  const auto mutable_config_path = mutable_root / "config.ocio";
  const auto mutable_lut_path = mutable_root / "luts" / "scale.clf";
  const auto mutable_config =
      photo_editor::pipeline::load_ocio_project_config(
          mutable_config_path.string());
  const auto mutable_processor = mutable_config.compile_processor({
      .kind = OcioProjectProcessorKind::ColorSpace,
      .source_color_space = "linear",
      .destination_color_space = "clf_scaled",
  });
  {
    std::ofstream changed_lut(mutable_lut_path, std::ios::app);
    changed_lut << "\n<!-- provenance mutation -->\n";
  }
  const auto stale_dependency_processor = mutable_config.compile_processor({
      .kind = OcioProjectProcessorKind::ColorSpace,
      .source_color_space = "linear",
      .destination_color_space = "clf_scaled",
  });
  if (!mutable_config.ready() || !mutable_processor.ready() ||
      stale_dependency_processor.ready() ||
      stale_dependency_processor.description().status !=
          OcioProjectStatus::InvalidRequest ||
      stale_dependency_processor.description().failure_reason !=
          "config_inputs_changed_reload_required") {
    fs::remove_all(temp_root);
    return fail("loaded OCIO configs should fail closed after a referenced LUT changes");
  }
  const auto reloaded_mutable_config =
      photo_editor::pipeline::load_ocio_project_config(
          mutable_config_path.string());
  if (!reloaded_mutable_config.ready() ||
      !reloaded_mutable_config
           .compile_processor({
               .kind = OcioProjectProcessorKind::ColorSpace,
               .source_color_space = "linear",
               .destination_color_space = "clf_scaled",
           })
           .ready()) {
    fs::remove_all(temp_root);
    return fail("reloading an OCIO config should accept its new dependency identity");
  }
  {
    std::ofstream changed_config(mutable_config_path, std::ios::app);
    changed_config << "\n# config provenance mutation\n";
  }
  const auto stale_source_match =
      reloaded_mutable_config.match_file_rule("plate.exr");
  if (stale_source_match.status == OcioProjectStatus::Ready ||
      stale_source_match.failure_reason !=
          "config_inputs_changed_reload_required") {
    fs::remove_all(temp_root);
    return fail("loaded OCIO configs should fail closed after their source changes");
  }
  const auto archive_path = temp_root / "project.ocioz";
  const auto archive = photo_editor::pipeline::archive_ocio_project_config(
      config_path.string(), archive_path.string());
  const auto archived_config =
      photo_editor::pipeline::load_ocio_project_config(archive_path.string());
  if (archive.status != OcioProjectStatus::Ready ||
      archive.archive_sha256.empty() || !archived_config.ready() ||
      archived_config.description().source_kind != "ocioz-archive" ||
      archived_config.description().source_sha256 != archive.archive_sha256 ||
      archived_config.description().config_name != description.config_name) {
    fs::remove_all(temp_root);
    return fail("OCIO project configs should round-trip through content-hashed ocioz archives");
  }
  const auto archived_processor = archived_config.compile_processor({
      .kind = OcioProjectProcessorKind::ColorSpace,
      .source_color_space = "linear",
      .destination_color_space = "clf_scaled",
  });
  std::array<OcioProjectRgbaFloat, 1> archived_pixel = {{
      {.red = 0.8F, .green = 0.8F, .blue = 0.8F, .alpha = 0.29F},
  }};
  const auto overwrite_attempt =
      photo_editor::pipeline::archive_ocio_project_config(
          config_path.string(), archive_path.string());
  if (!archived_processor.apply(archived_pixel) ||
      !pixel_matches(archived_pixel[0], {1.0F, 0.6F, 0.4F, 0.29F}) ||
      archived_processor.description().external_files.size() != 1U ||
      archived_processor.description().external_files[0].sha256 !=
          archive.archive_sha256 ||
      archived_processor.description().external_files[0].provenance_scope !=
          "containing-archive" ||
      overwrite_attempt.status != OcioProjectStatus::ArchiveFailed ||
      overwrite_attempt.failure_reason != "ocio_archive_destination_exists") {
    fs::remove_all(temp_root);
    return fail("ocioz archives should retain LUTs and archive creation should never overwrite silently");
  }
  fs::remove_all(temp_root);

  return EXIT_SUCCESS;
}
