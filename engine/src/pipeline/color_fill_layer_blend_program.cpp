#include "color_fill_layer_blend_program.h"

namespace photo_editor::pipeline {

std::uint32_t retained_layer_blend_mode_id(std::string_view mode) {
  if (mode.empty() || mode == "normal") return 0U;
  if (mode == "multiply") return 1U;
  if (mode == "screen") return 2U;
  if (mode == "difference") return 3U;
  if (mode == "lighten") return 4U;
  if (mode == "darken") return 5U;
  if (mode == "overlay") return 6U;
  if (mode == "soft light" || mode == "soft-light") return 7U;
  if (mode == "hard light" || mode == "hard-light") return 8U;
  if (mode == "linear light" || mode == "linear-light") return 9U;
  if (mode == "hue") return 10U;
  if (mode == "saturation") return 11U;
  if (mode == "color") return 12U;
  if (mode == "luminosity") return 13U;
  return 255U;
}

}  // namespace photo_editor::pipeline
