#pragma once

#include <QAudioBuffer>
#include <QAudioFormat>
#include <QMediaDevices>
#include <QMetaObject>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>
#include <QtQml/qqmlregistration.h>

#include <atomic>
#include <memory>
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
#include <QAudioBufferOutput>
#else
#include <QAudioOutput>
#endif

class QAudioSink;
class QIODevice;

namespace photo_editor::desktop {

class TimelinePcmAudioDecoder;

class TimelineAudioOutput : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool pcmFallbackEnabled READ pcmFallbackEnabled CONSTANT)
  QML_ELEMENT
  Q_PROPERTY(double volume READ volume WRITE setVolume NOTIFY volumeChanged)
  Q_PROPERTY(double pan READ pan WRITE setPan NOTIFY panChanged)
  Q_PROPERTY(bool panEnabled READ panEnabled WRITE setPanEnabled NOTIFY panEnabledChanged)
  Q_PROPERTY(QString channelMode READ channelMode WRITE setChannelMode NOTIFY channelMappingChanged)
  Q_PROPERTY(bool swapChannels READ swapChannels WRITE setSwapChannels NOTIFY channelMappingChanged)
  Q_PROPERTY(bool invertLeftPhase READ invertLeftPhase WRITE setInvertLeftPhase NOTIFY channelMappingChanged)
  Q_PROPERTY(bool invertRightPhase READ invertRightPhase WRITE setInvertRightPhase NOTIFY channelMappingChanged)
  Q_PROPERTY(double eqLowDb READ eqLowDb WRITE setEqLowDb NOTIFY processingChanged)
  Q_PROPERTY(double eqMidDb READ eqMidDb WRITE setEqMidDb NOTIFY processingChanged)
  Q_PROPERTY(double eqHighDb READ eqHighDb WRITE setEqHighDb NOTIFY processingChanged)
  Q_PROPERTY(bool equalizerEnabled READ equalizerEnabled WRITE setEqualizerEnabled NOTIFY processingChanged)
  Q_PROPERTY(bool compressorEnabled READ compressorEnabled WRITE setCompressorEnabled NOTIFY processingChanged)
  Q_PROPERTY(double compressorThresholdDb READ compressorThresholdDb WRITE setCompressorThresholdDb NOTIFY processingChanged)
  Q_PROPERTY(double compressorRatio READ compressorRatio WRITE setCompressorRatio NOTIFY processingChanged)
  Q_PROPERTY(double compressorMakeupDb READ compressorMakeupDb WRITE setCompressorMakeupDb NOTIFY processingChanged)
  Q_PROPERTY(bool limiterEnabled READ limiterEnabled WRITE setLimiterEnabled NOTIFY processingChanged)
  Q_PROPERTY(double limiterCeilingDb READ limiterCeilingDb WRITE setLimiterCeilingDb NOTIFY processingChanged)
  Q_PROPERTY(bool gateEnabled READ gateEnabled WRITE setGateEnabled NOTIFY processingChanged)
  Q_PROPERTY(double gateThresholdDb READ gateThresholdDb WRITE setGateThresholdDb NOTIFY processingChanged)
  Q_PROPERTY(bool deEsserEnabled READ deEsserEnabled WRITE setDeEsserEnabled NOTIFY processingChanged)
  Q_PROPERTY(double deEsserAmount READ deEsserAmount WRITE setDeEsserAmount NOTIFY processingChanged)
  Q_PROPERTY(bool highPassEnabled READ highPassEnabled WRITE setHighPassEnabled NOTIFY processingChanged)
  Q_PROPERTY(double highPassHz READ highPassHz WRITE setHighPassHz NOTIFY processingChanged)
  Q_PROPERTY(bool lowPassEnabled READ lowPassEnabled WRITE setLowPassEnabled NOTIFY processingChanged)
  Q_PROPERTY(double lowPassHz READ lowPassHz WRITE setLowPassHz NOTIFY processingChanged)
  Q_PROPERTY(bool processingEnabled READ processingEnabled WRITE setProcessingEnabled NOTIFY processingChanged)
  Q_PROPERTY(bool dialogueCleanupEnabled READ dialogueCleanupEnabled WRITE setDialogueCleanupEnabled NOTIFY processingChanged)
  Q_PROPERTY(double voiceIsolationAmount READ voiceIsolationAmount WRITE setVoiceIsolationAmount NOTIFY processingChanged)
  Q_PROPERTY(double noiseReductionAmount READ noiseReductionAmount WRITE setNoiseReductionAmount NOTIFY processingChanged)
  Q_PROPERTY(double dereverbAmount READ dereverbAmount WRITE setDereverbAmount NOTIFY processingChanged)
  Q_PROPERTY(double humRemovalAmount READ humRemovalAmount WRITE setHumRemovalAmount NOTIFY processingChanged)
  Q_PROPERTY(int humFrequencyHz READ humFrequencyHz WRITE setHumFrequencyHz NOTIFY processingChanged)
  Q_PROPERTY(double artifactSmoothing READ artifactSmoothing WRITE setArtifactSmoothing NOTIFY processingChanged)
  Q_PROPERTY(double ambiencePreservation READ ambiencePreservation WRITE setAmbiencePreservation NOTIFY processingChanged)
  Q_PROPERTY(double duckingGainDb READ duckingGainDb WRITE setDuckingGainDb NOTIFY processingChanged)
  Q_PROPERTY(double peakLeft READ peakLeft NOTIFY meterChanged)
  Q_PROPERTY(double peakRight READ peakRight NOTIFY meterChanged)
  Q_PROPERTY(double truePeakLeft READ truePeakLeft NOTIFY meterChanged)
  Q_PROPERTY(double truePeakRight READ truePeakRight NOTIFY meterChanged)
  Q_PROPERTY(double rmsLeft READ rmsLeft NOTIFY meterChanged)
  Q_PROPERTY(double rmsRight READ rmsRight NOTIFY meterChanged)
  Q_PROPERTY(bool clipping READ clipping NOTIFY meterChanged)
  Q_PROPERTY(double phaseCorrelation READ phaseCorrelation NOTIFY meterChanged)
  Q_PROPERTY(QVariantList phasePoints READ phasePoints NOTIFY meterChanged)

 public:
  explicit TimelineAudioOutput(QObject* parent = nullptr);
  ~TimelineAudioOutput() override;

