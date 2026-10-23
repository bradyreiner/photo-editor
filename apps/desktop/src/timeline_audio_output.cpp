#include "timeline_audio_output.h"

#include "timeline_audio_math.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <type_traits>

#include <QAudioSink>
#include <QIODevice>
#include <QMediaPlayer>
#include <QStringList>
#include <QtMath>

namespace photo_editor::desktop {

namespace {

constexpr int kDrainIntervalMs = 5;
constexpr int kMeterIntervalMs = 50;
constexpr int kClippingHoldTicks = 12;
constexpr double kMeterDecay = 0.76;
constexpr qint64 kSinkBufferDurationUs = 60000;
constexpr qint64 kMaximumPendingDurationUs = 120000;

QAudioFormat preferred_audio_format() {
  QAudioFormat format;
  format.setSampleRate(48000);
  format.setChannelCount(2);
  format.setSampleFormat(QAudioFormat::Float);
  return format;
}

QString normalized_channel_mode(const QString& mode) {
  const auto normalized = mode.trimmed().toLower();
  static const QStringList kModes = {
      QStringLiteral("source"),
      QStringLiteral("mono-left"),
      QStringLiteral("mono-right"),
      QStringLiteral("dual-mono"),
      QStringLiteral("stereo"),
      QStringLiteral("downmix-stereo"),
  };
  return kModes.contains(normalized) ? normalized : QStringLiteral("source");
}

template <typename Sample>
double normalized_sample(Sample sample) {
  if constexpr (std::is_same_v<Sample, float>) {
    return static_cast<double>(sample);
  } else if constexpr (std::is_same_v<Sample, quint8>) {
    return (static_cast<double>(sample) - 128.0) / 128.0;
  } else {
    constexpr auto maximum = static_cast<double>(std::numeric_limits<Sample>::max());
    return static_cast<double>(sample) / maximum;
  }
}

template <typename Sample>
Sample encoded_sample(double sample) {
  const auto bounded = qBound(-1.0, sample, 1.0);
  if constexpr (std::is_same_v<Sample, float>) {
    return static_cast<float>(bounded);
  } else if constexpr (std::is_same_v<Sample, quint8>) {
    return static_cast<quint8>(qBound(0, qRound((bounded * 128.0) + 128.0), 255));
  } else {
    constexpr auto maximum = static_cast<double>(std::numeric_limits<Sample>::max());
    const auto encoded = qRound64(bounded * maximum);
    return static_cast<Sample>(qBound(
        static_cast<qint64>(std::numeric_limits<Sample>::min()),
        encoded,
        static_cast<qint64>(std::numeric_limits<Sample>::max())));
  }
}

template <typename Sample, typename Processor>
void process_samples(
    QByteArray* bytes,
    int channel_count,
    double left_gain,
    double right_gain,
    const QString& channel_mode,
    bool swap_channels,
    bool invert_left_phase,
    bool invert_right_phase,
    double* peak_left,
    double* peak_right,
    double* true_peak_left,
    double* true_peak_right,
    double* square_sum_left,
    double* square_sum_right,
    double* cross_sum,
    QVariantList* phase_points,
    qint64* frame_count,
    bool* clipping,
    Processor&& processor) {
  if (bytes == nullptr || channel_count <= 0) {
    return;
  }
  auto* samples = reinterpret_cast<Sample*>(bytes->data());
  const auto sample_count = bytes->size() / static_cast<qsizetype>(sizeof(Sample));
  const auto source_frame_count = qMax<qsizetype>(1, sample_count / channel_count);
  const auto phase_stride = qMax<qsizetype>(1, source_frame_count / 128);
  double left_history[4] = {};
  double right_history[4] = {};
  auto history_size = 0;
  const auto publish_true_peak_segment = [](
      const double* history, double* true_peak) {
    for (auto phase = 0; phase <= 4; ++phase) {
      const auto t = static_cast<double>(phase) / 4.0;
      const auto interpolated = 0.5 *
          ((2.0 * history[1]) +
           (-history[0] + history[2]) * t +
           (2.0 * history[0] - 5.0 * history[1] + 4.0 * history[2] - history[3]) * t * t +
           (-history[0] + 3.0 * history[1] - 3.0 * history[2] + history[3]) * t * t * t);
      *true_peak = qMax(*true_peak, qAbs(interpolated));
    }
  };
  for (qsizetype index = 0; index < sample_count; index += channel_count) {
    auto left = normalized_sample(samples[index]);
    const auto right_index = channel_count > 1 ? index + 1 : index;
    auto right = normalized_sample(samples[right_index]);
    if (channel_mode == QStringLiteral("mono-left")) {
      right = left;
    } else if (channel_mode == QStringLiteral("mono-right")) {
      left = right;
    }
    if (swap_channels) {
      std::swap(left, right);
    }
    if (invert_left_phase) {
      left = -left;
    }
    if (invert_right_phase) {
      right = -right;
    }
    processor(&left, &right);
    left *= left_gain;
    right *= right_gain;
    *peak_left = qMax(*peak_left, qAbs(left));
    *peak_right = qMax(*peak_right, qAbs(right));
    *true_peak_left = qMax(*true_peak_left, qAbs(left));
    *true_peak_right = qMax(*true_peak_right, qAbs(right));
    if (history_size < 4) {
      left_history[history_size] = left;
      right_history[history_size] = right;
      ++history_size;
    } else {
      std::move(left_history + 1, left_history + 4, left_history);
      std::move(right_history + 1, right_history + 4, right_history);
      left_history[3] = left;
      right_history[3] = right;
    }
    if (history_size == 4) {
      publish_true_peak_segment(left_history, true_peak_left);
      publish_true_peak_segment(right_history, true_peak_right);
    }
    *square_sum_left += left * left;
    *square_sum_right += right * right;
    *cross_sum += left * right;
    if (phase_points != nullptr && phase_points->size() < 128 &&
        *frame_count % phase_stride == 0) {
      phase_points->push_back(QVariantMap{
          {QStringLiteral("left"), qBound(-1.0, left, 1.0)},
          {QStringLiteral("right"), qBound(-1.0, right, 1.0)},
      });
    }
    ++(*frame_count);
    *clipping = *clipping || *true_peak_left > 1.0 || *true_peak_right > 1.0;
    samples[index] = encoded_sample<Sample>(left);
    if (channel_count > 1) {
      samples[right_index] = encoded_sample<Sample>(right);
    }
    for (auto channel = 2; channel < channel_count; ++channel) {
      samples[index + channel] = encoded_sample<Sample>(
          normalized_sample(samples[index + channel]) * qMin(left_gain, right_gain));
    }
  }
}

}  // namespace

TimelineAudioOutput::TimelineAudioOutput(QObject* parent)
    : QObject(parent),
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
      buffer_output_(preferred_audio_format()),
#else
      playback_output_(this),
#endif
      native_audio_enabled_(!qEnvironmentVariableIsSet(
          "PHOTO_EDITOR_TIMELINE_PLAYBACK_HEADLESS")) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
  connect(
      &buffer_output_,
      &QAudioBufferOutput::audioBufferReceived,
      this,
      [this](const QAudioBuffer& buffer) { processBuffer(buffer); });
#else
  playback_output_.setVolume(volume_);
#endif
  if (native_audio_enabled_) {
    connect(
        &media_devices_,
        &QMediaDevices::audioOutputsChanged,
        this,
        [this]() {
          recoverOutputDevice();
          emit outputDeviceRecoveryRequested(
              QStringLiteral("The available audio outputs changed; playback moved to the current default device."));
        });
  }

