#include <QApplication>
#include <QAudioOutput>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEvent>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMediaPlayer>
#include <QPointer>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QVideoFrame>
#include <QVideoSink>

#include "timeline_memory_autorelease_pool.h"

#include <mach/mach.h>
#include <mach/task_info.h>
#include <malloc/malloc.h>

#include <atomic>
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <vector>

namespace {

enum class PumpMode {
  process_events,
  event_loop,
};

struct Options final {
  int cycles = 24;
  QString artifact_path;
  PumpMode pump_mode = PumpMode::process_events;
  bool cycle_autorelease_pool = true;
  bool valid = true;
  bool help = false;
};

struct MemorySnapshot final {
  QJsonObject json;
  bool available = false;
};

Options parse_options(int argc, char** argv) {
  Options options;
  for (int index = 1; index < argc; ++index) {
    const auto argument = QString::fromLocal8Bit(argv[index]);
    if (argument == QStringLiteral("--help") || argument == QStringLiteral("-h")) {
      options.help = true;
      continue;
    }
    if (argument == QStringLiteral("--cycles") && index + 1 < argc) {
      bool ok = false;
      const auto cycles = QString::fromLocal8Bit(argv[++index]).toInt(&ok);
      if (!ok || cycles < 1 || cycles > 1000) {
        options.valid = false;
      } else {
        options.cycles = cycles;
      }
      continue;
    }
    if (argument == QStringLiteral("--artifact") && index + 1 < argc) {
      options.artifact_path = QString::fromLocal8Bit(argv[++index]);
      continue;
    }
    if (argument == QStringLiteral("--pump") && index + 1 < argc) {
      const auto mode = QString::fromLocal8Bit(argv[++index]);
      if (mode == QStringLiteral("process-events")) {
        options.pump_mode = PumpMode::process_events;
      } else if (mode == QStringLiteral("event-loop")) {
        options.pump_mode = PumpMode::event_loop;
      } else {
        options.valid = false;
      }
      continue;
    }
    if (argument == QStringLiteral("--cycle-autorelease-pool")) {
      options.cycle_autorelease_pool = true;
      continue;
    }
    if (argument == QStringLiteral("--without-cycle-autorelease-pool")) {
      options.cycle_autorelease_pool = false;
      continue;
    }
    options.valid = false;
  }
  return options;
}

QString pump_mode_name(PumpMode mode) {
  return mode == PumpMode::event_loop ? QStringLiteral("event-loop")
                                      : QStringLiteral("process-events");
}

void pump_once(PumpMode mode, int wait_ms = 10) {
  if (mode == PumpMode::event_loop) {
    QEventLoop loop;
    QTimer::singleShot(qMax(1, wait_ms), &loop, &QEventLoop::quit);
    loop.exec(QEventLoop::AllEvents);
  } else {
    QCoreApplication::processEvents(QEventLoop::AllEvents, qMax(1, wait_ms));
    QThread::msleep(1);
  }
  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

constexpr int kPostWarmFirstCycle = 6;
constexpr std::size_t kMinimumPostWarmMemorySamples = 12U;
constexpr qint64 kPostWarmLiveHeapSpanLimitBytes = 8LL * 1024LL * 1024LL;
constexpr qint64 kPostWarmThreadCountSpanLimit = 12LL;

template <typename Predicate>
bool wait_until(Predicate predicate, int timeout_ms, PumpMode mode) {
  QElapsedTimer elapsed;
  elapsed.start();
  while (elapsed.elapsed() < timeout_ms) {
    if (predicate()) {
      return true;
    }
    pump_once(mode);
  }
  return predicate();
}

void settle_after_teardown(int duration_ms, PumpMode mode) {
  QElapsedTimer elapsed;
  elapsed.start();
  while (elapsed.elapsed() < duration_ms) {
    pump_once(mode, qMin(10, duration_ms - static_cast<int>(elapsed.elapsed())));
  }
  for (int pass = 0; pass < 3; ++pass) {
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
  }
  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

MemorySnapshot memory_snapshot() {
  MemorySnapshot snapshot;
  mach_task_basic_info_data_t resident_info{};
  mach_msg_type_number_t resident_count = MACH_TASK_BASIC_INFO_COUNT;
  const auto resident_result = task_info(
      mach_task_self(),
      MACH_TASK_BASIC_INFO,
      reinterpret_cast<task_info_t>(&resident_info),
      &resident_count);
  if (resident_result != KERN_SUCCESS) {
    return snapshot;
  }

  malloc_statistics_t allocation{};
  // nullptr asks libmalloc to sum statistics for all zones.
  malloc_zone_statistics(nullptr, &allocation);
  snapshot.json = QJsonObject{
      {QStringLiteral("rssBytes"), static_cast<qint64>(resident_info.resident_size)},
      {QStringLiteral("mallocLiveBytes"), static_cast<qint64>(allocation.size_in_use)},
      {QStringLiteral("mallocReservedBytes"), static_cast<qint64>(allocation.size_allocated)},
      {QStringLiteral("mallocLiveBlocks"), static_cast<qint64>(allocation.blocks_in_use)},
  };

  task_vm_info_data_t vm_info{};
  mach_msg_type_number_t vm_count = TASK_VM_INFO_COUNT;
  if (task_info(
          mach_task_self(), TASK_VM_INFO,
          reinterpret_cast<task_info_t>(&vm_info), &vm_count) == KERN_SUCCESS) {
    snapshot.json.insert(QStringLiteral("physicalFootprintBytes"),
                         static_cast<qint64>(vm_info.phys_footprint));
    snapshot.json.insert(QStringLiteral("vmInternalBytes"),
                         static_cast<qint64>(vm_info.internal));
    snapshot.json.insert(QStringLiteral("vmExternalBytes"),
                         static_cast<qint64>(vm_info.external));
    snapshot.json.insert(QStringLiteral("vmReusableBytes"),
                         static_cast<qint64>(vm_info.reusable));
    snapshot.json.insert(QStringLiteral("vmCompressedBytes"),
                         static_cast<qint64>(vm_info.compressed));
    snapshot.json.insert(QStringLiteral("vmMediaFootprintBytes"),
                         static_cast<qint64>(vm_info.ledger_tag_media_footprint));
    snapshot.json.insert(QStringLiteral("vmGraphicsFootprintBytes"),
                         static_cast<qint64>(vm_info.ledger_tag_graphics_footprint));
  }

  thread_act_array_t threads = nullptr;
  mach_msg_type_number_t thread_count = 0;
  auto threads_available = false;
  if (task_threads(mach_task_self(), &threads, &thread_count) == KERN_SUCCESS) {
    snapshot.json.insert(QStringLiteral("threadCount"),
                         static_cast<qint64>(thread_count));
    for (mach_msg_type_number_t index = 0; index < thread_count; ++index) {
      static_cast<void>(mach_port_deallocate(mach_task_self(), threads[index]));
    }
    static_cast<void>(vm_deallocate(
        mach_task_self(), reinterpret_cast<vm_address_t>(threads),
        static_cast<vm_size_t>(thread_count) * sizeof(thread_t)));
    threads_available = true;
  }
  snapshot.available = threads_available;
  return snapshot;
}

bool create_source(const QString& path, QString* error) {
  const auto ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
  if (ffmpeg.isEmpty()) {
    if (error != nullptr) {
      *error = QStringLiteral("ffmpeg is not installed");
    }
    return false;
  }
  QProcess process;
  process.setProgram(ffmpeg);
  process.setArguments({
      QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"),
      QStringLiteral("error"), QStringLiteral("-y"),
      QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
      QStringLiteral("color=c=red:s=640x360:r=30:d=3"),
      QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
      QStringLiteral("sine=frequency=440:sample_rate=48000:duration=3"),
      QStringLiteral("-map"), QStringLiteral("0:v:0"),
      QStringLiteral("-map"), QStringLiteral("1:a:0"),
      QStringLiteral("-shortest"), QStringLiteral("-c:v"),
      QStringLiteral("libx264"), QStringLiteral("-preset"),
      QStringLiteral("ultrafast"), QStringLiteral("-crf"),
      QStringLiteral("22"), QStringLiteral("-pix_fmt"),
      QStringLiteral("yuv420p"), QStringLiteral("-c:a"),
      QStringLiteral("aac"), QStringLiteral("-ar"),
      QStringLiteral("48000"), QStringLiteral("-ac"),
      QStringLiteral("2"), QStringLiteral("-movflags"),
      QStringLiteral("+faststart"), path,
  });
  process.start();
  if (!process.waitForStarted(5000) || !process.waitForFinished(60000)) {
    process.kill();
    static_cast<void>(process.waitForFinished(3000));
    if (error != nullptr) {
      *error = QStringLiteral("ffmpeg did not finish: %1")
                   .arg(QString::fromUtf8(process.readAllStandardError()));
    }
    return false;
  }
  if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0 ||
      !QFileInfo::exists(path) || QFileInfo(path).size() < 4096) {
    if (error != nullptr) {
      *error = QStringLiteral("ffmpeg failed (%1): %2")
                   .arg(process.exitCode())
                   .arg(QString::fromUtf8(process.readAllStandardError()));
    }
    return false;
  }
  return true;
}

bool media_loaded(const QMediaPlayer& player) {
  return player.mediaStatus() == QMediaPlayer::LoadedMedia ||
         player.mediaStatus() == QMediaPlayer::BufferedMedia;
}

QJsonObject make_row(
    int cycle,
    const QString& stage,
    PumpMode mode,
    const MemorySnapshot& memory,
    bool loaded = false,
    bool first_frame = false,
    int frame_count = 0,
    int media_status = static_cast<int>(QMediaPlayer::NoMedia),
    int media_error = static_cast<int>(QMediaPlayer::NoError),
    const QString& error_string = QString{}) {
  return QJsonObject{
      {QStringLiteral("schema"), QStringLiteral("timeline-native-player-memory:v1")},
      {QStringLiteral("cycle"), cycle},
      {QStringLiteral("stage"), stage},
      {QStringLiteral("pumpMode"), pump_mode_name(mode)},
      {QStringLiteral("loaded"), loaded},
      {QStringLiteral("firstVideoFrameReceived"), first_frame},
      {QStringLiteral("videoFrameCount"), frame_count},
      {QStringLiteral("mediaStatus"), media_status},
      {QStringLiteral("mediaError"), media_error},
      {QStringLiteral("mediaErrorString"), error_string},
      {QStringLiteral("memory"), memory.json},
      {QStringLiteral("memoryAvailable"), memory.available},
  };
}

void write_row(QFile* artifact, const QJsonObject& row) {
  const auto json = QJsonDocument(row).toJson(QJsonDocument::Compact);
  std::cout << json.constData() << '\n';
  if (artifact != nullptr && artifact->isOpen()) {
    artifact->write(json);
    artifact->write("\n");
    artifact->flush();
  }
}

int fail(const QString& message) {
  std::cerr << "timeline_native_player_memory: " << message.toStdString() << '\n';
  return EXIT_FAILURE;
}

}  // namespace

int main(int argc, char** argv) {
  const auto options = parse_options(argc, argv);
  if (options.help) {
    std::cout << "Usage: timeline_native_player_memory_test [--cycles N] "
                 "[--pump process-events|event-loop] [--cycle-autorelease-pool] "
                 "[--without-cycle-autorelease-pool] "
                 "[--artifact PATH]\n";
    return EXIT_SUCCESS;
  }
  if (!options.valid) {
    return fail(QStringLiteral("invalid arguments; use --help for usage"));
  }

  qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("cocoa"));
  qunsetenv("QT_MEDIA_BACKEND");
  int qt_argc = 1;
  char* qt_argv[]{argv[0], nullptr};
  QApplication app(qt_argc, qt_argv);
  app.setQuitOnLastWindowClosed(false);

