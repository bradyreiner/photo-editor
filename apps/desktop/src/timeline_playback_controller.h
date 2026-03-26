#pragma once

#include <QObject>
#include <QPointer>
#include <QString>

#include <QtMultimedia/QAudioOutput>
#include <QtMultimedia/QMediaPlayer>

namespace photo_editor::desktop {

class TimelineDocumentController;

class TimelinePlaybackController final : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool hasVideoSource READ hasVideoSource NOTIFY stateChanged)
  Q_PROPERTY(bool mediaReady READ mediaReady NOTIFY stateChanged)
  Q_PROPERTY(QString errorString READ errorString NOTIFY stateChanged)

 public:
  explicit TimelinePlaybackController(TimelineDocumentController* timeline_document, QObject* parent = nullptr);

  [[nodiscard]] bool hasVideoSource() const;
  [[nodiscard]] bool mediaReady() const;
  [[nodiscard]] QString errorString() const;

  Q_INVOKABLE void attachVideoOutput(QObject* output);
  Q_INVOKABLE void play();
  Q_INVOKABLE void pause();
  Q_INVOKABLE void togglePlayback();
  Q_INVOKABLE void stepFrames(int direction);
  Q_INVOKABLE void seekSequenceSeconds(double seconds);
  Q_INVOKABLE void seekCurrentClipSeconds(double seconds);

 signals:
  void stateChanged();

 private:
  void syncToDocument();
  void setErrorString(const QString& error_string);

  QPointer<TimelineDocumentController> timeline_document_;
  QMediaPlayer player_;
  QAudioOutput audio_output_;
  QString current_source_url_;
  QString error_string_;
  qint64 pending_position_ms_ = -1;
  bool play_when_ready_ = false;
};

}  // namespace photo_editor::desktop
