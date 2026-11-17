#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace photo_editor::desktop::timeline_compaction {

// A read-only projection of a native graphics track. `grouped` must include
// group membership, non-default group transforms, and inherited group hiding.
// `has_track_effects` covers any track-level visual processing not represented
// by the other fields. The planner never edits or reconstructs track objects.
struct GraphicsTrack {
  std::string track_id;
  std::string custom_name;
  std::string display_label;
  int lane_index = 0;
  bool is_video_graphics = true;
  bool locked = false;
  bool visible = true;
  double opacity = 1.0;
  bool grouped = false;
  bool has_automation = false;
  bool has_track_effects = false;
  bool has_audio = false;
  bool independent = true;
  bool targeted = false;
  bool muted = false;
  bool solo = false;
};

// A read-only projection of the clip fields used for lane planning.
// `start_seconds` and `duration_seconds` must describe the clip's full
// render-visible interval; callers must account for any transition preroll.
// `source_clip_order` is its index in the current native track clip vector and
// breaks ties for clips sharing a lane and start time. Clip animations, masks,
// blend modes, effects, source paths, and all other clip properties stay in
// the native Clip: the integration changes only its containing Track.
struct GraphicsClip {
  std::string clip_id;
  std::string source_track_id;
  std::string display_label;
  double start_seconds = 0.0;
  double duration_seconds = 0.0;
  std::size_t source_clip_order = 0;
  bool has_audio = false;
  bool has_unsupported_timing = false;
};

struct ClipMove {
  std::string clip_id;
  std::string source_track_id;
  std::string target_track_id;
  std::size_t target_slot = 0;
};

struct RemovedTrack {
  std::string track_id;
  std::string custom_name;
  std::string display_label;
};

struct Plan {
  bool ok = false;
  bool changed = false;
  std::string error;
  std::string message;
  std::size_t original_track_count = 0;
  std::size_t compacted_track_count = 0;
  std::size_t maximum_simultaneous_layers = 0;
  std::vector<std::string> target_track_ids;
  std::vector<ClipMove> assignments;
  std::vector<ClipMove> moves;
  std::vector<RemovedTrack> removed_tracks;
};

// Computes the minimum number of ordered graphics lanes that can hold every
// clip while preserving the source compositor order for every pair of clips
// whose half-open sequence intervals overlap. Graphics tracks must form one
// lane block strictly above all non-graphics video tracks. The result is only
// a plan; callers must apply it atomically and revalidate against the current
// document before mutation. Retained tracks are the first required lanes in
// current low-to-high order; removed track names are returned for an explicit
// receipt because those row labels are not transferred to the retained lanes.
[[nodiscard]] Plan planGraphicsTrackCompaction(
    const std::vector<GraphicsTrack>& tracks,
    const std::vector<GraphicsClip>& clips,
    int highest_non_graphics_video_lane);

}  // namespace photo_editor::desktop::timeline_compaction
