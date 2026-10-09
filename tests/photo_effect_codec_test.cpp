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

  PhotoEffectInstance grain;
  grain.id = "grain";
  grain.type_id = "film_grain";
  grain.grain = {1.375, 0.027, -0.62, 20261007U};
  PhotoEffectInstance leak;
  leak.id = "leak";
  leak.type_id = "light_leak";
  leak.leak = {-0.14, 1.12, 32.5, 0.54, 0.31, 0.76, 913U};
  PhotoEffectInstance split;
  split.id = "split";
  split.type_id = "split_tone";
  split.split_tone = {28.0, 0.21, 221.0, 0.13, -0.34};
  PhotoEffectInstance vignette;
  vignette.id = "vignette";
  vignette.type_id = "vignette";
  vignette.vignette = {0.76, 0.24, 0.48, 0.65};
  PhotoEffectInstance duotone;
  duotone.id = "duotone";
  duotone.type_id = "duotone";
  duotone.duotone = {248.0, 43.0, 0.81, 0.17};
  PhotoEffectInstance fade;
  fade.id = "fade";
  fade.type_id = "soft_fade";
  fade.soft_fade = {0.067, 0.88, 0.93};
  const std::vector new_effects{grain, leak, split, vignette, duotone, fade};
  const auto new_payload = serialize_photo_effect_stack(new_effects);
  const auto new_decoded = new_payload ? deserialize_photo_effect_stack(*new_payload) : std::nullopt;
  if (!new_decoded || *new_decoded != new_effects || serialize_photo_effect_stack(*new_decoded) != new_payload)
    return fail("Six new photographic effect parameters must roundtrip exactly");
  auto invalid_grain = grain;
  invalid_grain.grain.strength = 0.51;
  if (serialize_photo_effect_stack(std::vector{invalid_grain}))
    return fail("Out-of-range film grain strength serialized");
  auto invalid_leak = leak;
  invalid_leak.leak.center_x = 2.01;
  if (serialize_photo_effect_stack(std::vector{invalid_leak}))
    return fail("Out-of-range light leak position serialized");
  auto invalid_tone = split;
  invalid_tone.split_tone.highlight_hue = 361.0;
  if (serialize_photo_effect_stack(std::vector{invalid_tone}))
    return fail("Out-of-range split tone hue serialized");
  auto invalid_vignette = vignette;
  invalid_vignette.vignette.feather = -0.01;
  if (serialize_photo_effect_stack(std::vector{invalid_vignette}))
    return fail("Negative vignette feather serialized");
  auto invalid_duotone = duotone;
  invalid_duotone.duotone.balance = 1.01;
  if (serialize_photo_effect_stack(std::vector{invalid_duotone}))
    return fail("Out-of-range duotone balance serialized");
  auto invalid_fade = fade;
  invalid_fade.soft_fade.saturation = std::numeric_limits<double>::infinity();
  if (serialize_photo_effect_stack(std::vector{invalid_fade}))
    return fail("Non-finite soft fade saturation serialized");
  std::cout << "Effect codec roundtrip, precision, locale, future preservation and rejection checks passed\n";
}
