#pragma once

#include <QAudioBuffer>
#include <QAudioBufferOutput>
#include <QAudioFormat>
#include <QMediaDevices>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>
#include <QVariantList>
#include <QVector>
#include <QtQml/qqmlregistration.h>

class QAudioSink;
class QIODevice;

namespace photo_editor::desktop {

class TimelineAudioOutput : public QObject {
  Q_OBJECT
  QML_ELEMENT
  Q_PROPERTY(QAudioBufferOutput* bufferOutput READ bufferOutput CONSTANT)
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
  Q_PROPERTY(bool compressorEnabled READ compressorEnabled WRITE setCompressorEnabled NOTIFY processingChanged)
  Q_PROPERTY(double compressorThresholdDb READ compressorThresholdDb WRITE setCompressorThresholdDb NOTIFY processingChanged)
  Q_PROPERTY(double compressorRatio READ compressorRatio WRITE setCompressorRatio NOTIFY processingChanged)
  Q_PROPERTY(double compressorMakeupDb READ compressorMakeupDb WRITE setCompressorMakeupDb NOTIFY processingChanged)
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

  [[nodiscard]] QAudioBufferOutput* bufferOutput();
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
  [[nodiscard]] bool compressorEnabled() const;
  [[nodiscard]] double compressorThresholdDb() const;
  [[nodiscard]] double compressorRatio() const;
  [[nodiscard]] double compressorMakeupDb() const;
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
  void setCompressorEnabled(bool enabled);
  void setCompressorThresholdDb(double value);
  void setCompressorRatio(double value);
  void setCompressorMakeupDb(double value);
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
      const QVector<float>& interleaved_samples);
  Q_INVOKABLE void resetMeters();
  void recoverOutputDevice();

 signals:
  void volumeChanged();
  void panChanged();
  void panEnabledChanged();
  void channelMappingChanged();
  void processingChanged();
  void meterChanged();
  void outputDeviceRecoveryRequested(const QString& detail);

 private:
  void processBuffer(const QAudioBuffer& buffer);
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

  QAudioBufferOutput buffer_output_;
  QMediaDevices media_devices_;
  QPointer<QAudioSink> sink_;
  QPointer<QIODevice> sink_device_;
  QAudioFormat sink_format_;
  QByteArray pending_audio_;
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
  bool compressor_enabled_ = false;
  double compressor_threshold_db_ = -18.0;
  double compressor_ratio_ = 4.0;
  double compressor_makeup_db_ = 0.0;
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
  double compressor_envelope_ = 0.0;
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
};

}  // namespace photo_editor::desktop
