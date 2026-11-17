#include "catalog_asset_model.h"
#include "catalog_similarity_search.h"
#include "edit_adjustment_patch.h"
#include "edit_recipe_state.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>
#include <QThread>
#include <QUrl>
#include <iostream>

using namespace photo_editor::desktop;
namespace {
int fail(const char* message) { std::cerr << message << '\n'; return 1; }
int fail(const QString& message) { std::cerr << message.toStdString() << '\n'; return 1; }
QString state(const CatalogAssetModel& catalog, const LocalSimilaritySearch* search) {
  int photos = 0;
  int current_previews = 0;
  for (const auto& value : catalog.similaritySearchAssets()) {
    const auto asset = value.toMap();
    if (asset.value(QStringLiteral("mediaType")).toString() != QStringLiteral("photo")) continue;
    ++photos;
    if (asset.value(QStringLiteral("searchPreviewCurrent")).toBool()) ++current_previews;
  }
  return QStringLiteral(" running=%1 preparing=%2 status='%3' results=[%4] visualSearch=%5 visible=%6 total=%7 photoScope=%8 currentPreviews=%9")
      .arg(search->running())
      .arg(search->preparing())
      .arg(search->status())
      .arg(search->resultIds().join(QLatin1Char(',')))
      .arg(catalog.visualSearchActive())
      .arg(catalog.assetCount())
      .arg(catalog.totalAssetCount())
      .arg(photos)
      .arg(current_previews);
}
bool wait(auto predicate) {
  QElapsedTimer timer; timer.start();
  while (!predicate() && timer.elapsed() < 10000) { QCoreApplication::processEvents(); QThread::msleep(1); }
  return predicate();
}
bool write(const QString& path, const QByteArray& bytes) {
  QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
}
int run_catalog_similarity_search_contract() {
  QTemporaryDir root;
  if (!root.isValid()) return fail("binding fixture unavailable");
  const auto worker = root.filePath("worker.py"), preview = root.filePath("preview");
  if (!write(preview, "fixture") || !write(worker, R"PY(import json,sys,time
from pathlib import Path
print(json.dumps(dict(type='ready',protocol=1,spaceKey='a'*64,dimensions=768,
 revision='75de2d55ec2d0b4efc50b3e9ad70dba96a7b2fa2',device='cpu')),flush=True)
root=Path(sys.argv[sys.argv.index('--model-root')+1])
for line in sys.stdin:
 r=json.loads(line)
 with (root/'requests').open('a') as log: log.write(r['kind']+'\n')
 time.sleep(.1)
 print(json.dumps(dict(type='embedding',id=r['id'],kind=r['kind'],spaceKey='a'*64,
  contentKey='b'*64,vector=[1.0]+[0.0]*767,milliseconds=1)),flush=True)
)PY")) return fail("binding worker setup failed");
  CatalogAssetModel catalog;
  catalog.setInteractiveRenderingActive(true);
  CatalogSimilaritySearch binding(&catalog, "test-catalog");
  auto* search = binding.controller();
  if (!search->configure(QStringLiteral(PHOTO_EDITOR_TEST_PYTHON), worker, root.path(), "cpu", root.filePath("index.sqlite3")))
    return fail("binding runtime rejected");
  CatalogAssetItem a, b;
  a.asset_id = "a"; a.file_path = preview; a.camera_label = "Camera A"; a.online = true;
  b.asset_id = "b"; b.file_path = preview; b.camera_label = "Camera B"; b.online = true;
  const auto publish = [&](const QString& id) {
    const auto asset = catalog.assetById(id);
    return catalog.applyThumbnailResultForTesting(id, asset.value("filePath").toString(),
        asset.value("adjustmentPatchJson").toString(), QUrl::fromLocalFile(preview).toString());
  };
  catalog.replaceAssetsForTesting({a, b});
  if (!publish("a") || !publish("b")) return fail("binding previews rejected");
  catalog.setCameraFilter("Camera A");
  const auto preview_scope = catalog.similaritySearchAssets();
  if (preview_scope.size() != 1 || preview_scope.first().toMap().size() != 5 ||
      preview_scope.first().toMap().contains("adjustmentPatchJson") ||
      preview_scope.first().toMap().value("assetId").toString() != "a" ||
      !preview_scope.first().toMap().value("searchPreviewCurrent").toBool())
    return fail("embedding scope retained editable recipes, lost preview provenance or broadened filters");

  const auto saved_recipe = catalog.assetById("a").value("adjustmentPatchJson").toString();
  const auto request_log = root.filePath("requests");
  if (!write(request_log, {})) return fail("binding request log setup failed");
  catalog.setRecipesPendingForTesting({"a"});
  binding.search("photo");
  if (!search->running() || !search->preparing() || !search->status().contains("Preparing"))
    return fail("binding did not expose the current-preview preparation state");
  QFile requests_during_recipe_load(request_log);
  if (requests_during_recipe_load.open(QIODevice::ReadOnly) && !requests_during_recipe_load.readAll().isEmpty())
    return fail("local image/text inference started before current previews were ready");
  binding.cancel();
  if (search->running() || search->preparing() || !search->results().isEmpty())
    return fail("cancel did not stop a pending recipe/preview barrier");
  QFile requests_after_cancel(request_log);
  if (requests_after_cancel.open(QIODevice::ReadOnly) && !requests_after_cancel.readAll().isEmpty())
    return fail("cancel launched inference after releasing the pending scope");

  if (!catalog.applyRecipeForTesting("a", saved_recipe))
    return fail("test recipe hydration did not publish the exact saved recipe");
  binding.search("photo");
  if (!search->running() || !search->preparing())
    return fail("offviewport preview generation did not keep Find Photos preparing");
  CatalogAssetItem timeline;
  timeline.asset_id = "timeline-a";
  timeline.file_path = preview;
  timeline.media_type = "timeline";
  timeline.camera_label = "Camera A";
  timeline.online = true;
  catalog.replaceTimelineAssetsForTesting({timeline});
  if (!search->running() || !search->preparing())
    return fail("timeline reconciliation changed the captured photo scope during preview preparation");
  QFile requests_before_preview(request_log);
  if (requests_before_preview.open(QIODevice::ReadOnly) && !requests_before_preview.readAll().isEmpty())
    return fail("inference started while the captured scope still lacked a current rendered preview");
  if (!publish("a")) return fail("current recipe preview could not be published for the captured scope");
  catalog.requestSearchPreviews({"a"});
  if (!wait([&] { return !search->running(); }) || search->preparing() ||
      !catalog.visualSearchActive() || search->resultIds() != QStringList{"a"})
    return fail(QStringLiteral("recipe hydration and exact current preview did not resume the captured filtered scope:") +
        state(catalog, search));
  QFile requests_after_search(request_log);
  if (!requests_after_search.open(QIODevice::ReadOnly) ||
      requests_after_search.readAll().split('\n').count("image") != 1)
    return fail("prepared-scope search did not encode exactly its one current preview");

  if (!write(request_log, {})) return fail("binding request log reset failed");
  binding.clearFilter();
  catalog.setRecipesPendingForTesting({"a"});
  binding.search("photo");
  if (!search->preparing()) return fail("catalog scope invalidation fixture did not enter preparation");
  catalog.setCameraFilter("Camera B");
  if (search->running() || search->preparing() || !search->results().isEmpty() || catalog.visualSearchActive())
    return fail("a filtered-scope change failed to cancel the pending search");
  QFile requests_after_filter_change(request_log);
  if (requests_after_filter_change.open(QIODevice::ReadOnly) && !requests_after_filter_change.readAll().isEmpty())
    return fail("filtered-scope invalidation launched local inference");

  catalog.replaceTimelineAssetsForTesting({});
  catalog.setCameraFilter("Camera A");
  if (!publish("a")) {
    if (!catalog.applyRecipeForTesting("a", saved_recipe) || !publish("a"))
      return fail("canceled search fixture could not restore a current preview");
  }
  binding.search("photo");
  if (!wait([&] { return !search->running(); }) || !catalog.visualSearchActive() ||
      search->resultIds() != QStringList{"a"} || catalog.assetCount() != 1)
    return fail("binding search broadened camera scope or self-invalidated result projection");
  binding.clearFilter();
  if (catalog.visualSearchActive() || catalog.assetCount() != 1) return fail("clear projection changed camera scope");
  binding.showResults();
  if (!catalog.visualSearchActive()) return fail("completed current results could not be restored");
  // An unrelated background thumbnail must not continuously cancel a filtered
  // search; B is outside both the query scope and its result projection.
  if (!publish("b") || search->resultIds() != QStringList{"a"} || !catalog.visualSearchActive())
    return fail("unrelated preview publication invalidated a scoped search");
  // Changing ordinary filters must clear old projected IDs and never restore them.
  catalog.setCameraFilter("Camera B");
  if (!search->results().isEmpty() || !wait([&] { return !catalog.visualSearchActive(); }) || catalog.assetCount() != 1)
    return fail("filter change retained stale results");
  binding.showResults();
  if (catalog.visualSearchActive()) return fail("invalidated results were republished");
  binding.search("photo");
  catalog.replaceAssetsForTesting({a});  // Delete B while a query is in flight.
  if (!wait([&] { return !search->running(); }) || !search->results().isEmpty() || catalog.visualSearchActive())
    return fail("removed photo returned from late inference");
  catalog.setCameraFilter("all");
  catalog.replaceAssetsForTesting({a, b});
  if (!publish("a") || !publish("b")) return fail("recreated previews rejected");
  binding.search("photo");
  if (!wait([&] { return !search->running(); }) || search->resultIds() != QStringList{"a", "b"})
    return fail("recreated current catalog did not search");
  binding.search("photo", 1);
  if (!wait([&] { return !search->running(); }) || search->resultIds() != QStringList{"a"} || catalog.assetCount() != 1)
    return fail("bounded result projection failed");
  EditRecipeState edit; edit.exposure = 0.5;
  const auto before = catalog.assetById("a").value("adjustmentPatchJson").toString();
  const auto patch = canonical_asset_bound_adjustment_patch_json(serialize_edit_recipe_json(edit));
  // B was in the query scope but is hidden by top-K projection. A row-only
  // dataChanged subscription cannot see this update.
  if (catalog.applyTransientAdjustmentPatchJsonToAssetIds({"b"}, patch, {"tone"}) != 1 ||
      !search->results().isEmpty() || !wait([&] { return !catalog.visualSearchActive(); }))
    return fail("hidden transient edit did not invalidate current results");
  // No edit is dispatched by the search binding itself.
  if (catalog.assetById("a").value("adjustmentPatchJson").toString() != before)
    return fail("search unexpectedly edited an unrelated asset");
  binding.search("photo");
  if (!wait([&] { return !search->running(); }) || search->resultIds() != QStringList{"a"})
    return fail("transient retained preview was indexed as current");
  binding.cancel();
  if (catalog.visualSearchActive() || !search->results().isEmpty()) return fail("cancel retained projection");
  search->stop();
  if (!wait([&] { return !search->running(); })) return fail("owned worker did not stop");
  return 0;
}
