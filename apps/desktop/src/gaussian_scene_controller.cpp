#include "gaussian_scene_controller.h"
#include "gaussian_ply_validation.h"
#include "gaussian_training_schedule.h"
#include "gaussian_scene_process.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QPointer>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QDataStream>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <QDateTime>
#include <QSettings>
#include <QtConcurrent>

namespace photo_editor::desktop {
namespace {
QString local_path(const QString& value) {
  const QUrl url(value);
  return url.isLocalFile() ? url.toLocalFile() : value;
}
QString binary(const char* override_name, const QString& name) {
  auto configured = qEnvironmentVariable(override_name).trimmed();
  if (configured.isEmpty() && name == "brush_app") configured = QSettings().value("gaussian/trainerPath").toString();
  auto path = configured.isEmpty() ? QStandardPaths::findExecutable(name) : configured;
#ifdef Q_OS_MACOS
  if (path.isEmpty()) path = QStandardPaths::findExecutable(name, {"/opt/homebrew/bin", "/usr/local/bin"});
#endif
  const QFileInfo info(path);
  return info.isFile() && info.isExecutable() ? info.canonicalFilePath() : QString();
}
QVariantMap rejected(const QString& error) { return {{"ok", false}, {"error", error}}; }
}

GaussianSceneController::GaussianSceneController(QObject* parent) : QObject(parent) {
  state_ = {{"schema", "framehouse-gaussian-scene-v1"}, {"state", "idle"}, {"mode", "static-3d"}};
  process_log_timer_.setInterval(100);
  connect(&process_log_timer_, &QTimer::timeout, this, &GaussianSceneController::updateProcessLog);
  connect(&process_, &QFutureWatcher<QVariantMap>::finished, this, [this] {
    process_log_timer_.stop();
    const auto result = process_.result();
    QPointer<GaussianSceneController> guard(this);
    updateProcessLog();
    if (!guard) return;
    process_active_ = false;
    if (result.value("failedToStart").toBool() && !canceled_->load()) {
      fail(QStringLiteral("Cannot start reconstruction backend: %1").arg(result.value("error").toString()));
      return;
    }
    finishStage(result.value("exitCode").toInt(), static_cast<QProcess::ExitStatus>(result.value("exitStatus").toInt()));
  });
  connect(&preparation_, &QFutureWatcher<QVariantMap>::finished, this, [this] {
    preparation_active_ = false;
    if (stage_ == -2) process_log_timer_.stop();
    const auto result = preparation_.result();
    if (canceled_->load()) { fail(QStringLiteral("Scene operation canceled; originals are unchanged.")); return; }
    if (!result.value("ok").toBool()) { fail(result.value("error").toString()); return; }
    if (stage_ == 7) {
      if (result.value("artifactSha256") != state_.value("artifactValidation").toMap().value("artifactSha256")) {
        fail("The 4D artifact does not match its saved integrity receipt."); return;
      }
      state_.insert("artifactValidation", result);
      state_.insert("artifactPath", result.value("artifactPath"));
      state_.insert("splatCount", result.value("pointCount"));
      state_.insert("state", "artifact-ready");
      state_.insert("trainingAvailable", false);
      busy_ = false; publish(); return;
    }
    if (stage_ == 6) {
      const auto expected = state_.value("capture").toMap().value("manifestSha256").toString();
      if (!expected.isEmpty() && expected != result.value("manifestSha256").toString()) {
        fail("The timed capture manifest has changed since the project was saved. The project was not modified."); return;
      }
      state_.insert("capture", result);
      state_.insert("state", "prepared-not-trained");
      state_.insert("sourceCount", result.value("imageCount"));
      state_.insert("trainingAvailable", false);
      busy_ = false;
      if (new_timed_project_) read_only_project_ = false;
      publish();
      return;
    }
    if (stage_ == 5) {
      if (read_only_project_ && result.value("artifactSha256").toString() !=
          state_.value("artifactValidation").toMap().value("artifactSha256").toString()) {
        fail("The trained scene has changed since it was saved. The project was not modified.");
        return;
      }
      state_.insert("artifactValidation", result);
      state_.insert("artifactPath", result.value("artifactPath"));
      state_.insert("splatCount", result.value("vertexCount"));
      state_.insert("state", "trained");
      if (!read_only_project_) state_.insert("iteration", steps_);
      busy_ = false;
      publish();
      return;
    }
    state_.insert("capture", result);
    if (stage_ == -2) {
      sources_ = result.value("videoFrames").toStringList();
      state_.insert("sourceCount", sources_.size());
      state_.insert("video", result.value("video"));
    }
    camera_groups_ = result.value("cameraGroups").toList();
    // Consecutive video frames share one estimated camera. Do not fabricate an
    // EXIF focal prior or optimize an independent intrinsic model per frame.
    if (stage_ == -2) camera_groups_.clear();
    camera_group_ = 0;
    state_.insert("focalPriorFrames", result.value("focalPriorFrames", 0));
    state_.insert("estimatedIntrinsicsFrames", result.value("estimatedIntrinsicsFrames", sources_.size()));
    state_.insert("cameraGroupCount", camera_groups_.size());
    stage_ = 0;
    runStage();
  });
}

void GaussianSceneController::updateProcessLog() {
  if (!process_log_) return;
  QString tail;
  { std::lock_guard lock(process_log_->mutex); tail = process_log_->tail; }
  if (tail == log_tail_) return;
  log_tail_ = std::move(tail);
  static const QRegularExpression progress(QStringLiteral("Refine iter (\\d+), (\\d+) splats"));
  auto matches = progress.globalMatch(log_tail_);
  while (matches.hasNext()) {
    const auto match = matches.next();
    state_.insert("iteration", match.captured(1).toInt());
    state_.insert("splatCount", match.captured(2).toInt());
  }
  static const QRegularExpression evaluation(QStringLiteral("Eval iter (\\d+): PSNR ([0-9.]+), ssim ([0-9.]+)"));
  auto evaluations = evaluation.globalMatch(log_tail_);
  while (evaluations.hasNext()) {
    const auto match = evaluations.next();
    state_.insert("evaluatedIteration", match.captured(1).toInt());
    state_.insert("heldOutPsnr", match.captured(2).toDouble());
    state_.insert("heldOutSsim", match.captured(3).toDouble());
  }
  state_.insert("logTail", log_tail_);
  if (stage_ == -2) {
    static const QRegularExpression frames("(?:^|[\\r\\n])frame=(\\d+)");
    auto found = frames.globalMatch(log_tail_);
    while (found.hasNext()) state_.insert("extractedFrames", found.next().captured(1).toInt());
  }
  emit changed();
}

GaussianSceneController::~GaussianSceneController() {
  if (canceled_) canceled_->store(true);
}

void GaussianSceneController::setCapturePreparer(CapturePreparer prepare) { prepare_ = std::move(prepare); }
void GaussianSceneController::setVideoPreparer(VideoPreparer prepare) { prepare_video_ = std::move(prepare); }
QString GaussianSceneController::suggestedProjectPath(const QString& source) const {
  auto name = QFileInfo(local_path(source)).completeBaseName();
  name.replace(QRegularExpression("[^\\p{L}\\p{N} _-]"), "-");
  if (name.trimmed().isEmpty()) name = "Scene";
  return QDir(QStandardPaths::writableLocation(QStandardPaths::PicturesLocation)).filePath(
      "Looksy Scenes/" + name.left(80) + " " + QDateTime::currentDateTime().toString("yyyy-MM-dd hhmmss-zzz"));
}
QVariantMap GaussianSceneController::setTrainerExecutable(const QString& path) {
  if (busy_ || preparation_active_ || process_active_) return rejected("Finish the active scene before changing its trainer.");
  const QFileInfo file(local_path(path));
  if (!file.isFile() || !file.isExecutable()) return rejected("Choose the installed Brush trainer executable (brush_app).");
  QSettings settings;
  settings.setValue("gaussian/trainerPath", file.canonicalFilePath());
  settings.sync();
  if (settings.status() != QSettings::NoError) return rejected("Cannot save the trainer location.");
  emit changed();
  return {{"ok", true}};
}
void GaussianSceneController::setSpacetimeValidator(SpacetimeValidator validate) { validate_spacetime_ = std::move(validate); }
void GaussianSceneController::setTimedCaptureHandlers(TimedCapturePreparer prepare, TimedCaptureValidator validate) {
  prepare_timed_ = std::move(prepare); validate_timed_ = std::move(validate);
}
QVariantMap GaussianSceneController::snapshot() const {
  auto snapshot = state_;
  snapshot.insert("busy", busy_);
  snapshot.insert("projectPath", project_);
  snapshot.insert("trainerAvailable", !binary("PHOTO_EDITOR_BRUSH_BINARY", "brush_app").isEmpty());
  snapshot.insert("reconstructionAvailable", !binary("PHOTO_EDITOR_COLMAP_BINARY", "colmap").isEmpty());
  snapshot.insert("videoAvailable", bool(prepare_video_) && !binary("PHOTO_EDITOR_FFMPEG_BINARY", "ffmpeg").isEmpty()
      && !binary("PHOTO_EDITOR_FFPROBE_BINARY", "ffprobe").isEmpty());
  return snapshot;
}

bool GaussianSceneController::publish() {
  bool saved = true;
  if (!project_.isEmpty() && !read_only_project_) {
    QSaveFile file(QDir(project_).filePath(QStringLiteral("scene.json")));
    const auto bytes = QJsonDocument::fromVariant(snapshot()).toJson();
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
      saved = false;
      busy_ = false;
      if (canceled_) canceled_->store(true);
      state_.insert("state", "failed");
      state_.insert("error", "Scene status could not be saved. Work stopped; keep this project folder for recovery.");
      state_.insert("persistenceError", state_.value("error"));
    }
  }
  emit changed();
  return saved;
}

