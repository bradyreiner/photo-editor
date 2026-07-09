#include "qml_performance_monitor.h"

#include <QDateTime>

#include "observability.h"

namespace photo_editor::desktop {
namespace {

QString normalized_surface(QString surface) {
  surface = surface.trimmed().toLower();
  return surface.isEmpty() ? QStringLiteral("unknown") : surface;
}

QString workflow_id_for_surface(const QString& surface, const QString& workflow_id) {
  const auto normalized_workflow_id = workflow_id.trimmed();
  if (!normalized_workflow_id.isEmpty()) {
    return normalized_workflow_id;
  }
  return QStringLiteral("qml-route:%1").arg(normalized_surface(surface));
}

}  // namespace

QmlPerformanceMonitor::QmlPerformanceMonitor(QObject* parent) : QObject(parent) {}

qint64 QmlPerformanceMonitor::nowMs() const {
  return QDateTime::currentMSecsSinceEpoch();
}

void QmlPerformanceMonitor::routeStarted(
    const QString& surface,
    int route_index,
    const QString& workflow_id) {
  observability::logInfo(
      photoEditorPerfLog(),
      QStringLiteral("qml.route.started"),
      routeFields(surface, route_index, workflow_id));
}

void QmlPerformanceMonitor::routeReady(
    const QString& surface,
    int route_index,
    qint64 started_at_ms,
    qint64 component_created_at_ms,
    bool prewarmed,
    const QString& workflow_id) {
  const auto now = nowMs();
  constexpr auto kMaximumReasonableRouteElapsedMs = qint64{10 * 60 * 1000};
  const auto valid_start =
      started_at_ms > 0 &&
      started_at_ms <= now &&
      now - started_at_ms <= kMaximumReasonableRouteElapsedMs;
  const auto elapsed_ms = valid_start ? now - started_at_ms : 0;
  const auto component_creation_ms =
      component_created_at_ms > 0 && valid_start && component_created_at_ms >= started_at_ms
          ? qMax<qint64>(0, component_created_at_ms - started_at_ms)
          : 0;
  observability::logInfo(
      photoEditorPerfLog(),
      QStringLiteral("qml.route.ready"),
      routeReadyFields(
          surface,
          route_index,
          elapsed_ms,
          component_creation_ms,
          prewarmed,
          workflow_id));
}

void QmlPerformanceMonitor::frameSampled(
    const QString& surface,
    int route_index,
    double frame_interval_ms,
    bool janky,
    bool dropped) {
  observability::logInfo(
      photoEditorPerfLog(),
      QStringLiteral("qml.frame.sampled"),
      frameSampleFields(surface, route_index, frame_interval_ms, janky, dropped));
}

void QmlPerformanceMonitor::frameSummary(
    const QString& surface,
    int route_index,
    int sample_count,
    double max_frame_interval_ms,
    int jank_frame_count,
    int dropped_frame_count) {
  observability::logInfo(
      photoEditorPerfLog(),
      QStringLiteral("qml.frame.summary"),
      frameSummaryFields(
          surface,
          route_index,
          sample_count,
          max_frame_interval_ms,
          jank_frame_count,
          dropped_frame_count));
}

void QmlPerformanceMonitor::previewFailurePresented(
    const QString& surface,
    const QString& status,
    const QString& failure_stage,
    const QString& failure_detail,
    const QString& presentation_signature,
    bool processing,
    const QString& source) {
  observability::logWarning(
      photoEditorPreviewLog(),
      QStringLiteral("preview.failure.presented"),
      previewFailureFields(
          surface,
          status,
          failure_stage,
          failure_detail,
          presentation_signature,
          processing,
          source));
}

QVariantMap QmlPerformanceMonitor::routeFields(
    const QString& surface,
    int route_index,
    const QString& workflow_id) {
  const auto normalized = normalized_surface(surface);
  return {
      {QStringLiteral("workflow"), QStringLiteral("qml_route")},
      {QStringLiteral("workflow_id"), workflow_id_for_surface(normalized, workflow_id)},
      {QStringLiteral("surface"), normalized},
      {QStringLiteral("route"), normalized},
      {QStringLiteral("route_index"), route_index},
  };
}

QVariantMap QmlPerformanceMonitor::routeReadyFields(
    const QString& surface,
    int route_index,
    qint64 elapsed_ms,
    qint64 component_creation_ms,
    bool prewarmed,
    const QString& workflow_id) {
  auto fields = routeFields(surface, route_index, workflow_id);
  fields.insert(QStringLiteral("elapsed_ms"), elapsed_ms);
  fields.insert(QStringLiteral("component_creation_ms"), component_creation_ms);
  fields.insert(QStringLiteral("prewarmed"), prewarmed);
  return fields;
}

QVariantMap QmlPerformanceMonitor::frameSampleFields(
    const QString& surface,
    int route_index,
    double frame_interval_ms,
    bool janky,
    bool dropped) {
  const auto normalized = normalized_surface(surface);
  return {
      {QStringLiteral("workflow"), QStringLiteral("qml_frame")},
      {QStringLiteral("workflow_id"), QStringLiteral("qml-frame:%1").arg(normalized)},
      {QStringLiteral("surface"), normalized},
      {QStringLiteral("route"), normalized},
      {QStringLiteral("route_index"), route_index},
      {QStringLiteral("frame_interval_ms"), frame_interval_ms},
      {QStringLiteral("janky"), janky},
      {QStringLiteral("dropped"), dropped},
      {QStringLiteral("jank_frame_count"), janky ? 1 : 0},
      {QStringLiteral("dropped_frame_count"), dropped ? 1 : 0},
  };
}

QVariantMap QmlPerformanceMonitor::frameSummaryFields(
    const QString& surface,
    int route_index,
    int sample_count,
    double max_frame_interval_ms,
    int jank_frame_count,
    int dropped_frame_count) {
  const auto normalized = normalized_surface(surface);
  return {
      {QStringLiteral("workflow"), QStringLiteral("qml_frame")},
      {QStringLiteral("workflow_id"), QStringLiteral("qml-frame:%1").arg(normalized)},
      {QStringLiteral("surface"), normalized},
      {QStringLiteral("route"), normalized},
      {QStringLiteral("route_index"), route_index},
      {QStringLiteral("sample_count"), sample_count},
      {QStringLiteral("frame_interval_ms"), max_frame_interval_ms},
      {QStringLiteral("max_frame_interval_ms"), max_frame_interval_ms},
      {QStringLiteral("jank_frame_count"), jank_frame_count},
      {QStringLiteral("dropped_frame_count"), dropped_frame_count},
  };
}

QVariantMap QmlPerformanceMonitor::previewFailureFields(
    const QString& surface,
    const QString& status,
    const QString& failure_stage,
    const QString& failure_detail,
    const QString& presentation_signature,
    bool processing,
    const QString& source) {
  const auto normalized = normalized_surface(surface);
  const auto normalized_source = source.trimmed().isEmpty()
                                     ? QStringLiteral("unknown")
                                     : source.trimmed();
  const auto normalized_failure_stage = failure_stage.trimmed();
  const auto normalized_failure_detail = failure_detail.trimmed();
  return {
      {QStringLiteral("workflow"), QStringLiteral("preview")},
      {QStringLiteral("workflow_id"), QStringLiteral("preview-failure:%1").arg(normalized)},
      {QStringLiteral("surface"), normalized},
      {QStringLiteral("route"), normalized},
      {QStringLiteral("status"), status.trimmed()},
      {QStringLiteral("failure_stage"), normalized_failure_stage},
      {QStringLiteral("failure_detail_present"), !normalized_failure_detail.isEmpty()},
      {QStringLiteral("presentation_signature"), presentation_signature},
      {QStringLiteral("processing"), processing},
      {QStringLiteral("source"), normalized_source},
  };
}

}  // namespace photo_editor::desktop
