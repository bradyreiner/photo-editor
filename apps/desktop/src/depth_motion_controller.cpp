#include "depth_motion_controller.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>
#include <QSet>
#include <QStandardPaths>
#include <QSaveFile>
#include <QTimer>
#include <QThread>
#include <QUrl>
#include <QUuid>

#include <memory>

#ifdef Q_OS_UNIX
#include <signal.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace photo_editor::desktop {
namespace {
QString local_path(const QString& value) {
  const QUrl url(value);
  return url.isLocalFile() ? url.toLocalFile() : value;
}
QString executable(const QString& name) {
  const auto found = QStandardPaths::findExecutable(name);
  if (!found.isEmpty()) return found;
  for (const auto& prefix : {QStringLiteral("/opt/homebrew/bin/"), QStringLiteral("/usr/local/bin/")}) {
    if (QFileInfo(prefix + name).isExecutable()) return prefix + name;
  }
  return {};
}

void signal_process_group(QProcess* process, bool force) {
  if (!process || process->state() == QProcess::NotRunning) return;
#ifdef Q_OS_UNIX
  const auto pid = process->processId();
  if (pid > 0 && ::kill(-static_cast<pid_t>(pid), force ? SIGKILL : SIGTERM) == 0) return;
#endif
  if (force) process->kill();
  else process->terminate();
}

void signal_process_group_id(qint64 process_group, bool force) {
#ifdef Q_OS_UNIX
  if (process_group > 0) ::kill(-static_cast<pid_t>(process_group), force ? SIGKILL : SIGTERM);
#else
  Q_UNUSED(process_group);
  Q_UNUSED(force);
#endif
}

bool process_group_exists(qint64 process_group) {
#ifdef Q_OS_UNIX
  if (process_group <= 0) return false;
  if (::kill(-static_cast<pid_t>(process_group), 0) == 0) return true;
  return errno == EPERM;
#else
  Q_UNUSED(process_group);
  return false;
#endif
}

class DepthMotionProcessReaper final : public QObject {
 public:
  explicit DepthMotionProcessReaper(QObject* parent) : QObject(parent) {
    setObjectName(QStringLiteral("depthMotionProcessReaper"));
    uptime_.start();
  }

  static DepthMotionProcessReaper* for_application() {
    auto* application = QCoreApplication::instance();
    if (!application || QCoreApplication::closingDown()) return nullptr;
    if (auto* existing = application->findChild<QObject*>(QStringLiteral("depthMotionProcessReaper")))
      return static_cast<DepthMotionProcessReaper*>(existing);
    auto* reaper = new DepthMotionProcessReaper(application);
    QObject::connect(application, &QCoreApplication::aboutToQuit, reaper, [reaper] { reaper->drain(); });
    return reaper;
  }

  void adopt(QProcess* process, std::shared_ptr<QTemporaryDir> retained_scratch, bool graceful_stop,
             qint64 known_process_group) {
    if (!process) return;
    auto process_group = process->processId();
    if (process_group <= 0) process_group = known_process_group;
    process->setParent(this);
    retained_scratch_.insert(process, std::move(retained_scratch));
    process_groups_.insert(process, process_group);
    settle_started_ms_.insert(process, uptime_.elapsed());
    QObject::connect(process, &QProcess::started, this, [this, process, graceful_stop] {
      if (!retained_scratch_.contains(process)) return;
      const auto group = process->processId();
      if (group > 0) process_groups_.insert(process, group);
      signal_process_group_id(group, !graceful_stop);
    });
    QObject::connect(process, &QProcess::finished, this, [this, process] { settle(process); });
    QObject::connect(process, &QProcess::errorOccurred, this,
                     [this, process](QProcess::ProcessError error) {
      if (error == QProcess::FailedToStart && process->state() == QProcess::NotRunning) release(process);
    });
    if (process->state() == QProcess::NotRunning) {
      if (process_group_exists(process_group)) settle(process);
      else release(process);
      return;
    }
    if (shutting_down_) {
      signal_process_group_id(process_group, true);
      process->kill();
      process->waitForFinished(1000);
      QElapsedTimer timer;
      timer.start();
      while (process_group_exists(process_group) && timer.elapsed() < 1000) {
        signal_process_group_id(process_group, true);
        QThread::msleep(2);
      }
      if (process->state() == QProcess::NotRunning && !process_group_exists(process_group)) release(process);
      return;
    }
    if (graceful_stop) signal_process_group(process, false);
    else signal_process_group(process, true);
    QTimer::singleShot(250, this, [this, process] {
      if (!retained_scratch_.contains(process)) return;
      signal_process_group_id(process_groups_.value(process), true);
      if (process->state() != QProcess::NotRunning) process->kill();
      if (process->state() == QProcess::NotRunning) settle(process);
    });
  }

