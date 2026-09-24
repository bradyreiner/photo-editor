#pragma once

namespace photo_editor::desktop {

struct MatteCleanupDefaults final {
  const char* profile = "neutral";
  double smooth = 0.0;
  double fine_edge = 0.0;
  double edge_contrast = 0.0;
  double decontaminate = 0.0;
  double feather = 0.0;
  double harden = 0.0;
  double despeckle = 0.0;
  double fill_holes = 0.0;
  double remove_islands = 0.0;
  double generated_hole_threshold = 0.004;
  double generated_island_threshold = 0.0015;
};

}  // namespace photo_editor::desktop
