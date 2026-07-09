#pragma once

#include <QVariantMap>
#include <QString>
#include <QStringList>

namespace photo_editor::desktop {

class WorkerQueueRegistry final {
 public:
  static QStringList registeredPoolIds();
  static bool isRegisteredPool(const QString& pool_id);

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