  void drain() {
    shutting_down_ = true;
    const auto processes = findChildren<QProcess*>(QString(), Qt::FindDirectChildrenOnly);
    for (auto* process : processes) {
      if (expired_groups_.contains(process)) {
        if (process->state() != QProcess::NotRunning) {
          process->kill();
          process->waitForFinished(1000);
        }
        if (!deadline_reported_.contains(process)) {
          deadline_reported_.insert(process);
          qWarning() << "Depth Motion child group exceeded its safe signal deadline; retaining its QProcess and scratch storage through Qt teardown";
        }
        continue;
      }
      const auto process_group = process_groups_.value(process);
      signal_process_group_id(process_group, true);
      if (process->state() != QProcess::NotRunning) {
        process->kill();
        process->waitForFinished(1000);
      }
      QElapsedTimer timer;
      timer.start();
      while (process_group_exists(process_group) && timer.elapsed() < 1000) {
        signal_process_group_id(process_group, true);
        QThread::msleep(2);
      }
      if (process->state() == QProcess::NotRunning && !process_group_exists(process_group)) release(process);
      else if (!deadline_reported_.contains(process)) {
        deadline_reported_.insert(process);
        qWarning() << "Depth Motion process group remains after application shutdown drain; process ownership and scratch retention are kept through Qt teardown";
      }
    }
  }

 private:
  void settle(QProcess* process) {
    if (!retained_scratch_.contains(process)) return;
    if (expired_groups_.contains(process)) return;
    const auto process_group = process_groups_.value(process);
    if (process_group_exists(process_group)) {
      if (process->state() == QProcess::NotRunning && !process_exit_seen_ms_.contains(process)) {
        process_exit_seen_ms_.insert(process, uptime_.elapsed());
        QTimer::singleShot(25, this, [this, process] { settle(process); });
        return;
      }
      signal_process_group_id(process_group, true);
      if (uptime_.elapsed() - settle_started_ms_.value(process) >= 5000) {
        if (!deadline_reported_.contains(process)) {
          deadline_reported_.insert(process);
          qWarning() << "Depth Motion child group is still alive after bounded cleanup; retaining its process and scratch files until application shutdown";
        }
        expired_groups_.insert(process);
        process_groups_.insert(process, 0);
        return;
      }
      QTimer::singleShot(10, this, [this, process] { settle(process); });
      return;
    }
    release(process);
  }

  void release(QProcess* process) {
    if (!retained_scratch_.contains(process)) return;
    retained_scratch_.remove(process);
    process_groups_.remove(process);
    settle_started_ms_.remove(process);
    process_exit_seen_ms_.remove(process);
    deadline_reported_.remove(process);
    expired_groups_.remove(process);
    process->deleteLater();
  }

