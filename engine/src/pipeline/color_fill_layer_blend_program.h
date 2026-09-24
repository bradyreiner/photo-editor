#pragma once

#include <cstdint>
#include <string_view>

namespace photo_editor::pipeline {

[[nodiscard]] std::uint32_t retained_layer_blend_mode_id(
    std::string_view mode);

}  // namespace photo_editor::pipeline
