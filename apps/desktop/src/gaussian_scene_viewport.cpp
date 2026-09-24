#include "gaussian_scene_viewport.h"
#include "gaussian_physical_camera.h"
#include "gaussian_saved_views.h"
#include "gaussian_spacetime_artifact.h"
#include "gaussian_film_publication.h"
#include "gaussian_focus_pick.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageWriter>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQuickWindow>
#include <QSGSimpleTextureNode>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUrl>
#include <QtConcurrent>
#include <qsgtexture_platform.h>
#include <algorithm>
#include <cmath>
#include <exception>

namespace photo_editor::desktop {
namespace {
class SceneTextureNode final : public QSGSimpleTextureNode {
 public:
  ~SceneTextureNode() override { setOwnsTexture(false); delete texture(); }
  quint64 handle = 0;
  std::shared_ptr<void> lease;
};
QSize fit_edge(QSize size, int edge) { size.scale(edge, edge, Qt::KeepAspectRatio); return size; }
// All inputs are owned by the worker closure; no UI object is accessed.
GaussianFrame render_scene_frame(const std::shared_ptr<GaussianSceneRenderer>& renderer,
    const std::shared_ptr<const GaussianSceneData>& scene, const GaussianCamera& camera, const QString& path,
    std::optional<double> scene_seconds = {}, const GaussianRenderOptions& options = {}) {
  try {
    auto frame = renderer->renderWithOptions(scene, camera, !path.isEmpty(), scene_seconds, options);
    frame.metrics.insert("camera", camera.description());
    if (path.isEmpty() || !frame.error.isEmpty() || frame.canceled) return frame;
    QTemporaryDir staging(QDir(QFileInfo(path).absolutePath()).filePath(".framehouse-photo-XXXXXX"));
    if (!staging.isValid()) { frame.error = "Cannot stage photo export beside the destination."; return frame; }
    QFile image_file(staging.filePath("photo")), camera_file(staging.filePath("camera.json"));
    if (!image_file.open(QIODevice::WriteOnly | QIODevice::NewOnly) || !camera_file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
      frame.error = "Cannot stage photo export. No final output was published."; return frame;
    }
    QImageWriter writer(&image_file, QFileInfo(path).suffix().toLatin1().toLower());
    writer.setQuality(95);
    if (!writer.write(frame.image)) { frame.error = "Photo export staging failed: " + writer.errorString(); return frame; }
    QVariantMap receipt{{"schema", "framehouse-gaussian-photo-v1"}, {"artifactSha256", scene->artifact_sha256},
        {"camera", camera.description()}, {"colorSpace", "sRGB"}, {"backend", "metal-gaussian-v1"}, {"outputPath", path}};
    if (camera.photo != GaussianPhotoSettings{}) receipt.insert("schema", "framehouse-gaussian-photo-v2");
    if (camera.physical) receipt.insert("schema", "framehouse-gaussian-photo-v3");
    if(scene_seconds) { receipt.insert("sceneSeconds",*scene_seconds); receipt.insert("timeUnit","seconds"); }
    const auto bytes = QJsonDocument::fromVariant(receipt).toJson();
    if (camera_file.write(bytes) != bytes.size() || !camera_file.flush() || !image_file.flush()) {
      frame.error = "Cannot finish staged photo and camera receipt. No final output was published."; return frame;
    }
    image_file.close(); camera_file.close();
    const auto publication = publish_gaussian_media_files(image_file.fileName(), camera_file.fileName(), path, ".camera.json");
    if (!publication.ok) { frame.error = publication.error; return frame; }
    frame.metrics.insert("exportPath", path);
    frame.image = {};  // Do not retain a full-resolution CPU export in the viewport.
    return frame;
  } catch (const std::exception& error) {
    GaussianFrame failed; failed.error = QStringLiteral("Scene rendering/export failed: %1").arg(QString::fromUtf8(error.what()).left(512)); return failed;
  } catch (...) { GaussianFrame failed; failed.error = "Scene rendering/export failed unexpectedly."; return failed; }
}
}
GaussianSceneViewport::GaussianSceneViewport(QQuickItem* parent) : QQuickItem(parent) {
  setFlag(ItemHasContents, true);
  status_.insert("state", "idle");
  connect(&focus_pick_, &QFutureWatcher<QVariantMap>::finished, this, [this] {
    auto result = focus_pick_.result();
    focus_pick_active_ = false;
    if (focus_pick_epoch_ != epoch_ || focus_pick_request_ != requested_ ||
        !scene_ || !isVisible() || (focus_pick_canceled_ && focus_pick_canceled_->load())) { emit statusChanged(); return; }
    if (result.value("ok").toBool()) {
      const auto camera = fullCamera();
      auto photo = camera.photo;
      photo.lens.focus_distance = result.value("focusDistance").toFloat();
      if (!photo.valid()) result = {{"ok", false}, {"error", "This surface is too close for the current aperture radius. Reduce the aperture and pick again."}};
      else {
        auto physical = camera.physical ? camera.physical->settings : GaussianPhysicalCameraSettings{};
        physical.focus_distance_m = result.value("focusDistance").toDouble()*physical.metres_per_scene_unit;
        const auto applied = camera.physical ? setPhysicalCamera(physical.description()) : setPhotoSettings(photo.description());
        if (!applied.value("ok").toBool()) result = applied;
      }
    }
    status_.insert("focusPickResult", result);
    status_.insert("focusPickError", result.value("error"));
    emit statusChanged();
  });
  refinement_progress_timer_.setInterval(100);
  connect(&refinement_progress_timer_, &QTimer::timeout, this, [this] {
    if (progressive_frames_) {
      auto partial = progressive_frames_->takeLatest();
      if (partial && partial->native_texture && partial->native_lease && !partial->canceled && partial->error.isEmpty() &&
          render_active_ && !export_in_flight_ && !render_interactive_ &&
          render_epoch_ == epoch_ && submitted_ == requested_ && !navigating_ && isVisible() &&
          render_canceled_ && !render_canceled_->load()) {
        frame_ = std::move(*partial);
        status_.insert("state", "ready");
        status_.insert("metrics", frame_.metrics);
        status_.insert("publishedFrames", status_.value("publishedFrames").toInt() + 1);
        status_.insert("publishedPhase", "refining");
        status_.insert("progressiveFramesPublished", status_.value("progressiveFramesPublished").toInt() + 1);
        update();
      }
    }
    emit statusChanged();
  });
  navigation_settle_timer_.setInterval(180);
  navigation_settle_timer_.setSingleShot(true);
  connect(&navigation_settle_timer_, &QTimer::timeout, this, [this] {
    if (pointer_navigation_) return;
    navigating_ = false;
    requestFrame();
  });
  scene_playback_timer_.setInterval(16);
  connect(&scene_playback_timer_, &QTimer::timeout, this, [this] {
    if(!isVisible() || !scene_seconds_) { playSceneTime(false); return; }
    seekSceneTime(scene_playback_start_+double(scene_playback_elapsed_.elapsed())/1000);
    if(status_.value("timePositionSeconds").toDouble()>=status_.value("timeDurationSeconds").toDouble()) playSceneTime(false);
  });
  playback_timer_.setInterval(16);
  connect(&playback_timer_, &QTimer::timeout, this, [this] {
    if (!camera_path_ || !isVisible()) { playCameraPath(false); return; }
    seekCameraPath(playback_start_ + static_cast<double>(playback_elapsed_.elapsed()) / 1000);
    if (status_.value("pathSeconds").toDouble() >= camera_path_->duration()) playCameraPath(false);
  });
  film_progress_timer_.setInterval(100);
  connect(&film_progress_timer_, &QTimer::timeout, this, [this] {
    status_.insert("filmFramesCompleted", film_progress_->completed.load()); emit statusChanged();
  });
  connect(&film_, &QFutureWatcher<QVariantMap>::finished, this, [this] {
    film_progress_timer_.stop();
    status_.insert("filmExporting", false);
    status_.insert("filmFramesCompleted", film_progress_->completed.load());
    status_.insert("filmResult", film_.result()); emit statusChanged();
  });
  connect(this, &QQuickItem::visibleChanged, this, [this] {
    if (isVisible()) requestFrame(); else {
      if (focus_pick_canceled_) focus_pick_canceled_->store(true);
      if (render_canceled_ && !export_in_flight_) render_canceled_->store(true);
      navigation_settle_timer_.stop(); navigating_ = pointer_navigation_ = false;
      playSceneTime(false); playCameraPath(false);
    }
  });
  connect(&load_, &QFutureWatcher<GaussianSceneLoad>::finished, this, [this] {
    const auto result = load_.result();
    load_active_ = false;
    if (load_epoch_ != epoch_) { loadScene(); return; }
    if (!result.scene) { status_.insert("state", "failed"); status_.insert("error", result.error); emit statusChanged(); return; }
    scene_ = result.scene;
    scene_seconds_ = scene_->temporal.empty() ? std::optional<double>{} :
        std::optional<double>{scene_->time_origin_seconds+scene_->local_time_range[0]};
    status_.insert("temporal",scene_seconds_.has_value());
    status_.insert("timePositionSeconds",0);
    status_.insert("timeDurationSeconds",scene_seconds_ ? double(scene_->local_time_range[1])-scene_->local_time_range[0] : 0);
    status_.insert("state", "rendering");
    QStringList cameras;
    for (const auto& camera : scene_->cameras) cameras.append(camera.name);
    status_.insert("cameras", cameras);
    status_.insert("pointCount", static_cast<qulonglong>(scene_->points.size()));
    const auto saved = load_gaussian_saved_views(source_.value("projectPath").toString(), *scene_);
    status_.insert("savedViews", saved.value("views", QVariantList{}));
    status_.insert("savedViewError", saved.value("error"));
    status_.insert("cameraPath", saved.value("cameraPath"));
    camera_path_ = gaussian_camera_path(saved.value("cameraPath").toMap(), saved.value("views").toList());
    status_.insert("pathDuration", camera_path_ ? camera_path_->duration() : 0);
    resetCamera();
  });
  connect(&render_, &QFutureWatcher<GaussianFrame>::finished, this, [this] {
    refinement_progress_timer_.stop();
    progressive_frames_.reset();
    const auto result = render_.result();
    const bool exported = export_in_flight_;
    export_in_flight_ = false;
    if ((render_epoch_ == epoch_ || exported) && !result.canceled) {
      if (!result.error.isEmpty()) {
        status_.insert("error", result.error);
        if (!exported) status_.insert("state", "failed");
      } else if (exported) {
        status_.insert("lastExport", result.metrics.value("exportPath"));
        status_.insert("lastExportCamera", result.metrics.value("camera"));
        status_.insert("lastExportSceneSeconds", result.metrics.value("sceneSeconds"));
        status_.insert("lastExportRenderMetrics", result.metrics);
      } else if (render_interactive_ || submitted_ == requested_) {
        frame_ = result;
        status_.insert("state", "ready");
        status_.insert("metrics", result.metrics);
        status_.insert("publishedFrames", status_.value("publishedFrames").toInt() + 1);
        status_.insert("publishedPhase", render_interactive_ ? "live" : "photo");
        if (submitted_ == requested_ && !navigating_ && !render_interactive_)
          status_.insert("previewPhase", "photo");
        update();
      }
      emit statusChanged();
    }
    render_active_ = false;
    if (!export_path_.isEmpty() || submitted_ != requested_) renderNext();
  });
}
GaussianSceneViewport::~GaussianSceneViewport() {
  if (focus_pick_canceled_) focus_pick_canceled_->store(true);
  cancelFilm();  // The detached film worker owns its immutable scene and cancellation state.
  if (canceled_) canceled_->store(true);
  if (render_canceled_ && !export_in_flight_) render_canceled_->store(true);
  // Do not block the GUI on file cancellation or GPU completion. Workers own
  // their data; QObject-context connections disconnect when this view dies.
  // Preserve an accepted queued photo after the current render, using the same
  // serialized renderer and the export's frozen camera and scene.
  if (!export_path_.isEmpty() && renderer_ && export_scene_) {
    (void)QtConcurrent::run([previous = render_.future(), renderer = renderer_,
        scene = export_scene_, camera = export_camera_, path = export_path_, seconds = export_seconds_]() mutable {
      if (previous.isValid()) previous.waitForFinished();
      return render_scene_frame(renderer, scene, camera, path, seconds);
    });
  }
}
QVariantMap GaussianSceneViewport::status() const {
  auto result = status_;
  result.insert("frameAvailable", frame_.native_texture != 0);
  result.insert("focusPicking", focus_pick_active_);
  result.insert("cameraIndex", camera_index_);
  result.insert("exporting", export_in_flight_ || !export_path_.isEmpty());
  result.insert("navigating", navigating_);
  result.insert("photoSettings", scene_ ? fullCamera().photo.description() : GaussianPhotoSettings{}.description());
  result.insert("lensSettings", scene_ ? fullCamera().photo.lens.description() : GaussianLensSettings{}.description());
  result.insert("sceneFocusDistance", scene_ ? scene_->focus_distance : 1);
  result.insert("physicalCamera", scene_ && fullCamera().physical ? fullCamera().physical->settings.description() : QVariantMap{});
  result.insert("refinementSamplesCompleted", render_samples_completed_ ? render_samples_completed_->load() : 0);
  result.insert("refinementSamplesTotal", render_samples_total_);
  result.insert("exportTilesCompleted", export_in_flight_ && render_tiles_completed_ ? render_tiles_completed_->load() : 0);
  result.insert("exportTilesTotal", export_in_flight_ ? render_tiles_total_ : 0);
  return result;
}
void GaussianSceneViewport::setSource(const QVariantMap& source) {
  const auto hash = source.value("artifactValidation").toMap().value("artifactSha256").toString();
  const bool spacetime=source.value("mode")=="spacetime-4d" && source.value("state")=="artifact-ready";
  const auto key = source.value("state").toString() == "trained" || spacetime ?
      source.value("projectPath").toString() + QChar(0) + source.value("artifactPath").toString() + QChar(0) + hash + QChar(0) + QString::number(spacetime) : QString();
  if (key == source_key_) return;
  if (focus_pick_canceled_) focus_pick_canceled_->store(true);
  if (render_canceled_ && !export_in_flight_) render_canceled_->store(true);
  navigation_settle_timer_.stop(); navigating_ = pointer_navigation_ = false;
  playSceneTime(false); playCameraPath(false); cancelFilm(); camera_path_.reset();
  source_key_ = key;
  source_ = {{"projectPath", source.value("projectPath")}, {"artifactPath", source.value("artifactPath")}, {"hash", hash},{"spacetime",spacetime}};
  ++epoch_;
  if (canceled_) canceled_->store(true);
  scene_.reset(); scene_seconds_.reset(); frame_ = {}; camera_index_ = 0; saved_camera_.reset();
  status_ = {{"state", key.isEmpty() ? "idle" : "loading"}, {"filmExporting", film_.isRunning()}};
  emit sourceChanged(); emit statusChanged(); update();
  loadScene();
}
void GaussianSceneViewport::loadScene() {
  if (source_key_.isEmpty() || load_active_) return;
  load_epoch_ = epoch_;
  canceled_ = std::make_shared<std::atomic_bool>(false);
  const auto canceled = canceled_;
  const auto source = source_;
  load_active_ = true;
  load_.setFuture(QtConcurrent::run([source, canceled] {
    try {
    const auto root = QFileInfo(source.value("projectPath").toString()).canonicalFilePath();
    const auto artifact = source.value("artifactPath").toString();
    if(source.value("spacetime").toBool()) {
      if(root.isEmpty() || QFileInfo(artifact).canonicalFilePath()!=QDir(root).filePath("scene/scene.safetensors"))
        return GaussianSceneLoad{{},"The 4D artifact must remain inside the verified project."};
      return load_gaussian_spacetime_artifact(artifact,source.value("hash").toString(),canceled);
    }
    const auto cameras = QDir(root).filePath("dataset/sparse");
    if (root.isEmpty() || QFileInfo(artifact).canonicalFilePath() != QDir(root).filePath("trained/scene.ply") ||
        QFileInfo(cameras).canonicalFilePath() != cameras)
      return GaussianSceneLoad{{}, "Scene files must remain inside the verified project."};
    return load_gaussian_scene(artifact, cameras, source.value("hash").toString(), canceled);
    } catch (const std::exception& error) {
      return GaussianSceneLoad{{}, QStringLiteral("Scene loading failed: %1").arg(QString::fromUtf8(error.what()).left(512))};
    } catch (...) { return GaussianSceneLoad{{}, "Scene loading failed unexpectedly."}; }
  }));
}
GaussianCamera GaussianSceneViewport::fullCamera() const {
  const auto base = saved_camera_.value_or(scene_->cameras[static_cast<size_t>(camera_index_)]);
  if (yaw_ == 0 && pitch_ == 0 && dolly_ == 0 && pan_x_ == 0 && pan_y_ == 0) return base;
  return navigate_gaussian_camera(base, scene_->focus_distance, yaw_, pitch_, dolly_, pan_x_, pan_y_);
}
GaussianCamera GaussianSceneViewport::currentCamera(QSize size) const {
  auto camera = fullCamera();
  if (size.isValid()) return camera.resized(size);
  const double scale = window() ? window()->devicePixelRatio() : 1.0;
  QSizeF display = camera.image_size;
  display.scale(QSizeF(width() * scale, height() * scale), Qt::KeepAspectRatio);
  const auto edge = std::clamp(static_cast<int>(std::max(display.width(), display.height())), 256, navigating_ ? 720 : 4096);
  return camera.resized(fit_edge(camera.image_size, edge));
}
void GaussianSceneViewport::requestFrame() {
  if (focus_pick_canceled_) focus_pick_canceled_->store(true);
  status_.remove("focusPickError");
  if (render_canceled_ && !export_in_flight_ && !render_interactive_) render_canceled_->store(true);
  ++requested_;
  status_.insert("previewPhase", navigating_ ? "live" : "refining");
  renderNext(); emit statusChanged();
}
void GaussianSceneViewport::beginNavigation() {
  if (focus_pick_canceled_) focus_pick_canceled_->store(true);
  pointer_navigation_ = true;
  navigationChanged();
}
void GaussianSceneViewport::endNavigation() {
  pointer_navigation_ = false;
  if (navigating_) navigation_settle_timer_.start();
}
QVariantMap GaussianSceneViewport::pickFocusAt(double x, double y) {
  if (!scene_ || focus_pick_active_ || navigating_ || !isVisible() || !frame_.native_texture ||
      frame_.metrics.value("camera").toMap() != currentCamera().description())
    return {{"ok", false}, {"error", "Wait for the current camera preview, then pick a surface."}};
  const auto pixel = gaussian_focus_pixel(frame_.size, QSizeF(width(), height()), QPointF(x,y));
  if (!pixel) return {{"ok", false}, {"error", "Pick inside the image, not the empty border."}};
  const auto scene = scene_; const auto camera = currentCamera(); const auto seconds = scene_seconds_;
  focus_pick_epoch_ = epoch_; focus_pick_request_ = requested_;
  focus_pick_canceled_ = std::make_shared<std::atomic_bool>(false);
  const auto canceled = focus_pick_canceled_;
  focus_pick_active_ = true;
  status_.remove("focusPickError"); status_.remove("focusPickResult");
  focus_pick_.setFuture(QtConcurrent::run([scene,camera,pixel,seconds,canceled] {
    try { return pick_gaussian_focus(*scene,camera,*pixel,seconds,canceled); }
    catch (const std::exception& error) { return QVariantMap{{"ok",false},{"error",QString::fromUtf8(error.what()).left(256)}}; }
    catch (...) { return QVariantMap{{"ok",false},{"error","Focus estimation failed."}}; }
  }));
  emit statusChanged();
  return {{"ok",true}};
}
QVariantMap GaussianSceneViewport::setPhotoSettings(const QVariantMap& settings) {
  if (!scene_) return {{"ok", false}, {"error", "Wait for the scene before changing photo settings."}};
  const auto photo = gaussian_photo_settings(settings);
  if (!photo) return {{"ok", false}, {"error", "Invalid photo, lens or film settings. Check control ranges; aperture radius cannot exceed one quarter of the focus distance."}};
  auto camera = saved_camera_.value_or(scene_->cameras[static_cast<size_t>(camera_index_)]);
  if (camera.physical && (camera.photo.focal_scale != photo->focal_scale ||
      camera.photo.lens.focus_distance != photo->lens.focus_distance || camera.photo.lens.aperture_radius != photo->lens.aperture_radius))
    return {{"ok",false},{"error","Use the physical camera controls for focal length, focus and f-stop, or return to scene-unit controls."}};
  if (camera.photo == *photo) return {{"ok", true}};
  const bool optical_change = camera.photo.focal_scale != photo->focal_scale || camera.photo.lens != photo->lens;
  playCameraPath(false);
  camera.photo = *photo;
  saved_camera_ = camera;
  status_.remove("activeViewId");
  // Film/tone edits do not move the camera. Keep the resolved image size and
  // lens state so the renderer can reuse its completed pre-film image.
  if (optical_change) navigationChanged();
  requestFrame();
  return {{"ok", true}};
}
QVariantMap GaussianSceneViewport::setPhysicalCamera(const QVariantMap& settings) {
  if (!scene_) return {{"ok",false},{"error","Wait for the scene before configuring a physical camera."}};
  const auto physical = gaussian_physical_camera_settings(settings);
  if (!physical) return {{"ok",false},{"error","Enter a positive scene scale and valid sensor, focal length, f-stop and focus distance."}};
  const auto base = saved_camera_.value_or(scene_->cameras[static_cast<size_t>(camera_index_)]);
  const auto configured = make_gaussian_physical_camera(base,base.image_size,*physical);
  if (!configured.camera) return {{"ok",false},{"error",configured.error}};
  playCameraPath(false);
  saved_camera_ = *configured.camera;
  status_.remove("activeViewId");
  navigationChanged(); requestFrame();
  return {{"ok",true},{"calibration",configured.camera->physical->description()}};
}
QVariantMap GaussianSceneViewport::clearPhysicalCamera() {
  if (!scene_) return {{"ok",false},{"error","Wait for the scene before changing the camera model."}};
  auto camera = saved_camera_.value_or(scene_->cameras[static_cast<size_t>(camera_index_)]);
  if (!camera.physical) return {{"ok",true}};
  camera.physical.reset();
  saved_camera_ = camera; status_.remove("activeViewId");
  playCameraPath(false); requestFrame();
  return {{"ok",true}};
}
void GaussianSceneViewport::navigationChanged() {
  navigating_ = true;
  navigation_settle_timer_.start();
}
void GaussianSceneViewport::renderNext() {
  if ((!scene_ && export_path_.isEmpty()) || render_active_ || (!isVisible() && export_path_.isEmpty())) return;
  if (export_path_.isEmpty() && window() && window()->rendererInterface()->graphicsApi() != QSGRendererInterface::Metal) {
    status_.insert("state", "failed"); status_.insert("error", "Scene viewing requires Qt's Metal graphics backend."); emit statusChanged(); return;
  }
  if (!renderer_) renderer_ = make_gaussian_scene_renderer();
  if (!renderer_) { status_.insert("state", "failed"); status_.insert("error", "Native Gaussian rendering currently requires Metal on macOS."); emit statusChanged(); return; }
  render_epoch_ = epoch_;
  const auto path = export_path_;
  // A frozen export does not satisfy pending preview requests. In particular,
  // changes queued before this export starts must still publish afterward.
  if(path.isEmpty()) submitted_ = requested_;
  render_interactive_ = path.isEmpty() && navigating_;
  const auto camera = path.isEmpty() ? currentCamera() : export_camera_;
  export_path_.clear(); export_in_flight_ = !path.isEmpty();
  const auto scene = path.isEmpty() ? scene_ : export_scene_;
  const auto seconds = path.isEmpty() ? scene_seconds_ : export_seconds_;
  if (!path.isEmpty()) export_scene_.reset();
  const auto renderer = renderer_;
  render_canceled_ = std::make_shared<std::atomic_bool>(false);
  render_samples_completed_ = std::make_shared<std::atomic_int>(0);
  render_tiles_completed_ = std::make_shared<std::atomic_int>(0);
  render_tiles_total_ = !path.isEmpty() && (camera.image_size.width() > 4096 || camera.image_size.height() > 4096)
      ? ((camera.image_size.width()+2047)/2048)*((camera.image_size.height()+2047)/2048) : 0;
  render_samples_total_ = !render_interactive_ && camera.photo.lens.aperture_radius > 0 ? camera.photo.lens.samples : 1;
  progressive_frames_ = path.isEmpty() && !render_interactive_ && render_samples_total_ > 1
      ? std::make_shared<GaussianProgressiveFrames>() : nullptr;
  const GaussianRenderOptions options{render_canceled_, render_samples_completed_, render_interactive_, 0,
      render_tiles_completed_, progressive_frames_};
  if (render_samples_total_ > 1 || render_tiles_total_ > 0) refinement_progress_timer_.start();
  render_active_ = true;
  render_.setFuture(QtConcurrent::run([renderer, scene, camera, path, seconds, options] {
    return render_scene_frame(renderer, scene, camera, path, seconds, options);
  }));
}
void GaussianSceneViewport::selectCamera(int index) {
  if (!scene_ || index < 0 || static_cast<size_t>(index) >= scene_->cameras.size()) return;
  camera_index_ = index; resetCamera();
}
void GaussianSceneViewport::seekSceneTime(double elapsed_seconds) {
  if(!scene_ || !scene_seconds_ || !std::isfinite(elapsed_seconds)) return;
  navigationChanged();
  const auto duration=double(scene_->local_time_range[1])-scene_->local_time_range[0];
  const auto elapsed=std::clamp(elapsed_seconds,0.0,duration);
  scene_seconds_=scene_->time_origin_seconds+scene_->local_time_range[0]+elapsed;
  status_.insert("timePositionSeconds",elapsed);
  requestFrame();
}
void GaussianSceneViewport::playSceneTime(bool play) {
  if(!play && !scene_playback_timer_.isActive()) return;
  if(play && scene_seconds_ && isVisible()) {
    scene_playback_start_=status_.value("timePositionSeconds").toDouble();
    if(scene_playback_start_>=status_.value("timeDurationSeconds").toDouble()) scene_playback_start_=0;
    scene_playback_elapsed_.restart(); scene_playback_timer_.start();
  } else scene_playback_timer_.stop();
  status_.insert("timePlaying",scene_playback_timer_.isActive()); emit statusChanged();
}
void GaussianSceneViewport::resetCamera() {
  playCameraPath(false);
  navigation_settle_timer_.stop(); navigating_ = pointer_navigation_ = false;
  saved_camera_.reset(); status_.remove("activeViewId");
  yaw_ = pitch_ = dolly_ = pan_x_ = pan_y_ = 0; requestFrame();
}
QVariantMap GaussianSceneViewport::saveView(const QString& name) {
  if (!scene_) return {{"ok", false}, {"error", "Wait for the scene to load before saving a view."}};
  const auto result = save_gaussian_view(source_.value("projectPath").toString(), *scene_, name, camera_index_, fullCamera());
  if (result.value("ok").toBool()) {
    status_.insert("savedViews", result.value("views"));
    status_.insert("activeViewId", result.value("id"));
    status_.remove("savedViewError");
  } else status_.insert("savedViewError", result.value("error"));
  emit statusChanged(); return result;
}
QVariantMap GaussianSceneViewport::restoreView(const QString& id) {
  if (!scene_) return {{"ok", false}, {"error", "Wait for the scene to load before recalling a view."}};
  const auto loaded = load_gaussian_saved_views(source_.value("projectPath").toString(), *scene_);
  if (!loaded.value("ok").toBool()) {
    status_.insert("savedViewError", loaded.value("error")); emit statusChanged(); return loaded;
  }
  status_.insert("savedViews", loaded.value("views"));
  for (const auto& item : loaded.value("views").toList()) {
    const auto view = item.toMap();
    if (view.value("id").toString() != id) continue;
    playCameraPath(false);
    saved_camera_ = gaussian_saved_camera(view.value("camera").toMap());
    camera_index_ = view.value("captureCameraIndex").toInt();
    yaw_ = pitch_ = dolly_ = pan_x_ = pan_y_ = 0;
    status_.insert("activeViewId", id); status_.remove("savedViewError");
    requestFrame(); return {{"ok", true}, {"camera", fullCamera().description()}};
  }
  emit statusChanged();
  return {{"ok", false}, {"error", "That saved view is no longer available."}};
}
void GaussianSceneViewport::orbit(double dx, double dy) {
  if (!std::isfinite(dx) || !std::isfinite(dy)) return;
  navigationChanged();
  playCameraPath(false);
  status_.remove("activeViewId");
  yaw_ = std::fmod(yaw_ + static_cast<float>(std::clamp(dx, -360.0, 360.0)), 360.0F);
  pitch_ = std::clamp(pitch_ + static_cast<float>(std::clamp(dy, -180.0, 180.0)), -85.0F, 85.0F);
  requestFrame();
}
void GaussianSceneViewport::pan(double dx, double dy) {
  if (!std::isfinite(dx) || !std::isfinite(dy)) return;
  navigationChanged();
  playCameraPath(false);
  status_.remove("activeViewId");
  pan_x_ = std::clamp(pan_x_ + static_cast<float>(std::clamp(dx, -1.0, 1.0)), -50.0F, 50.0F);
  pan_y_ = std::clamp(pan_y_ + static_cast<float>(std::clamp(dy, -1.0, 1.0)), -50.0F, 50.0F);
  requestFrame();
}
void GaussianSceneViewport::dolly(double steps) {
  if (!std::isfinite(steps)) return;
  navigationChanged();
  playCameraPath(false);
  status_.remove("activeViewId");
  dolly_ = std::clamp(dolly_ + static_cast<float>(std::clamp(steps, -5.0, 5.0)) * 0.12F, -5.0F, 5.0F); requestFrame();
}
QVariantMap GaussianSceneViewport::saveCameraPath(const QVariantMap& settings) {
  if (!scene_) return {{"ok", false}, {"error", "Load a scene first."}};
  const auto result = save_gaussian_camera_path(source_.value("projectPath").toString(), *scene_, settings);
  if (result.value("ok").toBool()) {
    const auto loaded = load_gaussian_saved_views(source_.value("projectPath").toString(), *scene_);
    if (!loaded.value("ok").toBool()) return loaded;
    camera_path_ = gaussian_camera_path(loaded.value("cameraPath").toMap(), loaded.value("views").toList());
    status_.insert("savedViews", loaded.value("views"));
    status_.insert("cameraPath", loaded.value("cameraPath"));
    status_.insert("pathDuration", camera_path_ ? camera_path_->duration() : 0);
    playCameraPath(false); seekCameraPath(0);
  }
  emit statusChanged(); return result;
}
void GaussianSceneViewport::seekCameraPath(double seconds) {
  if (!scene_ || !camera_path_ || !std::isfinite(seconds)) return;
  navigationChanged();
  const auto time = std::clamp(seconds, 0.0, camera_path_->duration());
  saved_camera_ = camera_path_->sample(time);
  yaw_ = pitch_ = dolly_ = pan_x_ = pan_y_ = 0;
  status_.remove("activeViewId"); status_.insert("pathSeconds", time);
  requestFrame();
}
void GaussianSceneViewport::playCameraPath(bool play) {
  if (!play && !playback_timer_.isActive()) return;
  if (play && camera_path_) {
    playback_start_ = status_.value("pathSeconds").toDouble();
    if (playback_start_ >= camera_path_->duration()) playback_start_ = 0;
    playback_elapsed_.restart(); playback_timer_.start();
  } else playback_timer_.stop();
  status_.insert("pathPlaying", playback_timer_.isActive()); emit statusChanged();
}
QVariantMap GaussianSceneViewport::exportFilm(const QString& destination, int maximum_edge) {
  if(scene_seconds_) return {{"ok",false},{"error","Use Export 4D film to choose the scene-time range and film duration."}};
  if (!scene_ || !camera_path_ || film_progress_timer_.isActive()) return {{"ok", false}, {"error", "Save a camera path first and wait for any current film export."}};
  return submitFilm(destination,maximum_edge,*camera_path_);
}
QVariantMap GaussianSceneViewport::exportTimedFilm(const QString& destination, const QVariantMap& settings, int maximum_edge) {
  const auto fail=[](const QString& error) { return QVariantMap{{"ok",false},{"error",error}}; };
  if(!scene_ || !scene_seconds_ || film_progress_timer_.isActive()) return fail("Load a 4D artifact and wait for any current film export.");
  const auto json=QJsonObject::fromVariantMap(settings);
  const auto duration=json.value("durationSeconds").toDouble(-1), fps=json.value("fps").toDouble(-1);
  const auto start=json.value("startSeconds").toDouble(-1), end=json.value("endSeconds").toDouble(-1);
  const auto available=double(scene_->local_time_range[1])-scene_->local_time_range[0];
  if(!std::isfinite(duration) || duration<0.5 || duration>600 ||
      (fps!=24 && fps!=25 && fps!=30 && fps!=60) || !std::isfinite(start) || !std::isfinite(end) ||
      start<0 || end<0 || start>available || end>available) return fail("Choose scene times inside the capture, a 0.5–600 second film and 24/25/30/60 fps.");
  GaussianCameraPath path;
  if(json.value("cameraMotion")=="saved-path") {
    if(!camera_path_) return fail("Save a camera path before choosing path motion.");
    path=*camera_path_;
  } else if(json.value("cameraMotion")=="current-camera") {
    path.cameras={fullCamera(),fullCamera()};
    path.view_ids={"current-camera","current-camera"};
  } else return fail("Choose current camera or saved camera path.");
  path.segment_seconds=duration/static_cast<double>(path.cameras.size()-1); path.fps=static_cast<int>(fps);
  const auto first=scene_->time_origin_seconds+scene_->local_time_range[0];
  return submitFilm(destination,maximum_edge,path,GaussianFilmSceneTime{first+start,first+end});
}
QVariantMap GaussianSceneViewport::submitFilm(const QString& destination, int maximum_edge,
    const GaussianCameraPath& path, std::optional<GaussianFilmSceneTime> scene_time) {
  const auto encoder = QStandardPaths::findExecutable("ffmpeg");
  if (encoder.isEmpty()) return {{"ok", false}, {"error", "Film export requires FFmpeg with libx264. No encoder was found."}};
  const QUrl url(destination);
  const auto output = url.isLocalFile() ? url.toLocalFile() : destination;
  const auto scene = scene_;
  film_progress_ = std::make_shared<GaussianFilmProgress>();
  const auto progress = film_progress_;
  status_.insert("filmExporting", true); status_.insert("filmFramesCompleted", 0);
  status_.insert("filmFrameCount", path.frameCount()); status_.remove("filmResult");
  playCameraPath(false); playSceneTime(false);
  film_.setFuture(QtConcurrent::run([scene, path, output, maximum_edge, encoder, progress, scene_time] {
    try { return export_gaussian_film(scene, path, output, maximum_edge, encoder, progress, scene_time); }
    catch (const std::exception& error) { return QVariantMap{{"ok", false}, {"error", QString::fromUtf8(error.what()).left(512)}}; }
    catch (...) { return QVariantMap{{"ok", false}, {"error", "Film export failed unexpectedly."}}; }
  }));
  film_progress_timer_.start(); emit statusChanged(); return {{"ok", true}};
}
void GaussianSceneViewport::cancelFilm() { if (film_progress_) film_progress_->canceled.store(true); }
QVariantMap GaussianSceneViewport::exportPhoto(const QString& destination, int maximum_edge) {
  const QUrl url(destination);
  const auto path = url.isLocalFile() ? url.toLocalFile() : destination;
  const auto suffix = QFileInfo(path).suffix().toLower();
  if (!scene_ || export_in_flight_ || !export_path_.isEmpty() || maximum_edge < 256 || maximum_edge > 8192 ||
      !QFileInfo(path).isAbsolute() || QFileInfo::exists(path) || QFileInfo::exists(path + ".camera.json") ||
      (suffix != "png" && suffix != "jpg" && suffix != "jpeg"))
    return {{"ok", false}, {"error", "Choose a new PNG or JPG destination and an image edge of 256–8192 pixels."}};
  export_path_ = path;
  export_scene_ = scene_;
  export_seconds_ = scene_seconds_;
  const auto base = fullCamera();
  export_camera_ = currentCamera(fit_edge(base.image_size, maximum_edge));
  status_.remove("error");
  renderNext(); emit statusChanged();
  return {{"ok", true}, {"outputPath", path}};
}
void GaussianSceneViewport::geometryChange(const QRectF& next, const QRectF& previous) {
  QQuickItem::geometryChange(next, previous);
  if (next.size() != previous.size()) requestFrame();
}
QSGNode* GaussianSceneViewport::updatePaintNode(QSGNode* old, UpdatePaintNodeData*) {
#if defined(QT_FEATURE_metal) && QT_CONFIG(metal)
  if (!frame_.native_texture || !window() || window()->rendererInterface()->graphicsApi() != QSGRendererInterface::Metal) { delete old; return nullptr; }
  auto* node = static_cast<SceneTextureNode*>(old);
  if (!node) node = new SceneTextureNode;
  if (node->handle != frame_.native_texture) {
    auto* texture = QNativeInterface::QSGMetalTexture::fromNative(
        reinterpret_cast<QT_OBJC_PROTOCOL(MTLTexture)>(static_cast<quintptr>(frame_.native_texture)), window(), frame_.size);
    if (!texture) { delete node; return nullptr; }
    // Destroy the old Qt wrapper before releasing its native lease.
    auto* old_texture = node->texture();
    node->setOwnsTexture(false); node->setTexture(texture); delete old_texture;
    node->lease = frame_.native_lease; node->handle = frame_.native_texture;
  }
  QSizeF size = frame_.size; size.scale(boundingRect().size(), Qt::KeepAspectRatio);
  node->setRect(QRectF(QPointF((width() - size.width()) / 2, (height() - size.height()) / 2), size));
  node->setFiltering(QSGTexture::Linear);
  node->markDirty(QSGNode::DirtyGeometry | QSGNode::DirtyMaterial);
  return node;
#else
  delete old; return nullptr;
#endif
}
}  // namespace photo_editor::desktop
