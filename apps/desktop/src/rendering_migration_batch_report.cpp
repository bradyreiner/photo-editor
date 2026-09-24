#include "rendering_migration_batch_report.h"

#include "rendering_migration_evidence.h"

#include <algorithm>
#include <cmath>
#include <ranges>

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>
#include <QUuid>
#include <QVariantList>

namespace photo_editor::desktop {
namespace {

[[nodiscard]] bool valid_thresholds(
    const RenderingMigrationBatchThresholds& thresholds) {
  return std::isfinite(thresholds.p95_delta_e00) &&
         thresholds.p95_delta_e00 >= 0.0 &&
         std::isfinite(thresholds.maximum_delta_e00) &&
         thresholds.maximum_delta_e00 >=
             thresholds.p95_delta_e00 &&
         thresholds.maximum_added_shadow_clips >= 0 &&
         thresholds.maximum_added_highlight_clips >= 0;
}

[[nodiscard]] bool evidence_identity_is_valid(
    const QVariantMap& evidence) {
  if (evidence.value(QStringLiteral("schema")).toString() !=
          QStringLiteral(
              "photo-editor-rendering-migration-evidence:v2") ||
      evidence.value(QStringLiteral("sampleCount"))
              .toULongLong() == 0) {
    return false;
  }
  const auto expected =
      evidence.value(QStringLiteral("evidenceSha256"))
          .toString();
  if (expected.size() != 64) {
    return false;
  }
  const auto actual = QString::fromLatin1(
      QCryptographicHash::hash(
          canonical_rendering_migration_evidence_payload(
              evidence),
          QCryptographicHash::Sha256)
          .toHex());
  return actual == expected;
}

void append_unique(
    QStringList* values,
    const QString& value) {
  if (!value.isEmpty() && !values->contains(value)) {
    values->append(value);
  }
}

[[nodiscard]] QStringList normalized_identities(
    QStringList values) {
  values.removeAll(QString{});
  values.removeDuplicates();
  values.sort();
  return values;
}

[[nodiscard]] QStringList expected_artifact_file_names(
    const QVariantMap& evidence) {
  const auto candidate_hash =
      evidence
          .value(QStringLiteral("candidateEncodedPngSha256"))
          .toString();
  const auto reference_hash =
      evidence
          .value(QStringLiteral("referenceEncodedPngSha256"))
          .toString();
  const auto heatmap_hash =
      evidence
          .value(QStringLiteral("deltaE00HeatmapPngSha256"))
          .toString();
  if (candidate_hash.size() != 64 ||
      reference_hash.size() != 64 ||
      heatmap_hash.size() != 64) {
    return {};
  }
  auto names = QStringList{
      QStringLiteral("candidate-export-sha256-%1.png")
          .arg(candidate_hash),
      QStringLiteral("reference-export-sha256-%1.png")
          .arg(reference_hash),
      QStringLiteral("delta-e00-heatmap-sha256-%1.png")
          .arg(heatmap_hash),
  };
  const auto shipping_formats =
      evidence
          .value(QStringLiteral("shippingFormatEvidence"))
          .toMap();
  for (auto iterator = shipping_formats.cbegin();
       iterator != shipping_formats.cend();
       ++iterator) {
    const auto format = iterator.value().toMap();
    if (!format.value(QStringLiteral("available"))
             .toBool()) {
      continue;
    }
    const auto extension =
        format.value(QStringLiteral("writerFormat"))
            .toString();
    const auto candidate_shipping_hash =
        format.value(QStringLiteral("candidateSha256"))
            .toString();
    const auto reference_shipping_hash =
        format.value(QStringLiteral("referenceSha256"))
            .toString();
    if (extension.isEmpty() ||
        candidate_shipping_hash.size() != 64 ||
        reference_shipping_hash.size() != 64) {
      return {};
    }
    names.append(
        QStringLiteral("candidate-%1-sha256-%2.%3")
            .arg(
                iterator.key(),
                candidate_shipping_hash,
                extension));
    names.append(
        QStringLiteral("reference-%1-sha256-%2.%3")
            .arg(
                iterator.key(),
                reference_shipping_hash,
                extension));
  }
  const auto target_comparisons =
      evidence
          .value(QStringLiteral("targetComparisonEvidence"))
          .toMap();
  for (auto iterator = target_comparisons.cbegin();
       iterator != target_comparisons.cend();
       ++iterator) {
    const auto target = iterator.value().toMap();
    if (target.value(QStringLiteral("artifactKind"))
            .toString() != QStringLiteral("dedicated")) {
      continue;
    }
    const auto candidate_target_hash =
        target.value(QStringLiteral("candidateSha256"))
            .toString();
    const auto reference_target_hash =
        target.value(QStringLiteral("referenceSha256"))
            .toString();
    if (candidate_target_hash.size() != 64 ||
        reference_target_hash.size() != 64) {
      return {};
    }
    names.append(
        QStringLiteral("candidate-target-%1-sha256-%2.png")
            .arg(iterator.key(), candidate_target_hash));
    names.append(
        QStringLiteral("reference-target-%1-sha256-%2.png")
            .arg(iterator.key(), reference_target_hash));
  }
  const auto structure_attributions =
      evidence
          .value(QStringLiteral("structureAttributionEvidence"))
          .toMap();
  for (auto iterator = structure_attributions.cbegin();
       iterator != structure_attributions.cend();
       ++iterator) {
    const auto attribution = iterator.value().toMap();
    if (!attribution.value(QStringLiteral("available"))
             .toBool()) {
      continue;
    }
    const auto candidate_attribution_hash =
        attribution.value(QStringLiteral("candidateSha256"))
            .toString();
    const auto reference_attribution_hash =
        attribution.value(QStringLiteral("referenceSha256"))
            .toString();
    if (candidate_attribution_hash.size() != 64 ||
        reference_attribution_hash.size() != 64) {
      return {};
    }
    names.append(
        QStringLiteral("candidate-without-%1-sha256-%2.png")
            .arg(iterator.key(), candidate_attribution_hash));
    names.append(
        QStringLiteral("reference-without-%1-sha256-%2.png")
            .arg(iterator.key(), reference_attribution_hash));
  }
  names.sort();
  return names;
}

[[nodiscard]] QVariantMap thresholds_document(
    const RenderingMigrationBatchThresholds& thresholds) {
  return QVariantMap{
      {QStringLiteral("p95DeltaE00"),
       thresholds.p95_delta_e00},
      {QStringLiteral("maximumDeltaE00"),
       thresholds.maximum_delta_e00},
      {QStringLiteral("maximumAddedShadowClips"),
       thresholds.maximum_added_shadow_clips},
      {QStringLiteral("maximumAddedHighlightClips"),
       thresholds.maximum_added_highlight_clips},
  };
}

[[nodiscard]] QStringList string_list(
    const QVariant& value) {
  QStringList result;
  const auto append = [&result](const QString& item) {
    const auto normalized = item.trimmed();
    if (!normalized.isEmpty() && !result.contains(normalized)) {
      result.append(normalized);
    }
  };
  if (value.canConvert<QStringList>()) {
    for (const auto& item : value.toStringList()) {
      append(item);
    }
  } else {
    for (const auto& item : value.toList()) {
      append(item.toString());
    }
  }
  return result;
}

[[nodiscard]] bool report_identity_is_valid(
    const QVariantMap& report) {
  const auto expected =
      report.value(QStringLiteral("reportSha256"))
          .toString();
  if (report.value(QStringLiteral("schema")).toString() !=
          QStringLiteral(
              "photo-editor-rendering-migration-batch-report:v2") ||
      expected.size() != 64) {
    return false;
  }
  const auto producer = report.value(QStringLiteral("producer")).toMap();
  static const QRegularExpression sha256_pattern(
      QStringLiteral("^[0-9a-f]{64}$"));
  if (producer.value(QStringLiteral("contract")).toString() !=
          QStringLiteral(
              "photo-editor-rendering-migration-batch-producer:v1") ||
      !sha256_pattern
           .match(producer.value(QStringLiteral("executableSha256")).toString())
           .hasMatch() ||
      producer.value(QStringLiteral("executableName")).toString().isEmpty() ||
      producer.value(QStringLiteral("buildMode")).toString().isEmpty()) {
    return false;
  }
  const auto actual = QString::fromLatin1(
      QCryptographicHash::hash(
          canonical_rendering_migration_batch_report_payload(
              report),
          QCryptographicHash::Sha256)
          .toHex());
  return actual == expected;
}

[[nodiscard]] RenderingMigrationBatchReport
report_from_asset_documents(
    QVariantList asset_documents,
    const QVariantMap& thresholds,
    const QVariantMap& producer,
    const QStringList& component_report_hashes) {
  RenderingMigrationBatchReport result;
  std::ranges::sort(
      asset_documents,
      {},
      [](const QVariant& value) {
        return value.toMap()
            .value(QStringLiteral("assetId"))
            .toString();
      });
  QStringList missing_originals;
  QStringList unsupported_profiles;
  QStringList candidate_failures;
  QStringList missing_evidence;
  QStringList high_difference_assets;
  QStringList clipping_risk_assets;
  QStringList mask_layer_review_assets;
  QStringList color_target_review_assets;
  QStringList shipping_format_unavailable_assets;
  QStringList shipping_format_risk_assets;
  QStringList target_difference_risk_assets;
  QStringList hdr_perceptual_approval_required_assets;
  QStringList structure_attribution_unavailable_assets;
  QStringList presets_needing_attention;
  auto edited_count = 0;
  auto candidate_count = 0;
  auto evidence_count = 0;
  auto ready_count = 0;
  QSet<QString> seen_asset_ids;
  const auto collect = [](
                           const QStringList& values,
                           const QString& identity,
                           const QString& marker,
                           QStringList* output) {
    if (values.contains(marker)) {
      output->append(identity);
    }
  };
  for (const auto& value : asset_documents) {
    const auto asset = value.toMap();
    const auto asset_id =
        asset.value(QStringLiteral("assetId"))
            .toString()
            .trimmed();
    if (asset_id.isEmpty() || seen_asset_ids.contains(asset_id)) {
      result.error = QStringLiteral(
          "merged migration report contains an invalid or duplicate asset id");
      return result;
    }
    seen_asset_ids.insert(asset_id);
    if (!asset.value(QStringLiteral("edited")).toBool()) {
      continue;
    }
    ++edited_count;
    const auto blockers =
        string_list(asset.value(QStringLiteral("blockers")));
    const auto risks =
        string_list(asset.value(QStringLiteral("risks")));
    if (!blockers.contains(
            QStringLiteral("candidate-not-compiled"))) {
      ++candidate_count;
    }
    if (!blockers.contains(
            QStringLiteral("evidence-missing-or-invalid"))) {
      ++evidence_count;
    }
    const auto status =
        asset.value(QStringLiteral("status")).toString();
    if (status == QStringLiteral("ready") ||
        status == QStringLiteral("already-current")) {
      ++ready_count;
    }
    collect(blockers, asset_id, QStringLiteral("missing-original"), &missing_originals);
    collect(blockers, asset_id, QStringLiteral("unsupported-profile"), &unsupported_profiles);
    collect(blockers, asset_id, QStringLiteral("candidate-not-compiled"), &candidate_failures);
    collect(blockers, asset_id, QStringLiteral("evidence-missing-or-invalid"), &missing_evidence);
    collect(blockers, asset_id, QStringLiteral("shipping-format-unavailable"), &shipping_format_unavailable_assets);
    collect(blockers, asset_id, QStringLiteral("structure-attribution-unavailable"), &structure_attribution_unavailable_assets);
    collect(risks, asset_id, QStringLiteral("high-perceptual-difference"), &high_difference_assets);
    collect(risks, asset_id, QStringLiteral("added-clipping"), &clipping_risk_assets);
    collect(risks, asset_id, QStringLiteral("mask-layer-review-required"), &mask_layer_review_assets);
    collect(risks, asset_id, QStringLiteral("color-target-review-required"), &color_target_review_assets);
    collect(risks, asset_id, QStringLiteral("shipping-format-difference"), &shipping_format_risk_assets);
    collect(risks, asset_id, QStringLiteral("target-specific-difference"), &target_difference_risk_assets);
    collect(risks, asset_id, QStringLiteral("hdr-perceptual-approval-required"), &hdr_perceptual_approval_required_assets);
    if (!risks.isEmpty()) {
      const auto presets = string_list(
          asset.value(QStringLiteral("migrationContext"))
              .toMap()
              .value(QStringLiteral("presetIdentities")));
      for (const auto& preset : presets) {
        append_unique(&presets_needing_attention, preset);
      }
    }
  }
  presets_needing_attention.sort();
  result.document = QVariantMap{
      {QStringLiteral("schema"),
       QStringLiteral(
           "photo-editor-rendering-migration-batch-report:v2")},
      {QStringLiteral("producer"), producer},
      {QStringLiteral("thresholds"), thresholds},
      {QStringLiteral("componentReportSha256s"),
       component_report_hashes},
      {QStringLiteral("summary"),
       QVariantMap{
           {QStringLiteral("assetCount"), asset_documents.size()},
           {QStringLiteral("editedAssetCount"), edited_count},
           {QStringLiteral("candidateCompiledCount"), candidate_count},
           {QStringLiteral("evidenceReadyCount"), evidence_count},
           {QStringLiteral("readyCount"), ready_count},
           {QStringLiteral("allEditedAssetsCandidateCompiled"),
            candidate_count == edited_count},
           {QStringLiteral("allEditedAssetsHaveEvidence"),
            evidence_count == edited_count},
           {QStringLiteral("missingOriginalAssetIds"), missing_originals},
           {QStringLiteral("unsupportedProfileAssetIds"), unsupported_profiles},
           {QStringLiteral("candidateFailureAssetIds"), candidate_failures},
           {QStringLiteral("missingEvidenceAssetIds"), missing_evidence},
           {QStringLiteral("highDifferenceAssetIds"), high_difference_assets},
           {QStringLiteral("clippingRiskAssetIds"), clipping_risk_assets},
           {QStringLiteral("maskLayerReviewAssetIds"), mask_layer_review_assets},
           {QStringLiteral("colorTargetReviewAssetIds"), color_target_review_assets},
           {QStringLiteral("shippingFormatUnavailableAssetIds"), shipping_format_unavailable_assets},
           {QStringLiteral("shippingFormatRiskAssetIds"), shipping_format_risk_assets},
           {QStringLiteral("targetDifferenceRiskAssetIds"), target_difference_risk_assets},
           {QStringLiteral("hdrPerceptualApprovalRequiredAssetIds"), hdr_perceptual_approval_required_assets},
           {QStringLiteral("structureAttributionUnavailableAssetIds"), structure_attribution_unavailable_assets},
           {QStringLiteral("presetsNeedingAttention"), presets_needing_attention},
       }},
      {QStringLiteral("assets"), asset_documents},
  };
  result.report_hash = QString::fromLatin1(
      QCryptographicHash::hash(
          canonical_rendering_migration_batch_report_payload(
              result.document),
          QCryptographicHash::Sha256)
          .toHex());
  result.document.insert(
      QStringLiteral("reportSha256"), result.report_hash);
  result.valid = result.report_hash.size() == 64;
  if (!result.valid) {
    result.error = QStringLiteral(
        "merged migration report hash generation failed");
  }
  return result;
}

}  // namespace

RenderingMigrationBatchReport
build_rendering_migration_batch_report(
    const std::vector<RenderingMigrationBatchAsset>& assets,
    RenderingMigrationBatchThresholds thresholds,
    const RenderingMigrationBatchProducer& producer) {
  RenderingMigrationBatchReport result;
  if (!valid_thresholds(thresholds)) {
    result.error =
        QStringLiteral("migration report thresholds are invalid");
    return result;
  }
  static const QRegularExpression sha256_pattern(
      QStringLiteral("^[0-9a-f]{64}$"));
  if (!sha256_pattern.match(producer.executable_sha256).hasMatch() ||
      producer.executable_name.trimmed().isEmpty() ||
      producer.build_mode.trimmed().isEmpty()) {
    result.error = QStringLiteral(
        "migration report producer identity is invalid");
    return result;
  }
  const auto producer_document = QVariantMap{
      {QStringLiteral("contract"),
       QStringLiteral(
           "photo-editor-rendering-migration-batch-producer:v1")},
      {QStringLiteral("executableSha256"),
       producer.executable_sha256},
      {QStringLiteral("executableName"),
       producer.executable_name.trimmed()},
      {QStringLiteral("buildMode"),
       producer.build_mode.trimmed()},
  };

  auto ordered_assets = assets;
  std::ranges::sort(
      ordered_assets,
      {},
      &RenderingMigrationBatchAsset::asset_id);
  QSet<QString> seen_asset_ids;
  QVariantList asset_documents;
  QStringList missing_originals;
  QStringList unsupported_profiles;
  QStringList candidate_failures;
  QStringList missing_evidence;
  QStringList high_difference_assets;
  QStringList clipping_risk_assets;
  QStringList mask_layer_review_assets;
  QStringList color_target_review_assets;
  QStringList shipping_format_unavailable_assets;
  QStringList shipping_format_risk_assets;
  QStringList target_difference_risk_assets;
  QStringList hdr_perceptual_approval_required_assets;
  QStringList structure_attribution_unavailable_assets;
  QStringList presets_needing_attention;
  auto edited_count = 0;
  auto candidate_count = 0;
  auto evidence_count = 0;
  auto ready_count = 0;

  for (const auto& asset : ordered_assets) {
    const auto asset_id = asset.asset_id.trimmed();
    if (asset_id.isEmpty()) {
      result.error =
          QStringLiteral("migration report asset id is required");
      return result;
    }
    if (seen_asset_ids.contains(asset_id)) {
      result.error = QStringLiteral(
                         "migration report contains duplicate asset id: %1")
                         .arg(asset_id);
      return result;
    }
    seen_asset_ids.insert(asset_id);

    QVariantMap asset_document{
        {QStringLiteral("assetId"), asset_id},
        {QStringLiteral("edited"), asset.edited},
        {QStringLiteral("sourcePath"), asset.source_path},
    };
    QStringList blockers;
    QStringList risks;
    auto status = QStringLiteral("not-required");
    if (asset.edited) {
      ++edited_count;
      status = QStringLiteral("ready");
      if (!asset.original_available ||
          asset.source_path.trimmed().isEmpty()) {
        blockers.append(QStringLiteral("missing-original"));
        missing_originals.append(asset_id);
      }

      const auto profile_identities =
          normalized_identities(
              asset.unsupported_profile_identities);
      if (!profile_identities.isEmpty()) {
        blockers.append(QStringLiteral("unsupported-profile"));
        unsupported_profiles.append(asset_id);
        asset_document.insert(
            QStringLiteral("unsupportedProfileIdentities"),
            profile_identities);
      }

      if (!asset.migration_required && blockers.isEmpty()) {
        ++candidate_count;
        ++evidence_count;
        ++ready_count;
        asset_document.insert(
            QStringLiteral("status"),
            QStringLiteral("already-current"));
        asset_document.insert(
            QStringLiteral("blockers"), blockers);
        asset_document.insert(
            QStringLiteral("risks"), risks);
        asset_documents.append(asset_document);
        continue;
      }

      if (!asset.candidate_compiled) {
        blockers.append(QStringLiteral("candidate-not-compiled"));
        candidate_failures.append(asset_id);
        if (!asset.candidate_error.trimmed().isEmpty()) {
          asset_document.insert(
              QStringLiteral("candidateError"),
              asset.candidate_error.trimmed());
        }
      } else {
        ++candidate_count;
      }

      const auto evidence_valid =
          asset.candidate_compiled &&
          evidence_identity_is_valid(asset.evidence);
      asset_document.insert(
          QStringLiteral("evidenceValid"),
          evidence_valid);
      if (!evidence_valid) {
        blockers.append(QStringLiteral("evidence-missing-or-invalid"));
        missing_evidence.append(asset_id);
      } else {
        ++evidence_count;
        auto artifact_file_names =
            asset.evidence_artifact_file_names;
        artifact_file_names.removeDuplicates();
        artifact_file_names.sort();
        if (!artifact_file_names.isEmpty()) {
          const auto expected_names =
              expected_artifact_file_names(
                  asset.evidence);
          if (artifact_file_names != expected_names) {
            result.error = QStringLiteral(
                               "migration evidence artifact identities do not match for %1")
                               .arg(asset_id);
            return result;
          }
          asset_document.insert(
              QStringLiteral("evidenceArtifactFileNames"),
              artifact_file_names);
        }
        const auto shipping_formats =
            asset.evidence
                .value(QStringLiteral(
                    "shippingFormatEvidence"))
                .toMap();
        if (!shipping_formats.isEmpty()) {
          asset_document.insert(
              QStringLiteral("shippingFormatEvidence"),
              shipping_formats);
          QStringList unavailable_formats;
          auto shipping_difference_risk = false;
          for (auto iterator = shipping_formats.cbegin();
               iterator != shipping_formats.cend();
               ++iterator) {
            const auto format = iterator.value().toMap();
            if (!format.value(QStringLiteral("available"))
                     .toBool()) {
              unavailable_formats.append(iterator.key());
              continue;
            }
            const auto difference =
                format
                    .value(QStringLiteral(
                        "candidateVsReference"))
                    .toMap();
            shipping_difference_risk =
                shipping_difference_risk ||
                difference
                        .value(QStringLiteral("p95DeltaE00"))
                        .toDouble() >
                    thresholds.p95_delta_e00 ||
                difference
                        .value(QStringLiteral("maximumDeltaE00"))
                        .toDouble() >
                    thresholds.maximum_delta_e00;
          }
          if (!unavailable_formats.isEmpty()) {
            blockers.append(
                QStringLiteral("shipping-format-unavailable"));
            shipping_format_unavailable_assets.append(
                asset_id);
            asset_document.insert(
                QStringLiteral("unavailableShippingFormats"),
                unavailable_formats);
          }
          if (shipping_difference_risk) {
            risks.append(
                QStringLiteral("shipping-format-difference"));
            shipping_format_risk_assets.append(asset_id);
          }
        }
        const auto target_comparisons =
            asset.evidence
                .value(QStringLiteral(
                    "targetComparisonEvidence"))
                .toMap();
        if (!target_comparisons.isEmpty()) {
          asset_document.insert(
              QStringLiteral("targetComparisonEvidence"),
              target_comparisons);
          auto target_difference_risk = false;
          auto hdr_perceptual_approval_required = false;
          for (auto iterator = target_comparisons.cbegin();
               iterator != target_comparisons.cend();
               ++iterator) {
            if (iterator.key() ==
                QStringLiteral("srgb-simulation")) {
              continue;
            }
            const auto difference =
                iterator.value()
                    .toMap()
                    .value(QStringLiteral(
                        "candidateVsReference"))
                    .toMap();
            target_difference_risk =
                target_difference_risk ||
                difference
                        .value(QStringLiteral("p95DeltaE00"))
                        .toDouble() >
                    thresholds.p95_delta_e00 ||
                difference
                        .value(QStringLiteral("maximumDeltaE00"))
                        .toDouble() >
                    thresholds.maximum_delta_e00;
            const auto target = iterator.value().toMap();
            if (target
                    .value(QStringLiteral("outputColorSpace"))
                    .toString()
                    .startsWith(
                        QStringLiteral("Rec.2100"),
                        Qt::CaseInsensitive) &&
                !target
                     .value(QStringLiteral("hdrPerceptualApproval"))
                     .toBool()) {
              hdr_perceptual_approval_required = true;
            }
          }
          if (target_difference_risk) {
            risks.append(
                QStringLiteral("target-specific-difference"));
            target_difference_risk_assets.append(asset_id);
          }
          if (hdr_perceptual_approval_required) {
            risks.append(QStringLiteral(
                "hdr-perceptual-approval-required"));
            hdr_perceptual_approval_required_assets.append(
                asset_id);
          }
        }
        const auto structure_attributions =
            asset.evidence
                .value(QStringLiteral(
                    "structureAttributionEvidence"))
                .toMap();
        if (!structure_attributions.isEmpty()) {
          asset_document.insert(
              QStringLiteral("structureAttributionEvidence"),
              structure_attributions);
          QStringList unavailable_attributions;
          for (auto iterator = structure_attributions.cbegin();
               iterator != structure_attributions.cend();
               ++iterator) {
            if (!iterator.value()
                     .toMap()
                     .value(QStringLiteral("available"))
                     .toBool()) {
              unavailable_attributions.append(iterator.key());
            }
          }
          if (!unavailable_attributions.isEmpty()) {
            blockers.append(QStringLiteral(
                "structure-attribution-unavailable"));
            structure_attribution_unavailable_assets.append(
                asset_id);
            asset_document.insert(
                QStringLiteral("unavailableStructureAttributions"),
                unavailable_attributions);
          }
        }
        const auto p95 =
            asset.evidence
                .value(QStringLiteral("p95DeltaE00"))
                .toDouble();
        const auto maximum =
            asset.evidence
                .value(QStringLiteral("maximumDeltaE00"))
                .toDouble();
        const auto added_shadow_clips = std::max(
            0,
            asset.evidence
                    .value(QStringLiteral(
                        "candidateShadowClipCount"))
                    .toInt() -
                asset.evidence
                    .value(QStringLiteral(
                        "referenceShadowClipCount"))
                    .toInt());
        const auto added_highlight_clips = std::max(
            0,
            asset.evidence
                    .value(QStringLiteral(
                        "candidateHighlightClipCount"))
                    .toInt() -
                asset.evidence
                    .value(QStringLiteral(
                        "referenceHighlightClipCount"))
                    .toInt());
        asset_document.insert(
            QStringLiteral("evidenceSha256"),
            asset.evidence
                .value(QStringLiteral("evidenceSha256")));
        asset_document.insert(
            QStringLiteral("p95DeltaE00"), p95);
        asset_document.insert(
            QStringLiteral("maximumDeltaE00"), maximum);
        asset_document.insert(
            QStringLiteral("addedShadowClipCount"),
            added_shadow_clips);
        asset_document.insert(
            QStringLiteral("addedHighlightClipCount"),
            added_highlight_clips);
        const auto high_difference =
            p95 > thresholds.p95_delta_e00 ||
            maximum > thresholds.maximum_delta_e00;
        const auto added_clipping =
            added_shadow_clips >
                thresholds.maximum_added_shadow_clips ||
            added_highlight_clips >
                thresholds.maximum_added_highlight_clips;
        if (high_difference) {
          risks.append(QStringLiteral("high-perceptual-difference"));
          high_difference_assets.append(asset_id);
        }
        if (added_clipping) {
          risks.append(QStringLiteral("added-clipping"));
          clipping_risk_assets.append(asset_id);
        }
        const auto migration_context =
            asset.evidence
                .value(QStringLiteral("migrationContext"))
                .toMap();
        if (!migration_context.isEmpty()) {
          asset_document.insert(
              QStringLiteral("migrationContext"),
              migration_context);
          if ((high_difference || added_clipping) &&
              (migration_context
                       .value(QStringLiteral("maskCount"))
                       .toInt() > 0 ||
               migration_context
                       .value(QStringLiteral("layerCount"))
                       .toInt() > 0)) {
            risks.append(
                QStringLiteral("mask-layer-review-required"));
            mask_layer_review_assets.append(asset_id);
          }
          if ((high_difference || added_clipping) &&
              (migration_context
                       .value(QStringLiteral("softProofEnabled"))
                       .toBool() ||
               !migration_context
                    .value(QStringLiteral("profileIdentities"))
                    .toStringList()
                    .isEmpty())) {
            risks.append(
                QStringLiteral("color-target-review-required"));
            color_target_review_assets.append(asset_id);
          }
        }
      }

      if (!blockers.isEmpty()) {
        status = QStringLiteral("blocked");
      } else if (!risks.isEmpty()) {
        status = QStringLiteral("attention-required");
      } else {
        ++ready_count;
      }
      if (!risks.isEmpty()) {
        for (const auto& preset :
             normalized_identities(
                 asset.preset_identities)) {
          append_unique(
              &presets_needing_attention, preset);
        }
      }
    }
    asset_document.insert(
        QStringLiteral("status"), status);
    asset_document.insert(
        QStringLiteral("blockers"), blockers);
    asset_document.insert(
        QStringLiteral("risks"), risks);
    asset_documents.append(asset_document);
  }

  presets_needing_attention.sort();
  result.document = QVariantMap{
      {QStringLiteral("schema"),
       QStringLiteral(
           "photo-editor-rendering-migration-batch-report:v2")},
      {QStringLiteral("producer"), producer_document},
      {QStringLiteral("thresholds"),
       thresholds_document(thresholds)},
      {QStringLiteral("summary"),
       QVariantMap{
           {QStringLiteral("assetCount"),
            static_cast<qulonglong>(ordered_assets.size())},
           {QStringLiteral("editedAssetCount"), edited_count},
           {QStringLiteral("candidateCompiledCount"), candidate_count},
           {QStringLiteral("evidenceReadyCount"), evidence_count},
           {QStringLiteral("readyCount"), ready_count},
           {QStringLiteral("allEditedAssetsCandidateCompiled"),
            candidate_count == edited_count},
           {QStringLiteral("allEditedAssetsHaveEvidence"),
            evidence_count == edited_count},
           {QStringLiteral("missingOriginalAssetIds"),
            missing_originals},
           {QStringLiteral("unsupportedProfileAssetIds"),
            unsupported_profiles},
           {QStringLiteral("candidateFailureAssetIds"),
            candidate_failures},
           {QStringLiteral("missingEvidenceAssetIds"),
            missing_evidence},
           {QStringLiteral("highDifferenceAssetIds"),
            high_difference_assets},
           {QStringLiteral("clippingRiskAssetIds"),
            clipping_risk_assets},
           {QStringLiteral("maskLayerReviewAssetIds"),
            mask_layer_review_assets},
           {QStringLiteral("colorTargetReviewAssetIds"),
            color_target_review_assets},
           {QStringLiteral("shippingFormatUnavailableAssetIds"),
            shipping_format_unavailable_assets},
           {QStringLiteral("shippingFormatRiskAssetIds"),
            shipping_format_risk_assets},
           {QStringLiteral("targetDifferenceRiskAssetIds"),
            target_difference_risk_assets},
           {QStringLiteral(
                "hdrPerceptualApprovalRequiredAssetIds"),
            hdr_perceptual_approval_required_assets},
           {QStringLiteral(
                "structureAttributionUnavailableAssetIds"),
            structure_attribution_unavailable_assets},
           {QStringLiteral("presetsNeedingAttention"),
            presets_needing_attention},
       }},
      {QStringLiteral("assets"), asset_documents},
  };
  const auto payload =
      canonical_rendering_migration_batch_report_payload(
          result.document);
  result.report_hash = QString::fromLatin1(
      QCryptographicHash::hash(
          payload,
          QCryptographicHash::Sha256)
          .toHex());
  result.document.insert(
      QStringLiteral("reportSha256"),
      result.report_hash);
  result.valid = result.report_hash.size() == 64;
  if (!result.valid) {
    result.error =
        QStringLiteral("migration report hash generation failed");
  }
  return result;
}

RenderingMigrationBatchReport
merge_rendering_migration_batch_reports(
    const QVariantMap& base_report,
    const std::vector<QVariantMap>& overlay_reports) {
  RenderingMigrationBatchReport result;
  if (!report_identity_is_valid(base_report)) {
    result.error = QStringLiteral(
        "base migration report identity is invalid");
    return result;
  }
  const auto thresholds =
      base_report.value(QStringLiteral("thresholds"))
          .toMap();
  const auto producer =
      base_report.value(QStringLiteral("producer")).toMap();
  QMap<QString, QVariantMap> merged_assets;
  QMap<QString, QStringList> base_blockers;
  for (const auto& value :
       base_report.value(QStringLiteral("assets")).toList()) {
    const auto asset = value.toMap();
    const auto asset_id =
        asset.value(QStringLiteral("assetId"))
            .toString()
            .trimmed();
    if (asset_id.isEmpty() || merged_assets.contains(asset_id)) {
      result.error = QStringLiteral(
          "base migration report contains an invalid or duplicate asset id");
      return result;
    }
    merged_assets.insert(asset_id, asset);
    base_blockers.insert(
        asset_id,
        string_list(asset.value(QStringLiteral("blockers"))));
  }
  if (merged_assets.isEmpty()) {
    result.error = QStringLiteral(
        "base migration report contains no assets");
    return result;
  }

  QStringList component_report_hashes{
      base_report.value(QStringLiteral("reportSha256"))
          .toString()};
  QSet<QString> replaced_asset_ids;
  for (const auto& overlay : overlay_reports) {
    if (!report_identity_is_valid(overlay)) {
      result.error = QStringLiteral(
          "overlay migration report identity is invalid");
      return result;
    }
    if (overlay.value(QStringLiteral("thresholds")).toMap() !=
        thresholds) {
      result.error = QStringLiteral(
          "migration reports use different thresholds");
      return result;
    }
    if (overlay.value(QStringLiteral("producer")).toMap() != producer) {
      result.error = QStringLiteral(
          "migration reports were produced by different executables");
      return result;
    }
    component_report_hashes.append(
        overlay.value(QStringLiteral("reportSha256"))
            .toString());
    for (const auto& value :
         overlay.value(QStringLiteral("assets")).toList()) {
      const auto candidate = value.toMap();
      const auto asset_id =
          candidate.value(QStringLiteral("assetId"))
              .toString()
              .trimmed();
      if (!merged_assets.contains(asset_id)) {
        result.error = QStringLiteral(
                           "overlay migration report contains an asset absent from the base: %1")
                           .arg(asset_id);
        return result;
      }
      const auto& base_asset = merged_assets.value(asset_id);
      if (candidate.value(QStringLiteral("sourcePath")).toString() !=
              base_asset.value(QStringLiteral("sourcePath")).toString() ||
          candidate.value(QStringLiteral("edited")).toBool() !=
              base_asset.value(QStringLiteral("edited")).toBool()) {
        result.error = QStringLiteral(
                           "overlay migration report changed source identity for %1")
                           .arg(asset_id);
        return result;
      }
      merged_assets.insert(asset_id, candidate);
      replaced_asset_ids.insert(asset_id);
    }
  }
  for (const auto& asset_id : replaced_asset_ids) {
    const auto before = base_blockers.value(asset_id);
    const auto after = string_list(
        merged_assets.value(asset_id)
            .value(QStringLiteral("blockers")));
    if (before.isEmpty() || after.size() >= before.size()) {
      result.error = QStringLiteral(
                         "overlay migration report did not reduce blockers for %1")
                         .arg(asset_id);
      return result;
    }
    for (const auto& blocker : after) {
      if (!before.contains(blocker)) {
        result.error = QStringLiteral(
                           "overlay migration report introduced a new blocker for %1: %2")
                           .arg(asset_id, blocker);
        return result;
      }
    }
  }

  QVariantList merged_documents;
  merged_documents.reserve(merged_assets.size());
  for (auto iterator = merged_assets.cbegin();
       iterator != merged_assets.cend();
       ++iterator) {
    merged_documents.append(iterator.value());
  }
  return report_from_asset_documents(
      merged_documents,
      thresholds,
      producer,
      component_report_hashes);
}

QByteArray canonical_rendering_migration_batch_report_payload(
    const QVariantMap& report) {
  auto payload = report;
  payload.remove(QStringLiteral("reportSha256"));
  return QJsonDocument::fromVariant(payload)
      .toJson(QJsonDocument::Compact);
}

bool write_immutable_rendering_migration_batch_report(
    const QString& file_path,
    const QVariantMap& report,
    QString* error) {
  const auto fail = [error](const QString& message) {
    if (error != nullptr) {
      *error = message;
    }
    return false;
  };
  const auto expected_hash =
      report.value(QStringLiteral("reportSha256"))
          .toString();
  const auto payload =
      canonical_rendering_migration_batch_report_payload(
          report);
  const auto actual_hash = QString::fromLatin1(
      QCryptographicHash::hash(
          payload,
          QCryptographicHash::Sha256)
          .toHex());
  if (report.value(QStringLiteral("schema")).toString() !=
          QStringLiteral(
              "photo-editor-rendering-migration-batch-report:v2") ||
      expected_hash.size() != 64 ||
      actual_hash != expected_hash) {
    return fail(
        QStringLiteral(
            "migration report identity is invalid"));
  }
  if (file_path.trimmed().isEmpty()) {
    return fail(
        QStringLiteral(
            "migration report path is required"));
  }
  const auto bytes =
      QJsonDocument::fromVariant(report)
          .toJson(QJsonDocument::Compact);
  QFile existing(file_path);
  if (existing.exists()) {
    if (!existing.open(QIODevice::ReadOnly) ||
        existing.readAll() != bytes) {
      return fail(
          QStringLiteral(
              "existing migration report does not match its identity"));
    }
    return true;
  }

  const QFileInfo destination(file_path);
  if (!QDir().mkpath(destination.dir().absolutePath())) {
    return fail(
        QStringLiteral(
            "migration report directory could not be created"));
  }
  const auto temporary_path =
      destination.dir().filePath(
          QStringLiteral(".%1.%2.tmp")
              .arg(
                  destination.fileName(),
                  QUuid::createUuid().toString(
                      QUuid::WithoutBraces)));
  QFile temporary(temporary_path);
  if (!temporary.open(
          QIODevice::WriteOnly | QIODevice::NewOnly) ||
      temporary.write(bytes) != bytes.size() ||
      !temporary.flush()) {
    temporary.close();
    temporary.remove();
    return fail(
        QStringLiteral(
            "migration report could not be written"));
  }
  temporary.close();
  if (!temporary.rename(file_path)) {
    temporary.remove();
    QFile raced_destination(file_path);
    if (raced_destination.open(QIODevice::ReadOnly) &&
        raced_destination.readAll() == bytes) {
      return true;
    }
    return fail(
        QStringLiteral(
            "migration report could not be committed immutably"));
  }
  return true;
}

}  // namespace photo_editor::desktop
