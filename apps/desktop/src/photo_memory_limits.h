#pragma once

#include <cstdint>

namespace photo_editor::desktop {

inline constexpr auto kMaximumPhotoWorkingSurfaceBytes =
    std::uint64_t{1536U} * 1024U * 1024U;

}  // namespace photo_editor::desktop
