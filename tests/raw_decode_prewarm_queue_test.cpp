#include "raw_decode_prewarm_queue.h"
#include "source_image_decode_cache.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QSemaphore>
#include <QTemporaryDir>
#include <QtConcurrent>
#include <iostream>
#include <stdexcept>

using namespace photo_editor::desktop;
using photo_editor::pipeline::CancellationToken;

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  const auto args = app.arguments();
  if (args.size() != 1 && args.size() != 3) return 2;
  QTemporaryDir runtime;
  if (!runtime.isValid()) return 2;
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", runtime.path().toUtf8());
  qputenv("PHOTO_EDITOR_CACHE_DIR", runtime.filePath("cache").toUtf8());
  QThreadPool pool;
  pool.setMaxThreadCount(1);
  bool passed = true;
  QVariantMap receipt;
  {
    RawDecodePrewarmQueue queue(&pool);
    QSemaphore entered, release;
    std::atomic_bool first_canceled = false;
    std::atomic_int last_ran = -1;
    queue.submit("first", [&](CancellationToken token) {
      entered.release(); release.acquire(); first_canceled = token.cancelled(); return false;
    });
    entered.acquire();
    for (int index = 0; index < 100; ++index) {
      queue.submit(QString::number(index), [&, index](CancellationToken token) {
        if (token.cancelled()) return false;
        last_ran = index;
        if (args.size() == 3)
          return prewarm_source_image_decode_pyramid(args[1], EditRecipeState{}, 1600, 768, token);
        return true;
      });
    }
    // A duplicate latest request must preserve the existing callback.
    queue.submit("99", [&](CancellationToken) { last_ran = -2; return false; });
    const auto held = queue.snapshot();
    passed = passed && held.pending == 1 && held.started == 1 && held.replaced == 99;
    release.release(); pool.waitForDone();
    const auto final = queue.snapshot();
    passed = passed && first_canceled && last_ran == 99 && final.started == 2 &&
        final.finished == 2 && final.failed == 0 && final.pending == 0 && !final.scheduled;
    if (args.size() == 3) passed = passed &&
        source_image_decode_cache_contains(args[1], EditRecipeState{}, 1600) &&
        source_image_decode_cache_contains(args[1], EditRecipeState{}, 768);
    receipt.insert("burst", QVariantMap{{"requestsAfterActive", 100}, {"pendingWhileHeld", held.pending},
        {"replaced", QVariant::fromValue(final.replaced)}, {"started", QVariant::fromValue(final.started)},
        {"firstCanceled", first_canceled.load()}, {"lastRan", last_ran.load()},
        {"realRawPyramidExercised", args.size() == 3}, {"failed", QVariant::fromValue(final.failed)}});
  }
  {
    QSemaphore entered, release;
    auto held = QtConcurrent::run(&pool, [&] { entered.release(); release.acquire(); });
    entered.acquire();
    RawDecodePrewarmQueue queue(&pool);
    std::atomic_int ran = 0;
    for (int index = 0; index < 100; ++index)
      queue.submit(QString::number(index), [&](CancellationToken) { ++ran; return true; });
    queue.cancel(); // Selecting JPEG or an already resident RAW cancels speculation.
    passed = passed && queue.snapshot().pending == 0;
    release.release(); held.waitForFinished(); pool.waitForDone();
    passed = passed && ran == 0 && !queue.snapshot().scheduled;
    receipt.insert("cancelBeforeDispatch", ran == 0);
  }
  {
    RawDecodePrewarmQueue queue(&pool);
    QSemaphore entered, release;
    std::atomic_int last = 0;
    queue.submit("A", [&](CancellationToken token) { entered.release(); release.acquire(); return !token.cancelled(); });
    entered.acquire();
    queue.submit("B", [&](CancellationToken) { last = 1; return true; });
    queue.submit("A", [&](CancellationToken) { last = 2; return true; });
    release.release(); pool.waitForDone();
    passed = passed && last == 2 && queue.snapshot().started == 2;
    receipt.insert("returnToCanceledKey", last == 2);
    queue.submit("throws", [](CancellationToken) -> bool { throw std::runtime_error("decoder failed"); });
    pool.waitForDone();
    queue.submit("after-error", [&](CancellationToken) { last = 3; return true; });
    pool.waitForDone();
    passed = passed && last == 3 && queue.snapshot().failed == 1;
    receipt.insert("exceptionRecovery", last == 3 && queue.snapshot().failed == 1);
  }
  {
    auto queue = std::make_unique<RawDecodePrewarmQueue>(&pool);
    QSemaphore entered, release;
    std::atomic_bool canceled = false;
    queue->submit("held", [&](CancellationToken token) {
      entered.release(); release.acquire(); canceled = token.cancelled(); return false;
    });
    entered.acquire();
    // Destruction must return before the held task can finish; a destructor
    // join would deadlock this regression (bounded by the CTest timeout).
    queue.reset();
    release.release(); pool.waitForDone();
    passed = passed && canceled;
    receipt.insert("activeDestructionCanceled", canceled.load());
  }
  {
    QSemaphore entered, release;
    auto held = QtConcurrent::run(&pool, [&] { entered.release(); release.acquire(); });
    entered.acquire();
    auto queue = std::make_unique<RawDecodePrewarmQueue>(&pool);
    auto payload = std::make_shared<int>(1);
    std::weak_ptr<int> weak = payload;
    std::atomic_bool ran = false;
    queue->submit("queued", [payload, &ran](CancellationToken) { ran = true; return *payload == 1; });
    payload.reset(); queue.reset();
    const bool released_payload = weak.expired();
    release.release(); held.waitForFinished(); pool.waitForDone();
    passed = passed && released_payload && !ran;
    receipt.insert("queuedDestructionReleasedPayload", released_payload && !ran);
  }
  receipt.insert("passed", passed);
  const auto bytes = QJsonDocument::fromVariant(receipt).toJson();
  if (args.size() == 3) {
    QFile file(args[2]);
    if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly) || file.write(bytes) != bytes.size()) return 2;
  } else std::cout << bytes.toStdString();
  return passed ? 0 : 1;
}
