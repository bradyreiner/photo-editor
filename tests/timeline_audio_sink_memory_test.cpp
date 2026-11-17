#include <QApplication>
#include <QAudioDevice>
#include <QAudioFormat>
#include <QAudioSink>
#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEvent>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMediaDevices>
#include <QPointer>
#include <QThread>

#include "timeline_memory_autorelease_pool.h"

#include <mach/mach.h>
#include <mach/task_info.h>
#include <malloc/malloc.h>

#include <cstdlib>
#include <iostream>
#include <vector>

namespace {

struct Options final {
  int cycles = 12;
  QString artifact_path;
  bool reuse_sink = false;
  bool cycle_autorelease_pool = false;
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
    } else if (argument == QStringLiteral("--cycles") && index + 1 < argc) {
      bool ok = false;
      const auto cycles = QString::fromLocal8Bit(argv[++index]).toInt(&ok);
      if (!ok || cycles < 1 || cycles > 1000) {
        options.valid = false;
      } else {
        options.cycles = cycles;
      }
    } else if (argument == QStringLiteral("--artifact") && index + 1 < argc) {
      options.artifact_path = QString::fromLocal8Bit(argv[++index]);
    } else if (argument == QStringLiteral("--reuse-sink")) {
      options.reuse_sink = true;
    } else if (argument == QStringLiteral("--cycle-autorelease-pool")) {
      options.cycle_autorelease_pool = true;
    } else {
      options.valid = false;
    }
  }
  return options;
}

MemorySnapshot memory_snapshot() {
  MemorySnapshot snapshot;
  mach_task_basic_info_data_t resident_info{};
  mach_msg_type_number_t resident_count = MACH_TASK_BASIC_INFO_COUNT;
  if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO,
                reinterpret_cast<task_info_t>(&resident_info),
                &resident_count) != KERN_SUCCESS) {
    return snapshot;
  }

  malloc_statistics_t allocation{};
  malloc_zone_statistics(nullptr, &allocation);
  snapshot.json = QJsonObject{
      {QStringLiteral("rssBytes"), static_cast<qint64>(resident_info.resident_size)},
      {QStringLiteral("mallocLiveBytes"), static_cast<qint64>(allocation.size_in_use)},
      {QStringLiteral("mallocReservedBytes"), static_cast<qint64>(allocation.size_allocated)},
      {QStringLiteral("mallocLiveBlocks"), static_cast<qint64>(allocation.blocks_in_use)},
  };

  task_vm_info_data_t vm_info{};
  mach_msg_type_number_t vm_count = TASK_VM_INFO_COUNT;
  if (task_info(mach_task_self(), TASK_VM_INFO,
                reinterpret_cast<task_info_t>(&vm_info), &vm_count) == KERN_SUCCESS) {
    snapshot.json.insert(QStringLiteral("physicalFootprintBytes"),
                         static_cast<qint64>(vm_info.phys_footprint));
  }

  thread_act_array_t threads = nullptr;
  mach_msg_type_number_t thread_count = 0;
  if (task_threads(mach_task_self(), &threads, &thread_count) == KERN_SUCCESS) {
    snapshot.json.insert(QStringLiteral("threadCount"), static_cast<qint64>(thread_count));
    for (mach_msg_type_number_t index = 0; index < thread_count; ++index) {
      static_cast<void>(mach_port_deallocate(mach_task_self(), threads[index]));
    }
    static_cast<void>(vm_deallocate(
        mach_task_self(), reinterpret_cast<vm_address_t>(threads),
        static_cast<vm_size_t>(thread_count) * sizeof(thread_t)));
  }
  snapshot.available = true;
  return snapshot;
}