  QHash<QProcess*, std::shared_ptr<QTemporaryDir>> retained_scratch_;
  QHash<QProcess*, qint64> process_groups_;
  QHash<QProcess*, qint64> settle_started_ms_;
  QHash<QProcess*, qint64> process_exit_seen_ms_;
  QSet<QProcess*> deadline_reported_;
  QSet<QProcess*> expired_groups_;
  QElapsedTimer uptime_;
  bool shutting_down_ = false;
};

void stop_without_reaper(QProcess* process, qint64 known_process_group) {
  if (!process) return;
  auto process_group = process->processId();
  if (process_group <= 0) process_group = known_process_group;
  if (process->state() == QProcess::NotRunning && !process_group_exists(process_group)) return;
  signal_process_group_id(process_group, true);
  signal_process_group(process, true);
  if (process->state() != QProcess::NotRunning) process->waitForFinished(1000);
  QElapsedTimer timer;
  timer.start();
  while (process_group_exists(process_group) && timer.elapsed() < 1000) {
    signal_process_group_id(process_group, true);
    QThread::msleep(2);
  }
}
}

DepthMotionController::DepthMotionController(QObject* parent) : QObject(parent) {
  process_ = new QProcess(this);
  source_probe_ = new QProcess(this);
  scratch_ = std::make_shared<QTemporaryDir>();
#ifdef Q_OS_UNIX
  process_->setChildProcessModifier([] { ::setpgid(0, 0); });
  source_probe_->setChildProcessModifier([] { ::setpgid(0, 0); });
#endif
  source_probe_timeout_.setSingleShot(true);
  connect(&source_probe_timeout_, &QTimer::timeout, this, [this] {
    if (active_source_probe_generation_ == source_probe_generation_ && source_probe_ &&
        source_probe_->state() != QProcess::NotRunning) source_probe_->kill();
  });
  home_ = qEnvironmentVariable("PHOTO_EDITOR_DEPTH_MOTION_HOME");
  if (home_.isEmpty()) {
    const auto checkout = QDir::current().filePath(QStringLiteral(".photo-editor/depth-motion"));
    home_ = QFileInfo(checkout).isDir() ? checkout
        : QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
              .filePath(QStringLiteral("depth-motion"));
  }
  home_ = QFileInfo(home_).absoluteFilePath();
  status_ = QStringLiteral("Choose an image or a short video to give it depth.");
  connect(process_, &QProcess::readyReadStandardOutput, this, &DepthMotionController::readOutput);
  connect(process_, &QProcess::readyReadStandardError, this, [this] {
    diagnostics_ += process_->readAllStandardError();
    diagnostics_ = diagnostics_.right(8000);
  });
  connect(process_, &QProcess::started, this, [this] { worker_process_group_ = process_->processId(); });
  connect(process_, &QProcess::finished, this, &DepthMotionController::finish);
  connect(process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
    if (error == QProcess::FailedToStart) {
      busy_ = false;
      fail(QStringLiteral("Could not start the local depth runtime: %1").arg(process_->errorString()));
    }
  });
  connect(source_probe_, &QProcess::started, this,
          [this] { source_probe_process_group_ = source_probe_->processId(); });
  connect(source_probe_, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
    source_probe_timeout_.stop();
    signal_process_group_id(source_probe_process_group_, true);
    if (!process_group_exists(source_probe_process_group_)) source_probe_process_group_ = 0;
    const auto generation = active_source_probe_generation_;
    const auto metadata = QJsonDocument::fromJson(source_probe_->readAllStandardOutput()).object();
    const auto streams = metadata.value("streams").toArray();
    if (generation == source_probe_generation_) {
      source_info_.insert("busy", false);
      if (code == 0 && status == QProcess::NormalExit && !streams.isEmpty()) {
        const auto stream = streams.first().toObject();
        auto duration = metadata.value("format").toObject().value("duration").toString().toDouble();
        if (duration <= 0) duration = stream.value("duration").toString().toDouble();
        source_info_.insert("duration", duration);
        source_info_.insert("width", stream.value("width").toInt());
        source_info_.insert("height", stream.value("height").toInt());
        if (duration <= 0) source_info_.insert("error", "This video has no readable duration.");
      } else source_info_.insert("error", "Could not read this video's trim range.");
      emit changed();
    }
    startPendingSourceProbe();
  });
  connect(source_probe_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
    if (error != QProcess::FailedToStart) return;
    source_probe_timeout_.stop();
    publishSourceProbeFailure(QStringLiteral("FFprobe could not read this video's trim range."),
                              active_source_probe_generation_);
    startPendingSourceProbe();
  });
}

