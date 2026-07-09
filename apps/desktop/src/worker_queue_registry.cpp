#include "worker_queue_registry.h"

#include <QSet>

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

}  // namespace

QStringList WorkerQueueRegistry::registeredPoolIds() {
  return registered_pool_ids();
}

bool WorkerQueueRegistry::isRegisteredPool(const QString& pool_id) {
  static const QSet<QString> pools(registered_pool_ids().cbegin(), registered_pool_ids().cend());
  return pools.contains(normalized_pool_id(pool_id));
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
