#include "editor_preview_controller.h"
#include "editor_recipe_save_journal.h"
#include "editor_recipe_persistence_launcher.h"
#include "edit_adjustment_patch.h"

#include <QFile>
#include <QDir>
#include <QGuiApplication>
#include <QImage>
#include <QSettings>
#include <QTemporaryDir>
#include <cmath>
#include <iostream>

namespace photo_editor::desktop {
struct EditorPreviewControllerTestAccess final {
  static bool open(EditorPreviewController& preview) { return preview.ensureRecipeSaveJournal(); }
  static bool cached(const EditorPreviewController& preview, const QString& asset) {
    return preview.history_service_.hasCachedState(asset);
  }
  static QString journalPath(const EditorPreviewController& preview) { return preview.recipe_save_journal_.path(); }
  static bool reject(EditorPreviewController& preview, EditorRecipeSaveSnapshot snapshot,
                     const QString& reason) {
    if (!preview.ensureRecipeSaveJournal()) return false;
    preview.recipe_save_queue_.enqueue(std::move(snapshot));
    if (!preview.recipe_save_queue_.begin()) return false;
    preview.recipe_save_queue_.rejectActive(reason);
    return preview.checkpointRecipeSaves();
  }
  static bool checkpointPending(EditorPreviewController& preview, const EditorRecipeSaveSnapshot& snapshot) {
    if (!preview.ensureRecipeSaveJournal()) return false;
    preview.recipe_save_queue_.enqueue(snapshot);
    preview.history_service_.setCachedState(snapshot.asset_id, snapshot.state);
    return preview.checkpointRecipeSaves();
  }
  static bool acknowledge(EditorPreviewController& preview, const QString& path) {
    const auto* active = preview.recipe_save_queue_.begin();
    if (!active || active->file_path != path || active->state.exposure != 1.25 ||
        !preview.recipe_save_queue_.hasRejected()) return false;
    preview.recipe_save_queue_.complete(true);
    return preview.checkpointRecipeSaves();
  }
};
}  // namespace photo_editor::desktop

