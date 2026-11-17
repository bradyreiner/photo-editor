# Complete video production and agent authoring improvement plan

Updated: 2026-10-31. Parent work package: [APP-12](app-improvement-plan.md#app-12--complete-video-production-and-agent-authoring).

## Product goal

Let a capable LLM enter Looksy with a brief and available media, understand the
project, plan the story, find suitable shots, build and refine the edit, author
graphics, shape color, mix sound, review moving output, respond to notes and deliver
finished videos plus an editable native project. Humans must be able to adjust the
same clips, layers, paths, controllers, curves, effects and compositions in the UI.

This covers the complete editing product: the human workspace, the native media
and rendering engines, the MCP interface, and the agent's production/review loop.
An agent must be able to see what it is editing, understand human feedback and
judge the resulting sequence. A person must be able to open that same project,
understand its structure and continue editing comfortably.

`brief → media discovery → script/storyboard → assembly → picture edit → color/compositing/motion → sound → review/revision → delivery → reopen/handoff`

## What the complete product must enable

The target is a usable production environment for a person working alone, an
agent working from a brief, or both working on the same edit. That includes the
ordinary editing work that makes advanced graphics useful: choosing the right
shot, trimming it well, hearing the mix, finding an effect, understanding a note
and making a small revision without disturbing the rest of the film.

| Stage | Required connected workflow | Result to inspect |
| --- | --- | --- |
| Plan | Turn a brief and references into a script, shot plan and storyboard; map claims and examples to chapters | A coherent story with suitable media and purposeful pacing |
| Prepare | Import, search, cull and organize photos, video and sound; select useful source ranges | A browsable library with identified sources, alternatives and dependencies |
| Edit | Assemble and refine picture and linked sound with readable tracks, precise trims and responsive playback | An understandable, editable sequence that works before decorative graphics |
| Finish | Combine RAW/color work, pixel edits, masks, retouching, layers, text, effects, animation, carousel designs and Scene output where supported | Actual image changes and connected compositions with controls a human can revise |
| Mix and review | Audition music and SFX, shape the mix, inspect frames and moving excerpts, collect notes and compare revisions | A sequence whose picture, motion and sound have been reviewed together |
| Deliver | Adapt framing and layout, export, inspect the finished files and reopen the native project | Finished requested formats and a reproducible editable handoff |

Cross-workspace continuity is part of this goal. A graded photograph used in a
poster, split carousel or timeline must have an explicit source/version relationship.
Changing its crop, mask or grade must offer understandable refresh behavior without
silently changing accepted output. Scene/Gaussian renders need the same dependency,
timing, color and update contracts when used in a video. Unsupported combinations
must be discoverable before an agent builds a scene around them.

### First implementation milestones

Use these milestones to organize the work packages below. Each must produce a
usable improvement to the ongoing edit; adding a command or an isolated visual
effect is insufficient.

1. **See, hear and understand the existing project.** A fresh agent can discover
   the cut, identify clips and sources, read the user's notes, retrieve actual
   requested frames and review a synchronized excerpt. Record current availability
   for each operation. If frame capture or audio review is unavailable, identify
   that specific gap instead of treating metadata as review. Covers VP-01/03/08/09.
2. **Make the large timeline comfortable to edit.** Complete playback, scrubbing,
   playhead following, scrolling, clip identity, track reuse and the Library/Effect
   Controls/effects-browser layout. A person and agent can select a clip, adjust an
   effect or keyframe, undo it and save/reopen the result. Covers VP-04/10.
3. **Complete one chapter through every production stage.** Keep its story and
   source selection, author real image changes and advanced graphics, mix sound,
   review the moving result, apply an anchored note and export it. Use this to
   expose missing connections between tools before expanding the whole film.
   Covers VP-02/05/06/07/09 and the applicable MG packages.
4. **Carry the workflow through the full film and variants.** Refine every chapter,
   review continuity and the complete mix, adapt both orientations and inspect the
   delivered files. Reopen and make a human revision in the native project.
   Covers VP-11/12 and the remaining production requirements.

Milestone 3 qualifies the connected workflow on a bounded section; milestone 4
remains necessary for completion. Keep the same accepted project and creative
decisions throughout these passes.

## Production principles

Motion graphics is one workstream within this production system. Success requires
editorial judgment, visible demonstrations, audiovisual review, responsive editing
and reliable project lifecycle as well as animation. Access to individual methods
or a collection of rendered overlays does not establish a complete workflow.

The current trailer remains the production target. Preserve successful art
direction and accepted edits while improving it. Do not restart the film or replace
native authoring with a flattened external render to satisfy these requirements.
Derived preview caches are acceptable when the editable source remains authoritative.
The broader product must also support films, travel/documentary edits, interviews,
tutorials and social deliverables. Do not hardcode this trailer's chapters, fonts,
aspect ratios or creative choices as universal defaults.

This is proposed work, not an implementation or release-completion claim. The
[Timeline roadmap](timeline-editor-roadmap.md), [readiness gates](timeline-video-editing-readiness-2026-10-22.md),
[MCP integration plan](mcp-agent-tool-integration-plan.md), and existing capability
and release authorities retain ownership of their requirements.

## Scope and evidence boundary

An agent needs to discover, observe, act, evaluate and revise across the whole app.
Native structured tools should be the primary authoring interface, with computer
use available for direct manipulation and inspection. Provider-specific prompts,
private scripts, direct project-file rewriting or knowledge of one workstation
must not be prerequisites for another capable model to complete a supported edit.

Local models and external providers should share versioned native contracts.
Generative media is optional, explicitly requested work; editing, search, grading,
masking and animation must remain usable without generation.

The inspected schemas below cover only part of this expanded plan. Audit each
stage before deciding whether to expose an existing implementation, repair it or
add a missing feature. Keep implemented, exposed and qualified status separate.

## Observed motion and review foundation

The live isolated trailer worker's command schemas were inspected on 2026-10-30.
This verifies discoverable authoring contracts, not every rendered combination.
See the retained [schema snapshot](../marketing/native-recreation-v4/evidence/timeline-motion-authoring-capability-schemas-current.json)
and [tool catalog](../marketing/native-recreation-v4/evidence/timeline-polish-capability-tools-current.json).

| Area | Exposed foundation | Work to audit or complete |
| --- | --- | --- |
| Shapes and typography | Editable titles, rectangles, rounded rectangles and ellipses | Rich vector operations, text sequencing, layout and reusable controls |
| Procedural content | Desktop light, transition veil, linear gradient, fractal noise and animated vector-path generators | General parameter animation, composition with effects and creative qualification |
| Animation | Channels with hold, linear, ease-in/out and Bezier interpolation | Explicit tangent control, procedural controllers, hierarchy and efficient revisions |
| Perspective | Four-corner poses, linear/hold interpolation and trajectory validation | Coherent camera, depth, parenting and easing workflow |
| Compositing | Blend operations, rectangle/ellipse masks and file-backed alpha/luminance mattes | Editable linked mattes, richer mask animation and advanced light/effect workflows |
| Transactions | Reversible batches of up to 16 allowed actions with references to earlier results | Component-level authoring, safe retry and scalable scene updates |
| Review | Notes, source/effect inspection, preview seek and visible-window capture | Confirmed frame presentation, bounded composition readback and synchronized range review |

Do not interpret a gap in this table as proof that the engine lacks the feature.
For every work package, first classify each requirement as implemented and exposed,
implemented but not exposed, missing implementation, or implemented but unqualified.
The current approach relies too heavily on calculated coordinates and sampled
keyframe arrays. More keys and more tracks must not substitute for a useful
authoring model.

## Capability audit for the whole workflow

Maintain one implementation ledger across the VP and MG packages. For each
capability record the native owner, UI behavior, discoverable MCP operation,
observation method, persistence/undo behavior, preview/export path, remaining gap
and a concrete acceptance artifact. Classify gaps as missing implementation,
missing tool exposure, broken behavior, performance, or creative qualification.
Keep evidence tied to the source/build and document revision it actually exercises.

For each gap, record the user-visible task, current native/UI/MCP support, missing
connection, next bounded change, and the output that will prove it works. Include
who can consume each observation: the human, the agent, or both. For example,
"the preview displays a frame" does not establish that a model can retrieve it,
and "notes are stored" does not establish that the agent can read their anchors,
resolve their targets after an edit or show a reviewed revision to the user.

| Production need | What an agent must be able to inspect or change | What a human must be able to do |
| --- | --- | --- |
| Understand the assignment | Brief, references, accepted decisions, storyboards, deliverables and existing edit | Review and revise the plan without losing the cut |
| Choose material | Real source frames, contact sheets, source ranges, audio/transcripts and media metadata | Browse, audition, compare, rate and replace shots |
| Build the edit | Clip identities, source/sequence timing, tracks, trims, transitions and linked audio | Recognize footage and navigate, scrub and edit a large sequence |
| Finish the image | Evaluated grades, masks, layers, text, effects and source/output color handling | Adjust the same parameters and see their effect on the image |
| Animate and composite | Editable compositions, curves, vector/text controls, cameras and effect order | Refine timing, hierarchy and appearance through usable controls |
| Shape sound | Actual audio excerpts, sources, cue placement, automation and mix diagnostics | Audition music, edit sound and hear synchronized playback |
| Review and revise | Requested frames, moving excerpts, notes, affected ranges and before/after output | Leave anchored feedback, compare revisions and resume manual editing |
| Deliver and resume | Export preflight, progress, output identity, project dependencies and task state | Export variants, reopen/relink the project and recover interrupted work |

Proposed interfaces in this plan are requirements, not names of available tools.
For every inspection operation, declare whether the agent receives actual pixels
or audio, structured metadata, or both. When a model cannot consume a medium,
report that limitation and retain a supported review route; metadata must not be
presented as visual or listening review.

## Whole-production work packages

All VP packages are open proposals. P1 covers the complete supported production
loop. Dependencies establish implementation and qualification order, not permission
to omit stages from the finished workflow. Owners are subsystems, not assigned people.

| ID | Priority | Package | Owner | Depends on |
| --- | --- | --- | --- | --- |
| VP-01 | P1 | Model-independent discovery and project understanding | Agent bridge/document/registries | Existing capability authorities |
| VP-02 | P1 | Brief, script, storyboard and continuity | Agent planning/project UI | VP-01 |
| VP-03 | P1 | Media ingestion, search, selection and source inspection | Library/media/catalog | VP-01 |
| VP-04 | P1 | Full editorial timeline authoring | Timeline/editing/agent bridge | VP-01/03 |
| VP-05 | P1 | Color, finishing and image-based demonstrations | Photo engine/video compositor | VP-03/04 |
| VP-06 | P1 | Advanced motion, typography and compositing | Animation/text/compositor | VP-04; MG workstream below |
| VP-07 | P1 | Audio editing, music and sound design | Audio/timeline | VP-03/04; MG-07 |
| VP-08 | P1 | Frame, clip and audiovisual inspection | Preview/render/review | VP-01/04; MG-08 |
| VP-09 | P1 | Notes, targeted revision and creative quality control | Review/agent/project UI | VP-02/08 |
| VP-10 | P1 | Responsive editing and reliable task lifecycle | Runtime/history/persistence | Cross-cutting from VP-01 |
| VP-11 | P1 | Multi-format delivery and editable handoff | Render/export/project packaging | VP-05/06/07/08/10 |
| VP-12 | P1 | Complete production qualification | Product/creative/engineering review | VP-01 through VP-11 |

### VP-01 — Discovery and project understanding

- Expose bounded project, sequence, chapter, track, clip, source, effect, keyframe,
  component, note and job queries with stable IDs and current revision identities.
- Make inspection of an unfamiliar existing edit as complete as new-project
  creation, without requiring a dump of the entire project file.
- Publish units, time bases, coordinate systems, limits, examples, task ownership,
  asynchronous completion and error/recovery contracts. Extend MG-01 to every stage.
- Acceptance: a fresh model session identifies the project, existing work, offline
  assets, unresolved notes and available operations without mutation or undocumented
  paths. Exercise the same contracts with different capable model providers.

### VP-02 — Brief, story and production planning

- Persist audience, purpose, deliverables, tone, factual claims, references, media
  constraints and accepted creative decisions as editable project context.
- Support scripts, text-only scripts, chapter/shot lists, storyboards and animatics
  linked to sequence ranges, media and intended outcomes. Version plans with the
  edit and disclose when timeline changes make a storyboard outdated.
- Track what each shot demonstrates, required source material and reading time.
  Distinguish proposed copy from verified product behavior.
- Acceptance: resume in a fresh session, explain the story and revise a chapter
  without losing successful design, earlier decisions or required feature coverage.
  Added duration must contain useful material rather than slower repeated shots.

### VP-03 — Media discovery, selection and ingestion

- Discover permitted media through catalog/search tools; inspect metadata,
  thumbnails, source frames, contact sheets, video excerpts and available audio
  or transcripts. Judge actual framing and content rather than filenames alone.
- Support semantic/metadata search, ratings/culling, bins, selected shot ranges and
  alternatives, with originals and derivatives clearly identified.
- Audit RAW/still/video/audio decode, orientation, dimensions, frame/time bases,
  variable-frame-rate handling, color tags, proxies and missing-media/relink behavior.
- Retain provenance for stock/generated assets, music and fonts. Keep resources
  replaceable and preserve source identity and original camera files.
- Acceptance: build and inspect a useful shortlist from mixed media, import it,
  select source ranges, and relink without losing trims or accepted edits.

### VP-04 — Editorial authoring

- Discover and expose supported insert/overwrite, trim, ripple, roll, slip, slide,
  split, move, duplicate, replace, gap, transition and retiming operations.
- Address clips by stable ID and explicit time space; expose source in/out,
  sequence timing, speed, linked audio, track targeting and resulting changes.
- Support readable track reuse, names, chapters, groups/nested sequences,
  selection, navigation, fit and playhead following through UI and native tools.
- Acceptance: assemble a rough cut, replace a shot and ripple a chapter without
  unintended gaps, broken sync or unrelated changes. Inspect timing, undo it and
  confirm the reopened structure and output.

### VP-05 — Color and image finishing

- Expose source/working/output transforms and supported grades, curves, masks,
  layers, retouch/inpainting, cutouts, text and effects through their native owners.
- Keep Photo/Canvas authoring linked to Timeline usage with explicit refresh/version
  behavior. Audit shared versus unsupported spatial processing and delivery routes.
- Link demonstrations to real adjustments: exposure must alter the image, a mask
  must isolate the intended region, and pixel editing must show useful detail at
  an appropriate zoom. Match framing for before/after comparisons.
- Acceptance: verify RAW, video and layered-design edits through preview, history,
  reopen and export with declared color/alpha/geometry tolerances. A visible label
  alone does not qualify a color space, codec, resolution or bit depth.

### VP-06 — Motion, typography and compositing

- Complete the MG workstream as part of the whole edit, sharing source, color,
  time, audio, review and export contracts with the rest of production.
- Support actual app demonstrations and fully animated storytelling according to
  the brief; retain native editable app/design elements where requested.
- Acceptance: revise content, pacing and typography without flattening graphics,
  breaking transitions or reconstructing the entire composition.

### VP-07 — Sound and music

- Cover dialogue, music, ambience and SFX selection, audition/review, editing,
  synchronization, fades, levels, available processing and automation.
- Expose source/output audio properties, waveforms and meaningful mix diagnostics.
  Preserve sample/frame-aware cue placement and asset provenance.
- Acceptance: review a complete mix for deliberate musical structure, intelligible
  dialogue where present, synchronized cues and clean joins/tails. Waveforms and
  beat metadata alone cannot qualify how a soundtrack feels.

### VP-08 — Observe the actual edit

- Extend MG-08 across source, clip, composition, sequence and delivered-file
  inspection, with confirmed frame/source readiness and revision identity.
- Provide exact frames, transition contact sheets, bounded audiovisual excerpts,
  evaluated layer/effect readback and before/after comparison from native output.
- Expose transport, current preview quality, stale frames and cache/render readiness.
  An advanced playhead clock is not proof of newly presented frames.
- Check audio continuity inside the range, including fractional cue boundaries.
  Correct duration and matching audio/video start times can still hide inserted
  silence or a shifted music bed. Compare rendered PCM against a continuous
  source fixture as well as checking frame count and the output clock.
- Acceptance: locate a visible defect's source, reproduce the correct frame, inspect
  adjacent frames and judge the corrected moving excerpt with synchronized sound.

### VP-09 — Notes, revisions and quality control

- Support human/agent frame-, range- and clip-linked notes, categories, priority,
  status and before/after evidence, visible and editable inside Looksy.
- Define anchoring through ripple edits, replacements and deletions. Disclose
  stale/uncertain links rather than silently attaching notes to unrelated material.
- Turn feedback into bounded revisions with an affected range and retained creative
  decisions. Resolve a note after checking the result, not just executing a command.
- Review story/coverage, image selection, reading time, typography, layout, motion
  continuity, effects/masks, musical phrasing and pacing alongside technical defects.
- Acceptance: a human can request a change, inspect the agent's revision, accept or
  revise it and continue manually without losing unrelated successful work.
  Creative approval remains distinct from automated technical checks.

### VP-10 — Performance, history and resumable execution

- Keep large edits usable for playback, scrubbing, scrolling, selection, keyframing
  and manual intervention. Expose proxy/cache readiness and meaningful progress.
- Preserve accepted edits, plan versions, sources, task state and pending operations
  across context changes, app restarts and interrupted background work.
- Use revision guards, task ownership, atomic reversible changes, safe retries and
  cancellation. Conflicting human changes must not be silently overwritten.
- Avoid unnecessary worker instances, whole-document dumps and full scene rebuilds
  for scalar edits. Caches must not replace authoritative editable source.
- Report task readiness and export snapshot readiness separately, with named
  blockers and bounded queue counts. Preserve edit/save/analysis, immutable
  snapshot and source checksum guards while allowing derived preview work to
  remain independent of export. Supply the matching hash in the compact response.
- Acceptance: measure foreground responsiveness and sustained playback on a large
  representative project; interrupt/resume, undo and reopen without duplicated
  operations, lost edits or altered originals. Declare budgets and remaining failures.

#### Human editing workspace

- Make clips identifiable through source names, media type, thumbnails, waveforms
  and useful summaries. Animated property names must not replace clip identity.
- Support compact adjustable track heights, vertical/horizontal scrolling,
  resizing, zoom around the intended location, clear selection and an obvious
  playhead. During playback, follow the playhead according to the user's setting
  without fighting manual navigation. Sequential clips should reuse suitable tracks.
- Keep the preview large and useful. Expose loading, proxy and stale-frame states;
  scrubbing must show the requested picture rather than silently retaining an old one.
- Provide Library and selected-clip Effect Controls in the upper-left workspace,
  including an effect/layer hierarchy, parameter controls and aligned keyframe lanes.
  Keep a searchable effects browser on the right. Selecting a clip must reveal its
  actual controls, with clear scope for clip, layer, group and sequence settings.
- Make keyframes selectable, draggable and editable with visible interpolation,
  curves, current values and time context. Align parameter rows and mini timelines;
  scrolling or resizing must preserve their relationship.
- Integrate notes, chapters and element navigation with the edit. Jumping to a note
  should reveal the relevant picture and clip without losing the user's workspace.
- Qualify visual hierarchy, contrast, spacing, focus, keyboard navigation, resize
  behavior and pointer interactions on the real large project. Preserve useful brand
  glow while keeping labels, handles, selection and active controls readable.

Acceptance: a person unfamiliar with the project can locate a shot, see and hear
it, adjust an effect/keyframe, leave feedback, compare the revision and undo it.
Record actual foreground interaction and playback measurements; a component
screenshot or successful command cannot close this workflow.

### VP-11 — Delivery, versions and handoff

- Produce landscape, portrait and requested variants with editable framing, text
  reflow, safe areas and timing overrides. Do not assume a center crop is sufficient.
- Audit containers/codecs, dimensions, frame rates, color, alpha/audio and quality
  settings. Preflight unsupported combinations, offline assets, fonts, available
  space and output conflicts before rendering.
- Expose export progress, cancellation, failures and exact document/output receipts.
  Inspect delivered frames, duration, color tags and audio instead of trusting job success.
- Package required project resources or a clear relink manifest.
- Acceptance: review both trailer orientations, reopen the editable handoff in a
  fresh process, revise it and reproduce output without workstation-specific hacks.

### VP-12 — Complete production qualification

The three MG examples below are building blocks, not substitutes for full videos.
Qualify complete workflows for:

1. The existing Looksy trailer, preserving accepted design and demonstrating Photo,
   Library, Canvas/Carousel, video and optional local-agent workflows.
2. A footage-led travel/documentary or interview edit with shot selection,
   continuity, color, dialogue/ambience where applicable, music and sound design.
3. A short social adaptation with a deliberate opening, readable reframing,
   graphics and delivery rather than a blindly cropped full-length edit.

Retain the brief, script/storyboard, selected media, rough cut, notes, revisions,
native editable project, final outputs and fresh-process handoff evidence.
Acceptance: a capable agent completes the supported workflow through discoverable
native tools, a human can revise every stage, and creative review evaluates the
finished audiovisual piece. Report model-specific failures separately from app gaps.

## Motion graphics workstream

All packages are open. P1 establishes authoring/review foundations; P2 adds creative
depth on those foundations. Owners identify subsystems, not assigned people.

| ID | Priority | Package | Owner | Depends on |
| --- | --- | --- | --- | --- |
| MG-01 | P1 | Capability audit and discoverable authoring contracts | Agent bridge/registries | Existing capability authorities |
| MG-02 | P1 | Nested compositions, parenting and controllers | Timeline/document/compositor | MG-01 |
| MG-03 | P1 | Precise curves and procedural motion | Animation/runtime/UI | MG-01/02 |
| MG-04 | P2 | 2.5D camera and scene transitions | Compositor/camera | MG-02/03 |
| MG-05 | P2 | Advanced vector and text animation | Vector/text/animation | MG-02/03 |
| MG-06 | P2 | Light, texture and compositing effects | Render/effects/color | MG-02/03 |
| MG-07 | P1 | Music and sound authoring parity | Audio/timeline/agent bridge | MG-01 |
| MG-08 | P1 | Exact-frame, range-render and note review | Preview/delivery/review | MG-01 |
| MG-09 | P1 | Reusable components and reliable scene transactions | Document/agent bridge/UI | MG-02/03/08 |
| MG-10 | P1 | Native production examples and qualification | Creative tools/product review | Relevant packages above |

### MG-01 — Discoverable authoring contracts

- Inventory UI, renderer, persistence, export and MCP support separately; retain
  exact command/schema and source/build identities for the audit.
- Publish typed properties, units, coordinate/time spaces, bounds, defaults,
  animatability, interpolation support, resource ownership and reversal behavior.
- Provide bounded examples and actionable unsupported-operation responses. Do not
  expose a method as usable when it has no complete adapter.
- Support stable named compositions/layers/controllers and lookup by persistent ID.
- Acceptance: an agent starting without undocumented project knowledge can discover
  the tools, create an example, inspect it and revise the intended element.

### MG-02 — Nested compositions and shared controls

- Support nested editable compositions, groups, parent transforms, anchor points,
  local time, offsets and shared controller properties with explicit evaluation order.
- Allow an animated slider, poster or app window to move as one component while
  its internal animation stays independently editable. Show the same structure in UI.
- Avoid requiring a separate track for every sequential element. Keep simultaneous
  layers and internal composition structure understandable in large projects.
- Acceptance: reparent, duplicate, retime and revise a nested example; preserve
  identities, appearance and hierarchy through undo/redo and save/reopen. Reject cycles.

### MG-03 — Curves and procedural motion

- Expose precise Bezier handles, velocity-aware easing, springs, overshoot,
  stagger, follow-through, loops, path following and property linking.
- Store controller intent and sparse editable keys rather than silently baking
  every frame. Define deterministic evaluation from frame/time and random seed.
- Provide an inspectable, bounded expression/controller system with dependency
  validation and clear errors; use the same evaluator for preview and export.
- Acceptance: random-order seeking, reverse scrubbing, frame-rate changes and reopen
  reproduce the intended motion. Parameter edits update the whole gesture without
  regenerating thousands of manually managed keys. Test spring settling and curve joins.

### MG-04 — Camera motion through 2D artwork

- Provide a scene camera with coherent projection, depth/parallax, parenting and
  animated framing for flat layers, including controllable perspective where needed.
- Connect scenes through shared elements and continuous camera movement rather
  than unrelated cutaways. Keep layer placement editable during camera animation.
- Acceptance: a camera move between two compositions preserves spatial continuity,
  clipping, masks and alpha in preview/export and both landscape/portrait layouts.

### MG-05 — Vector and text animation

- Expose editable paths, strokes/fills, trim paths, dashes, gradient animation,
  path following and morphs with explicit compatible-topology rules.
- Add character/word/line selectors, stagger, range animation and typography/layout
  controls without creating one top-level track per glyph.
- Retain shaping, grapheme clusters, kerning, font identity, line breaks and Unicode
  behavior through animation; define missing-font behavior instead of silent substitution.
- Acceptance: revise text and reshape a path without rebuilding the scene. Validate
  edge quality at delivery resolution, morph failure behavior and reopen/export parity.

### MG-06 — Light, texture and compositing

- Audit and expose ordered glow/bloom, blur, displacement, noise/texture, animated
  gradients, linked alpha/luminance mattes and temporal motion blur as applicable.
- Support flowing light bands with editable paths, width/taper, reveal timing,
  gradient, distortion and glow. Light must interact with the composite deliberately.
- Specify working color domain, additive blending, premultiplied alpha, HDR behavior,
  effect padding and temporal sampling. Do not label a stylized light effect as a
  physically qualified caustics simulation without separate evidence.
- Acceptance: a light reveal actually drives the image/matte and carries into the
  next scene; no cropped halos, edge fringes, random flicker or preview/export drift.

### MG-07 — Music and sound authoring

- Discover and inspect waveforms, existing beat/phrase analysis, markers, trims,
  fades, clip gain and available mixing/automation controls through MCP.
- Place SFX by frame/sample-aware time and preserve synchronization through retiming.
  Use one coherent musical structure rather than arbitrary track changes or loops.
- Acceptance: review rendered audiovisual excerpts; confirm cue placement, gain,
  fades and transitions after reopen and in final export, including audio tails.

### MG-08 — Frame and audiovisual review

- Capture a requested sequence frame with document/version, requested/actual source
  time, readiness and output identity. A seek receipt is not displayed-frame proof.
- Generate bounded contact sheets around cuts and transitions, plus short selected
  range renders with synchronized audio using the native rendering path.
- Inspect active clips, stacking order, evaluated properties, masks, effects and
  source times at a frame without dumping the entire document.
- Connect frame/range notes and stable clip links to before/after review artifacts.
  Retain revision identity and expose stale-note/target status after timeline changes.
- Acceptance: detect and reproduce a one-frame flash, mask slip and mistimed cue;
  verify each correction visually/audibly before resolving its note.

### MG-09 — Reusable components and transactions

- Package branded reveals, tactile sliders, callouts and transformations with named
  content slots, exposed controls, fonts/assets and responsive layout constraints.
- Allow component creation/update as an atomic reversible operation, with stable
  references, validation, idempotent retry and asynchronous completion/cancellation
  receipts where needed. Do not replay uncertain mutations after a timeout.
- Support content/timing/aspect-ratio changes without flattening components or
  silently rebuilding unrelated accepted edits.
- Acceptance: another agent can discover a saved component, insert it, replace its
  media/text and retime it; a human can then adjust the same controls in Looksy.

### MG-10 — Production examples and qualification

Build these isolated examples first, then reuse the accepted results in the current
trailer. Each example must exercise native authoring, review, revision and delivery:

1. **Flowing light reveal:** an animated curved band reveals a photograph and
   carries into the next composition, with restrained glow, texture and staggered text.
2. **Tactile color-edit demonstration:** an animated slider and tone curve cause
   visible, corresponding changes to the photograph. The graphic and actual grade
   share a controller or explicit linked timing; do not fake the result with an
   unrelated decorative overlay.
3. **Photo → poster → split carousel:** build masks, layered text and design around
   a photo, then span that composition across canvases with intentional split points
   and clearly demonstrate the resulting carousel output.

Acceptance requires editable source, compact meaningful hierarchy, successful human
revision, undo/redo, save/reopen, matching preview/export and review of moving output
with audio. Record both technical defects and art-direction feedback. Attractive
stills, tool success, or test counts alone cannot close creative qualification.

## Execution and evidence

Use the current trailer as a continuous production test. Keep a shared defect
queue linking the visible/audible problem, chapter and frame range to its native
clip/component, responsible subsystem and acceptance comparison. Preserve accepted
creative decisions across passes. Fix an app/tool blocker at its native owner, then
return to the affected edit and check the output; infrastructure work alone does
not complete the video.

1. Audit the whole workflow, understand the existing trailer and preserve its
   accepted state. Establish planning, media and review access (VP-01/02/03/08)
   and record performance/lifecycle baselines (VP-10).
2. Complete reliable picture editing, color and audio (VP-04/05/07), alongside
   composition/motion foundations (MG-02/03) and targeted revision (VP-09).
3. Exercise advanced motion through the MG examples, package reusable results and
   improve the trailer chapter by chapter (VP-06, MG-04/05/06/09/10).
4. Qualify complete films, responsive human editing, notes/revisions, multi-format
   exports and native handoff (VP-10/11/12). Do not stop at isolated animation demos.

For each package retain the native fixture, exact build/schema identities, commands,
document hashes, frame/range artifacts and remaining limitations. Exercise stale
targets, cancellation, failure, undo and fresh-process reopen where relevant.
Measure foreground input-to-visible response, scrub latency, sustained playback,
memory and render cost on representative projects. Set numerical acceptance budgets
from a recorded baseline before implementation; do not infer realtime performance
from method duration, offscreen UI timing or playback clock advancement.

Completion means an agent can understand a brief and existing project, discover and
select media, plan and build the story, edit picture, author graphics, finish color,
mix sound, review, revise and deliver a complete video while a human retains full
editing control. It does not guarantee that any model produces good art direction
or establish Hollywood-level quality without review of the finished film.

### Completion record

For each production example retain:

- [ ] Brief, script/storyboard, approved direction and selected source media.
- [ ] Editable native picture edit, image finishing, graphics and sound mix.
- [ ] Frame/range-linked notes and reviewed before/after revisions.
- [ ] Usable large-project playback, scrubbing, navigation and manual editing.
- [ ] Discoverable agent operations with observation, safe retry and recovery.
- [ ] Complete audiovisual review of the exported film and requested variants.
- [ ] Fresh-process reopen, relink where needed and a successful human revision.

Track app readiness and creative completion separately. Passing an export test does
not approve the film, and an attractive film does not qualify missing editing,
recovery or handoff behavior.

### Native production findings, 2026-10-31

- Fractional sound-effect boundaries exposed a music-stem sample-clock error.
  Audio-only concatenation and absolute sample endpoints fixed the measured
  eight-second native intro: all four reference windows have zero-sample lag.
  Full-film mix and listening review remain separate requirements.
- Source-time animation existed in the engine but lacked a task-owned authoring
  action. The new guarded retime action supports local duration changes, locked
  tracks, explicit overlap/ripple policy, and linked-audio synchronization. Native
  export-pixel, undo/reopen and revert fixtures pass; the P14 trailer chapter has
  been authored with it, with complete moving review still pending.
- Typed creator results and non-creating retime actions no longer need two rich
  clip-list projections to discover IDs. Exact task snapshots and journaling stay
  in place. The large-fixture measurements establish removed projection cost,
  not a qualification of live UI responsiveness.
- Repeated lossless generator encoding, cache inventory scans, PNG decoding and
  alpha-bound scans are distinct render costs. The bounded cache/filter changes
  retain pixel/PTS parity. The alpha-support benchmark is a measured stage win,
  not a full-film speed claim. Set `PHOTO_EDITOR_TIMELINE_EXPORT_TIMINGS=1` to log
  per-window generator, graph and encoding time during native qualification.
- The full-film source audit found eight visible clips whose combined animated
  scale and rotation are rejected by the exporter. Treat simultaneous transforms
  as a compositional requirement: preserve their authored curves and provide a
  bounded transparent rotation canvas, with pixel/frame-clock tests. Removing
  either animation would conceal a missing capability rather than complete the
  production example.
- Actual P15 windows expose costs beyond isolated cache benchmarks. At 199 seconds,
  four output frames produced 188 graph inputs and took roughly 22–25 seconds
  across generator preparation, graph construction and encoding. At 229 seconds,
  16 animated waveform paths repeatedly validate their unchanged coordinate data.
  Measure and remove repeated preparation and redundant inputs while retaining
  exact decoded pixels; do not infer whole-film throughput from codec-only wins.
- Native review must verify correspondence as well as appearance. The P15 review
  now shows the correct city shot/caption under the playhead and the waveform in
  its lane. A separate 229-second render shows the music duck and location gain
  labels with the corresponding smaller/larger waveform drawings. These are
  scoped rendered checks, not a substitute for watching the complete film.
- The cumulative repair build produced a byte-identical native 199–200-second
  MP4. Across its eight successful windows, measured time changed from 147.519
  seconds to 41.313 seconds; the new run used warm procedural-source cache hits.
  This establishes preserved output and observed window cost, not controlled
  cold-start speed or full-film completion.
- Full export exposed mixed integrity accounting: an actual compound MOV is
  already pinned and verified as a primary SHA-256 source, but its missing
  generated-media sidecar adds a second untracked warning. Separate primary
  and generated counters. Exempt that duplicate sidecar warning only when its
  exact canonical path is already covered by the authoritative SHA manifest;
  preserve verification of the MOV and all other required inputs. The current
  full export was initially rejected before enqueue. The focused health/bridge
  contracts now pass, and the full P16 native export has been accepted and is
  rendering; final mux and complete-film review remain pending.
- Selected stabilization renders need an explicit manifest entry, rather than
  relying only on the original source checksum and a submission-time cache
  sidecar check. Pin and verify the actual selected render before encoding and
  publishing. This is a separate dependency gap; P16 does not use stabilization.
- Distinguish an explicit native rejection before enqueue from a transport
  failure after submission. Retain the original structured response and request
  ID. The export wrapper now resolves only the known pre-enqueue source guard;
  unknown errors remain uncertain and cannot silently trigger a duplicate job.
- Authoring schemas need explicit color encoding and normalized shape units.
  A CSS-order eight-digit color produced a nearly transparent Qt ARGB plate;
  the P16 native correction is rendered and verified. Agents should not need to
  infer alpha order or whether a corner radius is in pixels from generic numbers.
- The full P16 frame audit exposed a second coordinate-contract failure: Library
  photos were authored with offsets one quarter of the intended displacement,
  while shape positions used CSS top-left coordinates where the renderer expects
  centers. Backplates also covered photos because of layer order. The typed MCP
  schemas now describe quarter-canvas frame offsets, absolute animation values,
  text alignment anchors, vertical block centers, normalized dimensions, shape
  radius units and Qt alpha order. The header passes a standalone syntax check;
  these descriptions are source changes, not yet deployed to the running review
  worker. Correct the existing native clips separately and verify actual frames.
- A mixed text/shape style silently takes the shape-only rendering branch.
  The P17 proposal caught this before execution and uses separate editable text
  and plate clips. The schema now states that exclusivity. A reusable labeled
  shape component should expose both parts without requiring an agent to infer
  renderer branch behavior.
- P17 is now authored and saved natively: eight existing photo-title corrections,
  72 frame-aligned numeric readouts and two shared background plates, followed
  by six synchronized animation edits. No tracks were added; 1,281 pre-existing
  clips and all existing motion keys were preserved. This is saved-state proof,
  not post-edit render approval. A native numeric text channel would replace the
  short readout clips with a single editable counter on each lane.
- Rebinding a proposed edit needs layer placement guards, not only clip fields.
  The native authoring helper now checks source-track membership, unchanged
  compositing order/state and sequential destination intervals. Five focused
  tests cover unrelated edits on a reused lane, stale source placement, changed
  track state, collisions and abutting clips. Existing render/task guards remain.
- The P18 native saved-state audit found that the typed base-frame action called
  the UI's animated setters: it changed 26 initial rotation/opacity samples on
  15 clips and left 17 base Y offsets at their previous values. A layout command
  must update the base frame and active variant without touching authored keys.
  The live revision uses reversible clear/base/restore transactions while the
  focused controller repair is tested separately. Preserve both the requested
  base values and every independent curve in the regression and native readback;
  verifying only the apparent pose would miss this failure.
  The direct base-frame source repair now passes its private controller and
  task-batch regressions, including exact curve retention, base readback,
  undo/redo/reopen and sampled pixel parity. It has not yet replaced the running
  export worker. Its preserved proof also records a separate pre-existing
  animated-perspective export failure, rather than hiding that qualification.
- The separate legacy animated-perspective export failure now has a source
  repair and private regression proof: sequence-frame-clock expressions replace
  the unsupported filter time variable. Full/range frame identity, static-pose
  oracles and the existing quad path pass alongside the base-frame and task-batch
  checks. The prepared review bundle contains these fixes; live deployment waits
  for the current immutable export to finish. Reverse-video timing is outside
  this still-source fixture's qualification.
- Export inspection currently reports a compositor phase marker (`40`) while
  hundreds of windows render. Publish rendered/total frame counts and bounded
  window diagnostics through MCP; do not present phase progress as a completed
  frame percentage or an ETA. The current partial visual review reads exact
  closed-window receipts, which is a production workaround rather than a general
  frame-observation API for every agent.
- The Canvas audit distinguishes static group rendering from agent authoring:
  controller/UI group transforms are serialized and exported, but the inspected
  task command/reflection allowlists do not expose group creation, membership or
  transform authoring. Per-clip animated quads already exist. Provide a typed,
  reversible shared transform/pivot contract rather than requiring an agent to
  approximate coherent poster motion with independent child rotations.
- The portrait continuation still needs a fresh verified native import and its
  remaining authoring steps. Run owned graphics-track compaction afterward so
  the older per-clip track layout is not carried into the handoff. Preserve clip
  identities and verify overlap order before subsequent portrait refinements.
- Paused agent seeks must reveal the playhead in the timeline viewport. The
  live P21 window remained around 40–53 seconds after seeking to 148.5 seconds;
  its automatic time-window filtering can consequently hide the relevant rows.
  The bridge now explicitly centers validated preview seeks without changing
  authored state or undo history; its focused contract test passes. Passive
  paused updates must still preserve a user's chosen viewport.
- Program composition and visible timeline rows require separate diagnostics.
  At 148.5 seconds the saved document contains 35 active visual elements, but
  the native capture shows only the shoe and no timeline rows. Releasing the
  task did not change that capture. Program uses the full sequence snapshot,
  independently of viewport-filtered rows, so time-window filtering alone does
  not explain the missing composition. A bounded, fixed-key projection summary
  now reports snapshot, row and Program-layer counts through the existing
  geometry tool. The refreshed native probe confirmed an empty QML snapshot:
  one startup refresh, zero tracks, zero background layers, and no active render
  cache, while the controller retained all 196 tracks. The source repair refreshes
  on document lifecycle state and coalesces its paired sequence notification;
  focused checks cover empty startup, opening 196 tracks, active Program layers,
  closing, a later independent edit and scalar seeks without full refresh.
  Actual native presentation of that repair remains a separate required check.
- A second live check still discarded the track projection despite receiving
  authored-state notifications. The real Qt controller test identified the
  boundary the JavaScript-array fixture missed: `snapshotTimelineTracks()`
  returns `[object V4Sequence]`, with length 196 and `Array.isArray=false`.
  The helper treated that valid native sequence as empty. Normalize the outer
  sequence once at authored refresh, preserve the compact nested data, and test
  actual Program collection and virtual-row adoption through a real QQmlEngine.
  Native list conversion and displayed-frame proof must precede any UI-fix claim.
- Task admission must expose its supported action kinds in the machine-readable
  schema and return them in validation errors. The Carousel admission rejected
  `graphics` as an action kind even though `graphics` is a task capability;
  a corrected plan using `framing`, `text`, `layers`, `effects` and `delivery`
  succeeded. Preserve the rejected receipt and verify that no task was created
  before correcting a plan; this is different from retrying an uncertain edit.
- A model-independent production workflow also needs guarded asynchronous save
  and clean-draft close operations. This permits authoring another project while
  an accepted export renders its immutable snapshot in the same app process.
  Save must remain asynchronous; close must reject active/review-held tasks,
  stale identities, dirty or saving documents, save errors, and mismatched
  canonical Library/document readback. Any failure leaves the editor open.
  These tools are being implemented; export-continuation and native qualification
  are still required. The current header's New Timeline action journals dirty
  edits but is not a substitute for a verified canonical save before closing.

- Native background delegates were allocating media players even for text,
  photographs and generators. A video-only Loader now preserves the player
  through ordinary parameter edits and allocates none for non-video layers.
  The production-bound regression, 100 Cocoa lifecycle cycles and 16 seeks
  through the protected 1,465-clip project passed with the authored hash retained.
  This reduces a 36-layer fixture from 36 players to one. It does not prove
  elimination of the original CoreMedia/MediaToolbox teardown segfault: that
  failure was not reproduced in the smaller baseline fixture. The initially
  unexposed window blocked playback proof; after exposing it, foreground
  playback advanced through scene changes and visible Program content returned.
  Startup delay and frame jank still need qualification separately from liveness.
  Evidence is retained in `crash-2026-10-31-090444/verification.json` alongside
  the original crash report. Do not equate zero observed crashes in a bounded
  test with a completed stability qualification.
- The exact project's animation index was thrashing above its 64-snapshot
  working set. Static snapshots no longer evict animated channels, and the
  retained set is bounded at 128. Three real-project Qt/QVariant evaluations
  of the 110-snapshot carousel scene preserve sampled values and reduce median
  index work across 120 simulated frames from 2,986 to 167 ms. Peak fixture
  process RSS falls from 2.54 GB to 0.87 GB. These are index measurements, not
  displayed-video FPS. Whole-app frame/latency/memory gates remain open.
- The focused track view retained 14 abandoned empty lanes. It now keeps the
  newest empty insertion lane per kind, all explicitly targeted lanes, selected
  clips and group controls. The full track view remains available; authored
  tracks and clips are unchanged. The current native review shows relevant
  clips without the previous stack of empty Library Media lanes.
- A real UI note-create/MCP-readback/UI-undo check passes on the 1,465-clip
  project and restores its exact document hash. Do not replace this with only
  a source check of the Notes panel.
- Native paused-thread sampling found Recent Timelines verifying an 8.7 GB
  journal in the background. Recents now uses a bounded, presentation-only
  tail read; opening or restoring a candidate still takes the full verified
  chain path. The exact-project tail probe reads 54.5 MB in about 2.1 seconds
  and retains the current document hash. Damaged-tail, bounded-read,
  cancellation and strict-prefix-rejection codec tests pass. A consistent
  native build and controller checks pass; runtime migration is under review.
- Cold checkpoint writing was separately verifying the same 8.7 GB legacy
  chain. Fully verified active journals above 64 MiB now preserve an exact
  `.sealed-UUID` archive before atomically replacing the active file with a
  small current head. The old active file remains discoverable until commit;
  exact previously shown candidate IDs still resolve through sealed archives.
  Snapshots of at least 1 MiB use the existing compressed v2 codec by default
  (`PHOTO_EDITOR_TIMELINE_COMPRESSED_JOURNAL=0` opts out; `=1` forces it).
  Controller tests verify old bytes, new head, strict reopening, and existing
  recovery/cache behavior. Native rollout must retain both archive SHA evidence
  and the canonical project hash; history preservation is a separate gate from
  reducing storage size or cold-scan CPU.

- The native history migration is verified: the 8,821,467,394-byte journal is
  retained byte-for-byte in its sealed archive, while the active head is about
  4.6 MB. The protected trailer hash and original camera sources are unchanged.
- Static transform/opacity/no-fade bindings now avoid subscribing to the global
  playback clock. The actual binding regression drops 100 static-tick updates
  from 101 evaluations to one while still testing animated and fallback edits.
- Loading this 298-second trailer exposed synchronous preview-cache hashing in
  a UI count getter. With no generated cache, the UI now counts the 75 fixed
  segments without constructing their signatures and defers the range model.
  Three exact-project controller probes measured a median 2,833.84 ms for the
  old materialization path versus 0.409 ms for the count path. Explicit cache
  generation and signed-range inspection retain the existing validation path.
  This measures a controller query, not displayed playback FPS.
- Runtime checksum verification and repeated cached media facts must not mark
  an otherwise unchanged saved project dirty. Persist new expected checksums
  and changed media facts, retain mismatch warnings and the original expected
  hash, and test ordinary and bulk hydration paths separately. Native clean
  save/reopen and foreground interaction remain explicit qualification gates.
- Polling must remain cheap on the actual large document. The native trailer
  is 143 MiB; `timeline.inspect_document` now accepts the optional boolean
  `includeDocumentHash:false` to report progress without serializing its hash.
  This mode returns an empty hash and cannot authorize a mutation. The default
  still verifies the deterministic hash before guarded editing or saving.
  Successful `timeline.open_saved_draft` now returns a compact identity receipt
  (about 4 KiB on this project, previously 123 MiB) and preserves canonical path
  validation. Fetch detailed media and keyframes through bounded inspections.
  Typed-argument rejection and default hash behavior have focused MCP coverage.
  On the paused exact project, three paired native calls measured median
  lightweight status at 2.2 ms versus 2,966 ms with full hashing. Guarded edit
  and seek requests still pay full-hash costs; efficient revision-backed hash
  verification needs its own comprehensive invalidation audit before caching
  can safely replace those guards.
- The final internal build passes 12 focused checks and the native saved-project
  workflow: exact hash retained, clean save/close/reopen, three identical notes,
  seven chapter seeks and a clean final state. The desktop remains locked;
  pointer scrubbing, dock/scroll interaction, final keyframe/note editing and
  displayed playback responsiveness are still unqualified. Do not equate these
  native functional receipts with completion of the timeline polish gate.

These findings do not check off the completion record. Both complete films,
foreground interaction performance and the final editable handoff still require
their own evidence. Current production state and receipts are maintained in
[`polish-pass-2026-10-30/progress.json`](../marketing/native-recreation-v4/polish-pass-2026-10-30/progress.json).
