#include "depth_motion_controller.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QVariantMap>

#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cmath>
#include <functional>
#include <iostream>
#include <memory>
#include <sys/resource.h>
#include <sys/types.h>

using photo_editor::desktop::DepthMotionController;

namespace {

bool write_file(const QString& path, const QByteArray& contents) {
  if (!QDir().mkpath(QFileInfo(path).absolutePath())) return false;
  QFile file(path);
  return file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(contents) == contents.size();
}

bool make_executable(const QString& path, const QByteArray& contents) {
  if (!write_file(path, contents)) return false;
  QFile file(path);
  return file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner |
                             QFileDevice::ReadGroup | QFileDevice::ExeGroup |
                             QFileDevice::ReadOther | QFileDevice::ExeOther);
}

bool process_exists(qint64 pid) {
  if (pid <= 0) return false;
  if (::kill(static_cast<pid_t>(pid), 0) == 0) return true;
  return errno == EPERM;
}

qint64 peak_rss_bytes() {
  rusage usage{};
  if (getrusage(RUSAGE_SELF, &usage) != 0) return -1;
#ifdef Q_OS_MACOS
  return static_cast<qint64>(usage.ru_maxrss);
#else
  return static_cast<qint64>(usage.ru_maxrss) * 1024;
#endif
}

qint64 read_pid(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) return -1;
  bool ok = false;
  const auto pid = file.readAll().trimmed().toLongLong(&ok);
  return ok ? pid : -1;
}

int app_owned_process_count(const QCoreApplication& app, bool running_only) {
  const auto processes = app.findChildren<QProcess*>();
  if (!running_only) return static_cast<int>(processes.size());
  return static_cast<int>(std::count_if(processes.cbegin(), processes.cend(), [](const QProcess* process) {
    return process->state() != QProcess::NotRunning;
  }));
}

bool worker_scratch_files_exist(const QString& root) {
  QFile files(QDir(root).filePath("worker-files"));
  if (!files.open(QIODevice::ReadOnly)) return false;
  const auto paths = files.readAll().split('\n');
  return paths.size() >= 2 && !paths[0].isEmpty() && !paths[1].isEmpty() &&
      QFileInfo::exists(QString::fromUtf8(paths[0])) && QFileInfo::exists(QString::fromUtf8(paths[1]));
}

bool wait_until(const std::function<bool()>& predicate, int timeout_ms, QCoreApplication& app) {
  QElapsedTimer timer;
  timer.start();
  while (!predicate() && timer.elapsed() < timeout_ms) {
    app.processEvents(QEventLoop::AllEvents, 10);
    QThread::msleep(1);
  }
  app.processEvents(QEventLoop::AllEvents, 10);
  return predicate();
}

class Heartbeat final : public QObject {
 public:
  explicit Heartbeat(QCoreApplication& app) {
    timer_.setInterval(1);
    QObject::connect(&timer_, &QTimer::timeout, &app, [this] {
      const auto gap = clock_.nsecsElapsed();
      clock_.restart();
      maximum_gap_ns_ = std::max(maximum_gap_ns_, gap);
    });
    clock_.start();
    timer_.start();
  }
  void reset_window() {
    maximum_gap_ns_ = 0;
    clock_.restart();
  }
  qint64 maximum_gap_ns() const { return std::max(maximum_gap_ns_, clock_.nsecsElapsed()); }
 private:
  QTimer timer_;
  QElapsedTimer clock_;
  qint64 maximum_gap_ns_ = 0;
};

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  QTemporaryDir sandbox;
  if (!sandbox.isValid()) {
    std::cerr << "Could not create the isolated lifecycle fixture directory\n";
    return 1;
  }

  const auto root = sandbox.path();
  const auto bin = QDir(root).filePath("bin");
  const auto home = QDir(root).filePath("depth-home");
  const auto ffprobe = QDir(bin).filePath("ffprobe");
  const auto ffmpeg = QDir(bin).filePath("ffmpeg");
  const auto fake_python = QDir(bin).filePath("depth-python");
  const QByteArray ffprobe_script = R"SH(#!/bin/sh