QVariantMap GaussianSceneController::startTimedCapture(const QString& manifest, const QString& new_project_folder,
                                                      int maximum_edge) {
  const auto customer_edit_scope = customer_edit_access_.enter();
  if (!customer_edit_scope) return {{QStringLiteral("ok"), false}, {QStringLiteral("errorCode"), QStringLiteral("access_required")}, {QStringLiteral("error"), QStringLiteral("Editing requires an active trial or purchase.")}};
  if (busy_ || preparation_active_ || process_active_) return rejected("A scene operation is already running.");
  if (!prepare_timed_ || !validate_timed_) return rejected("Timed capture preparation is unavailable.");
  const auto source = local_path(manifest);
  const auto target = QDir::cleanPath(local_path(new_project_folder));
  if (!QFileInfo(source).isFile() || QFileInfo(source).isSymLink() || !QFileInfo(target).isAbsolute() ||
      QFileInfo::exists(target) || QFileInfo(target).isSymLink() || maximum_edge < 256 || maximum_edge > 4096)
    return rejected("Choose a timed-capture JSON file, a new absolute project folder, and an image edge from 256 to 4096.");
  // The worker owns output creation. Do not write scene.json before it has
  // validated the input and committed its dataset; cancellation must not race
  // that new-folder check. A committed dataset can also reopen without scene.json.
  project_ = target;
  read_only_project_ = true;
  new_timed_project_ = true;
  busy_ = true;
  stage_ = 6;
  log_tail_.clear();
  state_ = {{"schema", "framehouse-gaussian-scene-v1"}, {"state", "preparing-timed-capture"},
      {"mode", "timed-4d-input"}, {"sourceManifest", source}, {"maximumEdge", maximum_edge},
      {"trainingAvailable", false}, {"originalsModified", false}};
  canceled_ = std::make_shared<std::atomic_bool>(false);
  const auto canceled = canceled_;
  const auto prepare = prepare_timed_;
  const auto validate = validate_timed_;
  preparation_active_ = true;
  preparation_.setFuture(QtConcurrent::run([prepare, validate, source, target, maximum_edge, canceled] {
    const auto result = prepare(source, target, maximum_edge, canceled);
    if (!result.value("ok").toBool()) return result;
    const auto verified = validate(target, canceled);
    if (!verified.value("ok").toBool()) return verified;
    if (result.value("manifestSha256") != verified.value("manifestSha256"))
      return rejected("The timed manifest changed during preparation verification.");
    return verified;
  }));
  emit changed();
  return {{"ok", true}, {"projectPath", target}};
}

