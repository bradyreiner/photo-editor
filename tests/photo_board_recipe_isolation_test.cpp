#include "editor_preview_controller.h"
#include "edit_adjustment_patch.h"
#include "editor_recipe_save_journal.h"

#include <QDir>
#include <QGuiApplication>
#include <QImage>
#include <QSettings>
#include <QTemporaryDir>
#include <iostream>

namespace photo_editor::desktop {
struct EditorPreviewControllerTestAccess final {
  static void restore(EditorPreviewController& editor, const QString& asset,
                      const QString& path, const EditRecipeState& catalog,
                      bool canonical_catalog) {
    editor.asset_id_ = asset;
    editor.file_path_ = path;
    editor.ensureRecipeSaveJournal();
    editor.restoreStateFromAdjustmentPayload(asset,
        serialize_adjustment_patch_json(catalog, all_adjustment_groups(),
                                        AdjustmentPatchScope::AssetBound), false, canonical_catalog);
  }
  static EditRecipeState state(const EditorPreviewController& editor) {
    return editor.currentState();
  }
  static EditorRecipeSaveCheckpoint checkpoint(const EditorPreviewController& editor) {
    return editor.recipe_save_queue_.checkpoint();
  }
  static bool documentOwnerCleared(const EditorPreviewController& editor) {
    return editor.document_owned_asset_id_.isEmpty();
  }
};
}

