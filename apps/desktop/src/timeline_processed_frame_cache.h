#pragma once

#include <QHash>
#include <QImage>
#include <QMutex>
#include <QMutexLocker>
#include <QString>

#include <cstddef>
#include <cstdint>
#include <list>

namespace photo_editor::desktop {

// Stores completed Program preview frames for exact decoded timestamps. The
// caller owns the key's correctness; this cache only provides bounded LRU
// storage and never retains decoder-backed QVideoFrame surfaces.
class TimelineProcessedFrameCache final {
 public:
  struct Statistics final {
    std::size_t entries = 0U;
    std::size_t resident_bytes = 0U;
    std::uint64_t hits = 0U;
    std::uint64_t misses = 0U;
    std::uint64_t evictions = 0U;
  };

  explicit TimelineProcessedFrameCache(
      std::size_t byte_budget = 128U * 1024U * 1024U,
      std::size_t entry_limit = 240U)
      : byte_budget_(byte_budget), entry_limit_(entry_limit) {}

  [[nodiscard]] QImage find(const QString& key) {
    if (key.isEmpty()) {
      return {};
    }
    const QMutexLocker lock(&mutex_);
    const auto found = by_key_.find(key);
    if (found == by_key_.end()) {
      ++misses_;
      return {};
    }
    ++hits_;
    entries_.splice(entries_.begin(), entries_, found.value());
    return found.value()->image;
  }

  [[nodiscard]] bool insert(const QString& key, const QImage& image) {
    if (key.isEmpty() || image.isNull()) {
      return false;
    }
    const auto image_bytes = image.sizeInBytes();
    if (image_bytes <= 0) {
      return false;
    }
    const auto byte_cost = static_cast<std::size_t>(image_bytes);
    if (byte_cost > byte_budget_ || entry_limit_ == 0U) {
      return false;
    }

    const QMutexLocker lock(&mutex_);
    const auto existing = by_key_.find(key);
    if (existing != by_key_.end()) {
      resident_bytes_ -= existing.value()->byte_cost;
      entries_.erase(existing.value());
      by_key_.erase(existing);
    }
    entries_.push_front(Entry{key, image, byte_cost});
    by_key_.insert(key, entries_.begin());
    resident_bytes_ += byte_cost;
    while (resident_bytes_ > byte_budget_ || entries_.size() > entry_limit_) {
      const auto last = std::prev(entries_.end());
      resident_bytes_ -= last->byte_cost;
      by_key_.remove(last->key);
      entries_.erase(last);
      ++evictions_;
    }
    return true;
  }

  [[nodiscard]] Statistics statistics() const {
    const QMutexLocker lock(&mutex_);
    return Statistics{
        .entries = entries_.size(),
        .resident_bytes = resident_bytes_,
        .hits = hits_,
        .misses = misses_,
        .evictions = evictions_,
    };
  }

 private:
  struct Entry final {
    QString key;
    QImage image;
    std::size_t byte_cost = 0U;
  };

  const std::size_t byte_budget_;
  const std::size_t entry_limit_;
  mutable QMutex mutex_;
  std::list<Entry> entries_;
  QHash<QString, std::list<Entry>::iterator> by_key_;
  std::size_t resident_bytes_ = 0U;
  std::uint64_t hits_ = 0U;
  std::uint64_t misses_ = 0U;
  std::uint64_t evictions_ = 0U;
};

}  // namespace photo_editor::desktop
