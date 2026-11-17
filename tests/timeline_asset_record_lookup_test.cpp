#include "timeline_asset_record_lookup.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QThread>
#include <QTimer>

#include <algorithm>
#include <atomic>
#include <functional>
#include <iostream>
#include <vector>

using photo_editor::desktop::TimelineAssetRecord;
using photo_editor::desktop::TimelineAssetRecordLookup;

namespace {

bool pump_until(const std::function<bool()>& predicate, int timeout_ms) {
  QElapsedTimer elapsed;
  elapsed.start();
  while (!predicate() && elapsed.elapsed() < timeout_ms) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    QThread::msleep(1);
  }
  QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
  return predicate();
}

bool test_coalescing_stale_suppression_and_heartbeat() {
  std::atomic_int loader_calls{0};
  std::atomic_bool loader_on_gui_thread{false};
  const auto* gui_thread = QThread::currentThread();
  std::atomic_int active_loaders{0};
  std::atomic_int max_active_loaders{0};
  QString current_timeline_id = QStringLiteral("timeline-old");
  quint64 current_session_generation = 1;

  struct Publication final {
    QString timeline_id;
    quint64 session_generation = 0;
    QString record_timeline_id;
  };
  std::vector<Publication> publications;

  TimelineAssetRecordLookup lookup(
      [&]() {
        if (QThread::currentThread() == gui_thread) loader_on_gui_thread.store(true);
        const auto call = loader_calls.fetch_add(1) + 1;
        const auto active = active_loaders.fetch_add(1) + 1;
        auto previous_max = max_active_loaders.load();
        while (active > previous_max &&
               !max_active_loaders.compare_exchange_weak(previous_max, active)) {
        }

        QThread::msleep(call == 1 ? 160UL : 20UL);
        TimelineAssetRecord record;
        record.timeline_id = QStringLiteral("records-%1").arg(call);
        active_loaders.fetch_sub(1);
        return QVector<TimelineAssetRecord>{record};
      },
      [&](const QString& timeline_id, quint64 session_generation) {
        return current_timeline_id == timeline_id &&
               current_session_generation == session_generation;
      },
      [&](const QString& timeline_id, quint64 session_generation,
          const QVector<TimelineAssetRecord>& records) {
        publications.push_back(Publication{
            timeline_id,
            session_generation,
            records.isEmpty() ? QString() : records.constFirst().timeline_id});
      });

  QElapsedTimer heartbeat_clock;
  heartbeat_clock.start();
  qint64 last_heartbeat_ms = -1;
  qint64 maximum_heartbeat_gap_ms = 0;
  int heartbeat_count = 0;
  QTimer heartbeat;
  heartbeat.setInterval(2);
  QObject::connect(&heartbeat, &QTimer::timeout, [&]() {
    const auto now_ms = heartbeat_clock.elapsed();
    if (last_heartbeat_ms >= 0) {
      maximum_heartbeat_gap_ms =
          std::max(maximum_heartbeat_gap_ms, now_ms - last_heartbeat_ms);
    }
    last_heartbeat_ms = now_ms;
    ++heartbeat_count;
  });
  heartbeat.start();

  lookup.request(QStringLiteral("timeline-old"), 1);
  if (!pump_until([&]() { return loader_calls.load() == 1; }, 500)) {
    return false;
  }

  current_timeline_id = QStringLiteral("timeline-latest");
  current_session_generation = 7;
  for (int index = 0; index < 64; ++index) {
    lookup.request(QStringLiteral("timeline-intermediate"), 2);
    lookup.request(QStringLiteral("timeline-latest"), 6);
    lookup.request(QStringLiteral("timeline-latest"), 7);
  }

  const bool completed = pump_until(
      [&]() { return loader_calls.load() == 2 && publications.size() == 1; },
      1500);
  heartbeat.stop();

  std::cout << "catalog-lookup calls=" << loader_calls.load()
            << " maxConcurrent=" << max_active_loaders.load()
            << " heartbeatMaxGapMs=" << maximum_heartbeat_gap_ms
            << " heartbeatCount=" << heartbeat_count
            << " loaderOnGuiThread=" << loader_on_gui_thread.load() << '\n';
  return completed && !loader_on_gui_thread.load() && max_active_loaders.load() == 1 &&
         publications.front().timeline_id == QStringLiteral("timeline-latest") &&
         publications.front().session_generation == 7 &&
         publications.front().record_timeline_id == QStringLiteral("records-2") &&
         heartbeat_count >= 10 && maximum_heartbeat_gap_ms < 50;
}

bool test_destruction_during_in_flight_load() {
  std::atomic_bool loader_started{false};
  std::atomic_bool loader_finished{false};
  int publication_count = 0;

  auto* lookup = new TimelineAssetRecordLookup(
      [&]() {
        loader_started.store(true);
        QThread::msleep(100UL);
        loader_finished.store(true);
        return QVector<TimelineAssetRecord>{};
      },
      [](const QString&, quint64) { return true; },
      [&](const QString&, quint64, const QVector<TimelineAssetRecord>&) {
        ++publication_count;
      });

  lookup->request(QStringLiteral("timeline-destroyed"), 4);
  if (!pump_until([&]() { return loader_started.load(); }, 500)) {
    delete lookup;
    return false;
  }

  QElapsedTimer destruction_clock;
  destruction_clock.start();
  delete lookup;
  const auto destruction_ms = destruction_clock.elapsed();

  const bool worker_finished =
      pump_until([&]() { return loader_finished.load(); }, 1000);
  QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
  std::cout << "catalog-lookup destructionMs=" << destruction_ms
            << " latePublications=" << publication_count << '\n';
  return worker_finished && destruction_ms < 50 && publication_count == 0;
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication application(argc, argv);
  if (!test_coalescing_stale_suppression_and_heartbeat()) {
    std::cerr << "Timeline asset lookup coalescing, stale suppression, or GUI heartbeat failed\n";
    return 1;
  }
  if (!test_destruction_during_in_flight_load()) {
    std::cerr << "Timeline asset lookup destruction safety failed\n";
    return 1;
  }
  std::cout << "Timeline asset lookup coalescing, stale guards, heartbeat, and destruction passed\n";
  return 0;
}
