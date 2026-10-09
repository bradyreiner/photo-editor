#include "export_job_controller.h"
#include "edit_recipe_state.h"
#include "edit_adjustment_patch.h"
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QSemaphore>
#include <QTemporaryDir>
#include <QThread>
#include <QThreadPool>
#include <QTimer>
#include <functional>
#include <iostream>
#include <memory>

namespace photo_editor::desktop {
struct ExportPreflightTestAccess {
  static QThreadPool& pool() { return ExportJobController::preflightThreadPool(); }
  static bool active(const ExportJobController& controller) { return bool(controller.active_preflight_); }
  static int builds(const ExportJobController& controller) { return controller.preflight_source_build_count_; }
  static QString firstPreviewSource(const ExportJobController& controller) {
    return controller.jobs_.isEmpty() ? QString{} : controller.jobs_.front().edited_preview_source;
  }
  static QString firstRecipe(const ExportJobController& controller) {
    return controller.jobs_.isEmpty() ? QString{} : controller.jobs_.front().recipe_json;
  }
  static void isolateAdmissions(ExportJobController& controller) {
    controller.admission_writer_ = [](PersistentJobRecord record) {
      record.status = "Queued";
      return ExportAdmissionResult{record, {}};
    };
  }
};
}
using namespace photo_editor::desktop;
namespace {
bool wait_until(const std::function<bool()>& ready) {
  QElapsedTimer timer;
  timer.start();
  while (!ready() && timer.elapsed() < 10000) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    QThread::msleep(1);
  }
  return ready();
}
int fail(const char* message) { std::cerr << message << '\n'; return 1; }
bool photo(const QString& path, int width) {
  QImage image(width, 80, QImage::Format_RGB32);
  image.fill(Qt::red);
  return image.save(path);
}
struct PoolGate {
  QSemaphore started;
  QSemaphore release;
  PoolGate() {
    ExportPreflightTestAccess::pool().start([this] { started.release(); release.acquire(); });
    started.acquire();
  }
  ~PoolGate() { release.release(); ExportPreflightTestAccess::pool().waitForDone(); }
};
}
int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  QTemporaryDir temporary;
  if (!temporary.isValid()) return fail("Temporary directory unavailable");
  const QDir root(temporary.path());
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", root.filePath("data").toUtf8());
  qputenv("PHOTO_EDITOR_CACHE_DIR", root.filePath("cache").toUtf8());
  qputenv("PHOTO_EDITOR_DATABASE_URL", "postgresql://127.0.0.1:1/export_fixture");
  const auto source = root.filePath("photo.png");
  const auto destination = root.filePath("output");
  if (!photo(source, 120) || !QDir().mkpath(destination)) return fail("Cannot create fixture");
  ExportJobController controller;
  ExportPreflightTestAccess::isolateAdmissions(controller);
  EditRecipeState state;
  state.output_color_space = "Display P3";
  state.soft_proof_enabled = true;
  state.soft_proof_profile = "sRGB";
  const auto recipe = serialize_edit_recipe_json(state);
  QVariantMap asset{{"assetId", "fixture"}, {"mediaType", "photo"}, {"filePath", source},
      {"adjustmentPatchJson", recipe}, {"displayLabel", "photo"}};
  QVariantMap scopes{{"histogramSamples", 100}, {"shadowClip", 3}, {"highlightClip", 4},
      {"rawSamples", 80}, {"rawShadowClip", 1}};
  QVariantMap request{{"single", asset}, {"assets", QVariantList{asset}}, {"preset", "Web Proof"},
      {"destination", destination}, {"scopes", scopes}};
  const auto reference = controller.exportPreflight("photo", source, {}, recipe, "Web Proof", destination, 100, 3, 4, 80, 1, 0);
  const auto batch_reference = controller.batchExportPreflight({asset}, "Web Proof", destination);
  if (!reference.value("ok").toBool() || reference.value("estimatedPixelWidth").toInt() != 120 ||
      reference.value("warningCount").toInt() < 4) return fail("Baseline fixture invariants failed");
  int ticks = 0;
  QTimer heartbeat;
  heartbeat.setInterval(1);
  QObject::connect(&heartbeat, &QTimer::timeout, [&] { ++ticks; });
  heartbeat.start();
  {
    PoolGate gate;
    controller.requestPreflight(request);
    if (!controller.preflightBusy() || !controller.singlePreflight().isEmpty() ||
        !wait_until([&] { return ticks >= 10 && ExportPreflightTestAccess::active(controller); }))
      return fail("Export review blocked the UI or published before worker completion");
  }
  if (!wait_until([&] { return !controller.preflightBusy(); }) ||
      controller.singlePreflight() != reference || controller.batchPreflight() != batch_reference)
    return fail("Async source, clipping, destination, or batch report differs");
  const auto builds = ExportPreflightTestAccess::builds(controller);
  if (builds != 1) return fail("Single and batch duplicated identical source checks");
  controller.requestPreflight(request);
  if (controller.preflightBusy()) return fail("Identical completed report was not deduplicated");
  scopes["shadowClip"] = 0;
  scopes["highlightClip"] = 0;
  request["scopes"] = scopes;
  request["destination"] = root.filePath("new-output");
  controller.requestPreflight(request);
  if (!wait_until([&] { return !controller.preflightBusy(); }) ||
      ExportPreflightTestAccess::builds(controller) != builds ||
      !controller.singlePreflight().value("outputPath").toString().contains("new-output"))
    return fail("Scope/destination update rebuilt the source or retained old output");
  if (!photo(source, 130)) return fail("Cannot revise source");
  controller.requestPreflight(request, true);
  if (!wait_until([&] { return !controller.preflightBusy(); }) ||
      controller.singlePreflight().value("estimatedPixelWidth").toInt() != 130 ||
      ExportPreflightTestAccess::builds(controller) != builds + 1)
    return fail("Source revision reused stale source checks");
  {
    PoolGate gate;
    controller.requestPreflight(request, true);
    if (!wait_until([&] { return ExportPreflightTestAccess::active(controller); })) return fail("Worker not queued");
    for (int i = 0; i < 20; ++i) {
      request["destination"] = root.filePath(QString("latest-%1").arg(i));
      controller.requestPreflight(request);
    }
  }
  if (!wait_until([&] { return !controller.preflightBusy(); }) ||
      !controller.singlePreflight().value("outputPath").toString().contains("latest-19"))
    return fail("Superseded report overwrote latest destination");
  controller.requestPreflight(request, true);
  if (!wait_until([&] { return ExportPreflightTestAccess::active(controller); })) return fail("Late worker not queued");
  ExportPreflightTestAccess::pool().waitForDone();
  controller.cancelPreflight();
  request["destination"] = destination;
  controller.requestPreflight(request);
  if (!wait_until([&] { return !controller.preflightBusy(); }) ||
      controller.batchPreflight().value("destinationPath").toString() != destination)
    return fail("Completed stale report survived cancellation");
  // Registry-backed custom profiles must be checked anew, even with identical files.
  state.export_icc_profile_asset.asset_id = "icc:preflight-missing";
  state.export_icc_profile_asset.payload_sha256 = QString(64, QLatin1Char('0'));
  state.export_icc_profile_asset.byte_size = 128;
  auto custom_asset = asset;
  custom_asset["adjustmentPatchJson"] = serialize_edit_recipe_json(state);
  auto custom_request = request;
  custom_request["single"] = custom_asset;
  custom_request["assets"] = QVariantList{};
  controller.requestPreflight(custom_request);
  if (!wait_until([&] { return !controller.preflightBusy(); }) || controller.singlePreflight().value("ok").toBool())
    return fail("Missing custom profile did not block export");
  const auto custom_builds = ExportPreflightTestAccess::builds(controller);
  controller.requestPreflight(custom_request, true);
  if (!wait_until([&] { return !controller.preflightBusy(); }) ||
      ExportPreflightTestAccess::builds(controller) != custom_builds + 1)
    return fail("Mutable custom profile result was cached");
  {
    PoolGate gate;
    if (!controller.queueBatchExportAsync({asset}, "Web Proof", destination, {}, 0, 0, 0, 0) ||
        !controller.batchSubmissionBusy() || !QFile::remove(source)) return fail("Async submission fixture failed");
  }
  if (!wait_until([&] { return !controller.batchSubmissionBusy(); }) || controller.jobCount() != 0)
    return fail("Submission accepted a source removed after review");
  if (!photo(source, 130)) return fail("Cannot restore source");
  const auto preview = root.filePath("edited-preview.png");
  if (!photo(preview, 96)) return fail("Cannot create edited preview fixture");
  auto current_asset = asset;
  current_asset.insert("editedPreviewSource", preview);
  const auto planned = controller.batchExportPreflight({current_asset}, "Web Proof", destination)
      .value("items").toList().at(0).toMap().value("outputPath").toString();
  if (!controller.queueBatchExportAsync({current_asset}, "Web Proof", destination, {}, 0, 0, 0, 0))
    return fail("Valid submission rejected");
  ExportPreflightTestAccess::pool().waitForDone();
  QFile collision(planned);
  if (!collision.open(QIODevice::WriteOnly) || collision.write("preserve") != 8) return fail("Cannot create late collision");
  collision.close();
  if (!wait_until([&] { return !controller.batchSubmissionBusy() && controller.pendingAdmissionCount() == 0; }) ||
      controller.jobCount() != 1 || controller.jobs().at(0).toMap().value("outputPath").toString() == planned ||
      ExportPreflightTestAccess::firstPreviewSource(controller) != preview ||
      ExportPreflightTestAccess::firstRecipe(controller) != canonical_asset_bound_adjustment_patch_json(recipe))
    return fail("Late collision, current preview, or source recipe was not preserved through admission");
  {
    PoolGate gate;
    auto closing = std::make_unique<ExportJobController>();
    closing->requestPreflight(request, true);
    if (!wait_until([&] { return ExportPreflightTestAccess::active(*closing); })) return fail("Closing worker not queued");
    closing.reset();
  }
  ExportJobController::finishPendingAdmissions();
  std::cout << "Export report parity, caching, revision checks, stale rejection, fresh admission, collision and destruction passed\n";
  return 0;
}
