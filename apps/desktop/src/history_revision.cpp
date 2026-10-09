#include "history_revision.h"
#include <QMutex>
#include <QMutexLocker>
#include <atomic>

namespace photo_editor::desktop {
struct HistoryRevisionData {
  QMutex mutex;
  quint64 id = 0;
  std::optional<EditRecipeState> resident;
  std::optional<HistoryStateRecord> record;
  bool pinned = true;
  bool large = false;
  qint64 payload_bytes = 0;
  QString warning;
};
struct HistoryStorageData {
  QMutex mutex;
  QString directory;
  qint64 budget = 0;
  std::shared_ptr<HistoryPayloadStore> store;
  std::atomic<bool> collecting{false};
  std::function<void(const QString&)> warning;
};
namespace {
std::atomic<quint64> next_revision{1};
void payload_sizes(const QVariant& value, QHash<const char*, qint64>& sizes) {
  if (value.metaType().id() == QMetaType::QByteArray) {
    const auto bytes = value.toByteArray();
    if (!bytes.isEmpty()) sizes[bytes.constData()] = std::max(sizes.value(bytes.constData()),
        static_cast<qint64>(std::max(bytes.size(), bytes.capacity())));
  } else if (value.metaType().id() == QMetaType::QVariantList) {
    for (const auto& child : value.toList()) payload_sizes(child, sizes);
  } else if (value.metaType().id() == QMetaType::QVariantMap) {
    const auto map = value.toMap();
    for (const auto& child : map) payload_sizes(child, sizes);
  } else if (value.metaType().id() == QMetaType::QVariantHash) {
    const auto hash = value.toHash();
    for (const auto& child : hash) payload_sizes(child, sizes);
  }
}
qint64 payload_size(const EditRecipeState& state) {
  QHash<const char*, qint64> sizes;
  for (const auto& list : {state.mask_stack, state.layers, state.recipe_snapshots, state.preset_applications,
       state.master_curve_points, state.luma_curve_points, state.red_curve_points, state.green_curve_points, state.blue_curve_points})
    payload_sizes(list, sizes);
  qint64 result = 0;
  for (auto size : sizes) result += size;
  return result;
}
}
HistoryRevision::HistoryRevision() : HistoryRevision(EditRecipeState{}) {}
HistoryRevision::HistoryRevision(const EditRecipeState& state) : data_(std::make_shared<HistoryRevisionData>()) {
  data_->id = next_revision.fetch_add(1);
  data_->resident = state;
  data_->payload_bytes = payload_size(state);
  data_->large = data_->payload_bytes >= 64 * 1024;
}
quint64 HistoryRevision::id() const { return data_->id; }
bool HistoryRevision::hasLargePayload() const { return data_->large; }
qint64 HistoryRevision::payloadBytes() const { return data_->payload_bytes; }
std::optional<EditRecipeState> HistoryRevision::residentState() const {
  QMutexLocker lock(&data_->mutex);
  return data_->resident;
}
std::optional<EditRecipeState> HistoryRevision::restore(QString* error) const {
  if (error) error->clear();
  std::optional<HistoryStateRecord> record;
  {
    QMutexLocker lock(&data_->mutex);
    if (data_->resident) return data_->resident;
    record = data_->record;
  }
  if (record) return record->restore(error);
  if (error) *error = QStringLiteral("History revision is unavailable.");
  return std::nullopt;
}
void HistoryRevision::unpin() const {
  QMutexLocker lock(&data_->mutex);
  data_->pinned = false;
  if (data_->record) data_->resident.reset();
}
HistoryStorage::HistoryStorage(QString directory, qint64 budget, std::function<void(const QString&)> warning)
    : data_(std::make_shared<HistoryStorageData>()) {
  data_->directory = std::move(directory);
  data_->budget = budget;
  data_->warning = std::move(warning);
  pool_.setMaxThreadCount(1);
  pool_.setExpiryTimeout(-1);
}
HistoryStorage::~HistoryStorage() { pool_.clear(); pool_.waitForDone(); }
HistoryRevision HistoryStorage::retain(const EditRecipeState& state) {
  HistoryRevision result(state);
  if (!result.hasLargePayload()) return result;
  const auto weak = std::weak_ptr<HistoryRevisionData>(result.data_);
  const auto storage = data_;
  pool_.start([weak, storage] {
    const auto entry = weak.lock();
    if (!entry) return;
    std::shared_ptr<HistoryPayloadStore> store;
    {
      QMutexLocker lock(&storage->mutex);
      if (!storage->store) storage->store = std::make_shared<HistoryPayloadStore>(storage->directory, storage->budget);
      store = storage->store;
    }
    std::optional<EditRecipeState> state;
    {
      QMutexLocker lock(&entry->mutex);
      state = entry->resident;
    }
    if (!state) return;
    QString warning;
    auto record = HistoryStateRecord::capture(*state, *store, &warning, 1);
    {
      QMutexLocker lock(&entry->mutex);
      entry->warning = warning;
      if (record) {
        entry->record = std::move(record);
        if (!entry->pinned) entry->resident.reset();
      }
    }
    state.reset();
    if (!warning.isEmpty() && storage->warning) storage->warning(warning);
    store->collectUnused();
  });
  return result;
}
void HistoryStorage::pin(const HistoryRevision& revision) {
  {
    QMutexLocker lock(&revision.data_->mutex);
    revision.data_->pinned = true;
    if (revision.data_->resident) return;
  }
  const auto weak = std::weak_ptr<HistoryRevisionData>(revision.data_);
  pool_.start([weak] {
    const auto entry = weak.lock();
    if (!entry) return;
    std::optional<HistoryStateRecord> record;
    {
      QMutexLocker lock(&entry->mutex);
      if (!entry->pinned || entry->resident) return;
      record = entry->record;
    }
    if (!record) return;
    auto restored = record->restore();
    QMutexLocker lock(&entry->mutex);
    if (entry->pinned && restored) entry->resident = std::move(restored);
  }, 1);
}
void HistoryStorage::collectUnused() {
  if (data_->collecting.exchange(true)) return;
  const auto storage = data_;
  pool_.start([storage] {
    std::shared_ptr<HistoryPayloadStore> store;
    { QMutexLocker lock(&storage->mutex); store = storage->store; }
    if (store) store->collectUnused();
    storage->collecting.store(false);
  }, -1);
}
void HistoryStorage::waitForIdle() { pool_.waitForDone(); }
QVariantMap HistoryStorage::statistics() const {
  std::shared_ptr<HistoryPayloadStore> store;
  {
    QMutexLocker lock(&data_->mutex);
    store = data_->store;
  }
  auto result = store ? store->statistics() : QVariantMap{};
  if (store) result.insert(QStringLiteral("spill_directory"), store->directory());
  return result;
}
}  // namespace photo_editor::desktop
