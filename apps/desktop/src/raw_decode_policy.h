#pragma once

#include "edit_recipe_state.h"
#include "raw_image_decoder.h"

namespace photo_editor::desktop {

[[nodiscard]] inline RawDecodeSettings raw_decode_settings_for_state(
    const EditRecipeState& state) {
  return RawDecodeSettings{
      .input_transform_enabled = state.raw_input_transform_enabled,
      .input_color_profile = state.input_color_profile,
      .demosaic_quality = state.raw_demosaic_quality,
      .exposure = state.raw_exposure,
      .hot_dead_pixel_cleanup_amount = state.raw_hot_dead_pixel_cleanup,
      .false_color_suppression_amount = state.raw_false_color_suppression,
      .highlight_recovery_enabled = state.raw_highlight_recovery,
      .white_balance_override_enabled = state.raw_white_balance_override,
      .white_balance_temperature = state.raw_white_balance_temperature,
      .white_balance_tint = state.raw_white_balance_tint};
}

[[nodiscard]] inline QString raw_decode_settings_key_for_state(
    const EditRecipeState& state) {
  return raw_decode_settings_cache_key(raw_decode_settings_for_state(state));
}

}  // namespace photo_editor::desktop