DepthMotionController::~DepthMotionController() {
  source_probe_timeout_.stop();
  auto* reaper = DepthMotionProcessReaper::for_application();
  if (source_probe_ && (source_probe_->state() != QProcess::NotRunning ||
                        process_group_exists(source_probe_process_group_))) {
    QObject::disconnect(source_probe_, nullptr, this, nullptr);
    if (reaper) reaper->adopt(source_probe_, {}, false, source_probe_process_group_);
    else {
      stop_without_reaper(source_probe_, source_probe_process_group_);
      delete source_probe_;
    }
    source_probe_ = nullptr;
  }
  if (process_ && (process_->state() != QProcess::NotRunning ||
                   process_group_exists(worker_process_group_))) {
    QObject::disconnect(process_, nullptr, this, nullptr);
    if (reaper) reaper->adopt(process_, scratch_, true, worker_process_group_);
    else {
      stop_without_reaper(process_, worker_process_group_);
      delete process_;
    }
    process_ = nullptr;
  }
}

QString DepthMotionController::runtimePython() const {
  const auto configured = qEnvironmentVariable("PHOTO_EDITOR_DEPTH_MOTION_PYTHON");
  if (!configured.isEmpty()) return configured;
#ifdef Q_OS_WIN
  return QDir(home_).filePath(QStringLiteral("venv/Scripts/python.exe"));
#else
  return QDir(home_).filePath(QStringLiteral("venv/bin/python"));
#endif
}

QVariantMap DepthMotionController::snapshot() const {
  const auto url = [this](const QString& file) {
    return project_.isEmpty() ? QString{} : QUrl::fromLocalFile(QDir(project_).filePath(file)).toString();
  };
  return {{"busy", busy_}, {"status", status_}, {"error", error_}, {"progress", progress_},
          {"runtimeReady", QFileInfo(runtimePython()).isExecutable()},
          {"modelReady", QFileInfo(QDir(home_).filePath("model/config.json")).isFile()
              && QFileInfo(QDir(home_).filePath("model/model.safetensors")).size() > 0},
          {"home", home_}, {"project", project_}, {"recipe", recipe_}, {"sourceInfo", source_info_},
          {"depthVideoAvailable", !project_.isEmpty() && QFileInfo(QDir(project_).filePath("depth-video/manifest.json")).isFile()},
          {"recordingLayersAvailable", !project_.isEmpty() && QFileInfo(QDir(project_).filePath("source-video/manifest.json")).isFile()
              && QFileInfo(QDir(project_).filePath("depth-video/manifest.json")).isFile()},
          {"cloudAvailable", !project_.isEmpty() && QFileInfo(QDir(project_).filePath("pointcloud/manifest.json")).isFile()},
          {"movieUrl", url("motion.mp4")}, {"previewUrl", url("preview.png")},
          {"depthUrl", url("depth.png")}, {"sourceUrl", url("source.png")}};
}

QUrl DepthMotionController::localMediaUrl(const QString& source) const {
  if (source.isEmpty()) return {};
  const QUrl url(source);
  if (url.isLocalFile()) return QUrl::fromLocalFile(QFileInfo(url.toLocalFile()).absoluteFilePath());
  if (!QDir::isAbsolutePath(source) && !url.scheme().isEmpty()) return {};
  // QML resolves bare paths against the component's qrc URL. Convert at the
  // media boundary, preserving spaces, Unicode and URL-reserved filename bytes.
  return QUrl::fromLocalFile(QFileInfo(source).absoluteFilePath());
}

