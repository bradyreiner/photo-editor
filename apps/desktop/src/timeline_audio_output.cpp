#include "timeline_audio_output.h"
#include "timeline_pcm_audio_decoder.h"

#include <QCoreApplication>
#include <QDir>

#include "timeline_audio_math.h"
#include "timeline_audio_sink_policy.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <type_traits>

#include <QAudioSink>
#include <QIODevice>
#include <QMetaObject>
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

double finite_value(double value, double fallback) {
  return std::isfinite(value) ? value : fallback;
}

double bounded_finite_value(
    double value,
    double fallback,
    double minimum,
    double maximum) {
  return qBound(minimum, finite_value(value, fallback), maximum);
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
  const auto bounded = qBound(-1.0, finite_value(sample, 0.0), 1.0);
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
    if (!std::isfinite(left)) left = 0.0;
    if (!std::isfinite(right)) right = 0.0;
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
    const auto gained_left = left * left_gain;
    const auto gained_right = right * right_gain;
    if (!std::isfinite(gained_left) || !std::isfinite(gained_right)) {
      *clipping = true;
    }
    left = finite_value(gained_left, 0.0);
    right = finite_value(gained_right, 0.0);
    processor(&left, &right);
    if (!std::isfinite(left) || !std::isfinite(right)) {
      *clipping = true;
    }
    left = finite_value(left, 0.0);
    right = finite_value(right, 0.0);
    constexpr auto kMeterSampleLimit = 1.0e6;
    const auto measured_left = qBound(-kMeterSampleLimit, left, kMeterSampleLimit);
    const auto measured_right = qBound(-kMeterSampleLimit, right, kMeterSampleLimit);
    *peak_left = qMax(*peak_left, qAbs(measured_left));
    *peak_right = qMax(*peak_right, qAbs(measured_right));
    *true_peak_left = qMax(*true_peak_left, qAbs(measured_left));
    *true_peak_right = qMax(*true_peak_right, qAbs(measured_right));
    if (history_size < 4) {
      left_history[history_size] = measured_left;
      right_history[history_size] = measured_right;
      ++history_size;
    } else {
      std::move(left_history + 1, left_history + 4, left_history);
      std::move(right_history + 1, right_history + 4, right_history);
      left_history[3] = measured_left;
      right_history[3] = measured_right;
    }
    if (history_size == 4) {
      publish_true_peak_segment(left_history, true_peak_left);
      publish_true_peak_segment(right_history, true_peak_right);
    }
    *square_sum_left += measured_left * measured_left;
    *square_sum_right += measured_right * measured_right;
    *cross_sum += measured_left * measured_right;
    if (phase_points != nullptr && phase_points->size() < 128 &&
        *frame_count % phase_stride == 0) {
      phase_points->push_back(QVariantMap{
          {QStringLiteral("left"), qBound(-1.0, measured_left, 1.0)},
          {QStringLiteral("right"), qBound(-1.0, measured_right, 1.0)},
      });
    }
    ++(*frame_count);
    *clipping = *clipping || *true_peak_left > 1.0 || *true_peak_right > 1.0;
    samples[index] = encoded_sample<Sample>(left);
    if (channel_count > 1) {
      samples[right_index] = encoded_sample<Sample>(right);
    }
    for (auto channel = 2; channel < channel_count; ++channel) {
      auto channel_sample = normalized_sample(samples[index + channel]);
      if (!std::isfinite(channel_sample)) {
        channel_sample = 0.0;
      }
      const auto channel_gain = qMin(left_gain, right_gain);
      const auto processed_channel = channel_sample * channel_gain;
      if (!std::isfinite(processed_channel) || qAbs(processed_channel) > 1.0) {
        *clipping = true;
      }
      samples[index + channel] = encoded_sample<Sample>(
          processed_channel);
    }
  }
}