QVariantMap GaussianSceneController::startCapture(const QString& source_folder, const QString& new_project_folder,
                                                 int training_steps, int maximum_edge, int max_splats) {
  const auto customer_edit_scope = customer_edit_access_.enter();
  if (!customer_edit_scope) return {{QStringLiteral("ok"), false}, {QStringLiteral("errorCode"), QStringLiteral("access_required")}, {QStringLiteral("error"), QStringLiteral("Editing requires an active trial or purchase.")}};
  if (busy_ || preparation_active_ || process_active_) return rejected("A scene operation is already running.");
  if (!prepare_) return rejected("The canonical capture renderer is unavailable.");
  const auto source = QFileInfo(local_path(source_folder)).canonicalFilePath();
  const auto target = QDir::cleanPath(local_path(new_project_folder));
  if (source.isEmpty() || !QFileInfo(source).isDir() || !QFileInfo(target).isAbsolute() ||
      QFileInfo::exists(target)) return rejected("Choose an existing capture folder and a new, unused project folder.");
  if (QFileInfo::exists(QDir(source).filePath("capture-4d.json")) ||
      QFileInfo::exists(QDir(source).filePath("timed-capture.json")))
    return rejected("This is a timed capture. The static 3D trainer cannot reconstruct motion; use timed-capture preparation and a time-aware trainer.");
  if (training_steps < 200 || training_steps > 100000 || maximum_edge < 256 || maximum_edge > 4096)
    return rejected("Training steps must be 200–100000 and preparation resolution 256–4096 pixels.");
  if (gaussian_training_schedule(training_steps, 8, max_splats).isEmpty())
    return rejected("Choose a splat budget from 100,000 to 2,000,000, within the native viewer limit.");
  QStringList inputs;
  const QSet<QString> extensions{"jpg", "jpeg", "png", "tif", "tiff", "dng", "cr2", "cr3", "nef", "arw", "raf", "rw2", "orf"};
  for (const auto& info : QDir(source).entryInfoList(QDir::Files | QDir::Readable, QDir::Name)) {
    if (extensions.contains(info.suffix().toLower()) && !info.isSymLink()) inputs.append(info.canonicalFilePath());
  }
  if (inputs.size() < 8 || inputs.size() > 2000) return rejected("A static scene needs 8–2000 overlapping photos of the same unmoving subject.");
  return beginCapture(source, target, inputs, training_steps, maximum_edge, max_splats);
}

