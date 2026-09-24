#include "local_image_text_encoder.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QUuid>

#include <cmath>

namespace photo_editor::desktop {
namespace {
constexpr auto kRevision = "75de2d55ec2d0b4efc50b3e9ad70dba96a7b2fa2";
constexpr qsizetype kOutputLimit = 128 * 1024;
bool hash_valid(const QString& value) {
  static const QRegularExpression expression(QStringLiteral("^[0-9a-f]{64}$"));
  return expression.match(value).hasMatch();
}
}

LocalImageTextEncoder::LocalImageTextEncoder(QObject* parent) : QObject(parent) {
  deadline_.setSingleShot(true);
  connect(&deadline_, &QTimer::timeout, this, [this] {
    fail(ready_ ? QStringLiteral("Image/text encoding timed out; restart the local encoder.")
                : QStringLiteral("Local encoder startup timed out."));
  });
}

LocalImageTextEncoder::~LocalImageTextEncoder() {
  deadline_.stop();
  // Bounded final ownership cleanup only. Interactive stop never waits.
  if (process_) {
    process_->disconnect(this);
    process_->kill();
    process_->waitForFinished(1000);
  }
}

QVariantMap LocalImageTextEncoder::snapshot() const {
  return {{"state", state_}, {"error", error_}, {"ready", ready_},
          {"busy", !pending_id_.isEmpty()}, {"cancelPending", discard_reply_},
          {"spaceKey", space_key_}, {"dimensions", ready_ ? 768 : 0}, {"device", device_},
          {"ownsProcess", process_ && process_->state() != QProcess::NotRunning},
          {"processId", process_ ? process_->processId() : 0}};
}

bool LocalImageTextEncoder::start(const QString& python, const QString& worker, const QString& model_root, const QString& device) {
  if (process_) return false;
  if (!QFileInfo(python).isAbsolute() || !QFileInfo(python).isExecutable() ||
      !QFileInfo(worker).isAbsolute() || !QFileInfo(worker).isFile()) {
    fail(QStringLiteral("Explicit local encoder paths or device are invalid."));
    return false;
  }
  return startOwned(python, {"-I", "-u", worker, "--model-root", model_root, "--device", device},
                    QFileInfo(worker).absolutePath(), model_root, device);
}

bool LocalImageTextEncoder::startExecutable(const QString& executable, const QString& model_root, const QString& device) {
  if (process_) return false;
  if (!QFileInfo(executable).isAbsolute() || !QFileInfo(executable).isFile() || !QFileInfo(executable).isExecutable()) {
    fail(QStringLiteral("The explicit local search executable is unavailable."));
    return false;
  }
  return startOwned(executable, {"--model-root", model_root, "--device", device},
                    QFileInfo(executable).absolutePath(), model_root, device);
}

bool LocalImageTextEncoder::startOwned(const QString& program, const QStringList& arguments,
                                     const QString& working_directory, const QString& model_root, const QString& device) {
  if (!QFileInfo(model_root).isAbsolute() || !QFileInfo(model_root).isDir() || (device != "cpu" && device != "mps")) {
    fail(QStringLiteral("Explicit local model root or device is invalid."));
    return false;
  }
  error_.clear(); output_.clear(); diagnostics_.clear(); space_key_.clear();
  pending_id_.clear(); pending_kind_.clear(); discard_reply_ = false; ready_ = false;
  device_ = device;
  auto* child = new QProcess(this);
  process_ = child;
  auto environment = QProcessEnvironment::systemEnvironment();
  for (const auto& name : environment.keys()) {
    if (name.startsWith("PYTHON") || name.startsWith("HF_") || name.startsWith("TRANSFORMERS_") ||
        name.startsWith("TOKENIZERS_") || name.startsWith("DYLD_") || name == "LD_PRELOAD") environment.remove(name);
  }
  environment.insert("HF_HUB_OFFLINE", "1");
  environment.insert("TRANSFORMERS_OFFLINE", "1");
  environment.insert("HF_HUB_DISABLE_TELEMETRY", "1");
  child->setProcessEnvironment(environment);
  // Preserve the virtualenv executable path: canonicalizing its symlink selects
  // the system environment and loses the isolated installed dependencies.
  child->setProgram(QFileInfo(program).absoluteFilePath());
  child->setArguments(arguments);
  child->setWorkingDirectory(working_directory);
  child->setProcessChannelMode(QProcess::SeparateChannels);
  connect(child, &QProcess::readyReadStandardOutput, this, [this, child] { if (process_ == child) readOutput(); });
  connect(child, &QProcess::readyReadStandardError, this, [this, child] {
    if (process_ != child) return;
    diagnostics_.append(child->readAllStandardError());
    diagnostics_ = diagnostics_.right(16384);
  });
  connect(child, &QProcess::errorOccurred, this, [this, child](QProcess::ProcessError error) {
    if (process_ != child) return;
    if (error == QProcess::FailedToStart) {
      process_.clear(); child->deleteLater();
      fail(QStringLiteral("Local encoder could not start."));
    }
  });
  connect(child, &QProcess::finished, this, [this, child](int code, QProcess::ExitStatus) {
    if (process_ != child) return;
    // Never publish a final buffered embedding from a worker that has exited.
    process_.clear(); child->deleteLater(); deadline_.stop();
    ready_ = false; space_key_.clear();
    const auto pending = pending_id_;
    const bool cancelled = discard_reply_;
    pending_id_.clear(); pending_kind_.clear(); discard_reply_ = false;
    if (state_ == "stopping") state_ = "stopped";
    else if (state_ != "failed") {
      state_ = "failed"; error_ = QStringLiteral("Local encoder exited (%1); no request was replayed.").arg(code);
    }
    emit changed();
    if (!pending.isEmpty() && !cancelled) emit requestFailed(pending, error_);
  });
  state_ = "starting";
  deadline_.start(120000);
  child->start();
  emit changed();
  return true;
}

QString LocalImageTextEncoder::encodeText(const QString& text) {
  if (text.trimmed().isEmpty() || text.size() > 2000) return {};
  return submit("text", "text", text);
}

QString LocalImageTextEncoder::encodeImage(const QString& path) {
  if (path.size() > 4096 || !QFileInfo(path).isAbsolute() || !QFileInfo(path).isFile()) return {};
  return submit("image", "path", path);
}

QString LocalImageTextEncoder::submit(const QString& kind, const QString& field, const QString& value) {
  if (!ready_ || !process_ || !pending_id_.isEmpty()) return {};
  pending_id_ = QUuid::createUuid().toString(QUuid::WithoutBraces);
  pending_kind_ = kind; discard_reply_ = false;
  const auto id = pending_id_;
  const auto payload = QJsonDocument(QJsonObject{{"id", id}, {"kind", kind}, {field, value}}).toJson(QJsonDocument::Compact) + '\n';
  if (process_->write(payload) != payload.size()) { fail(QStringLiteral("Could not submit the encoder request.")); return {}; }
  deadline_.start(30000);
  emit changed();
  return id;
}

void LocalImageTextEncoder::cancel() {
  if (pending_id_.isEmpty() || discard_reply_) return;
  discard_reply_ = true;
  const auto id = pending_id_;
  emit changed(); emit requestCancelled(id);
  // Keep one occupied slot until the reply drains; never queue replacement work
  // behind an unknown amount of GPU computation. The deadline still bounds it.
}

void LocalImageTextEncoder::terminateOwned() {
  const auto child = process_;
  if (!child) return;
  child->terminate();
  QTimer::singleShot(500, this, [child] { if (child && child->state() != QProcess::NotRunning) child->kill(); });
}

void LocalImageTextEncoder::stop() {
  deadline_.stop(); ready_ = false; space_key_.clear();
  const auto pending = pending_id_;
  const bool cancelled = discard_reply_;
  pending_id_.clear(); pending_kind_.clear(); discard_reply_ = false;
  state_ = process_ ? "stopping" : "stopped";
  terminateOwned();
  emit changed();
  if (!pending.isEmpty() && !cancelled) emit requestFailed(pending, QStringLiteral("Local encoder stopped."));
}

void LocalImageTextEncoder::fail(const QString& message) {
  error_ = message; state_ = "failed"; ready_ = false; space_key_.clear(); deadline_.stop();
  const auto pending = pending_id_;
  const bool cancelled = discard_reply_;
  pending_id_.clear(); pending_kind_.clear(); discard_reply_ = false;
  terminateOwned();
  emit changed();
  if (!pending.isEmpty() && !cancelled) emit requestFailed(pending, message);
}

void LocalImageTextEncoder::readOutput() {
  if (!process_ || state_ == "stopping" || state_ == "failed") return;
  output_.append(process_->readAllStandardOutput());
  if (output_.size() > kOutputLimit) { fail(QStringLiteral("Local encoder exceeded its response limit.")); return; }
  while (output_.contains('\n')) {
    const auto boundary = output_.indexOf('\n');
    const auto line = output_.left(boundary); output_.remove(0, boundary + 1);
    const auto object = QJsonDocument::fromJson(line).object();
    const auto type = object.value("type").toString();
    if (type == "fatal" && object.size() == 2 && !object.value("message").toString().trimmed().isEmpty() &&
        object.value("message").toString().size() <= 500) { fail(object.value("message").toString()); return; }
    if (!ready_) {
      if (state_ != "starting" || type != "ready" || object.size() != 6 || object.value("protocol").toDouble() != 1 ||
          object.value("dimensions").toDouble() != 768 || object.value("revision") != kRevision ||
          object.value("device").toString() != device_ || !hash_valid(object.value("spaceKey").toString())) {
        fail(QStringLiteral("Invalid local encoder readiness handshake.")); return;
      }
      space_key_ = object.value("spaceKey").toString(); ready_ = true; state_ = "ready"; deadline_.stop();
      emit changed();
      continue;
    }
    if (pending_id_.isEmpty() || object.value("id").toString() != pending_id_) {
      fail(QStringLiteral("Unsolicited or stale encoder response.")); return;
    }
    const auto id = pending_id_;
    const bool discarded = discard_reply_;
    if (type == "error" && object.size() == 3 && !object.value("message").toString().trimmed().isEmpty() &&
        object.value("message").toString().size() <= 500) {
      pending_id_.clear(); pending_kind_.clear(); discard_reply_ = false; deadline_.stop();
      emit changed();
      if (!discarded) emit requestFailed(id, object.value("message").toString());
      continue;
    }
    const auto vector = object.value("vector").toArray();
    double squared = 0;
    bool valid = type == "embedding" && object.size() == 7 && object.value("kind").toString() == pending_kind_ &&
        object.value("spaceKey").toString() == space_key_ && hash_valid(object.value("contentKey").toString()) &&
        vector.size() == 768 && object.value("milliseconds").isDouble() &&
        std::isfinite(object.value("milliseconds").toDouble()) && object.value("milliseconds").toDouble() >= 0;
    for (const auto& element : vector) {
      const auto value = element.toDouble();
      if (!element.isDouble() || !std::isfinite(value)) valid = false;
      squared += value * value;
    }
    if (!valid || std::abs(squared - 1.0) > 0.0001) { fail(QStringLiteral("Invalid local encoder vector response.")); return; }
    pending_id_.clear(); pending_kind_.clear(); discard_reply_ = false; deadline_.stop();
    emit changed();
    if (!discarded) emit embeddingReady(id, object.toVariantMap());
  }
}
}  // namespace photo_editor::desktop
