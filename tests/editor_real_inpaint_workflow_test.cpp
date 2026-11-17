#include <cstdlib>
#include <functional>
#include <iostream>
#include <stdexcept>

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QThread>

#include "ai_model_runtime.h"
#include "edit_adjustment_patch.h"
#include "editor_preview_controller.h"
#include "flattened_output_renderer.h"
#include "mask_workflow_ownership.h"

using namespace photo_editor::desktop;

namespace photo_editor::desktop {
struct EditorPreviewControllerTestAccess {
  static void cancel_test_preview(EditorPreviewController& c) {
    c.cancelPendingAssetScopedWork();
    c.updateProcessingState();
  }
  static QVariantMap pending_work(const EditorPreviewController& c) {
    return {{"interactiveTimer", c.render_scheduler_.interactiveActive()},
        {"transactionPreviewTimer", c.edit_transaction_interactive_preview_timer_.isActive()},
        {"fullTimer", c.render_scheduler_.fullActive()}, {"overlayRunning", c.overlay_render_watcher_.isRunning()},
        {"overlayPending", c.overlay_request_pending_}, {"interactiveRunning", c.interactive_render_watcher_.isRunning()},
        {"interactiveResultPending", c.interactive_render_result_pending_}, {"fullRunning", c.full_render_watcher_.isRunning()},
        {"brushRunning", c.mask_worker_coordinator_.brushRasterCommitWatcher().isRunning()}, {"brushActive", c.brush_raster_commit_active_},
        {"inpaintActive", c.inpaint_progress_active_}, {"inpaintApplyRunning", c.mask_worker_coordinator_.inpaintApplyWatcher().isRunning()},
        {"inpaintRegenerateRunning", c.mask_worker_coordinator_.inpaintRegenerateWatcher().isRunning()},
        {"brushScheduled", c.pending_brush_commit_scheduled_}, {"brushQueue", c.pending_brush_commits_.size()},
        {"smartRunning", c.mask_worker_coordinator_.smartSelectCommitWatcher().isRunning()},
        {"smartPending", c.mask_worker_coordinator_.smartSelectCommitPending()},
        {"interactivePending", c.interactive_request_pending_}, {"fullPending", c.full_request_pending_}};
  }
};
}  // namespace photo_editor::desktop

