#include "timeline_graphics_track_compaction.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace photo_editor::desktop::timeline_compaction {
namespace {

class RangeMaximum {
 public:
  explicit RangeMaximum(std::size_t value_count)
      : values_(value_count * 4, -1), lazy_(value_count * 4, -1),
        value_count_(value_count) {}

  [[nodiscard]] int query(std::size_t first, std::size_t last) {
    return query(1, 0, value_count_ - 1, first, last);
  }

  void raise(std::size_t first, std::size_t last, int value) {
    raise(1, 0, value_count_ - 1, first, last, value);
  }

 private:
  void apply(std::size_t node, int value) {
    values_[node] = std::max(values_[node], value);
    lazy_[node] = std::max(lazy_[node], value);
  }

  void push(std::size_t node) {
    if (lazy_[node] < 0) return;
    apply(node * 2, lazy_[node]);
    apply(node * 2 + 1, lazy_[node]);
    lazy_[node] = -1;
  }

  [[nodiscard]] int query(std::size_t node, std::size_t low,
                          std::size_t high, std::size_t first,
                          std::size_t last) {
    if (first <= low && high <= last) return values_[node];
    push(node);
    const auto middle = low + (high - low) / 2;
    auto result = -1;
    if (first <= middle) {
      result = std::max(result,
                        query(node * 2, low, middle, first, last));
    }
    if (last > middle) {
      result = std::max(result,
                        query(node * 2 + 1, middle + 1, high, first, last));
    }
    return result;
  }

  void raise(std::size_t node, std::size_t low, std::size_t high,
             std::size_t first, std::size_t last, int value) {
    if (first <= low && high <= last) {
      apply(node, value);
      return;
    }
    push(node);
    const auto middle = low + (high - low) / 2;
    if (first <= middle) {
      raise(node * 2, low, middle, first, last, value);
    }
    if (last > middle) {
      raise(node * 2 + 1, middle + 1, high, first, last, value);
    }
    values_[node] = std::max(values_[node * 2], values_[node * 2 + 1]);
  }

  std::vector<int> values_;
  std::vector<int> lazy_;
  std::size_t value_count_;
};

struct OrderedClip {
  const GraphicsClip* clip = nullptr;
  int lane_index = 0;
  double end_seconds = 0.0;
  std::size_t source_order = 0;
  int slot = 0;
};

Plan reject(std::string message) {
  Plan result;
  result.error = std::move(message);
  return result;
}

std::size_t countMaximumOverlap(
    const std::vector<GraphicsClip>& clips) {
  std::vector<std::pair<double, int>> events;
  events.reserve(clips.size() * 2);
  for (const auto& clip : clips) {
    const auto end = clip.start_seconds + clip.duration_seconds;
    events.emplace_back(clip.start_seconds, 1);
    events.emplace_back(end, -1);
  }
  std::sort(events.begin(), events.end());
  std::size_t active = 0;
  std::size_t maximum = 0;
  for (const auto& [time, delta] : events) {
    (void)time;
    if (delta < 0) {
      --active;
    } else {
      ++active;
      maximum = std::max(maximum, active);
    }
  }
  return maximum;
}

}  // namespace

