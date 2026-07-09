#pragma once

#include <QObject>
#include <QString>
#include <QVariantMap>

namespace photo_editor::desktop {

class QmlPerformanceMonitor final : public QObject {
  Q_OBJECT

 public:
  explicit QmlPerformanceMonitor(QObject* parent = nullptr);

  Q_INVOKABLE qint64 nowMs() const;
  Q_INVOKABLE void routeStarted(
      const QString& surface,
      int route_index,
      const QString& workflow_id = {});
  Q_INVOKABLE void routeReady(
      const QString& surface,
      int route_index,
      qint64 started_at_ms,
      qint64 component_created_at_ms = 0,
      bool prewarmed = false,
      const QString& workflow_id = {});
  Q_INVOKABLE void frameSampled(
      const QString& surface,
      int route_index,
      double frame_interval_ms,
      bool janky,
      bool dropped);
  Q_INVOKABLE void frameSummary(
      const QString& surface,
      int route_index,
      int sample_count,
      double max_frame_interval_ms,
      int jank_frame_count,
      int dropped_frame_count);
  Q_INVOKABLE void previewFailurePresented(
      const QString& surface,
      const QString& status,
      const QString& failure_stage,
      const QString& failure_detail,
      const QString& presentation_signature,
      bool processing,
      const QString& source);

  [[nodiscard]] static QVariantMap routeFields(
      const QString& surface,
      int route_index,
      const QString& workflow_id = {});
  [[nodiscard]] static QVariantMap routeReadyFields(
      const QString& surface,
      int route_index,
      qint64 elapsed_ms,
      qint64 component_creation_ms,
      bool prewarmed,
      const QString& workflow_id = {});
  [[nodiscard]] static QVariantMap frameSampleFields(
      const QString& surface,
      int route_index,
      double frame_interval_ms,
      bool janky,
      bool dropped);
  [[nodiscard]] static QVariantMap frameSummaryFields(
      const QString& surface,
      int route_index,
      int sample_count,
      double max_frame_interval_ms,
      int jank_frame_count,
      int dropped_frame_count);
  [[nodiscard]] static QVariantMap previewFailureFields(
      const QString& surface,
      const QString& status,
      const QString& failure_stage,
      const QString& failure_detail,
      const QString& presentation_signature,
      bool processing,
      const QString& source);
};

}  // namespace photo_editor::desktop
