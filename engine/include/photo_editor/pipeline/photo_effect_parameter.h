#pragma once

#include <map>
#include <string>
#include <string_view>

namespace photo_editor::pipeline {

// Numeric controls share the same validated values in recipes and the inspector.
// Structured data (gradient stops and external map identity) has its own codec.
using PhotoEffectParameterValues = std::map<std::string, double>;

struct PhotoEffectParameterField final {
  std::string_view key;
  std::string_view label;
  double minimum = 0.0;
  double maximum = 1.0;
  double step = 0.01;
};

}  // namespace photo_editor::pipeline
