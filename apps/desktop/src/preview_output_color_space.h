#pragma once

#include "edit_recipe_state.h"
#include "working_image.h"

#include <QColorSpace>

namespace photo_editor::desktop {

[[nodiscard]] inline QColorSpace preview_output_color_space_for_name(
    const QString& name) {
  return preview_color_space_for_name(
      normalized_output_color_space_name(name));
}

}  // namespace photo_editor::desktop