void pump_once(int wait_ms = 10) {
  QCoreApplication::processEvents(QEventLoop::AllEvents, qMax(1, wait_ms));
  QThread::msleep(1);
  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

void settle_after_teardown(int duration_ms) {
  QElapsedTimer elapsed;
  elapsed.start();
  while (elapsed.elapsed() < duration_ms) {
    pump_once(qMin(10, duration_ms - static_cast<int>(elapsed.elapsed())));
  }
  for (int pass = 0; pass < 3; ++pass) {
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
  }
  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

bool append_row(QFile* artifact, const QJsonObject& row) {
  const auto bytes = QJsonDocument(row).toJson(QJsonDocument::Compact) + '\n';
  if (artifact != nullptr) {
    if (artifact->write(bytes) != bytes.size() || !artifact->flush()) return false;
  }
  std::cout << bytes.constData();
  return true;
}

int fail(const QString& message) {
  std::cerr << "timeline_audio_sink_memory_test: " << message.toStdString() << '\n';
  return EXIT_FAILURE;
}

}  // namespace

int main(int argc, char** argv) {
  const auto options = parse_options(argc, argv);
  if (options.help) {
    std::cout << "Usage: timeline_audio_sink_memory_test [--cycles N] [--artifact PATH] "
                 "[--reuse-sink] [--cycle-autorelease-pool]\n";
    return EXIT_SUCCESS;
  }
  if (!options.valid) return fail(QStringLiteral("invalid arguments"));

  QApplication app(argc, argv);
  const auto output_device = QMediaDevices::defaultAudioOutput();
  if (output_device.isNull()) {
    std::cerr << "SKIP: no default audio output device\n";
    return 77;
  }

  QFile artifact;
  if (!options.artifact_path.isEmpty()) {
    const QFileInfo info(options.artifact_path);
    if (!QDir().mkpath(info.absolutePath())) return fail(QStringLiteral("artifact directory creation failed"));
    artifact.setFileName(options.artifact_path);
    if (!artifact.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
      return fail(QStringLiteral("could not open artifact file"));
    }
  }

  QAudioFormat format;
  format.setSampleRate(48000);
  format.setChannelCount(2);
  format.setSampleFormat(QAudioFormat::Float);
  const auto silence = QByteArray(480 * format.bytesPerFrame(), '\0');
  const auto mode = options.reuse_sink ? QStringLiteral("reuse") : QStringLiteral("recreate");
  auto total_creations = 0;
  auto total_source_starts = 0;
  auto total_written_frames = qint64{0};
  auto total_errors = 0;
  auto total_lifetime_failures = 0;

  for (int cycle = 0; cycle < options.cycles; ++cycle) {
    std::vector<QPointer<QAudioSink>> lifetimes;
    auto cycle_creations = 0;
    auto cycle_written_frames = qint64{0};
    auto cycle_errors = 0;
    auto cycle_starts = 0;
    auto start_failed = false;
    auto lifetimes_ok = true;
    {
      // Declared first so its autorelease pool drains only after all cycle sinks
      // and their native queues have been torn down.
      TimelineMemoryAutoreleasePool autorelease_pool(options.cycle_autorelease_pool);
      QPointer<QAudioSink> sink;
      QPointer<QIODevice> sink_device;

      for (int segment = 0; segment < 5; ++segment) {
        if (!options.reuse_sink && sink != nullptr) {
          auto* old_sink = sink.data();
          sink = nullptr;
          sink_device = nullptr;
          old_sink->stop();
          old_sink->deleteLater();
          pump_once(1);
        }

        if (sink == nullptr) {
          auto* created = new QAudioSink(output_device, format, &app);
          lifetimes.emplace_back(created);
          sink = created;
          ++cycle_creations;
          sink->setBufferFrameCount(qMax(256, format.framesForDuration(60'000)));
        }

        ++cycle_starts;
        sink_device = sink->start();
        if (sink_device == nullptr) {
          ++cycle_errors;
          start_failed = true;
          break;
        }
        const auto written_bytes = sink_device->write(silence.constData(), silence.size());
        if (written_bytes > 0) {
          cycle_written_frames += written_bytes / format.bytesPerFrame();
        }
        pump_once(12);
        if (sink->error() != QAudio::NoError) ++cycle_errors;
        sink->reset();
        sink_device = nullptr;
      }

      if (sink != nullptr) {
        auto* final_sink = sink.data();
        sink = nullptr;
        sink_device = nullptr;
        final_sink->stop();
        final_sink->deleteLater();
      }
      settle_after_teardown(0);
      for (const auto& lifetime : lifetimes) {
        if (!lifetime.isNull()) lifetimes_ok = false;
      }
    }

    settle_after_teardown(250);
    const auto memory = memory_snapshot();
    if (!memory.available) return fail(QStringLiteral("Mach memory snapshot unavailable"));
    total_creations += cycle_creations;
    total_source_starts += cycle_starts;
    total_written_frames += cycle_written_frames;
    total_errors += cycle_errors;
    if (!lifetimes_ok) ++total_lifetime_failures;

    auto row = memory.json;
    row.insert(QStringLiteral("cycle"), cycle);
    row.insert(QStringLiteral("sinkMode"), mode);
    row.insert(QStringLiteral("segmentStarts"), cycle_starts);
    row.insert(QStringLiteral("sourceStartsTotal"), total_source_starts);
    row.insert(QStringLiteral("sinkCreationsThisCycle"), cycle_creations);
    row.insert(QStringLiteral("sinkCreationsTotal"), total_creations);
    row.insert(QStringLiteral("writtenFramesThisCycle"), cycle_written_frames);
    row.insert(QStringLiteral("writtenFramesTotal"), total_written_frames);
    row.insert(QStringLiteral("sinkErrorsThisCycle"), cycle_errors);
    row.insert(QStringLiteral("sinkErrorsTotal"), total_errors);
    row.insert(QStringLiteral("sinkLifetimesDestroyed"), lifetimes_ok);
    row.insert(QStringLiteral("cycleAutoreleasePool"), options.cycle_autorelease_pool);
    if (!append_row(options.artifact_path.isEmpty() ? nullptr : &artifact, row)) {
      return fail(QStringLiteral("could not append memory sample"));
    }
    if (start_failed) {
      std::cerr << "SKIP: QAudioSink could not open the default output device\n";
      return 77;
    }
  }

  if (total_errors != 0) return fail(QStringLiteral("audio sink reported errors"));
  if (total_lifetime_failures != 0) return fail(QStringLiteral("a sink survived cycle teardown"));
  if (total_written_frames == 0) return fail(QStringLiteral("no silence frames were accepted"));
  return EXIT_SUCCESS;
}
