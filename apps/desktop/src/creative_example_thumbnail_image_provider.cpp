#include "creative_example_thumbnail_image_provider.h"

#include "creative_example_thumbnail_service.h"

#include <QCoreApplication>
#include <QFutureWatcher>
#include <QMutex>
#include <QMutexLocker>
#include <QMetaObject>
#include <QPointer>
#include <QQmlEngine>
#include <QQuickTextureFactory>
#include <QTimer>
#include <QThread>

#include <atomic>
#include <functional>
#include <memory>
#include <utility>

namespace photo_editor::desktop {
namespace {

class CreativeExampleThumbnailResponse final : public QQuickImageResponse {
 public:
  CreativeExampleThumbnailResponse()
      : watcher_(new QFutureWatcher<CreativeExampleThumbnailResult>(this)),
        cancelled_(std::make_shared<std::atomic_bool>(false)) {
    QObject::connect(
        watcher_,
        &QFutureWatcher<CreativeExampleThumbnailResult>::finished,
        this,
        [this]() {
          const auto future = watcher_->future();
          if (future.resultCount() > 0) {
            const auto result = future.result();
            if (result.cancelled) {
              setError(QStringLiteral("The creative example thumbnail request was cancelled."));
            } else {
              QMutexLocker lock(&image_mutex_);
              image_ = result.image;
              error_ = result.error;
            }
          } else {
            setError(QStringLiteral("The creative example thumbnail request did not complete."));
          }
          if (cancelled_->load(std::memory_order_relaxed)) {
            QMutexLocker lock(&image_mutex_);
            image_ = {};
            if (error_.isEmpty())
              error_ = QStringLiteral("The creative example thumbnail request was cancelled.");
          }
          release(false);
          emit finished();
        });
  }

  ~CreativeExampleThumbnailResponse() override {
    release(true);
  }

  void start(const QString& id) {
    auto ticket = CreativeExampleThumbnailService::shared().request(id);
    if (!ticket.error.isEmpty()) {
      setError(ticket.error);
      QTimer::singleShot(0, this, [this]() { emit finished(); });
      return;
    }
    if (!ticket.cached_image.isNull()) {
      {
        QMutexLocker lock(&image_mutex_);
        image_ = ticket.cached_image;
      }
      QTimer::singleShot(0, this, [this]() {
        if (cancelled_->load(std::memory_order_relaxed)) {
          QMutexLocker lock(&image_mutex_);
          image_ = {};
          error_ = QStringLiteral("The creative example thumbnail request was cancelled.");
        }
        emit finished();
      });
      return;
    }
    release_ = std::move(ticket.release);
    watcher_->setFuture(ticket.future);
  }

  QQuickTextureFactory* textureFactory() const override {
    if (cancelled_->load(std::memory_order_relaxed)) return nullptr;
    QImage image;
    {
      QMutexLocker lock(&image_mutex_);
      image = image_;
    }
    if (image.isNull()) return nullptr;
    return QQuickTextureFactory::textureFactoryForImage(image);
  }

  QString errorString() const override {
    QMutexLocker lock(&image_mutex_);
    return error_;
  }

  void cancel() override {
    cancelled_->store(true, std::memory_order_relaxed);
    if (QThread::currentThread() == thread()) {
      cancelOnOwnerThread();
      return;
    }
    QMetaObject::invokeMethod(
        this,
        [this]() { cancelOnOwnerThread(); },
        Qt::QueuedConnection);
  }

 private:
  void cancelOnOwnerThread() {
    release(true);
    QMutexLocker lock(&image_mutex_);
    image_ = {};
    if (error_.isEmpty())
      error_ = QStringLiteral("The creative example thumbnail request was cancelled.");
  }

  void setError(const QString& error) {
    QMutexLocker lock(&image_mutex_);
    error_ = error;
  }

  void release(bool cancel) {
    if (!release_) return;
    auto release_job = std::move(release_);
    release_ = {};
    release_job(cancel);
  }

  QFutureWatcher<CreativeExampleThumbnailResult>* watcher_ = nullptr;
  std::shared_ptr<std::atomic_bool> cancelled_;
  std::function<void(bool)> release_;
  mutable QMutex image_mutex_;
  QImage image_;
  QString error_;
};

}  // namespace

QQuickImageResponse* CreativeExampleThumbnailImageProvider::requestImageResponse(
    const QString& id,
    const QSize& requested_size) {
  Q_UNUSED(requested_size)
  auto* response = new CreativeExampleThumbnailResponse;
  auto* application = QCoreApplication::instance();
  if (application != nullptr && response->thread() != application->thread()) {
    response->moveToThread(application->thread());
    QMetaObject::invokeMethod(
        response,
        [response, id]() { response->start(id); },
        Qt::QueuedConnection);
  } else {
    response->start(id);
  }
  return response;
}

void register_creative_example_thumbnail_provider(QQmlEngine* engine) {
  if (engine == nullptr) return;
  engine->addImageProvider(
      QStringLiteral("creative-example"),
      new CreativeExampleThumbnailImageProvider);
}

}  // namespace photo_editor::desktop
