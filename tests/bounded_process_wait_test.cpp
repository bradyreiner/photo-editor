#include <QCoreApplication>
#include <QElapsedTimer>
#include <QProcess>
#include <QThread>

#include <iostream>

#include "bounded_process_wait.h"

namespace {

int fail(const char* message) {
  std::cerr << message << '\n';
  return 1;
}

}  // namespace

int main(int argc, char* argv[]) {
  QCoreApplication app(argc, argv);
  if (app.arguments().contains(QStringLiteral("--child-hang"))) {
    QThread::sleep(30);
    return 0;
  }

  QProcess first;
  QProcess second;
  first.start(app.applicationFilePath(), {QStringLiteral("--child-hang")});
  second.start(app.applicationFilePath(), {QStringLiteral("--child-hang")});
  if (!first.waitForStarted(2000) || !second.waitForStarted(2000)) {
    return fail("failed to start shutdown-budget child processes");
  }

  QElapsedTimer timer;
  timer.start();
  photo_editor::desktop::kill_processes_with_shared_budget(
      nullptr, {&first, &second}, 1000);
  if (timer.elapsed() > 1500) {
    return fail("process shutdown exceeded the shared global budget");
  }
  if (first.state() != QProcess::NotRunning ||
      second.state() != QProcess::NotRunning) {
    return fail("shared shutdown did not terminate every child process");
  }
  return 0;
}
