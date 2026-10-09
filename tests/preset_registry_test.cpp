#include "preset_registry.h"

#include <cmath>
#include <iostream>
#include <set>

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

  if (presets.size() != 82 || packs.size() != presets.size() ||
      categories.size() != presets.size() || groups.size() != presets.size()) {
    return fail("built-in preset registry maps must publish the complete eighty-two-preset inventory");
  }
  const std::set<QString> valid_groups = {
      QStringLiteral("raw"), QStringLiteral("tone"), QStringLiteral("color"),
      QStringLiteral("crop"), QStringLiteral("masks"), QStringLiteral("presets"),
      QStringLiteral("layers"), QStringLiteral("history"), QStringLiteral("detail")};
  for (auto iterator = presets.cbegin(); iterator != presets.cend(); ++iterator) {
    const auto& name = iterator.key();
    if (!packs.contains(name) || !categories.contains(name) ||
        !groups.contains(name) || packs.value(name).isEmpty() ||
        categories.value(name).isEmpty() || groups.value(name).isEmpty()) {
      return fail("every built-in preset must have pack, category, and adjustment-group metadata");
    }
    for (const auto& group : groups.value(name)) {
      if (!valid_groups.contains(group)) {
        return fail("built-in preset metadata must use recognized adjustment groups");
      }
    }
    const auto& state = iterator.value();
    const double core_values[] = {
        state.exposure, state.contrast, state.highlights, state.shadows,
        state.whites, state.blacks, state.temperature, state.tint,
        state.vibrance, state.saturation, state.texture, state.clarity,
        state.dehaze, state.sharpening, state.luminance_nr, state.color_nr,
        state.grading_shadows_hue, state.grading_midtones_hue,
        state.grading_highlights_hue};
    for (const double value : core_values) {
      if (!std::isfinite(value) || value < -100.0 || value > 100.0) {
        return fail("built-in preset core adjustments must be finite and within supported slider bounds");
      }
    }
    const double grading_hues[] = {
        state.grading_shadows_hue, state.grading_midtones_hue,
        state.grading_highlights_hue};
    for (const double hue : grading_hues) {
      if (hue < 0.0 || hue > 1.0) {
        return fail("built-in preset grading hues must use normalized hue values");
      }
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
  if (groups.value(QStringLiteral("Night Market Lights")) !=
          QStringList{QStringLiteral("tone"), QStringLiteral("color"), QStringLiteral("detail")}) {
    return fail("night presets must declare the adjustment groups they populate");
  }
  const auto cinematic = presets.value(QStringLiteral("Teal and Amber Cinema"));
  const auto pastel = presets.value(QStringLiteral("Lavender Haze"));
  const auto analog = presets.value(QStringLiteral("Warm Negative"));
  const auto food = presets.value(QStringLiteral("Citrus Menu"));
  const auto architecture = presets.value(QStringLiteral("Night Grid"));
  const auto duotone = presets.value(QStringLiteral("Rose and Teal Duotone"));
  if (cinematic.grading_shadows_saturation < 15.0 ||
      pastel.contrast >= 0.0 || analog.lens_vignette >= 0.0 ||
      !presets.value(QStringLiteral("Silver Gelatin")).black_and_white_enabled ||
      food.vibrance < 15.0 || architecture.temperature > -3.0 ||
      duotone.grading_amount < 50.0) {
    return fail("representative cinematic, editorial, analog, food, architecture, and duotone presets must retain their intended character");
  }

  std::cout << "preset_registry_test passed\n";
  return 0;
}
