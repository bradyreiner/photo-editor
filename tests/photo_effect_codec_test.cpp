#include "photo_editor/pipeline/photo_effect_codec.h"
#include "photo_editor/pipeline/photo_effects.h"

#include <algorithm>
#include <iostream>
#include <limits>
#include <locale>

using namespace photo_editor::pipeline;

namespace {
class CommaDecimal final : public std::numpunct<char> {
 protected:
  char do_decimal_point() const override { return ','; }
};
}

int main() {
  const auto fail = [](const char* reason) { std::cerr << reason << '\n'; return 1; };
  std::vector<PhotoEffectInstance> effects(2);
  effects[0].id = "blur-α\n\"quoted\"\\";
  effects[0].amount = 0.12345678901234567;
  effects[0].gaussian.sigma = 25.987654321098765;
  effects[0].gaussian.edge_mode = PhotoEffectEdgeMode::Reflect;
  effects[0].mask_ref = "mask-猫";
  effects[0].input_scope = PhotoEffectInputScope::CompositeBelow;
  effects[1].id = "blur-2";
  effects[1].enabled = false;
  std::string error = "old error";
  const auto encoded = serialize_photo_effect_stack(effects, &error);
  if (!encoded || !error.empty()) return fail("Could not serialize effects");
  const auto decoded = deserialize_photo_effect_stack(*encoded, &error);
  if (!decoded || *decoded != effects || !error.empty()) return fail("Exact round trip failed");
  PhotoEffectInstance zero;
  zero.id = "identity-blur";
  zero.gaussian.sigma = 0.0;
  const auto positive_zero = serialize_photo_effect_stack(std::vector{zero});
  zero.gaussian.sigma = -0.0;
  if (!positive_zero || serialize_photo_effect_stack(std::vector{zero}) != positive_zero)
    return fail("Equivalent zero-radius blur settings must have the same serialized identity");
  const auto old_locale = std::locale();
  std::locale::global(std::locale(old_locale, new CommaDecimal));
  const auto localized = serialize_photo_effect_stack(effects);
  const auto localized_read = deserialize_photo_effect_stack(*encoded);
  std::locale::global(old_locale);
  if (localized != encoded || localized_read != decoded) return fail("Effect identity depends on host locale");
  std::reverse(effects.begin(), effects.end());
  if (serialize_photo_effect_stack(effects) == encoded) return fail("Serialization ignores effect order");

  PhotoEffectInstance unknown;
  unknown.id = "future";
  unknown.type_id = "custom_future_blur";
  unknown.algorithm_version = 42;
  unknown.enabled = false;
  unknown.opaque_parameters = "{\"depth\":\"immutable:abc\",\"shape\":[1,2]}\n";
  effects.push_back(unknown);
  const auto future_payload = serialize_photo_effect_stack(effects);
  const auto future_read = future_payload ? deserialize_photo_effect_stack(*future_payload) : std::nullopt;
  if (!future_read || *future_read != effects || serialize_photo_effect_stack(*future_read) != future_payload)
    return fail("Unknown disabled effect parameters were lost");
  auto pixels = std::vector<ColorPixel>{{1, 0, 0, 1}};
  if (!apply_photo_effect(&pixels, 1, 1, future_read->back()).ok()) return fail("Disabled future effect cannot be bypassed");
  auto enabled_unknown = future_read->back();
  enabled_unknown.enabled = true;
  if (apply_photo_effect(&pixels, 1, 1, enabled_unknown).status != PhotoEffectStatus::Unsupported)
    return fail("Unknown effect became renderable without admission");

  for (const auto* malformed : {
           "photo-editor-effects:v2 0\n", "photo-editor-effects:v1 4097\n",
           "photo-editor-effects:v1 1\n", "photo-editor-effects:v1 0\ntrailing",
           "photo-editor-effects:v1 1\n\"a\" \"gaussian_blur\" -1 1 1 0 \"\" \"2 0\"",
           "photo-editor-effects:v1 1\n\"a\" \"gaussian_blur\" 1 2 1 0 \"\" \"2 0\"",
           "photo-editor-effects:v1 1\n\"a\" \"gaussian_blur\" 1 1 1 0 \"\" \"2 3\"",
           "photo-editor-effects:v1 1\n\"a\" \"gaussian_blur\" 1 1 1 0 \"\" \"2 0 extra\"",
       }) {
    if (deserialize_photo_effect_stack(malformed, &error) || error.empty())
      return fail("Malformed effect payload accepted or no error supplied");
  }
  effects[1].id = effects[0].id;
  if (serialize_photo_effect_stack(effects, &error) || error.empty()) return fail("Duplicate ID serialized");
  effects.resize(1);
  effects[0].gaussian.sigma = std::numeric_limits<double>::quiet_NaN();
  if (serialize_photo_effect_stack(effects)) return fail("Disabled invalid Gaussian serialized");
  effects[0].gaussian.sigma = 1;
  effects[0].amount = 1.1;
  if (serialize_photo_effect_stack(effects)) return fail("Out-of-range amount serialized");
  if (deserialize_photo_effect_stack(std::string(4U * 1024U * 1024U + 1, 'x')))
    return fail("Oversized payload accepted");
  const auto empty = serialize_photo_effect_stack({});
  if (!empty || !deserialize_photo_effect_stack(*empty)->empty()) return fail("Empty stack roundtrip failed");
  PhotoEffectInstance box;
  box.id = "box";
  box.type_id = "box_blur";
  box.box.radius = 17;
  box.box.edge_mode = PhotoEffectEdgeMode::Reflect;
  const std::vector boxes{box};
  const auto box_payload = serialize_photo_effect_stack(boxes);
  if (!box_payload) return fail("Box blur serialization failed");
  const auto decoded_box = deserialize_photo_effect_stack(*box_payload);
  if (!decoded_box || *decoded_box != boxes) return fail("Box radius/edge roundtrip failed");
  box.box.radius = -1;
  if (serialize_photo_effect_stack(std::vector{box})) return fail("Negative box radius accepted");
  PhotoEffectInstance invert;
  invert.id = "invert";
  invert.type_id = "invert_linear_rgb";
  const auto invert_payload = serialize_photo_effect_stack(std::vector{invert});
  if (!invert_payload || *deserialize_photo_effect_stack(*invert_payload) != std::vector{invert})
    return fail("Known parameter-free invert must roundtrip");
  invert.enabled = false;
  invert.opaque_parameters = "unexpected-settings";
  if (serialize_photo_effect_stack(std::vector{invert}))
    return fail("Known invert must reject unexpected settings even while disabled");
  if (deserialize_photo_effect_stack(
      "photo-editor-effects:v1 1\n\"invert\" \"invert_linear_rgb\" 1 0 1 0 \"\" \"unexpected-settings\"\n"))
    return fail("Malformed known invert payload must be rejected on read");
  invert.algorithm_version = 2;
  const auto future_invert = serialize_photo_effect_stack(std::vector{invert});
  if (!future_invert || *deserialize_photo_effect_stack(*future_invert) != std::vector{invert})
    return fail("Unknown future invert algorithms must retain opaque settings");
  std::cout << "Effect codec roundtrip, precision, locale, future preservation and rejection checks passed\n";
}
