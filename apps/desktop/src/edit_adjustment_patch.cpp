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

bool exact_identity_payload_equal(
    const QVariant& left,
    const QVariant& right) {
  if (left.metaType().id() != right.metaType().id()) {
    return false;
  }
  const auto type = left.metaType().id();
  if (type == QMetaType::QVariantList) {
    const auto left_list = left.toList();
    const auto right_list = right.toList();
    if (left_list.size() != right_list.size()) {
      return false;
    }
    if (left_list.constData() == right_list.constData()) {
      return true;
    }
    for (qsizetype index = 0; index < left_list.size(); ++index) {
      if (!exact_identity_payload_equal(left_list.at(index), right_list.at(index))) {
        return false;
      }
    }
    return true;
  }
  if (type == QMetaType::QVariantMap) {
    const auto left_map = left.toMap();
    const auto right_map = right.toMap();
    if (left_map.size() != right_map.size()) {
      return false;
    }
    auto left_it = left_map.cbegin();
    auto right_it = right_map.cbegin();
    for (; left_it != left_map.cend(); ++left_it, ++right_it) {
      if (left_it.key() != right_it.key() ||
          !exact_identity_payload_equal(left_it.value(), right_it.value())) {
        return false;
      }
    }
    return true;
  }
  return left == right;
}

qsizetype identity_payload_retained_bytes(const QVariant& value, qsizetype budget) {
  qsizetype retained = 0;
  const auto consume = [&](qsizetype bytes) {
    if (bytes > budget - retained) {
      retained = budget + 1;
      return false;
    }
    retained += bytes;
    return true;
  };
  const auto visit = [&](const auto& self, const QVariant& item) -> bool {
    if (!consume(sizeof(QVariant))) return false;
    const auto type = item.metaType().id();
    if (type == QMetaType::QString) {
      const auto string = item.toString();
      return string.capacity() <= (budget - retained) / 2 &&
          consume(string.capacity() * 2);
    }
    if (type == QMetaType::QByteArray) return consume(item.toByteArray().capacity());
    if (type == QMetaType::QVariantList) {
      const auto list = item.toList();
      if (list.capacity() > (budget - retained) / static_cast<qsizetype>(sizeof(QVariant)) ||
          !consume(list.capacity() * static_cast<qsizetype>(sizeof(QVariant)))) return false;
      for (const auto& child : list) if (!self(self, child)) return false;
    } else if (type == QMetaType::QVariantMap) {
      const auto map = item.toMap();
      for (auto it = map.cbegin(); it != map.cend(); ++it) {
        // Conservatively cover the map node and the key's shared allocation.
        if (!consume(128) || !self(self, it.key()) || !self(self, it.value())) return false;
      }
    } else if (type != QMetaType::UnknownType && type != QMetaType::Nullptr &&
               type != QMetaType::Bool && type != QMetaType::Int && type != QMetaType::UInt &&
               type != QMetaType::LongLong && type != QMetaType::ULongLong &&
               type != QMetaType::Double && type != QMetaType::Float) {
      // Unknown allocations (for example an image in imported payload data)
      // must not bypass the byte ceiling.
      return false;
    }
    return true;
  };
  return visit(visit, value) ? retained : budget + 1;
}

constexpr qsizetype kBrushHistoryIdentityCacheEntryOverheadBytes = 512;

struct BrushHistoryIdentityCacheEntry final {
  QVariant source_payload;
  QVariantMap identity_payload;
  qsizetype retained_bytes = 0;
};

struct BrushHistoryIdentityCache final {
  QMutex mutex;
  std::deque<BrushHistoryIdentityCacheEntry> entries;
  qsizetype retained_bytes = 0;
  int hit_count = 0;
  int miss_count = 0;
  int eviction_count = 0;

  QVariantMap identity_for(const QVariant& payload) {
    {
      QMutexLocker lock(&mutex);
      for (auto it = entries.begin(); it != entries.end(); ++it) {
        if (exact_identity_payload_equal(it->source_payload, payload)) {
          auto hit = std::move(*it);
          entries.erase(it);
          entries.push_front(std::move(hit));
          ++hit_count;
          return entries.front().identity_payload;
        }
      }
    }

    // Never accept a caller-provided signature as proof of identity. The
    // cache is keyed by exact QVariant content equality; this serialization
    // and SHA-256 digest is only reused after that equality check succeeds.
    // Keep scalar payloads (notably QByteArray and QString) inside an object:
    // QJsonDocument only serializes top-level arrays/objects, and an unsupported
    // top-level value would otherwise produce the same empty byte sequence.
    const auto bytes = QJsonDocument::fromVariant(
                           QVariantMap{{QStringLiteral("payload"), payload}})
                           .toJson(QJsonDocument::Compact);
    const QVariantMap identity{
        {QStringLiteral("brushHistoryIdentityVersion"), 1},
        {QStringLiteral("sha256"), QString::fromLatin1(
             QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex())},
        {QStringLiteral("bytes"), bytes.size()},
    };
    const auto payload_bytes = identity_payload_retained_bytes(
        payload,
        kMaxBrushHistoryIdentityCacheMemoryBytes -
            kBrushHistoryIdentityCacheEntryOverheadBytes);
    if (payload_bytes > kMaxBrushHistoryIdentityCacheMemoryBytes -
                            kBrushHistoryIdentityCacheEntryOverheadBytes) {
      QMutexLocker lock(&mutex);
      ++miss_count;
      return identity;
    }
    const auto entry_bytes = payload_bytes + kBrushHistoryIdentityCacheEntryOverheadBytes;

    QMutexLocker lock(&mutex);
    // Other render/UI workers may have finished this same history while the
    // digest was being computed. Verify the full payload again before reuse.
    for (auto it = entries.begin(); it != entries.end(); ++it) {
      if (exact_identity_payload_equal(it->source_payload, payload)) {
        auto hit = std::move(*it);
        entries.erase(it);
        entries.push_front(std::move(hit));
        ++hit_count;
        return entries.front().identity_payload;
      }
    }
    ++miss_count;
    while (!entries.empty() &&
           (entries.size() >= static_cast<std::size_t>(
                                  kMaxBrushHistoryIdentityCacheEntries) ||
            retained_bytes + entry_bytes > kMaxBrushHistoryIdentityCacheMemoryBytes)) {
      retained_bytes -= entries.back().retained_bytes;
      entries.pop_back();
      ++eviction_count;
    }
    entries.push_front({payload, identity, entry_bytes});
    retained_bytes += entry_bytes;
    return identity;
  }

