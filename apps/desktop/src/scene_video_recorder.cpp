#include "scene_video_recorder.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageWriter>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QQuickItemGrabResult>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>
#include <QtConcurrent>
#include <algorithm>

namespace photo_editor::desktop {
namespace {
QString ffmpeg() {
  const auto found = QStandardPaths::findExecutable("ffmpeg");
  if (!found.isEmpty()) return found;
  for (const auto* prefix : {"/opt/homebrew/bin/", "/usr/local/bin/"}) {
    const auto candidate = QString::fromLatin1(prefix) + "ffmpeg";
    if (QFileInfo(candidate).isExecutable()) return candidate;
  }
  return {};
}
}
SceneVideoRecorder::SceneVideoRecorder(QObject* parent) : QObject(parent) {
  timer_.setInterval(1000 / 24); timer_.setTimerType(Qt::PreciseTimer);
  connect(&timer_, &QTimer::timeout, this, &SceneVideoRecorder::tick);
  encoder_.setStandardOutputFile(QProcess::nullDevice());
  connect(&encoder_, &QProcess::readyReadStandardError, this, [this] {
    diagnostics_ = (diagnostics_ + encoder_.readAllStandardError()).right(8192);
  });
  connect(&encoder_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
    if (busy_) fail("Camera recording could not encode the video. " + encoder_.errorString());
  });
  connect(&encoder_, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
      [this](int code, QProcess::ExitStatus status) {
    if (!busy_) return;
    if (!encoding_ || code != 0 || status != QProcess::NormalExit || QFileInfo(temporary_).size() <= 0) {
      fail("Camera recording could not finish. " + QString::fromUtf8(diagnostics_.right(1200))); return;
    }
    if (layered_ && ++layer_index_ < layer_names_.size()) { encodeLayer(); return; }
    if (layered_) {
      QJsonArray layers;
      for (const auto& name : layer_names_) layers.append(name + ".mp4");
      QSaveFile metadata(staging_->filePath("outputs/recording.json"));
      const auto bytes = QJsonDocument(QJsonObject{{"version", 1}, {"fps", 24}, {"duration", seconds()},
          {"layers", layers}, {"samples", camera_samples_}}).toJson();
      if (!metadata.open(QIODevice::WriteOnly) || metadata.write(bytes) != bytes.size() || !metadata.commit() ||
          !QDir().rename(staging_->filePath("outputs"), recording_directory_)) {
        fail("Could not publish the synchronized recording layers."); return;
      }
      for (const auto& name : layer_names_) output_files_.push_back(QDir(recording_directory_).filePath(name + ".mp4"));
      output_url_ = QUrl::fromLocalFile(output_files_.first());
      cleanup(); emit changed(); return;
    }
    if (!QFile::rename(temporary_, destination_)) { fail("Could not save the recording into the scene project."); return; }
    output_url_ = QUrl::fromLocalFile(destination_);
    output_files_ = {destination_};
    cleanup(); emit changed();
  });
  connect(&writer_, &QFutureWatcher<QString>::finished, this, [this] {
    if (!writing_) return;
    writing_ = false;
    const auto error = writer_.result();
    if (!error.isEmpty()) { fail(error); return; }
    timestamps_.push_back(timestamps_.empty() ? 0 : pending_timestamp_);
    if (!recording_) finalize();
  });
}
SceneVideoRecorder::~SceneVideoRecorder() { cleanup(); }
QVariantMap SceneVideoRecorder::status() const {
  return {{"frames", int(timestamps_.size())}, {"capturing", capturing_}, {"writing", writing_},
      {"width", size_.width()}, {"height", size_.height()}, {"encoding", encoding_},
      {"encoderState", int(encoder_.state())}, {"encoderLog", QString::fromUtf8(diagnostics_)}};
}
void SceneVideoRecorder::cleanup() {
  ++generation_; recording_ = busy_ = capturing_ = encoding_ = false; timer_.stop();
  if (encoder_.state() != QProcess::NotRunning) { encoder_.kill(); encoder_.waitForFinished(1000); }
  if (writing_) { writer_.waitForFinished(); writing_ = false; }
  staging_.reset();
  if (viewport_ && layered_) viewport_->setProperty("captureLayers", false);
  viewport_.clear();
}
void SceneVideoRecorder::fail(const QString& error) { error_ = error; cleanup(); emit changed(); }
void SceneVideoRecorder::cancel() { cleanup(); emit changed(); }
bool SceneVideoRecorder::start(QQuickItem* viewport, const QString& project) {
  if (busy_) return false;
  error_.clear();
  const QUrl url(project); const auto folder = QFileInfo(url.isLocalFile() ? url.toLocalFile() : project).canonicalFilePath();
  if (!viewport || !viewport->window() || !viewport->isVisible() || viewport->width() < 2 || viewport->height() < 2 ||
      folder.isEmpty() || (!QFileInfo(QDir(folder).filePath("scene.json")).isFile() &&
      !QFileInfo(QDir(folder).filePath("project.depthmotion.json")).isFile())) {
    fail("Load a saved scene before recording its camera."); return false;
  }
  executable_ = ffmpeg();
  if (executable_.isEmpty()) { fail("Install FFmpeg to record the scene camera."); return false; }
  const auto recordings = QDir(folder).filePath("recordings");
  if (!QDir().mkpath(recordings)) { fail("Could not create the project's recordings folder."); return false; }
  staging_ = std::make_unique<QTemporaryDir>(QDir(recordings).filePath(".camera-XXXXXX"));
  if (!staging_->isValid()) { fail("Could not prepare the camera recording."); return false; }
  temporary_ = staging_->filePath("video.mp4");
  destination_ = QDir(recordings).filePath("Camera-" + QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss") + "-" + QUuid::createUuid().toString(QUuid::Id128).left(6) + ".mp4");
  size_ = QSize(int(viewport->width()), int(viewport->height())); size_.scale(1280, 1280, Qt::KeepAspectRatio);
  size_.setWidth(std::max(2, size_.width() / 2 * 2)); size_.setHeight(std::max(2, size_.height() / 2 * 2));
  viewport_ = viewport; output_url_ = QUrl{}; diagnostics_.clear(); timestamps_.clear();
  layered_ = viewport->metaObject()->indexOfMethod("recordingLayers()") >= 0;
  layer_names_.clear(); output_files_.clear(); camera_samples_ = {}; layer_index_ = 0;
  if (layered_) {
    recording_directory_ = destination_.chopped(4);
    if (!QDir().mkpath(staging_->filePath("outputs"))) { fail("Could not prepare the layer outputs."); return false; }
    viewport_->setProperty("captureLayers", true);
  }
  elapsed_ms_ = last_capture_ms_ = finishing_at_ = 0;
  capturing_ = writing_ = encoding_ = false; recording_ = busy_ = true; ++generation_;
  clock_.start(); timer_.start(); capture(); emit changed(); return true;
}
void SceneVideoRecorder::capture() {
  if (capturing_ || writing_ || !viewport_ || !recording_) return;
  if (layered_) {
    QVariantMap frame;
    if (!QMetaObject::invokeMethod(viewport_, "recordingLayers", Qt::DirectConnection, Q_RETURN_ARG(QVariantMap, frame))) {
      fail("The scene cannot provide synchronized recording layers."); return;
    }
    if (!frame.value("error").toString().isEmpty()) { fail(frame.value("error").toString()); return; }
    if (frame.value("images").toMap().isEmpty()) return; // Wait for the first complete layer publication.
    storeLayers(frame); return;
  }
  const auto grab = viewport_->grabToImage(size_);
  if (!grab) { fail("The scene viewport could not be captured."); return; }
  capturing_ = true; const auto generation = generation_;
  connect(grab.data(), &QQuickItemGrabResult::ready, this, [this, grab, generation] {
    if (generation != generation_ || !busy_) return;
    capturing_ = false;
    auto image = grab->image().convertToFormat(QImage::Format_RGB32);
    if (image.isNull()) { fail("The scene viewport returned an empty recording frame."); return; }
    if (image.size() != size_) image = image.scaled(size_);
    last_capture_ms_ = clock_.elapsed(); pending_timestamp_ = std::min(last_capture_ms_, recording_ ? last_capture_ms_ : elapsed_ms_);
    const auto path = staging_->filePath(QStringLiteral("%1.png").arg(timestamps_.size(), 6, 10, QLatin1Char('0')));
    writing_ = true;
    writer_.setFuture(QtConcurrent::run([image = std::move(image), path] {
      QSaveFile file(path);
      QImageWriter writer(&file, "png"); writer.setCompression(1);
      if (!file.open(QIODevice::WriteOnly) || !writer.write(image) || !file.commit())
        return QStringLiteral("Could not store the camera frames in this project's recordings folder.");
      return QString{};
    }));
  }, Qt::SingleShotConnection);
}
void SceneVideoRecorder::storeLayers(const QVariantMap& frame) {
  auto images = frame.value("images").toMap();
  // Four deliverables: the selected point look, source-colored points,
  // the flat depth map, and the original source-color reference.
  images.remove("depth-points");
  const auto names = images.keys();
  if (layer_names_.isEmpty()) {
    layer_names_ = names;
    // The displayed point-cloud look is the primary output.
    layer_names_.removeAll("point-cloud"); layer_names_.prepend("point-cloud");
    for (const auto& name : layer_names_) if (!QDir().mkpath(staging_->filePath(name))) {
      fail("Could not prepare the layer recording folders."); return;
    }
  }
  if (names.size() != layer_names_.size()) { fail("The recording layers changed during capture."); return; }
  last_capture_ms_ = clock_.elapsed(); pending_timestamp_ = last_capture_ms_;
  auto sample = QJsonObject::fromVariantMap(frame);
  sample.remove("images"); sample.insert("timestampMs", timestamps_.empty() ? 0 : pending_timestamp_);
  camera_samples_.append(sample);
  const auto directory = staging_->path(); const auto size = size_;
  const auto number = QStringLiteral("%1.png").arg(timestamps_.size(), 6, 10, QLatin1Char('0'));
  writing_ = true;
  writer_.setFuture(QtConcurrent::run([images, directory, size, number] {
    for (auto it = images.cbegin(); it != images.cend(); ++it) {
      const auto image = qvariant_cast<QImage>(it.value());
      if (image.isNull()) return QStringLiteral("A synchronized recording layer is empty.");
      QImage canvas(size, QImage::Format_RGB32); canvas.fill(Qt::black);
      const auto scaled = image.scaled(size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
      { QPainter painter(&canvas); painter.drawImage((size.width() - scaled.width()) / 2, (size.height() - scaled.height()) / 2, scaled); }
      QSaveFile file(QDir(directory).filePath(it.key() + '/' + number));
      QImageWriter writer(&file, "png"); writer.setCompression(1);
      if (!file.open(QIODevice::WriteOnly) || !writer.write(canvas) || !file.commit())
        return QStringLiteral("Could not store the synchronized layer frames.");
    }
    return QString{};
  }));
}
void SceneVideoRecorder::finalize() {
  if (recording_ || capturing_ || writing_ || encoding_ || !busy_) return;
  if (timestamps_.empty()) { fail("The scene did not deliver any camera frames."); return; }
  QByteArray manifest;
  for (size_t i = 0; i < timestamps_.size(); ++i) {
    const auto next = i + 1 < timestamps_.size() ? timestamps_[i + 1] : elapsed_ms_;
    manifest += "file '" + QStringLiteral("%1.png").arg(i, 6, 10, QLatin1Char('0')).toUtf8() + "'\noption framerate 1000\n";
    manifest += "duration " + QByteArray::number(double(std::max<qint64>(1, next - timestamps_[i])) / 1000, 'f', 6) + '\n';
  }
  manifest += "file '" + QStringLiteral("%1.png").arg(timestamps_.size() - 1, 6, 10, QLatin1Char('0')).toUtf8() + "'\noption framerate 1000\n";
  QSaveFile input(staging_->filePath("frames.txt"));
  if (!input.open(QIODevice::WriteOnly) || input.write(manifest) != manifest.size() || !input.commit()) {
    fail("Could not prepare the camera video in this project."); return;
  }
  encoding_ = true;
  if (layered_) { encodeLayer(); return; }
  encoder_.start(executable_, {"-hide_banner", "-loglevel", "error", "-nostdin", "-f", "concat", "-safe", "0",
      "-i", input.fileName(), "-an", "-r", "24", "-t", QString::number(std::max(.042, seconds()), 'f', 6),
      "-c:v", "libx264", "-preset", "veryfast", "-crf", "18", "-threads", "2", "-pix_fmt", "yuv420p",
      "-movflags", "+faststart", "-n", temporary_});
}
void SceneVideoRecorder::encodeLayer() {
  const auto name = layer_names_.at(layer_index_);
  QByteArray manifest;
  for (size_t i = 0; i < timestamps_.size(); ++i) {
    const auto next = i + 1 < timestamps_.size() ? timestamps_[i + 1] : elapsed_ms_;
    const auto file = name + '/' + QStringLiteral("%1.png").arg(i, 6, 10, QLatin1Char('0'));
    manifest += "file '" + file.toUtf8() + "'\noption framerate 1000\n";
    manifest += "duration " + QByteArray::number(double(std::max<qint64>(1, next - timestamps_[i])) / 1000, 'f', 6) + '\n';
  }
  manifest += "file '" + name.toUtf8() + '/' + QStringLiteral("%1.png").arg(timestamps_.size() - 1, 6, 10, QLatin1Char('0')).toUtf8() + "'\noption framerate 1000\n";
  QSaveFile input(staging_->filePath("layer-frames.txt"));
  if (!input.open(QIODevice::WriteOnly) || input.write(manifest) != manifest.size() || !input.commit()) {
    fail("Could not prepare the synchronized layer video."); return;
  }
  temporary_ = staging_->filePath("outputs/" + name + ".mp4");
  encoder_.start(executable_, {"-hide_banner", "-loglevel", "error", "-nostdin", "-f", "concat", "-safe", "0",
      "-i", input.fileName(), "-an", "-r", "24", "-t", QString::number(std::max(.042, seconds()), 'f', 6),
      "-c:v", "libx264", "-preset", "veryfast", "-crf", "18", "-threads", "2", "-pix_fmt", "yuv420p",
      "-movflags", "+faststart", "-n", temporary_});
}
void SceneVideoRecorder::tick() {
  if (!busy_) return;
  if (recording_) {
    elapsed_ms_ = clock_.elapsed();
    if (!viewport_ || !viewport_->isVisible()) { stop(); return; }
    if (elapsed_ms_ - last_capture_ms_ > 5000) { fail("The scene viewport stopped delivering recording frames."); return; }
    if (elapsed_ms_ >= 600000) { stop(); return; }
    capture();
  } else if (clock_.elapsed() - finishing_at_ > (encoding_ ? 300000 : 10000)) {
    fail("The camera recording did not finish."); return;
  }
  emit changed();
}
void SceneVideoRecorder::stop() {
  if (!recording_) return;
  elapsed_ms_ = clock_.elapsed(); recording_ = false; finishing_at_ = elapsed_ms_;
  finalize(); emit changed();
}
}
