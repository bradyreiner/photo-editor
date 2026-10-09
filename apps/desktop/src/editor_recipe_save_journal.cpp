#include "editor_recipe_save_journal.h"
#include <QAnyStringView>

#include <QCryptographicHash>
#include <QDir>
#include <QDebug>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <algorithm>
#include <array>
#include <limits>
#include <type_traits>
#if defined(Q_OS_DARWIN) && !defined(PHOTO_EDITOR_TEST_PORTABLE_CHECKPOINT_SHA256)
#include <CommonCrypto/CommonDigest.h>
#endif

namespace photo_editor::desktop {
namespace {
constexpr qint64 kMaxJournalBytes = 256LL * 1024 * 1024;
constexpr qsizetype kMaxPendingAssets = 512;
// Bound serialized cache bytes separately from the journal's on-disk limit.
// QJsonValue comparison data also stays alive for these entries.
constexpr qsizetype kMaxCachedSerializationBytes = 64LL * 1024 * 1024;
const QString kSchema = QStringLiteral("photo-editor-recipe-outbox:v3");
const QString kPreviousSchema = QStringLiteral("photo-editor-recipe-outbox:v2");
const QString kLegacySchema = QStringLiteral("photo-editor-recipe-outbox:v1");
constexpr qsizetype kLargeRasterJsonThreshold = 64 * 1024;

QByteArray compact_json_scalar(const QJsonValue& value) {
  const auto wrapped = QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact);
  return wrapped.mid(1, wrapped.size() - 2);
}

bool contains_large_raster_string(const QJsonValue& value) {
  // Cutout paint tiles and pixel payloads are also large encoded strings.
  // Detect by value so nested tile dictionaries and arrays share the mask path.
  if (value.isString()) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
    return value.toStringView().size() >= kLargeRasterJsonThreshold;
#else
    return value.toString().size() >= kLargeRasterJsonThreshold;
#endif
  }
  if (value.isObject()) {
    const auto object = value.toObject();
    for (auto field = object.constBegin(); field != object.constEnd(); ++field) {
      if (contains_large_raster_string(field.value())) return true;
    }
    return false;
  }
  if (value.isArray()) {
    for (const auto& child : value.toArray()) {
      if (contains_large_raster_string(child)) return true;
    }
  }
  return false;
}

bool append_compact_json(
    const QJsonValue& value,
    QByteArray* output,
    qsizetype maximum_size,
    bool contains_large_raster = false) {
  const auto append = [output, maximum_size](const QByteArray& bytes) {
    if (bytes.size() > maximum_size - output->size()) return false;
    output->append(bytes);
    return true;
  };
  if (!contains_large_raster) {
    const auto bytes = value.isObject()
                           ? QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact)
                           : value.isArray()
                                 ? QJsonDocument(value.toArray()).toJson(QJsonDocument::Compact)
                                 : compact_json_scalar(value);
    return append(bytes);
  }

  if (value.isString() && contains_large_raster_string(value)) {
    // Borrow JSON's encoded storage where Qt supports it. Validate before
    // copying: every non-ASCII/control/quote/backslash uses Qt's exact escaping.
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
    const auto string_view = value.toStringView();
#else
    const auto string = value.toString();
    const auto string_view = QAnyStringView(string);
#endif
    return string_view.visit([&](auto view) {
      unsigned int requires_escaping = 0;
      for (const auto character : view) {
        const auto code = [&] {
          if constexpr (std::is_same_v<decltype(view), QStringView>)
            return static_cast<unsigned int>(character.unicode());
          else
            return static_cast<unsigned int>(static_cast<unsigned char>(character));
        }();
        requires_escaping |= static_cast<unsigned int>(
            code < 0x20U || code >= 0x7fU || code == '"' || code == '\\');
      }
      if (requires_escaping != 0) return append(compact_json_scalar(value));
      if (!append(QByteArrayLiteral("\""))) return false;
      if constexpr (std::is_same_v<decltype(view), QStringView>) {
        if (!append(view.toString().toLatin1())) return false;
      } else {
        if (view.size() > maximum_size - output->size()) return false;
        output->append(view.data(), view.size());
      }
      return append(QByteArrayLiteral("\""));
    });
  }

  if (value.isObject()) {
    if (!append(QByteArrayLiteral("{"))) return false;
    bool first = true;
    const auto object = value.toObject();
    for (auto field = object.constBegin(); field != object.constEnd(); ++field) {
      if (!first && !append(QByteArrayLiteral(","))) return false;
      first = false;
      if (!append(compact_json_scalar(QJsonValue(field.key()))) ||
          !append(QByteArrayLiteral(":"))) {
        return false;
      }
      const auto& child = field.value();
      const bool child_contains_raster = contains_large_raster_string(child);
      if (!append_compact_json(child, output, maximum_size, child_contains_raster)) {
        return false;
      }
    }
    return append(QByteArrayLiteral("}"));
  }

  if (value.isArray()) {
    if (!append(QByteArrayLiteral("["))) return false;
    bool first = true;
    for (const auto& child : value.toArray()) {
      if (!first && !append(QByteArrayLiteral(","))) return false;
      first = false;
      const bool child_contains_raster = contains_large_raster_string(child);
      if (!append_compact_json(child, output, maximum_size, child_contains_raster)) {
        return false;
      }
    }
    return append(QByteArrayLiteral("]"));
  }
  return append(compact_json_scalar(value));
}