QVariantMap GaussianSceneController::startVideo(const QString& video, const QString& new_project_folder,
    int training_steps, int maximum_edge, int max_splats, int frame_count) {
  const auto customer_edit_scope = customer_edit_access_.enter();
  if (!customer_edit_scope) return {{QStringLiteral("ok"), false}, {QStringLiteral("errorCode"), QStringLiteral("access_required")}, {QStringLiteral("error"), QStringLiteral("Editing requires an active trial or purchase.")}};
  if (busy_ || preparation_active_ || process_active_) return rejected("A scene operation is already running.");
  if (!prepare_ || !prepare_video_) return rejected("Video capture preparation is unavailable.");
  const QFileInfo source(local_path(video));
  const auto target = QDir::cleanPath(local_path(new_project_folder));
  if (!source.isFile() || source.isSymLink() || !QFileInfo(target).isAbsolute() || QFileInfo::exists(target))
    return rejected("Choose a video file and a new, unused scene folder.");
  if (frame_count < 16 || frame_count > 1920) return rejected("Choose 16–1920 video frames.");
  if (binary("PHOTO_EDITOR_FFMPEG_BINARY", "ffmpeg").isEmpty() || binary("PHOTO_EDITOR_FFPROBE_BINARY", "ffprobe").isEmpty())
    return rejected("Video import needs installed FFmpeg and FFprobe.");
  return beginCapture(source.canonicalFilePath(), target, {}, training_steps, maximum_edge, max_splats, frame_count);
}

