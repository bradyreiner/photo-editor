#pragma once
#include <QObject>
#include <QElapsedTimer>
#include <QFutureWatcher>
#include <QPointer>
#include <QProcess>
#include <QQuickItem>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>
#include <QVariantMap>
#include <QJsonArray>
#include <memory>
#include <vector>
#include <QtQml/qqmlregistration.h>

namespace photo_editor::desktop {
class SceneVideoRecorder : public QObject {
  Q_OBJECT
  QML_NAMED_ELEMENT(SceneVideoRecorder)
  Q_PROPERTY(bool recording READ recording NOTIFY changed)
  Q_PROPERTY(bool busy READ busy NOTIFY changed)
  Q_PROPERTY(double seconds READ seconds NOTIFY changed)
  Q_PROPERTY(QString error READ error NOTIFY changed)
  Q_PROPERTY(QUrl outputUrl READ outputUrl NOTIFY changed)
  Q_PROPERTY(QStringList outputFiles READ outputFiles NOTIFY changed)
  Q_PROPERTY(QVariantMap status READ status NOTIFY changed)
 public:
  explicit SceneVideoRecorder(QObject* parent = nullptr);
  ~SceneVideoRecorder() override;
  bool recording() const { return recording_; }
  bool busy() const { return busy_; }
  double seconds() const { return double(elapsed_ms_) / 1000; }
  QString error() const { return error_; }
  QUrl outputUrl() const { return output_url_; }
  QStringList outputFiles() const { return output_files_; }
  QVariantMap status() const;
  Q_INVOKABLE bool start(QQuickItem* viewport, const QString& project);
  Q_INVOKABLE void stop();
  Q_INVOKABLE void cancel();
 signals:
  void changed();
 private:
  void tick();
  void capture();
  void finalize();
  void encodeLayer();
  void storeLayers(const QVariantMap& frame);
  void fail(const QString& error);
  void cleanup();
  QPointer<QQuickItem> viewport_;
  QProcess encoder_;
  QFutureWatcher<QString> writer_;
  QTimer timer_;
  QElapsedTimer clock_;
  std::unique_ptr<QTemporaryDir> staging_;
  QSize size_;
  QByteArray diagnostics_;
  QString error_, destination_, temporary_, executable_;
  QStringList layer_names_, output_files_;
  QString recording_directory_;
  QJsonArray camera_samples_;
  int layer_index_ = 0;
  bool layered_ = false;
  QUrl output_url_;
  std::vector<qint64> timestamps_;
  qint64 elapsed_ms_ = 0, last_capture_ms_ = 0, finishing_at_ = 0, pending_timestamp_ = 0;
  quint64 generation_ = 0;
  bool recording_ = false, busy_ = false, capturing_ = false, writing_ = false, encoding_ = false;
};
}
