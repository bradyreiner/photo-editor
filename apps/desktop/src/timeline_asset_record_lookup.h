#pragma once

#include "timeline_asset_store.h"

#include <QFutureWatcher>
#include <QObject>
#include <QString>
#include <QTimer>

#include <functional>
#include <optional>

namespace photo_editor::desktop {

// Coalesces notifications into one catalog lookup at a time. The worker only
// owns a copy of Loader; all request validation and publication stay on the
// QObject's thread.
class TimelineAssetRecordLookup final : public QObject {
 public:
  using Loader = std::function<QVector<TimelineAssetRecord>()>;
  using IsCurrent = std::function<bool(const QString&, quint64)>;
  using Publisher = std::function<void(
      const QString&, quint64, const QVector<TimelineAssetRecord>&)>;

  TimelineAssetRecordLookup(
      Loader loader,
      IsCurrent is_current,
      Publisher publisher,
      QObject* parent = nullptr);

  void request(const QString& timeline_id, quint64 working_session_generation);

 private:
  using LoadResult = std::optional<QVector<TimelineAssetRecord>>;

  void startLatestRequest();
  void finishRequest();

  Loader loader_;
  IsCurrent is_current_;
  Publisher publisher_;
  QTimer start_timer_;
  QFutureWatcher<LoadResult> watcher_;
  QString latest_timeline_id_;
  quint64 latest_working_session_generation_ = 0;
  QString active_timeline_id_;
  quint64 active_working_session_generation_ = 0;
  bool loading_ = false;
  bool rerun_requested_ = false;
};

}  // namespace photo_editor::desktop