int main(int argc, char** argv) {
  QTemporaryDir runtime;
  if (!runtime.isValid()) return 2;
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", runtime.path().toUtf8());
  qputenv("PHOTO_EDITOR_DATABASE_URL", "postgresql://127.0.0.1:1/recovery-contract");
  QGuiApplication app(argc, argv);
  QSettings::setDefaultFormat(QSettings::IniFormat);
  QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, runtime.filePath("preferences"));
  using namespace photo_editor::desktop;
  const auto resolved = runtime.filePath("restored.CR2");
  QFile file(resolved);
  if (!file.open(QIODevice::WriteOnly) || file.write("source-path-fixture") < 0) return 2;
  file.close();
  QString expected_conflict_recipe;
  {
    EditorPreviewController preview;
    preview.pauseRecipePersistence(true); // Exercise checkpoints without starting any database helper.
    EditorRecipeSaveSnapshot orphan{.asset_id = "recovered", .file_path = "/Volumes/Old/photo.CR2"};
    orphan.state.photographic_rendering_model = QStringLiteral("legacy-oklch-v1");
    orphan.state.exposure = 1.25;
    const auto prepared = prepare_editor_recipe_persistence_payload(
        orphan.asset_id, orphan.state, {}, {}, runtime.path(), false);
    if (!prepared.ok()) return 1;
    const auto arguments = QJsonDocument::fromJson(prepared.payload).object().value("arguments").toArray();
    if (arguments.size() != 3 || arguments[0].toString() != "--legacy-v1" ||
        parse_edit_recipe_json(arguments[2].toString()).exposure != orphan.state.exposure) return 1;
    if (!EditorPreviewControllerTestAccess::reject(preview, orphan, "catalog_asset_missing") ||
        !preview.recipeSaveError().isEmpty() || preview.recipeSavePending() ||
        preview.recipeSaveRecoveryAssetIds() != QStringList{"recovered"} ||
        !preview.recipeSaveRecoveryDetail().contains("photo.CR2")) return 1;
    if (preview.retryRecoveredRecipeSave("recovered", runtime.filePath("missing.CR2")) ||
        !preview.retryRecoveredRecipeSave("recovered", resolved) ||
        preview.retryRecoveredRecipeSave("recovered", resolved) || !preview.recipeSavePending()) return 1;
    if (!EditorPreviewControllerTestAccess::acknowledge(preview, resolved) ||
        !preview.recipeSaveRecoveryAssetIds().isEmpty() || !preview.recipeSaveError().isEmpty()) return 1;

    orphan.asset_id = "invalid";
    if (!EditorPreviewControllerTestAccess::reject(preview, orphan, "canonical_recipe_invalid") ||
        preview.retryRecoveredRecipeSave("invalid", resolved)) return 1;
    orphan.asset_id = "agent-owned";
    orphan.agent_task_id = "task-recovery";
    if (!EditorPreviewControllerTestAccess::reject(preview, orphan, "catalog_asset_missing") ||
        preview.retryRecoveredRecipeSave("agent-owned", resolved) || preview.recipeSavePending()) return 1;

    // Opening a migrated photo must prefer its catalog recipe over a durable
    // older edit while preserving that exact older edit for recovery.
    const auto image_path = runtime.filePath("canonical-photo.png");
    QImage image(16, 16, QImage::Format_RGB32);
    image.fill(Qt::gray);
    if (!image.save(image_path)) return 2;
    auto conflict = orphan;
    conflict.asset_id = "canonical-conflict";
    conflict.file_path = image_path;
    conflict.agent_task_id.clear();
    conflict.state.layers = {QVariantMap{{"id", "foreign-board-source"}, {"kind", "source"},
        {"sourceAssetId", "other-photo"}, {"sourcePath", image_path}, {"opacity", 0.65}}};
    expected_conflict_recipe = serialize_edit_recipe_json(conflict.state);
    if (!EditorPreviewControllerTestAccess::checkpointPending(preview, conflict)) return 1;
    auto canonical = conflict.state;
    canonical.photographic_rendering_model = "aces2-ocio-v1";
    canonical.exposure = 0.45;
    canonical.layers.clear();
    const auto canonical_payload = serialize_adjustment_patch_json(
        canonical, all_adjustment_groups(), AdjustmentPatchScope::AssetBound);
    preview.loadAsset(conflict.asset_id, image_path, image_path, {}, {}, {}, canonical_payload, false, true);
    if (std::abs(preview.exposure() - canonical.exposure) > 0.0001 ||
        preview.currentPhotographicRenderingModel() != canonical.photographic_rendering_model ||
        preview.recipeSavePending() || !preview.recipeSaveError().isEmpty() ||
        !preview.recipeSaveRecoveryAssetIds().contains(conflict.asset_id) ||
        !preview.recipeSaveRecoveryDetail().contains("Older incompatible edits") ||
        preview.retryRecoveredRecipeSave(conflict.asset_id, image_path)) {
      std::cerr << "Migrated catalog recipes must remain active while incompatible edits stay recoverable.\n";
      return 1;
    }
  }
  // Restart with a classified conflict: its older state must never seed history,
  // even before canonical provenance is supplied by the catalog projection.
  {
    EditorPreviewController preview;
    preview.pauseRecipePersistence(true);
    if (!EditorPreviewControllerTestAccess::open(preview) ||
        EditorPreviewControllerTestAccess::cached(preview, "canonical-conflict") ||
        !EditorPreviewControllerTestAccess::cached(preview, "invalid") ||
        !preview.recipeSaveRecoveryDetail().contains("will not fix this format error") ||
        preview.archiveConflictingRecipeRecovery("invalid").value("ok").toBool() ||
        preview.archiveConflictingRecipeRecovery("agent-owned").value("ok").toBool()) return 1;
    EditRecipeState canonical;
    canonical.photographic_rendering_model = "aces2-ocio-v1";
    canonical.exposure = 0.45;
    const auto image_path = runtime.filePath("canonical-photo.png");
    const auto payload = serialize_adjustment_patch_json(canonical, all_adjustment_groups(), AdjustmentPatchScope::AssetBound);
    preview.loadAsset("canonical-conflict", image_path, image_path, {}, {}, {}, payload);
    if (std::abs(preview.exposure() - canonical.exposure) > 0.0001) return 1;

    // An unwritable live checkpoint must leave the recovery entry in memory
    // and on disk, even though the independent backup has already succeeded.
    const auto path = EditorPreviewControllerTestAccess::journalPath(preview);
    if (!QFile::rename(path, path + ".retained") || !QDir().mkdir(path)) return 2;
    const auto failed = preview.archiveConflictingRecipeRecovery("canonical-conflict");
    if (failed.value("ok").toBool() ||
        !preview.recipeSaveRecoveryAssetIds().contains("canonical-conflict")) return 1;
    if (!QDir().rmdir(path) || !QFile::rename(path + ".retained", path)) return 2;
    const auto result = preview.archiveConflictingRecipeRecovery("canonical-conflict");
    if (!result.value("ok").toBool() || preview.recipeSaveRecoveryAssetIds().contains("canonical-conflict") ||
        std::abs(preview.exposure() - canonical.exposure) > 0.0001) {
      std::cerr << result.value("error").toString().toStdString() << '\n';
      return 1;
    }
    // Backups use the same checksummed, lossless recovery reader as live saves.
    EditorRecipeSaveJournal archive;
    EditorRecipeSaveCheckpoint archived;
    QString archive_error;
    const auto archive_root = QFileInfo(result.value("archivePath").toString()).dir().absolutePath() + "/..";
    if (!archive.open(archive_root, EditorRecipeSaveJournal::currentAuthority(), &archived, &archive_error) ||
        archived.rejected.size() != 1 || archived.rejected.first().asset_id != "canonical-conflict" ||
        archived.rejected.first().state.exposure != 1.25 ||
        archived.rejected.first().state.photographic_rendering_model != "legacy-oklch-v1" ||
        serialize_edit_recipe_json(archived.rejected.first().state) != expected_conflict_recipe ||
        archived.rejected.first().migration_evidence.value("saveRecoveryReason") !=
            "legacy_renderer_conflicts_with_canonical_catalog" ||
        !archived.pending.isEmpty()) return 1;
  }
  // A fresh controller also loads the saved photo after its backup is resolved.
  {
    EditorPreviewController preview;
    preview.pauseRecipePersistence(true);
    EditRecipeState canonical;
    canonical.photographic_rendering_model = "aces2-ocio-v1";
    canonical.exposure = 0.45;
    const auto image_path = runtime.filePath("canonical-photo.png");
    const auto payload = serialize_adjustment_patch_json(canonical, all_adjustment_groups(), AdjustmentPatchScope::AssetBound);
    preview.loadAsset("canonical-conflict", image_path, image_path, {}, {}, {}, payload);
    if (std::abs(preview.exposure() - canonical.exposure) > 0.0001 ||
        preview.recipeSaveRecoveryAssetIds().contains("canonical-conflict")) return 1;
  }
  EditorRecipeSaveJournal journal;
  EditorRecipeSaveCheckpoint checkpoint;
  QString error;
  if (!journal.open(runtime.path(), EditorRecipeSaveJournal::currentAuthority(), &checkpoint, &error) ||
      checkpoint.rejected.size() != 2 || !checkpoint.pending.isEmpty() || checkpoint.active) return 1;
  for (const auto& snapshot : checkpoint.rejected)
    if (snapshot.state.exposure != 1.25 || snapshot.state.photographic_rendering_model != "legacy-oklch-v1") return 1;
  std::cout << "Recovered source retries preserve exact edits until acknowledgment; incompatible, invalid and agent-owned recovery stays durable.\n";
  return 0;
}