  drain_timer_.setInterval(kDrainIntervalMs);
  drain_timer_.setTimerType(Qt::PreciseTimer);
  connect(&drain_timer_, &QTimer::timeout, this, [this]() { drainPendingAudio(); });

  meter_decay_timer_.setInterval(kMeterIntervalMs);
  connect(&meter_decay_timer_, &QTimer::timeout, this, [this]() {
    peak_left_ *= kMeterDecay;
    peak_right_ *= kMeterDecay;
    true_peak_left_ *= kMeterDecay;
    true_peak_right_ *= kMeterDecay;
    rms_left_ *= kMeterDecay;
    rms_right_ *= kMeterDecay;
    if (clipping_hold_ticks_ > 0) {
      --clipping_hold_ticks_;
    } else {
      clipping_ = false;
    }
    if (peak_left_ < 0.001) {
      peak_left_ = 0.0;
    }
    if (peak_right_ < 0.001) {
      peak_right_ = 0.0;
    }
    if (rms_left_ < 0.001) {
      rms_left_ = 0.0;
    }
    if (rms_right_ < 0.001) {
      rms_right_ = 0.0;
    }
    if (true_peak_left_ < 0.001) {
      true_peak_left_ = 0.0;
    }
    if (true_peak_right_ < 0.001) {
      true_peak_right_ = 0.0;
    }
    if (peak_left_ == 0.0 && peak_right_ == 0.0 && rms_left_ == 0.0 &&
        rms_right_ == 0.0 && true_peak_left_ == 0.0 && true_peak_right_ == 0.0 &&
        !clipping_) {
      meter_decay_timer_.stop();
    }
    emit meterChanged();
  });
}