QByteArray compact_json_bytes(const QJsonValue& value, qsizetype maximum_size) {
  QByteArray bytes;
  if (!append_compact_json(
          value, &bytes, maximum_size, contains_large_raster_string(value))) {
    return {};
  }
  return bytes;
}

QByteArray checkpoint_sha256_hex(const QByteArray& bytes) {
#if defined(Q_OS_DARWIN) && !defined(PHOTO_EDITOR_TEST_PORTABLE_CHECKPOINT_SHA256)
  // Qt's portable SHA-256 dominates large checkpoints on this Apple Silicon
  // build. CommonCrypto provides the same digest via the platform implementation.
  // Keep the portable path for other platforms and inputs beyond CC_LONG.
  if (static_cast<quint64>(bytes.size()) <= std::numeric_limits<CC_LONG>::max()) {
    std::array<unsigned char, CC_SHA256_DIGEST_LENGTH> digest{};
    if (CC_SHA256(bytes.constData(), static_cast<CC_LONG>(bytes.size()), digest.data()))
      return QByteArray(reinterpret_cast<const char*>(digest.data()), CC_SHA256_DIGEST_LENGTH).toHex();
  }
#endif
  return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex();
}

struct JournalWriteProfile final {
  bool enabled = qEnvironmentVariableIsSet("PHOTO_EDITOR_PROFILE_RECIPE_CHECKPOINTS");
  QElapsedTimer timer;
  QJsonObject stages;
  JournalWriteProfile() { if (enabled) timer.start(); }
  void stage(const QString& name) {
    if (!enabled) return;
    stages.insert(name, static_cast<double>(timer.nsecsElapsed()) / 1000.0);
    timer.restart();
  }
  ~JournalWriteProfile() {
    if (!enabled) return;
    stages.insert("event", "recipe.checkpoint.profile");
    qInfo().noquote() << QJsonDocument(stages).toJson(QJsonDocument::Compact);
  }
};

bool fail(QString* error, const QString& detail) {
  if (error) *error = QStringLiteral("Edit recovery checkpoint unavailable: %1").arg(detail);
  return false;
}

QJsonValue encode(const std::optional<EditorRecipeSaveSnapshot>& snapshot,
                  EditRecipeMaskEncodingCache* cache) {
  if (!snapshot) return QJsonValue(QJsonValue::Null);
  return QJsonObject{{"assetId", snapshot->asset_id}, {"filePath", snapshot->file_path},
      {"recipe", build_edit_recipe_object_with_mask_cache(snapshot->state, cache)},
      {"migrationEvidence", QJsonObject::fromVariantMap(snapshot->migration_evidence)},
      {"agentTaskId", snapshot->agent_task_id.isEmpty() ? QStringLiteral("") : snapshot->agent_task_id}};
}

