#pragma once

#include "edit_recipe_state.h"
#include "system_monitor_profile_state.h"

class QImage;

namespace photo_editor::desktop {

void publish_preview_image_to_display(
    QImage* image,
    const EditRecipeState& state,
    const SystemMonitorProfileSnapshot& system_profile);

}  // namespace photo_editor::desktop
