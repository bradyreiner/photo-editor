#pragma once

#include <QJsonDocument>
#include <QJsonParseError>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <utility>

namespace photo_editor::desktop::agent_detail {

inline bool mcp_presentation_only_key(const QString& key) {
  const auto normalized = key.toLower();
  return normalized.startsWith(QStringLiteral("thumbnail")) ||
      normalized == QStringLiteral("previewimage") ||
      normalized == QStringLiteral("previewdata") ||
      normalized == QStringLiteral("previewdataurl") ||
      normalized == QStringLiteral("previewsource") ||
      normalized == QStringLiteral("renderedthumbnail") ||
      normalized == QStringLiteral("cachedrenderedimage");
}

inline QVariantMap mcp_projection_row(const QVariantMap& row) {
  QVariantMap compact;
  for (auto iterator = row.cbegin(); iterator != row.cend(); ++iterator) {
    if (!mcp_presentation_only_key(iterator.key()))
      compact.insert(iterator.key(), iterator.value());
  }
  return compact;
}

inline QVariantList mcp_projection_rows(const QVariantList& rows) {
  QVariantList result;
  result.reserve(rows.size());
  for (const auto& row_value : rows)
    result.append(mcp_projection_row(row_value.toMap()));
  return result;
}

inline bool mcp_content_item_duplicates_structured_content(
    const QVariant& content_item,
    const QVariantMap& structured_content) {
  if (structured_content.isEmpty()) return false;
  const auto item = content_item.toMap();
  if (item.value(QStringLiteral("type")).toString() != QStringLiteral("text"))
    return false;
  const auto text = item.value(QStringLiteral("text")).toString();
  QJsonParseError content_error;
  const auto content_document = QJsonDocument::fromJson(text.toUtf8(), &content_error);
  if (content_error.error != QJsonParseError::NoError || !content_document.isObject())
    return false;

  // Normalize both maps through JSON so QVariant integer/double differences
  // and JSON object key order do not keep a semantically duplicate payload.
  const auto structured_json = QJsonDocument::fromVariant(structured_content)
                                   .toJson(QJsonDocument::Compact);
  QJsonParseError structured_error;
  const auto normalized_structured =
      QJsonDocument::fromJson(structured_json, &structured_error);
  return structured_error.error == QJsonParseError::NoError &&
      normalized_structured.isObject() &&
      content_document.object().toVariantMap() ==
          normalized_structured.object().toVariantMap();
}

inline QVariantMap proof_critique_composition_inspection_projection(
    const QVariantMap& inspection) {
  QVariantMap compact_inspection;
  for (const auto& key : {QStringLiteral("toolName"), QStringLiteral("ok"),
           QStringLiteral("error"), QStringLiteral("arguments"),
           QStringLiteral("endpointKind"), QStringLiteral("runId")}) {
    if (inspection.contains(key)) compact_inspection.insert(key, inspection.value(key));
  }

  const auto structured_content =
      inspection.value(QStringLiteral("structuredContent")).toMap();
  QStringList used_font_families;
  for (const auto& layer_value : structured_content.value(QStringLiteral("layers")).toList()) {
    const auto layer = layer_value.toMap();
    if (layer.value(QStringLiteral("kind")).toString() != QStringLiteral("text")) continue;
    auto family = layer.value(QStringLiteral("textTypography")).toMap()
                      .value(QStringLiteral("family")).toString().trimmed();
    if (family.isEmpty()) family = layer.value(QStringLiteral("fontFamily")).toString().trimmed();
    if (!family.isEmpty() && !used_font_families.contains(family)) used_font_families.append(family);
  }
  QVariantMap compact_content;
  for (auto iterator = structured_content.cbegin();
       iterator != structured_content.cend(); ++iterator) {
    if (mcp_presentation_only_key(iterator.key())) continue;
    if (iterator.key() == QStringLiteral("fontFaces") && !used_font_families.isEmpty()) {
      QVariantList used_faces;
      QStringList unavailable_families = used_font_families;
      for (const auto& entry_value : iterator.value().toList()) {
        const auto entry = entry_value.toMap();
        const auto family = entry.value(QStringLiteral("family")).toString();
        if (!used_font_families.contains(family)) continue;
        used_faces.append(entry_value);
        unavailable_families.removeAll(family);
      }
      compact_content.insert(iterator.key(), used_faces);
      compact_content.insert(QStringLiteral("unavailableUsedFontFamilies"), unavailable_families);
      compact_content.insert(QStringLiteral("fontInventoryProjection"),
          QStringLiteral("all-native-faces-for-every-used-text-family"));
    } else if (iterator.key() == QStringLiteral("fonts") && !used_font_families.isEmpty()) {
      QStringList available_used_families;
      for (const auto& family : iterator.value().toStringList())
        if (used_font_families.contains(family)) available_used_families.append(family);
      compact_content.insert(iterator.key(), available_used_families);
    } else if (iterator.key() == QStringLiteral("layers") ||
        iterator.key() == QStringLiteral("masks")) {
      compact_content.insert(iterator.key(), mcp_projection_rows(iterator.value().toList()));
    } else {
      compact_content.insert(iterator.key(), iterator.value());
    }
  }
  if (!compact_content.isEmpty())
    compact_inspection.insert(QStringLiteral("structuredContent"), compact_content);

  QVariantList content_projection;
  for (const auto& item : inspection.value(QStringLiteral("content")).toList()) {
    if (mcp_content_item_duplicates_structured_content(item, structured_content))
      continue;
    const auto content_map = item.toMap();
    if (content_map.value(QStringLiteral("type")).toString() == QStringLiteral("image"))
      continue;
    content_projection.append(item);
  }
  if (!content_projection.isEmpty())
    compact_inspection.insert(QStringLiteral("content"), content_projection);
  compact_inspection.insert(QStringLiteral("metadataProjection"),
      QStringLiteral("semantic-native-readback-without-presentation-cache-or-duplicate-json-transport"));
  return compact_inspection;
}

inline QVariantMap proof_critique_validation_projection(const QVariantMap& validation_report) {
  auto projected = validation_report;
  const auto photo_evidence = validation_report.value(QStringLiteral("photographicQualityEvidence")).toMap();
  for (const auto& pair : {std::pair{QStringLiteral("imageStats"), QStringLiteral("baselineImageStats")},
           std::pair{QStringLiteral("compositeImageStats"), QStringLiteral("currentCompositeImageStats")},
           std::pair{QStringLiteral("qualityScoreReport"), QStringLiteral("baselineQualityScoreReport")}}) {
    if (projected.contains(pair.first) && photo_evidence.contains(pair.second) &&
        projected.value(pair.first) == photo_evidence.value(pair.second)) {
      projected.remove(pair.first);
      projected.insert(pair.first + QStringLiteral("Reference"),
          QStringLiteral("validationReport.photographicQualityEvidence.") + pair.second);
    }
  }
  const auto original_review = validation_report.value(QStringLiteral("compositionReview")).toMap();
  if (!original_review.isEmpty()) {
    auto review = original_review;

    auto inspection = original_review.value(QStringLiteral("inspection")).toMap();
    if (!inspection.isEmpty())
      review.insert(QStringLiteral("inspection"),
          proof_critique_composition_inspection_projection(inspection));
    if (review.value(QStringLiteral("maskState")).metaType().id() == QMetaType::QVariantList)
      review.insert(QStringLiteral("maskState"),
          mcp_projection_rows(review.value(QStringLiteral("maskState")).toList()));
    // Keep one authoritative copy of the complete hashed crop reports. Native
    // review publishes the same rows at both paths for UI consumers.
    const auto detail_report = original_review.value(QStringLiteral("detailProofReport")).toMap();
    for (const auto& key : {QStringLiteral("targetedDetailProofs"),
             QStringLiteral("detailProofImagePaths")}) {
      if (review.contains(key) && detail_report.contains(key) &&
          review.value(key) == detail_report.value(key)) {
        review.remove(key);
        review.insert(key + QStringLiteral("Reference"),
            QStringLiteral("validationReport.compositionReview.detailProofReport.") + key);
      }
    }
    projected.insert(QStringLiteral("compositionReview"), review);
  }

  auto live_evidence = projected.value(QStringLiteral("liveRepairEvidence")).toMap();
  if (!live_evidence.isEmpty() && !original_review.isEmpty()) {
    if (live_evidence.contains(QStringLiteral("compositionReview")) &&
        live_evidence.value(QStringLiteral("compositionReview")).toMap() == original_review) {
      live_evidence.remove(QStringLiteral("compositionReview"));
      live_evidence.insert(QStringLiteral("compositionReviewReference"),
                         QStringLiteral("validationReport.compositionReview"));
    }
    if (live_evidence.contains(QStringLiteral("targetedDetailProofs")) &&
        original_review.contains(QStringLiteral("targetedDetailProofs")) &&
        live_evidence.value(QStringLiteral("targetedDetailProofs")) ==
            original_review.value(QStringLiteral("targetedDetailProofs"))) {
      live_evidence.remove(QStringLiteral("targetedDetailProofs"));
      live_evidence.insert(QStringLiteral("targetedDetailProofsReference"),
        projected.value(QStringLiteral("compositionReview")).toMap()
            .contains(QStringLiteral("targetedDetailProofs"))
            ? QStringLiteral("validationReport.compositionReview.targetedDetailProofs")
            : QStringLiteral("validationReport.compositionReview.detailProofReport.targetedDetailProofs"));
    }
    projected.insert(QStringLiteral("liveRepairEvidence"), live_evidence);
  }
  return projected;
}

}  // namespace photo_editor::desktop::agent_detail