bool decode(const QJsonValue& value, bool legacy, std::optional<EditorRecipeSaveSnapshot>* result) {
  if (value.isNull()) { result->reset(); return true; }
  if (!value.isObject()) return false;
  const auto object = value.toObject();
  if (object.size() != (object.contains("agentTaskId") ? 5 : 4) || !object.value("assetId").isString() ||
      object.value("assetId").toString().isEmpty() || !object.value("filePath").isString() ||
      object.value("filePath").toString().isEmpty() ||
      (legacy ? !object.value("recipe").isString() : !object.value("recipe").isObject()) ||
      !object.value("migrationEvidence").isObject()) return false;
  if (object.contains("agentTaskId") && (!object.value("agentTaskId").isString() ||
      (!object.value("agentTaskId").toString().isEmpty() &&
       !QRegularExpression(QStringLiteral("^[A-Za-z0-9_-]{1,128}$"))
           .match(object.value("agentTaskId").toString()).hasMatch()))) return false;
  const auto recipe = legacy ? QJsonDocument::fromJson(object.value("recipe").toString().toUtf8()).object()
                             : object.value("recipe").toObject();
  const auto version = recipe.value("recipeVersion").toInt();
  // The writer selects a desktop format according to the layer content.
  // Recovery must accept every version that build_edit_recipe_object emits.
  if (version != kCurrentEditRecipeVersion && version != kPhotoEffectsEditRecipeVersion &&
      version != kPhotoTypographyEditRecipeVersion && version != kPhotoPaintEditRecipeVersion)
    return false;
  auto state = parse_edit_recipe_json(QString::fromUtf8(
      QJsonDocument(recipe).toJson(QJsonDocument::Compact)));
  // Do not silently normalize/drop fields from an unsupported recovery format.
  if (build_edit_recipe_object(state) != recipe) return false;
  *result = EditorRecipeSaveSnapshot{.asset_id = object.value("assetId").toString(),
      .file_path = object.value("filePath").toString(), .state = std::move(state),
      .migration_evidence = object.value("migrationEvidence").toObject().toVariantMap(),
      .agent_task_id = object.value("agentTaskId").toString()};
  return true;
}
}

QString EditorRecipeSaveJournal::currentAuthority() {
  // The root separately scopes managed catalogs. Hash explicit authority to keep
  // credentials out of filenames, checkpoint contents and diagnostic messages.
  const auto configured = qgetenv("PHOTO_EDITOR_DATABASE_URL");
  return QString::fromLatin1(QCryptographicHash::hash(
      configured.isNull() ? QByteArray("managed-catalog:v1") : QByteArray("explicit:") + configured,
      QCryptographicHash::Sha256).toHex());
}

