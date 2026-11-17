#include "../apps/desktop/src/timeline_graphics_track_compaction.h"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

using photo_editor::desktop::timeline_compaction::GraphicsClip;
using photo_editor::desktop::timeline_compaction::GraphicsTrack;
using photo_editor::desktop::timeline_compaction::Plan;
using photo_editor::desktop::timeline_compaction::planGraphicsTrackCompaction;

void require(bool condition, const std::string& message) {
  if (!condition) throw std::runtime_error(message);
}

GraphicsTrack track(std::string id, int lane) {
  GraphicsTrack result;
  result.track_id = std::move(id);
  result.display_label = result.track_id;
  result.lane_index = lane;
  return result;
}

GraphicsClip clip(std::string id, std::string track_id, double start,
                  double duration, std::size_t order = 0) {
  GraphicsClip result;
  result.clip_id = std::move(id);
  result.source_track_id = std::move(track_id);
  result.display_label = result.clip_id;
  result.start_seconds = start;
  result.duration_seconds = duration;
  result.source_clip_order = order;
  return result;
}

std::unordered_map<std::string, std::string> assignment_targets(const Plan& plan) {
  std::unordered_map<std::string, std::string> result;
  for (const auto& assignment : plan.assignments) {
    result.emplace(assignment.clip_id, assignment.target_track_id);
  }
  return result;
}

bool preservesEveryOverlappingPair(
    const std::vector<GraphicsTrack>& tracks,
    const std::vector<GraphicsClip>& clips, const Plan& plan) {
  std::unordered_map<std::string, int> lanes;
  for (const auto& track_data : tracks) {
    lanes.emplace(track_data.track_id, track_data.lane_index);
  }
  std::unordered_map<std::string, std::size_t> slots;
  for (const auto& assignment : plan.assignments) {
    slots.emplace(assignment.clip_id, assignment.target_slot);
  }
  std::vector<const GraphicsClip*> ordered;
  for (const auto& clip_data : clips) ordered.push_back(&clip_data);
  std::sort(ordered.begin(), ordered.end(), [&lanes](const auto* left,
                                                      const auto* right) {
    const auto left_lane = lanes.at(left->source_track_id);
    const auto right_lane = lanes.at(right->source_track_id);
    if (left_lane != right_lane) return left_lane < right_lane;
    if (left->start_seconds != right->start_seconds) {
      return left->start_seconds < right->start_seconds;
    }
    return left->source_clip_order < right->source_clip_order;
  });
  for (std::size_t lower = 0; lower < ordered.size(); ++lower) {
    const auto* lower_clip = ordered[lower];
    const auto lower_end = lower_clip->start_seconds + lower_clip->duration_seconds;
    for (std::size_t upper = lower + 1; upper < ordered.size(); ++upper) {
      const auto* upper_clip = ordered[upper];
      const auto upper_end = upper_clip->start_seconds + upper_clip->duration_seconds;
      const auto overlaps = lower_clip->start_seconds < upper_end &&
                            upper_clip->start_seconds < lower_end;
      if (overlaps && slots.at(lower_clip->clip_id) >= slots.at(upper_clip->clip_id)) {
        return false;
      }
    }
  }
  return true;
}

void nonoverlapping_clips_reuse_lane_without_changing_overlaps() {
  const std::vector<GraphicsTrack> tracks = {
      track("g0", 100), track("g1", 101), track("g2", 102), track("g3", 103)};
  const std::vector<GraphicsClip> clips = {
      clip("base-a", "g0", 0.0, 1.0),
      clip("base-b", "g1", 1.0, 1.0),
      clip("overlay", "g2", 0.5, 1.0),
      clip("later", "g3", 2.0, 1.0)};

  const auto plan = planGraphicsTrackCompaction(tracks, clips, 99);
  require(plan.ok && plan.changed, "A safe compaction plan should be produced.");
  require(plan.compacted_track_count == 2,
          "The overlap order requires two lanes, not one lane per source track.");
  require(plan.maximum_simultaneous_layers == 2,
          "The layer-count diagnostic should use half-open intervals.");
  require(plan.target_track_ids == std::vector<std::string>({"g0", "g1"}),
          "Targets must preserve existing low-to-high track order.");

  const auto targets = assignment_targets(plan);
  require(targets.at("base-b") == "g0",
          "Adjacent nonoverlapping clips can share the lower reusable lane.");
  require(targets.at("overlay") == "g1",
          "The overlapping overlay must remain above both base clips.");
  require(targets.at("later") == "g0",
          "A later disjoint clip can reuse the lower lane.");
  require(plan.removed_tracks.size() == 2,
          "Only tracks outside the retained target stack are removable.");
  require(preservesEveryOverlappingPair(tracks, clips, plan),
          "Every simultaneous pair must retain its exact low-to-high order.");
}

