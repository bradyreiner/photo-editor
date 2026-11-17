#pragma once

#include <QAudioFormat>
#include <QElapsedTimer>
#include <QFile>
#include <QMediaDevices>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <QString>

class QAudioDevice;
class QAudioSource;
class QIODevice;

namespace photo_editor::desktop {

class TimelineVoiceoverWaveWriter final {
 public:
  [[nodiscard]] bool begin(
      const QString& directory,
      int sample_rate,
      int channel_count);
  [[nodiscard]] bool appendPcm16Frames(const QByteArray& interleaved_pcm);
  [[nodiscard]] bool finalize(QString* output_path);
  [[nodiscard]] bool preservePartial();
  void discard();

  [[nodiscard]] QString partialPath() const;
  [[nodiscard]] QString finalPath() const;
  [[nodiscard]] qint64 frameCount() const;
  [[nodiscard]] qint64 durationMs() const;
  [[nodiscard]] qint64 dataByteCount() const;

 private:
  [[nodiscard]] bool updateHeader();

  QFile file_;
  QString partial_path_;
  QString final_path_;
  int sample_rate_ = 0;
  int channel_count_ = 0;
  qint64 data_byte_count_ = 0;
};

class TimelineVoiceoverRecorder : public QObject {
  Q_OBJECT
  QML_ELEMENT
  Q_PROPERTY(QVariantList inputDevices READ inputDevices NOTIFY inputDevicesChanged)
  Q_PROPERTY(QString selectedInputDeviceId READ selectedInputDeviceId WRITE setSelectedInputDeviceId NOTIFY selectedInputDeviceIdChanged)
  Q_PROPERTY(QString state READ state NOTIFY stateChanged)
  Q_PROPERTY(int preRollRemainingMs READ preRollRemainingMs NOTIFY stateChanged)
  Q_PROPERTY(qint64 recordedDurationMs READ recordedDurationMs NOTIFY stateChanged)
  Q_PROPERTY(QString errorString READ errorString NOTIFY stateChanged)
  Q_PROPERTY(QString recoverablePath READ recoverablePath NOTIFY stateChanged)
  Q_PROPERTY(QString captureTimelineIdentity READ captureTimelineIdentity NOTIFY stateChanged)
  Q_PROPERTY(QString captureDocumentPath READ captureDocumentPath NOTIFY stateChanged)

 public:
  explicit TimelineVoiceoverRecorder(QObject* parent = nullptr);
  ~TimelineVoiceoverRecorder() override;

  [[nodiscard]] QVariantList inputDevices() const;
  [[nodiscard]] QString selectedInputDeviceId() const;
  [[nodiscard]] QString state() const;
  [[nodiscard]] int preRollRemainingMs() const;
  [[nodiscard]] qint64 recordedDurationMs() const;
  [[nodiscard]] QString errorString() const;
  [[nodiscard]] QString recoverablePath() const;
  [[nodiscard]] QString captureTimelineIdentity() const;
  [[nodiscard]] QString captureDocumentPath() const;

  void setSelectedInputDeviceId(const QString& device_id);

  Q_INVOKABLE void refreshInputDevices();
  Q_INVOKABLE bool startRecording(
      const QString& project_document_path,
      int pre_roll_ms = 3000,
      const QString& timeline_identity = QString());
  Q_INVOKABLE bool stopRecording();
  Q_INVOKABLE void cancelRecording();
  Q_INVOKABLE void stopForTimelineChange(
      const QString& active_timeline_identity,
      const QString& active_document_path);
  Q_INVOKABLE void reportInsertionFailure(
      const QString& file_path,
      const QString& detail);

 signals:
  void inputDevicesChanged();
  void selectedInputDeviceIdChanged();
  void stateChanged();
  void recordingStarted();
  void recordingFinished(const QString& file_path, qint64 duration_ms);
  void recordingCancelled(const QString& recoverable_path);
  void recordingFailed(const QString& detail, const QString& recoverable_path);

 private:
  [[nodiscard]] QAudioDevice selectedDevice() const;
  void beginCapture();
  [[nodiscard]] bool startRecordingAfterPermission(
      const QString& project_document_path,
      int pre_roll_ms,
      const QString& timeline_identity);
  void setStartError(const QString& detail);
  void consumeAvailableAudio();
  [[nodiscard]] bool appendCapturedBytes(const QByteArray& input_bytes);
  void stopCapture();
  void failRecording(const QString& detail);
  [[nodiscard]] QString recordingDirectory(const QString& project_document_path) const;

  QMediaDevices media_devices_;
  QVariantList input_devices_;
  QString selected_input_device_id_;
  QString state_ = QStringLiteral("idle");
  QString error_string_;
  QString recoverable_path_;
  QString capture_timeline_identity_;
  QString capture_document_path_;
  QString pending_project_document_path_;
  QString pending_timeline_identity_;
  int pending_pre_roll_ms_ = 0;
  quint64 permission_request_generation_ = 0;
  int pre_roll_total_ms_ = 0;
  int pre_roll_remaining_ms_ = 0;
  QTimer pre_roll_timer_;
  QElapsedTimer pre_roll_clock_;
  QAudioFormat capture_format_;
  QPointer<QAudioSource> source_;
  QPointer<QIODevice> capture_device_;
  QByteArray incomplete_input_frame_;
  QString capture_error_;
  TimelineVoiceoverWaveWriter writer_;
};

}  // namespace photo_editor::desktop
