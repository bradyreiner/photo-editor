#include "editor_recipe_save_journal.h"
#include "edit_recipe_mask_schema.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QCryptographicHash>
#include <QFile>
#include <QDir>
#include <QProcess>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QUrl>
#include <iostream>

using namespace photo_editor::desktop;
namespace {
bool require(bool condition, const char* message) {
  if (!condition) std::cerr << message << '\n';
  return condition;
}
QByteArray read(const QString& path) {
  QFile input(path);
  if (!input.open(QIODevice::ReadOnly)) return {};
  return input.readAll();
}
bool independent_envelope_match(const QByteArray& bytes) {
  const auto document = QJsonDocument::fromJson(bytes);
  const auto envelope = document.object();
  const auto payload = QJsonDocument(envelope.value("payload").toObject()).toJson(QJsonDocument::Compact);
  return document.isObject() && document.toJson(QJsonDocument::Compact) == bytes &&
      envelope.value("sha256").toString().toLatin1() ==
          QCryptographicHash::hash(payload, QCryptographicHash::Sha256).toHex();
}
bool legacy_checkpoint_roundtrip(const QByteArray& current_bytes, bool string_recipes) {
  const auto original = QJsonDocument::fromJson(current_bytes).object().value("payload").toObject();
  auto legacy = original;
  // v1/v2 predate rejected records. Construct an actual old-format payload,
  // rather than changing only the schema label on a six-field v3 payload.
  if (!legacy.value("rejected").toArray().isEmpty()) return false;
  legacy.remove("rejected");
  legacy.insert("schema", string_recipes ? "photo-editor-recipe-outbox:v1" : "photo-editor-recipe-outbox:v2");
  const auto legacy_snapshot = [string_recipes](const QJsonValue& value) -> QJsonValue {
    if (value.isNull()) return value;
    auto object = value.toObject();
    object.remove("agentTaskId");
    if (string_recipes)
      object.insert("recipe", QString::fromUtf8(QJsonDocument(object.value("recipe").toObject()).toJson(QJsonDocument::Compact)));
    return object;
  };
  QJsonArray pending;
  for (const auto& value : legacy.value("pending").toArray()) pending.append(legacy_snapshot(value));
  legacy.insert("pending", pending);
  legacy.insert("active", legacy_snapshot(legacy.value("active")));
  legacy.insert("retryMigration", legacy_snapshot(legacy.value("retryMigration")));
  const auto payload_bytes = QJsonDocument(legacy).toJson(QJsonDocument::Compact);
  const auto bytes = QJsonDocument(QJsonObject{{"payload", legacy}, {"sha256", QString::fromLatin1(
      QCryptographicHash::hash(payload_bytes, QCryptographicHash::Sha256).toHex())}}).toJson(QJsonDocument::Compact);
  QTemporaryDir root;
  QString path, error;
  {
    EditorRecipeSaveJournal journal;
    EditorRecipeSaveCheckpoint recovered;
    if (!journal.open(root.path(), "catalog-a", &recovered, &error)) return false;
    path = journal.path();
  }
  {
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) return false;
  }
  {
    EditorRecipeSaveJournal journal;
    EditorRecipeSaveCheckpoint recovered;
    if (!journal.open(root.path(), "catalog-a", &recovered, &error) || !journal.write(recovered, &error)) return false;
    const auto upgraded = read(path);
    if (!independent_envelope_match(upgraded) ||
        QJsonDocument::fromJson(upgraded).object().value("payload").toObject() != original) return false;
  }
  EditorRecipeSaveJournal final;
  EditorRecipeSaveCheckpoint recovered;
  return final.open(root.path(), "catalog-a", &recovered, &error) && recovered.active && recovered.pending.size() == 2;
}

