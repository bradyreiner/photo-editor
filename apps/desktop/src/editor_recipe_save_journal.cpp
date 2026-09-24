#include "editor_recipe_save_journal.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDebug>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>
#include <array>
#include <limits>
#if defined(Q_OS_DARWIN) && !defined(PHOTO_EDITOR_TEST_PORTABLE_CHECKPOINT_SHA256)
#include <CommonCrypto/CommonDigest.h>
#endif

namespace photo_editor::desktop {
namespace {
constexpr qint64 kMaxJournalBytes = 256LL * 1024 * 1024;
constexpr qsizetype kMaxPendingAssets = 512;
const QString kSchema = QStringLiteral("photo-editor-recipe-outbox:v2");
const QString kLegacySchema = QStringLiteral("photo-editor-recipe-outbox:v1");

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

QJsonValue encode(const std::optional<EditorRecipeSaveSnapshot>& snapshot) {
  if (!snapshot) return QJsonValue(QJsonValue::Null);
  return QJsonObject{{"assetId", snapshot->asset_id}, {"filePath", snapshot->file_path},
      {"recipe", build_edit_recipe_object(snapshot->state)},
      {"migrationEvidence", QJsonObject::fromVariantMap(snapshot->migration_evidence)}};
}

bool decode(const QJsonValue& value, bool legacy, std::optional<EditorRecipeSaveSnapshot>* result) {
  if (value.isNull()) { result->reset(); return true; }
  if (!value.isObject()) return false;
  const auto object = value.toObject();
  if (object.size() != 4 || !object.value("assetId").isString() ||
      object.value("assetId").toString().isEmpty() || !object.value("filePath").isString() ||
      object.value("filePath").toString().isEmpty() ||
      (legacy ? !object.value("recipe").isString() : !object.value("recipe").isObject()) ||
      !object.value("migrationEvidence").isObject()) return false;
  const auto recipe = legacy ? QJsonDocument::fromJson(object.value("recipe").toString().toUtf8()).object()
                             : object.value("recipe").toObject();
  if (recipe.value("recipeVersion").toInt() != kCurrentEditRecipeVersion) return false;
  // The adjustment-object API deliberately rejects versioned recipe documents.
  // This boundary has already validated the exact current recipe version.
  auto adjustment = recipe;
  adjustment.remove(QStringLiteral("recipeVersion"));
  auto state = parse_edit_adjustment_state_object(adjustment);
  // Do not silently normalize/drop fields from an unsupported recovery format.
  if (build_edit_recipe_object(state) != recipe) return false;
  *result = EditorRecipeSaveSnapshot{.asset_id = object.value("assetId").toString(),
      .file_path = object.value("filePath").toString(), .state = std::move(state),
      .migration_evidence = object.value("migrationEvidence").toObject().toVariantMap()};
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
  const auto payload_bytes = QJsonDocument(payload).toJson(QJsonDocument::Compact);
  if (!document.isObject() || envelope.size() != 2 || !envelope.value("payload").isObject() ||
      envelope.value("sha256").toString().toLatin1() != checkpoint_sha256_hex(payload_bytes) ||
      payload.size() != 5 || (!legacy && payload.value("schema") != kSchema) || payload.value("authority") != authority_ ||
      !payload.value("pending").isArray() || payload.value("pending").toArray().size() > kMaxPendingAssets)
    return fail(error, QStringLiteral("invalid checkpoint; original retained"));
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
  *recovered = std::move(candidate);
  valid_ = true;
  return true;
}

bool EditorRecipeSaveJournal::write(const EditorRecipeSaveCheckpoint& checkpoint, QString* error) {
  JournalWriteProfile profile;
  if (error) error->clear();
  if (!isOpen()) return fail(error, QStringLiteral("checkpoint is not writable; existing recovery data retained"));
  if (checkpoint.pending.size() > kMaxPendingAssets) return fail(error, QStringLiteral("too many pending assets"));
  if (checkpoint.active && checkpoint.retry_migration)
    return fail(error, QStringLiteral("invalid active save ordering"));
  QSet<QString> ids;
  for (const auto& snapshot : checkpoint.pending) {
    if (snapshot.asset_id.isEmpty() || snapshot.file_path.isEmpty() || ids.contains(snapshot.asset_id))
      return fail(error, QStringLiteral("invalid pending asset identity"));
    ids.insert(snapshot.asset_id);
  }
  for (const auto* snapshot : {&checkpoint.active, &checkpoint.retry_migration}) {
    if (*snapshot && ((*snapshot)->asset_id.isEmpty() || (*snapshot)->file_path.isEmpty()))
      return fail(error, QStringLiteral("invalid active asset identity"));
  }
  QJsonArray pending;
  for (const auto& snapshot : checkpoint.pending) pending.append(encode(snapshot));
  const QJsonObject payload{{"schema", kSchema}, {"authority", authority_}, {"pending", pending},
      {"active", encode(checkpoint.active)}, {"retryMigration", encode(checkpoint.retry_migration)}};
  profile.stage(QStringLiteral("encode_us"));
  const auto payload_bytes = QJsonDocument(payload).toJson(QJsonDocument::Compact);
  profile.stage(QStringLiteral("payload_json_us"));
  if (payload_bytes.size() > kMaxJournalBytes - 90)
    return fail(error, QStringLiteral("pending recipes exceed the 256 MiB checkpoint limit"));
  const auto digest = checkpoint_sha256_hex(payload_bytes);
  profile.stage(QStringLiteral("digest_us"));
  // payload_bytes is already complete, escaped JSON; digest is ASCII hex.
  // Embed those exact bytes instead of traversing all raster strings again.
  QByteArray bytes;
  bytes.reserve(payload_bytes.size() + 90);
  bytes.append("{\"payload\":").append(payload_bytes).append(",\"sha256\":\"").append(digest).append("\"}");
  profile.stage(QStringLiteral("envelope_us"));
  if (bytes.size() > kMaxJournalBytes) return fail(error, QStringLiteral("pending recipes exceed the 256 MiB checkpoint limit"));
  QSaveFile output(path_);
  output.setDirectWriteFallback(false);
  if (!output.open(QIODevice::WriteOnly) ||
      !output.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner) ||
      output.write(bytes) != bytes.size() || !output.commit())
    return fail(error, QStringLiteral("atomic write failed; previous checkpoint retained"));
  profile.stage(QStringLiteral("atomic_write_us"));
  return true;
}

}  // namespace photo_editor::desktop