void transitive_overlap_order_can_require_more_lanes_than_peak_overlap() {
  const std::vector<GraphicsTrack> tracks = {
      track("g0", 100), track("g1", 101), track("g2", 102), track("g3", 103)};
  const std::vector<GraphicsClip> clips = {
      clip("a", "g0", 0.0, 1.5),
      clip("b", "g1", 1.0, 1.5),
      clip("c", "g2", 2.0, 1.0),
      clip("unused", "g3", 4.0, 1.0)};

  const auto plan = planGraphicsTrackCompaction(tracks, clips, 99);
  require(plan.ok && plan.compacted_track_count == 3,
          "A-to-B and B-to-C overlap constraints must remain transitively ordered.");
  require(plan.maximum_simultaneous_layers == 2,
          "The diagnostic should distinguish peak overlap from the order constraint chain.");
  const auto targets = assignment_targets(plan);
  require(targets.at("a") == "g0" && targets.at("b") == "g1" &&
              targets.at("c") == "g2",
          "The old bottom-to-top order must survive every active interval.");
  require(preservesEveryOverlappingPair(tracks, clips, plan),
          "Transitive stack constraints must preserve every simultaneous pair.");
}

void same_track_overlaps_use_clip_order_and_half_open_endpoints() {
  const std::vector<GraphicsTrack> tracks = {
      track("g0", 100), track("g1", 101), track("g2", 102)};
  const std::vector<GraphicsClip> clips = {
      clip("early", "g0", 0.0, 2.0, 0),
      clip("late", "g0", 1.0, 1.0, 1),
      clip("adjacent", "g1", 2.0, 1.0),
      clip("empty-slot", "g2", 4.0, 1.0)};

  const auto plan = planGraphicsTrackCompaction(tracks, clips, 99);
  require(plan.ok && plan.compacted_track_count == 2,
          "Same-track overlaps need distinct ordered lanes; boundary adjacency does not.");
  const auto targets = assignment_targets(plan);
  require(targets.at("early") == "g0" && targets.at("late") == "g1",
          "Later-starting overlapping clips must stay above earlier clips on the same lane.");
  require(targets.at("adjacent") == "g0",
          "A clip starting exactly at another clip's end may reuse its lane.");
  require(preservesEveryOverlappingPair(tracks, clips, plan),
          "The plan must preserve same-track start order for all overlaps.");
}

void nonneutral_or_audio_tracks_fail_closed() {
  auto tracks = std::vector<GraphicsTrack>{track("g0", 100), track("g1", 101)};
  auto clips = std::vector<GraphicsClip>{clip("a", "g0", 0.0, 1.0),
                                         clip("b", "g1", 1.0, 1.0)};

  tracks[1].opacity = 0.5;
  auto plan = planGraphicsTrackCompaction(tracks, clips, 99);
  require(!plan.ok && plan.moves.empty() && plan.removed_tracks.empty(),
          "Track opacity must not be lost by moving clips to another track.");

  tracks[1].opacity = 1.0;
  tracks[1].has_automation = true;
  plan = planGraphicsTrackCompaction(tracks, clips, 99);
  require(!plan.ok, "Track keyframes must block compaction.");

  tracks[1].has_automation = false;
  tracks[1].independent = false;
  plan = planGraphicsTrackCompaction(tracks, clips, 99);
  require(!plan.ok, "Paired tracks must not be folded into an independent graphics lane.");

  tracks[1].independent = true;
  clips[1].has_audio = true;
  plan = planGraphicsTrackCompaction(tracks, clips, 99);
  require(!plan.ok, "Visual-only compaction must reject clips carrying audio.");

  clips[1].has_audio = false;
  clips[1].has_unsupported_timing = true;
  plan = planGraphicsTrackCompaction(tracks, clips, 99);
  require(!plan.ok, "Transition preroll and retiming need explicit interval handling.");
}

void ambiguous_or_invalid_lane_geometry_fails_closed() {
  auto tracks = std::vector<GraphicsTrack>{track("g0", 100), track("g1", 100)};
  auto clips = std::vector<GraphicsClip>{clip("a", "g0", 0.0, 1.0),
                                         clip("b", "g1", 1.0, 1.0)};
  auto plan = planGraphicsTrackCompaction(tracks, clips, 99);
  require(!plan.ok, "Duplicate source lane indices make layer order ambiguous.");

  tracks = {track("g0", 100), track("g1", 101)};
  plan = planGraphicsTrackCompaction(tracks, clips, 100);
  require(!plan.ok, "The graphics lane block must remain above non-graphics video.");

  clips[1].duration_seconds = 0.0;
  plan = planGraphicsTrackCompaction(tracks, clips, 99);
  require(!plan.ok, "Empty intervals cannot be compacted.");
}

void no_op_stack_does_not_reassign_clips() {
  const std::vector<GraphicsTrack> tracks = {track("g0", 100), track("g1", 101)};
  const std::vector<GraphicsClip> clips = {clip("a", "g0", 0.0, 2.0),
                                           clip("b", "g1", 0.0, 2.0)};
  const auto plan = planGraphicsTrackCompaction(tracks, clips, 99);
  require(plan.ok && !plan.changed && plan.moves.empty() &&
              plan.removed_tracks.empty(),
          "A plan that cannot remove tracks must be a true no-op.");
}

}  // namespace

int main() {
  nonoverlapping_clips_reuse_lane_without_changing_overlaps();
  transitive_overlap_order_can_require_more_lanes_than_peak_overlap();
  same_track_overlaps_use_clip_order_and_half_open_endpoints();
  nonneutral_or_audio_tracks_fail_closed();
  ambiguous_or_invalid_lane_geometry_fails_closed();
  no_op_stack_does_not_reassign_clips();
  return 0;
}