QByteArray reference_checkpoint_bytes(const EditorRecipeSaveCheckpoint& checkpoint, const QString& authority) {
  const auto record = [](const std::optional<EditorRecipeSaveSnapshot>& snapshot) -> QJsonValue {
    if (!snapshot) return QJsonValue(QJsonValue::Null);
    return QJsonObject{{"assetId", snapshot->asset_id}, {"filePath", snapshot->file_path},
        {"recipe", build_edit_recipe_object(snapshot->state)},
        {"migrationEvidence", QJsonObject::fromVariantMap(snapshot->migration_evidence)},
        {"agentTaskId", snapshot->agent_task_id.isEmpty() ? QStringLiteral("") : snapshot->agent_task_id}};
  };
  QJsonArray pending, rejected;
  for (const auto& snapshot : checkpoint.pending) pending.append(record(snapshot));
  for (const auto& snapshot : checkpoint.rejected) rejected.append(record(snapshot));
  const QJsonObject payload{{"schema", "photo-editor-recipe-outbox:v3"}, {"authority", authority},
      {"pending", pending}, {"rejected", rejected}, {"active", record(checkpoint.active)},
      {"retryMigration", record(checkpoint.retry_migration)}};
  const auto payload_bytes = QJsonDocument(payload).toJson(QJsonDocument::Compact);
  return QJsonDocument(QJsonObject{{"payload", payload}, {"sha256", QString::fromLatin1(
      QCryptographicHash::hash(payload_bytes, QCryptographicHash::Sha256).toHex())}})
      .toJson(QJsonDocument::Compact);
}

bool large_string_checkpoint_contracts() {
  QTemporaryDir root;
  if (!root.isValid()) return false;
  QString error, path;
  EditorRecipeSaveCheckpoint checkpoint;
  EditorRecipeSaveSnapshot snapshot{.asset_id = "large-string", .file_path = "/cutout.png"};
  const auto encoded = QString::fromLatin1(QByteArray(64 * 1024, 'x').toBase64());
  snapshot.state.layers = {QVariantMap{{"id", "cutout"}, {"kind", "pixel"},
      {"paintTiles", QVariantMap{{"version", 1}, {"tiles", QVariantMap{{"0,0", encoded}}}}}}};
  snapshot.migration_evidence["nested"] = QVariantList{encoded, QString{}, QVariantMap{{"encoded", encoded}}};
  checkpoint.pending.append(snapshot);
  {
    EditorRecipeSaveJournal journal;
    EditorRecipeSaveCheckpoint recovered;
    if (!journal.open(root.path(), "large-string-catalog", &recovered, &error)) return false;
    path = journal.path();
    for (const auto& suffix : {QString{}, QStringLiteral("=inside"),
         QString::fromUtf8("\"\\\nλ-雪") + QChar(0), QString(1, QChar(0x100))}) {
      checkpoint.pending[0].migration_evidence["longText"] = encoded + suffix;
      if (!journal.write(checkpoint, &error) ||
          read(path) != reference_checkpoint_bytes(checkpoint, "large-string-catalog")) return false;
    }
    auto layer = checkpoint.pending[0].state.layers[0].toMap();
    auto paint = layer.value("paintTiles").toMap();
    auto tiles = paint.value("tiles").toMap();
    auto changed = tiles.value("0,0").toString(); changed[5] = 'A';
    tiles["0,0"] = changed; paint["tiles"] = tiles; layer["paintTiles"] = paint;
    checkpoint.pending[0].state.layers[0] = layer;
    if (!journal.write(checkpoint, &error) ||
        read(path) != reference_checkpoint_bytes(checkpoint, "large-string-catalog")) return false;
  }
  EditorRecipeSaveJournal reopened;
  EditorRecipeSaveCheckpoint recovered;
  return reopened.open(root.path(), "large-string-catalog", &recovered, &error) &&
      recovered.pending.size() == 1 &&
      recovered.pending[0].state.layers == checkpoint.pending[0].state.layers &&
      recovered.pending[0].migration_evidence == checkpoint.pending[0].migration_evidence;
}

