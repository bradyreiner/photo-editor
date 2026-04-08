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
[[nodiscard]] QVector<PersistentJobRecord> loadPersistedJobRecords();
[[nodiscard]] bool upsertPersistedJobRecord(
    const PersistentJobRecord& record,
    QString* error_message = nullptr);
[[nodiscard]] bool removePersistedJobRecord(
    const QString& job_id,
    QString* error_message = nullptr);
[[nodiscard]] int recoverInterruptedJobRecords(
    QString* error_message = nullptr);
[[nodiscard]] bool retryPersistedJobRecord(
    const QString& job_id,
    QString* error_message = nullptr);
[[nodiscard]] bool cancelPersistedJobRecord(
    const QString& job_id,
    QString* error_message = nullptr);

}  // namespace photo_editor::desktop
