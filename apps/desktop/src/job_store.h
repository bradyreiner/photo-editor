#pragma once

#include <QObject>
#include <QJsonObject>
#include <QString>
#include <QVector>

namespace photo_editor::desktop {

struct PersistentJobRecord final {
  QString job_id;
  QString job_kind = QStringLiteral("asset_export");
  QString asset_id;
  QString media_type;
  QString display_label;
  QString source_path;
  QString edited_preview_source;
  QString recipe_json;
  QJsonObject metadata_json;
  QString preset_name;
  QString destination_path;
  QString output_path;
  QString status = QStringLiteral("Queued");
  QString error_message;
  QString created_at;
  QString updated_at;
  int progress = 0;
};

class JobStoreNotifier final : public QObject {
  Q_OBJECT

 public:
  explicit JobStoreNotifier(QObject* parent = nullptr);
  void notifyJobsChanged();

 signals:
  void jobsChanged();
};

[[nodiscard]] JobStoreNotifier* jobStoreNotifier();
// Background observers pass false; recovery retains the existing migration behavior.
[[nodiscard]] QVector<PersistentJobRecord> loadPersistedJobRecords(
    QString* error_message = nullptr, bool migrate_legacy = true);
[[nodiscard]] qint64 persistentRenderQueueOrderForNewJob(const QString& job_id = QString());
// Startup uses require_catalog: local fallback is not proof that the selected
// catalog committed recovery. Other callers retain legacy compatibility.
[[nodiscard]] bool upsertPersistedJobRecord(
    const PersistentJobRecord& record,
    QString* error_message = nullptr, bool require_catalog = false);
[[nodiscard]] bool removePersistedJobRecord(
    const QString& job_id,
    QString* error_message = nullptr);
[[nodiscard]] int recoverInterruptedJobRecords(
    QString* error_message = nullptr, bool require_catalog = false);
[[nodiscard]] bool retryPersistedJobRecord(
    const QString& job_id,
    QString* error_message = nullptr);
[[nodiscard]] bool cancelPersistedJobRecord(
    const QString& job_id,
    QString* error_message = nullptr);
[[nodiscard]] bool pausePersistedImportJobRecord(
    const QString& job_id,
    QString* error_message = nullptr);
[[nodiscard]] bool resumePersistedImportJobRecord(
    const QString& job_id,
    QString* error_message = nullptr);
[[nodiscard]] bool pausePersistedJobRecord(
    const QString& job_id,
    QString* error_message = nullptr);
[[nodiscard]] bool resumePersistedJobRecord(
    const QString& job_id,
    QString* error_message = nullptr);

}  // namespace photo_editor::desktop