template <typename Sample>
QByteArray stereo_mixdown(
    const QByteArray& input,
    const QAudioFormat& source_format,
    const QString& channel_mode) {
  const auto channel_count = source_format.channelCount();
  const auto sample_count = input.size() / static_cast<qsizetype>(sizeof(Sample));
  if (channel_count <= 0 || sample_count % channel_count != 0) {
    return {};
  }
  const auto frame_count = sample_count / channel_count;
  QByteArray output(frame_count * 2 * static_cast<qsizetype>(sizeof(float)), '\0');
  const auto* source = reinterpret_cast<const Sample*>(input.constData());
  auto* destination = reinterpret_cast<float*>(output.data());
  const auto unknown_layout =
      source_format.channelConfig() == QAudioFormat::ChannelConfigUnknown;

  auto channel_at = [&source_format, channel_count, unknown_layout](
                        QAudioFormat::AudioChannelPosition position) {
    if (unknown_layout) {
      return -1;
    }
    const auto offset = source_format.channelOffset(position);
    return offset >= 0 && offset < channel_count ? offset : -1;
  };
  // Unknown layouts conservatively preserve only the first stereo pair. The
  // caller assigns Qt's channel-count default where one exists; extras remain
  // unmapped if the format still has no defensible channel positions.
  const auto resolved_left = unknown_layout ? 0 : channel_at(QAudioFormat::FrontLeft);
  const auto resolved_right = unknown_layout ? 1 : channel_at(QAudioFormat::FrontRight);
  const auto left_channel = resolved_left >= 0 ? resolved_left : 0;
  const auto right_channel = channel_count > 1
                                 ? (resolved_right >= 0 ? resolved_right : 1)
                                 : left_channel;

  constexpr auto kMinusThreeDb = 0.7071067811865476;
  struct Contribution final {
    QAudioFormat::AudioChannelPosition position;
    double left_gain;
    double right_gain;
  };
  static constexpr Contribution kContributions[] = {
      {QAudioFormat::FrontCenter, kMinusThreeDb, kMinusThreeDb},
      {QAudioFormat::BackLeft, kMinusThreeDb, 0.0},
      {QAudioFormat::SideLeft, kMinusThreeDb, 0.0},
      {QAudioFormat::FrontLeftOfCenter, kMinusThreeDb, 0.0},
      {QAudioFormat::TopFrontLeft, 0.5, 0.0},
      {QAudioFormat::TopBackLeft, 0.5, 0.0},
      {QAudioFormat::TopSideLeft, 0.5, 0.0},
      {QAudioFormat::BottomFrontLeft, 0.5, 0.0},
      {QAudioFormat::BackRight, 0.0, kMinusThreeDb},
      {QAudioFormat::SideRight, 0.0, kMinusThreeDb},
      {QAudioFormat::FrontRightOfCenter, 0.0, kMinusThreeDb},
      {QAudioFormat::TopFrontRight, 0.0, 0.5},
      {QAudioFormat::TopBackRight, 0.0, 0.5},
      {QAudioFormat::TopSideRight, 0.0, 0.5},
      {QAudioFormat::BottomFrontRight, 0.0, 0.5},
      {QAudioFormat::BackCenter, 0.5, 0.5},
      {QAudioFormat::TopCenter, 0.5, 0.5},
      {QAudioFormat::TopFrontCenter, 0.5, 0.5},
      {QAudioFormat::TopBackCenter, 0.5, 0.5},
      {QAudioFormat::BottomFrontCenter, 0.5, 0.5},
  };
  const auto take_first_pair = channel_mode == QStringLiteral("stereo") ||
                               channel_mode == QStringLiteral("dual-mono");
  const auto take_mono_left = channel_mode == QStringLiteral("mono-left");
  const auto take_mono_right = channel_mode == QStringLiteral("mono-right");

  for (qsizetype frame = 0; frame < frame_count; ++frame) {
    const auto sample = [source, frame, channel_count](int channel) {
      auto value = normalized_sample(source[frame * channel_count + channel]);
      return std::isfinite(value) ? value : 0.0;
    };
    auto left = 0.0;
    auto right = 0.0;
    if (channel_count == 1) {
      left = right = sample(0);
    } else if (take_mono_left) {
      left = right = sample(left_channel);
    } else if (take_mono_right) {
      left = right = sample(right_channel);
    } else if (take_first_pair) {
      left = sample(left_channel);
      right = sample(right_channel);
    } else {
      left = sample(left_channel);
      right = sample(right_channel);
      for (const auto& contribution : kContributions) {
        const auto channel = channel_at(contribution.position);
        if (channel < 0 || channel == left_channel || channel == right_channel) {
          continue;
        }
        const auto value = sample(channel);
        left += contribution.left_gain * value;
        right += contribution.right_gain * value;
      }
      // LFE and LFE2 are intentionally omitted so bass effects do not
      // overload stereo monitoring when no dedicated LFE output exists.
    }
    constexpr auto kFloatMaximum =
        static_cast<double>(std::numeric_limits<float>::max());
    destination[frame * 2] = static_cast<float>(qBound(
        -kFloatMaximum, finite_value(left, 0.0), kFloatMaximum));
    destination[frame * 2 + 1] = static_cast<float>(qBound(
        -kFloatMaximum, finite_value(right, 0.0), kFloatMaximum));
  }
  return output;
}

}  // namespace

