#include "app_bootstrap.h"
#include "observability.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <algorithm>
#include <barrier>
#include <cmath>
#include <iostream>
#include <thread>
#include <vector>
#if defined(__APPLE__)
#include <sys/resource.h>
#endif

// Only the storage-root dependency is replaced. The logger/Qt message handler
// and disk writes are the production implementation.
QString photo_editor::desktop::AppBootstrap::logLocation() {
  return qEnvironmentVariable("PHOTO_EDITOR_OBSERVABILITY_FIXTURE_ROOT");
}

namespace {
void require(bool value, const char* message) {
  if (!value) throw std::runtime_error(message);
}
double percentile(std::vector<double> values, double fraction) {
  std::sort(values.begin(), values.end());
  return values.at(static_cast<std::size_t>(std::ceil(static_cast<double>(values.size()) * fraction)) - 1);
}
}

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  if (argc != 4 && argc != 5) return 2;
  const auto mode = argc == 5 ? QString::fromLocal8Bit(argv[4]) : QStringLiteral("quiet");
  const bool disabled = mode == "disabled";
  const int producers = QString::fromLocal8Bit(argv[1]).toInt();
  const int count = QString::fromLocal8Bit(argv[2]).toInt();
  if (producers < 1 || producers > 16 || count < 100) return 2;
  QTemporaryDir root;
  if (!root.isValid()) return 2;
  qputenv("PHOTO_EDITOR_OBSERVABILITY_FIXTURE_ROOT", root.path().toUtf8());
  qputenv("PHOTO_EDITOR_OBSERVABILITY_RUN_DIR", (root.path() + "/run").toUtf8());
  qputenv("PHOTO_EDITOR_SESSION_ID", "logging-fixture");
  qputenv("PHOTO_EDITOR_OBSERVABILITY_CONSOLE", disabled ? QByteArray("quiet") : mode.toUtf8());
  qputenv("PHOTO_EDITOR_LOG_COLOR", "never");
  qputenv("PHOTO_EDITOR_LOG_LEVEL", "info");
  namespace logs = photo_editor::desktop::observability;
  logs::initialize();
  if (disabled) QLoggingCategory::setFilterRules(QStringLiteral("photo_editor.perf.info=false"));
  std::vector<std::vector<double>> latencies(static_cast<std::size_t>(producers));
  std::vector<std::vector<double>> chunks(static_cast<std::size_t>(producers));
  std::barrier start(producers + 1);
  std::vector<std::thread> workers;
  QElapsedTimer total;
  for (int producer = 0; producer < producers; ++producer) {
    workers.emplace_back([&, producer] {
      auto& samples = latencies.at(static_cast<std::size_t>(producer));
      auto& chunk_samples = chunks.at(static_cast<std::size_t>(producer));
      samples.reserve(static_cast<std::size_t>(count));
      const QString payload(256, QLatin1Char('x'));
      start.arrive_and_wait();
      QElapsedTimer chunk;
      chunk.start();
      for (int index = 0; index < count; ++index) {
        QElapsedTimer call;
        call.start();
        logs::logInfo(photoEditorPerfLog(), "preview.logging.fixture",
            {{"producer", producer}, {"index", index}, {"payload", payload},
             {"render_reason", "slider_drag"}, {"full_resolution", false},
             {"decoded_cache_hit", true}, {"elapsed_ms", 12.5}});
        samples.push_back(static_cast<double>(call.nsecsElapsed()) / 1e3);
        if ((index + 1) % 100 == 0) {
          chunk_samples.push_back(static_cast<double>(chunk.nsecsElapsed()) / 1e6);
          chunk.restart();
        }
      }
    });
  }
  total.start();
  start.arrive_and_wait();
  for (auto& worker : workers) worker.join();
  const double producer_ms = static_cast<double>(total.nsecsElapsed()) / 1e6;
  // Critical records are an immediate delivery barrier in both implementations.
  qCritical().noquote() << "logging.fixture.flush";
  const double drained_ms = static_cast<double>(total.nsecsElapsed()) / 1e6;
#if defined(__APPLE__)
  rusage usage{};
  getrusage(RUSAGE_SELF, &usage);
#endif
  try {
    QFile file(logs::desktopLogFilePath());
    require(file.open(QIODevice::ReadOnly), "Cannot read fixture log");
    const auto bytes = file.readAll();
    std::vector<int> next(static_cast<std::size_t>(producers), 0);
    const int expected_records = disabled ? 0 : producers * count;
    std::vector<QJsonObject> normalized(static_cast<std::size_t>(expected_records));
    int observed = 0;
    for (const auto& line : bytes.split('\n')) {
      if (line.isEmpty()) continue;
      QJsonParseError error;
      const auto document = QJsonDocument::fromJson(line, &error);
      require(error.error == QJsonParseError::NoError && document.isObject(), "Malformed JSONL record");
      auto record = document.object();
      if (record.value("event") != "preview.logging.fixture") continue;
      auto fields = record.value("fields").toObject();
      const int producer = fields.value("producer").toInt(-1);
      require(producer >= 0 && producer < producers, "Invalid producer identity");
      const int index = fields.value("index").toInt(-1);
      require(index == next.at(static_cast<std::size_t>(producer))++, "Missing, duplicated or reordered record");
      require(record.value("thread_id") == fields.value("thread"), "Event lost its producer thread identity");
      require(fields.value("workflow") == "preview" && fields.value("reason") == "slider_drag" &&
              fields.value("cache_hit").toBool() && fields.value("interactive").toBool(), "Standard fields changed");
      require(!record.value("timestamp").toString().isEmpty(), "Event lost timestamp");
      record.remove("timestamp");record.remove("pid");record.remove("thread_id");fields.remove("thread");
      record.remove("file");record.remove("line");record.remove("function");
      record.insert("fields", fields);
      normalized.at(static_cast<std::size_t>(producer * count + index)) = record;
      ++observed;
    }
    require(observed == expected_records, "Logging dropped records before the critical barrier");
    QJsonArray canonical;
    for (const auto& record : normalized) canonical.append(record);
    std::vector<double> all, all_chunks;
    for (const auto& samples : latencies) all.insert(all.end(), samples.begin(), samples.end());
    for (const auto& samples : chunks) all_chunks.insert(all_chunks.end(), samples.begin(), samples.end());
    QJsonObject report{{"producers", producers}, {"events_per_producer", count}, {"records", observed},
        {"console_mode", mode},
        {"producer_call_p95_us", percentile(all, 0.95)}, {"producer_call_p99_us", percentile(all, 0.99)},
        {"producer_chunk_p95_ms", percentile(all_chunks, 0.95)}, {"producer_elapsed_ms", producer_ms},
        {"drained_elapsed_ms", drained_ms}, {"log_bytes", static_cast<qint64>(bytes.size())},
        {"exact_records_and_order", true},
        {"normalized_sha256", QString::fromLatin1(QCryptographicHash::hash(QJsonDocument(canonical).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256).toHex())}};
#if defined(__APPLE__)
    report.insert("peak_rss_mib", static_cast<double>(usage.ru_maxrss) / (1024.0 * 1024.0));
#endif
    QFile output(QString::fromLocal8Bit(argv[3]));
    require(output.open(QIODevice::WriteOnly), "Cannot write benchmark report");
    output.write(QJsonDocument(report).toJson());
    std::cout << QJsonDocument(report).toJson(QJsonDocument::Compact).constData() << '\n';
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
  return 0;
}
