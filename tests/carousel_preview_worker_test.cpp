#include "carousel_document_controller.h"
#include "edit_adjustment_patch.h"
#include <QApplication>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QSemaphore>
#include <QTemporaryDir>
#include <QThread>
#include <QThreadPool>
#include <QTimer>
#include <QUrl>
#include <QtConcurrent>
#include <functional>
#include <iostream>

namespace photo_editor::desktop {
struct CarouselPreviewTestAccess {
  static QThreadPool& pool() { return CarouselDocumentController::previewThreadPool(); }
  static bool idle(const CarouselDocumentController& controller) {
    return !controller.preview_worker_active_ && controller.pending_preview_boards_.isEmpty() &&
        controller.pending_preview_items_.isEmpty() && !controller.preview_debounce_timer_->isActive();
  }
  static bool finished(const CarouselDocumentController& controller) { return controller.findChild<QFutureWatcherBase*>(QStringLiteral("carouselPreviewWorker"))->isFinished(); }
  static bool active(const CarouselDocumentController& controller) { return controller.preview_worker_active_; }
  static QImage reference(const CarouselDocumentController& controller, int board) {
    return controller.renderSlideBaseImage(board, controller.exportPixelSize().scaled(1600, 1600, Qt::KeepAspectRatio), true);
  }
  static QString itemSource(const CarouselDocumentController& controller, const QString& id) {
    return controller.items_.at(controller.indexOfItem(id)).preview_source;
  }
  static QImage itemReference(const CarouselDocumentController& controller, const QString& id) {
    QImage image;
    if (!controller.decodeEditedImage(controller.items_.at(controller.indexOfItem(id)), &image, true, 1600)) return {};
    return image;
  }
  static void refresh(CarouselDocumentController& controller) { controller.refreshPreviews(); }
};
}
using namespace photo_editor::desktop;
namespace {
bool wait_until(const std::function<bool()>& ready) {
  QElapsedTimer timer;
  timer.start();
  while (!ready() && timer.elapsed() < 15000) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    QThread::msleep(1);
  }
  return ready();
}
struct Gate {
  QSemaphore entered, release;
  QFuture<void> task;
  bool opened = false;
  Gate() {
    task = QtConcurrent::run(&CarouselPreviewTestAccess::pool(), [this] { entered.release(); release.acquire(); });
    if (!entered.tryAcquire(1, 5000)) qFatal("Preview worker fixture did not start");
  }
  void open() { if (!opened) { opened = true; release.release(); } }
  ~Gate() { open(); task.waitForFinished(); }
};
int fail(const char* message) { std::cerr << message << '\n'; return 1; }
QString source(const CarouselDocumentController& controller, int board) {
  return controller.slides().at(board).toMap().value("previewSource").toString();
}
bool matches(const CarouselDocumentController& controller, int board) {
  const QImage actual(QUrl(source(controller, board)).toLocalFile());
  const auto expected = CarouselPreviewTestAccess::reference(controller, board);
  return !actual.isNull() && actual.convertToFormat(QImage::Format_RGBA8888) == expected.convertToFormat(QImage::Format_RGBA8888);
}
}
int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QApplication app(argc, argv);
  QTemporaryDir temporary;
  if (!temporary.isValid()) return fail("Cannot create isolated cache");
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", temporary.path().toUtf8());
  qputenv("PHOTO_EDITOR_APP_DATA_ROOT", temporary.path().toUtf8());
  qputenv("PHOTO_EDITOR_CACHE_DIR", temporary.filePath("cache").toUtf8());
  qputenv("PHOTO_EDITOR_DATABASE_URL", "postgresql://127.0.0.1:1/preview_fixture");
  QImage photo(960, 640, QImage::Format_RGB32);
  photo.fill(QColor("#735421"));
  QPainter painter(&photo);
  painter.fillRect(30, 50, 500, 300, QColor("#2277bb"));
  painter.end();
  const auto path = temporary.filePath("photo.png");
  if (!photo.save(path)) return fail("Cannot save synthetic photo");
  CarouselDocumentController controller;
  int ticks = 0;
  QTimer heartbeat;
  heartbeat.setInterval(1);
  QObject::connect(&heartbeat, &QTimer::timeout, [&] { ++ticks; });
  heartbeat.start();
  QString photo_item_id;
  {
    Gate gate;
    QElapsedTimer call;
    call.start();
    controller.createCarouselWithSettings({{"carouselName", "Preview fixture"}, {"slideCount", 3}});
    const auto creation_ms = call.elapsed();
    controller.assignPrimaryImage(path, "Synthetic photo");
    EditRecipeState edit;
    edit.exposure = 0.5;
    controller.addPhotoItem(path, "Edited photo", serialize_adjustment_patch_json(
        edit, all_adjustment_groups(), AdjustmentPatchScope::AssetBound));
    photo_item_id = controller.selectedItemId();
    if (!wait_until([&] { return CarouselPreviewTestAccess::active(controller); })) return fail("No queued preview worker");
    controller.selectSlide(1);
    controller.setCurrentSlideBackgroundColor("#ff0000");
    controller.selectSlide(2);
    controller.setCurrentSlideBackgroundColor("#00ff00");
    controller.selectSlide(1);
    controller.setCurrentSlideBackgroundColor("#0000ff");
    if (!wait_until([&] { return ticks >= 5; })) return fail("Held rendering blocked the UI loop");
    if (creation_ms > 1000 || !source(controller, 0).isEmpty()) return fail("Preview work ran synchronously or published unfinished files");
    gate.open();
    if (!wait_until([&] { return CarouselPreviewTestAccess::idle(controller); })) return fail("Preview worker failed to drain");
    for (int board = 0; board < 3; ++board)
      if (!matches(controller, board)) return fail("Latest coalesced board output differs from synchronous rendering");
    std::cout << "Three-board creation returned in " << creation_ms << " ms while worker was held; heartbeat continued.\n";
  }
  const auto item_source = CarouselPreviewTestAccess::itemSource(controller, photo_item_id);
  const QImage item_image(QUrl(item_source).toLocalFile());
  if (item_image.isNull() || item_image.convertToFormat(QImage::Format_RGBA8888) !=
      CarouselPreviewTestAccess::itemReference(controller, photo_item_id).convertToFormat(QImage::Format_RGBA8888))
    return fail("Edited item preview differs from synchronous decoding");
  CarouselPreviewTestAccess::refresh(controller);
  if (!wait_until([&] { return CarouselPreviewTestAccess::idle(controller); }) ||
      CarouselPreviewTestAccess::itemSource(controller, photo_item_id) != item_source ||
      controller.previewDiagnostics().value("itemsDecoded").toInt() != 0 ||
      !QFileInfo::exists(QUrl(item_source).toLocalFile()))
    return fail("Unchanged edited-item preview was decoded again or lost cache ownership");
  const auto unchanged = source(controller, 0);
  const auto replaced = source(controller, 1);
  controller.selectSlide(1);
  controller.setCurrentSlideBackgroundColor("#123456");
  if (!wait_until([&] { return CarouselPreviewTestAccess::idle(controller); }) || !matches(controller, 1) || source(controller, 0) != unchanged)
    return fail("A one-board edit refreshed unrelated boards or lost the newest content");
  if (source(controller, 1) == replaced || !QFileInfo::exists(QUrl(unchanged).toLocalFile()))
    return fail("Preview ownership removed an unchanged board's cached file");
  QThreadPool::globalInstance()->waitForDone();
  if (QFileInfo::exists(QUrl(replaced).toLocalFile()))
    return fail("Replaced board files remained pinned by unchanged sibling boards");
  {
    Gate gate;
    const auto before = source(controller, 1);
    controller.selectSlide(1);
    controller.setCurrentSlideBackgroundColor("#ff00ff");
    if (!wait_until([&] { return CarouselPreviewTestAccess::active(controller); })) return fail("No late-completion fixture");
    gate.open();
    QElapsedTimer completion;
    completion.start();
    // Let rendering finish while deliberately withholding its UI completion.
    while (!CarouselPreviewTestAccess::finished(controller) && completion.elapsed() < 10000) QThread::msleep(1);
    if (!CarouselPreviewTestAccess::finished(controller) || source(controller, 1) != before)
      return fail("Worker published into the live model before its UI completion");
    controller.setCurrentSlideBackgroundColor("#654321");
    if (!wait_until([&] { return CarouselPreviewTestAccess::idle(controller); }) || !matches(controller, 1))
      return fail("Completed-but-undelivered old result replaced a newer edit");
  }
  {
    Gate gate;
    CarouselPreviewTestAccess::refresh(controller);
    if (!wait_until([&] { return CarouselPreviewTestAccess::active(controller); })) return fail("No old-document preview task");
    controller.closeCarousel();
    controller.createCarouselWithSettings({{"carouselName", "Replacement"}, {"slideCount", 1}});
    controller.setCurrentSlideBackgroundColor("#abcdef");
    gate.open();
    if (!wait_until([&] { return CarouselPreviewTestAccess::idle(controller); }) || controller.slides().size() != 1 || !matches(controller, 0))
      return fail("Old-document completion contaminated the replacement document");
  }
  {
    Gate gate;
    auto disposable = std::make_unique<CarouselDocumentController>();
    disposable->createCarouselWithSettings({{"carouselName", "Disposable"}, {"slideCount", 2}});
    if (!wait_until([&] { return CarouselPreviewTestAccess::active(*disposable); })) return fail("No disposable preview task");
    disposable.reset();
    gate.open();
  }
  controller.closeCarousel();
  CarouselPreviewTestAccess::pool().waitForDone();
  QThreadPool::globalInstance()->waitForDone();
  if (QFileInfo::exists(QUrl(unchanged).toLocalFile())) return fail("Released preview cache was not cleaned up");
  std::cout << "Async/synchronous pixel parity, coalescing, board isolation, document replacement, destruction, and cache ownership passed.\n";
  return 0;
}
