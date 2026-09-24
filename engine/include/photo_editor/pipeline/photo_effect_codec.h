#pragma once

#include "photo_editor/pipeline/photo_effect_types.h"

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace photo_editor::pipeline {

// Independent versioned envelope for embedding into document recipes and presets.
// This does not change the interpretation of any existing document recipe version.
// A successful decode is not render admission: unknown algorithms are preserved.
[[nodiscard]] std::optional<std::string> serialize_photo_effect_stack(
    std::span<const PhotoEffectInstance> effects, std::string* error = nullptr);
[[nodiscard]] std::optional<std::vector<PhotoEffectInstance>> deserialize_photo_effect_stack(
    std::string_view payload, std::string* error = nullptr);

}  // namespace photo_editor::pipeline
