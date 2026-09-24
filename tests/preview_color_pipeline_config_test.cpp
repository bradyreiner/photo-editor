#include "preview_color_pipeline_config.h"

#include <QColorSpace>
#include <QCoreApplication>

#include <cmath>
#include <cstdlib>
#include <iostream>

namespace {

int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

bool close(double left, double right) {
  return std::abs(left - right) <= 1.0e-9;
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  using namespace photo_editor::desktop;

  auto match_export = EditRecipeState{};
  match_export.preview_output_mode = QStringLiteral("Match Export");
  match_export.output_color_space = QStringLiteral("Display P3");
  const auto match_export_config =
      compile_preview_color_pipeline_config_for_display(
          match_export, nullptr, {});
  if (!match_export_config.soft_proof_enabled ||
      match_export_config.preview_output_profile.profile_name !=
          match_export_config.export_output_profile.profile_name) {
    return fail("Match Export must derive preview and proof intent from export output");
  }

  auto manual_proof = EditRecipeState{};
  manual_proof.preview_output_mode = QStringLiteral("Soft Proof");
  manual_proof.monitor_color_profile = QStringLiteral("Display P3");
  const auto manual_proof_config =
      compile_preview_color_pipeline_config_for_display(
          manual_proof, nullptr, {});
  if (!manual_proof_config.soft_proof_enabled ||
      manual_proof_config.preview_output_profile.color_space.primaries !=
          photo_editor::pipeline::ColorPrimaries::DisplayP3) {
    return fail("manual soft proof must retain its explicit preview carrier");
  }

  auto monitor_state = EditRecipeState{};
  monitor_state.preview_output_mode = QStringLiteral("Monitor");
  const auto monitor_snapshot = SystemMonitorProfileSnapshot{
      .color_space = QColorSpace(QColorSpace::SRgb),
      .display_name = QStringLiteral("Reference Panel"),
      .identity = QStringLiteral("display:test|icc:srgb"),
      .display_identity = QStringLiteral("display:test"),
      .display_target_identity = QStringLiteral("display:test|measured"),
      .display_target_provenance = QStringLiteral("test-measurement"),
      .peak_luminance_nits = 600.0,
      .black_luminance_nits = 0.02,
      .reference_white_nits = 120.0,
      .surround_luminance_nits = 8.0,
      .display_target_verified = true,
  };
  const auto monitor_config =
      compile_preview_color_pipeline_config_for_display(
          monitor_state, nullptr, monitor_snapshot);
  if (monitor_config.preview_display_target.target_name !=
          "Verified active display: Reference Panel" ||
      !close(monitor_config.preview_display_target.peak_luminance_nits, 600.0) ||
      !close(monitor_config.preview_display_target.black_luminance_nits, 0.02) ||
      !close(monitor_config.preview_display_target.reference_white_nits, 120.0) ||
      !close(monitor_config.preview_display_target.surround_luminance_nits, 8.0)) {
    return fail("monitor mode must publish the complete verified display target");
  }

  const auto repeated = compile_preview_color_pipeline_config_for_display(
      monitor_state, nullptr, monitor_snapshot);
  if (repeated.preview_display_target.target_name !=
          monitor_config.preview_display_target.target_name ||
      !close(repeated.preview_display_target.peak_luminance_nits,
             monitor_config.preview_display_target.peak_luminance_nits)) {
    return fail("preview color configuration must be deterministic");
  }
  return EXIT_SUCCESS;
}
