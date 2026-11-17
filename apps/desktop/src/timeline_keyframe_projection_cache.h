#pragma once

#include <QVector>
#include <QString>

#include <mutex>
#include <utility>

namespace photo_editor::desktop::timeline_detail {

// Caches a projection of an implicitly shared authored keyframe vector.
// Keeping source_ alive makes the QVector backing identity safe to compare:
// an edit detaches the source, while the old allocation cannot be recycled
// into a false cache hit before this entry is replaced or cleared.
template <typename Value>
class KeyframeProjectionCache final {
 public:
  template <typename Projector>
  QVector<Value> materialize(
      const QVector<Value>& source,
      const QString& projection_key,
      Projector&& project) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (has_projection_ && projection_key == projection_key_ &&
        source.size() == source_.size() && source.isSharedWith(source_)) {
      return projected_;
    }

    auto projected = std::forward<Projector>(project)(source);
    source_ = source;
    projection_key_ = projection_key;
    projected_ = std::move(projected);
    has_projection_ = true;
    return projected_;
  }

  void clear() const {
    std::lock_guard<std::mutex> lock(mutex_);
    source_.clear();
    projection_key_.clear();
    projected_.clear();
    has_projection_ = false;
  }

 private:
  mutable std::mutex mutex_;
  mutable QVector<Value> source_;
  mutable QString projection_key_;
  mutable QVector<Value> projected_;
  mutable bool has_projection_ = false;
};

}  // namespace photo_editor::desktop::timeline_detail
