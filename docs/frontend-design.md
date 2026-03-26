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

## V1 visual system

This section turns the philosophy into concrete tokens and layout rules that can be implemented in QML.

### Color tokens

Use a restrained studio palette with one warm family and one cool family.

#### Core neutrals

- `ink-950`: `#0c0f12`
- `ink-900`: `#11161a`
- `ink-850`: `#151b20`
- `ink-800`: `#1a2127`
- `ink-750`: `#202830`
- `ink-700`: `#26303a`
- `line-strong`: `#313c46`
- `line-soft`: `#252d35`

#### Text

- `text-strong`: `#f3f5f6`
- `text-main`: `#d9dee3`
- `text-muted`: `#98a3ad`
- `text-faint`: `#727d88`

#### Warm edit accent

- `accent-warm-500`: `#c79a5a`
- `accent-warm-400`: `#ddb77f`
- `accent-warm-300`: `#edd4ae`
- `accent-warm-fill`: `#2f2418`

#### Cool mask accent

- `accent-cool-500`: `#6bb7c8`
- `accent-cool-400`: `#8dd2e1`
- `accent-cool-300`: `#b8e8f0`
- `accent-cool-fill`: `#15252c`

#### State

- `success`: `#7fa87c`
- `warning`: `#b98f54`
- `error`: `#a56a5e`

### Surface recipes

Do not use one flat dark color everywhere. Assign surfaces by role.

- app background: `ink-950` with a subtle top-to-bottom graphite gradient
- primary workspace shell: `ink-900`
- side rails: `ink-850`
- inspector cards or grouped sections: `ink-800`
- inset controls: `ink-900`
- hover state: move one step lighter in the scale
- active fill: tinted version of the accent family at low alpha

### Typography

The UI needs three voices.

#### 1. Display voice

Use for workspace headers, major panel titles, and key empty states.

Recommended if shipping custom fonts:

- `Instrument Serif`
- `Publico`
- `GT Super Display`
- `Canela Text`

Recommended if staying system-only for now:

- macOS: `"Avenir Next Condensed"` or `"Helvetica Neue"`
- Windows fallback: `"Bahnschrift"` or `"Segoe UI Variable Display"`

Use it sparingly. The goal is identity, not ornament.

#### 2. UI voice

Use for controls, labels, menus, and standard body copy.

- current practical choice: `"Avenir Next"`
- cross-platform fallback target: a neutral sans with strong legibility

#### 3. Technical voice

Use for zoom, EV values, dimensions, capture info, queue states, and filenames.

- `"SF Mono"`
- `"Menlo"`
- `"Consolas"`

### Type scale

- workspace title: `30-34px`, medium or demi-bold
- panel title: `16-18px`, medium
- section kicker: `10-11px`, uppercase, tracked out
- body label: `11-12px`
- metadata/body copy: `12px`
- technical readout: `10-11px`
- hero metric: `22-26px` when needed

### Spacing scale

Use a tight professional spacing system.

- `4`: micro spacing inside controls
- `8`: between related control atoms
- `12`: default gap between controls
- `16`: panel inner padding
- `20`: major inset padding
- `24`: workspace gutter
- `32`: large separation between zones

Avoid random one-off values unless they are tied to image framing.

### Radius scale

- `4`: tiny pills and markers
- `8`: controls
- `12`: grouped sections
- `16`: large shells
- `20`: only for major outer rails if needed

Image frames should stay sharper than controls.

### Borders and highlights

Prefer low-contrast border systems over heavy shadows.

- outer shell border: `1px` using `line-soft`
- active shell border: accent family at `12-18%` alpha
- inset control border: optional, only when needed for affordance
- highlight rule: `2-3px` accent line on active panels or selected modules

### Motion tokens

Keep motion short and measured.

- hover fade: `90-120ms`
- panel reveal: `160-220ms`
- workspace transition: `220-280ms`
- compare or overlay wipe: `140-180ms`