  QFile artifact;
  if (!options.artifact_path.isEmpty()) {
    if (!QDir().mkpath(QFileInfo(options.artifact_path).absolutePath())) {
      return fail(QStringLiteral("could not create artifact directory"));
    }
    artifact.setFileName(options.artifact_path);
    if (!artifact.open(QIODevice::WriteOnly | QIODevice::Append)) {
      return fail(QStringLiteral("could not open artifact path"));
    }
  }

  QTemporaryDir workspace;
  if (!workspace.isValid()) {
    return fail(QStringLiteral("temporary directory creation failed"));
  }
  const auto media_path = QDir(workspace.path()).filePath(QStringLiteral("native-memory-fixture.mp4"));
  QString fixture_error;
  if (!create_source(media_path, &fixture_error)) {
    if (QStandardPaths::findExecutable(QStringLiteral("ffmpeg")).isEmpty()) {
      std::cerr << "SKIP: " << fixture_error.toStdString() << '\n';
      return 77;
    }
    return fail(QStringLiteral("could not create the H.264/AAC fixture: %1")
                    .arg(fixture_error));
  }

  const auto initial_memory = memory_snapshot();
  if (!initial_memory.available) {
    return fail(QStringLiteral("Mach RSS sampling is unavailable"));
  }
  auto fixture_ready = make_row(-1, QStringLiteral("fixture-ready"), options.pump_mode,
                                initial_memory);
  fixture_ready.insert(QStringLiteral("cycleAutoreleasePool"),
                       options.cycle_autorelease_pool);
  write_row(&artifact, fixture_ready);

