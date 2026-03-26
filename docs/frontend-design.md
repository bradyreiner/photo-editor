# Frontend Design Direction

## Design philosophy

The product should feel like an editorial light table, not a generic creative dashboard.

The reference point is the photographer's working surface:

- a large illuminated image stage
- contact sheets and selects arranged with intent
- technical controls that feel precise, quiet, and tactile
- metadata presented like production notes, not admin chrome

The app should feel:

- premium
- precise
- creative
- calm under heavy use
- editorial rather than utilitarian
- distinct from the usual "dark SaaS panel" aesthetic

## North star

Design around one tension:

- the photo is emotional and expressive
- the tools are disciplined and exact

The interface should support that tension rather than flatten it. The image gets space, contrast, and drama. The controls feel engineered, dense, and dependable. The result should feel closer to a studio desk or print lab than a spreadsheet with sliders.

## Core principles

### 1. Canvas is the stage

The image is always the hero. Surrounding UI exists to support judgment, not compete for attention.

- Keep the edit canvas dominant in every workspace.
- Avoid decorative elements inside the image area.
- Use framing, shadow, and negative space to make the image feel placed, not merely displayed.

### 2. Chrome should feel architectural

Panels should read like rails, trays, and instrument surfaces rather than floating cards.

- Favor long vertical tool rails and grounded side panels.
- Use panel grouping to create rhythm instead of many isolated boxes.
- Make dividers, edges, and spacing do more of the work than colored fills.

### 3. Typography should create hierarchy and mood

Use typography to separate creative context from technical control.

- Workspace names and major section titles can be larger and more expressive.
- Everyday controls should stay neutral and highly legible.
- Metadata, numeric values, and system states should use a mono face.

Suggested split:

- display or heading face: editorial, condensed, or elegant grotesk
- UI face: straightforward sans
- technical face: mono for values, file info, zoom, and export state

### 4. Accent color must carry meaning

Accent color is not decoration. It should indicate action, mode, and confidence.

- warm accent: active tools, focus, selected state, primary call to action
- cool accent: masks, overlays, compare state, analysis surfaces
- neutrals do the baseline work

The UI should stay mostly restrained so color retains signal value.

### 5. Motion should feel mechanical, not playful

Animation should reinforce workspace changes and tool depth.

- panels slide like drawers
- compare and mask overlays reveal with crisp wipes or measured fades
- selected assets lock into place rather than bounce

Avoid soft, floaty, generic micro-animation.

### 6. Density should feel curated

Professional tools need density, but density should feel composed rather than crowded.

- group controls by intent instead of by underlying math
- reveal depth progressively
- keep the first read simple, with expert detail layered underneath

## Visual language

### Palette

Base the UI on dark graphite and ink neutrals rather than pure black.

- background: charcoal, graphite, smoked blue-black
- surface: slightly warmer dark neutrals for panels
- highlight: muted brass or amber
- mask/system cool: cyan or glacial blue
- destructive/error: restrained rust, not bright red

This creates a studio-night feel without looking like a gaming UI.

### Shape

Use a mix of crisp structure and selective rounding.

- main shells and panels: restrained radius
- pills and status markers: more rounded
- image framing: sharp and deliberate

Too much roundness makes the app feel soft and generic.

### Texture

Keep texture subtle but present.

- quiet tonal gradients on large surfaces
- faint edge highlights on rails and inspectors
- occasional grain or noise only if it remains extremely restrained

The goal is atmosphere, not ornament.

## Signature elements

These are the parts that can make the app feel ownable:

- a narrow illuminated edge or accent rule on key active panels
- mono exposure, zoom, and state readouts that feel like instrument labels
- contact-sheet inspired selection treatments in Library
- compare mode divider that feels like a real proofing tool, not a generic splitter
- mask overlays that use the cool accent family consistently across the app
- workspace headers that feel like magazine section openers rather than default software tabs

## Workspace expression

### Library

The Library should feel like an edit desk for sorting and sequencing.

- lean into contact sheet language
- make selection, rating, and grouping feel fast and editorial
- surface project, capture, and import context as production information

### Import

Import should feel like intake and triage, not file management.

- emphasize source, destination, and selection confidence
- show what is about to happen clearly
- treat folders and batches as physical groups entering the system

### Edit

Edit is the core stage.

- the canvas dominates
- the inspector feels like a calibrated instrument rail
- zoom, compare, histogram, and value readouts feel deliberate and exact

### Mask

Mask should shift slightly cooler and more analytical.

- local adjustments need clearer overlay language
- controls should feel surgical
- the preview should communicate shape, feather, and influence immediately

### Export

Export should feel like a print and delivery bench.

- queue state needs operational clarity
- presets should feel like production recipes
- destinations and format decisions should read as final-output decisions, not form fields

## Anti-goals

Avoid these traps:

- generic frosted-glass creative app styling
- bright accent color sprayed across every control
- too many boxed cards fighting for attention
- oversized padding that wastes space in a professional workflow
- playful motion that undermines precision
- looking like Lightroom, Notion, or Figma by default

## Practical implementation rules

- Keep the image canvas dominant.
- Use restrained dark neutrals for long editing sessions.
- Reserve accent color for active state, masks, and high-signal actions.
- Avoid stock control styling by building custom QML primitives.
- Use larger, intentional typography at workspace and panel headers.
- Prefer persistent rails and anchored inspectors over floating popovers.
- Let spacing and alignment create hierarchy before adding more borders or color.
- Group controls by task intent: light, color, detail, mask, output.

## Primary workspaces

- Library: browse, cull, compare, organize, filter
- Edit: hero-image canvas and manual inspector
- Mask: dedicated mask stack and local adjustment workflow
- Export: queue management and delivery presets
