# Edit adjustment UI refinement

The shared application palette now uses neutral charcoal and gray values for
chrome, fields, buttons, popups, text, and selection fills. Semantic status colors,
color-editing gradients, and brand light colors retain their functional hues.

The adjustment stack follows this visible order: RAW (when available), White
Balance, Light, Tone Curve, Levels, Color, Color Mixer, Color Grading, Presence,
Detail, Lens, then Specialist Color. This is presentation order; rendering order
and saved adjustment values are preserved.

Core panels stay open. Tone shaping and tone ranges start expanded. Specialist
headings are visible without an additional Extended controls mode switch. The
Expand controls action opens every secondary adjustment subgroup. Focus tabs and
search continue to filter the same target-aware adjustment controls.

The Color Mixer presents eight named color ranges, one selected-channel label,
Hue/Saturation/Luminance sliders, a response selector, the image adjustment tool,
and a labeled Selective Color CMYK subgroup. The XY pad, redundant numeric
summary, and duplicate range strip are removed. The response selector identifies
the actual saved color model (OKLab, HSL, HSV, or LCH), including legacy HSL edits.
Resetting the selected HSL range is one undo operation.

Edit sliders use a shared compact row with aligned labels, numeric inputs, and
reset controls. Narrow panels switch to a taller two-row layout to preserve a
usable slider track. Grading Amount and Balance use the same slider component.
The grading wheel is capped at 160 logical pixels and further bounded by window
height and available width. The curve editor is bounded to 300 logical pixels.

Native proof uses the real Main.qml shell and an isolated photographic fixture.
It checks section order, neutral theme values, visible primary controls, slider
geometry, preserved channel state, numeric input, compound reset undo, and a
complete grading layout at 1280 by 760. Captures and test logs live under
`docs/evidence/edit-adjustments-2026-10-22`.