void DepthMotionController::inspectSource(const QString& source) {
  ++source_probe_generation_;
  source_probe_pending_ = false;
  source_probe_timeout_.stop();
  const auto path = local_path(source);
  source_info_ = {{"path", path}, {"busy", false}, {"duration", 0}};
  const auto suffix = QFileInfo(path).suffix().toLower();
  if (!QStringList{"mp4", "mov", "m4v", "mkv"}.contains(suffix) || !QFileInfo(path).isFile()) {
    if (source_probe_->state() != QProcess::NotRunning) source_probe_->kill();
    emit changed();
    return;
  }
  const auto probe = executable("ffprobe");
  if (probe.isEmpty()) {
    source_info_.insert("error", "Install FFprobe to choose a video trim range.");
    if (source_probe_->state() != QProcess::NotRunning) source_probe_->kill();
    emit changed();
    return;
  }
  source_info_.insert("busy", true);
  pending_source_probe_path_ = path;
  pending_source_probe_program_ = probe;
  source_probe_pending_ = true;
  if (source_probe_->state() != QProcess::NotRunning) source_probe_->kill();
  else startPendingSourceProbe();
  emit changed();
}

void DepthMotionController::startPendingSourceProbe() {
  if (!source_probe_pending_ || !source_probe_ || source_probe_->state() != QProcess::NotRunning) return;
  source_probe_pending_ = false;
  active_source_probe_generation_ = source_probe_generation_;
  source_probe_process_group_ = 0;
  source_probe_->start(pending_source_probe_program_, {"-v", "error", "-select_streams", "v:0", "-show_entries",
      "stream=width,height,duration:format=duration", pending_source_probe_path_});
  source_probe_timeout_.start(10000);
}

void DepthMotionController::publishSourceProbeFailure(const QString& message, quint64 generation) {
  if (generation != source_probe_generation_) return;
  source_info_.insert("busy", false);
  source_info_.insert("error", message);
  emit changed();
}

bool DepthMotionController::fail(const QString& message) {
  error_ = message;
  emit changed();
  return false;
}

QString DepthMotionController::worker() {
  if (!scratch_) {
    fail(QStringLiteral("The local depth worker storage is unavailable."));
    return {};
  }
  const auto path = scratch_->filePath(QStringLiteral("depth-motion-worker.py"));
  if (QFileInfo::exists(path)) return path;
  if (!scratch_->isValid() || !QFile::copy(QStringLiteral(":/photo-editor/runtime/depth-motion-worker.py"), path)) {
    fail(QStringLiteral("The bundled Depth Motion worker is missing."));
    return {};
  }
  return path;
}

bool DepthMotionController::start(const QString& python, const QStringList& arguments) {
  error_.clear();
  diagnostics_.clear();
  buffer_.clear();
  completed_ = false;
  canceled_ = false;
  ++worker_generation_;
  worker_process_group_ = 0;
  busy_ = true;
  progress_ = 0;
  if (!process_) return fail(QStringLiteral("The local depth runtime is unavailable."));
  process_->setProgram(python);
  process_->setArguments(arguments);
  process_->start(QIODevice::ReadOnly);
  emit changed();
  return true;
}

bool DepthMotionController::install() {
  if (busy_) return false;
  const auto script = worker();
  if (script.isEmpty()) return false;
  const auto python = executable(QStringLiteral("python3"));
  if (python.isEmpty()) return fail(QStringLiteral("Install Python 3.10 or later to set up local depth tools."));
  operation_ = QStringLiteral("setup");
  status_ = QStringLiteral("Installing local depth tools…");
  return start(python, {"-u", script, "--setup", home_});
}

