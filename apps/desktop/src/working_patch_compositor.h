#pragma once

#include "working_image.h"

#include <QRect>

#include <algorithm>
#include <cstddef>

namespace photo_editor::desktop {

struct WorkingPatchCopyJob final {
  const WorkingImage* working = nullptr;
  QRect source_rect;
  QRect destination_rect;
};

[[nodiscard]] inline bool copy_working_rect_to_working_patch(
    const WorkingPatchCopyJob& job,
    WorkingImage* patch,
    photo_editor::pipeline::CancellationToken cancellation) {
  if (job.working == nullptr || job.working->isNull() || patch == nullptr || patch->isNull() ||
      job.source_rect.isEmpty() || job.destination_rect.isEmpty() ||
      job.source_rect.size() != job.destination_rect.size() ||
      !QRect(0, 0, job.working->width, job.working->height).contains(job.source_rect) ||
      !QRect(0, 0, patch->width, patch->height).contains(job.destination_rect)) {
    return false;
  }
  for (int y = 0; y < job.source_rect.height(); ++y) {
    if (cancellation.cancelled()) {
      return false;
    }
    const auto source_offset = static_cast<std::size_t>(
        (job.source_rect.y() + y) * job.working->width + job.source_rect.x());
    const auto destination_offset = static_cast<std::size_t>(
        (job.destination_rect.y() + y) * patch->width + job.destination_rect.x());
    std::copy_n(
        job.working->pixels.cbegin() + static_cast<std::ptrdiff_t>(source_offset),
        job.source_rect.width(),
        patch->pixels.begin() + static_cast<std::ptrdiff_t>(destination_offset));
  }
  return true;
}

}  // namespace photo_editor::desktop
