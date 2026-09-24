#include "editor_close_coordinator.h"
#include "mask_session.h"
#include <QTemporaryDir>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QThread>
#include <csignal>
#include <iostream>
using namespace photo_editor::desktop;
namespace {
bool spin(const std::function<bool()>& predicate, int timeout = 5000) {
  QElapsedTimer timer; timer.start();
  do {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    if (predicate()) return true;
    QThread::msleep(2);
  } while (timer.elapsed() < timeout);
  return false;
}
bool start(QProcess& helper, const QString& mode) {
  helper.start(QCoreApplication::applicationFilePath(), {"--helper", mode});
  return helper.waitForStarted(2000) && helper.waitForReadyRead(2000) && helper.readAllStandardOutput().trimmed() == "READY";
}
}
int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  if (argc == 1) {
    QTemporaryDir storage;
    if (!storage.isValid()) return 1;
    const auto path = storage.filePath(QStringLiteral("selection"));
    QFile obstruction(path);
    if (!obstruction.open(QIODevice::WriteOnly)) return 1;
    obstruction.close();
    MaskSession session;
    session.setSelectionStorageDirectory(path);
    session.setAssetScope(QStringLiteral("photo"));
    session.replaceStack({QVariantMap{{QStringLiteral("id"), QStringLiteral("first")},
                                     {QStringLiteral("maskSetId"), QStringLiteral("set")}},
                          QVariantMap{{QStringLiteral("id"), QStringLiteral("component")},
                                     {QStringLiteral("maskSetId"), QStringLiteral("set")}}}, 1);
    bool paused = false;
    EditorCloseCoordinator close(nullptr, [&](QString* error) {
      if (session.flushSelectionPreferences()) return true;
      *error = QStringLiteral("Selection preferences could not be saved");
      return false;
    }, [&](bool value) { paused = value; });
    if (close.handleWindowClose() || close.error().isEmpty() || close.ready() || paused) return 1;
    close.keepEditing();
    if (!close.error().isEmpty() || paused) return 1;
    if (!obstruction.remove()) return 1;
    close.requestClose();
    if (!spin([&] { return close.ready(); }) || !paused) return 1;
    MaskSession restored;
    restored.setSelectionStorageDirectory(path);
    restored.setAssetScope(QStringLiteral("photo"));
    restored.replaceStack(session.stack(), -1);
    if (restored.indexForMaskSet(QStringLiteral("set")) != 1) return 1;
  }
  if (argc == 3 && app.arguments().at(1) == "--helper") {
    if (app.arguments().at(2) == "stubborn") std::signal(SIGTERM, SIG_IGN);
    else QTimer::singleShot(400, &app, &QCoreApplication::quit);
    std::cout << "READY\n" << std::flush;
    return app.exec();
  }
  if (app.arguments().contains("--quit-test")) {
    bool allowed = false, paused = false, blocked = false;
    EditorCloseCoordinator close(nullptr, [&](QString* error) {
      if (!allowed) *error = "injected checkpoint failure";
      return allowed;
    }, [&](bool value) { paused = value; });
    close.installQuitGuard(&app);
    QTimer::singleShot(0, &app, &QCoreApplication::quit);
    QTimer::singleShot(50, &app, [&] {
      blocked = !close.error().isEmpty() && !close.ready() && !paused;
      if (!blocked) { app.exit(1); return; }
      allowed = true;
      close.requestClose();
    });
    QTimer::singleShot(2000, &app, [&] { app.exit(1); });
    const auto result = app.exec();
    return result == 0 && blocked && close.ready() ? 0 : 1;
  }
  {
    QProcess helper;
    if (!start(helper, "stubborn")) return 1;
    bool allowed = false, paused = false;
    int checkpoints = 0, ready_count = 0, heartbeat = 0;
    EditorCloseCoordinator close(&helper, [&](QString* error) {
      ++checkpoints; if (!allowed) *error = "injected disk failure"; return allowed;
    }, [&](bool value) { paused = value; });
    QObject::connect(&close, &EditorCloseCoordinator::closeReady, &app, [&] { ++ready_count; });
    close.requestClose();
    if (close.error().isEmpty() || paused || close.closing() || helper.state() != QProcess::Running) return 1;
    close.keepEditing();
    if (!close.error().isEmpty() || paused) return 1;
    allowed = true;
    QTimer tick; tick.setInterval(5);
    QObject::connect(&tick, &QTimer::timeout, &app, [&] { ++heartbeat; });
    tick.start();
    QElapsedTimer elapsed; elapsed.start();
    close.requestClose(); close.requestClose();
    if (!paused || !close.closing() || checkpoints != 2 ||
        !spin([&] { return close.ready(); }) || elapsed.elapsed() > 3500 || heartbeat < 30 ||
        helper.state() != QProcess::NotRunning || ready_count != 1 || checkpoints != 3) return 1;
    allowed = false;
    if (close.handleWindowClose() || close.error().isEmpty() || close.ready() || paused) return 1;
  }
  {
    QProcess helper;
    if (!start(helper, "normal")) return 1;
    bool paused = false;
    int checkpoints = 0;
    EditorCloseCoordinator close(&helper, [&](QString*) { ++checkpoints; return true; },
        [&](bool value) { paused = value; });
    close.requestClose(); close.keepEditing();
    if (!spin([&] { return helper.state() == QProcess::NotRunning; }) || close.ready() || paused || checkpoints != 1) return 1;
    close.requestClose();
    if (!spin([&] { return close.ready(); }) || !paused || checkpoints != 3) return 1;
  }
  {
    int checkpoints = 0;
    bool paused = false;
    EditorCloseCoordinator close(nullptr, [&](QString* error) {
      if (++checkpoints == 2) { *error = "late checkpoint failure"; return false; } return true;
    }, [&](bool value) { paused = value; });
    close.requestClose();
    if (!spin([&] { return !close.error().isEmpty(); }) || close.ready() || paused) return 1;
    close.requestClose();
    if (!spin([&] { return close.ready(); })) return 1;
  }
  QProcess quit_test;
  quit_test.start(app.applicationFilePath(), {"--quit-test"});
  if (!quit_test.waitForFinished(4000) || quit_test.exitStatus() != QProcess::NormalExit || quit_test.exitCode() != 0) return 1;
  std::cout << "Quit interception, checkpoint rejection/retry, cancel, late failure, responsive grace/terminate/kill and stopped-helper readiness passed.\n";
}