bool EditorRecipeSaveJournal::open(const QString& app_data_root, const QString& authority,
                                 EditorRecipeSaveCheckpoint* recovered, QString* error) {
  if (error) error->clear();
  if (lock_ || app_data_root.isEmpty() || authority.isEmpty() || !recovered)
    return fail(error, QStringLiteral("invalid initialization"));
  const auto directory = QDir(app_data_root).filePath(QStringLiteral("recipe-outbox"));
  if (!QDir().mkpath(directory)) return fail(error, QStringLiteral("cannot create local recovery directory"));
  authority_ = authority;
  const auto namespace_hash = QString::fromLatin1(QCryptographicHash::hash(authority.toUtf8(), QCryptographicHash::Sha256).toHex());
  path_ = QDir(directory).filePath(namespace_hash + QStringLiteral(".json"));
  lock_ = std::make_unique<QLockFile>(path_ + QStringLiteral(".lock"));
  lock_->setStaleLockTime(0); // Do not expire a live editor's lock by age.
  if (!lock_->tryLock(0)) return fail(error, QStringLiteral("another editor owns this catalog's pending saves"));
  QFile input(path_);
  if (!input.exists()) { *recovered = {}; valid_ = true; return true; }
  if (!input.open(QIODevice::ReadOnly) || input.size() > kMaxJournalBytes)
    return fail(error, QStringLiteral("unreadable or oversized checkpoint; original retained"));
  const auto bytes = input.read(kMaxJournalBytes + 1);
  if (input.error() != QFileDevice::NoError || bytes.size() > kMaxJournalBytes)
    return fail(error, QStringLiteral("checkpoint read failed; original retained"));
  const auto document = QJsonDocument::fromJson(bytes);
  const auto envelope = document.object();
  const auto payload = envelope.value("payload").toObject();
  const bool legacy = payload.value("schema") == kLegacySchema;
  const bool previous_schema = payload.value("schema") == kPreviousSchema;
  const bool current_schema = payload.value("schema") == kSchema;
  const auto payload_bytes = QJsonDocument(payload).toJson(QJsonDocument::Compact);
  if (!document.isObject() || envelope.size() != 2 || !envelope.value("payload").isObject() ||
      envelope.value("sha256").toString().toLatin1() != checkpoint_sha256_hex(payload_bytes) ||
      payload.size() != (current_schema ? 6 : 5) ||
      (!legacy && !previous_schema && !current_schema) || payload.value("authority") != authority_ ||
      !payload.value("pending").isArray() || payload.value("pending").toArray().size() > kMaxPendingAssets)
    return fail(error, QStringLiteral("invalid checkpoint; original retained"));
  if (current_schema && (!payload.value("rejected").isArray() ||
      payload.value("rejected").toArray().size() > kMaxPendingAssets))
    return fail(error, QStringLiteral("invalid rejected recovery entries; original retained"));
  EditorRecipeSaveCheckpoint candidate;
  if (!decode(payload.value("active"), legacy, &candidate.active) ||
      !decode(payload.value("retryMigration"), legacy, &candidate.retry_migration) ||
      (candidate.active && candidate.retry_migration) ||
      (candidate.retry_migration && !candidate.retry_migration->migration_evidence.value("migrationPending").toBool()))
    return fail(error, QStringLiteral("invalid active save; original retained"));
  QSet<QString> ids;
  for (const auto& value : payload.value("pending").toArray()) {
    std::optional<EditorRecipeSaveSnapshot> snapshot;
    if (!decode(value, legacy, &snapshot) || !snapshot || ids.contains(snapshot->asset_id))
      return fail(error, QStringLiteral("invalid pending save; original retained"));
    ids.insert(snapshot->asset_id);
    candidate.pending.append(std::move(*snapshot));
  }
  if (current_schema) {
    QSet<QString> rejected_ids;
    for (const auto& value : payload.value("rejected").toArray()) {
      std::optional<EditorRecipeSaveSnapshot> snapshot;
      if (!decode(value, false, &snapshot) || !snapshot ||
          rejected_ids.contains(snapshot->asset_id))
        return fail(error, QStringLiteral("invalid rejected recovery entry; original retained"));
      rejected_ids.insert(snapshot->asset_id);
      candidate.rejected.append(std::move(*snapshot));
    }
  }
  *recovered = std::move(candidate);
  valid_ = true;
  return true;
}