bool DepthMotionController::generate(const QString& source, const QString& depth_map, const QVariantMap& settings) {
  if (busy_) return false;
  if (!editing_allowed_) return fail(QStringLiteral("Editing access is required to create depth motion."));
  const auto path = local_path(source);
  const auto depth = local_path(depth_map);
  if (!QFileInfo(path).isFile()) return fail(QStringLiteral("Choose a readable image or video first."));
  if (!depth.isEmpty() && !QFileInfo(depth).isFile()) return fail(QStringLiteral("The supplied depth map is missing."));
  if (!QFileInfo(runtimePython()).isExecutable()) return fail(QStringLiteral("Install the local depth tools first."));
  const auto script = worker();
  if (script.isEmpty()) return false;
  const auto ffmpeg = executable(QStringLiteral("ffmpeg"));
  const auto ffprobe = executable(QStringLiteral("ffprobe"));
  if (ffmpeg.isEmpty() || ffprobe.isEmpty()) return fail(QStringLiteral("Install FFmpeg and FFprobe to render Depth Motion clips."));
  const auto projects = QDir(home_).filePath(QStringLiteral("projects"));
  if (!QDir().mkpath(projects)) return fail(QStringLiteral("Could not create the local Depth Motion projects folder."));
  pending_project_ = QDir(projects).filePath(QUuid::createUuid().toString(QUuid::WithoutBraces));
  const QVariantMap request{{"source", QFileInfo(path).absoluteFilePath()}, {"depthMap", depth},
      {"settings", settings}, {"modelPath", QDir(home_).filePath("model")}, {"cachePath", cache_},
      {"cacheIdentity", recipe_.value("identity").toMap()},
      {"interactiveView", recipe_.value("source").toString() == QFileInfo(path).absoluteFilePath()
          ? recipe_.value("interactiveView").toMap() : QVariantMap{}},
      {"ffmpeg", ffmpeg}, {"ffprobe", ffprobe}};
  QFile file(scratch_->filePath(QStringLiteral("request.json")));
  if (!file.open(QIODevice::WriteOnly) || file.write(QJsonDocument::fromVariant(request).toJson()) < 0)
    return fail(QStringLiteral("Could not save the depth render request."));
  file.close();
  operation_ = QStringLiteral("render");
  status_ = QStringLiteral("Preparing depth motion…");
  return start(runtimePython(), {"-u", script, "--request", file.fileName(), "--output", pending_project_});
}

void DepthMotionController::readOutput() {
  if (!process_) return;
  buffer_ += process_->readAllStandardOutput();
  qsizetype end = 0;
  while ((end = buffer_.indexOf('\n')) >= 0) {
    const auto line = buffer_.left(end);
    buffer_.remove(0, end + 1);
    const auto value = QJsonDocument::fromJson(line).object();
    if (value.isEmpty()) continue;
    if (value.contains("status")) status_ = value.value("status").toString();
    if (value.contains("progress")) progress_ = value.value("progress").toDouble();
    if (value.contains("error")) error_ = value.value("error").toString();
    if (value.value("done").toBool()) completed_ = true;
  }
  emit changed();
}

void DepthMotionController::finish(int code, QProcess::ExitStatus exit_status) {
  signal_process_group_id(worker_process_group_, true);
  if (!process_group_exists(worker_process_group_)) worker_process_group_ = 0;
  readOutput();
  busy_ = false;
  if (canceled_) {
    status_ = QStringLiteral("Depth motion stopped. Your previous render is still available.");
    error_.clear();
  } else if (code != 0 || exit_status != QProcess::NormalExit || !completed_) {
    if (error_.isEmpty()) error_ = QStringLiteral("Depth Motion could not finish. %1").arg(QString::fromUtf8(diagnostics_.right(2000)));
    status_ = QStringLiteral("Depth Motion needs attention");
  } else if (operation_ == QStringLiteral("cloud") || operation_ == QStringLiteral("depth-video")) {
    openProject(QDir(project_).filePath(QStringLiteral("project.depthmotion.json")));
  } else if (operation_ == QStringLiteral("render")) {
    openProject(QDir(pending_project_).filePath(QStringLiteral("project.depthmotion.json")));
  } else {
    status_ = QStringLiteral("Local depth tools ready. Choose a source to begin.");
  }
  emit changed();
}

bool DepthMotionController::preparePointCloud() {
  if (busy_ || project_.isEmpty()) return false;
  const auto script = worker();
  if (script.isEmpty()) return false;
  if (!QFileInfo(runtimePython()).isExecutable()) return fail(QStringLiteral("Install the local depth tools first."));
  operation_ = QStringLiteral("cloud");
  status_ = QStringLiteral("Preparing the playable point cloud from saved depth…");
  return start(runtimePython(), {"-u", script, "--prepare-project", QDir(project_).filePath("project.depthmotion.json")});
}