root=${DEPTH_MOTION_FAKE_ROOT:?}
last=
for arg do last=$arg; done
printf '%s %s\n' "$$" "$last" >> "$root/probe-start.log"
case "$last" in
  *held-*.mp4|*intermediate-*.mp4)
    case "$last" in
      *held-*.mp4) witness=held-probe ;;
      *) witness=intermediate-probe ;;
    esac
    printf '%s\n' "$$" > "$root/$witness.pid"
    : > "$root/$witness.started"
    trap '' TERM
    exec /bin/sleep 30
    ;;
  *same-path.mp4)
    count=0
    if [ -f "$root/same-probe-count" ]; then count=$(/bin/cat "$root/same-probe-count"); fi
    count=$((count + 1))
    printf '%s\n' "$count" > "$root/same-probe-count"
    printf '%s\n' "$$" > "$root/same-probe-$count.pid"
    : > "$root/same-probe-$count.started"
    trap '' TERM
    exec /bin/sleep 30
    ;;
esac
printf '%s\n' '{"streams":[{"width":1280,"height":720,"duration":"8.5"}],"format":{"duration":"8.5"}}'
)SH";
  const QByteArray ffmpeg_script = "#!/bin/sh\nexit 0\n";
  const QByteArray python_script = R"SH(#!/bin/sh
root=${DEPTH_MOTION_FAKE_ROOT:?}
mode=$(/bin/cat "$root/worker-mode")
script=$2
request=$4
printf '%s\n%s\n' "$script" "$request" > "$root/worker-files"
printf '%s\n' "$$" > "$root/worker.pid"
case "$mode" in
  responsive)
    /bin/sleep 30 &
    child=$!
    printf '%s\n' "$child" > "$root/worker-child.pid"
    trap 'kill "$child" 2>/dev/null; wait "$child" 2>/dev/null; : > "$root/worker.exit"; exit 0' TERM
    : > "$root/worker.started"
    while :; do :; done
    ;;
  held)
    trap '' TERM
    : > "$root/worker.started"
    exec /bin/sleep 30
    ;;
  stubborn)
    trap '' TERM
    /bin/sleep 30 &
    child=$!
    printf '%s\n' "$child" > "$root/worker-child.pid"
    : > "$root/worker.started"
    wait "$child"
    while :; do :; done
    ;;
  parent-exits)
    /bin/sh -c 'root=$1; trap "" TERM; : > "$root/worker-child-ready"; while [ ! -f "$root/worker-parent-exit" ] || [ ! -f "$root/worker-child-release" ]; do :; done; : > "$root/worker-child-exit"; exit 0' sh "$root" &
    child=$!
    printf '%s\n' "$child" > "$root/worker-child.pid"
    while [ ! -f "$root/worker-child-ready" ]; do :; done
    trap ': > "$root/worker-parent-exit"; exit 0' TERM
    : > "$root/worker.started"
    while :; do :; done
    ;;
