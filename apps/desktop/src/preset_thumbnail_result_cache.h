#pragma once

#include "edit_recipe_state.h"

#include <QColorSpace>
#include <QHash>
#include <QImage>
#include <QList>
#include <QMutex>
#include <QMutexLocker>
#include <QString>
#include <QtGlobal>

namespace photo_editor::desktop {

struct PresetThumbnailResultCacheSnapshot final {
  int entries = 0;
  qsizetype resident_bytes = 0;
  quint64 hits = 0;
  quint64 misses = 0;
};

class PresetThumbnailResultCache final {
 public:
  static constexpr int kMaximumEntries = 64;
  static constexpr qsizetype kMaximumBytes = 8 * 1024 * 1024;

  [[nodiscard]] bool lookup(const QString& key, QString* result) {
    if (key.isEmpty() || result == nullptr) return false;
    QMutexLocker lock(&mutex_);
    const auto found = entries_.constFind(key);
    if (found == entries_.cend()) {
      ++misses_;
      return false;
    }
    lru_.removeAll(key);
    lru_.append(key);
    *result = found->result;
    ++hits_;
    return true;
  }

  void insert(const QString& key, const QString& result) {
    if (key.isEmpty() || result.isEmpty()) return;
    const auto bytes = static_cast<qsizetype>(
        (key.size() + result.size()) * static_cast<qsizetype>(sizeof(QChar)));
    if (bytes <= 0 || bytes > kMaximumBytes) return;

    QMutexLocker lock(&mutex_);
    const auto existing = entries_.find(key);
    if (existing != entries_.end()) {
      resident_bytes_ -= existing->bytes;
      lru_.removeAll(key);
      entries_.erase(existing);
    }
    while (!lru_.isEmpty() &&
           (entries_.size() >= kMaximumEntries ||
            resident_bytes_ + bytes > kMaximumBytes)) {
      const auto oldest = lru_.takeFirst();
      const auto victim = entries_.find(oldest);
      if (victim == entries_.end()) continue;
      resident_bytes_ -= victim->bytes;
      entries_.erase(victim);
    }
    entries_.insert(key, Entry{result, bytes});
    lru_.append(key);
    resident_bytes_ += bytes;
  }

  [[nodiscard]] PresetThumbnailResultCacheSnapshot snapshot() const {
    QMutexLocker lock(&mutex_);
    return {
        .entries = static_cast<int>(entries_.size()),
        .resident_bytes = resident_bytes_,
        .hits = hits_,
        .misses = misses_,
    };
  }

  void clear() {
    QMutexLocker lock(&mutex_);
    entries_.clear();
    lru_.clear();
    resident_bytes_ = 0;
    hits_ = 0;
    misses_ = 0;
  }

 private:
  struct Entry final {
    QString result;
    qsizetype bytes = 0;
  };

  mutable QMutex mutex_;
  QHash<QString, Entry> entries_;
  QList<QString> lru_;
  qsizetype resident_bytes_ = 0;
  quint64 hits_ = 0;
  quint64 misses_ = 0;
};

[[nodiscard]] QString preset_thumbnail_result_cache_key(
    const EditRecipeState& state,
    const QImage& fixture,
    const QColorSpace& target_color_space,
    const QString& normalized_recipe_identity = {});

[[nodiscard]] PresetThumbnailResultCache& preset_thumbnail_result_cache();

}  // namespace photo_editor::desktop