#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
  [[nodiscard]] QAudioBufferOutput* bufferOutput();
#endif
  [[nodiscard]] QAudioFormat bufferFormat() const;
  Q_INVOKABLE bool attachPlayer(QObject* player);
  Q_INVOKABLE void setPlayerPitchCompensation(QObject* player, bool enabled);
  [[nodiscard]] double volume() const;
  [[nodiscard]] double pan() const;
  [[nodiscard]] bool panEnabled() const;
  [[nodiscard]] QString channelMode() const;
  [[nodiscard]] bool swapChannels() const;
  [[nodiscard]] bool invertLeftPhase() const;
  [[nodiscard]] bool invertRightPhase() const;
  [[nodiscard]] double eqLowDb() const;
  [[nodiscard]] double eqMidDb() const;
  [[nodiscard]] double eqHighDb() const;
  [[nodiscard]] bool equalizerEnabled() const;
  [[nodiscard]] bool compressorEnabled() const;
  [[nodiscard]] double compressorThresholdDb() const;
  [[nodiscard]] double compressorRatio() const;
  [[nodiscard]] double compressorMakeupDb() const;
  [[nodiscard]] bool limiterEnabled() const;
  [[nodiscard]] double limiterCeilingDb() const;
  [[nodiscard]] bool gateEnabled() const;
  [[nodiscard]] double gateThresholdDb() const;
  [[nodiscard]] bool deEsserEnabled() const;
  [[nodiscard]] double deEsserAmount() const;
  [[nodiscard]] bool highPassEnabled() const;
  [[nodiscard]] double highPassHz() const;
  [[nodiscard]] bool lowPassEnabled() const;
  [[nodiscard]] double lowPassHz() const;
  [[nodiscard]] bool processingEnabled() const;
  [[nodiscard]] bool dialogueCleanupEnabled() const;
  [[nodiscard]] double voiceIsolationAmount() const;
  [[nodiscard]] double noiseReductionAmount() const;
  [[nodiscard]] double dereverbAmount() const;
  [[nodiscard]] double humRemovalAmount() const;
  [[nodiscard]] int humFrequencyHz() const;
  [[nodiscard]] double artifactSmoothing() const;
  [[nodiscard]] double ambiencePreservation() const;
  [[nodiscard]] double duckingGainDb() const;
  [[nodiscard]] double peakLeft() const;
  [[nodiscard]] double peakRight() const;
  [[nodiscard]] double truePeakLeft() const;
  [[nodiscard]] double truePeakRight() const;
  [[nodiscard]] double rmsLeft() const;
  [[nodiscard]] double rmsRight() const;
  [[nodiscard]] bool clipping() const;
  [[nodiscard]] double phaseCorrelation() const;
  [[nodiscard]] QVariantList phasePoints() const;

  void setVolume(double volume);
  void setPan(double pan);
  void setPanEnabled(bool enabled);
  void setChannelMode(const QString& mode);
  void setSwapChannels(bool enabled);
  void setInvertLeftPhase(bool enabled);
  void setInvertRightPhase(bool enabled);
  void setEqLowDb(double value);
  void setEqMidDb(double value);
  void setEqHighDb(double value);
  void setEqualizerEnabled(bool enabled);
  void setCompressorEnabled(bool enabled);
  void setCompressorThresholdDb(double value);
  void setCompressorRatio(double value);
  void setCompressorMakeupDb(double value);
  void setLimiterEnabled(bool enabled);
  void setLimiterCeilingDb(double value);
  void setGateEnabled(bool enabled);
  void setGateThresholdDb(double value);
  void setDeEsserEnabled(bool enabled);
  void setDeEsserAmount(double value);
  void setHighPassEnabled(bool enabled);
  void setHighPassHz(double value);
  void setLowPassEnabled(bool enabled);
  void setLowPassHz(double value);
  void setProcessingEnabled(bool enabled);
  void setDialogueCleanupEnabled(bool enabled);
  void setVoiceIsolationAmount(double value);
  void setNoiseReductionAmount(double value);
  void setDereverbAmount(double value);
  void setHumRemovalAmount(double value);
  void setHumFrequencyHz(int value);
  void setArtifactSmoothing(double value);
  void setAmbiencePreservation(double value);
  void setDuckingGainDb(double value);

  [[nodiscard]] QVector<double> channelGains() const;
  [[nodiscard]] QVector<float> processStereoSamplesForTesting(
      const QVector<float>& interleaved_samples,
      int sample_rate = 48000);
  [[nodiscard]] QVector<float> processInterleavedSamplesForTesting(
      const QVector<float>& interleaved_samples,
      int channel_count,
      int sample_rate = 48000);
  [[nodiscard]] QVector<float> processInterleavedSamplesWithConfigForTesting(
      const QVector<float>& interleaved_samples,
      int channel_count,
      QAudioFormat::ChannelConfig channel_config,
      int sample_rate = 48000);
  Q_INVOKABLE void resetMeters();
  Q_INVOKABLE void resetForDiscontinuity();
  void setOutputSuspended(bool suspended);
  [[nodiscard]] bool outputSuspended() const;
  void recoverOutputDevice();
  [[nodiscard]] bool pcmFallbackEnabled() const { return pcm_decoder_ != nullptr; }
  Q_INVOKABLE void synchronizeDecodedAudio(const QString& source_url, qint64 source_position_ms,
                               double playback_rate, int channels, bool playing);
  Q_INVOKABLE QVariantMap decoderDiagnostics() const;

 signals:
  void volumeChanged();
  void panChanged();
  void panEnabledChanged();
  void channelMappingChanged();
  void processingChanged();
  void meterChanged();
  void outputDeviceRecoveryRequested(const QString& detail);

 private:
  void processBuffer(const QAudioBuffer& buffer, quint64 callback_generation);
  void ensureSink(const QAudioFormat& format);
  void drainPendingAudio();
  void publishMeters(
      double left,
      double right,
      double true_peak_left,
      double true_peak_right,
      double rms_left,
      double rms_right,
      double phase_correlation,
      const QVariantList& phase_points,
      bool clipping);
  [[nodiscard]] QByteArray processAudioBytes(
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
      bool* clipping);
  void resetProcessingState();

  struct BiquadState final {
    double x1 = 0.0;
    double x2 = 0.0;
    double y1 = 0.0;
    double y2 = 0.0;
  };