TimelineAudioOutput::~TimelineAudioOutput() {
  if (sink_ != nullptr) {
    sink_->stop();
  }
}

#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
QAudioBufferOutput* TimelineAudioOutput::bufferOutput() {
  return &buffer_output_;
}
#endif

QAudioFormat TimelineAudioOutput::bufferFormat() const {
  return preferred_audio_format();
}

bool TimelineAudioOutput::attachPlayer(QObject* player) {
  auto* media_player = qobject_cast<QMediaPlayer*>(player);
  if (media_player == nullptr) {
    return false;
  }
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
  media_player->setAudioBufferOutput(&buffer_output_);
#else
  media_player->setAudioOutput(&playback_output_);
#endif
  return true;
}

void TimelineAudioOutput::setPlayerPitchCompensation(QObject* player, bool enabled) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
  if (auto* media_player = qobject_cast<QMediaPlayer*>(player); media_player != nullptr) {
    media_player->setPitchCompensation(enabled);
  }
#else
  Q_UNUSED(player)
  Q_UNUSED(enabled)
#endif
}

double TimelineAudioOutput::volume() const { return volume_; }
double TimelineAudioOutput::pan() const { return pan_; }
bool TimelineAudioOutput::panEnabled() const { return pan_enabled_; }
QString TimelineAudioOutput::channelMode() const { return channel_mode_; }
bool TimelineAudioOutput::swapChannels() const { return swap_channels_; }
bool TimelineAudioOutput::invertLeftPhase() const { return invert_left_phase_; }
bool TimelineAudioOutput::invertRightPhase() const { return invert_right_phase_; }
double TimelineAudioOutput::eqLowDb() const { return eq_low_db_; }
double TimelineAudioOutput::eqMidDb() const { return eq_mid_db_; }
double TimelineAudioOutput::eqHighDb() const { return eq_high_db_; }
bool TimelineAudioOutput::compressorEnabled() const { return compressor_enabled_; }
double TimelineAudioOutput::compressorThresholdDb() const {
  return compressor_threshold_db_;
}
double TimelineAudioOutput::compressorRatio() const { return compressor_ratio_; }
double TimelineAudioOutput::compressorMakeupDb() const { return compressor_makeup_db_; }
bool TimelineAudioOutput::dialogueCleanupEnabled() const {
  return dialogue_cleanup_enabled_;
}
double TimelineAudioOutput::voiceIsolationAmount() const {
  return voice_isolation_amount_;
}
double TimelineAudioOutput::noiseReductionAmount() const {
  return noise_reduction_amount_;
}
double TimelineAudioOutput::dereverbAmount() const { return dereverb_amount_; }
double TimelineAudioOutput::humRemovalAmount() const { return hum_removal_amount_; }
int TimelineAudioOutput::humFrequencyHz() const { return hum_frequency_hz_; }
double TimelineAudioOutput::artifactSmoothing() const { return artifact_smoothing_; }
double TimelineAudioOutput::ambiencePreservation() const {
  return ambience_preservation_;
}
double TimelineAudioOutput::duckingGainDb() const { return ducking_gain_db_; }
double TimelineAudioOutput::peakLeft() const { return peak_left_; }
double TimelineAudioOutput::peakRight() const { return peak_right_; }
double TimelineAudioOutput::truePeakLeft() const { return true_peak_left_; }
double TimelineAudioOutput::truePeakRight() const { return true_peak_right_; }
double TimelineAudioOutput::rmsLeft() const { return rms_left_; }
double TimelineAudioOutput::rmsRight() const { return rms_right_; }
bool TimelineAudioOutput::clipping() const { return clipping_; }
double TimelineAudioOutput::phaseCorrelation() const { return phase_correlation_; }
QVariantList TimelineAudioOutput::phasePoints() const { return phase_points_; }

void TimelineAudioOutput::setVolume(double volume) {
  const auto bounded = qBound(0.0, volume, 4.0);
  if (qAbs(volume_ - bounded) <= 0.0001) {
    return;
  }
  volume_ = bounded;
#if QT_VERSION < QT_VERSION_CHECK(6, 8, 0)
  playback_output_.setVolume(qBound(0.0, volume_, 1.0));
#endif
  emit volumeChanged();
}

void TimelineAudioOutput::setPan(double pan) {
  const auto bounded = qBound(-1.0, pan, 1.0);
  if (qAbs(pan_ - bounded) <= 0.0001) {
    return;
  }
  pan_ = bounded;
  emit panChanged();
}

void TimelineAudioOutput::setPanEnabled(bool enabled) {
  if (pan_enabled_ == enabled) {
    return;
  }
  pan_enabled_ = enabled;
  emit panEnabledChanged();
}

