#include "editor_close_coordinator.h"
#include <QCoreApplication>
#include <QEvent>

namespace photo_editor::desktop {
EditorCloseCoordinator::EditorCloseCoordinator(QProcess* helper,
    std::function<bool(QString*)> checkpoint, std::function<void(bool)> pause, QObject* parent)
    : QObject(parent), helper_(helper), checkpoint_(std::move(checkpoint)), pause_(std::move(pause)) {
  timer_.setSingleShot(true);
  if (helper) connect(helper, &QProcess::stateChanged, this, [this](QProcess::ProcessState state) {
    // QProcess publishes NotRunning before finished. Let the controller's
    // acknowledgement slot update the queue before the final checkpoint.
    if (state == QProcess::NotRunning) QTimer::singleShot(0, this, &EditorCloseCoordinator::finish);
  });
  connect(&timer_, &QTimer::timeout, this, [this] {
    if (!closing()) return;
    if (!helper_ || helper_->state() == QProcess::NotRunning) { finish(); return; }
    if (phase_ == Phase::Waiting) {
      phase_ = Phase::Terminating;
      helper_->terminate();
      timer_.start(250);
    } else if (phase_ == Phase::Terminating) {
      phase_ = Phase::Killing;
      helper_->kill();
      timer_.start(1000);
    } else {
      fail(QStringLiteral("The save helper has not stopped. Your checkpoint was retained; try closing again."));
    }
  });
}

void EditorCloseCoordinator::installQuitGuard(QObject* application) {
  if (quit_target_ || !application) return;
  quit_target_ = application;
  application->installEventFilter(this);
  connect(this, &EditorCloseCoordinator::closeReady, application, [] { QCoreApplication::quit(); }, Qt::QueuedConnection);
}

bool EditorCloseCoordinator::checkpoint() {
  QString error;
  if (checkpoint_ && checkpoint_(&error)) return true;
  fail(error.isEmpty() ? QStringLiteral("Edits could not be checkpointed. Keep the app open and retry.") : error);
  return false;
}

void EditorCloseCoordinator::fail(const QString& message) {
  timer_.stop();
  phase_ = Phase::Failed;
  error_ = message;
  if (pause_) pause_(false);
  emit stateChanged();
}

void EditorCloseCoordinator::requestClose() {
  if (closing() || ready()) return;
  if (!checkpoint()) return;
  error_.clear();
  if (pause_) pause_(true);
  phase_ = Phase::Waiting;
  emit stateChanged();
  if (!helper_ || helper_->state() == QProcess::NotRunning) {
    QTimer::singleShot(0, this, &EditorCloseCoordinator::finish);
  } else {
    timer_.start(1500);
  }
}

bool EditorCloseCoordinator::handleWindowClose() {
  if (ready()) return checkpoint();
  requestClose();
  return false;
}

void EditorCloseCoordinator::finish() {
  if (!closing() || (helper_ && helper_->state() != QProcess::NotRunning)) return;
  timer_.stop();
  if (!checkpoint()) return;
  phase_ = Phase::Ready;
  emit stateChanged();
  emit closeReady();
}

void EditorCloseCoordinator::keepEditing() {
  if (ready()) return;
  timer_.stop();
  phase_ = Phase::Idle;
  error_.clear();
  if (pause_) pause_(false);
  emit stateChanged();
}

bool EditorCloseCoordinator::eventFilter(QObject* watched, QEvent* event) {
  if (watched != quit_target_ || event->type() != QEvent::Quit) return false;
  // Revalidate at the actual Quit boundary, including edits arriving after
  // closeReady was queued. A failed checkpoint must never become consent.
  if (ready() && checkpoint()) return false;
  event->ignore();
  if (phase_ != Phase::Failed) requestClose();
  return true;
}
}  // namespace photo_editor::desktop