Plan planGraphicsTrackCompaction(
    const std::vector<GraphicsTrack>& input_tracks,
    const std::vector<GraphicsClip>& input_clips,
    int highest_non_graphics_video_lane) {
  if (input_tracks.empty()) {
    return reject("No graphics tracks were supplied for compaction.");
  }
  if (input_clips.empty()) {
    return reject("An empty graphics stack is not compacted automatically.");
  }

  std::vector<const GraphicsTrack*> tracks;
  tracks.reserve(input_tracks.size());
  std::unordered_map<std::string, const GraphicsTrack*> track_by_id;
  track_by_id.reserve(input_tracks.size());
  std::unordered_set<int> lanes;
  lanes.reserve(input_tracks.size());

  for (const auto& track : input_tracks) {
    if (track.track_id.empty() || !track_by_id.emplace(track.track_id, &track).second) {
      return reject("Graphics track IDs must be nonempty and unique.");
    }
    if (!track.is_video_graphics || track.lane_index <= highest_non_graphics_video_lane) {
      return reject("Graphics tracks must form one ordered lane block above all other video tracks.");
    }
    if (!lanes.insert(track.lane_index).second) {
      return reject("Graphics track lane indices must be unique.");
    }
    if (track.locked || !track.visible || track.opacity != 1.0 || track.grouped ||
        track.has_automation || track.has_track_effects || track.has_audio ||
        !track.independent || track.targeted || track.muted || track.solo) {
      return reject("A graphics track has locked, hidden, grouped, animated, audio, or other track-level state.");
    }
    tracks.push_back(&track);
  }
  std::sort(tracks.begin(), tracks.end(), [](const auto* left, const auto* right) {
    return left->lane_index < right->lane_index;
  });

  std::unordered_set<std::string> clip_ids;
  clip_ids.reserve(input_clips.size());
  std::unordered_map<std::string, std::unordered_set<std::size_t>> ordinals_by_track;
  ordinals_by_track.reserve(input_tracks.size());
  std::vector<double> endpoints;
  endpoints.reserve(input_clips.size() * 2);
  std::vector<OrderedClip> ordered;
  ordered.reserve(input_clips.size());

  for (const auto& clip : input_clips) {
    const auto track = track_by_id.find(clip.source_track_id);
    if (clip.clip_id.empty() || !clip_ids.insert(clip.clip_id).second) {
      return reject("Graphics clip IDs must be nonempty and unique.");
    }
    if (track == track_by_id.end()) {
      return reject("Every graphics clip must resolve to one supplied source track.");
    }
    if (!std::isfinite(clip.start_seconds) || clip.start_seconds < 0.0 ||
        !std::isfinite(clip.duration_seconds) || clip.duration_seconds <= 0.0 ||
        !std::isfinite(clip.start_seconds + clip.duration_seconds)) {
      return reject("Graphics clip intervals must be finite, nonnegative, and nonempty.");
    }
    if (clip.has_audio) {
      return reject("A graphics clip with audio cannot be moved by visual-only compaction.");
    }
    if (clip.has_unsupported_timing) {
      return reject("Transition preroll or retiming must be resolved before graphics compaction.");
    }
    if (!ordinals_by_track[clip.source_track_id].insert(clip.source_clip_order).second) {
      return reject("Source clip order must be unique within each graphics track.");
    }
    const auto end = clip.start_seconds + clip.duration_seconds;
    endpoints.push_back(clip.start_seconds);
    endpoints.push_back(end);
    ordered.push_back({&clip, track->second->lane_index, end,
                       clip.source_clip_order, 0});
  }

  std::sort(ordered.begin(), ordered.end(), [](const auto& left, const auto& right) {
    if (left.lane_index != right.lane_index) {
      return left.lane_index < right.lane_index;
    }
    if (left.clip->start_seconds != right.clip->start_seconds) {
      return left.clip->start_seconds < right.clip->start_seconds;
    }
    if (left.source_order != right.source_order) {
      return left.source_order < right.source_order;
    }
    return left.clip->clip_id < right.clip->clip_id;
  });

  std::sort(endpoints.begin(), endpoints.end());
  endpoints.erase(std::unique(endpoints.begin(), endpoints.end()), endpoints.end());
  if (endpoints.size() < 2) {
    return reject("Graphics clip intervals do not define a sequence range.");
  }

  const auto segment_count = endpoints.size() - 1;
  RangeMaximum order_depth_by_time(segment_count);
  int highest_depth = -1;
  for (auto& item : ordered) {
    const auto start_index = static_cast<std::size_t>(
        std::lower_bound(endpoints.begin(), endpoints.end(),
                         item.clip->start_seconds) - endpoints.begin());
    const auto end_index = static_cast<std::size_t>(
        std::lower_bound(endpoints.begin(), endpoints.end(),
                         item.end_seconds) - endpoints.begin());
    if (start_index >= end_index || end_index > segment_count) {
      return reject("A graphics clip interval could not be represented exactly.");
    }
    const auto prior_depth = order_depth_by_time.query(start_index, end_index - 1);
    item.slot = prior_depth + 1;
    highest_depth = std::max(highest_depth, item.slot);
    order_depth_by_time.raise(start_index, end_index - 1, item.slot);
  }

  const auto required_track_count = static_cast<std::size_t>(highest_depth + 1);
  if (required_track_count > tracks.size()) {
    return reject("The order-preserving stack needs more lanes than this plan can retain.");
  }
  Plan plan;
  plan.ok = true;
  plan.original_track_count = tracks.size();
  plan.compacted_track_count = required_track_count;
  plan.maximum_simultaneous_layers = countMaximumOverlap(input_clips);
  if (required_track_count >= tracks.size()) {
    plan.message = "The graphics stack already uses the minimum order-preserving lane count.";
    return plan;
  }
  plan.target_track_ids.reserve(required_track_count);
  for (std::size_t slot = 0; slot < required_track_count; ++slot) {
    plan.target_track_ids.push_back(tracks[slot]->track_id);
  }

  plan.assignments.reserve(input_clips.size());
  plan.moves.reserve(input_clips.size());
  for (const auto& item : ordered) {
    const auto& target_track_id = plan.target_track_ids[static_cast<std::size_t>(item.slot)];
    ClipMove assignment{item.clip->clip_id, item.clip->source_track_id,
                       target_track_id, static_cast<std::size_t>(item.slot)};
    plan.assignments.push_back(assignment);
    if (item.clip->source_track_id != target_track_id) {
      plan.moves.push_back(std::move(assignment));
    }
  }
  for (std::size_t index = required_track_count; index < tracks.size(); ++index) {
    plan.removed_tracks.push_back({tracks[index]->track_id,
                                   tracks[index]->custom_name,
                                   tracks[index]->display_label});
  }
  plan.changed = true;
  return plan;
}

}  // namespace photo_editor::desktop::timeline_compaction
