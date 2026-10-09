#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QThread>

#include "agent_session_controller.h"

namespace {

int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

bool wait_until(const std::function<bool()>& predicate, int timeout_ms = 3000) {
  QElapsedTimer timer;
  timer.start();
  while (!predicate() && timer.elapsed() < timeout_ms) {
    QCoreApplication::processEvents();
    QThread::msleep(2);
  }
  QCoreApplication::processEvents();
  return predicate();
}

photo_editor::desktop::AgentLocalModelTaskSessionResult ask(
    const QString& id,
    const QString& prompt) {
  using namespace photo_editor::desktop;
  AgentLocalModelTaskSessionResult result;
  result.ok = true;
  result.decision = QStringLiteral("ask_questions");
  result.questions = QVariantList{QVariantMap{
      {QStringLiteral("id"), id},
      {QStringLiteral("prompt"), prompt},
      {QStringLiteral("reason"), QStringLiteral("A required fact is not in the request yet.")},
      {QStringLiteral("required"), true},
      {QStringLiteral("inputType"), QStringLiteral("text")},
      {QStringLiteral("options"), QVariantList{}},
  }};
  return result;
}

photo_editor::desktop::AgentLocalModelTaskSessionResult ready(
    const photo_editor::desktop::AgentLocalModelTaskSessionRequest& request) {
  using namespace photo_editor::desktop;
  AgentLocalModelTaskSessionResult result;
  result.ok = true;
  result.decision = QStringLiteral("plan_ready");
  const auto answers = request.answers;
  result.plan = QVariantMap{
      {QStringLiteral("summary"), QStringLiteral("Create the requested editable design.")},
      {QStringLiteral("outcome"), QStringLiteral("An editable design for the selected target.")},
      {QStringLiteral("actions"), QVariantList{QVariantMap{
          {QStringLiteral("kind"), QStringLiteral("text")},
          {QStringLiteral("summary"), QStringLiteral("Place the approved copy on editable text layers.")},
      }}},
      {QStringLiteral("exactCopy"), QVariantList{QVariantMap{
          {QStringLiteral("text"), answers.value(QStringLiteral("exact_copy"))},
          {QStringLiteral("source"), QStringLiteral("answer")},
      }}},
      {QStringLiteral("deliverables"), QStringList{QStringLiteral("Editable design document")}},
      {QStringLiteral("constraints"), QStringList{QStringLiteral("Preserve the source asset")}},
  };
  return result;
}

bool has_typed_actions(const QVariantMap& plan) {
  const auto actions = plan.value(QStringLiteral("actions")).toList();
  if (actions.isEmpty() || actions.size() > 6) return false;
  for (const auto& value : actions) {
    const auto action = value.toMap();
    if (action.size() != 2 || action.value(QStringLiteral("kind")).toString().isEmpty() ||
        action.value(QStringLiteral("summary")).toString().trimmed().isEmpty())
      return false;
  }
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  using namespace photo_editor::desktop;

  const QVariantMap typed_action_context{
      {QStringLiteral("targetIds"), QStringList{QStringLiteral("photo-typed-actions")}},
      {QStringLiteral("projectId"), QStringLiteral("project-typed-actions")},
      {QStringLiteral("contextReady"), true},
  };
  std::atomic_int action_recovery_calls{0};
  std::atomic_bool action_recovery_feedback_received{false};
  AgentSessionController action_recovery([&](const AgentLocalModelTaskSessionRequest& request) {
    const auto call = ++action_recovery_calls;
    if (call > 1) {
      const auto feedback = request.brief.value(QStringLiteral("planValidationFeedback")).toMap();
      const auto rejected = feedback.value(QStringLiteral("rejectedPlan")).toMap();
      action_recovery_feedback_received =
          feedback.value(QStringLiteral("code")).toString() == QStringLiteral("invalid_typed_actions") &&
          !feedback.value(QStringLiteral("instruction")).toString().isEmpty() &&
          rejected.size() == 2 && rejected.contains(QStringLiteral("summary")) &&
          rejected.contains(QStringLiteral("outcome")) &&
          !rejected.contains(QStringLiteral("exactCopy")) &&
          !rejected.contains(QStringLiteral("actions"));
    }
    auto result = ready(request);
    if (call == 1) result.plan.remove(QStringLiteral("actions"));
    return result;
  });
  action_recovery.setContextSnapshotHandler([](const QVariantMap& context) { return context; });
  QVariantMap recovered_approval;
  QObject::connect(&action_recovery, &AgentSessionController::executionRequested, &app,
      [&recovered_approval](const QVariantMap& snapshot) { recovered_approval = snapshot; });
  action_recovery.submit(QStringLiteral("Create a text design with my supplied wording."),
                         typed_action_context);
  if (!wait_until([&action_recovery]() { return action_recovery.stage() == QStringLiteral("plan-ready"); }) ||
      action_recovery_calls != 2 || !action_recovery_feedback_received ||
      action_recovery.brief().contains(QStringLiteral("planValidationFeedback")) ||
      !action_recovery.canStart() ||
      !has_typed_actions(action_recovery.plan())) {
    return fail("a missing typed-action plan should receive one bounded repair without changing the user's brief");
  }
  action_recovery.start();
  if (recovered_approval.isEmpty() ||
      !has_typed_actions(recovered_approval.value(QStringLiteral("plan")).toMap())) {
    return fail("Start must approve only the corrected plan with explicit typed actions");
  }

  std::atomic_int repeated_missing_action_calls{0};
  std::atomic_int repeated_missing_action_feedback{0};
  AgentSessionController repeated_missing_actions([&](const AgentLocalModelTaskSessionRequest& request) {
    ++repeated_missing_action_calls;
    if (request.brief.contains(QStringLiteral("planValidationFeedback")))
      ++repeated_missing_action_feedback;
    auto result = ready(request);
    result.plan.remove(QStringLiteral("actions"));
    return result;
  });
  repeated_missing_actions.setContextSnapshotHandler([](const QVariantMap& context) { return context; });
  int repeated_missing_action_execution_count = 0;
  QObject::connect(&repeated_missing_actions, &AgentSessionController::executionRequested, &app,
      [&repeated_missing_action_execution_count](const QVariantMap&) {
        ++repeated_missing_action_execution_count;
      });
  repeated_missing_actions.submit(QStringLiteral("Add editable text to the selected photo."),
                                  typed_action_context);
  if (!wait_until([&repeated_missing_actions]() { return repeated_missing_actions.stage() == QStringLiteral("error"); }) ||
      repeated_missing_action_calls != 2 || repeated_missing_action_feedback != 1 ||
      repeated_missing_actions.canStart()) {
    return fail("a second incomplete typed-action plan must fail closed after exactly one correction");
  }
  repeated_missing_actions.start();
  if (repeated_missing_action_execution_count != 0) {
    return fail("a plan without typed actions must never dispatch Start");
  }
  repeated_missing_actions.retryPlanning();
  if (!wait_until([&repeated_missing_actions]() { return repeated_missing_actions.stage() == QStringLiteral("error"); }) ||
      repeated_missing_action_calls != 4 || repeated_missing_action_feedback != 2) {
    return fail("an explicit user retry should receive a fresh single correction allowance");
  }

  std::atomic_int native_validation_failure_calls{0};
  std::atomic_bool native_failure_feedback_received{false};
  AgentSessionController native_validation_recovery([&](const AgentLocalModelTaskSessionRequest& request) {
    if (++native_validation_failure_calls == 1) {
      AgentLocalModelTaskSessionResult result;
      result.ok = false;
      result.decision = QStringLiteral("plan_ready");
      result.error_message = QStringLiteral("The plan needs typed actions.");
      QVariantList rejected_actions;
      for (int index = 0; index < 7; ++index) {
        rejected_actions.append(QVariantMap{
            {QStringLiteral("kind"), QString(100, QLatin1Char('x'))},
            {QStringLiteral("summary"), QString(1000, QLatin1Char('y'))},
        });
      }
      result.prompt_audit.insert(QStringLiteral("planValidationFailure"), QVariantMap{
          {QStringLiteral("code"), QStringLiteral("invalid_typed_actions")},
          {QStringLiteral("instruction"), QStringLiteral("Return explicit typed outcome actions.")},
          {QStringLiteral("candidatePlan"), QVariantMap{
              {QStringLiteral("summary"), QString(1000, QLatin1Char('s'))},
              {QStringLiteral("outcome"), QString(1000, QLatin1Char('o'))},
              {QStringLiteral("actions"), rejected_actions},
              {QStringLiteral("toolCalls"), QVariantList{QStringLiteral("must not be forwarded")}},
          }},
      });
      return result;
    }
    const auto feedback = request.brief.value(QStringLiteral("planValidationFeedback")).toMap();
    const auto rejected = feedback.value(QStringLiteral("rejectedPlan")).toMap();
    const auto rejected_actions = rejected.value(QStringLiteral("actions")).toList();
    const auto rejected_json = QJsonDocument::fromVariant(rejected).toJson(QJsonDocument::Compact);
    native_failure_feedback_received =
        feedback.value(QStringLiteral("code")).toString() == QStringLiteral("invalid_typed_actions") &&
        feedback.value(QStringLiteral("instruction")).toString() ==
            QStringLiteral("Return explicit typed outcome actions.") &&
        rejected.value(QStringLiteral("summary")).toString().size() == 120 &&
        rejected.value(QStringLiteral("outcome")).toString().size() == 200 &&
        rejected_actions.size() == 6 &&
        rejected_actions.first().toMap().value(QStringLiteral("kind")).toString().size() == 24 &&
        rejected_actions.first().toMap().value(QStringLiteral("summary")).toString().size() == 120 &&
        !rejected.contains(QStringLiteral("toolCalls")) && rejected_json.size() < 8192;
    return ready(request);
  });
  native_validation_recovery.setContextSnapshotHandler([](const QVariantMap& context) { return context; });
  native_validation_recovery.submit(QStringLiteral("Create the requested editable design."),
                                    typed_action_context);
  if (!wait_until([&native_validation_recovery]() {
        return native_validation_recovery.stage() == QStringLiteral("plan-ready");
      }) || native_validation_failure_calls != 2 || !native_failure_feedback_received ||
      !native_validation_recovery.canStart()) {
    return fail("only the native typed-action validation failure should enter bounded correction");
  }

  std::atomic_int transport_failure_calls{0};
  AgentSessionController transport_failure([&transport_failure_calls](
      const AgentLocalModelTaskSessionRequest&) {
    ++transport_failure_calls;
    AgentLocalModelTaskSessionResult result;
    result.error_message = QStringLiteral("The local model transport is unavailable.");
    return result;
  });
  transport_failure.submit(QStringLiteral("Create the requested editable design."), typed_action_context);
  if (!wait_until([&transport_failure]() { return transport_failure.stage() == QStringLiteral("error"); }) ||
      transport_failure_calls != 1) {
    return fail("transport errors must remain ordinary errors without typed-action correction retries");
  }

  std::atomic_int planner_calls{0};
  AgentSessionController session([&planner_calls](const AgentLocalModelTaskSessionRequest& request) {
    ++planner_calls;
    if (!request.answers.contains(QStringLiteral("event_date"))) {
      return ask(QStringLiteral("event_date"), QStringLiteral("What date should the design show?"));
    }
    if (!request.answers.contains(QStringLiteral("exact_copy"))) {
      return ask(QStringLiteral("exact_copy"), QStringLiteral("What exact wording should appear?"));
    }
    request.activity_received(QStringLiteral("Planning the approved edit for review."));
    return ready(request);
  });
  QString live_revision = QStringLiteral("asset-rev-1");
  QString live_inspiration_revision = QStringLiteral("inspiration-rev-1");
  QString live_style_revision = QStringLiteral("style-rev-1");
  bool live_context_ready = true;
  session.setContextSnapshotHandler([&live_revision, &live_inspiration_revision,
                                     &live_style_revision, &live_context_ready](const QVariantMap& raw) {
    auto context = raw;
    context.insert(QStringLiteral("assetRevisions"),
                   QVariantMap{{QStringLiteral("photo-1"), live_revision}});
    context.insert(QStringLiteral("inspirationRevisions"),
                   QVariantMap{{QStringLiteral("look-2"), live_inspiration_revision}});
    context.insert(QStringLiteral("styleRevisions"),
                   QVariantMap{{QStringLiteral("saved-style-1"), live_style_revision}});
    context.insert(QStringLiteral("contextReady"), live_context_ready);
    return context;
  });
  int execution_count = 0;
  int activity_change_count = 0;
  QString activity_at_execution_request;
  bool execution_activity_notified_before_dispatch = false;
  QStringList execution_controls;
  QVariantMap revert_owner_state{{QStringLiteral("available"), false}};
  session.setExecutionControlHandler([&execution_controls](const QString& action) {
    execution_controls.append(action);
    return QVariantMap{{"ok", true}, {"pending", true}};
  });
  session.setRevertAvailabilityHandler([&revert_owner_state] { return revert_owner_state; });
  int owner_state_notifications = 0;
  QObject::connect(&session, &AgentSessionController::stateChanged, &app,
      [&owner_state_notifications] { ++owner_state_notifications; });
  const QString execution_start_activity = QStringLiteral("Working from the approved plan.");
  QObject::connect(&session, &AgentSessionController::activityChanged, &app,
      [&session, &activity_change_count, &execution_activity_notified_before_dispatch,
       &execution_start_activity] {
        ++activity_change_count;
        if (session.stage() == QStringLiteral("executing") && session.activity() == execution_start_activity)
          execution_activity_notified_before_dispatch = true;
      });
  if (session.requestStop().value("ok").toBool() || session.requestRevert().value("ok").toBool() ||
      !execution_controls.isEmpty()) return fail("Unapproved conversation cannot dispatch execution controls");
  if (session.canRevert()) return fail("A session must not advertise Revert without owner confirmation");
  revert_owner_state = {{QStringLiteral("available"), true},
                        {QStringLiteral("pending"), false},
                        {QStringLiteral("owner"), QStringLiteral("photo-task")}};
  const auto notifications_before_owner_change = owner_state_notifications;
  session.notifyRevertAvailabilityChanged();
  if (!session.canRevert() || session.revertPending() ||
      session.revertOwner() != QStringLiteral("photo-task") ||
      owner_state_notifications != notifications_before_owner_change + 1)
    return fail("Owner availability changes must notify QML with the actual reversible task owner");
  revert_owner_state.insert(QStringLiteral("pending"), true);
  session.notifyRevertAvailabilityChanged();
  if (!session.canRevert() || !session.revertPending())
    return fail("A recoverable task must remain identified while pending settlement disables Revert");
  revert_owner_state.insert(QStringLiteral("pending"), false);
  session.notifyRevertAvailabilityChanged();
  QVariantMap approved;
  QObject::connect(&session, &AgentSessionController::executionRequested, &app,
      [&session, &execution_count, &approved, &activity_at_execution_request](const QVariantMap& snapshot) {
        ++execution_count;
        approved = snapshot;
        activity_at_execution_request = session.activity();
      });

  const QVariantMap first_context{
      {QStringLiteral("targetIds"), QStringList{QStringLiteral("photo-1")}},
      {QStringLiteral("projectId"), QStringLiteral("project-7")},
      {QStringLiteral("blankCanvas"), false},
      {QStringLiteral("inspirationIds"), QStringList{QStringLiteral("look-2")}},
      {QStringLiteral("selectedStyles"), QStringList{QStringLiteral("saved-style-1")}},
  };
  session.submit(QStringLiteral("Make an event design for my photo"), first_context);
  if (!wait_until([&session]() { return session.stage() == QStringLiteral("clarifying"); })) {
    return fail("ordinary prompt should asynchronously reach the relevant clarification page");
  }
  if (execution_count != 0 || session.questions().size() != 1 ||
      session.questions().first().toMap().value(QStringLiteral("id")).toString() !=
          QStringLiteral("event_date")) {
    return fail("clarification must not request execution or omit the required question");
  }
  const auto call_count_before_missing = planner_calls.load();
  session.answerQuestions({});
  if (session.stage() != QStringLiteral("clarifying") || planner_calls.load() != call_count_before_missing) {
    return fail("a missing required answer must not start another planning request");
  }
  session.answerQuestions(QVariantMap{{QStringLiteral("event_date"), QStringLiteral("October 20")}});
  if (!wait_until([&session]() { return session.stage() == QStringLiteral("clarifying"); }) ||
      session.questions().first().toMap().value(QStringLiteral("id")).toString() !=
          QStringLiteral("exact_copy")) {
    return fail("the second relevant detail should be asked only after the first answer");
  }
  session.answerQuestions(QVariantMap{{QStringLiteral("exact_copy"), QStringLiteral("Autumn Open House")}});
  if (!wait_until([&session]() { return session.stage() == QStringLiteral("plan-ready"); })) {
    return fail("a complete brief should resolve to an approved plan");
  }
  if (!session.canStart() || session.brief().value(QStringLiteral("answers")).toMap()
          .value(QStringLiteral("event_date")).toString() != QStringLiteral("October 20") ||
      session.plan().value(QStringLiteral("targetIds")).toStringList() != QStringList{QStringLiteral("photo-1")}) {
    return fail("the plan must retain answers and the concrete target snapshot");
  }
  const auto first_plan_version = session.plan().value(QStringLiteral("briefVersion")).toULongLong();
  session.reviseAnswers(QVariantMap{
      {QStringLiteral("event_date"), QStringLiteral("October 21")},
      {QStringLiteral("exact_copy"), QStringLiteral("Autumn Open House")},
  });
  if (session.canStart() || !session.busy()) {
    return fail("revising a material answer must invalidate the existing plan immediately");
  }
  if (!wait_until([&session]() { return session.stage() == QStringLiteral("plan-ready"); }) ||
      session.plan().value(QStringLiteral("briefVersion")).toULongLong() == first_plan_version ||
      session.brief().value(QStringLiteral("answers")).toMap()
          .value(QStringLiteral("event_date")).toString() != QStringLiteral("October 21")) {
    return fail("the refreshed plan must bind the revised answer version");
  }

  const QVariantMap changed_target{
      {QStringLiteral("targetIds"), QStringList{QStringLiteral("photo-2")}},
      {QStringLiteral("projectId"), QStringLiteral("project-7")},
      {QStringLiteral("blankCanvas"), false},
      {QStringLiteral("inspirationIds"), QStringList{QStringLiteral("look-2")}},
  };
  session.updateContext(changed_target);
  if (session.canStart() || !session.busy()) {
    return fail("changing the target must invalidate the prior plan immediately");
  }
  if (!wait_until([&session]() { return session.stage() == QStringLiteral("plan-ready"); }) ||
      session.plan().value(QStringLiteral("targetIds")).toStringList() != QStringList{QStringLiteral("photo-2")}) {
    return fail("the refreshed plan must use the newly selected target");
  }

  live_revision = QStringLiteral("asset-rev-2");
  session.start();
  if (execution_count != 0 || !session.busy() || session.stage() != QStringLiteral("planning")) {
    return fail("a fresh target snapshot must invalidate and replan instead of emitting a stale approval");
  }
  if (!wait_until([&session]() { return session.stage() == QStringLiteral("plan-ready"); })) {
    return fail("the changed authoritative target snapshot should produce a replacement plan");
  }
  live_inspiration_revision = QStringLiteral("inspiration-rev-2");
  session.start();
  if (execution_count != 0 || !session.busy() || session.stage() != QStringLiteral("planning") ||
      !wait_until([&session]() { return session.stage() == QStringLiteral("plan-ready"); })) {
    return fail("a changed approved inspiration must invalidate and refresh the plan before Start");
  }
  live_style_revision = QStringLiteral("style-rev-2");
  session.start();
  if (execution_count != 0 || !session.busy() || session.stage() != QStringLiteral("planning") ||
      !wait_until([&session]() { return session.stage() == QStringLiteral("plan-ready"); })) {
    return fail("a changed saved-look revision must invalidate and refresh the plan before Start");
  }
  session.reset();
  if (session.stage() != QStringLiteral("idle") || execution_count != 0) {
    return fail("reset before Start must return to idle without requesting execution");
  }

  // Verify the exact approved payload, revision continuation, and protection
  // against abandoning an active executor-owned task.
  session.submit(QStringLiteral("Make an event design for my photo"), changed_target);
  if (!wait_until([&session]() { return session.stage() == QStringLiteral("clarifying"); })) {
    return fail("a reset session should accept a new prompt");
  }
  session.answerQuestions(QVariantMap{
      {QStringLiteral("event_date"), QStringLiteral("October 20")},
  });
  if (!wait_until([&session]() { return session.stage() == QStringLiteral("clarifying"); })) {
    return fail("new session should ask for exact copy");
  }
  session.answerQuestions(QVariantMap{
      {QStringLiteral("exact_copy"), QStringLiteral("Autumn Open House")},
  });
  if (!wait_until([&session]() { return session.stage() == QStringLiteral("plan-ready"); })) {
    return fail("new session should become ready after all required answers");
  }
  live_revision = QStringLiteral("asset-rev-3");
  session.start();
  if (execution_count != 0 || session.stage() != QStringLiteral("planning")) {
    return fail("a revision changed during Start preflight must be replanned before execution");
  }
  if (!wait_until([&session]() { return session.stage() == QStringLiteral("plan-ready"); })) {
    return fail("the final target version should be included in a replacement plan");
  }
  if (!wait_until([&session]() {
        return session.activity() == QStringLiteral("Planning the approved edit for review.");
      }))
    return fail("the planner should publish its latest activity before Start");
  session.start();
  if (execution_count != 1 || session.stage() != QStringLiteral("executing") ||
      session.activity() != execution_start_activity || activity_at_execution_request != execution_start_activity ||
      !execution_activity_notified_before_dispatch ||
      approved.value(QStringLiteral("contract")).toString() != QStringLiteral("agent-approved-session-v1") ||
      approved.value(QStringLiteral("targetIds")).toStringList() != QStringList{QStringLiteral("photo-2")} ||
      approved.value(QStringLiteral("exactCopy")).toList().isEmpty() ||
      approved.value(QStringLiteral("targetSnapshot")).toMap().value(QStringLiteral("assetRevisions")).toMap()
          .value(QStringLiteral("photo-1")).toString() != QStringLiteral("asset-rev-3") ||
      approved.value(QStringLiteral("targetSnapshot")).toMap().value(QStringLiteral("inspirationRevisions")).toMap()
          .value(QStringLiteral("look-2")).toString() != QStringLiteral("inspiration-rev-2") ||
      approved.value(QStringLiteral("targetSnapshot")).toMap().value(QStringLiteral("styleRevisions")).toMap()
          .value(QStringLiteral("saved-style-1")).toString() != QStringLiteral("style-rev-2")) {
    return fail("Start must emit the immutable prompt, answer, exact copy, and authoritative target snapshot");
  }
  if (!session.validateApprovedExecutionSnapshot(approved).value(QStringLiteral("ok")).toBool())
    return fail("an unchanged approved snapshot should pass execution admission");
  live_revision = QStringLiteral("asset-rev-changed-after-start");
  if (session.validateApprovedExecutionSnapshot(approved).value(QStringLiteral("ok")).toBool())
    return fail("execution admission must reject target revision drift after Start");
  live_revision = QStringLiteral("asset-rev-3");
  live_context_ready = false;
  if (session.validateApprovedExecutionSnapshot(approved).value(QStringLiteral("ok")).toBool())
    return fail("execution admission must reject a target context that is no longer ready");
  live_context_ready = true;
  auto altered_approval = approved;
  auto altered_copy = altered_approval.value(QStringLiteral("exactCopy")).toList();
  auto altered_copy_item = altered_copy.first().toMap();
  altered_copy_item.insert(QStringLiteral("text"), QStringLiteral("Unapproved copy"));
  altered_copy[0] = altered_copy_item;
  altered_approval.insert(QStringLiteral("exactCopy"), altered_copy);
  if (session.validateApprovedExecutionSnapshot(altered_approval).value(QStringLiteral("ok")).toBool() ||
      !session.validateApprovedExecutionSnapshot(approved).value(QStringLiteral("ok")).toBool())
    return fail("execution admission must bind the exact copy while retaining the unchanged approval");
  const int activity_changes_before_step = activity_change_count;
  const QString execution_step = QStringLiteral("Applying the approved adjustments to photo-2.");
  session.reportExecutionActivity(execution_step);
  if (session.activity() != execution_step || activity_change_count != activity_changes_before_step + 1)
    return fail("execution activity should replace the Start status and notify the live activity surface");
  session.reset();
  session.submit(QStringLiteral("Do something else"), changed_target);
  if (session.stage() != QStringLiteral("executing") || execution_count != 1) {
    return fail("reset or submit must not abandon an executor-owned active task");
  }
  if (!session.requestStop().value("ok").toBool() || session.stage() != QStringLiteral("executing") ||
      !session.requestRevert().value("ok").toBool() || session.stage() != QStringLiteral("executing") ||
      !session.revertPending() ||
      execution_controls != QStringList({"stop", "revert"})) {
    return fail("Stop and Revert route to the task owner but wait for its settled receipt before changing stage");
  }
  session.acknowledgeExecution(QVariantMap{
      {QStringLiteral("ok"), true},
      {QStringLiteral("taskId"), QStringLiteral("task-77")},
      {QStringLiteral("headline"), QStringLiteral("The initial editable design is ready for review.")},
      {QStringLiteral("toolCalls"), QVariantList(79, QVariantMap{{QStringLiteral("name"), QStringLiteral("photo.edit_composition")}})},
      {QStringLiteral("taskTransaction"), QVariantMap{
          {QStringLiteral("status"), QStringLiteral("review")},
          {QStringLiteral("taskId"), QStringLiteral("task-77")},
          {QStringLiteral("resources"), QVariantList{QVariantMap{
              {QStringLiteral("id"), QStringLiteral("photo-2")},
              {QStringLiteral("beforeHash"), QString(64, QLatin1Char('a'))},
              {QStringLiteral("currentHash"), QString(64, QLatin1Char('b'))},
              {QStringLiteral("pending"), false},
              {QStringLiteral("reverted"), false},
          }}},
      }},
  });
  if (session.stage() != QStringLiteral("review")) {
    return fail("successful native task receipts should enter review");
  }
  if (session.validateApprovedExecutionSnapshot(approved).value(QStringLiteral("ok")).toBool())
    return fail("a queued approval must not remain valid after the executor reaches Review");
  const auto reviewed_activity = session.activity();
  const auto activity_changes_after_review = activity_change_count;
  session.reportExecutionActivity(QStringLiteral("A late executor event must not replace the review receipt."));
  if (session.activity() != reviewed_activity || activity_change_count != activity_changes_after_review)
    return fail("executor activity received after Review must not overwrite settled task status");
  if (session.revertPending()) return fail("A settled execution receipt must clear the pending Revert guard");
  session.submit(QStringLiteral("Make the headline larger"), changed_target);
  if (!wait_until([&session]() { return session.stage() == QStringLiteral("plan-ready"); })) {
    return fail("a revision prompt should be planned from the reviewed task");
  }
  const auto revision_context = session.brief().value(QStringLiteral("revisionContext")).toMap();
  const auto current_execution = revision_context.value(QStringLiteral("currentExecution")).toMap();
  const auto target_checkpoints = current_execution.value(QStringLiteral("targetCheckpoints")).toList();
  if (revision_context.value(QStringLiteral("originalPrompt")).toString() !=
          QStringLiteral("Make an event design for my photo") ||
      revision_context.value(QStringLiteral("originalPlan")).toMap()
              .value(QStringLiteral("summary")).toString() != QStringLiteral("Create the requested editable design.") ||
      current_execution.value(QStringLiteral("summary")).toString() !=
          QStringLiteral("The initial editable design is ready for review.") ||
      current_execution.value(QStringLiteral("taskId")).toString() != QStringLiteral("task-77") ||
      target_checkpoints.size() != 1 ||
      target_checkpoints.first().toMap().value(QStringLiteral("currentHash")).toString() != QString(64, QLatin1Char('b')) ||
      current_execution.contains(QStringLiteral("toolCalls"))) {
    return fail("revision planning must receive the original approved brief and compact task checkpoints, not the full tool ledger");
  }
  live_revision = QStringLiteral("asset-rev-3");
  session.start();
  if (execution_count != 2 || !approved.value(QStringLiteral("isRevision")).toBool() ||
      approved.value(QStringLiteral("continuationTaskId")).toString() != QStringLiteral("task-77") ||
      approved.value(QStringLiteral("previousExecutionReport")).toMap().contains(QStringLiteral("toolCalls")) ||
      approved.value(QStringLiteral("revisionContext")).toMap().value(QStringLiteral("originalPrompt")).toString() !=
          QStringLiteral("Make an event design for my photo")) {
    return fail("revision Start must preserve the original executor task identity");
  }
  session.acknowledgeExecution(QVariantMap{
      {QStringLiteral("ok"), true},
      {QStringLiteral("taskId"), QStringLiteral("task-77")},
      {QStringLiteral("taskTransaction"), QVariantMap{{QStringLiteral("status"), QStringLiteral("review")}}},
  });
  session.acknowledgeExecution(QVariantMap{
      {QStringLiteral("ok"), true},
      {QStringLiteral("status"), QStringLiteral("reverted")},
      {QStringLiteral("taskTransaction"), QVariantMap{{QStringLiteral("status"), QStringLiteral("reverted")}}},
  });
  if (session.stage() != QStringLiteral("interrupted") ||
      session.brief().value(QStringLiteral("prompt")).toString() != QStringLiteral("Make the headline larger") ||
      !session.brief().value(QStringLiteral("answers")).toMap().contains(QStringLiteral("event_date"))) {
    return fail("a review-stage revert must leave review and preserve the prompt and answers for another attempt");
  }

  AgentSessionController canvas_session([](const AgentLocalModelTaskSessionRequest& request) { return ready(request); });
  QVariantMap canvas_approved;
  QObject::connect(&canvas_session, &AgentSessionController::executionRequested, &app,
      [&canvas_approved](const QVariantMap& snapshot) { canvas_approved = snapshot; });
  canvas_session.setContextSnapshotHandler([](const QVariantMap& raw) {
    auto context = raw;
    const auto ids = context.value("targetIds").toStringList();
    context.insert("contextReady", ids.isEmpty() || ids == QStringList{"created-canvas"});
    return context;
  });
  canvas_session.submit("Make an event poster on a blank canvas", {{"blankCanvas", true}});
  if (!wait_until([&] { return canvas_session.stage() == "plan-ready"; }) ||
      canvas_session.resolvePreparedCanvasTarget("canvas-task", "created-canvas").value("ok").toBool())
    return fail("Created-target attachment requires an executing approved blank-canvas plan");
  canvas_session.start();
  if (!canvas_session.validateApprovedExecutionSnapshot(canvas_approved).value("ok").toBool())
    return fail("an unchanged blank-canvas approval should pass admission before creation starts");
  if (canvas_session.resolvePreparedCanvasTarget("canvas-task", "missing").value("ok").toBool())
    return fail("Prepared canvas attachment must consult authoritative target readiness");
  const auto attached = canvas_session.resolvePreparedCanvasTarget("canvas-task", "created-canvas");
  if (!attached.value("ok").toBool() ||
      attached.value("approvedSnapshot").toMap().value("targetIds").toStringList() != QStringList{"created-canvas"} ||
      canvas_session.resolvePreparedCanvasTarget("canvas-task", "another-canvas").value("ok").toBool())
    return fail("Prepared document binds exactly once without substituting the approved creation outcome");
  canvas_session.acknowledgeExecution({{"ok", true}, {"taskId", "canvas-task"},
      {"taskTransaction", QVariantMap{{"status", "review"}, {"taskId", "canvas-task"}}}});
  canvas_session.submit("Make the title larger", {{"blankCanvas", true}});
  if (!wait_until([&] { return canvas_session.stage() == "plan-ready"; }) ||
      canvas_session.plan().value("targetIds").toStringList() != QStringList{"created-canvas"} ||
      canvas_session.brief().value("blankCanvas").toBool() ||
      canvas_session.brief().value("continuationTaskId").toString() != "canvas-task")
    return fail("Canvas revision must edit the created document under the original task instead of creating another");
  canvas_session.start();
  if (!canvas_session.validateApprovedExecutionSnapshot(canvas_approved).value("ok").toBool())
    return fail("an unchanged revised-canvas approval should pass admission before task dispatch");
  canvas_session.acknowledgeExecution({{"cancelled", true}, {"taskTransaction", QVariantMap{{"status", "interrupted"}}}});
  if (canvas_session.stage() != "interrupted" ||
      canvas_session.validateApprovedExecutionSnapshot(canvas_approved).value("ok").toBool())
    return fail("a canceled execution must invalidate its queued approved snapshot");
  canvas_session.acknowledgeExecution({{"ok", true}, {"status", "reverted"}});
  if (canvas_session.stage() != "interrupted" || canvas_session.brief().contains("continuationTaskId") ||
      canvas_session.brief().value("isRevision").toBool())
    return fail("Revert after Stop must clear the old task continuation while retaining the conversation");

  std::atomic_int cancellation_calls{0};
  AgentSessionController cancel_session([&cancellation_calls](const AgentLocalModelTaskSessionRequest& request) {
    const auto call = ++cancellation_calls;
    if (call == 1) {
      while (!request.cancellation_requested()) std::this_thread::sleep_for(std::chrono::milliseconds(2));
      AgentLocalModelTaskSessionResult canceled;
      canceled.error_message = QStringLiteral("Planning canceled.");
      return canceled;
    }
    return ready(request);
  });
  cancel_session.submit(QStringLiteral("Create a blank canvas poster"),
                        QVariantMap{{QStringLiteral("blankCanvas"), true}});
  if (!wait_until([&cancel_session]() { return cancel_session.busy(); })) {
    return fail("session planning should run asynchronously");
  }
  cancel_session.cancelPlanning();
  if (cancel_session.stage() != QStringLiteral("interrupted") || cancel_session.busy() ||
      cancel_session.brief().value(QStringLiteral("prompt")).toString() !=
          QStringLiteral("Create a blank canvas poster")) {
    return fail("cancel should stop planning while retaining the prompt and brief");
  }
  cancel_session.retryPlanning();
  if (!wait_until([&cancel_session]() { return cancel_session.stage() == QStringLiteral("plan-ready"); }) ||
      cancellation_calls.load() != 2) {
    return fail("retry should preserve the conversation and start a fresh planning request");
  }

  // A worker that ignores cancellation must not overwrite a newer request.
  std::atomic_int stale_calls{0};
  std::atomic_bool release_stale{false};
  std::atomic_bool stale_finished{false};
  AgentSessionController stale_session([&](const AgentLocalModelTaskSessionRequest& request) {
    const auto call = ++stale_calls;
    if (call == 1) {
      while (!release_stale.load()) std::this_thread::sleep_for(std::chrono::milliseconds(2));
      stale_finished.store(true);
      auto result = ready(request);
      result.plan.insert(QStringLiteral("summary"), QStringLiteral("Stale plan"));
      return result;
    }
    return ready(request);
  });
  stale_session.submit(QStringLiteral("Create a design"),
      QVariantMap{{QStringLiteral("targetIds"), QStringList{QStringLiteral("old-target")}}});
  if (!wait_until([&stale_session]() { return stale_session.busy(); })) {
    return fail("stale-request fixture should start its first planning worker");
  }
  stale_session.updateContext(
      QVariantMap{{QStringLiteral("targetIds"), QStringList{QStringLiteral("new-target")}}});
  if (!wait_until([&stale_session]() { return stale_session.stage() == QStringLiteral("plan-ready"); })) {
    return fail("new target request should finish independently of the stale worker");
  }
  release_stale.store(true);
  if (!wait_until([&stale_finished]() { return stale_finished.load(); })) {
    return fail("stale worker fixture should finish");
  }
  QThread::msleep(10);
  QCoreApplication::processEvents();
  if (stale_session.plan().value(QStringLiteral("summary")).toString() == QStringLiteral("Stale plan") ||
      stale_session.plan().value(QStringLiteral("targetIds")).toStringList() != QStringList{QStringLiteral("new-target")}) {
    return fail("a late planning response must not replace a plan bound to newer targets");
  }

  AgentSessionController default_session([](const AgentLocalModelTaskSessionRequest& request) {
    if (!request.brief.value(QStringLiteral("useDefaults")).toBool())
      return ask(QStringLiteral("exact_copy"), QStringLiteral("What should the title say?"));
    return ready(request);
  });
  int default_execution_requests = 0;
  QObject::connect(&default_session, &AgentSessionController::executionRequested, &app,
      [&default_execution_requests](const QVariantMap&) { ++default_execution_requests; });
  default_session.submit(QStringLiteral("Make a thumbnail from this photo"), first_context);
  if (!wait_until([&default_session]() { return default_session.stage() == QStringLiteral("clarifying"); }))
    return fail("the defaults fixture must first expose a clarification");
  default_session.planWithDefaults({{QStringLiteral("creative_direction"), QStringLiteral("Minimal and colorful")}});
  if (!wait_until([&default_session]() { return default_session.stage() == QStringLiteral("plan-ready"); }) ||
      !default_session.canStart() || default_execution_requests != 0 ||
      default_session.brief().value(QStringLiteral("prompt")).toString() != QStringLiteral("Make a thumbnail from this photo") ||
      default_session.brief().value(QStringLiteral("answers")).toMap().value(QStringLiteral("creative_direction")) != QStringLiteral("Minimal and colorful"))
    return fail("using judgment must bypass unanswered required details, retain the brief, and still await Start approval");
  default_session.start();
  default_session.planWithDefaults({});
  if (default_execution_requests != 1 || default_session.stage() != QStringLiteral("executing"))
    return fail("choosing defaults must not restart an executing task");

  return EXIT_SUCCESS;
}
