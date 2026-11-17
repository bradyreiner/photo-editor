#include "gaussian_scene_viewport.h"
#include "gaussian_physical_camera.h"
#include "gaussian_camera_optics.h"
#include "gaussian_saved_views.h"
#include "gaussian_spacetime_artifact.h"
#include "gaussian_film_publication.h"
#include "gaussian_focus_pick.h"
#include "gaussian_camera_navigation.h"
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
#include <QUuid>
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
    const auto resolved = frame.metrics.value("resolvedCamera").toMap();
    const auto actual_camera = resolved.isEmpty() ? camera.description() : resolved;
    frame.metrics.insert("exportCamera",actual_camera);
    QVariantMap receipt{{"schema", "framehouse-gaussian-photo-v1"}, {"artifactSha256", scene->artifact_sha256},
        {"camera", actual_camera}, {"colorSpace", "sRGB"}, {"backend", "metal-gaussian-v1"}, {"outputPath", path}};
    if (camera.photo != GaussianPhotoSettings{}) receipt.insert("schema", "framehouse-gaussian-photo-v2");
    if (camera.physical) receipt.insert("schema", "framehouse-gaussian-photo-v3");
    if (!frame.metrics.value("autofocus").toMap().isEmpty()) {
      receipt.insert("schema","framehouse-gaussian-photo-v4");
      receipt.insert("authoredCamera",camera.description());
      receipt.insert("autofocus",frame.metrics.value("autofocus"));
    }
    receipt.insert("alphaModel",gaussian_alpha_model_name(gaussian_alpha_model(camera)));
    if (gaussian_alpha_model(camera) != GaussianAlphaModel::Brush999) receipt.insert("schema","framehouse-gaussian-photo-v5");
    if (!scene->cleanup_receipt.isEmpty()) receipt.insert("sceneCleanup", scene->cleanup_receipt);
    if (camera.composition && camera.composition->description() != GaussianSceneComposition{}.description()) receipt.insert("sceneObjects", camera.composition->description());
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
  connect(&cleanup_, &QFutureWatcher<GaussianCleanupResult>::finished, this, [this] {
    const auto edited = cleanup_.result();
    cleanup_active_ = false;
    if (cleanup_epoch_ != epoch_) { emit statusChanged(); return; }
    status_.insert("cleanupResult", edited.result);
    if (edited.scene) {
      const auto previous = gaussian_cleanup_mask(*scene_);
      if (cleanup_history_ == -1) { cleanup_undo_.pop_back(); cleanup_redo_.push_back(previous); }
      else {
        if (cleanup_history_ == 1) cleanup_redo_.pop_back(); else cleanup_redo_.clear();
        cleanup_undo_.push_back(previous);
        if (cleanup_undo_.size() > 20) cleanup_undo_.erase(cleanup_undo_.begin());
      }
      scene_ = edited.scene;
      status_.insert("pointCount", static_cast<qulonglong>(scene_->points.size()));
      // Discard every old publication, including interactive previews and focus picks.
      ++epoch_;
      if (render_canceled_ && !export_in_flight_) render_canceled_->store(true);
      frame_ = {}; update(); requestFrame();
    }
    emit statusChanged();
  });
  connect(&focus_pick_, &QFutureWatcher<QVariantMap>::finished, this, [this] {
    auto result = focus_pick_.result();
    focus_pick_active_ = false;
    if (focus_pick_epoch_ != epoch_ || focus_pick_request_ != requested_ ||
        !scene_ || !isVisible() || (focus_pick_canceled_ && focus_pick_canceled_->load())) { emit statusChanged(); return; }
    const auto target_coordinates = result.value("worldPoint").toList();
    const bool has_coordinates = target_coordinates.size() == 3;
    const auto target = has_coordinates ? QVector3D(target_coordinates[0].toFloat(), target_coordinates[1].toFloat(),
        target_coordinates[2].toFloat()) : QVector3D{};
    const bool has_target = has_coordinates && std::isfinite(target.x()) && std::isfinite(target.y()) && std::isfinite(target.z());
    if (result.value("ok").toBool() && focus_pick_orbit_ && !has_target)
      result = {{"ok", false}, {"error", "The selected surface has no valid orbit position."}};
    if (result.value("ok").toBool() && focus_pick_orbit_ && has_target) {
      // Selecting an orbit subject changes the navigation pivot, never the
      // camera's framing, optical focus or exposure.
      saved_camera_ = fullCamera();
      yaw_ = pitch_ = dolly_ = pan_x_ = pan_y_ = 0;
      orbit_target_ = target;
      playCameraPath(false);
      result.insert("orbitTargetSelected", true);
    } else if (result.value("ok").toBool()) {
      const auto camera = fullCamera();
      auto photo = camera.photo;
      photo.lens.focus_distance = result.value("focusDistance").toFloat();
      photo.lens.auto_focus = false;
      if (!photo.valid()) result = {{"ok", false}, {"error", "This surface is too close for the current aperture or barrel distance. Reduce them and pick again."}};
      else {
        auto physical = camera.physical ? camera.physical->settings : GaussianPhysicalCameraSettings{};
        physical.focus_distance_m = result.value("focusDistance").toDouble()*physical.metres_per_scene_unit;
        QVariantMap applied;
        if (camera.physical) {
          auto base = camera; base.photo = photo;
          const auto configured = make_gaussian_physical_camera(base,base.image_size,physical);
          if (!configured.camera) applied = {{"ok",false},{"error",configured.error}};
          else {
            playCameraPath(false); saved_camera_ = *configured.camera; status_.remove("activeViewId");
            yaw_ = pitch_ = dolly_ = pan_x_ = pan_y_ = 0;
            cameraAdjustmentChanged(); requestFrame(); applied = {{"ok",true}};
          }
        } else applied = setPhotoSettings(photo.description());
        if (!applied.value("ok").toBool()) result = applied;
        else if (has_target) orbit_target_ = target;
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
          render_active_ && !export_in_flight_ && render_epoch_ == epoch_ &&
          (render_interactive_ || (submitted_ == requested_ && !navigating_)) &&
          (frame_epoch_ != render_epoch_ || submitted_ >= frame_request_) && isVisible() &&
          render_canceled_ && !render_canceled_->load()) {
        frame_ = std::move(*partial);
        frame_epoch_ = render_epoch_; frame_request_ = submitted_;
        status_.insert("state", "ready");
        status_.insert("metrics", frame_.metrics);
        status_.insert("publishedFrames", status_.value("publishedFrames").toInt() + 1);
        status_.insert("publishedPhase", render_interactive_ ? "live" : "refining");
        status_.insert("progressiveFramesPublished", status_.value("progressiveFramesPublished").toInt() + 1);
        update();
        // Finish at least one visible live prefix before rescheduling pending
        // camera input. Canceling on every pointer event would starve previews.
        if (render_interactive_ && submitted_ != requested_) render_canceled_->store(true);
      }
    }
    emit statusChanged();
  });
  navigation_settle_timer_.setInterval(180);
  navigation_settle_timer_.setSingleShot(true);
  connect(&navigation_settle_timer_, &QTimer::timeout, this, [this] {
    if (pointer_navigation_) return;
    navigating_ = false;
    optical_adjusting_ = false;
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
    auto result = film_.result();
    auto receipt = result.value("receipt").toMap();
    auto focus = receipt.value("autofocus").toMap();
    if (focus.contains("samples")) {
      focus.insert("sampleCount",focus.value("samples").toList().size());
      focus.remove("samples");  // Full per-frame trace remains in the movie sidecar.
      receipt.insert("autofocus",focus);
    }
    if (receipt.contains("frames")) { receipt.insert("frameIntegrityCount",receipt.value("frames").toList().size()); receipt.remove("frames"); }
    if (!receipt.isEmpty()) result.insert("receipt",receipt);
    status_.insert("filmResult", result); emit statusChanged();
  });
  connect(this, &QQuickItem::visibleChanged, this, [this] {
    if (isVisible()) requestFrame(); else {
      if (focus_pick_canceled_) focus_pick_canceled_->store(true);
      if (render_canceled_ && !export_in_flight_) render_canceled_->store(true);
      navigation_settle_timer_.stop(); navigating_ = pointer_navigation_ = optical_adjusting_ = false;
      playSceneTime(false); playCameraPath(false);
    }
  });
  connect(&mesh_import_, &QFutureWatcher<MeshImportResult>::finished,this,[this] {
    const auto imported = mesh_import_.result(); const auto& loaded = imported.geometry; mesh_import_active_ = false;
    if (mesh_import_source_ != source_key_ || !scene_ || !composition_) { emit statusChanged(); return; }
    QVariantMap result;
    if (!loaded.mesh) result = {{"ok",false},{"error",loaded.error}};
    else {
      auto next = *composition_; const auto camera = fullCamera();
      GaussianSceneObject object; object.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
      object.name = QStringLiteral("Imported mesh %1").arg(next.objects.size()+1); object.kind = 2;
      object.mesh_sha256 = loaded.mesh->sha256; object.mesh = loaded.mesh; object.position = {}; object.size = {1,1,1};
      if (imported.materials.assets) {
        object.material_assets = imported.materials.assets; object.material_set_sha256 = object.material_assets->sha256;
        object.materials = object.material_assets->materials; object.color = {1,1,1};
      }
      if (!mesh_import_preserve_) {
        const auto extent = loaded.mesh->upper-loaded.mesh->lower;
        const float depth = std::clamp(scene_->focus_distance,0.01F,1e6F);
        const float scale = std::clamp(depth*0.25F/std::max({extent.x(),extent.y(),extent.z(),1e-10F}),0.0001F,1e6F);
        object.size = {scale,scale,scale}; object.position = camera.position+camera.forward*depth-(loaded.mesh->lower+loaded.mesh->upper)*(0.5F*scale);
      }
      next.objects.push_back(object);
      if (next.lights.empty()) {
        GaussianSceneLight key; key.id = QUuid::createUuid().toString(QUuid::WithoutBraces); key.name = "Key light";
        key.vector = (camera.right*0.5F-camera.down*0.8F-camera.forward).normalized(); next.lights.push_back(key);
      }
      result = applyComposition(next);
      if (result.value("ok").toBool()) { result.insert("id",object.id); result.insert("triangles",static_cast<qulonglong>(loaded.mesh->triangles.size())); result.insert("skippedDegenerate",loaded.mesh->skipped_degenerate); if (object.material_assets) { result.insert("materials",static_cast<int>(object.materials.size())); result.insert("notices",object.material_assets->warnings); } }
    }
    status_.insert("meshImportResult",result); emit statusChanged();
  });
  connect(&load_, &QFutureWatcher<GaussianSceneLoad>::finished, this, [this] {
    const auto result = load_.result();
    load_active_ = false;
    if (load_epoch_ != epoch_) { loadScene(); return; }
    if (!result.scene) { status_.insert("state", "failed"); status_.insert("error", result.error); emit statusChanged(); return; }
    scene_ = result.scene;
    const auto& composition = result.composition;
    if (!composition.error.isEmpty()) { scene_.reset(); status_.insert("state", "failed"); status_.insert("error", composition.error); emit statusChanged(); return; }
    composition_ = composition.composition; composition_receipt_ = composition.receipt;
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
        status_.insert("lastExportCamera", result.metrics.value("exportCamera",result.metrics.value("camera")));
        status_.insert("lastExportSceneSeconds", result.metrics.value("sceneSeconds"));
        status_.insert("lastExportRenderMetrics", result.metrics);
      } else if (render_interactive_ || submitted_ == requested_) {
        frame_ = result;
        frame_epoch_ = render_epoch_; frame_request_ = submitted_;
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
  if (mesh_import_canceled_) mesh_import_canceled_->store(true);
  if (cleanup_canceled_) cleanup_canceled_->store(true);
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
  result.insert("importingMesh",mesh_import_active_);
  QStringList material_notices;
  if (composition_) for (const auto& object : composition_->objects) if (object.material_assets) material_notices.append(object.material_assets->warnings);
  material_notices.removeDuplicates(); result.insert("materialNotices",material_notices);
  result.insert("cleanupBusy", cleanup_active_);
  result.insert("cleanupAvailable", scene_ && scene_->temporal.empty());
  result.insert("cleanupCanUndo", !cleanup_undo_.empty() && !cleanup_active_);
  result.insert("cleanupCanRedo", !cleanup_redo_.empty() && !cleanup_active_);
  result.insert("originalPointCount", scene_ ? static_cast<qulonglong>(gaussian_cleanup_source(scene_)->points.size()) : 0);
  auto cleanup_summary = scene_ ? scene_->cleanup_receipt : QVariantMap{};
  cleanup_summary.remove("visibility");  // Keep the per-vertex mask out of QML/automation snapshots.
  result.insert("sceneCleanup", cleanup_summary);
  result.insert("sceneComposition", composition_ ? composition_->description() : GaussianSceneComposition{}.description());
  result.insert("compositionCanUndo", !composition_undo_.empty()); result.insert("compositionCanRedo", !composition_redo_.empty());
  result.insert("cameraIndex", camera_index_);
  const bool valid_camera_index = scene_ && camera_index_ >= 0 && static_cast<size_t>(camera_index_) < scene_->cameras.size();
  const auto capture_camera = valid_camera_index ? scene_->cameras[static_cast<size_t>(camera_index_)] : GaussianCamera{};
  result.insert("navigationRollDegrees",valid_camera_index ? gaussian_camera_relative_roll_degrees(capture_camera,fullCamera()) : 0.0);
  result.insert("exporting", export_in_flight_ || !export_path_.isEmpty());
  result.insert("navigating", navigating_);
  result.insert("cameraAdjusting", navigating_ && optical_adjusting_);
  result.insert("orbitPicking", focus_pick_active_ && focus_pick_orbit_);
  if (scene_ && orbit_target_) {
    const auto camera = fullCamera();
    const auto relative = *orbit_target_ - camera.position;
    const float depth = QVector3D::dotProduct(relative, camera.forward);
    result.insert("orbitTarget", QVariantList{orbit_target_->x(), orbit_target_->y(), orbit_target_->z()});
    if (depth > 0.01F) {
      const auto pixel = gaussian_lens_display_pixel(camera, QPointF(
          camera.cx + camera.fx * camera.photo.focal_scale * QVector3D::dotProduct(relative, camera.right) / depth,
          camera.cy + camera.fy * camera.photo.focal_scale * QVector3D::dotProduct(relative, camera.down) / depth));
      result.insert("orbitTargetView", QVariantList{pixel.x()/camera.image_size.width(), pixel.y()/camera.image_size.height()});
    }
  }
  result.insert("photoSettings", scene_ ? fullCamera().photo.description() : GaussianPhotoSettings{}.description());
  result.insert("lensSettings", scene_ ? fullCamera().photo.lens.description() : GaussianLensSettings{}.description());
  const bool focus_current = scene_ && !frame_.size.isEmpty() && frame_request_ == requested_ && frame_epoch_ == epoch_ &&
      frame_.metrics.value("camera").toMap() == currentCamera(frame_.size).description();
  result.insert("autofocus",focus_current ? frame_.metrics.value("autofocus").toMap() : QVariantMap{});
  const bool lighting_preview = focus_current && frame_.metrics.value("progressivePreview").toBool();
  result.insert("lightingPreviewSamples",lighting_preview ? frame_.metrics.value("softLightSamples").toInt() : 0);
  result.insert("lightingPreviewTargetSamples",lighting_preview ? frame_.metrics.value("targetLightingSamples").toInt() : 0);
  result.insert("sceneFocusDistance", scene_ ? scene_->focus_distance : 1);
  result.insert("imageAspectRatio", scene_ ? double(fullCamera().image_size.width()) / fullCamera().image_size.height() : 1.6);
  result.insert("physicalCamera", scene_ && fullCamera().physical ? fullCamera().physical->settings.description() : QVariantMap{});
  result.insert("refinementSamplesCompleted", render_samples_completed_ ? render_samples_completed_->load() : 0);
  result.insert("refinementSamplesTotal", render_samples_total_);
  result.insert("lightingBatchesCompleted",render_lighting_batches_ ? render_lighting_batches_->load() : 0);
  result.insert("softLightRefining",render_active_ && !render_interactive_ && render_light_target_ > 1);
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
  if (cleanup_canceled_) cleanup_canceled_->store(true);
  cleanup_undo_.clear(); cleanup_redo_.clear();
  if (mesh_import_canceled_) mesh_import_canceled_->store(true);
  composition_.reset(); composition_receipt_.clear(); composition_undo_.clear(); composition_redo_.clear();
  if (focus_pick_canceled_) focus_pick_canceled_->store(true);
  if (render_canceled_ && !export_in_flight_) render_canceled_->store(true);
  navigation_settle_timer_.stop(); navigating_ = pointer_navigation_ = optical_adjusting_ = false;
  playSceneTime(false); playCameraPath(false); cancelFilm(); camera_path_.reset();
  source_key_ = key;
  source_ = {{"projectPath", source.value("projectPath")}, {"artifactPath", source.value("artifactPath")}, {"hash", hash},
      {"spacetime",spacetime}, {"imported",source.value("importedArtifact").toBool()}};
  ++epoch_;
  if (canceled_) canceled_->store(true);
  scene_.reset(); scene_seconds_.reset(); frame_ = {}; camera_index_ = 0; saved_camera_.reset();
  orbit_target_.reset();
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
      auto result = load_gaussian_spacetime_artifact(artifact,source.value("hash").toString(),canceled);
      if (result.scene) {
        result.composition = load_gaussian_composition(root,result.scene->artifact_sha256,true,canceled);
        if (!result.composition.error.isEmpty()) { result.error = result.composition.error; result.scene.reset(); }
      }
      return result;
    }
    const auto cameras = QDir(root).filePath("dataset/sparse");
    if (root.isEmpty() || QFileInfo(artifact).canonicalFilePath() != QDir(root).filePath("trained/scene.ply") ||
        (!source.value("imported").toBool() && QFileInfo(cameras).canonicalFilePath() != cameras))
      return GaussianSceneLoad{{}, "Scene files must remain inside the verified project."};
    auto result = restore_gaussian_cleanup(load_gaussian_scene(artifact, source.value("imported").toBool() ? QString() : cameras,
        source.value("hash").toString(), canceled), root, canceled);
    if (result.scene) {
      result.composition = load_gaussian_composition(root,result.scene->artifact_sha256,true,canceled);
      if (!result.composition.error.isEmpty()) { result.error = result.composition.error; result.scene.reset(); }
    }
    return result;
    } catch (const std::exception& error) {
      return GaussianSceneLoad{{}, QStringLiteral("Scene loading failed: %1").arg(QString::fromUtf8(error.what()).left(512))};
    } catch (...) { return GaussianSceneLoad{{}, "Scene loading failed unexpectedly."}; }
  }));
}
GaussianCamera GaussianSceneViewport::fullCamera() const {
  auto base = saved_camera_.value_or(scene_->cameras[static_cast<size_t>(camera_index_)]);
  base.composition = composition_;
  if (yaw_ == 0 && pitch_ == 0 && dolly_ == 0 && pan_x_ == 0 && pan_y_ == 0) return base;
  // A photographed subject supplies the orbit/pan distance. A background-heavy
  // imported scene can have a median depth many metres behind a focused face.
  const float navigation_focus = base.physical || base.photo.lens.aperture_radius > 0
      ? base.photo.lens.focus_distance : scene_->focus_distance;
  if (orbit_target_) return navigate_gaussian_camera_around(base, *orbit_target_, yaw_, pitch_, dolly_, pan_x_, pan_y_);
  return navigate_gaussian_camera(base, navigation_focus, yaw_, pitch_, dolly_, pan_x_, pan_y_);
}
GaussianCamera GaussianSceneViewport::currentCamera(QSize size) const {
  auto camera = fullCamera();
  if (size.isValid()) return camera.resized(size);
  const double scale = window() ? window()->devicePixelRatio() : 1.0;
  QSizeF display = camera.image_size;
  display.scale(QSizeF(width() * scale, height() * scale), Qt::KeepAspectRatio);
  const auto edge = std::clamp(static_cast<int>(std::max(display.width(), display.height())), 256, navigating_ ? 720 : 4096);
  camera = camera.resized(fit_edge(camera.image_size, edge));
  // Camera adjustments retain real optical response. A short, low-resolution
  // pupil integration gives useful feedback instead of hiding depth of field
  // behind the pinhole draft used for orbit/pan navigation.
  if (navigating_ && optical_adjusting_) {
    camera.photo.lens.samples = std::min(32, camera.photo.lens.samples);
    if (camera.composition && camera.composition->softLighting()) {
      auto draft = std::make_shared<GaussianSceneComposition>(*camera.composition);
      draft->light_samples = std::min(16, draft->light_samples);
      camera.composition = std::move(draft);
    }
  }
  return camera;
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
void GaussianSceneViewport::beginCameraAdjustment() {
  if (focus_pick_canceled_) focus_pick_canceled_->store(true);
  pointer_navigation_ = true;
  cameraAdjustmentChanged();
}
void GaussianSceneViewport::endCameraAdjustment() { endNavigation(); }
QVariantMap GaussianSceneViewport::describeCameraOptics(double permissible_coc_microns) const {
  if (!scene_) return {{"ok",false}};
  auto camera=fullCamera();
  const bool current=!frame_.size.isEmpty() && frame_request_==requested_ && frame_epoch_==epoch_ &&
      frame_.metrics.value("camera").toMap()==currentCamera(frame_.size).description();
  if (current && camera.photo.lens.auto_focus) {
    const auto resolved=gaussian_saved_camera(frame_.metrics.value("resolvedCamera").toMap());
    if (resolved) camera=*resolved;
  }
  return gaussian_camera_optics(camera,permissible_coc_microns);
}
QVariantMap GaussianSceneViewport::pickFocusAt(double x, double y) {
  if (!scene_ || focus_pick_active_ || navigating_ || !isVisible() || !frame_.native_texture ||
      frame_request_ != requested_ || frame_epoch_ != epoch_ ||
      frame_.metrics.value("camera").toMap() != currentCamera().description())
    return {{"ok", false}, {"error", "Wait for the current camera preview, then pick a surface."}};
  const auto pixel = gaussian_focus_pixel(frame_.size, QSizeF(width(), height()), QPointF(x,y));
  if (!pixel) return {{"ok", false}, {"error", "Pick inside the image, not the empty border."}};
  const auto scene = scene_; auto camera = currentCamera(); const auto seconds = scene_seconds_;
  if (camera.photo.lens.auto_focus) {
    const auto resolved = gaussian_saved_camera(frame_.metrics.value("resolvedCamera").toMap());
    if (!resolved) return {{"ok",false},{"error","Wait for the autofocus camera before picking a surface."}};
    camera = *resolved; camera.composition = composition_;
  }
  focus_pick_epoch_ = epoch_; focus_pick_request_ = requested_;
  focus_pick_canceled_ = std::make_shared<std::atomic_bool>(false);
  const auto canceled = focus_pick_canceled_;
  focus_pick_active_ = true;
  focus_pick_orbit_ = false;
  status_.remove("focusPickError"); status_.remove("focusPickResult");
  focus_pick_.setFuture(QtConcurrent::run([scene,camera,pixel,seconds,canceled] {
    try { return pick_gaussian_focus(*scene,camera,*pixel,seconds,canceled); }
    catch (const std::exception& error) { return QVariantMap{{"ok",false},{"error",QString::fromUtf8(error.what()).left(256)}}; }
    catch (...) { return QVariantMap{{"ok",false},{"error","Focus estimation failed."}}; }
  }));
  emit statusChanged();
  return {{"ok",true}};
}
QVariantMap GaussianSceneViewport::setFocusMode(const QString& mode) {
  if (!scene_ || (mode != "manual" && mode != "auto-centre")) return {{"ok",false},{"error","Choose manual focus or centre autofocus after loading the scene."}};
  auto camera = saved_camera_.value_or(scene_->cameras[static_cast<size_t>(camera_index_)]);
  const bool automatic = mode == "auto-centre";
  if (camera.photo.lens.auto_focus == automatic) return {{"ok",true}};
  if (!automatic && frame_request_ == requested_ && frame_epoch_ == epoch_ && !frame_.size.isEmpty() &&
      frame_.metrics.value("camera").toMap() == currentCamera(frame_.size).description()) {
    const auto resolved = gaussian_saved_camera(frame_.metrics.value("resolvedCamera").toMap());
    if (resolved) {
      if (camera.physical && resolved->physical) {
        auto physical = camera.physical->settings; physical.focus_distance_m = resolved->physical->settings.focus_distance_m;
        const auto configured = make_gaussian_physical_camera(camera,camera.image_size,physical);
        if (!configured.camera) return {{"ok",false},{"error",configured.error}};
        camera = *configured.camera;
      } else if (!camera.physical) camera.photo.lens.focus_distance = resolved->photo.lens.focus_distance;
    }
  }
  camera.photo.lens.auto_focus = automatic;
  if (!camera.photo.valid()) return {{"ok",false},{"error","The selected focus is outside the current lens range."}};
  playCameraPath(false); saved_camera_ = camera; status_.remove("activeViewId");
  cameraAdjustmentChanged(); requestFrame(); return {{"ok",true}};
}
QVariantMap GaussianSceneViewport::setPhotoSettings(const QVariantMap& settings) {
  if (!scene_) return {{"ok", false}, {"error", "Wait for the scene before changing photo settings."}};
  const auto photo = gaussian_photo_settings(settings);
  if (!photo) return {{"ok", false}, {"error", "Invalid photo, lens or film settings. Check the control ranges and focus distance."}};
  auto camera = saved_camera_.value_or(scene_->cameras[static_cast<size_t>(camera_index_)]);
  if (camera.physical && (camera.photo.focal_scale != photo->focal_scale ||
      camera.photo.lens.focus_distance != photo->lens.focus_distance || camera.photo.lens.aperture_radius != photo->lens.aperture_radius))
    return {{"ok",false},{"error","Use the physical camera controls for focal length, focus and f-stop, or return to scene-unit controls."}};
  if (camera.photo == *photo) return {{"ok", true}};
  const bool optical_change = camera.photo.focal_scale != photo->focal_scale || camera.photo.lens != photo->lens;
  if (optical_change) {
    camera = fullCamera();
    yaw_ = pitch_ = dolly_ = pan_x_ = pan_y_ = 0;
  }
  playCameraPath(false);
  camera.photo = *photo;
  saved_camera_ = camera;
  status_.remove("activeViewId");
  // Film/tone edits do not move the camera. Keep the resolved image size and
  // lens state so the renderer can reuse its completed pre-film image.
  if (optical_change) cameraAdjustmentChanged();
  requestFrame();
  return {{"ok", true}};
}
QVariantMap GaussianSceneViewport::orbitAroundAt(double x, double y) {
  const auto result = pickFocusAt(x, y);
  if (result.value("ok").toBool()) { focus_pick_orbit_ = true; emit statusChanged(); }
  return result;
}
QVariantMap GaussianSceneViewport::setPhysicalCamera(const QVariantMap& settings) {
  if (!scene_) return {{"ok",false},{"error","Wait for the scene before configuring a physical camera."}};
  const auto physical = gaussian_physical_camera_settings(settings);
  if (!physical) return {{"ok",false},{"error","Enter a positive scene scale and valid sensor, focal length, f-stop and focus distance."}};
  const auto base = fullCamera();
  const auto configured = make_gaussian_physical_camera(base,base.image_size,*physical);
  if (!configured.camera) return {{"ok",false},{"error",configured.error}};
  playCameraPath(false);
  saved_camera_ = *configured.camera;
  yaw_ = pitch_ = dolly_ = pan_x_ = pan_y_ = 0;
  status_.remove("activeViewId");
  cameraAdjustmentChanged(); requestFrame();
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
  optical_adjusting_ = false;
  navigation_settle_timer_.start();
}
void GaussianSceneViewport::cameraAdjustmentChanged() {
  navigating_ = true;
  optical_adjusting_ = true;
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
  const bool pinhole_draft = render_interactive_ && !optical_adjusting_;
  const auto camera = path.isEmpty() ? currentCamera() : export_camera_;
  export_path_.clear(); export_in_flight_ = !path.isEmpty();
  const auto scene = path.isEmpty() ? scene_ : export_scene_;
  const auto seconds = path.isEmpty() ? scene_seconds_ : export_seconds_;
  if (!path.isEmpty()) export_scene_.reset();
  const auto renderer = renderer_;
  render_canceled_ = std::make_shared<std::atomic_bool>(false);
  render_samples_completed_ = std::make_shared<std::atomic_int>(0);
  render_tiles_completed_ = std::make_shared<std::atomic_int>(0);
  render_lighting_batches_ = std::make_shared<std::atomic_int>(0);
  render_light_target_ = camera.composition && camera.composition->softLighting() ? (pinhole_draft ? 8 : camera.composition->light_samples) : 1;
  const bool lens_tiles = camera.photo.lens.ray_mapping();
  const int tile_edge = lens_tiles ? 1024 : 2048;
  const bool tiled = camera.image_size.width() > 4096 || camera.image_size.height() > 4096 ||
      (lens_tiles && std::max(camera.image_size.width(),camera.image_size.height()) > 2048);
  render_tiles_total_ = tiled && (!path.isEmpty() || (lens_tiles && std::max(camera.image_size.width(),camera.image_size.height()) > 2048)) ? ((camera.image_size.width()+tile_edge-1)/tile_edge)*
      ((camera.image_size.height()+tile_edge-1)/tile_edge) : 0;
  render_samples_total_ = !pinhole_draft && camera.photo.lens.aperture_radius > 0 ? camera.photo.lens.samples : 1;
  progressive_frames_ = path.isEmpty() && (render_samples_total_ > 1 || render_light_target_ > 1)
      ? std::make_shared<GaussianProgressiveFrames>() : nullptr;
  const GaussianRenderOptions options{render_canceled_, render_samples_completed_, pinhole_draft, 0,
      render_tiles_completed_, progressive_frames_, render_lighting_batches_};
  if (render_samples_total_ > 1 || render_tiles_total_ > 0 || render_light_target_ > 1) refinement_progress_timer_.start();
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
  navigation_settle_timer_.stop(); navigating_ = pointer_navigation_ = optical_adjusting_ = false;
  saved_camera_.reset(); status_.remove("activeViewId");
  orbit_target_.reset();
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
    orbit_target_.reset();
    camera_index_ = view.value("captureCameraIndex").toInt();
    yaw_ = pitch_ = dolly_ = pan_x_ = pan_y_ = 0;
    status_.insert("activeViewId", id); status_.remove("savedViewError");
    requestFrame(); return {{"ok", true}, {"camera", fullCamera().description()}};
  }
  emit statusChanged();
  return {{"ok", false}, {"error", "That saved view is no longer available."}};
}
void GaussianSceneViewport::orbit(double dx, double dy) {
  if (!scene_ || scene_->cameras.empty() || !std::isfinite(dx) || !std::isfinite(dy)) return;
  const auto camera = fullCamera();
  const auto base = saved_camera_.value_or(scene_->cameras[static_cast<size_t>(camera_index_)]);
  const float focus = base.physical || base.photo.lens.aperture_radius > 0 ? base.photo.lens.focus_distance : scene_->focus_distance;
  auto target = orbit_target_.value_or(camera.position+camera.forward*focus);
  const float pivot_depth = QVector3D::dotProduct(target-camera.position,camera.forward);
  if (!std::isfinite(pivot_depth) || pivot_depth < 0.01F || pivot_depth > 1.0e6F) {
    const float safe_focus = std::isfinite(focus) ? std::clamp(focus,0.01F,1.0e6F) : 1.0F;
    target = camera.position+camera.forward*safe_focus;
  }
  const float yaw = static_cast<float>(std::remainder(dx,360.0));
  const float pitch = static_cast<float>(std::remainder(dy,360.0));
  saved_camera_ = rotate_gaussian_camera_around(camera,target,yaw,pitch,0);
  orbit_target_ = target;
  yaw_ = pitch_ = dolly_ = pan_x_ = pan_y_ = 0;
  navigationChanged();
  playCameraPath(false);
  status_.remove("activeViewId");
  requestFrame();
}
void GaussianSceneViewport::look(double yaw_degrees,double pitch_degrees) {
  if (!scene_ || scene_->cameras.empty() || !std::isfinite(yaw_degrees) || !std::isfinite(pitch_degrees)) return;
  const auto camera = fullCamera();
  saved_camera_ = look_gaussian_camera(camera,
      static_cast<float>(std::remainder(yaw_degrees,360.0)),
      static_cast<float>(std::remainder(pitch_degrees,360.0)));
  yaw_ = pitch_ = dolly_ = pan_x_ = pan_y_ = 0;
  navigationChanged();
  playCameraPath(false);
  status_.remove("activeViewId");
  requestFrame();
}
void GaussianSceneViewport::moveCamera(double right,double down,double forward) {
  if (!scene_ || scene_->cameras.empty() || !std::isfinite(right) || !std::isfinite(down) || !std::isfinite(forward)) return;
  const auto camera = fullCamera();
  saved_camera_ = move_gaussian_camera_local(camera,right,down,forward);
  // Free-flight preserves an explicitly picked world-space orbit point; the
  // user can return to that subject after moving back to its neighborhood.
  yaw_ = pitch_ = dolly_ = pan_x_ = pan_y_ = 0;
  navigationChanged();
  playCameraPath(false);
  status_.remove("activeViewId");
  requestFrame();
}
void GaussianSceneViewport::roll(double degrees) {
  if (!scene_ || scene_->cameras.empty() || !std::isfinite(degrees)) return;
  const auto camera = fullCamera();
  saved_camera_ = roll_gaussian_camera(camera,static_cast<float>(std::remainder(degrees,360.0)));
  yaw_ = pitch_ = dolly_ = pan_x_ = pan_y_ = 0;
  navigationChanged();
  playCameraPath(false);
  status_.remove("activeViewId");
  requestFrame();
}
void GaussianSceneViewport::pan(double dx, double dy) {
  if (!scene_ || !std::isfinite(dx) || !std::isfinite(dy)) return;
  auto camera = fullCamera();
  const auto base = saved_camera_.value_or(scene_->cameras[static_cast<size_t>(camera_index_)]);
  const float focus = base.physical || base.photo.lens.aperture_radius > 0 ? base.photo.lens.focus_distance : scene_->focus_distance;
  const auto target = orbit_target_.value_or(base.position + base.forward * focus);
  const float depth = std::max(0.01F, QVector3D::dotProduct(target-camera.position, camera.forward));
  const auto translation = gaussian_camera_pan_translation(camera, depth,
      std::clamp(dx, -1.0, 1.0), std::clamp(dy, -1.0, 1.0));
  camera.position += translation;
  saved_camera_ = camera;
  orbit_target_ = target + translation;
  yaw_ = pitch_ = dolly_ = pan_x_ = pan_y_ = 0;
  navigationChanged();
  playCameraPath(false);
  status_.remove("activeViewId");
  requestFrame();
}
void GaussianSceneViewport::dolly(double steps) {
  if (!scene_ || !std::isfinite(steps)) return;
  const auto camera = fullCamera();
  const auto base = saved_camera_.value_or(scene_->cameras[static_cast<size_t>(camera_index_)]);
  const float focus = base.physical || base.photo.lens.aperture_radius > 0 ? base.photo.lens.focus_distance : scene_->focus_distance;
  const auto target = orbit_target_.value_or(base.position + base.forward * focus);
  saved_camera_ = navigate_gaussian_camera_around(camera, target, 0, 0,
      static_cast<float>(std::clamp(steps, -5.0, 5.0)) * 0.08F, 0, 0);
  orbit_target_ = target;
  yaw_ = pitch_ = dolly_ = pan_x_ = pan_y_ = 0;
  navigationChanged();
  playCameraPath(false);
  status_.remove("activeViewId");
  requestFrame();
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
  orbit_target_.reset();
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
  return submitTimedFilm(destination,settings,maximum_edge,GaussianFilmFormat::Mp4);
}
QVariantMap GaussianSceneViewport::exportFrameSequence(const QString& destination, int maximum_edge) {
  if (scene_seconds_) return {{"ok",false},{"error","Choose the scene-time range with Export 4D film first."}};
  if (!scene_ || !camera_path_ || film_progress_timer_.isActive()) return {{"ok",false},{"error","Save a camera path first and wait for the current export."}};
  return submitFilm(destination,maximum_edge,*camera_path_,{},GaussianFilmFormat::PngSequence);
}
QVariantMap GaussianSceneViewport::exportTimedFrameSequence(const QString& destination, const QVariantMap& settings, int maximum_edge) {
  return submitTimedFilm(destination,settings,maximum_edge,GaussianFilmFormat::PngSequence);
}
QVariantMap GaussianSceneViewport::submitTimedFilm(const QString& destination, const QVariantMap& settings, int maximum_edge, GaussianFilmFormat format) {
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
  return submitFilm(destination,maximum_edge,path,GaussianFilmSceneTime{first+start,first+end},format);
}
QVariantMap GaussianSceneViewport::submitFilm(const QString& destination, int maximum_edge,
    const GaussianCameraPath& path, std::optional<GaussianFilmSceneTime> scene_time, GaussianFilmFormat format) {
  const auto encoder = format == GaussianFilmFormat::Mp4 ? QStandardPaths::findExecutable("ffmpeg") : QString();
  if (format == GaussianFilmFormat::Mp4 && encoder.isEmpty()) return {{"ok", false}, {"error", "MP4 export requires FFmpeg with libx264. No encoder was found."}};
  const QUrl url(destination);
  const auto output = url.isLocalFile() ? url.toLocalFile() : destination;
  const auto scene = scene_;
  auto frozen_path = path;
  for (auto& camera : frozen_path.cameras) camera.composition = composition_;
  film_progress_ = std::make_shared<GaussianFilmProgress>();
  const auto progress = film_progress_;
  status_.insert("filmExporting", true); status_.insert("filmFramesCompleted", 0);
  status_.insert("filmFrameCount", path.frameCount()); status_.remove("filmResult");
  playCameraPath(false); playSceneTime(false);
  film_.setFuture(QtConcurrent::run([scene, frozen_path, output, maximum_edge, encoder, progress, scene_time, format] {
    try { return export_gaussian_film(scene, frozen_path, output, maximum_edge, encoder, progress, scene_time, format); }
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
QVariantMap GaussianSceneViewport::applyComposition(const GaussianSceneComposition& composition, int history) {
  if (!scene_ || !composition_ || cleanup_active_)
    return {{"ok", false}, {"error", "Load a scene and finish its cleanup operation before changing objects."}};
  if (composition.description() == composition_->description()) return {{"ok", true}};
  const auto saved = save_gaussian_composition(source_.value("projectPath").toString(), scene_->artifact_sha256, composition, composition_receipt_);
  if (!saved.error.isEmpty()) return {{"ok", false}, {"error", saved.error}};
  if (history == -1) { composition_undo_.pop_back(); composition_redo_.push_back(*composition_); }
  else {
    if (history == 1) composition_redo_.pop_back(); else composition_redo_.clear();
    composition_undo_.push_back(*composition_);
    if (composition_undo_.size() > 50) composition_undo_.erase(composition_undo_.begin());
  }
  composition_ = saved.composition; composition_receipt_ = saved.receipt;
  playCameraPath(false);
  ++epoch_;
  if (render_canceled_ && !export_in_flight_) render_canceled_->store(true);
  requestFrame(); return {{"ok", true}};
}
QVariantMap GaussianSceneViewport::setSceneComposition(const QVariantMap& value) {
  auto parsed = gaussian_scene_composition(value);
  if (!parsed) return {{"ok", false}, {"error", "Choose valid object transforms, materials and lights (maximum 16 objects and 8 lights)."}};
  for (auto& object : parsed->objects) if (object.kind == 2) {
    if (composition_) for (const auto& previous : composition_->objects)
      if (previous.mesh_sha256 == object.mesh_sha256 && previous.mesh) {
        object.mesh = previous.mesh;
        if (previous.material_set_sha256 == object.material_set_sha256) object.material_assets = previous.material_assets;
        if (object.material_set_sha256.isEmpty() || object.material_assets) break;
      }
    if (!object.mesh) return {{"ok",false},{"error","Import the OBJ before assigning its geometry."}};
  }
  return applyComposition(*parsed);
}
QVariantMap GaussianSceneViewport::importSceneMesh(const QString& input, bool preserve_coordinates, bool import_materials) {
  if (!scene_ || !composition_ || cleanup_active_ || mesh_import_active_ || composition_->objects.size() >= 16)
    return {{"ok",false},{"error","Load a scene, finish any import/cleanup, and keep fewer than 16 objects."}};
  const QUrl url(input); const auto path = url.isLocalFile() ? url.toLocalFile() : input;
  if (QFileInfo(path).suffix().compare("obj",Qt::CaseInsensitive) != 0) return {{"ok",false},{"error","Choose a polygon OBJ file."}};
  mesh_import_active_ = true; mesh_import_source_ = source_key_; mesh_import_preserve_ = preserve_coordinates;
  mesh_import_canceled_ = std::make_shared<std::atomic_bool>(false);
  status_.insert("meshImportResult",QVariantMap{});
  const auto canceled = mesh_import_canceled_; const auto project = source_.value("projectPath").toString();
  mesh_import_.setFuture(QtConcurrent::run([path,project,canceled,import_materials] {
    try {
      auto loaded = load_gaussian_obj(path,canceled); GaussianMaterialLoad materials;
      if (loaded.mesh && import_materials) {
        materials = load_gaussian_obj_materials(path,*loaded.mesh,canceled);
        if (!materials.error.isEmpty()) loaded.error = materials.error;
        else if (materials.assets) loaded.error = store_gaussian_material_assets(project,materials);
      }
      if (loaded.mesh && loaded.error.isEmpty() && !canceled->load()) loaded.error = store_gaussian_mesh_asset(project,loaded);
      if (canceled->load()) loaded.error = "Geometry import canceled.";
      if (!loaded.error.isEmpty()) loaded.mesh.reset();
      loaded.source.clear(); materials.manifest.clear(); materials.texture_sources.clear(); return MeshImportResult{loaded,materials};
    } catch (const std::exception& error) { return MeshImportResult{GaussianMeshLoad{{},QString::fromUtf8(error.what()).left(512),{}},{}}; }
  }));
  emit statusChanged(); return {{"ok",true}};
}
QVariantMap GaussianSceneViewport::addSceneObject(const QString& kind) {
  if (!scene_ || !composition_ || (kind != "sphere" && kind != "box" && kind != "floor"))
    return {{"ok", false}, {"error", "Load a scene and choose Sphere, Box or Floor."}};
  auto next = *composition_; const auto camera = fullCamera();
  const float depth = std::clamp(scene_->focus_distance, 0.01F, 1e6F);
  GaussianSceneObject object;
  object.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
  object.kind = kind == "sphere" ? 0 : 1;
  object.name = (kind == "sphere" ? QStringLiteral("Sphere ") : kind == "floor" ? QStringLiteral("Floor ") : QStringLiteral("Box ")) + QString::number(next.objects.size()+1);
  object.position = camera.position + camera.forward*depth;
  object.size = QVector3D(depth*0.12F,depth*0.12F,depth*0.12F);
  if (kind == "floor") {
    object.size = {depth*0.65F,depth*0.01F,depth*0.65F}; object.roughness = 0.8F;
    QMatrix3x3 orientation;
    for (int row = 0; row < 3; ++row) { orientation(row,0) = camera.right[row]; orientation(row,1) = camera.down[row]; orientation(row,2) = camera.forward[row]; }
    object.rotation = QQuaternion::fromRotationMatrix(orientation).toEulerAngles();
    float support_radius = depth*0.12F;
    if (!next.objects.empty()) {
      const auto& support = next.objects.back(); object.position = support.position;
      const auto rotation = QQuaternion::fromEulerAngles(support.rotation);
      QVector3D half = support.size;
      if (support.kind == 2 && support.mesh) {
        auto center = (support.mesh->lower+support.mesh->upper)*0.5F;
        half = (support.mesh->upper-support.mesh->lower)*0.5F;
        for (int axis = 0; axis < 3; ++axis) { center[axis] *= support.size[axis]; half[axis] *= support.size[axis]; }
        object.position += rotation.rotatedVector(center);
      }
      float radius = 0;
      for (int axis = 0; axis < 3; ++axis) {
        QVector3D basis; basis[axis] = half[axis];
        const float projected = QVector3D::dotProduct(camera.down,rotation.rotatedVector(basis));
        radius += support.kind == 0 ? projected*projected : std::abs(projected);
      }
      support_radius = support.kind == 0 ? std::sqrt(radius) : radius;
    }
    object.position += camera.down*(support_radius+object.size.y());
  }
  next.objects.push_back(object);
  if (next.lights.empty()) {
    GaussianSceneLight key; key.id = QUuid::createUuid().toString(QUuid::WithoutBraces); key.name = "Key light";
    key.vector = (camera.right*0.5F-camera.down*0.8F-camera.forward).normalized(); next.lights.push_back(key);
  }
  const auto result = setSceneComposition(next.description());
  if (!result.value("ok").toBool()) return result;
  return {{"ok", true}, {"id", object.id}};
}
QVariantMap GaussianSceneViewport::addSceneLight(const QString& kind) {
  if (!scene_ || !composition_ || (kind != "directional" && kind != "point" && kind != "rectangle" && kind != "sphere")) return {{"ok", false}, {"error", "Load a scene and choose a light type."}};
  auto next = *composition_; const auto camera = fullCamera();
  GaussianSceneLight light; light.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
  light.kind = kind == "point" ? 1 : kind == "rectangle" ? 2 : kind == "sphere" ? 3 : 0;
  light.name = light.kind == 0 ? "Directional light" : light.kind == 1 ? "Point light" : light.kind == 2 ? "Rectangle light" : "Spherical light";
  light.vector = light.kind ? camera.position + camera.forward*scene_->focus_distance*0.5F : -camera.forward;
  if (light.kind >= 2) {
    const float depth = std::clamp(scene_->focus_distance,0.01F,1e6F);
    light.vector += (camera.right-camera.down)*depth*0.3F; light.radius = depth*0.1F; light.size = {depth*0.35F,depth*0.25F}; light.intensity = 5;
    QMatrix3x3 axes; for (int row = 0; row < 3; ++row) { axes(row,0) = camera.right[row]; axes(row,1) = camera.down[row]; axes(row,2) = camera.forward[row]; }
    light.rotation = QQuaternion::fromRotationMatrix(axes).toEulerAngles();
  }
  next.lights.push_back(light); const auto result = setSceneComposition(next.description());
  if (!result.value("ok").toBool()) return result;
  return {{"ok", true}, {"id", light.id}};
}
QVariantMap GaussianSceneViewport::undoSceneComposition() {
  if (composition_undo_.empty()) return {{"ok", false}, {"error", "No object or light edit to undo."}};
  return applyComposition(composition_undo_.back(), -1);
}
QVariantMap GaussianSceneViewport::redoSceneComposition() {
  if (composition_redo_.empty()) return {{"ok", false}, {"error", "No object or light edit to redo."}};
  return applyComposition(composition_redo_.back(), 1);
}
QVariantMap GaussianSceneViewport::submitCleanup(std::function<QByteArray(const GaussianSceneData&, QByteArray,
    const std::shared_ptr<std::atomic_bool>&)> operation, int history) {
  if (!scene_ || !scene_->temporal.empty() || cleanup_active_ || export_in_flight_ || !export_path_.isEmpty() || film_progress_timer_.isActive())
    return {{"ok", false}, {"error", "Load a static scene and wait for the current edit or export."}};
  const auto original = gaussian_cleanup_source(scene_);
  const auto mask = gaussian_cleanup_mask(*scene_);
  const auto project = source_.value("projectPath").toString();
  cleanup_active_ = true; cleanup_epoch_ = epoch_; cleanup_history_ = history;
  cleanup_canceled_ = std::make_shared<std::atomic_bool>(false);
  const auto canceled = cleanup_canceled_;
  playCameraPath(false);
  status_.remove("cleanupResult");
  cleanup_.setFuture(QtConcurrent::run([original, mask, project, canceled, operation] {
    try {
      const auto next = operation(*original, mask, canceled);
      if (next == mask) return GaussianCleanupResult{{}, {{"ok", true}, {"unchanged", true}}};
      return apply_gaussian_cleanup(original, next, project, canceled);
    } catch (const std::exception& error) {
      return GaussianCleanupResult{{}, {{"ok", false}, {"error", QString::fromUtf8(error.what()).left(512)}}};
    } catch (...) { return GaussianCleanupResult{{}, {{"ok", false}, {"error", "Scene cleanup failed."}}}; }
  }));
  emit statusChanged(); return {{"ok", true}};
}
QVariantMap GaussianSceneViewport::cleanupSelection(double x, double y, double width, double height, bool keep_inside) {
  return cleanupSelection(x,y,width,height,keep_inside,0,std::numeric_limits<double>::infinity());
}
QVariantMap GaussianSceneViewport::cleanupSelection(double x, double y, double width, double height, bool keep_inside, double near_depth, double far_depth) {
  const GaussianCleanupDepthRange depth{near_depth,far_depth};
  if (!depth.valid()) return {{"ok",false},{"error","Enter a nonnegative near depth and a larger far depth."}};
  if (!scene_ || !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(width) || !std::isfinite(height) ||
      width <= 0 || height <= 0 || x < 0 || y < 0 || x + width > 1.000001 || y + height > 1.000001)
    return {{"ok", false}, {"error", "Draw a rectangle inside the scene image."}};
  const auto camera = fullCamera(); const QRectF rect(x, y, width, height);
  return submitCleanup([camera, rect, keep_inside, depth](const auto& original, auto mask, const auto& canceled) {
    return select_gaussian_cleanup(original, mask, camera, rect, keep_inside, canceled, depth);
  });
}
QVariantMap GaussianSceneViewport::cleanupLasso(const QVariantList& points, bool keep_inside) {
  return cleanupLasso(points,keep_inside,0,std::numeric_limits<double>::infinity());
}
QVariantMap GaussianSceneViewport::cleanupLasso(const QVariantList& points, bool keep_inside, double near_depth, double far_depth) {
  const GaussianCleanupDepthRange depth{near_depth,far_depth};
  if (!depth.valid()) return {{"ok",false},{"error","Enter a nonnegative near depth and a larger far depth."}};
  const auto reject = [] { return QVariantMap{{"ok",false},{"error","Draw an outline with at least three points inside the scene image."}}; };
  if (!scene_ || points.size() < 3 || points.size() > 512) return reject();
  QPolygonF polygon;
  const auto numeric = [](const QVariant& value) {
    const auto type = value.metaType().id();
    return type == QMetaType::Double || type == QMetaType::Float || type == QMetaType::Int ||
        type == QMetaType::UInt || type == QMetaType::LongLong || type == QMetaType::ULongLong;
  };
  for (const auto& value : points) {
    const auto point = value.toMap();
    if (point.size() != 2 || !numeric(point.value("x")) || !numeric(point.value("y"))) return reject();
    polygon.append(QPointF(point.value("x").toDouble(),point.value("y").toDouble()));
  }
  if (!valid_gaussian_cleanup_polygon(polygon)) return reject();
  const auto camera = fullCamera();
  return submitCleanup([camera,polygon,keep_inside,depth](const auto& original, auto mask, const auto& canceled) {
    return select_gaussian_cleanup_polygon(original,mask,camera,polygon,keep_inside,canceled,depth);
  });
}
QVariantMap GaussianSceneViewport::cleanupFilter(double minimum_opacity, double maximum_sigma) {
  if (!std::isfinite(minimum_opacity) || minimum_opacity < 0 || minimum_opacity > 1 ||
      !std::isfinite(maximum_sigma) || maximum_sigma < 0 || maximum_sigma > 1e8)
    return {{"ok", false}, {"error", "Choose opacity from 0–1 and a nonnegative RMS radius in scene units."}};
  return submitCleanup([minimum_opacity, maximum_sigma](const auto& original, auto mask, const auto& canceled) {
    return filter_gaussian_cleanup(original, mask, minimum_opacity, maximum_sigma, canceled);
  });
}
QVariantMap GaussianSceneViewport::undoCleanup() {
  if (cleanup_undo_.empty()) return {{"ok", false}, {"error", "No cleanup edit to undo."}};
  const auto previous = cleanup_undo_.back();
  return submitCleanup([previous](const auto&, auto, const auto&) { return previous; }, -1);
}
QVariantMap GaussianSceneViewport::redoCleanup() {
  if (cleanup_redo_.empty()) return {{"ok", false}, {"error", "No cleanup edit to redo."}};
  const auto next = cleanup_redo_.back();
  return submitCleanup([next](const auto&, auto, const auto&) { return next; }, 1);
}
QVariantMap GaussianSceneViewport::resetCleanup() {
  return submitCleanup([](const auto& original, auto, const auto&) { return QByteArray(static_cast<qsizetype>(original.points.size()), '\1'); });
}
QVariantMap GaussianSceneViewport::exportSplat(const QString& destination) {
  if (!scene_ || !scene_->temporal.empty() || cleanup_active_)
    return {{"ok", false}, {"error", "Load a static scene and wait for the current cleanup operation."}};
  const QUrl url(destination); const auto output = url.isLocalFile() ? url.toLocalFile() : destination;
  const auto artifact = source_.value("artifactPath").toString();
  const auto original = gaussian_cleanup_source(scene_); const auto mask = gaussian_cleanup_mask(*scene_);
  const auto current = fullCamera(); const auto camera = current.description(); const auto alpha_model = gaussian_alpha_model(current);
  cleanup_active_ = true; cleanup_epoch_ = epoch_;
  cleanup_canceled_ = std::make_shared<std::atomic_bool>(false); const auto canceled = cleanup_canceled_;
  status_.remove("cleanupResult");
  cleanup_.setFuture(QtConcurrent::run([artifact, output, original, mask, canceled, camera, alpha_model] {
    try { return GaussianCleanupResult{{}, export_gaussian_cleanup(artifact, output, *original, mask, canceled, camera, alpha_model)}; }
    catch (...) { return GaussianCleanupResult{{}, {{"ok", false}, {"error", "Splat export failed."}}}; }
  }));
  emit statusChanged(); return {{"ok", true}};
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
