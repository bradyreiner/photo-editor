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
  static int rendered_hits(const EditorPreviewController& c) { return c.rendered_preview_cache_hit_count_; }
  static int rendered_fast_schedules(const EditorPreviewController& c) {
    return c.rendered_preview_cache_fast_schedule_count_;
  }
  static bool full_running(const EditorPreviewController& c) { return c.full_render_watcher_.isRunning(); }
  static bool interactive_running(const EditorPreviewController& c) {
    return c.interactive_render_watcher_.isRunning();
  }
  static void retry(EditorPreviewController& c) { c.renderPreview(true, QStringLiteral("preview_retry")); }
  static void clear_rendered_cache() { EditorPreviewController::clearRenderedPreviewImageCacheForTesting(); }
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
bool wait_for_render_worker(EditorPreviewController& c, int timeout_ms) {
  QElapsedTimer timer;
  timer.start();
  do {
    QCoreApplication::processEvents();
    if (EditorPreviewControllerTestAccess::full_running(c) ||
        EditorPreviewControllerTestAccess::interactive_running(c)) return true;
    QThread::msleep(1);
  } while (timer.elapsed() < timeout_ms);
  return false;
}
void load(EditorPreviewController& c, const QString& path, const QString& id) {
  c.loadAsset(id, path, {}, {}, {}, {}, {});
}
}

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  const auto fail = [](const char* checkpoint) {
    std::cerr << "[warm-asset] failed checkpoint: " << checkpoint << '\n';
    return 1;
  };
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
  // With an image provider attached, settled full previews use the bounded
  // same-session rendered-image cache rather than an encoded disk preview.
  // Read once after seeding so later samples compare against the exact cached
  // pixels and profile, not a separate cold render.
  load(controller, second, "second-seed");
  if (!settle(controller)) return fail("second-seed settle");
  load(controller, second, "second-warm");
  if (!settle(controller)) return fail("second-warm settle");
  if (sink.edited.isNull()) return fail("second-warm published null image");
  const auto second_hash = digest(sink.edited);
  const auto second_space = sink.edited.colorSpace();
  load(controller, source, "first-seed");
  if (!settle(controller)) return fail("first-seed settle");
  load(controller, source, "first-warm");
  if (!settle(controller)) return fail("first-warm settle");
  if (sink.edited.isNull()) return fail("first-warm published null image");
  const auto expected_hash = digest(sink.edited);
  const auto expected_space = sink.edited.colorSpace();
  const auto expected_size = sink.edited.size();
  QVariantList samples;
  for (int index = 0; index < 12; ++index) {
    sink.publications.clear();
    const auto hits_before = EditorPreviewControllerTestAccess::rendered_hits(controller);
    const auto schedules_before =
        EditorPreviewControllerTestAccess::rendered_fast_schedules(controller);
    QElapsedTimer timer;
    timer.start();
    load(controller, source, QStringLiteral("warm-%1").arg(index));
    const auto admission_ms = double(timer.nsecsElapsed()) / 1e6;
    if (!settle(controller)) return fail("warm-hit settle");
    const auto ready_ms = double(timer.nsecsElapsed()) / 1e6;
    const auto actual_hash = digest(sink.edited);
    const auto hits_after = EditorPreviewControllerTestAccess::rendered_hits(controller);
    const auto schedules_after =
        EditorPreviewControllerTestAccess::rendered_fast_schedules(controller);
    const bool hash_matches = actual_hash == expected_hash;
    const bool space_matches = sink.edited.colorSpace() == expected_space;
    const bool hit_matches = hits_after > hits_before && schedules_after == schedules_before + 1;
    if (!hash_matches || !space_matches || !hit_matches) {
      return fail("warm-hit pixels/profile/rendered-cache contract");
    }
    samples.push_back(QVariantMap{{"admissionMs", admission_ms}, {"readyMs", ready_ms},
        {"rgbaSha256", QString::fromLatin1(actual_hash)}, {"cacheHit", true}});
  }
  // Block the worker queue, not the GUI. The first selection must not decode or
  // publish inline, and a replacement selection must supersede its queued work.
  auto* pool = QThreadPool::globalInstance();
  pool->waitForDone();
  const auto threads = pool->maxThreadCount();
  pool->setMaxThreadCount(1);
  EditorPreviewControllerTestAccess::clear_rendered_cache();
  QSemaphore entered, release;
  auto held = QtConcurrent::run([&] { entered.release(); release.acquire(); });
  entered.acquire();
  sink.publications.clear();
  sink.edited = {};
  const auto hits_before = EditorPreviewControllerTestAccess::rendered_hits(controller);
  QElapsedTimer admission;
  admission.start();
  load(controller, source, "held-first");
  const auto held_admission_ms = double(admission.nsecsElapsed()) / 1e6;
  const bool first_worker_queued = wait_for_render_worker(controller, 2000);
  const bool queued = first_worker_queued &&
      EditorPreviewControllerTestAccess::rendered_hits(controller) == hits_before && sink.edited.isNull();
  load(controller, second, "held-second");
  int event_turns = 0;
  QElapsedTimer event_timer;
  event_timer.start();
  while (event_timer.elapsed() < 350) { QCoreApplication::processEvents(); ++event_turns; QThread::msleep(1); }
  const bool no_early_publication = sink.publications.isEmpty();
  release.release();
  held.waitForFinished();
  pool->setMaxThreadCount(threads);
  if (!settle(controller)) return fail("held-selection settle");
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
    if (!settle(controller)) return fail("replace-before-delivery settle");
    for (const auto& published : sink.publications)
      recovery_passed = recovery_passed && digest(published) == second_hash;
    recovery_passed = recovery_passed && !sink.publications.isEmpty();

    // Retry must use the same asynchronous path. Clear the rendered-image
    // entry only after admission, forcing the queued worker to render and
    // repopulate it rather than relying on a disk cache disabled by the sink.
    pool->waitForDone();
    pool->setMaxThreadCount(1);
    QSemaphore retry_entered, release_retry;
    auto held_retry = QtConcurrent::run([&] { retry_entered.release(); release_retry.acquire(); });
    retry_entered.acquire();
    sink.publications.clear();
    const auto retry_hits = EditorPreviewControllerTestAccess::rendered_hits(controller);
    EditorPreviewControllerTestAccess::retry(controller);
    const bool retry_worker_queued = wait_for_render_worker(controller, 2000);
    const bool retry_queued = retry_worker_queued &&
        sink.publications.isEmpty() &&
        EditorPreviewControllerTestAccess::rendered_hits(controller) == retry_hits;
    EditorPreviewControllerTestAccess::clear_rendered_cache();
    release_retry.release();
    held_retry.waitForFinished();
    pool->setMaxThreadCount(threads);
    const bool retry_settled = settle(controller);
    recovery_passed = recovery_passed && retry_queued && retry_settled &&
        !sink.edited.isNull() && digest(sink.edited) == second_hash &&
        sink.edited.colorSpace() == second_space &&
        EditorPreviewControllerTestAccess::rendered_hits(controller) == retry_hits;
    // A subsequent selection must hit the cache repopulated by the retry and
    // preserve the original cached pixels and profile.
    const auto recovered_schedules_before =
        EditorPreviewControllerTestAccess::rendered_fast_schedules(controller);
    load(controller, second, "recovered-cache");
    const bool recovered_settled = settle(controller);
    recovery_passed = recovery_passed && recovered_settled &&
        digest(sink.edited) == second_hash &&
        sink.edited.colorSpace() == second_space &&
        EditorPreviewControllerTestAccess::rendered_hits(controller) > retry_hits &&
        EditorPreviewControllerTestAccess::rendered_fast_schedules(controller) ==
            recovered_schedules_before + 1;
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
  if (baseline) return 0;
  if (!async_passed) return fail("async latest-selection contract");
  if (!recovery_passed) return fail("retry cache recovery contract");
  return 0;
}