void TimelineAudioOutput::setChannelMode(const QString& mode) {
  const auto normalized = normalized_channel_mode(mode);
  if (channel_mode_ == normalized) {
    return;
  }
  channel_mode_ = normalized;
  resetProcessingState();
  emit channelMappingChanged();
}

void TimelineAudioOutput::setSwapChannels(bool enabled) {
  if (swap_channels_ == enabled) {
    return;
  }
  swap_channels_ = enabled;
  resetProcessingState();
  emit channelMappingChanged();
}

void TimelineAudioOutput::setInvertLeftPhase(bool enabled) {
  if (invert_left_phase_ == enabled) {
    return;
  }
  invert_left_phase_ = enabled;
  resetProcessingState();
  emit channelMappingChanged();
}

void TimelineAudioOutput::setInvertRightPhase(bool enabled) {
  if (invert_right_phase_ == enabled) {
    return;
  }
  invert_right_phase_ = enabled;
  resetProcessingState();
  emit channelMappingChanged();
}

void TimelineAudioOutput::setEqLowDb(double value) {
  const auto bounded = qBound(-12.0, value, 12.0);
  if (qAbs(eq_low_db_ - bounded) <= 0.0001) return;
  eq_low_db_ = bounded;
  resetProcessingState();
  emit processingChanged();
}

void TimelineAudioOutput::setEqMidDb(double value) {
  const auto bounded = qBound(-12.0, value, 12.0);
  if (qAbs(eq_mid_db_ - bounded) <= 0.0001) return;
  eq_mid_db_ = bounded;
  resetProcessingState();
  emit processingChanged();
}

void TimelineAudioOutput::setEqHighDb(double value) {
  const auto bounded = qBound(-12.0, value, 12.0);
  if (qAbs(eq_high_db_ - bounded) <= 0.0001) return;
  eq_high_db_ = bounded;
  resetProcessingState();
  emit processingChanged();
}

void TimelineAudioOutput::setCompressorEnabled(bool enabled) {
  if (compressor_enabled_ == enabled) return;
  compressor_enabled_ = enabled;
  compressor_envelope_ = 0.0;
  emit processingChanged();
}

void TimelineAudioOutput::setCompressorThresholdDb(double value) {
  const auto bounded = qBound(-48.0, value, -1.0);
  if (qAbs(compressor_threshold_db_ - bounded) <= 0.0001) return;
  compressor_threshold_db_ = bounded;
  emit processingChanged();
}

void TimelineAudioOutput::setCompressorRatio(double value) {
  const auto bounded = qBound(1.0, value, 20.0);
  if (qAbs(compressor_ratio_ - bounded) <= 0.0001) return;
  compressor_ratio_ = bounded;
  emit processingChanged();
}

void TimelineAudioOutput::setCompressorMakeupDb(double value) {
  const auto bounded = qBound(0.0, value, 18.0);
  if (qAbs(compressor_makeup_db_ - bounded) <= 0.0001) return;
  compressor_makeup_db_ = bounded;
  emit processingChanged();
}

void TimelineAudioOutput::setDialogueCleanupEnabled(bool enabled) {
  if (dialogue_cleanup_enabled_ == enabled) return;
  dialogue_cleanup_enabled_ = enabled;
  resetProcessingState();
  emit processingChanged();
}

void TimelineAudioOutput::setVoiceIsolationAmount(double value) {
  const auto bounded = qBound(0.0, value, 1.0);
  if (qAbs(voice_isolation_amount_ - bounded) <= 0.0001) return;
  voice_isolation_amount_ = bounded;
  resetProcessingState();
  emit processingChanged();
}

void TimelineAudioOutput::setNoiseReductionAmount(double value) {
  const auto bounded = qBound(0.0, value, 1.0);
  if (qAbs(noise_reduction_amount_ - bounded) <= 0.0001) return;
  noise_reduction_amount_ = bounded;
  resetProcessingState();
  emit processingChanged();
}

void TimelineAudioOutput::setDereverbAmount(double value) {
  const auto bounded = qBound(0.0, value, 1.0);
  if (qAbs(dereverb_amount_ - bounded) <= 0.0001) return;
  dereverb_amount_ = bounded;
  resetProcessingState();
  emit processingChanged();
}

void TimelineAudioOutput::setHumRemovalAmount(double value) {
  const auto bounded = qBound(0.0, value, 1.0);
  if (qAbs(hum_removal_amount_ - bounded) <= 0.0001) return;
  hum_removal_amount_ = bounded;
  resetProcessingState();
  emit processingChanged();
}

