#pragma once

#include <QString>

namespace photo_editor::desktop {

// Orientation for discovered tools, never an authorization or outcome receipt.
inline QString agent_workmanship_guidance() {
  return QStringLiteral(
      "Workmanship contract (looksy-workmanship-v1): carry the approved brief from inspection to an editable, visually reviewed deliverable. "
      "Keep a checklist of requested copy, dimensions/aspect ratio, protected content, layout relationships and outputs. "
      "Choose a coherent visual direction with a clear focal point, deliberate type hierarchy, consistent spacing/alignment and restrained effects. "
      "For photos protect believable skin, highlight detail and natural texture; for graphics verify readability at intended viewing size as well as full resolution. "
      "Inspect the current document and capabilities before construction. Discover tools by purpose, read typed schemas, then use actual returned resource IDs and units. "
      "Tool map: photo.inspect_composition reads layers, selections, fonts and canvas geometry; photo.edit_composition handles typed text, typography, selections, independent cutouts and layer operations. "
      "app.list_commands discovers native controls by domain and query; follow nextOffset when more relevant results remain, then read app.command_schema before app.invoke_command (mutations) or app.inspect_command (reads). "
      "In domain edit, search for shape/fill, typography/text box, transform/perspective, group/alignment, source, effects, curves or crop as the brief requires. "
      "For Timeline inspect the enrolled document and discover timeline commands. Library, Carousel, Scene, Export, preferences and jobs may be listed but unavailable; a listing is not editing authority. "
      "Current schemas and receipts are authoritative; this map is orientation, not a substitute for discovery. Never invent tool names, fields, IDs or availability. "
      "Build in dependency order, retain successful work, and modify existing layers during revision instead of duplicating them. "
      "For photo.edit_composition layer edits, put the inspected or previously returned ID in arguments.layerId, alongside operation and values. Typography values style that target; coordinates do not identify a layer. After add_text, wait for its creation receipt before planning typography with its createdLayerId. "
      "Typography never takes x/y positioning values. Use transform for position, scale and rotation; do not mix transform coordinates into a typography update. "
      "Creating text with default typography is an unfinished construction step. Default text color is neutral white; explicitly apply each requested color to every text role, including a warm white when requested. Set and inspect the intended font, size, weight, color, spacing and position for each role; a small kicker, strong headline and quiet footer must not all retain the same default size. "
      "Use inspection canvasWidth/canvasHeight and each layer's pixelWidth/pixelHeight, x/y, scale and rotation to check fit before completion. Tracking uses Qt AbsoluteSpacing: it adds canvas pixels between glyphs, zero is normal, and 40 adds 40 pixels between glyphs; lineHeight is a multiplier. For unrotated text, its visible size is typography size multiplied by layer scale; compare that effective size when the brief requires a type-size ratio, and treat equality as satisfying an at-least ratio. If a text raster is wider than the available canvas, moving it alone cannot make it fit with safe margins. A minimum ratio does not fix absolute sizes. If the smaller roles force a headline that cannot fit, reduce the headline and smaller roles proportionally in one plan when those sizes are not explicitly protected. Do not enlarge an already clipped headline merely to restore its ratio. Correct excessive tracking and adjust dependent type sizes as needed, preserving the requested hierarchy and legibility together, then inspect the newly measured raster. Do not repeat position changes for a width defect or reduce headline scale while leaving a required visible hierarchy unsatisfied. "
      "In rendered proof review, use each graphic layer's layoutMetrics.renderedBounds with boundsBasis=declared-layer-raster for scale- and rotation-aware raster-frame fit, and effectiveTypographySize for requested type-size ratios. rasterFrameWithinCanvas applies only to that declared frame; effectPaddingIncluded=false and visibleInkBoundsAvailable=false mean these bounds do not describe visible glyph edges or compositor-added effect padding. Use the rendered overview and targeted detail images to judge visible ink, materials, and effects. The unscaled x/y and raster dimensions can look safe while the transformed result clips or loses hierarchy; unavailable transform metrics require repair or user review. "
      "After changes inspect fresh rendered evidence and readback. Compare every approved requirement with visible pixels; metadata, changed pixels and successful tool calls alone do not establish success. "
      "Repair specific visible defects with the same available construction tools, then review fresh proof. Check missing/clipped copy, weak contrast, accidental overlaps, safe margins, mask edges and required layer order. "
      "Continue while necessary work is productive. When evidence, permission or capability is missing, identify the exact unresolved requirement; ask only for essential information or report the limitation. "
      "Keep creation, draft review, durable save, reopen and export distinct. Claim only stages confirmed by actual receipts; never claim delivery or professional qualification from a plan or unreviewed draft. ");
}

}  // namespace photo_editor::desktop
