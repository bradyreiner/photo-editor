#include "editor_preview_controller.h"
#include "photo_paint_surface.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonObject>
#include <QJsonDocument>
#include <sys/resource.h>
#include <QTemporaryDir>
#include <QThread>
#include <iostream>

namespace photo_editor::desktop {
struct EditorHistoryStorageTestAccess {
  static void setup(EditorPreviewController& controller, const QString& directory) {
    controller.history_service_.configureStorage(directory, 8 * 1024 * 1024);
    controller.asset_id_ = "history-storage-a";
  }
  static void append(EditorPreviewController& controller, const EditRecipeState& state, int index) {
    controller.applyState(state);
    controller.pushUndoState(state, QStringLiteral("Revision %1").arg(index));
  }
  static void stressSetup(EditorPreviewController& controller, const QString& directory, bool baseline) {
    if (baseline) controller.history_service_ = HistoryService{};
    else controller.history_service_.configureStorage(directory, 32 * 1024 * 1024);
    controller.asset_id_ = "history-storage-stress";
  }
  static void apply(EditorPreviewController& controller, const EditRecipeState& state) { controller.applyState(state); }
  static EditRecipeState current(const EditorPreviewController& controller) { return controller.currentState(); }
  static void wait(EditorPreviewController& controller) { controller.history_service_.waitForStorage(); }
  static QVariantMap stats(const EditorPreviewController& controller) { return controller.history_service_.storageStatistics(); }
  static int residentCount(const EditorPreviewController& controller) {
    int count = 0;
    for (const auto& revision : controller.history_service_.undoStackValue(controller.asset_id_))
      if (revision.residentState()) ++count;
    return count;
  }
  static void unpin(EditorPreviewController& controller) {
    for (const auto& revision : controller.history_service_.undoStackValue(controller.asset_id_)) revision.unpin();
    for (const auto& revision : controller.history_service_.redoStackValue(controller.asset_id_)) revision.unpin();
    wait(controller);
  }
  static bool formatReady(const EditorPreviewController& controller) {
    return !controller.history_format_busy_ && !controller.history_format_ready_key_.isEmpty();
  }
  static bool preview(const EditorPreviewController& controller) { return controller.history_preview_state_.has_value(); }
  static quint64 generation(const EditorPreviewController& controller) { return controller.history_state_generation_; }
  static bool pending(const EditorPreviewController& controller) { return controller.history_requested_index_ >= 0; }
};
}
using namespace photo_editor::desktop;
namespace {
int fail(const char* message) { std::cerr << message << '\n'; return 1; }
QByteArray bytes(int revision) { return QByteArray(1024 * 1024, static_cast<char>(revision)); }
EditRecipeState state(int revision) {
  EditRecipeState result;
  result.exposure = static_cast<double>(revision) / 100.0;
  result.recipe_snapshots = {QVariantMap{{"id", "nested-payload"}, {"binary", bytes(revision)}}};
  return result;
}
bool exact(const EditorPreviewController& controller, int revision) {
  const auto current = EditorHistoryStorageTestAccess::current(controller);
  return current.exposure == static_cast<double>(revision) / 100.0 &&
      current.recipe_snapshots == state(revision).recipe_snapshots;
}
bool until(const std::function<bool()>& done, int limit = 10000) {
  QElapsedTimer timer; timer.start();
  while (!done() && timer.elapsed() < limit) { QCoreApplication::processEvents(); QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); QThread::msleep(1); }
  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  return done();
}
void settle() { QElapsedTimer t; t.start(); until([&] { return t.elapsed() >= 150; }); }
}
int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  QTemporaryDir directory;
  if (!directory.isValid()) return fail("Temporary storage unavailable");
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", directory.path().toUtf8());
  qputenv("PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE", "1");
  if (app.arguments().contains("--stress")) {
    const bool baseline = app.arguments().contains("--baseline");
    const bool burst = app.arguments().contains("--burst");
    EditorPreviewController controller;
    EditorHistoryStorageTestAccess::stressSetup(controller, directory.path(), baseline);
    const auto revision = [](int i) {
      EditRecipeState value;
      value.exposure = static_cast<double>(i) / 100.0;
      value.recipe_snapshots = {QVariantMap{{"id", "nested-payload"}, {"binary", QByteArray(4 * 1024 * 1024, static_cast<char>(i))}}};
      return value;
    };
    QList<double> admission_ms;
    for (int i = 0; i < 64; ++i) {
      const auto value = revision(i);
      QElapsedTimer timer; timer.start();
      EditorHistoryStorageTestAccess::append(controller, value, i);
      admission_ms.append(static_cast<double>(timer.nsecsElapsed()) / 1e6);
      if (!burst) EditorHistoryStorageTestAccess::wait(controller);
    }
    EditorHistoryStorageTestAccess::apply(controller, revision(64));
    EditorHistoryStorageTestAccess::wait(controller);
    const auto resident_revisions = EditorHistoryStorageTestAccess::residentCount(controller);
    const auto stats = EditorHistoryStorageTestAccess::stats(controller);
    struct rusage captured_usage{}; getrusage(RUSAGE_SELF, &captured_usage);
    qint64 capture_peak = static_cast<qint64>(captured_usage.ru_maxrss);
#if !defined(Q_OS_MACOS)
    capture_peak *= 1024;
#endif
    for (int i = 63; i >= 0; --i) {
      controller.undo();
      if (!until([&] { return controller.editHistoryCurrentIndex() == i; }) ||
          EditorHistoryStorageTestAccess::current(controller).recipe_snapshots != revision(i).recipe_snapshots)
        return fail("Stress undo failed exact restoration");
    }
    for (int i = 1; i <= 64; ++i) {
      controller.redo();
      if (!until([&] { return controller.editHistoryCurrentIndex() == i; }) ||
          EditorHistoryStorageTestAccess::current(controller).recipe_snapshots != revision(i).recipe_snapshots)
        return fail("Stress redo failed exact restoration");
    }
    EditorHistoryStorageTestAccess::wait(controller);
    struct rusage usage{}; getrusage(RUSAGE_SELF, &usage);
    qint64 peak = static_cast<qint64>(usage.ru_maxrss);
#if !defined(Q_OS_MACOS)
    peak *= 1024;
#endif
    std::sort(admission_ms.begin(), admission_ms.end());
    const auto p95 = admission_ms.at(60);
    const QJsonObject result{{"baseline", baseline}, {"burst", burst}, {"revisions", 64}, {"payload_bytes", 4 * 1024 * 1024},
        {"admission_p95_ms", p95}, {"resident_revisions_after_capture", resident_revisions},
        {"peak_rss_bytes", peak}, {"capture_peak_rss_bytes", capture_peak}, {"exact_undo_redo", true}, {"storage", QJsonObject::fromVariantMap(stats)}};
    const auto output = app.arguments().indexOf("--output");
    if (output >= 0 && output + 1 < app.arguments().size()) {
      QFile file(app.arguments().at(output + 1));
      if (!file.open(QIODevice::WriteOnly)) return fail("Artifact unavailable");
      file.write(QJsonDocument(result).toJson());
    }
    std::cout << QJsonDocument(result).toJson(QJsonDocument::Compact).constData() << '\n';
    return 0;
  }
  EditorPreviewController controller;
  EditorHistoryStorageTestAccess::setup(controller, directory.path());
  for (int i = 0; i < 12; ++i) EditorHistoryStorageTestAccess::append(controller, state(i), i);
  EditorHistoryStorageTestAccess::apply(controller, state(12));
  EditorHistoryStorageTestAccess::wait(controller);
  if (controller.editHistoryCurrentIndex() != 12 || EditorHistoryStorageTestAccess::residentCount(controller) != 2 ||
      EditorHistoryStorageTestAccess::stats(controller).value("resident_bytes").toLongLong() > 8 * 1024 * 1024)
    return fail("History did not release older raw states or enforce its store budget");

  const auto before_no_op = EditorHistoryStorageTestAccess::generation(controller);
  if (!controller.restoreEditHistoryIndex(12) || EditorHistoryStorageTestAccess::generation(controller) != before_no_op)
    return fail("Restoring the current row should not reapply or persist the recipe");
  if (controller.editHistoryEntries().size() != 13 || !until([&] { return EditorHistoryStorageTestAccess::formatReady(controller); }))
    return fail("Background history descriptions did not complete");
  if (controller.editHistoryEntries().at(1).toMap().value("label").toString() != "Revision 0")
    return fail("History label changed during background formatting");
  if (!controller.restoreEditHistoryIndex(2) || controller.editHistoryCurrentIndex() != 12 || !exact(controller, 12))
    return fail("Pending restore mutated history before its payload was available");
  if (!until([&] { return controller.editHistoryCurrentIndex() == 2; }) || !exact(controller, 2))
    return fail("Spilled history jump did not restore exact state");
  controller.redo();
  if (!until([&] { return controller.editHistoryCurrentIndex() == 3; }) || !exact(controller, 3))
    return fail("Redo after a disk restore did not preserve state");
  controller.undo(); controller.undo();
  if (!until([&] { return controller.editHistoryCurrentIndex() == 1; }) || !exact(controller, 1))
    return fail("Repeated undo intent or labels were lost");

  EditorHistoryStorageTestAccess::unpin(controller);
  if (!controller.setEditHistoryPreviewIndex(0)) return fail("Spilled hover was rejected");
  controller.setEditHistoryPreviewIndex(-1);
  settle();
  if (EditorHistoryStorageTestAccess::preview(controller) || !exact(controller, 1) || controller.editHistoryCurrentIndex() != 1)
    return fail("Canceled hover changed the active edit or published late");

  if (!controller.restoreEditHistoryIndex(12) || !until([&] { return controller.editHistoryCurrentIndex() == 12; }))
    return fail("Could not restore timeline tip");
  EditorHistoryStorageTestAccess::unpin(controller);
  const auto id = QString::fromLatin1(QCryptographicHash::hash(bytes(0), QCryptographicHash::Sha256).toHex());
  const auto path = EditorHistoryStorageTestAccess::stats(controller).value("spill_directory").toString() + "/" + id + ".blob";
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) || file.write(bytes(99)) != bytes(99).size())
    return fail("Could not construct checksum failure");
  file.close();
  if (!controller.restoreEditHistoryIndex(0) || !until([&] { return !EditorHistoryStorageTestAccess::pending(controller); }))
    return fail("Corrupt restore did not finish with an error");
  if (controller.editHistoryCurrentIndex() != 12 || !exact(controller, 12) || !controller.status().contains("checksum"))
    return fail("Corrupt history advanced the cursor or damaged current edits");
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) || file.write(bytes(0)) != bytes(0).size()) return fail("Repair failed");
  file.close();
  if (!controller.restoreEditHistoryIndex(0) || !until([&] { return controller.editHistoryCurrentIndex() == 0; }) || !exact(controller, 0))
    return fail("A repaired payload could not be retried");

  EditorHistoryStorageTestAccess::unpin(controller);
  controller.restoreEditHistoryIndex(6);
  controller.setExposure(0.77);
  settle();
  if (controller.exposure() != 0.77 || controller.editHistoryCurrentIndex() == 6)
    return fail("A stale restore overwrote a newer edit");
  EditorHistoryStorageTestAccess::unpin(controller);
  controller.restoreEditHistoryIndex(0);
  controller.loadAsset("history-storage-b", {}, {}, {}, {}, {}, {});
  settle();
  if (controller.exposure() != 0.0 || controller.editHistoryCurrentIndex() != 0 || EditorHistoryStorageTestAccess::preview(controller))
    return fail("A late history result crossed an asset switch");
  {
    EditorPreviewController paint_controller;
    EditorHistoryStorageTestAccess::setup(paint_controller, directory.filePath("small-paint-history"));
    PhotoPaintSurface surface;
    surface.size = QSize(64, 64);
    surface.color_space = QColorSpace(QColorSpace::SRgbLinear);
    surface.payload = empty_photo_paint_tiles(surface.color_space);
    QImage tile(surface.size, QImage::Format_RGBA32FPx4_Premultiplied);
    tile.fill(Qt::red);
    surface.store(0, 0, tile);
    EditRecipeState painted;
    painted.layers = {QVariantMap{{"id", "small-paint"}, {"kind", "pixel"},
        {"pixelWidth", 64}, {"pixelHeight", 64}, {"canvasWidth", 64}, {"canvasHeight", 64},
        {"paintTiles", surface.snapshot()}, {"enabled", true}, {"opacity", 1.0}}};
    EditorHistoryStorageTestAccess::append(paint_controller, painted, 0);
    painted.exposure = 0.25;
    EditorHistoryStorageTestAccess::apply(paint_controller, painted);
    const auto entries = paint_controller.editHistoryEntries();
    if (entries.size() != 2 || entries.at(1).toMap().value("detail") != "Preparing edit details")
      return fail("Small compressed paint history should defer detailed diffing to a worker");
    if (!until([&] { return EditorHistoryStorageTestAccess::formatReady(paint_controller); }) ||
        paint_controller.editHistoryEntries().at(1).toMap().value("label") != "Revision 0")
      return fail("Deferred small-paint history lost the detailed label");
  }
  std::cout << "History ownership, worker restore, cursor safety, labels, hover cancellation, and stale results passed\n";
  return 0;
}
