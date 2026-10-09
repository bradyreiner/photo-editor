#include "agent_execution_admission.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QStringList>
#include <QThread>
#include <QVariantList>

#include <functional>
#include <iostream>

using photo_editor::desktop::AgentExecutionAdmission;

namespace {

bool check(bool condition, const char* message) {
  if (!condition) std::cerr << message << '\n';
  return condition;
}

bool wait_until(const std::function<bool()>& predicate, int timeout_ms = 1500) {
  QElapsedTimer timer;
  timer.start();
  while (!predicate() && timer.elapsed() < timeout_ms) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    QThread::msleep(1);
  }
  QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
  return predicate();
}

QVariantMap approved_snapshot(QString task_id, QString revision) {
  return {{QStringLiteral("contract"), QStringLiteral("agent-approved-session-v1")},
      {QStringLiteral("taskId"), std::move(task_id)},
      {QStringLiteral("targetIds"), QVariantList{QStringLiteral("asset-a")}},
      {QStringLiteral("targetSnapshot"),
          QVariantMap{{QStringLiteral("asset-a"), std::move(revision)}}}};
}

bool pending_admission_waits_then_starts_once() {
  bool settled = false;
  int readiness_checks = 0;
  int validations = 0;
  int starts = 0;
  int failures = 0;
  QVariantMap started_snapshot;
  bool readiness_received_pinned_targets = true;
  QStringList activities;
  const auto approved = approved_snapshot(QStringLiteral("task-a"), QStringLiteral("sha-a"));

  AgentExecutionAdmission admission(
      [&](const QVariantMap& snapshot) {
        ++readiness_checks;
        readiness_received_pinned_targets = readiness_received_pinned_targets && snapshot == approved;
        return QVariantMap{{QStringLiteral("ready"), settled},
            {QStringLiteral("pending"), !settled}};
      },
      [&](const QVariantMap& snapshot) {
        ++validations;
        return QVariantMap{{QStringLiteral("ok"), snapshot == approved}};
      },
      [&](const QVariantMap& snapshot) {
        ++starts;
        started_snapshot = snapshot;
      },
      [&](const QVariantMap&) { ++failures; },
      [&](const QString& message) { activities.append(message); });

  const auto submitted = admission.submit(approved);
  if (!check(submitted.value(QStringLiteral("ok")).toBool() &&
                 submitted.value(QStringLiteral("pending")).toBool(),
          "A valid approved snapshot should enter pending admission.")) return false;
  if (!check(wait_until([&] { return readiness_checks >= 2; }),
          "Pending readiness should be polled asynchronously.")) return false;
  if (!check(starts == 0 && validations == 0 && admission.pending(),
          "The task must not validate or start while photo saves remain pending.")) return false;
  if (!check(!activities.isEmpty(), "Waiting should report factual activity.")) return false;

  settled = true;
  if (!check(wait_until([&] { return starts == 1; }),
          "A settled readiness state should dispatch the task.")) return false;
  QCoreApplication::processEvents(QEventLoop::AllEvents, 60);
  return check(starts == 1 && validations == 1 && failures == 0 &&
                   readiness_received_pinned_targets &&
                   started_snapshot == approved && !admission.pending(),
      "The exact approved snapshot should be validated and started exactly once.");
}

bool changed_snapshot_is_rejected_after_wait() {
  bool settled = false;
  QString current_revision = QStringLiteral("sha-original");
  int starts = 0;
  int failures = 0;
  QVariantMap failure_report;
  const auto approved = approved_snapshot(QStringLiteral("task-b"), QStringLiteral("sha-original"));

  AgentExecutionAdmission admission(
      [&](const QVariantMap&) { return QVariantMap{{QStringLiteral("ready"), settled},
          {QStringLiteral("pending"), !settled}}; },
      [&](const QVariantMap& snapshot) {
        const auto pinned = snapshot.value(QStringLiteral("targetSnapshot")).toMap()
                               .value(QStringLiteral("asset-a")).toString();
        return QVariantMap{{QStringLiteral("ok"), pinned == current_revision},
            {QStringLiteral("error"), QStringLiteral("The approved target changed while saves were settling.")}};
      },
      [&](const QVariantMap&) { ++starts; },
      [&](const QVariantMap& report) { ++failures; failure_report = report; });

  static_cast<void>(admission.submit(approved));
  if (!check(wait_until([&] { return admission.pending(); }),
          "The admission should remain pending before settlement.")) return false;
  current_revision = QStringLiteral("sha-newer");
  settled = true;
  if (!check(wait_until([&] { return failures == 1; }),
          "The changed target should be reported after readiness settles.")) return false;
  return check(starts == 0 && !admission.pending() &&
                   failure_report.value(QStringLiteral("phase")).toString() == QStringLiteral("snapshot") &&
                   failure_report.value(QStringLiteral("error")).toString().contains(QStringLiteral("target changed")),
      "A target revision change must fail closed without dispatching the approved snapshot.");
}

