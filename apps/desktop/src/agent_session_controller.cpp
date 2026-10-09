#include "agent_session_controller.h"

#include <QFutureWatcher>
#include <QMetaObject>
#include <QPointer>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <atomic>

namespace photo_editor::desktop {
namespace {

QStringList normalized_ids(const QVariant& value) {
  QStringList result;
  const auto append_id = [&result](const QVariant& item) {
    const auto map = item.toMap();
    const auto id = item.metaType().id() == QMetaType::QString
                        ? item.toString().trimmed()
                        : map.value(QStringLiteral("id")).toString().trimmed();
    if (!id.isEmpty() && !result.contains(id)) result.append(id);
  };
  if (value.metaType().id() == QMetaType::QString) {
    append_id(value);
  } else {
    for (const auto& item : value.toList()) append_id(item);
  }
  return result;
}

QStringList context_target_ids(const QVariantMap& context) {
  for (const auto& key : {QStringLiteral("targetIds"), QStringLiteral("currentAssetIds"),
           QStringLiteral("assetIds"), QStringLiteral("selectedAssetIds")}) {
    const auto ids = normalized_ids(context.value(key));
    if (!ids.isEmpty()) return ids;
  }
  return {};
}

QString context_readiness_error(const QVariantMap& context) {
  for (const auto& key : {QStringLiteral("contextError"), QStringLiteral("targetError"),
           QStringLiteral("snapshotError"), QStringLiteral("readinessError")}) {
    const auto error = context.value(key).toString().trimmed();
    if (!error.isEmpty()) return error;
  }
  if (context.contains(QStringLiteral("contextReady")) &&
      !context.value(QStringLiteral("contextReady")).toBool()) {
    return QStringLiteral("The current target context is not ready to plan.");
  }
  if (context.contains(QStringLiteral("selectedModelReady")) &&
      !context.value(QStringLiteral("selectedModelReady")).toBool()) {
    const auto reason = context.value(QStringLiteral("selectedModelReadinessReason")).toString().trimmed();
    return reason.isEmpty() ? QStringLiteral("The selected local model is not ready.") : reason;
  }
  return {};
}

bool has_valid_typed_plan_actions(const QVariantMap& plan) {
  const auto actions_value = plan.value(QStringLiteral("actions"));
  if (actions_value.metaType().id() != QMetaType::QVariantList) return false;
  const auto actions = actions_value.toList();
  if (actions.isEmpty() || actions.size() > 6) return false;
  static const QStringList supported_kinds{
      QStringLiteral("color"), QStringLiteral("framing"),
      QStringLiteral("masking"), QStringLiteral("text"),
      QStringLiteral("layers"), QStringLiteral("effects")};
  for (const auto& value : actions) {
    if (value.metaType().id() != QMetaType::QVariantMap) return false;
    const auto action = value.toMap();
    const auto kind = action.value(QStringLiteral("kind"));
    const auto summary = action.value(QStringLiteral("summary"));
    if (action.size() != 2 || kind.metaType().id() != QMetaType::QString ||
        !supported_kinds.contains(kind.toString()) ||
        summary.metaType().id() != QMetaType::QString ||
        summary.toString().trimmed().isEmpty() || summary.toString().trimmed().size() > 180)
      return false;
  }
  return true;
}

QVariantMap bounded_rejected_plan(const QVariantMap& plan) {
  QVariantMap bounded;
  const auto append_text = [&bounded, &plan](const QString& key, int max_chars) {
    const auto value = plan.value(key);
    if (value.metaType().id() != QMetaType::QString) return;
    const auto text = value.toString().simplified().left(max_chars);
    if (!text.isEmpty()) bounded.insert(key, text);
  };
  append_text(QStringLiteral("summary"), 120);
  append_text(QStringLiteral("outcome"), 200);
  QVariantList actions;
  const auto raw_actions = plan.value(QStringLiteral("actions"));
  if (raw_actions.metaType().id() == QMetaType::QVariantList) {
    const auto values = raw_actions.toList();
    for (qsizetype index = 0; index < std::min<qsizetype>(values.size(), 6); ++index) {
      const auto raw_action = values.at(index);
      if (raw_action.metaType().id() != QMetaType::QVariantMap) continue;
      const auto action = raw_action.toMap();
      QVariantMap bounded_action;
      for (const auto& key : {QStringLiteral("kind"), QStringLiteral("summary")}) {
        const auto value = action.value(key);
        if (value.metaType().id() != QMetaType::QString) continue;
        const auto text = value.toString().simplified().left(
            key == QStringLiteral("summary") ? 120 : 24);
        if (!text.isEmpty()) bounded_action.insert(key, text);
      }
      if (!bounded_action.isEmpty()) actions.append(bounded_action);
    }
  }
  if (!actions.isEmpty()) bounded.insert(QStringLiteral("actions"), actions);
  return bounded;
}

QVariantMap build_plan_validation_feedback(
    const AgentLocalModelTaskSessionResult& result) {
  auto failure = result.prompt_audit.value(QStringLiteral("planValidationFailure")).toMap();
  const auto native_typed_action_failure =
      failure.value(QStringLiteral("code")).toString() ==
          QStringLiteral("invalid_typed_actions");
  const auto incomplete_ready_plan = result.ok &&
      result.decision == QStringLiteral("plan_ready") &&
      !has_valid_typed_plan_actions(result.plan);
  if (!native_typed_action_failure && !incomplete_ready_plan) return {};

  auto instruction = failure.value(QStringLiteral("instruction")).toString().simplified();
  if (instruction.isEmpty()) {
    instruction = QStringLiteral(
        "Replan the same approved request with one to six explicit typed outcome actions. "
        "Choose each action kind from color, framing, masking, text, layers, or effects. "
        "Preserve the original prompt, supplied copy, targets, and constraints; do not infer "
        "permissions from free-form deliverable or constraint prose.");
  }
  instruction = instruction.left(600);
  auto candidate_plan = failure.value(QStringLiteral("candidatePlan")).toMap();
  if (candidate_plan.isEmpty()) candidate_plan = result.plan;
  return QVariantMap{
      {QStringLiteral("code"), QStringLiteral("invalid_typed_actions")},
      {QStringLiteral("instruction"), instruction},
      {QStringLiteral("rejectedPlan"), bounded_rejected_plan(candidate_plan)},
  };
}

QVariantMap compact_plan_for_revision(const QVariantMap& snapshot) {
  const auto plan = snapshot.value(QStringLiteral("plan")).toMap();
  QVariantMap compact;
  for (const auto& key : {QStringLiteral("summary"), QStringLiteral("outcome"),
           QStringLiteral("actions"), QStringLiteral("exactCopy"), QStringLiteral("deliverables"),
           QStringLiteral("constraints"), QStringLiteral("copySuggestionApprovalRequired"),
           QStringLiteral("briefVersion")}) {
    if (plan.contains(key)) compact.insert(key, plan.value(key));
  }
  return compact;
}

QVariantMap compact_task_report_for_revision(
    const QVariantMap& report,
    const QVariantMap& task_snapshot) {
  const auto transaction = report.value(QStringLiteral("taskTransaction")).toMap();
  const auto report_task_id = report.value(QStringLiteral("taskId")).toString().trimmed();
  const auto transaction_task_id = transaction.value(QStringLiteral("taskId")).toString().trimmed();
  const auto task_id = report_task_id.isEmpty() ? transaction_task_id : report_task_id;
  auto summary = report.value(QStringLiteral("headline")).toString().simplified();
  if (summary.isEmpty()) summary = report.value(QStringLiteral("summary")).toString().simplified();
  if (summary.isEmpty()) summary = report.value(QStringLiteral("failureReason")).toString().simplified();
  if (summary.size() > 500) summary = summary.left(500).trimmed();

  const auto target_ids = normalized_ids(task_snapshot.value(QStringLiteral("targetIds")));
  const auto resources_value = transaction.value(QStringLiteral("resources"));
  const auto resources = resources_value.isValid()
                            ? resources_value.toList()
                            : report.value(QStringLiteral("resources")).toList();
  QVariantList target_checkpoints;
  for (const auto& value : resources) {
    const auto resource = value.toMap();
    const auto id = resource.value(QStringLiteral("id")).toString().trimmed();
    if (id.isEmpty() || (!target_ids.isEmpty() && !target_ids.contains(id))) continue;
    QVariantMap checkpoint{{QStringLiteral("id"), id}};
    for (const auto& key : {QStringLiteral("beforeHash"), QStringLiteral("currentHash"),
             QStringLiteral("checkpointHash"), QStringLiteral("pending"), QStringLiteral("reverted")}) {
      if (resource.contains(key)) checkpoint.insert(key, resource.value(key));
    }
    target_checkpoints.append(checkpoint);
  }
  return QVariantMap{
      {QStringLiteral("contract"), QStringLiteral("agent-task-revision-context-v1")},
      {QStringLiteral("taskId"), task_id},
      {QStringLiteral("status"), transaction.value(QStringLiteral("status"), report.value(QStringLiteral("status")))},
      {QStringLiteral("summary"), summary},
      {QStringLiteral("targetIds"), target_ids},
      {QStringLiteral("targetCheckpoints"), target_checkpoints},
  };
}

}  // namespace

AgentSessionController::AgentSessionController(QObject* parent)
    : AgentSessionController(PlanningHandler{}, parent) {
  test_handler_injected_ = false;
}

AgentSessionController::AgentSessionController(
    PlanningHandler test_handler,
    QObject* parent)
    : QObject(parent), planning_handler_(std::move(test_handler)),
      test_handler_injected_(static_cast<bool>(planning_handler_)) {
  if (!planning_handler_) {
    planning_handler_ = [](const AgentLocalModelTaskSessionRequest& request) {
      return plan_agent_task_session(request);
    };
  }
}

AgentSessionController::~AgentSessionController() {
  if (cancellation_token_) cancellation_token_->store(true, std::memory_order_relaxed);
}

QString AgentSessionController::stage() const { return stage_; }
QVariantMap AgentSessionController::brief() const { return brief_; }
QVariantMap AgentSessionController::plan() const { return plan_; }
QVariantList AgentSessionController::questions() const { return questions_; }
QString AgentSessionController::status() const { return status_; }
QString AgentSessionController::activity() const { return activity_; }
bool AgentSessionController::busy() const { return busy_; }

bool AgentSessionController::canStart() const {
  return !busy_ && stage_ == QStringLiteral("plan-ready") && !plan_.isEmpty() &&
         has_valid_typed_plan_actions(plan_) &&
         plan_.value(QStringLiteral("briefVersion")).toULongLong() == brief_version_ &&
         plan_.value(QStringLiteral("targetSnapshot")).toMap() == currentTargetSnapshot() &&
         (test_handler_injected_ || context_snapshot_handler_) &&
         (test_handler_injected_ || contextReadinessError().isEmpty());
}

QVariantMap AgentSessionController::revertAvailability() const {
  return revert_availability_handler_ ? revert_availability_handler_() : QVariantMap{};
}

bool AgentSessionController::canRevert() const {
  return revertAvailability().value(QStringLiteral("available")).toBool();
}

bool AgentSessionController::revertPending() const {
  const auto availability = revertAvailability();
  return revert_request_pending_ || availability.value(QStringLiteral("pending")).toBool() ||
         availability.value(QStringLiteral("settling")).toBool();
}

QString AgentSessionController::revertOwner() const {
  return revertAvailability().value(QStringLiteral("owner")).toString();
}

void AgentSessionController::setStage(const QString& stage, const QString& status) {
  stage_ = stage;
  status_ = status;
  emit stateChanged();
}

QString AgentSessionController::selectedModelId() const {
  const auto selected = context_.value(QStringLiteral("selectedModelId")).toString().trimmed();
  return selected.isEmpty() ? default_agent_local_model_id() : selected;
}

QVariantMap AgentSessionController::normalizeContext(const QVariantMap& context) const {
  if (!context_snapshot_handler_) return context;
  auto normalized = context_snapshot_handler_(context);
  for (auto it = context.cbegin(); it != context.cend(); ++it) {
    if (!normalized.contains(it.key())) normalized.insert(it.key(), it.value());
  }
  return normalized;
}

QVariantMap AgentSessionController::normalizeContextSnapshot(const QVariantMap& context) const {
  QVariantMap snapshot;
  for (const auto& key : {
           QStringLiteral("targetIds"), QStringLiteral("currentAssetIds"),
           QStringLiteral("assetIds"), QStringLiteral("selectedAssetIds"),
           QStringLiteral("projectId"), QStringLiteral("scope"),
           QStringLiteral("blankCanvas"), QStringLiteral("targetSnapshot"),
           QStringLiteral("targetRevision"), QStringLiteral("assetRevisions"),
           QStringLiteral("documentIds"), QStringLiteral("timelineId"),
           QStringLiteral("projectRevision"), QStringLiteral("inspirationIds"),
           QStringLiteral("selectedStyles"), QStringLiteral("preferences"),
           QStringLiteral("projectPreferences"), QStringLiteral("userPreferences"),
           QStringLiteral("knownPreferences"), QStringLiteral("inspirationRevisions"),
           QStringLiteral("styleRevisions")}) {
    if (context.contains(key)) snapshot.insert(key, context.value(key));
  }
  return snapshot;
}

QString AgentSessionController::contextReadinessError() const {
  return context_readiness_error(context_);
}

QVariantMap AgentSessionController::currentTargetSnapshot() const {
  QVariantMap snapshot{
      {QStringLiteral("targetIds"), context_target_ids(context_)},
      {QStringLiteral("projectId"), context_.value(QStringLiteral("projectId"))},
      {QStringLiteral("scope"), context_.value(QStringLiteral("scope"))},
      {QStringLiteral("blankCanvas"), context_.value(QStringLiteral("blankCanvas")).toBool()},
  };
  for (const auto& key : {QStringLiteral("targetSnapshot"), QStringLiteral("targetRevision"),
           QStringLiteral("assetRevisions"), QStringLiteral("documentIds"),
           QStringLiteral("timelineId"), QStringLiteral("projectRevision"),
           QStringLiteral("inspirationRevisions"), QStringLiteral("styleRevisions")}) {
    if (context_.contains(key)) snapshot.insert(key, context_.value(key));
  }
  return snapshot;
}

void AgentSessionController::updateBriefFromContext() {
  brief_.insert(QStringLiteral("context"), context_);
  brief_.insert(QStringLiteral("targetIds"), currentTargetSnapshot().value(QStringLiteral("targetIds")));
  brief_.insert(QStringLiteral("targetSnapshot"), currentTargetSnapshot());
  brief_.insert(QStringLiteral("projectId"), context_.value(QStringLiteral("projectId")));
  brief_.insert(QStringLiteral("blankCanvas"), context_.value(QStringLiteral("blankCanvas")).toBool());
  brief_.insert(QStringLiteral("inspirationIds"), context_.value(QStringLiteral("inspirationIds")));
  brief_.insert(QStringLiteral("version"), QVariant::fromValue(brief_version_));
  brief_.insert(QStringLiteral("isRevision"), revision_session_);
  if (revision_session_) {
    const auto transaction = last_execution_report_.value(QStringLiteral("taskTransaction")).toMap();
    const auto task_id = last_execution_report_.value(QStringLiteral("taskId")).toString().trimmed().isEmpty()
                             ? transaction.value(QStringLiteral("taskId")).toString()
                             : last_execution_report_.value(QStringLiteral("taskId")).toString();
    brief_.insert(QStringLiteral("continuationTaskId"), task_id);
    const auto origin = task_origin_snapshot_.isEmpty() ? last_approved_snapshot_ : task_origin_snapshot_;
    brief_.insert(QStringLiteral("revisionContext"), QVariantMap{
        {QStringLiteral("originalPrompt"), origin.value(QStringLiteral("prompt"))},
        {QStringLiteral("originalPlan"), compact_plan_for_revision(origin)},
        {QStringLiteral("currentExecution"), compact_task_report_for_revision(last_execution_report_, origin)},
    });
  } else {
    brief_.remove(QStringLiteral("continuationTaskId"));
    brief_.remove(QStringLiteral("revisionContext"));
  }
}

void AgentSessionController::submit(const QString& prompt, const QVariantMap& context) {
  if (stage_ == QStringLiteral("executing")) return;
  const auto user_prompt = prompt;
  if (user_prompt.trimmed().isEmpty()) {
    setStage(QStringLiteral("error"), QStringLiteral("Describe what you want the agent to do first."));
    return;
  }
  const auto new_session = stage_ == QStringLiteral("idle") || brief_.isEmpty();
  invalidatePlanning();
  if (new_session) {
    brief_.clear();
    plan_.clear();
    questions_.clear();
    prior_questions_.clear();
  }
  revision_session_ = !new_session && !last_execution_report_.isEmpty();
  raw_context_ = context;
  if (revision_session_ && !last_approved_snapshot_.isEmpty()) {
    raw_context_.insert(QStringLiteral("targetIds"), last_approved_snapshot_.value(QStringLiteral("targetIds")));
    raw_context_.insert(QStringLiteral("projectId"), last_approved_snapshot_.value(QStringLiteral("projectId")));
    raw_context_.insert(QStringLiteral("blankCanvas"), last_approved_snapshot_.value(QStringLiteral("blankCanvas")));
    raw_context_.remove(QStringLiteral("targetSnapshot"));
  }
  context_ = normalizeContext(raw_context_);
  const auto old_answers = brief_.value(QStringLiteral("answers")).toMap();
  brief_.clear();
  brief_.insert(QStringLiteral("prompt"), user_prompt);
  brief_.insert(QStringLiteral("answers"), old_answers);
  ++brief_version_;
  plan_.clear();
  questions_.clear();
  updateBriefFromContext();
  if (!test_handler_injected_ && !contextReadinessError().isEmpty()) {
    setStage(QStringLiteral("error"), contextReadinessError());
    return;
  }
  launchPlanning();
}

void AgentSessionController::answerQuestions(const QVariantMap& answers) {
  if (stage_ != QStringLiteral("clarifying") || questions_.isEmpty() || busy_) return;
  auto retained_answers = brief_.value(QStringLiteral("answers")).toMap();
  for (const auto& question_value : questions_) {
    const auto question = question_value.toMap();
    const auto id = question.value(QStringLiteral("id")).toString();
    if (!answers.contains(id)) {
      if (question.value(QStringLiteral("required")).toBool()) {
        setStage(QStringLiteral("clarifying"), QStringLiteral("Answer the required question to continue."));
        return;
      }
      continue;
    }
    const auto answer = answers.value(id).toString();
    if (question.value(QStringLiteral("required")).toBool() && answer.trimmed().isEmpty()) {
      setStage(QStringLiteral("clarifying"), QStringLiteral("Answer the required question to continue."));
      return;
    }
    retained_answers.insert(id, answer);
  }
  for (const auto& question : questions_) prior_questions_.append(question);
  brief_.insert(QStringLiteral("clarificationComplete"), true);
  brief_.insert(QStringLiteral("answers"), retained_answers);
  ++brief_version_;
  plan_.clear();
  questions_.clear();
  updateBriefFromContext();
  if (!test_handler_injected_ && !contextReadinessError().isEmpty()) {
    setStage(QStringLiteral("error"), contextReadinessError());
    return;
  }
  launchPlanning();
}

void AgentSessionController::planWithDefaults(const QVariantMap& answers) {
  if (busy_ || (stage_ != QStringLiteral("clarifying") && stage_ != QStringLiteral("error")) ||
      brief_.value(QStringLiteral("prompt")).toString().trimmed().isEmpty()) return;
  auto retained_answers = brief_.value(QStringLiteral("answers")).toMap();
  for (auto it = answers.cbegin(); it != answers.cend(); ++it) {
    if (!it.value().toString().trimmed().isEmpty()) retained_answers.insert(it.key(), it.value());
  }
  for (const auto& question : questions_) prior_questions_.append(question);
  brief_.insert(QStringLiteral("answers"), retained_answers);
  brief_.insert(QStringLiteral("useDefaults"), true);
  ++brief_version_;
  questions_.clear();
  plan_.clear();
  context_ = normalizeContext(raw_context_);
  updateBriefFromContext();
  launchPlanning();
}

void AgentSessionController::reviseAnswers(const QVariantMap& answers) {
  if (brief_.isEmpty() || stage_ == QStringLiteral("idle") ||
      stage_ == QStringLiteral("executing") || stage_ == QStringLiteral("review")) return;
  const auto existing_answers = brief_.value(QStringLiteral("answers")).toMap();
  if (existing_answers == answers) return;
  invalidatePlanning();
  brief_.insert(QStringLiteral("answers"), answers);
  ++brief_version_;
  questions_.clear();
  plan_.clear();
  updateBriefFromContext();
  if (!test_handler_injected_ && !contextReadinessError().isEmpty()) {
    setStage(QStringLiteral("error"), contextReadinessError());
    return;
  }
  launchPlanning();
}

void AgentSessionController::updateContext(const QVariantMap& context) {
  if (stage_ == QStringLiteral("executing") || stage_ == QStringLiteral("review")) return;
  if (raw_context_ == context) return;
  raw_context_ = context;
  context_ = normalizeContext(raw_context_);
  if (stage_ == QStringLiteral("idle") || brief_.isEmpty()) {
    emit stateChanged();
    return;
  }
  invalidatePlanning();
  ++brief_version_;
  updateBriefFromContext();
  plan_.clear();
  questions_.clear();
  launchPlanning();
}

void AgentSessionController::retryPlanning() {
  if (stage_ == QStringLiteral("executing") || stage_ == QStringLiteral("review") ||
      brief_.value(QStringLiteral("prompt")).toString().trimmed().isEmpty()) return;
  questions_.clear();
  plan_.clear();
  context_ = normalizeContext(raw_context_);
  ++brief_version_;
  updateBriefFromContext();
  launchPlanning();
}

void AgentSessionController::invalidatePlanning() {
  if (cancellation_token_) cancellation_token_->store(true, std::memory_order_relaxed);
  cancellation_token_.reset();
  ++request_id_;
  busy_ = false;
  active_watcher_ = nullptr;
}

void AgentSessionController::launchPlanning(QVariantMap plan_validation_feedback) {
  invalidatePlanning();
  if (!test_handler_injected_ && !contextReadinessError().isEmpty()) {
    stage_ = QStringLiteral("error");
    status_ = contextReadinessError();
    emit stateChanged();
    return;
  }
  const auto request_id = request_id_;
  const auto token = std::make_shared<std::atomic_bool>(false);
  cancellation_token_ = token;
  AgentLocalModelTaskSessionRequest request;
  request.model_id = selectedModelId();
  request.brief = brief_;
  if (!plan_validation_feedback.isEmpty())
    request.brief.insert(QStringLiteral("planValidationFeedback"), plan_validation_feedback);
  request.context = context_;
  request.answers = brief_.value(QStringLiteral("answers")).toMap();
  request.prior_questions = prior_questions_;
  request.cancellation_requested = [token]() {
    return token->load(std::memory_order_relaxed);
  };
  const QPointer<AgentSessionController> controller(this);
  request.activity_received = [controller, request_id](const QString& update) {
    if (!controller) return;
    QMetaObject::invokeMethod(
        controller.data(),
        [controller, request_id, update]() {
          if (!controller || controller->request_id_ != request_id || update.isEmpty()) return;
          controller->activity_ = update;
          emit controller->activityChanged();
          emit controller->activityReceived(update);
        },
        Qt::QueuedConnection);
  };
  const auto handler = planning_handler_;
  const auto is_plan_validation_recovery = !plan_validation_feedback.isEmpty();
  auto* watcher = new QFutureWatcher<AgentLocalModelTaskSessionResult>(this);
  active_watcher_ = watcher;
  connect(watcher, &QFutureWatcher<AgentLocalModelTaskSessionResult>::finished, this,
      [this, watcher, request_id, token, is_plan_validation_recovery]() {
        const auto result = watcher->result();
        watcher->deleteLater();
        if (active_watcher_ == watcher) active_watcher_ = nullptr;
        if (request_id != request_id_) return;
        cancellation_token_.reset();
        busy_ = false;
        if (token->load(std::memory_order_relaxed)) {
          stage_ = QStringLiteral("interrupted");
          status_ = QStringLiteral("Planning stopped. Your prompt and answers are saved; retry to continue.");
          emit stateChanged();
          return;
        }
        const auto validation_feedback = build_plan_validation_feedback(result);
        if (!validation_feedback.isEmpty()) {
          if (!is_plan_validation_recovery) {
            plan_.clear();
            questions_.clear();
            launchPlanning(validation_feedback);
            return;
          }
          plan_.clear();
          questions_.clear();
          stage_ = QStringLiteral("error");
          status_ = QStringLiteral(
              "The agent could not prepare a complete plan after retrying. Your request is saved; retry to continue.");
          emit stateChanged();
          return;
        }
        if (!result.ok) {
          plan_.clear();
          stage_ = QStringLiteral("error");
          status_ = result.error_message.trimmed().isEmpty()
                        ? QStringLiteral("The local model could not prepare a plan. Retry when it is available.")
                        : result.error_message.trimmed();
          emit stateChanged();
          return;
        }
        if (result.decision == QStringLiteral("ask_questions")) {
          questions_ = result.questions;
          plan_.clear();
          stage_ = QStringLiteral("clarifying");
          status_ = QStringLiteral("A couple of details will make the plan specific.");
          emit stateChanged();
          return;
        }
        if (result.decision != QStringLiteral("plan_ready") || result.plan.isEmpty()) {
          plan_.clear();
          stage_ = QStringLiteral("error");
          status_ = QStringLiteral("The local model returned no usable plan. Retry after reviewing the request.");
          emit stateChanged();
          return;
        }
        plan_ = result.plan;
        plan_.insert(QStringLiteral("briefVersion"), QVariant::fromValue(brief_version_));
        plan_.insert(QStringLiteral("targetIds"), currentTargetSnapshot().value(QStringLiteral("targetIds")));
        plan_.insert(QStringLiteral("targetSnapshot"), currentTargetSnapshot());
        plan_.insert(QStringLiteral("contextSnapshot"), normalizeContextSnapshot(context_));
        stage_ = QStringLiteral("plan-ready");
        status_ = QStringLiteral("Review the outcome and targets, then Start when it looks right.");
        emit stateChanged();
      });
  busy_ = true;
  stage_ = QStringLiteral("planning");
  status_ = is_plan_validation_recovery
      ? QStringLiteral("Refining the plan before review.")
      : QStringLiteral("Preparing a plan from your prompt and current context.");
  activity_.clear();
  emit activityChanged();
  emit stateChanged();
  watcher->setFuture(QtConcurrent::run([handler, request]() { return handler(request); }));
}

void AgentSessionController::start() {
  if (stage_ != QStringLiteral("plan-ready") || busy_ || plan_.isEmpty()) return;
  if (!has_valid_typed_plan_actions(plan_)) {
    plan_.clear();
    setStage(QStringLiteral("error"), QStringLiteral(
        "The plan is incomplete. Retry planning before starting."));
    return;
  }
  if (!test_handler_injected_ && !context_snapshot_handler_) {
    setStage(QStringLiteral("error"), QStringLiteral("The current targets could not be refreshed for approval."));
    return;
  }
  if (context_snapshot_handler_) {
    const auto fresh_context = normalizeContext(raw_context_);
    context_ = fresh_context;
    const auto readiness_error = contextReadinessError();
    if (!readiness_error.isEmpty()) {
      ++brief_version_;
      updateBriefFromContext();
      plan_.clear();
      stage_ = QStringLiteral("error");
      status_ = readiness_error;
      emit stateChanged();
      return;
    }
    const auto snapshot_changed =
        plan_.value(QStringLiteral("targetSnapshot")).toMap() != currentTargetSnapshot() ||
        plan_.value(QStringLiteral("contextSnapshot")).toMap() != normalizeContextSnapshot(context_);
    if (snapshot_changed) {
      ++brief_version_;
      updateBriefFromContext();
      plan_.clear();
      questions_.clear();
      launchPlanning();
      return;
    }
  }
  if (!canStart()) return;
  // Use the just-refreshed authoritative context in the immutable approval,
  // including reference/style payloads whose revisions were checked above.
  updateBriefFromContext();
  if (!contextReadinessError().isEmpty()) {
    setStage(QStringLiteral("error"), contextReadinessError());
    return;
  }
  if (!canStart()) return;
  QVariantMap approved_snapshot{
      {QStringLiteral("contract"), QStringLiteral("agent-approved-session-v1")},
      {QStringLiteral("briefVersion"), QVariant::fromValue(brief_version_)},
      {QStringLiteral("prompt"), brief_.value(QStringLiteral("prompt"))},
      {QStringLiteral("answers"), brief_.value(QStringLiteral("answers"))},
      {QStringLiteral("revisionContext"), brief_.value(QStringLiteral("revisionContext"))},
      {QStringLiteral("context"), brief_.value(QStringLiteral("context"))},
      {QStringLiteral("targetIds"), plan_.value(QStringLiteral("targetIds"))},
      {QStringLiteral("targetSnapshot"), plan_.value(QStringLiteral("targetSnapshot"))},
      {QStringLiteral("projectId"), brief_.value(QStringLiteral("projectId"))},
      {QStringLiteral("blankCanvas"), brief_.value(QStringLiteral("blankCanvas"))},
      {QStringLiteral("inspirationIds"), brief_.value(QStringLiteral("inspirationIds"))},
      {QStringLiteral("exactCopy"), plan_.value(QStringLiteral("exactCopy"))},
      {QStringLiteral("deliverables"), plan_.value(QStringLiteral("deliverables"))},
      {QStringLiteral("plan"), plan_},
      {QStringLiteral("isRevision"), revision_session_},
  };
  if (revision_session_ && !last_execution_report_.isEmpty()) {
    const auto task_transaction = last_execution_report_.value(QStringLiteral("taskTransaction")).toMap();
    const auto task_id = last_execution_report_.value(QStringLiteral("taskId")).toString().trimmed().isEmpty()
                             ? task_transaction.value(QStringLiteral("taskId")).toString()
                             : last_execution_report_.value(QStringLiteral("taskId")).toString();
    approved_snapshot.insert(QStringLiteral("continuationTaskId"), task_id);
    approved_snapshot.insert(QStringLiteral("previousExecutionReport"),
        compact_task_report_for_revision(last_execution_report_,
            task_origin_snapshot_.isEmpty() ? last_approved_snapshot_ : task_origin_snapshot_));
  }
  last_approved_snapshot_ = approved_snapshot;
  if (!revision_session_ || task_origin_snapshot_.isEmpty()) task_origin_snapshot_ = approved_snapshot;
  stage_ = QStringLiteral("executing");
  status_ = QStringLiteral("Working from the approved plan.");
  activity_ = status_;
  emit activityChanged();
  emit stateChanged();
  emit executionRequested(approved_snapshot);
}

QVariantMap AgentSessionController::validateApprovedExecutionSnapshot(
    const QVariantMap& snapshot) const {
  const auto reject = [](const QString& error) {
    return QVariantMap{{QStringLiteral("ok"), false}, {QStringLiteral("error"), error}};
  };
  if (stage_ != QStringLiteral("executing"))
    return reject(QStringLiteral("The approved request is no longer active."));
  if (last_approved_snapshot_.isEmpty() || snapshot != last_approved_snapshot_)
    return reject(QStringLiteral("The approved request changed before execution could begin."));
  if (snapshot.value(QStringLiteral("contract")).toString() !=
          QStringLiteral("agent-approved-session-v1") ||
      snapshot.value(QStringLiteral("briefVersion")).toULongLong() != brief_version_ ||
      snapshot.value(QStringLiteral("prompt")) != brief_.value(QStringLiteral("prompt")) ||
      snapshot.value(QStringLiteral("answers")).toMap() != brief_.value(QStringLiteral("answers")).toMap()) {
    return reject(QStringLiteral("The approved request no longer matches the current conversation."));
  }

  const auto approved_plan = snapshot.value(QStringLiteral("plan")).toMap();
  if (approved_plan.isEmpty() || !has_valid_typed_plan_actions(approved_plan) ||
      approved_plan != plan_ ||
      approved_plan.value(QStringLiteral("briefVersion")).toULongLong() != brief_version_ ||
      snapshot.value(QStringLiteral("targetIds")) != approved_plan.value(QStringLiteral("targetIds")) ||
      snapshot.value(QStringLiteral("targetSnapshot")) != approved_plan.value(QStringLiteral("targetSnapshot")) ||
      snapshot.value(QStringLiteral("exactCopy")) != approved_plan.value(QStringLiteral("exactCopy")) ||
      snapshot.value(QStringLiteral("deliverables")) != approved_plan.value(QStringLiteral("deliverables"))) {
    return reject(QStringLiteral("The approved targets or plan contents changed before execution could begin."));
  }
  if (!context_snapshot_handler_)
    return reject(QStringLiteral("The current target context cannot be refreshed for execution."));

  const auto fresh_context = normalizeContext(raw_context_);
  const auto readiness_error = context_readiness_error(fresh_context);
  if (!readiness_error.isEmpty()) return reject(readiness_error);
  if (normalizeContextSnapshot(fresh_context) !=
      approved_plan.value(QStringLiteral("contextSnapshot")).toMap()) {
    return reject(QStringLiteral("The selected targets or their saved revisions changed after approval."));
  }
  return {{QStringLiteral("ok"), true}};
}

void AgentSessionController::cancelPlanning() {
  if (!busy_ || stage_ != QStringLiteral("planning")) return;
  invalidatePlanning();
  stage_ = QStringLiteral("interrupted");
  status_ = QStringLiteral("Planning stopped. Your prompt and answers are saved; retry to continue.");
  emit stateChanged();
}

QVariantMap AgentSessionController::requestStop() {
  if (stage_ == QStringLiteral("planning")) {
    cancelPlanning();
    return {{"ok", true}, {"status", "interrupted"}};
  }
  if (stage_ != QStringLiteral("executing") || !execution_control_handler_)
    return {{"ok", false}, {"error", "There is no active task owner to stop."}};
  const auto result = execution_control_handler_(QStringLiteral("stop"));
  if (result.value("ok").toBool() && stage_ == QStringLiteral("executing"))
    setStage(stage_, QStringLiteral("Stop requested. Waiting for the task's active operation to settle."));
  return result;
}

QVariantMap AgentSessionController::requestRevert() {
  if (!execution_control_handler_ || stage_ == QStringLiteral("idle") ||
      stage_ == QStringLiteral("clarifying") || stage_ == QStringLiteral("planning") ||
      stage_ == QStringLiteral("plan-ready"))
    return {{"ok", false}, {"error", "There is no approved task to revert."}};
  // Revert may request cancellation but cannot declare restoration complete.
  // Only the resource owner's settled receipt changes the conversation stage.
  const auto result = execution_control_handler_(QStringLiteral("revert"));
  revert_request_pending_ = result.value(QStringLiteral("ok")).toBool() &&
                            (result.value(QStringLiteral("pending")).toBool() ||
                             stage_ == QStringLiteral("executing"));
  emit stateChanged();
  return result;
}

void AgentSessionController::setExecutionControlHandler(ExecutionControlHandler handler) {
  execution_control_handler_ = std::move(handler);
}

void AgentSessionController::setRevertAvailabilityHandler(RevertAvailabilityHandler handler) {
  revert_availability_handler_ = std::move(handler);
  emit stateChanged();
}

void AgentSessionController::notifyRevertAvailabilityChanged() {
  emit stateChanged();
}

void AgentSessionController::reportExecutionActivity(const QString& activity) {
  if (stage_ != QStringLiteral("executing") || activity.trimmed().isEmpty()) return;
  activity_ = activity;
  emit activityChanged();
  emit activityReceived(activity);
}

QVariantMap AgentSessionController::resolvePreparedCanvasTarget(const QString& task_id, const QString& asset_id) {
  if (stage_ != QStringLiteral("executing") || task_id.trimmed().isEmpty() || asset_id.trimmed().isEmpty() ||
      !last_approved_snapshot_.value(QStringLiteral("blankCanvas")).toBool() ||
      !normalized_ids(last_approved_snapshot_.value(QStringLiteral("targetIds"))).isEmpty() ||
      !context_snapshot_handler_)
    return {{"ok", false}, {"error", "Only the approved blank canvas can acquire its newly created target."}};
  auto resolved_context = raw_context_;
  resolved_context.insert(QStringLiteral("targetIds"), QStringList{asset_id});
  resolved_context.insert(QStringLiteral("blankCanvas"), false);
  resolved_context.remove(QStringLiteral("targetSnapshot"));
  const auto fresh = normalizeContext(resolved_context);
  if (context_target_ids(fresh) != QStringList{asset_id} ||
      fresh.value(QStringLiteral("blankCanvas")).toBool() ||
      !fresh.value(QStringLiteral("contextReady")).toBool() ||
      !fresh.value(QStringLiteral("contextError")).toString().trimmed().isEmpty())
    return {{"ok", false}, {"error", "The created canvas is not available in the authoritative catalog."}};
  raw_context_ = resolved_context;
  context_ = fresh;
  updateBriefFromContext();
  last_approved_snapshot_.insert(QStringLiteral("targetIds"), QStringList{asset_id});
  last_approved_snapshot_.insert(QStringLiteral("targetSnapshot"), currentTargetSnapshot());
  last_approved_snapshot_.insert(QStringLiteral("context"), fresh);
  last_approved_snapshot_.insert(QStringLiteral("blankCanvas"), false);
  last_approved_snapshot_.insert(QStringLiteral("preparedTaskId"), task_id);
  last_approved_snapshot_.insert(QStringLiteral("createdAssetId"), asset_id);
  task_origin_snapshot_.insert(QStringLiteral("targetIds"), QStringList{asset_id});
  task_origin_snapshot_.insert(QStringLiteral("blankCanvas"), false);
  emit stateChanged();
  emit preparedTargetResolved(asset_id);
  return {{"ok", true}, {"approvedSnapshot", last_approved_snapshot_}};
}

void AgentSessionController::reset() {
  if (stage_ == QStringLiteral("executing")) return;
  invalidatePlanning();
  ++brief_version_;
  revert_request_pending_ = false;
  stage_ = QStringLiteral("idle");
  brief_.clear();
  context_.clear();
  raw_context_.clear();
  plan_.clear();
  last_execution_report_.clear();
  last_approved_snapshot_.clear();
  task_origin_snapshot_.clear();
  revision_session_ = false;
  questions_.clear();
  prior_questions_.clear();
  status_.clear();
  activity_.clear();
  emit activityChanged();
  emit stateChanged();
}

void AgentSessionController::acknowledgeExecution(const QVariantMap& report) {
  const auto report_status = report.value(QStringLiteral("status")).toString().trimmed().toLower();
  const auto report_transaction_status = report.value(QStringLiteral("taskTransaction")).toMap()
                                            .value(QStringLiteral("status")).toString().trimmed().toLower();
  const auto reverted = report.value(QStringLiteral("reverted")).toBool() ||
                        report_status == QStringLiteral("reverted") ||
                        report_transaction_status == QStringLiteral("reverted");
  if ((stage_ == QStringLiteral("review") || stage_ == QStringLiteral("interrupted") ||
       stage_ == QStringLiteral("error")) && reverted) {
    revert_request_pending_ = false;
    last_execution_report_.clear();
    last_approved_snapshot_.clear();
    task_origin_snapshot_.clear();
    revision_session_ = false;
    updateBriefFromContext();
    setStage(QStringLiteral("interrupted"),
             QStringLiteral("The task was reverted. Your prompt and answers are saved if you want to plan another attempt."));
    return;
  }
  if (stage_ != QStringLiteral("executing")) {
    if (revert_request_pending_ &&
        !(report.value(QStringLiteral("queued")).toBool() &&
          report.value(QStringLiteral("running")).toBool())) {
      revert_request_pending_ = false;
      emit stateChanged();
    }
    return;
  }
  if (report.value(QStringLiteral("queued")).toBool() &&
      report.value(QStringLiteral("running")).toBool()) {
    status_ = QStringLiteral("The approved task is running.");
    emit stateChanged();
    return;
  }
  revert_request_pending_ = false;
  const auto transaction_status = report_transaction_status;
  const auto interrupted = report.value(QStringLiteral("cancelled")).toBool() ||
                           !report.value(QStringLiteral("cancellationReason")).toString().trimmed().isEmpty() ||
                           report.value(QStringLiteral("canceledBeforeExecution")).toBool() ||
                           report_status == QStringLiteral("cancelled") ||
                           report_status == QStringLiteral("stopped") ||
                           report_status == QStringLiteral("interrupted") ||
                           transaction_status == QStringLiteral("interrupted") ||
                           transaction_status == QStringLiteral("reverted");
  const auto successful = !interrupted && (report.value(QStringLiteral("ok")).toBool() ||
                          report.value(QStringLiteral("success")).toBool() ||
                          report_status == QStringLiteral("completed") || report_status == QStringLiteral("review") ||
                          transaction_status == QStringLiteral("review"));
  auto message = report.value(QStringLiteral("headline")).toString().trimmed();
  if (message.isEmpty()) message = report.value(QStringLiteral("summary")).toString().trimmed();
  if (message.isEmpty()) message = report.value(QStringLiteral("failureReason")).toString().trimmed();
  if (message.isEmpty()) message = report.value(QStringLiteral("errors")).toStringList().join(QLatin1Char(' ')).trimmed();
  if (message.isEmpty()) {
    message = successful ? QStringLiteral("The task is ready for review.")
              : interrupted ? QStringLiteral("The task stopped. You can review its current state.")
                            : report.value(QStringLiteral("error")).toString().trimmed();
  }
  if (successful) {
    last_execution_report_ = report;
    if (last_approved_snapshot_.isEmpty()) {
      last_approved_snapshot_ = QVariantMap{{QStringLiteral("targetIds"), brief_.value(QStringLiteral("targetIds"))},
          {QStringLiteral("projectId"), brief_.value(QStringLiteral("projectId"))},
          {QStringLiteral("blankCanvas"), brief_.value(QStringLiteral("blankCanvas"))}};
    }
    setStage(QStringLiteral("review"), message);
  } else if (interrupted) {
    if (transaction_status == QStringLiteral("reverted") ||
        report.value(QStringLiteral("canceledBeforeExecution")).toBool()) {
      last_execution_report_.clear();
      last_approved_snapshot_.clear();
      task_origin_snapshot_.clear();
      revision_session_ = false;
      updateBriefFromContext();
    }
    setStage(QStringLiteral("interrupted"), message);
  } else {
    setStage(QStringLiteral("error"), message.isEmpty()
        ? QStringLiteral("The task could not be completed. Review the report and retry.") : message);
  }
}

void AgentSessionController::setContextSnapshotHandler(ContextSnapshotHandler handler) {
  context_snapshot_handler_ = std::move(handler);
  if (stage_ == QStringLiteral("idle")) {
    context_ = normalizeContext(raw_context_);
    emit stateChanged();
    return;
  }
  if (stage_ == QStringLiteral("executing") || stage_ == QStringLiteral("review")) return;
  context_ = normalizeContext(raw_context_);
  ++brief_version_;
  updateBriefFromContext();
  plan_.clear();
  questions_.clear();
  launchPlanning();
}

}  // namespace photo_editor::desktop
