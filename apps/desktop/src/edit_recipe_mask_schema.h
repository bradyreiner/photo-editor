#pragma once

#include "edit_recipe_state.h"
#include "edit_recipe_mask_types.h"

namespace photo_editor::desktop {

inline constexpr qint64 kMaxMaskPayloadBytes = 64LL * 1024LL * 1024LL;
inline constexpr int kMaxMaskPayloadEdge = 16384;

inline bool valid_segmentation_payload(const QByteArray& data, int width, int height) {
  if (width <= 0 || height <= 0) {
    return false;
  }
  if (width > kMaxMaskPayloadEdge || height > kMaxMaskPayloadEdge) {
    return false;
  }

  const auto expected_size = static_cast<qint64>(width) * static_cast<qint64>(height);
  return expected_size > 0 &&
         expected_size <= kMaxMaskPayloadBytes &&
         data.size() == expected_size;
}

inline int map_int_value_or(const QVariantMap& map, const QString& key, int fallback) {
  return map.contains(key) ? map.value(key).toInt() : fallback;
}

inline void copy_inpaint_patch_payload(QVariantMap* target, const QVariantMap& source) {
  if (target == nullptr) {
    return;
  }

  const auto patch_data = source.value(QStringLiteral("inpaintPatchData")).toByteArray();
  const auto patch_width = source.value(QStringLiteral("inpaintPatchWidth")).toInt();
  const auto patch_height = source.value(QStringLiteral("inpaintPatchHeight")).toInt();
  if (patch_data.isEmpty() || patch_width <= 0 || patch_height <= 0) {
    return;
  }

  target->insert(QStringLiteral("inpaintPatchData"), patch_data);
  target->insert(QStringLiteral("inpaintPatchX"), source.value(QStringLiteral("inpaintPatchX")).toInt());
  target->insert(QStringLiteral("inpaintPatchY"), source.value(QStringLiteral("inpaintPatchY")).toInt());
  target->insert(QStringLiteral("inpaintPatchWidth"), patch_width);
  target->insert(QStringLiteral("inpaintPatchHeight"), patch_height);
  target->insert(
      QStringLiteral("inpaintPatchColorSpace"),
      source.value(QStringLiteral("inpaintPatchColorSpace"), QStringLiteral("sRGB")).toString());
  target->insert(
      QStringLiteral("inpaintPatchSchemaVersion"),
      std::max(1, map_int_value_or(source, QStringLiteral("inpaintPatchSchemaVersion"), 1)));
  target->insert(
      QStringLiteral("inpaintModelName"),
      source.value(QStringLiteral("inpaintModelName")).toString());
  const auto source_mask_data = source.value(QStringLiteral("inpaintSourceMaskData")).toByteArray();
  const auto source_mask_width = source.value(QStringLiteral("inpaintSourceMaskWidth")).toInt();
  const auto source_mask_height = source.value(QStringLiteral("inpaintSourceMaskHeight")).toInt();
  if (valid_segmentation_payload(source_mask_data, source_mask_width, source_mask_height)) {
    target->insert(QStringLiteral("inpaintSourceMaskData"), source_mask_data);
    target->insert(QStringLiteral("inpaintSourceMaskWidth"), source_mask_width);
    target->insert(QStringLiteral("inpaintSourceMaskHeight"), source_mask_height);
    const auto source_bounds_width =
        source.value(QStringLiteral("inpaintSourceBoundsWidth")).toInt();
    const auto source_bounds_height =
        source.value(QStringLiteral("inpaintSourceBoundsHeight")).toInt();
    if (source_bounds_width > 0 && source_bounds_height > 0) {
      target->insert(
          QStringLiteral("inpaintSourceBoundsX"),
          source.value(QStringLiteral("inpaintSourceBoundsX")).toInt());
      target->insert(
          QStringLiteral("inpaintSourceBoundsY"),
          source.value(QStringLiteral("inpaintSourceBoundsY")).toInt());
      target->insert(QStringLiteral("inpaintSourceBoundsWidth"), source_bounds_width);
      target->insert(QStringLiteral("inpaintSourceBoundsHeight"), source_bounds_height);
    }
  }
  const auto selected_variation_index =
      source.contains(QStringLiteral("inpaintSelectedVariationIndex"))
          ? source.value(QStringLiteral("inpaintSelectedVariationIndex")).toInt()
          : source.value(QStringLiteral("inpaintCandidateIndex")).toInt();
  target->insert(QStringLiteral("inpaintCandidateIndex"), selected_variation_index);
  target->insert(QStringLiteral("inpaintSelectedVariationIndex"), selected_variation_index);
  target->insert(
      QStringLiteral("inpaintSelectedVariationSeed"),
      std::max(
          1,
          source.contains(QStringLiteral("inpaintSelectedVariationSeed"))
              ? source.value(QStringLiteral("inpaintSelectedVariationSeed")).toInt()
              : source.value(QStringLiteral("inpaintSeed")).toInt()));
  target->insert(
      QStringLiteral("inpaintSelectedVariationModelName"),
      source.value(
                QStringLiteral("inpaintSelectedVariationModelName"),
                source.value(QStringLiteral("inpaintModelName")))
          .toString());
  const auto source_candidates = source.value(QStringLiteral("inpaintCandidates")).toList();
  if (!source_candidates.isEmpty()) {
    QVariantList candidates;
    for (const auto& candidate_value : source_candidates) {
      const auto candidate = candidate_value.toMap();
      const auto candidate_data = candidate.value(QStringLiteral("patchData")).toByteArray();
      const auto candidate_width = candidate.value(QStringLiteral("patchWidth")).toInt();
      const auto candidate_height = candidate.value(QStringLiteral("patchHeight")).toInt();
      if (candidate_data.isEmpty() || candidate_width <= 0 || candidate_height <= 0) {
        continue;
      }
      QVariantMap normalized_candidate;
      normalized_candidate.insert(QStringLiteral("patchData"), candidate_data);
      normalized_candidate.insert(QStringLiteral("patchX"), candidate.value(QStringLiteral("patchX")).toInt());
      normalized_candidate.insert(QStringLiteral("patchY"), candidate.value(QStringLiteral("patchY")).toInt());
      normalized_candidate.insert(QStringLiteral("patchWidth"), candidate_width);
      normalized_candidate.insert(QStringLiteral("patchHeight"), candidate_height);
      normalized_candidate.insert(
          QStringLiteral("patchColorSpace"),
          candidate.value(QStringLiteral("patchColorSpace"), QStringLiteral("sRGB")).toString());
      normalized_candidate.insert(QStringLiteral("modelName"), candidate.value(QStringLiteral("modelName")).toString());
      normalized_candidate.insert(QStringLiteral("index"), candidate.value(QStringLiteral("index")).toInt());
      normalized_candidate.insert(
          QStringLiteral("seed"),
          std::max(1, candidate.value(QStringLiteral("seed")).toInt()));
      candidates.append(normalized_candidate);
    }
    if (!candidates.isEmpty()) {
      target->insert(QStringLiteral("inpaintCandidates"), candidates);
      target->insert(QStringLiteral("inpaintCandidateCount"), candidates.size());
    }
  }
  target->insert(
      QStringLiteral("inpaintPatchPayloadBytes"),
      std::max(0, source.value(QStringLiteral("inpaintPatchPayloadBytes")).toInt()));
  target->insert(
      QStringLiteral("inpaintCandidatePayloadBytes"),
      std::max(0, source.value(QStringLiteral("inpaintCandidatePayloadBytes")).toInt()));
  target->insert(
      QStringLiteral("inpaintCandidateEvictedCount"),
      std::max(0, source.value(QStringLiteral("inpaintCandidateEvictedCount")).toInt()));
  const auto prompt = source.value(QStringLiteral("inpaintPrompt")).toString();
  if (!prompt.isEmpty()) {
    target->insert(QStringLiteral("inpaintPrompt"), prompt);
  }
  const auto quality_mode = source.value(QStringLiteral("inpaintQualityMode")).toString().trimmed().toLower();
  target->insert(
      QStringLiteral("inpaintQualityMode"),
      quality_mode == QStringLiteral("final") ? QStringLiteral("final") : QStringLiteral("preview"));
  target->insert(
      QStringLiteral("inpaintFinalRequired"),
      source.contains(QStringLiteral("inpaintFinalRequired"))
          ? source.value(QStringLiteral("inpaintFinalRequired")).toBool()
          : quality_mode != QStringLiteral("final"));
  target->insert(
      QStringLiteral("inpaintSeed"),
      std::max(1, source.value(QStringLiteral("inpaintSeed")).toInt()));
  target->insert(
      QStringLiteral("inpaintBlendAmount"),
      qBound(0.0, source.value(QStringLiteral("inpaintBlendAmount"), 1.0).toDouble(), 1.0));
  target->insert(
      QStringLiteral("inpaintSeamFeather"),
      qBound(0.0, source.value(QStringLiteral("inpaintSeamFeather"), 4.0).toDouble(), 24.0));
  target->insert(
      QStringLiteral("inpaintColorMatch"),
      qBound(0.0, source.value(QStringLiteral("inpaintColorMatch"), 0.75).toDouble(), 1.0));
  target->insert(
      QStringLiteral("inpaintFallback"),
      source.contains(QStringLiteral("inpaintFallback"))
          ? source.value(QStringLiteral("inpaintFallback")).toBool()
          : false);
  const auto fallback_reason = source.value(QStringLiteral("inpaintFallbackReason")).toString();
  if (!fallback_reason.isEmpty()) {
    target->insert(QStringLiteral("inpaintFallbackReason"), fallback_reason);
  }
  const auto region_kind = source.value(QStringLiteral("inpaintRegionKind")).toString();
  if (!region_kind.isEmpty()) {
    target->insert(QStringLiteral("inpaintRegionKind"), region_kind);
    target->insert(
        QStringLiteral("inpaintEdgeContinuation"),
        source.value(QStringLiteral("inpaintEdgeContinuation")).toBool());
    target->insert(
        QStringLiteral("inpaintTextureSynthesis"),
        source.value(QStringLiteral("inpaintTextureSynthesis")).toBool());
    target->insert(
        QStringLiteral("inpaintTextureExemplar"),
        source.value(QStringLiteral("inpaintTextureExemplar")).toBool());
    target->insert(
        QStringLiteral("inpaintTextureStrength"),
        qBound(0.0, source.value(QStringLiteral("inpaintTextureStrength")).toDouble(), 1.0));
    target->insert(
        QStringLiteral("inpaintRegionCoverage"),
        qBound(0.0, source.value(QStringLiteral("inpaintRegionCoverage")).toDouble(), 1.0));
    target->insert(QStringLiteral("inpaintRegionX"), source.value(QStringLiteral("inpaintRegionX")).toInt());
    target->insert(QStringLiteral("inpaintRegionY"), source.value(QStringLiteral("inpaintRegionY")).toInt());
    target->insert(QStringLiteral("inpaintRegionWidth"), source.value(QStringLiteral("inpaintRegionWidth")).toInt());
    target->insert(QStringLiteral("inpaintRegionHeight"), source.value(QStringLiteral("inpaintRegionHeight")).toInt());
  }
}

inline void copy_generated_confidence_payload(QVariantMap* target, const QVariantMap& source) {
  if (target == nullptr) {
    return;
  }

  const auto confidence_data = source.value(QStringLiteral("confidenceData")).toByteArray();
  const auto confidence_width = source.value(QStringLiteral("confidenceWidth")).toInt();
  const auto confidence_height = source.value(QStringLiteral("confidenceHeight")).toInt();
  if (!valid_segmentation_payload(confidence_data, confidence_width, confidence_height)) {
    return;
  }
  target->insert(QStringLiteral("confidenceData"), confidence_data);
  target->insert(QStringLiteral("confidenceWidth"), confidence_width);
  target->insert(QStringLiteral("confidenceHeight"), confidence_height);
  target->insert(
      QStringLiteral("confidenceMapSchemaVersion"),
      std::max(1, map_int_value_or(source, QStringLiteral("confidenceMapSchemaVersion"), 1)));
}

inline void copy_generated_mask_metadata(QVariantMap* target, const QVariantMap& source) {
  if (target == nullptr) {
    return;
  }

  const auto metadata = source.value(QStringLiteral("generatedMaskMetadata")).toMap();
  if (!metadata.isEmpty()) {
    target->insert(QStringLiteral("generatedMaskMetadata"), metadata);
  }
  if (source.contains(QStringLiteral("generatedMaskStale"))) {
    target->insert(
        QStringLiteral("generatedMaskStale"),
        source.value(QStringLiteral("generatedMaskStale")).toBool());
  }
  for (const auto& key : {
           QStringLiteral("generatedAlphaSourceContract"),
           QStringLiteral("generatedAlphaModelName"),
           QStringLiteral("generatedAlphaModelVersion"),
           QStringLiteral("generatedAlphaTargetType"),
           QStringLiteral("generatedAlphaPromptType"),
           QStringLiteral("generatedAlphaSourceImageVersion"),
           QStringLiteral("generatedAlphaWidth"),
           QStringLiteral("generatedAlphaHeight"),
           QStringLiteral("generatedAlphaCoordinateSpaceContract"),
           QStringLiteral("generatedAlphaSourceCoordinateSpace"),
           QStringLiteral("generatedAlphaPromptCoordinateSpace"),
           QStringLiteral("generatedAlphaRasterCoordinateSpace"),
           QStringLiteral("generatedAlphaDownscaleFactor"),
           QStringLiteral("generatedAlphaConfidenceAvailable"),
           QStringLiteral("generatedAlphaConfidenceWidth"),
           QStringLiteral("generatedAlphaConfidenceHeight"),
           QStringLiteral("generatedAlphaRefinementStateSignature"),
           QStringLiteral("generatedAlphaSourceSignature"),
       }) {
    if (source.contains(key)) {
      target->insert(key, source.value(key));
    }
  }
  copy_generated_confidence_payload(target, source);
}

inline void read_inpaint_patch_payload(QVariantMap* target, const QJsonObject& geometry) {
  if (target == nullptr) {
    return;
  }

  const auto patch_width = geometry.value(QStringLiteral("inpaintPatchWidth")).toInt();
  const auto patch_height = geometry.value(QStringLiteral("inpaintPatchHeight")).toInt();
  const auto patch_data =
      detail::read_byte_array(geometry, QStringLiteral("inpaintPatchData"));
  if (patch_data.isEmpty() || patch_width <= 0 || patch_height <= 0) {
    return;
  }

  target->insert(QStringLiteral("inpaintPatchData"), patch_data);
  target->insert(QStringLiteral("inpaintPatchX"), geometry.value(QStringLiteral("inpaintPatchX")).toInt());
  target->insert(QStringLiteral("inpaintPatchY"), geometry.value(QStringLiteral("inpaintPatchY")).toInt());
  target->insert(QStringLiteral("inpaintPatchWidth"), patch_width);
  target->insert(QStringLiteral("inpaintPatchHeight"), patch_height);
  target->insert(
      QStringLiteral("inpaintPatchColorSpace"),
      geometry.value(QStringLiteral("inpaintPatchColorSpace")).toString(QStringLiteral("sRGB")));
  target->insert(
      QStringLiteral("inpaintPatchSchemaVersion"),
      std::max(1, geometry.value(QStringLiteral("inpaintPatchSchemaVersion")).toInt(1)));
  target->insert(
      QStringLiteral("inpaintModelName"),
      geometry.value(QStringLiteral("inpaintModelName")).toString());
  const auto source_mask_data =
      detail::read_byte_array(geometry, QStringLiteral("inpaintSourceMaskData"));
  const auto source_mask_width = geometry.value(QStringLiteral("inpaintSourceMaskWidth")).toInt();
  const auto source_mask_height = geometry.value(QStringLiteral("inpaintSourceMaskHeight")).toInt();
  if (valid_segmentation_payload(source_mask_data, source_mask_width, source_mask_height)) {
    target->insert(QStringLiteral("inpaintSourceMaskData"), source_mask_data);
    target->insert(QStringLiteral("inpaintSourceMaskWidth"), source_mask_width);
    target->insert(QStringLiteral("inpaintSourceMaskHeight"), source_mask_height);
    const auto source_bounds_width =
        geometry.value(QStringLiteral("inpaintSourceBoundsWidth")).toInt();
    const auto source_bounds_height =
        geometry.value(QStringLiteral("inpaintSourceBoundsHeight")).toInt();
    if (source_bounds_width > 0 && source_bounds_height > 0) {
      target->insert(
          QStringLiteral("inpaintSourceBoundsX"),
          geometry.value(QStringLiteral("inpaintSourceBoundsX")).toInt());
      target->insert(
          QStringLiteral("inpaintSourceBoundsY"),
          geometry.value(QStringLiteral("inpaintSourceBoundsY")).toInt());
      target->insert(QStringLiteral("inpaintSourceBoundsWidth"), source_bounds_width);
      target->insert(QStringLiteral("inpaintSourceBoundsHeight"), source_bounds_height);
    }
  }
  const auto selected_variation_index =
      geometry.contains(QStringLiteral("inpaintSelectedVariationIndex"))
          ? geometry.value(QStringLiteral("inpaintSelectedVariationIndex")).toInt()
          : geometry.value(QStringLiteral("inpaintCandidateIndex")).toInt();
  target->insert(QStringLiteral("inpaintCandidateIndex"), selected_variation_index);
  target->insert(QStringLiteral("inpaintSelectedVariationIndex"), selected_variation_index);
  target->insert(
      QStringLiteral("inpaintSelectedVariationSeed"),
      std::max(
          1,
          geometry.contains(QStringLiteral("inpaintSelectedVariationSeed"))
              ? geometry.value(QStringLiteral("inpaintSelectedVariationSeed")).toInt()
              : geometry.value(QStringLiteral("inpaintSeed")).toInt()));
  target->insert(
      QStringLiteral("inpaintSelectedVariationModelName"),
      geometry.contains(QStringLiteral("inpaintSelectedVariationModelName"))
          ? geometry.value(QStringLiteral("inpaintSelectedVariationModelName")).toString()
          : geometry.value(QStringLiteral("inpaintModelName")).toString());
  const auto source_candidates = geometry.value(QStringLiteral("inpaintCandidates")).toArray();
  if (!source_candidates.isEmpty()) {
    QVariantList candidates;
    for (const auto& candidate_value : source_candidates) {
      const auto candidate_object = candidate_value.toObject();
      const auto candidate_data = detail::read_byte_array(candidate_object, QStringLiteral("patchData"));
      const auto candidate_width = candidate_object.value(QStringLiteral("patchWidth")).toInt();
      const auto candidate_height = candidate_object.value(QStringLiteral("patchHeight")).toInt();
      if (candidate_data.isEmpty() || candidate_width <= 0 || candidate_height <= 0) {
        continue;
      }
      QVariantMap candidate;
      candidate.insert(QStringLiteral("patchData"), candidate_data);
      candidate.insert(QStringLiteral("patchX"), candidate_object.value(QStringLiteral("patchX")).toInt());
      candidate.insert(QStringLiteral("patchY"), candidate_object.value(QStringLiteral("patchY")).toInt());
      candidate.insert(QStringLiteral("patchWidth"), candidate_width);
      candidate.insert(QStringLiteral("patchHeight"), candidate_height);
      candidate.insert(
          QStringLiteral("patchColorSpace"),
          candidate_object.value(QStringLiteral("patchColorSpace")).toString(QStringLiteral("sRGB")));
      candidate.insert(QStringLiteral("modelName"), candidate_object.value(QStringLiteral("modelName")).toString());
      candidate.insert(QStringLiteral("index"), candidate_object.value(QStringLiteral("index")).toInt());
      candidate.insert(
          QStringLiteral("seed"),
          std::max(1, candidate_object.value(QStringLiteral("seed")).toInt()));
      candidates.append(candidate);
    }
    if (!candidates.isEmpty()) {
      target->insert(QStringLiteral("inpaintCandidates"), candidates);
      target->insert(QStringLiteral("inpaintCandidateCount"), candidates.size());
    }
  }
  target->insert(
      QStringLiteral("inpaintPatchPayloadBytes"),
      std::max(0, geometry.value(QStringLiteral("inpaintPatchPayloadBytes")).toInt()));
  target->insert(
      QStringLiteral("inpaintCandidatePayloadBytes"),
      std::max(0, geometry.value(QStringLiteral("inpaintCandidatePayloadBytes")).toInt()));
  target->insert(
      QStringLiteral("inpaintCandidateEvictedCount"),
      std::max(0, geometry.value(QStringLiteral("inpaintCandidateEvictedCount")).toInt()));
  const auto prompt = geometry.value(QStringLiteral("inpaintPrompt")).toString();
  if (!prompt.isEmpty()) {
    target->insert(QStringLiteral("inpaintPrompt"), prompt);
  }
  const auto quality_mode = geometry.value(QStringLiteral("inpaintQualityMode")).toString().trimmed().toLower();
  target->insert(
      QStringLiteral("inpaintQualityMode"),
      quality_mode == QStringLiteral("final") ? QStringLiteral("final") : QStringLiteral("preview"));
  target->insert(
      QStringLiteral("inpaintFinalRequired"),
      geometry.contains(QStringLiteral("inpaintFinalRequired"))
          ? geometry.value(QStringLiteral("inpaintFinalRequired")).toBool()
          : quality_mode != QStringLiteral("final"));
  target->insert(
      QStringLiteral("inpaintSeed"),
      std::max(1, geometry.value(QStringLiteral("inpaintSeed")).toInt()));
  target->insert(
      QStringLiteral("inpaintBlendAmount"),
      qBound(0.0, geometry.value(QStringLiteral("inpaintBlendAmount")).toDouble(1.0), 1.0));
  target->insert(
      QStringLiteral("inpaintSeamFeather"),
      qBound(0.0, geometry.value(QStringLiteral("inpaintSeamFeather")).toDouble(4.0), 24.0));
  target->insert(
      QStringLiteral("inpaintColorMatch"),
      qBound(0.0, geometry.value(QStringLiteral("inpaintColorMatch")).toDouble(0.75), 1.0));
  target->insert(
      QStringLiteral("inpaintFallback"),
      geometry.value(QStringLiteral("inpaintFallback")).toBool(false));
  const auto fallback_reason = geometry.value(QStringLiteral("inpaintFallbackReason")).toString();
  if (!fallback_reason.isEmpty()) {
    target->insert(QStringLiteral("inpaintFallbackReason"), fallback_reason);
  }
  const auto region_kind = geometry.value(QStringLiteral("inpaintRegionKind")).toString();
  if (!region_kind.isEmpty()) {
    target->insert(QStringLiteral("inpaintRegionKind"), region_kind);
    target->insert(
        QStringLiteral("inpaintEdgeContinuation"),
        geometry.value(QStringLiteral("inpaintEdgeContinuation")).toBool(false));
    target->insert(
        QStringLiteral("inpaintTextureSynthesis"),
        geometry.value(QStringLiteral("inpaintTextureSynthesis")).toBool(false));
    target->insert(
        QStringLiteral("inpaintTextureExemplar"),
        geometry.value(QStringLiteral("inpaintTextureExemplar")).toBool(false));
    target->insert(
        QStringLiteral("inpaintTextureStrength"),
        qBound(0.0, geometry.value(QStringLiteral("inpaintTextureStrength")).toDouble(), 1.0));
    target->insert(
        QStringLiteral("inpaintRegionCoverage"),
        qBound(0.0, geometry.value(QStringLiteral("inpaintRegionCoverage")).toDouble(), 1.0));
    target->insert(QStringLiteral("inpaintRegionX"), geometry.value(QStringLiteral("inpaintRegionX")).toInt());
    target->insert(QStringLiteral("inpaintRegionY"), geometry.value(QStringLiteral("inpaintRegionY")).toInt());
    target->insert(QStringLiteral("inpaintRegionWidth"), geometry.value(QStringLiteral("inpaintRegionWidth")).toInt());
    target->insert(QStringLiteral("inpaintRegionHeight"), geometry.value(QStringLiteral("inpaintRegionHeight")).toInt());
  }
}

inline void read_generated_mask_metadata(QVariantMap* target, const QJsonObject& geometry) {
  if (target == nullptr) {
    return;
  }
  const auto metadata = geometry.value(QStringLiteral("generatedMaskMetadata")).toObject();
  if (!metadata.isEmpty()) {
    target->insert(QStringLiteral("generatedMaskMetadata"), metadata.toVariantMap());
  }
  if (geometry.contains(QStringLiteral("generatedMaskStale"))) {
    target->insert(
        QStringLiteral("generatedMaskStale"),
        geometry.value(QStringLiteral("generatedMaskStale")).toBool(false));
  }
  for (const auto& key : {
           QStringLiteral("generatedAlphaSourceContract"),
           QStringLiteral("generatedAlphaModelName"),
           QStringLiteral("generatedAlphaModelVersion"),
           QStringLiteral("generatedAlphaTargetType"),
           QStringLiteral("generatedAlphaPromptType"),
           QStringLiteral("generatedAlphaSourceImageVersion"),
           QStringLiteral("generatedAlphaCoordinateSpaceContract"),
           QStringLiteral("generatedAlphaSourceCoordinateSpace"),
           QStringLiteral("generatedAlphaPromptCoordinateSpace"),
           QStringLiteral("generatedAlphaRasterCoordinateSpace"),
           QStringLiteral("generatedAlphaRefinementStateSignature"),
           QStringLiteral("generatedAlphaSourceSignature"),
       }) {
    const auto value = geometry.value(key).toString();
    if (!value.isEmpty()) {
      target->insert(key, value);
    }
  }
  for (const auto& key : {
           QStringLiteral("generatedAlphaWidth"),
           QStringLiteral("generatedAlphaHeight"),
           QStringLiteral("generatedAlphaConfidenceWidth"),
           QStringLiteral("generatedAlphaConfidenceHeight"),
       }) {
    if (geometry.contains(key)) {
      target->insert(key, std::max(0, geometry.value(key).toInt()));
    }
  }
  if (geometry.contains(QStringLiteral("generatedAlphaDownscaleFactor"))) {
    target->insert(
        QStringLiteral("generatedAlphaDownscaleFactor"),
        std::max(0.0, geometry.value(QStringLiteral("generatedAlphaDownscaleFactor")).toDouble()));
  }
  if (geometry.contains(QStringLiteral("generatedAlphaConfidenceAvailable"))) {
    target->insert(
        QStringLiteral("generatedAlphaConfidenceAvailable"),
        geometry.value(QStringLiteral("generatedAlphaConfidenceAvailable")).toBool(false));
  }
  const auto confidence_data = detail::read_byte_array(geometry, QStringLiteral("confidenceData"));
  const auto confidence_width = geometry.value(QStringLiteral("confidenceWidth")).toInt();
  const auto confidence_height = geometry.value(QStringLiteral("confidenceHeight")).toInt();
  if (valid_segmentation_payload(confidence_data, confidence_width, confidence_height)) {
    target->insert(QStringLiteral("confidenceData"), confidence_data);
    target->insert(QStringLiteral("confidenceWidth"), confidence_width);
    target->insert(QStringLiteral("confidenceHeight"), confidence_height);
    target->insert(
        QStringLiteral("confidenceMapSchemaVersion"),
        std::max(1, geometry.value(QStringLiteral("confidenceMapSchemaVersion")).toInt(1)));
  }
}

inline void write_inpaint_patch_payload(QJsonObject* geometry, const QVariantMap& mask_definition) {
  if (geometry == nullptr) {
    return;
  }

  const auto patch_data = mask_definition.value(QStringLiteral("inpaintPatchData")).toByteArray();
  const auto patch_width = mask_definition.value(QStringLiteral("inpaintPatchWidth")).toInt();
  const auto patch_height = mask_definition.value(QStringLiteral("inpaintPatchHeight")).toInt();
  if (patch_data.isEmpty() || patch_width <= 0 || patch_height <= 0) {
    return;
  }

  geometry->insert(
      QStringLiteral("inpaintPatchData"),
      QString::fromLatin1(patch_data.toBase64()));
  geometry->insert(QStringLiteral("inpaintPatchX"), mask_definition.value(QStringLiteral("inpaintPatchX")).toInt());
  geometry->insert(QStringLiteral("inpaintPatchY"), mask_definition.value(QStringLiteral("inpaintPatchY")).toInt());
  geometry->insert(QStringLiteral("inpaintPatchWidth"), patch_width);
  geometry->insert(QStringLiteral("inpaintPatchHeight"), patch_height);
  geometry->insert(
      QStringLiteral("inpaintPatchColorSpace"),
      mask_definition.value(QStringLiteral("inpaintPatchColorSpace"), QStringLiteral("sRGB")).toString());
  geometry->insert(
      QStringLiteral("inpaintPatchSchemaVersion"),
      std::max(1, map_int_value_or(mask_definition, QStringLiteral("inpaintPatchSchemaVersion"), 1)));
  geometry->insert(
      QStringLiteral("inpaintModelName"),
      mask_definition.value(QStringLiteral("inpaintModelName")).toString());
  const auto source_mask_data =
      mask_definition.value(QStringLiteral("inpaintSourceMaskData")).toByteArray();
  const auto source_mask_width = mask_definition.value(QStringLiteral("inpaintSourceMaskWidth")).toInt();
  const auto source_mask_height = mask_definition.value(QStringLiteral("inpaintSourceMaskHeight")).toInt();
  if (valid_segmentation_payload(source_mask_data, source_mask_width, source_mask_height)) {
    geometry->insert(
        QStringLiteral("inpaintSourceMaskData"),
        QString::fromLatin1(source_mask_data.toBase64()));
    geometry->insert(QStringLiteral("inpaintSourceMaskWidth"), source_mask_width);
    geometry->insert(QStringLiteral("inpaintSourceMaskHeight"), source_mask_height);
    const auto source_bounds_width =
        mask_definition.value(QStringLiteral("inpaintSourceBoundsWidth")).toInt();
    const auto source_bounds_height =
        mask_definition.value(QStringLiteral("inpaintSourceBoundsHeight")).toInt();
    if (source_bounds_width > 0 && source_bounds_height > 0) {
      geometry->insert(
          QStringLiteral("inpaintSourceBoundsX"),
          mask_definition.value(QStringLiteral("inpaintSourceBoundsX")).toInt());
      geometry->insert(
          QStringLiteral("inpaintSourceBoundsY"),
          mask_definition.value(QStringLiteral("inpaintSourceBoundsY")).toInt());
      geometry->insert(QStringLiteral("inpaintSourceBoundsWidth"), source_bounds_width);
      geometry->insert(QStringLiteral("inpaintSourceBoundsHeight"), source_bounds_height);
    }
  }
  const auto selected_variation_index =
      mask_definition.contains(QStringLiteral("inpaintSelectedVariationIndex"))
          ? mask_definition.value(QStringLiteral("inpaintSelectedVariationIndex")).toInt()
          : mask_definition.value(QStringLiteral("inpaintCandidateIndex")).toInt();
  geometry->insert(QStringLiteral("inpaintCandidateIndex"), selected_variation_index);
  geometry->insert(QStringLiteral("inpaintSelectedVariationIndex"), selected_variation_index);
  geometry->insert(
      QStringLiteral("inpaintSelectedVariationSeed"),
      std::max(
          1,
          mask_definition.contains(QStringLiteral("inpaintSelectedVariationSeed"))
              ? mask_definition.value(QStringLiteral("inpaintSelectedVariationSeed")).toInt()
              : mask_definition.value(QStringLiteral("inpaintSeed")).toInt()));
  geometry->insert(
      QStringLiteral("inpaintSelectedVariationModelName"),
      mask_definition
          .value(
              QStringLiteral("inpaintSelectedVariationModelName"),
              mask_definition.value(QStringLiteral("inpaintModelName")))
          .toString());
  const auto source_candidates = mask_definition.value(QStringLiteral("inpaintCandidates")).toList();
  if (!source_candidates.isEmpty()) {
    QJsonArray candidates;
    for (const auto& candidate_value : source_candidates) {
      const auto candidate = candidate_value.toMap();
      const auto candidate_data = candidate.value(QStringLiteral("patchData")).toByteArray();
      const auto candidate_width = candidate.value(QStringLiteral("patchWidth")).toInt();
      const auto candidate_height = candidate.value(QStringLiteral("patchHeight")).toInt();
      if (candidate_data.isEmpty() || candidate_width <= 0 || candidate_height <= 0) {
        continue;
      }
      QJsonObject candidate_object;
      candidate_object.insert(QStringLiteral("patchData"), QString::fromLatin1(candidate_data.toBase64()));
      candidate_object.insert(QStringLiteral("patchX"), candidate.value(QStringLiteral("patchX")).toInt());
      candidate_object.insert(QStringLiteral("patchY"), candidate.value(QStringLiteral("patchY")).toInt());
      candidate_object.insert(QStringLiteral("patchWidth"), candidate_width);
      candidate_object.insert(QStringLiteral("patchHeight"), candidate_height);
      candidate_object.insert(
          QStringLiteral("patchColorSpace"),
          candidate.value(QStringLiteral("patchColorSpace"), QStringLiteral("sRGB")).toString());
      candidate_object.insert(QStringLiteral("modelName"), candidate.value(QStringLiteral("modelName")).toString());
      candidate_object.insert(QStringLiteral("index"), candidate.value(QStringLiteral("index")).toInt());
      candidate_object.insert(
          QStringLiteral("seed"),
          std::max(1, candidate.value(QStringLiteral("seed")).toInt()));
      candidates.append(candidate_object);
    }
    if (!candidates.isEmpty()) {
      geometry->insert(QStringLiteral("inpaintCandidates"), candidates);
      geometry->insert(QStringLiteral("inpaintCandidateCount"), candidates.size());
    }
  }
  geometry->insert(
      QStringLiteral("inpaintPatchPayloadBytes"),
      std::max(0, mask_definition.value(QStringLiteral("inpaintPatchPayloadBytes")).toInt()));
  geometry->insert(
      QStringLiteral("inpaintCandidatePayloadBytes"),
      std::max(0, mask_definition.value(QStringLiteral("inpaintCandidatePayloadBytes")).toInt()));
  geometry->insert(
      QStringLiteral("inpaintCandidateEvictedCount"),
      std::max(0, mask_definition.value(QStringLiteral("inpaintCandidateEvictedCount")).toInt()));
  const auto prompt = mask_definition.value(QStringLiteral("inpaintPrompt")).toString();
  if (!prompt.isEmpty()) {
    geometry->insert(QStringLiteral("inpaintPrompt"), prompt);
  }
  const auto quality_mode =
      mask_definition.value(QStringLiteral("inpaintQualityMode")).toString().trimmed().toLower();
  geometry->insert(
      QStringLiteral("inpaintQualityMode"),
      quality_mode == QStringLiteral("final") ? QStringLiteral("final") : QStringLiteral("preview"));
  geometry->insert(
      QStringLiteral("inpaintFinalRequired"),
      mask_definition.contains(QStringLiteral("inpaintFinalRequired"))
          ? mask_definition.value(QStringLiteral("inpaintFinalRequired")).toBool()
          : quality_mode != QStringLiteral("final"));
  geometry->insert(
      QStringLiteral("inpaintSeed"),
      std::max(1, mask_definition.value(QStringLiteral("inpaintSeed")).toInt()));
  geometry->insert(
      QStringLiteral("inpaintBlendAmount"),
      qBound(0.0, mask_definition.value(QStringLiteral("inpaintBlendAmount"), 1.0).toDouble(), 1.0));
  geometry->insert(
      QStringLiteral("inpaintSeamFeather"),
      qBound(0.0, mask_definition.value(QStringLiteral("inpaintSeamFeather"), 4.0).toDouble(), 24.0));
  geometry->insert(
      QStringLiteral("inpaintColorMatch"),
      qBound(0.0, mask_definition.value(QStringLiteral("inpaintColorMatch"), 0.75).toDouble(), 1.0));
  geometry->insert(
      QStringLiteral("inpaintFallback"),
      mask_definition.value(QStringLiteral("inpaintFallback")).toBool());
  const auto fallback_reason =
      mask_definition.value(QStringLiteral("inpaintFallbackReason")).toString();
  if (!fallback_reason.isEmpty()) {
    geometry->insert(QStringLiteral("inpaintFallbackReason"), fallback_reason);
  }
  const auto region_kind = mask_definition.value(QStringLiteral("inpaintRegionKind")).toString();
  if (!region_kind.isEmpty()) {
    geometry->insert(QStringLiteral("inpaintRegionKind"), region_kind);
    geometry->insert(
        QStringLiteral("inpaintEdgeContinuation"),
        mask_definition.value(QStringLiteral("inpaintEdgeContinuation")).toBool());
    geometry->insert(
        QStringLiteral("inpaintTextureSynthesis"),
        mask_definition.value(QStringLiteral("inpaintTextureSynthesis")).toBool());
    geometry->insert(
        QStringLiteral("inpaintTextureExemplar"),
        mask_definition.value(QStringLiteral("inpaintTextureExemplar")).toBool());
    geometry->insert(
        QStringLiteral("inpaintTextureStrength"),
        qBound(0.0, mask_definition.value(QStringLiteral("inpaintTextureStrength")).toDouble(), 1.0));
    geometry->insert(
        QStringLiteral("inpaintRegionCoverage"),
        qBound(0.0, mask_definition.value(QStringLiteral("inpaintRegionCoverage")).toDouble(), 1.0));
    geometry->insert(
        QStringLiteral("inpaintRegionX"),
        mask_definition.value(QStringLiteral("inpaintRegionX")).toInt());
    geometry->insert(
        QStringLiteral("inpaintRegionY"),
        mask_definition.value(QStringLiteral("inpaintRegionY")).toInt());
    geometry->insert(
        QStringLiteral("inpaintRegionWidth"),
        mask_definition.value(QStringLiteral("inpaintRegionWidth")).toInt());
    geometry->insert(
        QStringLiteral("inpaintRegionHeight"),
        mask_definition.value(QStringLiteral("inpaintRegionHeight")).toInt());
  }
}

inline void write_generated_mask_metadata(QJsonObject* geometry, const QVariantMap& mask_definition) {
  if (geometry == nullptr) {
    return;
  }
  const auto metadata = mask_definition.value(QStringLiteral("generatedMaskMetadata")).toMap();
  if (!metadata.isEmpty()) {
    geometry->insert(QStringLiteral("generatedMaskMetadata"), QJsonObject::fromVariantMap(metadata));
  }
  if (mask_definition.contains(QStringLiteral("generatedMaskStale"))) {
    geometry->insert(
        QStringLiteral("generatedMaskStale"),
        mask_definition.value(QStringLiteral("generatedMaskStale")).toBool());
  }
  for (const auto& key : {
           QStringLiteral("generatedAlphaSourceContract"),
           QStringLiteral("generatedAlphaModelName"),
           QStringLiteral("generatedAlphaModelVersion"),
           QStringLiteral("generatedAlphaTargetType"),
           QStringLiteral("generatedAlphaPromptType"),
           QStringLiteral("generatedAlphaSourceImageVersion"),
           QStringLiteral("generatedAlphaWidth"),
           QStringLiteral("generatedAlphaHeight"),
           QStringLiteral("generatedAlphaCoordinateSpaceContract"),
           QStringLiteral("generatedAlphaSourceCoordinateSpace"),
           QStringLiteral("generatedAlphaPromptCoordinateSpace"),
           QStringLiteral("generatedAlphaRasterCoordinateSpace"),
           QStringLiteral("generatedAlphaDownscaleFactor"),
           QStringLiteral("generatedAlphaConfidenceAvailable"),
           QStringLiteral("generatedAlphaConfidenceWidth"),
           QStringLiteral("generatedAlphaConfidenceHeight"),
           QStringLiteral("generatedAlphaRefinementStateSignature"),
           QStringLiteral("generatedAlphaSourceSignature"),
       }) {
    if (mask_definition.contains(key)) {
      geometry->insert(key, QJsonValue::fromVariant(mask_definition.value(key)));
    }
  }
  const auto confidence_data = mask_definition.value(QStringLiteral("confidenceData")).toByteArray();
  const auto confidence_width = mask_definition.value(QStringLiteral("confidenceWidth")).toInt();
  const auto confidence_height = mask_definition.value(QStringLiteral("confidenceHeight")).toInt();
  if (valid_segmentation_payload(confidence_data, confidence_width, confidence_height)) {
    geometry->insert(
        QStringLiteral("confidenceData"),
        QString::fromLatin1(confidence_data.toBase64()));
    geometry->insert(QStringLiteral("confidenceWidth"), confidence_width);
    geometry->insert(QStringLiteral("confidenceHeight"), confidence_height);
    geometry->insert(
        QStringLiteral("confidenceMapSchemaVersion"),
        std::max(1, map_int_value_or(mask_definition, QStringLiteral("confidenceMapSchemaVersion"), 1)));
  }
}

inline QString normalized_mask_kind(const QString& kind) {
  const auto normalized = kind.trimmed().toLower();
  const auto canonical = normalized == QStringLiteral("person") ? QStringLiteral("people") : normalized;
  if (canonical == QStringLiteral("linear") || canonical == QStringLiteral("radial") ||
      canonical == QStringLiteral("ellipse") || canonical == QStringLiteral("rectangle") ||
      canonical == QStringLiteral("brush") || canonical == QStringLiteral("polygon") ||
      canonical == QStringLiteral("lasso") || canonical == QStringLiteral("luminance") ||
      canonical == QStringLiteral("color") || canonical == QStringLiteral("depth") ||
      canonical == QStringLiteral("alpha") ||
      canonical == QStringLiteral("subject") || canonical == QStringLiteral("sky") ||
      canonical == QStringLiteral("background") || canonical == QStringLiteral("people") ||
      canonical == QStringLiteral("face") || canonical == QStringLiteral("hair") ||
      canonical == QStringLiteral("clothing") || canonical == QStringLiteral("object")) {
    return canonical;
  }
  return QStringLiteral("linear");
}

inline bool is_center_radius_mask_kind(const QString& kind) {
  const auto normalized = normalized_mask_kind(kind);
  return normalized == QStringLiteral("radial") || normalized == QStringLiteral("ellipse") ||
         normalized == QStringLiteral("rectangle");
}

inline bool is_ai_segmentation_mask_kind(const QString& kind) {
  const auto normalized = normalized_mask_kind(kind);
  return normalized == QStringLiteral("subject") || normalized == QStringLiteral("sky") ||
         normalized == QStringLiteral("background") || normalized == QStringLiteral("people") ||
         normalized == QStringLiteral("face") || normalized == QStringLiteral("hair") ||
         normalized == QStringLiteral("clothing") || normalized == QStringLiteral("object");
}

inline bool is_raster_mask_kind(const QString& kind) {
  const auto normalized = normalized_mask_kind(kind);
  return normalized == QStringLiteral("alpha") || is_ai_segmentation_mask_kind(normalized);
}

inline bool is_range_mask_kind(const QString& kind) {
  const auto normalized = normalized_mask_kind(kind);
  return normalized == QStringLiteral("luminance") || normalized == QStringLiteral("color") ||
         normalized == QStringLiteral("depth");
}

inline QString inferred_mask_source_class(
    const QString& kind,
    const QString& tool_role,
    const QVariantMap& candidate) {
  const auto normalized_kind = normalized_mask_kind(kind);
  if (tool_role.trimmed().toLower() == QStringLiteral("retouch")) {
    return QStringLiteral("retouch-patch-mask");
  }
  if (is_range_mask_kind(normalized_kind)) {
    return QStringLiteral("analytic-range-rule");
  }
  if (is_raster_mask_kind(normalized_kind) ||
      valid_segmentation_payload(
          candidate.value(QStringLiteral("segmentationData")).toByteArray(),
          candidate.value(QStringLiteral("segmentationWidth")).toInt(),
          candidate.value(QStringLiteral("segmentationHeight")).toInt())) {
    return QStringLiteral("source-resolution-alpha");
  }
  return QStringLiteral("vector-geometry");
}

inline QString default_mask_coordinate_space(const QString& mask_source_class) {
  if (mask_source_class == QStringLiteral("source-resolution-alpha") ||
      mask_source_class == QStringLiteral("retouch-patch-mask")) {
    return QStringLiteral("source-pixels");
  }
  return QStringLiteral("source-normalized");
}

inline QString default_alpha_sampling_mode(const QString& mask_source_class, const QVariantMap& candidate) {
  const auto pixel_mode =
      candidate.value(QStringLiteral("pixelAlphaSamplingMode")).toString().trimmed();
  if (mask_source_class == QStringLiteral("source-resolution-alpha") &&
      !pixel_mode.isEmpty()) {
    return pixel_mode;
  }
  if (mask_source_class == QStringLiteral("analytic-range-rule") ||
      mask_source_class == QStringLiteral("vector-geometry")) {
    return QStringLiteral("analytic");
  }
  return QStringLiteral("nearest-source-pixel");
}

inline QString default_softness_mode(const QString& kind, const QString& mask_source_class, const QVariantMap& candidate) {
  const auto normalized_kind = normalized_mask_kind(kind);
  if (normalized_kind == QStringLiteral("brush")) {
    return candidate.value(QStringLiteral("pixelSelection")).toBool()
               ? QStringLiteral("hard-pixel")
               : QStringLiteral("brush-falloff");
  }
  if (mask_source_class == QStringLiteral("analytic-range-rule")) {
    return QStringLiteral("range-feather");
  }
  if (mask_source_class == QStringLiteral("vector-geometry")) {
    return QStringLiteral("geometric-feather");
  }
  return QStringLiteral("alpha-hard-edge");
}

inline void insert_mask_source_truth_metadata(
    QVariantMap* mask,
    const QVariantMap& candidate,
    const QString& kind,
    const QString& tool_role) {
  if (mask == nullptr) {
    return;
  }
  const auto source_class = inferred_mask_source_class(kind, tool_role, candidate);
  const auto source_revision =
      candidate.value(QStringLiteral("sourceImageRevision")).toString().trimmed();
  auto width = 0;
  auto height = 0;
  if (source_class == QStringLiteral("retouch-patch-mask") &&
      candidate.contains(QStringLiteral("inpaintPatchWidth")) &&
      candidate.contains(QStringLiteral("inpaintPatchHeight"))) {
    width = candidate.value(QStringLiteral("inpaintPatchWidth")).toInt();
    height = candidate.value(QStringLiteral("inpaintPatchHeight")).toInt();
  } else if (source_class == QStringLiteral("source-resolution-alpha") &&
             (candidate.contains(QStringLiteral("segmentationWidth")) ||
              candidate.contains(QStringLiteral("segmentationHeight")))) {
    width = candidate.value(QStringLiteral("segmentationWidth")).toInt();
    height = candidate.value(QStringLiteral("segmentationHeight")).toInt();
  } else if (source_class == QStringLiteral("source-resolution-alpha") &&
             (candidate.contains(QStringLiteral("maskRasterWidth")) ||
              candidate.contains(QStringLiteral("maskRasterHeight")))) {
    width = candidate.value(QStringLiteral("maskRasterWidth")).toInt();
    height = candidate.value(QStringLiteral("maskRasterHeight")).toInt();
  } else if (source_class == QStringLiteral("analytic-range-rule") &&
             (candidate.contains(QStringLiteral("depthWidth")) ||
              candidate.contains(QStringLiteral("depthHeight")))) {
    width = candidate.value(QStringLiteral("depthWidth")).toInt();
    height = candidate.value(QStringLiteral("depthHeight")).toInt();
  }
  mask->insert(QStringLiteral("maskSourceClass"), source_class);
  mask->insert(
      QStringLiteral("maskCoordinateSpace"),
      default_mask_coordinate_space(source_class));
  mask->insert(QStringLiteral("maskRasterWidth"), std::max(0, width));
  mask->insert(QStringLiteral("maskRasterHeight"), std::max(0, height));
  mask->insert(QStringLiteral("alphaSamplingMode"), default_alpha_sampling_mode(source_class, candidate));
  mask->insert(QStringLiteral("softnessMode"), default_softness_mode(kind, source_class, candidate));
  mask->insert(QStringLiteral("sourceImageRevision"), source_revision);
}

inline QString ai_segmentation_mask_label(const QString& kind) {
  const auto normalized = normalized_mask_kind(kind);
  if (normalized == QStringLiteral("sky")) {
    return QStringLiteral("Sky");
  }
  if (normalized == QStringLiteral("background")) {
    return QStringLiteral("Background");
  }
  if (normalized == QStringLiteral("people")) {
    return QStringLiteral("People");
  }
  if (normalized == QStringLiteral("face")) {
    return QStringLiteral("Face");
  }
  if (normalized == QStringLiteral("hair")) {
    return QStringLiteral("Hair");
  }
  if (normalized == QStringLiteral("clothing")) {
    return QStringLiteral("Clothing");
  }
  if (normalized == QStringLiteral("object")) {
    return QStringLiteral("Object");
  }
  if (normalized == QStringLiteral("alpha")) {
    return QStringLiteral("Alpha");
  }
  return QStringLiteral("Subject");
}

inline QString normalized_mask_operation(const QString& operation) {
  const auto normalized = operation.trimmed().toLower();
  if (normalized == QStringLiteral("add") || normalized == QStringLiteral("subtract") ||
      normalized == QStringLiteral("intersect") || normalized == QStringLiteral("exclude") ||
      normalized == QStringLiteral("replace") || normalized == QStringLiteral("union")) {
    return normalized;
  }
  return QStringLiteral("add");
}

inline QString normalized_brush_stroke_operation(const QString& operation) {
  const auto normalized = operation.trimmed().toLower();
  if (normalized == QStringLiteral("erase")) {
    return normalized;
  }
  return normalized_mask_operation(normalized);
}

inline QString normalized_mask_blend_mode(const QString& blend_mode) {
  auto normalized = blend_mode.trimmed().toLower();
  if (normalized == QStringLiteral("soft light")) {
    normalized = QStringLiteral("soft-light");
  } else if (normalized == QStringLiteral("hard light")) {
    normalized = QStringLiteral("hard-light");
  } else if (normalized == QStringLiteral("linear light")) {
    normalized = QStringLiteral("linear-light");
  }
  if (normalized == QStringLiteral("normal") || normalized == QStringLiteral("multiply") ||
      normalized == QStringLiteral("screen") || normalized == QStringLiteral("overlay") ||
      normalized == QStringLiteral("soft-light") ||
      normalized == QStringLiteral("hard-light") ||
      normalized == QStringLiteral("color") || normalized == QStringLiteral("luminosity") ||
      normalized == QStringLiteral("hue") || normalized == QStringLiteral("saturation") ||
      normalized == QStringLiteral("difference") || normalized == QStringLiteral("linear-light") ||
      normalized == QStringLiteral("lighten") || normalized == QStringLiteral("darken") ||
      normalized == QStringLiteral("replace")) {
    return normalized;
  }
  return QStringLiteral("normal");
}

inline QString normalized_mask_tool_role(const QString& tool_role) {
  const auto normalized = tool_role.trimmed().toLower();
  if (normalized == QStringLiteral("retouch")) {
    return normalized;
  }
  return QStringLiteral("selection");
}

inline bool is_selection_mask_tool_role(const QString& tool_role) {
  return normalized_mask_tool_role(tool_role) == QStringLiteral("selection");
}

inline QString default_mask_set_name(const QString& kind, int ordinal) {
  const auto normalized_kind = normalized_mask_kind(kind);
  QString label = QStringLiteral("Linear");
  if (normalized_kind == QStringLiteral("radial")) {
    label = QStringLiteral("Radial");
  } else if (normalized_kind == QStringLiteral("ellipse")) {
    label = QStringLiteral("Ellipse");
  } else if (normalized_kind == QStringLiteral("rectangle")) {
    label = QStringLiteral("Rectangle");
  } else if (normalized_kind == QStringLiteral("brush")) {
    label = QStringLiteral("Brush");
  } else if (normalized_kind == QStringLiteral("polygon")) {
    label = QStringLiteral("Polygon");
  } else if (normalized_kind == QStringLiteral("lasso")) {
    label = QStringLiteral("Lasso");
  } else if (normalized_kind == QStringLiteral("luminance")) {
    label = QStringLiteral("Luminance");
  } else if (normalized_kind == QStringLiteral("color")) {
    label = QStringLiteral("Color");
  } else if (normalized_kind == QStringLiteral("depth")) {
    label = QStringLiteral("Depth");
  } else if (normalized_kind == QStringLiteral("alpha")) {
    label = QStringLiteral("Alpha");
  } else if (is_ai_segmentation_mask_kind(normalized_kind)) {
    label = ai_segmentation_mask_label(normalized_kind);
  }
  return QStringLiteral("%1 Mask %2").arg(label).arg(ordinal);
}

inline MatteCleanupDefaults matte_cleanup_defaults_for_kind(const QString& kind) {
  const auto normalized_kind = normalized_mask_kind(kind);
  if (normalized_kind == QStringLiteral("hair")) {
    return MatteCleanupDefaults{
        .profile = "hair-soft-detail",
        .smooth = 6.0,
        .fine_edge = 65.0,
        .decontaminate = 8.0,
        .feather = 10.0,
        .despeckle = 12.0,
        .fill_holes = 5.0,
        .remove_islands = 8.0,
        .generated_hole_threshold = 0.0025,
        .generated_island_threshold = 0.0010,
    };
  }
  if (normalized_kind == QStringLiteral("sky")) {
    return MatteCleanupDefaults{
        .profile = "sky-large-region",
        .smooth = 4.0,
        .edge_contrast = 10.0,
        .feather = 4.0,
        .despeckle = 8.0,
        .fill_holes = 14.0,
        .remove_islands = 18.0,
        .generated_hole_threshold = 0.0100,
        .generated_island_threshold = 0.0040,
    };
  }
  if (normalized_kind == QStringLiteral("object") ||
      normalized_kind == QStringLiteral("subject") ||
      normalized_kind == QStringLiteral("people") ||
      normalized_kind == QStringLiteral("background")) {
    return MatteCleanupDefaults{
        .profile = normalized_kind == QStringLiteral("object") ? "object-hard-edge"
                                                               : "subject-object",
        .smooth = 3.0,
        .edge_contrast = normalized_kind == QStringLiteral("object") ? 8.0 : 4.0,
        .feather = 4.0,
        .despeckle = 6.0,
        .fill_holes = 8.0,
        .remove_islands = 10.0,
        .generated_hole_threshold = 0.0060,
        .generated_island_threshold = 0.0025,
    };
  }
  if (normalized_kind == QStringLiteral("brush")) {
    return MatteCleanupDefaults{
        .profile = "brush-painted",
        .despeckle = 3.0,
        .fill_holes = 2.0,
        .remove_islands = 2.0,
    };
  }
  if (normalized_kind == QStringLiteral("polygon") || normalized_kind == QStringLiteral("lasso")) {
    return MatteCleanupDefaults{
        .profile = "manual-shape",
        .smooth = 2.0,
        .edge_contrast = 6.0,
        .harden = 4.0,
    };
  }
  if (normalized_kind == QStringLiteral("alpha")) {
    return MatteCleanupDefaults{
        .profile = "alpha-import",
        .smooth = 3.0,
        .edge_contrast = 8.0,
        .feather = 4.0,
        .despeckle = 10.0,
        .fill_holes = 10.0,
        .remove_islands = 12.0,
        .generated_hole_threshold = 0.0060,
        .generated_island_threshold = 0.0030,
    };
  }
  return {};
}

inline QVariantMap default_mask_definition(const QString& kind, int ordinal = 1) {
  static int unique_suffix = 0;
  ++unique_suffix;
  const auto normalized_kind = normalized_mask_kind(kind);
  const auto matte_cleanup_defaults = matte_cleanup_defaults_for_kind(normalized_kind);
  QVariantMap mask;
  const auto mask_id =
      QStringLiteral("%1-mask-%2-%3").arg(normalized_kind).arg(ordinal).arg(unique_suffix);
  mask.insert(QStringLiteral("id"), mask_id);
  mask.insert(QStringLiteral("schemaVersion"), 1);
  mask.insert(QStringLiteral("sourceSchemaVersion"), 1);
  mask.insert(QStringLiteral("parentId"), QString());
  mask.insert(QStringLiteral("outputId"), QString());
  mask.insert(QStringLiteral("reusableOutput"), false);
  QString label = QStringLiteral("Linear");
  if (normalized_kind == QStringLiteral("radial")) {
    label = QStringLiteral("Radial");
  } else if (normalized_kind == QStringLiteral("ellipse")) {
    label = QStringLiteral("Ellipse");
  } else if (normalized_kind == QStringLiteral("rectangle")) {
    label = QStringLiteral("Rectangle");
  } else if (normalized_kind == QStringLiteral("brush")) {
    label = QStringLiteral("Brush");
  } else if (normalized_kind == QStringLiteral("polygon")) {
    label = QStringLiteral("Polygon");
  } else if (normalized_kind == QStringLiteral("lasso")) {
    label = QStringLiteral("Lasso");
  } else if (normalized_kind == QStringLiteral("luminance")) {
    label = QStringLiteral("Luminance");
  } else if (normalized_kind == QStringLiteral("color")) {
    label = QStringLiteral("Color");
  } else if (normalized_kind == QStringLiteral("depth")) {
    label = QStringLiteral("Depth");
  } else if (normalized_kind == QStringLiteral("alpha")) {
    label = QStringLiteral("Alpha");
  } else if (is_ai_segmentation_mask_kind(normalized_kind)) {
    label = ai_segmentation_mask_label(normalized_kind);
  }
  mask.insert(QStringLiteral("name"), QStringLiteral("%1 %2").arg(label).arg(ordinal));
  mask.insert(QStringLiteral("kind"), normalized_kind);
  mask.insert(QStringLiteral("toolRole"), QStringLiteral("selection"));
  mask.insert(QStringLiteral("maskSetId"), QStringLiteral("%1-set-%2-%3").arg(normalized_kind).arg(ordinal).arg(unique_suffix));
  mask.insert(QStringLiteral("maskSetName"), default_mask_set_name(normalized_kind, ordinal));
  mask.insert(QStringLiteral("maskSetSelectionInvert"), false);
  mask.insert(QStringLiteral("maskSetSelectionExpand"), 0.0);
  mask.insert(QStringLiteral("maskSetSelectionSmooth"), 0.0);
  mask.insert(QStringLiteral("maskSetSelectionFineEdge"), 0.0);
  mask.insert(QStringLiteral("maskSetSelectionEdgeContrast"), 0.0);
  mask.insert(QStringLiteral("maskSetDensity"), 1.0);
  mask.insert(QStringLiteral("maskSetOpacity"), 1.0);
  mask.insert(QStringLiteral("maskSetPreviewBypassed"), false);
  mask.insert(QStringLiteral("operation"), QStringLiteral("add"));
  mask.insert(QStringLiteral("enabled"), true);
  mask.insert(QStringLiteral("solo"), false);
  mask.insert(QStringLiteral("locked"), false);
  mask.insert(QStringLiteral("committed"), true);
  mask.insert(QStringLiteral("opacity"), 1.0);
  mask.insert(QStringLiteral("density"), 1.0);
  mask.insert(QStringLiteral("invert"), false);
  mask.insert(QStringLiteral("blendMode"), QStringLiteral("normal"));
  mask.insert(QStringLiteral("exposure"), 0.0);
  mask.insert(QStringLiteral("contrast"), 0.0);
  mask.insert(QStringLiteral("highlights"), 0.0);
  mask.insert(QStringLiteral("shadows"), 0.0);
  mask.insert(QStringLiteral("whites"), 0.0);
  mask.insert(QStringLiteral("blacks"), 0.0);
  mask.insert(QStringLiteral("temperature"), 0.0);
  mask.insert(QStringLiteral("tint"), 0.0);
  mask.insert(QStringLiteral("vibrance"), 0.0);
  mask.insert(QStringLiteral("saturation"), 0.0);
  mask.insert(QStringLiteral("hueShift"), 0.0);
  mask.insert(QStringLiteral("saturationShift"), 0.0);
  mask.insert(QStringLiteral("colorizeHue"), 0.0);
  mask.insert(QStringLiteral("colorizeStrength"), 0.0);
  mask.insert(QStringLiteral("removeStrength"), 0.0);
  mask.insert(QStringLiteral("healTextureBlend"), 0.68);
  mask.insert(QStringLiteral("healColorBlend"), 0.46);
  mask.insert(QStringLiteral("retouchMode"), QString());
  mask.insert(QStringLiteral("retouchOrder"), 0);
  mask.insert(QStringLiteral("selectionInvert"), false);
  mask.insert(QStringLiteral("selectionExpand"), 0.0);
  mask.insert(QStringLiteral("selectionSmooth"), matte_cleanup_defaults.smooth);
  mask.insert(QStringLiteral("selectionFineEdge"), matte_cleanup_defaults.fine_edge);
  mask.insert(QStringLiteral("selectionEdgeContrast"), matte_cleanup_defaults.edge_contrast);
  mask.insert(QStringLiteral("selectionDecontaminate"), matte_cleanup_defaults.decontaminate);
  mask.insert(QStringLiteral("selectionGrow"), 0.0);
  mask.insert(QStringLiteral("selectionShrink"), 0.0);
  mask.insert(QStringLiteral("selectionFeather"), matte_cleanup_defaults.feather);
  mask.insert(QStringLiteral("selectionBlur"), 0.0);
  mask.insert(QStringLiteral("selectionHarden"), matte_cleanup_defaults.harden);
  mask.insert(QStringLiteral("selectionDespeckle"), matte_cleanup_defaults.despeckle);
  mask.insert(QStringLiteral("selectionFillHoles"), matte_cleanup_defaults.fill_holes);
  mask.insert(QStringLiteral("selectionRemoveIslands"), matte_cleanup_defaults.remove_islands);
  mask.insert(QStringLiteral("matteCleanupSchemaVersion"), 1);
  mask.insert(QStringLiteral("matteCleanupProfile"), QString::fromLatin1(matte_cleanup_defaults.profile));
  mask.insert(QStringLiteral("matteCleanupDefaultsApplied"), true);
  mask.insert(QStringLiteral("refineLuminanceMin"), 0.0);
  mask.insert(QStringLiteral("refineLuminanceMax"), 1.0);
  mask.insert(QStringLiteral("refineHueCenter"), 0.0);
  mask.insert(QStringLiteral("refineHueRange"), 0.5);
  mask.insert(QStringLiteral("refineSaturationMin"), 0.0);
  mask.insert(QStringLiteral("refineSaturationMax"), 1.0);
  mask.insert(QStringLiteral("refineDepthMin"), 0.0);
  mask.insert(QStringLiteral("refineDepthMax"), 1.0);
  mask.insert(QStringLiteral("refineEdgeConfidenceMin"), 0.0);
  mask.insert(QStringLiteral("refineRangeFeather"), 0.05);
  mask.insert(QStringLiteral("generatedMaskHoleFillThreshold"), matte_cleanup_defaults.generated_hole_threshold);
  mask.insert(QStringLiteral("generatedMaskIslandRemovalThreshold"), matte_cleanup_defaults.generated_island_threshold);
  mask.insert(QStringLiteral("generatedMaskHolesFilled"), 0);
  mask.insert(QStringLiteral("generatedMaskIslandsRemoved"), 0);
  mask.insert(QStringLiteral("generatedMaskPixelsFilled"), 0);
  mask.insert(QStringLiteral("generatedMaskPixelsRemoved"), 0);
  mask.insert(
      QStringLiteral("feather"),
      normalized_kind == QStringLiteral("radial") ? 0.25
      : normalized_kind == QStringLiteral("ellipse") || normalized_kind == QStringLiteral("rectangle") ? 0.08
      : normalized_kind == QStringLiteral("brush") ? 0.75
      : normalized_kind == QStringLiteral("polygon") || normalized_kind == QStringLiteral("lasso") ? 0.06
      : is_raster_mask_kind(normalized_kind) ? 0.5
      : 0.22);
  mask.insert(QStringLiteral("rotation"), 0.0);
  if (is_center_radius_mask_kind(normalized_kind)) {
    mask.insert(QStringLiteral("centerX"), 0.5);
    mask.insert(QStringLiteral("centerY"), 0.5);
    mask.insert(QStringLiteral("radiusX"), normalized_kind == QStringLiteral("rectangle") ? 0.30 : 0.25);
    mask.insert(QStringLiteral("radiusY"), normalized_kind == QStringLiteral("rectangle") ? 0.20 : 0.25);
  } else if (normalized_kind == QStringLiteral("linear")) {
    mask.insert(QStringLiteral("position"), 0.5);
  } else if (normalized_kind == QStringLiteral("brush")) {
    mask.insert(QStringLiteral("retouchMode"), QString());
    mask.insert(QStringLiteral("brushPreset"), QStringLiteral("Soft Round"));
    mask.insert(QStringLiteral("brushSize"), 0.08);
    mask.insert(QStringLiteral("brushFlow"), 1.0);
    mask.insert(QStringLiteral("brushHardness"), 0.25);
    mask.insert(QStringLiteral("brushSpacing"), 0.75);
    mask.insert(QStringLiteral("brushSmoothing"), 0.0);
    mask.insert(QStringLiteral("brushPressureSize"), 0.65);
    mask.insert(QStringLiteral("brushPressureFlow"), 1.0);
    mask.insert(QStringLiteral("brushPressureOpacity"), 0.0);
    mask.insert(QStringLiteral("brushBuildup"), true);
    mask.insert(QStringLiteral("brushEraseMode"), false);
    mask.insert(QStringLiteral("brushAutoMask"), false);
    mask.insert(QStringLiteral("brushAutoMaskTolerance"), 0.18);
    mask.insert(QStringLiteral("brushAutoMaskContinuousSampling"), false);
    mask.insert(QStringLiteral("brushProtectEdges"), false);
    mask.insert(QStringLiteral("brushRefineMode"), false);
    mask.insert(QStringLiteral("refineBrushRecalculate"), false);
    mask.insert(QStringLiteral("brushRefineTargetId"), QString());
    mask.insert(QStringLiteral("brushRefineTargetName"), QString());
    mask.insert(QStringLiteral("brushRefineTargetKind"), QString());
    mask.insert(QStringLiteral("cloneSourceSet"), false);
    mask.insert(QStringLiteral("cloneSourceX"), 0.5);
    mask.insert(QStringLiteral("cloneSourceY"), 0.5);
    mask.insert(QStringLiteral("cloneOffsetX"), 0.0);
    mask.insert(QStringLiteral("cloneOffsetY"), 0.0);
    mask.insert(QStringLiteral("cloneScale"), 1.0);
    mask.insert(QStringLiteral("cloneRotation"), 0.0);
    mask.insert(QStringLiteral("cloneAligned"), true);
    mask.insert(QStringLiteral("cloneSources"), QVariantList{});
    mask.insert(QStringLiteral("cloneSourceIndex"), -1);
    mask.insert(QStringLiteral("patchSourceSet"), false);
    mask.insert(QStringLiteral("patchSourceX"), 0.5);
    mask.insert(QStringLiteral("patchSourceY"), 0.5);
    mask.insert(QStringLiteral("patchSourceWidth"), 0.18);
    mask.insert(QStringLiteral("patchSourceHeight"), 0.18);
    mask.insert(QStringLiteral("brushPoints"), QVariantList{});
    mask.insert(QStringLiteral("brushStrokes"), QVariantList{});
  } else if (normalized_kind == QStringLiteral("polygon") || normalized_kind == QStringLiteral("lasso")) {
    mask.insert(QStringLiteral("polygonPoints"), QVariantList{});
    if (normalized_kind == QStringLiteral("lasso")) {
      mask.insert(QStringLiteral("lassoRawPoints"), QVariantList{});
      mask.insert(QStringLiteral("lassoRawPointCount"), 0);
      mask.insert(QStringLiteral("lassoSimplifiedPointCount"), 0);
      mask.insert(QStringLiteral("lassoClosed"), false);
      mask.insert(QStringLiteral("lassoSimplified"), false);
      mask.insert(QStringLiteral("lassoSmoothed"), false);
      mask.insert(QStringLiteral("lassoFreehandPath"), true);
    }
  } else if (normalized_kind == QStringLiteral("luminance")) {
    mask.insert(QStringLiteral("luminanceMin"), 0.2);
    mask.insert(QStringLiteral("luminanceMax"), 0.8);
    mask.insert(QStringLiteral("rangeFeather"), 0.1);
    mask.insert(QStringLiteral("luminanceSamples"), QVariantList{});
  } else if (normalized_kind == QStringLiteral("color")) {
    mask.insert(QStringLiteral("hueCenter"), 0.08);
    mask.insert(QStringLiteral("hueRange"), 0.12);
    mask.insert(QStringLiteral("saturationMin"), 0.1);
    mask.insert(QStringLiteral("saturationMax"), 1.0);
    mask.insert(QStringLiteral("rangeFeather"), 0.08);
    mask.insert(QStringLiteral("colorSamples"), QVariantList{});
  } else if (normalized_kind == QStringLiteral("depth")) {
    mask.insert(QStringLiteral("depthMin"), 0.2);
    mask.insert(QStringLiteral("depthMax"), 0.8);
    mask.insert(QStringLiteral("rangeFeather"), 0.1);
    mask.insert(QStringLiteral("depthSamples"), QVariantList{});
  } else if (normalized_kind == QStringLiteral("alpha")) {
    mask.insert(QStringLiteral("alphaSourcePath"), QString());
    mask.insert(QStringLiteral("alphaImportInvert"), false);
    mask.insert(QStringLiteral("alphaImportThreshold"), 0.0);
    mask.insert(QStringLiteral("alphaImportResampleMode"), QStringLiteral("Original"));
  }
  return mask;
}

inline QVariantList normalized_color_range_samples(const QVariantList& sample_values) {
  QVariantList samples;
  const auto start_index =
      sample_values.size() > qsizetype{5} ? sample_values.size() - qsizetype{5} : qsizetype{0};
  for (auto index = start_index; index < sample_values.size(); ++index) {
    const auto sample = sample_values.at(index).toMap();
    if (sample.isEmpty()) {
      continue;
    }
    QVariantMap normalized_sample;
    normalized_sample.insert(
        QStringLiteral("x"), qBound(0.0, sample.value(QStringLiteral("x")).toDouble(), 1.0));
    normalized_sample.insert(
        QStringLiteral("y"), qBound(0.0, sample.value(QStringLiteral("y")).toDouble(), 1.0));
    normalized_sample.insert(
        QStringLiteral("hue"), qBound(0.0, sample.value(QStringLiteral("hue")).toDouble(), 1.0));
    normalized_sample.insert(
        QStringLiteral("saturation"),
        qBound(0.0, sample.value(QStringLiteral("saturation")).toDouble(), 1.0));
    normalized_sample.insert(
        QStringLiteral("luminance"),
        qBound(0.0, sample.value(QStringLiteral("luminance")).toDouble(), 1.0));
    normalized_sample.insert(
        QStringLiteral("red"), qBound(0, sample.value(QStringLiteral("red")).toInt(), 255));
    normalized_sample.insert(
        QStringLiteral("green"), qBound(0, sample.value(QStringLiteral("green")).toInt(), 255));
    normalized_sample.insert(
        QStringLiteral("blue"), qBound(0, sample.value(QStringLiteral("blue")).toInt(), 255));
    normalized_sample.insert(
        QStringLiteral("hex"), sample.value(QStringLiteral("hex")).toString().left(16));
    samples.append(normalized_sample);
  }
  return samples;
}

inline QVariantList normalized_luminance_range_samples(const QVariantList& sample_values) {
  QVariantList samples;
  const auto start_index =
      sample_values.size() > qsizetype{5} ? sample_values.size() - qsizetype{5} : qsizetype{0};
  for (auto index = start_index; index < sample_values.size(); ++index) {
    const auto sample = sample_values.at(index).toMap();
    if (sample.isEmpty()) {
      continue;
    }
    QVariantMap normalized_sample;
    normalized_sample.insert(
        QStringLiteral("x"), qBound(0.0, sample.value(QStringLiteral("x")).toDouble(), 1.0));
    normalized_sample.insert(
        QStringLiteral("y"), qBound(0.0, sample.value(QStringLiteral("y")).toDouble(), 1.0));
    normalized_sample.insert(
        QStringLiteral("width"),
        qBound(0.0, sample.value(QStringLiteral("width")).toDouble(), 1.0));
    normalized_sample.insert(
        QStringLiteral("height"),
        qBound(0.0, sample.value(QStringLiteral("height")).toDouble(), 1.0));
    auto min_value = qBound(
        0.0,
        sample.contains(QStringLiteral("luminanceMin"))
            ? sample.value(QStringLiteral("luminanceMin")).toDouble()
            : 0.2,
        1.0);
    auto max_value = qBound(
        0.0,
        sample.contains(QStringLiteral("luminanceMax"))
            ? sample.value(QStringLiteral("luminanceMax")).toDouble()
            : 0.8,
        1.0);
    if (min_value > max_value) {
      std::swap(min_value, max_value);
    }
    normalized_sample.insert(QStringLiteral("luminanceMin"), min_value);
    normalized_sample.insert(QStringLiteral("luminanceMax"), max_value);
    normalized_sample.insert(
        QStringLiteral("luminanceMean"),
        qBound(
            0.0,
            sample.contains(QStringLiteral("luminanceMean"))
                ? sample.value(QStringLiteral("luminanceMean")).toDouble()
                : (min_value + max_value) * 0.5,
            1.0));
    normalized_sample.insert(
        QStringLiteral("pixelCount"), std::max(0, sample.value(QStringLiteral("pixelCount")).toInt()));
    samples.append(normalized_sample);
  }
  return samples;
}

inline QVariantList normalized_depth_range_samples(const QVariantList& sample_values) {
  QVariantList samples;
  const auto start_index =
      sample_values.size() > qsizetype{5} ? sample_values.size() - qsizetype{5} : qsizetype{0};
  for (auto index = start_index; index < sample_values.size(); ++index) {
    const auto sample = sample_values.at(index).toMap();
    if (sample.isEmpty()) {
      continue;
    }
    QVariantMap normalized_sample;
    normalized_sample.insert(
        QStringLiteral("x"), qBound(0.0, sample.value(QStringLiteral("x")).toDouble(), 1.0));
    normalized_sample.insert(
        QStringLiteral("y"), qBound(0.0, sample.value(QStringLiteral("y")).toDouble(), 1.0));
    auto min_value = qBound(
        0.0,
        sample.contains(QStringLiteral("depthMin")) ? sample.value(QStringLiteral("depthMin")).toDouble() : 0.2,
        1.0);
    auto max_value = qBound(
        0.0,
        sample.contains(QStringLiteral("depthMax")) ? sample.value(QStringLiteral("depthMax")).toDouble() : 0.8,
        1.0);
    if (min_value > max_value) {
      std::swap(min_value, max_value);
    }
    normalized_sample.insert(QStringLiteral("depthMin"), min_value);
    normalized_sample.insert(QStringLiteral("depthMax"), max_value);
    normalized_sample.insert(
        QStringLiteral("depthMean"),
        qBound(
            0.0,
            sample.contains(QStringLiteral("depthMean"))
                ? sample.value(QStringLiteral("depthMean")).toDouble()
                : (min_value + max_value) * 0.5,
            1.0));
    normalized_sample.insert(
        QStringLiteral("pixelCount"), std::max(0, sample.value(QStringLiteral("pixelCount")).toInt()));
    samples.append(normalized_sample);
  }
  return samples;
}

inline QVariantMap normalized_mask_definition(const QVariantMap& candidate, int ordinal = 1) {
  const auto normalized_kind = normalized_mask_kind(candidate.value(QStringLiteral("kind")).toString());
  QVariantMap mask = default_mask_definition(normalized_kind, ordinal);
  const auto id = candidate.value(QStringLiteral("id")).toString().simplified();
  const auto name = candidate.value(QStringLiteral("name")).toString().simplified();
  mask.insert(
      QStringLiteral("schemaVersion"),
      std::max(1, map_int_value_or(candidate, QStringLiteral("schemaVersion"), 1)));
  mask.insert(
      QStringLiteral("sourceSchemaVersion"),
      std::max(1, map_int_value_or(candidate, QStringLiteral("sourceSchemaVersion"), 1)));
  mask.insert(QStringLiteral("id"), id.isEmpty() ? mask.value(QStringLiteral("id")).toString() : id);
  mask.insert(QStringLiteral("parentId"), candidate.value(QStringLiteral("parentId")).toString().simplified());
  const auto reusable_output = candidate.contains(QStringLiteral("reusableOutput"))
                                   ? candidate.value(QStringLiteral("reusableOutput")).toBool()
                                   : false;
  const auto output_id = candidate.value(QStringLiteral("outputId")).toString().simplified();
  mask.insert(
      QStringLiteral("outputId"),
      output_id.isEmpty() && reusable_output ? mask.value(QStringLiteral("id")).toString()
                                             : output_id);
  mask.insert(
      QStringLiteral("reusableOutput"),
      reusable_output);
  mask.insert(QStringLiteral("name"), name.isEmpty() ? mask.value(QStringLiteral("name")).toString() : name);
  mask.insert(QStringLiteral("kind"), normalized_kind);
  const auto agent_visual_referent_id =
      candidate.value(QStringLiteral("agentVisualReferentId")).toString().simplified();
  const auto agent_selection_source =
      candidate.value(QStringLiteral("agentSelectionSource")).toString().simplified();
  if (!agent_visual_referent_id.isEmpty()) {
    mask.insert(QStringLiteral("agentVisualReferentId"),
                agent_visual_referent_id.left(80));
  }
  if (!agent_selection_source.isEmpty()) {
    mask.insert(QStringLiteral("agentSelectionSource"),
                agent_selection_source.left(80));
  }
  const auto inferred_tool_role = [&]() {
    if (candidate.contains(QStringLiteral("toolRole"))) {
      return normalized_mask_tool_role(candidate.value(QStringLiteral("toolRole")).toString());
    }
    const auto retouch_mode =
        candidate.value(QStringLiteral("retouchMode")).toString().trimmed().toLower();
    if (!retouch_mode.isEmpty()) {
      return QStringLiteral("retouch");
    }
    const auto has_selection_adjustments =
        std::abs(candidate.value(QStringLiteral("exposure")).toDouble()) > 1e-6 ||
        std::abs(candidate.value(QStringLiteral("contrast")).toDouble()) > 1e-6 ||
        std::abs(candidate.value(QStringLiteral("highlights")).toDouble()) > 1e-6 ||
        std::abs(candidate.value(QStringLiteral("shadows")).toDouble()) > 1e-6 ||
        std::abs(candidate.value(QStringLiteral("whites")).toDouble()) > 1e-6 ||
        std::abs(candidate.value(QStringLiteral("blacks")).toDouble()) > 1e-6 ||
        std::abs(candidate.value(QStringLiteral("temperature")).toDouble()) > 1e-6 ||
        std::abs(candidate.value(QStringLiteral("tint")).toDouble()) > 1e-6 ||
        std::abs(candidate.value(QStringLiteral("vibrance")).toDouble()) > 1e-6 ||
        std::abs(candidate.value(QStringLiteral("saturation")).toDouble()) > 1e-6 ||
        std::abs(candidate.value(QStringLiteral("texture")).toDouble()) > 1e-6 ||
        std::abs(candidate.value(QStringLiteral("clarity")).toDouble()) > 1e-6 ||
        std::abs(candidate.value(QStringLiteral("dehaze")).toDouble()) > 1e-6 ||
        std::abs(candidate.value(QStringLiteral("sharpening")).toDouble()) > 1e-6 ||
        std::abs(candidate.value(QStringLiteral("sharpenRadius"), 1.0).toDouble() - 1.0) > 1e-6 ||
        std::abs(candidate.value(QStringLiteral("sharpenDetail")).toDouble()) > 1e-6 ||
        std::abs(candidate.value(QStringLiteral("sharpenMasking")).toDouble()) > 1e-6 ||
        std::abs(candidate.value(QStringLiteral("luminanceNr")).toDouble()) > 1e-6 ||
        std::abs(candidate.value(QStringLiteral("colorNr")).toDouble()) > 1e-6 ||
        std::abs(candidate.value(QStringLiteral("hueShift")).toDouble()) > 1e-6 ||
        std::abs(candidate.value(QStringLiteral("saturationShift")).toDouble()) > 1e-6 ||
        std::abs(candidate.value(QStringLiteral("colorizeHue")).toDouble()) > 1e-6 ||
        std::abs(candidate.value(QStringLiteral("colorizeStrength")).toDouble()) > 1e-6;
    if (!has_selection_adjustments &&
        std::abs(candidate.value(QStringLiteral("removeStrength")).toDouble()) > 1e-6) {
      return QStringLiteral("retouch");
    }
    return QStringLiteral("selection");
  }();
  mask.insert(QStringLiteral("toolRole"), inferred_tool_role);
  insert_mask_source_truth_metadata(&mask, candidate, normalized_kind, inferred_tool_role);
  if (is_selection_mask_tool_role(inferred_tool_role)) {
    const auto fallback_set_id = QStringLiteral("%1-set").arg(mask.value(QStringLiteral("id")).toString());
    const auto set_id = candidate.value(QStringLiteral("maskSetId")).toString().simplified();
    const auto set_name = candidate.value(QStringLiteral("maskSetName")).toString().simplified();
    mask.insert(
        QStringLiteral("maskSetId"),
        set_id.isEmpty() ? fallback_set_id : set_id);
    mask.insert(
        QStringLiteral("maskSetName"),
        set_name.isEmpty() ? mask.value(QStringLiteral("name")).toString()
                           : set_name);
  } else {
    mask.insert(QStringLiteral("maskSetId"), QString());
    mask.insert(QStringLiteral("maskSetName"), QString());
  }
  mask.insert(
      QStringLiteral("operation"),
      normalized_mask_operation(candidate.value(QStringLiteral("operation")).toString()));
  mask.insert(
      QStringLiteral("maskSetSelectionInvert"),
      candidate.contains(QStringLiteral("maskSetSelectionInvert"))
          ? candidate.value(QStringLiteral("maskSetSelectionInvert")).toBool()
          : false);
  mask.insert(
      QStringLiteral("maskSetSelectionExpand"),
      qBound(
          -100.0,
          candidate.contains(QStringLiteral("maskSetSelectionExpand"))
              ? candidate.value(QStringLiteral("maskSetSelectionExpand")).toDouble()
              : 0.0,
          100.0));
  mask.insert(
      QStringLiteral("maskSetSelectionSmooth"),
      qBound(
          0.0,
          candidate.contains(QStringLiteral("maskSetSelectionSmooth"))
              ? candidate.value(QStringLiteral("maskSetSelectionSmooth")).toDouble()
              : 0.0,
          100.0));
  mask.insert(
      QStringLiteral("maskSetSelectionFineEdge"),
      qBound(
          0.0,
          candidate.contains(QStringLiteral("maskSetSelectionFineEdge"))
              ? candidate.value(QStringLiteral("maskSetSelectionFineEdge")).toDouble()
              : 0.0,
          100.0));
  mask.insert(
      QStringLiteral("maskSetSelectionEdgeContrast"),
      qBound(
          0.0,
          candidate.contains(QStringLiteral("maskSetSelectionEdgeContrast"))
              ? candidate.value(QStringLiteral("maskSetSelectionEdgeContrast")).toDouble()
              : 0.0,
          100.0));
  mask.insert(
      QStringLiteral("maskSetDensity"),
      qBound(
          0.0,
          candidate.contains(QStringLiteral("maskSetDensity"))
              ? candidate.value(QStringLiteral("maskSetDensity")).toDouble()
              : 1.0,
          1.0));
  mask.insert(
      QStringLiteral("maskSetOpacity"),
      qBound(
          0.0,
          candidate.contains(QStringLiteral("maskSetOpacity"))
              ? candidate.value(QStringLiteral("maskSetOpacity")).toDouble()
              : 1.0,
          1.0));
  mask.insert(
      QStringLiteral("maskSetPreviewBypassed"),
      candidate.contains(QStringLiteral("maskSetPreviewBypassed"))
          ? candidate.value(QStringLiteral("maskSetPreviewBypassed")).toBool()
          : false);
  mask.insert(
      QStringLiteral("enabled"),
      candidate.contains(QStringLiteral("enabled"))
          ? candidate.value(QStringLiteral("enabled")).toBool()
          : true);
  mask.insert(
      QStringLiteral("solo"),
      candidate.contains(QStringLiteral("solo"))
          ? candidate.value(QStringLiteral("solo")).toBool()
          : false);
  mask.insert(
      QStringLiteral("locked"),
      candidate.contains(QStringLiteral("locked"))
          ? candidate.value(QStringLiteral("locked")).toBool()
          : false);
  mask.insert(
      QStringLiteral("committed"),
      candidate.contains(QStringLiteral("committed"))
          ? candidate.value(QStringLiteral("committed")).toBool()
          : true);
  mask.insert(
      QStringLiteral("opacity"),
      qBound(0.0, candidate.contains(QStringLiteral("opacity"))
                       ? candidate.value(QStringLiteral("opacity")).toDouble()
                       : 1.0,
             1.0));
  mask.insert(
      QStringLiteral("density"),
      qBound(0.0, candidate.contains(QStringLiteral("density"))
                       ? candidate.value(QStringLiteral("density")).toDouble()
                       : 1.0,
             1.0));
  const auto mask_inverted = candidate.contains(QStringLiteral("invert"))
                                 ? candidate.value(QStringLiteral("invert")).toBool()
                                 : candidate.value(QStringLiteral("selectionInvert")).toBool();
  mask.insert(QStringLiteral("invert"), mask_inverted);
  mask.insert(
      QStringLiteral("blendMode"),
      normalized_mask_blend_mode(candidate.value(QStringLiteral("blendMode")).toString()));
  mask.insert(QStringLiteral("exposure"), candidate.value(QStringLiteral("exposure")).toDouble());
  mask.insert(QStringLiteral("contrast"), candidate.value(QStringLiteral("contrast")).toDouble());
  mask.insert(QStringLiteral("highlights"), candidate.value(QStringLiteral("highlights")).toDouble());
  mask.insert(QStringLiteral("shadows"), candidate.value(QStringLiteral("shadows")).toDouble());
  mask.insert(QStringLiteral("whites"), candidate.value(QStringLiteral("whites")).toDouble());
  mask.insert(QStringLiteral("blacks"), candidate.value(QStringLiteral("blacks")).toDouble());
  mask.insert(QStringLiteral("temperature"), candidate.value(QStringLiteral("temperature")).toDouble());
  mask.insert(QStringLiteral("tint"), candidate.value(QStringLiteral("tint")).toDouble());
  mask.insert(QStringLiteral("vibrance"), candidate.value(QStringLiteral("vibrance")).toDouble());
  mask.insert(QStringLiteral("saturation"), candidate.value(QStringLiteral("saturation")).toDouble());
  mask.insert(QStringLiteral("texture"), candidate.value(QStringLiteral("texture")).toDouble());
  mask.insert(QStringLiteral("clarity"), candidate.value(QStringLiteral("clarity")).toDouble());
  mask.insert(QStringLiteral("dehaze"), candidate.value(QStringLiteral("dehaze")).toDouble());
  mask.insert(QStringLiteral("sharpening"), candidate.value(QStringLiteral("sharpening")).toDouble());
  mask.insert(
      QStringLiteral("sharpenRadius"),
      candidate.contains(QStringLiteral("sharpenRadius"))
          ? candidate.value(QStringLiteral("sharpenRadius")).toDouble()
          : 1.0);
  mask.insert(QStringLiteral("sharpenDetail"), candidate.value(QStringLiteral("sharpenDetail")).toDouble());
  mask.insert(QStringLiteral("sharpenMasking"), candidate.value(QStringLiteral("sharpenMasking")).toDouble());
  mask.insert(QStringLiteral("luminanceNr"), candidate.value(QStringLiteral("luminanceNr")).toDouble());
  mask.insert(QStringLiteral("colorNr"), candidate.value(QStringLiteral("colorNr")).toDouble());
  mask.insert(QStringLiteral("hueShift"), candidate.value(QStringLiteral("hueShift")).toDouble());
  mask.insert(QStringLiteral("saturationShift"), candidate.value(QStringLiteral("saturationShift")).toDouble());
  mask.insert(
      QStringLiteral("retouchMode"),
      candidate.contains(QStringLiteral("retouchMode"))
          ? candidate.value(QStringLiteral("retouchMode")).toString().trimmed().toLower()
          : QString());
  mask.insert(
      QStringLiteral("retouchOrder"),
      normalized_mask_tool_role(mask.value(QStringLiteral("toolRole")).toString()) == QStringLiteral("retouch")
          ? std::max(1, map_int_value_or(candidate, QStringLiteral("retouchOrder"), ordinal))
          : 0);
  mask.insert(
      QStringLiteral("colorizeHue"),
      candidate.contains(QStringLiteral("colorizeHue"))
          ? candidate.value(QStringLiteral("colorizeHue")).toDouble()
          : 0.0);
  mask.insert(
      QStringLiteral("colorizeStrength"),
      candidate.contains(QStringLiteral("colorizeStrength"))
          ? candidate.value(QStringLiteral("colorizeStrength")).toDouble()
          : 0.0);
  mask.insert(
      QStringLiteral("removeStrength"),
      candidate.contains(QStringLiteral("removeStrength"))
          ? candidate.value(QStringLiteral("removeStrength")).toDouble()
          : 0.0);
  mask.insert(
      QStringLiteral("healTextureBlend"),
      qBound(
          0.0,
          candidate.value(QStringLiteral("healTextureBlend"), mask.value(QStringLiteral("healTextureBlend"))).toDouble(),
          1.0));
  mask.insert(
      QStringLiteral("healColorBlend"),
      qBound(
          0.0,
          candidate.value(QStringLiteral("healColorBlend"), mask.value(QStringLiteral("healColorBlend"))).toDouble(),
          1.0));
  mask.insert(
      QStringLiteral("selectionInvert"),
      candidate.contains(QStringLiteral("selectionInvert"))
          ? candidate.value(QStringLiteral("selectionInvert")).toBool()
          : mask.value(QStringLiteral("invert")).toBool());
  mask.insert(
      QStringLiteral("selectionExpand"),
      candidate.contains(QStringLiteral("selectionExpand"))
          ? candidate.value(QStringLiteral("selectionExpand")).toDouble()
          : 0.0);
  mask.insert(
      QStringLiteral("selectionSmooth"),
      candidate.contains(QStringLiteral("selectionSmooth"))
          ? candidate.value(QStringLiteral("selectionSmooth")).toDouble()
          : matte_cleanup_defaults_for_kind(normalized_kind).smooth);
  mask.insert(
      QStringLiteral("selectionFineEdge"),
      candidate.contains(QStringLiteral("selectionFineEdge"))
          ? candidate.value(QStringLiteral("selectionFineEdge")).toDouble()
          : matte_cleanup_defaults_for_kind(normalized_kind).fine_edge);
  mask.insert(
      QStringLiteral("selectionEdgeContrast"),
      candidate.contains(QStringLiteral("selectionEdgeContrast"))
          ? candidate.value(QStringLiteral("selectionEdgeContrast")).toDouble()
          : matte_cleanup_defaults_for_kind(normalized_kind).edge_contrast);
  mask.insert(
      QStringLiteral("selectionDecontaminate"),
      candidate.contains(QStringLiteral("selectionDecontaminate"))
          ? candidate.value(QStringLiteral("selectionDecontaminate")).toDouble()
          : matte_cleanup_defaults_for_kind(normalized_kind).decontaminate);
  const auto matte_cleanup_defaults = matte_cleanup_defaults_for_kind(normalized_kind);
  const auto refine_percent_value = [&](const QString& key) {
    return qBound(0.0, candidate.contains(key) ? candidate.value(key).toDouble() : mask.value(key).toDouble(), 100.0);
  };
  mask.insert(QStringLiteral("selectionGrow"), refine_percent_value(QStringLiteral("selectionGrow")));
  mask.insert(QStringLiteral("selectionShrink"), refine_percent_value(QStringLiteral("selectionShrink")));
  mask.insert(
      QStringLiteral("selectionFeather"),
      qBound(0.0,
             candidate.contains(QStringLiteral("selectionFeather"))
                 ? candidate.value(QStringLiteral("selectionFeather")).toDouble()
                 : matte_cleanup_defaults.feather,
             100.0));
  mask.insert(QStringLiteral("selectionBlur"), refine_percent_value(QStringLiteral("selectionBlur")));
  mask.insert(
      QStringLiteral("selectionHarden"),
      qBound(0.0,
             candidate.contains(QStringLiteral("selectionHarden"))
                 ? candidate.value(QStringLiteral("selectionHarden")).toDouble()
                 : matte_cleanup_defaults.harden,
             100.0));
  mask.insert(
      QStringLiteral("selectionDespeckle"),
      qBound(0.0,
             candidate.contains(QStringLiteral("selectionDespeckle"))
                 ? candidate.value(QStringLiteral("selectionDespeckle")).toDouble()
                 : matte_cleanup_defaults.despeckle,
             100.0));
  mask.insert(
      QStringLiteral("selectionFillHoles"),
      qBound(0.0,
             candidate.contains(QStringLiteral("selectionFillHoles"))
                 ? candidate.value(QStringLiteral("selectionFillHoles")).toDouble()
                 : matte_cleanup_defaults.fill_holes,
             100.0));
  mask.insert(
      QStringLiteral("selectionRemoveIslands"),
      qBound(0.0,
             candidate.contains(QStringLiteral("selectionRemoveIslands"))
                 ? candidate.value(QStringLiteral("selectionRemoveIslands")).toDouble()
                 : matte_cleanup_defaults.remove_islands,
             100.0));
  mask.insert(
      QStringLiteral("matteCleanupSchemaVersion"),
      std::max(1, candidate.value(QStringLiteral("matteCleanupSchemaVersion"), 1).toInt()));
  mask.insert(
      QStringLiteral("matteCleanupProfile"),
      candidate.value(
                   QStringLiteral("matteCleanupProfile"),
                   QString::fromLatin1(matte_cleanup_defaults.profile))
          .toString()
          .simplified());
  mask.insert(
      QStringLiteral("matteCleanupDefaultsApplied"),
      candidate.contains(QStringLiteral("matteCleanupDefaultsApplied"))
          ? candidate.value(QStringLiteral("matteCleanupDefaultsApplied")).toBool()
          : true);
  const auto refine_value = [&](const QString& key, double fallback) {
    return candidate.contains(key) ? candidate.value(key).toDouble() : fallback;
  };
  mask.insert(QStringLiteral("refineLuminanceMin"), qBound(0.0, refine_value(QStringLiteral("refineLuminanceMin"), 0.0), 1.0));
  mask.insert(QStringLiteral("refineLuminanceMax"), qBound(0.0, refine_value(QStringLiteral("refineLuminanceMax"), 1.0), 1.0));
  mask.insert(QStringLiteral("refineHueCenter"), refine_value(QStringLiteral("refineHueCenter"), 0.0));
  mask.insert(QStringLiteral("refineHueRange"), qBound(0.02, refine_value(QStringLiteral("refineHueRange"), 0.5), 0.5));
  mask.insert(QStringLiteral("refineSaturationMin"), qBound(0.0, refine_value(QStringLiteral("refineSaturationMin"), 0.0), 1.0));
  mask.insert(QStringLiteral("refineSaturationMax"), qBound(0.0, refine_value(QStringLiteral("refineSaturationMax"), 1.0), 1.0));
  mask.insert(QStringLiteral("refineDepthMin"), qBound(0.0, refine_value(QStringLiteral("refineDepthMin"), 0.0), 1.0));
  mask.insert(QStringLiteral("refineDepthMax"), qBound(0.0, refine_value(QStringLiteral("refineDepthMax"), 1.0), 1.0));
  mask.insert(QStringLiteral("refineEdgeConfidenceMin"), qBound(0.0, refine_value(QStringLiteral("refineEdgeConfidenceMin"), 0.0), 1.0));
  mask.insert(QStringLiteral("refineRangeFeather"), qBound(0.0, refine_value(QStringLiteral("refineRangeFeather"), 0.05), 0.5));
  mask.insert(
      QStringLiteral("generatedMaskHoleFillThreshold"),
      qBound(
          0.0,
          refine_value(
              QStringLiteral("generatedMaskHoleFillThreshold"),
              matte_cleanup_defaults.generated_hole_threshold),
          0.05));
  mask.insert(
      QStringLiteral("generatedMaskIslandRemovalThreshold"),
      qBound(
          0.0,
          refine_value(
              QStringLiteral("generatedMaskIslandRemovalThreshold"),
              matte_cleanup_defaults.generated_island_threshold),
          0.05));
  mask.insert(
      QStringLiteral("generatedMaskHolesFilled"),
      std::max(0, candidate.value(QStringLiteral("generatedMaskHolesFilled")).toInt()));
  mask.insert(
      QStringLiteral("generatedMaskIslandsRemoved"),
      std::max(0, candidate.value(QStringLiteral("generatedMaskIslandsRemoved")).toInt()));
  mask.insert(
      QStringLiteral("generatedMaskPixelsFilled"),
      std::max(0, candidate.value(QStringLiteral("generatedMaskPixelsFilled")).toInt()));
  mask.insert(
      QStringLiteral("generatedMaskPixelsRemoved"),
      std::max(0, candidate.value(QStringLiteral("generatedMaskPixelsRemoved")).toInt()));
  mask.insert(
      QStringLiteral("feather"),
      candidate.contains(QStringLiteral("feather"))
          ? candidate.value(QStringLiteral("feather")).toDouble()
          : (normalized_kind == QStringLiteral("radial") ? 0.25
             : normalized_kind == QStringLiteral("ellipse") || normalized_kind == QStringLiteral("rectangle") ? 0.08
             : normalized_kind == QStringLiteral("brush") ? 0.75
             : normalized_kind == QStringLiteral("polygon") || normalized_kind == QStringLiteral("lasso") ? 0.06
             : is_raster_mask_kind(normalized_kind) ? 0.5
             : 0.22));
  mask.insert(QStringLiteral("rotation"), candidate.value(QStringLiteral("rotation")).toDouble());
  if (is_center_radius_mask_kind(normalized_kind)) {
    mask.insert(
        QStringLiteral("centerX"),
        candidate.contains(QStringLiteral("centerX"))
            ? candidate.value(QStringLiteral("centerX")).toDouble()
            : 0.5);
    mask.insert(
        QStringLiteral("centerY"),
        candidate.contains(QStringLiteral("centerY"))
            ? candidate.value(QStringLiteral("centerY")).toDouble()
            : 0.5);
    mask.insert(
        QStringLiteral("radiusX"),
        candidate.contains(QStringLiteral("radiusX"))
            ? candidate.value(QStringLiteral("radiusX")).toDouble()
            : (normalized_kind == QStringLiteral("rectangle") ? 0.30 : 0.25));
    mask.insert(
        QStringLiteral("radiusY"),
        candidate.contains(QStringLiteral("radiusY"))
            ? candidate.value(QStringLiteral("radiusY")).toDouble()
            : (normalized_kind == QStringLiteral("rectangle") ? 0.20 : 0.25));
  } else if (normalized_kind == QStringLiteral("linear")) {
    mask.insert(
        QStringLiteral("position"),
        candidate.contains(QStringLiteral("position"))
            ? candidate.value(QStringLiteral("position")).toDouble()
            : 0.5);
  } else if (normalized_kind == QStringLiteral("brush")) {
    const auto brush_preset = candidate.value(QStringLiteral("brushPreset")).toString().simplified();
    mask.insert(
        QStringLiteral("brushPreset"),
        brush_preset.isEmpty() ? QStringLiteral("Soft Round") : brush_preset);
    mask.insert(
        QStringLiteral("brushSize"),
        candidate.contains(QStringLiteral("brushSize"))
            ? candidate.value(QStringLiteral("brushSize")).toDouble()
            : 0.08);
    mask.insert(
        QStringLiteral("brushFlow"),
        candidate.contains(QStringLiteral("brushFlow"))
            ? candidate.value(QStringLiteral("brushFlow")).toDouble()
            : 1.0);
    mask.insert(
        QStringLiteral("brushHardness"),
        qBound(0.0,
               candidate.contains(QStringLiteral("brushHardness"))
                   ? candidate.value(QStringLiteral("brushHardness")).toDouble()
                   : 0.25,
               1.0));
    mask.insert(
        QStringLiteral("brushSpacing"),
        qBound(0.05,
               candidate.contains(QStringLiteral("brushSpacing"))
                   ? candidate.value(QStringLiteral("brushSpacing")).toDouble()
                   : 0.75,
               2.0));
    mask.insert(
        QStringLiteral("brushSmoothing"),
        qBound(0.0,
               candidate.contains(QStringLiteral("brushSmoothing"))
                   ? candidate.value(QStringLiteral("brushSmoothing")).toDouble()
                   : 0.0,
               1.0));
    mask.insert(
        QStringLiteral("brushPressureSize"),
        qBound(0.0,
               candidate.contains(QStringLiteral("brushPressureSize"))
                   ? candidate.value(QStringLiteral("brushPressureSize")).toDouble()
                   : 0.65,
               1.0));
    mask.insert(
        QStringLiteral("brushPressureFlow"),
        qBound(0.0,
               candidate.contains(QStringLiteral("brushPressureFlow"))
                   ? candidate.value(QStringLiteral("brushPressureFlow")).toDouble()
                   : 1.0,
               1.0));
    mask.insert(
        QStringLiteral("brushPressureOpacity"),
        qBound(0.0,
               candidate.contains(QStringLiteral("brushPressureOpacity"))
                   ? candidate.value(QStringLiteral("brushPressureOpacity")).toDouble()
                   : 0.0,
               1.0));
    mask.insert(
        QStringLiteral("brushBuildup"),
        candidate.contains(QStringLiteral("brushBuildup"))
            ? candidate.value(QStringLiteral("brushBuildup")).toBool()
            : true);
    mask.insert(
        QStringLiteral("brushEraseMode"),
        candidate.contains(QStringLiteral("brushEraseMode"))
            ? candidate.value(QStringLiteral("brushEraseMode")).toBool()
            : false);
    mask.insert(
        QStringLiteral("brushAutoMask"),
        candidate.contains(QStringLiteral("brushAutoMask"))
            ? candidate.value(QStringLiteral("brushAutoMask")).toBool()
            : false);
    mask.insert(
        QStringLiteral("brushAutoMaskTolerance"),
        qBound(0.02,
               candidate.contains(QStringLiteral("brushAutoMaskTolerance"))
                   ? candidate.value(QStringLiteral("brushAutoMaskTolerance")).toDouble()
                   : 0.18,
               0.75));
    mask.insert(
        QStringLiteral("brushAutoMaskContinuousSampling"),
        candidate.contains(QStringLiteral("brushAutoMaskContinuousSampling"))
            ? candidate.value(QStringLiteral("brushAutoMaskContinuousSampling")).toBool()
            : false);
    mask.insert(
        QStringLiteral("brushProtectEdges"),
        candidate.contains(QStringLiteral("brushProtectEdges"))
            ? candidate.value(QStringLiteral("brushProtectEdges")).toBool()
            : false);
    mask.insert(
        QStringLiteral("brushRefineMode"),
        candidate.contains(QStringLiteral("brushRefineMode"))
            ? candidate.value(QStringLiteral("brushRefineMode")).toBool()
            : false);
    mask.insert(
        QStringLiteral("refineBrushRecalculate"),
        candidate.contains(QStringLiteral("refineBrushRecalculate"))
            ? candidate.value(QStringLiteral("refineBrushRecalculate")).toBool()
            : false);
    mask.insert(
        QStringLiteral("brushRefineTargetId"),
        candidate.value(QStringLiteral("brushRefineTargetId")).toString().simplified());
    mask.insert(
        QStringLiteral("brushRefineTargetName"),
        candidate.value(QStringLiteral("brushRefineTargetName")).toString().simplified());
    mask.insert(
        QStringLiteral("brushRefineTargetKind"),
        normalized_mask_kind(candidate.value(QStringLiteral("brushRefineTargetKind")).toString()));
    mask.insert(
        QStringLiteral("cloneSourceSet"),
        candidate.contains(QStringLiteral("cloneSourceSet"))
            ? candidate.value(QStringLiteral("cloneSourceSet")).toBool()
            : false);
    mask.insert(
        QStringLiteral("cloneSourceX"),
        candidate.contains(QStringLiteral("cloneSourceX"))
            ? candidate.value(QStringLiteral("cloneSourceX")).toDouble()
            : 0.5);
    mask.insert(
        QStringLiteral("cloneSourceY"),
        candidate.contains(QStringLiteral("cloneSourceY"))
            ? candidate.value(QStringLiteral("cloneSourceY")).toDouble()
            : 0.5);
    mask.insert(
        QStringLiteral("cloneOffsetX"),
        candidate.contains(QStringLiteral("cloneOffsetX"))
            ? candidate.value(QStringLiteral("cloneOffsetX")).toDouble()
            : 0.0);
    mask.insert(
        QStringLiteral("cloneOffsetY"),
        candidate.contains(QStringLiteral("cloneOffsetY"))
            ? candidate.value(QStringLiteral("cloneOffsetY")).toDouble()
            : 0.0);
    mask.insert(
        QStringLiteral("cloneScale"),
        candidate.contains(QStringLiteral("cloneScale"))
            ? candidate.value(QStringLiteral("cloneScale")).toDouble()
            : 1.0);
    mask.insert(
        QStringLiteral("cloneRotation"),
        candidate.contains(QStringLiteral("cloneRotation"))
            ? candidate.value(QStringLiteral("cloneRotation")).toDouble()
            : 0.0);
    mask.insert(
        QStringLiteral("cloneAligned"),
        candidate.contains(QStringLiteral("cloneAligned"))
            ? candidate.value(QStringLiteral("cloneAligned")).toBool()
            : true);
    QVariantList clone_sources;
    for (const auto& source_value : candidate.value(QStringLiteral("cloneSources")).toList()) {
      const auto source_map = source_value.toMap();
      QVariantMap clone_source;
      clone_source.insert(
          QStringLiteral("name"),
          source_map.value(QStringLiteral("name")).toString().simplified());
      clone_source.insert(
          QStringLiteral("x"),
          std::clamp(source_map.value(QStringLiteral("x"), 0.5).toDouble(), 0.0, 1.0));
      clone_source.insert(
          QStringLiteral("y"),
          std::clamp(source_map.value(QStringLiteral("y"), 0.5).toDouble(), 0.0, 1.0));
      clone_source.insert(
          QStringLiteral("scale"),
          std::clamp(source_map.value(QStringLiteral("scale"), 1.0).toDouble(), 0.25, 4.0));
      clone_source.insert(
          QStringLiteral("rotation"),
          std::clamp(source_map.value(QStringLiteral("rotation")).toDouble(), -180.0, 180.0));
      clone_source.insert(
          QStringLiteral("aligned"),
          source_map.contains(QStringLiteral("aligned"))
              ? source_map.value(QStringLiteral("aligned")).toBool()
              : true);
      clone_sources.append(clone_source);
    }
    mask.insert(QStringLiteral("cloneSources"), clone_sources);
    const auto clone_source_index = candidate.value(QStringLiteral("cloneSourceIndex"), -1).toInt();
    mask.insert(
        QStringLiteral("cloneSourceIndex"),
        clone_source_index >= 0 && clone_source_index < clone_sources.size() ? clone_source_index : -1);
    const auto patch_source_width =
        std::clamp(candidate.value(QStringLiteral("patchSourceWidth"), 0.18).toDouble(), 0.02, 1.0);
    const auto patch_source_height =
        std::clamp(candidate.value(QStringLiteral("patchSourceHeight"), 0.18).toDouble(), 0.02, 1.0);
    mask.insert(
        QStringLiteral("patchSourceSet"),
        candidate.value(QStringLiteral("patchSourceSet"), false).toBool());
    mask.insert(
        QStringLiteral("patchSourceX"),
        std::clamp(candidate.value(QStringLiteral("patchSourceX"), 0.5).toDouble(), 0.0, 1.0 - patch_source_width));
    mask.insert(
        QStringLiteral("patchSourceY"),
        std::clamp(candidate.value(QStringLiteral("patchSourceY"), 0.5).toDouble(), 0.0, 1.0 - patch_source_height));
    mask.insert(QStringLiteral("patchSourceWidth"), patch_source_width);
    mask.insert(QStringLiteral("patchSourceHeight"), patch_source_height);
    QVariantList normalized_points;
    const auto normalize_brush_point = [&](const QVariant& point_value) {
      const auto point = point_value.toMap();
      QVariantMap normalized_point;
      normalized_point.insert(QStringLiteral("x"), point.value(QStringLiteral("x")).toDouble());
      normalized_point.insert(QStringLiteral("y"), point.value(QStringLiteral("y")).toDouble());
      normalized_point.insert(
          QStringLiteral("size"),
          point.contains(QStringLiteral("size"))
              ? point.value(QStringLiteral("size")).toDouble()
              : mask.value(QStringLiteral("brushSize")).toDouble());
      normalized_point.insert(
          QStringLiteral("flow"),
          point.contains(QStringLiteral("flow"))
              ? point.value(QStringLiteral("flow")).toDouble()
              : mask.value(QStringLiteral("brushFlow")).toDouble());
      normalized_point.insert(
          QStringLiteral("opacity"),
          qBound(0.0,
                 point.contains(QStringLiteral("opacity"))
                     ? point.value(QStringLiteral("opacity")).toDouble()
                      : 1.0,
                  1.0));
      normalized_point.insert(
          QStringLiteral("autoMaskAnchorX"),
          qBound(0.0,
                 point.contains(QStringLiteral("autoMaskAnchorX"))
                     ? point.value(QStringLiteral("autoMaskAnchorX")).toDouble()
                     : point.value(QStringLiteral("x")).toDouble(),
                 1.0));
      normalized_point.insert(
          QStringLiteral("autoMaskAnchorY"),
          qBound(0.0,
                 point.contains(QStringLiteral("autoMaskAnchorY"))
                     ? point.value(QStringLiteral("autoMaskAnchorY")).toDouble()
                     : point.value(QStringLiteral("y")).toDouble(),
                 1.0));
      normalized_point.insert(
          QStringLiteral("pressure"),
          qBound(0.0,
                 point.contains(QStringLiteral("pressure"))
                     ? point.value(QStringLiteral("pressure")).toDouble()
                     : 1.0,
                 1.0));
      normalized_point.insert(
          QStringLiteral("tiltX"),
          qBound(-1.0,
                 point.contains(QStringLiteral("tiltX")) ? point.value(QStringLiteral("tiltX")).toDouble() : 0.0,
                 1.0));
      normalized_point.insert(
          QStringLiteral("tiltY"),
          qBound(-1.0,
                 point.contains(QStringLiteral("tiltY")) ? point.value(QStringLiteral("tiltY")).toDouble() : 0.0,
                 1.0));
      normalized_point.insert(
          QStringLiteral("erase"),
          point.contains(QStringLiteral("erase")) ? point.value(QStringLiteral("erase")).toBool() : false);
      return normalized_point;
    };
    const auto points = candidate.value(QStringLiteral("brushPoints")).toList();
    for (const auto& point_value : points) {
      const auto normalized_point = normalize_brush_point(point_value);
      normalized_points.append(normalized_point);
    }
    mask.insert(QStringLiteral("brushPoints"), normalized_points);
    QVariantList normalized_strokes;
    const auto normalize_stroke = [&](const QVariant& stroke_value, int fallback_order) {
      const auto stroke_map = stroke_value.toMap();
      QVariantMap stroke;
      const auto id = stroke_map.value(QStringLiteral("id")).toString().simplified();
      const auto order = std::max(
          1,
          stroke_map.contains(QStringLiteral("createdAtOrder"))
              ? stroke_map.value(QStringLiteral("createdAtOrder")).toInt()
              : fallback_order);
      QVariantList samples;
      const auto sample_values = stroke_map.value(QStringLiteral("samples")).toList();
      for (const auto& sample_value : sample_values) {
        samples.append(normalize_brush_point(sample_value));
      }
      if (samples.isEmpty()) {
        return QVariantMap{};
      }
      QVariantMap settings = stroke_map.value(QStringLiteral("settings")).toMap();
      if (settings.isEmpty()) {
        settings.insert(QStringLiteral("preset"), mask.value(QStringLiteral("brushPreset")).toString());
        settings.insert(QStringLiteral("size"), mask.value(QStringLiteral("brushSize")).toDouble());
        settings.insert(QStringLiteral("flow"), mask.value(QStringLiteral("brushFlow")).toDouble());
        settings.insert(QStringLiteral("hardness"), mask.value(QStringLiteral("brushHardness")).toDouble());
        settings.insert(QStringLiteral("spacing"), mask.value(QStringLiteral("brushSpacing")).toDouble());
        settings.insert(QStringLiteral("smoothing"), mask.value(QStringLiteral("brushSmoothing")).toDouble());
        settings.insert(QStringLiteral("pressureSize"), mask.value(QStringLiteral("brushPressureSize")).toDouble());
        settings.insert(QStringLiteral("pressureFlow"), mask.value(QStringLiteral("brushPressureFlow")).toDouble());
        settings.insert(
            QStringLiteral("pressureOpacity"),
            mask.value(QStringLiteral("brushPressureOpacity")).toDouble());
        settings.insert(QStringLiteral("buildup"), mask.value(QStringLiteral("brushBuildup")).toBool());
        settings.insert(QStringLiteral("eraseMode"), mask.value(QStringLiteral("brushEraseMode")).toBool());
        settings.insert(QStringLiteral("autoMask"), mask.value(QStringLiteral("brushAutoMask")).toBool());
        settings.insert(
            QStringLiteral("autoMaskTolerance"),
            mask.value(QStringLiteral("brushAutoMaskTolerance")).toDouble());
        settings.insert(
            QStringLiteral("autoMaskContinuousSampling"),
            mask.value(QStringLiteral("brushAutoMaskContinuousSampling")).toBool());
        settings.insert(
            QStringLiteral("protectEdges"),
            mask.value(QStringLiteral("brushProtectEdges")).toBool());
        settings.insert(QStringLiteral("feather"), mask.value(QStringLiteral("feather")).toDouble());
      }
      if (!settings.contains(QStringLiteral("pressureSize"))) {
        settings.insert(QStringLiteral("pressureSize"), mask.value(QStringLiteral("brushPressureSize")).toDouble());
      }
      if (!settings.contains(QStringLiteral("pressureFlow"))) {
        settings.insert(QStringLiteral("pressureFlow"), mask.value(QStringLiteral("brushPressureFlow")).toDouble());
      }
      if (!settings.contains(QStringLiteral("pressureOpacity"))) {
        settings.insert(
            QStringLiteral("pressureOpacity"),
            mask.value(QStringLiteral("brushPressureOpacity")).toDouble());
      }
      if (!settings.contains(QStringLiteral("autoMaskContinuousSampling"))) {
        settings.insert(
            QStringLiteral("autoMaskContinuousSampling"),
            mask.value(QStringLiteral("brushAutoMaskContinuousSampling")).toBool());
      }
      if (!settings.contains(QStringLiteral("protectEdges"))) {
        settings.insert(
            QStringLiteral("protectEdges"),
            mask.value(QStringLiteral("brushProtectEdges")).toBool());
      }
      stroke.insert(QStringLiteral("id"), id.isEmpty() ? QStringLiteral("stroke-%1").arg(order) : id);
      stroke.insert(QStringLiteral("createdAtOrder"), order);
      stroke.insert(
          QStringLiteral("operation"),
          normalized_brush_stroke_operation(
              stroke_map.value(QStringLiteral("operation"), mask.value(QStringLiteral("operation"))).toString()));
      stroke.insert(QStringLiteral("sampleCount"), samples.size());
      stroke.insert(QStringLiteral("settings"), settings);
      stroke.insert(QStringLiteral("samples"), samples);
      return stroke;
    };
    const auto stroke_values = candidate.value(QStringLiteral("brushStrokes")).toList();
    for (auto stroke_index = 0; stroke_index < stroke_values.size(); ++stroke_index) {
      const auto normalized_stroke = normalize_stroke(stroke_values.at(stroke_index), stroke_index + 1);
      if (!normalized_stroke.isEmpty()) {
        normalized_strokes.append(normalized_stroke);
      }
    }
    if (normalized_strokes.isEmpty() && !normalized_points.isEmpty()) {
      QVariantMap legacy_stroke;
      legacy_stroke.insert(QStringLiteral("createdAtOrder"), 1);
      legacy_stroke.insert(QStringLiteral("operation"), mask.value(QStringLiteral("operation")).toString());
      legacy_stroke.insert(QStringLiteral("samples"), normalized_points);
      normalized_strokes.append(normalize_stroke(legacy_stroke, 1));
    }
    mask.insert(QStringLiteral("brushStrokes"), normalized_strokes);
    const auto segmentation_data =
        candidate.value(QStringLiteral("segmentationData")).toByteArray();
    const auto segmentation_width =
        candidate.value(QStringLiteral("segmentationWidth")).toInt();
    const auto segmentation_height =
        candidate.value(QStringLiteral("segmentationHeight")).toInt();
    if (valid_segmentation_payload(
            segmentation_data,
            segmentation_width,
            segmentation_height)) {
      mask.insert(QStringLiteral("segmentationData"), segmentation_data);
      mask.insert(QStringLiteral("segmentationWidth"), segmentation_width);
      mask.insert(QStringLiteral("segmentationHeight"), segmentation_height);
      mask.insert(
          QStringLiteral("rasterSchemaVersion"),
          std::max(1, map_int_value_or(candidate, QStringLiteral("rasterSchemaVersion"), 1)));
    }
    if (candidate.contains(QStringLiteral("brushAlphaSourceContract"))) {
      mask.insert(
          QStringLiteral("brushAlphaSourceContract"),
          candidate.value(QStringLiteral("brushAlphaSourceContract")).toString());
      mask.insert(
          QStringLiteral("brushAlphaSourceOfTruth"),
          candidate.value(QStringLiteral("brushAlphaSourceOfTruth")).toString());
      mask.insert(
          QStringLiteral("brushAlphaStrokeCount"),
          std::max(0, candidate.value(QStringLiteral("brushAlphaStrokeCount")).toInt()));
      mask.insert(
          QStringLiteral("brushAlphaPointCount"),
          std::max(0, candidate.value(QStringLiteral("brushAlphaPointCount")).toInt()));
      mask.insert(
          QStringLiteral("brushAlphaRasterWidth"),
          std::max(0, candidate.value(QStringLiteral("brushAlphaRasterWidth")).toInt()));
      mask.insert(
          QStringLiteral("brushAlphaRasterHeight"),
          std::max(0, candidate.value(QStringLiteral("brushAlphaRasterHeight")).toInt()));
      mask.insert(
          QStringLiteral("brushAlphaTileCacheKind"),
          candidate.value(QStringLiteral("brushAlphaTileCacheKind")).toString());
      mask.insert(
          QStringLiteral("brushAlphaTileCacheEnabled"),
          candidate.value(QStringLiteral("brushAlphaTileCacheEnabled")).toBool());
      mask.insert(
          QStringLiteral("brushAlphaTileCacheTileSize"),
          std::max(0, candidate.value(QStringLiteral("brushAlphaTileCacheTileSize")).toInt()));
      mask.insert(
          QStringLiteral("brushAlphaTileCacheDirtyTileCount"),
          std::max(0, candidate.value(QStringLiteral("brushAlphaTileCacheDirtyTileCount")).toInt()));
      mask.insert(
          QStringLiteral("brushAlphaSourceSignature"),
          candidate.value(QStringLiteral("brushAlphaSourceSignature")).toString());
    }
  } else if (normalized_kind == QStringLiteral("polygon") || normalized_kind == QStringLiteral("lasso")) {
    QVariantList normalized_points;
    const auto points = candidate.value(QStringLiteral("polygonPoints")).toList();
    for (const auto& point_value : points) {
      const auto point = point_value.toMap();
      QVariantMap normalized_point;
      normalized_point.insert(QStringLiteral("x"), point.value(QStringLiteral("x")).toDouble());
      normalized_point.insert(QStringLiteral("y"), point.value(QStringLiteral("y")).toDouble());
      normalized_points.append(normalized_point);
    }
    mask.insert(QStringLiteral("polygonPoints"), normalized_points);
    if (normalized_kind == QStringLiteral("lasso")) {
      QVariantList normalized_raw_points;
      const auto raw_points = candidate.value(QStringLiteral("lassoRawPoints")).toList();
      for (const auto& point_value : raw_points) {
        const auto point = point_value.toMap();
        QVariantMap normalized_point;
        normalized_point.insert(QStringLiteral("x"), point.value(QStringLiteral("x")).toDouble());
        normalized_point.insert(QStringLiteral("y"), point.value(QStringLiteral("y")).toDouble());
        normalized_raw_points.append(normalized_point);
      }
      mask.insert(QStringLiteral("lassoRawPoints"), normalized_raw_points);
      mask.insert(
          QStringLiteral("lassoRawPointCount"),
          std::max(
              0,
              candidate.contains(QStringLiteral("lassoRawPointCount"))
                  ? candidate.value(QStringLiteral("lassoRawPointCount")).toInt()
                  : static_cast<int>(normalized_raw_points.size())));
      mask.insert(
          QStringLiteral("lassoSimplifiedPointCount"),
          std::max(
              0,
              candidate.contains(QStringLiteral("lassoSimplifiedPointCount"))
                  ? candidate.value(QStringLiteral("lassoSimplifiedPointCount")).toInt()
                  : static_cast<int>(normalized_points.size())));
      mask.insert(
          QStringLiteral("lassoClosed"),
          candidate.contains(QStringLiteral("lassoClosed"))
              ? candidate.value(QStringLiteral("lassoClosed")).toBool()
              : false);
      mask.insert(
          QStringLiteral("lassoSimplified"),
          candidate.contains(QStringLiteral("lassoSimplified"))
              ? candidate.value(QStringLiteral("lassoSimplified")).toBool()
              : false);
      mask.insert(
          QStringLiteral("lassoSmoothed"),
          candidate.contains(QStringLiteral("lassoSmoothed"))
              ? candidate.value(QStringLiteral("lassoSmoothed")).toBool()
              : false);
      mask.insert(
          QStringLiteral("lassoFreehandPath"),
          candidate.contains(QStringLiteral("lassoFreehandPath"))
              ? candidate.value(QStringLiteral("lassoFreehandPath")).toBool()
              : true);
    }
    if (candidate.contains(QStringLiteral("shapeAlphaSourceContract"))) {
      mask.insert(
          QStringLiteral("shapeAlphaSourceContract"),
          candidate.value(QStringLiteral("shapeAlphaSourceContract")).toString());
      mask.insert(
          QStringLiteral("shapeAlphaSourceOfTruth"),
          candidate.value(QStringLiteral("shapeAlphaSourceOfTruth")).toString());
      mask.insert(
          QStringLiteral("shapeAlphaPointCount"),
          std::max(0, candidate.value(QStringLiteral("shapeAlphaPointCount")).toInt()));
      mask.insert(
          QStringLiteral("shapeAlphaRawPointCount"),
          std::max(0, candidate.value(QStringLiteral("shapeAlphaRawPointCount")).toInt()));
      mask.insert(
          QStringLiteral("shapeAlphaClosed"),
          candidate.value(QStringLiteral("shapeAlphaClosed")).toBool());
      mask.insert(
          QStringLiteral("shapeAlphaEditable"),
          candidate.value(QStringLiteral("shapeAlphaEditable")).toBool());
      mask.insert(
          QStringLiteral("shapeAlphaTileCacheKind"),
          candidate.value(QStringLiteral("shapeAlphaTileCacheKind")).toString());
      mask.insert(
          QStringLiteral("shapeAlphaTileCacheEnabled"),
          candidate.value(QStringLiteral("shapeAlphaTileCacheEnabled")).toBool());
      mask.insert(
          QStringLiteral("shapeAlphaTileCacheTileSize"),
          std::max(0, candidate.value(QStringLiteral("shapeAlphaTileCacheTileSize")).toInt()));
      mask.insert(
          QStringLiteral("shapeAlphaSourceSignature"),
          candidate.value(QStringLiteral("shapeAlphaSourceSignature")).toString());
    }
  } else if (normalized_kind == QStringLiteral("luminance")) {
    auto min_value = candidate.contains(QStringLiteral("luminanceMin"))
                         ? candidate.value(QStringLiteral("luminanceMin")).toDouble()
                         : 0.2;
    auto max_value = candidate.contains(QStringLiteral("luminanceMax"))
                         ? candidate.value(QStringLiteral("luminanceMax")).toDouble()
                         : 0.8;
    if (min_value > max_value) {
      std::swap(min_value, max_value);
    }
    mask.insert(QStringLiteral("luminanceMin"), min_value);
    mask.insert(QStringLiteral("luminanceMax"), max_value);
    mask.insert(
        QStringLiteral("rangeFeather"),
        candidate.contains(QStringLiteral("rangeFeather"))
            ? candidate.value(QStringLiteral("rangeFeather")).toDouble()
            : 0.1);
    mask.insert(
        QStringLiteral("luminanceSamples"),
        normalized_luminance_range_samples(candidate.value(QStringLiteral("luminanceSamples")).toList()));
  } else if (normalized_kind == QStringLiteral("color")) {
    auto min_value = candidate.contains(QStringLiteral("saturationMin"))
                         ? candidate.value(QStringLiteral("saturationMin")).toDouble()
                         : 0.1;
    auto max_value = candidate.contains(QStringLiteral("saturationMax"))
                         ? candidate.value(QStringLiteral("saturationMax")).toDouble()
                         : 1.0;
    if (min_value > max_value) {
      std::swap(min_value, max_value);
    }
    mask.insert(
        QStringLiteral("hueCenter"),
        candidate.contains(QStringLiteral("hueCenter"))
            ? candidate.value(QStringLiteral("hueCenter")).toDouble()
            : 0.08);
    mask.insert(
        QStringLiteral("hueRange"),
        candidate.contains(QStringLiteral("hueRange"))
            ? candidate.value(QStringLiteral("hueRange")).toDouble()
            : 0.12);
    mask.insert(QStringLiteral("saturationMin"), min_value);
    mask.insert(QStringLiteral("saturationMax"), max_value);
    mask.insert(
        QStringLiteral("rangeFeather"),
        candidate.contains(QStringLiteral("rangeFeather"))
            ? candidate.value(QStringLiteral("rangeFeather")).toDouble()
            : 0.08);
    mask.insert(
        QStringLiteral("colorSamples"),
        normalized_color_range_samples(candidate.value(QStringLiteral("colorSamples")).toList()));
  } else if (normalized_kind == QStringLiteral("depth")) {
    auto min_value = candidate.contains(QStringLiteral("depthMin"))
                         ? candidate.value(QStringLiteral("depthMin")).toDouble()
                         : 0.2;
    auto max_value = candidate.contains(QStringLiteral("depthMax"))
                         ? candidate.value(QStringLiteral("depthMax")).toDouble()
                         : 0.8;
    if (min_value > max_value) {
      std::swap(min_value, max_value);
    }
    mask.insert(QStringLiteral("depthMin"), min_value);
    mask.insert(QStringLiteral("depthMax"), max_value);
    mask.insert(
        QStringLiteral("rangeFeather"),
        candidate.contains(QStringLiteral("rangeFeather"))
            ? candidate.value(QStringLiteral("rangeFeather")).toDouble()
            : 0.1);
    mask.insert(
        QStringLiteral("depthSamples"),
        normalized_depth_range_samples(candidate.value(QStringLiteral("depthSamples")).toList()));
    const auto depth_data = candidate.value(QStringLiteral("depthData")).toByteArray();
    const auto depth_width = candidate.value(QStringLiteral("depthWidth")).toInt();
    const auto depth_height = candidate.value(QStringLiteral("depthHeight")).toInt();
    if (valid_segmentation_payload(depth_data, depth_width, depth_height)) {
      mask.insert(QStringLiteral("depthData"), depth_data);
      mask.insert(QStringLiteral("depthWidth"), depth_width);
      mask.insert(QStringLiteral("depthHeight"), depth_height);
    }
  } else if (is_raster_mask_kind(normalized_kind) || normalized_kind == QStringLiteral("brush")) {
    if (normalized_kind == QStringLiteral("alpha")) {
      mask.insert(
          QStringLiteral("alphaSourcePath"),
          candidate.value(QStringLiteral("alphaSourcePath")).toString());
      mask.insert(
          QStringLiteral("alphaImportInvert"),
          candidate.contains(QStringLiteral("alphaImportInvert"))
              ? candidate.value(QStringLiteral("alphaImportInvert")).toBool()
              : false);
      mask.insert(
          QStringLiteral("alphaImportThreshold"),
          qBound(0.0, candidate.value(QStringLiteral("alphaImportThreshold")).toDouble(), 1.0));
      const auto resample_mode =
          candidate.value(QStringLiteral("alphaImportResampleMode")).toString().simplified();
      mask.insert(
          QStringLiteral("alphaImportResampleMode"),
          resample_mode == QStringLiteral("Match Source") ? resample_mode : QStringLiteral("Original"));
      if (candidate.value(QStringLiteral("pixelSelection")).toBool()) {
        mask.insert(QStringLiteral("pixelSelection"), true);
        mask.insert(
            QStringLiteral("pixelSelectionContract"),
            candidate.value(QStringLiteral("pixelSelectionContract")).toString());
        mask.insert(
            QStringLiteral("pixelAlphaSamplingMode"),
            candidate.value(QStringLiteral("pixelAlphaSamplingMode")).toString());
        mask.insert(
            QStringLiteral("pixelDirtyX"),
            std::max(0, candidate.value(QStringLiteral("pixelDirtyX")).toInt()));
        mask.insert(
            QStringLiteral("pixelDirtyY"),
            std::max(0, candidate.value(QStringLiteral("pixelDirtyY")).toInt()));
        mask.insert(
            QStringLiteral("pixelDirtyWidth"),
            std::max(0, candidate.value(QStringLiteral("pixelDirtyWidth")).toInt()));
        mask.insert(
            QStringLiteral("pixelDirtyHeight"),
            std::max(0, candidate.value(QStringLiteral("pixelDirtyHeight")).toInt()));
        mask.insert(
            QStringLiteral("pixelLastOperation"),
            candidate.value(QStringLiteral("pixelLastOperation")).toString());
        mask.insert(
            QStringLiteral("pixelLastNormalizedX"),
            qBound(
                0.0,
                candidate.value(QStringLiteral("pixelLastNormalizedX")).toDouble(),
                1.0));
        mask.insert(
            QStringLiteral("pixelLastNormalizedY"),
            qBound(
                0.0,
                candidate.value(QStringLiteral("pixelLastNormalizedY")).toDouble(),
                1.0));
        mask.insert(
            QStringLiteral("pixelLastX"),
            std::max(0, candidate.value(QStringLiteral("pixelLastX")).toInt()));
        mask.insert(
            QStringLiteral("pixelLastY"),
            std::max(0, candidate.value(QStringLiteral("pixelLastY")).toInt()));
      }
    }
    const auto segmentation_data = candidate.value(QStringLiteral("segmentationData")).toByteArray();
    const auto segmentation_width = candidate.value(QStringLiteral("segmentationWidth")).toInt();
    const auto segmentation_height = candidate.value(QStringLiteral("segmentationHeight")).toInt();
    if (valid_segmentation_payload(segmentation_data, segmentation_width, segmentation_height)) {
      mask.insert(QStringLiteral("segmentationData"), segmentation_data);
      mask.insert(QStringLiteral("segmentationWidth"), segmentation_width);
      mask.insert(QStringLiteral("segmentationHeight"), segmentation_height);
      mask.insert(
          QStringLiteral("rasterSchemaVersion"),
          std::max(1, map_int_value_or(candidate, QStringLiteral("rasterSchemaVersion"), 1)));
      const auto cleanup_source_data =
          candidate.value(QStringLiteral("generatedMaskCleanupSourceData")).toByteArray();
      const auto cleanup_source_width =
          candidate.value(QStringLiteral("generatedMaskCleanupSourceWidth")).toInt();
      const auto cleanup_source_height =
          candidate.value(QStringLiteral("generatedMaskCleanupSourceHeight")).toInt();
      if (valid_segmentation_payload(cleanup_source_data, cleanup_source_width, cleanup_source_height) &&
          cleanup_source_width == segmentation_width &&
          cleanup_source_height == segmentation_height) {
        mask.insert(QStringLiteral("generatedMaskCleanupSourceData"), cleanup_source_data);
        mask.insert(QStringLiteral("generatedMaskCleanupSourceWidth"), cleanup_source_width);
        mask.insert(QStringLiteral("generatedMaskCleanupSourceHeight"), cleanup_source_height);
      }
    }
  }
  copy_inpaint_patch_payload(&mask, candidate);
  copy_generated_mask_metadata(&mask, candidate);
  return mask;
}

inline bool mask_definition_affects_render(const QVariantMap& candidate, int ordinal = 1) {
  const auto mask = normalized_mask_definition(candidate, ordinal);
  return !(mask.value(QStringLiteral("toolRole")).toString() == QStringLiteral("retouch") &&
           !mask.value(QStringLiteral("committed")).toBool());
}

inline void copy_selection_adjustments(QVariantMap* target, const QVariantMap& source) {
  if (target == nullptr) {
    return;
  }
  target->insert(QStringLiteral("exposure"), source.value(QStringLiteral("exposure")).toDouble());
  target->insert(QStringLiteral("contrast"), source.value(QStringLiteral("contrast")).toDouble());
  target->insert(QStringLiteral("highlights"), source.value(QStringLiteral("highlights")).toDouble());
  target->insert(QStringLiteral("shadows"), source.value(QStringLiteral("shadows")).toDouble());
  target->insert(QStringLiteral("whites"), source.value(QStringLiteral("whites")).toDouble());
  target->insert(QStringLiteral("blacks"), source.value(QStringLiteral("blacks")).toDouble());
  target->insert(QStringLiteral("temperature"), source.value(QStringLiteral("temperature")).toDouble());
  target->insert(QStringLiteral("tint"), source.value(QStringLiteral("tint")).toDouble());
  target->insert(QStringLiteral("vibrance"), source.value(QStringLiteral("vibrance")).toDouble());
  target->insert(QStringLiteral("saturation"), source.value(QStringLiteral("saturation")).toDouble());
  target->insert(QStringLiteral("texture"), source.value(QStringLiteral("texture")).toDouble());
  target->insert(QStringLiteral("clarity"), source.value(QStringLiteral("clarity")).toDouble());
  target->insert(QStringLiteral("dehaze"), source.value(QStringLiteral("dehaze")).toDouble());
  target->insert(QStringLiteral("sharpening"), source.value(QStringLiteral("sharpening")).toDouble());
  target->insert(QStringLiteral("sharpenRadius"), source.value(QStringLiteral("sharpenRadius"), 1.0).toDouble());
  target->insert(QStringLiteral("sharpenDetail"), source.value(QStringLiteral("sharpenDetail")).toDouble());
  target->insert(QStringLiteral("sharpenMasking"), source.value(QStringLiteral("sharpenMasking")).toDouble());
  target->insert(QStringLiteral("luminanceNr"), source.value(QStringLiteral("luminanceNr")).toDouble());
  target->insert(QStringLiteral("colorNr"), source.value(QStringLiteral("colorNr")).toDouble());
  target->insert(QStringLiteral("hueShift"), source.value(QStringLiteral("hueShift")).toDouble());
  target->insert(
      QStringLiteral("saturationShift"),
      source.value(QStringLiteral("saturationShift")).toDouble());
  target->insert(QStringLiteral("colorizeHue"), source.value(QStringLiteral("colorizeHue")).toDouble());
  target->insert(
      QStringLiteral("colorizeStrength"),
      source.value(QStringLiteral("colorizeStrength")).toDouble());
  target->insert(
      QStringLiteral("selectionInvert"),
      source.value(QStringLiteral("selectionInvert")).toBool());
  target->insert(
      QStringLiteral("selectionExpand"),
      source.value(QStringLiteral("selectionExpand")).toDouble());
  target->insert(
      QStringLiteral("selectionEdgeContrast"),
      source.value(QStringLiteral("selectionEdgeContrast")).toDouble());
  target->insert(
      QStringLiteral("selectionSmooth"),
      source.value(QStringLiteral("selectionSmooth")).toDouble());
  target->insert(
      QStringLiteral("selectionFineEdge"),
      source.value(QStringLiteral("selectionFineEdge")).toDouble());
  target->insert(
      QStringLiteral("selectionDecontaminate"),
      source.value(QStringLiteral("selectionDecontaminate")).toDouble());
  target->insert(QStringLiteral("selectionGrow"), source.value(QStringLiteral("selectionGrow")).toDouble());
  target->insert(QStringLiteral("selectionShrink"), source.value(QStringLiteral("selectionShrink")).toDouble());
  target->insert(QStringLiteral("selectionFeather"), source.value(QStringLiteral("selectionFeather")).toDouble());
  target->insert(QStringLiteral("selectionBlur"), source.value(QStringLiteral("selectionBlur")).toDouble());
  target->insert(QStringLiteral("selectionHarden"), source.value(QStringLiteral("selectionHarden")).toDouble());
  target->insert(QStringLiteral("selectionDespeckle"), source.value(QStringLiteral("selectionDespeckle")).toDouble());
  target->insert(QStringLiteral("selectionFillHoles"), source.value(QStringLiteral("selectionFillHoles")).toDouble());
  target->insert(
      QStringLiteral("selectionRemoveIslands"),
      source.value(QStringLiteral("selectionRemoveIslands")).toDouble());
  target->insert(
      QStringLiteral("maskSetSelectionInvert"),
      source.value(QStringLiteral("maskSetSelectionInvert")).toBool());
  target->insert(
      QStringLiteral("maskSetSelectionExpand"),
      source.value(QStringLiteral("maskSetSelectionExpand")).toDouble());
  target->insert(
      QStringLiteral("maskSetSelectionSmooth"),
      source.value(QStringLiteral("maskSetSelectionSmooth")).toDouble());
  target->insert(
      QStringLiteral("maskSetSelectionFineEdge"),
      source.value(QStringLiteral("maskSetSelectionFineEdge")).toDouble());
  target->insert(
      QStringLiteral("maskSetSelectionEdgeContrast"),
      source.value(QStringLiteral("maskSetSelectionEdgeContrast")).toDouble());
  target->insert(QStringLiteral("maskSetDensity"), source.value(QStringLiteral("maskSetDensity"), 1.0).toDouble());
  target->insert(QStringLiteral("maskSetOpacity"), source.value(QStringLiteral("maskSetOpacity"), 1.0).toDouble());
}

inline QStringList mask_set_adjustment_keys() {
  return {
      QStringLiteral("exposure"),
      QStringLiteral("contrast"),
      QStringLiteral("highlights"),
      QStringLiteral("shadows"),
      QStringLiteral("whites"),
      QStringLiteral("blacks"),
      QStringLiteral("temperature"),
      QStringLiteral("tint"),
      QStringLiteral("vibrance"),
      QStringLiteral("saturation"),
      QStringLiteral("texture"),
      QStringLiteral("clarity"),
      QStringLiteral("dehaze"),
      QStringLiteral("sharpening"),
      QStringLiteral("sharpenRadius"),
      QStringLiteral("sharpenDetail"),
      QStringLiteral("sharpenMasking"),
      QStringLiteral("luminanceNr"),
      QStringLiteral("colorNr"),
      QStringLiteral("hueShift"),
      QStringLiteral("saturationShift"),
      QStringLiteral("colorizeHue"),
      QStringLiteral("colorizeStrength"),
      QStringLiteral("selectionEdgeContrast"),
      QStringLiteral("selectionSmooth"),
      QStringLiteral("selectionFineEdge"),
      QStringLiteral("selectionDecontaminate"),
      QStringLiteral("selectionGrow"),
      QStringLiteral("selectionShrink"),
      QStringLiteral("selectionFeather"),
      QStringLiteral("selectionBlur"),
      QStringLiteral("selectionHarden"),
      QStringLiteral("selectionDespeckle"),
      QStringLiteral("selectionFillHoles"),
      QStringLiteral("selectionRemoveIslands"),
      QStringLiteral("maskSetSelectionInvert"),
      QStringLiteral("maskSetSelectionExpand"),
      QStringLiteral("maskSetSelectionSmooth"),
      QStringLiteral("maskSetSelectionFineEdge"),
      QStringLiteral("maskSetSelectionEdgeContrast"),
      QStringLiteral("maskSetDensity"),
      QStringLiteral("maskSetOpacity"),
  };
}

inline bool mask_component_matte_key(const QString& key) {
  return key == QStringLiteral("segmentationData") ||
         key == QStringLiteral("segmentationWidth") ||
         key == QStringLiteral("segmentationHeight") ||
         key == QStringLiteral("rasterSchemaVersion") ||
         key == QStringLiteral("depthData") ||
         key == QStringLiteral("depthWidth") ||
         key == QStringLiteral("depthHeight") ||
         key == QStringLiteral("maskRasterWidth") ||
         key == QStringLiteral("maskRasterHeight") ||
         key == QStringLiteral("alphaSamplingMode") ||
         key == QStringLiteral("softnessMode") ||
         key == QStringLiteral("generatedMaskMetadata") ||
         key == QStringLiteral("generatedMaskConfidenceData") ||
         key == QStringLiteral("generatedMaskConfidenceWidth") ||
         key == QStringLiteral("generatedMaskConfidenceHeight") ||
         key == QStringLiteral("generatedMaskQualityReport") ||
         key == QStringLiteral("generatedMaskStale") ||
         key == QStringLiteral("generatedAlphaSourceContract") ||
         key == QStringLiteral("generatedAlphaModelName") ||
         key == QStringLiteral("generatedAlphaModelVersion") ||
         key == QStringLiteral("generatedAlphaTargetType") ||
         key == QStringLiteral("generatedAlphaPromptType") ||
         key == QStringLiteral("generatedAlphaSourceImageVersion") ||
         key == QStringLiteral("generatedAlphaWidth") ||
         key == QStringLiteral("generatedAlphaHeight") ||
         key == QStringLiteral("generatedAlphaCoordinateSpaceContract") ||
         key == QStringLiteral("generatedAlphaSourceCoordinateSpace") ||
         key == QStringLiteral("generatedAlphaPromptCoordinateSpace") ||
         key == QStringLiteral("generatedAlphaRasterCoordinateSpace") ||
         key == QStringLiteral("generatedAlphaDownscaleFactor") ||
         key == QStringLiteral("generatedAlphaConfidenceAvailable") ||
         key == QStringLiteral("generatedAlphaConfidenceWidth") ||
         key == QStringLiteral("generatedAlphaConfidenceHeight") ||
         key == QStringLiteral("generatedAlphaRefinementStateSignature") ||
         key == QStringLiteral("generatedAlphaSourceSignature");
}

inline bool mask_component_constraint_key(const QString& key) {
  return key == QStringLiteral("refineLuminanceMin") ||
         key == QStringLiteral("refineLuminanceMax") ||
         key == QStringLiteral("refineHueCenter") ||
         key == QStringLiteral("refineHueRange") ||
         key == QStringLiteral("refineSaturationMin") ||
         key == QStringLiteral("refineSaturationMax") ||
         key == QStringLiteral("refineDepthMin") ||
         key == QStringLiteral("refineDepthMax") ||
         key == QStringLiteral("refineEdgeConfidenceMin") ||
         key == QStringLiteral("refineRangeFeather") ||
         key == QStringLiteral("generatedMaskHoleFillThreshold") ||
         key == QStringLiteral("generatedMaskIslandRemovalThreshold") ||
         key == QStringLiteral("generatedMaskHolesFilled") ||
         key == QStringLiteral("generatedMaskIslandsRemoved") ||
         key == QStringLiteral("generatedMaskPixelsFilled") ||
         key == QStringLiteral("generatedMaskPixelsRemoved") ||
         key == QStringLiteral("matteCleanupSchemaVersion") ||
         key == QStringLiteral("matteCleanupProfile") ||
         key == QStringLiteral("matteCleanupDefaultsApplied") ||
         key == QStringLiteral("luminanceMin") ||
         key == QStringLiteral("luminanceMax") ||
         key == QStringLiteral("hueCenter") ||
         key == QStringLiteral("hueRange") ||
         key == QStringLiteral("saturationMin") ||
         key == QStringLiteral("saturationMax") ||
         key == QStringLiteral("depthMin") ||
         key == QStringLiteral("depthMax") ||
         key == QStringLiteral("rangeFeather") ||
         key == QStringLiteral("luminanceSamples") ||
         key == QStringLiteral("colorSamples") ||
         key == QStringLiteral("depthSamples");
}

inline bool mask_component_metadata_key(const QString& key) {
  return key == QStringLiteral("id") ||
         key == QStringLiteral("schemaVersion") ||
         key == QStringLiteral("sourceSchemaVersion") ||
         key == QStringLiteral("parentId") ||
         key == QStringLiteral("outputId") ||
         key == QStringLiteral("reusableOutput") ||
         key == QStringLiteral("name") ||
         key == QStringLiteral("kind") ||
         key == QStringLiteral("toolRole") ||
         key == QStringLiteral("operation") ||
         key == QStringLiteral("enabled") ||
         key == QStringLiteral("solo") ||
         key == QStringLiteral("locked") ||
         key == QStringLiteral("committed") ||
         key == QStringLiteral("maskSourceClass") ||
         key == QStringLiteral("maskCoordinateSpace") ||
         key == QStringLiteral("sourceImageRevision") ||
         key == QStringLiteral("retouchOrder") ||
         key == QStringLiteral("opacity") ||
         key == QStringLiteral("density") ||
         key == QStringLiteral("invert") ||
         key == QStringLiteral("blendMode") ||
         key == QStringLiteral("maskSetId") ||
         key == QStringLiteral("maskSetName");
}

struct MaskSourceModel final {
  int flat_index = -1;
  QVariantMap metadata;
  QVariantMap adjustments;
  QVariantMap geometry;
  QVariantMap constraints;
  QVariantMap matte;

