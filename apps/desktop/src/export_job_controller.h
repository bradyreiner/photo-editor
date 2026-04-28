#pragma once

#include <QObject>
#include <QJsonObject>
#include <QString>
#include <QVariantList>
#include <QVector>

#include "job_store.h"

namespace photo_editor::desktop {

class ExportJobController final : public QObject {
  Q_OBJECT
  Q_PROPERTY(QVariantList jobs READ jobs NOTIFY jobsChanged)
  Q_PROPERTY(int jobCount READ jobCount NOTIFY jobsChanged)
  Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
  Q_PROPERTY(QString status READ status NOTIFY statusChanged)

 public:
  explicit ExportJobController(QObject* parent = nullptr);

  [[nodiscard]] QVariantList jobs() const;
  [[nodiscard]] int jobCount() const;
  [[nodiscard]] bool busy() const;
  [[nodiscard]] QString status() const;

  Q_INVOKABLE QString defaultDestinationDirectory() const;
  Q_INVOKABLE QString pickDestinationFolder(
      const QString& title = QStringLiteral("Choose Export Folder")) const;
  Q_INVOKABLE bool queueExport(
      const QString& asset_id,
      const QString& media_type,
      const QString& display_label,
      const QString& source_path,
      const QString& edited_preview_source,
      const QString& recipe_json,
      const QString& preset,
      const QString& destination_directory);
  Q_INVOKABLE int queueBatchExport(
      const QVariantList& assets,
      const QString& preset,
      const QString& destination_directory);
  Q_INVOKABLE void retryJob(const QString& job_id);
  Q_INVOKABLE void removeJob(const QString& job_id);
  Q_INVOKABLE void clearFinishedJobs();

 signals:
  void jobsChanged();
  void busyChanged();
  void statusChanged();

 private:
  struct ExportJob final {
    QString job_id;
    QString asset_id;
    QString media_type;
    QString display_label;
    QString source_path;
    QString edited_preview_source;
    QString recipe_json;
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
  [[nodiscard]] int jobIndexById(const QString& job_id) const;
  [[nodiscard]] QString resolvedSourcePath(const ExportJob& job, bool* uses_preview) const;
  [[nodiscard]] ExportJob jobFromRecord(const PersistentJobRecord& record) const;
  void loadPersistedJobs(bool emit_signal = true);
  void persistJob(const ExportJob& job) const;
  void removePersistedJob(const QString& job_id) const;
  void refreshJobs();
  void updateBusyState();
  void setBusy(bool busy);
  void setStatus(const QString& status);

  QVector<ExportJob> jobs_;
  bool busy_ = false;
  QString status_ = QStringLiteral("Export queue ready.");
};

}  // namespace photo_editor::desktop
