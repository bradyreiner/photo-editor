#include "search_model_setup_controller.h"

namespace photo_editor::desktop {
SearchModelSetupController::SearchModelSetupController(LocalSimilaritySearch* search, QString root, QString device,
    QString index, QObject* parent, Verify verify, Install install)
    : QObject(parent), search_(search), root_(std::move(root)), device_(std::move(device)), index_(std::move(index)),
      verify_(std::move(verify)), install_(std::move(install)) {
  if (!verify_) verify_ = [](const QString& path, const State& state) {
    return verify_agent_model_artifact(path, pinned_search_model_artifact(),
        std::shared_ptr<std::atomic_bool>(state, &state->canceled));
  };
  if (!install_) install_ = [](const SearchModelInstallRequest& request, const State& state) {
    if (state->canceled.load()) return QVariantMap{{"ok", false}, {"canceled", true}};
    const auto parent_path = QFileInfo(request.root).dir().absolutePath();
    if (!QDir().mkpath(parent_path)) return QVariantMap{{"ok", false}, {"error", "Cannot create search-model storage."}};
    return install_search_model(request, state);
  };
  timer_.setInterval(100);
  connect(&timer_, &QTimer::timeout, this, &SearchModelSetupController::pump);
  connect(&watcher_, &QFutureWatcher<QVariantMap>::finished, this, &SearchModelSetupController::finish);
}
SearchModelSetupController::~SearchModelSetupController() {
  shutdown(); watcher_.disconnect(this);
}
void SearchModelSetupController::setRuntime(const QString& executable) {
  if (running_ || shutting_down_ || !QFileInfo(executable).isAbsolute()) return;
  executable_ = executable;
  verify();
}
bool SearchModelSetupController::verify() { return begin("verify"); }
bool SearchModelSetupController::download(bool confirmed) {
  if (!confirmed) {
    if (!running_) { status_ = "Confirm the 1.54 GB local model download before starting."; emit changed(); }
    return false;
  }
  return begin("download");
}
bool SearchModelSetupController::importFolder(const QUrl& folder) {
  if (!folder.isLocalFile() || !QFileInfo(folder.toLocalFile()).isAbsolute()) {
    if (!running_) { status_ = "Choose an existing local model folder. No model was imported."; emit changed(); }
    return false;
  }
  return begin("import", folder.toLocalFile());
}
bool SearchModelSetupController::begin(const QString& operation, const QString& source) {
  if (!available() || running_ || shutting_down_ || !search_) return false;
  state_ = std::make_shared<AgentModelDownloadState>();
  operation_ = operation; source_ = source; pending_ = true; running_ = true; ready_ = false; progress_ = 0;
  status_ = "Stopping search before checking model files…"; waiting_.start();
  // Clear admission immediately, including direct calls that bypass UI state.
  search_->clearConfiguration();
  emit changed();
  if (running_) { timer_.start(); pump(); }
  return running_;
}
void SearchModelSetupController::pump() {
  if (!running_ || shutting_down_ || !search_) return;
  if (!pending_) {
    const auto snapshot = state_->snapshot();
    if (!snapshot.isEmpty() && !state_->canceled.load()) {
      status_ = snapshot.value("status").toString(); progress_ = snapshot.value("progress").toDouble(); emit changed();
    }
    return;
  }
  if (search_->running()) {
    if (waiting_.elapsed() > 10000) {
      pending_ = running_ = false; timer_.stop();
      status_ = "Search has not stopped yet. Wait and retry model setup."; emit changed();
    }
    return;
  }
  pending_ = false;
  status_ = operation_ == "verify" ? "Verifying all local search model files…" : "Preparing verified search model setup…";
  const auto state = state_; const auto root = root_; const auto source = source_; const auto operation = operation_;
  const auto verify = verify_; const auto install = install_;
  watcher_.setFuture(QtConcurrent::run([state, root, source, operation, verify, install] {
    try {
      if (state->canceled.load()) return QVariantMap{{"ok", false}, {"canceled", true}};
      return operation == "verify" ? verify(root, state) : install({root, source, operation == "download"}, state);
    } catch (...) { return QVariantMap{{"ok", false}, {"error", "Search model setup worker failed."}}; }
  }));
  emit changed();
}
void SearchModelSetupController::finish() {
  timer_.stop(); running_ = pending_ = false;
  if (shutting_down_ || !search_) return;
  const auto result = watcher_.result();
  if (result.value("ok").toBool()) {
    ready_ = search_->configureExecutable(executable_, root_, device_, index_);
    progress_ = ready_ ? 1.0 : 0.0;
    status_ = ready_ ? "SigLIP 2 is verified and ready for local similarity search."
                     : "Model verified, but search could not be configured. Retry verification.";
  } else if (state_->canceled.load() || result.value("canceled").toBool()) {
    status_ = "Model setup paused. Choose download or import again to resume, or verify existing files.";
  } else {
    status_ = result.value("error").toString();
    if (status_.isEmpty()) status_ = "Search model is unavailable. Download it or import a verified local copy.";
  }
  const auto backups = result.value("quarantinedPaths").toStringList();
  if (!backups.isEmpty()) status_ += QString(" %1 replaced file(s) were preserved beside the model for recovery.").arg(backups.size());
  if (!ready_) search_->setSetupStatus(status_);
  emit changed();
}
void SearchModelSetupController::cancel() {
  if (!running_) return;
  state_->canceled.store(true);
  status_ = "Pausing model setup…";
  if (pending_) { pending_ = running_ = false; timer_.stop(); status_ = "Model setup paused before starting."; }
  emit changed();
}
void SearchModelSetupController::shutdown() {
  shutting_down_ = true;
  if (state_) state_->canceled.store(true);
  timer_.stop();
  if (search_) search_->stop();
}
}  // namespace photo_editor::desktop
