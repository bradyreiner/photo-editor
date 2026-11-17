#include "worker_queue_registry.h"

#include <QDateTime>
#include <QMutex>
#include <QMutexLocker>
#include <QProcess>
#include <QSet>

#ifdef Q_OS_UNIX
#include <sys/resource.h>
#endif

#ifdef Q_OS_WIN
#define NOMINMAX
#include <windows.h>
#endif

#include "observability.h"

namespace photo_editor::desktop {
namespace {

QString normalized_token(QString value, const QString& fallback) {
  value = value.trimmed().toLower();
  return value.isEmpty() ? fallback : value;
}

QString normalized_pool_id(const QString& pool_id) {
  return normalized_token(pool_id, QStringLiteral("unknown"));
}

const QStringList& registered_pool_ids() {
  static const QStringList pools{
      QStringLiteral("preview-render"),
      QStringLiteral("overlay-render"),
      QStringLiteral("histogram-scope"),
      QStringLiteral("thumbnail"),
      QStringLiteral("mask-commit"),
      QStringLiteral("smart-selection"),
      QStringLiteral("inpaint"),
      QStringLiteral("export"),
      QStringLiteral("catalog-import"),
      QStringLiteral("metadata-extraction"),
      QStringLiteral("timeline-media"),
      QStringLiteral("carousel-render"),
  };
  return pools;
}

struct UserIntentState final {
  QString surface;
  bool workspace_visible = false;
  bool playback_active = false;
  bool interaction_active = false;
  bool transitioning = false;
  bool workspace_intent_authoritative = false;
  QStringList visible_item_ids;
  QString focused_item_id;
  qint64 generation = 0;
  QString updated_at;
};

QMutex& intent_mutex() {
  static QMutex mutex;
  return mutex;
}

UserIntentState& user_intent_state() {
  static UserIntentState state;
  return state;
}

QStringList normalized_unique_ids(const QStringList& ids) {
  QStringList normalized;
  QSet<QString> seen;
  for (const auto& id : ids) {
    const auto value = id.trimmed();
    if (!value.isEmpty() && !seen.contains(value)) {
      seen.insert(value);
      normalized.push_back(value);
    }
  }
  normalized.sort();
  return normalized;
}

bool is_foreground_workflow(const QString& workflow) {
  static const QSet<QString> foreground{
      QStringLiteral("playback"),
      QStringLiteral("program-preview"),
      QStringLiteral("interactive-preview"),
      QStringLiteral("media-hover"),
      QStringLiteral("direct-edit"),
  };
  return foreground.contains(workflow);
}

bool is_deferrable_background_workflow(const QString& workflow) {
  static const QSet<QString> background{
      QStringLiteral("proxy"),
      QStringLiteral("transcription"),
      QStringLiteral("thumbnail"),
      QStringLiteral("poster"),
      QStringLiteral("waveform"),
      QStringLiteral("analysis"),
      QStringLiteral("media-analysis"),
      QStringLiteral("media-integrity"),
      QStringLiteral("silence-analysis"),
      QStringLiteral("beat-analysis"),
      QStringLiteral("loudness-analysis"),
      QStringLiteral("render-cache"),
      QStringLiteral("export-preview"),
      QStringLiteral("compound-render"),
      QStringLiteral("stabilization"),
      QStringLiteral("auto-reframe"),
  };
  return background.contains(workflow);
}

int requested_priority_rank(const QString& priority) {
  if (priority == QStringLiteral("high")) {
    return 240;
  }
  if (priority == QStringLiteral("low")) {
    return 120;
  }
  return 180;
}

QVariantMap intent_map(const UserIntentState& state) {
  return {
      {QStringLiteral("surface"), state.surface},
      {QStringLiteral("workspaceVisible"), state.workspace_visible},
      {QStringLiteral("playbackActive"), state.playback_active},
      {QStringLiteral("interactionActive"), state.interaction_active},
      {QStringLiteral("transitioning"), state.transitioning},
      {QStringLiteral("workspaceIntentAuthoritative"),
       state.workspace_intent_authoritative},
      {QStringLiteral("latencySensitive"),
       state.workspace_visible &&
           (state.playback_active || state.interaction_active ||
            state.transitioning)},
      {QStringLiteral("visibleItemIds"), state.visible_item_ids},
      {QStringLiteral("focusedItemId"), state.focused_item_id},
      {QStringLiteral("generation"), state.generation},
      {QStringLiteral("updatedAt"), state.updated_at},
  };
}

}  // namespace

QStringList WorkerQueueRegistry::registeredPoolIds() {
  return registered_pool_ids();
}

bool WorkerQueueRegistry::isRegisteredPool(const QString& pool_id) {
  static const QSet<QString> pools(registered_pool_ids().cbegin(), registered_pool_ids().cend());
  return pools.contains(normalized_pool_id(pool_id));
}

namespace {

void publish_workspace_intent(
    const QString& surface,
    bool workspace_visible,
    bool playback_active,
    bool interaction_active,
    bool transitioning,
    const QStringList& visible_item_ids,
    const QString& focused_item_id,
    bool authoritative) {
  const auto next_surface = normalized_token(surface, QStringLiteral("background"));
  const auto next_workspace_visible = workspace_visible;
  const auto next_playback_active = workspace_visible && playback_active;
  const auto next_interaction_active = workspace_visible && interaction_active;
  const auto next_transitioning = workspace_visible && transitioning;
  const auto next_visible_item_ids = workspace_visible
                                         ? normalized_unique_ids(visible_item_ids)
                                         : QStringList{};
  const auto next_focused_item_id = workspace_visible ? focused_item_id.trimmed() : QString{};

  QMutexLocker locker(&intent_mutex());
  auto& state = user_intent_state();
  // A late hidden-workspace callback must not replace the visible editor intent.
  if (!workspace_visible && state.workspace_visible && state.surface != next_surface) return;
  if (state.surface == next_surface &&
      state.workspace_visible == next_workspace_visible &&
      state.playback_active == next_playback_active &&
      state.interaction_active == next_interaction_active &&
      state.transitioning == next_transitioning &&
      state.workspace_intent_authoritative == authoritative &&
      state.visible_item_ids == next_visible_item_ids &&
      state.focused_item_id == next_focused_item_id) {
    return;
  }
  state.surface = next_surface;
  state.workspace_visible = next_workspace_visible;
  state.playback_active = next_playback_active;
  state.interaction_active = next_interaction_active;
  state.transitioning = next_transitioning;
  state.workspace_intent_authoritative = authoritative;
  state.visible_item_ids = next_visible_item_ids;
  state.focused_item_id = next_focused_item_id;
  ++state.generation;
  state.updated_at = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
}

}  // namespace

void WorkerQueueRegistry::publishWorkspaceIntent(
    const QString& surface,
    bool workspace_visible,
    bool playback_active,
    bool interaction_active,
    bool transitioning,
    const QStringList& visible_item_ids,
    const QString& focused_item_id) {
  publish_workspace_intent(
      surface, workspace_visible, playback_active, interaction_active,
      transitioning, visible_item_ids, focused_item_id, true);
}

void WorkerQueueRegistry::publishUserIntent(
    const QString& surface,
    bool workspace_visible,
    bool playback_active,
    bool interaction_active,
    const QStringList& visible_item_ids,
    const QString& focused_item_id) {
  publish_workspace_intent(
      surface, workspace_visible, playback_active, interaction_active,
      false, visible_item_ids, focused_item_id, false);
}

void WorkerQueueRegistry::clearUserIntent(const QString& surface) {
  const auto normalized_surface = normalized_token(surface, QStringLiteral("background"));
  QMutexLocker locker(&intent_mutex());
  auto& state = user_intent_state();
  if (!state.surface.isEmpty() && state.surface != normalized_surface) {
    return;
  }
  if (state.surface.isEmpty() && !state.workspace_visible &&
      !state.playback_active && !state.interaction_active &&
      !state.transitioning && state.visible_item_ids.isEmpty() &&
      state.focused_item_id.isEmpty()) {
    return;
  }
  const auto next_generation = state.generation + 1;
  state = UserIntentState{};
  state.generation = next_generation;
  state.updated_at = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
}

QVariantMap WorkerQueueRegistry::userIntentSnapshot() {
  QMutexLocker locker(&intent_mutex());
  return intent_map(user_intent_state());
}

QVariantMap WorkerQueueRegistry::priorityDecision(
    const QString& workflow,
    const QString& surface,
    const QString& affected_item_id,
    const QString& requested_priority) {
  const auto normalized_workflow = normalized_token(workflow, QStringLiteral("background"));
  const auto normalized_surface = normalized_token(surface, QStringLiteral("background"));
  const auto normalized_affected_id = affected_item_id.trimmed();
  const auto normalized_priority = requested_priority.trimmed().toLower() == QStringLiteral("high")
                                       ? QStringLiteral("high")
                                       : requested_priority.trimmed().toLower() == QStringLiteral("low")
                                             ? QStringLiteral("low")
                                             : QStringLiteral("normal");
  UserIntentState intent;
  {
    QMutexLocker locker(&intent_mutex());
    intent = user_intent_state();
  }

  const auto latency_sensitive = intent.workspace_visible &&
                                 (intent.playback_active || intent.interaction_active ||
                                  intent.transitioning);
  const auto foreground = is_foreground_workflow(normalized_workflow);
  const auto background = is_deferrable_background_workflow(normalized_workflow);
  const auto inactive_timeline = intent.workspace_visible &&
                                 intent.workspace_intent_authoritative &&
                                 normalized_surface == QStringLiteral("timeline") &&
                                 intent.surface != QStringLiteral("timeline");
  const auto active_surface = intent.workspace_visible &&
                              normalized_surface == intent.surface;
  const auto focused = !normalized_affected_id.isEmpty() &&
                       normalized_affected_id == intent.focused_item_id;
  const auto visible = !normalized_affected_id.isEmpty() &&
                       intent.visible_item_ids.contains(normalized_affected_id);
  auto effective_rank = foreground ? 400 : requested_priority_rank(normalized_priority);
  auto effective_priority = foreground ? QStringLiteral("critical") : normalized_priority;
  auto reason = foreground
                    ? QStringLiteral("Direct playback or visible interaction")
                    : QStringLiteral("Manual queue priority");
  const auto defer_for_activity = intent.transitioning || intent.playback_active ||
                                  (intent.interaction_active &&
                                   intent.surface != QStringLiteral("library"));
  const auto deferred = background &&
                        (defer_for_activity || inactive_timeline);
  if (active_surface) {
    effective_rank += 40;
  }
  if (deferred) {
    effective_rank = qMin(effective_rank, 80);
    effective_priority = QStringLiteral("deferred");
    if (intent.transitioning) {
      reason = QStringLiteral("Waiting for the workspace transition");
    } else if (intent.playback_active) {
      reason = QStringLiteral("Waiting for playback");
    } else if (intent.interaction_active &&
               intent.surface != QStringLiteral("library")) {
      reason = QStringLiteral("Waiting for the active edit");
    } else {
      reason = QStringLiteral("Waiting until Timeline is active");
    }
  } else if (focused) {
    effective_rank += 40;
    reason = QStringLiteral("Affects the focused item");
  } else if (visible) {
    effective_rank += 20;
    reason = QStringLiteral("Affects a visible item");
  } else if (active_surface) {
    reason = QStringLiteral("Background work for the active workspace");
  }

  return {
      {QStringLiteral("workflow"), normalized_workflow},
      {QStringLiteral("surface"), normalized_surface},
      {QStringLiteral("affectedItemId"), normalized_affected_id},
      {QStringLiteral("requestedPriority"), normalized_priority},
      {QStringLiteral("effectivePriority"), effective_priority},
      {QStringLiteral("effectiveRank"), effective_rank},
      {QStringLiteral("deferred"), deferred},
      {QStringLiteral("transitioning"), intent.transitioning},
      {QStringLiteral("reason"), reason},
      {QStringLiteral("focused"), focused},
      {QStringLiteral("visible"), visible},
      {QStringLiteral("intentGeneration"), intent.generation},
      {QStringLiteral("latencySensitive"), latency_sensitive},
  };
}

bool WorkerQueueRegistry::shouldDefer(
    const QString& workflow,
    const QString& surface,
    const QString& affected_item_id) {
  return priorityDecision(workflow, surface, affected_item_id)
      .value(QStringLiteral("deferred")).toBool();
}

void WorkerQueueRegistry::configureBackgroundProcess(
    QProcess* process,
    const QString& workflow,
    int niceness) {
  if (process == nullptr) {
    return;
  }
  const auto normalized_workflow = normalized_token(
      workflow, QStringLiteral("background"));
  process->setProperty("backgroundWorkflow", normalized_workflow);
  process->setProperty("backgroundPriorityBounded", true);
  const auto bounded_niceness = qBound(0, niceness, 19);
  process->setProperty("backgroundNiceness", bounded_niceness);
  QObject::connect(
      process,
      &QProcess::started,
      process,
      [process, normalized_workflow, bounded_niceness]() {
        auto applied = false;
#ifdef Q_OS_UNIX
        const auto pid = process->processId();
        if (pid > 0) {
          applied = setpriority(
                        PRIO_PROCESS,
                        static_cast<id_t>(pid),
                        bounded_niceness) == 0;
        }
#elif defined(Q_OS_WIN)
        const auto pid = process->processId();
        if (pid > 0) {
          auto* handle = OpenProcess(
              PROCESS_SET_INFORMATION,
              FALSE,
              static_cast<DWORD>(pid));
          if (handle != nullptr) {
            applied = SetPriorityClass(
                          handle,
                          bounded_niceness == 0 ? NORMAL_PRIORITY_CLASS
                                                : BELOW_NORMAL_PRIORITY_CLASS) != 0;
            CloseHandle(handle);
          }
        }
#endif
        process->setProperty("backgroundPriorityApplied", applied);
        observability::logInfo(
            photoEditorPerfLog(),
            QStringLiteral("worker.priority.applied"),
            {
                {QStringLiteral("workflow"), normalized_workflow},
                {QStringLiteral("process_id"), process->processId()},
                {QStringLiteral("niceness"), bounded_niceness},
                {QStringLiteral("background_priority_applied"), applied},
            });
      });
}

QVariantMap WorkerQueueRegistry::sampleFields(
    const QString& pool_id,
    const QString& workflow,
    const QString& surface,
    int active_worker_count,
    int pending_worker_count,
    int worker_limit,
    qint64 queue_wait_ms,
    int completed_count,
    int cancelled_count) {
  const auto normalized_pool = normalized_pool_id(pool_id);
  return {
      {QStringLiteral("worker_pool"), normalized_pool},
      {QStringLiteral("pool_id"), normalized_pool},
      {QStringLiteral("workflow"), normalized_token(workflow, normalized_pool)},
      {QStringLiteral("surface"), normalized_token(surface, QStringLiteral("background"))},
      {QStringLiteral("active_worker_count"), qMax(0, active_worker_count)},
      {QStringLiteral("pending_worker_count"), qMax(0, pending_worker_count)},
      {QStringLiteral("worker_limit"), qMax(0, worker_limit)},
      {QStringLiteral("queue_wait_ms"), qMax<qint64>(0, queue_wait_ms)},
      {QStringLiteral("completed_count"), qMax(0, completed_count)},
      {QStringLiteral("cancelled_count"), qMax(0, cancelled_count)},
      {QStringLiteral("registered_pool"), isRegisteredPool(normalized_pool)},
  };
}

QVariantMap WorkerQueueRegistry::summaryFields(
    const QString& pool_id,
    const QString& workflow,
    const QString& surface,
    int active_worker_count,
    int pending_worker_count,
    int worker_limit,
    qint64 queue_wait_ms,
    qint64 worker_elapsed_ms,
    int completed_count,
    int cancelled_count) {
  auto fields = sampleFields(
      pool_id,
      workflow,
      surface,
      active_worker_count,
      pending_worker_count,
      worker_limit,
      queue_wait_ms,
      completed_count,
      cancelled_count);
  fields.insert(QStringLiteral("worker_elapsed_ms"), qMax<qint64>(0, worker_elapsed_ms));
  return fields;
}

void WorkerQueueRegistry::logSample(
    const QString& pool_id,
    const QString& workflow,
    const QString& surface,
    int active_worker_count,
    int pending_worker_count,
    int worker_limit,
    qint64 queue_wait_ms,
    int completed_count,
    int cancelled_count) {
  observability::logInfo(
      photoEditorPerfLog(),
      QStringLiteral("worker.queue.sampled"),
      sampleFields(
          pool_id,
          workflow,
          surface,
          active_worker_count,
          pending_worker_count,
          worker_limit,
          queue_wait_ms,
          completed_count,
          cancelled_count));
}

void WorkerQueueRegistry::logSummary(
    const QString& pool_id,
    const QString& workflow,
    const QString& surface,
    int active_worker_count,
    int pending_worker_count,
    int worker_limit,
    qint64 queue_wait_ms,
    qint64 worker_elapsed_ms,
    int completed_count,
    int cancelled_count) {
  observability::logInfo(
      photoEditorPerfLog(),
      QStringLiteral("worker.queue.summary"),
      summaryFields(
          pool_id,
          workflow,
          surface,
          active_worker_count,
          pending_worker_count,
          worker_limit,
          queue_wait_ms,
          worker_elapsed_ms,
          completed_count,
          cancelled_count));
}

}  // namespace photo_editor::desktop
