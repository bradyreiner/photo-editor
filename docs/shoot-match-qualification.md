# Reference-based shoot matching

Implementation slice: 2026-11-11, within the authorized nine-workstream
[professional expansion program](professional-expansion-program.md).

## Delivered workflow

Select a reference and other catalog photos, then open **Edit → Match Selected
Photos…** or **Library → More → Match selected photos…**. Edit also exposes the
action in **Sync Selected Photos**. The command palette includes the action.

The first page chooses the reference from that captured selection and independent
Lighting/Look strengths. Lighting fits global exposure, temperature and tint;
white balance is preserved when the reference or target lacks enough neutral
pixels. Look fits contrast and saturation. The existing learned-style profile
system remains available separately.

Matching runs locally on one worker using the canonical source decoder and CPU
renderer. It optimizes bounded rendered-image statistics, then produces larger
comparison images from the fitted editable recipes. It does not infer semantic
style, identify skin or promise that different subjects should share a histogram.
Distance is a statistic, not confidence or a photographic quality score.

The second page shows the reference and before/proposed output for each target.
Large residual differences, increased clipping and large exposure moves are
flagged; outliers start unchecked. Failed targets report errors individually.
Only explicitly selected proposals are applied. One batch Undo restores all exact
previous catalog recipes; Redo and existing recipe persistence retain the result.

## State and rendering contracts

- Proposal generation changes no accepted recipe or source file.
- Only exposure, temperature, tint, contrast and saturation are fitted. Current
  layers, masks, crop, retouching, detail and profile settings retain their definitions.
- Reference snapshots include the current saved recipe, source path and a cheap
  filesystem revision including inode/change-time information where available.
  Linked image-layer revisions are also checked. This is revision validation,
  not an immutable file lock or a full source-content hash.
- Applying a selected batch revalidates the reference and selected targets before
  mutation. Changed/missing recipes or sources reject the selected commit.
- The host synchronizes any pending catalog-photo edit before building or applying
  proposals, so an editor-side Undo cannot silently invalidate the review.
- Cancellation requests reach the decode/render token and optimization loop.
  Workers never capture the catalog QObject; destruction cancels outstanding work.
- Private proposal recipes stay outside the QML list model. A review is limited
  to 64 targets and a bounded recipe snapshot budget. Temporary preview files are
  owned by the current review and its in-flight worker.
- Final recipe updates use existing catalog persistence, thumbnail invalidation,
  photo-recipe notifications and batch history. No recipe schema change or new
  model download is required.

## Automated qualification

`shoot_match_workflow_test` covers controlled image shifts, reduced reference
distance, actual rendered previews, field preservation, selected-batch history,
Undo/Redo, catalog-model reopening, canonical-render agreement, stale recipes,
source replacement and cancellation.

`shoot_match_real_photo_test` repeats that workflow on the existing mountain-photo
fixture with the ACES renderer. It tests known edit shifts on the same photograph;
it does not establish matching quality across unrelated compositions or cameras.

`shoot_match_ui_test` operates the production popup with a real catalog: keyboard
activation, populated comparison images, unchecked outliers, visible primary
actions and compact/wide window bounds. Captures wait for every visible reference
and comparison image to decode.

Current receipts, captures, source/binary hashes and logs live in
`build/shoot-matching-2026-11-11/`. Test execution status is recorded there; the
full workstream remains **In progress**.

The 2026-11-11 scoped receipt passes the desktop Debug build, all three focused
tests above, QML lint, and an additional real-photograph production-popup check.
The UI captures use the offscreen software backend; they are not physical native
interaction proof. See `build/shoot-matching-2026-11-11/report.md`.

## Remaining gates

- Human review on mixed cameras, lighting changes, skin tones, different subjects
  and compositions; tune outlier policy against those outcomes.
- Physical foreground mouse/keyboard workflow and native modal/menu behavior.
- Named-machine latency, cancellation latency, large layered/RAW memory budgets,
  and large-shoot throughput. Small-fixture runtime is not a performance claim.
- Full local-model/style integration, richer creative color/curve matching,
  reusable reference profiles and representative-sample/outlier group workflows.
- Packaged-release and clean-machine qualification.
