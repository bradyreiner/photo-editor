#pragma once

#include <cstdint>

namespace photo_editor::pipeline {

// Blend If evaluates one working-space component for the current layer and
// the already-composited layers beneath it. Gray uses working-space XYZ Y;
// the RGB choices use the corresponding linear working-space channel.
enum class BlendIfChannel : std::uint8_t {
  Gray,
  Red,
  Green,
  Blue,
};

// Each black or white interval is independently feathered. Thresholds are
// normalized to the layer blend reference headroom: negative working values
// map to 0, values at or above that headroom map to 1, and intermediate values
// map linearly. Defaults are identity when enabled and the whole feature is
// bypassed by default.
struct LayerBlendIf final {
  bool operator==(const LayerBlendIf&) const = default;

  bool enabled = false;
  BlendIfChannel channel = BlendIfChannel::Gray;
  double source_black_low = 0.0;
  double source_black_high = 0.0;
  double source_white_low = 1.0;
  double source_white_high = 1.0;
  double underlying_black_low = 0.0;
  double underlying_black_high = 0.0;
  double underlying_white_low = 1.0;
  double underlying_white_high = 1.0;
};

}  // namespace photo_editor::pipeline