Preferred feel:

- `OutCubic` or `InOutCubic` for panels
- linear or near-linear for sliders and scrub interactions

## Component guidance

### Sidebar rail

The left navigation should feel like a machined tool rail.

- slightly narrower and more deliberate than a generic app sidebar
- selected item gets an illuminated edge and a darker nested well
- icons should look constructed, not rounded and playful
- consider adding one vertical label or section mark near the top for identity

### Brand panel

Current `BrandPanel` is close, but it should feel less like a card.

- reduce the sense of floating rectangles
- use a stronger header treatment
- consider a top alignment rule or inset title band instead of a standalone accent dash

### Inspector sections

The inspector should read like one calibrated instrument wall.

- fewer visible boxed sections
- more use of dividers and spacing rhythm
- values aligned in a way that feels systematic
- active controls inherit the workspace accent color

### Buttons

Buttons should be flatter, denser, and more intentional.

- primary actions: warm fill with dark text
- secondary actions: ink surface with line and hover lift
- active toggles: tinted accent fill, not full bright accent

### Sliders

Sliders are one of the strongest opportunities for ownable design.

- slimmer tracks
- more precise handles
- mono value chips
- center markers and color tracks where meaningful
- allow selected slider rows to subtly tint the entire row

### Combo boxes and fields

These should look like instrument inputs, not forms.

- stronger inset feel
- higher text contrast
- use mono for technical dropdowns where appropriate
- keep menu surfaces compact and dense

## Edit workspace blueprint

This should be the flagship composition.

### Layout

Use a four-zone structure:

1. left rail: tool mode, compare, crop, reference, navigator
2. center stage: image canvas with framed presentation
3. bottom filmstrip: active sequence and selection context
4. right rail: inspector stack with grouped edit controls

### Center stage

The canvas should feel mounted, not simply stretched into a rectangle.

- deep graphite surround
- slightly inset image frame
- optional subtle shadow or edge glow around the image
- zoom and compare readouts anchored to corners in mono labels

### Left rail

Keep this sparse and mode-driven.

- hand, crop, compare, before-after, mask, loupe
- show only tools that affect the current viewing mode
- avoid putting many text labels here

### Bottom filmstrip

This is where the app can feel more editorial.

- contact-sheet framing
- strong selected-state treatment
- metadata below or beside selection in mono
- quick flags, ratings, and status marks can live here later

### Right rail

Organize by user intent, not engine implementation.

Suggested group order:

- Light
- Color
- Presence
- Detail
- Optics
- Effects
- Geometry

Each group should be collapsible and scannable at a glance.

### Top context band

Avoid a heavy top toolbar. Instead use a lean context band inside the edit shell.

Suggested contents:

- current image name
- capture metadata
- zoom percentage
- compare status
- undo and redo

This should feel like a proofing header, not a website navbar.

## Library blueprint

The current Library can be pushed toward a stronger contact-sheet identity.

- reduce the dashboard-card feeling in the header
- make filters feel like a compact editorial control strip
- let the grid carry more of the visual weight
- selected assets should feel physically lifted from the sheet

## Implementation plan

Implement in this order:

1. create shared color, type, spacing, and motion tokens in QML
2. restyle shell primitives: `BrandPanel`, `ActionButton`, `SidebarButton`, `InspectorSection`, `InspectorSlider`, `ToneComboBox`
3. reshape `EditView` into the flagship four-zone layout
4. bring `LibraryView` closer to a contact-sheet workflow
5. tune `MaskView` and `ExportView` so each workspace has its own tonal identity

## Practical first pass for this repo

If we want a realistic first implementation without adding asset pipelines yet:

- keep `"Avenir Next"` for base UI
- use `"Avenir Next Condensed"` for major headings where available
- keep `"Menlo"` for technical readouts
- introduce centralized color tokens
- redesign `EditView` first, because it will define the product's identity fastest
