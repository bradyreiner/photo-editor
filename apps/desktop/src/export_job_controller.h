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
#include <QTimer>
#include <QElapsedTimer>
#include <functional>
#include <memory>
#include <atomic>

class QThreadPool;

#include "job_store.h"

namespace photo_editor::desktop {

struct ExportPreflightRequest;
struct ExportPreflightCache;

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
  Q_PROPERTY(QVariantMap singlePreflight READ singlePreflight NOTIFY preflightChanged)
  Q_PROPERTY(QVariantMap batchPreflight READ batchPreflight NOTIFY preflightChanged)
  Q_PROPERTY(bool preflightBusy READ preflightBusy NOTIFY preflightChanged)
  Q_PROPERTY(bool batchSubmissionBusy READ batchSubmissionBusy NOTIFY batchSubmissionChanged)

 public:
  explicit ExportJobController(QObject* parent = nullptr);
  ~ExportJobController() override;
  [[nodiscard]] QVariantMap singlePreflight() const { return single_preflight_; }
  [[nodiscard]] QVariantMap batchPreflight() const { return batch_preflight_; }
  [[nodiscard]] bool preflightBusy() const { return preflight_busy_; }
  Q_INVOKABLE void requestPreflight(const QVariantMap& request, bool force = false);
  Q_INVOKABLE void cancelPreflight();
  [[nodiscard]] bool batchSubmissionBusy() const { return batch_submission_busy_; }
  // Accepts validation work; batchSubmissionFinished reports jobs submitted to
  // the existing durable-admission path. A true return does not mean saved.
  Q_INVOKABLE bool queueBatchExportAsync(
      const QVariantList& assets, const QString& preset, const QString& destination_directory,
      const QString& crop_preset, double crop_x, double crop_y, double crop_width, double crop_height);
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
  void preflightChanged();
  void batchSubmissionChanged();
  void batchSubmissionFinished(int queued, int requested);

 private:
  friend struct ExportJobControllerTestAccess;
  friend struct ExportPreflightTestAccess;
  static QThreadPool& preflightThreadPool();
  void startPreflight();
  void finishPreflight();
  QFutureWatcher<QVariantMap> preflight_watcher_;
  QTimer preflight_timer_;
  QElapsedTimer preflight_age_;
  std::shared_ptr<ExportPreflightRequest> active_preflight_;
  std::shared_ptr<ExportPreflightRequest> pending_preflight_;
  std::shared_ptr<ExportPreflightCache> preflight_cache_;
  QVariantMap preflight_input_;
  QVariantMap single_preflight_;
  QVariantMap batch_preflight_;
  quint64 preflight_generation_ = 0;
  int preflight_source_build_count_ = 0;
  bool preflight_busy_ = false;
  std::shared_ptr<std::atomic_bool> batch_submission_cancellation_;
  bool batch_submission_busy_ = false;
  int queueBatchExportWithPlan(
      const QVariantList& assets, const QString& preset, const QString& destination_directory,
      const QString& crop_preset, double crop_x, double crop_y, double crop_width, double crop_height,
      const QVariantMap& report);
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