TimelineAudioOutput::TimelineAudioOutput(QObject* parent)
    : QObject(parent),
      buffer_output_(),
      native_audio_enabled_(!qEnvironmentVariableIsSet(
          "PHOTO_EDITOR_TIMELINE_PLAYBACK_HEADLESS")) {
  buffer_output_connection_ = connect(
      &buffer_output_,
      &QAudioBufferOutput::audioBufferReceived,
      this,
      [this](const QAudioBuffer& buffer) {
        if (pcm_decoder_ != nullptr) return;
        const auto callback_generation =
            output_generation_.load(std::memory_order_acquire);
        QMetaObject::invokeMethod(
            this,
            [this, buffer, callback_generation]() {
              processBuffer(buffer, callback_generation);
            },
            Qt::QueuedConnection);
      },
      Qt::DirectConnection);
#ifdef Q_OS_MACOS
  if (native_audio_enabled_) {
    const auto backend = qEnvironmentVariable("QT_MEDIA_BACKEND");
    auto ffmpeg_plugin_available = false;
    for (const auto& path : QCoreApplication::libraryPaths()) {
      if (!QDir(path + QStringLiteral("/multimedia"))
               .entryList({QStringLiteral("*ffmpeg*")}, QDir::Files).isEmpty()) {
        ffmpeg_plugin_available = true;
        break;
      }
    }
    if (backend == QStringLiteral("darwin") || !ffmpeg_plugin_available) {
      pcm_decoder_ = std::make_unique<TimelinePcmAudioDecoder>(
          [this](const QAudioBuffer& buffer) {
            processBuffer(buffer, output_generation_.load(std::memory_order_acquire));
          });
    }
  }
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
  output_generation_.fetch_add(1U, std::memory_order_acq_rel);
  QObject::disconnect(buffer_output_connection_);
  if (sink_ != nullptr) {
    sink_->stop();
  }
}

QAudioBufferOutput* TimelineAudioOutput::bufferOutput() {
  return &buffer_output_;
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
bool TimelineAudioOutput::equalizerEnabled() const { return equalizer_enabled_; }
bool TimelineAudioOutput::compressorEnabled() const { return compressor_enabled_; }
double TimelineAudioOutput::compressorThresholdDb() const {
  return compressor_threshold_db_;
}
double TimelineAudioOutput::compressorRatio() const { return compressor_ratio_; }
double TimelineAudioOutput::compressorMakeupDb() const { return compressor_makeup_db_; }
bool TimelineAudioOutput::limiterEnabled() const { return limiter_enabled_; }
double TimelineAudioOutput::limiterCeilingDb() const { return limiter_ceiling_db_; }
bool TimelineAudioOutput::gateEnabled() const { return gate_enabled_; }
double TimelineAudioOutput::gateThresholdDb() const { return gate_threshold_db_; }
bool TimelineAudioOutput::deEsserEnabled() const { return deesser_enabled_; }
double TimelineAudioOutput::deEsserAmount() const { return deesser_amount_; }
bool TimelineAudioOutput::highPassEnabled() const { return high_pass_enabled_; }
double TimelineAudioOutput::highPassHz() const { return high_pass_hz_; }
bool TimelineAudioOutput::lowPassEnabled() const { return low_pass_enabled_; }
double TimelineAudioOutput::lowPassHz() const { return low_pass_hz_; }
bool TimelineAudioOutput::processingEnabled() const { return processing_enabled_; }
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
  // Clip and track gain can add +48 dB; matched bypass may add another +12 dB.
  const auto bounded = bounded_finite_value(volume, volume_, 0.0, 4096.0);
  if (qAbs(volume_ - bounded) <= 0.0001) {
    return;
  }
  volume_ = bounded;
  emit volumeChanged();
}

void TimelineAudioOutput::setPan(double pan) {
  const auto bounded = bounded_finite_value(pan, pan_, -1.0, 1.0);
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
  const auto bounded = bounded_finite_value(value, eq_low_db_, -12.0, 12.0);
  if (qAbs(eq_low_db_ - bounded) <= 0.0001) return;
  eq_low_db_ = bounded;
  emit processingChanged();
}

void TimelineAudioOutput::setEqMidDb(double value) {
  const auto bounded = bounded_finite_value(value, eq_mid_db_, -12.0, 12.0);
  if (qAbs(eq_mid_db_ - bounded) <= 0.0001) return;
  eq_mid_db_ = bounded;
  emit processingChanged();
}

void TimelineAudioOutput::setEqHighDb(double value) {
  const auto bounded = bounded_finite_value(value, eq_high_db_, -12.0, 12.0);
  if (qAbs(eq_high_db_ - bounded) <= 0.0001) return;
  eq_high_db_ = bounded;
  emit processingChanged();
}

void TimelineAudioOutput::setEqualizerEnabled(bool enabled) {
  if (equalizer_enabled_ == enabled) return;
  equalizer_enabled_ = enabled;
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
  const auto bounded = bounded_finite_value(value, compressor_threshold_db_, -48.0, -1.0);
  if (qAbs(compressor_threshold_db_ - bounded) <= 0.0001) return;
  compressor_threshold_db_ = bounded;
  emit processingChanged();
}

void TimelineAudioOutput::setCompressorRatio(double value) {
  const auto bounded = bounded_finite_value(value, compressor_ratio_, 1.0, 20.0);
  if (qAbs(compressor_ratio_ - bounded) <= 0.0001) return;
  compressor_ratio_ = bounded;
  emit processingChanged();
}

