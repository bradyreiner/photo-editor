#include "preset_registry.h"

#include <iostream>

namespace {

int fail(const char* message) {
  std::cerr << message << '\n';
  return 1;
}

}  // namespace

int main() {
  const auto presets = photo_editor::desktop::built_in_preset_map();
  const auto packs = photo_editor::desktop::built_in_preset_pack_map();
  const auto categories = photo_editor::desktop::built_in_preset_category_map();
  const auto groups = photo_editor::desktop::built_in_preset_group_map();
  const auto adaptive_rules =
      photo_editor::desktop::built_in_preset_adaptive_rule_map();

  if (presets.size() != 14 || packs.size() != presets.size() ||
      categories.size() != presets.size() || groups.size() != presets.size()) {
    return fail("built-in preset registry maps must publish the complete fourteen-preset inventory");
  }
  for (auto iterator = presets.cbegin(); iterator != presets.cend(); ++iterator) {
    const auto& name = iterator.key();
    if (!packs.contains(name) || !categories.contains(name) ||
        !groups.contains(name) || packs.value(name).isEmpty() ||
        categories.value(name).isEmpty() || groups.value(name).isEmpty()) {
      return fail("every built-in preset must have pack, category, and adjustment-group metadata");
    }
  }
  const auto camera_aware_rules =
      adaptive_rules.value(QStringLiteral("Camera Aware Portrait"));
  if (adaptive_rules.size() != 1 || camera_aware_rules.size() != 3) {
    return fail("adaptive preset metadata must remain attached only to Camera Aware Portrait");
  }
  if (!presets.value(QStringLiteral("RAW Highlight Recovery"))
           .raw_input_transform_enabled ||
      !presets.value(QStringLiteral("Clean Black and White"))
           .black_and_white_enabled) {
    return fail("RAW recovery and monochrome preset semantics must survive registry extraction");
  }

  std::cout << "preset_registry_test passed\n";
  return 0;
}