  [[nodiscard]] QVariantMap toVariantMap() const {
    QVariantMap model;
    model.insert(QStringLiteral("modelSchemaVersion"), 1);
    model.insert(QStringLiteral("flatIndex"), flat_index);
    model.insert(QStringLiteral("metadata"), metadata);
    model.insert(QStringLiteral("adjustments"), adjustments);
    model.insert(QStringLiteral("geometry"), geometry);
    model.insert(QStringLiteral("constraints"), constraints);
    model.insert(QStringLiteral("matte"), matte);
    return model;
  }

  [[nodiscard]] QVariantMap toLegacyVariantMap() const {
    QVariantMap mask;
    const auto append_bucket = [&mask](const QVariantMap& bucket) {
      for (auto iterator = bucket.cbegin(); iterator != bucket.cend(); ++iterator) {
        mask.insert(iterator.key(), iterator.value());
      }
    };
    append_bucket(metadata);
    append_bucket(adjustments);
    append_bucket(geometry);
    append_bucket(constraints);
    append_bucket(matte);
    return mask;
  }

  [[nodiscard]] QJsonObject toJsonObject() const {
    QJsonObject object;
    object.insert(QStringLiteral("modelSchemaVersion"), 1);
    object.insert(QStringLiteral("flatIndex"), flat_index);
    object.insert(QStringLiteral("metadata"), jsonBucket(metadata));
    object.insert(QStringLiteral("adjustments"), jsonBucket(adjustments));
    object.insert(QStringLiteral("geometry"), jsonBucket(geometry));
    object.insert(QStringLiteral("constraints"), jsonBucket(constraints));
    object.insert(QStringLiteral("matte"), jsonBucket(matte));
    return object;
  }

