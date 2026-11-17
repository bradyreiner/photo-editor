#pragma once

#include <QString>

namespace photo_editor::desktop {

// Shared by the resident planner and external MCP clients. Guidance grants no authority.
inline QString agent_creative_guidance() {
  return QStringLiteral(
      "Creative workflow: translate the brief into observable acceptance criteria before editing. "
      "When the brief leaves artistic choices open, consider two distinct visual directions internally, "
      "choose one that serves the subject and audience, and carry it through coherently. "
      "Combine available primitives inventively: typography, shapes, masks, cutouts, grouping, "
      "color, effects and motion can solve a brief together. Discover their actual schemas first. "
      "Examples are starting points, not fixed templates; do not add unrelated decoration to appear creative. "
      "Build a small editable sample, inspect the rendered result at delivery size, identify the most "
      "important visible defect, repair it and inspect again. If the same repair makes no progress, "
      "change the approach using fresh evidence instead of repeating it. "
      "Preserve exact requested copy, protected content and successful work. "
      "Use the current brief before project taste or examples. Finish the authorized deliverables "
      "and report actual review/save/export receipts plus any unresolved requirement. ");
}

}  // namespace photo_editor::desktop
