#include "timeline_pcm_audio_decoder.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QProcess>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QThread>

#include <cmath>
#include <iostream>

using photo_editor::desktop::TimelinePcmAudioDecoder;

namespace {
bool wait_until(const std::function<bool()>& ready, int timeout_ms = 5000) {
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < timeout_ms) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    if (ready()) return true;
    QThread::msleep(5);
  }
  return false;
}
}

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  const auto ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
  if (ffmpeg.isEmpty()) return 77;
  QTemporaryDir workspace;
  if (!workspace.isValid()) return 1;
  const auto create_source = [&](const QString& name, const QString& expression) {
    const auto path = QDir(workspace.path()).filePath(name);
    QProcess process;
    process.start(ffmpeg, {QStringLiteral("-v"), QStringLiteral("error"),
        QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"), expression,
        QStringLiteral("-t"), QStringLiteral("3"), QStringLiteral("-ac"), QStringLiteral("2"),
        QStringLiteral("-y"), path});
    if (!process.waitForFinished(10000) || process.exitCode() != 0) return QString{};
    return QUrl::fromLocalFile(path).toString();
  };
  const auto source = create_source(QStringLiteral("seek-tone.wav"),
      QStringLiteral("aevalsrc=if(lt(t\\,1)\\,0\\,0.5*sin(2*PI*880*t)):s=48000"));
  const auto silence = create_source(QStringLiteral("silence.wav"),
      QStringLiteral("anullsrc=r=48000:cl=stereo"));
  if (source.isEmpty() || silence.isEmpty()) return 1;
#ifdef Q_OS_MACOS
  const auto original_path = qgetenv("PATH");
  const auto restore_path = qScopeGuard([&]() { qputenv("PATH", original_path); });
  qputenv("PATH", QByteArrayLiteral("/usr/bin:/bin"));
#endif
  int received = 0;
  qint64 first_timestamp = -1;
  auto peak_rms = 0.0;
  auto valid_layout = true;
  TimelinePcmAudioDecoder decoder([&](const QAudioBuffer& buffer) {
    ++received;
    valid_layout = valid_layout && buffer.format().sampleFormat() == QAudioFormat::Float &&
        buffer.format().channelCount() == 2 && buffer.format().sampleRate() == 48000;
    if (first_timestamp < 0) first_timestamp = buffer.startTime();
    const auto* samples = buffer.constData<float>();
    auto energy = 0.0;
    for (int index = 0; index < buffer.sampleCount(); ++index) {
      energy += static_cast<double>(samples[index]) * samples[index];
    }
    peak_rms = qMax(peak_rms, std::sqrt(energy / qMax(1, buffer.sampleCount())));
  });
  // The first second is silent. Real nonzero decoded samples prove seeking
  // into the tone, rather than decoding from the beginning and relabeling it.
  decoder.synchronize(source, 1250, 2.0, 2, true);
  if (!wait_until([&]() { return received >= 3 && peak_rms > 0.1; }) ||
      !valid_layout || first_timestamp != 1250000 || !decoder.error().isEmpty()) {
    std::cerr << "seek/rate PCM decode failed: " << decoder.error().toStdString() << '\n';
    return 1;
  }
  decoder.stop();
  // Deliberately stall GUI delivery. Bound stale PCM without killing the lane
  // or losing the ability to resume audible output after the event loop returns.
  decoder.synchronize(source, 1000, 1.0, 2, true);
  const auto before_stall = received;
  if (!wait_until([&]() { return received >= before_stall + 3; })) return 1;
  const auto before_resume = received;
  QThread::msleep(650);
  if (!wait_until([&]() { return decoder.discardedFrameCount() > 0 && received >= before_resume + 3; }) ||
      !decoder.error().isEmpty()) {
    std::cerr << "bounded PCM backlog must recover after a GUI stall: " << decoder.error().toStdString() << '\n';
    return 1;
  }
  decoder.stop();
  const auto stopped_count = received;
  (void)wait_until([]() { return false; }, 150);
  if (received != stopped_count || decoder.active()) {
    std::cerr << "paused PCM decoder delivered stale audio\n";
    return 1;
  }
  received = 0;
  peak_rms = 0.0;
  first_timestamp = -1;
  decoder.synchronize(source, 1250, 1.0, 2, true);
  // Switch before startup callbacks: no former source may leak into silence.
  decoder.synchronize(silence, 0, 1.0, 2, true);
  if (!wait_until([&]() { return received >= 3; }) || peak_rms != 0.0 ||
      first_timestamp != 0 || !decoder.error().isEmpty()) {
    std::cerr << "source replacement leaked stale PCM or failed decoding\n";
    return 1;
  }
  decoder.synchronize(silence, 0, 1.0, 2, false);
  if (decoder.active()) return 1;
  std::cout << "Real PCM seek/rate, silence, source replacement, and stop passed\n";
  return 0;
}
