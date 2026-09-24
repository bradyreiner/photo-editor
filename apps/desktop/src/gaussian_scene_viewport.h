#pragma once
#include "gaussian_scene_renderer.h"
#include "gaussian_film_export.h"
#include <QElapsedTimer>
#include <QTimer>
#include <QFutureWatcher>
#include <QQuickItem>
#include <optional>

namespace photo_editor::desktop {
class GaussianSceneViewport : public QQuickItem {
  Q_OBJECT
  QML_NAMED_ELEMENT(GaussianSceneViewport)
  QML_ADDED_IN_VERSION(1, 0)
  Q_PROPERTY(QVariantMap source READ source WRITE setSource NOTIFY sourceChanged)
  Q_PROPERTY(QVariantMap status READ status NOTIFY statusChanged)
 public:
  explicit GaussianSceneViewport(QQuickItem* parent = nullptr);
  ~GaussianSceneViewport() override;
  QVariantMap source() const { return source_; }
  void setSource(const QVariantMap& source);
  QVariantMap status() const;
  Q_INVOKABLE void selectCamera(int index);
  Q_INVOKABLE void orbit(double dx, double dy);
  Q_INVOKABLE void pan(double dx, double dy);
  Q_INVOKABLE void dolly(double steps);
  Q_INVOKABLE void resetCamera();
  Q_INVOKABLE void beginNavigation();
  Q_INVOKABLE void endNavigation();
  Q_INVOKABLE QVariantMap setPhotoSettings(const QVariantMap& settings);
  Q_INVOKABLE QVariantMap setPhysicalCamera(const QVariantMap& settings);
  Q_INVOKABLE QVariantMap clearPhysicalCamera();
  Q_INVOKABLE QVariantMap pickFocusAt(double x, double y);
  Q_INVOKABLE void seekSceneTime(double elapsed_seconds);
  Q_INVOKABLE void playSceneTime(bool play);
  Q_INVOKABLE QVariantMap saveView(const QString& name);
  Q_INVOKABLE QVariantMap restoreView(const QString& id);
  Q_INVOKABLE QVariantMap saveCameraPath(const QVariantMap& settings);
  Q_INVOKABLE void seekCameraPath(double seconds);
  Q_INVOKABLE void playCameraPath(bool play);
  Q_INVOKABLE QVariantMap exportFilm(const QString& destination, int maximum_edge = 1600);
  Q_INVOKABLE QVariantMap exportTimedFilm(const QString& destination, const QVariantMap& settings, int maximum_edge = 1600);
  Q_INVOKABLE void cancelFilm();
  Q_INVOKABLE QVariantMap exportPhoto(const QString& destination, int maximum_edge = 4096);
 signals:
  void sourceChanged();
  void statusChanged();
 protected:
  QSGNode* updatePaintNode(QSGNode*, UpdatePaintNodeData*) override;
  void geometryChange(const QRectF&, const QRectF&) override;
 private:
  friend struct GaussianSceneViewportTestAccess;
  void loadScene();
  void requestFrame();
  void navigationChanged();
  void renderNext();
  QVariantMap submitFilm(const QString& destination, int maximum_edge, const GaussianCameraPath& path,
                        std::optional<GaussianFilmSceneTime> scene_time = {});
  GaussianCamera currentCamera(QSize size = {}) const;
  GaussianCamera fullCamera() const;
  QVariantMap source_, status_;
  QString source_key_;
  QFutureWatcher<GaussianSceneLoad> load_;
  QFutureWatcher<GaussianFrame> render_;
  QFutureWatcher<QVariantMap> focus_pick_;
  bool focus_pick_active_ = false;
  quint64 focus_pick_epoch_ = 0, focus_pick_request_ = 0;
  std::shared_ptr<std::atomic_bool> focus_pick_canceled_;
  // Include completed-but-undelivered futures in ownership. Replacing a
  // watcher before consuming its result can lose export completion metadata.
  bool load_active_ = false;
  bool render_active_ = false;
  std::shared_ptr<std::atomic_bool> canceled_;
  std::shared_ptr<const GaussianSceneData> scene_;
  std::shared_ptr<GaussianSceneRenderer> renderer_;
  GaussianFrame frame_;
  quint64 epoch_ = 0, load_epoch_ = 0, render_epoch_ = 0, requested_ = 0, submitted_ = 0;
  int camera_index_ = 0;
  std::optional<GaussianCamera> saved_camera_;
  std::optional<GaussianCameraPath> camera_path_;
  QTimer playback_timer_, film_progress_timer_;
  QTimer navigation_settle_timer_;
  QTimer refinement_progress_timer_;
  std::shared_ptr<std::atomic_bool> render_canceled_;
  std::shared_ptr<std::atomic_int> render_samples_completed_;
  std::shared_ptr<std::atomic_int> render_tiles_completed_;
  std::shared_ptr<GaussianProgressiveFrames> progressive_frames_;
  int render_tiles_total_ = 0;
  int render_samples_total_ = 1;
  bool navigating_ = false, pointer_navigation_ = false, render_interactive_ = false;
  QTimer scene_playback_timer_;
  QElapsedTimer scene_playback_elapsed_;
  double scene_playback_start_ = 0;
  std::optional<double> scene_seconds_, export_seconds_;
  QElapsedTimer playback_elapsed_;
  double playback_start_ = 0;
  QFutureWatcher<QVariantMap> film_;
  std::shared_ptr<GaussianFilmProgress> film_progress_;
  float yaw_ = 0, pitch_ = 0, dolly_ = 0, pan_x_ = 0, pan_y_ = 0;
  QString export_path_;
  GaussianCamera export_camera_;
  std::shared_ptr<const GaussianSceneData> export_scene_;
  bool export_in_flight_ = false;
};
}  // namespace photo_editor::desktop
