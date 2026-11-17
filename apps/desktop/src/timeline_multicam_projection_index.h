#pragma once

#include <QHash>
#include <QString>
#include <QtGlobal>

namespace photo_editor::desktop {

struct MulticamClipAddress final {
  int group_index = -1;
  int angle_index = -1;

  [[nodiscard]] bool isValid() const {
    return group_index >= 0 && angle_index >= 0;
  }
};

// Builds the same first-match lookup used by TimelineDocumentController's
// multicamGroupForClip()/indexOf() pair, once per projection instead of once
// per projected clip.
class MulticamProjectionIndex final {
 public:
  template <typename GroupRange>
  void rebuild(const GroupRange& groups, qsizetype expected_clip_count) {
    address_by_clip_id_.clear();
    if (groups.isEmpty()) {
      return;
    }
    qsizetype total_angle_count = 0;
    for (const auto& group : groups) {
      total_angle_count += group.angle_clip_ids.size();
    }
    const auto reserve_count =
        qMin(qMax(qsizetype{0}, expected_clip_count), total_angle_count);
    address_by_clip_id_.reserve(reserve_count);
    for (qsizetype group_index = 0; group_index < groups.size(); ++group_index) {
      const auto& angle_clip_ids = groups.at(group_index).angle_clip_ids;
      for (qsizetype angle_index = 0; angle_index < angle_clip_ids.size(); ++angle_index) {
        const auto& clip_id = angle_clip_ids.at(angle_index);
        if (clip_id.isEmpty() || address_by_clip_id_.contains(clip_id)) {
          continue;
        }
        address_by_clip_id_.insert(
            clip_id,
            MulticamClipAddress{
                static_cast<int>(group_index), static_cast<int>(angle_index)});
      }
    }
  }

  [[nodiscard]] MulticamClipAddress addressForClip(const QString& clip_id) const {
    return address_by_clip_id_.value(clip_id);
  }

  [[nodiscard]] qsizetype size() const {
    return address_by_clip_id_.size();
  }

 private:
  QHash<QString, MulticamClipAddress> address_by_clip_id_;
};

}  // namespace photo_editor::desktop