bool cached_records_roundtrip() {
  QTemporaryDir root;
  if (!root.isValid()) return false;
  const auto authority = QString::fromUtf8("catalog-λ-\"-\\-\n") + QChar(0);
  QString error, path;
  EditorRecipeSaveCheckpoint checkpoint;
  EditorRecipeSaveSnapshot active{.asset_id = "active", .file_path = "/active.dng"};
  active.state.exposure = 0.125;
  active.agent_task_id = QStringLiteral("owned-task-a");
  auto raster = default_mask_definition("brush");
  raster["id"] = "journal-cache-mask";
  raster["segmentationWidth"] = 512;
  raster["segmentationHeight"] = 512;
  raster["segmentationData"] = QByteArray(512 * 512, 'a');
  active.state.mask_stack = {raster};
  active.migration_evidence.insert("migrationPending", true);
  active.migration_evidence.insert("proof", QString::fromUtf8("λ-雪-\"-\\-\n") + QChar(0));
  checkpoint.active = active;
  auto pending = active;
  pending.agent_task_id = QStringLiteral("owned-task-b");
  pending.state.exposure = 0.5;
  pending.migration_evidence.clear();
  checkpoint.pending.append(pending);
  auto rejected = pending;
  rejected.asset_id = "rejected";
  rejected.file_path = "/rejected.dng";
  rejected.migration_evidence.insert("largeProof", QString(2 * 1024 * 1024, QChar('x')));
  checkpoint.rejected.append(rejected);
  {
    EditorRecipeSaveJournal journal;
    EditorRecipeSaveCheckpoint recovered;
    if (!journal.open(root.path(), authority, &recovered, &error)) return false;
    path = journal.path();
    const auto write_exact = [&]() {
      return journal.write(checkpoint, &error) &&
          read(path) == reference_checkpoint_bytes(checkpoint, authority) &&
          independent_envelope_match(read(path));
    };
    for (int i = 0; i < 4; ++i) {
      checkpoint.pending.first().state.exposure += 0.125;
      if (!write_exact()) return false;
    }
    // These changes compare equal under EditRecipeState's tolerance but must
    // remain distinct in recovery bytes, including a previously cached record.
    checkpoint.rejected.first().state.exposure += 1e-10;
    if (!write_exact()) return false;
    checkpoint.rejected.first().file_path = QString::fromUtf8("/renamed-雪.dng");
    checkpoint.rejected.first().migration_evidence.insert("proofRevision", 2);
    if (!write_exact()) return false;
    checkpoint.retry_migration = checkpoint.active;
    checkpoint.active.reset();
    if (!write_exact()) return false;
    checkpoint.retry_migration->migration_evidence.insert("proofRevision", 3);
    if (!write_exact()) return false;

    // Exercise fragment reuse in ordinary pending-only edits, then invalidate
    // geometry and scalar fields independently before restoring queue roles.
    const auto role_checkpoint = checkpoint;
    checkpoint.active.reset();
    checkpoint.retry_migration.reset();
    checkpoint.rejected.clear();
    for (int i = 0; i < 3; ++i) {
      checkpoint.pending.first().state.exposure += 1e-10;
      if (!write_exact()) return false;
    }
    checkpoint.pending.first().state.layers = {QVariantMap{{"id", "delete-with-large-matte"},
        {"kind", "adjustment"}, {"name", "Exposure"},
        {"effectStack", QVariantMap{{"version", 1}, {"items", QVariantList{}}}}}};
    if (!write_exact()) return false;
    checkpoint.pending.first().state.layers.first() = QVariantMap{{"id", "delete-with-large-matte"},
        {"kind", "adjustment"}, {"name", "Exposure"}};
    if (!write_exact()) return false;
    checkpoint.pending.first().state.layers.clear();
    if (!write_exact()) return false;
    auto changed_mask = checkpoint.pending.first().state.mask_stack.first().toMap();
    changed_mask["segmentationData"] = QByteArray(512 * 512, 'c');
    checkpoint.pending.first().state.mask_stack = {changed_mask};
    if (!write_exact()) return false;
    changed_mask["name"] = QString::fromUtf8("Renamed λ-雪-\"-\\-\n");
    checkpoint.pending.first().state.mask_stack = {changed_mask};
    if (!write_exact()) return false;
    checkpoint = role_checkpoint;
    if (!write_exact()) return false;
    const auto before = read(path);
    const auto directory = QFileInfo(path).absolutePath();
    const auto held = directory + "-held";
    if (!QDir().rename(directory, held)) return false;
    checkpoint.rejected.first().state.exposure += 0.25;
    raster["segmentationData"] = QByteArray(512 * 512, 'b');
    checkpoint.rejected.first().state.mask_stack = {raster};
    const auto refused = !journal.write(checkpoint, &error) && !error.isEmpty() &&
        read(QDir(held).filePath(QFileInfo(path).fileName())) == before;
    if (!QDir().rename(held, directory) || !refused || !write_exact()) return false;
  }
  {
    EditorRecipeSaveJournal reopened;
    EditorRecipeSaveCheckpoint recovered;
    if (!reopened.open(root.path(), authority, &recovered, &error) ||
        reference_checkpoint_bytes(recovered, authority) != reference_checkpoint_bytes(checkpoint, authority) ||
        !reopened.write({}, &error) || read(path) != reference_checkpoint_bytes({}, authority)) return false;
  }
  EditorRecipeSaveJournal empty;
  EditorRecipeSaveCheckpoint recovered;
  return empty.open(root.path(), authority, &recovered, &error) && recovered.pending.isEmpty() &&
      recovered.rejected.isEmpty() && !recovered.active && !recovered.retry_migration;
}

