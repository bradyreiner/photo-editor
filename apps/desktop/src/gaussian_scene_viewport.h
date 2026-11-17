#pragma once
#include "gaussian_scene_renderer.h"
#include "gaussian_film_export.h"
#include "gaussian_scene_cleanup.h"
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
  Q_INVOKABLE QVariantMap describeCameraOptics(double permissible_coc_microns = 30) const;
  Q_INVOKABLE void selectCamera(int index);
  Q_INVOKABLE void orbit(double dx, double dy);
  Q_INVOKABLE void look(double yaw_degrees, double pitch_degrees);
  Q_INVOKABLE void moveCamera(double right, double down, double forward);
  Q_INVOKABLE void roll(double degrees);
  Q_INVOKABLE void pan(double dx, double dy);
  Q_INVOKABLE void dolly(double steps);
  Q_INVOKABLE void resetCamera();
  Q_INVOKABLE void beginNavigation();
  Q_INVOKABLE void endNavigation();
  Q_INVOKABLE void beginCameraAdjustment();
  Q_INVOKABLE void endCameraAdjustment();
  Q_INVOKABLE QVariantMap setFocusMode(const QString& mode);
  Q_INVOKABLE QVariantMap setPhotoSettings(const QVariantMap& settings);
  Q_INVOKABLE QVariantMap setPhysicalCamera(const QVariantMap& settings);
  Q_INVOKABLE QVariantMap clearPhysicalCamera();
  Q_INVOKABLE QVariantMap pickFocusAt(double x, double y);
  Q_INVOKABLE QVariantMap orbitAroundAt(double x, double y);
  Q_INVOKABLE void seekSceneTime(double elapsed_seconds);
  Q_INVOKABLE void playSceneTime(bool play);
  Q_INVOKABLE QVariantMap saveView(const QString& name);
  Q_INVOKABLE QVariantMap restoreView(const QString& id);
  Q_INVOKABLE QVariantMap saveCameraPath(const QVariantMap& settings);
  Q_INVOKABLE void seekCameraPath(double seconds);
  Q_INVOKABLE void playCameraPath(bool play);
  Q_INVOKABLE QVariantMap exportFilm(const QString& destination, int maximum_edge = 1600);
  Q_INVOKABLE QVariantMap exportFrameSequence(const QString& destination, int maximum_edge = 1600);
  Q_INVOKABLE QVariantMap exportTimedFilm(const QString& destination, const QVariantMap& settings, int maximum_edge = 1600);
  Q_INVOKABLE QVariantMap exportTimedFrameSequence(const QString& destination, const QVariantMap& settings, int maximum_edge = 1600);
  Q_INVOKABLE void cancelFilm();
  Q_INVOKABLE QVariantMap exportPhoto(const QString& destination, int maximum_edge = 4096);
  Q_INVOKABLE QVariantMap cleanupSelection(double x, double y, double width, double height, bool keep_inside);
  Q_INVOKABLE QVariantMap cleanupSelection(double x, double y, double width, double height, bool keep_inside, double near_depth, double far_depth);
  Q_INVOKABLE QVariantMap cleanupLasso(const QVariantList& points, bool keep_inside);
  Q_INVOKABLE QVariantMap cleanupLasso(const QVariantList& points, bool keep_inside, double near_depth, double far_depth);
  Q_INVOKABLE QVariantMap cleanupFilter(double minimum_opacity, double maximum_sigma = 0);
  Q_INVOKABLE QVariantMap undoCleanup();
  Q_INVOKABLE QVariantMap redoCleanup();
  Q_INVOKABLE QVariantMap resetCleanup();
  Q_INVOKABLE QVariantMap exportSplat(const QString& destination);
  Q_INVOKABLE QVariantMap setSceneComposition(const QVariantMap& composition);
  Q_INVOKABLE QVariantMap importSceneMesh(const QString& path, bool preserve_coordinates = false, bool import_materials = true);
  Q_INVOKABLE QVariantMap addSceneObject(const QString& kind);
  Q_INVOKABLE QVariantMap addSceneLight(const QString& kind);
  Q_INVOKABLE QVariantMap undoSceneComposition();
  Q_INVOKABLE QVariantMap redoSceneComposition();
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
  void cameraAdjustmentChanged();
  void renderNext();
  QVariantMap submitFilm(const QString& destination, int maximum_edge, const GaussianCameraPath& path,
                        std::optional<GaussianFilmSceneTime> scene_time = {}, GaussianFilmFormat format = GaussianFilmFormat::Mp4);
  QVariantMap submitTimedFilm(const QString& destination, const QVariantMap& settings, int maximum_edge, GaussianFilmFormat format);
  GaussianCamera currentCamera(QSize size = {}) const;
  GaussianCamera fullCamera() const;
  QVariantMap applyComposition(const GaussianSceneComposition& composition, int history = 0);
  std::shared_ptr<const GaussianSceneComposition> composition_;
  QVariantMap composition_receipt_;
  std::vector<GaussianSceneComposition> composition_undo_, composition_redo_;
  QVariantMap submitCleanup(std::function<QByteArray(const GaussianSceneData&, QByteArray,
      const std::shared_ptr<std::atomic_bool>&)> operation, int history = 0);
  struct MeshImportResult { GaussianMeshLoad geometry; GaussianMaterialLoad materials; };
  QFutureWatcher<MeshImportResult> mesh_import_;
  std::shared_ptr<std::atomic_bool> mesh_import_canceled_;
  bool mesh_import_active_ = false;
  QString mesh_import_source_;
  bool mesh_import_preserve_ = false;
  QFutureWatcher<GaussianCleanupResult> cleanup_;
  std::shared_ptr<std::atomic_bool> cleanup_canceled_;
  bool cleanup_active_ = false;
  quint64 cleanup_epoch_ = 0;
  int cleanup_history_ = 0;
  std::vector<QByteArray> cleanup_undo_, cleanup_redo_;
  QVariantMap source_, status_;
  QString source_key_;
  QFutureWatcher<GaussianSceneLoad> load_;
  QFutureWatcher<GaussianFrame> render_;
  QFutureWatcher<QVariantMap> focus_pick_;
  bool focus_pick_active_ = false;
  bool focus_pick_orbit_ = false;
  std::optional<QVector3D> orbit_target_;
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
  // Submission describes the worker, while these identify the displayed image.
  quint64 frame_epoch_ = 0, frame_request_ = 0;
  int camera_index_ = 0;
  std::optional<GaussianCamera> saved_camera_;
  std::optional<GaussianCameraPath> camera_path_;
  QTimer playback_timer_, film_progress_timer_;
  QTimer navigation_settle_timer_;
  QTimer refinement_progress_timer_;
  std::shared_ptr<std::atomic_bool> render_canceled_;
  std::shared_ptr<std::atomic_int> render_samples_completed_;
  std::shared_ptr<std::atomic_int> render_tiles_completed_;
  std::shared_ptr<std::atomic_int> render_lighting_batches_;
  int render_light_target_ = 1;
  std::shared_ptr<GaussianProgressiveFrames> progressive_frames_;
  int render_tiles_total_ = 0;
  int render_samples_total_ = 1;
  bool navigating_ = false, pointer_navigation_ = false, render_interactive_ = false;
  bool optical_adjusting_ = false;
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
