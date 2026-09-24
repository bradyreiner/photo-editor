#pragma once

#include <algorithm>
#include <initializer_list>

#include <QElapsedTimer>
#include <QObject>
#include <QProcess>

namespace photo_editor::desktop {

inline bool bounded_process_succeeded(QProcess* process, int timeout_ms) {
  if (process == nullptr) {
    return false;
  }
  if (process->state() == QProcess::Starting &&
      !process->waitForStarted(1000)) {
    return false;
  }
  if (process->state() == QProcess::NotRunning) {
    return process->exitStatus() == QProcess::NormalExit &&
           process->exitCode() == 0;
  }
  if (!process->waitForFinished(timeout_ms)) {
    process->terminate();
    if (!process->waitForFinished(500)) {
      process->kill();
      process->waitForFinished(500);
    }
    return false;
  }
  return process->exitStatus() == QProcess::NormalExit &&
         process->exitCode() == 0;
}

inline void kill_processes_with_shared_budget(
    QObject* receiver,
    std::initializer_list<QProcess*> processes,
    int total_timeout_ms) {
  QElapsedTimer timer;
  timer.start();
  for (auto* process : processes) {
    if (process == nullptr) {
      continue;
    }
    if (receiver != nullptr) {
      process->disconnect(receiver);
    }
    if (process->state() != QProcess::NotRunning) {
      process->kill();
    }
  }
  for (auto* process : processes) {
    if (process == nullptr || process->state() == QProcess::NotRunning) {
      continue;
    }
    const auto remaining_ms =
        std::max(0, total_timeout_ms - static_cast<int>(timer.elapsed()));
    if (remaining_ms == 0) {
      return;
    }
    process->waitForFinished(remaining_ms);
  }
}

}  // namespace photo_editor::desktop