bool EditorRecipeSaveJournal::write(const EditorRecipeSaveCheckpoint& checkpoint, QString* error) {
  JournalWriteProfile profile;
  if (error) error->clear();
  if (!isOpen()) return fail(error, QStringLiteral("checkpoint is not writable; existing recovery data retained"));
  if (checkpoint.pending.size() > kMaxPendingAssets) return fail(error, QStringLiteral("too many pending assets"));
  if (checkpoint.rejected.size() > kMaxPendingAssets) return fail(error, QStringLiteral("too many rejected recovery entries"));
  if (checkpoint.active && checkpoint.retry_migration)
    return fail(error, QStringLiteral("invalid active save ordering"));
  QSet<QString> ids;
  for (const auto& snapshot : checkpoint.pending) {
    if (snapshot.asset_id.isEmpty() || snapshot.file_path.isEmpty() || ids.contains(snapshot.asset_id))
      return fail(error, QStringLiteral("invalid pending asset identity"));
    ids.insert(snapshot.asset_id);
  }
  QSet<QString> rejected_ids;
  for (const auto& snapshot : checkpoint.rejected) {
    if (snapshot.asset_id.isEmpty() || snapshot.file_path.isEmpty() ||
        rejected_ids.contains(snapshot.asset_id))
      return fail(error, QStringLiteral("invalid rejected recovery identity"));
    rejected_ids.insert(snapshot.asset_id);
  }
  for (const auto* snapshot : {&checkpoint.active, &checkpoint.retry_migration}) {
    if (*snapshot && ((*snapshot)->asset_id.isEmpty() || (*snapshot)->file_path.isEmpty()))
      return fail(error, QStringLiteral("invalid active asset identity"));
  }
  QHash<QString, QList<SerializedSnapshot>> next_cache;
  qsizetype cached_bytes = 0;
  qsizetype cache_hits = 0;
  qsizetype cache_misses = 0;
  const auto json_bytes = [](const QJsonValue& value) {
    return compact_json_bytes(value, std::numeric_limits<qsizetype>::max());
  };
  const auto remember = [&](const QString& id, const QJsonValue& value, const QByteArray& bytes) {
    if (bytes.size() > kMaxCachedSerializationBytes - cached_bytes) return;
    auto& entries = next_cache[id];
    const auto duplicate = std::any_of(entries.cbegin(), entries.cend(), [&](const auto& entry) {
      return entry.value == value;
    });
    if (!duplicate) {
      entries.append({value, bytes});
      cached_bytes += bytes.size();
    }
  };
  const auto cached = [&](const QString& id, const QJsonValue& value) {
    for (const auto* cache : {&next_cache, &serialization_cache_}) {
      const auto found = cache->constFind(id);
      if (found == cache->cend()) continue;
      for (const auto& entry : found.value()) if (entry.value == value) return entry.bytes;
    }
    return QByteArray{};
  };
  const auto serialize_record = [&](const QString& id, const QJsonObject& object) {
    QByteArray record("{");
    bool first_field = true;
    for (auto field = object.constBegin(); field != object.constEnd(); ++field) {
      if (!first_field) record.append(',');
      first_field = false;
      record.append(json_bytes(field.key()));
      record.append(':');
      if (field.key() != QStringLiteral("recipe")) {
        record.append(json_bytes(field.value()));
        continue;
      }
      record.append('{');
      const auto recipe = field.value().toObject();
      bool first_recipe_field = true;
      for (auto part = recipe.constBegin(); part != recipe.constEnd(); ++part) {
        if (!first_recipe_field) record.append(',');
        first_recipe_field = false;
        record.append(json_bytes(part.key()));
        record.append(':');
        const auto reusable = part.key() == QStringLiteral("maskStack") ||
            part.key() == QStringLiteral("maskSets") || part.key() == QStringLiteral("retouchStack") ||
            part.key() == QStringLiteral("layers") || part.key() == QStringLiteral("photoLayers");
        auto bytes = reusable ? cached(id, part.value()) : QByteArray{};
        if (bytes.isEmpty()) bytes = json_bytes(part.value());
        // Exact encoded values protect tiny scalar changes and imported data.
        // Retain only large fragments; the shared budget includes whole records.
        if (reusable && bytes.size() >= 64 * 1024) remember(id, part.value(), bytes);
        record.append(bytes);
      }
      record.append('}');
    }
    record.append('}');
    return record;
  };
  const auto serialized = [&](const std::optional<EditorRecipeSaveSnapshot>& snapshot, bool retain) {
    if (!snapshot) return QByteArray("null");
    const auto value = encode(snapshot, &mask_encoding_cache_);
    // Compare exact encoded values: EditRecipeState::operator== is approximate.
    auto bytes = cached(snapshot->asset_id, value);
    if (bytes.isEmpty()) {
      bytes = compact_json_bytes(
          QJsonValue(value.toObject()), std::numeric_limits<qsizetype>::max());
      ++cache_misses;
    } else {
      ++cache_hits;
    }
    if (retain) remember(snapshot->asset_id, value, bytes);
    return bytes;
  };

  // Emit the same compact, alphabetically ordered object as QJsonDocument.
  // Each inserted record is complete JSON, never an unescaped recipe string.
  QByteArray payload_bytes;
  const auto append = [&](const QByteArray& bytes) {
    if (bytes.size() > kMaxJournalBytes - 90 - payload_bytes.size()) return false;
    payload_bytes.append(bytes);
    return true;
  };
  const auto append_string = [&](const QString& value) {
    const auto array = QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact);
    return append(array.mid(1, array.size() - 2));
  };
  const auto append_snapshots = [&](const QList<EditorRecipeSaveSnapshot>& snapshots, bool retain) {
    if (!append("[")) return false;
    bool first = true;
    for (const auto& snapshot : snapshots) {
      if ((!first && !append(",")) || !append(serialized(snapshot, retain))) return false;
      first = false;
    }
    return append("]");
  };
  bool encoded = false;
  if (!checkpoint.active && !checkpoint.retry_migration && checkpoint.rejected.isEmpty()) {
    // Fragment reuse helps repeated layer/filter or tone edits with an unchanged
    // matte. Changing geometry keeps the original single-buffer writer: retaining
    // both raster generations would inflate full-resolution brush memory.
    const auto reusable = !checkpoint.pending.isEmpty() &&
        std::all_of(checkpoint.pending.cbegin(), checkpoint.pending.cend(), [&](const auto& snapshot) {
          return !snapshot.state.mask_stack.isEmpty() && mask_encoding_cache_.find(snapshot.state.mask_stack);
        });
    if (!reusable) {
      for (auto it = serialization_cache_.begin(); it != serialization_cache_.end();) {
        auto& entries = it.value();
        entries.removeIf([](const auto& entry) { return entry.value.isArray(); });
        if (entries.isEmpty()) it = serialization_cache_.erase(it);
        else ++it;
      }
    }
    QJsonArray pending;
    for (const auto& snapshot : checkpoint.pending) pending.append(encode(snapshot, &mask_encoding_cache_));
    if (reusable) {
      encoded = append("{\"active\":null,\"authority\":") && append_string(authority_) && append(",\"pending\":[");
      for (qsizetype i = 0; encoded && i < pending.size(); ++i) {
        encoded = (i == 0 || append(",")) &&
            append(serialize_record(checkpoint.pending.at(i).asset_id, pending.at(i).toObject()));
      }
      encoded = encoded && append("],\"rejected\":[],\"retryMigration\":null,\"schema\":") &&
          append_string(kSchema) && append("}");
    } else {
      const QJsonObject payload{{"schema", kSchema}, {"authority", authority_}, {"pending", pending},
          {"active", QJsonValue(QJsonValue::Null)}, {"retryMigration", QJsonValue(QJsonValue::Null)},
          {"rejected", QJsonArray{}}};
      payload_bytes = compact_json_bytes(
          QJsonValue(payload), kMaxJournalBytes - 90);
      encoded = !payload_bytes.isEmpty() &&
          payload_bytes.size() <= kMaxJournalBytes - 90;
    }
    cache_misses = checkpoint.pending.size();
  } else {
    encoded = append("{\"active\":") && append(serialized(checkpoint.active, true)) &&
      append(",\"authority\":") && append_string(authority_) &&
      append(",\"pending\":") && append_snapshots(checkpoint.pending, false) &&
      append(",\"rejected\":") && append_snapshots(checkpoint.rejected, true) &&
      append(",\"retryMigration\":") && append(serialized(checkpoint.retry_migration, true)) &&
      append(",\"schema\":") && append_string(kSchema) && append("}");
  }
  profile.stage(QStringLiteral("records_and_payload_us"));
  if (!encoded) return fail(error, QStringLiteral("pending recipes exceed the 256 MiB checkpoint limit"));
  if (profile.enabled) {
    profile.stages.insert(QStringLiteral("serialized_cache_hits"), static_cast<qint64>(cache_hits));
    profile.stages.insert(QStringLiteral("serialized_cache_misses"), static_cast<qint64>(cache_misses));
    profile.stages.insert(QStringLiteral("serialized_cache_bytes"), static_cast<qint64>(cached_bytes));
    profile.stages.insert(QStringLiteral("payload_bytes"), static_cast<qint64>(payload_bytes.size()));
  }
  const auto digest = checkpoint_sha256_hex(payload_bytes);
  profile.stage(QStringLiteral("digest_us"));
  const QByteArray prefix("{\"payload\":");
  const auto suffix = QByteArray(",\"sha256\":\"") + digest + QByteArray("\"}");
  if (prefix.size() + payload_bytes.size() + suffix.size() > kMaxJournalBytes)
    return fail(error, QStringLiteral("pending recipes exceed the 256 MiB checkpoint limit"));
  QSaveFile output(path_);
  output.setDirectWriteFallback(false);
  if (!output.open(QIODevice::WriteOnly) ||
      !output.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner) ||
      output.write(prefix) != prefix.size() ||
      output.write(payload_bytes) != payload_bytes.size() ||
      output.write(suffix) != suffix.size() || !output.commit())
    return fail(error, QStringLiteral("atomic write failed; previous checkpoint retained"));
  // Publish retained record bytes only after a successful checkpoint. The mask
  // encoding cache is pure derived data and may warm even if the write fails.
  // A successful empty checkpoint releases both caches.
  serialization_cache_ = std::move(next_cache);
  if (!checkpoint.active && !checkpoint.retry_migration && checkpoint.pending.isEmpty() &&
      checkpoint.rejected.isEmpty()) mask_encoding_cache_.clear();
  profile.stage(QStringLiteral("atomic_write_us"));
  return true;
}

}  // namespace photo_editor::desktop