esac
exit 91
)SH";
  if (!make_executable(ffprobe, ffprobe_script) || !make_executable(ffmpeg, ffmpeg_script) ||
      !make_executable(fake_python, python_script) || !QDir().mkpath(home)) {
    std::cerr << "Could not prepare isolated fake media tools\n";
    return 1;
  }

  const auto previous_path = qEnvironmentVariable("PATH").toUtf8();
  qputenv("PATH", (bin.toUtf8() + ":" + previous_path));
  qputenv("PHOTO_EDITOR_DEPTH_MOTION_HOME", home.toUtf8());
  qputenv("PHOTO_EDITOR_DEPTH_MOTION_PYTHON", fake_python.toUtf8());
  qputenv("DEPTH_MOTION_FAKE_ROOT", root.toUtf8());

  Heartbeat heartbeat(app);
  QJsonArray source_switch_samples;
  bool source_switch_identity_ok = true;
  bool source_switch_children_gone = true;
  qint64 source_switch_max_heartbeat_ns = 0;
  for (int i = 0; i < 3; ++i) {
    const auto held = QDir(root).filePath(QStringLiteral("held-%1.mp4").arg(i));
    const auto intermediate = QDir(root).filePath(QStringLiteral("intermediate-%1.mp4").arg(i));
    const auto fast = QDir(root).filePath(QStringLiteral("fast-%1.mp4").arg(i));
    if (!write_file(held, QByteArrayLiteral("held")) || !write_file(intermediate, QByteArrayLiteral("intermediate")) ||
        !write_file(fast, QByteArrayLiteral("fast"))) {
      std::cerr << "Could not create source-switch media witnesses\n";
      return 1;
    }
    const auto held_pid_path = QDir(root).filePath("held-probe.pid");
    const auto intermediate_pid_path = QDir(root).filePath("intermediate-probe.pid");
    QFile::remove(held_pid_path);
    QFile::remove(intermediate_pid_path);
    QFile::remove(QDir(root).filePath("held-probe.started"));
    QFile::remove(QDir(root).filePath("intermediate-probe.started"));
    DepthMotionController controller;
    controller.inspectSource(held);
    if (!wait_until([&] { return QFileInfo::exists(QDir(root).filePath("held-probe.started")); }, 2000, app)) {
      std::cerr << "Fake ffprobe did not reach the held state\n";
      return 1;
    }
    heartbeat.reset_window();
    QElapsedTimer switch_timer;
    switch_timer.start();
    controller.inspectSource(intermediate);
    controller.inspectSource(fast);
    const auto switch_us = switch_timer.nsecsElapsed() / 1000;
    source_switch_samples.append(static_cast<double>(switch_us));
    if (!wait_until([&] {
          const auto info = controller.snapshot().value("sourceInfo").toMap();
          return !info.value("busy").toBool() && info.value("path").toString() == fast;
        }, 3000, app)) {
      source_switch_identity_ok = false;
    }
    const auto info = controller.snapshot().value("sourceInfo").toMap();
    source_switch_identity_ok = source_switch_identity_ok && info.value("path").toString() == fast &&
        std::abs(info.value("duration").toDouble() - 8.5) < .001;
    source_switch_max_heartbeat_ns = std::max(source_switch_max_heartbeat_ns, heartbeat.maximum_gap_ns());
    const auto held_pid = read_pid(held_pid_path);
    const auto intermediate_pid = read_pid(intermediate_pid_path);
    source_switch_children_gone = source_switch_children_gone && !process_exists(held_pid) &&
        !process_exists(intermediate_pid);
  }

  // Reinspection of the same path exposes the 10-second timeout's path-only
  // identity check: the first request's timer can kill the newer request.
  const auto same_path = QDir(root).filePath("same-path.mp4");
  if (!write_file(same_path, QByteArrayLiteral("same"))) {
    std::cerr << "Could not create the same-path timeout witness\n";
    return 1;
  }
  DepthMotionController same_path_controller;
  QElapsedTimer stale_timer_clock;
  stale_timer_clock.start();
  same_path_controller.inspectSource(same_path);
  if (!wait_until([&] { return QFileInfo::exists(QDir(root).filePath("same-probe-1.started")); }, 2000, app)) {
    std::cerr << "First same-path probe did not reach the held state\n";
    return 1;
  }
  if (!wait_until([&] { return stale_timer_clock.elapsed() >= 8500; }, 9000, app)) {
    std::cerr << "Could not reach the same-path timeout window\n";
    return 1;
  }
  QElapsedTimer same_path_switch_clock;
  same_path_switch_clock.start();
  same_path_controller.inspectSource(same_path);
  const auto same_path_switch_us = same_path_switch_clock.nsecsElapsed() / 1000;
  if (!wait_until([&] { return QFileInfo::exists(QDir(root).filePath("same-probe-2.started")); }, 2000, app)) {
    std::cerr << "Second same-path probe did not start\n";
    return 1;
  }
  if (!wait_until([&] { return stale_timer_clock.elapsed() >= 10300; }, 3000, app)) {
    std::cerr << "Could not observe the first request's timeout\n";
    return 1;
  }
  app.processEvents(QEventLoop::AllEvents, 20);
  const auto same_path_info = same_path_controller.snapshot().value("sourceInfo").toMap();
  const bool second_same_path_survived_old_timer = same_path_info.value("busy").toBool();
  const auto same_path_timeout_observed_ms = stale_timer_clock.elapsed();
  const auto current_probe_pid = read_pid(QDir(root).filePath("same-probe-2.pid"));
  const auto release_path = QDir(root).filePath("fast-release.mp4");
  write_file(release_path, QByteArrayLiteral("release"));
  same_path_controller.inspectSource(release_path);
  const bool release_probe_completed = wait_until([&] {
    const auto info = same_path_controller.snapshot().value("sourceInfo").toMap();
    return !info.value("busy").toBool() && info.value("path").toString().endsWith("fast-release.mp4");
  }, 2000, app);
  const bool second_probe_pid_gone = !process_exists(current_probe_pid);

  const auto input_source = QDir(root).filePath("input.png");
  const auto input_depth = QDir(root).filePath("depth.png");
  if (!write_file(input_source, QByteArrayLiteral("source")) || !write_file(input_depth, QByteArrayLiteral("depth"))) {
    std::cerr << "Could not create generation inputs\n";
    return 1;
  }
  QVariantMap settings;
  QJsonObject cancellation_result;
  write_file(QDir(root).filePath("worker-mode"), "responsive");
  QFile::remove(QDir(root).filePath("worker.started"));
  QFile::remove(QDir(root).filePath("worker.exit"));
  qint64 cancellation_call_us = -1;
  qint64 cancellation_exit_observed_us = -1;
  qint64 cancellation_idle_us = -1;
  qint64 replacement_cancel_call_us = -1;
  qint64 responsive_worker_pid = -1;
  qint64 responsive_worker_child_pid = -1;
  qint64 replacement_worker_pid = -1;
  bool replacement_job_survived_old_cancel_deadline = false;
  bool cancellation_reached_idle = false;
  {
    DepthMotionController controller;
    if (!controller.generate(input_source, input_depth, settings)) {
      std::cerr << "Controller rejected the isolated fake-worker request: "
                << controller.snapshot().value("error").toString().toStdString() << '\n';
      return 1;
    }
    if (!wait_until([&] { return QFileInfo::exists(QDir(root).filePath("worker.started")); }, 3000, app)) {
      std::cerr << "Fake depth worker did not start\n";
      return 1;
    }
    responsive_worker_pid = read_pid(QDir(root).filePath("worker.pid"));
    responsive_worker_child_pid = read_pid(QDir(root).filePath("worker-child.pid"));
    QElapsedTimer cancel_clock;
    cancel_clock.start();
    controller.cancel();
    cancellation_call_us = cancel_clock.nsecsElapsed() / 1000;
    if (wait_until([&] { return QFileInfo::exists(QDir(root).filePath("worker.exit")); }, 2000, app))
      cancellation_exit_observed_us = cancel_clock.nsecsElapsed() / 1000;
    if (wait_until([&] { return !controller.snapshot().value("busy").toBool(); }, 2000, app))
      cancellation_idle_us = cancel_clock.nsecsElapsed() / 1000;
    cancellation_reached_idle = !controller.snapshot().value("busy").toBool();

    if (!wait_until([&] { return cancel_clock.elapsed() >= 4500; }, 5000, app)) {
      std::cerr << "Could not reach the prior cancellation deadline window\n";
      return 1;
    }
    write_file(QDir(root).filePath("worker-mode"), QByteArrayLiteral("held"));
    QFile::remove(QDir(root).filePath("worker.started"));
    QFile::remove(QDir(root).filePath("worker.pid"));
    QFile::remove(QDir(root).filePath("worker-child.pid"));
    if (!controller.generate(input_source, input_depth, settings) ||
        !wait_until([&] { return QFileInfo::exists(QDir(root).filePath("worker.started")); }, 2000, app)) {
      std::cerr << "Replacement held worker did not start before the old cancel deadline\n";
      return 1;
    }
    replacement_worker_pid = read_pid(QDir(root).filePath("worker.pid"));
    QElapsedTimer replacement_cancel_clock;
    replacement_cancel_clock.start();
    controller.cancel();
    replacement_cancel_call_us = replacement_cancel_clock.nsecsElapsed() / 1000;
    if (!wait_until([&] { return cancel_clock.elapsed() >= 5600; }, 2500, app)) {
      std::cerr << "Could not pass the prior cancellation deadline\n";
      return 1;
    }
    replacement_job_survived_old_cancel_deadline = controller.snapshot().value("busy").toBool() &&
        process_exists(replacement_worker_pid) && replacement_cancel_clock.elapsed() < 2000;
  }
  cancellation_result.insert("cancelCallUs", static_cast<double>(cancellation_call_us));
  cancellation_result.insert("cancelToChildExitObservedUs", static_cast<double>(cancellation_exit_observed_us));
  cancellation_result.insert("cancelToIdleUs", static_cast<double>(cancellation_idle_us));
  cancellation_result.insert("exitWitness", QFileInfo::exists(QDir(root).filePath("worker.exit")));
  cancellation_result.insert("pidGoneAfterControllerScope", !process_exists(responsive_worker_pid));
  cancellation_result.insert("responsiveChildPidGone", !process_exists(responsive_worker_child_pid));
  cancellation_result.insert("busyCleared", cancellation_reached_idle);
  cancellation_result.insert("replacementCancelCallUs", static_cast<double>(replacement_cancel_call_us));
  cancellation_result.insert("replacementWorkerPid", static_cast<double>(replacement_worker_pid));
  cancellation_result.insert("replacementJobSurvivedOldCancelDeadline", replacement_job_survived_old_cancel_deadline);

  QElapsedTimer destructor_clock;
  qint64 stubborn_worker_pid = -1;
  qint64 destructor_us = -1;
  qint64 destructor_to_child_exit_us = -1;
  bool destructor_child_gone_immediately = false;
  qint64 stubborn_worker_child_pid = -1;
  bool destructor_child_exited_within_bound = false;
  bool forced_orphan_cleanup = false;
  bool scratch_retained_while_child_alive = false;
  int app_owned_processes_after_controller_scope = 0;
  int app_owned_running_processes_after_controller_scope = 0;
  write_file(QDir(root).filePath("worker-mode"), "stubborn");
  QFile::remove(QDir(root).filePath("worker.started"));
  QFile::remove(QDir(root).filePath("worker-child.pid"));
  {
    auto controller = std::make_unique<DepthMotionController>();
    if (!controller->generate(input_source, input_depth, settings)) {
      std::cerr << "Controller rejected the stubborn teardown fixture\n";
      return 1;
    }
    if (!wait_until([&] { return QFileInfo::exists(QDir(root).filePath("worker.started")); }, 3000, app)) {
      std::cerr << "Stubborn fake depth worker did not start\n";
      return 1;
    }
    stubborn_worker_pid = read_pid(QDir(root).filePath("worker.pid"));
    if (!wait_until([&] { return QFileInfo::exists(QDir(root).filePath("worker-child.pid")); }, 1000, app)) {
      std::cerr << "Stubborn fake depth worker did not spawn its child witness\n";
      return 1;
    }
    stubborn_worker_child_pid = read_pid(QDir(root).filePath("worker-child.pid"));
    heartbeat.reset_window();
    destructor_clock.start();
    controller.reset();
    destructor_us = destructor_clock.nsecsElapsed() / 1000;
    destructor_child_gone_immediately = !process_exists(stubborn_worker_pid) &&
        !process_exists(stubborn_worker_child_pid);
    app_owned_processes_after_controller_scope = app_owned_process_count(app, false);
    app_owned_running_processes_after_controller_scope = app_owned_process_count(app, true);
    scratch_retained_while_child_alive = !destructor_child_gone_immediately && worker_scratch_files_exist(root);
  }
  destructor_child_exited_within_bound = wait_until([&] {
    return !process_exists(stubborn_worker_pid) && !process_exists(stubborn_worker_child_pid);
  }, 6000, app);
  destructor_to_child_exit_us = destructor_clock.nsecsElapsed() / 1000;
  if (!destructor_child_exited_within_bound && stubborn_worker_pid > 0) {
    ::kill(static_cast<pid_t>(stubborn_worker_pid), SIGKILL);
    if (stubborn_worker_child_pid > 0)
      ::kill(static_cast<pid_t>(stubborn_worker_child_pid), SIGKILL);
    forced_orphan_cleanup = wait_until([&] {
      return !process_exists(stubborn_worker_pid) && !process_exists(stubborn_worker_child_pid);
    }, 1000, app);
  }
  const auto destructor_heartbeat_gap_ns = heartbeat.maximum_gap_ns();

  // Verify that a worker child which outlives its parent keeps its temporary
  // files until the child exits. The child waits for an explicit release so the
  // immediate post-destruction sample is not dependent on poll timing.
  QFile::remove(QDir(root).filePath("worker.started"));
  QFile::remove(QDir(root).filePath("worker-child.pid"));
  QFile::remove(QDir(root).filePath("worker-parent-exit"));
  QFile::remove(QDir(root).filePath("worker-child-ready"));
  QFile::remove(QDir(root).filePath("worker-child-release"));
  QFile::remove(QDir(root).filePath("worker-child-exit"));
  write_file(QDir(root).filePath("worker-mode"), QByteArrayLiteral("parent-exits"));
  qint64 parent_exit_worker_pid = -1;
  qint64 parent_exit_child_pid = -1;
  bool parent_exit_marker_seen = false;
  bool parent_exit_child_marker_seen = false;
  bool parent_exit_child_alive_after_reset = false;
  bool parent_exit_scratch_retained_while_child_alive = false;
  bool parent_exit_children_gone = false;
  bool parent_exit_forced_cleanup = false;
  {
    auto controller = std::make_unique<DepthMotionController>();
    if (!controller->generate(input_source, input_depth, settings)) {
      std::cerr << "Controller rejected the parent-exits fixture\n";
      return 1;
    }
    if (!wait_until([&] { return QFileInfo::exists(QDir(root).filePath("worker.started")) &&
                                  QFileInfo::exists(QDir(root).filePath("worker-child-ready")); }, 3000, app)) {
      std::cerr << "Parent-exits fake worker and ready child did not start\n";
      return 1;
    }
    parent_exit_worker_pid = read_pid(QDir(root).filePath("worker.pid"));
    parent_exit_child_pid = read_pid(QDir(root).filePath("worker-child.pid"));
    controller.reset();
  }
  parent_exit_child_alive_after_reset = process_exists(parent_exit_child_pid);
  parent_exit_scratch_retained_while_child_alive = !parent_exit_child_alive_after_reset ||
      worker_scratch_files_exist(root);
  if (!write_file(QDir(root).filePath("worker-child-release"), QByteArrayLiteral("release"))) {
    std::cerr << "Could not release the parent-exits child witness\n";
    return 1;
  }
  parent_exit_marker_seen = wait_until(
      [&] { return QFileInfo::exists(QDir(root).filePath("worker-parent-exit")); }, 2000, app);
  parent_exit_child_marker_seen = wait_until(
      [&] { return QFileInfo::exists(QDir(root).filePath("worker-child-exit")); }, 2000, app);
  parent_exit_children_gone = wait_until([&] {
    return !process_exists(parent_exit_worker_pid) && !process_exists(parent_exit_child_pid);
  }, 2000, app);
  if (!parent_exit_children_gone) {
    if (parent_exit_worker_pid > 0) ::kill(static_cast<pid_t>(parent_exit_worker_pid), SIGKILL);
    if (parent_exit_child_pid > 0) ::kill(static_cast<pid_t>(parent_exit_child_pid), SIGKILL);
    parent_exit_forced_cleanup = wait_until([&] {
      return !process_exists(parent_exit_worker_pid) && !process_exists(parent_exit_child_pid);
    }, 1000, app);
  }

  // Now test application shutdown with both an active worker tree and held
  // source probe. Sample ownership and scratch immediately after reset; do not
  // wait through the reaper's transient state before entering aboutToQuit.
  QFile::remove(QDir(root).filePath("worker.started"));
  QFile::remove(QDir(root).filePath("held-probe.started"));
  QFile::remove(QDir(root).filePath("held-probe.pid"));
  QFile::remove(QDir(root).filePath("worker-child.pid"));
  write_file(QDir(root).filePath("worker-mode"), QByteArrayLiteral("stubborn"));
  qint64 shutdown_worker_pid = -1;
  qint64 shutdown_worker_child_pid = -1;
  qint64 shutdown_probe_pid = -1;
  bool shutdown_worker_alive_before_quit = false;
  bool shutdown_probe_alive_before_quit = false;
  bool shutdown_scratch_retained_before_quit = false;
  int shutdown_app_owned_running_processes = 0;
  int shutdown_app_owned_processes = 0;
  bool shutdown_witness_seen = false;
  bool shutdown_child_gone_at_about_to_quit = false;
  bool shutdown_child_gone_after_exec = false;
  {
    auto controller = std::make_unique<DepthMotionController>();
    const auto shutdown_probe_path = QDir(root).filePath("held-shutdown.mp4");
    if (!write_file(shutdown_probe_path, QByteArrayLiteral("shutdown probe"))) {
      std::cerr << "Could not create the app-shutdown probe witness\n";
      return 1;
    }
    controller->inspectSource(shutdown_probe_path);
    if (!wait_until([&] { return QFileInfo::exists(QDir(root).filePath("held-probe.started")); }, 3000, app)) {
      std::cerr << "Shutdown fake ffprobe did not start\n";
      return 1;
    }
    if (!controller->generate(input_source, input_depth, settings)) {
      std::cerr << "Controller rejected the app-shutdown fixture\n";
      return 1;
    }
    if (!wait_until([&] { return QFileInfo::exists(QDir(root).filePath("worker.started")) &&
                                  QFileInfo::exists(QDir(root).filePath("worker-child.pid")); }, 3000, app)) {
      std::cerr << "Shutdown fake depth worker did not start its child\n";
      return 1;
    }
    shutdown_worker_pid = read_pid(QDir(root).filePath("worker.pid"));
    shutdown_worker_child_pid = read_pid(QDir(root).filePath("worker-child.pid"));
    shutdown_probe_pid = read_pid(QDir(root).filePath("held-probe.pid"));
    controller.reset();
    shutdown_worker_alive_before_quit = process_exists(shutdown_worker_pid) ||
        process_exists(shutdown_worker_child_pid);
    shutdown_probe_alive_before_quit = process_exists(shutdown_probe_pid);
    shutdown_scratch_retained_before_quit = worker_scratch_files_exist(root);
    shutdown_app_owned_processes = app_owned_process_count(app, false);
    shutdown_app_owned_running_processes = app_owned_process_count(app, true);
  }
  QObject::connect(&app, &QCoreApplication::aboutToQuit, &app, [&] {
    shutdown_witness_seen = true;
    shutdown_child_gone_at_about_to_quit = !process_exists(shutdown_worker_pid) &&
        !process_exists(shutdown_worker_child_pid) && !process_exists(shutdown_probe_pid);
  });
  QTimer::singleShot(0, &app, &QCoreApplication::quit);
  const auto event_loop_result = app.exec();
  shutdown_child_gone_after_exec = !process_exists(shutdown_worker_pid) &&
      !process_exists(shutdown_worker_child_pid) && !process_exists(shutdown_probe_pid);
  bool forced_shutdown_cleanup = false;
  if (!shutdown_child_gone_after_exec && (shutdown_worker_pid > 0 || shutdown_probe_pid > 0)) {
    for (auto* process : app.findChildren<QProcess*>()) {
      if (process->processId() == static_cast<qint64>(shutdown_worker_pid) ||
          process->processId() == static_cast<qint64>(shutdown_probe_pid)) {
        process->kill();
        process->waitForFinished(1000);
      }
    }
    if (process_exists(shutdown_worker_pid)) ::kill(static_cast<pid_t>(shutdown_worker_pid), SIGKILL);
    if (process_exists(shutdown_worker_child_pid))
      ::kill(static_cast<pid_t>(shutdown_worker_child_pid), SIGKILL);
    if (process_exists(shutdown_probe_pid)) ::kill(static_cast<pid_t>(shutdown_probe_pid), SIGKILL);
    forced_shutdown_cleanup = wait_until(
        [&] { return !process_exists(shutdown_worker_pid) && !process_exists(shutdown_worker_child_pid) &&
                     !process_exists(shutdown_probe_pid); }, 1000, app);
  }
  const int shutdown_live_children = static_cast<int>(shutdown_worker_alive_before_quit) +
      static_cast<int>(shutdown_probe_alive_before_quit);
  const bool shutdown_ownership_safe = shutdown_live_children == 0 ||
      (shutdown_app_owned_running_processes >= shutdown_live_children &&
       (!shutdown_worker_alive_before_quit || shutdown_scratch_retained_before_quit));

  QJsonObject parent_exit_result;
  parent_exit_result.insert("parentPid", static_cast<double>(parent_exit_worker_pid));
  parent_exit_result.insert("childPid", static_cast<double>(parent_exit_child_pid));
  parent_exit_result.insert("parentExitMarkerSeen", parent_exit_marker_seen);
  parent_exit_result.insert("childExitMarkerSeen", parent_exit_child_marker_seen);
  parent_exit_result.insert("childAliveAtControllerReturn", parent_exit_child_alive_after_reset);
  parent_exit_result.insert("scratchRetainedWhileChildAlive", parent_exit_scratch_retained_while_child_alive);
  parent_exit_result.insert("allChildrenGone", parent_exit_children_gone);
  parent_exit_result.insert("forcedCleanup", parent_exit_forced_cleanup);
  QJsonObject source_switch;
  source_switch.insert("replacementBurstCallUs", source_switch_samples);
  source_switch.insert("maximumHeartbeatGapUs", static_cast<double>(source_switch_max_heartbeat_ns / 1000));
  source_switch.insert("currentPathAndResultWon", source_switch_identity_ok);
  source_switch.insert("killedProbePidsGone", source_switch_children_gone);
  QJsonObject same_path_result;
  same_path_result.insert("switchCallUs", static_cast<double>(same_path_switch_us));
  same_path_result.insert("secondProbeStillBusyAfterFirstRequestTimeout", second_same_path_survived_old_timer);
  same_path_result.insert("secondProbePidGoneAfterExplicitRelease", second_probe_pid_gone);
  same_path_result.insert("releaseProbeCompleted", release_probe_completed);
  same_path_result.insert("timeoutObservedAtMs", static_cast<double>(same_path_timeout_observed_ms));
  QJsonObject destructor_result;
  destructor_result.insert("destructorCallUs", static_cast<double>(destructor_us));
  destructor_result.insert("destructorToChildExitObservedUs", static_cast<double>(destructor_to_child_exit_us));
  destructor_result.insert("maximumHeartbeatGapUs", static_cast<double>(destructor_heartbeat_gap_ns / 1000));
  destructor_result.insert("childGoneImmediately", destructor_child_gone_immediately);
  destructor_result.insert("workerPid", static_cast<double>(stubborn_worker_pid));
  destructor_result.insert("workerChildPid", static_cast<double>(stubborn_worker_child_pid));
  destructor_result.insert("childExitedWithinBound", destructor_child_exited_within_bound);
  destructor_result.insert("forcedOrphanCleanup", forced_orphan_cleanup);
  destructor_result.insert("scratchRetainedWhileChildAlive", scratch_retained_while_child_alive);
  destructor_result.insert("appOwnedProcessCountAfterControllerScope", app_owned_processes_after_controller_scope);
  destructor_result.insert("appOwnedRunningProcessCountAfterControllerScope", app_owned_running_processes_after_controller_scope);
  QJsonObject app_shutdown;
  app_shutdown.insert("workerAliveBeforeQuit", shutdown_worker_alive_before_quit);
  app_shutdown.insert("workerPid", static_cast<double>(shutdown_worker_pid));
  app_shutdown.insert("workerChildPid", static_cast<double>(shutdown_worker_child_pid));
  app_shutdown.insert("probeAliveBeforeQuit", shutdown_probe_alive_before_quit);
  app_shutdown.insert("appOwnedProcessCount", shutdown_app_owned_processes);
  app_shutdown.insert("appOwnedRunningProcessCount", shutdown_app_owned_running_processes);
  app_shutdown.insert("scratchRetainedBeforeQuit", shutdown_scratch_retained_before_quit);
  app_shutdown.insert("aboutToQuitWitnessSeen", shutdown_witness_seen);
  app_shutdown.insert("childGoneAtAboutToQuit", shutdown_child_gone_at_about_to_quit);
  app_shutdown.insert("childGoneAfterExec", shutdown_child_gone_after_exec);
  app_shutdown.insert("forcedCleanup", forced_shutdown_cleanup);
  app_shutdown.insert("ownershipSafe", shutdown_ownership_safe);
  QJsonObject result;
  result.insert("fixture", "depth_motion_controller_lifecycle");
  result.insert("windowless", true);
  result.insert("sourceSwitch", source_switch);
  result.insert("samePathTimeoutGeneration", same_path_result);
  result.insert("responsiveCancel", cancellation_result);
  result.insert("stubbornWorkerDestruction", destructor_result);
  result.insert("parentExitsFirst", parent_exit_result);
  result.insert("applicationShutdown", app_shutdown);
  result.insert("peakRssBytes", static_cast<double>(peak_rss_bytes()));
  result.insert("applicationExecResult", event_loop_result);
  const auto json = QJsonDocument(result).toJson(QJsonDocument::Compact) + '\n';
  const auto output_path = qEnvironmentVariable("DEPTH_MOTION_LIFECYCLE_OUTPUT");
  if (!output_path.isEmpty() && !write_file(output_path, json)) {
    std::cerr << "Could not write fixture JSON output\n";
    return 1;
  }
  std::cout << json.constData();

  if (!source_switch_identity_ok || !source_switch_children_gone || !second_same_path_survived_old_timer ||
      !cancellation_reached_idle || cancellation_exit_observed_us < 0 ||
      !QFileInfo::exists(QDir(root).filePath("worker.exit")) || process_exists(responsive_worker_pid) ||
      process_exists(responsive_worker_child_pid) || !replacement_job_survived_old_cancel_deadline ||
      !second_probe_pid_gone || !release_probe_completed || !destructor_child_exited_within_bound ||
      forced_orphan_cleanup || !parent_exit_marker_seen || !parent_exit_child_marker_seen ||
      !parent_exit_scratch_retained_while_child_alive || !parent_exit_children_gone || parent_exit_forced_cleanup ||
      !shutdown_worker_alive_before_quit || !shutdown_probe_alive_before_quit ||
      !shutdown_scratch_retained_before_quit || !shutdown_witness_seen || !shutdown_child_gone_at_about_to_quit ||
      !shutdown_child_gone_after_exec || forced_shutdown_cleanup || !shutdown_ownership_safe || event_loop_result != 0) {
    std::cerr << "Depth Motion lifecycle invariant failed; see the JSON metrics above\n";
    return 2;
  }
  return 0;
}
