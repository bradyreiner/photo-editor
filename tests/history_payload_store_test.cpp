#include "history_payload_store.h"
#include "history_state_record.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <array>
#include <future>
#include <iostream>
#if defined(Q_OS_UNIX)
#include <sys/resource.h>
#endif

using namespace photo_editor::desktop;
namespace {
int fail(const char* message) { std::cerr << message << '\n'; return 1; }
QByteArray payload(int revision, qsizetype size) {
  QByteArray bytes(size, Qt::Uninitialized);
  quint32 value = static_cast<quint32>(revision) + 1;
  for (qsizetype i = 0; i < size; ++i) {
    value = value * 1664525U + 1013904223U;
    bytes[i] = static_cast<char>(value >> 24U);
  }
  return bytes;
}
bool contracts(const QString& directory) {
  QString error;
  HistoryPayloadStore store(directory, 8192);
  auto a_bytes = payload(1, 4096);
  auto a = store.retain(a_bytes, &error);
  auto duplicate = store.retain(QByteArray(a_bytes.constData(), a_bytes.size()), &error);
  if (!error.isEmpty() || a.contentId() != duplicate.contentId() ||
      store.statistics().value("unique_payloads").toInt() != 1 ||
      store.statistics().value("resident_bytes").toLongLong() != 4096) return false;
  a_bytes[0] = static_cast<char>(a_bytes[0] ^ 1);
  if (a.load() != payload(1, 4096)) return false;
  auto b = store.retain(payload(2, 4096));
  auto c = store.retain(payload(3, 4096));
  if (store.statistics().value("resident_bytes").toLongLong() > 8192 || a.load() != payload(1, 4096)) return false;
  // A successful returned buffer owns its data across further spills/collection.
  auto returned = b.load();
  if (!store.setResidentBudget(0, &error) || !error.isEmpty() || b.load() != returned ||
      store.statistics().value("resident_bytes").toLongLong() != 0) return false;
  const auto b_id = b.contentId();
  b = {};
  store.collectUnused();
  if (QFileInfo::exists(store.directory() + "/" + b_id + ".blob") || returned != payload(2, 4096)) return false;
  auto empty = store.retain({});
  if (!empty.load().has_value() || !empty.load()->isEmpty()) return false;
  EditRecipeState state;
  state.exposure = 1.234567890123;
  const auto raster = payload(17, 128 * 1024);
  state.mask_stack = {QVariantMap{{"id", "mask"}, {"segmentationData", raster}}};
  state.layers = {QVariantMap{{"id", "layer"}, {"paintTiles", QVariantHash{{"tile", raster}}}}};
  state.recipe_snapshots = {QVariantMap{{"nested", state.mask_stack}}};
  auto record = HistoryStateRecord::capture(state, store, &error);
  if (!record || !error.isEmpty() || record->payloadCount() != 3) return false;
  const auto restored_state = record->restore(&error);
  if (!restored_state || !error.isEmpty() || restored_state->exposure != state.exposure ||
      restored_state->layers != state.layers || restored_state->mask_stack != state.mask_stack ||
      restored_state->recipe_snapshots != state.recipe_snapshots) return false;
  const auto restored_mask = restored_state->mask_stack.front().toMap().value("segmentationData").toByteArray();
  const auto restored_tile = restored_state->layers.front().toMap().value("paintTiles").toHash().value("tile").toByteArray();
  if (restored_mask.constData() != restored_tile.constData()) return false;
  // Whole-record failure is reported without exposing a partially restored state.
  const auto raster_id = QString::fromLatin1(QCryptographicHash::hash(raster, QCryptographicHash::Sha256).toHex());
  QFile::remove(store.directory() + "/" + raster_id + ".blob");
  if (record->restore(&error).has_value() || error.isEmpty() || state.layers.isEmpty()) return false;
  QByteArray borrowed_source = payload(4, 4096);
  HistoryPayloadStore borrowed_store(directory, 8192);
  auto borrowed = borrowed_store.retain(QByteArray::fromRawData(borrowed_source.constData(), borrowed_source.size()));
  borrowed_source.fill('x');
  if (borrowed.load() != payload(4, 4096)) return false;

  // Same-length corruption must not return unverified bytes or alter handles.
  QFile corrupt(store.directory() + "/" + a.contentId() + ".blob");
  if (!corrupt.open(QIODevice::WriteOnly | QIODevice::Truncate) || corrupt.write(QByteArray(4096, 'x')) != 4096) return false;
  corrupt.close();
  if (a.load(&error).has_value() || error.isEmpty() || !a.valid()) return false;
  QFile::remove(store.directory() + "/" + c.contentId() + ".blob");
  if (c.load(&error).has_value() || error.isEmpty()) return false;

  QFile blocker(directory + "/not-a-directory");
  if (!blocker.open(QIODevice::WriteOnly)) return false;
  blocker.close();
  HistoryPayloadStore failed(blocker.fileName(), 0);
  auto preserved = failed.retain(payload(5, 4096), &error);
  if (error.isEmpty() || preserved.load() != payload(5, 4096) ||
      failed.statistics().value("over_budget_bytes").toLongLong() != 4096 ||
      failed.statistics().value("spill_failures").toInt() != 1) return false;

  HistoryPayloadHandle surviving;
  QString surviving_directory;
  {
    HistoryPayloadStore short_lived(directory, 0);
    surviving = short_lived.retain(payload(6, 4096));
    surviving_directory = short_lived.directory();
  }
  if (!QFileInfo::exists(surviving_directory) || surviving.load() != payload(6, 4096)) return false;
  surviving = {};
  if (QFileInfo::exists(surviving_directory)) return false;

  HistoryPayloadStore concurrent(directory, 8192);
  std::array<std::future<bool>, 4> workers;
  for (int i = 0; i < 4; ++i) workers[static_cast<std::size_t>(i)] = std::async(std::launch::async, [&, i] {
    for (int j = 0; j < 10; ++j) {
      const auto original = payload(i * 10 + j, 4096);
      const auto handle = concurrent.retain(original);
      if (handle.load() != original) return false;
    }
    return true;
  });
  for (auto& worker : workers) if (!worker.get()) return false;
  concurrent.collectUnused();
  return concurrent.statistics().value("unique_payloads").toInt() == 0 &&
      concurrent.statistics().value("resident_bytes").toLongLong() == 0;
}
}
int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  QTemporaryDir directory;
  if (!directory.isValid()) return fail("Temporary directory unavailable");
  const bool baseline = app.arguments().contains("--baseline");
  const bool large = app.arguments().contains("--45mp");
  const qsizetype block_size = large ? 45'000'000 : 4 * 1024 * 1024;
  const int revisions = large ? 100 : 64;
  const qint64 budget = large ? 128 * 1024 * 1024 : 32 * 1024 * 1024;
  if (baseline && large) return fail("The 4.5 GB in-memory control is intentionally disabled; use the bounded baseline");
  HistoryPayloadStore store(directory.path(), budget);
  QVector<QByteArray> baseline_payloads;
  QVector<HistoryPayloadHandle> handles;
  QVector<QByteArray> expected_hashes;
  QElapsedTimer timer;
  qint64 capture_ns = 0;
  for (int i = 0; i < revisions; ++i) {
    const auto bytes = payload(i, block_size);
    expected_hashes.append(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256));
    timer.start();
    if (baseline) baseline_payloads.append(bytes);
    else {
      QString error;
      handles.append(store.retain(bytes, &error));
      if (!error.isEmpty()) return fail("Spill failed in benchmark");
    }
    capture_ns += timer.nsecsElapsed();
  }
  qint64 load_ns = 0;
  for (int i = 0; i < revisions; ++i) {
    timer.start();
    const auto restored = baseline ? std::optional<QByteArray>{baseline_payloads.at(i)} : handles.at(i).load();
    load_ns += timer.nsecsElapsed();
    if (!restored || QCryptographicHash::hash(*restored, QCryptographicHash::Sha256) != expected_hashes.at(i))
      return fail("Exact payload restoration failed");
  }
  const auto stats = store.statistics();
  if (!baseline && stats.value("resident_bytes").toLongLong() > budget) return fail("Resident budget exceeded");
  qint64 peak = 0;
