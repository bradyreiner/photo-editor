#pragma once

#include <string>
#include <vector>

namespace photo_editor::pipeline {

struct AdjustmentBlock final {
  std::string id;
  std::string label;
};

struct MaskDefinition final {
  std::string id;
  std::string kind;
  bool ai_assisted = false;
};

struct EditRecipe final {
  int recipe_version = 2;
  std::string asset_id;
  std::vector<AdjustmentBlock> global_adjustments;
  std::vector<MaskDefinition> mask_stack;
};

}  // namespace photo_editor::pipeline