void TimelineAudioOutput::setHumFrequencyHz(int value) {
  const auto normalized = qAbs(value - 50) <= qAbs(value - 60) ? 50 : 60;
  if (hum_frequency_hz_ == normalized) return;
  hum_frequency_hz_ = normalized;
  resetProcessingState();
  emit processingChanged();
}

void TimelineAudioOutput::setArtifactSmoothing(double value) {
  const auto bounded = qBound(0.0, value, 1.0);
  if (qAbs(artifact_smoothing_ - bounded) <= 0.0001) return;
  artifact_smoothing_ = bounded;
  resetProcessingState();
  emit processingChanged();
}

void TimelineAudioOutput::setAmbiencePreservation(double value) {
  const auto bounded = qBound(0.0, value, 1.0);
  if (qAbs(ambience_preservation_ - bounded) <= 0.0001) return;
  ambience_preservation_ = bounded;
  resetProcessingState();
  emit processingChanged();
}

void TimelineAudioOutput::setDuckingGainDb(double value) {
  const auto bounded = qBound(-30.0, value, 0.0);
  if (qAbs(ducking_gain_db_ - bounded) <= 0.0001) return;
  ducking_gain_db_ = bounded;
  emit processingChanged();
}

void TimelineAudioOutput::resetProcessingState() {
  for (auto& channel : eq_state_) {
    for (auto& band : channel) {
      band = {};
    }
  }
  for (auto& channel : hum_state_) {
    for (auto& band : channel) {
      band = {};
    }
  }
  compressor_envelope_ = 0.0;
  cleanup_fast_envelope_ = 0.0;
  cleanup_slow_envelope_ = 0.0;
  cleanup_gain_ = 1.0;
}

QVector<double> TimelineAudioOutput::channelGains() const {
  const auto gains = stereoPanGains(pan_, volume_, pan_enabled_);
  return {gains.left, gains.right};
}

QVector<float> TimelineAudioOutput::processStereoSamplesForTesting(
    const QVector<float>& interleaved_samples) {
  auto bytes = QByteArray(
      reinterpret_cast<const char*>(interleaved_samples.constData()),
      interleaved_samples.size() * static_cast<qsizetype>(sizeof(float)));
  auto left = 0.0;
  auto right = 0.0;
  auto rms_left = 0.0;
  auto rms_right = 0.0;
  auto true_peak_left = 0.0;
  auto true_peak_right = 0.0;
  auto phase_correlation = 0.0;
  QVariantList phase_points;
  auto clipped = false;
  const QAudioFormat format = preferred_audio_format();
  QAudioBuffer buffer(bytes, format, -1);
  bytes = processAudioBytes(
      buffer, &left, &right, &true_peak_left, &true_peak_right,
      &rms_left, &rms_right, &phase_correlation, &phase_points, &clipped);
  publishMeters(
      left, right, true_peak_left, true_peak_right, rms_left, rms_right,
      phase_correlation, phase_points, clipped);
  QVector<float> processed(interleaved_samples.size());
  std::copy_n(
      reinterpret_cast<const float*>(bytes.constData()),
      processed.size(),
      processed.begin());
  return processed;
}

void TimelineAudioOutput::resetMeters() {
  peak_left_ = 0.0;
  peak_right_ = 0.0;
  true_peak_left_ = 0.0;
  true_peak_right_ = 0.0;
  rms_left_ = 0.0;
  rms_right_ = 0.0;
  phase_correlation_ = 0.0;
  phase_points_.clear();
  clipping_ = false;
  clipping_hold_ticks_ = 0;
  meter_decay_timer_.stop();
  emit meterChanged();
}

void TimelineAudioOutput::processBuffer(const QAudioBuffer& buffer) {
  if (!buffer.isValid() || buffer.byteCount() <= 0) {
    return;
  }
  auto left = 0.0;
  auto right = 0.0;
  auto rms_left = 0.0;
  auto rms_right = 0.0;
  auto true_peak_left = 0.0;
  auto true_peak_right = 0.0;
  auto phase_correlation = 0.0;
  QVariantList phase_points;
  auto clipped = false;
  auto bytes = processAudioBytes(
      buffer, &left, &right, &true_peak_left, &true_peak_right,
      &rms_left, &rms_right, &phase_correlation, &phase_points, &clipped);
  publishMeters(
      left, right, true_peak_left, true_peak_right, rms_left, rms_right,
      phase_correlation, phase_points, clipped);
  if (!native_audio_enabled_ || bytes.isEmpty()) {
    return;
  }
  ensureSink(buffer.format());
  pending_audio_.append(bytes);
  const auto maximum_pending = qMax<qsizetype>(
      buffer.format().bytesForDuration(kMaximumPendingDurationUs), buffer.byteCount() * 2);
  if (pending_audio_.size() > maximum_pending) {
    pending_audio_.remove(0, pending_audio_.size() - maximum_pending);
  }
  drainPendingAudio();
  if (!pending_audio_.isEmpty() && !drain_timer_.isActive()) {
    drain_timer_.start();
  }
}