using namespace photo_editor::desktop;
namespace {
bool check(bool value, const char* message) {
  if (!value) std::cerr << message << '\n';
  return value;
}

bool recovery_case(const QString& root, bool conflicting_renderer,
                   bool canonical_catalog = true) {
  QDir().mkpath(root);
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", root.toUtf8());
  qputenv("PHOTO_EDITOR_APP_DATA_ROOT", root.toUtf8());
  EditRecipeState catalog;
  catalog.exposure = 0.4;
  catalog.crop_rect_x = 0.1;
  catalog.crop_rect_y = 0.08;
  catalog.crop_rect_width = 0.8;
  catalog.crop_rect_height = 0.75;
  catalog.layers = {QVariantMap{{"id", "original-adjustment"}, {"kind", "adjustment"},
      {"enabled", true}, {"opacity", 0.7}, {"exposure", 0.25}}};
  auto pending = catalog;
  pending.exposure = 0.9;
  if (conflicting_renderer) {
    pending.photographic_rendering_model = "legacy-oklch-v1";
    pending.layers = {QVariantMap{{"id", "foreign-board-source"}, {"kind", "source"},
        {"name", "Another photo"}, {"enabled", true}, {"opacity", 1.0},
        {"sourceAssetId", "other-photo"}, {"sourcePath", root + "/other.png"},
        {"pixelWidth", 120}, {"pixelHeight", 80}, {"canvasWidth", 3840},
        {"canvasHeight", 3072}, {"x", -22}, {"y", 240}, {"scale", 0.65}}};
  }
  const auto original_pending = serialize_edit_recipe_json(pending);
  {
    EditorRecipeSaveJournal journal;
    EditorRecipeSaveCheckpoint recovered;
    QString error;
    if (!check(journal.open(root, EditorRecipeSaveJournal::currentAuthority(),
                            &recovered, &error), "Open private journal")) return false;
    EditorRecipeSaveCheckpoint seed;
    seed.pending = {{.asset_id = "photo", .file_path = root + "/photo.png", .state = pending},
                    {.asset_id = "unrelated", .file_path = root + "/unrelated.png"}};
    if (!check(journal.write(seed, &error), "Seed durable pending edit")) return false;
  }
  {
    EditorPreviewController editor;
    editor.pauseRecipePersistence(true);
    EditorPreviewControllerTestAccess::restore(editor, "photo", root + "/photo.png", catalog,
                                               canonical_catalog);
    const auto actual = EditorPreviewControllerTestAccess::state(editor);
    const bool quarantine = conflicting_renderer && canonical_catalog;
    const auto& expected = quarantine ? catalog : pending;
    if (!check(actual.exposure == expected.exposure &&
               actual.crop_rect_x == catalog.crop_rect_x &&
               actual.crop_rect_y == catalog.crop_rect_y &&
               actual.crop_rect_width == catalog.crop_rect_width &&
               actual.crop_rect_height == catalog.crop_rect_height &&
               actual.photographic_rendering_model == expected.photographic_rendering_model &&
               actual.layers.size() == expected.layers.size() &&
               actual.layers.first().toMap().value("id") == expected.layers.first().toMap().value("id"),
               "Standalone photo must retain its own composition; valid unsaved edits must survive")) return false;
    const auto checkpoint = EditorPreviewControllerTestAccess::checkpoint(editor);
    if (quarantine &&
        !check(checkpoint.rejected.size() == 1 && checkpoint.pending.size() == 1 &&
               checkpoint.pending.first().asset_id == "unrelated" &&
               serialize_edit_recipe_json(checkpoint.rejected.first().state) == original_pending &&
               editor.recipeSaveRecoveryAssetIds() == QStringList{"photo"},
               "Foreign recovery must remain exact and durable without blocking unrelated photos")) return false;
    if (!quarantine && !check(checkpoint.rejected.isEmpty() && checkpoint.pending.size() == 2,
                             "Compatible or noncanonical pending work must remain queued")) return false;
  }
  if (conflicting_renderer && canonical_catalog) {
    EditorRecipeSaveJournal journal;
    EditorRecipeSaveCheckpoint checkpoint;
    QString error;
    if (!check(journal.open(root, EditorRecipeSaveJournal::currentAuthority(), &checkpoint, &error) &&
               checkpoint.rejected.size() == 1 &&
               serialize_edit_recipe_json(checkpoint.rejected.first().state) == original_pending,
               "Quarantined board state must survive closing and reopening the journal")) return false;
  }
  return true;
}

bool document_owner_case(const QString& root, bool legacy_board) {
  QDir().mkpath(root);
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", root.toUtf8());
  qputenv("PHOTO_EDITOR_APP_DATA_ROOT", root.toUtf8());
  QImage image(120, 80, QImage::Format_ARGB32);
  image.fill(Qt::red);
  const auto path = root + "/photo.png";
  if (!image.save(path)) return false;
  EditRecipeState photo;
  photo.exposure = 0.4;
  photo.crop_rect_x = 0.1;
  photo.crop_rect_width = 0.8;
  photo.crop_rect_height = 1;
  auto board = photo;
  board.exposure = -1;
  if (legacy_board) board.photographic_rendering_model = "legacy-oklch-v1";
  const auto payload = [](const EditRecipeState& state) {
    return serialize_adjustment_patch_json(state, all_adjustment_groups(),
                                          AdjustmentPatchScope::AssetBound);
  };
  EditorPreviewController editor;
  editor.pauseRecipePersistence(true);
  editor.setCatalogRecipePersistenceEnabled(false);
  editor.loadAsset("photo", path, {}, {}, {}, {}, payload(board));
  editor.setCatalogRecipePersistenceEnabled(true);
  editor.loadAsset("photo", path, {}, {}, {}, {}, payload(photo), false, true);
  const auto restored = EditorPreviewControllerTestAccess::state(editor);
  return check(EditorPreviewControllerTestAccess::documentOwnerCleared(editor) &&
               restored.exposure == photo.exposure &&
               restored.photographic_rendering_model == photo.photographic_rendering_model &&
               restored.crop_rect_x == photo.crop_rect_x &&
               restored.crop_rect_width == photo.crop_rect_width,
               "Returning from a document-owned edit must restore catalog ownership and composition");
}

bool namespaced_board_case(const QString& root) {
  QDir().mkpath(root);
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", root.toUtf8());
  qputenv("PHOTO_EDITOR_APP_DATA_ROOT", root.toUtf8());
  QImage image(120, 80, QImage::Format_ARGB32);
  image.fill(Qt::blue);
  const auto path = root + "/photo.png";
  if (!image.save(path)) return false;
  EditRecipeState photo;
  photo.exposure = 0.4;
  auto board = photo;
  board.exposure = -1;
  const auto payload = [](const EditRecipeState& state) {
    return serialize_adjustment_patch_json(state, all_adjustment_groups(),
                                          AdjustmentPatchScope::AssetBound);
  };
  EditorPreviewController editor;
  editor.pauseRecipePersistence(true);
  editor.loadAsset("photo", path, {}, {}, {}, {}, payload(photo), false, true);
  editor.setExposure(0.6);
  editor.setCatalogRecipePersistenceEnabled(false);
  editor.loadAsset("board:document:one", path, {}, {}, {}, {}, payload(board));
  editor.setExposure(-0.8);
  editor.setCatalogRecipePersistenceEnabled(true);
  editor.loadAsset("photo", path, {}, {}, {}, {}, payload(photo), false, true);
  const auto state = EditorPreviewControllerTestAccess::state(editor);
  const auto checkpoint = EditorPreviewControllerTestAccess::checkpoint(editor);
  if (!check(state.exposure == 0.6 && state.layers.isEmpty() && checkpoint.pending.size() == 1 &&
             checkpoint.pending.first().asset_id == "photo" &&
             checkpoint.pending.first().state.exposure == 0.6,
             "Photo-to-board-to-photo must preserve unsaved photo edits and never enqueue a board recipe")) return false;
  return true;
}
}

int main(int argc, char** argv) {
  QTemporaryDir runtime;
  if (!runtime.isValid()) return 2;
  qputenv("QT_QPA_PLATFORM", "offscreen");
  qputenv("PHOTO_EDITOR_DATABASE_URL", "postgresql://127.0.0.1:1/board-isolation-test");
  qputenv("PHOTO_EDITOR_CACHE_DIR", runtime.filePath("cache").toUtf8());
  QGuiApplication app(argc, argv);
  QSettings::setDefaultFormat(QSettings::IniFormat);
  QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, runtime.filePath("preferences"));
  if (!recovery_case(runtime.filePath("foreign-recovery"), true) ||
      !recovery_case(runtime.filePath("valid-pending"), false) ||
      !recovery_case(runtime.filePath("no-canonical-authority"), true, false) ||
      !document_owner_case(runtime.filePath("legacy-owner-transition"), true) ||
      !document_owner_case(runtime.filePath("canonical-owner-transition"), false) ||
      !namespaced_board_case(runtime.filePath("namespaced-board"))) return 1;
  std::cout << "Standalone photo composition and unsaved edits isolated from stale board recovery.\n";
}
