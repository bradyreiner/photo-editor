#pragma once
#include <cstddef>

namespace photo_editor::desktop {
// Static imported compositions may combine a detailed subject and environment.
// Keep this shared by loading, focus, scan visibility and rendering so an
// admitted scene remains usable throughout the photographic workflow.
inline constexpr std::size_t gaussian_static_point_limit=4000000;
}
