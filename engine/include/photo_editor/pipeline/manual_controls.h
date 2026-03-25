#pragma once

#include <string>
#include <vector>

namespace photo_editor::pipeline {

struct ManualControlGroup final {
  std::string id;
  std::string label;
  std::vector<std::string> controls;
};

inline std::vector<ManualControlGroup> DefaultManualControlGroups() {
  return {
      {"global_tone", "Global Tone", {"exposure", "contrast", "highlights", "shadows", "whites", "blacks"}},
      {"white_balance", "White Balance", {"temperature", "tint", "eyedropper"}},
      {"presence", "Presence", {"texture", "clarity", "dehaze", "vibrance", "saturation"}},
      {"curve_color", "Curve and Color", {"tone_curve", "color_mixer", "color_grading", "black_and_white_mix"}},
      {"detail", "Detail", {"sharpening", "sharpen_masking", "luminance_nr", "color_nr"}},
      {"geometry", "Crop and Transform", {"crop", "rotate", "straighten", "perspective", "lens_correction"}},
      {"retouch", "Retouch", {"healing", "clone", "dust_spot"}},
  };
}

}  // namespace photo_editor::pipeline
