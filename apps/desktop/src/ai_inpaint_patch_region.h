#pragma once

#include "ai_model_runtime.h"

#include <QRect>

namespace photo_editor::desktop {

// Model context remains unchanged. Persist only the reviewed repair region,
// which already includes its seam padding; preserve its original pixels.
inline bool retain_reviewed_inpaint_output(AiInpaintResult* result, const QRect& reviewed) {
  if (result == nullptr || !reviewed.isValid() || result->patch_image.isNull()) return false;
  const QRect context(result->patch_x, result->patch_y, result->patch_width, result->patch_height);
  if (!context.contains(reviewed) || result->patch_image.size() != context.size()) return false;
  for (const auto& candidate : result->candidate_images)
    if (candidate.size() != context.size()) return false;
  const QRect local(reviewed.topLeft() - context.topLeft(), reviewed.size());
  const auto patch = result->patch_image.copy(local);
  if (patch.isNull()) return false;
  auto candidates = result->candidate_images;
  for (auto& candidate : candidates) {
    candidate = candidate.copy(local);
    if (candidate.isNull()) return false;
  }
  result->patch_image = patch;
  result->candidate_images = candidates;
  result->patch_x = reviewed.x();
  result->patch_y = reviewed.y();
  result->patch_width = reviewed.width();
  result->patch_height = reviewed.height();
  return true;
}

}  // namespace photo_editor::desktop
