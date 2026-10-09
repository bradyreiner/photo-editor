#include "cancellable_process_wait.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QThread>
#include <iostream>

using photo_editor::desktop::cancellable_process_succeeded;
#define CHECK(condition, message) do { if (!(condition)) { std::cerr << message << '\n'; return 1; } } while (false)

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  if (app.arguments().contains("--child-success")) { QThread::msleep(900); return 0; }
  if (app.arguments().contains("--child-long")) { QThread::sleep(121); return 0; }
  if (app.arguments().contains("--child-hang")) { QThread::sleep(30); return 0; }
  if (app.arguments().contains("--child-failure")) return 7;
  QProcess success;
  QElapsedTimer elapsed; elapsed.start();
  const bool long_work = app.arguments().contains("--long-work-contract");
  success.start(app.applicationFilePath(), {long_work ? "--child-long" : "--child-success"});
  CHECK(cancellable_process_succeeded(&success), "Productive work must finish without an elapsed cutoff");
  CHECK(elapsed.elapsed() >= (long_work ? 121000 : 800), "Observe real child work, not a cached result");
  QProcess pending;
  pending.start(app.applicationFilePath(), {"--child-hang"});
  CHECK(pending.waitForStarted(2000), "Start cancellable work");
  elapsed.restart();
  CHECK(!cancellable_process_succeeded(&pending, [&]() { return elapsed.elapsed() >= 180; }), "Stop interrupts a running child");
  CHECK(elapsed.elapsed() < 1500 && pending.state() == QProcess::NotRunning, "Canceled work is stopped promptly");
  QProcess failed;
  failed.start(app.applicationFilePath(), {"--child-failure"});
  CHECK(!cancellable_process_succeeded(&failed), "Nonzero process exit remains a failure");
  QProcess missing;
  missing.start("/nonexistent/looksy-test-process", {});
  CHECK(!cancellable_process_succeeded(&missing), "Missing executable cannot become an infinite wait");
  std::cout << "Productive process work, explicit cancellation and genuine failures passed.\n";
  return 0;
}
