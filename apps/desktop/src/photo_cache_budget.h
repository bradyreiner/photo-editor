#pragma once

#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QString>
#include <QVariantMap>
#include <QtGlobal>
#include <functional>
#include <list>
#include <utility>

namespace photo_editor::desktop {

enum class PhotoCacheOwner { Decoded, Linear, Adjusted, Rendered, RenderedTiles, ForegroundMaps, MaskTiles, Count };
enum class PhotoCacheEviction { Replacement, LocalLimit, SharedLimit, Pressure, Clear };

// The seven participating caches use this one short-lived lock for lookup and
// ownership changes. No cache calls back into another cache or takes a second
// cache lock while evicting. Rendering/decoding happens outside this lock.
QMutex& photo_cache_mutex();
quint64 reserve_photo_cache_entry_locked(PhotoCacheOwner owner, qint64 bytes, std::function<void()> evict);
void release_photo_cache_entry_locked(quint64 token, PhotoCacheEviction reason);
void touch_photo_cache_entry_locked(quint64 token);
void record_photo_cache_miss_locked(PhotoCacheOwner owner);
qint64 automatic_photo_cache_budget_bytes(quint64 installed_bytes);
void set_photo_cache_budget_bytes(qint64 bytes);
void set_photo_cache_pressure(const QString& level);
void clear_photo_caches();
QVariantMap photo_cache_budget_snapshot();

// Copies retained here are reproducible cache ownership. Eviction releases that
// ownership only; QImage/QByteArray copies and shared working images held by a
// renderer or image provider remain valid. Bytes are accounted references, not
// unique physical allocations or a process-RSS measurement.
template <typename Value>
class BudgetedPhotoCache {
 public:
  QMutex& mutex = photo_cache_mutex();
  QHash<QString, Value> entries;
  qint64 total_bytes = 0;
  qint64 budget_bytes;

  BudgetedPhotoCache(PhotoCacheOwner owner, qint64 limit) : budget_bytes(limit), owner_(owner) {}
  BudgetedPhotoCache(const BudgetedPhotoCache&) = delete;
  BudgetedPhotoCache& operator=(const BudgetedPhotoCache&) = delete;
  ~BudgetedPhotoCache() {
    QMutexLocker locker(&mutex);
    clear();
  }

  // Methods below require mutex to be held by the caller.
  void touch(const QString& key) {
    auto found = accounting_.find(key);
    if (found == accounting_.end()) return;
    access_order_.splice(access_order_.begin(), access_order_, found->position);
    touch_photo_cache_entry_locked(found->token);
  }
  void noteMiss() { record_photo_cache_miss_locked(owner_); }
  void clear() {
    while (!access_order_.empty()) erase(access_order_.back(), PhotoCacheEviction::Clear);
  }
  void trim() {
    while (total_bytes > budget_bytes && !access_order_.empty())
      erase(access_order_.back(), PhotoCacheEviction::LocalLimit);
  }
  void insertValue(const QString& key, const Value& value, qint64 bytes) {
    if (key.isEmpty() || bytes <= 0) return;
    const auto retained = value;
    erase(key, PhotoCacheEviction::Replacement);
    if (bytes > budget_bytes) return;
    while (total_bytes > budget_bytes - bytes && !access_order_.empty())
      erase(access_order_.back(), PhotoCacheEviction::LocalLimit);
    const auto token = reserve_photo_cache_entry_locked(owner_, bytes, [this, key] { eraseLocal(key); });
    if (token == 0) return;
    try {
      entries.insert(key, retained);
      access_order_.push_front(key);
      accounting_.insert(key, Accounting{bytes, token, access_order_.begin()});
      total_bytes += bytes;
    } catch (...) {
      entries.remove(key);
      access_order_.remove(key);
      release_photo_cache_entry_locked(token, PhotoCacheEviction::Clear);
      throw;
    }
  }

 private:
  struct Accounting { qint64 bytes; quint64 token; std::list<QString>::iterator position; };
  PhotoCacheOwner owner_;
  std::list<QString> access_order_;
  QHash<QString, Accounting> accounting_;
  void erase(const QString& key, PhotoCacheEviction reason) {
    const auto found = accounting_.constFind(key);
    if (found == accounting_.cend()) return;
    release_photo_cache_entry_locked(found->token, reason);
    eraseLocal(key);
  }
  void eraseLocal(const QString& key) {
    const auto found = accounting_.find(key);
    if (found == accounting_.end()) return;
    // The key can refer to the LRU node being removed; take an owning copy.
    const auto retained_key = key;
    total_bytes -= found->bytes;
    access_order_.erase(found->position);
    accounting_.erase(found);
    entries.remove(retained_key);
  }
};

}  // namespace photo_editor::desktop