void TimelineAudioOutput::ensureSink(const QAudioFormat& format) {
  if (sink_ != nullptr && sink_format_ == format && sink_device_ != nullptr) {
    return;
  }
  if (sink_ != nullptr) {
    sink_->stop();
    sink_->deleteLater();
  }
  sink_format_ = format;
  sink_ = new QAudioSink(format, this);
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
  sink_->setBufferFrameCount(qMax(256, format.framesForDuration(kSinkBufferDurationUs)));
#else
  sink_->setBufferSize(format.bytesForFrames(
      qMax<qint64>(256, format.framesForDuration(kSinkBufferDurationUs))));
#endif
  sink_device_ = sink_->start();
  pending_audio_.clear();
}

void TimelineAudioOutput::drainPendingAudio() {
  if (sink_ != nullptr && sink_->error() != QAudio::NoError) {
    const auto detail = QStringLiteral("The active audio output became unavailable.");
    recoverOutputDevice();
    emit outputDeviceRecoveryRequested(detail);
    return;
  }
  if (sink_ == nullptr || sink_device_ == nullptr || pending_audio_.isEmpty()) {
    if (pending_audio_.isEmpty()) {
      drain_timer_.stop();
    }
    return;
  }
  const auto writable = qMin<qsizetype>(sink_->bytesFree(), pending_audio_.size());
  if (writable <= 0) {
    return;
  }
  const auto written = sink_device_->write(pending_audio_.constData(), writable);
  if (written > 0) {
    pending_audio_.remove(0, written);
  }
  if (pending_audio_.isEmpty()) {
    drain_timer_.stop();
  }
}

void TimelineAudioOutput::recoverOutputDevice() {
#if QT_VERSION < QT_VERSION_CHECK(6, 8, 0)
  playback_output_.setDevice(QMediaDevices::defaultAudioOutput());
#endif
  drain_timer_.stop();
  pending_audio_.clear();
  sink_device_.clear();
  if (sink_ != nullptr) {
    auto* stale_sink = sink_.data();
    sink_.clear();
    stale_sink->stop();
    stale_sink->deleteLater();
  }
  sink_format_ = {};
}

void TimelineAudioOutput::publishMeters(
    double left,
    double right,
    double true_peak_left,
    double true_peak_right,
    double rms_left,
    double rms_right,
    double phase_correlation,
    const QVariantList& phase_points,
    bool clipping) {
  peak_left_ = qMax(peak_left_, left);
  peak_right_ = qMax(peak_right_, right);
  true_peak_left_ = qMax(true_peak_left_, true_peak_left);
  true_peak_right_ = qMax(true_peak_right_, true_peak_right);
  rms_left_ = qMax(rms_left_, rms_left);
  rms_right_ = qMax(rms_right_, rms_right);
  phase_correlation_ = qBound(-1.0, phase_correlation, 1.0);
  phase_points_ = phase_points;
  if (clipping) {
    clipping_ = true;
    clipping_hold_ticks_ = kClippingHoldTicks;
  }
  if ((peak_left_ > 0.0 || peak_right_ > 0.0 || clipping_) &&
      !meter_decay_timer_.isActive()) {
    meter_decay_timer_.start();
  }
  emit meterChanged();
}