QVariantMap GaussianSceneController::beginCapture(const QString& source, const QString& target, const QStringList& inputs,
    int training_steps, int maximum_edge, int max_splats, int video_frames) {
  if (training_steps < 200 || training_steps > 100000 || maximum_edge < 256 || maximum_edge > 4096 ||
      gaussian_training_schedule(training_steps, 8, max_splats).isEmpty())
    return rejected("Choose 200–100000 training steps, a 256–4096 image edge and 100,000–2,000,000 splats.");
  colmap_ = binary("PHOTO_EDITOR_COLMAP_BINARY", QStringLiteral("colmap"));
  trainer_ = binary("PHOTO_EDITOR_BRUSH_BINARY", QStringLiteral("brush_app"));
  if (colmap_.isEmpty() || trainer_.isEmpty()) return rejected("COLMAP and the Brush trainer must be installed or explicitly configured before reconstruction.");
  if (!QDir().mkpath(QDir(target).filePath("images")) || !QDir().mkpath(QDir(target).filePath("sparse")) ||
      !QDir().mkpath(QDir(target).filePath("trained"))) return rejected("Cannot create the new scene project.");
  project_ = QFileInfo(target).canonicalFilePath();
  partial_build_authorized_ = false;
  read_only_project_ = false;
  sources_ = inputs;
  camera_groups_.clear(); camera_group_ = 0;
  steps_ = training_steps;
  maximum_edge_ = maximum_edge;
  max_splats_ = max_splats;
  busy_ = true;
  log_tail_.clear();
  stage_ = video_frames ? -2 : -1;
  canceled_ = std::make_shared<std::atomic_bool>(false);
  state_ = {{"schema", "framehouse-gaussian-scene-v1"}, {"state", "preparing"}, {"mode", "static-3d"},
      {"sourceFolder", source}, {"sourceCount", inputs.size()}, {"trainingSteps", steps_}, {"maximumEdge", maximum_edge_}, {"maxSplats", max_splats_},
      {"trainingColorSpace", "sRGB-display-referred"}, {"originalsModified", false}, {"colmapBinary", colmap_}, {"trainerBinary", trainer_}};
  if (video_frames) {
    state_.insert("state", "extracting-video");
    state_.insert("sourceVideo", source);
    state_.insert("requestedFrames", video_frames);
    state_.insert("extractedFrames", 0);
  }
  if (!publish()) return rejected(state_.value("error").toString());
  const auto prepare = prepare_;
  const auto canceled = canceled_;
  preparation_active_ = true;
  const auto video_prepare = prepare_video_;
  const auto ffmpeg = binary("PHOTO_EDITOR_FFMPEG_BINARY", "ffmpeg"), ffprobe = binary("PHOTO_EDITOR_FFPROBE_BINARY", "ffprobe");
  auto log = std::make_shared<GaussianProcessLog>();
  if (video_frames) { process_log_ = log; process_log_timer_.start(); }
  preparation_.setFuture(QtConcurrent::run([prepare, video_prepare, source, inputs, target, maximum_edge, canceled,
      video_frames, ffmpeg, ffprobe, log] {
    if (!video_frames) return prepare(inputs, target, maximum_edge, canceled);
    const auto extracted = video_prepare(source, target, maximum_edge, video_frames, ffmpeg, ffprobe, canceled, log);
    if (!extracted.value("ok").toBool()) return extracted;
    const auto frames = extracted.value("frames").toStringList();
    if (frames.size() < 8 || frames.size() > video_frames) return rejected("Video did not produce enough usable frames.");
    auto result = prepare(frames, target, maximum_edge, canceled);
    result.insert("videoFrames", frames);
    result.insert("video", extracted);
    return result;
  }));
  return {{"ok", true}, {"projectPath", project_}};
}

