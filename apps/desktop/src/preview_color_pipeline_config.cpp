#include "preview_color_pipeline_config.h"

#include "edit_recipe_pipeline_bridge.h"
#include "system_monitor_profile_asset_bridge.h"

namespace photo_editor::desktop {

photo_editor::pipeline::ColorPipelineConfig
compile_preview_color_pipeline_config_for_display(
    const EditRecipeState& state,
    const photo_editor::pipeline::DecodedImageMetadata* metadata,
    const SystemMonitorProfileSnapshot& monitor_snapshot) {
  auto effective_state = state;
  effective_state.preview_output_mode =
      normalized_preview_output_mode_name(state.preview_output_mode);
  if (effective_state.preview_output_mode == QStringLiteral("Match Export")) {
    effective_state.soft_proof_enabled = true;
    effective_state.soft_proof_profile = effective_state.output_color_space;
    effective_state.preview_color_space = effective_state.output_color_space;
  } else if (effective_state.preview_output_mode == QStringLiteral("Soft Proof")) {
    effective_state.soft_proof_enabled = true;
    if (!effective_state.monitor_color_profile.trimmed().isEmpty()) {
      effective_state.preview_color_space = normalized_output_color_space_name(
          effective_state.monitor_color_profile);
    }
  } else if (effective_state.preview_output_mode != QStringLiteral("Soft Proof")) {
    effective_state.soft_proof_enabled = false;
  }

  auto config = compile_color_pipeline_config(effective_state, metadata);
  const auto uses_system_monitor =
      effective_state.monitor_color_profile.trimmed().isEmpty() &&
      (!effective_state.preview_icc_profile_asset.configured() ||
       effective_state.preview_icc_profile_asset.source_kind ==
           QStringLiteral("system-monitor"));
  if (!uses_system_monitor) {
    return config;
  }

  if (!effective_state.preview_icc_profile_asset.configured()) {
    constexpr auto kMaximumMonitorProfileBytes = qsizetype{64} * 1024 * 1024;
    const auto monitor_profile = resolve_system_monitor_profile_asset(
        monitor_snapshot, kMaximumMonitorProfileBytes);
    if (monitor_profile.ready()) {
      config.preview_icc_profile_asset = monitor_profile;
    }
  }
  if (effective_state.preview_output_mode != QStringLiteral("Monitor") ||
      !monitor_snapshot.display_target_valid()) {
    return config;
  }

  auto& target = config.preview_display_target;
  target.target_name =
      (monitor_snapshot.display_target_verified
           ? QStringLiteral("Verified active display: %1")
           : QStringLiteral("Reference active display: %1"))
          .arg(monitor_snapshot.display_name)
          .toStdString();
  target.peak_luminance_nits = monitor_snapshot.peak_luminance_nits;
  target.black_luminance_nits = monitor_snapshot.black_luminance_nits;
  target.reference_white_nits = monitor_snapshot.reference_white_nits;
  target.surround_luminance_nits = monitor_snapshot.surround_luminance_nits;
  return config;
}

}  // namespace photo_editor::desktop