bool DepthMotionController::prepareDepthVideo() {
  if (busy_ || project_.isEmpty()) return false;
  const auto script = worker();
  if (script.isEmpty()) return false;
  if (!QFileInfo(runtimePython()).isExecutable()) return fail(QStringLiteral("Install the local depth tools first."));
  operation_ = QStringLiteral("depth-video");
  status_ = QStringLiteral("Preparing depth video from this project's saved depth…");
  return start(runtimePython(), {"-u", script, "--depth-video", QDir(project_).filePath("project.depthmotion.json")});
}

bool DepthMotionController::savePointCloudView(const QVariantMap& view) {
  if (busy_ || project_.isEmpty() || view.isEmpty() || !editing_allowed_) return false;
  auto recipe = QJsonObject::fromVariantMap(recipe_);
  recipe.insert("interactiveView", QJsonObject::fromVariantMap(view));
  QSaveFile output(QDir(project_).filePath("project.depthmotion.json"));
  if (!output.open(QIODevice::WriteOnly) || output.write(QJsonDocument(recipe).toJson()) < 0 || !output.commit())
    return fail(QStringLiteral("Could not save the point-cloud camera view."));
  recipe_ = recipe.toVariantMap();
  emit changed();
  return true;
}

QVariantMap DepthMotionController::openProject(const QString& path) {
  if (busy_) return {{"ok", false}, {"error", "Wait for the current render to finish."}};
  QFile file(local_path(path));
  if (!file.open(QIODevice::ReadOnly)) {
    fail(QStringLiteral("Could not open the Depth Motion project."));
    return {{"ok", false}, {"error", error_}};
  }
  const auto document = QJsonDocument::fromJson(file.readAll());
  const auto recipe = document.object();
  const auto folder = QFileInfo(file.fileName()).absolutePath();
  if (recipe.value("version").toInt() != 1 || !recipe.value("source").isString()
      || !recipe.value("settings").isObject() || !QFileInfo(QDir(folder).filePath("motion.mp4")).isFile()) {
    fail(QStringLiteral("This is not a completed Depth Motion project."));
    return {{"ok", false}, {"error", error_}};
  }
  project_ = folder;
  recipe_ = recipe.toVariantMap();
  cache_ = recipe.value("cachePath").toString();
  progress_ = 1;
  error_.clear();
  status_ = QStringLiteral("Depth motion ready");
  emit changed();
  return {{"ok", true}, {"recipe", recipe_}};
}

bool DepthMotionController::exportFile(const QString& kind, const QString& destination) {
  if (busy_) return false;
  if (!editing_allowed_) return fail(QStringLiteral("Editing access is required to export depth motion."));
  const QMap<QString, QString> files{{"movie", "motion.mp4"}, {"cloud", "cloud.ply"}, {"depth", "depth.png"}};
  if (!files.contains(kind) || project_.isEmpty()) return fail(QStringLiteral("Render a depth motion shot first."));
  const auto target = local_path(destination);
  if (!QFile::copy(QDir(project_).filePath(files.value(kind)), target))
    return fail(QStringLiteral("Could not export the file. Choose a writable location and a new filename."));
  error_.clear();
  status_ = QStringLiteral("Exported %1").arg(QFileInfo(target).fileName());
  emit changed();
  return true;
}

void DepthMotionController::cancel() {
  if (!busy_) return;
  canceled_ = true;
  status_ = QStringLiteral("Stopping depth motion…");
  if (!process_) return;
  const auto generation = worker_generation_;
  process_->terminate();
  QTimer::singleShot(5000, this, [this, generation] {
    if (busy_ && canceled_ && process_ && generation == worker_generation_) process_->kill();
  });
  emit changed();
}

}  // namespace photo_editor::desktop