  auto load_successes = 0;
  auto frame_successes = 0;
  auto teardown_failures = 0;
  auto memory_sample_failures = 0;
  std::vector<qint64> post_warm_live_heap_bytes;
  std::vector<qint64> post_warm_thread_counts;
  for (int cycle = 0; cycle < options.cycles; ++cycle) {
    std::atomic<int> received_frames{0};
    QPointer<QMediaPlayer> player_lifetime;
    QPointer<QVideoSink> sink_lifetime;
    QPointer<QAudioOutput> audio_lifetime;
    auto loaded = false;
    auto first_frame = false;
    auto status_at_teardown = static_cast<int>(QMediaPlayer::NoMedia);
    auto error_at_teardown = static_cast<int>(QMediaPlayer::NoError);
    QString error_string_at_teardown;
    {
      // This pool wraps direct media calls made outside processEvents' native
      // event boundary. Without this scope, autoreleases from direct calls can
      // remain in the outer pool beyond subsequent nested event-pump pools.
      TimelineMemoryAutoreleasePool autorelease_pool(options.cycle_autorelease_pool);
      auto audio = std::make_unique<QAudioOutput>();
      audio->setVolume(0.0F);
      audio->setMuted(true);
      auto sink = std::make_unique<QVideoSink>();
      QObject::connect(sink.get(), &QVideoSink::videoFrameChanged, sink.get(),
                       [&received_frames](const QVideoFrame& frame) {
                         if (frame.isValid()) {
                           received_frames.fetch_add(1, std::memory_order_relaxed);
                         }
                       });
      auto player = std::make_unique<QMediaPlayer>();
      player_lifetime = player.get();
      sink_lifetime = sink.get();
      audio_lifetime = audio.get();
      player->setAudioOutput(audio.get());
      player->setVideoSink(sink.get());
      player->setSource(QUrl::fromLocalFile(media_path));

      const auto load_finished = wait_until(
          [&] {
            return media_loaded(*player) || player->mediaStatus() == QMediaPlayer::InvalidMedia ||
                   player->error() != QMediaPlayer::NoError;
          },
          5000, options.pump_mode);
      loaded = load_finished && media_loaded(*player);
      if (loaded) {
        player->play();
        static_cast<void>(wait_until(
            [&] { return received_frames.load(std::memory_order_relaxed) > 0; },
            5000, options.pump_mode));
        first_frame = received_frames.load(std::memory_order_relaxed) > 0;
      }

      status_at_teardown = static_cast<int>(player->mediaStatus());
      error_at_teardown = static_cast<int>(player->error());
      error_string_at_teardown = player->errorString();
      player->stop();
      player->setSource(QUrl{});
      player->setVideoSink(nullptr);
      player->setAudioOutput(nullptr);
      player.reset();
      sink.reset();
      audio.reset();
    }

    settle_after_teardown(250, options.pump_mode);
    const auto player_destroyed = player_lifetime.isNull();
    const auto sink_destroyed = sink_lifetime.isNull();
    const auto audio_destroyed = audio_lifetime.isNull();
    const auto memory = memory_snapshot();
    const auto frame_count = received_frames.load(std::memory_order_relaxed);
    const auto teardown_ok = player_destroyed && sink_destroyed && audio_destroyed;
    if (loaded) ++load_successes;
    if (first_frame) ++frame_successes;
    if (!teardown_ok) ++teardown_failures;
    if (!memory.available) {
      ++memory_sample_failures;
    } else if (cycle >= kPostWarmFirstCycle) {
      const auto live_heap_bytes =
          memory.json.value(QStringLiteral("mallocLiveBytes")).toInteger(-1);
      const auto thread_count =
          memory.json.value(QStringLiteral("threadCount")).toInteger(-1);
      if (live_heap_bytes < 0 || thread_count < 0) {
        ++memory_sample_failures;
      } else {
        post_warm_live_heap_bytes.push_back(live_heap_bytes);
        post_warm_thread_counts.push_back(thread_count);
      }
    }

    auto row = make_row(cycle, QStringLiteral("post-teardown"), options.pump_mode,
                        memory, loaded, first_frame, frame_count,
                        status_at_teardown, error_at_teardown,
                        error_string_at_teardown);
    row.insert(QStringLiteral("playerDestroyed"), player_destroyed);
    row.insert(QStringLiteral("videoSinkDestroyed"), sink_destroyed);
    row.insert(QStringLiteral("audioOutputDestroyed"), audio_destroyed);
    row.insert(QStringLiteral("cycleAutoreleasePool"), options.cycle_autorelease_pool);
    write_row(&artifact, row);
  }

