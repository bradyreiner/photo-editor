#pragma once

#include <QVariantMap>
#include <QString>
#include <QStringList>

class QProcess;

namespace photo_editor::desktop {

class WorkerQueueRegistry final {
 public:
  static QStringList registeredPoolIds();
  static bool isRegisteredPool(const QString& pool_id);

  // Publishes the currently visible editing surface and its latency-sensitive
  // intent. Background producers query the same policy before taking work so
  // playback and direct manipulation win without each queue inventing its own
  // meaning of "high priority".
  static void publishUserIntent(
      const QString& surface,
      bool workspace_visible,
      bool playback_active,
      bool interaction_active,
      const QStringList& visible_item_ids = {},
      const QString& focused_item_id = QString());
  static void clearUserIntent(const QString& surface);
  static QVariantMap userIntentSnapshot();
  static QVariantMap priorityDecision(
      const QString& workflow,
      const QString& surface,
      const QString& affected_item_id = QString(),
      const QString& requested_priority = QStringLiteral("normal"));
  static bool shouldDefer(
      const QString& workflow,
      const QString& surface,
      const QString& affected_item_id = QString());
  static void configureBackgroundProcess(
      QProcess* process,
      const QString& workflow,
      int niceness = 10);

  static QVariantMap sampleFields(
      const QString& pool_id,
      const QString& workflow,
      const QString& surface,
      int active_worker_count,
      int pending_worker_count,
      int worker_limit,
      qint64 queue_wait_ms = -1,
      int completed_count = 0,
      int cancelled_count = 0);

  static QVariantMap summaryFields(
      const QString& pool_id,
      const QString& workflow,
      const QString& surface,
      int active_worker_count,
      int pending_worker_count,
      int worker_limit,
      qint64 queue_wait_ms,
      qint64 worker_elapsed_ms,
      int completed_count,
      int cancelled_count);

  static void logSample(
      const QString& pool_id,
      const QString& workflow,
      const QString& surface,
      int active_worker_count,
      int pending_worker_count,
      int worker_limit,
      qint64 queue_wait_ms = -1,
      int completed_count = 0,
      int cancelled_count = 0);

  static void logSummary(
      const QString& pool_id,
      const QString& workflow,
      const QString& surface,
      int active_worker_count,
      int pending_worker_count,
      int worker_limit,
      qint64 queue_wait_ms,
      qint64 worker_elapsed_ms,
      int completed_count,
      int cancelled_count);
};

}  // namespace photo_editor::desktop
