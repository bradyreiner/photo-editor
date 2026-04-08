#pragma once

#include <QAbstractListModel>
#include <QVector>

#include "job_store.h"

namespace photo_editor::desktop {

class JobQueueModel final : public QAbstractListModel {
  Q_OBJECT
  Q_PROPERTY(int jobCount READ jobCount NOTIFY jobsChanged)
  Q_PROPERTY(int activeJobCount READ activeJobCount NOTIFY jobsChanged)
  Q_PROPERTY(int failedJobCount READ failedJobCount NOTIFY jobsChanged)
  Q_PROPERTY(int completedJobCount READ completedJobCount NOTIFY jobsChanged)
  Q_PROPERTY(bool busy READ busy NOTIFY jobsChanged)
  Q_PROPERTY(bool hasFailures READ hasFailures NOTIFY jobsChanged)
  Q_PROPERTY(QString status READ status NOTIFY statusChanged)

 public:
  enum Roles {
    JobIdRole = Qt::UserRole + 1,
    JobKindRole,
    AssetIdRole,
    MediaTypeRole,
    DisplayLabelRole,
    SourcePathRole,
    PresetNameRole,
    DestinationPathRole,
    OutputPathRole,
    StatusRole,
    ErrorMessageRole,
    ProgressRole,
    CreatedAtRole,
    UpdatedAtRole,
    MetadataRole,
  };

  explicit JobQueueModel(QObject* parent = nullptr);

  [[nodiscard]] int rowCount(const QModelIndex& parent = QModelIndex()) const override;
  [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
  [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

  [[nodiscard]] int jobCount() const;
  [[nodiscard]] int activeJobCount() const;
  [[nodiscard]] int failedJobCount() const;
  [[nodiscard]] int completedJobCount() const;
  [[nodiscard]] bool busy() const;
  [[nodiscard]] bool hasFailures() const;
  [[nodiscard]] QString status() const;

  Q_INVOKABLE void refresh();
  Q_INVOKABLE int activeJobCountForKind(const QString& job_kind) const;
  Q_INVOKABLE int failedJobCountForKind(const QString& job_kind) const;
  Q_INVOKABLE int totalJobCountForKind(const QString& job_kind) const;
  Q_INVOKABLE QVariantMap latestJobForKind(const QString& job_kind) const;
  Q_INVOKABLE QString statusForKind(
      const QString& job_kind,
      const QString& idle_status = QString()) const;
  Q_INVOKABLE bool retryJob(const QString& job_id);
  Q_INVOKABLE bool cancelJob(const QString& job_id);
  Q_INVOKABLE void retryFailedJobs();
  Q_INVOKABLE void removeJob(const QString& job_id);
  Q_INVOKABLE void clearFinishedJobs();

 signals:
  void jobsChanged();
  void statusChanged();

 private:
  void reloadJobs();
  void setStatus(const QString& status);
  [[nodiscard]] static QString summaryStatusForJobSubset(
      const QVector<PersistentJobRecord>& jobs,
      const QString& idle_status);
  [[nodiscard]] int activeJobCountValue() const;
  [[nodiscard]] int failedJobCountValue() const;
  [[nodiscard]] int completedJobCountValue() const;
  void updateSummaryStatus();

  QVector<PersistentJobRecord> jobs_;
  QString status_;
};

}  // namespace photo_editor::desktop
