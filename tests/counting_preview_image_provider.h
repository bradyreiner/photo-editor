#pragma once

#include "editor_preview_image_provider.h"

#include <atomic>

// Benchmark-only forwarding sink: the real provider still owns and serves
// pixels. Count QML requests separately from direct correctness/hash reads.
class CountingPreviewImageProvider final : public QQuickImageProvider,
    public photo_editor::desktop::EditorPreviewImageSink {
 public:
  CountingPreviewImageProvider() : QQuickImageProvider(QQuickImageProvider::Image) {}
  QImage requestImage(const QString& id, QSize* size, const QSize& requested_size) override {
    auto result = provider_.requestImage(id, size, requested_size);
    if (id.section(QLatin1Char('?'), 0, 0) == QStringLiteral("edited")) {
      edited_requests_.fetch_add(1, std::memory_order_relaxed);
      edited_bytes_.fetch_add(static_cast<quint64>(result.sizeInBytes()), std::memory_order_relaxed);
    }
    return result;
  }
  void setImage(const QString& key, const QImage& image) override { provider_.setImage(key, image); }
  void clearImage(const QString& key) override { provider_.clearImage(key); }
  QImage image(const QString& key) const override { return provider_.image(key); }
  quint64 editedRequests() const { return edited_requests_.load(std::memory_order_relaxed); }
  quint64 editedBytes() const { return edited_bytes_.load(std::memory_order_relaxed); }

 private:
  photo_editor::desktop::EditorPreviewImageProvider provider_;
  std::atomic<quint64> edited_requests_{0};
  std::atomic<quint64> edited_bytes_{0};
};
