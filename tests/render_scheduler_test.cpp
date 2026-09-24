#include "render_scheduler.h"

#include <cstdlib>
#include <iostream>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QThread>

namespace {

int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

bool wait_for_value(const int& value, int expected, int timeout_ms) {
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < timeout_ms) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
    if (value == expected) {
      return true;
    }
    QThread::msleep(2);
  }
  QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
  return value == expected;
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  auto interactive_count = 0;
  auto full_count = 0;
  auto dispatch_count = 0;
  photo_editor::desktop::RenderScheduler scheduler(
      &app,
      [&interactive_count]() { ++interactive_count; },
      [&full_count]() { ++full_count; },
      [&dispatch_count]() { ++dispatch_count; });

  scheduler.scheduleInteractive(40);
  scheduler.scheduleFull(80);
  if (!scheduler.interactiveActive() || !scheduler.fullActive() ||
      scheduler.interactiveDelayMs() != 40 || scheduler.fullDelayMs() != 80) {
    return fail("scheduler should publish independent interactive and full timing state");
  }
  scheduler.scheduleInteractive(5);
  if (!wait_for_value(interactive_count, 1, 250) || full_count != 0) {
    return fail("rescheduling should coalesce the interactive stage without firing full");
  }
  scheduler.stopFull();
  QThread::msleep(90);
  QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
  if (full_count != 0 || scheduler.fullActive()) {
    return fail("stopping the full stage should cancel its pending callback");
  }

  scheduler.schedulePendingDispatchIfIdle(false, false);
  scheduler.schedulePendingDispatchIfIdle(true, true);
  QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
  if (dispatch_count != 0 || scheduler.pendingDispatchActive()) {
    return fail("dispatch should remain idle without a stranded pending request");
  }
  scheduler.schedulePendingDispatchIfIdle(true, false);
  scheduler.schedulePendingDispatchIfIdle(true, false);
  if (!scheduler.pendingDispatchActive() ||
      !wait_for_value(dispatch_count, 1, 250)) {
    return fail("pending request dispatch should coalesce and run on the next event turn");
  }

  scheduler.scheduleInteractive(-5);
  scheduler.scheduleFull(0);
  if (!wait_for_value(interactive_count, 2, 250) ||
      !wait_for_value(full_count, 1, 250)) {
    return fail("non-positive delays should schedule both stages on the next event turn");
  }

  scheduler.scheduleInteractive(100);
  scheduler.scheduleFull(100);
  scheduler.schedulePendingDispatch();
  scheduler.stopAll();
  if (scheduler.interactiveActive() || scheduler.fullActive() ||
      scheduler.pendingDispatchActive()) {
    return fail("stopAll should leave no render or dispatch stage scheduled");
  }

  return EXIT_SUCCESS;
}
