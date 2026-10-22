#include "color_look_compilation_runtime.h"

#include <bit>

namespace photo_editor::pipeline {

namespace {

[[nodiscard]] std::uint64_t hash_append(
    std::uint64_t hash,
    std::uint32_t value) {
  constexpr auto kFnvPrime = std::uint64_t{1099511628211ULL};
  for (auto shift = 0U; shift < 32U; shift += 8U) {
    hash ^= static_cast<std::uint8_t>((value >> shift) & 0xffU);
    hash *= kFnvPrime;
  }
  return hash;
}

}  // namespace

std::uint64_t look_stage_program_payload_identity(
    const CompiledLookStageProgram& compiled) {
  auto identity = std::uint64_t{14695981039346656037ULL};
  const auto append_float = [&](float value) {
    identity = hash_append(identity, std::bit_cast<std::uint32_t>(value));
  };
  for (const auto value : compiled.rgb_to_xyz_d65.values) {
    append_float(static_cast<float>(value));
  }
  for (const auto value : compiled.xyz_d65_to_rgb.values) {
    append_float(static_cast<float>(value));
  }
  for (const auto value : compiled.rgb_to_selection_rgb.values) {
    append_float(static_cast<float>(value));
  }
  for (const auto value : compiled.rgb_mixer) append_float(value);
  for (const auto value : compiled.black_and_white_mixer) append_float(value);
  for (const auto value : compiled.harmony_anchors) append_float(value);
  for (const auto* values : {&compiled.grading_hue,
                             &compiled.grading_saturation,
                             &compiled.grading_luminance}) {
    for (const auto value : *values) append_float(value);
  }
  for (const auto* values : {&compiled.mixer_hue,
                             &compiled.mixer_saturation,
                             &compiled.mixer_luminance,
                             &compiled.selective_cyan,
                             &compiled.selective_magenta,
                             &compiled.selective_yellow,
                             &compiled.selective_black}) {
    for (const auto value : *values) append_float(value);
  }
  identity = hash_append(identity, compiled.active_mask);
  identity = hash_append(identity, compiled.mixer_model);
  identity = hash_append(identity, compiled.harmony_anchor_count);
  identity = hash_append(identity, compiled.replacement_preserve_luminance);
  identity = hash_append(identity, compiled.replacement_preserve_saturation);
  append_float(compiled.saturation_strength);
  append_float(compiled.vibrance_strength);
  append_float(compiled.grading_amount);
  append_float(compiled.grading_balance);
  append_float(compiled.harmony_strength);
  append_float(compiled.replacement_source_hue);
  append_float(compiled.replacement_target_oklab_hue);
  append_float(compiled.replacement_range);
  append_float(compiled.replacement_softness);
  append_float(compiled.replacement_strength);
  append_float(compiled.skin_hue);
  append_float(compiled.skin_range);
  append_float(compiled.skin_strength);
  append_float(compiled.output_headroom);
  return identity != 0U ? identity : 1U;
}

}  // namespace photo_editor::pipeline
