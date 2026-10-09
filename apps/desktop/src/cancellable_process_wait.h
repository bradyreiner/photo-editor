#pragma once

#include <QProcess>
#include <functional>

namespace photo_editor::desktop {

// For work on a worker thread. Polling keeps cancellation observable without
// imposing an elapsed work deadline. Startup and forced shutdown are separate.
inline bool cancellable_process_succeeded(QProcess* process,
    const std::function<bool()>& canceled = {}) {
  if (!process || process->program().isEmpty()) return false;
  const auto stop = [&]() {
    if (process->state() != QProcess::NotRunning) {
      process->terminate();
      if (!process->waitForFinished(500)) {
        process->kill();
        process->waitForFinished(500);
      }
    }
    return false;
  };
  if (canceled && canceled()) return stop();
  if (process->state() == QProcess::Starting && !process->waitForStarted(5000)) return stop();
  while (process->state() != QProcess::NotRunning) {
    if (canceled && canceled()) return stop();
    process->waitForFinished(50);
  }
  return !(canceled && canceled()) && process->error() != QProcess::FailedToStart &&
      process->exitStatus() == QProcess::NormalExit && process->exitCode() == 0;
}

}  // namespace photo_editor::desktop
