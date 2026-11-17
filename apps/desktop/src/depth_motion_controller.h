#pragma once

#include <QObject>
#include <QProcess>
#include <QTemporaryDir>
#include <QTimer>
#include <QVariantMap>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

#include <memory>

namespace photo_editor::desktop {

class DepthMotionController : public QObject {
  Q_OBJECT
  QML_NAMED_ELEMENT(DepthMotionController)
  Q_PROPERTY(QVariantMap snapshot READ snapshot NOTIFY changed)
  Q_PROPERTY(bool editingAllowed MEMBER editing_allowed_ NOTIFY changed)
 public:
  explicit DepthMotionController(QObject* parent = nullptr);
  ~DepthMotionController() override;
  QVariantMap snapshot() const;
  Q_INVOKABLE bool install();
  Q_INVOKABLE void inspectSource(const QString& source);
  Q_INVOKABLE QUrl localMediaUrl(const QString& source) const;
  Q_INVOKABLE bool generate(const QString& source, const QString& depth_map, const QVariantMap& settings);
  Q_INVOKABLE QVariantMap openProject(const QString& path);
  Q_INVOKABLE bool preparePointCloud();
  Q_INVOKABLE bool prepareDepthVideo();
  Q_INVOKABLE bool savePointCloudView(const QVariantMap& view);
  Q_INVOKABLE bool exportFile(const QString& kind, const QString& destination);
  Q_INVOKABLE void cancel();
 signals:
  void changed();
 private:
  bool start(const QString& python, const QStringList& arguments);
  QString worker();
  QString runtimePython() const;
  void readOutput();
  void finish(int code, QProcess::ExitStatus status);
  void startPendingSourceProbe();
  void publishSourceProbeFailure(const QString& message, quint64 generation);
  bool fail(const QString& message);
  QString home_, project_, cache_, operation_, pending_project_, error_, status_;
  QVariantMap recipe_;
  QVariantMap source_info_;
  QString pending_source_probe_path_, pending_source_probe_program_;
  double progress_ = 0;
  bool busy_ = false, canceled_ = false, completed_ = false;
  bool editing_allowed_ = true;
  bool source_probe_pending_ = false;
  quint64 source_probe_generation_ = 0, active_source_probe_generation_ = 0;
  quint64 worker_generation_ = 0;
  qint64 worker_process_group_ = 0, source_probe_process_group_ = 0;
  QByteArray buffer_, diagnostics_;
  QProcess* process_ = nullptr;
  QProcess* source_probe_ = nullptr;
  QTimer source_probe_timeout_;
  std::shared_ptr<QTemporaryDir> scratch_;
};

}  // namespace photo_editor::desktop
