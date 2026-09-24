#include "managed_agent_runtime.h"
#include "agent_runtime_paths.h"
#include "agent_model_artifact.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QTcpServer>
#include <QtConcurrent>

namespace photo_editor::desktop {

ManagedAgentRuntime::ManagedAgentRuntime(QObject* parent) : QObject(parent) {
  network_.setProxy(QNetworkProxy::NoProxy);
  poll_timer_.setInterval(200);
  startup_timer_.setSingleShot(true);
  startup_timer_.setInterval(15000);
  connect(&poll_timer_, &QTimer::timeout, this, &ManagedAgentRuntime::poll);
  connect(&startup_timer_, &QTimer::timeout, this, [this] {
    const auto detail = startup_probe_error_.isEmpty() ? QString{} : QStringLiteral(" %1").arg(startup_probe_error_);
    fail(QStringLiteral("Local runtime startup timed out.%1 Retry setup.").arg(detail));
  });
  connect(&verification_watcher_, &QFutureWatcher<QVariantMap>::finished, this, [this] {
    verification_active_ = false;
    acceptModelVerification(verification_watcher_.result());
    // A canceled generation or changed artifact may have been waiting for this
    // completion. Resume on the next event-loop turn, without polling latency.
    QTimer::singleShot(0, this, [this] {
      if (ready_) verifyInstalledModels();
    });
  });
  publish();
}

ManagedAgentRuntime::~ManagedAgentRuntime() {
  stop();
  // Shutdown only; interactive startup and readiness never block the UI thread.
  const QPointer<QProcess> child(process_);
  if (child && child->state() != QProcess::NotRunning) {
    if (!child->waitForFinished(1000) && child && child->state() != QProcess::NotRunning) {
      child->kill();
      child->waitForFinished(1000);
    }
  }
}

QVariantMap ManagedAgentRuntime::snapshot() const {
  return {{"state", state_}, {"ready", ready_}, {"error", error_},
          {"endpoint", ready_ ? endpoint_ : QString()}, {"binaryPath", binary_},
          {"modelsPath", model_store_}, {"version", version_},
          {"installedModels", installed_models_}, {"cloudEnabled", false},
          {"verifiedModels", verified_models_}, {"modelVerification", model_verification_},
          {"processId", process_ ? process_->processId() : 0},
          {"ownsProcess", process_ && process_->state() != QProcess::NotRunning}};
}

void ManagedAgentRuntime::publish() {
  publish_managed_agent_runtime(snapshot());
  emit changed();
}

bool ManagedAgentRuntime::start(const QString& binary, const QString& model_store, quint16 port) {
  if (process_) return false;
  ++generation_;
  if (verification_canceled_) verification_canceled_->store(true, std::memory_order_relaxed);
  verified_models_.clear();
  model_verification_.clear();
  installing_models_.clear();
  binary_ = QFileInfo(binary).canonicalFilePath();
  model_store_ = QDir::cleanPath(model_store);
  requested_port_ = port;
  error_.clear();
  startup_probe_error_.clear();
  output_.clear();
  version_.clear();
  installed_models_.clear();
  ready_ = false;
  listening_ = false;
  health_failures_ = 0;
  endpoint_.clear();
  if (binary_.isEmpty() || !QFileInfo(binary_).isExecutable() ||
      !QFileInfo(model_store_).isAbsolute() || model_store_ == QDir::rootPath() ||
      !QDir().mkpath(model_store_)) {
    fail(QStringLiteral("The bundled runtime or private model store is unavailable."));
    return false;
  }
  store_lock_ = std::make_unique<QLockFile>(QDir(model_store_).filePath(QStringLiteral(".framehouse-owner.lock")));
  store_lock_->setStaleLockTime(0);
  if (!store_lock_->tryLock(0)) {
    fail(QStringLiteral("Another app instance owns this private model store. No server was adopted or stopped."));
    return false;
  }
  QTcpServer reservation;
  if (!reservation.listen(QHostAddress::LocalHost, port)) {
    fail(QStringLiteral("The runtime port is already in use. No existing server was adopted or stopped."));
    return false;
  }
  const auto actual_port = reservation.serverPort();
  endpoint_ = QStringLiteral("http://127.0.0.1:%1").arg(actual_port);
  reservation.close();
  auto* child = new QProcess(this);
  process_ = child;
  auto environment = QProcessEnvironment::systemEnvironment();
  for (const auto& key : environment.keys()) {
    if (key.startsWith(QStringLiteral("OLLAMA_"))) environment.remove(key);
  }
  environment.insert(QStringLiteral("OLLAMA_HOST"), endpoint_);
  environment.insert(QStringLiteral("OLLAMA_MODELS"), model_store_);
  environment.insert(QStringLiteral("OLLAMA_NO_CLOUD"), QStringLiteral("1"));
  environment.insert(QStringLiteral("OLLAMA_NOPRUNE"), QStringLiteral("1"));
  environment.insert(QStringLiteral("OLLAMA_NUM_PARALLEL"), QStringLiteral("1"));
  environment.insert(QStringLiteral("OLLAMA_MAX_LOADED_MODELS"), QStringLiteral("1"));
  environment.insert(QStringLiteral("OLLAMA_MAX_QUEUE"), QStringLiteral("4"));
  environment.insert(QStringLiteral("OLLAMA_CONTEXT_LENGTH"), QStringLiteral("4096"));
  environment.insert(QStringLiteral("OLLAMA_KEEP_ALIVE"), QStringLiteral("1m"));
  child->setProcessEnvironment(environment);
  child->setProgram(binary_);
  child->setArguments({QStringLiteral("serve")});
  child->setProcessChannelMode(QProcess::MergedChannels);
  connect(child, &QProcess::readyReadStandardOutput, this, &ManagedAgentRuntime::readOutput);
  connect(child, &QProcess::errorOccurred, this, [this, child](QProcess::ProcessError error) {
    if (process_ != child) return;
    if (error == QProcess::FailedToStart) {
      process_ = nullptr;
      child->deleteLater();
      fail(QStringLiteral("The local runtime could not start: %1").arg(child->errorString()));
    }
  });
  connect(child, &QProcess::finished, this, [this, child](int code, QProcess::ExitStatus) {
    if (process_ != child) return;
    readOutput();
    process_ = nullptr;
    store_lock_.reset();
    child->deleteLater();
    poll_timer_.stop();
    startup_timer_.stop();
    if (reply_) reply_->abort();
    ready_ = false;
    endpoint_.clear();
    installed_models_.clear();
    verified_models_.clear();
    model_verification_.clear();
    if (state_ == QStringLiteral("stopping")) state_ = QStringLiteral("stopped");
    else if (state_ != QStringLiteral("failed")) {
      state_ = QStringLiteral("failed");
      error_ = QStringLiteral("The local runtime exited (%1). Retry setup; no requests were replayed.").arg(code);
    }
    publish();
  });
  state_ = QStringLiteral("starting");
  child->start();
  poll_timer_.setInterval(200);
  poll_timer_.start();
  startup_timer_.start();
  publish();
  return true;
}

void ManagedAgentRuntime::retry() {
  if (!process_ && !binary_.isEmpty()) start(binary_, model_store_, requested_port_);
}

void ManagedAgentRuntime::stop() {
  ++generation_;
  if (verification_canceled_) verification_canceled_->store(true, std::memory_order_relaxed);
  verified_models_.clear();
  model_verification_.clear();
  installing_models_.clear();
  poll_timer_.stop();
  startup_timer_.stop();
  ready_ = false;
  state_ = process_ ? QStringLiteral("stopping") : QStringLiteral("stopped");
  if (!process_) store_lock_.reset();
  if (reply_) reply_->abort();
  publish();
  if (!process_) return;
  const QPointer<QProcess> child(process_);
  child->terminate();
  QTimer::singleShot(1000, this, [child] {
    if (child && child->state() != QProcess::NotRunning) child->kill();
  });
}

void ManagedAgentRuntime::fail(const QString& message) {
  error_ = message;
  stop();
  state_ = QStringLiteral("failed");
  publish();
}

void ManagedAgentRuntime::readOutput() {
  if (!process_) return;
  output_.append(process_->readAllStandardOutput());
  // The pinned server emits this only after binding. A version response from a
  // different process that won a port race must not establish ownership.
  const auto authority = QUrl(endpoint_).authority().toUtf8();
  if (!authority.isEmpty() && output_.contains("Listening on " + authority + " (version 0.33.3)")) listening_ = true;
  if (output_.size() > 16384) output_ = output_.right(16384);
}

void ManagedAgentRuntime::poll() {
  if (!process_ || process_->state() != QProcess::Running || !listening_ || reply_) return;
  const auto path = version_.isEmpty() ? QStringLiteral("/api/version") : QStringLiteral("/api/tags");
  QNetworkRequest request(QUrl(endpoint_ + path));
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
  request.setTransferTimeout(1500);
  auto* response = network_.get(request);
  reply_ = response;
  const QPointer<QProcess> owner(process_);
  connect(response, &QNetworkReply::readyRead, this, [response] {
    if (response->bytesAvailable() > 1024 * 1024) response->abort();
  });
  connect(response, &QNetworkReply::finished, this, [this, response, owner, path] {
    const auto bytes = response->isOpen() ? response->readAll() : QByteArray{};
    const auto status = response->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const auto network_error = response->error();
    const auto ok = network_error == QNetworkReply::NoError && status == 200 && bytes.size() <= 1024 * 1024;
    response->deleteLater();
    if (reply_ == response) reply_.clear();
    if (!owner || process_ != owner || owner->state() != QProcess::Running ||
        state_ == QStringLiteral("stopping") || state_ == QStringLiteral("failed")) return;
    const auto document = QJsonDocument::fromJson(bytes);
    if (!ok || !document.isObject()) {
      if (!ready_) {
        startup_probe_error_ = QStringLiteral("HTTP %1, network error %2: %3")
            .arg(status).arg(static_cast<int>(network_error))
            .arg(response->errorString().left(256));
      }
      // A newly bound server can still be initializing. The startup timer owns
      // that deadline; the shorter failure threshold applies only after ready.
      if (ready_ && ++health_failures_ >= 3) {
        fail(QStringLiteral("The owned local runtime stopped responding (HTTP %1, network error %2). Retry setup.")
                 .arg(status).arg(static_cast<int>(network_error)));
      }
      return;
    }
    health_failures_ = 0;
    startup_probe_error_.clear();
    const auto object = document.object();
    if (path == QStringLiteral("/api/version")) {
      if (object.value(QStringLiteral("version")).toString() != QStringLiteral("0.33.3")) {
        fail(QStringLiteral("The local runtime version does not match the bundled release."));
        return;
      }
      version_ = QStringLiteral("0.33.3");
      poll();
      return;
    }
    if (!object.value(QStringLiteral("models")).isArray()) {
      fail(QStringLiteral("The local runtime returned an invalid model inventory."));
      return;
    }
    QStringList names;
    for (const auto& value : object.value(QStringLiteral("models")).toArray()) {
      const auto name = value.toObject().value(QStringLiteral("name")).toString();
      if (!name.isEmpty()) names.append(name);
    }
    names.sort();
    const auto changed = !ready_ || names != installed_models_;
    installed_models_ = names;
    for (const auto& name : model_verification_.keys()) {
      if (!names.contains(name)) {
        model_verification_.remove(name);
        verified_models_.removeAll(name);
      }
    }
    ready_ = true;
    state_ = QStringLiteral("ready");
    startup_timer_.stop();
    poll_timer_.setInterval(2000);
    if (changed) publish();
    verifyInstalledModels();
  });
}

void ManagedAgentRuntime::verifyInstalledModels() {
  for (const auto& name : installed_models_) {
    if (installing_models_.contains(name)) continue;
    const auto pin = pinned_agent_model_artifact(name);
    if (pin.files.isEmpty()) continue;
    const auto fingerprint = agent_model_artifact_fingerprint(model_store_, pin);
    const auto previous = model_verification_.value(name).toMap();
    if (!previous.isEmpty() && previous.value("fingerprint").toString() == fingerprint) continue;
    verified_models_.removeAll(name);
    if (fingerprint.isEmpty()) {
      model_verification_.insert(name, QVariantMap{{"state", "failed"}, {"fingerprint", fingerprint},
          {"error", "Pinned model files are missing, incomplete, or outside the private store."}});
      publish();
      continue;
    }
    if (verification_active_) {
      // Invalidate an old ready result immediately, even while another scan is
      // finishing. A later inventory poll will start the replacement scan.
      model_verification_.remove(name);
      publish();
      continue;
    }
    model_verification_.insert(name, QVariantMap{{"state", "verifying"}, {"fingerprint", fingerprint}});
    verification_canceled_ = std::make_shared<std::atomic_bool>(false);
    const auto root = model_store_;
    const auto generation = generation_;
    const auto canceled = verification_canceled_;
    verification_active_ = true;
    verification_watcher_.setFuture(QtConcurrent::run([root, pin, generation, canceled] {
      auto result = verify_agent_model_artifact(root, pin, canceled);
      result.insert(QStringLiteral("generation"), QVariant::fromValue(generation));
      return result;
    }));
    publish();
  }
}

void ManagedAgentRuntime::acceptModelVerification(QVariantMap result) {
  if (result.value("generation").toULongLong() != generation_ || !ready_) return;
  const auto model = result.value("model").toString();
  if (!installed_models_.contains(model) || installing_models_.contains(model)) return;
  verified_models_.removeAll(model);
  if (result.value("ok").toBool()) {
    // The worker checked freshness before returning, but files can change while
    // its result is queued. Never expose even one transient ready publication
    // for an artifact that no longer matches the verified snapshot.
    const auto fingerprint = agent_model_artifact_fingerprint(
        model_store_, pinned_agent_model_artifact(model));
    if (fingerprint.isEmpty() || fingerprint != result.value("fingerprint").toString()) {
      model_verification_.remove(model);
      publish();
      return;
    }
  }
  result.insert(QStringLiteral("state"), result.value("ok").toBool() ? QStringLiteral("verified") : QStringLiteral("failed"));
  model_verification_.insert(model, result);
  if (result.value("ok").toBool()) verified_models_.append(model);
  publish();
}

void ManagedAgentRuntime::setModelInstalling(const QString& model, bool installing) {
  ++generation_;
  if (verification_canceled_) verification_canceled_->store(true, std::memory_order_relaxed);
  for (const auto& name : model_verification_.keys()) {
    if (model_verification_.value(name).toMap().value("state").toString() == "verifying")
      model_verification_.remove(name);
  }
  verified_models_.removeAll(model);
  model_verification_.remove(model);
  installing_models_.removeAll(model);
  if (installing) {
    installing_models_.append(model);
    model_verification_.insert(model, QVariantMap{{"state", "downloading"},
        {"error", "Model download is in progress. Verification is required before use."}});
  }
  publish();
  if (ready_ && !installing) verifyInstalledModels();
}

}  // namespace photo_editor::desktop