namespace {
void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}
bool settle(const std::function<bool()>& condition, int timeout = 120000) {
  QElapsedTimer timer;
  timer.start();
  while (!condition() && timer.elapsed() < timeout) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    QThread::msleep(2);
  }
  return condition();
}
EditRecipeState state(const EditorPreviewController& controller) {
  const auto decoded = parse_adjustment_patch_json(controller.currentAdjustmentPatchJson());
  require(decoded.ok(), "Current controller recipe must decode");
  return *decoded.state;
}
void diagnose_mask_stack(const QVariantList& expected, const QVariantList& actual) {
  std::cerr << "mask_stack_diff expected_count=" << expected.size() << " actual_count=" << actual.size() << '\n';
  for (qsizetype i = 0; i < std::min(expected.size(), actual.size()); ++i) {
    const auto wanted = expected[i].toMap();
    const auto found = actual[i].toMap();
    auto keys = wanted.keys();
    for (const auto& key : found.keys()) if (!keys.contains(key)) keys.append(key);
    for (const auto& key : keys) {
      if (wanted.value(key) == found.value(key)) continue;
      std::cerr << "mask_stack_field=" << i << ':' << key.toStdString();
      if (wanted.value(key).metaType().id() == QMetaType::QByteArray || found.value(key).metaType().id() == QMetaType::QByteArray)
        std::cerr << " expected_bytes=" << wanted.value(key).toByteArray().size() << " actual_bytes=" << found.value(key).toByteArray().size();
      else std::cerr << " expected=" << wanted.value(key).toString().toStdString() << " actual=" << found.value(key).toString().toStdString();
      std::cerr << '\n';
    }
  }
}
bool settle_repair(EditorPreviewController& controller, const char* phase, int timeout = 120000) {
  QElapsedTimer timer;
  timer.start();
  qint64 next_report = 0;
  while ((controller.inpaintProgressActive() || controller.processing()) && timer.elapsed() < timeout) {
    if (timer.elapsed() >= next_report) {
      const auto mask = controller.hasActiveMask() ? controller.maskStack()[controller.activeMaskIndex()].toMap() : QVariantMap{};
      std::cerr << "repair_phase=" << phase << " elapsed_ms=" << timer.elapsed()
                << " progress=" << controller.inpaintProgressValue() << " progress_active=" << controller.inpaintProgressActive()
                << " processing=" << controller.processing() << " full_render=" << controller.fullRenderRunning()
                << " committed=" << mask.value(QStringLiteral("committed")).toBool()
                << " region=" << mask.value(QStringLiteral("segmentationWidth")).toInt() << 'x' << mask.value(QStringLiteral("segmentationHeight")).toInt()
                << " patch=" << mask.value(QStringLiteral("inpaintPatchWidth")).toInt() << 'x' << mask.value(QStringLiteral("inpaintPatchHeight")).toInt()
                << " model=" << mask.value(QStringLiteral("inpaintModelName")).toString().toStdString()
                << " label=" << controller.inpaintProgressLabel().toStdString()
                << " status=" << controller.status().toStdString() << '\n';
      std::cerr << "repair_pending_flags=" << QJsonDocument(QJsonObject::fromVariantMap(
          EditorPreviewControllerTestAccess::pending_work(controller))).toJson(QJsonDocument::Compact).constData() << '\n';
      next_report = timer.elapsed() + 10000;
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    QThread::msleep(2);
  }
  const auto complete = !controller.inpaintProgressActive() && !controller.processing();
  std::cerr << "repair_phase=" << phase << " completed=" << complete << " elapsed_ms=" << timer.elapsed() << '\n';
  if (!complete) {
    controller.cancelActiveRetouch();
    EditorPreviewControllerTestAccess::cancel_test_preview(controller);
    const auto drained = settle([&] { return !controller.processing(); }, 5000);
    std::cerr << "repair_cancel_drain=" << drained << " phase=" << phase << '\n';
  }
  return complete;
}
}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  std::cerr << "isolated_test_pid=" << QCoreApplication::applicationPid() << '\n';
  QTemporaryDir runtime;
  if (!runtime.isValid() || argc < 2) return EXIT_FAILURE;
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", runtime.path().toUtf8());
  qputenv("PHOTO_EDITOR_CACHE_DIR", (runtime.path() + QStringLiteral("/cache")).toUtf8());
  qputenv("PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE", "1");
#ifdef PHOTO_EDITOR_TEST_AI_MODEL_BUNDLE
  qputenv("PHOTO_EDITOR_AI_MODEL_DIR", QByteArray(PHOTO_EDITOR_TEST_AI_MODEL_BUNDLE));
