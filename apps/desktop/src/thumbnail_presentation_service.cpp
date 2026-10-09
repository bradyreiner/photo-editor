#include "thumbnail_presentation_service.h"

#include "mask_presentation_schema.h"

#include <algorithm>

#include <QHash>

namespace photo_editor::desktop {
namespace {

QString mask_identity(const QVariantMap& value, const QString& fallback) {
  const auto id = value.value(QStringLiteral("id")).toString();
  if (!id.isEmpty()) {
    return id;
  }
  const auto flat_index = value.value(QStringLiteral("flatIndex"), -1).toInt();
  return flat_index >= 0
             ? QStringLiteral("flat:%1").arg(flat_index)
             : fallback;
}

bool thumbnail_field(const QString& key) {
  static const QStringList prefixes{
      QStringLiteral("thumbnail"),
      QStringLiteral("async_job_"),
      QStringLiteral("observabilityThumbnail"),
      QStringLiteral("maskAlphaProvider"),
      QStringLiteral("mask_alpha_"),
      QStringLiteral("mask_source_"),
      QStringLiteral("mask_set_"),
      QStringLiteral("mask_work_"),
      QStringLiteral("brushAlpha"),
      QStringLiteral("shapeAlpha"),
      QStringLiteral("generatedAlpha"),
      QStringLiteral("scaledMatte"),
  };
  return std::any_of(prefixes.cbegin(), prefixes.cend(), [&](const QString& prefix) {
    return key.startsWith(prefix);
  });
}

void mark_entry_cache_hit(QVariantMap* entry) {
  if (entry == nullptr) {
    return;
  }
  const auto cache_key = entry->value(QStringLiteral("thumbnailCacheKey")).toString();
  insert_thumbnail_entry_cache_fields(
      entry,
      cache_key,
      entry->value(QStringLiteral("thumbnailCacheAssetId")).toString(),
      entry->value(QStringLiteral("thumbnailCacheRecipeHash")).toString(),
      entry->value(QStringLiteral("thumbnailCacheLayerId")).toString(),
      entry->value(QStringLiteral("thumbnailCacheMaskId")).toString(),
      entry->value(QStringLiteral("thumbnailCacheBackingWidth")).toInt(),
      entry->value(QStringLiteral("thumbnailCacheBackingHeight")).toInt(),
      entry->value(QStringLiteral("thumbnailCacheDevicePixelRatio")).toDouble(),
      entry->value(QStringLiteral("thumbnailCacheColorPipelineVersion")).toString(),
      entry->value(QStringLiteral("thumbnailCacheMaskRevision")).toInt(),
      true,
      QStringLiteral("entry-cache"));
}

void mark_contained_failure(
    QVariantMap* entry,
    const ThumbnailRefreshQueueCompletion& completion) {
  if (entry == nullptr || completion.failure_stage.isEmpty()) {
    return;
  }
  const auto reused =
      !entry->value(QStringLiteral("thumbnailSource")).toString().trimmed().isEmpty();
  entry->insert(QStringLiteral("thumbnailReady"), reused);
  entry->insert(QStringLiteral("thumbnailStale"), reused);
  entry->insert(QStringLiteral("thumbnailProgressive"), reused);
  entry->insert(QStringLiteral("thumbnailFailureContained"), true);
  entry->insert(QStringLiteral("thumbnailFailureStage"), completion.failure_stage);
  entry->insert(QStringLiteral("thumbnailFailureDetail"), completion.failure_detail);
  entry->insert(
      QStringLiteral("thumbnailFailureSignature"),
      QStringLiteral("stage:%1|job:%2|reused:%3")
          .arg(completion.failure_stage,
               completion.job_id,
               reused ? QStringLiteral("true") : QStringLiteral("false")));
}

void mark_mask_failure(
    QVariantList* sets,
    const ThumbnailRefreshQueueCompletion& completion) {
  if (sets == nullptr) {
    return;
  }
  for (auto set_index = 0; set_index < sets->size(); ++set_index) {
    auto set = sets->at(set_index).toMap();
    mark_contained_failure(&set, completion);
    auto sources = set.value(QStringLiteral("sources")).toList();
    for (auto source_index = 0; source_index < sources.size(); ++source_index) {
      auto source = sources.at(source_index).toMap();
      mark_contained_failure(&source, completion);
      sources[source_index] = source;
    }
    set.insert(QStringLiteral("sources"), sources);
    (*sets)[set_index] = set;
  }
}

void mark_layer_failure(
    QVariantList* layers,
    const ThumbnailRefreshQueueCompletion& completion) {
  if (layers == nullptr) {
    return;
  }
  for (auto index = 0; index < layers->size(); ++index) {
    auto layer = layers->at(index).toMap();
    mark_contained_failure(&layer, completion);
    (*layers)[index] = layer;
  }
}

bool completion_matches(
    const ThumbnailPresentationFacts& facts,
    const ThumbnailRefreshQueueCompletion& completion) {
  return !completion.job_id.isEmpty() &&
         completion.state_signature == facts.state_signature &&
         completion.state_revision == facts.mask_revision;
}

QString retouch_mode_display_name(const QString& mode_name) {
  const auto mode = mode_name.trimmed().toLower();
  if (mode == QStringLiteral("remove")) return QStringLiteral("Remove");
  if (mode == QStringLiteral("heal")) return QStringLiteral("Heal");
  if (mode == QStringLiteral("patch")) return QStringLiteral("Patch");
  if (mode == QStringLiteral("clone")) return QStringLiteral("Clone");
  if (mode == QStringLiteral("dust")) return QStringLiteral("Dust");
  if (mode == QStringLiteral("red-eye")) return QStringLiteral("Red Eye");
  if (mode == QStringLiteral("skin-cleanup")) return QStringLiteral("Skin Cleanup");
  if (mode == QStringLiteral("frequency-smoothing")) {
    return QStringLiteral("Frequency Smoothing");
  }
  return QStringLiteral("Retouch");
}

}  // namespace

QVariantList ThumbnailPresentationService::groupMaskSets(
    const QVariantList& masks,
    bool masks_are_canonical) const {
  QVariantList normalized_masks;
  normalized_masks.reserve(masks.size());
  auto normalize_ordinal = 1;
  for (const auto& value : masks) {
    const auto candidate = value.toMap();
    normalized_masks.append(
        masks_are_canonical
            ? candidate
            : normalized_mask_definition(candidate, normalize_ordinal));
    ++normalize_ordinal;
  }

  QVariantList grouped;
  QHash<QString, int> indices_by_id;
  QHash<QString, QString> mask_names_by_id;
  for (const auto& value : normalized_masks) {
    const auto mask = value.toMap();
    const auto id = mask.value(QStringLiteral("id")).toString();
    if (!id.isEmpty()) {
      mask_names_by_id.insert(id, mask.value(QStringLiteral("name")).toString());
    }
  }

  auto ordinal = 1;
  for (const auto& value : normalized_masks) {
    const auto mask = value.toMap();
    ++ordinal;
    if (!is_selection_mask(mask)) {
      continue;
    }

    const auto set_id = mask.value(QStringLiteral("maskSetId")).toString();
    auto set_index = indices_by_id.value(set_id, -1);
    if (set_index < 0) {
      QVariantMap set;
      set.insert(QStringLiteral("id"), set_id);
      set.insert(QStringLiteral("name"), mask.value(QStringLiteral("maskSetName")).toString());
      set.insert(QStringLiteral("enabled"), mask.value(QStringLiteral("enabled")).toBool());
      set.insert(QStringLiteral("exposure"), mask.value(QStringLiteral("exposure")).toDouble());
      set.insert(
          QStringLiteral("temperature"),
          mask.value(QStringLiteral("temperature")).toDouble());
      set.insert(QStringLiteral("hueShift"), mask.value(QStringLiteral("hueShift")).toDouble());
      set.insert(
          QStringLiteral("saturationShift"),
          mask.value(QStringLiteral("saturationShift")).toDouble());
      set.insert(
          QStringLiteral("colorizeHue"),
          mask.value(QStringLiteral("colorizeHue")).toDouble());
      set.insert(
          QStringLiteral("colorizeStrength"),
          mask.value(QStringLiteral("colorizeStrength")).toDouble());
      set.insert(QStringLiteral("sources"), QVariantList{});
      set.insert(
          QStringLiteral("maskSetPreviewBypassed"),
          mask.value(QStringLiteral("maskSetPreviewBypassed")).toBool());
      grouped.append(set);
      set_index = static_cast<int>(grouped.size()) - 1;
      indices_by_id.insert(set_id, set_index);
    }

    auto set = grouped.at(set_index).toMap();
    auto sources = set.value(QStringLiteral("sources")).toList();
    auto source = mask;
    source.insert(QStringLiteral("flatIndex"), ordinal - 1);
    const auto alpha_availability = mask_alpha_availability_fields(
        source, raster_size_for_mask_trace(source));
    for (auto iterator = alpha_availability.constBegin();
         iterator != alpha_availability.constEnd();
         ++iterator) {
      source.insert(iterator.key(), iterator.value());
    }
    source.insert(QStringLiteral("mask_alpha_trace_deferred"), true);

    QStringList dependency_parts;
    const auto parent_id = source.value(QStringLiteral("parentId")).toString();
    if (!parent_id.isEmpty()) {
      dependency_parts.push_back(
          QStringLiteral("Depends on %1").arg(mask_names_by_id.value(parent_id, parent_id)));
    }
    if (source.value(QStringLiteral("reusableOutput")).toBool()) {
      const auto output_id = source.value(QStringLiteral("outputId")).toString();
      dependency_parts.push_back(
          output_id.isEmpty() ? QStringLiteral("Reusable output")
                              : QStringLiteral("Reusable output: %1").arg(output_id));
    }
    const auto generated_metadata =
        source.value(QStringLiteral("generatedMaskMetadata")).toMap();
    if (!generated_metadata.isEmpty()) {
      const auto generated_dependencies =
          generated_metadata.value(QStringLiteral("dependencies")).toMap();
      const auto source_resolution =
          generated_dependencies
              .value(
                  QStringLiteral("sourceDimensions"),
                  generated_metadata.value(QStringLiteral("sourceResolution")))
              .toString();
      const auto model_name =
          generated_metadata
              .value(
                  QStringLiteral("modelName"),
                  generated_metadata.value(QStringLiteral("runtime")))
              .toString();
      const auto prompt_type =
          generated_metadata.value(QStringLiteral("promptType")).toString();
      dependency_parts.push_back(
          QStringLiteral("Generated: %1 · %2 · %3")
              .arg(model_name, prompt_type, source_resolution));
      if (source.value(QStringLiteral("generatedMaskStale")).toBool()) {
        dependency_parts.push_back(QStringLiteral("Stale generated selection"));
      }
    }
    source.insert(
        QStringLiteral("dependencyLabel"),
        dependency_parts.join(QStringLiteral(" · ")));

    const auto source_badge = mask_source_badge_label(
        source.value(QStringLiteral("kind")).toString());
    const auto operation_badge = mask_operation_badge_label(
        source.value(QStringLiteral("operation")).toString());
    source.insert(QStringLiteral("sourceBadge"), source_badge);
    source.insert(QStringLiteral("sourceKindLabel"), source_badge);
    source.insert(QStringLiteral("operationBadge"), operation_badge);
    source.insert(QStringLiteral("operationLabel"), operation_badge);
    QStringList source_audit_parts{
        source_badge,
        operation_badge,
        source.value(QStringLiteral("enabled")).toBool()
            ? QStringLiteral("visible")
            : QStringLiteral("hidden"),
    };
    if (source.value(QStringLiteral("solo")).toBool()) {
      source_audit_parts.push_back(QStringLiteral("solo"));
    }
    if (source.value(QStringLiteral("locked")).toBool()) {
      source_audit_parts.push_back(QStringLiteral("locked"));
    }
    if (source.value(QStringLiteral("invert")).toBool() ||
        source.value(QStringLiteral("selectionInvert")).toBool()) {
      source_audit_parts.push_back(QStringLiteral("inverted"));
    }
    if (selection_mask_is_empty_or_invalid(source, ordinal - 1)) {
      source_audit_parts.push_back(QStringLiteral("invalid"));
    }
    if (source.value(QStringLiteral("generatedMaskStale")).toBool()) {
      source_audit_parts.push_back(QStringLiteral("stale"));
    }
    if (source.value(QStringLiteral("brushRefineMode")).toBool()) {
      source_audit_parts.push_back(QStringLiteral("brush refine"));
    }
    if (source.value(QStringLiteral("refineBrushRecalculate")).toBool()) {
      source_audit_parts.push_back(QStringLiteral("edge recalculation"));
    }
    if (source.value(QStringLiteral("maskSetPreviewBypassed")).toBool()) {
      source_audit_parts.push_back(QStringLiteral("preview: before"));
    }
    source.insert(
        QStringLiteral("sourceAuditLabel"),
        source_audit_parts.join(QStringLiteral(" · ")));
    sources.append(source);
    set.insert(QStringLiteral("sources"), sources);
    set.insert(QStringLiteral("sourceCount"), sources.size());

    auto any_source_enabled = false;
    auto any_source_solo = false;
    auto all_sources_preview_bypassed = !sources.isEmpty();
    QStringList combine_modes;
    auto any_source_inverted = false;
    for (const auto& source_value : sources) {
      const auto source_map = source_value.toMap();
      any_source_enabled =
          any_source_enabled ||
          (source_map.contains(QStringLiteral("enabled"))
               ? source_map.value(QStringLiteral("enabled")).toBool()
               : true);
      any_source_solo =
          any_source_solo || source_map.value(QStringLiteral("solo")).toBool();
      all_sources_preview_bypassed =
          all_sources_preview_bypassed &&
          source_map.value(QStringLiteral("maskSetPreviewBypassed")).toBool();
      const auto combine_mode = normalized_mask_operation(
          source_map.value(QStringLiteral("operation")).toString());
      if (!combine_modes.contains(combine_mode)) {
        combine_modes.push_back(combine_mode);
      }
      any_source_inverted =
          any_source_inverted || source_map.value(QStringLiteral("invert")).toBool() ||
          source_map.value(QStringLiteral("selectionInvert")).toBool();
    }

    const auto set_inverted =
        set.value(QStringLiteral("maskSetSelectionInvert")).toBool();
    QStringList combine_mode_labels;
    QVariantList combine_mode_values;
    combine_mode_labels.reserve(combine_modes.size());
    combine_mode_values.reserve(combine_modes.size());
    for (const auto& combine_mode : combine_modes) {
      combine_mode_labels.push_back(mask_operation_badge_label(combine_mode));
      combine_mode_values.append(combine_mode);
    }
    const auto first_source =
        sources.isEmpty() ? QVariantMap{} : sources.constFirst().toMap();
    const auto primary_kind =
        sources.size() == 1
            ? normalized_mask_kind(
                  first_source.value(QStringLiteral("kind")).toString())
            : QStringLiteral("composite");
    const auto primary_label =
        sources.size() == 1
            ? first_source.value(QStringLiteral("sourceKindLabel")).toString()
            : QStringLiteral("Composite");
    set.insert(QStringLiteral("primarySourceKind"), primary_kind);
    set.insert(QStringLiteral("sourceKind"), primary_kind);
    set.insert(QStringLiteral("sourceKindLabel"), primary_label);
    set.insert(QStringLiteral("combineModes"), combine_mode_values);
    set.insert(
        QStringLiteral("combineModeSummary"),
        combine_modes.isEmpty()
            ? QStringLiteral("Add")
            : combine_mode_labels.join(QStringLiteral(" + ")) +
                  (any_source_inverted || set_inverted
                       ? QStringLiteral(" · Inverted")
                       : QString()));
    set.insert(
        QStringLiteral("combineModeContract"),
        QStringLiteral("mask-combine:v1|sources:%1|modes:%2|inverted:%3")
            .arg(sources.size())
            .arg(combine_modes.join(QChar(',')))
            .arg(any_source_inverted || set_inverted ? QStringLiteral("true")
                                                     : QStringLiteral("false")));
    set.insert(QStringLiteral("enabled"), any_source_enabled);
    set.insert(QStringLiteral("solo"), any_source_solo);
    set.insert(QStringLiteral("maskSetPreviewBypassed"), all_sources_preview_bypassed);
    grouped[set_index] = set;
  }
  return grouped;
}

QVariantList ThumbnailPresentationService::groupRetouchStack(
    const QVariantList& masks,
    bool masks_are_canonical) const {
  QVariantList grouped;
  auto ordinal = 1;
  for (const auto& value : masks) {
    const auto candidate = value.toMap();
    if (normalized_mask_tool_role(
            candidate.value(QStringLiteral("toolRole")).toString()) !=
        QStringLiteral("retouch")) {
      ++ordinal;
      continue;
    }
    auto mask = masks_are_canonical
                    ? candidate
                    : normalized_mask_definition(candidate, ordinal);
    const auto point_count =
        mask.value(QStringLiteral("brushPoints")).toList().size();
    const auto committed = mask.value(QStringLiteral("committed")).toBool();
    const auto mode_label = retouch_mode_display_name(
        mask.value(QStringLiteral("retouchMode")).toString());
    QStringList audit_parts{
        committed ? QStringLiteral("applied") : QStringLiteral("staged")};
    if (!mask.value(QStringLiteral("enabled")).toBool()) {
      audit_parts.push_back(QStringLiteral("off"));
    }
    audit_parts.push_back(
        point_count > 0 ? QStringLiteral("%1 pts").arg(point_count)
                        : QStringLiteral("no strokes"));
    const auto candidate_count =
        mask.value(QStringLiteral("inpaintCandidateCount")).toInt();
    if (candidate_count > 0) {
      audit_parts.push_back(
          QStringLiteral("%1 candidates").arg(candidate_count));
    }
    if (mask.value(QStringLiteral("inpaintFallback")).toBool()) {
      audit_parts.push_back(QStringLiteral("fallback"));
    }
    const auto retouch_order = std::max(
        1, map_int_value_or(mask, QStringLiteral("retouchOrder"), ordinal));
    mask.insert(QStringLiteral("flatIndex"), ordinal - 1);
    mask.insert(QStringLiteral("retouchOrder"), retouch_order);
    mask.insert(
        QStringLiteral("retouchOrderLabel"),
        QStringLiteral("#%1").arg(retouch_order));
    mask.insert(QStringLiteral("retouchModeLabel"), mode_label);
    mask.insert(
        QStringLiteral("retouchStatus"),
        committed ? QStringLiteral("applied") : QStringLiteral("staged"));
    mask.insert(QStringLiteral("retouchPointCount"), point_count);
    mask.insert(
        QStringLiteral("retouchAuditLabel"),
        QStringLiteral("#%1 · %2 · ").arg(retouch_order).arg(mode_label) +
            audit_parts.join(QStringLiteral(" · ")));
    grouped.append(mask);
    ++ordinal;
  }
  return grouped;
}

void ThumbnailPresentationService::copyThumbnailFields(
    const QVariantMap& source,
    QVariantMap* target) {
  if (target == nullptr) {
    return;
  }
  for (auto iterator = source.constBegin(); iterator != source.constEnd(); ++iterator) {
    if (thumbnail_field(iterator.key())) {
      target->insert(iterator.key(), iterator.value());
    }
  }
}

bool ThumbnailPresentationService::insertProgressiveFields(
    QVariantMap* target,
    const QString& reason,
    const QString& current_recipe_hash,
    const QVariantMap& queue_fields,
    bool log_stale_warning,
    const ThumbnailPresentationDependencies& dependencies) {
  if (target == nullptr) {
    return false;
  }
  const auto reused_last_good =
      !target->value(QStringLiteral("thumbnailSource")).toString().trimmed().isEmpty();
  const auto previous_recipe_hash =
      target->value(QStringLiteral("thumbnailInvalidationRecipeHash")).toString().trimmed();
  const auto previous_invalidation_key =
      target->value(QStringLiteral("thumbnailInvalidationKey")).toString().trimmed();
  const auto normalized_recipe_hash = current_recipe_hash.trimmed();
  const auto recipe_mismatch = reused_last_good && !previous_recipe_hash.isEmpty() &&
                               !normalized_recipe_hash.isEmpty() &&
                               previous_recipe_hash != normalized_recipe_hash;
  target->insert(QStringLiteral("thumbnailReady"), reused_last_good);
  target->insert(QStringLiteral("thumbnailStale"), reused_last_good);
  target->insert(QStringLiteral("thumbnailProgressive"), reused_last_good);
  target->insert(QStringLiteral("thumbnailProgressiveReason"), reason);
  target->insert(
      QStringLiteral("thumbnailProgressivePreviousInvalidationKey"),
      previous_invalidation_key);
  target->insert(
      QStringLiteral("thumbnailProgressiveSignature"),
      QStringLiteral("progressive:%1|reason:%2|previous:%3")
          .arg(reused_last_good ? QStringLiteral("true") : QStringLiteral("false"),
               reason,
               previous_invalidation_key));
  target->insert(QStringLiteral("thumbnailStaleDetected"), reused_last_good);
  target->insert(QStringLiteral("thumbnailStaleMismatch"), recipe_mismatch);
  target->insert(QStringLiteral("thumbnailStaleDetectionReason"), reason);
  target->insert(QStringLiteral("thumbnailStalePreviousRecipeHash"), previous_recipe_hash);
  target->insert(QStringLiteral("thumbnailStaleCurrentRecipeHash"), normalized_recipe_hash);
  target->insert(
      QStringLiteral("thumbnailStalePreviousInvalidationKey"),
      previous_invalidation_key);
  target->insert(
      QStringLiteral("thumbnailStaleDetectionSignature"),
      QStringLiteral("stale:%1|mismatch:%2|reason:%3|previous-recipe:%4|current-recipe:%5|previous-key:%6")
          .arg(reused_last_good ? QStringLiteral("true") : QStringLiteral("false"),
               recipe_mismatch ? QStringLiteral("true") : QStringLiteral("false"),
               reason,
               previous_recipe_hash,
               normalized_recipe_hash,
               previous_invalidation_key));
  for (auto iterator = queue_fields.constBegin(); iterator != queue_fields.constEnd(); ++iterator) {
    target->insert(iterator.key(), iterator.value());
  }
  auto logged = false;
  if (recipe_mismatch && log_stale_warning && dependencies.log_stale_warning) {
    dependencies.log_stale_warning(
        reason,
        QVariantMap{
            {QStringLiteral("previous_recipe_hash"), previous_recipe_hash},
            {QStringLiteral("current_recipe_hash"), normalized_recipe_hash},
            {QStringLiteral("previous_invalidation_key"), previous_invalidation_key},
        });
    logged = true;
  }
  const auto signature = target->value(QStringLiteral("thumbnailSignature")).toString().trimmed();
  if (!signature.isEmpty()) {
    const auto suffix = signature.indexOf(QStringLiteral("|progressive:"));
    target->insert(
        QStringLiteral("thumbnailSignature"),
        QStringLiteral("%1|progressive:%2|reason:%3")
            .arg(suffix >= 0 ? signature.left(suffix) : signature,
                 reused_last_good ? QStringLiteral("true") : QStringLiteral("false"),
                 reason));
  }
  return logged;
}

QVariantList ThumbnailPresentationService::reuseMaskThumbnails(
    const QVariantList& current_sets,
    const QVariantList& cached_sets,
    const QString& reason,
    const ThumbnailPresentationFacts& facts,
    const ThumbnailPresentationDependencies& dependencies,
    bool log_stale_warning) {
  QHash<QString, QVariantMap> sets_by_id;
  QHash<QString, QVariantMap> sources_by_id;
  for (auto set_index = 0; set_index < cached_sets.size(); ++set_index) {
    const auto set = cached_sets.at(set_index).toMap();
    sets_by_id.insert(mask_identity(set, QStringLiteral("set:%1").arg(set_index)), set);
    const auto sources = set.value(QStringLiteral("sources")).toList();
    for (auto source_index = 0; source_index < sources.size(); ++source_index) {
      const auto source = sources.at(source_index).toMap();
      sources_by_id.insert(
          mask_identity(
              source,
              QStringLiteral("set:%1/source:%2").arg(set_index).arg(source_index)),
          source);
    }
  }
  QVariantList result;
  auto log_next = log_stale_warning;
  for (auto set_index = 0; set_index < current_sets.size(); ++set_index) {
    auto set = current_sets.at(set_index).toMap();
    copyThumbnailFields(
        sets_by_id.value(mask_identity(set, QStringLiteral("set:%1").arg(set_index))),
        &set);
    if (!reason.isEmpty() && insertProgressiveFields(
                                 &set,
                                 reason,
                                 facts.state_signature,
                                 facts.queue_fields,
                                 log_next,
                                 dependencies)) {
      log_next = false;
    }
    auto sources = set.value(QStringLiteral("sources")).toList();
    for (auto source_index = 0; source_index < sources.size(); ++source_index) {
      auto source = sources.at(source_index).toMap();
      copyThumbnailFields(
          sources_by_id.value(mask_identity(
              source,
              QStringLiteral("set:%1/source:%2").arg(set_index).arg(source_index))),
          &source);
      if (!reason.isEmpty() && insertProgressiveFields(
                                   &source,
                                   reason,
                                   facts.state_signature,
                                   facts.queue_fields,
                                   log_next,
                                   dependencies)) {
        log_next = false;
      }
      sources[source_index] = source;
    }
    set.insert(QStringLiteral("sources"), sources);
    result.append(set);
  }
  return result;
}

QVariantList ThumbnailPresentationService::reusePhotoLayerThumbnails(
    const QVariantList& current_layers,
    const QVariantList& cached_layers,
    const QString& reason,
    const ThumbnailPresentationFacts& facts,
    const ThumbnailPresentationDependencies& dependencies,
    bool log_stale_warning) {
  QHash<QString, QVariantMap> layers_by_id;
  for (const auto& value : cached_layers) {
    const auto layer = value.toMap();
    layers_by_id.insert(layer.value(QStringLiteral("id")).toString(), layer);
  }
  QVariantList result;
  auto log_next = log_stale_warning;
  for (const auto& value : current_layers) {
    auto layer = value.toMap();
    copyThumbnailFields(
        layers_by_id.value(layer.value(QStringLiteral("id")).toString()),
        &layer);
    const auto recipe_hash = photo_layer_thumbnail_recipe_hash(layer, facts.photo_layer_revision);
    if (!reason.isEmpty() && insertProgressiveFields(
                                 &layer,
                                 reason,
                                 recipe_hash,
                                 facts.queue_fields,
                                 log_next,
                                 dependencies)) {
      log_next = false;
    }
    result.append(layer);
  }
  return result;
}

QVariantList ThumbnailPresentationService::presentMaskSets(
    const QVariantList& current_sets,
    const ThumbnailPresentationFacts& facts,
    const ThumbnailPresentationDependencies& dependencies,
    ThumbnailCacheSession* session) const {
  if (session == nullptr || current_sets.isEmpty() || !facts.has_asset) {
    return current_sets;
  }
  if (!session->maskDirty()) {
    ++session->maskHitCount();
    return current_sets;
  }
  auto result = current_sets;
  auto all_ready = true;
  for (auto set_index = 0; set_index < result.size(); ++set_index) {
    auto set = result.at(set_index).toMap();
    auto sources = set.value(QStringLiteral("sources")).toList();
    for (auto source_index = 0; source_index < sources.size(); ++source_index) {
      auto source = sources.at(source_index).toMap();
      const auto mask_id = source.value(
          QStringLiteral("mask_source_id"), source.value(QStringLiteral("id"))).toString();
      const auto key = thumbnail_entry_cache_key(
          facts.asset_id,
          facts.state_signature,
          QString(),
          mask_id,
          kMaskThumbnailBackingEdge,
          kMaskThumbnailBackingEdge,
          static_cast<double>(kMaskThumbnailBackingEdge) /
              static_cast<double>(kMaskSetThumbnailDisplayEdge),
          QString::fromLatin1(kThumbnailColorPipelineVersion),
          facts.mask_revision);
      QVariantMap cached;
      if (session->entryLookup(key, &cached)) {
        copyThumbnailFields(cached, &source);
        mark_entry_cache_hit(&source);
        insert_thumbnail_queue_completion_fields(&source, facts.queue_completion);
      } else {
        all_ready = false;
      }
      sources[source_index] = source;
    }
    set.insert(QStringLiteral("sources"), sources);
    if (!sources.isEmpty()) {
      const auto set_id = set.value(
          QStringLiteral("mask_set_id"), set.value(QStringLiteral("id"))).toString();
      const auto key = thumbnail_entry_cache_key(
          facts.asset_id,
          facts.state_signature,
          QString(),
          set_id,
          kMaskThumbnailBackingEdge,
          kMaskThumbnailBackingEdge,
          static_cast<double>(kMaskThumbnailBackingEdge) /
              static_cast<double>(kMaskSetThumbnailDisplayEdge),
          QString::fromLatin1(kThumbnailColorPipelineVersion),
          facts.mask_revision);
      QVariantMap cached;
      if (session->entryLookup(key, &cached)) {
        copyThumbnailFields(cached, &set);
        set.insert(QStringLiteral("sources"), sources);
        mark_entry_cache_hit(&set);
        insert_thumbnail_queue_completion_fields(&set, facts.queue_completion);
      } else {
        all_ready = false;
      }
    }
    result[set_index] = set;
  }
  if (all_ready) {
    session->maskSetsWithThumbnails() = result;
    session->maskSetsDeferredKey().clear();
    session->maskDirty() = false;
    session->maskDeferCounted() = false;
    ++session->maskRefreshCount();
    return result;
  }

  const auto reason = QStringLiteral("mask_thumbnail_refresh_deferred");
  auto progressive_facts = facts;
  const auto completed_failure =
      completion_matches(facts, facts.queue_completion) &&
      facts.queue_completion.mask_thumbnail_failed;
  if (!completed_failure && dependencies.schedule_refresh) {
    progressive_facts.queue_fields = dependencies.schedule_refresh(reason);
  }
  auto log_warning = dependencies.consume_stale_warning
                         ? dependencies.consume_stale_warning(reason)
                         : false;
  result = reuseMaskThumbnails(
      current_sets,
      session->maskSetsWithThumbnails(),
      reason,
      progressive_facts,
      dependencies,
      log_warning);
  if (completed_failure) {
    mark_mask_failure(&result, facts.queue_completion);
    if (log_warning && dependencies.log_contained_failure) {
      dependencies.log_contained_failure(
          QStringLiteral("mask_thumbnail"), facts.queue_completion);
    }
  }
  session->maskSetsWithThumbnails() = result;
  session->maskSetsDeferredKey() = QStringLiteral("%1|%2|mask-revision:%3")
                                       .arg(facts.asset_id,
                                            facts.file_path,
                                            QString::number(facts.mask_revision));
  if (!session->maskDeferCounted()) {
    ++session->maskDeferredCount();
    session->maskDeferCounted() = true;
  }
  return result;
}

QVariantList ThumbnailPresentationService::presentPhotoLayers(
    const QVariantList& current_layers,
    const ThumbnailPresentationFacts& facts,
    const ThumbnailPresentationDependencies& dependencies,
    ThumbnailCacheSession* session) const {
  if (session == nullptr || current_layers.isEmpty() || !facts.has_asset) {
    return current_layers;
  }
  const auto asset_key = QStringLiteral("%1|%2").arg(facts.asset_id, facts.file_path);
  if (!session->photoLayerDirty() &&
      session->photoLayerAssetKey() == asset_key) {
    auto cached = reusePhotoLayerThumbnails(
        current_layers,
        session->photoLayersWithThumbnails(),
        QString(),
        facts,
        dependencies,
        false);
    session->photoLayersWithThumbnails() = cached;
    ++session->photoLayerHitCount();
    return cached;
  }
  auto result = current_layers;
  auto all_ready = true;
  for (auto index = 0; index < result.size(); ++index) {
    auto layer = result.at(index).toMap();
    const auto recipe_hash = photo_layer_thumbnail_entry_recipe_hash(layer, facts.photo_layer_revision);
    const auto mask_id = selection_mask_set_identity_signature(layer);
    const auto key = thumbnail_entry_cache_key(
        facts.asset_id,
        recipe_hash,
        layer.value(QStringLiteral("id")).toString(),
        mask_id,
        kLayerThumbnailEdge,
        kLayerThumbnailEdge,
        kLayerThumbnailDevicePixelRatio,
        QString::fromLatin1(kThumbnailColorPipelineVersion),
        facts.mask_revision);
    QVariantMap cached;
    if (session->entryLookup(key, &cached)) {
      copyThumbnailFields(cached, &layer);
      mark_entry_cache_hit(&layer);
      insert_thumbnail_queue_completion_fields(&layer, facts.queue_completion);
    } else {
      all_ready = false;
    }
    result[index] = layer;
  }
  if (all_ready) {
    session->photoLayersWithThumbnails() = result;
    session->photoLayerAssetKey() = asset_key;
    session->photoLayerDirty() = false;
    session->photoLayerDeferCounted() = false;
    ++session->photoLayerRefreshCount();
    return result;
  }

  const auto reason = QStringLiteral("photo_layer_thumbnail_refresh_deferred");
  auto progressive_facts = facts;
  const auto completed_failure =
      completion_matches(facts, facts.queue_completion) &&
      facts.queue_completion.photo_layer_thumbnail_failed;
  if (!completed_failure && dependencies.schedule_refresh) {
    progressive_facts.queue_fields = dependencies.schedule_refresh(reason);
  }
  const auto log_warning = dependencies.consume_stale_warning
                               ? dependencies.consume_stale_warning(reason)
                               : false;
  result = reusePhotoLayerThumbnails(
      current_layers,
      session->photoLayersWithThumbnails(),
      reason,
      progressive_facts,
      dependencies,
      log_warning);
  if (completed_failure) {
    mark_layer_failure(&result, facts.queue_completion);
    if (log_warning && dependencies.log_contained_failure) {
      dependencies.log_contained_failure(
          QStringLiteral("photo_layer_thumbnail"), facts.queue_completion);
    }
  }
  session->photoLayersWithThumbnails() = result;
  session->photoLayerAssetKey() = QStringLiteral("%1|deferred|revision:%2|mask:%3")
                                      .arg(asset_key,
                                           QString::number(session->photoLayerRevision()),
                                           QString::number(facts.mask_revision));
  if (!session->photoLayerDeferCounted()) {
    ++session->photoLayerDeferredCount();
    session->photoLayerDeferCounted() = true;
  }
  return result;
}

}  // namespace photo_editor::desktop