  BrushHistoryIdentityCacheSnapshot snapshot() {
    QMutexLocker lock(&mutex);
    return BrushHistoryIdentityCacheSnapshot{
        .entries = static_cast<int>(entries.size()),
        .retained_bytes = retained_bytes,
        .hit_count = hit_count,
        .miss_count = miss_count,
        .eviction_count = eviction_count,
    };
  }
};

BrushHistoryIdentityCache& brush_history_identity_cache() {
  static BrushHistoryIdentityCache cache;
  return cache;
}

QVariantMap brush_history_payload_identity(const QVariant& payload) {
  return brush_history_identity_cache().identity_for(payload);
}

QVariant brush_strokes_payload_identity(const QVariant& payload) {
  if (payload.metaType().id() != QMetaType::QVariantList) {
    return brush_history_payload_identity(payload);
  }
  auto strokes = payload.toList();
  bool compacted_samples = false;
  for (auto& stroke_value : strokes) {
    auto stroke = stroke_value.toMap();
    const auto samples = stroke.value(QStringLiteral("samples"));
    if (!contains_large_identity_payload(samples)) {
      continue;
    }
    stroke.insert(
        QStringLiteral("samples"),
        brush_history_payload_identity(samples));
    stroke_value = stroke;
    compacted_samples = true;
  }
  return brush_history_payload_identity(compacted_samples ? QVariant(strokes) : payload);
}

QVariantMap paint_payload_identity(const QVariantMap& payload) {
  struct CachedPayload {
    QVariantMap source;
    QVariantMap identity;
    qsizetype retained_bytes = 0;
  };
  static QMutex mutex;
  static std::deque<CachedPayload> cache;
  // Paint tiles are immutable, implicitly shared strings. Budget their UTF-16
  // storage conservatively rather than keeping serialized JSON alongside them.
  constexpr qsizetype budget = 256 * 1024 * 1024;
  constexpr std::size_t maximum_entries = 16;
  {
    QMutexLocker lock(&mutex);
    for (auto it = cache.begin(); it != cache.end(); ++it) {
      if (it->source == payload) {
        auto hit = std::move(*it);
        cache.erase(it);
        cache.push_front(std::move(hit));
        return cache.front().identity;
      }
    }
  }
  const auto bytes = QJsonDocument::fromVariant(payload).toJson(QJsonDocument::Compact);
  const QVariantMap identity{
      {QStringLiteral("paintPayloadIdentityVersion"), 1},
      {QStringLiteral("sha256"), QString::fromLatin1(
          QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex())},
      {QStringLiteral("bytes"), bytes.size()},
  };
  // Charge the retained source, including string capacity and container nodes,
  // rather than just its JSON length. No serialized pixels remain in the cache.
  const auto retained_bytes = identity_payload_retained_bytes(payload, budget);
  if (retained_bytes <= budget) {
    QMutexLocker lock(&mutex);
    for (auto it = cache.begin(); it != cache.end(); ++it) {
      if (it->source == payload) {
        auto hit = std::move(*it);
        cache.erase(it);
        cache.push_front(std::move(hit));
        return cache.front().identity;
      }
    }
    qsizetype retained = 0;
    for (const auto& entry : cache) retained += entry.retained_bytes;
    while (!cache.empty() &&
           (cache.size() >= maximum_entries || retained + retained_bytes > budget)) {
      retained -= cache.back().retained_bytes;
      cache.pop_back();
    }
    cache.push_front({payload, identity, retained_bytes});
  }
  return identity;
}

QVariantMap compact_identity_entry(
    const QVariantMap& source_entry,
    bool mask_entry) {
  auto entry = source_entry;
  // The numeric revision identifies transient paint pixels. Retaining QImage
  // snapshots here would bypass this cache's serialized-byte memory budget.
  entry.remove(QStringLiteral("paintDraftImage"));
  entry.remove(QStringLiteral("paintDraftTiles"));
  // Hash immutable pixels independently of position, text, opacity and other
  // metadata. A cutout larger than the ordinary entry-cache budget otherwise
  // serializes again on every viewport query and every transform revision.
  const auto paint_payload = entry.value(QStringLiteral("paintTiles")).toMap();
  if (!mask_entry && !paint_payload.isEmpty() &&
      contains_large_identity_payload(paint_payload)) {
    entry.insert(QStringLiteral("paintTiles"), paint_payload_identity(paint_payload));
  }
  for (const auto& key : {
           QStringLiteral("brushPoints"),
           QStringLiteral("brushStrokes"),
       }) {
    const auto history = entry.value(key);
    if (!history.isValid() || !contains_large_identity_payload(history)) {
      continue;
    }
    entry.insert(
        key,
        key == QStringLiteral("brushStrokes")
            ? brush_strokes_payload_identity(history)
            : QVariant(brush_history_payload_identity(history)));
  }
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

BrushHistoryIdentityCacheSnapshot brush_history_identity_cache_snapshot() {
  return brush_history_identity_cache().snapshot();
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
