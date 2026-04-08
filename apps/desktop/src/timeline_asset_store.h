#pragma once

#include <QJsonObject>
#include <QString>
#include <QVector>

namespace photo_editor::desktop {

struct TimelineAssetRecord final {
  QString asset_id;
  QString timeline_id;
  QString name;
  QString document_path;
  QString render_path;
  QString poster_path;
  double duration_seconds = 0.0;
  QString created_at;
  QString updated_at;
};

[[nodiscard]] QString timelineAssetsRootPath();
[[nodiscard]] QString timelineDocumentsRootPath();
[[nodiscard]] QString timelineRendersRootPath();
[[nodiscard]] QString timelinePostersRootPath();
[[nodiscard]] QString timelineIndexPath();
[[nodiscard]] QVector<TimelineAssetRecord> loadTimelineAssetRecords();
[[nodiscard]] bool saveTimelineAssetRecords(const QVector<TimelineAssetRecord>& records);
[[nodiscard]] QJsonObject loadTimelineDocumentBackup(const QString& timeline_id);
[[nodiscard]] bool saveTimelineDocumentBackup(
    const TimelineAssetRecord& record,
    const QJsonObject& document);

}  // namespace photo_editor::desktop