#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
  QAudioBufferOutput buffer_output_;
#else
  QAudioOutput playback_output_;
#endif
  std::unique_ptr<TimelinePcmAudioDecoder> pcm_decoder_;
  bool decoded_playing_ = false;
  QMetaObject::Connection buffer_output_connection_;
  QMediaDevices media_devices_;
  QPointer<QAudioSink> sink_;
  QPointer<QIODevice> sink_device_;
  QAudioFormat sink_format_;
  QByteArray pending_audio_;
  quint64 sink_written_bytes_ = 0;
  std::atomic<quint64> output_generation_{0U};
  int processing_sample_rate_ = 0;
  QTimer drain_timer_;
  QTimer meter_decay_timer_;
  double volume_ = 1.0;
  double pan_ = 0.0;
  bool pan_enabled_ = true;
  QString channel_mode_ = QStringLiteral("source");
  bool swap_channels_ = false;
  bool invert_left_phase_ = false;
  bool invert_right_phase_ = false;
  double eq_low_db_ = 0.0;
  double eq_mid_db_ = 0.0;
  double eq_high_db_ = 0.0;
  bool equalizer_enabled_ = true;
  bool compressor_enabled_ = false;
  double compressor_threshold_db_ = -18.0;
  double compressor_ratio_ = 4.0;
  double compressor_makeup_db_ = 0.0;
  bool limiter_enabled_ = false;
  double limiter_ceiling_db_ = -1.0;
  bool gate_enabled_ = false;
  double gate_threshold_db_ = -48.0;
  bool deesser_enabled_ = false;
  double deesser_amount_ = 0.35;
  bool high_pass_enabled_ = false;
  double high_pass_hz_ = 80.0;
  bool low_pass_enabled_ = false;
  double low_pass_hz_ = 16000.0;
  bool processing_enabled_ = true;
  bool dialogue_cleanup_enabled_ = false;
  double voice_isolation_amount_ = 0.65;
  double noise_reduction_amount_ = 0.35;
  double dereverb_amount_ = 0.25;
  double hum_removal_amount_ = 0.50;
  int hum_frequency_hz_ = 60;
  double artifact_smoothing_ = 0.65;
  double ambience_preservation_ = 0.35;
  double ducking_gain_db_ = 0.0;
  BiquadState eq_state_[2][3];
  BiquadState hum_state_[2][2];
  BiquadState high_pass_state_[2];
  BiquadState low_pass_state_[2];
  BiquadState deesser_crossover_state_[2];
  BiquadState deesser_detector_state_[2];
  double compressor_envelope_ = 0.0;
  double gate_envelope_ = 0.0;
  double gate_gain_ = 1.0;
  double deesser_envelope_ = 0.0;
  double limiter_gain_[2] = {1.0, 1.0};
  double cleanup_fast_envelope_ = 0.0;
  double cleanup_slow_envelope_ = 0.0;
  double cleanup_gain_ = 1.0;
  double peak_left_ = 0.0;
  double peak_right_ = 0.0;
  double true_peak_left_ = 0.0;
  double true_peak_right_ = 0.0;
  double rms_left_ = 0.0;
  double rms_right_ = 0.0;
  double phase_correlation_ = 0.0;
  QVariantList phase_points_;
  bool clipping_ = false;
  int clipping_hold_ticks_ = 0;
  bool native_audio_enabled_ = true;
  bool output_suspended_ = false;
};

}  // namespace photo_editor::desktop
