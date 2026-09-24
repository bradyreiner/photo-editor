#pragma once

#include <cmath>

#include <QtGlobal>

namespace photo_editor::desktop {

struct StereoPanGains final {
  double left = 1.0;
  double right = 1.0;
};

inline StereoPanGains stereoPanGains(double pan, double volume = 1.0, bool enabled = true) {
  if (!enabled) {
    return {volume, volume};
  }
  constexpr double kPi = 3.14159265358979323846;
  const auto bounded_pan = qBound(-1.0, pan, 1.0);
  const auto angle = (bounded_pan + 1.0) * kPi / 4.0;
  const auto center_reference = std::sqrt(2.0);
  return {
      volume * std::cos(angle) * center_reference,
      volume * std::sin(angle) * center_reference,
  };
}

}  // namespace photo_editor::desktop