void GaussianSceneController::runStage() {
  if (canceled_->load()) { fail("Reconstruction canceled; partial project retained."); return; }
  const QDir root(project_);
  const auto images = root.filePath("images");
  const auto database = root.filePath("features.db");
  QStringList arguments;
  QString program = colmap_;
  switch (stage_) {
    case 0:
      state_.insert("state", "extracting-features");
      arguments = {"feature_extractor", "--image_path", images, "--database_path", database,
          "--FeatureExtraction.use_gpu", "0", "--FeatureExtraction.num_threads", "4",
          "--SiftExtraction.max_image_size", QString::number(maximum_edge_)};
      if (state_.contains("sourceVideo")) arguments.append({"--ImageReader.single_camera", "1", "--ImageReader.camera_model", "SIMPLE_RADIAL"});
      if (!camera_groups_.isEmpty()) {
        const auto group = camera_groups_[camera_group_].toMap();
        const auto relative = QStringLiteral("camera-group-%1.txt").arg(camera_group_, 4, 10, QLatin1Char('0'));
        const auto list = root.filePath(relative);
        if (group.value("imageListPath").toString() != relative || !QFileInfo(list).isFile() ||
            QFileInfo(list).isSymLink() || QFileInfo(list).canonicalFilePath() != list) {
          fail("The prepared camera group list is missing or outside this project."); return;
        }
        const auto prior = group.value("prior").toMap();
        arguments.append({"--image_list_path", list});
        if (prior.value("available").toBool()) {
          arguments.append({"--ImageReader.single_camera", "1", "--ImageReader.camera_model", "SIMPLE_RADIAL",
              "--ImageReader.camera_params", prior.value("parameters").toString()});
        } else arguments.append({"--ImageReader.single_camera_per_image", "1"});
        state_.insert("extractingCameraGroup", camera_group_ + 1);
      }
      break;
    case 1:
      state_.insert("state", "matching-views");
      arguments = {"exhaustive_matcher", "--database_path", database, "--FeatureMatching.use_gpu", "0", "--FeatureMatching.num_threads", "4"};
      if (state_.contains("sourceVideo")) {
        // Dense video is ordered: match nearby frames plus exponentially spaced
        // neighbors, without quadratic all-pairs work or a vocabulary download.
        arguments[0] = "sequential_matcher";
        arguments.append({"--SequentialMatching.overlap", "20", "--SequentialMatching.quadratic_overlap", "1",
            "--SequentialMatching.loop_detection", "0", "--FeatureMatching.guided_matching", "1"});
        state_.insert("matchingStrategy", "video-sequential-guided-20");
      }
      break;
    case 2:
      state_.insert("state", "reconstructing-cameras");
      arguments = {"mapper", "--image_path", images, "--database_path", database, "--output_path", root.filePath("sparse"), "--Mapper.num_threads", "4"};
      break;
    case 3:
    {
      quint64 registered = 0;
      QString selected_model;
      for (const auto& component : QDir(root.filePath("sparse")).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        const auto candidate = root.filePath(QStringLiteral("sparse/%1").arg(component));
        if (QFileInfo(candidate).canonicalFilePath() != candidate ||
            QFileInfo(QDir(candidate).filePath("images.bin")).canonicalFilePath() != QDir(candidate).filePath("images.bin") ||
            QFileInfo(QDir(candidate).filePath("cameras.bin")).canonicalFilePath() != QDir(candidate).filePath("cameras.bin")) continue;
        QFile images_file(QDir(candidate).filePath("images.bin"));
        if (!QFileInfo::exists(QDir(candidate).filePath("cameras.bin")) || !images_file.open(QIODevice::ReadOnly)) continue;
        QDataStream stream(&images_file);
        stream.setByteOrder(QDataStream::LittleEndian);
        quint64 count = 0;
        stream >> count;
        if (stream.status() == QDataStream::Ok && count <= static_cast<quint64>(sources_.size()) && count > registered) {
          registered = count;
          selected_model = candidate;
        }
      }
      state_.insert("registeredViews", QVariant::fromValue(registered));
      state_.insert("unregisteredViews", sources_.size() - static_cast<qsizetype>(registered));
      if (registered < 8 || (!partial_build_authorized_ && registered * 2 < static_cast<quint64>(sources_.size()))) {
        fail(QString("Only %1 of %2 frames connected in the largest reconstruction; at least %3 are required. "
            "Try denser video sampling or a shorter continuous section with clear overlapping views.")
            .arg(registered).arg(sources_.size()).arg(qMax<qsizetype>(8, (sources_.size() + 1) / 2))); return;
      }
      state_.insert("state", "undistorting");
      if (partial_build_authorized_) {
        state_.insert("partialScene", true);
        state_.insert("coverageNotice", QString("Partial scene: %1 of %2 input views. Unreconstructed areas are missing.")
            .arg(registered).arg(sources_.size()));
      }
      arguments = {"image_undistorter", "--image_path", images, "--input_path", selected_model,
          "--output_path", root.filePath("dataset"), "--output_type", "COLMAP", "--max_image_size", QString::number(maximum_edge_)};
      break;
    }
    case 4: {
      const auto schedule = gaussian_training_schedule(steps_, state_.value("registeredViews").toInt(), max_splats_);
      if (schedule.isEmpty()) { fail("Cannot schedule training without a valid registered capture."); return; }
      state_.insert("trainingSchedule", schedule);
      state_.insert("state", "training-gaussians");
      program = trainer_;
      arguments = {root.filePath("dataset"), "--total-steps", QString::number(steps_), "--max-resolution", QString::number(maximum_edge_),
          "--max-splats", schedule.value("maxSplats").toString(), "--seed", schedule.value("seed").toString(),
          "--refine-every", schedule.value("refineEvery").toString(),
          "--growth-stop-iter", schedule.value("growthStopIteration").toString(),
          "--eval-split-every", schedule.value("evaluationSplitEvery").toString(),
          "--eval-every", QString::number(steps_), "--eval-save-to-disk",
          "--export-every", QString::number(steps_), "--export-path", root.filePath("trained"), "--export-name", "scene.ply"};
      break;
    }
    default: {
      state_.insert("state", "validating-artifact");
      if (!publish()) return;
      const auto path = root.filePath("trained/scene.ply");
      const auto canceled = canceled_;
      preparation_active_ = true;
      preparation_.setFuture(QtConcurrent::run([path, canceled] { return validate_gaussian_ply(path, canceled); }));
      return;
    }
  }
  state_.insert("stageIndex", stage_);
  if (!publish()) return;
  auto environment = QProcessEnvironment::systemEnvironment();
  environment.insert("RUST_LOG", "warn,brush_cli=info,brush_process=info");
  const auto directory = project_;
  const auto canceled = canceled_;
  auto log = std::make_shared<GaussianProcessLog>();
  log->tail = log_tail_;
  process_log_ = log;
  process_active_ = true;
  process_.setFuture(QtConcurrent::run([program, arguments, directory, environment, canceled, log] {
    return run_gaussian_process(program, arguments, directory, environment, canceled, log);
  }));
  process_log_timer_.start();
}

