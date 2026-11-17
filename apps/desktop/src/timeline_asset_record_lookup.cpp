#include "timeline_asset_record_lookup.h"

#include <QtConcurrent>

#include <utility>

namespace photo_editor::desktop {

TimelineAssetRecordLookup::TimelineAssetRecordLookup(
    Loader loader,
    IsCurrent is_current,
    Publisher publisher,
    QObject* parent)
    : QObject(parent),
      loader_(std::move(loader)),
      is_current_(std::move(is_current)),
      publisher_(std::move(publisher)) {
  start_timer_.setSingleShot(true);
  connect(&start_timer_, &QTimer::timeout, this,
          &TimelineAssetRecordLookup::startLatestRequest);
  connect(&watcher_, &QFutureWatcher<LoadResult>::finished, this,
          &TimelineAssetRecordLookup::finishRequest);
}

void TimelineAssetRecordLookup::request(
    const QString& timeline_id,
    quint64 working_session_generation) {
  latest_timeline_id_ = timeline_id.trimmed();
  latest_working_session_generation_ = working_session_generation;

  if (latest_timeline_id_.isEmpty()) {
    start_timer_.stop();
    if (loading_) {
      rerun_requested_ = true;
    }
    return;
  }

  if (loading_) {
    rerun_requested_ = true;
    return;
  }

  if (!start_timer_.isActive()) {
    start_timer_.start(0);
  }
}

void TimelineAssetRecordLookup::startLatestRequest() {
  if (loading_ || latest_timeline_id_.isEmpty()) {
    return;
  }

  active_timeline_id_ = latest_timeline_id_;
  active_working_session_generation_ =
      latest_working_session_generation_;
  rerun_requested_ = false;
  loading_ = true;

  // Keep the worker independent of this QObject and its owning controller.
  // Destroying either object only disconnects publication; the copied loader
  // can finish safely on the pool thread.
  auto loader = loader_;
  watcher_.setFuture(QtConcurrent::run(
      [loader = std::move(loader)]() mutable -> LoadResult {
        try {
          return loader();
        } catch (...) {
          return std::nullopt;
        }
      }));
}

void TimelineAssetRecordLookup::finishRequest() {
  LoadResult result;
  try {
    result = watcher_.result();
  } catch (...) {
    result = std::nullopt;
  }

  const auto completed_timeline_id = active_timeline_id_;
  const auto completed_generation = active_working_session_generation_;
  const bool request_still_current =
      completed_timeline_id == latest_timeline_id_ &&
      completed_generation == latest_working_session_generation_;
  const bool publish_result =
      result.has_value() && request_still_current && is_current_ &&
      is_current_(completed_timeline_id, completed_generation);
  const bool start_follow_up =
      rerun_requested_ && !latest_timeline_id_.isEmpty();

  loading_ = false;
  rerun_requested_ = false;
  active_timeline_id_.clear();
  active_working_session_generation_ = 0;

  if (start_follow_up && !start_timer_.isActive()) {
    start_timer_.start(0);
  }

  if (publish_result && publisher_) {
    publisher_(completed_timeline_id, completed_generation, *result);
  }
}

}  // namespace photo_editor::desktop
