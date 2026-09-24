#include "search_edit_workflow.h"
#include "agent_edit_controller.h"
#include "catalog_asset_model.h"
#include "edit_adjustment_patch.h"
#include "visual_search_test_server.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QTemporaryDir>
#include <QThread>
#include <iostream>

using namespace photo_editor::desktop;

int run_raw_locality_probe(const QStringList& arguments) {
  const auto position = arguments.indexOf("--raw-locality-probe");
  if (position < 0 || arguments.size() < position + 3) return 1;
  QTemporaryDir temporary;
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", temporary.path().toUtf8());
  qputenv("PHOTO_EDITOR_DATABASE_URL", "postgres://example/test");
  CatalogAssetModel catalog;
  catalog.setInteractiveRenderingActive(true);
  CatalogAssetItem item;
  item.asset_id = "locality-probe";
  item.file_path = arguments[position + 1];
  item.online = true;
  item.media_type = "photo";
  item.adjustment_patch_json = canonical_asset_bound_adjustment_patch_json("{}");
  if (arguments.size() > position + 4 && arguments[position + 4] == "aces2") {
    item.adjustment_patch_json = serialize_adjustment_patch_json(EditRecipeState{}, all_adjustment_groups(), AdjustmentPatchScope::AssetBound);
  }
  catalog.replaceAssetsForTesting({item});
  catalog.selectAssetById(item.asset_id, true);
  AgentEditController agent;
  agent.setCatalogModel(&catalog);
  agent.setSelectedModelId("rulebook-deterministic");
  agent.setUserInstructions("Warm only the background and keep people unchanged.");
  bool valid_alpha = true;
  const int alpha = arguments.size() > position + 3 ? arguments[position + 3].toInt(&valid_alpha) : 0;
  if (!valid_alpha || alpha < 0 || alpha > 255) return 1;
  const QVariantMap mask{{"id", "zero-mask"}, {"kind", "background"}, {"name", "Locality probe"},
      {"temperature", 12.0}, {"enabled", true}, {"operation", "add"}, {"opacity", 1.0}, {"density", 1.0},
      {"feather", 0.0}, {"segmentationWidth", 16}, {"segmentationHeight", 16},
      {"segmentationData", QByteArray(256, static_cast<char>(alpha))}};
  QDir output(arguments[position + 2]);
  if (!output.mkpath(".")) return 1;
  for (int pass = 0; pass < 2; ++pass) {
    if (pass == 1) {
      auto state = *parse_adjustment_patch_json(item.adjustment_patch_json).state;
      state.mask_stack = {mask};
      item.adjustment_patch_json = serialize_adjustment_patch_json(state, all_adjustment_groups(), AdjustmentPatchScope::AssetBound);
      catalog.replaceAssetsForTesting({item});
      catalog.selectAssetById(item.asset_id, true);
    }
    catalog.clearSelection();
    catalog.toggleSelectionIndex(0);
    const auto report = agent.renderProofForSelected();
    const auto path = report.value("artifacts").toMap().value("currentProofPath").toString();
    if (!report.value("ok").toBool() || !QFile::copy(path, output.filePath(pass == 0 ? "before.png" : "after.png"))) {
      std::cerr << QJsonDocument::fromVariant(QVariantMap{{"errors", report.value("errors")}, {"warnings", report.value("warnings")}, {"path", path}}).toJson().constData();
      return 1;
    }
  }
  return 0;
}

