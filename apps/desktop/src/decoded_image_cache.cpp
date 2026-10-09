#include "decoded_image_cache.h"
#include "linked_image_source.h"
#include "photo_cache_budget.h"

#include <QHash>
#include <QList>
#include <QMutex>
#include <QMutexLocker>
#include <QTimeZone>

#include <optional>
#include <array>
#include <algorithm>
#include <limits>
#if defined(Q_OS_MACOS)
#include <sys/sysctl.h>
#elif defined(Q_OS_WIN)
#include <qt_windows.h>
#elif defined(Q_OS_UNIX)
#include <unistd.h>
#endif

namespace photo_editor::desktop {

namespace {

constexpr qint64 kDefaultDecodedCacheMemoryBytes = 512 * 1024 * 1024;
constexpr qint64 kMinimumDecodedCacheMemoryBytes = 32 * 1024 * 1024;
constexpr qint64 kMaximumDecodedCacheMemoryBytes = 8LL * 1024 * 1024 * 1024;

quint64 installed_photo_memory_bytes() {
#if defined(Q_OS_MACOS)
  uint64_t bytes = 0;
  size_t size = sizeof(bytes);
  if (sysctlbyname("hw.memsize", &bytes, &size, nullptr, 0) == 0 && bytes > 0) return bytes;
#elif defined(Q_OS_WIN)
  MEMORYSTATUSEX status{};
  status.dwLength = sizeof(status);
  if (GlobalMemoryStatusEx(&status)) return status.ullTotalPhys;
#elif defined(Q_OS_UNIX)
  const auto pages = sysconf(_SC_PHYS_PAGES);
  const auto page_size = sysconf(_SC_PAGESIZE);
  if (pages > 0 && page_size > 0 && static_cast<quint64>(pages) <=
      std::numeric_limits<quint64>::max() / static_cast<quint64>(page_size))
    return static_cast<quint64>(pages) * static_cast<quint64>(page_size);
#endif
  return 8ULL * 1024 * 1024 * 1024;
}

struct PhotoCacheBudget final {
  struct Record { quint64 token; PhotoCacheOwner owner; qint64 bytes; std::function<void()> evict; };
  struct OwnerStats {
    qint64 bytes = 0;
    qint64 entries = 0;
    quint64 hits = 0;
    quint64 misses = 0;
    quint64 rejected = 0;
    std::array<quint64, 5> releases{};
  };
  QMutex mutex;
  std::list<Record> lru;
  QHash<quint64, std::list<Record>::iterator> entries;
  std::array<OwnerStats, static_cast<std::size_t>(PhotoCacheOwner::Count)> owners{};
  quint64 next_token = 1;
  qint64 bytes = 0;
  qint64 configured_limit = automatic_photo_cache_budget_bytes(installed_photo_memory_bytes());
  qint64 limit = configured_limit;
  QString pressure = QStringLiteral("normal");