void GaussianSceneController::finishStage(int code, QProcess::ExitStatus status) {
  if (!busy_) return;
  if (canceled_->load()) { fail("Reconstruction canceled; partial project retained."); return; }
  if (status != QProcess::NormalExit || code != 0) { fail(QStringLiteral("Reconstruction stage failed (exit %1). See the retained log.").arg(code)); return; }
  if (stage_ == 0 && camera_group_ + 1 < camera_groups_.size()) {
    ++camera_group_;
    runStage();
    return;
  }
  ++stage_;
  runStage();
}

void GaussianSceneController::fail(const QString& error) {
  busy_ = false;
  state_.insert("state", canceled_ && canceled_->load() ? "canceled" : "failed");
  state_.insert("error", error);
  state_.insert("logTail", log_tail_);
  publish();
}

void GaussianSceneController::cancel() {
  if (!busy_) return;
  canceled_->store(true);
  state_.insert("state", "canceling");
  publish();
}

QVariantMap GaussianSceneController::buildPartialScene() {
  const auto customer_edit_scope = customer_edit_access_.enter();
  if (!customer_edit_scope) return {{QStringLiteral("ok"), false}, {QStringLiteral("errorCode"), QStringLiteral("access_required")}, {QStringLiteral("error"), QStringLiteral("Editing requires an active trial or purchase.")}};
  if (busy_ || preparation_active_ || process_active_ || state_.value("state") != "failed" ||
      !state_.contains("sourceVideo") || state_.value("stageIndex").toInt() != 2 ||
      state_.value("registeredViews").toInt() < 8)
    return rejected("A finished video reconstruction with at least eight connected views is required.");
  const QDir root(project_);
  const auto frames = state_.value("video").toMap().value("frames").toStringList();
  const int steps = state_.value("trainingSteps").toInt(), edge = state_.value("maximumEdge").toInt();
  const int budget = state_.value("maxSplats").toInt();
  if (frames.size() != state_.value("sourceCount").toInt() || frames.size() < 8 || frames.size() > 1920 ||
      edge < 256 || edge > 4096 || gaussian_training_schedule(steps, 8, budget).isEmpty())
    return rejected("The saved capture or training settings are invalid.");
  for (const auto& directory : {"images", "sparse", "trained"}) {
    const auto path = root.filePath(directory);
    if (!QFileInfo(path).isDir() || QFileInfo(path).canonicalFilePath() != path)
      return rejected("Prepared capture folders are missing or outside this project.");
  }
  if (QFileInfo::exists(root.filePath("dataset")) ||
      !QDir(root.filePath("trained")).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden).isEmpty())
    return rejected("This project already has training output; it will not be overwritten.");
  colmap_ = binary("PHOTO_EDITOR_COLMAP_BINARY", "colmap");
  trainer_ = binary("PHOTO_EDITOR_BRUSH_BINARY", "brush_app");
  if (colmap_.isEmpty() || trainer_.isEmpty()) return rejected("COLMAP and Brush are required to build this section.");
  // Explicit user action only. Preserve the failed reconstruction receipt and
  // all its inputs; reopening a failed project never starts this workflow.
  if (!QFile::copy(root.filePath("scene.json"), root.filePath("scene-before-partial.json")))
    return rejected("Cannot preserve the pre-training scene receipt; no training was started.");
  sources_ = frames; steps_ = steps; maximum_edge_ = edge; max_splats_ = budget;
  canceled_ = std::make_shared<std::atomic_bool>(false);
  read_only_project_ = false; partial_build_authorized_ = true; busy_ = true;
  state_.remove("error"); state_.insert("partialScene", true);
  state_.insert("colmapBinary", colmap_); state_.insert("trainerBinary", trainer_);
  stage_ = 3;
  runStage();
  return {{"ok", busy_}, {"error", busy_ ? QString() : state_.value("error").toString()}};
}

