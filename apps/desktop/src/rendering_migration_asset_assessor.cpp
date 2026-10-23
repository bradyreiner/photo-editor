#include "rendering_migration_asset_assessor.h"

#include "render_plan_store.h"
#include "catalog_recipe_projection.h"
#include "color_profile_asset_bridge.h"
#include "edit_adjustment_patch.h"
#include "edit_recipe_mask_schema.h"
#include "hdr_avif_image_encoder.h"
#include "photo_render_utils.h"
#include "render_executor_service.h"
#include "rendering_migration_evidence.h"
#include "working_image.h"

#include <array>

#include <QColorSpace>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

namespace photo_editor::desktop {
namespace {

struct MigrationExportCacheScope final {
  MigrationExportCacheScope() {
    export_render_executor().clear_cache();
  }

  ~MigrationExportCacheScope() {
    export_render_executor().clear_cache();
  }
};

void release_migration_export_cache() {
  export_render_executor().clear_cache();
}

void release_migration_evidence_artifact_bytes(
    RenderingMigrationEvidence* evidence) {
  if (evidence == nullptr) {
    return;
  }
  evidence->candidate_export_png = {};
  evidence->reference_export_png = {};
  evidence->delta_e00_heatmap_png = {};
  evidence->candidate_shipping_format_bytes.clear();
  evidence->reference_shipping_format_bytes.clear();
  evidence->candidate_target_png_bytes.clear();
  evidence->reference_target_png_bytes.clear();
  evidence->candidate_attribution_png_bytes.clear();
  evidence->reference_attribution_png_bytes.clear();
}

[[nodiscard]] bool persist_migration_evidence_stage(
    const QString& artifact_root,
    RenderingMigrationEvidence* evidence,
    QStringList* artifact_file_names,
    QString* error) {
  if (evidence == nullptr || !evidence->valid) {
    if (error != nullptr) {
      *error = QStringLiteral(
          "valid migration evidence is required before artifact persistence");
    }
    return false;
  }
  if (!artifact_root.trimmed().isEmpty() &&
      !write_immutable_rendering_migration_evidence_artifacts(
          artifact_root,
          *evidence,
          artifact_file_names,
          error)) {
    return false;
  }
  release_migration_evidence_artifact_bytes(evidence);
  return true;
}

[[nodiscard]] QStringList layer_selection_mask_set_ids(
    const QVariantMap& layer) {
  QStringList ids;
  const auto append = [&ids](const QString& value) {
    const auto normalized = value.simplified();
    if (!normalized.isEmpty() && !ids.contains(normalized)) {
      ids.append(normalized);
    }
  };
  const auto values =
      layer.value(QStringLiteral("selectionMaskSetIds"));
  if (values.canConvert<QStringList>()) {
    for (const auto& value : values.toStringList()) {
      append(value);
    }
  } else {
    for (const auto& value : values.toList()) {
      append(value.toString());
    }
  }
  append(
      layer.value(QStringLiteral("selectionMaskSetId"))
          .toString());
  return ids;
}

void apply_candidate_structural_migrations(
    EditRecipeState* state,
    QVariantList* migrations) {
  if (state == nullptr) {
    return;
  }
  auto masks = normalize_mask_stack(state->mask_stack);
  QSet<QString> valid_mask_set_ids;
  for (qsizetype index = 0; index < masks.size(); ++index) {
    auto mask = masks.at(index).toMap();
    const auto source_id =
        mask.value(QStringLiteral("id")).toString().simplified();
    auto mask_set_id =
        mask.value(QStringLiteral("maskSetId"))
            .toString()
            .simplified();
    if (mask_set_id.isEmpty() && !source_id.isEmpty()) {
      mask_set_id = source_id + QStringLiteral("-set");
      mask.insert(QStringLiteral("maskSetId"), mask_set_id);
    }
    if (!mask_set_id.isEmpty()) {
      valid_mask_set_ids.insert(mask_set_id);
    }
    if (mask.value(QStringLiteral("kind"))
            .toString()
            .trimmed()
            .compare(
                QStringLiteral("ellipse"),
                Qt::CaseInsensitive) == 0) {
      mask.insert(QStringLiteral("kind"), QStringLiteral("radial"));
      if (migrations != nullptr) {
        migrations->append(
            QVariantMap{
                {QStringLiteral("field"),
                 QStringLiteral("masks.%1.kind").arg(source_id)},
                {QStringLiteral("sourceValue"),
                 QStringLiteral("ellipse")},
                {QStringLiteral("candidateValue"),
                 QStringLiteral("radial")},
                {QStringLiteral("classification"),
                 QStringLiteral("legacy-geometry-synonym")},
                {QStringLiteral("reason"),
                 QStringLiteral(
                     "v13 names the legacy ellipse geometry radial")},
            });
      }
    }
    masks[index] = mask;
  }
  state->mask_stack = masks;

  auto layers = state->layers;
  for (qsizetype index = 0; index < layers.size(); ++index) {
    auto layer = layers.at(index).toMap();
    const auto source_ids = layer_selection_mask_set_ids(layer);
    if (source_ids.isEmpty()) {
      continue;
    }
    QStringList retained_ids;
    QStringList missing_ids;
    for (const auto& id : source_ids) {
      if (valid_mask_set_ids.contains(id)) {
        retained_ids.append(id);
      } else {
        missing_ids.append(id);
      }
    }
    if (missing_ids.isEmpty()) {
      continue;
    }
    if (retained_ids.isEmpty()) {
      layer.insert(QStringLiteral("selectionScoped"), false);
      layer.remove(QStringLiteral("selectionMaskSetId"));
      layer.remove(QStringLiteral("selectionMaskSetIds"));
      layer.remove(QStringLiteral("selectionMaskSetName"));
      layer.remove(QStringLiteral("selectionMaskSetNames"));
      layer.insert(QStringLiteral("selectionMaskSetCount"), 0);
    } else {
      layer.insert(
          QStringLiteral("selectionMaskSetId"),
          retained_ids.front());
      layer.insert(
          QStringLiteral("selectionMaskSetIds"),
          retained_ids);
      layer.insert(
          QStringLiteral("selectionMaskSetCount"),
          retained_ids.size());
    }
    if (migrations != nullptr) {
      const auto layer_id =
          layer.value(QStringLiteral("id")).toString().simplified();
      migrations->append(
          QVariantMap{
              {QStringLiteral("field"),
               QStringLiteral("layers.%1.selectionMaskSetIds")
                   .arg(layer_id)},
              {QStringLiteral("sourceValue"), source_ids},
              {QStringLiteral("candidateValue"), retained_ids},
              {QStringLiteral("missingMaskSetIds"), missing_ids},
              {QStringLiteral("classification"),
               QStringLiteral("dangling-reference-normalization")},
              {QStringLiteral("reason"),
               QStringLiteral(
                   "the compatibility renderer applies no mask when the referenced set does not exist")},
          });
    }
    layers[index] = layer;
  }
  state->layers = layers;
}

[[nodiscard]] QJsonValue legacy_recipe_value(
    const QVariantMap& inventory_asset) {
  const auto value =
      inventory_asset.value(
          QStringLiteral("legacy_recipe"));
  return value.canConvert<QVariantMap>()
             ? QJsonObject::fromVariantMap(
                   value.toMap())
             : QJsonValue{};
}

[[nodiscard]] QJsonValue canonical_recipe_value(
    const QVariantMap& inventory_asset) {
  const auto payload =
      inventory_asset
          .value(QStringLiteral(
              "canonical_recipe_payload"))
          .toString();
  const auto cache_key =
      inventory_asset
          .value(QStringLiteral(
              "canonical_cache_key"))
          .toString();
  const auto provenance_payload =
      inventory_asset
          .value(QStringLiteral(
              "canonical_provenance_payload"))
          .toString();
  if (payload.isEmpty() && cache_key.isEmpty() &&
      provenance_payload.isEmpty()) {
    return {};
  }
  return QJsonObject{
      {QStringLiteral("recipe_payload"), payload},
      {QStringLiteral("cache_key"), cache_key},
      {QStringLiteral("provenance_payload"),
       provenance_payload},
  };
}

[[nodiscard]] QStringList preset_identities(
    const EditRecipeState& state) {
  QStringList identities;
  for (const auto& value : state.preset_applications) {
    const auto preset = value.toMap();
    auto identity =
        preset.value(QStringLiteral("identity"))
            .toString();
    if (identity.isEmpty()) {
      identity =
          preset.value(QStringLiteral("presetId"))
              .toString();
    }
    if (identity.isEmpty()) {
      identity =
          preset.value(QStringLiteral("name"))
              .toString();
    }
    if (!identity.isEmpty() &&
        !identities.contains(identity)) {
      identities.append(identity);
    }
  }
  identities.sort();
  return identities;
}

[[nodiscard]] QString profile_identity(
    const QString& slot,
    const QString& asset_id,
    const QString& payload_sha256,
    const QString& source_uri) {
  auto identity = asset_id.trimmed();
  if (identity.isEmpty() &&
      !payload_sha256.trimmed().isEmpty()) {
    identity = QStringLiteral("sha256:%1")
                   .arg(payload_sha256.trimmed().toLower());
  }
  if (identity.isEmpty()) {
    identity = source_uri.trimmed();
  }
  return QStringLiteral("%1:%2")
      .arg(slot, identity);
}

void collect_profile_identities(
    const EditRecipeState& state,
    QStringList* configured,
    QStringList* unsupported) {
  const auto collect_icc =
      [configured, unsupported](
          const QString& slot,
          const IccProfileAssetState& profile) {
        if (!profile.configured()) {
          return;
        }
        const auto identity = profile_identity(
            slot,
            profile.asset_id,
            profile.payload_sha256,
            profile.source_uri);
        configured->append(identity);
        if (!resolve_icc_profile_asset(profile).ready()) {
          unsupported->append(identity);
        }
      };
  if (state.camera_dcp_profile_asset.configured()) {
    const auto identity = profile_identity(
        QStringLiteral("camera-dcp"),
        state.camera_dcp_profile_asset.asset_id,
        state.camera_dcp_profile_asset.payload_sha256,
        state.camera_dcp_profile_asset.source_uri);
    configured->append(identity);
    if (!resolve_dcp_profile_asset(
             state.camera_dcp_profile_asset)
             .ready()) {
      unsupported->append(identity);
    }
  }
  collect_icc(
      QStringLiteral("input-icc"),
      state.input_icc_profile_asset);
  collect_icc(
      QStringLiteral("preview-icc"),
      state.preview_icc_profile_asset);
  collect_icc(
      QStringLiteral("proof-icc"),
      state.proof_icc_profile_asset);
  collect_icc(
      QStringLiteral("export-icc"),
      state.export_icc_profile_asset);
  configured->removeDuplicates();
  configured->sort();
  unsupported->removeDuplicates();
  unsupported->sort();
}

[[nodiscard]] std::optional<EditRecipeState>
state_without_recorded_presets(
    const EditRecipeState& state,
    QString* failure_reason) {
  auto restored = state;
  for (auto index = state.preset_applications.size();
       index-- > 0;) {
    const auto application =
        state.preset_applications.at(index).toMap();
    const auto before_json =
        application
            .value(QStringLiteral("beforeAdjustmentPatchJson"))
            .toString();
    const auto after_json =
        application
            .value(QStringLiteral("afterAdjustmentPatchJson"))
            .toString();
    const auto patch_hash = [](const QString& patch) {
      return QString::fromLatin1(
          QCryptographicHash::hash(
              patch.toUtf8(),
              QCryptographicHash::Sha256)
              .toHex());
    };
    const auto before = parse_adjustment_patch_json(
        before_json);
    const auto after = parse_adjustment_patch_json(
        after_json);
    if (!before.ok() || !after.ok() ||
        application
                .value(QStringLiteral(
                    "beforeAdjustmentPatchSha256"))
                .toString() != patch_hash(before_json) ||
        application
                .value(QStringLiteral(
                    "afterAdjustmentPatchSha256"))
                .toString() != patch_hash(after_json) ||
        before.scope != AdjustmentPatchScope::AssetBound ||
        after.scope != AdjustmentPatchScope::AssetBound ||
        before.groups != after.groups ||
        before.groups.isEmpty()) {
      if (failure_reason != nullptr) {
        *failure_reason = QStringLiteral(
            "preset application lacks reversible asset-bound before/after patches");
      }
      return std::nullopt;
    }
    const auto expected_current =
        merge_edit_recipe_state_by_groups(
            restored, *after.state, after.groups);
    if (!(expected_current == restored)) {
      if (failure_reason != nullptr) {
        *failure_reason = QStringLiteral(
            "preset application after-state does not match the current recipe");
      }
      return std::nullopt;
    }
    restored = merge_edit_recipe_state_by_groups(
        restored, *before.state, before.groups);
  }
  restored.preset_applications.clear();
  return restored;
}

void remove_all_masks(EditRecipeState* state) {
  state->mask_stack.clear();
  state->active_mask_index = -1;
  state->linear_mask_enabled = false;
  state->radial_mask_enabled = false;
}

}  // namespace

RenderingMigrationBatchAsset
assess_rendering_migration_inventory_asset(
    const QVariantMap& inventory_asset,
    const QString& recipe_asset_root,
    int maximum_decode_edge,
    const QString& evidence_artifact_root) {
  const MigrationExportCacheScope export_cache_scope;
  RenderingMigrationBatchAsset result;
  result.asset_id =
      inventory_asset
          .value(QStringLiteral("asset_id"))
          .toString()
          .trimmed();
  result.source_path =
      inventory_asset
          .value(QStringLiteral("source_path"))
          .toString();
  result.edited = true;
  result.original_available =
      inventory_asset
          .value(QStringLiteral("source_available"))
          .toBool();
  if (result.asset_id.isEmpty() ||
      result.source_path.isEmpty() ||
      !result.original_available) {
    result.candidate_error =
        QStringLiteral("migration asset source is unavailable");
    return result;
  }

  const auto projection =
      project_catalog_adjustments_for_desktop(
          legacy_recipe_value(inventory_asset),
          canonical_recipe_value(inventory_asset),
          recipe_asset_root);
  if (!projection.ok() ||
      projection.adjustment_patch_json.isEmpty()) {
    result.candidate_error =
        projection.error.isEmpty()
            ? QStringLiteral(
                  "migration asset has no recoverable edit state")
            : projection.error;
    return result;
  }
  const auto decoded_adjustments =
      parse_adjustment_patch_json(
          projection.adjustment_patch_json);
  if (!decoded_adjustments.ok()) {
    result.candidate_error =
        decoded_adjustments.error;
    return result;
  }
  const auto reference_state =
      *decoded_adjustments.state;
  result.preset_identities =
      preset_identities(reference_state);
  result.migration_required =
      reference_state.photographic_rendering_model !=
      QStringLiteral("aces2-ocio-v1");
  if (!result.migration_required) {
    result.candidate_compiled = true;
    return result;
  }
  QStringList profile_identities;
  collect_profile_identities(
      reference_state,
      &profile_identities,
      &result.unsupported_profile_identities);
  if (!result.unsupported_profile_identities.isEmpty()) {
    result.candidate_error =
        QStringLiteral(
            "migration profile assets are unavailable or unsupported");
    return result;
  }

  QImage decoded_image;
  photo_editor::pipeline::DecodedImageMetadata metadata;
  if (!decode_source_image(
          result.source_path,
          &decoded_image,
          maximum_decode_edge,
          reference_state,
          &metadata) ||
      decoded_image.isNull()) {
    result.candidate_error =
        QStringLiteral("migration source decode failed");
    return result;
  }
  RenderBackendTelemetry reference_telemetry;
  auto reference = render_output_image(
      decoded_image,
      reference_state,
      QColorSpace(QColorSpace::SRgb),
      QImage::Format_ARGB32,
      &metadata,
      &reference_telemetry);
  release_migration_export_cache();
  auto candidate_state = reference_state;
  candidate_state.photographic_rendering_model =
      QStringLiteral("aces2-ocio-v1");
  candidate_state.renderer_transition_look =
      QStringLiteral("preserve-legacy-direct-luminance-v1");
  const auto renderer_semantic_migrations = QVariantList{
      QVariantMap{
          {QStringLiteral("field"),
           QStringLiteral("output.rendererTransitionLook")},
          {QStringLiteral("sourceValue"), QStringLiteral("none")},
          {QStringLiteral("candidateValue"),
           QStringLiteral("preserve-legacy-direct-luminance-v1")},
          {QStringLiteral("classification"),
           QStringLiteral("measured-renderer-transition")},
          {QStringLiteral("reason"),
           QStringLiteral(
               "preserve legacy direct-linear luminance intent before the official ACES 2 output transform")},
      },
  };
  QVariantList input_semantic_migrations;
  if (candidate_state.raw_gamut_mapping) {
    candidate_state.raw_gamut_mapping = false;
    input_semantic_migrations.append(
        QVariantMap{
            {QStringLiteral("field"),
             QStringLiteral("input.rawGamutMapping")},
            {QStringLiteral("sourceValue"), true},
            {QStringLiteral("candidateValue"), false},
            {QStringLiteral("classification"),
             QStringLiteral("retired-no-op-control")},
            {QStringLiteral("reason"),
             QStringLiteral(
                 "the legacy flag had no executable pixel operator")},
        });
  }
  const auto source_highlight_reconstruction_model =
      normalized_highlight_reconstruction_model_name(
          candidate_state.highlight_reconstruction_model);
  if (source_highlight_reconstruction_model !=
      QStringLiteral("camera-linear-opposed-v2")) {
    candidate_state.highlight_reconstruction_model =
        QStringLiteral("camera-linear-opposed-v2");
    input_semantic_migrations.append(
        QVariantMap{
            {QStringLiteral("field"),
             QStringLiteral(
                 "input.highlightReconstructionModel")},
            {QStringLiteral("sourceValue"),
             source_highlight_reconstruction_model},
            {QStringLiteral("candidateValue"),
             QStringLiteral("camera-linear-opposed-v2")},
            {QStringLiteral("classification"),
             QStringLiteral("measured-semantic-upgrade")},
            {QStringLiteral("reason"),
             QStringLiteral(
                 "v13 reconstructs highlights in the camera-linear domain")},
        });
  }
  QVariantList structural_semantic_migrations;
  apply_candidate_structural_migrations(
      &candidate_state,
      &structural_semantic_migrations);
  const auto candidate_admission =
      RenderPlanStore::shared().admit(
          result.asset_id,
          result.source_path,
          candidate_state,
          decoded_image.size(),
          photo_editor::pipeline::RenderPurpose::Export,
          photo_editor::pipeline::DisplayRenderingTarget::SrgbSimulation,
          std::nullopt,
          &metadata);
  if (!candidate_admission.admitted ||
      !candidate_admission.plan.has_value() ||
      candidate_admission.plan_cache_key.trimmed().isEmpty()) {
    result.candidate_error =
        candidate_admission.error.isEmpty()
            ? QStringLiteral(
                  "migration candidate canonical plan compilation failed")
            : candidate_admission.error;
    return result;
  }
  RenderBackendTelemetry candidate_telemetry;
  auto candidate = render_output_image(
      decoded_image,
      candidate_state,
      QColorSpace(QColorSpace::SRgb),
      QImage::Format_ARGB32,
      &metadata,
      &candidate_telemetry,
      {},
      photo_editor::pipeline::RemovalQuality::Final,
      RenderOutputPolicy::DisplayReferred,
      &*candidate_admission.plan);
  release_migration_export_cache();
  if (reference.isNull() || candidate.isNull()) {
    result.candidate_error =
        candidate_telemetry.canonical_plan_error.isEmpty()
            ? QStringLiteral(
                  "migration candidate render failed")
            : candidate_telemetry.canonical_plan_error;
    return result;
  }
  const auto& plan_key =
      candidate_admission.plan_cache_key;
  const auto source_patch =
      serialize_adjustment_patch_json(
          reference_state,
          all_adjustment_groups(),
          AdjustmentPatchScope::AssetBound);
  auto evidence =
      build_rendering_migration_evidence(
          candidate,
          reference,
          candidate_state.photographic_rendering_model,
          reference_state.photographic_rendering_model,
          plan_key,
          source_patch);
  candidate = {};
  reference = {};
  if (!evidence.valid) {
    result.candidate_error = evidence.error;
    return result;
  }
  if (!persist_migration_evidence_stage(
          evidence_artifact_root,
          &evidence,
          nullptr,
          &result.candidate_error)) {
    return result;
  }
  {
    const auto p3_reference = render_output_image(
        decoded_image,
        reference_state,
        QColorSpace(QColorSpace::DisplayP3),
        QImage::Format_ARGB32,
        &metadata);
    release_migration_export_cache();
    const auto p3_admission =
        RenderPlanStore::shared().admit(
            result.asset_id,
            result.source_path,
            candidate_state,
            decoded_image.size(),
            photo_editor::pipeline::RenderPurpose::Export,
            photo_editor::pipeline::DisplayRenderingTarget::
                DisplayP3Simulation,
            std::nullopt,
            &metadata);
    if (!p3_admission.admitted ||
        !p3_admission.plan.has_value() ||
        p3_admission.plan_cache_key.trimmed().isEmpty()) {
      result.candidate_error =
          p3_admission.error.isEmpty()
              ? QStringLiteral(
                    "Display P3 migration candidate plan compilation failed")
              : p3_admission.error;
      return result;
    }
    RenderBackendTelemetry p3_candidate_telemetry;
    const auto p3_candidate = render_output_image(
        decoded_image,
        candidate_state,
        QColorSpace(QColorSpace::DisplayP3),
        QImage::Format_ARGB32,
        &metadata,
        &p3_candidate_telemetry,
        {},
        photo_editor::pipeline::RemovalQuality::Final,
        RenderOutputPolicy::DisplayReferred,
        &*p3_admission.plan);
    release_migration_export_cache();
    if (p3_reference.isNull() || p3_candidate.isNull() ||
        !add_rendering_migration_target_comparison(
            &evidence,
            QStringLiteral("display-p3-simulation"),
            QStringLiteral("Display P3"),
            p3_admission.plan_cache_key,
            p3_candidate,
            p3_reference,
            &result.candidate_error)) {
      if (result.candidate_error.isEmpty()) {
        result.candidate_error =
            p3_candidate_telemetry.canonical_plan_error.isEmpty()
                ? QStringLiteral(
                      "Display P3 migration target comparison failed")
                : p3_candidate_telemetry.canonical_plan_error;
      }
      return result;
    }
    if (!persist_migration_evidence_stage(
            evidence_artifact_root,
            &evidence,
            nullptr,
            &result.candidate_error)) {
      return result;
    }
  }
  const auto hdr_targets = std::array{
      std::pair{
          QStringLiteral("rec2100-pq"),
          QStringLiteral("HDR PQ AVIF")},
      std::pair{
          QStringLiteral("rec2100-hlg"),
          QStringLiteral("HDR HLG AVIF")},
  };
  const auto linear_rec2020 = linear_rec2020_color_space();
  for (const auto& [target_identity, export_preset] : hdr_targets) {
    const auto settings =
        export_image_settings_for_preset(export_preset);
    const auto display_target =
        hdr_display_target_for_export_settings(settings);
    const auto rendering_target =
        settings.hdr_transfer.compare(
            QStringLiteral("HLG"), Qt::CaseInsensitive) == 0
            ? photo_editor::pipeline::DisplayRenderingTarget::HdrHlg
            : photo_editor::pipeline::DisplayRenderingTarget::HdrPq;
    const auto admission =
        RenderPlanStore::shared().admit(
            result.asset_id,
            result.source_path,
            candidate_state,
            decoded_image.size(),
            photo_editor::pipeline::RenderPurpose::Export,
            rendering_target,
            std::nullopt,
            &metadata);
    if (!admission.admitted || !admission.plan.has_value() ||
        admission.plan_cache_key.trimmed().isEmpty()) {
      result.candidate_error =
          admission.error.isEmpty()
              ? QStringLiteral(
                    "%1 migration candidate plan compilation failed")
                    .arg(target_identity)
              : admission.error;
      return result;
    }
    const auto hdr_reference =
        render_output_image_for_display_target(
            decoded_image,
            reference_state,
            linear_rec2020,
            QImage::Format_RGBA32FPx4,
            display_target,
            &metadata);
    release_migration_export_cache();
    RenderBackendTelemetry hdr_candidate_telemetry;
    const auto hdr_candidate =
        render_output_image_for_display_target(
            decoded_image,
            candidate_state,
            linear_rec2020,
            QImage::Format_RGBA32FPx4,
            display_target,
            &metadata,
            &hdr_candidate_telemetry,
            {},
            photo_editor::pipeline::RemovalQuality::Final,
            &*admission.plan);
    release_migration_export_cache();
    if (hdr_reference.isNull() || hdr_candidate.isNull() ||
        !add_rendering_migration_hdr_avif_comparison(
            &evidence,
            target_identity,
            admission.plan_cache_key,
            export_preset,
            hdr_candidate,
            hdr_reference,
            &result.candidate_error)) {
      if (result.candidate_error.isEmpty()) {
        result.candidate_error =
            hdr_candidate_telemetry.canonical_plan_error.isEmpty()
                ? QStringLiteral(
                      "%1 migration target comparison failed")
                      .arg(target_identity)
                : hdr_candidate_telemetry.canonical_plan_error;
      }
      return result;
    }
    if (!persist_migration_evidence_stage(
            evidence_artifact_root,
            &evidence,
            nullptr,
            &result.candidate_error)) {
      return result;
    }
  }
  const auto add_structure_attribution =
      [&](const QString& identity,
          int removed_count,
          const EditRecipeState& attribution_reference_state) {
        auto attribution_candidate_state =
            attribution_reference_state;
        attribution_candidate_state.photographic_rendering_model =
            QStringLiteral("aces2-ocio-v1");
        attribution_candidate_state.renderer_transition_look =
            QStringLiteral("preserve-legacy-direct-luminance-v1");
        attribution_candidate_state.raw_gamut_mapping = false;
        attribution_candidate_state.highlight_reconstruction_model =
            QStringLiteral("camera-linear-opposed-v2");
        apply_candidate_structural_migrations(
            &attribution_candidate_state,
            nullptr);
        const auto attribution_recipe_digest =
            QString::fromLatin1(
                QCryptographicHash::hash(
                    QByteArray("migration-attribution-v2:") +
                        source_patch.toUtf8() + ':' +
                        identity.toUtf8(),
                    QCryptographicHash::Sha256)
                    .toHex());
        const auto admission =
            RenderPlanStore::shared().admit(
                result.asset_id,
                result.source_path,
                attribution_candidate_state,
                decoded_image.size(),
                photo_editor::pipeline::RenderPurpose::Export,
                photo_editor::pipeline::DisplayRenderingTarget::
                    SrgbSimulation,
                std::nullopt,
                &metadata,
                attribution_recipe_digest);
        if (!admission.admitted ||
            !admission.plan.has_value() ||
            admission.plan_cache_key.trimmed().isEmpty()) {
          return add_unavailable_rendering_migration_structure_attribution(
              &evidence,
              identity,
              removed_count,
              admission.error.isEmpty()
                  ? QStringLiteral(
                        "isolated canonical plan compilation failed")
                  : admission.error,
              &result.candidate_error);
        }
        const auto attribution_reference = render_output_image(
            decoded_image,
            attribution_reference_state,
            QColorSpace(QColorSpace::SRgb),
            QImage::Format_ARGB32,
            &metadata);
        release_migration_export_cache();
        RenderBackendTelemetry attribution_telemetry;
        const auto attribution_candidate = render_output_image(
            decoded_image,
            attribution_candidate_state,
            QColorSpace(QColorSpace::SRgb),
            QImage::Format_ARGB32,
            &metadata,
            &attribution_telemetry,
            {},
            photo_editor::pipeline::RemovalQuality::Final,
            RenderOutputPolicy::DisplayReferred,
            &*admission.plan);
        release_migration_export_cache();
        if (attribution_reference.isNull() ||
            attribution_candidate.isNull()) {
          return add_unavailable_rendering_migration_structure_attribution(
              &evidence,
              identity,
              removed_count,
              attribution_telemetry.canonical_plan_error.isEmpty()
                  ? QStringLiteral("isolated structure render failed")
                  : attribution_telemetry.canonical_plan_error,
              &result.candidate_error);
        }
        if (!add_rendering_migration_structure_attribution(
                &evidence,
                identity,
                removed_count,
                admission.plan_cache_key,
                attribution_candidate,
                attribution_reference,
                &result.candidate_error)) {
          return false;
        }
        return persist_migration_evidence_stage(
            evidence_artifact_root,
            &evidence,
            nullptr,
            &result.candidate_error);
      };
  const auto mask_count =
      !reference_state.mask_stack.isEmpty()
          ? static_cast<int>(
                reference_state.mask_stack.size())
          : (reference_state.linear_mask_enabled ? 1 : 0) +
                (reference_state.radial_mask_enabled ? 1 : 0);
  const auto layer_count = static_cast<int>(
      reference_state.layers.size());
  const auto preset_count = static_cast<int>(
      reference_state.preset_applications.size());
  if (mask_count > 0) {
    auto without_masks = reference_state;
    remove_all_masks(&without_masks);
    if (!add_structure_attribution(
            QStringLiteral("masks"),
            mask_count,
            without_masks)) {
      return result;
    }
  }
  if (layer_count > 0) {
    auto without_layers = reference_state;
    without_layers.layers.clear();
    if (!add_structure_attribution(
            QStringLiteral("layers"),
            layer_count,
            without_layers)) {
      return result;
    }
  }
  if (preset_count > 0) {
    QString preset_failure_reason;
    const auto without_presets =
        state_without_recorded_presets(
            reference_state,
            &preset_failure_reason);
    if (without_presets.has_value()) {
      if (!add_structure_attribution(
              QStringLiteral("presets"),
              preset_count,
              *without_presets)) {
        return result;
      }
    } else if (!add_unavailable_rendering_migration_structure_attribution(
                   &evidence,
                   QStringLiteral("presets"),
                   preset_count,
                   preset_failure_reason,
                   &result.candidate_error)) {
      return result;
    }
  }
  if (!bind_rendering_migration_evidence_context(
          &evidence,
          QVariantMap{
              {QStringLiteral("previewOutputMode"),
               reference_state.preview_output_mode},
              {QStringLiteral("previewColorSpace"),
               reference_state.preview_color_space},
              {QStringLiteral("workingColorProfile"),
               reference_state.working_color_profile},
              {QStringLiteral("outputColorSpace"),
               reference_state.output_color_space},
              {QStringLiteral("softProofEnabled"),
               reference_state.soft_proof_enabled},
              {QStringLiteral("softProofProfile"),
               reference_state.soft_proof_profile},
              {QStringLiteral("maskCount"),
               mask_count},
              {QStringLiteral("layerCount"),
               reference_state.layers.size()},
              {QStringLiteral("presetIdentities"),
               result.preset_identities},
              {QStringLiteral("profileIdentities"),
               profile_identities},
              {QStringLiteral("inputSemanticMigrations"),
               input_semantic_migrations},
              {QStringLiteral("rendererSemanticMigrations"),
               renderer_semantic_migrations},
              {QStringLiteral("structuralSemanticMigrations"),
               structural_semantic_migrations},
          },
          &result.candidate_error)) {
    return result;
  }
  if (!persist_migration_evidence_stage(
          evidence_artifact_root,
          &evidence,
          &result.evidence_artifact_file_names,
          &result.candidate_error)) {
    return result;
  }
  result.candidate_compiled = true;
  result.evidence = evidence.metrics;
  return result;
}

}  // namespace photo_editor::desktop
