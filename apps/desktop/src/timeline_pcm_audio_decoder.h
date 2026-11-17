#pragma once

#include <QAudioBuffer>
#include <QAudioFormat>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QProcess>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>

#include <cmath>
#include <functional>
#include <utility>

namespace photo_editor::desktop {

// Darwin's Qt media backend does not implement QAudioBufferOutput. Decode PCM
// separately there, retaining the same processing, meters, and QAudioSink path.
// FFmpeg reads at source speed; delivery is paced and capped to 250 ms of PCM.
class TimelinePcmAudioDecoder final : public QObject {
 public:
  explicit TimelinePcmAudioDecoder(std::function<void(const QAudioBuffer&)> deliver,
                                   QObject* parent = nullptr)
      : QObject(parent), deliver_(std::move(deliver)) {
    timer_.setInterval(10);
    timer_.setTimerType(Qt::PreciseTimer);
    connect(&timer_, &QTimer::timeout, this, [this]() { drain(); });
  }

  ~TimelinePcmAudioDecoder() override {
    stop();
    // Stop is asynchronous during interaction. At teardown, reap any killed
    // children before QObject destroys them without another event-loop turn.
    for (auto* process : findChildren<QProcess*>(QString(), Qt::FindDirectChildrenOnly)) {
      if (process->state() != QProcess::NotRunning) {
        process->kill();
        (void)process->waitForFinished(1000);
      }
    }
  }

  void stop() {
    timer_.stop();
    if (process_ != nullptr) {
      auto* old_process = process_;
      process_ = nullptr;
      old_process->disconnect(this);
      if (old_process->state() == QProcess::NotRunning) {
        old_process->deleteLater();
      } else {
        connect(old_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
                old_process, &QObject::deleteLater);
        old_process->kill();
      }
    }
    source_.clear();
    clock_.invalidate();
    emitted_frames_ = 0;
  }