void TimelineAudioOutput::setCompressorMakeupDb(double value) {
  const auto bounded = bounded_finite_value(value, compressor_makeup_db_, 0.0, 18.0);
  if (qAbs(compressor_makeup_db_ - bounded) <= 0.0001) return;
  compressor_makeup_db_ = bounded;
  emit processingChanged();
}

void TimelineAudioOutput::setLimiterEnabled(bool enabled) {
  if (limiter_enabled_ == enabled) return;
  limiter_enabled_ = enabled;
  resetProcessingState();
  emit processingChanged();
}

void TimelineAudioOutput::setLimiterCeilingDb(double value) {
  const auto bounded = bounded_finite_value(value, limiter_ceiling_db_, -12.0, 0.0);
  if (qAbs(limiter_ceiling_db_ - bounded) <= 0.0001) return;
  limiter_ceiling_db_ = bounded;
  emit processingChanged();
}

void TimelineAudioOutput::setGateEnabled(bool enabled) {
  if (gate_enabled_ == enabled) return;
  gate_enabled_ = enabled;
  resetProcessingState();
  emit processingChanged();
}

void TimelineAudioOutput::setGateThresholdDb(double value) {
  const auto bounded = bounded_finite_value(value, gate_threshold_db_, -80.0, -10.0);
  if (qAbs(gate_threshold_db_ - bounded) <= 0.0001) return;
  gate_threshold_db_ = bounded;
  emit processingChanged();
}

void TimelineAudioOutput::setDeEsserEnabled(bool enabled) {
  if (deesser_enabled_ == enabled) return;
  deesser_enabled_ = enabled;
  resetProcessingState();
  emit processingChanged();
}

void TimelineAudioOutput::setDeEsserAmount(double value) {
  const auto bounded = bounded_finite_value(value, deesser_amount_, 0.0, 1.0);
  if (qAbs(deesser_amount_ - bounded) <= 0.0001) return;
  deesser_amount_ = bounded;
  emit processingChanged();
}

void TimelineAudioOutput::setHighPassEnabled(bool enabled) {
  if (high_pass_enabled_ == enabled) return;
  high_pass_enabled_ = enabled;
  resetProcessingState();
  emit processingChanged();
}

void TimelineAudioOutput::setHighPassHz(double value) {
  const auto bounded = bounded_finite_value(value, high_pass_hz_, 20.0, 1000.0);
  if (qAbs(high_pass_hz_ - bounded) <= 0.0001) return;
  high_pass_hz_ = bounded;
  emit processingChanged();
}

void TimelineAudioOutput::setLowPassEnabled(bool enabled) {
  if (low_pass_enabled_ == enabled) return;
  low_pass_enabled_ = enabled;
  resetProcessingState();
  emit processingChanged();
}

void TimelineAudioOutput::setLowPassHz(double value) {
  const auto bounded = bounded_finite_value(value, low_pass_hz_, 1000.0, 20000.0);
  if (qAbs(low_pass_hz_ - bounded) <= 0.0001) return;
  low_pass_hz_ = bounded;
  emit processingChanged();
}

void TimelineAudioOutput::setProcessingEnabled(bool enabled) {
  if (processing_enabled_ == enabled) return;
  processing_enabled_ = enabled;
  resetProcessingState();
  emit processingChanged();
}

void TimelineAudioOutput::setDialogueCleanupEnabled(bool enabled) {
  if (dialogue_cleanup_enabled_ == enabled) return;
  dialogue_cleanup_enabled_ = enabled;
  resetProcessingState();
  emit processingChanged();
}

void TimelineAudioOutput::setVoiceIsolationAmount(double value) {
  const auto bounded = bounded_finite_value(value, voice_isolation_amount_, 0.0, 1.0);
  if (qAbs(voice_isolation_amount_ - bounded) <= 0.0001) return;
  voice_isolation_amount_ = bounded;
  emit processingChanged();
}

void TimelineAudioOutput::setNoiseReductionAmount(double value) {
  const auto bounded = bounded_finite_value(value, noise_reduction_amount_, 0.0, 1.0);
  if (qAbs(noise_reduction_amount_ - bounded) <= 0.0001) return;
  noise_reduction_amount_ = bounded;
  emit processingChanged();
}

void TimelineAudioOutput::setDereverbAmount(double value) {
  const auto bounded = bounded_finite_value(value, dereverb_amount_, 0.0, 1.0);
  if (qAbs(dereverb_amount_ - bounded) <= 0.0001) return;
  dereverb_amount_ = bounded;
  emit processingChanged();
}

void TimelineAudioOutput::setHumRemovalAmount(double value) {
  const auto bounded = bounded_finite_value(value, hum_removal_amount_, 0.0, 1.0);
  if (qAbs(hum_removal_amount_ - bounded) <= 0.0001) return;
  hum_removal_amount_ = bounded;
  emit processingChanged();
}

