#pragma once

#include <QObject>
#include <QJsonObject>
#include <QString>
#include <QVariantMap>
#include <QVariantList>
#include <QVector>
#include <QFutureWatcher>
#include <QHash>
#include <QSet>
#include <functional>

#include "job_store.h"

namespace photo_editor::desktop {

struct ExportHistoryReadResult {
  QVector<PersistentJobRecord> records;
  QString error;
};

struct ExportAdmissionResult {
  PersistentJobRecord record;
  QString error;
};

class ExportJobController final : public QObject {
  Q_OBJECT
  Q_PROPERTY(QVariantList jobs READ jobs NOTIFY jobsChanged)
  Q_PROPERTY(int jobCount READ jobCount NOTIFY jobsChanged)
  Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
  Q_PROPERTY(QString status READ status NOTIFY statusChanged)
  Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
  Q_PROPERTY(QString historyError READ historyError NOTIFY loadingChanged)
  Q_PROPERTY(bool historyMutationsEnabled READ historyMutationsEnabled NOTIFY startupRecoveryErrorChanged)
  Q_PROPERTY(int pendingAdmissionCount READ pendingAdmissionCount NOTIFY admissionChanged)
  Q_PROPERTY(QString admissionError READ admissionError NOTIFY admissionChanged)
  Q_PROPERTY(QString submissionError READ submissionError NOTIFY admissionChanged)

 public:
  explicit ExportJobController(QObject* parent = nullptr);
  // Shutdown only, after the event loop stops but before QApplication teardown.
  // Controller destruction itself remains nonblocking.
  static void finishPendingAdmissions();

  [[nodiscard]] QVariantList jobs() const;
  [[nodiscard]] int jobCount() const;
  [[nodiscard]] bool busy() const;
  [[nodiscard]] QString status() const;
  [[nodiscard]] bool loading() const { return history_loading_; }
  [[nodiscard]] QString historyError() const { return history_error_; }
  [[nodiscard]] bool historyMutationsEnabled() const { return startup_recovery_error_.isEmpty(); }
  void setStartupRecoveryError(const QString& error);
  [[nodiscard]] int pendingAdmissionCount() const { return static_cast<int>(admitting_ids_.size()); }
  [[nodiscard]] QString admissionError() const;
  [[nodiscard]] QString submissionError() const { return submission_error_; }
  Q_INVOKABLE void retryUnconfirmedExports();

  Q_INVOKABLE QString defaultDestinationDirectory() const;
  Q_INVOKABLE QString pickDestinationFolder(
      const QString& title = QStringLiteral("Choose Export Folder")) const;
  // Return values count accepted in-memory requests, not durable catalog rows.
  // Observe admissionFinished/pendingAdmissionCount for catalog confirmation.
  Q_INVOKABLE bool queueExport(
      const QString& asset_id,
      const QString& media_type,
      const QString& display_label,
      const QString& source_path,
      const QString& edited_preview_source,
      const QString& recipe_json,
      const QString& preset,
      const QString& destination_directory);
  Q_INVOKABLE bool queueExportWithCropConstraint(
      const QString& asset_id,
      const QString& media_type,
      const QString& display_label,
      const QString& source_path,
      const QString& edited_preview_source,
      const QString& recipe_json,
      const QString& preset,
      const QString& destination_directory,
      const QString& crop_preset,
      double crop_x,
      double crop_y,
      double crop_width,
      double crop_height);
  Q_INVOKABLE int queueBatchExport(
      const QVariantList& assets,
      const QString& preset,
      const QString& destination_directory);
  Q_INVOKABLE int queueBatchExportWithCropConstraint(
      const QVariantList& assets,
      const QString& preset,
      const QString& destination_directory,
      const QString& crop_preset,
      double crop_x,
      double crop_y,
      double crop_width,
      double crop_height);
  Q_INVOKABLE QVariantMap exportPreflight(
      const QString& media_type,
      const QString& source_path,
      const QString& edited_preview_source,
      const QString& recipe_json,
      const QString& preset,
      const QString& destination_directory,
      int histogram_sample_count,
      int shadow_clip_count,
      int highlight_clip_count,
      int raw_clip_sample_count,
      int raw_shadow_clip_count,
      int raw_highlight_clip_count) const;
  Q_INVOKABLE QVariantMap batchExportPreflight(
      const QVariantList& assets,
      const QString& preset,
      const QString& destination_directory) const;
  Q_INVOKABLE QString revealTarget(const QString& job_id) const;
  Q_INVOKABLE bool revealExport(const QString& job_id) const;
  Q_INVOKABLE void retryJob(const QString& job_id);
  Q_INVOKABLE void removeJob(const QString& job_id);
  Q_INVOKABLE void clearFinishedJobs();
  Q_INVOKABLE void initializeFromStore();

 signals:
  void jobsChanged();
  void busyChanged();
  void statusChanged();
  void loadingChanged();
  void startupRecoveryErrorChanged();
  void admissionChanged();
  void admissionFinished(const QString& job_id, bool confirmed, const QString& error);

 private:
  friend struct ExportJobControllerTestAccess;
  struct ExportJob final {
    QString job_id;
    QString asset_id;
    QString media_type;
    QString display_label;
    QString source_path;
    QString edited_preview_source;
    QString recipe_json;
    QJsonObject metadata_json;
    QString preset;
    QString destination_directory;
    QString output_path;
    QString status = QStringLiteral("Queued");
    QString error_message;
    QString created_at;
    QString updated_at;
    int progress = 0;
  };

  [[nodiscard]] QVariantMap jobMap(const ExportJob& job) const;
  [[nodiscard]] bool queuePlannedExportWithCropConstraint(
      const QString& asset_id,
      const QString& media_type,
      const QString& display_label,
      const QString& source_path,
      const QString& edited_preview_source,
      const QString& recipe_json,
      const QString& preset,
      const QString& destination_directory,
      const QString& crop_preset,
      double crop_x,
      double crop_y,
      double crop_width,
      double crop_height,
      const QString& planned_output_path,
      const QJsonObject& delivery_plan);
  [[nodiscard]] int jobIndexById(const QString& job_id) const;
  [[nodiscard]] QString resolvedSourcePath(const ExportJob& job, bool* uses_preview) const;
  [[nodiscard]] ExportJob jobFromRecord(const PersistentJobRecord& record) const;
  void loadPersistedJobs(bool emit_signal = true);
  void applyHistory(const ExportHistoryReadResult& result, bool emit_signal = true);
  void startHistoryLoad();
  [[nodiscard]] static PersistentJobRecord recordFromJob(const ExportJob& job);
  void startAdmission(const ExportJob& job);
  bool rejectSubmission(const QString& error);
  void removePersistedJob(const QString& job_id) const;
  void refreshJobs();
  void updateBusyState();
  void setBusy(bool busy);
  void setStatus(const QString& status);

  QVector<ExportJob> jobs_;
  // Values remain local until the selected catalog confirms them. Limit pending
  // payloads so an unavailable catalog cannot grow an unbounded in-memory queue.
  QHash<QString, ExportJob> unconfirmed_jobs_;
  QSet<QString> admitting_ids_;
  std::function<ExportAdmissionResult(PersistentJobRecord)> admission_writer_;
  QFutureWatcher<ExportHistoryReadResult> history_watcher_;
  std::function<ExportHistoryReadResult()> history_reader_;
  bool history_loading_ = false;
  bool history_pending_ = false;
  QString history_error_;
  bool busy_ = false;
  QString status_ = QStringLiteral("Export queue ready.");
  QString startup_recovery_error_;
  QString submission_error_;
};

}  // namespace photo_editor::desktop