  QJsonObject summary{
      {QStringLiteral("schema"), QStringLiteral("timeline-native-player-memory-summary:v1")},
      {QStringLiteral("cycles"), options.cycles},
      {QStringLiteral("pumpMode"), pump_mode_name(options.pump_mode)},
      {QStringLiteral("cycleAutoreleasePool"), options.cycle_autorelease_pool},
      {QStringLiteral("loadSuccesses"), load_successes},
      {QStringLiteral("firstFrameSuccesses"), frame_successes},
      {QStringLiteral("loadFailures"), options.cycles - load_successes},
      {QStringLiteral("firstFrameFailures"), options.cycles - frame_successes},
      {QStringLiteral("teardownFailures"), teardown_failures},
      {QStringLiteral("memorySampleFailures"), memory_sample_failures},
  };
  const auto post_warm_memory_guard_enforced =
      post_warm_live_heap_bytes.size() >= kMinimumPostWarmMemorySamples &&
      post_warm_thread_counts.size() >= kMinimumPostWarmMemorySamples &&
      memory_sample_failures == 0;
  qint64 post_warm_live_heap_span_bytes = -1;
  qint64 post_warm_thread_count_span = -1;
  if (!post_warm_live_heap_bytes.empty()) {
    const auto [minimum, maximum] = std::minmax_element(
        post_warm_live_heap_bytes.begin(), post_warm_live_heap_bytes.end());
    post_warm_live_heap_span_bytes = *maximum - *minimum;
  }
  if (!post_warm_thread_counts.empty()) {
    const auto [minimum, maximum] = std::minmax_element(
        post_warm_thread_counts.begin(), post_warm_thread_counts.end());
    post_warm_thread_count_span = *maximum - *minimum;
  }
  const auto post_warm_live_heap_guard_passed =
      post_warm_memory_guard_enforced &&
      post_warm_live_heap_span_bytes <= kPostWarmLiveHeapSpanLimitBytes;
  const auto post_warm_thread_guard_passed =
      post_warm_memory_guard_enforced &&
      post_warm_thread_count_span <= kPostWarmThreadCountSpanLimit;
  const auto post_warm_memory_guard_passed =
      post_warm_live_heap_guard_passed && post_warm_thread_guard_passed;
  summary.insert(QStringLiteral("postWarmFirstCycle"), kPostWarmFirstCycle);
  summary.insert(QStringLiteral("postWarmMemorySampleCount"),
                 static_cast<qint64>(post_warm_live_heap_bytes.size()));
  summary.insert(QStringLiteral("postWarmMemoryGuardEnforced"),
                 post_warm_memory_guard_enforced);
  summary.insert(QStringLiteral("postWarmLiveHeapSpanBytes"),
                 post_warm_live_heap_span_bytes);
  summary.insert(QStringLiteral("postWarmLiveHeapSpanLimitBytes"),
                 kPostWarmLiveHeapSpanLimitBytes);
  summary.insert(QStringLiteral("postWarmLiveHeapGuardPassed"),
                 post_warm_live_heap_guard_passed);
  summary.insert(QStringLiteral("postWarmThreadCountSpan"),
                 post_warm_thread_count_span);
  summary.insert(QStringLiteral("postWarmThreadCountSpanLimit"),
                 kPostWarmThreadCountSpanLimit);
  summary.insert(QStringLiteral("postWarmThreadCountGuardPassed"),
                 post_warm_thread_guard_passed);
  summary.insert(QStringLiteral("postWarmMemoryGuardPassed"),
                 post_warm_memory_guard_passed);
  write_row(&artifact, summary);
  if (load_successes != options.cycles || frame_successes != options.cycles ||
      teardown_failures != 0 || memory_sample_failures != 0 ||
      (post_warm_memory_guard_enforced && !post_warm_memory_guard_passed)) {
    return fail(QStringLiteral("one or more native load, first-frame, destruction, memory-availability, or enforced post-warm memory checks failed; all cycles were collected"));
  }
  return EXIT_SUCCESS;
}
