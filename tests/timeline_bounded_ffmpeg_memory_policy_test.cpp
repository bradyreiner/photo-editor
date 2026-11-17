#include "timeline_bounded_ffmpeg_memory_policy.h"

#include <iostream>
#include <limits>

using photo_editor::desktop::timeline_motion::bounded_h264::
    bounded_window_count;
using photo_editor::desktop::timeline_motion::bounded_h264::
    bounded_window_frame_limit;
using photo_editor::desktop::timeline_motion::bounded_h264::
    kBoundedWindowMaximumFrames;

int main() {
  if (bounded_window_frame_limit(30.0) != 16 ||
      bounded_window_frame_limit(60.0) != 16 ||
      bounded_window_frame_limit(4.0) != 16 ||
      bounded_window_frame_limit(3.0) != 12 ||
      bounded_window_frame_limit(0.24) != 1 ||
      bounded_window_frame_limit(0.0) != 0 ||
      bounded_window_frame_limit(-30.0) != 0 ||
      bounded_window_frame_limit(std::numeric_limits<double>::infinity()) != 0) {
    std::cerr << "The bounded FFmpeg frame policy should enforce a finite 16-frame cap and preserve the four-second cap at low rates\n";
    return 1;
  }
  if (bounded_window_count(8940, kBoundedWindowMaximumFrames) != 559 ||
      bounded_window_count(300, kBoundedWindowMaximumFrames) != 19 ||
      bounded_window_count(0, kBoundedWindowMaximumFrames) != 0 ||
      bounded_window_count(8940, 0) != 0) {
    std::cerr << "The bounded FFmpeg window planner should count contiguous maximum-size chunks safely\n";
    return 1;
  }
  return 0;
}
