#include "editor_preview_controller.h"
#include "editor_preview_image_sink.h"

#include <QColorSpace>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFile>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QSemaphore>
#include <QTemporaryDir>
#include <QThread>
#include <QThreadPool>
#include <QtConcurrent>
#include <iostream>

namespace photo_editor::desktop {
struct EditorPreviewControllerTestAccess {
  static int hits(const EditorPreviewController& c) { return c.disk_preview_cache_hit_count_; }
  static bool full_running(const EditorPreviewController& c) { return c.full_render_watcher_.isRunning(); }
  static void retry(EditorPreviewController& c) { c.renderPreview(true, QStringLiteral("preview_retry")); }
  static QString cache_path(const EditorPreviewController& c) { return c.cachePathForSelection(true, true); }
  static void finish_worker_without_delivery(EditorPreviewController& c) {
    c.full_render_watcher_.future().waitForFinished();
  }
};
}

namespace {
using namespace photo_editor::desktop;
QByteArray digest(const QImage& image) {
  const auto rgba = image.convertToFormat(QImage::Format_RGBA8888);
  return QCryptographicHash::hash(
      QByteArrayView(reinterpret_cast<const char*>(rgba.constBits()), rgba.sizeInBytes()),
      QCryptographicHash::Sha256).toHex();
}
struct Sink : EditorPreviewImageSink {
  QImage edited;
  QList<QImage> publications;
  void setImage(const QString& key, const QImage& image) override {
    if (key == QStringLiteral("edited")) { edited = image; publications.push_back(image); }
  }
  void clearImage(const QString& key) override {
    if (key == QStringLiteral("edited")) edited = {};
  }
};
bool settle(EditorPreviewController& c) {
  QElapsedTimer timer;
  timer.start();
  do {
    QCoreApplication::processEvents();
    if (!c.processing() && !EditorPreviewControllerTestAccess::full_running(c)) return true;
    QThread::msleep(1);
  } while (timer.elapsed() < 120000);
  return false;
}
void load(EditorPreviewController& c, const QString& path, const QString& id) {
  c.loadAsset(id, path, {}, {}, {}, {}, {});
}
}

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  auto args = app.arguments();
  const bool baseline = args.removeOne(QStringLiteral("--baseline"));
  if (args.size() != 1 && args.size() != 3) return 2;
  QTemporaryDir runtime;
  if (!runtime.isValid()) return 2;
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", runtime.path().toUtf8());
  qputenv("PHOTO_EDITOR_CACHE_DIR", runtime.filePath("cache").toUtf8());
  qputenv("PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE", "1");
  QString source = args.size() == 3 ? args[1] : runtime.filePath("first.png");
  if (args.size() == 1) {
    QImage fixture(1024, 768, QImage::Format_ARGB32);
    fixture.setColorSpace(QColorSpace(QColorSpace::DisplayP3));
    for (int y = 0; y < fixture.height(); ++y) for (int x = 0; x < fixture.width(); ++x)
      fixture.setPixelColor(x, y, QColor(x % 220, y % 220, (x + y) % 220));
    if (!fixture.save(source)) return 2;
  }
  const auto second = runtime.filePath("second.png");
  QImage fixture(160, 120, QImage::Format_ARGB32);
  fixture.fill(QColor(20, 50, 190));
  fixture.setColorSpace(QColorSpace(QColorSpace::SRgb));
  if (!fixture.save(second)) return 2;
  Sink sink;
  EditorPreviewController controller;
  controller.setImageProvider(&sink);
  // Seed each persistent cache, then read the JPEG once to establish the exact
  // warm-cache pixels (the encoded cache is not lossless relative to cold work).
  load(controller, second, "second-seed");
  if (!settle(controller)) return 1;
  load(controller, second, "second-warm");
  if (!settle(controller) || sink.edited.isNull()) return 1;
  const auto second_hash = digest(sink.edited);
  load(controller, source, "first-seed");
  if (!settle(controller)) return 1;
  load(controller, source, "first-warm");
  if (!settle(controller) || sink.edited.isNull()) return 1;
  const auto expected_hash = digest(sink.edited);
  const auto expected_space = sink.edited.colorSpace();
  const auto expected_size = sink.edited.size();
  QVariantList samples;
  for (int index = 0; index < 12; ++index) {
    sink.publications.clear();
    const auto hits_before = EditorPreviewControllerTestAccess::hits(controller);
    QElapsedTimer timer;
    timer.start();
    load(controller, source, QStringLiteral("warm-%1").arg(index));
    const auto admission_ms = double(timer.nsecsElapsed()) / 1e6;
    if (!settle(controller)) return 1;
    const auto ready_ms = double(timer.nsecsElapsed()) / 1e6;
    const auto actual_hash = digest(sink.edited);
    if (actual_hash != expected_hash || sink.edited.colorSpace() != expected_space ||
        EditorPreviewControllerTestAccess::hits(controller) != hits_before + 1) return 1;
    samples.push_back(QVariantMap{{"admissionMs", admission_ms}, {"readyMs", ready_ms},
        {"rgbaSha256", QString::fromLatin1(actual_hash)}, {"cacheHit", true}});
  }
  // Block the worker queue, not the GUI. The first selection must not decode or
  // publish inline, and a replacement selection must supersede its queued work.
  auto* pool = QThreadPool::globalInstance();
  pool->waitForDone();
  const auto threads = pool->maxThreadCount();
  pool->setMaxThreadCount(1);
  QSemaphore entered, release;
  auto held = QtConcurrent::run([&] { entered.release(); release.acquire(); });
  entered.acquire();
  sink.publications.clear();
  const auto hits_before = EditorPreviewControllerTestAccess::hits(controller);
  QElapsedTimer admission;
  admission.start();
  load(controller, source, "held-first");
  const auto held_admission_ms = double(admission.nsecsElapsed()) / 1e6;
  const bool queued = EditorPreviewControllerTestAccess::full_running(controller) &&
      EditorPreviewControllerTestAccess::hits(controller) == hits_before && sink.edited.isNull();
  load(controller, second, "held-second");
  int event_turns = 0;
  QElapsedTimer event_timer;
  event_timer.start();
  while (event_timer.elapsed() < 100) { QCoreApplication::processEvents(); ++event_turns; QThread::msleep(1); }
  const bool no_early_publication = sink.publications.isEmpty();
  release.release();
  held.waitForFinished();
  pool->setMaxThreadCount(threads);
  if (!settle(controller)) return 1;
  bool newest_only = !sink.publications.isEmpty();
  for (const auto& published : sink.publications) newest_only = newest_only && digest(published) == second_hash;
  const bool async_passed = queued && no_early_publication && newest_only && event_turns > 0;
  bool recovery_passed = true;
  if (args.size() == 1 && !baseline) {
    // A completed old future whose queued notification has not reached the GUI
    // must not publish after a new asset selection either.
    sink.publications.clear();
    load(controller, source, "completed-old");
    EditorPreviewControllerTestAccess::finish_worker_without_delivery(controller);
    load(controller, second, "replace-before-delivery");
    if (!settle(controller)) return 1;
    for (const auto& published : sink.publications)
      recovery_passed = recovery_passed && digest(published) == second_hash;
    recovery_passed = recovery_passed && !sink.publications.isEmpty();

    // Retry must use the same asynchronous path. Evict only this test profile's
    // cache after admission: the worker must safely fall back and repopulate it.
    pool->waitForDone();
    pool->setMaxThreadCount(1);
    QSemaphore retry_entered, release_retry;
    auto held_retry = QtConcurrent::run([&] { retry_entered.release(); release_retry.acquire(); });
    retry_entered.acquire();
    sink.publications.clear();
    const auto retry_hits = EditorPreviewControllerTestAccess::hits(controller);
    EditorPreviewControllerTestAccess::retry(controller);
    const auto cache = EditorPreviewControllerTestAccess::cache_path(controller);
    const bool retry_queued = EditorPreviewControllerTestAccess::full_running(controller) &&
        sink.publications.isEmpty() && EditorPreviewControllerTestAccess::hits(controller) == retry_hits;
    const bool evicted = cache.startsWith(runtime.path() + QLatin1Char('/')) &&
        QFile::rename(cache, cache + QStringLiteral(".evicted"));
    release_retry.release();
    held_retry.waitForFinished();
    pool->setMaxThreadCount(threads);
    recovery_passed = recovery_passed && retry_queued && evicted && settle(controller) &&
        !sink.edited.isNull() && QFileInfo::exists(cache) &&
        EditorPreviewControllerTestAccess::hits(controller) == retry_hits;
    // The fallback may use pre-encoding pixels. A subsequent warm read must
    // recover the original encoded pixel and profile contract.
    load(controller, second, "recovered-cache");
    recovery_passed = recovery_passed && settle(controller) && digest(sink.edited) == second_hash;
  }
  QVariantMap receipt{{"samples", samples}, {"width", expected_size.width()},
      {"height", expected_size.height()}, {"heldAdmissionMs", held_admission_ms},
      {"eventTurnsWhileHeld", event_turns}, {"queuedWhileHeld", queued},
      {"noEarlyPublication", no_early_publication}, {"newestOnly", newest_only},
      {"asyncPassed", async_passed}, {"pixelsPassed", true},
      {"recoveryExercised", args.size() == 1 && !baseline}, {"recoveryPassed", recovery_passed}};
  const auto bytes = QJsonDocument::fromVariant(receipt).toJson();
  if (args.size() == 3) {
    QFile output(args[2]);
    if (!output.open(QIODevice::WriteOnly | QIODevice::NewOnly) || output.write(bytes) != bytes.size()) return 2;
  } else std::cout << bytes.toStdString();
  return baseline || (async_passed && recovery_passed) ? 0 : 1;
}
