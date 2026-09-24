#pragma once

#include "edit_recipe_state.h"

#include "photo_editor/pipeline/color_pipeline.h"
#include "photo_editor/pipeline/color_profile_asset.h"
#include "photo_editor/pipeline/dcp_profile_asset.h"

#include <QVariantMap>

#include <algorithm>
#include <cstdint>

namespace photo_editor::desktop {

[[nodiscard]] inline photo_editor::pipeline::IccProfileAssetReference
icc_profile_asset_reference(const IccProfileAssetState& state) {
  return photo_editor::pipeline::IccProfileAssetReference{
      .asset_id = state.asset_id.trimmed().toStdString(),
      .payload_sha256 = state.payload_sha256.trimmed().toLower().toStdString(),
      .display_name = state.display_name.trimmed().toStdString(),
      .source_uri = state.source_uri.trimmed().toStdString(),
      .source_kind = state.source_kind.trimmed().toStdString(),
      .byte_size = static_cast<std::uint64_t>(std::max<qint64>(0, state.byte_size)),
  };
}

[[nodiscard]] inline photo_editor::pipeline::ResolvedIccProfileAsset
resolve_icc_profile_asset(const IccProfileAssetState& state) {
  return photo_editor::pipeline::default_icc_profile_asset_registry().resolve(
      icc_profile_asset_reference(state));
}

[[nodiscard]] inline photo_editor::pipeline::DcpProfileAssetReference
dcp_profile_asset_reference(const DcpProfileAssetState& state) {
  return photo_editor::pipeline::DcpProfileAssetReference{
      .schema_version = static_cast<std::uint32_t>(std::max(0, state.schema_version)),
      .asset_id = state.asset_id.trimmed().toStdString(),
      .payload_sha256 = state.payload_sha256.trimmed().toLower().toStdString(),
      .display_name = state.display_name.trimmed().toStdString(),
      .source_uri = state.source_uri.trimmed().toStdString(),
      .source_kind = state.source_kind.trimmed().toStdString(),
      .byte_size = static_cast<std::uint64_t>(std::max<qint64>(0, state.byte_size)),
  };
}

[[nodiscard]] inline photo_editor::pipeline::ResolvedDcpProfileAsset
resolve_dcp_profile_asset(const DcpProfileAssetState& state) {
  return photo_editor::pipeline::default_dcp_profile_asset_registry().resolve(
      dcp_profile_asset_reference(state));
}

inline void insert_camera_profile_provenance(
    QVariantMap* target,
    const photo_editor::pipeline::CameraProfile& profile,
    const photo_editor::pipeline::ResolvedDcpProfileAsset& asset) {
  if (target == nullptr) {
    return;
  }

  target->insert(QStringLiteral("cameraProfileSecondIlluminantWeight"),
                 profile.second_illuminant_weight);
  target->insert(QStringLiteral("cameraProfileThirdIlluminantWeight"),
                 profile.illuminant_weights[2]);
  target->insert(QStringLiteral("cameraProfileTripleIlluminant"),
                 profile.triple_illuminant);
  for (std::size_t index = 0; index < profile.illuminant_data_kinds.size(); ++index) {
    target->insert(
        QStringLiteral("cameraProfileIlluminantData%1Kind").arg(index + 1),
        QString::fromLatin1(photo_editor::pipeline::dcp_illuminant_data_kind_name(
            profile.illuminant_data_kinds[index])));
  }
  target->insert(QStringLiteral("cameraProfileUsedForwardMatrix"),
                 profile.used_forward_matrix);
  target->insert(QStringLiteral("cameraProfileUsedCameraCalibration"),
                 profile.used_camera_calibration);
  target->insert(QStringLiteral("cameraProfileHasHueSatMap"), profile.has_hue_sat_map);
  target->insert(QStringLiteral("cameraProfileHasGainTableMap"),
                 profile.has_profile_gain_table_map);
  target->insert(QStringLiteral("cameraProfileHasRgbTables"), profile.has_rgb_tables);
  target->insert(QStringLiteral("cameraProfileHasLookTable"), profile.has_look_table);
  target->insert(QStringLiteral("cameraProfileHasToneCurve"),
                 profile.has_profile_tone_curve);
  target->insert(QStringLiteral("cameraProfileToneMethod"),
                 QString::fromStdString(profile.profile_tone_method));
  target->insert(QStringLiteral("cameraProfileDynamicRange"),
                 profile.profile_dynamic_range_hdr ? QStringLiteral("hdr")
                                                   : QStringLiteral("sdr"));
  target->insert(QStringLiteral("cameraProfileHintMaxOutputValue"),
                 profile.profile_hint_max_output_value);
  target->insert(QStringLiteral("cameraProfileBaselineExposureOffset"),
                 profile.baseline_exposure_offset);
  target->insert(
      QStringLiteral("cameraProfileAssetStatus"),
      QString::fromLatin1(
          photo_editor::pipeline::dcp_profile_asset_status_name(asset.status)));
  target->insert(QStringLiteral("cameraProfileAssetFailureReason"),
                 QString::fromStdString(asset.failure_reason));
}

inline void insert_decoded_camera_profile_provenance(
    QVariantMap* target,
    bool decoded_boundary_available,
    bool decoded_boundary_valid,
    const photo_editor::pipeline::DecodedImageMetadata& decoded,
    const photo_editor::pipeline::InputColorProfile& requested_profile,
    const photo_editor::pipeline::CameraProfile& resolved_profile) {
  if (target == nullptr) {
    return;
  }

  target->insert(QStringLiteral("decodedColorBoundaryAvailable"),
                 decoded_boundary_available);
  target->insert(QStringLiteral("decodedColorBoundaryValid"), decoded_boundary_valid);
  target->insert(
      QStringLiteral("decodedPixelEncoding"),
      QString::fromLatin1(
          photo_editor::pipeline::decoded_pixel_encoding_name(decoded.pixel_encoding)));
  target->insert(QStringLiteral("decoderBackend"),
                 QString::fromStdString(decoded.decoder_backend));
  target->insert(QStringLiteral("decodedColorSpace"),
                 QString::fromStdString(decoded.decoded_color_space_name));
  target->insert(QStringLiteral("cameraProfileIdentity"),
                 QString::fromStdString(decoded.camera_profile_identity));
  target->insert(QStringLiteral("requestedCameraProfile"),
                 QString::fromStdString(requested_profile.profile_name));
  target->insert(
      QStringLiteral("cameraProfileResolutionStatus"),
      resolved_profile.valid
          ? QStringLiteral("ready")
          : (resolved_profile.selection_source ==
                     photo_editor::pipeline::CameraProfileSelectionSource::
                         DecoderCharacterization
                 ? QStringLiteral("decoder-applied")
                 : QStringLiteral("unavailable")));
  target->insert(
      QStringLiteral("cameraProfileSelectionSource"),
      QString::fromLatin1(photo_editor::pipeline::camera_profile_selection_source_name(
          resolved_profile.selection_source)));
  target->insert(QStringLiteral("cameraProfileSelectionPrecedenceRank"),
                 resolved_profile.selection_precedence_rank);
  target->insert(QStringLiteral("cameraProfileFallbackUsed"),
                 resolved_profile.fallback_used);
  target->insert(QStringLiteral("cameraProfileResolutionFailureReason"),
                 QString::fromStdString(resolved_profile.failure_reason));
  target->insert(QStringLiteral("cameraProfilePayloadSha256"),
                 QString::fromStdString(resolved_profile.payload_sha256));
  target->insert(QStringLiteral("cameraProfileExecutionModel"),
                 QString::fromStdString(resolved_profile.execution_model));
}

inline void apply_icc_profile_assets(
    photo_editor::pipeline::ColorPipelineConfig* config,
    const EditRecipeState& state) {
  if (config == nullptr) {
    return;
  }
  config->input_icc_profile_asset = resolve_icc_profile_asset(state.input_icc_profile_asset);
  config->preview_icc_profile_asset = resolve_icc_profile_asset(state.preview_icc_profile_asset);
  config->proof_icc_profile_asset = resolve_icc_profile_asset(state.proof_icc_profile_asset);
  config->export_icc_profile_asset = resolve_icc_profile_asset(state.export_icc_profile_asset);
  config->camera_dcp_profile_asset = resolve_dcp_profile_asset(state.camera_dcp_profile_asset);
}

}  // namespace photo_editor::desktop