  [[nodiscard]] static MaskSourceModel fromDefinition(
      const QVariantMap& mask_definition,
      int flat_index) {
    MaskSourceModel model;
    model.flat_index = flat_index;
    copy_selection_adjustments(&model.adjustments, mask_definition);
    const auto adjustment_keys = mask_set_adjustment_keys();
    for (auto iterator = mask_definition.cbegin(); iterator != mask_definition.cend(); ++iterator) {
      if (adjustment_keys.contains(iterator.key())) {
        continue;
      }
      if (mask_component_matte_key(iterator.key())) {
        model.matte.insert(iterator.key(), iterator.value());
      } else if (mask_component_constraint_key(iterator.key())) {
        model.constraints.insert(iterator.key(), iterator.value());
      } else if (mask_component_metadata_key(iterator.key())) {
        model.metadata.insert(iterator.key(), iterator.value());
      } else {
        model.geometry.insert(iterator.key(), iterator.value());
      }
    }
    return model;
  }

  [[nodiscard]] static MaskSourceModel fromVariantMap(
      const QVariantMap& candidate,
      int ordinal = 1) {
    return fromDefinition(normalized_mask_definition(candidate, ordinal), ordinal - 1);
  }

 private:
  [[nodiscard]] static QJsonObject jsonBucket(const QVariantMap& bucket) {
    QJsonObject object;
    for (auto iterator = bucket.cbegin(); iterator != bucket.cend(); ++iterator) {
      if (iterator.value().metaType().id() == QMetaType::QByteArray) {
        object.insert(iterator.key(), QString::fromLatin1(iterator.value().toByteArray().toBase64()));
      } else {
        object.insert(iterator.key(), QJsonValue::fromVariant(iterator.value()));
      }
    }
    return object;
  }
};

struct MaskSourceComponentModel final {
  int flat_index = -1;
  QVariantMap metadata;
  QVariantMap geometry;
  QVariantMap constraints;
  QVariantMap matte;
  QVariantMap adjustment_overrides;