#endif
  try {
    require(!qEnvironmentVariableIsSet("PHOTO_EDITOR_TEST_FAKE_SEGMENTATION"), "Real-model proof cannot run with fake segmentation enabled");
    const auto readiness = ai_model_runtime_status();
    if (!readiness.bundle_verified || !readiness.inpaint_artifact_verified || !readiness.inpaint_available) {
      std::cerr << readiness.summary.toStdString() << '\n' << readiness.diagnostics.toStdString() << '\n';
      throw std::runtime_error("Packaged LaMa runtime must be ready; missing model is a failure, never a skipped proof");
    }
    const auto source_path = QString::fromLocal8Bit(argv[1]);
    const QImage source(source_path);
    require(!source.isNull(), "Real photographic fixture must decode");
    EditorPreviewController spot_controller;
    spot_controller.loadAsset(QStringLiteral("default-spot-photo"), source_path, {}, {}, {}, {}, {});
    require(settle([&] { return !spot_controller.processing(); }), "Default spot photograph must load");
    spot_controller.startRemoveBrush();
    require(spot_controller.activeMaskBrushSize() == 0.08, "Spot proof must use the actual default brush size");
    spot_controller.beginActiveBrushSourceStroke(QStringLiteral("add"));
    spot_controller.addBrushPoint(0.12, 0.12);
    spot_controller.endActiveBrushStroke();
    spot_controller.applyActiveRetouch();
    require(settle_repair(spot_controller, "default-spot"),
        "Immediate Apply on the default spot brush must settle");
    const auto spot = spot_controller.maskStack()[spot_controller.activeMaskIndex()].toMap();
    require(spot.value(QStringLiteral("committed")).toBool() && !spot.value(QStringLiteral("inpaintPatchData")).toByteArray().isEmpty() &&
        spot.value(QStringLiteral("inpaintRegionKind")).toString() == QStringLiteral("spot") &&
        spot.value(QStringLiteral("inpaintModelName")).toString() == QStringLiteral("cpu-diffusion-inpaint-v1") &&
        spot_controller.status().contains(QStringLiteral("Local CPU Fill")),
        "Default small-spot repair must materialize its intended local algorithm and label it accurately");
    EditorPreviewController controller;
    controller.loadAsset(QStringLiteral("real-inpaint-photo"), source_path, {}, {}, {}, {}, {});
    require(settle([&] { return !controller.processing(); }), "Photograph must finish loading");
    const auto before = state(controller);
    controller.startRemoveBrush();
    // A small blemish intentionally chooses the existing local spot-repair
    // algorithm. Use an object-sized normal brush region to exercise LaMa.
    controller.setActiveMaskBrushSize(0.22);
    const auto repair_id = retouch_repair_id(controller.maskStack()[controller.activeMaskIndex()].toMap());
    require(!repair_id.isEmpty(), "Remove must establish stable repair ownership");
    controller.beginActiveBrushSourceStroke(QStringLiteral("add"));
    controller.addBrushPoint(0.36, 0.39);
    controller.addBrushPoint(0.39, 0.41);
    controller.addBrushPoint(0.42, 0.43);
    controller.endActiveBrushStroke();
    controller.setInpaintQualityMode(QStringLiteral("preview"));
    controller.applyActiveRetouch(); // Deliberately before queued raster work settles.
    require(controller.inpaintProgressActive(), "Immediate Apply must visibly wait for its pending repair region");
    require(settle([&] {
      return !controller.brushStrokeActive() && !controller.activeMaskCommitted() &&
          !controller.maskStack()[controller.activeMaskIndex()].toMap().value(QStringLiteral("segmentationData")).toByteArray().isEmpty();
    }), "Production Remove brush must materialize a usable raster region before Apply");
    const auto pending = controller.maskStack();
    const auto history_before_apply = controller.editHistoryEntries().size();
    require(settle_repair(controller, "object-apply"),
        "Real controller Apply must terminate with a result or actionable failure");
    const auto applied = controller.maskStack()[controller.activeMaskIndex()].toMap();
    std::cout << "controller_status=" << controller.status().toStdString() << '\n';
    require(applied.value(QStringLiteral("committed")).toBool() &&
        !applied.value(QStringLiteral("inpaintPatchData")).toByteArray().isEmpty(),
        "Real Remove Apply must commit a materialized patch");
    require(applied.value(QStringLiteral("inpaintModelName")).toString() == QStringLiteral("lama-onnx") &&
        !applied.value(QStringLiteral("inpaintFallback")).toBool(), "Controller Remove must use the verified real model, not fallback");
    require(!applied.value(QStringLiteral("inpaintRuntimeProvider")).toString().trimmed().isEmpty(),
        "Actual real-backend provider must survive publication and normalization before any passing receipt");
    require(retouch_repair_id(applied) == repair_id && !retouch_patch_requires_final(applied) &&
        applied.value(QStringLiteral("inpaintQualityMode")).toString() == QStringLiteral("final"),
        "Apply must finalize the same repair even when preview quality was selected");
    require(controller.editHistoryEntries().size() == history_before_apply + 1,
        "Final Apply must create one history entry");
    const auto accepted_raw = controller.maskStack();
    const auto accepted_state = state(controller);
    const auto render = [&](const EditRecipeState& recipe) {
      return render_flattened_output_file(QStringLiteral("real-inpaint-photo"), source_path, recipe,
          QColorSpace(QColorSpace::SRgb), QImage::Format_RGBA64);
    };
    const auto baseline_pixels = render(before);
    const auto accepted_pixels = render(accepted_state);
    require(!baseline_pixels.isNull() && !accepted_pixels.isNull() && accepted_pixels != baseline_pixels,
        "A real materialized repair must change canonical output pixels");
    controller.undo();
    require(settle([&] { return controller.maskStack() == pending; }), "One Undo must restore the exact pending repair");
    controller.redo();
    const auto redo_exact = settle([&] { return controller.maskStack() == accepted_raw; });
    if (!redo_exact) diagnose_mask_stack(accepted_raw, controller.maskStack());
    require(redo_exact && state(controller).mask_stack == accepted_state.mask_stack,
        "One Redo must restore exact accepted runtime state and persisted repair bytes");
    EditorPreviewController reopened;
    reopened.loadAsset(QStringLiteral("real-inpaint-reopened"), source_path, {}, {}, {}, {},
        serialize_edit_recipe_json(accepted_state));
    require(settle([&] { return !reopened.processing(); }), "Reopened repaired photograph must finish rendering");
    const auto reopened_state = state(reopened);
    require(reopened_state.mask_stack == accepted_state.mask_stack && render(reopened_state) == accepted_pixels,
        "Saved/reopened final repair must preserve identity, patch and rendered/export pixels");
    require(controller.regenerateActiveInpaintPatch(), "Accepted real patch must support regeneration");
    controller.cancelActiveRetouch();
    require(settle([&] { return !controller.processing(); }), "Real regeneration cancellation must settle");
    require(controller.maskStack() == accepted_raw && state(controller).mask_stack == accepted_state.mask_stack,
        "Cancelling real regeneration must preserve the accepted patch and repair identity");
    const auto source_id = applied.value(QStringLiteral("id")).toString();
    const auto paint_refinement = [&](bool subtract, double x, double y) {
      controller.startRemoveRefineBrush(subtract);
      require(retouch_repair_id(controller.maskStack()[controller.activeMaskIndex()].toMap()) == repair_id,
          "Add/Subtract must stay in the same accepted repair");
      controller.beginActiveBrushSourceStroke();
      controller.addBrushPoint(x, y);
      controller.addBrushPoint(x + 0.04, y + 0.03);
      controller.endActiveBrushStroke();
    };
    paint_refinement(false, 0.52, 0.48);
    paint_refinement(true, 0.37, 0.40);
    controller.applyActiveRetouch();
    require(settle([&] {
      const auto masks = controller.maskStack();
      if (masks.size() != 3) return false;
      for (const auto& value : masks) {
        const auto mask = value.toMap();
        if (mask.value(QStringLiteral("committed")).toBool() || mask.value(QStringLiteral("segmentationData")).toByteArray().isEmpty()) return false;
      }
      return true;
    }), "Real Add/Subtract repair must materialize each pending source before inference acceptance");
    const auto multi_draft = controller.maskStack();
    const auto multi_history = controller.editHistoryEntries().size();
    require(render(state(controller)) == accepted_pixels,
        "Generating a refined repair must preserve the previous accepted pixels");
    require(settle_repair(controller, "compound-apply"),
        "Real multi-component repair Apply must settle");
    const auto multi_accepted_raw = controller.maskStack();
    const auto multi_accepted = state(controller);
    require(multi_accepted.mask_stack.size() == 1 &&
        multi_accepted.mask_stack[0].toMap().value(QStringLiteral("id")).toString() == source_id &&
        retouch_repair_id(multi_accepted.mask_stack[0].toMap()) == repair_id &&
        !multi_accepted.mask_stack[0].toMap().value(QStringLiteral("inpaintPatchData")).toByteArray().isEmpty(),
        "Accepted Add/Subtract repair must consolidate to one patch with the original stable repair/source IDs");
    require(controller.editHistoryEntries().size() == multi_history + 1,
        "Consolidating a repair must be one global history transaction");
    const auto final_pixels = render(multi_accepted);
    require(!final_pixels.isNull(), "Consolidated repair must render");
    controller.undo();
    require(settle([&] { return controller.maskStack() == multi_draft; }) && render(state(controller)) == accepted_pixels,
        "One Undo must restore all exact Add/Subtract draft components and previous accepted pixels");
    controller.redo();
    require(settle([&] { return controller.maskStack() == multi_accepted_raw; }) &&
        state(controller).mask_stack == multi_accepted.mask_stack && render(state(controller)) == final_pixels,
        "One Redo must restore the exact single accepted patch and pixels");
    paint_refinement(false, 0.65, 0.3);
    require(settle([&] { return !controller.processing(); }), "Reopened repair brush must settle");
    require(render(state(controller)) == final_pixels, "Reopening and painting a repair must keep prior accepted output visible");
    controller.discardActiveRetouch();
    require(controller.maskCount() == 1 && controller.activeMaskCommitted() && render(state(controller)) == final_pixels,
        "Discard repair draft must restore its prior accepted patch instead of deleting it");
    if (argc >= 3) {
      const QDir output(QString::fromLocal8Bit(argv[2]));
      require(QDir().mkpath(output.absolutePath()), "Evidence output directory must exist");
      require(baseline_pixels.save(output.filePath(QStringLiteral("real-inpaint-before.png"))) &&
          final_pixels.save(output.filePath(QStringLiteral("real-inpaint-after.png"))), "Real repair evidence pixels must save");
      QJsonObject receipt{{QStringLiteral("schema"), QStringLiteral("looksy-real-inpaint-controller:v1")},
          {QStringLiteral("status"), QStringLiteral("passed")}, {QStringLiteral("repairId"), repair_id},
          {QStringLiteral("defaultSpotLocalFill"), true},
          {QStringLiteral("model"), applied.value(QStringLiteral("inpaintModelName")).toString()},
          {QStringLiteral("provider"), applied.value(QStringLiteral("inpaintRuntimeProvider")).toString()},
          {QStringLiteral("finalQuality"), true}, {QStringLiteral("undoRedoExact"), true},
          {QStringLiteral("reopenPixelsExact"), true}, {QStringLiteral("cancelPreservesAccepted"), true},
          {QStringLiteral("multiComponentOnePatch"), true}, {QStringLiteral("stableSourceId"), source_id},
          {QStringLiteral("draftPreservesAcceptedPixels"), true}, {QStringLiteral("discardRestoresAccepted"), true}};
      QFile fixture(source_path);
      require(fixture.open(QIODevice::ReadOnly), "Photographic source must remain readable");
      receipt.insert(QStringLiteral("sourceSha256"), QString::fromLatin1(
          QCryptographicHash::hash(fixture.readAll(), QCryptographicHash::Sha256).toHex()));
      QFile evidence(output.filePath(QStringLiteral("real-inpaint-controller.json")));
      require(evidence.open(QIODevice::WriteOnly) && evidence.write(QJsonDocument(receipt).toJson()) > 0,
          "Real repair receipt must save");
    }
    std::cout << "Real production-controller brush raster -> LaMa Apply -> final patch -> undo/reopen/render -> cancel passed\n";
    return EXIT_SUCCESS;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return EXIT_FAILURE;
  }
}