bool supported_recipe_versions_roundtrip() {
  for (const auto& key : {QString{}, QStringLiteral("effectStack"),
                         QStringLiteral("textTypography"), QStringLiteral("paintTiles")}) {
    QTemporaryDir root;
    EditorRecipeSaveSnapshot snapshot{.asset_id = "versioned-photo", .file_path = "/photo.dng"};
    if (!key.isEmpty()) {
      // Layer payloads are opaque to the journal; exercise each writer-selected
      // envelope, including content nested inside a group.
      const QVariantMap child{{"id", "child"}, {"kind", "pixel"},
          {key, QVariantMap{{"version", 1}, {"items", QVariantList{}}}}};
      snapshot.state.layers = {QVariantMap{{"id", "group"}, {"kind", "group"},
          {"children", QVariantList{child}}}};
    }
    const auto expected = build_edit_recipe_object(snapshot.state);
    const int version = key.isEmpty() ? kCurrentEditRecipeVersion : key == "effectStack"
        ? kPhotoEffectsEditRecipeVersion : key == "textTypography"
        ? kPhotoTypographyEditRecipeVersion : kPhotoPaintEditRecipeVersion;
    if (expected.value("recipeVersion").toInt() != version) return false;
    QString error;
    EditorRecipeSaveCheckpoint checkpoint;
    checkpoint.pending.append(snapshot);
    checkpoint.active = snapshot;
    {
      EditorRecipeSaveJournal journal;
      EditorRecipeSaveCheckpoint recovered;
      if (!journal.open(root.path(), "catalog-a", &recovered, &error) ||
          !journal.write(checkpoint, &error)) return false;
    }
    EditorRecipeSaveJournal reopened;
    EditorRecipeSaveCheckpoint recovered;
    if (!reopened.open(root.path(), "catalog-a", &recovered, &error) ||
        !recovered.active || recovered.pending.size() != 1 ||
        build_edit_recipe_object(recovered.active->state) != expected ||
        build_edit_recipe_object(recovered.pending.first().state) != expected) return false;
  }
  return true;
}
bool mask_encoding_cache_contracts() {
  EditRecipeMaskEncodingCache cache;
  EditRecipeState state;
  auto mask = default_mask_definition("brush");
  mask["id"] = "cache-contract-mask";
  mask["segmentationWidth"] = 32;
  mask["segmentationHeight"] = 32;
  mask["segmentationData"] = QByteArray(1024, 'a');
  state.mask_stack = {mask};
  const auto exact = [&] {
    const auto cached = QJsonDocument(build_edit_recipe_object_with_mask_cache(state, &cache))
        .toJson(QJsonDocument::Compact);
    return cached == QJsonDocument(build_edit_recipe_object(state)).toJson(QJsonDocument::Compact);
  };
  if (!exact() || !cache.find(state.mask_stack)) { std::cerr << "Cache contract line " << __LINE__ << " failed\n"; return false; }
  state.exposure = 0.123;
  if (!cache.find(state.mask_stack) || !exact()) { std::cerr << "Cache contract line " << __LINE__ << " failed\n"; return false; }
  for (const QVariant& value : {QVariant(0.1), QVariant(0.1000000000000001), QVariant("0.1")}) {
    mask["exposure"] = value;
    state.mask_stack = {mask};
    if (cache.find(state.mask_stack) || !exact()) { std::cerr << "Cache contract line " << __LINE__ << " failed\n"; return false; }
  }
  auto second = mask;
  second["id"] = "second-mask";
  state.mask_stack = {mask, second};
  if (!exact()) { std::cerr << "Cache contract line " << __LINE__ << " failed\n"; return false; }
  state.mask_stack = {second, mask};
  if (cache.find(state.mask_stack) || !exact()) { std::cerr << "Cache contract line " << __LINE__ << " failed\n"; return false; }
  // A raw-data wrapper can change behind QVariant's implicit sharing. The key
  // must own its old bytes, rather than mistaking that mutation for a hit.
  QByteArray raw(1024, 'b');
  mask["segmentationData"] = QByteArray::fromRawData(raw.constData(), raw.size());
  state.mask_stack = {mask};
  if (!exact()) { std::cerr << "Cache contract line " << __LINE__ << " failed\n"; return false; }
  raw[10] = 'c';
  if (cache.find(state.mask_stack) || !exact()) { std::cerr << "Cache contract line " << __LINE__ << " failed\n"; return false; }
  mask["segmentationData"] = QByteArray(32 * 1024 * 1024, 'a');
  cache.store(QVariantList{mask}, {});
  if (cache.find(QVariantList{mask})) { std::cerr << "Cache contract line " << __LINE__ << " failed\n"; return false; }
  // Unknown types are a miss, even if their equality could coerce a value.
  mask["segmentationData"] = QByteArray(1024, 'a');
  mask["unknownExtension"] = QUrl("file:///cache-contract");
  state.mask_stack = {mask};
  if (!exact() || cache.find(state.mask_stack)) { std::cerr << "Cache contract line " << __LINE__ << " failed\n"; return false; }
  state.mask_stack.clear();
  state.linear_mask_enabled = true;
  const auto legacy = build_edit_recipe_object_with_mask_cache(state, &cache);
  if (cache.find(state.mask_stack) || legacy.value("maskStack").toArray().size() != 1) return false;
  state.linear_mask_exposure = 0.67;
  const auto changed_legacy = build_edit_recipe_object_with_mask_cache(state, &cache);
  if (cache.find(state.mask_stack) ||
      changed_legacy.value("maskStack").toArray().first().toObject()
          .value("adjustments").toObject().value("exposure").toDouble() != 0.67) return false;
  mask.remove("unknownExtension");
  mask.remove("id");
  state.mask_stack = {mask};
  build_edit_recipe_object_with_mask_cache(state, &cache);
  if (cache.find(state.mask_stack)) return false;
  state.mask_stack = {second};
  if (!exact() || !cache.find(state.mask_stack)) { std::cerr << "Cache contract line " << __LINE__ << " failed\n"; return false; }
  cache.clear();
  return !cache.find(state.mask_stack);
}

