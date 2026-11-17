#pragma once

#include <QJsonArray>
#include <QString>
#include <QVector>

#include <mutex>
#include <utility>

namespace photo_editor::desktop::timeline_detail {

// Retains a shallow source copy so Qt's implicit-sharing identity remains
// stable until an edit detaches the source. A cache entry contains only
// derived projection/JSON data; callers should keep one cache per authored
// Clip field or output variant and provide a versioned projection key.
template <typename Value>
class KeyframeJsonArrayCache final {
 public:
  struct Materialized final {
    QVector<Value> projected;
    QJsonArray array;
  };

  template <typename Projector, typename Serializer>
  Materialized materialize(
      const QVector<Value>& source,
      const QString& projection_key,
      Projector&& project,
      Serializer&& serialize) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (projection_key == projection_key_ &&
        source.size() == source_.size() &&
        source.isSharedWith(source_)) {
      return {projected_, array_};
    }

    auto projected = std::forward<Projector>(project)(source);
    auto array = std::forward<Serializer>(serialize)(projected);
    source_ = source;
    projection_key_ = projection_key;
    projected_ = std::move(projected);
    array_ = std::move(array);
    return {projected_, array_};
  }

 private:
  std::mutex mutex_;
  QVector<Value> source_;
  QString projection_key_;
  QVector<Value> projected_;
  QJsonArray array_;
};

}  // namespace photo_editor::desktop::timeline_detail
