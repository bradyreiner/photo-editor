#pragma once

#include <algorithm>

#include <QHash>
#include <QSet>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include "photo_layer_effect_codec.h"
#include "edit_recipe_state.h"
#include "photo_layer_edit_access.h"
#include "photo_editor/pipeline/layer_styles.h"

namespace photo_editor::desktop {

inline qint64 selection_history_value_bytes(const QVariant& value) {
  if (value.metaType().id() == QMetaType::QByteArray) return value.toByteArray().size();
  if (value.metaType().id() == QMetaType::QString) return value.toString().size() * 2;
  qint64 bytes = 32;
  if (value.metaType().id() == QMetaType::QVariantMap) {
    const auto map = value.toMap();
    for (auto it = map.cbegin(); it != map.cend(); ++it) bytes += 64 + it.key().size() * 2 + selection_history_value_bytes(it.value());
  } else if (value.metaType().id() == QMetaType::QVariantList) {
    for (const auto& child : value.toList()) bytes += selection_history_value_bytes(child);
  }
  return bytes;
}

// Missing legacy repair ownership is deliberately scoped to one stable source,
// never to all pending repairs which happen to use the same tool.
inline QString retouch_repair_id(const QVariantMap& mask) {
  const auto explicit_id = mask.value(QStringLiteral("repairId")).toString().trimmed();
  return explicit_id.isEmpty() ? mask.value(QStringLiteral("id")).toString().trimmed() : explicit_id;
}

inline bool same_retouch_repair(const QVariantMap& mask, const QVariantMap& owner) {
  return mask.value(QStringLiteral("toolRole")).toString() == QStringLiteral("retouch") &&
      !retouch_repair_id(owner).isEmpty() && retouch_repair_id(mask) == retouch_repair_id(owner);
}

inline bool retouch_runtime_fallback_requires_retry(const QString& mode, bool fallback, const QString& detail) {
  // A deliberate local blemish algorithm has no runtime error. A failed LaMa
  // invocation must not replace a valid accepted object repair with a fallback.
  return mode.trimmed().toLower() == QStringLiteral("remove") && fallback && !detail.trimmed().isEmpty();
}

inline QVariantMap retouch_mask_with_composed_region(QVariantMap mask, const QByteArray& data, int width, int height) {
  mask.insert(QStringLiteral("operation"), QStringLiteral("add"));
  mask.insert(QStringLiteral("coordinateSpace"), QStringLiteral("source"));
  mask.remove(QStringLiteral("coordinateArea"));
  mask.remove(QStringLiteral("brushCompositeSource"));
  mask.insert(QStringLiteral("segmentationData"), data);
  mask.insert(QStringLiteral("segmentationWidth"), width);
  mask.insert(QStringLiteral("segmentationHeight"), height);
  mask.insert(QStringLiteral("brushPoints"), QVariantList{});
  mask.insert(QStringLiteral("brushStrokes"), QVariantList{});
  const auto keys = mask.keys();
  for (const auto& key : keys)
    if (key.startsWith(QStringLiteral("brushAlpha")) || key.startsWith(QStringLiteral("raster"))) mask.remove(key);
  return mask;
}

inline QVariantMap accepted_retouch_mask(const QVariantMap& mask) {
  if (mask.value(QStringLiteral("inpaintPatchData")).toByteArray().isEmpty()) return {};
  const auto data = mask.value(QStringLiteral("inpaintSourceMaskData")).toByteArray();
  const auto width = mask.value(QStringLiteral("inpaintSourceMaskWidth")).toInt();
  const auto height = mask.value(QStringLiteral("inpaintSourceMaskHeight")).toInt();
  if (width <= 0 || height <= 0 || data.size() != qint64(width) * height) return {};
  auto accepted = retouch_mask_with_composed_region(mask, data, width, height);
  accepted.insert(QStringLiteral("committed"), true);
  return accepted;
}

inline bool retouch_patch_requires_final(const QVariantMap& mask) {
  const auto role = mask.value(QStringLiteral("toolRole")).toString().trimmed().toLower();
  // Legacy patch payloads may precede role/committed/identity metadata. An
  // explicit non-retouch role is excluded; missing metadata cannot waive finalization.
  return (role.isEmpty() || role == QStringLiteral("retouch")) &&
      !mask.value(QStringLiteral("inpaintPatchData")).toByteArray().isEmpty() &&
      (mask.value(QStringLiteral("inpaintFinalRequired")).toBool() ||
       mask.value(QStringLiteral("inpaintQualityMode")).toString().trimmed().toLower() == QStringLiteral("preview"));
}

inline QString unfinished_inpaint_repair_id(const QVariantList& masks) {
  for (const auto& value : masks) {
    const auto mask = value.toMap();
    if (retouch_patch_requires_final(mask)) {
      const auto id = retouch_repair_id(mask);
      return id.isEmpty() ? QStringLiteral("(unnamed repair)") : id;
    }
  }
  return {};
}

inline QStringList mask_owner_references(const QVariantMap& layer, const QString& plural, const QString& singular) {
  QStringList result;
  for (const auto& value : layer.value(plural).toList()) {
    const auto id = value.toString().trimmed();
    if (!id.isEmpty() && !result.contains(id)) result.append(id);
  }
  const auto id = layer.value(singular).toString().trimmed();
  if (!id.isEmpty() && !result.contains(id)) result.append(id);
  return result;
}

struct MaskOwnerSelectionReferences {
  QStringList ids;
  QStringList legacy_names;
};

inline MaskOwnerSelectionReferences mask_owner_selection_references(const QVariantMap& layer) {
  MaskOwnerSelectionReferences actual;
  const auto primary_ids = mask_owner_references(layer, QStringLiteral("selectionMaskSetIds"), QStringLiteral("selectionMaskSetId"));
  const auto primary_names = mask_owner_references(layer, QStringLiteral("selectionMaskSetNames"), QStringLiteral("selectionMaskSetName"));
  for (qsizetype i = 0; i < std::max(primary_ids.size(), primary_names.size()); ++i) {
    if (i < primary_ids.size()) actual.ids.append(primary_ids[i]);
    else actual.legacy_names.append(primary_names[i]);
  }
  // Intersections are independent renderer consumers. An ID in the primary
  // selection must not hide an intersection's legacy name fallback.
  const auto intersection_ids = layer.value(QStringLiteral("selectionIntersectionMaskSetIds")).toStringList();
  const auto intersection_names = layer.value(QStringLiteral("selectionIntersectionMaskSetNames")).toStringList();
  for (qsizetype i = 0; i < std::max(intersection_ids.size(), intersection_names.size()); ++i) {
    const auto id = i < intersection_ids.size() ? intersection_ids[i].trimmed() : QString();
    const auto name = i < intersection_names.size() ? intersection_names[i].trimmed() : QString();
    if (!id.isEmpty()) actual.ids.append(id);
    else if (!name.isEmpty()) actual.legacy_names.append(name);
  }
  return actual;
}

inline bool mask_owner_contains_reference(const QVariant& value, const QSet<QString>& references) {
  const auto actual = mask_owner_selection_references(value.toMap());
  // User copy, labels and arbitrary metadata are never geometry references.
  for (const auto& reference : actual.ids + actual.legacy_names)
    if (references.contains(reference)) return true;
  return false;
}

// Effect and layer-style masks use the same desktop mask-set IDs, but layer
// styles are serialized by the engine's string codec rather than JSON. Keep
// parsing at the ownership boundary so callers never inspect a string as a
// QVariant map. Unknown or malformed stacks make ownership uncertain.
inline bool collect_layer_operation_mask_references(
    const QVariantMap& layer, QSet<QString>* references, int depth = 0) {
  if (references == nullptr || depth > 64) return false;
  if (layer.contains(QStringLiteral("effectStack"))) {
    const auto effects = decode_photo_layer_effects(layer);
    if (!effects) return false;
    for (const auto& effect : *effects) {
      const auto reference = QString::fromUtf8(
          effect.mask_ref.data(), static_cast<qsizetype>(effect.mask_ref.size())).trimmed();
      if (!reference.isEmpty()) references->insert(reference);
    }
  }
  if (layer.contains(QStringLiteral("layerStyleStack"))) {
    const auto encoded = layer.value(QStringLiteral("layerStyleStack"));
    if (encoded.metaType().id() != QMetaType::QString) return false;
    std::string detail;
    const auto stack = photo_editor::pipeline::deserialize_layer_style_stack(
        encoded.toString().toStdString(), &detail);
    if (!stack) return false;
    for (const auto& operation : stack->operations) {
      const auto reference = QString::fromUtf8(
          operation.mask_ref.data(),
          static_cast<qsizetype>(operation.mask_ref.size())).trimmed();
      if (!reference.isEmpty()) references->insert(reference);
    }
  }
  for (const auto* key : {"children", "layers"}) {
    const auto nested = layer.value(QString::fromLatin1(key));
    if (!layer.contains(QString::fromLatin1(key))) continue;
    if (nested.metaType().id() != QMetaType::QVariantList) return false;
    for (const auto& value : nested.toList()) {
      if (value.metaType().id() != QMetaType::QVariantMap ||
          !collect_layer_operation_mask_references(
              value.toMap(), references, depth + 1))
        return false;
    }
  }
  const auto document = layer.value(QStringLiteral("smartObjectDocument"));
  if (layer.contains(QStringLiteral("smartObjectDocument"))) {
    if (!document.canConvert<QVariantMap>()) return false;
    const auto doc_layers = document.toMap().value(QStringLiteral("layers"));
    if (doc_layers.metaType().id() != QMetaType::QVariantList) return false;
    for (const auto& value : doc_layers.toList()) {
      if (value.metaType().id() != QMetaType::QVariantMap ||
          !collect_layer_operation_mask_references(
              value.toMap(), references, depth + 1))
        return false;
    }
  }
  return true;
}

// Inspection/painting follows direct consumers on this layer. Operation masks
// stay separate from the layer's coverage masks and from masks inside a component.
inline bool layer_references_editable_mask_set(
    const QVariantMap& layer, const QString& set_id) {
  if (set_id.isEmpty()) return false;
  if (mask_owner_selection_references(layer).ids.contains(set_id)) return true;
  QVariantMap operations;
  for (const auto* key : {"effectStack", "layerStyleStack"}) {
    const auto name = QString::fromLatin1(key);
    if (layer.contains(name)) operations.insert(name, layer.value(name));
  }
  QSet<QString> references;
  return collect_layer_operation_mask_references(operations, &references) &&
         references.contains(set_id);
}

// Compute only exclusive sources. Unknown/shared consumers are preserved rather
// than guessed away; no render-order or legacy adjustment migration occurs.
inline QSet<QString> exclusive_mask_sets_for_deleted_layer(
    const QVariantMap& deleted, const QVariantList& survivors, const QVariantList& masks) {
  const auto deleted_references = mask_owner_selection_references(deleted);
  const auto& ids = deleted_references.ids;
  const auto& names = deleted_references.legacy_names;
  QSet<QString> candidates;
  QSet<QString> operation_ids;
  if (!collect_layer_operation_mask_references(deleted, &operation_ids))
    return {};  // A malformed deleted consumer may own any referenced source.
  QHash<QString, QSet<QString>> ids_by_name;
  for (const auto& value : masks) {
    const auto mask = value.toMap();
    if (mask.value(QStringLiteral("toolRole")).toString() == QStringLiteral("retouch")) continue;
    const auto id = mask.value(QStringLiteral("maskSetId")).toString().trimmed();
    const auto name = mask.value(QStringLiteral("maskSetName")).toString().trimmed();
    if (!id.isEmpty()) ids_by_name[name].insert(id);
    if (ids.contains(id) || operation_ids.contains(id)) candidates.insert(id);
  }
  for (const auto& name : names)
    if (ids_by_name.value(name).size() == 1) candidates.unite(ids_by_name.value(name));
  QSet<QString> exclusive;
  for (const auto& id : candidates) {
    QSet<QString> references{id};
    for (const auto& value : masks) {
      const auto mask = value.toMap();
      if (mask.value(QStringLiteral("maskSetId")).toString().trimmed() != id) continue;
      const auto source_id = mask.value(QStringLiteral("id")).toString().trimmed();
      const auto name = mask.value(QStringLiteral("maskSetName")).toString().trimmed();
      if (!source_id.isEmpty()) references.insert(source_id);
      if (!name.isEmpty()) references.insert(name); // preserve ambiguous legacy consumers
    }
    auto shared = false;
    for (const auto& survivor : survivors) {
      if (survivor.metaType().id() != QMetaType::QVariantMap) {
        shared = true;
        break;
      }
      if (mask_owner_contains_reference(survivor, references)) { shared = true; break; }
      QSet<QString> survivor_operations;
      if (!collect_layer_operation_mask_references(
              survivor.toMap(), &survivor_operations)) {
        shared = true;  // Opaque consumers are not safe to discard.
        break;
      }
      for (const auto& reference : survivor_operations)
        if (references.contains(reference)) { shared = true; break; }
      if (shared) break;
    }
    if (!shared) exclusive.insert(id);
  }
  return exclusive;
}

inline bool selection_has_locked_consumer(const QVariantMap& mask, const QVariantList& layers) {
  if (mask.value(QStringLiteral("toolRole")).toString() == QStringLiteral("retouch")) return false;
  const auto set_id = mask.value(QStringLiteral("maskSetId")).toString();
  const auto set_name = mask.value(QStringLiteral("maskSetName")).toString();
  const QSet<QString> ids{set_id, mask.value(QStringLiteral("id")).toString()};
  const layer_edit_access::LayerLockIndex locks(layers);
  for (auto index = 0; index < layers.size(); ++index) {
    if (!locks.effectively_locked(index)) continue;
    const auto value = layers.at(index);
    const auto layer = value.toMap();
    if (mask_owner_contains_reference(layer, ids)) return true;
    const auto owner_references = mask_owner_selection_references(layer);
    if (!set_name.isEmpty() && owner_references.legacy_names.contains(set_name)) return true;
    QSet<QString> operation_references;
    if (!collect_layer_operation_mask_references(layer, &operation_references) ||
        std::any_of(operation_references.cbegin(), operation_references.cend(),
            [&ids](const QString& reference) { return ids.contains(reference); }))
      return true;
  }
  return false;
}

// Layer masks and working selections may intentionally share source IDs. A
// layer lock therefore protects the referenced source set as a whole, even
// when the mask is currently being edited through the working-selection UI.
// Per-source lock and solo flags remain editable so users can unlock or inspect
// a protected set without changing its coverage or ownership.
inline bool has_forbidden_locked_selection_change(
    const QVariantList& before_masks, const QVariantList& after_masks,
    const QVariantList& layers) {
  QSet<QString> protected_set_ids;
  QSet<QString> protected_references;
  QSet<QString> protected_legacy_names;
  const layer_edit_access::LayerLockIndex locks(layers);
  for (auto index = 0; index < layers.size(); ++index) {
    if (!locks.effectively_locked(index)) continue;
    const auto layer = layers.at(index).toMap();
    const auto owner_references = mask_owner_selection_references(layer);
    for (const auto& id : owner_references.ids) {
      const auto reference = id.trimmed();
      if (reference.isEmpty()) continue;
      // Owner fields in older recipes sometimes refer to a source ID instead
      // of its enclosing mask-set ID. Keep the alias in both indexes so the
      // scan below can resolve that source back to the complete protected set.
      protected_set_ids.insert(reference);
      protected_references.insert(reference);
    }
    for (const auto& name : owner_references.legacy_names)
      if (!name.trimmed().isEmpty()) protected_legacy_names.insert(name.trimmed());
    QSet<QString> operation_references;
    if (!collect_layer_operation_mask_references(layer, &operation_references))
      return before_masks != after_masks;
    protected_references.unite(operation_references);
  }

  const auto collect_referenced_sets = [&](const QVariantList& masks) {
    for (const auto& value : masks) {
      const auto mask = value.toMap();
      const auto id = mask.value(QStringLiteral("id")).toString().trimmed();
      const auto set_id = mask.value(QStringLiteral("maskSetId")).toString().trimmed();
      const auto set_name = mask.value(QStringLiteral("maskSetName")).toString().trimmed();
      if (protected_references.contains(id) || protected_references.contains(set_id) ||
          protected_legacy_names.contains(set_name)) {
        if (!set_id.isEmpty()) protected_set_ids.insert(set_id);
        else if (!id.isEmpty()) protected_references.insert(id);
      }
    }
  };
  collect_referenced_sets(before_masks);
  collect_referenced_sets(after_masks);
  if (protected_set_ids.isEmpty() && protected_references.isEmpty() &&
      protected_legacy_names.isEmpty()) {
    return false;
  }

  const auto comparable_source = [](QVariantMap source) {
    source.remove(QStringLiteral("locked"));
    source.remove(QStringLiteral("solo"));
    return source;
  };
  const auto sources_for_set = [&](const QVariantList& masks, const QString& set_id) {
    QVariantList result;
    for (const auto& value : masks) {
      const auto source = value.toMap();
      if (source.value(QStringLiteral("maskSetId")).toString().trimmed() == set_id)
        result.append(comparable_source(source));
    }
    return result;
  };
  for (const auto& set_id : protected_set_ids)
    if (sources_for_set(before_masks, set_id) != sources_for_set(after_masks, set_id))
      return true;

  const auto standalone_sources = [&](const QVariantList& masks) {
    QVariantList result;
    for (const auto& value : masks) {
      const auto source = value.toMap();
      const auto id = source.value(QStringLiteral("id")).toString().trimmed();
      if (source.value(QStringLiteral("maskSetId")).toString().trimmed().isEmpty() &&
          protected_references.contains(id))
        result.append(comparable_source(source));
    }
    return result;
  };
  return standalone_sources(before_masks) != standalone_sources(after_masks);
}

// Earlier released strokes may publish after a later stroke captured its Undo
// boundary. Keep that boundary's topology/parameters, but include the coverage
// already committed on those same source IDs before dispatching the next stroke.
inline EditRecipeState rebase_selection_brush_boundary(EditRecipeState boundary, const EditRecipeState& current) {
  QHash<QString, QVariantMap> current_masks;
  for (const auto& value : current.mask_stack) {
    const auto mask = value.toMap();
    current_masks.insert(mask.value(QStringLiteral("id")).toString(), mask);
  }
  for (auto& value : boundary.mask_stack) {
    auto mask = value.toMap();
    if (mask.value(QStringLiteral("kind")).toString() != QStringLiteral("brush")) continue;
    const auto latest = current_masks.value(mask.value(QStringLiteral("id")).toString());
    if (latest.isEmpty()) continue;
    for (auto it = latest.cbegin(); it != latest.cend(); ++it) {
      const auto& key = it.key();
      if (key == QStringLiteral("brushPoints") || key == QStringLiteral("brushStrokes") ||
          key.startsWith(QStringLiteral("segmentation")) || key.startsWith(QStringLiteral("brushAlpha")) ||
          key.startsWith(QStringLiteral("raster"))) mask.insert(key, it.value());
    }
    value = mask;
  }
  return boundary;
}

}  // namespace photo_editor::desktop
