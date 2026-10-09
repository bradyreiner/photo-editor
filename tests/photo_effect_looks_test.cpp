#include "photo_effect_looks.h"
#include "photo_editor/pipeline/photo_effect_codec.h"
#include "photo_editor/pipeline/photo_effect_registry.h"
#include "photo_editor/pipeline/photo_effects.h"

#include <array>
#include <cmath>
#include <iostream>
#include <set>
#include <string>
#include <vector>

using namespace photo_editor::desktop;
using namespace photo_editor::pipeline;

#define CHECK(condition, message) do { if (!(condition)) { std::cerr << message << '\n'; return 1; } } while (false)

int main() {
  CHECK(photo_effect_looks.size() == 52, "Registry must expose 52 effect looks");
  std::set<std::string> ids;
  constexpr int width = 64;
  constexpr int height = 48;
  std::vector<ColorPixel> source(static_cast<std::size_t>(width * height));
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const auto index = static_cast<std::size_t>(y * width + x);
      source[index] = {
          static_cast<float>((x * 3 + y) % 73) / 72.0F,
          static_cast<float>((x + y * 5) % 67) / 66.0F,
          static_cast<float>((x * 7 + y * 2) % 79) / 78.0F,
          0.2F + static_cast<float>((x + y) % 8) / 10.0F};
    }
  }

  for (const auto& look : photo_effect_looks) {
    CHECK(!look.id.empty() && ids.insert(std::string(look.id)).second, "Look IDs must be nonempty and unique");
    CHECK(!look.name.empty() && !look.category.empty() && !look.description.empty(), "Look metadata must be complete");
    CHECK(look.stage_count > 0 && look.stage_count <= look.stages.size(), "Look must contain supported stages");
    std::vector<PhotoEffectInstance> effects;
    for (unsigned int i = 0; i < look.stage_count; ++i) {
      const auto effect_id = std::string(look.id) + "-" + std::to_string(i);
      auto effect = materialize_photo_effect_look_stage(
          look.stages[i], effect_id, PhotoEffectInputScope::LayerContent);
      CHECK(find_photo_effect(effect.type_id, effect.algorithm_version), "Every look stage must use a registered operator");
      effects.push_back(std::move(effect));
    }

    std::string error;
    const auto encoded = serialize_photo_effect_stack(effects, &error);
    CHECK(encoded.has_value(), "Look parameters must serialize: " + error);
    const auto decoded = deserialize_photo_effect_stack(*encoded, &error);
    CHECK(decoded.has_value() && *decoded == effects, "Look parameters must survive recipe serialization: " + error);

    auto rendered = source;
    CHECK(apply_photo_effect_stack(&rendered, width, height, *decoded).ok(),
        "Look stages must render on a nonuniform RGBA fixture");
    auto repeated = source;
    CHECK(apply_photo_effect_stack(&repeated, width, height, *decoded).ok() && repeated == rendered,
        "Look rendering must be deterministic");
    bool changed = false;
    for (std::size_t i = 0; i < source.size(); ++i) {
      CHECK(std::isfinite(rendered[i].red) && std::isfinite(rendered[i].green) &&
          std::isfinite(rendered[i].blue) && std::isfinite(rendered[i].alpha) &&
          rendered[i].alpha >= 0.0F && rendered[i].alpha <= 1.0F,
          "Look output must contain finite colors and bounded alpha");
      changed = changed || rendered[i] != source[i];
    }
    CHECK(changed, "Every look must change the photographic fixture");
  }
  CHECK(ids.size() == 52, "Every registered look must have a unique identifier");
  return 0;
}