int run_search_edit_workflow_contract() {
  auto require = [](bool pass, const char* message) {
    if (!pass) std::cerr << message << '\n';
    return pass;
  };
  QTemporaryDir temporary;
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", temporary.path().toUtf8());
  qputenv("PHOTO_EDITOR_DATABASE_URL", "postgres://example/test");
  CatalogAssetModel catalog;
  catalog.setInteractiveRenderingActive(true);
  QVector<CatalogAssetItem> items;
  for (int i = 0; i < 3; ++i) {
    CatalogAssetItem item;
    item.asset_id = QString::number(i);
    item.file_path = temporary.filePath(item.asset_id + ".jpg");
    QImage image(80, 60, QImage::Format_RGB32);
    image.fill(i == 2 ? Qt::blue : Qt::red);
    if (!image.save(item.file_path)) return 1;
    item.online = true;
    item.media_type = "photo";
    item.adjustment_patch_json = canonical_asset_bound_adjustment_patch_json(QStringLiteral("{}"));
    items.append(item);
  }
  catalog.replaceAssetsForTesting(items);
  visual_search_test::Server server;
  SearchEditWorkflow workflow(&catalog);
  QStringList dispatched;
  int dispatch_count = 0;
  workflow.setDraftBackend([] { return false; }, [&](const QStringList& ids, const QString& instructions) {
    dispatched = ids;
    ++dispatch_count;
    return QVariantMap{{"accepted", instructions.contains("keep people unchanged")}, {"requestId", "test-request"}};
  });
  auto wait = [&] {
    QElapsedTimer timer; timer.start();
    while (workflow.search()->running() && timer.elapsed() < 15000) {
      QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
      QThread::msleep(1);
    }
    QCoreApplication::processEvents();
    return workflow.phase() == "confirm";
  };
  auto start = [&] {
    return workflow.start("Find red scenes, warm the background and keep people unchanged.", server.model()).value("accepted").toBool() && wait();
  };
  const auto before = catalog.assetById("0").value("adjustmentPatchJson");
  if (!require(start() && server.protocol_ok && dispatch_count == 0 && workflow.search()->resultIds() == QStringList{"0", "1"},
               "search must parse an instruction and return real candidates without dispatching edits")) return 1;
  const auto token = workflow.confirmationToken();
  if (!require(!workflow.snapshot().contains("confirmationToken") && !workflow.confirmTargets("wrong", {"0"}, "keep people unchanged").value("accepted").toBool(),
               "status must not expose approval token and mismatched approval must fail")) return 1;
  catalog.selectIndex(2);
  if (!require(workflow.confirmTargets(token, {"1"}, "Warm the background and keep people unchanged.").value("accepted").toBool() &&
               dispatched == QStringList{"1"} && catalog.selectedAssetId() == "2" && catalog.assetById("0").value("adjustmentPatchJson") == before,
               "confirmation must dispatch only checked ids without rewriting selection or recipes")) return 1;
  if (!require(!workflow.confirmTargets(token, {"0"}, "keep people unchanged").value("accepted").toBool() && dispatch_count == 1,
               "approval must be single use")) return 1;
  workflow.draftFinished("test-request", {{"ok", true}});
  if (!require(start(), "repeat search should work after completed draft")) return 1;
  if (!require(!workflow.confirmTargets(workflow.confirmationToken(), {"2"}, "keep people unchanged").value("accepted").toBool() && dispatch_count == 1,
               "nonmatching ids must not enter a draft")) return 1;
  if (!start()) return 1;
  const auto stale_token = workflow.confirmationToken();
  QImage changed(90, 70, QImage::Format_RGB32); changed.fill(Qt::red);
  changed.save(items[0].file_path);
  if (!require(!workflow.confirmTargets(stale_token, {"0", "1"}, "keep people unchanged").value("accepted").toBool() && dispatch_count == 1,
               "changed originals must reject the complete target set atomically")) return 1;
  if (!start()) return 1;
  items[0].adjustment_patch_json = canonical_asset_bound_adjustment_patch_json(QStringLiteral("{\"exposure\":0.5}"));
  catalog.replaceAssetsForTesting(items);
  if (!require(!workflow.confirmTargets(workflow.confirmationToken(), {"0"}, "keep people unchanged").value("accepted").toBool(),
               "changed edits must expire the confirmation")) return 1;
  if (!start()) return 1;
  const auto canceled_token = workflow.confirmationToken();
  workflow.cancel();
  if (!require(!workflow.confirmTargets(canceled_token, {"1"}, "keep people unchanged").value("accepted").toBool(),
               "cancel must revoke confirmation")) return 1;
  server.malformed = true;
  workflow.start("Find red scenes and warm the background.", server.model());
  if (!require(!wait() && workflow.confirmationToken().isEmpty() && dispatch_count == 1,
               "malformed planning output must leave no confirmable target set")) return 1;
  server.malformed = false;

  AgentEditController agent;
  agent.setCatalogModel(&catalog);
  agent.setSelectedModelId(QStringLiteral("rulebook-deterministic"));
  const auto first_before = catalog.assetById("0").value("adjustmentPatchJson");
  const auto outside_before = catalog.assetById("2").value("adjustmentPatchJson");
  const auto accepted = agent.startDraftForConfirmedAssets({"0"}, "Lift exposure slightly.");
  catalog.selectIndex(2);
  catalog.setVisualSearchResults({"2"});
  QElapsedTimer timer; timer.start();
  while (agent.running() && timer.elapsed() < 60000) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    QThread::msleep(1);
  }
  const auto report = agent.lastRunReport();
  if (!require(accepted.value("accepted").toBool() && !agent.running() && agent.draftActive() &&
               report.value("assetIds").toStringList() == QStringList{"0"} &&
               catalog.assetById("2").value("adjustmentPatchJson") == outside_before,
               "real draft must use confirmed ids even after focus and filters change")) {
    std::cerr << QJsonDocument::fromVariant(report).toJson().constData(); return 1;
  }
  if (!require(agent.rollbackDraft().value("ok").toBool() && catalog.assetById("0").value("adjustmentPatchJson") == first_before,
               "rollback must restore the confirmed original recipe")) return 1;
  const QVariantMap semantic_mask{{"id", "required-background"}, {"name", "Required background"},
      {"kind", "radial"}, {"enabled", true}, {"operation", "add"}, {"opacity", 1.0}, {"density", 1.0},
      {"centerX", 0.5}, {"centerY", 0.5}, {"radiusX", 0.5}, {"radiusY", 0.5}, {"feather", 0.0},
      {"temperature", 12.0}, {"agentSelectionMode", "semantic"}, {"agentSemanticTarget", "background"},
      {"agentRequireSemanticMask", true}};
  const auto failed_mask = agent.applyDraftRecipePatchForScope("confirmed-assets",
      {{"masks", QVariantMap{{"appendMaskStack", QVariantList{semantic_mask}}}}});
  if (!require(!failed_mask.value("ok").toBool() && !agent.draftActive() &&
               catalog.assetById("0").value("adjustmentPatchJson") == first_before,
               "unavailable semantic segmentation must not silently apply an ellipse to a confirmed target")) return 1;
  // A pre-existing clipping/cast warning must not turn a protected local edit
  // into global exposure or white-balance repairs. Explicit geometry here
  // isolates the repair policy from semantic generation (tested above).
  agent.setUserInstructions("Warm only the background and keep people unchanged.");
  auto explicit_mask = semantic_mask;
  explicit_mask.remove("agentSelectionMode");
  explicit_mask.remove("agentSemanticTarget");
  explicit_mask.remove("agentRequireSemanticMask");
  const auto local_report = agent.applyDraftRecipePatchForScope("confirmed-assets",
      {{"masks", QVariantMap{{"appendMaskStack", QVariantList{explicit_mask}}}}});
  const auto local_state = parse_adjustment_patch_json(catalog.assetById("0").value("adjustmentPatchJson").toString());
  const auto baseline_state = parse_adjustment_patch_json(first_before.toString());
  if (!require(local_report.value("ok").toBool() && local_state.state && baseline_state.state &&
               local_state.state->exposure == baseline_state.state->exposure &&
               local_state.state->temperature == baseline_state.state->temperature &&
               local_state.state->saturation == baseline_state.state->saturation,
               "quality repair must preserve global adjustments for a protected local edit")) return 1;
  const auto protected_patch = catalog.assetById("0").value("adjustmentPatchJson");
  const auto preserved_report = agent.lastRunReport();
  if (!require(!agent.fixReviewIssue("0").value("ok").toBool() &&
               catalog.assetById("0").value("adjustmentPatchJson") == protected_patch &&
               agent.lastRunReport() == preserved_report && agent.draftActive(),
               "generic Fix must not bypass local protection or discard the active review")) return 1;
  if (!require(agent.rollbackDraft().value("ok").toBool(), "protected local draft must remain reversible")) return 1;
  std::cout << "search_edit_workflow_contract=passed\n";
  return 0;
}