  OwnerStats& stats(PhotoCacheOwner owner) { return owners[static_cast<std::size_t>(owner)]; }
  void release(quint64 token, PhotoCacheEviction reason, bool evict) {
    const auto found = entries.find(token);
    if (found == entries.end()) return;
    const auto record = found.value();
    auto callback = evict ? std::move(record->evict) : std::function<void()>{};
    auto& owner = stats(record->owner);
    bytes -= record->bytes;
    owner.bytes -= record->bytes;
    --owner.entries;
    ++owner.releases[static_cast<std::size_t>(reason)];
    lru.erase(record);
    entries.erase(found);
    if (callback) callback();
  }
  void trim(PhotoCacheEviction reason) {
    while (bytes > limit && !lru.empty()) release(lru.back().token, reason, true);
  }
  void updateLimit() {
    const auto percent = pressure == QStringLiteral("critical") ? 35 :
        pressure == QStringLiteral("warning") ? 65 : 100;
    limit = configured_limit / 100 * percent + (configured_limit % 100) * percent / 100;
  }
};

PhotoCacheBudget& shared_photo_cache_budget() {
  // Constructed by each cache's mutex reference before the cache itself, so
  // local/static caches release their registrations before this is destroyed.
  static PhotoCacheBudget budget;
  return budget;
}

struct DecodedCacheEntry final {
  QImage image;
  std::optional<photo_editor::pipeline::DecodedImageMetadata> metadata;
};

struct DecodedImageCache final : BudgetedPhotoCache<DecodedCacheEntry> {
  DecodedImageCache() : BudgetedPhotoCache(PhotoCacheOwner::Decoded, kDefaultDecodedCacheMemoryBytes) {}
  void insert(const QString& key, const QImage& image,
              std::optional<photo_editor::pipeline::DecodedImageMetadata> metadata) {
    if (image.isNull()) return;
    insertValue(key, DecodedCacheEntry{image, std::move(metadata)}, static_cast<qint64>(image.sizeInBytes()));
  }
};

DecodedImageCache& decoded_image_cache() {
  static DecodedImageCache cache;
  return cache;
}

DecodedImageCacheSnapshot snapshot_locked(const DecodedImageCache& cache) {
  return DecodedImageCacheSnapshot{
      .entries = static_cast<int>(cache.entries.size()),
      .bytes = cache.total_bytes,
  };
}

}  // namespace

qint64 automatic_photo_cache_budget_bytes(quint64 installed_bytes) {
  constexpr quint64 mib = 1024 * 1024;
  return static_cast<qint64>(std::clamp(installed_bytes / 8, 256 * mib, 2048 * mib));
}

QMutex& photo_cache_mutex() { return shared_photo_cache_budget().mutex; }

quint64 reserve_photo_cache_entry_locked(PhotoCacheOwner owner, qint64 bytes, std::function<void()> evict) {
  auto& budget = shared_photo_cache_budget();
  if (bytes <= 0 || bytes > budget.limit) { ++budget.stats(owner).rejected; return 0; }
  while (budget.bytes > budget.limit - bytes && !budget.lru.empty())
    budget.release(budget.lru.back().token, PhotoCacheEviction::SharedLimit, true);
  const auto token = budget.next_token++;
  budget.lru.push_front({token, owner, bytes, std::move(evict)});
  try {
    budget.entries.insert(token, budget.lru.begin());
  } catch (...) {
    budget.lru.pop_front();
    throw;
  }
  budget.bytes += bytes;
  budget.stats(owner).bytes += bytes;
  ++budget.stats(owner).entries;
  return token;
}

void release_photo_cache_entry_locked(quint64 token, PhotoCacheEviction reason) {
  shared_photo_cache_budget().release(token, reason, false);
}

void touch_photo_cache_entry_locked(quint64 token) {
  auto& budget = shared_photo_cache_budget();
  const auto found = budget.entries.constFind(token);
  if (found == budget.entries.cend()) return;
  ++budget.stats(found.value()->owner).hits;
  budget.lru.splice(budget.lru.begin(), budget.lru, found.value());
}

void record_photo_cache_miss_locked(PhotoCacheOwner owner) { ++shared_photo_cache_budget().stats(owner).misses; }

void set_photo_cache_budget_bytes(qint64 bytes) {
  QMutexLocker locker(&photo_cache_mutex());
  auto& budget = shared_photo_cache_budget();
  budget.configured_limit = std::max<qint64>(0, bytes);
  budget.updateLimit();
  budget.trim(PhotoCacheEviction::SharedLimit);
}

void set_photo_cache_pressure(const QString& level) {
  QMutexLocker locker(&photo_cache_mutex());
  auto& budget = shared_photo_cache_budget();
  budget.pressure = level == QStringLiteral("critical") || level == QStringLiteral("warning")
      ? level : QStringLiteral("normal");
  budget.updateLimit();
  budget.trim(PhotoCacheEviction::Pressure);
  if (budget.pressure == QStringLiteral("critical"))
    while (!budget.lru.empty()) budget.release(budget.lru.back().token, PhotoCacheEviction::Pressure, true);
}

void clear_photo_caches() {
  QMutexLocker locker(&photo_cache_mutex());
  auto& budget = shared_photo_cache_budget();
  while (!budget.lru.empty()) budget.release(budget.lru.back().token, PhotoCacheEviction::Clear, true);
}

QVariantMap photo_cache_budget_snapshot() {
  QMutexLocker locker(&photo_cache_mutex());
  const auto& budget = shared_photo_cache_budget();
  const std::array<const char*, 7> names{"decoded", "characterized", "adjusted", "rendered",
      "renderedTiles", "foregroundMaps", "maskTiles"};
  QVariantMap owners;
  for (std::size_t index = 0; index < names.size(); ++index) {
    const auto& stats = budget.owners[index];
    owners.insert(QString::fromLatin1(names[index]), QVariantMap{
        {QStringLiteral("bytes"), stats.bytes}, {QStringLiteral("entries"), stats.entries},
        {QStringLiteral("hits"), QVariant::fromValue(stats.hits)}, {QStringLiteral("misses"), QVariant::fromValue(stats.misses)},
        {QStringLiteral("rejected"), QVariant::fromValue(stats.rejected)},
        {QStringLiteral("replaced"), QVariant::fromValue(stats.releases[0])},
        {QStringLiteral("localLimitEvictions"), QVariant::fromValue(stats.releases[1])},
        {QStringLiteral("sharedLimitEvictions"), QVariant::fromValue(stats.releases[2])},
        {QStringLiteral("pressureEvictions"), QVariant::fromValue(stats.releases[3])},
        {QStringLiteral("cleared"), QVariant::fromValue(stats.releases[4])}});
  }
  return {{QStringLiteral("schema"), QStringLiteral("shared-photo-cache-budget:v1")},
      {QStringLiteral("accounting"), QStringLiteral("cache-owned-references-not-process-rss")},
      {QStringLiteral("configuredLimitBytes"), budget.configured_limit},
      {QStringLiteral("effectiveLimitBytes"), budget.limit}, {QStringLiteral("bytes"), budget.bytes},
      {QStringLiteral("pressure"), budget.pressure}, {QStringLiteral("owners"), owners}};
}

QString decoded_image_cache_key(const QFileInfo& file_info, int max_edge) {
  return QStringLiteral("%1|%2|%3|%4|%5")
      .arg(file_info.absoluteFilePath())
      .arg(max_edge)
      .arg(file_info.size())
      .arg(file_info.lastModified(QTimeZone::UTC).toMSecsSinceEpoch())
      .arg(linked_image_file_revision(file_info.absoluteFilePath()));
}

DecodedImageCacheSnapshot decoded_image_cache_snapshot() {
  auto& cache = decoded_image_cache();
  QMutexLocker locker(&cache.mutex);
  return snapshot_locked(cache);
}

qint64 decoded_image_cache_budget_bytes() {
  auto& cache = decoded_image_cache();
  QMutexLocker locker(&cache.mutex);
  return cache.budget_bytes;
}

void set_decoded_image_cache_budget_bytes(qint64 bytes) {
  auto& cache = decoded_image_cache();
  QMutexLocker locker(&cache.mutex);
  cache.budget_bytes = qBound(
      kMinimumDecodedCacheMemoryBytes, bytes, kMaximumDecodedCacheMemoryBytes);
  cache.trim();
}

void clear_decoded_image_cache() {
  auto& cache = decoded_image_cache();
  QMutexLocker locker(&cache.mutex);
  cache.clear();
}

bool find_decoded_image_cache_entry(
    const QString& key,
    QImage* image,
    DecodedImageCacheSnapshot* snapshot) {
  if (key.isEmpty() || image == nullptr) {
    return false;
  }

  auto& cache = decoded_image_cache();
  QMutexLocker locker(&cache.mutex);
  const auto cached = cache.entries.constFind(key);
  if (cached == cache.entries.cend()) {
    cache.noteMiss();
    if (snapshot != nullptr) {
      *snapshot = snapshot_locked(cache);
    }
    return false;
  }

  *image = cached.value().image;
  cache.touch(key);
  if (snapshot != nullptr) {
    *snapshot = snapshot_locked(cache);
  }
  return !image->isNull();
}

void insert_decoded_image_cache_entry(
    const QString& key,
    const QImage& image,
    DecodedImageCacheSnapshot* snapshot) {
  auto& cache = decoded_image_cache();
  QMutexLocker locker(&cache.mutex);
  cache.insert(key, image, std::nullopt);
  if (snapshot != nullptr) {
    *snapshot = snapshot_locked(cache);
  }
}

bool find_decoded_image_cache_entry_with_metadata(
    const QString& key,
    QImage* image,
    photo_editor::pipeline::DecodedImageMetadata* metadata,
    DecodedImageCacheSnapshot* snapshot) {
  if (key.isEmpty() || image == nullptr || metadata == nullptr) {
    return false;
  }

  auto& cache = decoded_image_cache();
  QMutexLocker locker(&cache.mutex);
  const auto cached = cache.entries.constFind(key);
  if (cached == cache.entries.cend() || !cached.value().metadata.has_value()) {
    cache.noteMiss();
    if (snapshot != nullptr) {
      *snapshot = snapshot_locked(cache);
    }
    return false;
  }

  *image = cached.value().image;
  *metadata = *cached.value().metadata;
  cache.touch(key);
  if (snapshot != nullptr) {
    *snapshot = snapshot_locked(cache);
  }
  return !image->isNull();
}

void insert_decoded_image_cache_entry_with_metadata(
    const QString& key,
    const QImage& image,
    const photo_editor::pipeline::DecodedImageMetadata& metadata,
    DecodedImageCacheSnapshot* snapshot) {
  auto& cache = decoded_image_cache();
  QMutexLocker locker(&cache.mutex);
  cache.insert(key, image, metadata);
  if (snapshot != nullptr) {
    *snapshot = snapshot_locked(cache);
  }
}

}  // namespace photo_editor::desktop