bool duplicate_submit_preserves_the_original_approved_snapshot() {
  bool settled = false;
  int validations = 0;
  int starts = 0;
  QVariantMap started_snapshot;
  const auto first = approved_snapshot(QStringLiteral("task-first"), QStringLiteral("sha-first"));
  const auto replacement = approved_snapshot(QStringLiteral("task-replacement"), QStringLiteral("sha-replacement"));

  AgentExecutionAdmission admission(
      [&](const QVariantMap&) { return QVariantMap{{QStringLiteral("ready"), settled},
          {QStringLiteral("pending"), !settled}}; },
      [&](const QVariantMap&) { ++validations; return QVariantMap{{QStringLiteral("ok"), true}}; },
      [&](const QVariantMap& snapshot) { ++starts; started_snapshot = snapshot; },
      [](const QVariantMap&) {});

  const auto submitted = admission.submit(first);
  if (!check(submitted.value(QStringLiteral("ok")).toBool(),
          "The first snapshot should be admitted.")) return false;
  const auto repeated = admission.submit(replacement);
  if (!check(!repeated.value(QStringLiteral("ok")).toBool() &&
                 repeated.value(QStringLiteral("pending")).toBool(),
          "A repeated submit must be rejected without replacing pending approval.")) return false;
  if (!check(wait_until([&] { return admission.pending(); }),
          "The original admission should still be pending.")) return false;
  settled = true;
  if (!check(wait_until([&] { return starts == 1; }),
          "Settling the original admission should start one task.")) return false;
  return check(!admission.pending() && validations == 1 && starts == 1 &&
                   started_snapshot == first,
      "A rejected repeated submit must not replace the original approved snapshot.");
}

bool cancel_pending_admission_never_starts() {
  bool settled = false;
  int validations = 0;
  int starts = 0;
  int failures = 0;
  AgentExecutionAdmission admission(
      [&](const QVariantMap&) { return QVariantMap{{QStringLiteral("ready"), settled},
          {QStringLiteral("pending"), !settled}}; },
      [&](const QVariantMap&) { ++validations; return QVariantMap{{QStringLiteral("ok"), true}}; },
      [&](const QVariantMap&) { ++starts; },
      [&](const QVariantMap&) { ++failures; });

  static_cast<void>(admission.submit(approved_snapshot(QStringLiteral("task-cancel"), QStringLiteral("sha-cancel"))));
  if (!check(wait_until([&] { return admission.pending(); }),
          "The pending admission should be cancelable before readiness.")) return false;
  const auto canceled = admission.cancel();
  if (!check(canceled.value(QStringLiteral("ok")).toBool() &&
                 canceled.value(QStringLiteral("canceled")).toBool() &&
                 canceled.value(QStringLiteral("noEdits")).toBool() &&
                 !canceled.value(QStringLiteral("changed")).toBool(),
          "Cancel should report that pending admission ended before task writes.")) return false;
  settled = true;
  QCoreApplication::processEvents(QEventLoop::AllEvents, 80);
  return check(!admission.pending() && validations == 0 && starts == 0 && failures == 0,
      "Cancel must invalidate queued polls so the approved task cannot start late.");
}

bool cancellation_from_activity_invalidates_the_current_poll() {
  int starts = 0;
  int validations = 0;
  AgentExecutionAdmission* admission_ptr = nullptr;
  AgentExecutionAdmission admission(
      [](const QVariantMap&) { return QVariantMap{{QStringLiteral("ready"), false}, {QStringLiteral("pending"), true}}; },
      [&](const QVariantMap&) { ++validations; return QVariantMap{{QStringLiteral("ok"), true}}; },
      [&](const QVariantMap&) { ++starts; },
      [](const QVariantMap&) {},
      [&](const QString&) { static_cast<void>(admission_ptr->cancel()); });
  admission_ptr = &admission;

  const auto submitted = admission.submit(approved_snapshot(QStringLiteral("task-c"), QStringLiteral("sha-c")));
  if (!check(submitted.value(QStringLiteral("ok")).toBool(), "Submit should be accepted.")) return false;
  if (!check(wait_until([&] { return !admission.pending(); }),
          "A re-entrant activity callback should be able to cancel admission.")) return false;
  QCoreApplication::processEvents(QEventLoop::AllEvents, 80);
  return check(starts == 0 && validations == 0,
      "A canceled readiness callback must not arm another poll or dispatch late.");
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  if (!pending_admission_waits_then_starts_once()) return 1;
  if (!changed_snapshot_is_rejected_after_wait()) return 1;
  if (!duplicate_submit_preserves_the_original_approved_snapshot()) return 1;
  if (!cancel_pending_admission_never_starts()) return 1;
  if (!cancellation_from_activity_invalidates_the_current_poll()) return 1;
  return 0;
}
