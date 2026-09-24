#pragma once

#include "edit_recipe_state.h"

#include <cmath>

namespace photo_editor::desktop {

inline bool has_structured_linear_mask(const EditRecipeState& state) {
  return state.linear_mask_enabled || std::abs(state.linear_mask_exposure) > 1e-6 ||
         std::abs(state.linear_mask_temperature) > 1e-6 ||
         std::abs(state.linear_mask_feather - 0.22) > 1e-6 ||
         std::abs(state.linear_mask_position - 0.5) > 1e-6 ||
         std::abs(state.linear_mask_rotation) > 1e-6;
}

inline bool has_structured_radial_mask(const EditRecipeState& state) {
  return state.radial_mask_enabled || std::abs(state.radial_mask_exposure) > 1e-6 ||
         std::abs(state.radial_mask_temperature) > 1e-6 ||
         std::abs(state.radial_mask_feather - 0.25) > 1e-6 ||
         std::abs(state.radial_mask_center_x - 0.5) > 1e-6 ||
         std::abs(state.radial_mask_center_y - 0.5) > 1e-6 ||
         std::abs(state.radial_mask_radius_x - 0.25) > 1e-6 ||
         std::abs(state.radial_mask_radius_y - 0.25) > 1e-6 ||
         std::abs(state.radial_mask_rotation) > 1e-6;
}

}  // namespace photo_editor::desktop
