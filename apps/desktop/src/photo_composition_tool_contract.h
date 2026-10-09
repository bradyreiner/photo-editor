#pragma once

#include <QStringList>

namespace photo_editor::desktop {

// These operations edit an existing layer in photoCompositionCommand. Creation,
// selection and copying from the base photo do not require a layer target.
inline const QStringList& composition_layer_target_operations() {
  static const QStringList operations{
      "text", "typography", "text_path", "transform", "perspective", "move",
      "erase_selection", "blur", "opacity", "paint", "add_effect", "update_effect",
      "remove_effect", "reorder_effect", "mask_effect", "clear_effect_mask"};
  return operations;
}

}  // namespace photo_editor::desktop