  void synchronize(const QString& source_url, qint64 source_ms, double rate,
                   int channels, bool playing) {
    if (!playing || source_url.isEmpty() || !std::isfinite(rate) || rate <= 0.0) {
      stop();
      return;
    }
    channels = qBound(1, channels, 8);
    const auto expected_position = start_position_ms_ +
        (clock_.isValid() ? static_cast<qint64>(static_cast<double>(clock_.elapsed()) * rate_) : 0);
    if (process_ != nullptr && source_ == source_url && channels_ == channels &&
        qAbs(rate_ - rate) < 0.0001 &&
        qAbs(expected_position - source_ms) <= 200) {
      return;
    }
    stop();
    const auto source = QUrl(source_url);
    auto ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
#ifdef Q_OS_MACOS
    // Finder does not inherit the developer shell's Homebrew PATH.
    if (ffmpeg.isEmpty()) {
      const auto application_directory = QCoreApplication::applicationDirPath();
      ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"),
          {application_directory,
           application_directory + QStringLiteral("/../Resources/bin"),
           QStringLiteral("/opt/homebrew/bin"), QStringLiteral("/usr/local/bin")});
    }
#endif
    if (!source.isLocalFile() || ffmpeg.isEmpty()) {
      error_ = QStringLiteral("Timeline audio requires a readable local source and ffmpeg on this Qt backend.");
      return;
    }
    source_ = source_url;
    channels_ = channels;
    rate_ = qBound(0.01, rate, 64.0);
    start_position_ms_ = qMax<qint64>(0, source_ms);
    emitted_frames_ = 0;
    error_.clear();
    format_.setSampleRate(48000);
    format_.setChannelCount(channels_);
    format_.setChannelConfig(QAudioFormat::defaultChannelConfigForChannelCount(channels_));
    format_.setSampleFormat(QAudioFormat::Float);
    QStringList tempo_filters;
    auto remaining_rate = rate_;
    while (remaining_rate > 2.0) {
      tempo_filters << QStringLiteral("atempo=2");
      remaining_rate /= 2.0;
    }
    while (remaining_rate < 0.5) {
      tempo_filters << QStringLiteral("atempo=0.5");
      remaining_rate /= 0.5;
    }
    if (qAbs(remaining_rate - 1.0) > 0.0001) {
      tempo_filters << QStringLiteral("atempo=%1").arg(remaining_rate, 0, 'f', 8);
    }
    QStringList arguments{
        QStringLiteral("-nostdin"), QStringLiteral("-hide_banner"),
        QStringLiteral("-loglevel"), QStringLiteral("error"),
        QStringLiteral("-readrate"), QString::number(rate_, 'f', 8),
        QStringLiteral("-ss"), QString::number(static_cast<double>(start_position_ms_) / 1000.0, 'f', 6),
        QStringLiteral("-i"), source.toLocalFile(),
        QStringLiteral("-map"), QStringLiteral("0:a:0"),
        QStringLiteral("-vn"), QStringLiteral("-sn"), QStringLiteral("-dn")};
    if (!tempo_filters.isEmpty()) {
      arguments << QStringLiteral("-af") << tempo_filters.join(QLatin1Char(','));
    }
    arguments << QStringLiteral("-ac") << QString::number(channels_)
              << QStringLiteral("-ar") << QStringLiteral("48000")
              << QStringLiteral("-c:a") << QStringLiteral("pcm_f32le")
              << QStringLiteral("-f") << QStringLiteral("f32le")
              << QStringLiteral("pipe:1");
    auto* process = new QProcess(this);
    process_ = process;
    connect(process, &QProcess::readyReadStandardError, this, [this, process]() {
      if (process_ != process) return;
      error_ = (error_ + QString::fromUtf8(process->readAllStandardError())).right(4096);
    });
    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError) {
      if (process_ == process) error_ = process->errorString();
    });
    connect(process, &QProcess::readyReadStandardOutput, this, [this, process]() {
      if (process_ == process) drain();
    });
    clock_.start();
    process->start(ffmpeg, arguments);
    timer_.start();
  }

  [[nodiscard]] bool active() const {
    return process_ != nullptr && timer_.isActive() &&
        (process_->state() != QProcess::NotRunning || process_->bytesAvailable() > 0);
  }
  [[nodiscard]] quint64 deliveredBufferCount() const { return delivered_buffers_; }
  [[nodiscard]] quint64 discardedFrameCount() const { return discarded_frames_; }
  [[nodiscard]] QString error() const { return error_; }

 private:
  void drain() {
    if (process_ == nullptr || !clock_.isValid()) return;
    const auto bytes_per_frame = channels_ * static_cast<int>(sizeof(float));
    const auto maximum_bytes = static_cast<qint64>(48000 * bytes_per_frame / 4);
    if (process_->bytesAvailable() > maximum_bytes) {
      // A busy GUI must not permanently silence a running lane. Drop only
      // oldest complete PCM frames and retain the newest bounded 250 ms.
      const auto excess = process_->bytesAvailable() - maximum_bytes;
      const auto discard_frames = (excess + bytes_per_frame - 1) / bytes_per_frame;
      const auto discarded = process_->read(discard_frames * bytes_per_frame);
      const auto dropped_frames = discarded.size() / bytes_per_frame;
      emitted_frames_ += dropped_frames;
      discarded_frames_ += static_cast<quint64>(dropped_frames);
    }
    const auto due_frames = clock_.elapsed() * 48 - emitted_frames_;
    const auto available_frames = process_->bytesAvailable() / bytes_per_frame;
    if (available_frames == 0 && process_->state() == QProcess::NotRunning) {
      timer_.stop();
      return;
    }
    const auto frames = qMin<qint64>(2048, qMin(due_frames, available_frames));
    if (frames <= 0) return;
    const auto bytes = process_->read(frames * bytes_per_frame);
    emitted_frames_ += frames;
    ++delivered_buffers_;
    deliver_(QAudioBuffer(bytes, format_, start_position_ms_ * 1000 +
        static_cast<qint64>(static_cast<double>(emitted_frames_ - frames) * rate_ * 1000000.0 / 48000.0)));
  }

  std::function<void(const QAudioBuffer&)> deliver_;
  QProcess* process_ = nullptr;
  QTimer timer_;
  QElapsedTimer clock_;
  QAudioFormat format_;
  QString source_;
  QString error_;
  int channels_ = 2;
  double rate_ = 1.0;
  qint64 start_position_ms_ = 0;
  qint64 emitted_frames_ = 0;
  quint64 delivered_buffers_ = 0;
  quint64 discarded_frames_ = 0;
};

}  // namespace photo_editor::desktop