QVariantMap GaussianSceneController::openProject(const QString& folder) {
  if (busy_ || preparation_active_ || process_active_)
    return rejected("Finish or cancel the active scene first.");
  const auto root = QFileInfo(local_path(folder)).canonicalFilePath();
  if (root.isEmpty()) return rejected("Cannot read the scene project.");
  QFile file(QDir(root).filePath("scene.json"));
  const bool standalone_timed = !QFileInfo::exists(file.fileName()) &&
      QFileInfo::exists(QDir(root).filePath("timed-capture.json"));
  if (standalone_timed) {
    if (!validate_timed_) return rejected("Timed capture verification is unavailable.");
  }
  if (root.isEmpty() || (!standalone_timed && (!file.open(QIODevice::ReadOnly) || file.size() > 4 * 1024 * 1024))) return rejected("Cannot read the scene project.");
  const auto saved = standalone_timed
      ? QVariantMap{{"schema", "framehouse-gaussian-scene-v1"}, {"mode", "timed-4d-input"}}
      : QJsonDocument::fromJson(file.readAll()).toVariant().toMap();
  if (saved.value("schema").toString() != "framehouse-gaussian-scene-v1") return rejected("Unsupported scene project format.");
  const bool timed = saved.value("mode") == "timed-4d-input";
  const bool spacetime = saved.value("mode") == "spacetime-4d";
  const auto spacetime_path = QDir(root).filePath("scene/scene.safetensors");
  const auto spacetime_hash = saved.value("artifactValidation").toMap().value("artifactSha256").toString();
  if (spacetime && (!validate_spacetime_ || QFileInfo(spacetime_path).canonicalFilePath() != spacetime_path ||
      !QRegularExpression("^[0-9a-f]{64}$").match(spacetime_hash).hasMatch()))
    return rejected("The 4D artifact, verifier or pinned integrity receipt is missing or outside the project.");
  if (!timed && !spacetime && saved.value("mode", "static-3d") != "static-3d")
    return rejected("Unsupported scene project mode.");
  if (timed && (!validate_timed_ || (!standalone_timed &&
      saved.value("capture").toMap().value("manifestSha256").toString().size() != 64)))
    return rejected("Timed capture verification or its integrity receipt is missing.");
  project_ = root;
  partial_build_authorized_ = false;
  read_only_project_ = true;
  state_ = saved;
  log_tail_ = saved.value("logTail").toString().right(32768);
  state_.insert("logTail", log_tail_);
  state_.remove("artifactPath");
  new_timed_project_ = false;
  if (spacetime) {
    stage_ = 7; busy_ = true;
    canceled_ = std::make_shared<std::atomic_bool>(false);
    state_.insert("state", "validating-spacetime-artifact");
    state_.insert("trainingAvailable", false);
    const auto canceled = canceled_; const auto validate = validate_spacetime_;
    preparation_active_ = true;
    preparation_.setFuture(QtConcurrent::run([validate, spacetime_path, spacetime_hash, canceled] {
      return validate(spacetime_path, spacetime_hash, canceled);
    }));
    emit changed(); return {{"ok", true}};
  }
  if (timed) {
    stage_ = 6;
    busy_ = true;
    canceled_ = std::make_shared<std::atomic_bool>(false);
    state_.insert("state", "verifying-timed-capture");
    state_.insert("trainingAvailable", false);
    const auto canceled = canceled_;
    const auto validate = validate_timed_;
    preparation_active_ = true;
    preparation_.setFuture(QtConcurrent::run([validate, root, canceled] { return validate(root, canceled); }));
    emit changed();
    return {{"ok", true}};
  }
  if (saved.value("busy").toBool()) state_.insert("state", "interrupted");
  // Opening a project never resumes training or executes persisted commands.
  // Resolve only the owned relative artifact, not an arbitrary persisted path.
  if (state_.value("state").toString() == "trained") {
    const auto path = QDir(root).filePath("trained/scene.ply");
    if (QFileInfo(path).canonicalFilePath() != path ||
        saved.value("artifactValidation").toMap().value("artifactSha256").toString().size() != 64) {
      state_.insert("state", "failed");
      state_.insert("error", "The scene artifact or integrity receipt is missing or outside the project.");
      emit changed();
      return rejected(state_.value("error").toString());
    }
    stage_ = 5;
    busy_ = true;
    canceled_ = std::make_shared<std::atomic_bool>(false);
    state_.insert("state", "validating-artifact");
    const auto canceled = canceled_;
    preparation_active_ = true;
    preparation_.setFuture(QtConcurrent::run([path, canceled] { return validate_gaussian_ply(path, canceled); }));
  }
  emit changed();
  return {{"ok", true}};
}

}  // namespace photo_editor::desktop
