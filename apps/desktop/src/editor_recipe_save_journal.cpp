#include "editor_recipe_save_journal.h"
#include "editor_recipe_journal_delta.h"
#if defined(Q_OS_UNIX)
#include <unistd.h>
#include <fcntl.h>
#endif
#include <QAnyStringView>

#include <QCryptographicHash>
#include <QDir>
#include <QDebug>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QUuid>
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
                  EditRecipeMaskEncodingCache* cache, QString* error) {
  if (!snapshot) return QJsonValue(QJsonValue::Null);
  const auto recipe = build_edit_recipe_object_with_mask_cache(snapshot->state, cache);
  if (recipe.contains(QStringLiteral("componentObjectTableError"))) {
    if (error) *error = recipe.value(QStringLiteral("componentObjectTableError")).toString();
    return QJsonValue(QJsonValue::Undefined);
  }
  return QJsonObject{{"assetId", snapshot->asset_id}, {"filePath", snapshot->file_path},
      {"recipe", recipe},
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
  if (!is_supported_structured_edit_recipe_version(version)) return false;
  auto state = parse_edit_recipe_json(QString::fromUtf8(
      QJsonDocument(recipe).toJson(QJsonDocument::Compact)));
  // Do not silently normalize/drop fields from an unsupported recovery format.
  auto rebuilt = version < kPhotoComponentObjectTableEditRecipeVersion
      ? build_edit_adjustment_state_object(state) : build_edit_recipe_object(state);
  rebuilt.insert(QStringLiteral("recipeVersion"), version);
  if (rebuilt != recipe) return false;
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
  retryable_lock_conflict_ = false;
  if (!lock_->tryLock(0)) {
    const auto lock_error = lock_->error();
    qint64 owner_pid = 0;
    QString owner_host, owner_application;
    const auto owner_known = lock_->getLockInfo(&owner_pid, &owner_host, &owner_application);
    retryable_lock_conflict_ = lock_error == QLockFile::LockFailedError;
    lock_.reset(); // A failed acquisition must not make this object permanently unusable.
    if (retryable_lock_conflict_) {
      return fail(error, owner_known && owner_pid > 0
          ? QStringLiteral("Another Looksy window is using this catalog (process %1). Close that window normally, then choose Retry close. Your edits are still in this window.").arg(owner_pid)
          : QStringLiteral("Another Looksy window is using this catalog. Close that window normally, then choose Retry close. Your edits are still in this window."));
    }
    return fail(error, QStringLiteral("cannot acquire the local recovery lock; check the recovery folder's permissions and available storage"));
  }
  QFile input(path_);
  if (!input.exists()) { *recovered = {}; valid_ = true; return true; }
  if (!input.open(QIODevice::ReadOnly) || input.size() > kMaxJournalBytes)
    return fail(error, QStringLiteral("unreadable or oversized checkpoint; original retained"));
  const auto bytes = input.read(kMaxJournalBytes + 1);
  if (input.error() != QFileDevice::NoError || bytes.size() > kMaxJournalBytes)
    return fail(error, QStringLiteral("checkpoint read failed; original retained"));
  // Legacy files remain a single JSON envelope. New files append newline-
  // terminated checksummed edits; a torn last record was never acknowledged.
  auto document = QJsonDocument::fromJson(bytes);
  qint64 valid_bytes = bytes.size();
  int record_count = 0;
  QByteArray base_bytes = bytes;
  if (document.isNull()) {
    const auto first_newline = bytes.indexOf('\n');
    if (first_newline < 0) return fail(error, QStringLiteral("invalid checkpoint; original retained"));
    base_bytes = bytes.first(first_newline);
    document = QJsonDocument::fromJson(base_bytes);
    valid_bytes = first_newline + 1;
  }
  const auto envelope = document.object();
  auto payload = envelope.value("payload").toObject();
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
  auto chain_digest = envelope.value("sha256").toString().toLatin1();
  if (base_bytes.size() != bytes.size()) {
    auto cursor = base_bytes.size() + 1;
    while (cursor < bytes.size()) {
      const auto end = bytes.indexOf('\n', cursor);
      if (end < 0) break;  // Ignore only an incomplete final record.
      const auto commit_end = bytes.indexOf('\n', end + 1);
      if (commit_end < 0) break;
      const auto record_document = QJsonDocument::fromJson(bytes.mid(cursor, end - cursor));
      const auto record = record_document.object();
      const auto body = record.value("record").toObject();
      const auto body_bytes = QJsonDocument(body).toJson(QJsonDocument::Compact);
      const auto digest = checkpoint_sha256_hex(body_bytes);
      QJsonValue next(payload);
      if (!record_document.isObject() || record.size() != 2 || body.size() != 3 ||
          body.value("schema") != QStringLiteral("photo-editor-recipe-delta:v1") ||
          body.value("previous").toString().toLatin1() != chain_digest ||
          record.value("sha256").toString().toLatin1() != digest ||
          !body.value("edit").isObject() ||
          !recipe_journal_delta::apply(&next, body.value("edit").toObject()) || !next.isObject())
        return fail(error, QStringLiteral("invalid edit record; original retained"));
      if (bytes.mid(end + 1, commit_end - end - 1) != QByteArray("commit:") + digest) {
        // The body was flushed before its fence was issued. A power loss can
        // tear that final fence even when its newline reaches storage. Keep
        // the preceding committed head; a damaged fence in the middle of a
        // chain is corruption and must remain a visible recovery failure.
        if (commit_end + 1 == bytes.size()) break;
        return fail(error, QStringLiteral("invalid edit commit fence; original retained"));
      }
      payload = next.toObject();
      chain_digest = digest;
      if (++record_count > 128)
        return fail(error, QStringLiteral("too many edit records; original retained"));
      cursor = commit_end + 1;
      valid_bytes = cursor;
    }
    if (!current_schema || payload.size() != 6 || payload.value("schema") != kSchema ||
        payload.value("authority") != authority_ || !payload.value("pending").isArray() ||
        payload.value("pending").toArray().size() > kMaxPendingAssets)
      return fail(error, QStringLiteral("invalid edited checkpoint; original retained"));
  }
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
  // Recovery already materialized the queue. Do not retain a second full JSON
  // tree beside its decoded raster state. The first write compacts this head;
  // subsequent writes can append edits to the writer-owned shared JSON tree.
  // This also normalizes legacy human-formatted (multi-line) envelopes before
  // introducing newline-delimited records.
  durable_payload_ = {};
  durable_digest_ = chain_digest;
  durable_bytes_ = valid_bytes;
  delta_records_ = record_count;
  valid_ = true;
  return true;
}

bool EditorRecipeSaveJournal::archiveRecoverySnapshot(
    const EditorRecipeSaveSnapshot& snapshot, QString* archive_path, QString* error) const {
  if (archive_path) archive_path->clear();
  if (!isOpen()) return fail(error, QStringLiteral("recovery journal is not writable"));
  const auto parent = QFileInfo(path_).absolutePath();
  const auto root = QDir(parent).filePath(
      QStringLiteral("archive-") + QUuid::createUuid().toString(QUuid::WithoutBraces));
  EditorRecipeSaveJournal archive;
  EditorRecipeSaveCheckpoint checkpoint;
  if (!archive.open(root, authority_, &checkpoint, error)) return false;
  checkpoint.rejected.append(snapshot);
  if (!archive.write(checkpoint, error)) return false;
#if defined(Q_OS_UNIX)
  // write() synced the archive file's directory. Persist both newly created
  // ancestor entries before the caller removes the live recovery record.
  for (const auto& directory_path : {root, parent}) {
    const auto bytes = QFile::encodeName(directory_path);
    const int directory = ::open(bytes.constData(), O_RDONLY);
    const bool synced = directory >= 0 && ::fsync(directory) == 0;
    if (directory >= 0) ::close(directory);
    if (!synced) return fail(error, QStringLiteral("archive directory sync failed; live recovery retained"));
  }
#endif
  if (archive_path) *archive_path = archive.path();
  return true;
}

bool EditorRecipeSaveJournal::write(const EditorRecipeSaveCheckpoint& checkpoint, QString* error) {
  JournalWriteProfile profile;
  QString encoding_error;
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
  const auto make_payload = [&]() {
    QJsonArray pending, rejected;
    for (const auto& snapshot : checkpoint.pending)
      pending.append(encode(snapshot, &mask_encoding_cache_, &encoding_error));
    for (const auto& snapshot : checkpoint.rejected)
      rejected.append(encode(snapshot, &mask_encoding_cache_, &encoding_error));
    return QJsonObject{{"schema", kSchema}, {"authority", authority_}, {"pending", pending},
        {"rejected", rejected}, {"active", encode(checkpoint.active, &mask_encoding_cache_, &encoding_error)},
        {"retryMigration", encode(checkpoint.retry_migration, &mask_encoding_cache_, &encoding_error)}};
  };
  // A changed raster already needs a full compaction. Drop the old decoded
  // tree before encoding its replacement instead of retaining two generations
  // and constructing an edit that would be rejected for its large payload.
  const auto masks_reusable = [&](const EditorRecipeSaveSnapshot& snapshot) {
    return snapshot.state.mask_stack.isEmpty() || mask_encoding_cache_.find(snapshot.state.mask_stack);
  };
  const bool reusable_masks =
      (!checkpoint.active || masks_reusable(*checkpoint.active)) &&
      (!checkpoint.retry_migration || masks_reusable(*checkpoint.retry_migration)) &&
      std::all_of(checkpoint.pending.cbegin(), checkpoint.pending.cend(), masks_reusable) &&
      std::all_of(checkpoint.rejected.cbegin(), checkpoint.rejected.cend(), masks_reusable);
  if (!reusable_masks) durable_payload_ = {};
  QJsonObject candidate_payload;
  const bool empty = checkpoint.pending.isEmpty() && checkpoint.rejected.isEmpty() &&
      !checkpoint.active && !checkpoint.retry_migration;
#if defined(Q_OS_UNIX)
  constexpr bool durable_append_supported = true;
#else
  constexpr bool durable_append_supported = false;
#endif
  if (durable_append_supported && !durable_payload_.isEmpty() && !empty && delta_records_ < 128) {
    candidate_payload = make_payload();
    if (!encoding_error.isEmpty()) return fail(error, encoding_error);
    bool large_edit = false;
    const auto edit = recipe_journal_delta::between(durable_payload_, candidate_payload, 0, &large_edit);
    if (!large_edit && edit.isEmpty()) {
      if (QFileInfo(path_).size() < durable_bytes_)
        return fail(error, QStringLiteral("durable checkpoint is missing; recovery data retained in memory"));
      return true;  // This exact revision is already durable.
    }
    if (!large_edit) {
      const QJsonObject body{{"schema", "photo-editor-recipe-delta:v1"},
          {"previous", QString::fromLatin1(durable_digest_)}, {"edit", edit}};
      const auto body_bytes = compact_json_bytes(body, kMaxJournalBytes);
      const auto digest = checkpoint_sha256_hex(body_bytes);
      const auto record = compact_json_bytes(QJsonObject{{"record", body},
          {"sha256", QString::fromLatin1(digest)}}, kMaxJournalBytes);
      // Compact large edits rather than keeping multiple raster generations.
      // Typed edit paths add nesting: fall back if an otherwise valid deep
      // evidence object would exceed the recovery parser's depth limit.
      if (!record.isEmpty() && record.size() < durable_bytes_ / 4 &&
          record.size() + durable_bytes_ + 74 <= kMaxJournalBytes &&
          QJsonDocument::fromJson(record).isObject()) {
        QFile output(path_);
        if (!output.open(QIODevice::ReadWrite))
          return fail(error, QStringLiteral("edit append failed; previous checkpoint retained"));
        const auto existing_bytes = output.size();
        if (existing_bytes < durable_bytes_ || !output.resize(durable_bytes_) ||
            !output.seek(durable_bytes_))
          return fail(error, QStringLiteral("edit append failed; previous checkpoint retained"));
#if defined(Q_OS_UNIX)
        // A failed earlier append can leave a complete but unacknowledged tail.
        // Persist its truncation before overwriting any bytes of that tail, so
        // a power loss cannot pair a partial new body with an old commit fence.
        if (existing_bytes > durable_bytes_ &&
            (!output.flush() || ::fsync(output.handle()) != 0))
          return fail(error, QStringLiteral("edit tail cleanup failed; previous committed head retained"));
#endif
        const auto separator = delta_records_ == 0 && durable_bytes_ > 0 &&
            output.seek(durable_bytes_ - 1) && output.read(1) != QByteArray("\n")
                ? QByteArray("\n") : QByteArray{};
        const auto body_record = separator + record + '\n';
        const auto commit_record = QByteArray("commit:") + digest + '\n';
        const auto bytes = body_record + commit_record;
        bool saved = output.seek(durable_bytes_) && output.write(body_record) == body_record.size() && output.flush();
#if defined(Q_OS_UNIX)
        // Persist the body before issuing its commit fence. An unsuccessful
        // body write may leave any suffix; without a committed fence recovery
        // can still use the preceding acknowledged head.
        if (saved) saved = ::fsync(output.handle()) == 0;
        if (saved) saved = output.write(commit_record) == commit_record.size() && output.flush();
        if (saved) saved = ::fsync(output.handle()) == 0;
#else
        // Platforms without a native durable append use atomic compaction below.
        saved = false;
#endif
        if (!saved) {
          const bool rolled_back = output.resize(durable_bytes_) && output.flush();
#if defined(Q_OS_UNIX)
          const bool rollback_durable = rolled_back && ::fsync(output.handle()) == 0;
#else
          const bool rollback_durable = rolled_back;
#endif
          return fail(error, rollback_durable
              ? QStringLiteral("durable edit append failed; previous checkpoint retained")
              : QStringLiteral("durable edit append failed; incomplete tail retained after the previous committed head"));
        }
        durable_payload_ = std::move(candidate_payload);
        durable_digest_ = digest;
        durable_bytes_ += bytes.size();
        ++delta_records_;
        if (profile.enabled) profile.stages.insert("edit_record_bytes", bytes.size());
        profile.stage(QStringLiteral("durable_edit_us"));
        return true;
      }
    }
  }
  if (candidate_payload.isEmpty()) candidate_payload = make_payload();
  // Compaction no longer needs the previous decoded head. Release it before
  // allocating the full serialized checkpoint, especially for a new raster.
  // A write failure still leaves the old durable file available for recovery;
  // the next write simply performs another atomic compaction.
  durable_payload_ = {};
  const auto payload_bytes = compact_json_bytes(candidate_payload, kMaxJournalBytes - 90);
  const bool encoded = !payload_bytes.isEmpty();
  profile.stage(QStringLiteral("records_and_payload_us"));
  if (!encoding_error.isEmpty())
    return fail(error, QStringLiteral("%1; previous checkpoint retained").arg(encoding_error));
  if (!encoded) return fail(error, QStringLiteral("pending recipes exceed the 256 MiB checkpoint limit"));
  if (profile.enabled) {
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
#if defined(Q_OS_UNIX)
  const auto directory_bytes = QFile::encodeName(QFileInfo(path_).absolutePath());
  const int directory = ::open(directory_bytes.constData(), O_RDONLY);
  const bool directory_synced = directory >= 0 && ::fsync(directory) == 0;
  if (directory >= 0) ::close(directory);
  if (!directory_synced) {
    durable_payload_ = {};
    return fail(error, QStringLiteral("checkpoint directory sync failed; retry saving"));
  }
#endif
  durable_payload_ = std::move(candidate_payload);
  durable_digest_ = digest;
  durable_bytes_ = prefix.size() + payload_bytes.size() + suffix.size();
  delta_records_ = 0;
  // Publish the new recovery head only after atomic replacement succeeds.
  if (!checkpoint.active && !checkpoint.retry_migration && checkpoint.pending.isEmpty() &&
      checkpoint.rejected.isEmpty()) mask_encoding_cache_.clear();
  profile.stage(QStringLiteral("atomic_write_us"));
  return true;
}

}  // namespace photo_editor::desktop