QByteArray TimelineAudioOutput::processAudioBytes(
    const QAudioBuffer& buffer,
    double* peak_left,
    double* peak_right,
    double* true_peak_left,
    double* true_peak_right,
    double* rms_left,
    double* rms_right,
    double* phase_correlation,
    QVariantList* phase_points,
    bool* clipping) {
  auto bytes = QByteArray(
      buffer.constData<char>(), buffer.byteCount());
  const auto channel_count = qMax(1, buffer.format().channelCount());
  const auto gains = channelGains();
  const auto sample_rate = qMax(8000, buffer.format().sampleRate());
  auto square_sum_left = 0.0;
  auto square_sum_right = 0.0;
  auto cross_sum = 0.0;
  qint64 frame_count = 0;
  struct Coefficients final {
    double b0 = 1.0;
    double b1 = 0.0;
    double b2 = 0.0;
    double a1 = 0.0;
    double a2 = 0.0;
  };
  const auto peaking_coefficients = [sample_rate](double frequency, double q, double gain_db) {
    const auto a = qPow(10.0, gain_db / 40.0);
    const auto omega = 2.0 * std::numbers::pi * frequency /
                       static_cast<double>(sample_rate);
    const auto alpha = qSin(omega) / (2.0 * q);
    const auto cosine = qCos(omega);
    const auto a0 = 1.0 + alpha / a;
    return Coefficients{
        (1.0 + alpha * a) / a0,
        (-2.0 * cosine) / a0,
        (1.0 - alpha * a) / a0,
        (-2.0 * cosine) / a0,
        (1.0 - alpha / a) / a0,
    };
  };
  const auto notch_coefficients = [sample_rate](double frequency, double q) {
    const auto omega = 2.0 * std::numbers::pi * frequency /
                       static_cast<double>(sample_rate);
    const auto alpha = qSin(omega) / (2.0 * q);
    const auto cosine = qCos(omega);
    const auto a0 = 1.0 + alpha;
    return Coefficients{
        1.0 / a0,
        (-2.0 * cosine) / a0,
        1.0 / a0,
        (-2.0 * cosine) / a0,
        (1.0 - alpha) / a0,
    };
  };
  const Coefficients coefficients[3] = {
      peaking_coefficients(120.0, 0.7, eq_low_db_),
      peaking_coefficients(1000.0, 0.9, eq_mid_db_),
      peaking_coefficients(8000.0, 0.7, eq_high_db_),
  };
  const Coefficients hum_coefficients[2] = {
      notch_coefficients(static_cast<double>(hum_frequency_hz_), 18.0),
      notch_coefficients(static_cast<double>(hum_frequency_hz_ * 2), 22.0),
  };
  const auto apply_biquad = [](double sample, const Coefficients& values, BiquadState* state) {
    const auto output = values.b0 * sample + values.b1 * state->x1 +
                        values.b2 * state->x2 - values.a1 * state->y1 -
                        values.a2 * state->y2;
    state->x2 = state->x1;
    state->x1 = sample;
    state->y2 = state->y1;
    state->y1 = output;
    return std::isfinite(output) ? output : 0.0;
  };
  const auto attack = qExp(-1.0 / (0.020 * static_cast<double>(sample_rate)));
  const auto release = qExp(-1.0 / (0.250 * static_cast<double>(sample_rate)));
  const auto cleanup_fast_attack =
      qExp(-1.0 / (0.004 * static_cast<double>(sample_rate)));
  const auto cleanup_fast_release =
      qExp(-1.0 / (0.055 * static_cast<double>(sample_rate)));
  const auto cleanup_slow =
      qExp(-1.0 / (0.420 * static_cast<double>(sample_rate)));
  const auto cleanup_gain_smoothing = qExp(
      -1.0 /
      ((0.003 + 0.080 * artifact_smoothing_) * static_cast<double>(sample_rate)));
  const auto processor = [this, &coefficients, &hum_coefficients, &apply_biquad,
                          attack, release, cleanup_fast_attack,
                          cleanup_fast_release, cleanup_slow,
                          cleanup_gain_smoothing](
                             double* left,
                             double* right) {
    if (dialogue_cleanup_enabled_) {
      const auto preservation_scale = 1.0 - 0.65 * ambience_preservation_;
      const auto hum_mix = hum_removal_amount_ * preservation_scale;
      if (hum_mix > 0.0001) {
        const auto dry_left = *left;
        const auto dry_right = *right;
        for (auto band = 0; band < 2; ++band) {
          *left = apply_biquad(*left, hum_coefficients[band], &hum_state_[0][band]);
          *right = apply_biquad(*right, hum_coefficients[band], &hum_state_[1][band]);
        }
        *left = dry_left + (*left - dry_left) * hum_mix;
        *right = dry_right + (*right - dry_right) * hum_mix;
      }

      const auto mid = (*left + *right) * 0.5;
      const auto side = (*left - *right) * 0.5;
      const auto isolation = voice_isolation_amount_ * preservation_scale;
      const auto isolated_mid = mid * (1.0 + 0.10 * isolation);
      const auto isolated_side = side * (1.0 - 0.88 * isolation);
      *left = isolated_mid + isolated_side;
      *right = isolated_mid - isolated_side;

      const auto peak = qMax(qAbs(*left), qAbs(*right));
      const auto fast_coefficient = peak > cleanup_fast_envelope_
                                        ? cleanup_fast_attack
                                        : cleanup_fast_release;
      cleanup_fast_envelope_ = fast_coefficient * cleanup_fast_envelope_ +
                               (1.0 - fast_coefficient) * peak;
      cleanup_slow_envelope_ = cleanup_slow * cleanup_slow_envelope_ +
                               (1.0 - cleanup_slow) * peak;

      const auto noise_floor = 0.028;
      const auto noise_confidence = qBound(
          0.0, (noise_floor - cleanup_fast_envelope_) / noise_floor, 1.0);
      const auto tail_ratio = cleanup_slow_envelope_ > 1.0e-6
                                  ? cleanup_fast_envelope_ / cleanup_slow_envelope_
                                  : 1.0;
      const auto tail_confidence = qBound(0.0, (0.78 - tail_ratio) / 0.78, 1.0);
      const auto noise_attenuation =
          noise_reduction_amount_ * preservation_scale * noise_confidence * 0.92;
      const auto tail_attenuation =
          dereverb_amount_ * preservation_scale * tail_confidence * 0.78;
      const auto target_gain = qBound(
          0.06 + 0.34 * ambience_preservation_,
          (1.0 - noise_attenuation) * (1.0 - tail_attenuation),
          1.0);
      cleanup_gain_ = cleanup_gain_smoothing * cleanup_gain_ +
                      (1.0 - cleanup_gain_smoothing) * target_gain;
      *left *= cleanup_gain_;
      *right *= cleanup_gain_;
    }

    for (auto band = 0; band < 3; ++band) {
      *left = apply_biquad(*left, coefficients[band], &eq_state_[0][band]);
      *right = apply_biquad(*right, coefficients[band], &eq_state_[1][band]);
    }
    auto gain_db = ducking_gain_db_;
    if (compressor_enabled_) {
      const auto peak = qMax(qAbs(*left), qAbs(*right));
      const auto smoothing = peak > compressor_envelope_ ? attack : release;
      compressor_envelope_ = smoothing * compressor_envelope_ + (1.0 - smoothing) * peak;
      const auto input_db = 20.0 * qLn(qMax(1.0e-9, compressor_envelope_)) / qLn(10.0);
      if (input_db > compressor_threshold_db_) {
        const auto output_db = compressor_threshold_db_ +
                               (input_db - compressor_threshold_db_) / compressor_ratio_;
        gain_db += output_db - input_db;
      }
      gain_db += compressor_makeup_db_;
    }
    const auto dynamics_gain = qPow(10.0, gain_db / 20.0);
    *left *= dynamics_gain;
    *right *= dynamics_gain;
  };
  switch (buffer.format().sampleFormat()) {
    case QAudioFormat::UInt8:
      process_samples<quint8>(
          &bytes, channel_count, gains.at(0), gains.at(1), channel_mode_, swap_channels_,
          invert_left_phase_, invert_right_phase_, peak_left, peak_right,
          true_peak_left, true_peak_right,
          &square_sum_left, &square_sum_right, &cross_sum, phase_points,
          &frame_count, clipping,
          processor);
      break;
    case QAudioFormat::Int16:
      process_samples<qint16>(
          &bytes, channel_count, gains.at(0), gains.at(1), channel_mode_, swap_channels_,
          invert_left_phase_, invert_right_phase_, peak_left, peak_right,
          true_peak_left, true_peak_right,
          &square_sum_left, &square_sum_right, &cross_sum, phase_points,
          &frame_count, clipping,
          processor);
      break;
    case QAudioFormat::Int32:
      process_samples<qint32>(
          &bytes, channel_count, gains.at(0), gains.at(1), channel_mode_, swap_channels_,
          invert_left_phase_, invert_right_phase_, peak_left, peak_right,
          true_peak_left, true_peak_right,
          &square_sum_left, &square_sum_right, &cross_sum, phase_points,
          &frame_count, clipping,
          processor);
      break;
    case QAudioFormat::Float:
      process_samples<float>(
          &bytes, channel_count, gains.at(0), gains.at(1), channel_mode_, swap_channels_,
          invert_left_phase_, invert_right_phase_, peak_left, peak_right,
          true_peak_left, true_peak_right,
          &square_sum_left, &square_sum_right, &cross_sum, phase_points,
          &frame_count, clipping,
          processor);
      break;
    case QAudioFormat::Unknown:
    case QAudioFormat::NSampleFormats:
      bytes.clear();
      break;
  }
  if (frame_count > 0) {
    *rms_left = qSqrt(square_sum_left / static_cast<double>(frame_count));
    *rms_right = qSqrt(square_sum_right / static_cast<double>(frame_count));
    const auto denominator = qSqrt(square_sum_left * square_sum_right);
    *phase_correlation = denominator > 1.0e-12
                             ? qBound(-1.0, cross_sum / denominator, 1.0)
                             : 0.0;
  }
  return bytes;
}

}  // namespace photo_editor::desktop