  [[nodiscard]] QVariantMap toVariantMap() const {
    QVariantMap component;
    component.insert(QStringLiteral("flatIndex"), flat_index);
    component.insert(QStringLiteral("metadata"), metadata);
    component.insert(QStringLiteral("geometry"), geometry);
    component.insert(QStringLiteral("constraints"), constraints);
    component.insert(QStringLiteral("matte"), matte);
    component.insert(QStringLiteral("adjustmentOverrides"), adjustment_overrides);
    return component;
  }
};

struct MaskSetModel final {
  QString id;
  QString name;
  QVariantMap adjustments;
  std::vector<MaskSourceComponentModel> components;

  [[nodiscard]] QVariantMap toVariantMap() const {
    QVariantList component_values;
    component_values.reserve(static_cast<int>(components.size()));
    for (const auto& component : components) {
      component_values.append(component.toVariantMap());
    }
    QVariantMap model;
    model.insert(QStringLiteral("schemaVersion"), 2);
    model.insert(QStringLiteral("id"), id);
    model.insert(QStringLiteral("name"), name);
    model.insert(QStringLiteral("adjustments"), adjustments);
    model.insert(QStringLiteral("components"), component_values);
    return model;
  }
};

inline QVariantMap mask_set_adjustments_from_definition(const QVariantMap& mask_definition) {
  QVariantMap adjustments;
  copy_selection_adjustments(&adjustments, mask_definition);
  return adjustments;
}

inline bool mask_adjustment_values_equal(const QVariant& left, const QVariant& right) {
  if (left.userType() == QMetaType::Bool || right.userType() == QMetaType::Bool) {
    return left.toBool() == right.toBool();
  }
  if (left.canConvert<double>() && right.canConvert<double>()) {
    return std::abs(left.toDouble() - right.toDouble()) < 1e-9;
  }
  return left.toString() == right.toString();
}

inline QVariantMap mask_source_adjustment_overrides(
    const QVariantMap& mask_definition,
    const QVariantMap& set_adjustments) {
  QVariantMap overrides;
  const auto adjustment_keys = mask_set_adjustment_keys();
  for (const auto& key : adjustment_keys) {
    if (!mask_definition.contains(key) || !set_adjustments.contains(key)) {
      continue;
    }
    if (!mask_adjustment_values_equal(mask_definition.value(key), set_adjustments.value(key))) {
      overrides.insert(key, mask_definition.value(key));
    }
  }
  return overrides;
}

inline MaskSourceComponentModel mask_source_component_from_definition(
    const QVariantMap& mask_definition,
    int flat_index,
    const QVariantMap& set_adjustments) {
  MaskSourceComponentModel component;
  const auto source_model = MaskSourceModel::fromDefinition(mask_definition, flat_index);
  component.flat_index = source_model.flat_index;
  component.metadata = source_model.metadata;
  component.geometry = source_model.geometry;
  component.constraints = source_model.constraints;
  component.matte = source_model.matte;
  component.adjustment_overrides =
      mask_source_adjustment_overrides(mask_definition, set_adjustments);
  return component;
}

inline std::vector<MaskSourceModel> typed_mask_source_models_from_stack(const QVariantList& stack) {
  std::vector<MaskSourceModel> models;
  models.reserve(static_cast<std::size_t>(stack.size()));
  auto ordinal = 1;
  for (const auto& value : stack) {
    models.push_back(MaskSourceModel::fromVariantMap(value.toMap(), ordinal));
    ++ordinal;
  }
  return models;
}

inline QVariantList typed_mask_source_model_variants_from_stack(const QVariantList& stack) {
  QVariantList variants;
  const auto models = typed_mask_source_models_from_stack(stack);
  variants.reserve(static_cast<int>(models.size()));
  for (const auto& model : models) {
    variants.append(model.toVariantMap());
  }
  return variants;
}

inline std::vector<MaskSetModel> typed_mask_set_models_from_stack(const QVariantList& stack) {
  std::vector<MaskSetModel> models;
  QHash<QString, std::size_t> indices_by_id;
  auto ordinal = 1;
  for (const auto& value : stack) {
    const auto mask = normalized_mask_definition(value.toMap(), ordinal);
    const auto flat_index = ordinal - 1;
    ++ordinal;
    if (mask.value(QStringLiteral("toolRole")).toString() != QStringLiteral("selection")) {
      continue;
    }
    const auto set_id = mask.value(QStringLiteral("maskSetId")).toString();
    auto set_index_iterator = indices_by_id.constFind(set_id);
    if (set_index_iterator == indices_by_id.constEnd()) {
      MaskSetModel model;
      model.id = set_id;
      model.name = mask.value(QStringLiteral("maskSetName")).toString();
      model.adjustments = mask_set_adjustments_from_definition(mask);
      models.push_back(model);
      const auto new_index = models.size() - 1;
      indices_by_id.insert(set_id, new_index);
      set_index_iterator = indices_by_id.constFind(set_id);
    }
    models[*set_index_iterator].components.push_back(
        mask_source_component_from_definition(
            mask,
            flat_index,
            models[*set_index_iterator].adjustments));
  }
  return models;
}

inline QVariantList typed_mask_set_model_variants_from_stack(const QVariantList& stack) {
  QVariantList variants;
  const auto models = typed_mask_set_models_from_stack(stack);
  variants.reserve(static_cast<int>(models.size()));
  for (const auto& model : models) {
    variants.append(model.toVariantMap());
  }
  return variants;
}

inline QVariantList normalize_mask_stack(const QVariantList& stack) {
  QVariantList normalized;
  normalized.reserve(stack.size());
  auto ordinal = 1;
  auto retouch_order = 1;
  for (const auto& value : stack) {
    auto mask = normalized_mask_definition(value.toMap(), ordinal);
    if (mask.value(QStringLiteral("toolRole")).toString() == QStringLiteral("retouch")) {
      mask.insert(QStringLiteral("retouchOrder"), retouch_order);
      ++retouch_order;
    } else {
      mask.insert(QStringLiteral("retouchOrder"), 0);
    }
    normalized.append(mask);
    ++ordinal;
  }
  return normalized;
}

}  // namespace photo_editor::desktop

#include "edit_recipe_mask_state_queries.h"