#if defined(Q_OS_UNIX)
  struct rusage usage{};
  getrusage(RUSAGE_SELF, &usage);
  peak = static_cast<qint64>(usage.ru_maxrss);
#if !defined(Q_OS_MACOS)
  peak *= 1024;
#endif
#endif
  if (!baseline && !contracts(directory.path())) return fail("Payload storage contract failed");
  QJsonObject result{{"baseline", baseline}, {"revisions", revisions}, {"block_bytes", static_cast<qint64>(block_size)},
      {"retained_logical_bytes", static_cast<qint64>(block_size) * revisions},
      {"resident_owned_bytes", baseline ? static_cast<qint64>(block_size) * revisions : stats.value("resident_bytes").toLongLong()},
      {"peak_rss_bytes", peak}, {"capture_total_ms", static_cast<double>(capture_ns) / 1e6},
      {"restore_total_ms", static_cast<double>(load_ns) / 1e6}, {"all_payloads_restored", true},
      {"store", QJsonObject::fromVariantMap(stats)}};
  const auto output = app.arguments().indexOf("--output");
  if (output >= 0 && output + 1 < app.arguments().size()) {
    QFile file(app.arguments().at(output + 1));
    if (!file.open(QIODevice::WriteOnly)) return fail("Cannot write artifact");
    file.write(QJsonDocument(result).toJson());
  }
  std::cout << QJsonDocument(result).toJson(QJsonDocument::Compact).constData() << '\n';
  return 0;
}
