#include "app_bootstrap.h"
#include "catalog_asset_model.h"
#include "edit_adjustment_patch.h"
#include "editor_preview_controller.h"

#include <QColorSpace>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QGuiApplication>
#include <QSaveFile>
#include <QSettings>
#include <QThread>
#include <QThreadPool>

#include <functional>
#include <iostream>

namespace {
bool wait_until(const std::function<bool()>& done, int timeout_ms = 60000) {
  QElapsedTimer elapsed;
  elapsed.start();
  while (!done() && elapsed.elapsed() < timeout_ms) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    QThread::msleep(1);
  }
  return done();
}
int fail(const QString& message) {
  std::cerr << message.toStdString() << '\n';
  return 1;
}
}

int main(int argc, char** argv) {
  // Explicit integration-only input: use a freshly imported isolated catalog.
  // Invoke write and reopen in separate processes against the same root.
  if (argc != 5) return 77;
  const QString mode = QString::fromLocal8Bit(argv[1]);
  const QDir root(QString::fromLocal8Bit(argv[2]));
  if ((mode != "write" && mode != "reopen") || !root.isAbsolute() ||
      !QFile::exists(root.filePath("catalog-reopen-proof.marker"))) return 2;
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", root.filePath("runtime").toUtf8());
  qputenv("PHOTO_EDITOR_CACHE_DIR", root.filePath("cache").toUtf8());
  qunsetenv("PHOTO_EDITOR_DATABASE_URL");
  qunsetenv("PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE");
  QGuiApplication app(argc, argv);
  QSettings::setDefaultFormat(QSettings::IniFormat);
  QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, root.filePath("preferences"));
  struct DrainWorkers {
    ~DrainWorkers() { QThreadPool::globalInstance()->waitForDone(60000); }
  } drain;
  using namespace photo_editor::desktop;
  const QString asset_id = QString::fromLocal8Bit(argv[3]);
  const QString source = QString::fromLocal8Bit(argv[4]);
  const auto prefix = root.filePath(QFileInfo(source).fileName());
  QString error;
  if (!AppBootstrap::ensureCatalogService(&error)) return fail(error);
  CatalogAssetModel catalog;
  catalog.refresh();
  if (!wait_until([&] { return !catalog.loading(); }) ||
      !catalog.errorMessage().isEmpty() || !catalog.selectAssetById(asset_id, true) ||
      catalog.selectedFilePath() != source) return fail("Imported catalog asset is not available");
  EditorPreviewController editor;
  auto payload = catalog.selectedAdjustmentPatchJson();
  if (mode == "write") {
    if (QFile::exists(prefix + ".expected.json")) return fail("Refusing to overwrite an existing proof");
    auto state = append_named_recipe_snapshot(EditRecipeState{}, "Before retouch", "review");
    state.preset_applications = {QVariantMap{{"presetId", "reopen-proof"}}};
    payload = serialize_adjustment_patch_json(state, all_adjustment_groups(), AdjustmentPatchScope::AssetBound);
  }
  editor.loadAsset(asset_id, source, {}, {}, {}, {}, payload);
  if (mode == "write") {
    QImage placed(64, 48, QImage::Format_RGB32);
    placed.fill(QColor(170, 50, 25));
    placed.setColorSpace(QColorSpace(QColorSpace::SRgb));
    const auto placed_path = prefix + ".placed.png";
    if (!placed.save(placed_path) || !editor.addSourceLayer(placed_path, "Reopen source"))
      return fail("Could not create the source-layer edit");
    editor.addMask("linear");
    editor.setActiveMaskExposure(0.6);
    editor.setOutputColorSpace("Display P3");
    editor.setExposure(0.75);
    if (!wait_until([&] { return !editor.recipeSavePending(); }) ||
        !editor.recipeSaveError().isEmpty()) return fail("Catalog autosave failed: " + editor.recipeSaveError());
    QSaveFile expected(prefix + ".expected.json");
    expected.setDirectWriteFallback(false);
    const auto bytes = editor.currentAssetBoundAdjustmentPatchJson().toUtf8();
    if (!expected.open(QIODevice::WriteOnly) || expected.write(bytes) != bytes.size() || !expected.commit())
      return fail("Could not write expected recipe");
  } else {
    QFile expected(prefix + ".expected.json");
    if (!expected.open(QIODevice::ReadOnly)) return fail("Missing saved-process reference");
    const auto saved = QString::fromUtf8(expected.readAll());
    if (editor.currentAssetBoundAdjustmentPatchJson() != saved) {
      QSaveFile actual(prefix + ".actual.json");
      if (actual.open(QIODevice::WriteOnly)) {
        actual.write(editor.currentAssetBoundAdjustmentPatchJson().toUtf8());
        actual.commit();
      }
      return fail("Fresh catalog/controller recipe differs from the saved editor recipe");
    }
  }
  const auto patch = parse_adjustment_patch_json(editor.currentAssetBoundAdjustmentPatchJson());
  if (!patch.ok() || patch.state->layers.size() != 2 || patch.state->mask_stack.size() != 1 ||
      patch.state->active_history_branch != "review" || patch.state->recipe_snapshots.size() != 1)
    return fail("Saved edits or history are incomplete");
  const auto output = prefix + (mode == "write" ? ".before.png" : ".after.png");
  if (!editor.startFlattenedPhotoExport(output) ||
      !wait_until([&] { return !editor.photoExportBusy(); }) || !editor.photoExportError().isEmpty())
    return fail("Full-resolution export failed: " + editor.photoExportError());
  const QImage rendered(output);
  if (rendered.isNull()) return fail("Export is not a readable image");
  if (mode == "reopen") {
    const QImage before(prefix + ".before.png");
    if (before != rendered || before.colorSpace() != rendered.colorSpace())
      return fail("Reopened export pixels or profile changed");
  }
  std::cout << "mode=" << mode.toStdString() << " full_recipe_preserved=1 history_preserved=1 width="
            << rendered.width() << " height=" << rendered.height()
            << " pixels_and_profile_exact=" << (mode == "reopen") << std::endl;
  return 0;
}