void TimelineAudioOutput::setHumFrequencyHz(int value) {
  const auto normalized = value <= 55 ? 50 : 60;
  if (hum_frequency_hz_ == normalized) return;
  hum_frequency_hz_ = normalized;
  emit processingChanged();
}

void TimelineAudioOutput::setArtifactSmoothing(double value) {
  const auto bounded = bounded_finite_value(value, artifact_smoothing_, 0.0, 1.0);
  if (qAbs(artifact_smoothing_ - bounded) <= 0.0001) return;
  artifact_smoothing_ = bounded;
  emit processingChanged();
}

void TimelineAudioOutput::setAmbiencePreservation(double value) {
  const auto bounded = bounded_finite_value(value, ambience_preservation_, 0.0, 1.0);
  if (qAbs(ambience_preservation_ - bounded) <= 0.0001) return;
  ambience_preservation_ = bounded;
  emit processingChanged();
}

void TimelineAudioOutput::setDuckingGainDb(double value) {
  const auto bounded = bounded_finite_value(value, ducking_gain_db_, -30.0, 0.0);
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
  for (auto channel = 0; channel < 2; ++channel) {
    high_pass_state_[channel] = {};
    low_pass_state_[channel] = {};
    deesser_crossover_state_[channel] = {};
    deesser_detector_state_[channel] = {};
  }
  compressor_envelope_ = 0.0;
  gate_envelope_ = 0.0;
  gate_gain_ = 1.0;
  deesser_envelope_ = 0.0;
  limiter_gain_[0] = 1.0;
  limiter_gain_[1] = 1.0;
  cleanup_fast_envelope_ = 0.0;
  cleanup_slow_envelope_ = 0.0;
  cleanup_gain_ = 1.0;
}

QVector<double> TimelineAudioOutput::channelGains() const {
  const auto gains = stereoPanGains(pan_, volume_, pan_enabled_);
  return {gains.left, gains.right};
}

QVector<float> TimelineAudioOutput::processStereoSamplesForTesting(
    const QVector<float>& interleaved_samples,
    int sample_rate) {
  return processInterleavedSamplesForTesting(interleaved_samples, 2, sample_rate);
}

QVector<float> TimelineAudioOutput::processInterleavedSamplesForTesting(
    const QVector<float>& interleaved_samples,
    int channel_count,
    int sample_rate) {
  const auto layout = channel_count == 2
                          ? QAudioFormat::ChannelConfigStereo
                          : QAudioFormat::defaultChannelConfigForChannelCount(channel_count);
  return processInterleavedSamplesWithConfigForTesting(
      interleaved_samples, channel_count, layout, sample_rate);
}

