#include "edit_adjustment_patch.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMutex>
#include <QMutexLocker>
#include <deque>

namespace photo_editor::desktop {
namespace {

constexpr auto kPatchFormat =
    "photo-editor-adjustment-patch";
constexpr int kPatchVersion = 1;

QString scope_name(AdjustmentPatchScope scope) {
  switch (scope) {
    case AdjustmentPatchScope::SourceAgnostic:
      return QStringLiteral("source-agnostic");
    case AdjustmentPatchScope::CameraBound:
      return QStringLiteral("camera-bound");
    case AdjustmentPatchScope::AssetBound:
      return QStringLiteral("asset-bound");
  }
  return {};
}

std::optional<AdjustmentPatchScope> parse_scope(
    const QString& value) {
  if (value == QStringLiteral("source-agnostic")) {
    return AdjustmentPatchScope::SourceAgnostic;
  }
  if (value == QStringLiteral("camera-bound")) {
    return AdjustmentPatchScope::CameraBound;
  }
  if (value == QStringLiteral("asset-bound")) {
    return AdjustmentPatchScope::AssetBound;
  }
  return std::nullopt;
}

QStringList validated_groups(
    const QJsonArray& values,
    QString* error) {
  QStringList groups;
  for (const auto& value : values) {
    if (!value.isString()) {
      *error =
          QStringLiteral(
              "Adjustment patch groups must be strings.");
      return {};
    }
    const auto normalized =
        normalize_adjustment_group(value.toString());
    if (normalized.isEmpty()) {
      *error =
          QStringLiteral(
              "Adjustment patch contains an unknown group.");
      return {};
    }
    if (normalized == QStringLiteral("all")) {
      return all_adjustment_groups();
    }
    if (!groups.contains(normalized)) {
      groups.push_back(normalized);
    }
  }
  if (groups.isEmpty()) {
    *error =
        QStringLiteral(
            "Adjustment patch must declare at least one group.");
  }
  return groups;
}

QJsonArray groups_to_json(const QStringList& groups) {
  QJsonArray result;
  for (const auto& group : groups) {
    result.push_back(group);
  }
  return result;
}

EditRecipeState scoped_state(
    const EditRecipeState& state,
    const QStringList& groups,
    AdjustmentPatchScope scope) {
  if (scope == AdjustmentPatchScope::SourceAgnostic) {
    return source_agnostic_adjustment_state(
        state, groups);
  }
  return merge_edit_recipe_state_by_groups(
      EditRecipeState{}, state, groups);
}

bool contains_large_identity_payload(
    const QVariant& value) {
  const auto type_id = value.metaType().id();
  if (type_id == QMetaType::QByteArray) {
    return value.toByteArray().size() > 4096;
  }
  if (type_id == QMetaType::QString) {
    return value.toString().toUtf8().size() > 4096;
  }
  if (type_id == QMetaType::QVariantList) {
    const auto source = value.toList();
    if (source.size() > 256) {
      return true;
    }
    for (const auto& entry : source) {
      if (contains_large_identity_payload(entry)) {
        return true;
      }
    }
  }
  if (type_id == QMetaType::QVariantMap) {
    const auto source = value.toMap();
    for (auto iterator = source.constBegin();
         iterator != source.constEnd();
         ++iterator) {
      if (contains_large_identity_payload(
              iterator.value())) {
        return true;
      }
    }
  }
  return false;
}

QVariantMap compact_identity_entry(
    const QVariantMap& source_entry,
    bool mask_entry) {
  auto entry = source_entry;
  // The numeric revision identifies transient paint pixels. Retaining QImage
  // snapshots here would bypass this cache's serialized-byte memory budget.
  entry.remove(QStringLiteral("paintDraftImage"));
  entry.remove(QStringLiteral("paintDraftTiles"));
  // QVariant maps retain immutable, implicitly shared payloads. Equality
  // detects edits (including binary masks) while repeated viewport queries
  // avoid serializing and hashing the same large payload again.
  struct CachedIdentity {
    QVariantMap source;
    QVariantMap compacted;
    bool mask = false;
    qsizetype serialized_bytes = 0;
  };
  static QMutex mutex;
  static std::deque<CachedIdentity> cache;
  constexpr qsizetype budget = 32 * 1024 * 1024;
  // A recipe can contain masks in both its stack and mask sets. Four entries
  // thrash even for one ordinary multi-mask photo. Keep the byte ceiling while
  // allowing a complete recipe and its nearby revisions to remain resident.
  constexpr std::size_t maximum_entries = 32;
  {
    QMutexLocker lock(&mutex);
    for (auto it = cache.begin(); it != cache.end(); ++it) {
      if (it->mask == mask_entry && it->source == entry) {
        auto hit = std::move(*it);
        cache.erase(it);
        cache.push_front(std::move(hit));
        return cache.front().compacted;
      }
    }
  }
  const auto bytes =
      QJsonDocument::fromVariant(entry).toJson(
          QJsonDocument::Compact);
  const auto digest =
      QString::fromLatin1(
          QCryptographicHash::hash(
              bytes,
              QCryptographicHash::Sha256)
              .toHex());
  QVariantMap compacted{
      {QStringLiteral("id"),
       QStringLiteral(
           "identity-sha256:%1:bytes:%2")
           .arg(digest)
           .arg(bytes.size())},
      {QStringLiteral("name"),
       QStringLiteral("Identity %1").arg(digest)},
  };
  if (mask_entry) {
    compacted.insert(
        QStringLiteral("kind"),
        QStringLiteral("linear"));
    compacted.insert(
        QStringLiteral("enabled"), true);
  }
  if (bytes.size() <= budget) {
    QMutexLocker lock(&mutex);
    // GUI and render workers may finish the same miss concurrently. A single
    // immutable source entry must consume only one cache slot and byte charge.
    for (auto it = cache.begin(); it != cache.end(); ++it) {
      if (it->mask == mask_entry && it->source == entry) {
        auto hit = std::move(*it);
        cache.erase(it);
        cache.push_front(std::move(hit));
        return cache.front().compacted;
      }
    }
    qsizetype retained = 0;
    for (const auto& cached : cache) retained += cached.serialized_bytes;
    while (!cache.empty() && (cache.size() >= maximum_entries || retained + bytes.size() > budget)) {
      retained -= cache.back().serialized_bytes;
      cache.pop_back();
    }
    cache.push_front({entry, compacted, mask_entry, bytes.size()});
  }
  return compacted;
}

QVariantList compact_identity_collection(
    const QVariantList& collection,
    bool mask_entries) {
  if (collection.size() > 256) {
    return QVariantList{
        compact_identity_entry(
            QVariantMap{
                {QStringLiteral("entries"), collection},
            },
            mask_entries),
    };
  }
  QVariantList compacted;
  compacted.reserve(collection.size());
  for (const auto& entry : collection) {
    if (!entry.canConvert<QVariantMap>() ||
        !contains_large_identity_payload(entry)) {
      compacted.append(entry);
      continue;
    }
    compacted.append(
        compact_identity_entry(
            entry.toMap(), mask_entries));
  }
  return compacted;
}

}  // namespace

EditRecipeState source_agnostic_adjustment_state(
    const EditRecipeState& state,
    const QStringList& groups) {
  auto result = merge_edit_recipe_state_by_groups(
      EditRecipeState{}, state, groups);

  // A reusable adjustment may describe rendering intent, but it
  // must never carry identities or paths belonging to the source,
  // monitor, proofing device, or export destination that authored it.
  result.input_color_profile = QStringLiteral("Embedded");
  result.camera_dcp_profile_asset = {};
  result.input_icc_profile_asset = {};
  result.preview_icc_profile_asset = {};
  result.proof_icc_profile_asset = {};
  result.export_icc_profile_asset = {};
  result.monitor_color_profile.clear();
  return result;
}

QJsonObject serialize_adjustment_patch(
    const EditRecipeState& state,
    const QStringList& groups,
    AdjustmentPatchScope scope) {
  const auto normalized_groups =
      normalize_adjustment_groups(groups);
  const auto state_object = build_edit_adjustment_state_object(
      scoped_state(state, normalized_groups, scope));

  return QJsonObject{
      {QStringLiteral("format"),
       QString::fromLatin1(kPatchFormat)},
      {QStringLiteral("version"), kPatchVersion},
      {QStringLiteral("scope"), scope_name(scope)},
      {QStringLiteral("groups"),
       groups_to_json(normalized_groups)},
      {QStringLiteral("adjustmentState"), state_object},
  };
}

QString serialize_adjustment_patch_json(
    const EditRecipeState& state,
    const QStringList& groups,
    AdjustmentPatchScope scope) {
  return QString::fromUtf8(
      QJsonDocument(
          serialize_adjustment_patch(
              state, groups, scope))
          .toJson(QJsonDocument::Compact));
}

QVariantMap build_reversible_preset_application_record(
    const EditRecipeState& before,
    const EditRecipeState& after,
    const QString& application_id,
    const QString& preset_identity,
    const QString& source,
    const QStringList& groups,
    double intensity) {
  auto recorded_groups = normalize_adjustment_groups(groups);
  if (recorded_groups.isEmpty()) {
    recorded_groups = all_adjustment_groups();
  }
  const auto before_patch = serialize_adjustment_patch_json(
      before,
      recorded_groups,
      AdjustmentPatchScope::AssetBound);
  const auto after_patch = serialize_adjustment_patch_json(
      after,
      recorded_groups,
      AdjustmentPatchScope::AssetBound);
  const auto patch_sha256 = [](const QString& patch) {
    return QString::fromLatin1(
        QCryptographicHash::hash(
            patch.toUtf8(), QCryptographicHash::Sha256)
            .toHex());
  };
  return QVariantMap{
      {QStringLiteral("id"), application_id},
      {QStringLiteral("identity"), preset_identity},
      {QStringLiteral("name"), preset_identity},
      {QStringLiteral("source"), source},
      {QStringLiteral("groups"), recorded_groups},
      {QStringLiteral("intensity"),
       qBound(0.0, intensity, 1.0)},
      {QStringLiteral("beforeAdjustmentPatchJson"),
       before_patch},
      {QStringLiteral("beforeAdjustmentPatchSha256"),
       patch_sha256(before_patch)},
      {QStringLiteral("afterAdjustmentPatchJson"),
       after_patch},
      {QStringLiteral("afterAdjustmentPatchSha256"),
       patch_sha256(after_patch)},
  };
}

QString adjustment_patch_identity_json(
    const EditRecipeState& state) {
  auto identity_state = state;
  identity_state.mask_stack =
      compact_identity_collection(
          state.mask_stack, true);
  identity_state.layers =
      compact_identity_collection(state.layers, false);
  return serialize_adjustment_patch_json(
      identity_state,
      all_adjustment_groups(),
      AdjustmentPatchScope::AssetBound);
}

AdjustmentPatchDecodeResult parse_adjustment_patch(
    const QJsonObject& patch) {
  AdjustmentPatchDecodeResult result;
  if (patch.value(QStringLiteral("format")).toString() !=
      QString::fromLatin1(kPatchFormat)) {
    result.error =
        QStringLiteral(
            "Unsupported adjustment patch format.");
    return result;
  }
  if (patch.value(QStringLiteral("version")).toInt() !=
      kPatchVersion) {
    result.error =
        QStringLiteral(
            "Unsupported adjustment patch version.");
    return result;
  }
  const auto scope =
      parse_scope(
          patch.value(QStringLiteral("scope")).toString());
  if (!scope.has_value()) {
    result.error =
        QStringLiteral(
            "Unsupported adjustment patch scope.");
    return result;
  }
  result.scope = *scope;
  result.groups = validated_groups(
      patch.value(QStringLiteral("groups")).toArray(),
      &result.error);
  if (!result.error.isEmpty()) {
    return result;
  }
  if (!patch.value(QStringLiteral("adjustmentState"))
           .isObject()) {
    result.error =
        QStringLiteral(
            "Adjustment patch state must be an object.");
    return result;
  }

  auto state_object =
      patch.value(QStringLiteral("adjustmentState"))
          .toObject();
  if (state_object.contains(
          QStringLiteral("recipeVersion"))) {
    result.error =
        QStringLiteral(
            "Adjustment patch cannot embed a legacy recipe.");
    return result;
  }
  auto parsed = parse_edit_adjustment_state_object(state_object);
  parsed = scoped_state(parsed, result.groups, *scope);
  result.state = std::move(parsed);
  return result;
}

AdjustmentPatchDecodeResult parse_adjustment_patch_json(
    const QString& patch_json) {
  const auto document =
      QJsonDocument::fromJson(patch_json.toUtf8());
  if (!document.isObject()) {
    AdjustmentPatchDecodeResult result;
    result.error =
        QStringLiteral(
            "Adjustment patch JSON must be an object.");
    return result;
  }
  return parse_adjustment_patch(document.object());
}

AdjustmentPatchDecodeResult
parse_adjustment_payload_or_legacy_recipe(
    const QString& payload) {
  if (payload.trimmed().isEmpty()) {
    AdjustmentPatchDecodeResult result;
    result.state = EditRecipeState{};
    result.groups = all_adjustment_groups();
    result.scope = AdjustmentPatchScope::AssetBound;
    return result;
  }
  const auto patch = parse_adjustment_patch_json(payload);
  if (patch.ok()) {
    return patch;
  }
  const auto document =
      QJsonDocument::fromJson(payload.toUtf8());
  if (document.isObject() &&
      (document.object().contains(
           QStringLiteral("format")) ||
       document.object().contains(
           QStringLiteral("adjustmentState")))) {
    return patch;
  }
  const auto migration =
      migrate_edit_recipe_json(payload);
  AdjustmentPatchDecodeResult result;
  if (!migration.errors.isEmpty() ||
      migration.recipe_json.isEmpty()) {
    result.error =
        QStringLiteral(
            "Adjustment payload is neither a patch nor a supported legacy recipe.");
    return result;
  }
  result.state =
      parse_edit_recipe_json(migration.recipe_json);
  result.groups = all_adjustment_groups();
  result.scope = AdjustmentPatchScope::AssetBound;
  return result;
}

QString canonical_asset_bound_adjustment_patch_json(
    const QString& payload) {
  if (payload.trimmed().isEmpty()) {
    return {};
  }
  const auto decoded =
      parse_adjustment_payload_or_legacy_recipe(
          payload);
  return decoded.ok()
             ? serialize_adjustment_patch_json(
                   *decoded.state,
                   all_adjustment_groups(),
                   AdjustmentPatchScope::AssetBound)
             : QString{};
}

StoredAdjustmentState parse_stored_adjustment_state(
    const QJsonObject& object,
    AdjustmentPatchScope required_scope) {
  const auto patch_value =
      object.value(QStringLiteral("adjustmentPatch"));
  if (patch_value.isObject()) {
    const auto decoded =
        parse_adjustment_patch(patch_value.toObject());
    if (!decoded.ok() ||
        decoded.scope != required_scope) {
      return {};
    }
    return StoredAdjustmentState{
        .state = decoded.state,
        .groups = decoded.groups,
    };
  }

  // Compatibility is deliberately read-only. Callers serialize
  // every subsequent mutation with adjustmentPatch.
  const auto migration = migrate_edit_recipe_json(
      object.value(QStringLiteral("recipeJson"))
          .toString());
  if (!migration.errors.isEmpty() ||
      migration.recipe_json.isEmpty()) {
    return {};
  }
  QString error;
  auto groups = validated_groups(
      object.value(QStringLiteral("adjustmentGroups"))
          .toArray(),
      &error);
  if (!error.isEmpty()) {
    groups.clear();
  }
  return StoredAdjustmentState{
      .state =
          parse_edit_recipe_json(migration.recipe_json),
      .groups = groups,
      .legacy_compatibility_read = true,
  };
}

}  // namespace photo_editor::desktop
