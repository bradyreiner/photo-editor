#pragma once

#include <QElapsedTimer>
#include <QProcess>
#include <QProcessEnvironment>
#include <QVariantMap>
#include <atomic>
#include <memory>
#include <mutex>

namespace photo_editor::desktop {

// A bounded mailbox, not a queue of output snapshots. No GUI objects cross threads.
struct GaussianProcessLog {
  std::mutex mutex;
  QString tail;
};

inline QVariantMap run_gaussian_process(const QString& program, const QStringList& arguments,
    const QString& directory, const QProcessEnvironment& environment,
    const std::shared_ptr<std::atomic_bool>& canceled,
    const std::shared_ptr<GaussianProcessLog>& log) {
  if (canceled->load()) return {{"exitCode", -1}, {"exitStatus", int(QProcess::CrashExit)}};
  // The worker owns the process through termination and reaping, even if its
  // controller is destroyed. All blocking QProcess operations stay off the GUI.
  QProcess process;
  process.setProcessChannelMode(QProcess::MergedChannels);
  process.setWorkingDirectory(directory);
  process.setProcessEnvironment(environment);
  process.start(program, arguments);
  QElapsedTimer startup, termination;
  startup.start();
  bool killed = false;
  bool startup_timeout = false;
  auto drain = [&] {
    const auto bytes = process.readAllStandardOutput();
    if (bytes.isEmpty()) return;
    std::lock_guard lock(log->mutex);
    log->tail = (log->tail + QString::fromUtf8(bytes)).right(32768);
  };
  while (process.state() != QProcess::NotRunning) {
    if (process.state() == QProcess::Starting && startup.elapsed() >= 30000)
      startup_timeout = true;
    if ((canceled->load() || startup_timeout) && !termination.isValid()) {
      process.terminate();
      termination.start();
    }
    if (termination.isValid() && termination.elapsed() >= 1000 && !killed) {
      process.kill();
      killed = true;
    }
    if (process.state() == QProcess::Starting) process.waitForStarted(50);
    else process.waitForReadyRead(50);
    drain();
  }
  drain();
  return {{"exitCode", process.exitCode()}, {"exitStatus", int(process.exitStatus())},
      {"failedToStart", process.error() == QProcess::FailedToStart || startup_timeout},
      {"error", startup_timeout ? QStringLiteral("Backend startup timed out.") : process.errorString()}};
}

}  // namespace photo_editor::desktop