bool shared_mask_payload_contracts() {
  for (const auto* kind : {"brush", "alpha", "subject", "background"}) {
    for (int variant = 0; variant < 3; ++variant) {
      auto mask = default_mask_definition(QString::fromLatin1(kind));
      mask["id"] = "shared-payload-mask";
      mask["maskSetId"] = "shared-payload-set";
      mask["segmentationWidth"] = 9;
      mask["segmentationHeight"] = 7;
      mask["segmentationData"] = QByteArray(variant == 1 ? 62 : 63, '\x7f');
      mask["brushPoints"] = QVariantList{QVariantMap{{"x", .2}, {"y", .6}, {"size", .08}}};
      if (variant == 2) mask["toolRole"] = "retouch";
      EditRecipeState state;
      state.mask_stack = {mask};
      const auto recipe = build_edit_recipe_object(state);
      const auto legacy = recipe.value("maskStack").toArray().first().toObject();
      if (variant == 2) {
        if (recipe.value("retouchStack").toArray().first().toObject() != legacy) return false;
        continue;
      }
      const auto source = recipe.value("maskSets").toArray().first().toObject()
          .value("sources").toArray().first().toObject().value("geometry").toObject();
      const auto geometry = legacy.value("geometry").toObject();
      for (const auto* key : {"brushPoints", "brushStrokes", "segmentationData", "segmentationWidth",
                              "segmentationHeight", "rasterSchemaVersion"}) {
        if (source.value(QLatin1String(key)) != geometry.value(QLatin1String(key))) return false;
      }
      if (QString::fromLatin1(kind) == "brush" && geometry.value("brushPoints").toArray().isEmpty()) return false;
    }
  }
  return true;
}


}
int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  if (!require(shared_mask_payload_contracts(), "Compatibility and canonical mask payloads differ")) return 1;
  if (!require(mask_encoding_cache_contracts(), "Mask encoding cache invalidation or exact output failed")) return 1;
  if (!require(large_string_checkpoint_contracts(), "Large tile/string encoding must preserve exact recovery bytes")) return 1;
  if (argc == 3 && app.arguments().at(1) == "--check-checkpoint-copy") {
    // Inspect a real checkpoint only through a temporary copy. Never acquire
    // the live editor's lock or write its recovery file.
    const auto bytes = read(app.arguments().at(2));
    const auto payload = QJsonDocument::fromJson(bytes).object().value("payload").toObject();
    QTemporaryDir root;
    QString path, error;
    {
      EditorRecipeSaveJournal journal;
      EditorRecipeSaveCheckpoint recovered;
      if (!journal.open(root.path(), payload.value("authority").toString(), &recovered, &error)) return 1;
      path = journal.path();
    }
    QFile copy(path);
    if (!copy.open(QIODevice::WriteOnly) || copy.write(bytes) != bytes.size()) return 1;
    copy.close();
    EditorRecipeSaveJournal journal;
    EditorRecipeSaveCheckpoint recovered;
    if (!journal.open(root.path(), payload.value("authority").toString(), &recovered, &error)) {
      std::cerr << error.toStdString() << '\n';
      for (const auto& value : payload.value("pending").toArray()) {
        const auto recipe = value.toObject().value("recipe").toObject();
        const auto rebuilt = build_edit_recipe_object(parse_edit_recipe_json(
            QString::fromUtf8(QJsonDocument(recipe).toJson(QJsonDocument::Compact))));
        for (const auto& key : recipe.keys())
          if (rebuilt.value(key) != recipe.value(key))
            std::cerr << "Round-trip mismatch: " << key.toStdString() << '\n';
      }
      return 1;
    }
    if (read(path) != bytes || !journal.write(recovered, &error)) return 1;
    std::cout << "Recovered " << recovered.pending.size()
              << " pending records from a temporary copy; original untouched.\n";
    return 0;
  }
  if (argc == 3 && app.arguments().at(1) == "--crash-writer") {
    EditorRecipeSaveJournal journal;
    EditorRecipeSaveCheckpoint recovered;
    QString error;
    if (!journal.open(app.arguments().at(2), "catalog-a", &recovered, &error)) return 2;
    EditorRecipeSaveQueue queue;
    EditorRecipeSaveSnapshot a{.asset_id = "a", .file_path = "/a.dng"};
    a.state.exposure = 1;
    a.migration_evidence.insert("migrationPending", true);
    a.migration_evidence.insert("approvalTime", "exact approval");
    a.migration_evidence.insert("unicodeProof", QString::fromUtf8("proof-λ-雪-\\-\"\n").repeated(65536));
    queue.enqueue(a);
    queue.begin();
    a.state.exposure = 2;
    a.migration_evidence.clear();
    queue.enqueue(a);
    auto b = a; b.asset_id = "b"; b.file_path = "/b.jpg";
    queue.enqueue(b);
    if (!journal.write(queue.checkpoint(), &error)) return 3;
    std::cout << "READY\n" << std::flush;
    return app.exec(); // Parent kills this exact process, without destructors.
  }
  QTemporaryDir root;
  if (!root.isValid()) return 1;
  if (!require(cached_records_roundtrip(), "Cached record bytes, role transitions, or exact recovery failed")) return 1;
  if (!require(supported_recipe_versions_roundtrip(),
               "Recovery must round-trip every desktop recipe version the writer emits")) return 1;
  QProcess child;
  child.start(app.applicationFilePath(), {"--crash-writer", root.path()});
  if (!child.waitForStarted(3000) || !child.waitForReadyRead(10000) ||
      child.readAllStandardOutput().trimmed() != "READY") return 1;
  child.kill();
  if (!child.waitForFinished(3000) || child.exitStatus() != QProcess::CrashExit) return 1;
  QString path, other_path, error;
  {
    EditorRecipeSaveJournal journal;
    EditorRecipeSaveCheckpoint recovered;
    if (!require(journal.open(root.path(), "catalog-a", &recovered, &error), error.toUtf8().constData())) return 1;
    path = journal.path();
    EditorRecipeSaveJournal second;
    EditorRecipeSaveCheckpoint untouched;
    if (!require(!second.open(root.path(), "catalog-a", &untouched, &error), "A concurrent writer acquired the journal")) return 1;
    EditorRecipeSaveJournal other_catalog;
    if (!other_catalog.open(root.path(), "catalog-b", &untouched, &error) ||
        !untouched.pending.isEmpty() || untouched.active) return 1;
    other_path = other_catalog.path();
    EditorRecipeSaveQueue queue;
    queue.restore(recovered);
    auto* active = queue.begin();
    if (!require(active && active->state.exposure == 1 && active->migration_evidence.value("approvalTime") == "exact approval",
                 "Crash lost the exact active migration approval")) return 1;
    queue.complete(true);
    active = queue.begin();
    if (!active || active->asset_id != "a" || active->state.exposure != 2 || !active->migration_evidence.isEmpty()) return 1;
    queue.complete(true);
    active = queue.begin();
    if (!active || active->asset_id != "b") return 1;
    queue.complete(true);
    const auto previous = read(path);
    if (!independent_envelope_match(previous)) return 1;
    if (!require(legacy_checkpoint_roundtrip(previous, true), "v1 recovery upgrade failed") ||
        !require(legacy_checkpoint_roundtrip(previous, false), "v2 recovery upgrade failed")) return 1;
    const auto directory = QFileInfo(path).absolutePath();
    const auto held = directory + "-held";
    if (!QDir().rename(directory, held)) return 1;
    if (journal.write(queue.checkpoint(), &error) || error.isEmpty() ||
        read(QDir(held).filePath(QFileInfo(path).fileName())) != previous) return 1;
    if (!QDir().rename(held, directory) || !journal.write(queue.checkpoint(), &error)) return 1;
  }
  {
    EditorRecipeSaveJournal journal;
    EditorRecipeSaveCheckpoint recovered;
    if (!journal.open(root.path(), "catalog-a", &recovered, &error) ||
        !recovered.pending.isEmpty() || recovered.active || recovered.retry_migration) return 1;
  }
  const auto valid = read(path);
  if (!independent_envelope_match(valid)) return 1;
  // Even if a file is moved to another catalog's namespace, its embedded
  // authority must reject replay rather than trusting the filename alone.
  if (!QFile::copy(path, other_path)) return 1;
  {
    EditorRecipeSaveJournal journal;
    EditorRecipeSaveCheckpoint recovered;
    if (journal.open(root.path(), "catalog-b", &recovered, &error) || read(other_path) != valid) return 1;
  }
  const auto reject_bytes = [&](const QByteArray& bytes) {
    {
      QSaveFile output(path);
      if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size() || !output.commit()) return false;
    }
    EditorRecipeSaveJournal journal;
    EditorRecipeSaveCheckpoint recovered;
    return !journal.open(root.path(), "catalog-a", &recovered, &error) && !error.isEmpty() &&
        !journal.write({}, &error) && read(path) == bytes;
  };
  auto envelope = QJsonDocument::fromJson(valid).object();
  auto payload = envelope.value("payload").toObject();
  payload.insert("schema", "future-unsupported-version");
  envelope.insert("payload", payload);
  if (!reject_bytes(QJsonDocument(envelope).toJson(QJsonDocument::Compact))) return 1; // Invalid checksum.
  envelope.insert("sha256", QString::fromLatin1(QCryptographicHash::hash(
      QJsonDocument(payload).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256).toHex()));
  if (!reject_bytes(QJsonDocument(envelope).toJson(QJsonDocument::Compact))) return 1; // Valid checksum, bad schema.
  {
    QSaveFile corrupt(path);
    if (!corrupt.open(QIODevice::WriteOnly) || corrupt.write("{broken") != 7 || !corrupt.commit()) return 1;
  }
  {
    EditorRecipeSaveJournal journal;
    EditorRecipeSaveCheckpoint recovered;
    if (journal.open(root.path(), "catalog-a", &recovered, &error) || error.isEmpty() ||
        journal.write({}, &error) || read(path) != "{broken") return 1;
  }
  if (valid.isEmpty()) return 1;
  std::cout << "Killed-writer recovery, exact migration ordering, catalog isolation, exclusive ownership, atomic failure retention, acknowledged removal and corrupt-file preservation passed.\n";
  return 0;
}
