#include "history_payload_store.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QSaveFile>
#include <QTemporaryDir>
#include <algorithm>
#include <list>

namespace photo_editor::desktop {
struct HistoryPayloadEntry final {
  QString id;
  qint64 size = 0;
  qint64 resident_bytes = 0;
  QByteArray data;
  bool stored = false;
  std::list<QString>::iterator position;
};

struct HistoryPayloadStoreState final {
  mutable QMutex mutex;
  std::unique_ptr<QTemporaryDir> directory;
  QString directory_error;
  QHash<QString, std::shared_ptr<HistoryPayloadEntry>> entries;
  std::list<QString> resident_order;
  qint64 budget = 0;
  qint64 resident_bytes = 0;
  qint64 spilled_bytes = 0;
  qint64 deduplicated_captures = 0;
  qint64 spill_failures = 0;
  qint64 load_failures = 0;
};

namespace {
QString payload_path(const HistoryPayloadStoreState& store, const HistoryPayloadEntry& entry) {
  return store.directory ? store.directory->filePath(entry.id + QStringLiteral(".blob")) : QString{};
}
QString content_id(const QByteArray& data) {
  return QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
}
bool fail(QString* error, const QString& message) {
  if (error) *error = message;
  return false;
}
// Caller holds the store mutex. A single store serializes worker I/O so a load
// cannot race spill publication or collection. No application/cache locks enter.
bool trim_locked(HistoryPayloadStoreState& store, QString* error) {
  while (store.resident_bytes > store.budget && !store.resident_order.empty()) {
    auto& entry = *store.entries.value(store.resident_order.front());
    if (!entry.stored) {
      if (!store.directory || !store.directory->isValid()) {
        ++store.spill_failures;
        return fail(error, store.directory_error);
      }
      QSaveFile file(payload_path(store, entry));
      if (!file.open(QIODevice::WriteOnly) || file.write(entry.data) != entry.size || !file.commit()) {
        ++store.spill_failures;
        return fail(error, QStringLiteral("Unable to preserve undo payload: %1").arg(file.errorString()));
      }
      entry.stored = true;
      store.spilled_bytes += entry.size;
    }
    // Only release RAM after the complete file has been atomically published.
    store.resident_bytes -= entry.resident_bytes;
    entry.data = {};
    entry.resident_bytes = 0;
    store.resident_order.pop_front();
  }
  return true;
}
}

HistoryPayloadStore::HistoryPayloadStore(const QString& parent_directory, qint64 resident_budget_bytes)
    : state_(std::make_shared<HistoryPayloadStoreState>()) {
  state_->budget = std::max(qint64{0}, resident_budget_bytes);
  if (!parent_directory.isEmpty() && QDir().mkpath(parent_directory)) {
    state_->directory = std::make_unique<QTemporaryDir>(QDir(parent_directory).filePath(QStringLiteral("history-XXXXXX")));
  }
  if (!state_->directory || !state_->directory->isValid())
    state_->directory_error = QStringLiteral("Undo spill storage is unavailable; payloads remain in memory.");
}

HistoryPayloadHandle HistoryPayloadStore::retain(const QByteArray& bytes, QString* error) {
  if (error) error->clear();
  const auto id = content_id(bytes);
  HistoryPayloadHandle result;
  result.store_ = state_;
  QMutexLocker lock(&state_->mutex);
  auto entry = state_->entries.value(id);
  if (entry && entry->size != bytes.size()) {
    fail(error, QStringLiteral("Undo payload identity has conflicting lengths."));
    return {};
  }
  if (entry && entry->size == bytes.size()) {
    ++state_->deduplicated_captures;
    if (entry->resident_bytes > 0)
      state_->resident_order.splice(state_->resident_order.end(), state_->resident_order, entry->position);
  } else {
    entry = std::make_shared<HistoryPayloadEntry>();
    entry->id = id;
    entry->size = bytes.size();
    // fromRawData() does not own its buffer. Materialize borrowed storage before
    // retaining it; ordinary Qt-owned buffers stay implicitly shared.
    entry->data = !bytes.isEmpty() && bytes.capacity() == 0
        ? QByteArray(bytes.constData(), bytes.size()) : bytes;
    entry->resident_bytes = std::max(entry->data.size(), entry->data.capacity());
    state_->entries.insert(id, entry);
    if (entry->resident_bytes > 0) {
      state_->resident_order.push_back(id);
      entry->position = std::prev(state_->resident_order.end());
      state_->resident_bytes += entry->resident_bytes;
    }
  }
  result.entry_ = std::move(entry);
  static_cast<void>(trim_locked(*state_, error));
  return result;
}

bool HistoryPayloadStore::setResidentBudget(qint64 bytes, QString* error) {
  if (error) error->clear();
  QMutexLocker lock(&state_->mutex);
  state_->budget = std::max(qint64{0}, bytes);
  return trim_locked(*state_, error);
}

QString HistoryPayloadHandle::contentId() const { return entry_ ? entry_->id : QString{}; }
qint64 HistoryPayloadHandle::size() const { return entry_ ? entry_->size : 0; }

std::optional<QByteArray> HistoryPayloadHandle::load(QString* error) const {
  if (error) error->clear();
  if (!store_ || !entry_) {
    fail(error, QStringLiteral("Undo payload handle is empty."));
    return std::nullopt;
  }
  QMutexLocker lock(&store_->mutex);
  if (entry_->resident_bytes > 0 || entry_->size == 0) return entry_->data;
  QFile file(payload_path(*store_, *entry_));
  if (!entry_->stored || !file.open(QIODevice::ReadOnly) || file.size() != entry_->size) {
    ++store_->load_failures;
    fail(error, QStringLiteral("Undo payload is missing or has the wrong length."));
    return std::nullopt;
  }
  auto bytes = file.readAll();
  if (bytes.size() != entry_->size || content_id(bytes) != entry_->id) {
    ++store_->load_failures;
    fail(error, QStringLiteral("Undo payload checksum does not match."));
    return std::nullopt;
  }
  return bytes;
}

void HistoryPayloadStore::collectUnused() {
  QMutexLocker lock(&state_->mutex);
  for (auto it = state_->entries.begin(); it != state_->entries.end();) {
    if (it.value().use_count() != 1) { ++it; continue; }
    const auto& entry = *it.value();
    if (entry.stored && QFileInfo::exists(payload_path(*state_, entry)) &&
        !QFile::remove(payload_path(*state_, entry))) { ++it; continue; }
    if (entry.resident_bytes > 0) {
      state_->resident_order.erase(entry.position);
      state_->resident_bytes -= entry.resident_bytes;
    }
    if (entry.stored) state_->spilled_bytes -= entry.size;
    it = state_->entries.erase(it);
  }
}

QVariantMap HistoryPayloadStore::statistics() const {
  QMutexLocker lock(&state_->mutex);
  return {{QStringLiteral("resident_bytes"), state_->resident_bytes},
          {QStringLiteral("budget_bytes"), state_->budget},
          {QStringLiteral("over_budget_bytes"), std::max(qint64{0}, state_->resident_bytes - state_->budget)},
          {QStringLiteral("spilled_bytes"), state_->spilled_bytes},
          {QStringLiteral("unique_payloads"), state_->entries.size()},
          {QStringLiteral("deduplicated_captures"), state_->deduplicated_captures},
          {QStringLiteral("spill_failures"), state_->spill_failures},
          {QStringLiteral("load_failures"), state_->load_failures}};
}

QString HistoryPayloadStore::directory() const {
  QMutexLocker lock(&state_->mutex);
  return state_->directory ? state_->directory->path() : QString{};
}

}  // namespace photo_editor::desktop
