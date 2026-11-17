# Professional expansion program

Authorized 2026-11-07: implement all nine capabilities proposed in the conversation.
This program preserves that full scope. It does not redefine the program as the
first delivery batch. Item status remains in `capability-verification-plan.md`;
existing Photoshop, masking, color, Timeline and performance owners retain their
acceptance gates.

## Workstreams and delivery order

| Workstream | Existing foundation | Next implementation and complete workflow |
| --- | --- | --- |
| Coordinated design variants | Boards, snapshots, reusable components, linked photo text, export planning | First: one job with original, square, portrait, story and landscape image outputs, explicit fit/crop, saved settings, per-output dimensions and hashes. Next: independently editable layout bounds and deliberate shared-content/local-layout overrides; edit one product/headline, reopen, and export every linked layout. |
| Reference-based shoot matching | Learned style profiles, recipe patches, catalog batches | Select a reference, separate lighting normalization from creative color/contrast, preserve local work, preview representative targets, inspect outliers, apply editable adjustments with per-image results and undo. Validate mixed cameras/lighting and skin-tone fidelity on real shoots. |
| Depth-aware lighting and compositing | Depth inference, Scene lighting/shadows, photo layers and masks | Place a subject, light and graphic in a common depth-aware composition; editable occlusion, contact shadows and atmospheric depth; correct uncertain edges; save/reopen/export through the same geometry and color semantics. Reuse PS-PRO-013. |
| Professional image restoration | Detail controls, model runtime setup, source-preserving recipes | Start with local denoise and enlargement, then reflection removal and motion-blur correction. Each model needs a usable runtime, declared source support, cancellation, durable output identity, synchronized 100% comparison and photographic review. Do not relabel ordinary smoothing as AI denoise. |
| Tracked subject masks | Independent selections, Timeline tracking, mask effects | Transfer a static subject into a clip, propagate with occlusion/confidence feedback, correct frames, constrain propagation, and retain corrections through history/reopen/export. Reuse PS-PRO-011; existing tracking alone does not establish subject segmentation. |
| Advanced retouching | Healing, clone, LaMa, local adjustments, layer recipes | Editable texture/tone separation, dodge/burn layers, clone-source controls and temporary inspection views; protect texture and content outside the accepted mask; qualify portraits/products. Reuse PS-PRO-008 for channel arithmetic and reconstruction. |
| Mesh and puppet warp | Affine transforms, perspective, editable layer content | Durable mesh/pins, anchor regions, direct canvas manipulation, Apply/Cancel, inherited locks, undo, reversible definitions and matching preview/export. Reuse PS-PRO-005. |
| Intelligent shoot review | Ratings, burst metadata, compare/survey, visual retrieval | Matched-zoom face review, focus/eye-state evidence, near-duplicate groups and suggested keepers. Human decisions remain authoritative; measure model quality and review throughput on actual shoots. |
| Production automation and delivery | Command registry, export preflight, collision policies, proof/manifest, variables | First: multi-format deliveries use existing worker/cancel/retry/preflight. Next: parameterized recorded actions, table-driven image/text replacement, per-item failures, portable dependency packaging and export checks. Reuse PS-PRO-010. |

The first batch addresses the shared delivery path for design variants and
production automation. Shoot matching follows, then depth-aware compositing.
Restoration, tracking, retouching, warp and review retain independent acceptance
corpora and model/runtime requirements; they must not be exposed as working tools
before their underlying workflow exists.

## First batch: multi-format delivery

In an Edit Board, expand **Layout**, select delivery formats, choose **Fit artwork**
or **Fill and crop**, then choose **Export formats…**. The same controls appear in
the carousel export sheet. Generic sizes are 1080×1080, 1080×1350, 1080×1920 and
1920×1080; these are presets, not promises about platform upload requirements.

- Original remains the default, retaining the existing export size controls.
- Added formats use an aspect-preserving render. Fit adds transparent padding
  when supported/enabled, otherwise the board background; Fill uses a centered
  crop with a preflight warning. Excessive crop renders fail before export.
- Image formats, color profiles, naming collisions, board scope, cancellation,
  retry, proof PDFs and delivery hashes continue through the existing exporter.
- Video exports retain their original output path; this batch adds image variants.
- Settings are document-owned, undoable and persisted. Delivery renders do not
  modify the artwork, source files, layer transforms or layout dimensions.
- This batch does not implement separate layout overrides, shared updates across
  layouts, recorded actions, model-based restoration or tracked subject masks.

## Second batch: reference-based shoot matching

Select a reference and up to 64 other photos. Open **Edit → Match Selected Photos…**,
**Library → More → Match selected photos…**, or the matching action in Edit's
**Sync Selected Photos** panel. The popup chooses a reference from that selection,
then builds local proposals for every other selected photo.

Lighting adjusts exposure and, when sufficient neutral areas are available,
white balance. Look adjusts contrast and saturation. Their strengths are
independent. This first matching slice fits rendered pixel statistics using the
existing canonical renderer; it does not claim semantic style transfer or copy
a reference's entire recipe. Existing learned-style profiles remain available.

Before/after comparisons and the reference are rendered from actual recipes.
Outliers start unchecked, failed targets report their own errors, and users choose
which proposals to apply. One batch Undo restores the exact previous recipes.
The source files, local masks, retouching, layers, crop and profile settings retain
their definitions. Recipe/source changes invalidate selected proposals before
application; cancellation changes no accepted edits. Reference images are never
modified by the matching operation.

See [matching qualification](shoot-match-qualification.md) for evidence and open gates.

## Acceptance and evidence

Every workstream must pass its owner checklist plus create/edit/undo/save/reopen/
export proof. Model quality, native foreground interaction, packaged builds and
performance are distinct gates. Keep incomplete gates visible.

First-batch automated evidence: `campaign_delivery_workflow_test` covers geometry,
selection validation, history, local persistence, asynchronous rendering, actual
image sizes/profiles/padding, PDF output, per-file manifest hashes, collision
policies and center-crop output. UI evidence and build receipts are recorded in
`build/professional-expansion-2026-11-07/`.
