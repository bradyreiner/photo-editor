#include "catalog_asset_model.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QTemporaryDir>
#include <QStandardPaths>
#include <QtEndian>
#include <QThread>
#include <QTimer>
#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>
#include <csignal>
#include <cerrno>

using namespace photo_editor::desktop;
namespace {
bool write_file(const QString& path, const QByteArray& bytes) {
  QFile file(path);
  return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
QByteArray read_file(const QString& path) {
  QFile file(path);
  return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}
bool wait_until(const std::function<bool()>& ready, int timeout_ms = 6000) {
  QElapsedTimer timer;
  timer.start();
  while (!ready() && timer.elapsed() < timeout_ms) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    QThread::msleep(1);
  }
  return ready();
}
void pump_events(int milliseconds) {
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < milliseconds) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    QThread::msleep(1);
  }
}
int fail(const char* message) {
  std::cerr << message << '\n';
  return 1;
}

// The same executable is linked as ffprobe in an isolated PATH. Gates make
// scheduling assertions independent of subprocess timing or installed codecs.
int probe_fixture(const QStringList& arguments) {
  const QDir root(qEnvironmentVariable("PHOTO_EDITOR_DURATION_FIXTURE_ROOT"));
  const auto source = arguments.last();
  const auto name = QFileInfo(source).fileName();
  const auto duration = read_file(source);
  {
    QLockFile lock(root.filePath("counter.lock"));
    if (!lock.tryLock(2000)) return 2;
    const auto count = read_file(root.filePath("counter")).toInt() + 1;
    if (!write_file(root.filePath(QStringLiteral("launch-%1").arg(count)), name.toUtf8()) ||
        !write_file(root.filePath(QStringLiteral("pid-%1").arg(count)), QByteArray::number(QCoreApplication::applicationPid())) ||
        !write_file(root.filePath("counter"), QByteArray::number(count))) return 2;
  }
  QElapsedTimer timer;
  timer.start();
  while (!QFileInfo::exists(root.filePath("release-" + name))) {
    if (timer.elapsed() > 10000) return 3;
    QThread::msleep(2);
  }
  std::cout << duration.constData() << '\n' << std::flush;
  if (!write_file(root.filePath("finished-" + name), "done")) return 2;
  return 0;
}
}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  if (app.arguments().contains(QStringLiteral("-show_entries")) &&
      qEnvironmentVariableIsSet("PHOTO_EDITOR_DURATION_FIXTURE_ROOT")) return probe_fixture(app.arguments());
  QCoreApplication::setOrganizationName(QStringLiteral("PhotoEditorTests"));
  QCoreApplication::setApplicationName(QStringLiteral("CatalogMediaDurationTest"));
  QTemporaryDir temporary;
  if (!temporary.isValid()) return fail("Cannot create fixture directory");
  const QDir root(temporary.path());
  const auto binaries = root.filePath("bin");
  if (!QDir().mkpath(binaries) ||
      !QFile::link(app.applicationFilePath(), QDir(binaries).filePath("ffprobe"))) return fail("Cannot install fake probe");
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", root.filePath("data").toUtf8());
  qputenv("PHOTO_EDITOR_CACHE_DIR", root.filePath("cache").toUtf8());
  qputenv("PHOTO_EDITOR_DURATION_FIXTURE_ROOT", root.absolutePath().toUtf8());
  const auto original_path = qgetenv("PATH");
  const bool real_probe_available = !QStandardPaths::findExecutable(QStringLiteral("ffprobe")).isEmpty();
  qputenv("PATH", binaries.toUtf8());
  const auto launch_count = [&]() { return read_file(root.filePath("counter")).toInt(); };
  const auto launched = [&](int index) { return QString::fromUtf8(read_file(root.filePath(QStringLiteral("launch-%1").arg(index)))); };
  const auto release = [&](const QString& name) { return write_file(root.filePath("release-" + name), "go"); };
  const auto make_item = [&](const QString& id, const QString& name, const QByteArray& seconds) {
    CatalogAssetItem item;
    item.asset_id = id;
    item.file_path = root.filePath(name);
    item.camera_label = name;
    item.media_type = QStringLiteral("video");
    item.online = true;
    if (!write_file(item.file_path, seconds)) item.online = false;
    return item;
  };
  CatalogAssetModel model;
  model.setInteractiveRenderingActive(true);  // No thumbnail decoding in this fixture.
  QVector<CatalogAssetItem> items;
  for (int index = 0; index < 8; ++index) {
    items.append(make_item(QStringLiteral("asset-%1").arg(index),
        QStringLiteral("clip-%1.mp4").arg(index), QByteArray::number(index + 1)));
    if (!items.last().online) return fail("Cannot write fixture media");
  }
  auto duplicate = items.at(2);
  duplicate.asset_id = "duplicate";
  items.append(duplicate);
  int heartbeats = 0;
  QTimer heartbeat;
  heartbeat.setInterval(5);
  QObject::connect(&heartbeat, &QTimer::timeout, [&]() { ++heartbeats; });
  heartbeat.start();
  QElapsedTimer publication;
  publication.start();
  model.replaceAssetsForTesting(items);
  const auto publication_ms = publication.elapsed();
  if (model.rowCount() != 9 || publication_ms > 500 ||
      !wait_until([&]() { return launch_count() == 2; })) return fail("Rows blocked on probes or workers did not start");
  const auto ticks_before = heartbeats;
  pump_events(100);
  if (launch_count() != 2 || heartbeats - ticks_before < 5)
    return fail("More than two probes ran, or the model event loop stalled");
  const QSet<QString> initial{launched(1), launched(2)};
  if (initial != QSet<QString>{"clip-0.mp4", "clip-1.mp4"}) return fail("Unexpected initial probe order");
  model.setVisibleAssetRange(5, 6);
  model.selectIndex(7);
  if (!release("clip-0.mp4") || !wait_until([&]() { return launch_count() >= 3; }) || launched(3) != "clip-7.mp4")
    return fail("New selection did not precede background probes");
  if (!release("clip-7.mp4") || !wait_until([&]() { return launch_count() >= 4; }) || launched(4) != "clip-5.mp4")
    return fail("Visible rows did not precede background probes");
  if (!release("clip-5.mp4") || !wait_until([&]() { return launch_count() >= 5; }) || launched(5) != "clip-6.mp4")
    return fail("Viewport priority order changed");
  for (int index = 0; index < 8; ++index)
    if (!release(QStringLiteral("clip-%1.mp4").arg(index))) return fail("Cannot release fixture probe");
  const auto all_durations_present = [&]() {
    for (const auto& item : items)
      if (model.assetById(item.asset_id).value("durationSeconds").toDouble() <= 0) return false;
    return true;
  };
  if (!wait_until(all_durations_present) || launch_count() != 8 ||
      model.assetById("duplicate").value("durationSeconds").toDouble() != 3.0)
    return fail("Duplicate paths were not grouped or durations were lost");
  model.replaceAssetsForTesting(items);
  if (!wait_until(all_durations_present) || launch_count() != 8) return fail("Warm refresh launched another probe");
  if (!write_file(items.at(2).file_path, "23")) return fail("Cannot change source revision");
  model.replaceAssetsForTesting(items);
  if (!wait_until([&]() {
        return all_durations_present() && model.assetById("duplicate").value("durationSeconds").toDouble() == 23.0;
      }) || launch_count() != 9) return fail("Changed source did not invalidate exactly one grouped cache entry");

  auto old = make_item("replacement", "old.mp4", "777");
  model.replaceAssetsForTesting({old});
  if (!wait_until([&]() { return launch_count() == 10; })) return fail("Old generation probe did not start");
  auto replacement = make_item("replacement", "new.mp4", "888");
  if (!release("new.mp4")) return fail("Cannot release replacement probe");
  model.replaceAssetsForTesting({replacement});
  if (!wait_until([&]() { return model.assetById("replacement").value("durationSeconds").toDouble() == 888; }))
    return fail("Replacement source did not publish");
  pump_events(150);
  if (model.assetById("replacement").value("durationSeconds").toDouble() != 888 || launch_count() != 11)
    return fail("Cancelled source replaced current duration");

  auto stale = make_item("same-path", "stale.mp4", "777");
  model.replaceAssetsForTesting({stale});
  if (!wait_until([&]() { return launch_count() == 12; }) || !release("stale.mp4"))
    return fail("Cannot prepare stale completion");
  // Deliberately hold model-thread delivery until the worker has finished, then
  // replace the generation with an authoritative prepopulated duration.
  QElapsedTimer finish_wait;
  finish_wait.start();
  while (!QFileInfo::exists(root.filePath("finished-stale.mp4")) && finish_wait.elapsed() < 2000) QThread::msleep(1);
  if (!QFileInfo::exists(root.filePath("finished-stale.mp4"))) return fail("Stale fixture did not finish");
  const auto watchers = model.findChildren<QFutureWatcherBase*>();
  const auto workers_finished = [&]() {
    return std::all_of(watchers.cbegin(), watchers.cend(), [](const QFutureWatcherBase* watcher) {
      return !watcher->isRunning();
    });
  };
  while (!workers_finished() && finish_wait.elapsed() < 3000) QThread::msleep(1);
  if (!workers_finished()) return fail("Old worker did not finish before generation replacement");
  stale.duration_seconds = 999;
  stale.duration_text = "16:39";
  model.replaceAssetsForTesting({stale});
  pump_events(200);
  if (model.assetById("same-path").value("durationSeconds").toDouble() != 999)
    return fail("Queued old-generation completion overwrote the newer duration");
  const auto duration = [&](const QString& id) {
    return model.assetById(id).value("durationSeconds").toDouble();
  };
  const auto cache_exists = [&](const CatalogAssetItem& item) {
    const auto key = QCryptographicHash::hash(QFileInfo(item.file_path).absoluteFilePath().toUtf8(),
                                             QCryptographicHash::Sha256).toHex();
    return QFileInfo::exists(root.filePath("cache/media-duration-v1/" + QString::fromLatin1(key) + ".json"));
  };
  auto invalid = make_item("invalid", "invalid.mp4", "not-a-number");
  if (!release("invalid.mp4")) return fail("Cannot release invalid probe");
  model.replaceAssetsForTesting({invalid});
  if (!wait_until([&]() { return launch_count() == 13 && workers_finished(); })) return fail("Invalid probe did not finish");
  pump_events(20);
  if (duration("invalid") != 0 || cache_exists(invalid)) return fail("Invalid output was published or cached");

  auto changing = make_item("changing", "changing.mp4", "5");
  model.replaceAssetsForTesting({changing});
  if (!wait_until([&]() { return launch_count() == 14; }) || !write_file(changing.file_path, "1234") ||
      !release("changing.mp4") || !wait_until(workers_finished)) return fail("Cannot finish changed-source probe");
  pump_events(20);
  if (duration("changing") != 0 || cache_exists(changing)) return fail("A source changed during probing was accepted");
  model.replaceAssetsForTesting({changing});
  if (!wait_until([&]() { return duration("changing") == 1234; }) || launch_count() != 15)
    return fail("Changed-source retry did not recover");
  changing.online = false;
  model.replaceAssetsForTesting({changing});
  pump_events(50);
  if (launch_count() != 15 || duration("changing") != 0) return fail("Offline asset was probed");
  changing.online = true;
  model.replaceAssetsForTesting({changing});
  if (!wait_until([&]() { return duration("changing") == 1234; }) || launch_count() != 15)
    return fail("Returning online did not reuse the valid cache");

  auto timeout = make_item("timeout", "timeout.mp4", "44");
  QElapsedTimer timeout_clock;
  timeout_clock.start();
  const auto timeout_heartbeats = heartbeats;
  model.replaceAssetsForTesting({timeout});
  if (!wait_until([&]() { return launch_count() == 16 && workers_finished(); }, 8000))
    return fail("Probe timeout did not release its worker");
  pump_events(20);
  if (timeout_clock.elapsed() < 4000 || heartbeats - timeout_heartbeats < 100 ||
      duration("timeout") != 0 || cache_exists(timeout)) return fail("Timeout blocked the event loop or cached a result");
  if (!release("timeout.mp4")) return fail("Cannot release timeout retry");
  model.replaceAssetsForTesting({timeout});
  if (!wait_until([&]() { return duration("timeout") == 44; }) || launch_count() != 17)
    return fail("A timed-out probe could not be retried");

  if (real_probe_available) {
    QByteArray wave;
    const auto append32 = [&](quint32 value) {
      const auto little = qToLittleEndian(value);
      wave.append(reinterpret_cast<const char*>(&little), 4);
    };
    const auto append16 = [&](quint16 value) {
      const auto little = qToLittleEndian(value);
      wave.append(reinterpret_cast<const char*>(&little), 2);
    };
    wave.append("RIFF", 4); append32(8036); wave.append("WAVEfmt ", 8);
    append32(16); append16(1); append16(1); append32(8000); append32(8000);
    append16(1); append16(8); wave.append("data", 4); append32(8000);
    wave.append(QByteArray(8000, static_cast<char>(0x80)));
    auto audio = make_item("real-audio", "one-second.wav", wave);
    audio.media_type = QStringLiteral("audio");
    qputenv("PATH", original_path);
    model.replaceAssetsForTesting({audio});
    if (!wait_until([&]() { return std::abs(duration("real-audio") - 1.0) < 0.001; }) || !cache_exists(audio))
      return fail("Real ffprobe did not publish/cache the generated one-second WAV");
    qputenv("PATH", binaries.toUtf8());
    model.replaceAssetsForTesting({audio});
    if (!wait_until([&]() { return std::abs(duration("real-audio") - 1.0) < 0.001; }) || launch_count() != 17)
      return fail("Warm real-media refresh depended on a probe executable");
    std::cout << "Real ffprobe: generated one-second WAV and executable-independent warm cache passed.\n";
  } else {
    std::cout << "Real ffprobe unavailable; deterministic subprocess coverage only.\n";
  }

  auto closing_model = std::make_unique<CatalogAssetModel>();
  closing_model->setInteractiveRenderingActive(true);
  const auto shutdown_start_count = launch_count();
  closing_model->replaceAssetsForTesting({make_item("close-0", "close-0.mp4", "11"),
      make_item("close-1", "close-1.mp4", "12")});
  if (!wait_until([&]() { return launch_count() == shutdown_start_count + 2; }))
    return fail("Shutdown fixture did not start two held probes");
  QVector<qint64> shutdown_pids;
  for (int n = 1; n <= 2; ++n)
    shutdown_pids.append(read_file(root.filePath(QStringLiteral("pid-%1").arg(shutdown_start_count + n))).toLongLong());
  QElapsedTimer shutdown; shutdown.start();
  closing_model.reset();
  const auto destruction_ms = shutdown.elapsed();
  if (destruction_ms > 100) return fail("Catalog destruction blocked on held duration probes");
  if (!wait_until([&]() {
        for (auto pid : shutdown_pids) {
          if (pid <= 0 || ::kill(static_cast<pid_t>(pid), 0) == 0 || errno != ESRCH) return false;
        }
        return true;
      }, 2000)) return fail("Held duration helper survived controller shutdown");
  std::cout << "Shutdown: model destruction " << destruction_ms << " ms; both owned helpers exited in "
            << shutdown.elapsed() << " ms.\n";
  std::cout << "Rows published in " << publication_ms << " ms; two-worker bound, event-loop heartbeat, selection/viewport priority, grouped probes, zero-probe warm refresh, revision invalidation, cancellation, stale completion, invalid output, changing sources, offline reuse, timeout recovery, and active shutdown checks passed.\n";
  return 0;
}