QVector<float> TimelineAudioOutput::processInterleavedSamplesWithConfigForTesting(
    const QVector<float>& interleaved_samples,
    int channel_count,
    QAudioFormat::ChannelConfig channel_config,
    int sample_rate) {
  if (channel_count <= 0 || interleaved_samples.size() % channel_count != 0) {
    return {};
  }
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
  auto format = preferred_audio_format();
  format.setSampleRate(qBound(8000, sample_rate, 192000));
  format.setChannelCount(channel_count);
  format.setChannelConfig(channel_config);
  QAudioBuffer buffer(bytes, format, -1);
  auto output_format = format;
  bytes = processAudioBytes(
      buffer, &output_format, &left, &right, &true_peak_left, &true_peak_right,
      &rms_left, &rms_right, &phase_correlation, &phase_points, &clipped);
  publishMeters(
      left, right, true_peak_left, true_peak_right, rms_left, rms_right,
      phase_correlation, phase_points, clipped);
  QVector<float> processed(bytes.size() / static_cast<qsizetype>(sizeof(float)));
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

void TimelineAudioOutput::resetForDiscontinuity() {
  if (pcm_decoder_ != nullptr) pcm_decoder_->stop();
  decoded_playing_ = false;
  output_generation_.fetch_add(1U, std::memory_order_acq_rel);
  resetProcessingState();
  pending_audio_.clear();
  if (sink_ != nullptr) {
    sink_->reset();
    sink_device_.clear();
  }
}

void TimelineAudioOutput::setOutputSuspended(bool suspended) {
  if (output_suspended_ == suspended) {
    return;
  }
  output_suspended_ = suspended;
  resetForDiscontinuity();
  if (suspended) resetMeters();
}

bool TimelineAudioOutput::outputSuspended() const {
  return output_suspended_;
}

void TimelineAudioOutput::synchronizeDecodedAudio(
    const QString& source_url, qint64 source_position_ms, double playback_rate,
    int channels, bool playing) {
  if (pcm_decoder_ == nullptr) return;
  playing = playing && !output_suspended_;
  if (!playing && decoded_playing_) {
    resetForDiscontinuity();
    resetMeters();
  }
  decoded_playing_ = playing;
  pcm_decoder_->synchronize(source_url, source_position_ms, playback_rate,
                            channels, playing);
}

QVariantMap TimelineAudioOutput::decoderDiagnostics() const {
  return {{QStringLiteral("pcmFallbackEnabled"), pcm_decoder_ != nullptr},
          {QStringLiteral("pcmFallbackActive"), pcm_decoder_ != nullptr && pcm_decoder_->active()},
          {QStringLiteral("pcmFallbackDecodedBufferCount"),
           static_cast<qulonglong>(pcm_decoder_ != nullptr ? pcm_decoder_->deliveredBufferCount() : 0)},
          {QStringLiteral("sinkWrittenBytes"), static_cast<qulonglong>(sink_written_bytes_)},
          {QStringLiteral("sinkError"), sink_ != nullptr ? static_cast<int>(sink_->error()) : 0},
          {QStringLiteral("pcmFallbackError"), pcm_decoder_ != nullptr ? pcm_decoder_->error() : QString{}}};
}

void TimelineAudioOutput::processBuffer(
    const QAudioBuffer& buffer,
    quint64 callback_generation) {
  if (callback_generation !=
          output_generation_.load(std::memory_order_acquire) ||
      output_suspended_ || !buffer.isValid() || buffer.byteCount() <= 0) {
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
  auto output_format = buffer.format();
  auto bytes = processAudioBytes(
      buffer, &output_format, &left, &right, &true_peak_left, &true_peak_right,
      &rms_left, &rms_right, &phase_correlation, &phase_points, &clipped);
  publishMeters(
      left, right, true_peak_left, true_peak_right, rms_left, rms_right,
      phase_correlation, phase_points, clipped);
  if (!native_audio_enabled_ || bytes.isEmpty()) {
    return;
  }
  ensureSink(output_format);
  const auto maximum_pending_frames = static_cast<qsizetype>(
      qMax<qint64>(1, output_format.framesForDuration(kMaximumPendingDurationUs)));
  appendBoundedAudioFrames(
      &pending_audio_, bytes, maximum_pending_frames,
      static_cast<qsizetype>(output_format.bytesPerFrame()));
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
  sink_->setBufferFrameCount(qMax(256, format.framesForDuration(kSinkBufferDurationUs)));
  sink_device_ = sink_->start();
  pending_audio_.clear();
}

void TimelineAudioOutput::drainPendingAudio() {
  if (sink_ != nullptr && audioSinkErrorNeedsRecovery(sink_->error())) {
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
  const auto bytes_per_frame = static_cast<qsizetype>(sink_format_.bytesPerFrame());
  if (bytes_per_frame <= 0) {
    return;
  }
  const auto available = qMin<qsizetype>(sink_->bytesFree(), pending_audio_.size());
  const auto writable = available - available % bytes_per_frame;
  if (writable <= 0) {
    return;
  }
  const auto written = sink_device_->write(pending_audio_.constData(), writable);
  if (written > 0) {
    sink_written_bytes_ += static_cast<quint64>(written);
    pending_audio_.remove(0, written);
  }
  if (pending_audio_.isEmpty()) {
    drain_timer_.stop();
  }
}

void TimelineAudioOutput::recoverOutputDevice() {
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
    QAudioFormat* output_format,
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
  auto processing_format = buffer.format();
  const auto source_channel_count = qMax(1, processing_format.channelCount());
  if (source_channel_count != 2) {
    if (processing_format.channelConfig() == QAudioFormat::ChannelConfigUnknown) {
      auto default_layout =
          QAudioFormat::defaultChannelConfigForChannelCount(source_channel_count);
      if (default_layout == QAudioFormat::ChannelConfigUnknown && source_channel_count == 8) {
        default_layout = QAudioFormat::ChannelConfigSurround7Dot1;
      }
      if (default_layout != QAudioFormat::ChannelConfigUnknown) {
        processing_format.setChannelConfig(default_layout);
      }
    }
    switch (processing_format.sampleFormat()) {
      case QAudioFormat::UInt8:
        bytes = stereo_mixdown<quint8>(bytes, processing_format, channel_mode_);
        break;
      case QAudioFormat::Int16:
        bytes = stereo_mixdown<qint16>(bytes, processing_format, channel_mode_);
        break;
      case QAudioFormat::Int32:
        bytes = stereo_mixdown<qint32>(bytes, processing_format, channel_mode_);
        break;
      case QAudioFormat::Float:
        bytes = stereo_mixdown<float>(bytes, processing_format, channel_mode_);
        break;
      case QAudioFormat::Unknown:
      case QAudioFormat::NSampleFormats:
        bytes.clear();
        break;
    }
    processing_format.setChannelCount(2);
    processing_format.setChannelConfig(QAudioFormat::ChannelConfigStereo);
    // Mix into float so a hot surround sum retains headroom for track gain,
    // EQ, dynamics and the explicit final device-output saturation.
    processing_format.setSampleFormat(QAudioFormat::Float);
  }
  if (output_format != nullptr) {
    *output_format = processing_format;
  }
  const auto channel_count = qMax(1, processing_format.channelCount());
  const auto gains = channelGains();
  const auto sample_rate = qMax(8000, processing_format.sampleRate());
  if (processing_sample_rate_ != sample_rate) {
    resetProcessingState();
    processing_sample_rate_ = sample_rate;
  }
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
  const auto low_pass_coefficients = [sample_rate](double frequency) {
    const auto cutoff = qBound(10.0, frequency, static_cast<double>(sample_rate) * 0.45);
    const auto omega = 2.0 * std::numbers::pi * cutoff /
                       static_cast<double>(sample_rate);
    constexpr auto q = 0.7071067811865476;
    const auto alpha = qSin(omega) / (2.0 * q);
    const auto cosine = qCos(omega);
    const auto a0 = 1.0 + alpha;
    return Coefficients{
        ((1.0 - cosine) * 0.5) / a0,
        (1.0 - cosine) / a0,
        ((1.0 - cosine) * 0.5) / a0,
        (-2.0 * cosine) / a0,
        (1.0 - alpha) / a0,
    };
  };
  const auto high_pass_coefficients = [sample_rate](double frequency) {
    const auto cutoff = qBound(10.0, frequency, static_cast<double>(sample_rate) * 0.45);
    const auto omega = 2.0 * std::numbers::pi * cutoff /
                       static_cast<double>(sample_rate);
    constexpr auto q = 0.7071067811865476;
    const auto alpha = qSin(omega) / (2.0 * q);
    const auto cosine = qCos(omega);
    const auto a0 = 1.0 + alpha;
    return Coefficients{
        ((1.0 + cosine) * 0.5) / a0,
        (-(1.0 + cosine)) / a0,
        ((1.0 + cosine) * 0.5) / a0,
        (-2.0 * cosine) / a0,
        (1.0 - alpha) / a0,
    };
  };
  const auto band_pass_coefficients = [sample_rate](double frequency, double q) {
    const auto cutoff = qBound(10.0, frequency, static_cast<double>(sample_rate) * 0.45);
    const auto omega = 2.0 * std::numbers::pi * cutoff /
                       static_cast<double>(sample_rate);
    const auto alpha = qSin(omega) / (2.0 * q);
    const auto cosine = qCos(omega);
    const auto a0 = 1.0 + alpha;
    return Coefficients{
        alpha / a0,
        0.0,
        -alpha / a0,
        (-2.0 * cosine) / a0,
        (1.0 - alpha) / a0,
    };
  };
  const auto high_pass_values = high_pass_coefficients(high_pass_hz_);
  const auto low_pass_values = low_pass_coefficients(low_pass_hz_);
  const auto deesser_crossover_values = low_pass_coefficients(3800.0);
  const auto deesser_detector_values = band_pass_coefficients(6500.0, 0.9);
  const auto apply_biquad = [](double sample, const Coefficients& values, BiquadState* state) {
    const auto output = values.b0 * sample + values.b1 * state->x1 +
                        values.b2 * state->x2 - values.a1 * state->y1 -
                        values.a2 * state->y2;
    const auto finite_output = std::isfinite(output) ? output : 0.0;
    state->x2 = state->x1;
    state->x1 = sample;
    state->y2 = state->y1;
    state->y1 = finite_output;
    return finite_output;
  };
  const auto attack = qExp(-1.0 / (0.020 * static_cast<double>(sample_rate)));
  const auto release = qExp(-1.0 / (0.250 * static_cast<double>(sample_rate)));
  const auto gate_attack = qExp(-1.0 / (0.010 * static_cast<double>(sample_rate)));
  const auto gate_release = qExp(-1.0 / (0.250 * static_cast<double>(sample_rate)));
  const auto deesser_attack = qExp(-1.0 / (0.005 * static_cast<double>(sample_rate)));
  const auto deesser_release = qExp(-1.0 / (0.050 * static_cast<double>(sample_rate)));
  const auto limiter_attack = qExp(-1.0 / (0.005 * static_cast<double>(sample_rate)));
  const auto limiter_release = qExp(-1.0 / (0.050 * static_cast<double>(sample_rate)));
  const auto cleanup_fast_attack =
      qExp(-1.0 / (0.004 * static_cast<double>(sample_rate)));
  const auto cleanup_fast_release =
      qExp(-1.0 / (0.055 * static_cast<double>(sample_rate)));
  const auto cleanup_slow =
      qExp(-1.0 / (0.420 * static_cast<double>(sample_rate)));
  const auto cleanup_gain_smoothing = qExp(
      -1.0 /
      ((0.003 + 0.080 * artifact_smoothing_) * static_cast<double>(sample_rate)));
  const auto processor = [this, &coefficients, &hum_coefficients,
                          &high_pass_values, &low_pass_values,
                          &deesser_crossover_values, &deesser_detector_values,
                          &apply_biquad, attack, release, gate_attack,
                          gate_release, deesser_attack, deesser_release,
                          limiter_attack, limiter_release, cleanup_fast_attack,
                          cleanup_fast_release, cleanup_slow,
                          cleanup_gain_smoothing](
                             double* left,
                             double* right) {
    if (processing_enabled_ && dialogue_cleanup_enabled_) {
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

    if (processing_enabled_ && high_pass_enabled_) {
      *left = apply_biquad(*left, high_pass_values, &high_pass_state_[0]);
      *right = apply_biquad(*right, high_pass_values, &high_pass_state_[1]);
    }
    if (processing_enabled_ && low_pass_enabled_) {
      *left = apply_biquad(*left, low_pass_values, &low_pass_state_[0]);
      *right = apply_biquad(*right, low_pass_values, &low_pass_state_[1]);
    }
    if (processing_enabled_ && deesser_enabled_ && deesser_amount_ > 0.0) {
      const auto detector_left = apply_biquad(
          *left, deesser_detector_values, &deesser_detector_state_[0]);
      const auto detector_right = apply_biquad(
          *right, deesser_detector_values, &deesser_detector_state_[1]);
      const auto detector_peak = qMax(qAbs(detector_left), qAbs(detector_right));
      const auto detector_smoothing = detector_peak > deesser_envelope_
                                          ? deesser_attack
                                          : deesser_release;
      deesser_envelope_ = detector_smoothing * deesser_envelope_ +
                          (1.0 - detector_smoothing) * detector_peak;
      const auto detector_db = 20.0 * qLn(qMax(1.0e-9, deesser_envelope_)) /
                               qLn(10.0);
      const auto sibilance = qBound(0.0, (detector_db + 32.0) / 20.0, 1.0);
      const auto reduction_db = deesser_amount_ * 9.0 * sibilance;
      const auto high_frequency_gain = qPow(10.0, -reduction_db / 20.0);
      const auto low_left = apply_biquad(
          *left, deesser_crossover_values, &deesser_crossover_state_[0]);
      const auto low_right = apply_biquad(
          *right, deesser_crossover_values, &deesser_crossover_state_[1]);
      *left = low_left + (*left - low_left) * high_frequency_gain;
      *right = low_right + (*right - low_right) * high_frequency_gain;
    }
    if (processing_enabled_ && gate_enabled_) {
      const auto input_peak = qMax(qAbs(*left), qAbs(*right));
      const auto envelope_smoothing = input_peak > gate_envelope_
                                          ? gate_attack
                                          : gate_release;
      gate_envelope_ = envelope_smoothing * gate_envelope_ +
                       (1.0 - envelope_smoothing) * input_peak;
      const auto input_db = 20.0 * qLn(qMax(1.0e-9, gate_envelope_)) / qLn(10.0);
      auto target_gain = 1.0;
      if (input_db < gate_threshold_db_) {
        const auto expanded_db = gate_threshold_db_ +
                                 (input_db - gate_threshold_db_) * 10.0;
        target_gain = qPow(10.0, qMax(-60.0, expanded_db - input_db) / 20.0);
      }
      const auto gain_smoothing = target_gain > gate_gain_ ? gate_attack : gate_release;
      gate_gain_ = gain_smoothing * gate_gain_ + (1.0 - gain_smoothing) * target_gain;
      *left *= gate_gain_;
      *right *= gate_gain_;
    }

    if (processing_enabled_ && equalizer_enabled_) {
      for (auto band = 0; band < 3; ++band) {
        *left = apply_biquad(*left, coefficients[band], &eq_state_[0][band]);
        *right = apply_biquad(*right, coefficients[band], &eq_state_[1][band]);
      }
    }
    auto gain_db = ducking_gain_db_;
    if (processing_enabled_ && compressor_enabled_) {
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
    if (processing_enabled_ && limiter_enabled_) {
      const auto output_peak = qMax(qAbs(*left), qAbs(*right));
      const auto ceiling = qPow(10.0, limiter_ceiling_db_ / 20.0);
      const auto requested_gain = output_peak > ceiling ? ceiling / output_peak : 1.0;
      const auto smoothing = requested_gain < limiter_gain_[0]
                                 ? limiter_attack
                                 : limiter_release;
      auto smoothed_gain = smoothing * limiter_gain_[0] +
                           (1.0 - smoothing) * requested_gain;
      smoothed_gain = qMin(smoothed_gain, requested_gain);
      limiter_gain_[0] = smoothed_gain;
      limiter_gain_[1] = smoothed_gain;
      *left *= smoothed_gain;
      *right *= smoothed_gain;
    }
  };
  switch (processing_format.sampleFormat()) {
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
