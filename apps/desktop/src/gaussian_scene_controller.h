#pragma once

#include "customer_edit_access.h"

#include <QObject>
#include <QFutureWatcher>
#include <QProcess>
#include <QTimer>
#include <QVariantMap>
#include <atomic>
#include <functional>
#include <memory>

namespace photo_editor::desktop {
struct GaussianProcessLog;

class GaussianSceneController final : public QObject {
  Q_OBJECT
  Q_PROPERTY(QVariantMap snapshot READ snapshot NOTIFY changed)
 public:
  void setEditAccessPolicy(std::function<bool()> policy) { customer_edit_access_.setPolicy(std::move(policy)); }

  using CapturePreparer = std::function<QVariantMap(const QStringList&, const QString&, int,
                                                   const std::shared_ptr<std::atomic_bool>&)>;
  using VideoPreparer = std::function<QVariantMap(const QString&, const QString&, int, int,
      const QString&, const QString&, const std::shared_ptr<std::atomic_bool>&,
      const std::shared_ptr<GaussianProcessLog>&)>;
  using TimedCapturePreparer = std::function<QVariantMap(const QString&, const QString&, int,
                                                        const std::shared_ptr<std::atomic_bool>&)>;
  using TimedCaptureValidator = std::function<QVariantMap(const QString&, const std::shared_ptr<std::atomic_bool>&)>;
  using SpacetimeValidator = std::function<QVariantMap(const QString&, const QString&, const std::shared_ptr<std::atomic_bool>&)>;
  explicit GaussianSceneController(QObject* parent = nullptr);
  ~GaussianSceneController() override;
  void setCapturePreparer(CapturePreparer prepare);
  void setVideoPreparer(VideoPreparer prepare);
  Q_INVOKABLE QString suggestedProjectPath(const QString& source) const;
  Q_INVOKABLE QVariantMap setTrainerExecutable(const QString& path);
  Q_INVOKABLE QVariantMap startVideo(const QString& video, const QString& new_project_folder,
      int training_steps = 5000, int maximum_edge = 1600, int max_splats = 500000, int frame_count = 160);
  void setTimedCaptureHandlers(TimedCapturePreparer prepare, TimedCaptureValidator validate);
  void setSpacetimeValidator(SpacetimeValidator validate);
  QVariantMap snapshot() const;
  Q_INVOKABLE QVariantMap startCapture(const QString& source_folder, const QString& new_project_folder,
                                      int training_steps = 3000, int maximum_edge = 1600,
                                      int max_splats = 1000000);
  Q_INVOKABLE void cancel();
  Q_INVOKABLE QVariantMap startTimedCapture(const QString& manifest, const QString& new_project_folder,
                                           int maximum_edge = 1600);
  Q_INVOKABLE QVariantMap openProject(const QString& folder);
  Q_INVOKABLE QVariantMap buildPartialScene();
 signals:
  void changed();
 private:
  CustomerEditAccess customer_edit_access_;

  bool publish();
  void fail(const QString& error);
  void runStage();
  void finishStage(int code, QProcess::ExitStatus status);
  void updateProcessLog();
  QVariantMap beginCapture(const QString& source, const QString& target, const QStringList& inputs,
      int training_steps, int maximum_edge, int max_splats, int video_frames = 0);
  CapturePreparer prepare_;
  VideoPreparer prepare_video_;
  TimedCapturePreparer prepare_timed_;
  TimedCaptureValidator validate_timed_;
  SpacetimeValidator validate_spacetime_;
  bool new_timed_project_ = false;
  QFutureWatcher<QVariantMap> preparation_;
  std::shared_ptr<std::atomic_bool> canceled_;
  QFutureWatcher<QVariantMap> process_;
  QTimer process_log_timer_;
  std::shared_ptr<GaussianProcessLog> process_log_;
  bool process_active_ = false;
  bool preparation_active_ = false;
  QVariantMap state_;
  QString project_, colmap_, trainer_;
  QStringList sources_;
  QVariantList camera_groups_;
  qsizetype camera_group_ = 0;
  QString log_tail_;
  int stage_ = -1;
  int steps_ = 3000;
  int maximum_edge_ = 1600;
  int max_splats_ = 1000000;
  bool busy_ = false;
  bool read_only_project_ = false;
  bool partial_build_authorized_ = false;
};

}  // namespace photo_editor::desktop
