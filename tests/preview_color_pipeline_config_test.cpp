#include "preview_color_pipeline_config.h"

#include <QColorSpace>
#include <QCoreApplication>
#include <QJsonDocument>

#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string_view>

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

  struct WorkingProfileCase final {
    QString name;
    QString alias;
    photo_editor::pipeline::ColorPrimaries primaries;
    photo_editor::pipeline::WorkingTransformKind kind;
  };
  const auto working_profiles = std::array<WorkingProfileCase, 10>{{
      {QStringLiteral("Linear Adobe RGB"), QStringLiteral("Linear Adobe RGB"),
       photo_editor::pipeline::ColorPrimaries::AdobeRgb,
       photo_editor::pipeline::WorkingTransformKind::LinearAdobeRgb},
      {QStringLiteral("Linear Adobe RGB"), QStringLiteral("linear-adobe-rgb"),
       photo_editor::pipeline::ColorPrimaries::AdobeRgb,
       photo_editor::pipeline::WorkingTransformKind::LinearAdobeRgb},
      {QStringLiteral("Linear Adobe RGB"), QStringLiteral("AdobeRGBLinear"),
       photo_editor::pipeline::ColorPrimaries::AdobeRgb,
       photo_editor::pipeline::WorkingTransformKind::LinearAdobeRgb},
      {QStringLiteral("ACEScg"), QStringLiteral("ACEScg/AP1"),
       photo_editor::pipeline::ColorPrimaries::AcesAp1,
       photo_editor::pipeline::WorkingTransformKind::LinearAcesCg},
      {QStringLiteral("ACEScg"), QStringLiteral("ACEScg"),
       photo_editor::pipeline::ColorPrimaries::AcesAp1,
       photo_editor::pipeline::WorkingTransformKind::LinearAcesCg},
      {QStringLiteral("ACEScg"), QStringLiteral("linearacesap1"),
       photo_editor::pipeline::ColorPrimaries::AcesAp1,
       photo_editor::pipeline::WorkingTransformKind::LinearAcesCg},
      {QStringLiteral("ACES2065-1"), QStringLiteral("ACES2065-1/AP0"),
       photo_editor::pipeline::ColorPrimaries::AcesAp0,
       photo_editor::pipeline::WorkingTransformKind::LinearAces2065_1},
      {QStringLiteral("ACES2065-1"), QStringLiteral("ACES2065-1"),
       photo_editor::pipeline::ColorPrimaries::AcesAp0,
       photo_editor::pipeline::WorkingTransformKind::LinearAces2065_1},
      {QStringLiteral("ACES2065-1"), QStringLiteral("ACES2065_1"),
       photo_editor::pipeline::ColorPrimaries::AcesAp0,
       photo_editor::pipeline::WorkingTransformKind::LinearAces2065_1},
      {QStringLiteral("ACES2065-1"), QStringLiteral("linearacesap0"),
       photo_editor::pipeline::ColorPrimaries::AcesAp0,
       photo_editor::pipeline::WorkingTransformKind::LinearAces2065_1},
  }};
  for (const auto& profile : working_profiles) {
    auto recipe = EditRecipeState{};
    recipe.working_color_profile = profile.alias;
    const auto serialized = serialize_edit_recipe_json(recipe);
    const auto document =
        QJsonDocument::fromJson(serialized.toUtf8()).object();
    const auto saved_profile = document
                                   .value(QStringLiteral("colorPipeline"))
                                   .toObject()
                                   .value(QStringLiteral("workingProfile"))
                                   .toString();
    const auto restored = parse_edit_recipe_json(serialized);
    const auto restored_config = compile_preview_color_pipeline_config_for_display(
        restored, nullptr, {});
    if (saved_profile != profile.name ||
        restored.working_color_profile != profile.name ||
        restored_config.working_profile.profile_name !=
            profile.name.toStdString() ||
        restored_config.working_profile.kind != profile.kind ||
        restored_config.working_profile.color_space.primaries !=
            profile.primaries ||
        restored_config.working_profile.color_space.transfer_function !=
            photo_editor::pipeline::TransferFunction::Linear) {
      std::cerr << "working profile recipe roundtrip="
                << profile.name.toStdString() << '\n';
      return fail("wide-gamut working profile names must survive recipe save/load and compile to distinct linear spaces");
    }
    if (normalized_working_color_profile_name(profile.alias) != profile.name) {
      return fail("working-profile aliases must normalize to the serialized canonical name");
    }
  }

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
