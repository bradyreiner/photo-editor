# Color Engine And App Foundations Completion Strategy

## Purpose

This document turns the July 2026 color-engine and application-foundations audit into the
implementation contract for completing the work. It is not a feature wishlist. It defines the
target architecture, migration sequence, proof requirements, performance budgets, legacy-removal
rules, and final release gate.

The strategy has four outcomes:

1. RAW, preview, scopes, thumbnails, carousel output, and export consume one canonical recipe and
   one executable render plan.
2. New edits use one scene-referred color model and the official ACES 2 display renderer, with the
   active monitor transform applied only at the final display boundary.
3. Interactive editing is visibly immediate, settled output matches export, and real RAW,
   real-device GPU, display, print, and editorial evidence gate release.
4. Superseded recipe models, renderers, duplicated mappings, decoder-rendered RAW color paths, and
   unused architectural facades are removed from production rather than retained indefinitely.

The four-ticket board in this strategy is the sole current execution and color-engine completion
authority. The detailed phase material is its technical record and a deferred app-foundations
backlog; it does not add work to the active color-engine goal. The historical
`docs/color-engine-industry-leading-plan.md` remains supporting evidence for the origin of several
color-science tolerances, but it does not own current status or override this document. This
strategy supersedes any earlier architectural direction that treats permanent production support
for every historical renderer as the final state. Historical projects are protected through an
explicit compatibility release, catalog backup, migration evidence, and user-visible approval
before legacy runtime code is deleted.

### Relationship to existing documents

- This document is the modernization and legacy-removal tracker.
- `docs/color-engine-industry-leading-plan.md` is historical supporting rationale for
  color-science tolerances and evidence categories; this strategy owns their current requirements,
  status, and promotion gate.
- `docs/app-performance-operability-plan.md` owns app-wide event vocabulary and suite routing; the
  stricter budgets in this strategy are the completion targets for the rebuilt path.
- `docs/production-launch-checklist.md` and `docs/release-checklist.md` own external credentials,
  signing, packaging, update, clean-machine, and public-release proof.
- `docs/current-blind-spots-and-improvement-areas.md` remains an audit input, not a completion
  tracker.
- `docs/color-science-roadmap.md` remains historical and must not receive new implementation status.
- When an older checklist conflicts with this strategy about canonical ownership or permanent
  legacy retention, this strategy governs the modernization sequence and the color evidence plan
  governs proof thresholds.

## Completion policy

The current color-engine goal is complete only when all four tickets in the active board pass. A
class, control, test, checklist item, or benchmark existing in isolation does not count as
completion. Phase exit gates below preserve the broader audit record, but only requirements mapped
to one of the four active tickets can block the current goal.

Status labels:

- `[x]` means the current repository directly proves the foundation.
- `[~]` means useful implementation exists but the product contract is incomplete.
- `[ ]` means required work remains.
- `BLOCKED` means only external media, hardware, legal rights, or human approval is missing after
  all repository work is complete.

No unchecked item may be relabeled complete from source inspection alone. The completing change
must record the exact test commands, artifacts, hardware, source revision, and any skipped proof.

## Active execution board

This is the entire remaining color-engine queue. There is one frontier ticket, no parallel feature
backlog, and no app-foundations work hidden inside it. A ticket closes only with its named artifact.

| Order | Ticket | Status | Required artifact |
| --- | --- | --- | --- |
| 1 | Fix the shipping RAW camera-characterization defect | DONE | Focused matrix/white-balance tests plus a corrected six-image technical render set |
| 2 | Certify one six-image acceptance corpus | **BLOCKED — EXTERNAL INPUTS** | Rights, hashes, camera/profile metadata, references, and one skin RAW in one promotion-valid manifest |
| 3 | Accept the color result | WAITING | One machine report and two independent recorded blinded decisions covering all six images |
| 4 | Delete replaced color paths | WAITING | PostgreSQL migration proof, focused regressions, and one final candidate build |

### Completed ticket: shipping RAW characterization

Before correction, the shipping camera-linear path rendered at least one technical-corpus DNG
strongly magenta while the portable characterized path was substantially more neutral. The focused
task was to locate and correct that input-boundary error. It did not grow into tone redesign,
preset tuning, UI work, catalog work, packaging, print, HDR-display validation, or a full build.

Exit conditions:

- [x] Focused DCP-matrix tests plus the decoder ownership test prove the matrix convention and
  white-balance boundary.
- [x] Camera-linear decoding leaves camera ratios unbalanced; the engine applies source-white
  mapping and characterization exactly once.
- [x] All six technical DNGs render finite, non-black output through both decoder contracts.
- [x] The shipping/portable diagnostic and six-image visual review show no unexplained gross hue
  cast; expected renderer differences remain explicitly non-promotional.
- [x] The exact focused tests and render-set digest are recorded below.

The camera-linear decoder now preserves unbalanced camera ratios so DCP source-white mapping and
characterization occur exactly once. A second defect was also removed: when LibRaw has no WBCT
lookup table, the decoder now solves temperature and tint from DNG `AsShotNeutral` and the
interpolated dual-illuminant ColorMatrix instead of stamping every capture at 5200 K. The six
resolved temperatures now span `2551.80 K` to `7450.85 K`, with per-capture tint.

The exact corrected technical 12-render set is
`/private/tmp/framehouse-six-raw-render-set-v6/real-raw-render-set.json`; its 24-artifact digest is
`f880eed3d3d082e6a2356188527c674ddd6b3d348bde338dc1165144c459ed0a`. Strict validation returns
`valid-non-promotable-real-raw-render-set`. The diagnostic comparison is
`/private/tmp/framehouse-six-raw-decoder-comparison-v3/decoder-path-comparison.json`; its
12-artifact digest is `7db20b919b508aefeed0e8c28614336cacc51019fdd455ab2c7a279286e5123b`.
The corrected shipping decoder now consumes the dataset's published sensor-RGB-to-linear-sRGB
matrix, composes the reciprocal `AsShotNeutral` gains exactly once, and converts that D65 result
to the engine input boundary. Against the portable regression baseline, the worst p95 Delta E 00
is `19.8547`; the formerly magenta Huawei saturated case fell from mean/p95
`12.6112`/`31.5826` before either fix to `5.5290`/`12.6282`. These are diagnostic
numbers, not color-accuracy acceptance thresholds, because LibRaw is not an independent measured
reference. The six-image contact review shows finite, plausible color without the prior gross cast.

Focused proof passed:

- `build/release-macos/tests/dcp_profile_test`
- `build/release-macos/tests/raw_camera_linear_decoder_test`
- `python3 -B scripts/color/test-real-raw-render-set.py`
- targeted Release builds of `real_raw_candidate_render_tool` and
  `color_corpus_comparison_tool`; no full application build was run

With this ticket closed, the only coverage set is six images: clipped highlight, deep shadow,
saturated wide-gamut color, skin, mixed light, and a 100-edit stack. Those are six rows in one
acceptance report—not six separate projects. Corpus certification is an external/data gate; it
does not authorize substituting synthetic media or broadening the application scope.

### Current blocking ticket: certify one six-image acceptance corpus

The checked-in source inventory now contains exactly six checksum-pinned DNG/reference pairs from
three camera make/model pairs and all four required lighting classes. It includes a mixed-light
Nexus 6 portrait with broad visible skin, highlight transition, and deep shadow. That portrait has
the explicit status `source-qualified-depicted-person-review-pending`; the inventory validator
requires `people_present=true`, the `depicted-person-review-required` blocker, and
`promotion_eligible=false` for that state. Ordinary `source-qualified` candidates still reject
recognizable people.

The exact materialized inventory is `/private/tmp/framehouse-six-raw-source-v3`; strict byte/hash
validation passes against checked-in manifest SHA-256
`77e0c28367d8b53279b83009f578bc50278afc923074b29c3875a90722907294`. Its exact 12-render result is
`/private/tmp/framehouse-six-raw-render-set-v7/real-raw-render-set.json`: 24 artifacts, digest
`89326be5da13b522459df05c3e6d3446ea3ee2e60d71107416651ce6f99b1ecb`, and strict status
`valid-non-promotable-real-raw-render-set`. The per-camera report at
`/private/tmp/framehouse-six-raw-render-set-v7/camera-support-report.json` validates with digest
`34492667eeca4166126241683a52083c11fe7a6aae0a06f69b21fb557cd3f24d`; all three cameras use distinct
shipping camera-linear and portable characterized routes, with zero shipping fallback cameras.

The v7 shipping/portable diagnostic is
`/private/tmp/framehouse-six-raw-decoder-comparison-v4/decoder-path-comparison.json`, artifact digest
`f1c7a492365695d5d8c6eeb4a0f0a6ad4531d0c3d47bfaa7daca245e207b5c3c`. Its worst mean/p95-upper/max
Delta E 00 values are `8.2459`/`17.2760`/`63.8765`; the portrait cell is
`7.3217`/`15.6616`/`42.1732`. Visual inspection found the shipping portrait finite and plausible,
with usable skin-highlight and shadow detail and no gross hue cast. This diagnostic still compares
two implementations, not an independently measured truth.

Repository-side acquisition, integrity, technical skin coverage, rendering, diagnostic, and
camera-support evidence are complete. Ticket 2 remains open only for inputs the repository cannot
truthfully manufacture: project license and depicted-person approval, recorded ISO or an approved
policy for genuinely absent source ISO, a real licensed camera DCP, and a measured/independent
reference suitable for promotion. Those decisions must be recorded in a promotion-valid corpus
manifest; the current public-source inventory must remain non-promotable.

The official object listing for the portrait burst publishes only ten DNGs, ten lens-shading maps,
and `rgb2rgb.txt`; it publishes no separate ISO or profile sidecar. `raw-identify -v` reports
`ISO speed: 0`, but confirms the DNG contains `AsShotNeutral`, D65/Illuminant-A ColorMatrix values,
and identity CameraCalibration matrices. The inventory therefore records `iso: null` and the
`missing-iso-metadata` blocker. No ISO value may be estimated or copied from another frame to make
the manifest pass.

Exact external handoff required to unblock Ticket 2:

1. Record project license approval for the HDR+ dataset and depicted-person approval for portrait
   `hdrplus-nexus6-skin-0218`, or replace it with a rights-cleared skin RAW.
2. Approve a reviewed corpus-policy exception for genuinely absent ISO metadata, or provide six
   equivalent captures with recorded ISO.
3. Supply a licensed camera DCP and measured chart or independently rendered reference whose bytes,
   producer, license, and SHA-256 identities can be bound into the production manifest.

Ticket 3 then requires two distinct people to record the blinded six-image decision. Ticket 4 does
not begin before that acceptance because deleting compatibility paths before approval would remove
the required comparison/rollback boundary.

#### Rejected evidence substitutions

The following readily available substitutes were checked and do not close Ticket 2:

- Creating a DCP from the selected DNG's own ColorMatrix values would only repackage the same source
  metadata. It would test the DCP parser but would not be an independently measured camera profile.
- DCamProf is suitable GPL-licensed profile-generation tooling and includes generic ColorChecker
  spectral data, but tooling and generic spectra are not a measured profile for any of the three
  cameras in this corpus. A real target capture or camera spectral-sensitivity measurement is still
  required.
- The existing Middlebury registered ColorChecker set contains independently dcraw-rendered linear
  RGB chart images, not sensor RAW or measurements of the exact physical target. It remains useful
  for implementation parity and exposure coverage but cannot become the missing measured camera
  truth by relabeling it.
- Adobe camera profiles are not an allowed shortcut. `THIRD_PARTY_NOTICES.md` explicitly states
  that this project does not redistribute Adobe camera-profile payloads, and no independently
  licensed matching Adobe profile has been supplied.
- The Google HDR+ final JPEG is an independently produced multi-frame photographic rendering, not a
  scene measurement or a same-input single-frame colorimetric reference. It may support blinded
  editorial comparison, but it cannot set pixel-accuracy thresholds for the candidate renderer.

These exclusions keep the blocker narrow and auditable: obtain genuine external evidence or record
the explicit policy/rights decisions above; do not manufacture a passing corpus from circular
references.

### Completed gates

| Gate | Result | Proof summary |
| --- | --- | --- |
| Scene-linear edit math | DONE | Neutral, exposure, tone/highlight, and deterministic 10/50/100-edit properties pass |
| Settled preview/export parity | DONE | RAW provenance, compiled plan, stage order, and pre-output float pixels are identical |
| RAW interaction performance | DONE | First feedback p50 `16 ms`/p95 `17 ms`; settled 2560 px p50 `132.5 ms`/p95 `157.7 ms`; memory stable |

Completed gates are not reopened unless a focused regression fails. Their detailed evidence below
is an audit log, not another task list.

Out of scope for the active board: general UI polish, timeline/carousel/agent work, branding,
packaging breadth, controller decomposition that does not block color behavior, and catalog
redesign. PostgreSQL remains the sole catalog authority. SQLite is neither a migration target nor
a fallback. App-wide modernization, clean-machine platform coverage, display/print expansion, and
general release operations remain follow-on programs after the color engine meets this board.

**Current position:** The math, parity, performance, and shipping RAW characterization gates passed
on 2026-09-03. Acceptance-corpus certification is the only frontier ticket and is blocked on the
external inputs listed above. The math gate is backed by the exact
neutral-Tone property, universal scene-exposure properties, deterministic 10/50/100-edit stacks,
monotone tone/highlight checks,
synthetic RAW invariants, and the focused golden render. The parity gate is backed by RAW-bound
settled-preview/export plan admission, exact Decode-through-Geometry stage and float-pixel
identity, and the high-risk encoded-output parity suite. C1 corrected the
benchmark so it edits one resident RAW before any mask or adjustment layer exists and separately
records draft publication, settled 2560 px output, and full-resolution refinement. The original
run had measured selection-layer exposure and treated the 24 MP refinement as settled output.
C1–C5 are complete. No catalog backend, UI, packaging, or broad-build work belongs in this queue.

The first corrected five-sample M4 Pro CPU-fallback run is recorded at
`/private/tmp/framehouse-gate3-global-resident-v2/desktop.jsonl`. It proves the three requested
stages are observable and that resident memory plateaus exactly at `803036320` bytes. It does not
pass C3: draft scheduling was p50 `8 ms`/p95 `13.2 ms`, first visible feedback was p50 `101 ms`/p95
`830.2 ms`, and settled 2560 px was p50 `135 ms`/p95 `1669.4 ms`. The wide tail includes the first
new adjusted-state cache population and confirms that performance work remains rather than hiding
it behind a warm microbenchmark.

C2 removed the RAW-global shortcut that skipped bounded settled rendering and jumped directly to
full resolution. The corrected route is draft when needed, one settled 2560 px render, then one
background full-resolution refinement. A three-write exposure burst coalesced to generation `9`;
only generation `9` rendered, its final exposure was `0.33`, and its full-resolution publication
also reported generation `9`. `editor_preview_feedback_test` then passed in `2.40 s`, including its
forged-stale-result rejection. This proves ordering and latest-generation publication; it does not
claim C3 latency compliance.

C4 and C5 were initially closed by the cache-honest ten-value real-RAW run at
`/private/tmp/framehouse-gate3-c3-pass-proof-v2/desktop.jsonl`. C3 then closed on the exact Release
benchmark producer after eliminating repeated finite-pixel scans at already validated canonical
stage boundaries. After one explicit unmeasured first-use warm-up, all ten measured exposure values
were unique: first visible feedback was p50 `16 ms`/p95 `17 ms`, exact settled 2560 px output was
p50 `132.5 ms`/p95 `157.7 ms`, and the two four-asset cycles held resident memory exactly at
`1541934240` bytes. The three-write supersession proof continued to publish only the latest
generation. The accepted implementation prewarms one 2560 px camera-linear RAW surface plus a
derived 256 px draft, keeps the complete global color interpretation in the draft, and preserves
finite-output validation at each producing stage without rescanning unchanged surfaces at every
executor boundary. `color_pipeline_test` and `render_executor_test` passed after the optimization;
the previously recorded service and forged-stale preview-feedback tests remain the focused C5
evidence. No full application build was run.

The following matrix maps the original report to the four active tickets. It is a scorecard, not a second
checklist.

| Reported failure | Completion test | Current truth | Next step |
| --- | --- | --- | --- |
| Exposure changes across the luma range | Direct scene-linear gain; statistically supported cross-region spread `<= 0.25` stop | Canonical math and six calibration candidates pass; current worst spread is `0.188887` stop | Reproduce on the new exact Release producer and approved licensed RAW set |
| Edits do not stack well | Global/full-mask/layer equivalence `<= 3e-6`; 10/50/100-edit output finite, deterministic, bounded, and undo/redo stable | Deterministic and controller stress gates pass | Blinded real-photo stack-integrity approval |
| Highlights and shadows lose detail | Sensor clipping/black-floor invariants, monotone tone order, finite output, no hue instability, and unanimous visual approval | Synthetic/property gates pass | Rights-cleared high-dynamic-range RAW and independent review |
| Color range looks small | No intermediate sRGB bottleneck or undeclared early gamut fit; signed/extended working color survives to one explicit output boundary | Wide-gamut/ACES architecture and deterministic tests pass | P3, HDR, calibrated-display, and print corpus proof plus preset retuning |
| Preview and final differ | Same canonical recipe/plan/decoder provenance; settled/export comparison passes the precommitted corpus thresholds | Deterministic preview/export tests pass | Rebound six-image candidate plus full licensed-corpus heatmaps |
| Editing and final RAW rendering are slow | First feedback p50 `<= 16 ms`, p95 `< 33 ms`; settled 2560 px p50 `< 150 ms`, p95 `< 250 ms`; zero stale frames and bounded memory | Resident real-RAW M4 Pro CPU-fallback proof passes with stable memory and latest-generation publication | Six-case photographic acceptance |

The authoritative acceptance-corpus asset set is a populated production instance of
`perf-corpora/color-validation-required/manifest.example.json`. The checked-in example and public
source inventories are schema/acquisition inputs only and cannot pass promotion. Until a
rights-approved production manifest exists and validates with `--require-promotion-approval`,
Ticket 2 remains open even when every synthetic test passes.

### Archived full-program frontier — 2026-09-02

This snapshot explains older evidence and migration constraints. It is not an execution queue and
does not override the active board above.

- The canonical v13.2 transition implementation, direct processor, production LUT contents,
  recipe persistence, render-plan admission, preview/export identity, and focused regression tests
  are implemented. The exact corrected Release producer is SHA-256
  `a793e95ca1b8f45255794ba4b8459a159c31ea42cc6dd6e1a25f8d6636456b42`.
- Source now contains non-pixel migration authorization/receipt enforcement that postdates that
  Release executable. The `a793e9...` producer remains the exact six-image color-calibration
  reference, but it is not the final execution producer because it cannot enforce the new native
  authorization and merge contracts. A new Release candidate must be built later, reproduce and
  rebind the six calibration artifacts and v3 diagnosis, and generate a new review packet before a
  final approval-bound refresh plan may exist. That candidate build is deliberately deferred here;
  no 346-step app build or asset rerender was started.
- Six current-catalog calibration candidates pass the machine universal-exposure gate with a worst
  statistically supported cross-region spread of `0.188887` stop against a `0.25`-stop ceiling.
  Their human review remains incomplete: six of six decisions are `pending`.
- Independent photographer review of those six assets remains required, but the current packet is
  now a calibration/reference handoff rather than final execution authorization. Until the new
  producer reproduces the calibration and a completed, unanimously approved ratings file validates
  against its rebound packet, none of the 39 available pending assets may be rendered and no
  migration may be accepted. The current self-contained deterministic reference archive is
  available at
  `build/release-macos/proofs/real-catalog-transition-v13-2-a793e9-review-06-20260902T1301Z.zip`.
- After calibration approval, generate a new plan that binds the approved ratings, review packet,
  v3 diagnosis, progress report, and exact producer. Then execute its 39 expected single-asset
  chunks one at a time, merge and validate each result, resolve the two missing originals by
  mount/relink or an explicit archive/unsupported disposition, and obtain the required
  catalog-wide approvals. No executable 39-asset plan exists before that approval-bound generation.
- Only after the catalog evidence is complete may the compatibility release, final package,
  clean-machine/platform, hardware GPU/display/print, real licensed RAW, professional-workflow,
  and legacy-removal gates run. Legacy deletion remains deliberately last.
- No two-hour all-or-nothing suite/package build is running. Continue source work with targeted
  objects and focused tests. Before final photographer review, one new exact Release application
  executable is required because native authorization enforcement changed its identity; schedule
  that candidate build as a separately bounded operation, then rerender only the six calibration
  assets. Reserve the complete suite/package rebuild for the candidate that passes those gates.

## Current audited position

### Executive audit: the good, the bad, and the ugly

**The good.** This is not an empty or unserious color engine. It already has floating-point
scene-linear buffers, wide-gamut working spaces, explicit decoded-pixel provenance, exact ICC
payload execution, official ACES 2 output rendering, unusually broad DCP support, local/layer/global
operator parity tests, deterministic recipes, output parity tests, and meaningful cancellation,
cache, fallback, and latency telemetry. The rebuilt v13 schema, compiler, RAW boundary, shared
preview/export bridge, and typed executor contracts are credible foundations.

**The bad.** The strongest foundations are not yet the only production route. Canonical v13 is the
durable catalog authority, but the desktop still materializes a v12-shaped compatibility state
inside the editor presentation model and migration-release persistence projection; catalog items,
multi-photo history, agent planning/model/repair requests, agent draft/rollback/commit state, and
editor-agent handoffs now retain asset-bound adjustment patches instead. Preview now defers Decode to
`RenderExecutor`, but export
uses a metadata-only RAW probe before plan compilation while the executor-owned Decode callback
performs the admitted file read, boundary validation, crop, linearization, and pixel-dependent mask
compilation;
and preview, display, migration, and workflow decisions
remain distributed across large C++ and QML files. Selectable scopes now cross the canonical
admission/execution boundary, but that does not make the surrounding controller decomposition
complete. CPU fallback is too slow for the
required interactive budgets, GPU residency/parity is incomplete, and compatibility code cannot
be deleted safely before catalog backup and user-approved migration ship.

**The ugly.** Some architectural duplication has produced real pixel errors, not just untidy code:
camera-linear RAW thumbnail pixels were previously allowed to lose their decoded metadata and be
treated like tagged preview RGB; carousel had an independent incomplete color route; output could
be applied at more than one publication boundary; retired no-op RAW controls polluted intent and
cache identity; and concurrent thumbnail workers could each build the same 97-cube ACES LUT.
Those specific defects are now removed. The remaining ugly risk is release truth: licensed
multi-camera RAW, physical Metal/CUDA, real monitor and print, clean-machine packaging,
target-hardware memory/latency plateaus, and blinded editorial acceptance are still absent.
Deterministic aggregate memory ceilings now exist, but synthetic and source-contract tests cannot
honestly substitute for physical workload evidence.

**Audit verdict.** The engine has moved from a collection of strong components with inconsistent
ownership toward a coherent modern path, but the application is not color-release-complete until
every unchecked gate below is implemented and the external evidence is attached. No current test
result justifies an “industry-leading” or fully WYSIWYG production claim.

### Proven foundations

- [x] Linear floating-point working buffers and wide-gamut working profiles exist.
- [x] Input, working, preview, proof, and export profile concepts are explicit.
- [x] Decoder metadata records whether demosaic, white balance, camera characterization, and
  decoder rendering have occurred.
- [x] Explicit DCP and camera ICC requests require camera-linear RAW pixels and fail closed.
- [x] LCMS owns payload-backed ICC execution and OCIO 2.5.1 provides the official ACES 2 output
  renderer.
- [x] DCP parsing and execution cover matrices, multiple illuminants, HueSatMap, LookTable,
  ProfileToneCurve, ProfileGainTableMap2, and RGBTables.
- [x] Preview/export, color-transform, DCP, chart, golden, property, and migration tests provide a
  substantial deterministic safety net.
- [x] Render cancellation, cache state, fallback reasons, stage timing, GPU counters, and
  user-visible latency have observability contracts.
- [x] Catalog and MCP Rust unit tests pass in the audited checkout.

### P0 defects this strategy must close

- [x] The preview compiler now replaces the intermediate recipe target with a manually selected
  monitor profile, or binds the discovered system monitor ICC payload directly as the preview
  output asset. The final display handoff recognizes the same profile and does not perform a
  second sRGB-first conversion. Physical wide-gamut monitor measurement remains a Phase 10 gate.
- [x] Ordinary RAW and explicit camera-profile RAW now share one engine-owned camera-linear
  contract whenever the RAW input transform is enabled. LibRaw DNG matrices are resolved to an
  executable camera-to-XYZ D50 transform; its native `cam_xyz` fallback carries an explicit D65
  reference. The exact coefficients and reference white cross the decoded-metadata boundary,
  participate in canonical decoder provenance, and execute in the engine input stage. A missing
  matrix/profile fails canonical admission with
  `camera_linear_characterization_required` instead of silently applying identity. The separately
  named characterized-linear decode remains only for the direct display helper and an explicitly
  disabled RAW input transform. Decoder-applied white balance is also carried into the resolved
  input recipe: it is no longer adapted a second time in camera RGB, while the creative
  Temperature/Tint operation remains active after camera characterization.
  The decoded source-linear cache publication was bumped to
  `source-linear-v2-camera-characterized-in-engine`, preventing a pre-cutover
  characterized-linear cache entry from being reused as camera RGB.
- [x] New desktop state, bare engine `ColorPipelineConfig`, and v13 recipes default to
  `aces2-ocio-v1`; old serialized recipes without an explicit renderer still load as legacy for
  the compatibility window. Compatibility tests now name the legacy renderer explicitly instead
  of depending on an unsafe engine default.
- [x] Preview and export no longer independently construct `ColorPipelineConfig` or the global
  `ColorAdjustmentState`; one desktop bridge owns compatibility mapping. Carousel no longer owns a
  mapper at all: it calls the shared source decoder and canonical output renderer.
- [x] Production preview and export now execute real NormalizeRaw, CameraCharacterization,
  SceneAdjustments, Tone, optional Curves, Look, Layers, Detail, Geometry,
  PhotographicOutput, and Publish nodes through `RenderExecutor` whenever those nodes are present
  in the admitted plan. Decode is now stage zero in the completed manifest: preview defers its
  file-to-linear operation to the executor callback, while export admits from size/metadata alone
  and performs its full file read and linear materialization inside the executor callback. Canonical
  preview and export execute real
  Proof, proof-gamut diagnostics, and custom DeviceTransform nodes. The misleading
  descriptive-only `RenderGraph` and the fused decode-to-Look compatibility body are deleted; the
  section execution helpers are also deleted after export moved to one full submission and preview
  moved to one adjustment submission plus one typed publication resumption at its declared cache
  boundary.
- [x] The ten common color/tone controls now execute one operator for global, full-mask local, and
  adjustment-layer use, with post-operator alpha composition and `3e-6` parity proof in both
  ordinary and ACES extended-range fixtures.
- [x] New ACES edits no longer use fixed tone anchors or an automatic image-extrema shoulder;
  common global/local/layer controls share one operator, composition is explicit, undeclared
  chroma protection is removed, and 10/50/100-edit properties are deterministic and finite.
  Historical anchor/gamut behavior remains isolated to the legacy compatibility renderer and is
  tracked for deletion only after the migration gate.
- [x] Modern ACES tone controls now operate over useful tonal regions instead of degenerating at
  scene-range extremes: Contrast is midtone-weighted, Brightness has a broad midtone bell, Whites
  reaches ordinary bright values, Blacks reaches ordinary shadow values, and explicit Filmic
  Shoulder uses a monotone signed scene-tone curve independent of the 8192-unit ACES storage
  headroom. Positive shoulder compresses and negative shoulder opens highlights while preserving
  finite ordering. The focused math and luminance-ramp property tests gate direction,
  monotonicity, extended range, and highlight-detail order.
- [ ] CPU fallback misses interactive budgets for common full-frame work, and current command-line
  proof cannot exercise an actual Metal device.
- [ ] Synthetic RAW-style patches dominate automated color proof; the required licensed,
  multi-camera real RAW corpus is not materialized.
- [x] The headless CMake preset no longer creates the Qt-dependent Metal ACES 2 parity target in a
  non-desktop build. The engine-only Metal parity targets remain configured. Verified 2026-07-28
  with `cmake --preset ci-headless`, `cmake --build --preset build-ci-headless`, and
  `ctest --preset test-ci-headless --output-on-failure`: the suite now passes 32 tests, with the
  two physical Metal tests explicitly reported as skipped because this execution context has no
  system device. The clean rebuild also caught and removed hidden test dependence on the former
  implicit legacy engine default: compatibility chart/sweep fixtures now select that renderer
  explicitly, and the Metal source contract follows the executor-owned output boundary.
- [x] The three built-in preset hash mismatches were classified as stale expected evidence within
  the same uncommitted renderer-modernization series, not accepted as final preset tuning. A
  bounded test mode now captures the exact synthetic source/render pairs for inspection. The
  inspected 2026-07-28 CPU renders were structurally valid. After the new-recipe default was
  promoted to ACES and output rendering was moved to its single publication boundary, the
  deterministic change-detector hashes were refreshed again after removing undeclared generic
  skin/tonal chroma suppression and, most recently, after correcting the canonical stage order:
  `9129669526530108660` (Clean Portrait), `15304467165491280083` (Landscape Pop), and
  `15195584239314180347` (Clean Black and White).
  This is not preset acceptance: licensed-photo retuning and two-reviewer approval remain open in
  Phases 6 and 10.

### P1 application-foundation defects

- [~] `EditorPreviewController` and `StudioWorkspace.qml` remain large coordination/presentation
  surfaces and still create broad regression risk. Their former engine, recipe-codec, mask,
  viewport, scope, display-profile, persistence, render-execution, and worker responsibilities have
  been extracted behind guarded services. `color_adjustments.cpp` is now a 1,223-line ordered-stage
  coordinator and `edit_recipe_state.h` is a 622-line compatibility/presentation state rather than
  the former engine/schema owners. Phase 8 records the still-open controller and QML ownership
  boundaries; the four files are no longer accurately described as equally undecomposed.
- [~] Canonical v13 is now the durable recipe authority, and new cross-feature edit handoffs use
  scoped adjustment patches. Catalog memory, thumbnails, recovery overrides, virtual copies, and
  batch undo/redo now retain asset-bound patches rather than version-12 documents. Agent
  draft/rollback/commit maps and the editor's external-agent undo handoff are also patch-native.
  The Qt desktop layer still materializes a version-12-shaped compatibility state in the editor
  presentation model and the required migration-release persistence projection, so the old model
  cannot yet be deleted. The separate obsolete engine version-3 candidate type is deleted.
- [ ] QML and the preview controller contain workflow and render-policy decisions that should be
  owned by typed C++ services.
- [~] PostgreSQL is restored as the sole desktop catalog authority after the product owner rejected
  the unapproved SQLite cutover. Managed/external startup, helper routing, SQLx migrations,
  `pg_dump` backup, runtime packaging, and the normal desktop environment contract are restored;
  clean-platform backup/restore and packaging proof remain open. The production release path now
  fails before configuration unless it receives an approved publisher, a complete Sparkle
  distribution/feed/public-key contract, and a self-contained PostgreSQL runtime with executable
  server/backup tools, `initdb` support data, and no Homebrew-linked Mach-O dependencies.
- [ ] Timeline, carousel, and AI breadth compete with the import, cull, batch edit, refine, export,
  relaunch, and recovery path that determines whether the photo editor is credible.
- [ ] The current checkout and generated performance evidence are too broad and dirty to represent
  a coherent release candidate.

## Non-negotiable product invariants

Every implementation decision must preserve these invariants.

1. **One recipe authority.** One Qt-independent recipe schema owns persisted edit intent. UI state
   may adapt it but may not redefine it.
2. **One recipe compiler.** Preview, export, thumbnails, scopes, presets, carousel, and batch work
   receive the same immutable compiled render plan.
3. **One executable graph.** The stage graph determines execution, caching, invalidation,
   cancellation, diagnostics, and backend selection.
4. **One settled RAW contract.** Settled preview and export start from the same camera-linear
   decode and characterization contract on every supported platform.
5. **Exposure is scene-linear gain.** A global exposure edit multiplies all scene-linear channels
   before tone, look, local-composite, and output rendering. No output or local stage may reinterpret
   the global exposure control.
6. **Draft may simplify spatial quality, not color intent.** Draft demosaic, scale, detail, and mask
   resolution may differ. Input characterization, working space, creative parameters, rendering
   model, and target identity may not.
7. **The monitor transform is last.** Scene grading and the photographic display renderer operate
   against an explicit display target. The verified OS monitor ICC transform is the final device
   conversion and must not follow a prior accidental sRGB bottleneck.
8. **Settled preview is WYSIWYG.** When preview and export use the same declared target, their
   decoded linear output must meet the same perceptual parity threshold before encoding.
9. **Stack semantics are defined.** The same operator with the same parameters and full mask alpha
   produces the same pre-composite result whether invoked globally, locally, or by an adjustment
   layer. Blend mode and opacity are separate, explicit operations.
10. **Extended values survive until the declared output boundary.** Negative and over-range
    working values are not clipped by QImage packing, cache serialization, tiling, masks, or GPU
    upload.
11. **Historical migration is explicit.** No project is silently moved to a new rendering model.
    Migration creates a backup, comparison evidence, and an undoable/user-approved cutover.
12. **No permanent dual implementation.** Compatibility code has a removal release and deletion
    gate when introduced.
13. **Correctness and responsiveness ship together.** A faster path cannot be accepted without
    visual/parity proof; a correct path cannot be accepted if normal interaction remains visibly
    late or memory-unbounded.

## Target architecture

```text
QML controls and commands
  -> EditCommandRouter
  -> Canonical EditRecipe v13
  -> RecipeCompiler
  -> immutable RenderPlan
  -> executable RenderGraph
       raw_decode
       input_characterization
       scene_adjustments
       masks_and_local_adjustments
       layer_composite
       detail
       geometry
       photographic_output_render
       proof_simulation
       device_or_export_transform
       encode_or_publish
  -> PreviewPublisher / ExportEncoder / ThumbnailPublisher / ScopeSampler
```

The graph has one CPU reference implementation. Metal and CUDA implement graph stages behind the
same plan and publish the same diagnostics. Backends may fuse stages, but the fused result must
remain equivalent to the reference plan and must report which logical stages it executed.

### Canonical ownership

| Concern | Final owner | Desktop responsibility |
| --- | --- | --- |
| Recipe schema, defaults, validation, migration output | `engine` | Bind fields and display errors |
| Recipe-to-render compilation | `engine` | Provide asset/profile handles and target intent |
| Stage order, invalidation, cache identity | `engine` | Submit current plan and viewport priority |
| RAW decode contract | engine-facing decoder service | File access and platform capability reporting |
| Camera characterization | `engine` | Profile selection UI |
| Tone, color, masks, layers, detail | `engine` | Commands, selection, and overlays |
| Display target and output rendering | `engine` | Supply verified display/profile facts |
| Monitor device conversion and publication | thin desktop display service | Native display discovery and texture publication |
| Export encoding and readback | output service | Destination, naming, and job UX |
| Catalog and job persistence | Rust repository interfaces | C++ models and actionable UI state |

## Completion ledger

This ledger is the top-level status surface. A phase changes to `COMPLETE` only when its exit gate
passes. Detailed checkboxes remain authoritative when the ledger and phase disagree.

| Phase | Status at strategy creation | Depends on |
| --- | --- | --- |
| 0. Stabilize baseline | OPEN | None |
| 1. Canonical recipe and compiler | OPEN | Phase 0 |
| 2. Executable render graph | OPEN | Phase 1 |
| 3. Display, proof, and export targeting | OPEN | Phases 1-2 |
| 4. One settled RAW pipeline | OPEN | Phases 1-2 |
| 5. Tone, highlight, color, and stack semantics | OPEN | Phases 1-4 |
| 6. One photographic renderer and look system | OPEN | Phases 3-5 |
| 7. Immediate GPU-resident interaction | OPEN | Phases 2-6 |
| 8. Desktop decomposition | OPEN | Phases 1-7 |
| 9. Catalog and product foundations | OPEN | Phase 0; may proceed beside Phases 1-8 |
| 10. Real-world validation | OPEN | Phases 3-9 |
| 11. User migration and legacy deletion | OPEN | Phases 1-10 |

## Initial file-level disposition

The names below are implementation targets, not permission for immediate deletion. Replacement
must land and pass its phase gate before the old owner is removed.

| Current surface | Disposition | Replacement or final responsibility |
| --- | --- | --- |
| `apps/desktop/src/edit_recipe_state.h` | Shrink to compatibility/UI adaptation, then remove as schema owner | Engine `EditRecipeV13`, serializer, validator, and migrator |
| Deleted engine v3 recipe/migrator files | Deleted | Canonical engine v13 recipe |
| `apps/desktop/src/editor_preview_controller.cpp` | Decompose | Session, scheduling, publication, display, mask/layer, preset, and history services |
| `apps/desktop/qml/workspaces/StudioWorkspace.qml` | Decompose | Focused canvas, page, inspector, tool, mask/layer, proof, and status components |
| `apps/desktop/src/photo_render_utils.cpp` | Reduce to graph client and output adapter | Engine compiler/executor and output service |
| Deleted carousel color mapper/renderer | Deleted | Shared source decode and canonical desktop output renderer |
| `engine/src/pipeline/color_adjustments.cpp` | Split by stage/operator and remove legacy branches | Shared scene, tone, look, local, layer, detail, and composition kernels |
| Deleted descriptive `render_graph.cpp` | Deleted | Typed recipe plan, `RenderExecutor`, and focused tile planner |
| Deleted `apps/desktop/src/raw_image_decoder.mm` `CIRAWFilter` route | Deleted | One LibRaw camera-linear contract |
| `apps/desktop/src/raw_image_decoder_libraw.cpp` | Keep and harden | Settled/final RAW authority and bounded draft decode |
| `apps/desktop/src/working_image.h` | Split and remove hidden core conversion decisions | Typed engine image buffers plus thin Qt pack/unpack adapters |
| `engine/src/pipeline/color_pipeline.cpp` legacy renderer selection | Delete after migration | Official ACES 2 renderer |
| `engine/src/pipeline/output_rendering.cpp` | Keep stable facade, remove obsolete implementations | Compiled display/export/proof transforms |
| `engine/src/pipeline/metal/` | Keep scheduling/backend, replace duplicated color math | Compiled graph stages and fused measured transforms |
| `rust/crates/catalog/` | Keep repository boundary | PostgreSQL-backed catalog repository |
| Managed local PostgreSQL runtime | Keep and harden | Loopback-only packaged PostgreSQL authority unless an explicit external URL is supplied |
| `tests/synthetic_raw_pipeline_invariant_test.cpp` | Retained and accurately named synthetic invariant test | Separate real RAW corpus execution gate |
| Golden preset hashes | Retain only as change detectors | Perceptual, behavioral, visual, and editorial approval evidence |

## Phase 0 — Stabilize the baseline

### Work

- [ ] Create a narrow modernization branch or clean worktree from an agreed revision.
- [ ] Preserve all unrelated user changes before moving implementation work.
- [x] Fix `ci-headless` configuration so non-desktop tests never reference undiscovered Qt targets.
  The desktop/Qt-dependent ACES 2 Metal parity target is now guarded by
  `PHOTO_EDITOR_BUILD_DESKTOP`; the exact-current headless build and 36-test suite pass, with two
  physical Metal members explicitly skipped.
- [x] Build and run `ci-headless`, macOS Debug, sanitizer, and Rust workspace gates.
  The exact-current 2026-09-01 verification reconfigured every preset before building. Headless,
  ASan/UBSan, and TSan each ran 36 tests with 34 passes, zero failures, and the same two explicit
  unavailable-hardware Metal skips; TSan reported no races. The complete 2,195-step macOS Debug
  build passed, followed by 163 passes, zero failures, and three explicit Metal hardware skips out
  of 166 tests. Its Debug-only model inference and macOS vision tests passed, and the comprehensive
  `editor_preview_controller_test` completed in 251.84 seconds. The locked Rust workspace passed
  57 unit tests plus every doc test across catalog, migration helpers, MCP, metadata, jobs, FFI,
  and telemetry. These results close the current-tree repository gate while leaving actual Metal,
  CUDA, Windows, clean-machine, and release-revision evidence to their named later phases.
  After the HDR AVIF and bounded-TIFF changes, the sanitizer configurations were refreshed again on
  2026-09-02: ASan/UBSan and TSan each ran all 36 headless tests with 34 passes, zero failures, and
  the same two explicit no-device Metal skips; TSan emitted no race report. The architecture
  boundary suite and full-worktree `git diff --check` also passed. This refresh covers the current
  source changes but does not convert either hardware skip into release evidence.
- [x] Resolve the three built-in preset hash mismatches by classifying the renderer change:
  unintended regression, intended renderer correction awaiting approval, or stale expected evidence.
  They are stale expected evidence. `PHOTO_EDITOR_TEST_PRESET_BASELINE_ONLY=1` plus
  `PHOTO_EDITOR_PRESET_BASELINE_DIR=<path>` captures the three source/render pairs without running
  the long controller suite. `render_golden_test`, the preset inventory validator, and the preset
  approval validator remain separate gates; refreshed hashes do not promote or approve a look.
- [ ] Record current preview/export output, tone ramps, preset renders, real-memory behavior, and CPU
  timings as a non-promoting diagnostic baseline.
- [x] Add a checked machine-readable capability report for LCMS, OCIO, LibRaw, Metal/CUDA,
  monitor-profile discovery, HDR encode, and optional model paths. The
  `photo-editor-runtime-capabilities:v1` report is generated from the production probes by
  `application_capability_report_test`, validates unavailable-reason completeness, and can be
  persisted with `PHOTO_EDITOR_CAPABILITY_REPORT_PATH`. Its 2026-07-28 execution reported LCMS
  2.18, OCIO 2.5.1, LibRaw 0.21.5, libheif 1.21.1, CPU fallback with
  `no_system_metal_device`, platform-inapplicable CUDA, unavailable GUI monitor discovery in the
  probe context, and the exact missing optional-model root/status.
- [x] Mark hardware-skipped tests as incomplete evidence in aggregate reports rather than successes.
  `run-preview-release-proof.py` requires a passed hardware Metal gate and a release-eligible desktop
  proof; smoke flags or skipped subgates cannot produce a release pass. Verified 2026-07-28 with
  `test-preview-release-proof-report.py` and `test-preview-release-proof-runner.py`, including their
  adversarial skipped-Metal cases. The headless CTest summary remains diagnostic evidence only.
- [x] Separate ignored/generated build and performance artifacts from release evidence and
  establish a retention limit. `docs/performance-artifact-retention.md` defines ignored local
  diagnostics as non-promoting, requires checked provenance before evidence promotion, and sets a
  14-day/20-runs-per-category default. `prune-performance-artifacts.py` is dry-run by default,
  accepts deletion only with `--apply`, manages only immediate timestamped directories under a
  root literally named `perf-artifacts`, never follows symlinks, and preserves `.retain` or
  `promotion-manifest.json`. Its CI self-test proves age/count pruning, pin retention, unmanaged
  entry preservation, non-mutating planning, exact applied deletion, and fail-closed roots.

### Phase 0 execution evidence — 2026-07-28

- `ci-headless`: configure and build passed; the exact-current 31/31 suite passed in 6.42 seconds
  after the canonical v13 recipe/compiler, typed executor, official-ACES engine default, and output
  publication cutovers, with two engine Metal runtime tests explicitly skipped for
  `no_system_metal_device`.
- `ci-headless-asan-ubsan`: configure/build passed; 29/29 tests passed with the same two explicit
  hardware skips.
- `ci-headless-tsan`: configure/build passed; 29/29 tests passed with the same two explicit
  hardware skips and no reported races.
- `build-dev-macos-debug`: the complete 946-step desktop/app/test build passed.
- `cargo test --manifest-path rust/Cargo.toml --workspace --locked`: 46 tests passed.
- `editor_preview_controller_test`: passed in 246.78 seconds after adding a color-managed,
  high-precision Qt TIFF fallback for execution contexts where all Core Image render contexts fail.
  The fallback preserves the native linear-image contract rather than reporting a misleading
  `Source file unavailable`.
- `editor_preview_controller_test`: passed again after the single-output-boundary, native-texture
  scheduling, proof-mask, reset, and deep-stack changes. This run proves that a retained
  full-resolution native texture does not trigger a hidden low-resolution upload, queued
  visible-tile/background renders cannot strand a pending request, built-in sRGB proofing
  publishes an exact proof-derived gamut mask, the 10,000-dab controller case advances, and reset
  preserves color-management state.
- The focused cross-boundary CTest suite passed 17/17 in 351.86 seconds against the final source
  state of this implementation slice: `render_tile_planner_test`, `render_executor_test`,
  `render_invalidation_policy_test`, `edit_recipe_v13_test`,
  `color_pipeline_test`, `color_pipeline_property_test`, `color_tool_invariant_test`,
  `edit_recipe_v13_adapter_test`, `edit_recipe_pipeline_bridge_test`,
  `raw_camera_linear_decoder_test`, `agent_edit_recipe_patch_test`,
  `editor_preview_math_test`, `edit_recipe_state_test`, `editor_preview_controller_test`,
  `render_parity_test`, `render_golden_test`, and `export_job_controller_test`. The controller
  case passed in 328.07 seconds.
- After the catalog thumbnail metadata/output cutover and ACES cold-cache deduplication,
  `catalog_asset_model_test` passed in 30.05 seconds. `color_pipeline_test`,
  `aces2_ocio_reference_test`, `render_parity_test`, and `render_golden_test` then passed 4/4 in
  6.06 seconds. The two color tests directly prove that eight concurrent cold-cache requests
  produce one immutable ACES processor build and one bounded 97-cube output-LUT build rather than
  eight duplicate builds. A final combined bridge/catalog/controller run passed 3/3 in 358.33
  seconds; the complete controller case passed in 327.03 seconds.
- After removing the successful-Metal detail early return, `color_pipeline_test`,
  `color_pipeline_property_test`, `color_tool_invariant_test`, `render_parity_test`, and
  `render_golden_test` passed 5/5 in 6.03 seconds. This is deterministic CPU and integration
  protection; the corrected successful-device branch still requires the physical Metal parity
  gate.
- After removing detail-stage output-gamut fitting, the same five color/parity/golden tests passed
  5/5 in 5.29 seconds. The property suite now requires a high-contrast chromatic sharpening edge to
  retain finite signed and over-range working-space excursions with unchanged alpha; the inspected
  intentional detail golden changed from `18191492922283544536` to
  `14959644719180171112`. Catalog, complete preview-controller, and export integration then passed
  3/3 in 369.63 seconds on the same source; the controller case passed in 324.85 seconds and export
  in 13.93 seconds.
- After making metadata-free engine state non-RAW by default, six focused color/RAW/parity tests
  passed 6/6 in 6.83 seconds. RAW calibration, camera-look, and DCP fixtures now opt into the RAW
  boundary explicitly, while a property test requires bare working RGB to retain signed and
  over-range channels.
- After deleting the engine's duplicate top-level exposure, temperature, and tint fields, the
  nine-test color/bridge/preview-math/parity/golden set passed 9/9 in 8.25 seconds. The desktop
  recipe boundary now maps its compatibility/UI fields directly into the sole authoritative
  `InputTransformSettings` values; there is no engine fallback or neutral shadow copy left to
  diverge. Catalog and export integration passed in 33.13 and 15.18 seconds, and the complete
  controller test passed in an isolated 340.63-second rerun. A preceding combined run hit the
  existing timing-sensitive 12-second 10,000-dab settle deadline while a full render was still
  active, so this evidence proves the mapping cutover but does not claim the controller stress
  case is latency-stable.
- On 2026-07-29 the exact controller suite passed in 332.31 seconds after canonical catalog/preset
  thumbnail admission and saved-preset integrity repair. The remaining 100 macOS tests were then
  run in parallel: 21 reported failures and three physical Metal cases were explicitly skipped.
  Catalog and export were load-sensitive false negatives in that run and passed immediately in
  isolation in 34.86 and 14.45 seconds. The remaining failures are not accepted as color proof:
  shared desktop-shell source/UI contracts, one mask-preview source contract, timeline stress
  hydration, and the external `pdfinfo` requirement remain open and keep the broad desktop gate
  red.
- After consolidating global, photo-layer, and slow spatial post-tone color processing onto one
  kernel and deferring their gamut fit to one final compatibility boundary, the focused
  color/property/tool/parity/golden set passed 5/5 in 7.72 seconds. The property suite constructs
  one recipe containing global, full-mask local, and adjustment-layer color edits and requires
  exactly one recorded final gamut-fit pass. The intentional Clean Portrait golden changed from
  `12238608087895143688` to `1765046725147164239` because detail now completes before that single
  fit, matching the declared `Look -> Layers -> Detail -> PhotographicOutput` order.
- After separating photometric lens-vignette correction from spatial geometry and moving global
  resampling after Detail, the same focused set passed 5/5 in 7.76 seconds. The property suite
  renders an asymmetric chromatic edge and proves combined execution matches an explicit
  `Detail -> Geometry` sequence within `2e-6` while measurably differing from the reversed order.
- Catalog and export integration then passed in 30.21 and 12.94 seconds. The first complete
  controller run correctly exposed the stale Landscape Pop golden; the inspected expected hash
  changed from `10405744260654893220` to `14887556646374585508`, while the other built-in preset
  hashes remained stable. After that fixture update, the isolated complete controller test passed
  in 327.43 seconds.
- After moving global creative LUTs ahead of retouch/photo layers and deferring
  texture/clarity/dehaze until Detail, the focused color/property/tool/parity/golden set passed
  5/5 in 6.56 seconds. A new asymmetric-edge property proves combined execution matches explicit
  `Look -> Layers -> Detail` execution within `2e-5` and differs from the reversed order. Behavior
  checks remained green while the inspected Night Recovery, Clean Portrait, and Landscape Pop
  goldens changed to `9094823290763938138`, `1092159188374688233`, and
  `7866668603536575522`.
- After preventing the compatibility monolith from gamut-fitting perceptual ACES working pixels,
  the same focused set passed 5/5 in 7.72 seconds with no final-render golden change. The mixed
  global/local/layer property now requires one final fit for the legacy renderer and zero for
  `aces2-ocio-v1`; ACES retains finite extended working RGB for its photographic-output stage,
  while the final preview/export mapping remains stable.
- On the reordered final stack, catalog and export integration passed in 34.54 and 14.51 seconds.
  The controller preset registry then reported the expected stage-order golden changes for Clean
  Portrait, Landscape Pop, and Clean Black and White; their inspected hashes are now
  `3842820940458986717`, `16543298839241495295`, and `5480670012431624456`. The isolated complete
  controller test passed in 327.68 seconds after those fixture updates.
- After extracting callable engine Geometry and compatibility-output stage functions, the focused
  set passed 5/5 in 7.84 seconds. The stage-order property now invokes the public Geometry stage
  directly and proves it accepts the declared working-surface contract; the monolithic entry point
  delegates its last two operations to those same functions.
- Perceptual ACES execution now ends the scene graph at Geometry instead of entering the legacy
  compatibility-output boundary. Black-point compensation and display headroom are therefore
  applied once by PhotographicOutput rather than once before it and again inside it. The former
  fixed four-stop working ceiling was also removed: the target-independent ACES working boundary
  is 8192, just above the official 10,000-nit shaper limit of 7168. A property carries a
  `24/12/6` linear highlight and zero black unchanged through scene execution, then requires
  PhotographicOutput to lift black and bound the display result. The focused
  color/property/tool/parity/golden set passed 5/5 in 6.88 seconds with no golden change.
- The Debug CPU benchmark measured the remaining latency honestly: before the no-mask copy fix,
  three 960x640 spatial/detail renders totaled 1050 ms and three 1920x1280 renders totaled 3786 ms;
  the simple-global totals were 440 and 1548 ms. Avoiding the unconditional full-frame
  edge-matte source copy when no local mask can consume it reduced the repeated spatial preview
  total to 978 ms and the simple-global totals to 429 and 1416 ms. The focused correctness set
  passed 5/5 in 7.76 seconds afterward. These Debug diagnostics show a real memory/copy reduction
  but remain far outside the release interaction budgets.
- Exact global tone compilation is now a bounded four-entry immutable LRU keyed only by the
  rendering model and the sixteen controls read by `apply_global_tone()`. A cache hit reuses the
  table by shared ownership instead of rebuilding and copying 16,385 doubles; the miss path is
  serialized so concurrent callers cannot duplicate compilation. Telemetry records hits, misses,
  and build time. A property requires one miss for a unique recipe, exact pixel identity plus one
  hit when the same recipe moves from interactive to final quality, and a fresh miss plus changed
  pixels when a tone control changes. The five focused color/property/tool/parity/golden tests
  passed 5/5 in 7.70 seconds. In a sequential five-iteration Debug diagnostic, simple-global
  960x640 samples measured 111-137 ms and 1920x1280 samples 442-466 ms; the spatial/detail samples
  measured 306-338 ms and 1193-1286 ms. This removes repeat compilation but does not meet or claim
  the release latency gate.
- The negative-contrast operator now uses an asymmetric, midtone-local compression response so
  bright highlights are not pulled down more strongly than midtones. `editor_preview_math_test`,
  `color_pipeline_test`, `color_pipeline_property_test`, `color_tool_invariant_test`, and
  `render_golden_test` pass without changing positive-contrast golden output.
- `application_capability_report_test`: passed and emitted a schema-checked JSON report. A copy was
  written to `/private/tmp/photo-editor-runtime-capabilities-v1.json`; it is diagnostic evidence,
  not a substitute for the unavailable physical-Metal or GUI monitor-profile gates.
- The full 95-test macOS Debug run reported 21 failures. Sixteen shell shards first failed on two
  raw `Button` declarations in `StudioWorkspace.qml`; those controls now use `ActionButton`.
  The source-contract shard now advances through corrected menu disabled-tooltip and scaled-row
  assertions to a broad typography rule triggered by twelve existing timeline ellipsis modes.
  The Studio pixel-inspection source contract was stale against the stricter full-resolution-backed
  v5/v6 implementation; its assertions now cover the current nearest-filtered native texture and
  the focused test passes. Remaining observed failures include missing external `pdfinfo` and
  unrelated timeline split/hydration assertions. These remain baseline failures and prevent the
  Phase 0 exit gate from passing.
- The subsequent 101-test macOS Debug baseline ran every color, preview, export, catalog, shell,
  and timeline target. Eighty tests passed and three physical-Metal tests reported explicit skips.
  The critical color path remained green under load: the complete preview controller passed in
  331 seconds, together with render migration, golden, parity, and export coverage. The catalog
  model passed in 28 seconds when isolated, classifying its aggregate failure as load-sensitive
  rather than a pixel-path regression. The carousel controller still stops at the external
  `pdfinfo`/`pdftoppm` proof dependency and therefore remains an environment gate.
- The same baseline exposed source-contract drift rather than seventeen independent UI
  regressions. Shared button contracts, current queue/carousel/timeline APIs, full-resolution
  outline ownership, and visible no-ellipsis rules now describe the shipped implementation.
  The focused desktop source-contract and outline-trace targets pass. The AI visual shard now
  instantiates the actual `AgentConversationView`, shows real before/after proof, and passes its
  batch-review capture; the unreferenced 1,700-line `legacyContentItem` walkthrough was physically
  deleted instead of retaining a parallel dormant UI.
- The 10,000-clip timeline proof now passes after replacing repeated projection-time scans and
  formatting with immutable per-projection lookup tables and fast paths. Projection now reuses
  track automation/audio state, does not restat already-classified offline waveform sources, and
  publishes title, caption, transition, keyframe, and silence roles only for clips that can own
  them instead of allocating hundreds of thousands of empty map entries. Two consecutive clean
  runs measured `load=413/426 ms`, `projection=1198/1252 ms`, `seek_p95=37/38 ms`,
  `selection_p95=12/11 ms`, `edit_p95=101/104 ms`, `undo_p95=28/27 ms`, and final bounded-edit
  `rss_growth=479/473 MiB`. These deterministic Debug results now have useful margin under the
  1500 ms projection ceiling; representative target-hardware plateau evidence remains required.
- On 2026-08-04 the complete macOS target graph built successfully through all 1,473 generated
  steps after adding the recipe normalization/codec sources omitted from the v13 adapter and
  pipeline-bridge test targets. A sequential 108-test desktop run then completed in 1,136.95
  seconds: every runnable color, RAW, recipe, catalog, preview, render-parity, golden, export,
  recovery, representative-workflow, and UI-shard test passed. The three physical-Metal parity
  cases reported explicit skips. The only stable external failure was the already-declared
  Carousel `pdfinfo` gate; the timeline split-alignment assertion passed on isolated rerun, so it
  remains a nondeterministic test-hardening item rather than accepted release evidence.
- That baseline exposed one stale public boundary rather than fourteen independent UI failures.
  Carousel already returned edits through the asset-bound adjustment-patch API, while the shared
  source contract still required the obsolete whole-recipe QML invokable. The whole-recipe entry
  point is now a private canonical helper, the public surface exposes only the validated
  asset-bound patch call, the focused source contract passes, and all fourteen dependent desktop
  UI shards pass sequentially in 318.10 seconds.
- Interactive slider edits no longer skip their visible bounded preview merely because a prior
  native full-resolution texture exists. The controller always publishes the immediate
  interactive request and independently schedules settled full resolution; the focused feedback
  proof passes in 11.28 seconds and the complete controller suite passes in 305.89 seconds. The
  layer-local LUT parity proof now checks the actual color-managed invariant: no unmasked leakage,
  a strong masked chromatic displacement, and preview/export agreement, rather than requiring an
  ACES display transform to emit an untransformed encoded `255,0,0` primary.
- The legacy unbounded desktop-shell monolith is no longer a default deterministic gate. A traced
  run reached the retouch Remove transition after 2,075.60 seconds and exposed an immediate query
  of not-yet-instantiated Repeater delegates; the test now waits for both dynamic controls and the
  focused brush/retouch proofs pass. A second traced run advanced beyond that former failure but
  exceeded the monolith's 3,000-second ceiling at 3,127.95 seconds. The executable remains
  available only as an explicit `PHOTO_EDITOR_RUN_MONOLITHIC_SMOKE=1` manual soak and for the two
  bounded CI visual-proof modes; an ordinary CTest invocation reports the named manual-soak skip
  in 0.87 seconds. Mask release and performance gates now require the focused brush, retouch HUD,
  selection geometry, tool-exit, and layer-hover shards instead. On the exercised Debug machine
  those five contracts passed individually; tool-exit required 294.94 seconds, so its bounded
  per-test/CI ceiling is 420 seconds rather than the false-negative 180/300-second limits.
- Repeated `ninja: warning: premature end of file; recovering` output was traced to both generated
  `.ninja_log` and `.ninja_deps` databases. The exact corrupt files were moved to recoverable
  timestamped backups under `/private/tmp`; after one dependency rebuild, the same target rebuilt
  with no warning and an immediate repeat reported `ninja: no work to do.` This closes the local
  incremental-build churn without deleting source or user work.

### Exit gate

- All deterministic baseline builds/tests pass from a clean revision.
- The baseline manifest names every skip and fallback.
- Preset output is either restored or explicitly accepted as a pending visual migration.
- No later phase begins from a revision that cannot be reproduced.

## Phase 1 — Create the canonical recipe and compiler

### Work

- [x] Introduce Qt-independent `EditRecipeV13` types in the engine.
- [x] Organize the recipe into typed sections:
  `input`, `scene`, `tone`, `curves`, `look`, `masks`, `layers`, `detail`, `geometry`, `output`, and
  `proof`.
- [x] Replace mode/profile strings in engine state with enums or validated asset identifiers.
- [x] Replace duplicated scalar ownership such as top-level and input-transform exposure,
  temperature, and tint with one authoritative field.
- [x] Use explicit optional/override state rather than treating numeric zero as “not provided.”
- [x] Move normalization, validation, equality, hashing, serialization semantics, and migration
  output out of `edit_recipe_state.h`.
- [x] Implement one `RecipeCompiler` that resolves:
  profile assets, decoder provenance, source geometry, masks, layer recipes, display target,
  proof target, export target, backend requirements, cache keys, and stage invalidation.
- [x] Make the compiler return structured errors; it must never silently manufacture a profile,
  replace an unavailable renderer, or fall back to a different characterization.
- [x] Add compiler determinism, round-trip, invalid-state, migration, cache-key, and property tests.
- [x] Add one adapter from desktop state during the compatibility release. No second compiler is
  permitted.

The first Phase 1 slice is directly covered by `edit_recipe_v13_test`: canonical serialization
round-trips exact scalar intent, signed zero normalizes to one canonical representation, invalid and
non-finite values return structured paths, cache identity changes with recipe state, compilation is
deterministic, logical color stages are ordered, and missing camera/display/proof assets fail closed
instead of selecting substitutes. The unused engine v3 candidate type, empty implementation, and
test-only migrator had no production reader or caller and are physically deleted. The much broader
desktop v12 state still adapts into v13 at the production boundary rather than writing v13
directly. Stage-local
chained cache identities now
prove that display/output changes preserve decode through geometry, exposure changes preserve
decode and camera characterization, and preview/export purpose changes alter publication without
rebuilding color stages. A deterministic 512-recipe property sweep validates round-trip, compilation,
and cache identity behavior. The desktop compatibility adapter maps the exactly representable v12
subset and rejects legacy renderer relabeling, dual exposure ownership, and remaining ambiguous
intent rather than manufacturing semantics. `edit_recipe_v13_adapter_test` proves the adapter
feeds the sole engine compiler, preserves global tone/look/detail/geometry/profile intent,
maps master/luma/R/G/B point curves, converts parametric curves to the exact canonical control
points used by the existing engine, and rejects malformed curve data. The full macOS application
target also builds with the adapter linked; this is compatibility infrastructure, not production
cutover. The initial mask placeholder has since been replaced by ordered typed mask sets and
sources with combine operation, parentage, blend mode, opacity, density, inversion, reusable output,
coordinate/sampling contract, source revision, immutable payload identity, and analytic geometry.
Layers reuse the same scene, tone, curve, look, detail, and geometry recipe sections as global
edits and identify non-adjustment content immutably. Layer-local hue/colorize, every mixer channel,
RGB channel mixing, black-and-white mixing, color replacement, skin protection, harmony, grading,
and creative LUT metadata now serialize canonically; LUT tables are referenced by derived
SHA-256 payload identities rather than embedded as mutable property bags. Compiler proof now fails
closed for unavailable mask, layer, or LUT assets. The desktop adapter preserves scalar
linear/radial masks, ordered analytic mask sets, shared mask-set adjustments, adjustment layers,
and bound raster/pixel assets; conflicting per-source adjustments and unbound payloads remain
explicit migration errors. Preview and export
now construct `ColorPipelineConfig` through the sole
`edit_recipe_pipeline_bridge.h` implementation. `edit_recipe_pipeline_bridge_test` proves
deterministic target resolution and source-contract rejection of the two deleted caller-local
implementations. The v13-only writable-recipe cutover remains open, so this does not pass the
Phase 1 exit gate. The duplicated global
`ColorAdjustmentState` aggregate has also been removed: both caller-specific wrappers now use
`compile_base_color_adjustment_state()` and only attach their target-specific mask/layer payloads.
Carousel now uses the same shared compiler and canonical renderer rather than its third incomplete
aggregate. The focused bridge contract rejects the former `to_pipeline_state()` entry point in all
three sources and requires Carousel to admit exactly one canonical plan before rendering. The desktop
v13 adapter now exposes one fail-closed `compile_desktop_render_plan()` boundary, and its focused
test proves two callers with identical context receive byte-identical serialized plans. Production
preview, export, and Carousel execution is now driven by admitted canonical plans. The canonical
schema and compiler identity
now also preserve the formerly rejected RAW controls and advanced tone, detail, geometry, output,
proof, and global/layer color state:
brightness, HDR local tone, filmic controls, levels, zones, dehaze, structured sharpening,
lens/perspective/upright/liquify/content-aware geometry, render intent, gamut policy, black-point
policy, ink-black simulation, demosaic/highlight/adaptation/camera-look selection, decode/detail/
cleanup controls, calibration, channel mixing, color grading, local creative color, and immutable
creative LUT references. `compile_desktop_render_plan_with_derived_assets()` derives stable
SHA-256-bound mask, layer, and LUT identities from the canonical desktop recipe and its focused test
proves repeat production-context compilation is byte-identical.
`RenderPlanStore::shared().admit()` is now the one production admission boundary used by preview,
photo export, catalog thumbnail, Carousel, agent proof, job-runner, and migration-assessment
clients. The former free `compile_canonical_render_plan_admission()` API and its header are
physically deleted. `RenderPlanStore` exclusively owns decoder-provenance resolution, recipe and
source admission validation, canonical compilation, plan/recipe identities, serialized-plan
publication, concurrent same-key build suppression, and a bounded 64-entry LRU. New ACES edits fail
closed at that boundary, expose plan/recipe identities and structured errors, and the
source-contract and architecture tests reject caller-local compilation or restoration of the old
API. Export jobs now persist the admitted plan and recipe cache keys in their
delivery metadata, and `export_job_controller_test` proves both a successful identified export and
fail-closed admission without a source identity. Scene-linear PFM/OpenEXR preparation also no longer
flattens through an 8-bit RGB image: alpha is flattened in RGBA32F, retaining scene-linear values
above 1.0. Large inline mask payloads are hashed once and removed from the structural adapter copy;
the immutable payload identity remains in the plan without repeatedly normalizing 10,000 brush
dabs. Preview, export, and Carousel now execute the admitted plan through typed executor
boundaries; Phase 1 remains open because v12 is still writable during compatibility and not every
application consumer is yet a graph variant. The former per-source mask override
ambiguity is now resolved explicitly: every flattened desktop mask source becomes its own
source-targeted v13 adjustment layer, and source `adjustmentOverrides` are merged into that layer
before normalization. V13 validation accepts declared mask-set, source, or reusable-output
identities as layer targets, and the adapter test proves two sources in one set retain different
exposure and contrast values without dropping or double-owning them.
Built-in proof spaces now receive stable immutable v13 identities such as `builtin:icc:srgb`, and
the derived compile context admits that exact identity. This fixes the former false
`proof.profile` rejection for a built-in sRGB soft proof while preserving asset-backed,
fail-closed behavior for custom ICC profiles. `edit_recipe_v13_adapter_test` proves both adaptation
and compilation of the built-in proof identity.
The first identity-bound canonical write API now exists in
`edit_recipe_v13_persistence.{h,cpp}`. It requires a complete `DesktopRecipeV13Context`, adapts,
normalizes, validates, and only then emits the engine v13 payload and canonical cache key. Missing
recipe/source/decoder identity and unsupported legacy intent return structured errors with no
partial payload. `edit_recipe_v13_persistence_test` decodes the emitted payload through the engine
reader, verifies recipe/source identity plus exposure/highlight/shadow/vibrance intent and cache
identity, and proves both failure paths. The same service now projects asset-free v13 input, scene,
tone, curves, look, detail, geometry, output, proof, and immutable profile identity back into
desktop edit state; projection followed by canonical rewrite is byte-identical. The
`RecipeAssetStore` resolver now externalizes and restores normalized masks, every layer kind, and
layer LUT payloads under verified SHA-256 identities. Reopen is accepted only when the resolved
desktop projection reproduces the byte-identical canonical v13 payload. Canonical sidecars use an
atomic v13 envelope plus adjacent immutable assets. Catalog writes resolve the exact RAW/raster
decoder provenance, prepare one canonical document, and atomically commit it beside a derived v12
compatibility projection; canonical reload verifies the cache key and assets and fails closed
instead of falling back to stale v12. The architecture guard prevents any other desktop source
from calling the engine serializer/cache-key writer directly and prevents restoration of a
legacy-only catalog helper command. Remaining Phase 1 work is Qt-independent ownership: the
desktop presentation state and adapter still depend on Qt even though the engine v13 document is
the rendering authority.

### Exit gate

- Preview and export tests consume the same compiled `RenderPlan`.
- The same recipe, source metadata, profile assets, and target produce a byte-identical serialized
  plan regardless of caller.
- Qt is not required to construct, validate, migrate, compile, or test a recipe.
- Direct production reads of desktop-only color fields are rejected by a source-contract test.

## Phase 2 — Make the render graph executable

The first engine-owned execution slice now exists in `render_executor.h/.cpp`. It consumes the
typed `RenderStageKind` sequence already emitted by `RecipeCompiler`, declares the pixel encoding,
color domain, exact color-space identity, transfer identity, range, alpha model, source area, and
resolution expected after every stage, derives
SHA-256 cache identities from stage-scoped source, version, backend, target/device dependencies,
resolution, source area, and prior intermediate, and owns bounded LRU intermediate reuse.
It rejects cancellation, stale generations, malformed requests, failed stages, and descriptor
violations before publication. `render_executor_test` proves the typed domain sequence, bounded
cache reuse, target-sensitive keys, cancellation, stale rejection, contract failure, and cache
clear. It now also proves cache-independent replay, exact logical-stage stopping, synchronous typed
surface snapshots for diagnostic dumps, and rejection of invalid replay boundaries. The five
engine-owned graph/contract/cache/diagnostic work boxes below are complete. Production
preview/export now move real Decode through Publish stages through this executor, excluding only
compiler-elided nodes. Preview's file-to-linear Decode callback is deferred until the executor has
checked cancellation and staleness, participates in the Decode cache, and is omitted on a cache
hit. Export's Decode materialization is also scheduled, but RAW metadata/plan preflight still
uses a metadata-only probe. Its full file read, decoded-boundary comparison, crop, linearization,
and source-pixel-dependent layer-mask compilation now occur synchronously inside the executor's
Decode callback. Export now submits Decode through Publish in one executor call and persists an
`executorSubmissionCount` of one. Preview now submits Decode through Geometry in one call, but
still submits its output/publication suffix separately so its adjusted-working cache can remain
an explicit intermediate. Canonical preview and export execute Proof into a
declared linear carrier and custom DeviceTransform into the encoded ICC device domain before
Publish. Variant consolidation and fusion admission/reporting are now complete, so the Phase 2
repository exit gates are closed.

The descriptor no longer relies on generic `WorkingScene`, `RenderingTarget`, or `Device` enums as
a substitute for a color-space contract. Camera-native, working, rendering-target, proof, and
device color-space identities plus the device transfer identity are required request fields,
propagate into every surface descriptor, participate in stage cache keys, and are checked when an
executor resumes from an intermediate. `render_executor_test` locks the camera-native, Linear
ProPhoto, Display P3, and sRGB-transfer fixture sequence; missing identities invalidate the request
rather than creating an ambiguously tagged cache entry.

Successful backend fallback is now first-class stage provenance rather than a generic CPU label:
`RenderCompletedStage` records the backend and exact fallback reason at the logical stage that
left the preferred backend. The executor test proves a camera-characterization fallback remains
visible in the completed-stage manifest. Every produced or cached `RenderSurface` must also contain
finite RGBA channels; signed and over-range finite scene values remain valid, while NaN/Inf fails as
a contract violation before cache insertion or publication.

Stage cache identity is dependency-scoped rather than salted by the whole render plan. Decode,
characterization, scene, tone, look, layer, detail, and geometry intermediates survive a
display-target change; photographic output survives a device-ICC reassignment; source-area and
resolution changes still produce distinct typed intermediates. The cache-key schema was bumped to
`render-stage:v2-dependency-scoped`, and `render_executor_test` locks target-only, device-only,
whole-plan, and source-area invalidation behavior.

The former string-named descriptive graph and its self-referential smoke test are physically
deleted. Its one production responsibility—dirty-region-to-pixel tile planning, padded edge tiles,
and visible/adjacent/background priority—is isolated in the typed
`render_tile_planner.h/.cpp` module. `render_tile_planner_test` preserves those contracts, and the
complete controller regression passed in 328.07 seconds in the consolidated final-source run after
production switched to the focused
module. Dirty-domain normalization, the nineteen-stage invalidation order, render-reason mapping,
and downstream-suffix selection now live in the Qt-independent typed
`render_invalidation_policy.h/.cpp` module rather than controller string tables.
`render_invalidation_policy_test` locks aliases and exact raw/display/full suffixes, and the
complete controller regression passed independently in 328.45 seconds immediately after the
cutover.

The pixel implementation now has a truthful Detail/Geometry seam: lens-vignette gain is owned by
CameraCharacterization, after RAW normalization and highlight analysis but before the camera
matrix, while distortion, chromatic-aberration resampling, perspective, transform, and liquify run
after Detail as the compiler declares. Camera cache payloads include the vignette setting;
NormalizeRaw cache payloads do not. The cached NormalizeRaw surface retains its highlight-analysis
sidecar, so invalidating CameraCharacterization does not force a second RAW highlight scan.
Separate telemetry fields record camera characterization and geometry time.

The pre-output split was audited at the actual operator reads rather than inferred from names.
The immutable edge source is consumed while constructing local-mask weights earlier in the slow
tone/local path; post-Layers texture, clarity, dehaze, sharpening, and noise reduction do not read
it. An intermediate auxiliary-surface design added during investigation was therefore removed
before production use: carrying a full-frame sidecar across Layers would have increased memory
without representing a real Detail dependency.

The color engine exposes separate real pixel boundaries:
`apply_color_adjustments_through_look_base()` renders a layer-free base through Look,
`apply_color_layers_stage()` evaluates local masks, retouch, and photo-layer composition against
that Look surface, and `apply_color_adjustments_through_layers()` composes those two operations.
It stops before texture/clarity/dehaze and fine Detail. The property suite proves the Look base is
bit-exact with an explicitly layer-free recipe, Layers makes real pixel changes, split Look plus
Layers is bit-exact with the canonical through-Layers boundary, and Layers records zero spatial
and fine-Detail time. The former spatial closure and inline fine-detail block are deleted:
`apply_color_detail_stage()` owns texture, clarity, dehaze, sharpening, luminance-noise reduction,
and chroma-noise reduction. A split-execution property applies that callable stage to the
post-Layers pixels and requires bit-exact equality with the canonical through-Detail result.
Invalid dimensions, cancellation, finite-output validation, CPU/Metal selection, and additive
stage telemetry remain explicit at the callable boundary.

Those seams are now callable and production-bearing rather than merely comment boundaries:
`apply_color_geometry_stage()`, `apply_color_photographic_output_stage()`, and
`apply_color_compatibility_output_stage()` validate dimensions, honor cancellation, and own their
stage telemetry. `apply_color_adjustments_through_detail()` exposes the preceding typed surface
without running either suffix. `RenderExecutor` can resume only from a finite,
identity-bearing intermediate whose descriptor exactly matches the preceding plan stage; a
contract test rejects a mismatched resumed surface. Admitted preview and export plans now use only
`execute_color_plan()`. Its Decode callback schedules real file-to-linear work and the same
submission executes NormalizeRaw when present, CameraCharacterization, SceneAdjustments, Tone,
optional Curves, Look, optional Layers, Detail, Geometry, PhotographicOutput, optional Proof,
optional DeviceTransform, and Publish according to the admitted vector. It accepts both canonical
shapes, `Look -> Detail` and `Look -> Layers -> Detail`, and it can resume from an exact typed
Geometry predecessor for output-only preview changes. Publish applies the declared transfer once,
clamps alpha, validates finite output, and preserves the Proof gamut sidecar through an encoded
DeviceTransform. All former section execution helpers—prefix, Layers, Detail, Geometry,
PhotographicOutput, Proof, DeviceTransform, and Publish—have no callers and are physically deleted
after their cache, cancellation, transformation, and sidecar assertions were folded into the
unified-plan test. Preview
permits cache reuse only when its adjusted-working identity includes the decoded source
fingerprint; export bypasses this cache until it has an equally strong content-revision identity.
The property suite requires the complete wrapper to match explicit NormalizeRaw,
CameraCharacterization, SceneAdjustments, Tone, Curves, Look, Layers, Detail, Geometry, and output
execution. Fresh supported preview and export results preserve every executed prefix stage, Layers
when present, then Detail, Geometry, PhotographicOutput, and Publish in their completed-stage
manifest; adjusted-scene cache reuse reports only stages actually traversed.
Device-encoded built-in pixels are tagged with their
actual non-linear Qt profile before format packing, rather than being mislabeled as linear and
encoded again. An over-range chroma fixture requires explicit PhotographicOutput plus Publish to
match the former Qt encoding route within one 8-bit code value. The stage callback receives a
move-owned intermediate and the executor moves its resumed surface into execution, so this suffix
does not repeatedly copy the full pixel allocation. `render_executor_test` proves the allocation
address survives request, callback, and output ownership transfer, proves real Layers, Detail, and
Publish pixel work, and proves Layers, Detail, and Geometry cache reuse independently.

On the exact supported-suffix source, the engine property and executor binaries pass, the rendering
migration proof passes, export persists the stages present in its admitted suffix in execution
order, and the complete preview-controller regression passes with direct completed-stage,
publication-parity, and fallback-profile assertions. The preceding consolidated
source also passed the broader color-science set. After rebuilding the source-contract binary, the
exact Detail-cutover source passed the focused color/RAW/parity/golden set 6/6 in 7.01 seconds and
the broader color-science set 20/20 in 13.85 seconds. After the Publish cutover, all twenty binaries
were rebuilt against the final engine and the same set passed 20/20 in 18.39 seconds. A fresh
headless rebuild of the final Detail/Publish executor contracts passed 2/2 in 0.91 seconds. The
complete preview-controller regression also passed after the final telemetry-only rebuild, so its
manifest and one-code-value publication parity assertions are from the exact recorded source.
After the truthful Layers and tone cutover, the focused
`color_pipeline_property_test`, `editor_preview_math_test`, `color_pipeline_test`,
`color_tool_invariant_test`, `render_parity_test`, `render_golden_test`,
`render_executor_test`, and `export_job_controller_test` set passed 8/8. The isolated complete
`editor_preview_controller_test` passed after rebuilding the exact source. Expected behavior
goldens were refreshed only after math, monotonicity, extended-range, blend-mode, preset behavior,
and stage-split tests passed. The obsolete monolithic retouch/layer/detail/output tail, the fused
input/tone/curves/Look prefix, and the unused pre-tone composite-mask-weight builder are physically
deleted. The Tone/Curves extraction intentionally changed the two parametric-curve goldens to
`14864096399532332633` and `5704213030300975520` after the behavior properties passed. Moving user
exposure to SceneAdjustments after camera characterization and making dehaze exclusively a Detail
operator changed the inspected Night Recovery and Landscape Pop fixture hashes to
`10976563582655933464` and `18209443385856626905`; their shadow/highlight and foliage/sky behavior
checks remained green. Lens-vignette stage ownership also changed the mixed optics coalescing
contract: a burst containing photometric vignette now invalidates from `input_color_transform`
through Geometry, while pure geometric edits still begin at Geometry. The exact rebuilt
`editor_preview_controller_test` passed in 326.77 seconds with that distinction. The final focused
engine, recipe, bridge, parity, golden, executor, migration, and export set then passed 13/13 in
22.85 seconds. Canonical soft-proof preview now compiles an explicit linear carrier with the
monitor primaries, executes `PhotographicOutput -> Proof -> Publish`, and preserves the established
Display P3 pixel and profile assertions; the exact controller suite passed in 328.48 seconds.
Diagnostic gamut-mask mode is now an optional, dimension-checked Proof-surface sidecar. Diagnostic
requests bypass stage caches, and DeviceTransform/Publish preserve the mask without changing visible
pixels. `render_executor_test` passed in 0.53 seconds and the exact controller suite passed in
332.04 seconds with a non-empty proof-derived mask. Custom monitor preview now uses a Linear
Rec.2020 interchange carrier, executes the combined
`PhotographicOutput -> Proof -> DeviceTransform -> Publish` plan, packs the already encoded device
pixels without a Qt reconversion, and retains the immutable ICC identity at the final QImage
handoff. The exact controller suite passed in 330.38 seconds. Three
physical-device members—Metal ACES
output-LUT parity, Metal OCIO parity, and Metal ICC-LUT parity—were explicitly skipped because this
execution context exposes no system Metal device; they are not counted as hardware proof.
`render_parity_test` now
compiles canonical custom-device and soft-proof export plans, requires DeviceTransform/Proof plus
Publish in their manifests, and keeps the custom ICC result matched to the direct
ACES-plus-Little-CMS oracle. That oracle exposed and now prevents a real double-encoding defect:
Little CMS had already produced destination-encoded device values, but Publish applied the display
transfer function again. Publish is now an explicit identity-and-finite-validation boundary after
a compiled DeviceTransform, reports `device-encoded-identity` as its backend, and retains ordinary
single display encoding on paths without a device transform. The executor regression covers full
and resumed publication plans; the direct custom-ICC pixel proof, export artifact test, and complete
preview-controller suite all pass.

After the preview Proof/DeviceTransform cutover, the exact focused engine, recipe, bridge, parity,
golden, executor, migration, and export set passed 13/13 in 20.33 seconds.
After adding the cache-accounted diagnostic Proof sidecar, the same exact set passed 13/13 in
22.42 seconds.
After the catalog and preset Thumbnail-purpose admissions, implicit cache-key fallback cleanup,
and preset-state integrity repair, the exact 13-test set passed again in 15.06 seconds; the
complete controller suite passed separately in 332.31 seconds.
After adding typed Scope-purpose domains, the exact current 13-test engine, recipe, bridge, parity,
golden, executor, migration, and export set passed 13/13 in 18.77 seconds. The focused
`PHOTO_EDITOR_TEST_SCOPE_DOMAIN_ONLY=1 editor_preview_controller_test` path passed, the complete
controller suite then passed in an isolated rerun, and `photo_editor_app` rebuilt through all 308
incremental C++/QML steps. A preceding complete-controller run reached the existing repeated
undo/redo stress deadline with a full render still active after all new scope checks had passed;
the isolated pass is evidence of correctness, not a claim that the timing-sensitive stress gate is
stable under load.
After scheduling Decode as stage zero and deferring preview file-to-linear decode into its executor
callback, the exact 13-test set passed 13/13 in 19.17 seconds. The executor test proves that
cancellation prevents decoder invocation, a repeated plan reuses cached Decode without invoking
the callback again, and a downstream camera-stage identity change preserves Decode and
NormalizeRaw. The export integration now requires a persisted Decode-through-Publish manifest;
the focused five-test executor/recipe/parity/golden/export set passed 5/5 in 16.28 seconds, and the
focused scope/controller path passed after requiring Decode as its first completed stage. After
deleting the old already-materialized prefix wrapper, the desktop app target rebuilt and the
current headless suite passed 31/31 in 11.85 seconds, with the two physical Metal parity cases
explicitly skipped for lack of a system device. The complete controller suite then passed after
updating its settled-preview manifest to begin at Decode and preserving regional overlay decode as
an executor-scheduled linearization of the already-decoded region.
Export plan admission now occurs after only a size query and, for RAW, a metadata-only decoder
probe. Full pixel decode now occurs inside the admitted executor Decode callback, fails closed if
its RAW boundary identity differs from the probe, and records both
`raw-metadata-only-before-plan`/`size-only-before-plan` and
`file-read-and-linearize-in-executor-decode` in persisted job evidence.
Export now uses `execute_color_plan()` once for the entire admitted Decode-through-Publish vector
and persists `executorSubmissionCount=1`; `render_executor_test` exercises the same one-submission
contract directly. `export_job_controller_test` passed with the new contract.
The preview adjustment half now uses the same entry point for one Decode-through-Geometry
submission instead of four application-coordinated prefix/layers/detail/geometry calls. Its
focused scope path passed, the full `editor_preview_controller_test` passed in 269 seconds, and the
exact 13-test engine/recipe/bridge/RAW/migration/parity/golden/export set passed 13/13 in 18.98
seconds after this consolidation. `photo_editor_app` also rebuilt successfully. The first
post-consolidation headless run exposed a source-contract assertion that still required the deleted
section helper name; the contract was updated to require `execute_color_plan` and persisted
submission-count telemetry instead. The rerun passed 31/31 in 6.46 seconds, with the two
physical-device Metal parity cases explicitly skipped because no system device was available.

Gate 2 now has a direct boundary test rather than relying only on two calls to a shared image
helper. `render_plan_store_test` admits the same RAW-bound recipe separately for
`SettledPreview` and `Export`, requires one recipe cache identity and distinct publication-plan
identities, and compares the exact decoder/profile-bound stage identities through Geometry. It
then executes both admitted plans from the same signed and extended scene-linear RAW surface and
requires identical pre-output descriptors, float channels, alpha, stage order, and backend
selection. The focused Release test passed in `0.31` second. `edit_recipe_v13_adapter_test` and
the high-risk encoded `render_parity_test` then passed together in `1.95` seconds. This closes the
Gate 2 code contract; physical licensed-RAW comparisons remain in Gate 4.

The remaining executor gaps were audited at operator level:

- Export now compiles its plan from a metadata/size-only probe. The full file read, fail-closed
  comparison with the admitted RAW boundary, crop, working-float linearization, and
  source-pixel-dependent selection-mask compilation happen inside the executor Decode callback.
  This closes physical pixel-decode ownership; the lightweight metadata/size probe remains an
  admission service outside execution by design.
- Preview still invokes two contiguous executor sections through focused helpers rather than one
  call from Decode through Publish. Export now makes one full-plan submission. Preview makes one
  Decode-through-Geometry submission and one typed PhotographicOutput-through-Publish submission
  around its explicit adjusted-working cache. `execute_color_plan()` now also
  accepts an explicitly typed predecessor surface and start stage; `render_executor_test` proves
  that an adjusted Geometry surface resumes PhotographicOutput through Publish in one submission,
  and the production preview caller now uses that path with Proof gamut-mask and scope sidecars
  carried through the same result. The focused scope path and complete controller suite passed
  after this cutover. The dormant four-helper production publication body is physically deleted;
  the old section APIs and implementations are now deleted as well. Their stage-specific cache,
  cancellation, transformation, gamut-sidecar, encoded-device, and publish assertions were folded
  into the unified-plan test. After deletion, `photo_editor_app` rebuilt, the exact 13-test
  color/recipe/RAW/migration/parity/export set passed 13/13 in 21.05 seconds, and headless passed
  31/31 in 12.11 seconds with only the two physical-device Metal parity cases skipped.
- Preview soft proof, proof-gamut diagnostics, custom monitor publication, and export all execute
  through the same canonical Proof/DeviceTransform contracts. Proof writes a proof-simulated
  linear carrier and optional gamut-mask sidecar, DeviceTransform consumes that carrier and writes
  encoded device values while preserving the sidecar, and Publish recognizes the encoded
  predecessor so it cannot apply the transfer function twice.

### Work

- [x] Replace the descriptive-only graph with typed stage nodes and typed stage inputs/outputs.
- [x] Move stage scheduling, cache policy, invalidation, cancellation, and diagnostics into a
  `RenderExecutor`.
- [x] Give every intermediate a declared pixel encoding, color space, range, alpha model, source
  area, and resolution.
- [x] Derive cache keys from source identity, compiled plan, stage version, backend version, profile
  bytes, target identity, resolution, and source area.
- [x] Make dirty-region and tile execution policy engine-owned rather than controller-local.
  `render_invalidation_policy` owns typed dirty-stage ordering and suffix invalidation;
  `render_tile_planner` owns region mapping, padding, tile coverage, and priority. The controller
  converts those typed results into its compatibility request format while remaining consumers
  move to canonical graph variants.
- [x] Make preview, full-resolution preview, export, thumbnail, scopes, and carousel graph variants,
  not independent render implementations. Preview/full-resolution, export/batch, Carousel, catalog
  photo thumbnails, preset thumbnails, and adjustment-layer thumbnails now admit Thumbnail- or
  surface-purpose canonical plans. Scope-purpose plans now admit and execute five typed stopping
  domains. Export owns one uninterrupted full-plan execution, preview uses an explicit cached
  Geometry predecessor between its adjustment and publication variants, and export pixel decode is
  executor-owned after metadata-only admission. No image-producing surface retains an independent
  color mapper or renderer.
- [x] Add truthful Look and Layers stopping points, execute the real Layers node through the
  executor, and prove Layers does not execute spatial/fine Detail.
- [x] Extract NormalizeRaw, CameraCharacterization, SceneAdjustments, Tone, Curves, and Look into
  real pixel stages; execute and cache each declared node in preview/export; delete the fused
  compatibility prefix.
- [x] Execute canonical export Proof and custom DeviceTransform as separate Little CMS nodes, with
  a declared linear proof carrier and no double encoding in Publish.
- [x] Execute ordinary preview Proof and custom monitor DeviceTransform as canonical Little CMS
  nodes; pack encoded custom-device pixels directly and preserve the exact ICC identity through
  publication.
- [x] Carry diagnostic proof-gamut output as a bounded Proof-surface sidecar through
  DeviceTransform and Publish, with cache-safe diagnostic execution and visible overlay proof.
- [x] Extract the pixel-only Detail body, execute it from the post-Layers surface, and delete the
  monolithic closure. The earlier immutable mask-weight source must remain owned and consumed by
  the local/tone stage where it is actually read.
- [x] Permit backend fusion only after the unfused CPU plan passes and fusion reports the logical
  stages covered. `RenderFusionEvidence` is validated at the executor boundary: it requires a
  passed, identity-bearing unfused reference and a unique contiguous logical-stage manifest that
  includes the current stage. Invalid fusion fails as a contract violation; admitted fused stages
  persist the manifest in `RenderCompletedStage` and bypass intermediate caching so diagnostics
  cannot silently become an unlabelled cache hit. `render_executor_test` proves rejection and
  admission across a two-stage fused backend declaration. The desktop app, focused export and
  scope paths rebuilt/passed after the contract landed; headless passed 31/31 in 10.31 seconds,
  with only the two physical-device Metal parity cases skipped.
- [x] Add stage-by-stage replay and dump support for diagnostics without exposing it in normal UI.
- [x] Add graph tests for identical plan order, invalidation, cache reuse, cancellation, stale
  rejection, tile seams, source-area maps, and backend fallback. `edit_recipe_v13_test`,
  `render_invalidation_policy_test`, `render_executor_test`, and `render_tile_planner_test` own
  these contracts; tile output rectangles now prove every output pixel is covered exactly once
  with no seam overlap. The exact four-test set passed in 1.08 seconds.

### Exit gate

- Production preview and export instantiate and execute the engine graph.
- No application controller manually sequences input, tone, masks, layers, detail, and output.
- Every published image has a plan identity and completed-stage manifest.
- The old descriptive graph implementation has no remaining caller and is deleted.

## Phase 3 — Correct preview, monitor, proof, and export targeting

### Work

- [x] Separate `DisplayRenderingTarget` from `DeviceIccTransform`. V13 and `RenderPlan` store the
  photographic target and optional immutable device ICC identity independently; the compiler emits
  distinct PhotographicOutput and DeviceTransform nodes. Recipe tests prove target-only changes
  invalidate PhotographicOutput without rebuilding scene work, while device transforms retain
  their own stage identity.
- [x] Build the preview photographic renderer for the verified active display target, including
  primaries/gamut, transfer function, peak, black, reference white, and surround.
  `SystemMonitorProfileSnapshot` now carries explicit peak, black, reference-white, and surround
  values with a separate target identity, provenance, validity, and verification flag. A target
  change invalidates display pixels even when ICC bytes are unchanged; transient same-display
  discovery failures retain the last verified target. The preview renderer consumes these values
  instead of silently reconstructing generic defaults, and diagnostics v23 exposes all values and
  whether they are measured/verified. Current platform discovery still labels its 100-nit SDR
  values as `icc-verified-color-reference-luminance-defaults`, not measured hardware truth, so this
  platform route remains explicitly unverified unless measured data exists. A validated calibration
  contract and persisted application-preference route accept calibration only for the exact
  physical display identity, require finite and ordered peak/black/reference-white/surround values
  plus a calibration identity, change the target cache identity, and mark provenance
  `user-calibration`. Invalid or cross-display values are ignored, and clearing calibration removes
  the entire preference group. Preferences now has a dedicated Color page that exposes the active
  display and ICC identity, distinguishes `Calibrated target` from `Reference defaults — not
  measured`, validates all values with units and bounds, applies them immediately through the
  controller refresh boundary, and clears them without retaining stale values. Stable QML
  properties expose the physical display identity and complete target map; a target-identity change
  invalidates only the display suffix even when ICC bytes are unchanged. The focused
  `system_monitor_profile_state_test`, `app_shell_controller_test`, controller path, QML cache
  compilation, and preferences source contract own this behavior.
- [x] Compile custom monitor ICC transforms from immutable profile bytes and cache them by display
  identity plus payload hash. `system_monitor_profile_asset_bridge.h` derives a stable SHA-256
  asset and the focused bridge test proves repeat resolution is identity-stable.
- [x] Apply the monitor/device transform exactly once, after photographic rendering and proof
  simulation. Manual monitor targets replace the intermediate preview target; discovered ICC
  payloads become the direct preview output asset, and the final handoff no-ops when already tagged
  with that same profile. The canonical device path now also preserves that identity through its
  final QImage wrapper: `publish_working_patch` always forwards the typed output-transform metadata,
  including an `encoded_device_profile` when the executor has already produced destination-encoded
  pixels. The former null-`color_pipeline` branch silently dropped that profile and retagged the
  pixels through the generic publication path. The complete controller suite now proves ordered
  `PhotographicOutput -> Proof -> DeviceTransform -> Publish` execution and exact equality with the
  imported ICC payload rather than accepting a visually plausible but incorrectly tagged image.
- [x] Remove the default route that gamut-maps into sRGB before converting to a wide-gamut monitor.
- [x] Make sRGB preview an explicit soft-proof/output simulation choice. Changing the export
  color space no longer silently rewrites the preview target. sRGB is selected deliberately as
  either the Soft Proof profile, the Match Export output, or the Reference Display Target; Monitor
  remains bound to the active display.
- [x] Define preview modes:
  `Monitor`, `Match Export`, `Soft Proof`, and `Reference Display Target`. The mode is normalized,
  serialized in `outputTransform.previewMode`, migrated as `Monitor` for legacy recipes, included
  in equality/cache identity, exposed through stable controller properties, and selectable in the
  Edit toolbar. Match Export derives the proof target from `outputColorSpace`; Soft Proof preserves
  its independent proof profile; Reference Display Target owns an independent named target; and
  Monitor uses the active measured/reference display target. System-monitor ICC bytes are injected
  into the render request before canonical-plan admission so DeviceTransform is an actual typed
  stage rather than a post-plan afterthought. The complete controller render test also caught and
  fixed a publication race where the full preview could erase the interactive proof-gamut mask;
  the latest exact proof mask now survives the matching full publication and is cleared on asset,
  proof-target, output-simulation, or mode changes.
- [x] Make output-target changes invalidate only the output/device stages. The engine-owned
  invalidation policy maps output-profile changes to the `display_transform` suffix, while
  dependency-scoped executor keys preserve decode through geometry across rendering-target changes
  and preserve photographic output across device-profile changes. The controller regression proves
  that a changed system monitor profile submits `display_transform` rather than a full-pipeline
  request.
- [x] Keep scopes selectable between scene-linear, working, pre-output, proof, and final-display
  domains; label the domain visibly. `RenderScopeDomain` is serialized into the v2 canonical plan,
  with deterministic stops at CameraCharacterization, Geometry, PhotographicOutput, Proof, and
  Publish. The scope panel exposes all five labels, disables Proof until soft proof is enabled,
  and returns to Final Display when proofing is disabled. Non-display scopes bypass final rendered
  caches, Scene Linear bypasses the post-characterization adjusted cache, and
  `scope_surface_stage` records the surface actually analyzed. Focused controller proof covers
  edited and unedited recipes, all five domains, proof gating, plan identity, and non-null scope
  images.
- [x] Add P3/wide-gamut patches that prove the monitor route preserves colors outside sRGB when the
  display target supports them. The controller regression covers direct Display P3 rendering,
  ACES-mapped channel separation, explicit sRGB proof, and monitor-profile tagging.
- [x] Add monitor hot-plug, profile reassignment, multiple-display, stale-profile, and transient
  discovery-failure tests. The controller refreshes on screen add/remove, primary-screen change,
  focus-window change, and focused-window `screenChanged`, while the state contract proves
  same-display transient retention, repeated failure observability, recovery without pixel churn,
  same-display ICC reassignment, A-to-B-to-A switching, no-display hot-unplug failure closed,
  replacement-display generation, verified target retention, and strict prevention of another
  display's stale profile or calibration. `system_monitor_profile_state_test` passes these
  transitions with monotonic generations, and the complete controller suite proves a system profile
  change invalidates only `display_transform`.
- [x] Make export readback verify encoded pixels, embedded profile, transfer function, bit depth,
  metadata, alpha, and display/mastering fields. Successful rendered exports now require
  `export-artifact-verification:v2`: bounded decoded-pixel comparison, dimensions and component
  depth, exact embedded color-space/transfer identity, orientation, alpha and component
  association, and format-appropriate metadata. PNG and JPEG use their portable encoded
  description field; the legacy Qt TIFF text path was removed because it silently dropped every
  tag, and a small libtiff adapter now writes and reads Software and ImageDescription in the TIFF
  directory. PFM readback validates its bottom-up little-endian float payload and linear scale.
  OpenEXR readback validates half/float pixels, linear transfer, chromaticities, rendering model,
  transform provenance, alpha, and non-finite samples. HDR AVIF readback validates bounded decoded
  PQ/HLG pixels plus 10-bit Rec.2020 NCLX, full range, MaxCLL, MaxFALL, mastering peak, and
  mastering black. Failed verification removes the delivery artifact instead of reporting a false
  success. `export_job_controller_test` passes and asserts the v2 contract for PNG 8/16, TIFF
  8/16, JPEG 8, PFM 32-float, OpenEXR half/float, and Rec.2100 PQ/HLG AVIF.

### Exit gate

- A wide-gamut patch remains outside sRGB through a wide-gamut preview and is clipped/compressed only
  when sRGB proof is selected.
- Preview and export match within the declared perceptual threshold when they target the same output.
- Moving the window between verified displays rebuilds only the device/display plan and produces no
  stale-profile frame.
- The old post-sRGB monitor conversion route is deleted.

Preview publication now keeps the adjusted-image cache scene-linear. The former controller path
applied ACES/output gamut mapping before inserting that cache and then performed an ICC/display
conversion again during publication; export had the same pre-publication mapping duplication. Both
routes now execute photographic output exactly once at the shared publication boundary. Built-in
targets use the final ACES output LUT for preview/export parity. Its target-linear result is now
packed directly; the former target-to-working matrix followed by Qt's working-to-target conversion
was a redundant round trip and produced a measured 64-code preview/export disagreement in a
16-bit crop fixture during cutover. The direct path restored exact integration parity. Direct
target-linear Qt publication is admitted only for sRGB/Rec.709, Display P3, Rec.2020, and
ProPhoto. Qt cannot represent target-linear Adobe RGB without synthesizing an invalid profile on
the supported build, so Adobe RGB deliberately stays on the exact shared output-transform route
and is tagged only after conversion; it is never mislabeled as Linear ProPhoto. A focused parity
assertion locks this admission boundary, and the controller integration test requires the
published Adobe image to carry Adobe primaries while reporting no executor output stage it did
not run.
Custom ICC
and soft-proof targets use the exact ACES-to-Rec.2020 plus Little CMS chain, and scene-linear
output bypasses display mapping entirely so values above 1.0 survive. `render_parity_test` covers
normal preview/export, masked/layered edits, custom ICC, proof masks, and over-range scene-linear
output. Reset
Adjustments also no longer clears working/input/output profiles, renderer, monitor/proof targets,
RAW characterization, gamut policy, or black-point policy; those are color-management settings,
not image adjustments. The controller regression additionally proves that enabling diagnostics
during an sRGB soft proof compiles a canonical proof request and publishes its exact proof-derived
gamut mask; it no longer substitutes the display-gamut heuristic or drops a mask-only native
texture result.

## Phase 4 — Establish one settled RAW pipeline

### Work

- [x] Make LibRaw camera-linear output the cross-platform contract for settled preview and export.
  `raw_decode_requests_camera_linear_backend` now selects camera-linear decode for every editable
  RAW with its input transform enabled, independent of whether the selected profile is Embedded,
  Camera Native, DCP, or ICC. The generated-DNG regression proves the shipping Embedded/default
  route is untagged float camera RGB, requires the engine input transform, carries a valid
  camera-to-XYZ D50 matrix through both decode and metadata-only reads, and no longer reports
  decoder-applied linear sRGB. `color_pipeline_test` additionally proves the matrix executes,
  decoder-owned white balance is not applied twice, and creative temperature still changes the
  characterized working-space result. `export_job_controller_test` proves canonical RAW admission
  rejects a missing characterization and admits the same request after an explicit matrix and
  reference white are supplied.
- [x] Use bounded half-size or reduced-resolution LibRaw decode for explicit draft interaction while
  retaining color ownership. LibRaw bounds decode by `max_edge`, uses half-size only when useful,
  and changes demosaic quality only for explicit Preview/Draft requests; Balanced retains final
  demosaic semantics. The generated-DNG decoder test proves a 32-pixel draft keeps the same
  camera/profile identity, white-balance and characterization ownership, float camera-linear
  contract, and backend provenance as the full decode.
- [x] Remove the unused embedded/native RAW preview decoder API rather than allowing it to be
  mistaken for settled edited output. No production or test caller used
  `decode_raw_preview_image`; all editable draft and settled RAW pixels now enter through the
  LibRaw linear APIs, while catalog thumbnails remain a separately identified fallback source.
- [x] Remove Core Image `CIRAWFilter` from settled/final color rendering. The macOS public decode
  and metadata routes now use the same LibRaw camera-linear path as other platforms for every
  editable RAW. The focused decoder test requires the shipping backend provenance to begin with
  `LibRaw camera-linear`; the direct display helper deliberately requests the separately labelled
  characterized-linear conversion because it does not execute the edit engine.
- [x] Remove the `CIRAWFilter` RAW renderer entirely rather than preserving a second color
  authority. No `CIRAWFilter` construction, draft-mode key, filter configuration, or render branch
  remains in `raw_image_decoder.mm`; Core Image is retained only for raster/native-image platform
  conversion helpers. LibRaw draft performance and real-camera coverage remain separate open gates.
- [x] Keep native platform APIs only for profile discovery, raster I/O, and independently useful
  oracles at platform edges. The macOS decoder uses ImageIO/Core Image only to read TIFF/native
  raster pixels into an explicitly tagged linear buffer; every RAW metadata and pixel request
  delegates to the same LibRaw camera-linear/characterized-linear boundary used cross-platform.
  ColorSync remains confined to physical-display ICC discovery, and Vision remains an optional
  segmentation oracle whose output is a mask rather than rendered color. The generated-DNG decoder
  regression rejects any shipping RAW provenance that does not begin with `LibRaw camera-linear`,
  while the controller regression exercises native linear TIFF raster I/O independently.
- [x] Define exactly where black subtraction, white normalization, demosaic, white balance,
  highlight reconstruction, lens shading, camera characterization, baseline exposure, and source
  gamut conversion occur. LibRaw owns sensor black subtraction, white normalization, demosaic,
  and source white balance; the engine owns opposed-channel highlight reconstruction,
  camera-to-working characterization, DNG baseline exposure, working-space gamut preservation,
  and matched-profile lens geometry/vignette correction.
- [x] Ensure each responsibility is owned once and recorded in decoded metadata.
  The LibRaw boundary now explicitly records black subtraction, white normalization, demosaic,
  white balance, highlight reconstruction, lens correction, baseline exposure, camera transform,
  and decoder rendering ownership. Current truth is: LibRaw owns black/white normalization,
  demosaic, and source white balance; the engine owns highlight reconstruction, baseline exposure,
  and camera characterization; a matched camera/lens profile is materialized as engine geometry
  corrections rather than being attributed to the decoder. The UI and agent patch schema no
  longer expose the no-op `Raw Boost`, `Shadow Boost`, `Raw Detail`, `Chroma NR`, or `Moire
  Reduction` fields; their controller write properties/setters are deleted, and the v12-to-v13
  adapter rejects non-neutral values instead of relabeling them as executable intent. The no-op
  input `Gamut Mapping` toggle is likewise removed from UI, controller writes, and agent writes,
  defaults off, and fails v13 adaptation when set. Compatibility fields remain read-only migration
  input until Phase 11 deletes the v12 reader. These retired values and engine-only lens/color
  settings were also removed from `RawDecodeSettings` and the decoded-image cache key, so changing
  them can no longer invalidate or duplicate an identical LibRaw decode. They are also physically
  absent from the writable `InputRecipeV13` schema and compiler stage payloads. Canonical
  serialization now identifies the cleaned layout as `photo-editor-edit-recipe-v13.1`; the
  read-only legacy-v13 parser accepts neutral retired fields, rejects non-neutral no-op intent, and
  rewrites accepted input into the cleaned canonical layout. The inert input-gamut and
  lens-correction booleans are also removed from `InputTransformSettings`; lens correction is
  represented only by executable geometry and gamut mapping only by the real output-stage policy,
  so neither retired input flag can perturb engine equality or cache identity.
  A metadata-free engine `ColorAdjustmentState` also defaults `raw_input_transform_enabled` to
  false. Only the decoded-metadata bridge or an explicit RAW fixture may enable sensor black-floor,
  highlight-reconstruction, calibration, camera-look, or camera-profile behavior; generic working
  RGB can no longer inherit RAW clipping from a default-constructed state.
- [x] Replace generic make/model characterization heuristics with byte-identified embedded, bundled,
  or user profile assets. Make/model now serves matching and diagnostics only. Embedded DNG/LibRaw
  camera matrices carry all nine exact coefficients and their reference white in canonical
  provenance; user DCP/ICC profiles require immutable payload identity. A camera-linear source
  without either executable path fails with `camera_linear_characterization_required` rather than
  fabricating a make-specific temperature or identity transform.
- [x] Add sensor-domain highlight reconstruction tests for one-, two-, and three-channel clipping,
  saturated highlights, speculars, colored light, and unrecoverable full clipping.
  `color_pipeline_test` locks the exact per-channel clipping matrix, preserves valid saturated
  highlights, records fully clipped regions and opposed-channel evidence, and keeps disabled,
  camera-default, and full modes distinct.
- [x] Add shadow tests for black-floor stability, chroma contamination, hue stability, false-color
  amplification, and exposure recovery. The property suite locks mathematical black, monotone
  neutral ramps, non-amplification of near-neutral sensor contamination, chromatic ratios for a
  deliberately colored low-light subject, and exact multi-stop exposure composition.
- [ ] Materialize the licensed RAW corpus required by the color evidence contract and exercise real
  decoder code, not constructed post-decode RGB patches.
- [ ] Publish per-camera support and fallback status from real corpus results.
  The strict `publish-camera-support-report.py` evidence layer is implemented and adversarially
  tested. It revalidates the complete real-RAW render set, groups every capture by exact make/model,
  publishes portable and shipping decoder/profile identities, identifies shipping-to-LibRaw
  fallback, retains all promotion blockers, and can revalidate an immutable published report.
  Publication remains open until the approved licensed corpus produces real results.

### Exit gate

- Draft, settled, and export manifests identify the same camera characterization and rendering model.
- Draft/settled perceptual differences stay inside the interactive tolerance; settled/export stay
  inside the final tolerance.
- Real high-dynamic-range RAW cases retain recoverable highlight and shadow detail without hue
  instability or non-finite pixels.
- No production RAW color route uses `CIRAWFilter`.

## Phase 5 — Rebuild tone, highlight, color, and stack semantics

### Work

- [x] Freeze the old fixed-anchor tone model as migration input only. Modern monotone tone and the
  fixed-anchor compatibility operator now live behind the focused `color_tone_runtime.cpp`
  boundary. Its admission predicate returns true only for an explicit `LegacyOklchV1` model;
  both ACES models route directly to the monotone operator, unknown renderer names normalize to
  official `aces2-ocio-v1`, v13 exposes no legacy renderer enum, and active Studio QML exposes the
  loaded renderer as read-only migration status rather than a new-edit selector. Legacy rendering
  remains available only to load, compare, undo, and migrate existing recipes until the Phase 11
  compatibility-release deletion gate is satisfied.
- [x] Define each control in a documented scene or display domain with pivot/range behavior,
  monotonicity, reversibility, image dependence, and stage order. The engine-owned 43-control
  semantics registry is complete and strictly ordered; the property test locks key
  classifications and rejects relabeling the modern monotone
  contrast/highlight/shadow/white/black operators as image-dependent.
- [x] Keep global exposure as direct scene-linear channel gain. The shared desktop mapper writes
  exposure only to `InputTransformSettings`; the engine's former top-level exposure,
  temperature, and tint compatibility fields and fallback accessors are physically removed. The
  desktop compatibility/UI boundary maps directly into the sole authoritative input-transform
  values, and the property suite proves exact one- and two-stop exposure composition across
  signed and extended values.
- [x] Replace overlapping fixed tone-anchor mutations with a monotone log-luminance operator whose
  shadows, highlights, whites, blacks, and contrast controls have measured influence bands. The
  ACES path now composes independently monotone log-stop mappings with compact support and leaves
  the anchor operator on the legacy compatibility renderer only. The property test sweeps every
  control at both extrema over a 12.5-stop unclipped ramp and verifies targeted-versus-remote
  influence for shadows, highlights, blacks, and whites.
- [x] Keep photographic scene-to-display compression in ACES 2 rather than hiding automatic
  display compression inside scene controls. Modern ACES edits skip the legacy image-extrema
  highlight prepass unless the user explicitly enables Highlight Roll-off; extended-range
  exposure and stacked-layer property tests prove values above diffuse white remain available to
  the output renderer.
- [x] Preserve hue/chroma while changing luminance until output gamut or declared creative behavior
  requires otherwise. Scene-tone operators scale working RGB by one luminance ratio; the property
  test locks channel ratios and alpha for a chromatic over-range sample under combined exposure,
  contrast, highlights, shadows, and whites.
- [x] Define highlight reconstruction separately from highlight tone compression. Reconstruction
  is an input-stage camera-linear operation with its own mode, model, telemetry, clipping analysis,
  and CPU fallback reason; Highlights remains a later monotone scene-tone control.
- [x] Define black-level normalization separately from shadows/blacks creative controls. RAW black
  and white levels normalize camera-linear input before characterization, while Shadows and
  Blacks remain later creative scene-tone fields; focused color-pipeline fixtures vary the input
  normalization independently.
- [x] Share exact operator kernels for global, local, and layer edits for exposure, temperature,
  tint, contrast, highlights, shadows, whites, blacks, vibrance, and saturation. The local
  compatibility path now carries the parent photographic rendering model, and the ACES slow path
  no longer inserts the legacy image-aware shoulder merely because a mask/layer selects it.
- [x] Represent mask alpha and normal adjustment-layer opacity as composition operations after the
  common operator result. Non-normal blend modes and local-only hue/colorize controls remain
  explicit specialized operations rather than pretending to satisfy common-operator parity.
- [x] Establish deterministic stage order for curves, channel mixing, perceptual color, grading,
  creative LUTs, local adjustments, layers, and output rendering. The v13 compiler locks
  `Decode -> NormalizeRaw -> CameraCharacterization -> SceneAdjustments -> Tone -> Curves -> Look
  -> Layers -> Detail -> Geometry -> PhotographicOutput -> Proof -> DeviceTransform -> Publish`;
  the Look payload has one deterministic internal order for channel mixing, perceptual controls,
  grading, and creative LUTs, while masks and adjustment/content layers are serialized in the
  following Layers stage. `edit_recipe_v13_test` now asserts the complete ordered vector rather
  than merely checking that selected stages exist. The compatibility pixel path now also runs
  global curves/color/LUT Look work before retouch and photo layers, then texture, clarity,
  dehaze, sharpening, and noise reduction in Detail, followed by Geometry and the final
  compatibility gamut/output boundary. Properties distinguish both `Look -> Layers -> Detail`
  and `Detail -> Geometry` from their reversed orders. Those image stages now execute through the
  production executor; Decode is now scheduled as stage zero and export pixel I/O executes inside
  its Decode callback, while the application-coordinated executor sections remain the Phase 2
  execution gap. The five
  selectable scope variants compile as purpose-specific truncations of this same order.
- [x] Remove implicit skin and tonal protection from generic controls unless the recipe explicitly
  selects it. Global and local vibrance no longer multiply undeclared skin-hue or lightness
  suppression, and local saturation now uses the same declared positive/negative scale as global
  saturation. The explicit Skin Protection recipe control remains the sole opt-in suppression;
  the property test proves generic vibrance uses its range and the explicit control measurably
  reduces that response.
- [x] Define saturation and vibrance in the active linear working-profile context without forcing
  an intermediate sRGB-like gamut. Saturation is a declared chroma scale, vibrance is a
  low-chroma-selective scale with no hidden hue or lightness suppression, and signed/over-range
  property samples remain finite with alpha unchanged. Global, local, and layer look operations
  now reconstruct unbounded working RGB through one shared post-tone color kernel; the complete
  compatibility render performs one final gamut fit instead of compressing after each stacked
  operator.
- [x] Add neutral, hue-linearity, monotonicity, derivative continuity, reversibility, commutation,
  stack-depth, over-range, alpha, and non-finite property tests. The property suite covers neutral
  axes and shadow ramps, Hellwig hue round trips and the 359/0 seam, monotone tone extrema with
  bounded adjacent log-slope changes, reversible built-in output subchains, exact exposure
  composition and deliberate exposure/curve non-commutation, 10/50/100-edit stacks, signed
  extended samples, alpha preservation, and fail-closed non-finite inverse input.
- [x] Tighten global/full-mask/layer equivalence to a numerical engine tolerance before blend and
  output transforms. Exposure, temperature, tint, contrast, highlights, shadows, whites, blacks,
  vibrance, and saturation now share the global operator kernel and pass at `3e-6`; mask alpha is
  tested as post-operator composition. A second fixture proves the same contract above diffuse
  white under `aces2-ocio-v1`, preventing a masked edit from silently re-entering the legacy
  highlight shoulder.
- [x] Add ten-, fifty-, and one-hundred-edit stress recipes to prove stable stacking, bounded memory,
  deterministic output, undo/redo, serialization, and cache identity.

The complete deep-stack gate is now executable: `edit_recipe_v13_test`
round-trips and recompiles 10-, 50-, and 100-layer recipes under a 1 MiB serialized bound with
distinct cache identities, while `color_pipeline_property_test` applies 10, 50, and 100 balanced
scene-edit passes to shadow, midtone, over-range, and partial-alpha samples and proves bit-exact
repeat output with finite non-negative channels. The desktop controller now also advances through
its 10,000-dab undo/redo stress case inside the existing 12-second settle deadline after reusing
precomputed recipe digests and avoiding repeated opaque-payload normalization; the deadline was not
increased. A second desktop workflow performs 100 real edit commands to build a 50-layer balanced
stack, settles and renders it, round-trips the authoritative recipe below 1 MiB, fully undoes and
redoes all 100 commands, and requires the restored pixels to hash exactly to the reference. It also
enforces a 256 MiB resident-growth ceiling on macOS. This gate exposed and removed a legacy
persistence leak: rendered thumbnail data URLs and queue diagnostics were serialized twice inside
every layer. Canonical layer serialization now recursively strips all `thumbnail*` runtime fields
while preserving actual layer pixels and masks. The complete `editor_preview_controller_test`
passes after the stress workflow, proving no later shared-state leakage; the fast
`edit_recipe_state_test` separately locks recursive runtime-field removal and authoritative
pixel/mask preservation.

Creative LUT execution now honors the declared input and output color spaces instead of sampling
every cube directly in the active working RGB domain. The runtime resolves the supported RGB
primaries and transfer functions, converts working-linear pixels into the LUT input encoding,
decodes the LUT output, and returns it to working-linear space before strength composition. An
unsupported declared space fails closed rather than silently applying a wrong-domain transform.
`cube_lut_test` locks an encoded-sRGB nonlinear fixture and the unsupported-space behavior; the
focused pipeline, property, tool-invariant, render-parity, layer-LUT-parity, and render-golden set
passes 7/7 after the change. The supported-space registry is now shared by runtime execution and
the canonical recipe compiler. Compilation rejects unsupported global or layer input/output spaces
with `UnsupportedColorSpace` and an exact recipe path, and produces no render plan; the recipe,
desktop adapter/bridge, LUT, property, invariant, parity, and golden regression set passes 10/10.
This closes the immediate LUT-domain stacking defect and its compile-time admission gap. Real-
corpus preset retuning and approval remain Phase 6 work.

### Exit gate

- Exposure produces the same stop change across the unclipped scene-linear range before the DRT.
- Highlight and shadow controls preserve recoverable texture on the real corpus and remain monotone
  on synthetic ramps.
- Equivalent global, full-mask, and normal adjustment-layer operators match at engine precision
  before explicit composition.
- One hundred stacked edits remain finite, deterministic, serializable, undoable, and visually
  stable.
- The fixed-anchor and legacy OKLCH runtime operators have no production caller.
  The localized brush-tile production caller no longer admits `legacy-oklch-v1`; it now admits only
  `aces2-ocio-v1`. Its exact-cover texture patch remains float working data until it executes the
  same compiled photographic-output transform as the full render. This removes the previous
  half-float pack/redecode and the raw Qt color conversion that bypassed ACES and caused a 29-level
  channel error. The focused `editor_preview_aces_brush_patch_test` requires one-tile, chained,
  exact brush-effect, and multi-tile reuse to use `adjusted_tile_cache`, limits full-frame/crop
  channel differences to one 8-bit level, requires zero patch-base copy pixels, and separately
  proves an explicit legacy recipe bypasses the ACES-only shortcut. It passed in 72.25 seconds on
  the final stricter run;
  the architecture boundary rejects restoring legacy admission or bypassing photographic output.
  The monolithic `editor_preview_controller_test` passed the complete localized section but later
  reached its existing global 625-second timeout during unrelated AI-mask work, so that broad suite
  is not recorded as green for this change.
  The complete Phase 5 exit statement remains open because compatibility-release readers and the
  legacy renderer still have intentional production callers until the Phase 11 migration gate.

## Phase 6 — Promote one photographic renderer and look system

### Work

- [x] Make `aces2-ocio-v1` the only renderer for new and migrated v13 recipes. The v13 enum has no
  legacy value, the adapter rejects relabeling, and both new desktop recipes and bare engine color
  configurations now select ACES 2. Only explicit compatibility fixtures and readers select the
  old renderer.
- [ ] Complete actual Metal and CUDA parity for the 65-cube interactive and 97-cube settled/final
  output LUT tiers.
  Metal owns a real tetrahedral LUT kernel and now measures the 65-cube interactive and 97-cube
  final tiers independently against their matching CPU LUT applications. The v2 physical-device
  proof publishes separate per-tier p99 Delta E 00, maximum Delta E 00, maximum channel error,
  alpha, cache identity, transfer, dispatch, and tagged-chart evidence; the adversarial Apple proof
  validator rejects either tier crossing its gate. The native test compiles here but correctly
  skips because this execution context exposes no system Metal device. CUDA remains a probe-only
  planned backend whose own status says rendering still uses CPU, so this item remains open.
  The built-in production publication helper now invokes that shared Metal ACES LUT backend when a
  device is available instead of leaving it test-only; failure retains the untouched float working
  pixels and executes the exact CPU LUT. Publication telemetry names `metal` or
  `exact-cpu-fallback` and records upload, readback, command-buffer, and dispatch counts. The source
  contract, `render_parity_test`, and localized ACES patch gate pass 3/3. This connects production
  dispatch but cannot replace the still-missing physical-device parity artifact.
  The production contract was then corrected from working-space output to target-linear output:
  the QImage fallback had always called `apply_aces2_output_lut_target_application`, while the
  Metal kernel incorrectly multiplied the rendered target back through `target_to_working` before
  the result was tagged as the target color space. That device-only mismatch is removed, the API is
  now explicitly named `apply_aces2_output_lut_target`, and the physical parity test compares both
  LUT tiers in the preview display target rather than the working gamut. The test also dispatches
  before measuring the interactive tier instead of accidentally measuring the preceding final-tier
  result. The source contract and shipping `render_parity_test` pass; physical execution remains a
  correctly skipped gate because this process reports `no_system_metal_device`.
- [ ] Require CPU/GPU p99 Delta E 00 `<= 0.25` and maximum `<= 0.75`, including negative,
  over-range, alpha, tiles, dirty patches, proof, and export.
  Both Metal LUT tiers now enforce p99 `<= 0.25`, maximum `<= 0.75`, channel error `<= 2e-4`,
  finite output, and exact publication alpha over signed and over-range synthetic samples. The
  97-cube tier additionally has the tagged-chart full-pixel contract. Tiles, dirty patches,
  proof/export route evidence, physical Metal execution, and CUDA remain open.
- [ ] Run the licensed corpus through CPU, Metal, CUDA, preview, proof, HDR, and export.
- [ ] Conduct blinded editorial comparison on skin, sky, foliage, neon, night, deep shadow,
  saturated fabric, speculars, and clipped highlights.
- [ ] Retune every built-in preset against the final input and output renderer.
- [x] Version presets independently from the engine, with an explicit source renderer and migration
  target.
  Every built-in and user preset now exposes `photo-preset-identity:v1` with its own
  `presetVersion`, `sourceRenderer`, `migrationTarget`, and built-in/user identity. New user presets
  begin at revision 1 and overwrite advances only that preset's revision. The contract persists
  through the v3 local preset store, portable preset-pack v3 export/import, and preset-sync v3
  export/import. All three now write `photo-editor-adjustment-patch` v1 rather than complete v12
  `recipeJson` documents. Reusable preset patches strip camera DCP, input/preview/proof/export ICC,
  monitor-path, and source-profile identity; camera defaults use the same codec with an explicit
  camera-bound scope and retain only their intentional camera identity. Legacy unversioned presets
  and camera defaults remain compatibility reads and are rewritten only in the patch format after
  a successful mutation. The architecture gate prohibits a new preset/default `recipeJson` writer,
  while the codec regression proves identity stripping, camera identity retention, round-trip,
  malformed embedded-recipe rejection, and marked legacy reads. Legacy presets are admitted as
  revision 1 with renderer provenance
  derived from their recipe and the current v13.1 migration target. The UI/controller can inspect
  the contract through `presetVersionInfo`. The complete controller regression proves built-in
  ACES provenance, revision advancement, pack round-trip, sync round-trip, rename retention, and
  deletion, while retaining the existing stress deadline after eliminating a redundant registry
  rebuild introduced during implementation.
- [ ] Replace render hashes as the sole approval mechanism with perceptual metrics, behavior
  assertions, and approved visual artifacts. Retain hashes only as deterministic change detectors.
- [ ] Require two recorded approvals for default promotion and preset acceptance.

### Exit gate

- The official ACES 2 renderer passes independent reference, CPU/GPU, real-photo, HDR, monitor, and
  editorial gates.
- All built-in presets have approved v13 output and no stale hashes.
- The new-recipe default is the official renderer.
- Native ACES prototypes and legacy OKLCH renderers are no longer selectable in normal UI.

## Phase 7 — Make interaction immediate and GPU-resident

### Work

- [x] Compile the recipe and reusable transforms once per state/target change, not once per tile or
  publication route.
  Canonical plan admission now uses a concurrency-safe, identity-complete 64-entry LRU keyed by
  recipe digest, source identity, exact decoder provenance, RAW boundary, dimensions, render
  purpose, photographic target, immutable device ICC identity, and scope domain. An in-flight key
  has one builder; concurrent callers wait and receive the same immutable plan rather than
  recompiling. Admission reports cache hit, resident entries, cumulative hits, and actual builds.
  The focused export regression launches eight identical requests concurrently and requires one
  build, seven hits, one plan identity, and one build generation; a target change requires exactly
  one new plan. It then inserts 65 unique identities, proves the resident count never exceeds 64,
  and proves the least-recently-used plan is actually evicted and rebuilt. The complete preview
  controller suite passes across preview, scopes, thumbnails, profile changes, and stale rejection,
  while the existing processor/output-LUT tests retain their single-builder guarantees.
- [x] Keep decoded, characterized, adjusted, and output intermediates in explicit bounded caches.
  The desktop path owns separate mutex-protected LRU stores for decoded images, characterized
  scene-linear buffers, adjusted working buffers, rendered previews, rendered tiles, and executor
  stage surfaces. Their hard resident ceilings are respectively configurable up to 8 GiB with a
  512 MiB default, 512 MiB, 768 MiB, 256 MiB, 256 MiB, and 384 MiB; an entry larger than its
  owning cache is rejected rather than temporarily exceeding the ceiling. Cache snapshots expose
  entries and bytes in render diagnostics, display-only changes reuse the adjusted buffer without
  re-decoding or re-editing, and stale generations are rejected before cache publication. Existing
  decoded-cache trim tests, executor byte-ceiling/eviction tests, display-suffix reuse tests, and
  the complete preview-controller suite pass.
- [~] Keep interactive graph stages GPU-resident from input upload through output texture when the
  backend supports the plan.
- [~] Fuse output LUT and device publication without removing stage-level diagnostics.
- [~] Eliminate unnecessary CPU-to-GPU and GPU-to-CPU round trips.
  Production built-in ACES publication now exposes its actual Metal upload/readback bytes and
  dispatch counts instead of silently remaining CPU-only. A retained `MetalOutputSurface` now lets
  exact-cover localized ACES brush patches, max-resolution visible-region prepasses,
  multi-tile visible/dirty patches, and ordinary full-frame in-memory previews execute the
  target-linear output LUT, encode standard sRGB into a BGRA8 Metal texture, and GPU-blit that
  texture into the existing preview texture with
  zero production GPU-to-CPU bytes and zero QImage-to-texture upload bytes. Admission requires an
  existing compatible BGRA8 preview texture and rejects soft proofing, custom preview ICC, manual
  monitor profiles, wide-gamut output, and non-sRGB system monitor profiles. Publication validates
  source/destination dimensions, format, and Metal device registry identity; retained ownership
  spans worker completion through deferred destination publication. The route reports
  `metal-native-srgb8`, one output command/dispatch, zero readback bytes, a zero CPU texture-upload
  byte count, and one GPU blit. Unsupported targets and unavailable Metal retain the canonical
  QImage/CPU fallback.
  Both adjusted-cache and freshly evaluated working-image routes preserve the retained surface in
  their typed render result, so the visible prepass no longer falls through merely because its
  QImage is intentionally absent. Multi-tile publication creates one retained surface for the
  current-state bounding patch and deliberately does not populate the rendered-QImage tile cache;
  later QImage consumers miss safely and render canonical tiles instead of receiving incomplete
  cache data. Full-frame admission is fail-closed: comparison and migration evidence, diagnostic
  overlays, active pixel inspection, disk publication, proof, custom ICC, and HDR continue to
  require the canonical CPU image. The broader item remains open: full-resolution/disk-backed
  publication and preceding decode/characterization stages still allocate/upload CPU float
  buffers, and the general QImage path still reads back before publication. Cropped native
  publication now addresses the retained working buffer directly with an explicit source stride
  and origin, so an exact visible or dirty subrectangle no longer materializes CPU pixels merely
  to crop it. Native preview publication itself is deferred: the producer signals an
  `MTLSharedEvent`, asynchronous patch blits and full-surface adoption wait on that event in their
  own queue, and completion handlers apply generation/commit guards before replacing the visible
  texture. Synchronous waits remain only at explicit CPU materialization and test-readback
  boundaries. Retaining surfaces across decode and camera characterization is still required. The
  output-LUT counters propagate
  through full-frame, tiled, dirty-tile, exact-region, and localized brush publication into normal
  preview render diagnostics. Their shared typed value lives in
  `output_lut_publication_telemetry.h`, and the float patch copy primitive moved to
  `working_patch_compositor.h`; native targeting/publication ownership lives in
  `native_output_surface_publication.h`, and the controller remains below its frozen architecture
  ceilings at 43,827 implementation lines and 2,707 header lines. The focused output contract and ACES brush
  gate pass 2/2, and the architecture boundary plus `git diff --check` pass. A runtime
  `render_parity_test` assertion now requires shipping built-in ACES publication to report either
  one real Metal command/dispatch with nonzero upload/readback bytes or an exact CPU fallback with
  all GPU counters zero. The output transform now also receives an explicit backend policy:
  `CpuOnly` suppresses Metal and must report the exact zero-GPU fallback while remaining within
  preview/export tolerance. The output contract and runtime parity gate pass 2/2 in 3.44 seconds;
  the architecture boundary and diff check pass without raising any frozen file-size ceiling. The
  localized production caller and output source contract then passed 2/2 in 66.19 seconds, and the
  complete `Framehouse` application target compiled and linked.
  After native publication and the target-linear correction, the output source contract, localized
  ACES brush patch, preview history, and shipping render parity gates pass 4/4 in 27.42 seconds;
  the physical Metal member is separately present and skipped. The expanded physical test reads
  the retained surface only inside the test, requires one-code-value sRGB8 parity, and requires
  production telemetry to remain at zero GPU-to-CPU bytes. The full `Framehouse` target compiles
  and links, the architecture boundary and `git diff --check` pass, and the attempted offline shader
  compile is recorded as unavailable because Command Line Tools does not provide the `metal`
  compiler. None of those local checks substitutes for the still-open physical-device run.
  A final current-tree boundary rerun still accepts the controller/header/color utility ceilings but
  is not globally green: unrelated concurrent QML work has `StudioWorkspace.qml` at 24,206 lines
  against its frozen 24,133-line ceiling. This color slice does not modify that workspace; the
  current `git diff --check` remains green, and the unrelated boundary failure is not relabeled as
  color or GPU proof.
  After multi-tile native publication was wired into both visible-tile and dirty-tile callers, the
  output contract, localized ACES brush patch, preview history, and shipping render parity gates
  passed 4/4 in 55.65 seconds. The focused targets compiled warning-clean and the controller stayed
  below its frozen ceiling; the complete `Framehouse` application target also compiled and linked.
  The global architecture rerun still reports only the pre-existing
  `StudioWorkspace.qml` overage described above.
  The subsequent full-frame in-memory cutover routes its retained surface through the same typed
  publication and device-identity checks, classifies a whole-image blit separately from a partial
  upload, and skips the CPU image provider only when the native surface was actually accepted.
  Explicit `require_cpu_preview_image` admission preserves comparison/migration evidence,
  diagnostic overlays, and active pixel inspection. The four focused gates passed 4/4 in 41.27
  seconds, the complete `Framehouse` target compiled and linked, `git diff --check` passed, and the
  controller/header stayed below their frozen ceilings. Physical-device execution remains open.
  Whole-frame native publication now adopts the validated retained BGRA8 Metal texture directly as
  the preview texture instead of issuing a redundant destination blit. Partial surfaces retain the
  bounded blit path, and failed adoption falls through to that path or the canonical QImage upload.
  Telemetry distinguishes adoption from upload and reports zero GPU blits for adoption. The focused
  gates passed 4/4 in 41.02 seconds and the complete `Framehouse` target compiled and linked; the
  controller remains below its frozen ceiling at 43,838 lines. The source producer still waits for
  Metal completion before returning, so asynchronous producer/render-queue synchronization remains
  required rather than being inferred from direct adoption.
  The physical Metal parity target now links the shipping preview-texture implementation and, when
  hardware is available, requires direct adoption to preserve the exact native texture handle and
  dimensions. It compiles successfully in the current configuration but reports an explicit skip
  because no system Metal device is exposed; that skip is not counted as adoption or color proof.
  Partial retained-surface publication no longer blocks UI publication on
  `waitUntilCompleted`. The Metal preview owner now double-buffers each patch: one command buffer
  copies the last complete texture and the new patch into a replacement, leaves the displayed
  texture untouched while work is pending, validates the originating render generation and a
  monotonic texture-publication serial on the main queue, then atomically swaps the handle and
  advances the native revision. A newer upload/adoption invalidates the pending serial, so an old
  patch cannot overwrite a newer frame; destruction is guarded by an independently retained
  lifetime token. Telemetry distinguishes pending asynchronous upload, direct adoption, and actual
  blit count. The output contract, localized brush, preview history, and render parity tests passed
  4/4; the expanded physical member compiled and remained explicitly skipped, producing a 5-test
  run with four passes and one skip in 40.36 seconds. The complete `Framehouse` target compiled and
  linked, `git diff --check` passed, and the controller remains within its frozen ceiling at 43,844
  lines. Partial output production now returns a deferred retained surface backed by an
  `MTLSharedEvent`: the output command queue signals readiness after the output kernel, and the
  preview command queue waits on that value before copying into its double-buffered replacement.
  A failed producer CPU-signals the event to prevent a deadlock, while the guarded main-queue commit
  separately requires producer success, the originating render generation, and the current
  publication serial. Full-frame production now uses the same deferred surface contract. The
  preview publication queue waits on its readiness event without a CPU wait and exposes the
  producer texture directly only after successful completion; it performs no destination blit.
  The asynchronous commit owns the rendered cache identity, so a scheduled surface is not treated
  as the current texture before publication. The expanded physical test requires both a deferred
  3x3 patch producer and a deferred full-frame producer, nonzero readiness handles/values,
  successful atomic native-handle changes, and rejection of a deliberately stale full-frame commit
  while preserving the last completed texture. The output contract, localized brush, preview
  history, and render parity gates pass 4/4; that physical member compiles but remains explicitly
  skipped because no system Metal device is exposed, giving four passes and one skip in 46.84
  seconds. The complete `Framehouse` target also compiles and links, and `git diff --check` passes.
  The controller is exactly at its frozen 43,845-line ceiling and its header is at 2,707 lines.
  The three concurrent architecture overages were subsequently decomposed rather than accepted:
  `photo_render_utils.cpp` is 3,355/3,393 lines, `color_mask_runtime.cpp` is 3,149/3,192, and
  `edit_recipe_mask_schema.h` is 2,967/2,977. The global architecture check and `git diff --check`
  are green again. Physical render-queue execution remains unavailable locally and the upstream
  graph is not yet end-to-end GPU-resident, so the overall GPU-residency item remains partial
  rather than complete.
  The canonical executor now admits the adjacent `PhotographicOutput -> Publish` suffix as one
  evidence-bearing Metal execution while retaining both logical stages in the completion manifest.
  Fusion is fail-closed: the executor requires a contiguous plan-ordered stage list, a passed
  unfused-reference identity, a final-surface descriptor matching `Publish`, and a stop boundary
  that covers the entire fusion. The controller's first implementation still materialized its
  retained adjusted surface before this suffix; that hidden GPU-to-CPU readback and subsequent
  upload have now been removed. `canonical_geometry_surface()` carries the existing
  `MetalWorkingSurface` backing into the executor without copying pixels, the ACES 2 output kernel
  waits on its producer event, and the resulting `MetalOutputSurface` is the canonical retained
  `Publish` surface. `MetalOutputSurface` implements the backend-neutral `RenderSurfaceBacking`
  contract, including exact descriptor validation, byte accounting, cancellation-aware generic
  materialization, and BGRA8-to-encoded-float channel ordering for CPU-only consumers. The direct
  sRGB branch remains deliberately limited to ordinary in-memory sRGB ACES previews with no proof,
  custom device ICC, HDR, scopes, comparison, tiles, forced CPU image, or unavailable Metal backend.
  Backend availability is an explicit policy input, so a Metal-enabled build with no exposed
  system device materializes the retained input once and continues through the canonical CPU
  reference suffix instead of selecting a fused callback that cannot publish a surface.
  The executor regression proves the fused callback runs once, consumes a retained input without
  invoking its readback method, skips the separately scheduled `Publish` callback, and reports both
  logical stages. `render_parity_test` proves the controller-facing geometry handoff preserves a
  native backing with zero materialization. The Metal source contract covers the production
  controller and both retained-surface services; the physical test additionally covers generic
  `RenderSurface` validation/readback and encoded channel parity when hardware exists. The focused
  run passed three runnable tests with one explicit `no_system_metal_device` skip in 4.20 seconds;
  the complete `editor_preview_controller_test` passed in 379.02 seconds. After decomposing the
  canonical suffix into `canonical_native_preview_suffix.h` instead of raising the frozen
  `retained_preview_surface.h` ceiling, the retained header is 97 lines, the new focused service is
  frozen at 134 lines, the controller is 43,807 lines, the architecture boundary and
  `git diff --check` pass, and the complete `Framehouse` target links. This closes the ordinary
  canonical sRGB output-suffix round trip, not the three broader checkboxes: upstream geometry and
  unsupported targets still use CPU surfaces, and real-device Metal plus CUDA evidence remains
  open. The deterministic admission regression separately proves `backend_available=false`
  rejects the native suffix while the same eligible sRGB request is admitted when availability is
  true, and that proof/device stages remain rejected. That runtime policy test and the source
  contract pass 2/2 in 3.95 seconds after the complete application and controller test targets
  rebuild and link; the architecture and diff gates remain green.
  The adjacent `PhotographicOutput -> DeviceTransform -> Publish` suffix now has a separate
  retained custom-monitor path rather than being forced through a full-frame CPU readback. It
  renders ACES 2 into the linear Rec.2020 interchange surface, evaluates the measured ICC LUT on
  Metal, and publishes the device-encoded BGRA8 texture while preserving all three logical stages
  in executor diagnostics. The first admission attempt incorrectly required the measured ICC LUT
  to contain zero unsafe cells; a normal linear-sRGB-to-sRGB profile disproved that assumption.
  The corrected path keeps the established Delta E gates: Metal counts and compacts only pixels
  whose LUT cell is marked unsafe or whose input is outside the LUT domain, Little CMS transforms
  that subset exactly, and a final Metal kernel patches those exact values into the retained
  texture. The selective transfer is capped at one quarter of the frame and 1,048,576 pixels; a
  larger subset fails with `aces2_icc_native_exact_subset_exceeds_budget` and resumes through the
  exact CPU reference instead of weakening accuracy or performing an unclassified full-frame
  readback. LUT-safe images use one four-byte count readback; corrected images report the exact
  compacted pixel/index traffic, command buffers, kernels, and zero blits. The source contract covers
  count, compaction, patching, the bounded fallback, controller admission, and three-stage fusion;
  the physical parity member now requires a LUT containing unsafe cells, bounded sub-frame
  readback, exact device-transform parity within one encoded code value, and a valid retained
  publish descriptor when a Metal device exists. The focused executor, source-contract, ICC LUT,
  and render-parity gates pass 4/4 in 9.93 seconds; `editor_preview_controller_test` passes in
  399.56 seconds; `Framehouse` and the physical test target compile and link; the architecture and
  diff gates pass. The physical member reports an explicit hardware skip because this environment
  exposes no system Metal device, and the absent standalone `metal` compiler prevents an offline
  shader compilation. Those skips leave real-device ICC parity and the broader three Phase 7
  checkboxes open. The retained ICC publisher subsequently replaced its initial full-frame
  working-to-target buffer blit with an out-of-place ACES output-LUT compute kernel. The source
  working buffer is now read directly, the target buffer is written once, and this route reports
  zero GPU blits while preserving its three initial logical kernel dispatches and bounded exact
  subset diagnostics. The source contract now rejects any blit encoder, buffer copy, or blit
  telemetry inside this function; the contract, editor layer, render parity, and layer-LUT parity
  gates pass, and the complete `Framehouse` target compiles and links. The physical parity member
  compiles but remains skipped because no system Metal device is exposed, so this is not yet
  device-executed shader proof. The audit also deleted the now-uncalled synchronous native patch
  upload API and its blocking `waitUntilCompleted`; production exposes only the generation-guarded
  asynchronous patch route, and the source contract forbids restoration of the synchronous method
  or wrapper. The same five-test slice passes with four runnable passes and the explicit physical
  Metal skip. The decomposed policy, execution, device-LUT, and retained-surface helpers are
  116, 205, 43, and 97 lines; the controller remains at its frozen 43,845/2,707-line ceilings.
  The next upstream audit found that the backend's `apply_pre_tone_effects` and
  `apply_post_color_effects` entry points had no production or test caller. They were not a safe
  retained-graph implementation: their three Metal kernels were mandatory at backend startup but
  had no parity evidence against the canonical CPU operators, so they could only increase shader
  compilation risk while suggesting acceleration that never occurred. The unused public methods,
  host uniforms, pipeline state, initialization, implementations, shader helpers, and kernels are
  now deleted, removing 845 lines from the live color-adjustment shader. The source contract reads
  that shader directly, requires the still-live LUT, mask, spatial-tone, and detail kernels, and
  rejects restoration of any of the three dead kernels or their host entry points. After deletion,
  the application and focused native targets compile and link; all seven runnable executor,
  pipeline, invariant, ICC, contract, and render-parity tests pass in 18.59 seconds; the architecture
  boundary and `git diff --check` pass. The physical Metal member remains an explicit
  `no_system_metal_device` skip. Canonical pre-tone and post-color work therefore remains on the
  exact CPU reference until it is replaced by an executor-owned retained stage with real parity
  evidence; this cleanup does not claim that upstream GPU residency is complete.
  The first executor-owned upstream retained seam now replaces the controller-only fast-path
  fiction for eligible canonical previews. `execute_color_plan()` recognizes the contiguous
  planned range from `SceneAdjustments` through `Geometry`, allows optional canonical stages such
  as `Curves`, and accepts a fusion only when its evidence manifest exactly matches the ordered
  plan. After CPU camera characterization, the production callback uploads the characterized
  pixels once, applies universal exposure as one exact float multiplication kernel, and returns a
  deferred `MetalWorkingSurface`. The first implementation incorrectly built the generic color LUT
  directly from the complete state even though that builder starts at decoded input; that would
  have repeated RAW normalization, camera characterization, white balance, and exposure on an
  already characterized surface. The corrected helper explicitly neutralizes the consumed input
  prefix, and the exposure-only admitted path does not upload or execute the bounded 3D LUT at all.
  Consequently negative values and highlights raised above `1.0` by exposure remain scene-linear
  instead of being clamped to the LUT domain. That same backing flows into the retained ACES 2
  output/device publication suffix without working-pixel readback or re-upload. Every fused logical
  stage remains present in executor completion diagnostics. Admission is deliberately fail-closed:
  every non-exposure tone control, post-color, local/photo layers, masks, detail, geometry, clipped/highlight-
  reconstruction, and negative or over-range inputs continue through the exact individual CPU
  stages with the existing `unsupported_stage:<Stage>:<detail>` reason. A Metal dispatch failure
  also runs those exact stages rather than failing the render or applying an approximate edit.
  The hardware-independent fusion test proves exact CPU-fallback pixels, fallback classification,
  complete stage manifestation, and rejection of an incomplete manifest. The executor regression
  proves one characterized-surface callback and all six present working-stage diagnostics. The
  physical Metal test now applies `+0.75 EV` to post-camera working pixels, compares the result
  against the unfused stage sequence with a 0.004 linear-channel ceiling and exact alpha, requires
  one pixel upload, no LUT upload, one kernel dispatch, no production readback, and values above
  `1.0` to survive into the retained surface before ACES publication. The hardware-independent
  test separately proved at that milestone that prefix-only state produced an identity working LUT
  and that exposure was admitted while contrast remained
  `unsupported_stage:Tone:exact_global_tone`; the next retained seam below supersedes that initial
  contrast restriction. The physical test
  compiles but remains an explicit `no_system_metal_device` skip locally. The
  seven-test runnable executor/color/contract/fallback/render slice passes 7/7 in 6.29 seconds, the
  complete `editor_preview_controller_test` passed, and `Framehouse` linked. At that milestone GPU fallback policy was
  extracted into a 156-line focused header, reducing the controller from the temporary 43,902-line
  overage to 43,729/43,845; the architecture and diff gates pass. This closes one bounded
  post-camera-to-output retained subset, not the three broad GPU-residency checkboxes: exact tone,
  color, local/layer, detail, geometry, upstream decode/characterization, CUDA, and target-hardware
  evidence remain open.
  The next retained seam now executes canonical per-pixel global tone directly after the universal
  exposure kernel instead of dropping contrast, highlights, shadows, whites, blacks, brightness,
  filmic toe, adaptive contrast, levels, and the five zone controls back to a full-frame CPU
  render. `CompiledToneStageLut` converts the existing 16,385-sample double-precision tone table to
  the immutable float payload actually consumed by a GPU, carries the working-space luminance
  coefficients and ACES scene headroom, and derives a stable cache identity from that complete
  payload. The dedicated `apply_global_tone_luminance` Metal kernel uses the same log-stop table
  interpolation, headroom-safe luminance scaling, lifted-shadow colorfulness stabilization, and
  exact alpha preservation as the canonical CPU stage. It runs after scene exposure in the same
  command buffer and still does not upload or execute the generic bounded RGB LUT. Metal tone
  buffers use an observable four-entry/512-KiB LRU; malformed payloads fail closed, and production
  admission rejects non-bounded source RGB or luminance above the compiled table domain rather
  than silently clamping unsupported input. The specialized preview policy neutralizes only the
  sixteen proven table controls plus exposure. Explicit filmic shoulder, HDR local tone, master,
  luma, parametric, and channel curves, post-color, masks/layers, detail, and geometry continue to
  report their existing exact CPU fallback classification.
  The hardware-independent proof compares the compiled float evaluator against the unfused CPU
  tone stage over black plus 768 neutral/chromatic samples spanning -30 through +12 stops, requires
  maximum linear-channel error no greater than `0.004`, exact alpha, stable cache identity, a real
  tone-table cache hit, a 1,024-sample neutral monotonicity ramp, supported-control admission, and
  rejection of image-dependent shoulder and out-of-domain source pixels. The physical Metal member
  now exercises exposure plus representative contrast/highlight/shadow/white/black edits, requires
  two kernels, one bounded tone-table upload, no production readback, an observable cache within its
  ceilings, and the same `0.004` unfused parity limit when a device exists. `Framehouse` and all
  changed targets compile and link; the seven-test executor/color/contract/fallback/parity slice
  passes 7/7 in 5.76 seconds; `editor_preview_controller_test` passes in 389.16 seconds; and the
  architecture and diff gates pass. To preserve the frozen monolith boundary, compilation and the
  float reference evaluator live in the new 179-line `color_tone_compilation.cpp` owner while
  `color_adjustments.cpp` remains at its 1,227-line ceiling. The local physical member is still an
  explicit `no_system_metal_device` skip, so physical shader execution, CUDA, image-dependent tone,
  curves, downstream working stages, and the broad Phase 7 residency checkboxes remain open.
  The following in-order retained seam now covers the complete global Curves stage rather than
  approximating it in the disabled bounded RGB LUT. `CompiledCurveStageLuts` samples the canonical
  monotone-Hermite evaluators into 4,097-entry float tables for parametric tone, master RGB, luma,
  red, green, and blue curves, preserving the CPU order and the distinct semantics of luminance
  scaling versus display-referred channel curves. Values above diffuse white remain unchanged by
  the parametric curve as required, while explicit point curves retain their documented `[0, 1]`
  display-referred clamp. One `apply_compiled_curve_stage` Metal kernel executes after exposure and
  global tone in the same retained command buffer, preserves alpha, and reuses the exact
  headroom-safe/lifted-shadow luminance-scaling contract for parametric and luma operations. The
  combined six-block payload uses an observable four-entry/512-KiB LRU and invalid/non-finite
  programs fail back to the unfused CPU graph.
  This audit also found and fixed a pre-existing fail-open omission: red, green, and blue point
  curves were absent from `render_state_requires_exact_global_tone()`. Because the generic LUT is
  disabled on the retained path, those curves could previously be admitted and skipped. The shared
  policy now classifies all five point-curve families; after the compiled implementation proved
  parity, the specialized working policy neutralizes exactly those curve fields and admits them
  only with a valid compiled payload. The hardware-independent proof compares all six simultaneous
  curve families against `apply_color_curves_stage()` over black plus 1,024 chromatic samples from
  `0` through `4.0`, requires stable cache identity, maximum linear-channel error `<= 0.004`, and
  exact alpha. The expanded physical member requires exposure, tone, and curves as three ordered
  kernels, one tone upload, one fixed six-block curve upload, no production readback, bounded cache
  statistics, and unfused parity when a Metal device exists. `Framehouse` and the changed targets
  compile and link; the focused seven-test slice passes 7/7 in 8.03 seconds;
  `editor_preview_controller_test` passes in 402.89 seconds; and architecture plus diff checks pass.
  The new curve compiler/reference owner is 229 lines, `color_adjustments.cpp` remains at its
  1,227-line ceiling, and the controller remains below its frozen limit at 43,743 lines. Physical
  Metal shader execution still reports `no_system_metal_device`; exact Look/post-color, layers,
  detail, geometry, upstream decode/characterization, CUDA, and broad Phase 7 residency therefore
  remain open.
  The next retained seam implements the common Look prefix directly instead of approximating it
  with the bounded RGB LUT. `CompiledLookStageProgram` carries the exact working-space/D65 matrices,
  native luminance coefficients, RGB mixer, black-and-white channel mixer, saturation, vibrance,
  three-way color grading, per-channel hue/saturation/luminance mixing, selective CMYK controls,
  scene headroom, an active-operation mask, and a stable identity. The CPU float evaluator and
  `apply_compiled_look_stage` Metal kernel preserve the canonical order after Curves: RGB channel
  mixing, black-and-white mixing, then extended-range Oklab vibrance, per-channel mixing, color
  replacement, harmony, grading, and saturation. Explicit skin protection attenuates vibrance,
  per-channel mixing, replacement, harmony, and grading from one pre-edit Oklab selection without
  silently weakening global saturation. Replacement uses the canonical RGB selection hue and
  feather, shortest Oklab hue rotation, optional saturation expansion, and optional post-conversion
  luminance restoration including lifted-shadow colorfulness stabilization. They preserve alpha
  and the native-working-space neutral result for minus-100 saturation; unlike a sampled
  three-dimensional cube, the direct program does not clamp highlights to a fixed RGB domain.
  Compilation, payload identity, mixer, and harmony math are decomposed across focused owners at
  or below their frozen 310-, 120-, 190-, and 100-line ceilings rather than growing another color
  monolith. A hardware-independent proof compares black
  plus 1,024 chromatic samples through fifteen
  representative programs against the complete CPU Look stage over scene-linear values through
  `32.0`, requires maximum linear-channel error `<= 0.004`, exact alpha, and stable identity. The
  physical member requires exposure, tone, Curves, Look, and a two-member user-LUT stack as six
  ordered kernels with no extra Look payload upload and zero production readback when a device
  exists. Admission neutralizes only
  those proved controls. The direct mixer retains OKLab, HSL, HSV, and LCH selection semantics,
  the eight overlapping feathered hue regions, legacy single-channel projection, and selective
  cyan/magenta/yellow/black order. Direct harmony execution retains analogous, complementary,
  triadic, split-complementary, and tetradic nearest-anchor semantics. Color replacement and skin
  protection are now admitted into that same compiled Look program. The following retained user-LUT
  stage preserves canonical sequential stack order, per-member strength, arbitrary valid cube
  domains, working-to-input and output-to-working matrices, sRGB, gamma 1.8/2.2/2.4, BT.2020, and
  ACEScct transfer semantics, alpha, and neutral-axis stabilization. Immutable table and program
  identities reuse unchanged Metal buffers instead of uploading the stack on every render. A CPU
  proof covers a two-member sRGB/Display-P3 then ACEScct/ACEScg stack with non-unit domain and
  partial strengths; the physical member carries the same stack and compares it with the canonical
  unfused Look when a device exists. The evaluator remains in the 251-line LUT runtime below its
  frozen 328-line ceiling, while validation, compilation, and identity live in a separate 243-line
  owner capped at 260 lines.
  This audit also closed a fail-open omission: the user LUT and LUT stack live in pipeline state,
  outside the desktop post-color predicate, so the retained path could once admit a LUT-bearing
  request and omit it. Supported payloads are now compiled and admitted; invalid shapes, non-finite
  tables, and unsupported input/output color spaces remain fail-closed as
  `unsupported_stage:Look:user_lut`, while advanced post-color remains
  `unsupported_stage:Look:exact_post_color`. After replacing
  function-local Metal constant arrays with compiler-portable scalar selection, `Framehouse`
  compiles and links, the architecture boundary and `git diff --check` pass, and the focused
  executor/color/LUT/contract/invariant/render slice has seven runnable passes plus the explicit
  `no_system_metal_device` skip in 4.19 seconds. The physical member's representative six-kernel
  state now combines replacement, skin protection, and the ordered LUT stack with exposure, Tone,
  Curves, mixing, harmony, and grading, but this host also lacks the standalone `metal` compiler,
  so shader compilation and
  pixel execution remain target-hardware evidence rather than a local claim. The complete
  controller gate is not recorded as
  passed: its current-tree run timed out after 1,506.40 seconds in the repeated large-mask undo/redo
  stress case with a full render still running. That run preceded replacement/skin admission and
  this host exposes no Metal device, so it does not demonstrate a Look pixel mismatch, but it
  remains a red application-lifecycle gate that must be
  isolated and fixed. The common Look seam therefore advances, but does not close, the broad GPU
  residency checkbox.
  The subsequent combined-stage audit found that production command encoding dispatched the new
  Look kernel before Curves and global Tone, reversing the canonical graph even though each stage's
  isolated parity test passed. The host now encodes `Tone -> Curves -> Look` after exposure. The
  hardware-independent source contract locates the actual pipeline-state dispatch statements after
  command-buffer creation and requires that strict order, preventing initialization symbols or
  per-stage tests from masking another stacking regression. The app links and the contract,
  canonical working-fusion, and render-parity tests pass after the correction. Physical combined
  execution remains explicitly unproved until the skipped Metal member runs on a real device.
  A subsequent Geometry admission audit found another fail-open boundary: vertical/horizontal
  perspective, rotation, scale, skew, and liquify were absent from the retained fallback policy even
  though the retained backend did not execute their resampling stage. The audit first made every
  omitted operation fail closed, then replaced that temporary restriction with an exact retained
  Geometry program. `CompiledGeometryStageProgram` validates and clamps the complete geometry state
  once and supplies the same distortion, chromatic-aberration, perspective, transform, liquify,
  center/radius, dimensions, and mirrored crop-fill values to the CPU float reference and Metal
  uniforms. The external and embedded `apply_geometry_stage` kernels preserve canonical operation
  order, bilinear sampling, clamp-versus-mirrored edge behavior, separate red/blue radial samples,
  and alpha clamping. They read the post-LUT working buffer and write one distinct retained device
  buffer, so resampling cannot race in place; that buffer becomes the published surface without a
  CPU readback. The source contract requires Geometry after Tone, Curves, Look, and the ordered user
  LUT stack. A 9-by-7 hardware-independent proof exercises every geometry family and crop-fill mode
  against the canonical CPU stage within `2e-5` per channel, plus non-finite fail-closed validation.
  The physical member now carries the same representative geometry state as a seventh ordered
  kernel and requires fourteen cumulative dispatches across the unchanged-payload reuse render.
  Geometry controls are admitted only after those proofs; Lens-vignette remains outside this suffix
  because it is already executed in Camera Characterization. The new compiler/reference owner is
  216 lines under a frozen 260-line ceiling and its public contract is 44 lines under a 60-line
  ceiling. The current executor/color/LUT/contract/invariant/render slice has seven runnable passes
  plus the explicit `no_system_metal_device` skip in 3.53 seconds; architecture and diff checks pass.
  The later full `Framehouse` rebuild compiles and links all 172 remaining application/QML-cache
  steps after Geometry and retained Detail integration. The prior unrelated dirty-tree
  `AgentEditController::importPhotographerTastePassportFile` declaration/definition mismatch is no
  longer present in the current tree.
- [~] Avoid full-frame copies for visible tiles, dirty masks, small layer edits, and output-only
  changes.
  Localized ACES brush edits now copy only affected float working-pixel rectangles, execute the
  photographic-output suffix only for those rectangles, and can publish the exact brush-effect
  texture patch without copying a rendered full-frame base. The focused parity gate also covers
  chained and four-tile edits. Other layer, output-only, proof/device, and unsupported-spatial
  paths still require full-frame work, so this item remains open.
- [x] Prewarm camera transforms, display transforms, and common output LUTs after source/monitor
  selection without delaying first feedback.
  Asset selection and every input/display-transform intent now enqueue an immutable-identity
  background prewarm rather than doing compilation on the UI thread. The job populates the same
  production caches used by rendering: official ACES 2 interactive/final output LUTs, input ICC,
  monitor ICC and measured Metal ICC LUTs when hardware is available, plus DCP camera render
  plans. DCP plans now use a concurrency-safe 16-entry LRU keyed by immutable profile SHA-256,
  source temperature, and tint, eliminating the previous per-render spline/table-plan rebuild.
  Prewarm requests expose scheduled, resident/pending deduplicated, completed, failed, and pending
  counts plus the last exact key in color diagnostics; only two completed compound identities are
  retained so this admission memory cannot outlive the smallest downstream transform cache.
  `dcp_profile_test` proves single-build reuse and real eviction, the controller regression proves
  asynchronous completion and exact-key deduplication, and the complete controller and render
  parity suites pass.
- [x] Keep the exact CPU path as the correctness reference and bounded fallback.
  `ExactCpuReference` remains an explicit canonical-plan requirement and `CpuOnly` executes the
  same stage plan used by preview and export. `Auto` may fall back only with a named availability
  or `unsupported_stage` reason; `GpuOnly` fails closed instead of silently changing the requested
  backend. CPU reference tests cover extended-range pixels, custom ICC, soft proof, RAW input
  boundaries, local masks, tile seams, and final export, while the physical Metal/CUDA comparison
  gates below remain open until supported devices are available.
- [x] Classify every backend fallback by exact unsupported stage and expose it behind diagnostics.
  Both the shared desktop renderer and the native preview path now emit the stable
  `unsupported_stage:<Stage>:<detail>` contract for CPU-required Layers, NormalizeRaw,
  SceneAdjustments, Tone, Look, Detail, and Geometry features. Availability and build failures
  remain separately named (`metal_unavailable` and `metal_not_compiled`) because no render stage
  was attempted. The reason is carried by render telemetry into the existing preview/export
  diagnostics payload instead of being collapsed to a generic full-pipeline fallback. The parity
  regression proves distinct reasons for local mask sets, retouch masks, legacy local masks,
  extended-range pixels on the older non-fused route, exact global tone cases outside the compiled
  contract, and exact post-color cases outside the retained Look contract. Texture, clarity,
  dehaze, sharpening, both
  noise-reduction controls, and HDR local tone are now admitted in the canonical preview fusion
  only through their exact retained programs.
  The retained working-fusion policy no longer re-runs camera-linear highlight-clipping analysis or
  rejects every signed/over-one channel after Camera Characterization: both checks belong to the
  already-consumed RAW prefix and were preventing ordinary extended-range RAW working pixels from
  reaching the exact retained path. Compiled Tone admission now validates finite RGB and the actual
  sampled luminance stop domain instead of imposing a display-referred `[0,1]` channel box. A
  regression admits characterized pixels containing `1.42` and `-0.04` channels while rejecting a
  true `1e9` stop-domain overflow. The older non-fused support query deliberately keeps its
  extended-range fallback because it does not carry the characterized-working-surface contract.
  Layers now has a first exact retained subset instead of an all-or-nothing exemption.
  `CompiledFillLayerProgram` accepts at most sixteen ordered, enabled simple layers: fills,
  pixel/text/source rasters, and core tone/color adjustment layers, including center-based raster
  scale/rotation, masks, and
  clipping-to-previous-layer alpha. Raster transforms use the authoritative inverse transform,
  `0.05...32` scale clamp, canvas placement, bounds, and nearest-neighbor sampling semantics. The
  program implements Normal, Multiply, Screen, Difference, Lighten, Darken, Overlay, Soft Light,
  Hard Light, and Linear Light with the authoritative creative blend headroom. Mask bytes are
  packed once with exact nearest sampling, inversion, density, and opacity semantics, then retained
  in a bounded 16-entry/64 MiB Metal LRU keyed by immutable content identity. Raster pixels are
  packed into the same ordered program and retained separately in a bounded 8-entry/512 MiB LRU,
  so unchanged masks and raster payloads are not re-uploaded. One kernel executes the mixed stack
  after the LUT stack and before Detail. Adjustment Exposure applies universal scene-linear gain;
  Contrast, Highlights, Shadows, Whites, and Blacks use the same immutable 16,385-sample compiled
  luminance-tone contract as the retained global Tone stage. Temperature and Tint embed the
  authoritative working-space white-balance matrix before exposure/tone, while Vibrance and
  Saturation use the same compiled OKLab matrices and control math as the retained Look stage. Hue
  Shift, Saturation Shift, and Colorize use a separate extracted CPU reference plus matching Metal
  OKLab implementation because their authoritative local-color branch deliberately differs from
  the global Look branch when controls are combined. Brightness, Filmic Shoulder/Toe, Adaptive
  Contrast, Levels Black/Gamma/White, and all five tonal zones execute in a second compiled
  luminance program after local color, preserving the canonical layer recipe order. Filmic
  Shoulder is baked into that immutable program with its monotone signed shoulder math and a new
  content identity. Combined layer-tone payloads use a separate bounded 8-entry/16 MiB Metal LRU.
  Adjustment-layer Curves now compile the four parametric tonal controls plus master, luminance,
  red, green, and blue point curves into the same immutable 4,097-sample contracts used by the
  retained global Curves stage. Only active curve blocks are packed; per-layer offsets and active
  masks preserve the canonical `advanced tone -> curves -> downstream color` order, and an
  independent bounded 8-entry/16 MiB Metal LRU prevents unchanged curve payloads from being
  uploaded on subsequent renders. The integration exposed a legacy stacking defect in the
  canonical CPU Layers stage: the presence of a master point curve caused parametric Shadows,
  Darks, Lights, and Highlights to be ignored. The canonical implementation now composes the
  parametric curve first and every point-curve family afterward, matching global curve semantics
  instead of preserving that silent suppression. The downstream layer Look program now carries the
  same compiled RGB mixer, black-and-white mixer, per-channel and selective HSL/HSV/OKLab/LCH
  mixers, color replacement, harmony, three-way grading, and protected-skin semantics as the
  canonical CPU Layers stage. It executes after layer curves in canonical order and finite-validates
  every matrix, scalar, range, and compiled payload before admission. Active programs are packed as
  one fixed Look record per stack slot and retained in a separate bounded 8-entry/1 MiB Metal LRU;
  immutable reuse therefore performs no second layer-Look upload. Effective user LUTs remain
  rejected here because the retained LUT program still belongs to the global stack rather than an
  individual adjustment layer. Layer lens-vignette and dehaze are now the first exact retained
  effects/optical subset. Vignette uses the canonical normalized image radius, edge smoothstep,
  signed gain, and floor; dehaze uses the canonical working-space OKLab chroma scale and
  output-headroom fit. Both run after downstream color and before mask opacity and blending. Their
  working matrices and output headroom are compiled into the immutable layer program, and explicit
  host/Metal size and offset assertions lock the expanded uniform layout. Because the complete
  sixteen-layer uniform block
  exceeds Metal's safe inline-byte range, it is now bound through an explicit shared buffer with a
  named fail-closed allocation error rather than relying on an oversized `setBytes` call that could
  fail only on physical hardware.
  The compiled CPU Layers reference has also moved from pixel-major to layer-major execution, which
  preserves clipping-alpha semantics while creating the full-frame intermediate required by
  neighborhood operators. On that boundary, layer Texture and Clarity now use the canonical guided
  luminance filters at radii 2 and 8 with their exact epsilon, guard, and luminance-rescaling math;
  layer Sharpening, sharpening masking/radius/detail, luminance NR, and color NR reuse the exact
  compiled fine-Detail program. The six-layer CPU comparison exercises both spatial and fine layer
  Detail and remains within the `0.004` aggregate bound. The external and embedded Metal
  sources now contain matched square/correlation, coefficient, and compose kernels for the exact
  guided-filter equation, and backend initialization requires all three pipeline states. They are
  intentionally not treated as completion evidence yet. The retained Layers kernel and host now
  share an explicitly size-checked range protocol, GPU-owned persistent clipping-alpha buffer, and
  GPU-only adjustment intermediate. Whole-stack renders use the same protocol with `[0, layer_count)`;
  source-only and composite-only modes provide the exact split point around a neighborhood pass
  without changing the established path. The scheduler now splits arbitrary stacks around every
  Texture/Clarity layer, runs exact radius-2 and radius-8 guided filters entirely in private GPU
  buffers, applies the shared spatial-detail kernel to the intermediate, and then resumes mask,
  opacity, clipping-alpha, blend, and subsequent layers. The same scheduler now runs the retained
  OKLab extraction, three-pass Gaussian approximations, Sharpening/masking, luminance NR, color NR,
  and OKLab composition against that intermediate in canonical order. Ordinary stacks retain their
  single Layers dispatch. The same retained layer-major boundary now compiles every adjustment-layer
  resampling control through the canonical Geometry program and executes distortion, chromatic
  aberration, vertical/horizontal perspective, rotation, scale, skew, liquify, and mirrored
  content-aware crop fill after point effects and before Detail. The Metal scheduler reuses the
  existing geometry kernel and a private intermediate rather than introducing a second resampler.
  Hue, Saturation, Color, and Luminosity blend modes are also admitted through the canonical OKLab
  component-transfer and gamut-fit policy instead of being approximated as channel blends. The CPU
  reference exercises all four perceptual modes and a masked geometry/Detail stack against the
  canonical Layers stage. While adding the gamut-policy uniform, the audit found that the external
  `.metal` `FillLayerUniforms` declaration lacked the range fields already read by its kernel and
  present in the embedded source; both shader sources and the host size/offset assertions now carry
  the same range, split-mode, and perceptual-gamut fields. Physical-device execution is still
  required before this complete layer chain can be called hardware-proven.
  Adjustment-layer user LUTs now compile through the same ordered user-LUT program as the global
  stack, including declared input/output spaces, transfer functions, domains, strengths, immutable
  identities, and malformed-payload rejection. CPU execution applies the retained LUT sequence
  after downstream Look and before point effects/Geometry/Detail. Metal uses the existing bounded
  LUT cache and dispatches each layer LUT in place on the private layer intermediate, so immutable
  LUT bytes upload once and the layer path introduces no GPU readback or CPU publication copy.
  These admitted controls then obey the authoritative adjustment
  opacity, mask, clipping, blend, and alpha rules; notably, a Normal adjustment preserves
  destination alpha rather than treating it as raster source alpha.
  A hardware-independent six-layer masked/inverted Normal fill, clipped Screen fill, Soft Light
  fill, clipped tone/color/curve/downstream-Look/vignette/dehaze/Texture/Clarity adjustment,
  black-and-white adjustment, and
  positioned scaled/rotated raster Multiply
  reference matches the canonical Layers stage within the compiled-curve `0.004` maximum linear
  channel bound while exercising every admitted adjustment control. The curve-only compiler proof
  separately covers black, extended-range chromatic samples, stable payload identities, alpha
  preservation, and all six curve families. Malformed mask shapes, non-finite transforms,
  non-finite curve points, non-finite adjustment controls, and non-representable exposure gains
  fail closed, and the source contract locks the full
  `Tone -> HDR local tone -> Curves -> Look -> LUTs -> Layers -> Detail -> Geometry` dispatch order.
  Over-cap stacks remain `unsupported_stage:Layers:photo_layers`; the compiler and policy fail
  closed. The physical parity member requires 138 dispatches per render and 276 across immutable
  payload reuse, and its
  telemetry contract requires the four-byte mask, four raster pixels, two layer-tone LUTs, six
  active layer-curve LUTs, one eight-entry adjustment-layer color LUT, and six layer-Look uniform
  records on first upload with no repeat mask/raster/tone/curve/color-LUT/Look upload on reuse.
  That member remains unexecuted here
  because this environment
  reports `no_system_metal_device`, and `xcrun -f metal` confirms that the standalone shader
  compiler is also unavailable; no physical-device or offline-shader result is claimed. The rebuilt
  focused nine-test slice has eight runnable passes plus that explicit skip;
  architecture, source-contract, render-parity, layer-controller, and diff gates pass, and the
  complete `Framehouse` target links. This closes retained Layers semantic residency for every
  representable stack within the explicit sixteen-layer cap; physical Metal execution and the
  broader end-to-end graph-residency gate remain open.
- [x] Enforce cancellation inside decode, transforms, blur/detail, masks, output packing, encode, and
  publication.
  A live persisted-job cancellation flag now feeds the same `CancellationToken` used by the
  canonical color executor. RAW decoding installs LibRaw's progress callback and checks every
  app-owned cleanup/suppression row; native, OpenEXR, and Qt decode boundaries discard canceled
  results before cache insertion. The token continues through graph execution, color transforms,
  detail/blur, mask work, working-row packing, and device publication. PFM and ordinary
  JPEG/PNG/TIFF writers use cancellation-aware transactional devices; OpenEXR checks validation,
  conversion, and every encoded scanline; HDR AVIF checks validation and plane conversion rows plus
  both sides of the codec call. Codec calls that provide no interruption callback are isolated to
  a hidden same-directory staging artifact which is never renamed after cancellation. Final
  delivery publication is an atomic same-volume rename followed by cancellation-aware integrity
  verification, and every canceled/failing path removes the staging file, final file, and sidecar.
  Pre-canceled RAW decode/cleanup tests preserve caller sentinels, preview cancellation after a
  real JPEG encode leaves no artifact, and the export integration test cancels a running
  2048-by-2048 detail render and requires `Canceled` with no delivery, sidecar, or hidden partial.
  `raw_camera_linear_decoder_test`, `editor_preview_controller_test`,
  `export_job_controller_test`, and the warning-clean desktop build pass.
- [x] Keep first-visible feedback, draft completion, settled completion, full-resolution restore,
  texture upload, GUI delivery, and QML frame time as separate measurements.
  Render telemetry emits distinct `tool.stage.completed` records for first feedback, draft,
  settled, and full-resolution restore with queue and render duration kept separate. Native texture
  upload and image-provider publication are independent `render.stage.completed` records with
  upload/publish duration; worker-complete-to-GUI handling delay is a separate event. QML frame
  interval, jank, and drop summaries remain a UI-thread measurement rather than being folded into
  render time. `preview-lifecycle-measurements:v1` exposes the exact event/stage contract and its
  render versus QML correlation keys, and the controller plus bootstrap observability tests lock
  the schemas. These measurements are complete; the target-hardware percentile budgets below
  remain release gates and are not inferred from unit-test timing.
- [x] Add memory ceilings for working buffers, tile caches, LUT caches, RAW decode, thumbnails,
  scopes, masks, exports, and repeated document switching.
  Preview and export now share a 1.5 GiB per-working-surface admission limit and reject oversized
  full-resolution work before allocating the canonical `ColorPixel` surface. LibRaw independently
  enforces its 1.5 GiB decoder limit. Decoded, characterized, adjusted, rendered, rendered-tile,
  executor-surface, foreground-map, mask-tile, scaled-mask, composite-mask, active-mask-preview,
  display-texture, and thumbnail-entry stores all expose their current bytes and hard ceiling in
  `preview-memory-ceilings:v2`. The thumbnail LRU now has a 128 MiB conservative heap estimate in
  addition to its old 256-entry limit, closing the count-only large-entry hole. ACES output and
  measured ICC LUT caches now reject an entry above 32 MiB and evict by both LRU count and aggregate
  bytes. Histogram/scopes retain fixed 128-column, 64-level, and 1,800-point shapes rather than
  source-sized storage. The export runner admits one photo job at a time and its working surface,
  encoder staging file, and transactional delivery lifetime are bounded and observable separately.
  A focused cache stress requires thumbnail byte eviction and complete accounting release; a
  two-cycle switch across the same four documents requires the second tracked resident total not
  to grow and verifies every reported category is at or below its ceiling. The complete preview,
  export, RAW, OpenEXR, HDR AVIF, and desktop build gates pass.

### Required budgets

| Workflow | Completion budget |
| --- | --- |
| Slider command acknowledgement | Same UI frame |
| First visible edit feedback | p50 `< 16 ms`, p95 `< 33 ms` |
| Draft color preview | p50 `< 33 ms`, p95 `< 50 ms` |
| Settled 2560 px preview | p50 `< 150 ms`, p95 `< 250 ms` |
| Brush overlay | p95 `< 16 ms` |
| Composited mask draft | p95 `< 80 ms` |
| Output-target-only update | p95 `< 100 ms` after transform cache warmup |
| Stale result rejection | Zero stale publications |
| Repeated edit memory | Bounded plateau; no monotonic unowned growth |

Budgets are release gates on named target hardware classes and representative media. Debug
microbenchmarks remain diagnostic and cannot promote a release.

The controller now avoids an expensive draft when a tone/color or display edit already has a
native texture covering the expected full output and that draft could not replace the retained
texture. It schedules the canonical full request immediately, while forced mask drafts retain
their dedicated path. Pending watcher replacements also receive a post-completion dispatch, so a
visible-tile prepass cannot leave its background render permanently pending. Dense exact-tone
renders reuse a bounded, dependency-keyed compiled log-luminance lookup table across preview and
final requests, and draft histogram work remains deferred. These changes pass the focused
determinism/parity gates, but the Phase 7 item and budgets remain open until representative
release-hardware latency distributions and bounded-memory plateaus are recorded.

The adjustment engine no longer clones the complete working surface merely to prepare an
edge-aware matte context when a recipe contains no local mask or mask set. The copy is now
allocated only for consumers that require an immutable pre-adjustment source. Debug CPU
microbenchmarks improved, especially for the 1920x1280 simple-global case, but still miss the
interactive budgets by a wide margin; this optimization is not represented as completion of the
GPU-residency or latency gates.

The canonical CPU prefix now reuses its prepared camera-linear highlight-reconstruction analysis
instead of scanning the same RAW frame again in NormalizeRaw. Exact identity predicates also skip
pixel math for NormalizeRaw, camera characterization, and scene exposure when their compiled
parameters cannot change a pixel; public stage entry points still perform their finite-surface
contract check. The benchmark event now records normalize, camera-characterization, scene,
tone, curve, and look timings so future work targets measured hot stages. On the working tree based
on `13dcbbf794a0`, the focused Release simple-global diagnostic changed from a p50/p95 of
`138/151 ms` to `57/63 ms` at 1920x1280 and from `51/55 ms` to `14/16 ms` at 960x640. A clean
target rebuild after extracting the identity predicates behind their own runtime boundary repeated
at `70/82 ms` and `21/22 ms`, respectively; both runs materially beat the pre-change binary and
the spread remains recorded rather than selecting only the faster sample. The broader spatial/detail
diagnostic recorded `179/329 ms` p50/p95 at 1920x1280, with high first-run variance; it remains
outside the release gate. The post-change artifacts are
`/private/tmp/framehouse-color-release-after/desktop.jsonl`,
`/private/tmp/framehouse-color-release-final/desktop.jsonl`, and
`/private/tmp/framehouse-color-release-spatial-after/desktop.jsonl`. Verification used the focused
benchmark target plus `color_pipeline_property_test`, `color_tool_invariant_test`, and
`synthetic_raw_pipeline_invariant_test`; all three correctness tests passed in 4.03 seconds. These
are bounded local diagnostics, not target-hardware promotion evidence.

The ACES Look stage now returns before OKLab conversion when no declared look control can change a
pixel and the ACES DRT owns highlight colorfulness. The legacy renderer still evaluates its
automatic highlight-chroma behavior, and positive dehaze chroma is now classified as real work
rather than accidentally satisfying the no-look predicate. The stage wrapper also skips its
otherwise identity traversal when RGB mixing, monochrome, post-tone color, and user LUTs are all
inactive, while retaining one finite-surface validation pass. Hue conversion and identity
classification moved behind focused runtime files; `color_look_runtime.cpp` decreased from 411 to
364 lines rather than raising its frozen ceiling. In the uncontended focused Release diagnostic,
the 1920x1280 simple-global result moved from the preceding `70/82 ms` repeat to `51/55 ms` p50/p95,
and the 960x640 result moved from `21/22 ms` to `12/14 ms`; the artifact is
`/private/tmp/framehouse-color-look-final/desktop.jsonl`. A later host-contended repeat is retained
at `/private/tmp/framehouse-color-look-stage-repeat/desktop.jsonl`: Look correctly reports zero
milliseconds, but the unchanged Curve stage rose from roughly `21 ms` to `203 ms`, so that run is
explicitly invalid for end-to-end promotion. `color_pipeline_property_test`,
`color_tool_invariant_test`, and `render_golden_test` pass, and a new property proves the identity
shortcut preserves pixels exactly and still rejects a non-finite surface. Curve evaluation is now
the next measured CPU hot stage.

Curve execution now lives behind `color_curve_stage_runtime.cpp`, reducing the coordinator rather
than extending it. A recipe without curve controls takes an exact finite identity path, while
active curves retain the established evaluation math. The neutral modern-ACES Tone stage now does
the same: it preserves finite signed and extended scene-linear channels and alpha bit-for-bit,
does not perform an implicit headroom fit, rejects non-finite input, and records zero Tone work.
The focused Release exposure-only diagnostic improved from `5/6 ms` to `2/3 ms` p50/p95 at
960x640 and from `19/24 ms` to `11/14 ms` at 1920x1280 across ten iterations. Curves, Look, and
Tone all report zero work for this recipe. `color_pipeline_property_test`,
`color_tool_invariant_test`, `synthetic_raw_pipeline_invariant_test`, and `render_golden_test`
pass; the latter three completed together in `0.91` second. This closes the Gate 1 code contract,
not Gate 3 target-hardware performance or Gate 4 real-RAW approval.

The optimized Release `canonical_native_working_fusion_test` passes in `0.44` second. The physical
`metal_aces2_output_lut_parity_test` still reports `no_system_metal_device`: a direct invocation
returns the configured skip code `77`, and a bounded CTest rerun with `--timeout 10` records the
skip normally in `0.07` second. An immediately preceding combined CTest invocation printed the
same skip but reported a false 3,613-second timeout after the host clock advanced discontinuously;
it is retained as harness noise and is not hardware or latency evidence. Future physical probes in
this environment must keep an explicit short timeout. No actual Metal parity gate is closed by a
skip.

Typed suffix execution also no longer copies a resumed full-frame surface at the request,
executor, and stage-callback boundaries. `RenderExecutionRequest` is consumed by value, its
initial surface is moved into the executor, and each callback owns and forwards that allocation.
The executor contract test checks pointer identity through the move. This removes avoidable CPU
memory traffic, but it is not substituted for release-hardware latency or resident-memory proof.

The reduced Metal-compatible preview/export route now encodes its mask adjustment and global
color LUT into one command buffer against one shared pixel buffer. The former implementation
called two backend methods, allocating, uploading, waiting, reading back, and re-uploading the
complete frame between those stages. The fused operation reports one command buffer, one or two
kernel dispatches according to mask use, exact CPU-to-GPU/GPU-to-CPU byte counts, and a named
failure reason; `Auto` retries through the exact executor while `GpuOnly` fails closed. Preview,
export, engine stages, and backend capability probing now share one Metal backend instead of
constructing four independent command queues and resource caches. Its compatibility color-LUT
buffers use a 4-entry/4 MiB aggregate LRU keyed by the immutable CPU LUT build identity, so a warm
edit uploads only the changed pixel surface; those bytes and ceilings are included in
`preview-memory-ceilings:v2`. The Metal device regression requires one upload and one readback for
the first two-dispatch masked case and no repeated LUT upload on reuse, and the source contract
locks the integration. This is one removed round trip and one removed duplicate-backend family,
not completion of the open end-to-end residency item: the route still uploads its input, waits
synchronously, and reads a full CPU surface back before later output stages. This host reports
`no_system_metal_device`, so its physical execution remains a target-hardware gate.

The next retained-surface cutover removes that compatibility readback from the eligible
in-memory preview route. `RenderSurface` now has a backend-neutral, byte-accounted
`RenderSurfaceBacking` contract with cancellation-aware CPU materialization, and
`MetalWorkingSurface` owns the adjusted RGBA32F buffer, producer command buffer, shared-event
readiness fence, device identity, and exact descriptor. The reduced mask/color producer stores
that surface in the adjusted working-image cache instead of immediately copying it back. Native
sRGB publication dynamically admits the Metal backing and encodes its wait on the producer event,
so the output LUT reads the retained buffer directly and the preview texture receives the deferred
`MetalOutputSurface` without an intervening GPU-to-CPU or second pixel upload. Cache accounting
includes whichever CPU or native representation is resident. If comparison, diagnostics, pixel
inspection, proofing, visible scopes, tiling, disk output, or another CPU consumer is active, the
same object performs one exact, cancellation-aware materialization, replaces its retained cache
payload with the CPU pixels, and reports the forced GPU-to-CPU bytes rather than hiding the
fallback. `render_parity_test` proves native-only validity, exact one-time materialization,
extended/signed pixel preservation, stable byte accounting, and cancellation without readback;
the Metal source contract locks the producer, cache, admission, and chained-output calls. The
`render_executor_test`, Metal source contract, and shipping render parity test pass 3/3; the
physical Metal member remains explicitly skipped on this `no_system_metal_device` host. The
complete Framehouse target compiles and links after this cutover. This closes the reduced
Metal-compatible preview round trip, but not the phase item: the canonical executor suffix still
materializes its working surface before PhotographicOutput, custom ICC/proof/HDR paths remain CPU,
and physical-device residency and latency evidence remain open.

ACES processor and output-LUT compilation is now single-build per cache key. Previously, a burst of
eight catalog thumbnail workers could all miss before insertion and independently build the same
official processor and 97-cube final LUT; the latter duplicated more than 900,000 samples per
worker, producing avoidable cold-start latency and transient memory. The processor and LUT caches
now serialize the first construction for a key, after which waiting workers receive the same
immutable object. Direct eight-worker tests require one miss, one build, seven hits, one resident
entry, and the exact bounded LUT byte count. This closes the cold-cache herd, not the broader
compile-once, prewarm, GPU-residency, or release-hardware budget items.

The older controller-facing 33-cube global-color LUT cache is no longer an unbounded-payload,
single-entry check-then-build cache. It is a four-entry LRU; a miss remains locked through
construction so concurrent callers cannot duplicate the same build, and retained key states
discard mask, retouch, layer, and creative-LUT pixel payloads that the global LUT never reads. The
current focused parity/golden suite stayed green. This bounds the compatibility cache while it
remains in use; it does not cancel the Phase 11 requirement to delete the standalone legacy LUT
route after full graph/GPU migration.

The earlier standalone Metal Detail repair made successful GPU execution rejoin Geometry instead
of returning early, but a later retained-pipeline audit found that this was still not a safe parity
route. The standalone spatial kernel hard-coded legacy luminance behavior and diverged from the
current texture/clarity/dehaze guards; the fine-detail kernels assumed the legacy fixed RGB/OKLab
context, accepted only radius `1` and zero Detail, and performed complete upload/wait/readback
cycles. That path could therefore make a Metal-capable preview differ from CPU/final output even
though its stage ordering was repaired. Canonical Detail no longer calls either approximate
backend method. Texture, Clarity, and Dehaze are now admitted through
`CompiledSpatialDetailProgram`. It carries native-working-profile luminance coefficients, scene
headroom, exact strengths, dimensions, and an active-operation mask. The retained command buffer
extracts luminance, performs one separable box blur at each active canonical radius `2`, `8`, or
`20`, applies the same local-activity, midtone, shadow/highlight, broad-edge, structure, haze, and
lifted-shadow colorfulness guards, and then continues into Geometry without readback. A 13-by-11
checker/gradient reference exercises all three signed controls against the authoritative
interactive CPU stage within `5e-5` per channel; invalid state fails closed. The source contract
requires retained Detail after Look/user LUTs and before Geometry.

Sharpening, luminance noise reduction, and color noise reduction are now admitted through
`CompiledFineDetailProgram`. The immutable program carries the authoritative working-profile
RGB/D65 matrices, signed OKLab conversion, arbitrary canonical sharpening radius, Detail gain,
masking threshold, noise strengths, dimensions, and active-operation mask. The retained command
buffer extracts working-space OKLab once, builds each sequential three-pass Gaussian dependency
with the shared scalar box kernels, applies the canonical `sharpen -> luminance NR -> color NR`
order, reconstructs directly to extended-range working RGB, and continues into Geometry without
upload, wait, readback, or premature output-gamut fitting. A 13-by-11 chromatic checker/gradient
reference exercises all three controls against the authoritative CPU stage within `5e-4` per
  channel; non-finite state fails closed.

HDR local tone mapping is image-dependent Tone work, not Detail work, and now has its own
`CompiledHdrLocalToneProgram` rather than being mislabeled as a Detail fallback. The program carries
the working-profile luminance coefficients, exact positive strength, scene headroom, dimensions,
and canonical image-dependent radius. Directly after the retained global-tone LUT and before
Curves, one kernel extracts log luminance, the shared scalar kernels execute the exact three-pass
Gaussian approximation, and a final kernel applies the canonical local-excess compression and
detail-preservation equation before scaling working RGB. A bright 13-by-11 chromatic reference
compares the staged compiled global/local Tone result with the authoritative combined CPU Tone
stage within `1e-3` per channel; invalid/non-finite state fails closed. The strict source contract
requires `Tone -> HDR local tone -> Curves`. With every representative Tone, Curves, Look, LUT,
spatial/fine Detail, and Geometry control active, the configured physical member now requires 64
kernel dispatches per render and 128 across immutable-payload reuse.

The obsolete standalone backend methods, public declarations, private pipelines, host uniforms,
shader helpers, and seven old kernels are physically deleted from both the external and embedded
Metal sources. Architecture and source-contract gates reject every retired API/pipeline/kernel
token and require retained fine/spatial Detail before Geometry, preventing the approximate route
from returning. The focused executor/color/LUT/contract/canonical-fusion/invariant/render/parity/
golden suite has eight runnable passes plus the explicit `no_system_metal_device` skip in 5.22
seconds; architecture and diff checks pass. This proves hardware-independent compilation and CPU
reference equivalence on this host. It does not substitute for compiling and executing the MSL on
a physical Metal device, so that hardware gate remains open.

The CPU detail reconstruction no longer calls an output-gamut fitting helper when converting
sharpened/noise-reduced OKLab buffers back to linear working RGB. Detail is not an output boundary:
premature fitting removed signed edge excursions and narrowed chromatic range before ACES. The
conversion is now direct and preserves alpha; the later shared working-headroom and declared
photographic/device output stages retain ownership of range and gamut policy. A focused chromatic
edge property fails if signed or over-range detail is clipped early.

Post-tone color math no longer has separate copied implementations for the fast global path and
the slow spatial path. Global, local-equivalent, and photo-layer color edits call the same kernel,
and both global and layer detail reconstruction remain unbounded until one final compatibility
gamut-fit pass. Engine telemetry records that pass count; a mixed global/local/layer recipe must
report exactly one. This removes a real repeated-compression source and establishes a usable
`Look` seam, but the final fit still lives inside the compatibility monolith until the production
executor owns the explicit photographic-output stage.

Perceptual ACES no longer enters that compatibility gamut-fit pass at all. The ACES output
renderer is already the declared gamut/colorfulness compression boundary, and the Metal LUT route
did not execute the CPU-only compatibility pass; retaining it on CPU therefore both compressed
color twice and created a backend discrepancy. Legacy/non-ACES rendering keeps one compatibility
fit, while ACES telemetry reports zero until photographic output. Final ACES render goldens remain
unchanged, demonstrating that this removes destructive intermediate compression rather than
silently changing the declared display result.

Render-request construction no longer serializes or hashes complete mask/layer payloads on the UI
thread. Interactive and settled requests use constant-time asset/edit/generation/mask/layer
revision identity for stale-result rejection; exact recipe/cache identities and layer-composite
alpha trace hashes are completed in the worker. Grouped mask presentation retains constant-time
provider and full-resolution-availability facts but marks the exact alpha trace deferred instead of
reading matte bytes synchronously. When a current full-resolution native texture already covers the
viewport, an edit publishes through the visible-overlay worker first and leaves the settled full
render queued behind it. This preserves immediate visible feedback without allowing a lower-quality
draft to replace the native texture. The focused feedback, outline, layer-adjustment, thumbnail
coordinator, thumbnail cache, and thumbnail presentation slice passes 6/6 in 53.06 seconds. The
complete controller suite, including the unchanged 12-second 10,000-dab undo/redo stress gate,
passes, and the architecture boundary plus complete Framehouse QML cache and executable build pass.
These are local behavioral proofs; target-hardware latency and GPU residency gates remain open.

Full-resolution active-mask outline evaluation no longer runs composite alpha, brush-matte replay,
morphology, or contour tracing from the QML-facing getter. Requests above 512 by 512 pixels carry
revision identity into a dedicated single-thread worker, coalesce newest pending work, reject stale
results, and publish exact trace/cache identity only after completion. The immediate brush fallback
uses the already sampled vector path; it no longer rebuilds and traces a hidden 2048-pixel matte,
which was measured at 190-330 ms on this host before removal. The shared contour tracer now derives
its threshold from the actual final-alpha maximum, capped at the prior 96/255 threshold with an
8/255 noise floor, so valid low-range refined or low-opacity masks no longer disappear merely
because their peak alpha is below 96. The focused outline proof requires admission within 33 ms,
nonempty temporary feedback, an exact 800-by-600 `composite-alpha` worker result, cache publication,
a non-UI worker thread, and zero stale publications. The affected thumbnail/feedback/outline/layer
slice passes 6/6 in 122.28 seconds; the outline test's additional exact full-resolution worker case
accounts for its increase from approximately four seconds to 67.35 seconds. This closes the local
UI-thread outline defect. The complete controller suite and full Framehouse application compile and
link pass after the cutover; this still does not close the target-hardware Phase 7 release budgets.

### Exit gate

- All required budgets pass on supported minimum and reference hardware.
- Performance artifacts include color parity, visual proof, GPU counters, fallback reasons, and
  memory together.
- A skipped GPU path cannot satisfy the release gate.
- The published preview is never older than the active recipe, target, source, mask, or viewport
  identity.

## Phase 8 — Decompose the desktop application

### Work

- [~] Reduce `EditorPreviewController` to a coordinator over focused services:
  `PreviewSession`, `RenderScheduler`, `RenderPlanStore`, `PreviewPublisher`,
  `DisplayProfileService`, `MaskSession`, `MaskWorkerCoordinator`, `LayerSession`, `PresetService`,
  and `HistoryService`. All ten named services are now real. `DisplayProfileService` owns the authoritative monitor
  snapshot, generation and cache identity, same-display failure retention, reconciliation, and
  user calibration settings. Platform ICC discovery, payload validation, Windows WCS lookup, and
  macOS CoreGraphics lookup now live in `display_profile_discovery.h`; the controller only requests
  discovery and coordinates refresh notifications. `PreviewPublisher` now owns viewport-settle
  timing, event-loop coalescing, request canonicalization, patch-edge policy, duplicate suppression,
  and forced retry publication. Studio submits presentation geometry and the controller delegates
  publication to that service. `RenderScheduler` now owns the independent single-shot interactive
  and full-preview timers, delay state, rescheduling/coalescing behavior, cancellation, and stage
  callbacks. The controller supplies the admitted delay and coordinates render-stage work without
  constructing or manipulating either timer. `RenderPlanStore` now owns process-wide canonical
  admission, decoder provenance, plan compilation and serialization, concurrent same-key build
  suppression, and the bounded LRU; preview, export, catalog, Carousel, agent, job, and migration
  clients submit typed admission facts instead of owning a compiler or cache. The former free
  admission API and header are deleted. `PreviewSession` now owns the monotonic render generation,
  shared asynchronous cancellation identity, interactive/full stale-publication ordering, and
  published-preview revision. Asset switches, coalesced edits, layer supersession, render requests,
  completion handlers, diagnostic overlays, and histogram publication all consult that service;
  the controller no longer retains parallel generation or publication-order fields. `MaskSession`
  now owns the normalized mask stack, clamped active selection, preview mode, complete
  smart-selection/inpaint retry payload, mask-work revision, independent brush-raster,
  smart-selection, smart-preview, and inpaint generations, and cancellation telemetry. Mask edits
  and asset switches invalidate those identities through the service, and worker completion paths
  consult them before publishing. The same service now owns active-definition caching, the bounded
  active-preview LRU with hit/miss/eviction telemetry, outline paths and trace metadata, and keyed
  color-tool and smart-selection preview caches. `LayerSession` now owns the durable normalized
  photo-layer list, saved visibility snapshots, and copied adjustment-layer recipe. All layer
  reads, stack replacement, snapshot append/removal, paste eligibility, and clipboard publication
  delegate through that session; the controller no longer retains parallel layer, visibility, or
  layer-adjustment clipboard fields. `MaskWorkerCoordinator` now owns the five smart-selection,
  brush-raster, and inpaint `QFutureWatcher` instances, complete queued smart-selection preview and
  commit payloads, pending/dispatched state, latest job identities, and the shared brush-generation
  identity. The controller connects completion callbacks and submits work through the coordinator;
  it no longer owns those Qt watchers or payload containers. `ThumbnailRefreshCoordinator` now
  owns the shared mask/layer thumbnail timer and future watcher, coalesced pending and active job
  identities, monotonic generations, supersession cancellation token, completion publication,
  worker/defer/stale telemetry, diagnostic fields, and bounded stale-warning deduplication. The
  controller supplies thumbnail inputs without retaining any of the former twenty queue-lifecycle
  fields. It now also owns the ordered admission policy that keeps low-priority thumbnail work
  behind visible preview, coalesced edit, live brush, brush commit/raster, layer debounce, and
  smart-selection work. The controller supplies typed activity facts instead of constructing the
  blocker vocabulary and priority order. The direct coordinator test proves idle admission and a
  mixed full-preview/brush/smart-selection case; the controller translation unit and complete test
  target build after the extraction. `ThumbnailRefreshQueueService` now owns the revision-only state identity, complete
  immutable worker request and job construction, cancellation-token carriage, and ordered
  photo-layer-then-mask cache-entry publication. Request admission no longer computes a complete
  serialized edit-state hash only to overwrite it with the revision identity.
  `ThumbnailPresentationService` now also owns ordered mask-set derivation, source/dependency audit
  labels, combine-mode summaries and contracts, deferred alpha availability, and aggregate
  visibility/solo/preview state. It consumes narrow callbacks for canonical mask schema semantics;
  the controller no longer contains the 205-line grouping algorithm. The controller still supplies
  adjustment/mask rendering dependency callbacks, so the broader worker decomposition is not
  complete. The direct queue-service contract test proves
  deterministic revision identity, complete copied request snapshots, cancellation propagation,
  exact publication counts, and photo-before-mask collision precedence. It and the coordinator,
  cache-session, presentation-service, and interaction-feedback tests pass 5/5 in 21.29 seconds;
  the complete controller regression passes in 602.21 seconds and the 295-step Framehouse target
  compiles and links after the queue extraction. The extended presentation-service proof passes in
  0.84 seconds and covers source order, non-selection rejection, dependency provenance, alpha
  deferral, audit labels, combination policy, aggregate state, and fail-closed missing dependencies.
  Photo-layer adjustment-recipe schema, compatibility, canonical field selection, extraction, and
  atomic replacement now live in the focused 118-line `photo_layer_adjustment_recipe.cpp` owner.
  The thumbnail queue service consumes that owner and owns complete adjustment and non-adjustment
  recipe/cache-entry identities; `ThumbnailPresentationService` no longer accepts recipe-hash
  callbacks from the controller. Its direct proof verifies canonical-only extraction, preservation
  of layer identity, exact replacement/no-op behavior, recipe-versus-composition invalidation, and
  both legacy-read and global-recipe schema contracts. That test plus the queue and presentation
  service tests pass 3/3, the full Framehouse application compiles and links, and the deterministic
  Edit route passes in 9.70 seconds. The controller shrinks from 43,380 to 43,215 lines, and
  architecture checks prevent recipe schema or thumbnail identity ownership from returning there.
  `ThumbnailCacheSession` now owns mask/layer thumbnail dirty and deferred state, derived thumbnail
  snapshots and asset/revision keys, mask/layer worker identities and generations, refresh/cache
  counters, and the byte-bounded entry LRU with hit/miss/store/eviction telemetry. Cache lookup,
  replacement, byte estimation, LRU promotion, eviction, and clearing execute in that session; the
  controller no longer retains any of the former twenty-four thumbnail cache/session fields.
  `PresetService` now owns the complete built-in and user preset catalogs, pack/category/group,
  adaptive-rule and versioned-renderer contract metadata, favorites, clamped preview intensity,
  and camera-default states. Built-in replacement, user load/import/sync upsert, collision-safe
  rename, pack move, deletion cleanup, favorite validation, and camera-default replacement are
  atomic service operations; the controller coordinates file I/O, application, and UI signals
  without retaining any of the former fifteen preset-domain fields. The executable fourteen-preset
  registry and its pack, category, adjustment-group, and adaptive-rule map construction now live in
  the focused 402-line `preset_registry.cpp` owner instead of the preview controller. The immutable
  inventory points to that owner with an updated source-region identity; its validator passes, and
  a direct registry test proves complete metadata, the three camera-aware adaptive rules, RAW-input
  recovery semantics, and monochrome semantics. The service and registry tests pass 2/2, the full
  Framehouse application compiles and links, the deterministic edit-rail route passes in 9.78
  seconds, and architecture checks prevent the registry from returning to the controller. This
  reduces `editor_preview_controller.cpp` from 43,768 to 43,380 lines and tightens its non-growth
  ceiling accordingly.
  `HistoryService` now owns cached per-asset edit state, undo/redo state-label pairs, named
  snapshots, bounded access-order eviction, recursive history-application suppression, and nested
  edit-transaction undo/persistence intent. The controller supplies current state, persistence,
  render, and signal callbacks but no longer retains the fourteen former history and transaction
  fields or the unused asset-state mutex.
  `render_executor_service` now owns the
  process-wide preview and export executors, their independent 384 MiB/512 MiB cache budgets, and
  one-shot ACES 2 interactive/final output-LUT prewarming. The controller and export utility request
  those services instead of constructing local executors. `HistogramScopeService` now owns the
  complete 4096-to-256-bin histogram reduction, clipping counts, 128-by-64 waveform/density
  analysis, and bounded 1,800-sample vectorscope computation. `HistogramScopeCoordinator` owns the
  async watcher, active lifecycle, complete pending request snapshot, latest-request coalescing,
  elapsed timing, and immediate dispatch of the coalesced successor. The controller retains typed
  result notification, immediate low-resolution feedback submission, and operational logging.
  `HistogramScopeSession` atomically owns the complete published typed result: normalized and count
  histograms, waveform and density planes, vectorscope points, display clipping, RAW clipping,
  sample counts, generation, timing, and interaction identity. Independent RAW clipping updates do
  not disturb the last published visual scope payload. It also owns scope visibility, canonical
  domain normalization and labels, Proof admission, and fallback to Final Display when soft proof
  becomes unavailable. The
  service's direct behavioral test proves empty input,
  exact bin conservation, clipping semantics, fixed public shapes, horizontal waveform mapping,
  normalized density/vectorscope output, and the non-ARGB color-safe sampling path. All named
  service boundaries exist. After typed publication extraction, the service, coordinator, session,
  and interaction-feedback tests pass 4/4 in 21.05 seconds; the focused scope-domain controller
  path passes in 7.42 seconds. The coordinator proof covers running-work coalescing, replacement by the
  latest complete snapshot, exact metadata carriage, active/idle transitions, and the
  finished-but-not-yet-consumed event-loop boundary. The session proof checks every typed result
  field, immutable copied publication, independent RAW clipping, and complete replacement. The
  complete controller suite passes in 599.87 seconds, and the complete 293-step Framehouse QML/C++
  target compiles and links for the typed-publication source. After the subsequent visibility and
  domain-policy extraction, the session and outline/controller contract pass 2/2 in 32.69 seconds,
  feedback passes in isolation in 15.74 seconds, and the focused scope-domain path passes in 4.96
  seconds; the Framehouse target compiles and relinks. Two broad attempts under sustained system load around 9-11 failed the unchanged
  12-second settled max-resolution handoff budget while the full render was actively running, with
  no histogram work, timer, or pending request blocking it; those attempts are recorded as missing
  final-source performance evidence rather than accepted as a regression-free broad pass. After
  mask-set derivation moved into `ThumbnailPresentationService`, the queue coordinator, queue
  service, and extended presentation service pass 3/3 in 0.29 seconds. A concurrent broad outline
  workflow under load about 11.3 ran for 939.61 seconds and reached its asynchronous outline check
  before the worker completed; that run is likewise recorded as missing load-valid performance
  evidence, not accepted as grouping or outline completion proof. However,
  a subsequent complete controller attempt under load around 11 failed after 499.97 seconds at the
  repeated large-mask undo/redo settle gate with both render timers and watchers idle but the final
  full request still pending. That signature exposed a real handoff race rather than a slow active
  render: the first queued dispatcher could observe `QFutureWatcher` as running, while the later
  processing-state callback observed it idle without rearming dispatch. `RenderScheduler` now owns
  a coalesced pending-request dispatch timer, and `updateProcessingState()` re-arms that dispatcher
  exactly when a pending interactive/full request has no timer or watcher blocker. The direct
  scheduler proof exercises false-pending, watcher-blocked, duplicate-coalesced, and idle-pending
  cases and passes in 0.86 seconds. The focused controller/history integration proof verifies the
  same idle-pending rearm and clean cancellation boundary before its 25-action exact undo/redo and
  relaunch workflow, and passes in 5.89 seconds. At that point the complete controller and
  interaction proof remained open until a representative-load pass; the unchanged interaction test also exceeded its settle
  budget while an interactive render was actively running at load 8-11, which is not accepted as
  proof of this handoff correction. A later complete controller run exposed the same class of
  accepted-work/idle-observer race at the inpaint boundary: the explicit progress state was active,
  but `QFutureWatcher::isRunning()` had not become observable yet, so `processing()` briefly returned
  false and callers could inspect the uncommitted retouch mask. `updateProcessingState()` now treats
  active inpaint progress as busy from admission through publication. After the custom-device ICC
  metadata fix and this inpaint admission fix, the complete controller suite passes in 398.97
  seconds, including asynchronous preview/final patch generation and every later mask workflow.
  Mask grouping then stopped accepting an eight-function
  controller callback bundle. The new 115-line `mask_presentation_schema` owns canonical
  normalization, selection admission, alpha availability, invalid-source classification, and
  source/operation labels; `ThumbnailPresentationService` calls those policies directly. The real
  presentation, scheduler, and controller/history integration tests pass 3/3 in 6.13 seconds, and
  the architecture and diff checks pass. The complete Framehouse application target compiles and
  links after the extraction. Retouch-stack grouping then moved beside mask-set grouping in the
  presentation service: selection sources are rejected, and canonical retouch order, mode,
  applied/staged status, stroke count, and audit text are produced without controller policy. The
  direct presentation and controller/history tests pass 2/2 in 6.50 seconds, and Framehouse
  compiles and links. Generated-mask cleanup is also no longer a controller algorithm. The focused
  38-line interface and 245-line implementation own bounded component traversal, enclosed-hole
  filling, isolated-island removal, threshold clamping, immutable cleanup-source replay, mutation
  metadata, and user summary. Its direct alpha-raster proof covers exact changed pixels, counts,
  clamping, deterministic replay, and fail-closed invalid/non-generated inputs; the cleanup,
  presentation, scheduler, and controller/history tests pass 4/4 in 2.38 seconds. Framehouse and
  the broad controller target compile and link, and architecture guards forbid restoration of the
  cleanup traversal to the controller. Generated-mask provenance construction and staleness then
  moved into a focused 34-line interface and 185-line implementation. It owns canonical kind,
  source/model/prompt/coordinate dependency metadata, complete required-key validation, crop,
  rotation, source-version, runtime, and model invalidation, explicit stale state, and the narrow
  legacy-object admission rule. Direct proof covers each dependency class and fail-closed missing
  provenance. The cleanup, metadata, presentation, scheduler, and controller/history tests pass
  5/5 in 1.97 seconds; controller, broad-test, and Framehouse targets compile and link. The
  generated-mask quality calculation is now outside the controller as well. Its focused 23-line
  interface and 124-line implementation own alpha/confidence raster statistics, edge and coverage
  classification, bounded quality scoring, warning flags, and the user-facing summary. Direct
  proof covers invalid and empty payloads, exact good-mask scoring, expected versus clipped edge
  contact, combined low-confidence and stale penalties, and deterministic tiny/huge thresholds.
  The presentation, scheduler, cleanup, metadata, quality, and controller/history tests pass 6/6
  in 1.72 seconds; the architecture boundary and diff checks pass. The
  object-removal assist is no longer another controller raster algorithm. Its focused 35-line
  interface and 223-line implementation own deterministic edge dilation, bounded likely
  shadow/reflection extension, exact expansion accounting, and additive mutation provenance. Its
  direct proof covers invalid and empty admission, exact single-pixel geometry and counters,
  deterministic repeat output, source preservation, trimmed runtime identity, prompt/source-image
  metadata, and fail-closed mutation. Shared segmentation raster primitives are also no longer
  controller-local: a focused 33-line interface and 118-line implementation own unsigned selected
  pixel counting, max-alpha union, clamped subtraction, dimension admission, and bilinear
  resampling. Direct proof covers exact four-pixel arithmetic, invalid/mismatched inputs, identity
  preservation, and exact 2-by-2 to 3-by-3 interpolation. The presentation, scheduler, generated
  cleanup/metadata/quality/object-remove, segmentation operations, and controller/history slice
  passes 8/8 in 2.42 seconds. The broad controller target and complete Framehouse application
  compile and link, and the architecture and diff checks pass. Sensor-dust image analysis then
  moved into a focused 27-line interface and 247-line implementation. It owns scene-linear luma
  sampling, smooth-region contrast/texture admission, local-minimum detection, weighted candidate
  clustering, overlap suppression, and feathered matte rasterization. Direct proof uses a synthetic
  smooth field with a compact dark defect and covers null/undersized/smooth admission, maximum
  count, bounded candidate location/radius/score, deterministic repetition, centered full-alpha
  matte output, and invalid-dimension rejection. Sensor-dust map identity and persistence then
  moved behind a focused 41-line interface and 115-line implementation with an explicit storage
  root. It owns canonical camera/lens hashing, contained paths, atomic JSON writes, exact alpha and
  retouch-parameter round trips, and fail-closed missing/corrupt reads. Direct temporary-directory
  proof covers normalized and isolated identities, invalid roots/payloads, physical file creation,
  byte-exact round trip, default-device isolation, and corrupt storage. Red-eye and skin-cleanup
  pixel analysis then moved into a focused 27-line interface and 195-line
  implementation. It owns red-dominance admission, connected-component traversal and pupil shape/
  area rejection, elliptical feathered rasterization, warm-hue/ratio skin classification, and
  empty-result normalization. Direct proof covers undersized inputs, a compact synthetic pupil,
  exact centered alpha, oversized red-region rejection, admitted skin alpha, and neutral-color
  rejection. The complete focused presentation/scheduling/generated-mask/segmentation/dust/
  retouch/history slice passes 11/11 in 2.64 seconds. The broad controller target and complete
  Framehouse application compile and link, and the architecture and diff checks pass. The
  smart-selection worker input then gained two focused owners. The 35-line geometry interface and
  122-line implementation own full-raster admission, direct/flipped instance lookup, bounded
  instance counting, point lookup, and orientation-aware seed construction; direct proof also
  closes the former truncated-raster out-of-bounds case. A focused 22-line instance-policy
  interface and 248-line implementation now own direct/nearby/flipped candidate arbitration,
  distance/coverage/identity ordering, path sampling and majority admission, touch/coverage/first-
  seen ranking, and selected-instance alpha materialization. Direct proof covers direct and invalid
  maps, equidistant coverage ranking, flipped-orientation fallback, stable multi-instance path
  voting, duplicate/invalid identity normalization, and byte-exact object-mask publication. The
  former duplicated nearby-candidate collection/sorting branches are removed. Candidate cycling
  and polygon-edit geometry moved into a focused 32-line interface and 206-line implementation. It
  owns bounded candidate selection, orientation cross products, coordinate normalization,
  segment intersection and bow-tie rejection, normalized variant publication, point-to-segment
  distance, closed-polygon simplification, and quarter-point smoothing. Direct proof covers both
  candidate-index bounds, incomplete/clamped rows, valid square versus intersecting bow tie,
  degenerate and projected distance, collinear removal with corner retention, and exact triangle
  smoothing. The 45-line prompt-schema interface and
  415-line implementation own canonical point/negative/box/brush aliases, coordinate and reversed-
  box normalization, invalid-prompt rejection, path expansion/deduplication, 512-step preview
  quantization, bounded path sampling, and runtime segmentation-prompt conversion. Direct proof
  covers every prompt class, exact box geometry, negative omission from display paths, exact
  quantization, deterministic spacing, combined brush/negative/box runtime prompts, exact audit
  counts/type order, additive prior-prompt merging, generated-provenance mirroring, and fail-closed
  null/empty mutation. Normalized point-bounds calculation now lives in that schema as well and is
  directly proven for clamping, invalid-row omission, and zero-area rejection. Segmentation-result
  publication and analysis moved into a focused 49-line interface and 201-line implementation. It
  owns result-to-mask round trips, confidence-map validity/publication/clearing and additive
  provenance, normalized alpha sampling, nonzero count, coverage, 96-alpha bounds, too-small
  admission, and thin-fragment classification. Direct proof covers byte-exact variants, invalid
  payloads, exact statistics/bounds/sampling, single-pixel rejection, a synthetic thin subject,
  confidence resolution metadata, and preservation of unrelated provenance. Generated-mask request
  and failure policy moved into a focused 34-line interface and 188-line implementation. It owns
  person/mountain/facial-hair aliases, unsupported landscape and people-part families and labels,
  actionable no-prompt/decode/tiny-selection/runtime/no-target/unknown guidance, and stable reason
  codes. Direct proof covers every alias family and failure category without controller or UI
  setup. Object-pick arbitration and geometry moved into a focused 39-line interface and 206-line
  implementation. It owns target-centered prompt/instance preference, exact bounded localized
  crop geometry, dense crop-shaped failure rejection, and localized-to-full raster remapping with
  explicit target-bound checks. Direct proof covers invalid/result-centered arbitration, exact
  centered and edge crop rectangles, synthetic crop-shaped rejection without compact-object false
  positives, byte-exact remapping, out-of-bounds write rejection, and centered/nontrivial/non-global
  single-point plausibility without the former unused source-image dependency. Localized seed-color
  refinement moved into a focused 15-line interface and 214-line implementation. It owns clamped
  color sampling, patch/perimeter references, weighted color distance, adaptive thresholds,
  mask-constrained four-connected flood fill, preservation when refinement cannot narrow a model
  result, and deterministic publication. Direct proof covers null input preservation, unchanged
  bounded-model results, and exact red/blue split-region isolation. Brush input normalization and
  serialization then moved into a focused 48-line interface and 321-line implementation. It owns
  raster admission, exact pressure/tilt bounds, preview-point response, variant round trips, stroke
  settings and operation publication, flat-point/stroke replay precedence, normalized erase
  propagation, padded dirty-region geometry, and quantized bounded source-
  decode sizing for auto-mask commits. Direct proof covers invalid and retained raster payloads,
  exact clamping and response math, every round-tripped input field, replay fallbacks and precedence,
  stroke-level erase override, empty and single-point dirty regions, erase-stroke identity/settings/
  samples, and 512/1024 decode admission. The complete
  brush-stroke sampling path then moved into a focused 25-line interface and 183-line
  implementation. It owns normalized dab spacing, live-segment smoothing, pressure/tilt-aware
  interpolation, duplicate suppression, and deterministic committed-stroke resampling across
  segment boundaries. Direct proof covers spacing clamps, first and sub-threshold events, exact
  smoothed dabs plus live endpoint, clamped full-field interpolation, cross-segment spacing, final-
  endpoint retention, determinism, and short-stroke preservation. Per-dab alpha and auto-mask
  rasterization then moved into a focused 22-line interface and 319-line implementation. It owns
  bounded paint/erase mutation, feather/flow composition, linearized perceptual-color distance,
  local and anchor-to-sample edge barriers, protect-edge gating, cancellation, changed rectangles,
  and candidate/evaluation telemetry. Direct proof covers absent payloads, exact opaque 9x9 paint
  bounds, exact half-flow erase alpha, red/blue split-region auto-mask isolation, nonzero bounded
  telemetry, and cancellation without partial publication. Ordered target-resolution brush-matte
  replay then moved into a focused 12-line interface and 81-line implementation. It owns fresh
  target raster allocation, point-order paint/erase replay, source/target provenance, replay-point
  counts, pixel/work timing, and full-resolution-specific telemetry. Direct proof covers fail-
  closed non-brush/empty/degenerate inputs, exact 9x9 replay alpha, original 4x3 provenance, all
  target and full-resolution fields, ordered erase, and omission of full-resolution telemetry from
  preview work. Fast brush-outline geometry and shared nested-path publication then moved into a
  focused 19-line interface and 145-line implementation. It owns normalized 40-point circles,
  sampled stroke-side/cap construction, bounded coordinates, erase filtering, stroke-history
  preference, and flat-point fallback. Direct proof covers null-safe nested publication, empty and
  exact single-point geometry, edge clamping, exact multi-point topology, admitted stroke history,
  erase-history fallback, and erase-point omission. Display-scale normalization, pixel-space path
  conversion, topology-safe duplicate-loop handling, measured closed-outline error, tolerance-
  bounded RDP simplification, deterministic point budgeting, nested-path publication, and aggregate
  simplification telemetry then moved into a focused 40-line interface and 246-line implementation.
  Direct proof covers scale quantization, invalid-point rejection, normalized-to-pixel round trips,
  exact duplicate-endpoint removal, measured segment error, topology-preserving simplification,
  deterministic budget enforcement, nested single/multi-path semantics, and exact point/error
  statistics. Alpha-raster contour tracing and segmentation fallback geometry then moved into a
  focused 21-line interface and 480-line implementation. It owns checked raster admission,
  collision-free directed-edge identity, deterministic contour walking and tie-breaking, dynamic
  low-alpha admission, material secondary-loop filtering, collinear removal, tolerance/budget
  simplification, convex-hull and bounding-box fallbacks, normalized publication, and edge-padding
  recovery. Direct proof covers truncated-raster rejection, exact normalized bounds, hull
  publication, a low-alpha single-pixel subject, deterministic multi-component ordering, retention
  of a material secondary component, one-pixel noise suppression, and bounded edge-touching masks.
  Vector/brush/raster outline routing and clean-canvas presentation policy then moved into a
  focused 35-line interface and 322-line implementation. It owns exact rectangle and 72-point
  ellipse publication, authored polygon order, bounded high-resolution brush aspect sizing,
  selection-refinement exclusion, raster/traced/fast fallback order, source-dependent brush
  detection across current and historical settings, final-single-mask eligibility, flat/nested
  first-path handling, normalized area measurement, composite-noise filtering, and filter
  telemetry. Direct proof covers every vector shape, exact resolution bounds, refinement admission,
  top-level and stroke-level brush dependencies, additive versus composed/inverted eligibility,
  exact normalized area, material-secondary retention, tiny-path removal, and complete filter stats.
  Mask-range preview mathematics then moved into a focused 68-line interface and 501-line
  implementation. It owns circular hue wrapping/distance/feathering, HSV and luminance analysis,
  skin-candidate weighting, normalized color/luminance/depth sample aggregation, depth-payload
  admission, clamped luminance-drag geometry, preview white-balance multipliers, selection
  expand/smooth/fine-edge/contrast/invert/density/opacity ordering, bilinear depth lookup, unit-range
  feathering, and combined color/luminance/depth refinement. Direct proof covers circular seam
  behavior, exact red HSV/luminance, warm-skin preference, wraparound hue samples, padded saturation/
  luminance/depth bounds, reversed drag normalization, neutral and warm white balance, identity/
  invert/density/opacity selection semantics, exact bilinear depth, bounded range feathering, and
  luminance-band rejection. The complete focused
  presentation/scheduling/generated-mask/segmentation/brush/dust/retouch/smart-selection/history
  slice passes 28/28 in 8.83 seconds. The broad controller
  target and complete Framehouse application compile and link, and architecture and diff checks
  for the affected C++ graph pass. At that intermediate milestone the controller was 43,845 lines and
  the scheduler is 56 lines, below its 63-line ceiling. The new ownership boundary is installed;
  the full architecture sweep is currently stopped by unrelated concurrent growth of
  `StudioWorkspace.qml` to 27,258 lines against its frozen 27,256-line ceiling, while the diff
  check passes.
  However,
  thumbnail refresh/presentation integration callbacks, remaining mask worker algorithms, render/configuration
  work, and the controller's still-enormous command surface keep this item partial. The unused public
  `EditorPreviewController::applyColorAdjustments(QImage*)` entry point is deleted, so callers can
  no longer request color work without a render request, target, cancellation identity, or admitted
  plan through that controller API. Flattened photo export also no longer decodes, compiles, or
  directly invokes output rendering in the controller. The focused
  `render_flattened_output_file()` owner in `photo_render_utils` performs metadata-only RAW
  preflight, stable asset admission, export-plan compilation, executor-owned full file decode,
  mask rebuild, and final output publication; the controller only requests the result and writes
  the selected file format.
- [~] Move decode, color configuration, recipe compilation, profile resolution, output conversion,
  cache policy, and render execution out of the controller.
  Display-profile state resolution and calibration policy moved behind `DisplayProfileService`;
  platform ICC discovery moved behind `display_profile_discovery`; flattened export decode,
  compilation, and execution moved behind `render_flattened_output_file`; interactive/full preview
  timing and callback ownership moved behind `RenderScheduler`; canonical plan compilation,
  decoder-provenance admission, serialization, and cache policy moved behind `RenderPlanStore`;
  render cancellation identity and stale-publication order moved behind `PreviewSession`; mask
  worker revision/generation and cancellation identity moved behind `MaskSession`; durable layer,
  saved-visibility, and layer-adjustment clipboard ownership moved behind `LayerSession`; mask
  future watchers, queued smart-selection payloads, and worker job identities moved behind
  `MaskWorkerCoordinator`; preset catalog, metadata, favorite, intensity, and camera-default
  ownership moved behind `PresetService`; cached asset state, undo/redo, snapshots, eviction,
  application guards, and nested transaction ownership moved behind `HistoryService`; the other
  listed responsibilities remain in the controller. Shared mask/layer thumbnail timing,
  cancellation, identity, completion, and telemetry moved behind `ThumbnailRefreshCoordinator`;
  thumbnail cache state and bounded entry policy moved behind `ThumbnailCacheSession`; thumbnail
  revision identity, request/job snapshot construction, cancellation carriage, and ordered cache
  publication moved behind `ThumbnailRefreshQueueService`; mask-set derived-list assembly moved
  behind `ThumbnailPresentationService`; recipe-identity, source-decode, adjustment-render, and
  mask-entry-render callbacks are now removed from the controller. The factory receives typed mask,
  trace, effect-preview, and shared-executor adapters, but it owns worker rendering policy and both
  high-level pixel-rendering algorithms.
  Histogram, waveform, density, vectorscope, and
  clipping computation moved behind `HistogramScopeService`; watcher ownership, latest-request
  coalescing, pending snapshots, timing, and active lifecycle moved behind
  `HistogramScopeCoordinator`; complete typed result and RAW-clipping publication moved behind
  `HistogramScopeSession`, together with visibility, canonical domain names/labels, Proof admission,
  and soft-proof fallback policy.
  Preview color configuration is no longer controller-owned policy. The focused 16-line
  `preview_color_pipeline_config.h` contract and 67-line implementation own Monitor, Match Export,
  Soft Proof, manual preview carrier, system-monitor ICC asset, and complete measured display-target
  derivation before the shared recipe bridge constructs `ColorPipelineConfig`. Its direct proof
  exposed a real mismatch: selecting `Soft Proof` did not itself enable proof execution and could
  leave the UI mode ahead of the rendered graph unless another flag was already true. Soft Proof
  mode now authoritatively enables proofing; Monitor still disables it and Match Export derives the
  proof carrier from export. The direct deterministic mode/ICC/luminance proof passes in 0.46
  seconds, the controller color-cache integration passes in 0.91 seconds, Framehouse compiles and
  links, and architecture/diff checks pass. The controller shrinks from its 39,735-line ceiling to
  39,647 lines, and exact non-growth ceilings protect both new files.
  Final preview display publication is now outside the controller as well. The focused
  `preview_display_publication` owner normalizes untagged pixels, recognizes already-published
  custom/monitor pixels, applies the declared preview output only when proof has not already done
  so, and performs manual or system monitor ICC conversion last through explicit-purpose Qt
  boundaries. A shared `preview_output_color_space_for_name()` mapping replaces duplicate lookup
  functions in the controller and export utility. Direct pixel/profile proof covers manual and
  system Display P3 publication, alpha preservation, idempotent already-published pixels, untagged
  normalization, and protection of proof-published pixels from an extra output conversion. It
  passes in 0.41 seconds; the extracted configuration, publication, and controller color-cache set
  passes 3/3 in 0.42 seconds; Framehouse compiles and links; architecture and diff checks pass.
  The controller is 39,521 lines and `photo_render_utils.cpp` is 3,419 lines under tightened exact
  non-growth ceilings. The controller retains only a three-line coordinator that supplies the
  current `DisplayProfileService` snapshot to the publication owner.
  The controller's second full source decoder is now physically deleted. Preview, thumbnail,
  comparison, inpaint, smart-selection, and export callers resolve the one
  `photo_render_utils::decode_source_image()` implementation through the narrow
  `source_image_decode.h` contract. That owner publishes camera-linear/native/OpenEXR/Qt decoding,
  RAW metadata, cancellation, one decoded-image cache identity, and equivalent backend/cache
  telemetry; the controller no longer owns its separate preview-encoded cache publication or
  decoder selection. This reduces `editor_preview_controller.cpp` from 43,215 to 43,042 lines.
  Framehouse compiles and links, render parity passes in 5.19 seconds while directly proving
  backend/cache telemetry and exact repeated-decode cache reuse, the color pipeline/property
  and synthetic RAW invariant tests pass 3/3, the three focused thumbnail/recipe services pass,
  and the deterministic Edit route passes in 10.68 seconds. These gates prove unified code-path and
  synthetic behavior, not the still-required licensed multi-camera RAW corpus.
  Thumbnail worker construction and rendering now live in the focused
  `thumbnail_worker_renderer_factory.cpp`. It carries queue cancellation into the shared source
  decoder, applies the canonical crop, owns mask-source and mask-set entry iteration, owns
  adjustment-thumbnail cache/shared-render selection, and publishes the stage/color metadata.
  `EditorPreviewController` no longer constructs a source-decode callback or either high-level
  pixel-rendering callback. Its approximate adjustment-thumbnail exposure, white-balance, curve,
  and blend fallback is physically deleted: a failed canonical shared render now fails the
  thumbnail closed instead of publishing pixels that can disagree with preview/export. Render
  parity directly proves tagged Display P3 decode/crop, published-preview reuse, canonical shared
  rendering, fail-closed behavior, mask-source/mask-set rendering, and color-metadata retention in
  7.04 seconds. The queue/presentation pair passes 2/2, the deterministic Edit route passes in
  12.38 seconds, Framehouse compiles and links, and the architecture and diff gates pass. The
  factory also owns selection-mask map construction plus before/after effect-preview assembly and
  metadata. This first reduced `editor_preview_controller.cpp` from 43,027 to 42,385 lines. Shared
  mask-alpha evaluation and the adapter into the shared preview executor remain controller
  dependencies and are later decomposition seams; thumbnail worker rendering ownership itself is
  no longer controller-local.
  The controller's second complete `QVariantMap`-to-`LocalMask` compiler is now physically deleted.
  Thumbnail, outline, local-mask, retouch, layer-selection, and output-resolution brush paths call
  the production mapping in `photo_render_utils.cpp` through the 21-line
  `local_mask_compiler.h` contract. The shared compiler preserves parent/output identities,
  reusable-output and blend intent, complete selection/refinement and retouch fields, brush tablet
  data, clone state, raster/depth payloads, and inpaint patches; its rebuild policy retains a stored
  raster only when that raster actually covers the requested output. The controller's duplicate
  brush-raster rebuild and inpaint-patch decoder are deleted with the mapping. Preset thumbnails no
  longer redeclare or directly invoke the broad output-render API either: the 17-line
  `thumbnail_canonical_renderer.h` boundary executes the admitted canonical plan in the shared
  utility. Render parity directly checks the shared mask compiler's identity, composition, tablet,
  inversion, and output-raster result and passes in 7.66 seconds. Color pipeline/property,
  synthetic RAW, render parity, thumbnail queue/presentation, and deterministic Edit-route gates
  pass 7/7 in 16.25 seconds; Framehouse compiles and links, and architecture/diff checks pass. The
  controller is now 42,046 lines.
  Thumbnail mask-alpha ownership is now also outside the controller. The focused 25-line
  `canonical_composite_mask_alpha.h` contract and 177-line implementation convert the tagged
  thumbnail into the declared linear working space, apply the layer exposure/white-balance input,
  compile each source through the production `LocalMask` compiler, and call the engine's canonical
  `rasterize_local_mask_set_alpha()` evaluator. Its cache identity includes source raster/profile,
  exact mask maps, exposure, and white balance; cache hits avoid both working-image conversion and
  mask rasterization. Adjustment thumbnails call this owner directly rather than accepting an
  alpha callback from `EditorPreviewController`, and mask-source/mask-set thumbnails use the same
  result for grayscale, solid-overlay, and metadata publication. The three controller-local
  thumbnail-alpha wrappers and their duplicate map-to-mask loop are physically deleted. Render
  parity directly compares a composed add/subtract thumbnail result byte-for-byte with the engine
  rasterizer and proves the second request is a shared-cache hit. Render parity, thumbnail queue,
  thumbnail presentation, and deterministic Edit-route tests pass 4/4. Mask thumbnail payload
  presentation then moved into the 17-line `mask_thumbnail_payload_service.h` contract and its
  114-line implementation. That service owns grayscale-alpha and solid-color overlay image
  generation, bounded PNG publication, pixel-density metadata, and canonical alpha metadata. The
  worker factory now supplies this production service directly to the queue rather than accepting
  a second controller callback. The old controller payload function, overlay helpers, and callback
  are physically deleted. The final affected set passes 4/4 in 11.58 seconds; Framehouse compiles
  and links, architecture and diff checks pass, and the controller is now 41,861 lines. The
  thumbnail-to-preview mapping is no longer an inline controller lambda either. The 45-line
  `thumbnail_shared_preview_adapter.h` owns the complete narrow request/result mapping and enters
  the same production preview executor used by normal settled publication; the controller now
  supplies that named adapter directly. The affected 4/4 set passes in 11.43 seconds after this
  final cutover, Framehouse compiles and links, and the controller is now 41,835 lines. General
  interactive mask-preview presentation still has separate controller math, and trace/job creation
  callbacks remain later decomposition seams; thumbnail image, alpha, payload, and shared-render
  adapter ownership are no longer controller-local.
  The canonical alpha service now also accepts already-compiled `LocalMask` sources for general
  interactive previews and outline work. `evaluate_composite_mask_alpha_for_preview()` retains its
  existing identity-complete cache key during migration but no longer evaluates the composite with
  controller-local sRGB mask math; on a miss it enters the same profile-aware working conversion
  and engine `rasterize_local_mask_set_alpha()` path as thumbnails and production layer masks. This
  closes a visible consistency gap for nested add/subtract/intersect/exclude stacks and prevents
  preview-only evaluation from silently disagreeing with the rendered mask. Render parity directly
  compares both the map-based thumbnail route and compiled interactive route byte-for-byte with
  the engine result. The complete layer/thumbnail adjustment test initially caught the preserved
  cache-key schema requirement, then passes after the canonical owner retained that `sha1:`
  provider contract. The final render-parity, thumbnail queue/presentation, deterministic Edit
  route, interactive feedback, and layer-adjustment set passes 6/6 in 66.37 seconds; the two broad
  controller routes take 25.36 and 28.85 seconds respectively. The controller is now 41,808 lines.
  The remaining production layer-selection seam is now closed as well. The 25-line
  `photo_layer_compiler.h` boundary exposes the one photo-layer compiler already owned by
  `photo_render_utils.cpp`; preview and export both enter that implementation. Preview compilation
  now supplies the real linear working pixels and active `ColorPipelineConfig`, so luminance,
  color, depth, and refinement-dependent layer selections no longer evaluate against the old
  fabricated neutral-gray pixel. The controller-local photo-layer mapper, stored-raster duplicate,
  selection-set composer, and approximate mask evaluator are physically deleted. Render parity
  constructs a color-range adjustment layer and compares its compiled mask bytes directly with
  `rasterize_local_mask_set_alpha()` over the same working pixels.
  Refinement-edge and single-range visualization no longer retain a second sRGB/geometry path
  either. Both render the refined mask and the same mask with refinement controls stripped through
  the canonical alpha service, then visualize their byte-alpha difference. This permitted deletion
  of the controller's private linear/radial/rectangle/brush/polygon/generated/range evaluators,
  raster morphology approximation, and add/subtract/intersect/exclude composer. The affected
  thumbnail queue/presentation, Edit rail, feedback, outline, layer-adjustment, and render-parity
  set passes 7/7 in 123.10 seconds. The previously stale outline source contract now checks the
  extracted viewport, input-command surface, photo-layer recipe service, and presentation-metrics
  owners and passes its full 58.20-second runtime. Framehouse compiles and links; architecture and
  diff checks pass. The controller is now 40,374 lines, a 1,434-line reduction from the preceding
  canonical-alpha checkpoint, while `photo_render_utils.cpp` remains at its frozen 3,422-line
  ceiling. Trace/job creation callbacks remain later decomposition seams.
  Thumbnail adjustment job identity, recipe hashes, and preview white-balance policy are no longer
  controller callbacks. The 34-line `thumbnail_render_job.h` owner constructs the immutable
  revision-bound job, while the worker factory calls the canonical recipe-hash and white-balance
  services directly. Mask-source and composite trace construction then moved beside the shared
  alpha provider: byte-payload fingerprints, normalized source identity, availability, revision,
  raster, component, and work-state fields are produced by `mask_alpha_provider` and consumed
  directly by the worker queue. The controller-local trace implementations and all corresponding
  worker dependency callbacks are physically deleted. This repaired the partially moved seam that
  initially failed compilation on an undeclared mask normalizer. The provider now includes its
  schema owner explicitly, Framehouse compiles and links, architecture and diff checks pass, and
  the affected thumbnail queue/presentation, Edit rail, feedback, outline, layer-adjustment, and
  render-parity set passes 7/7 in 124.47 seconds. The controller is 40,055 lines, the worker
  factory is 523 lines, and both the 845-line provider implementation and its 125-line interface
  have exact non-growth ceilings. The remaining thumbnail dependency seam is limited to scaled
  mask materialization, presentation metadata, and source-revision construction.
  That final dependency seam is now closed. The 30-line `scaled_mask_matte_service.h` contract and
  261-line implementation own target-resolution brush replay, raster resampling, immutable cache
  identity, the 32 MiB bounded LRU, cache telemetry, and scaled-matte presentation fields. The
  33-line `mask_source_revision.h` helper owns file/raster/RAW-decode revision identity. The worker
  factory calls both owners directly; `ThumbnailMaskRendererDependencies`, its three callbacks,
  the controller lambdas, controller-local cache implementation, scaling algorithm, metadata
  mapper, and source-revision implementation are physically deleted. Existing controller testing
  accessors now observe the service rather than a private cache. The pixel-zoom contract was also
  corrected to inspect the already-extracted `StudioImageViewport` and
  `StudioViewportInputCommandSurface` owners and their explicit `previewContext`/
  `workspaceContext` boundaries instead of stale inline-workspace spellings. Framehouse compiles
  and links; architecture and diff checks pass; thumbnail queue/presentation, Edit rail, feedback,
  pixel zoom, outline, layer adjustment, and render parity pass 8/8 in 131.45 seconds. The
  controller reached 39,785 lines and the callback-free worker factory is 502 lines, with exact
  non-growth ceilings on both new services. RAW decode settings and cache identity were then
  consolidated behind the 28-line `raw_decode_policy.h`: the controller-local copy of all eleven
  RAW settings and its private cache-key wrapper are deleted, and preview, thumbnail, mask-source,
  and shared decode identities now call the same mapper. Framehouse and render parity rebuild;
  synthetic RAW invariants, the camera-linear decoder gate, and render parity pass 3/3 in 15.37
  seconds; architecture and diff checks pass. The controller reached 39,766 lines. The seven
  controller-only scaled-matte testing methods were then deleted as obsolete compatibility
  surface. Pixel-zoom and controller regressions now call the focused service and inspect its
  cache snapshot directly; the pixel-zoom gate passes in 6.89 seconds, Framehouse relinks, and
  architecture and diff checks pass. The controller is now 39,735 lines and its header is 2,697
  lines, with both non-growth ceilings tightened.
  The complete `editor_preview_controller_test` target initially compiled and linked but failed
  three confirmation runs at the unchanged 12-second large-mask undo/redo settle gate with
  `processing=1`, no pending timer/request, and one full render still executing at 208.55, 208.31,
  and 212.71 seconds. The fixture at that boundary is only 64 by 64 pixels, so this was not accepted
  as ordinary full-resolution brush raster cost. An identity-complete compiled-mask cache
  experiment did not move the failure boundary and was removed in full;
  `photo_render_utils.cpp` remains exactly at its frozen 3,422-line ceiling. Moving top-level
  preview and overlay futures onto a controller-owned two-thread pool then reproduced the same
  `processing=1`, `fullRunning=1`, `fullPending=0` state at about 195 seconds, so that unproven pool
  and its formatting churn were also removed.

  Timing the exact current generation exposed the actual defect. Mask feedback draft execution
  intentionally omitted masks, photo layers, and deferred detail, but canonical identity and plan
  admission still serialized and compiled the complete 10,000-dab state. The executor therefore
  ran a reduced draft against a plan describing different work, then ran the complete state again
  for settled output. Before repair, the final draft spent about 0.4 seconds on identity, reached
  plan readiness around 1.3 seconds, and completed around 5.99 seconds; the following full render
  then exceeded nine seconds. `mask_feedback_draft_execution_state()` now creates the exact reduced
  state used by both draft plan admission and execution, retaining global tone/color while clearing
  masks, layers, linear/radial masks, detail, noise reduction, and lens work. The direct policy test
  locks both the deferred-stage removal and retained global exposure/saturation. With that shared
  state, the same final draft reached identity in 0 ms, plan readiness in 1 ms, and completion in
  about 3.944 seconds; the complete full render retained all edits and completed in about 10.119
  seconds. All temporary timing diagnostics were removed.

  The next uninterrupted controller run crossed the former 208-213 second boundary and completed
  successfully in approximately 536 seconds without increasing the 12-second fixture deadline.
  It then exposed a later correctness defect that the old stress failure had hidden: selection
  expand changed the canonical alpha hash but not the visible outline. Raster morphology used the
  slider amount once to choose dilation/erosion radius and again to attenuate the neighborhood
  maximum/minimum, so geometric edits could remain below the contour threshold. Grow and shrink
  now publish the neighborhood extrema directly after their amount selects radius. The controller
  also no longer substitutes the unrefined raster contour while refined preview is selected;
  source-free tracing is restricted to explicit `original` preview, and refined mode traces the
  canonical evaluated alpha, including source-aware fine-edge behavior. A 96-by-64 single-brush
  regression requires expansion to change both alpha identity and traced paths. The complete
  outline suite passes in 62.79 seconds, layer-adjustment coverage in 28.95 seconds, render parity,
  edit-render policy, and contour tracing pass, and the complete 348-step Framehouse QML/native
  target rebuilds and links. Running the eight-test affected slice concurrently with that full
  rebuild produced two load-sensitive timeout failures; the other six passed, and both failed tests
  passed immediately when rerun sequentially after the build. The architecture boundary and
  `git diff --check` pass, the controller is 39,733 lines, its header is 2,697 lines, and PostgreSQL
  remains the sole production catalog authority. This local scheduling/correctness gate is closed;
  target-hardware percentile, GPU residency, physical-display, and real-media evidence remain open.
  A later source audit found that the controller still retained an OpenEXR implementation and a
  four-way OpenEXR/native/RAW/Qt branch inside its linear-preview callback even though ordinary
  preview, thumbnail, and export decoding had already moved. That final duplicate is now physically
  deleted. Preview support checks and source-size queries use the narrow shared decode contract,
  and executor-scheduled preview decode calls the same `decode_source_image()` authority with the
  request state, edge, cancellation token, cache identity, and camera-linear RAW boundary used by
  the other surfaces. The shared contract remains at 30 lines and `photo_render_utils.cpp` remains
  at its 3,419-line ceiling. Synthetic RAW, render parity, preview color-cache, display publication,
  and color-configuration tests pass 5/5 in 5.83 seconds; Framehouse compiles and links;
  architecture and diff checks pass. The controller is now 39,389 lines under a tightened exact
  non-growth ceiling, and the architecture guard rejects restoration of any controller-local
  OpenEXR, native, or RAW pixel decoder.
  Preview and export pipeline-state assembly are now one purpose-aware production implementation.
  The 48-line `desktop_pipeline_state_compiler.h` contract and 227-line implementation own base
  color state, local mask sets, retouch ordering, and photo-layer attachment. Preview supplies its
  immutable monitor/proof configuration and the explicit `Preview` purpose; export supplies its
  output/device configuration and the explicit `Output` purpose. This preserves the intentional
  preview-only `maskSetPreviewBypassed` behavior without maintaining two compilers. Consolidation
  exposed a real divergence: preview carried mask-set invert, expand, smooth, fine-edge, edge
  contrast, opacity, and density controls while export silently dropped them, and the two private
  legacy tool-role classifiers recognized different adjustment sets. The shared compiler now owns
  the complete classifier and all selection controls, so stacked local edits have identical
  semantics outside the declared preview bypass. Controller- and export-local mask-set, retouch,
  and base-state assembly are physically deleted; selection-scoped photo layers call the same
  purpose-aware mask compiler. The focused contract is locked by render-parity coverage that
  proves preview bypass does not suppress export and that output preserves every selection
  control. Render parity, preview color cache, export-job, synthetic RAW, preview color
  configuration, and display publication pass 6/6 in 21.52 seconds. Framehouse compiles and links;
  architecture and diff checks pass. The controller is now 39,225 lines and
  `photo_render_utils.cpp` is 3,269 lines under tightened exact non-growth ceilings.
- [~] Split `StudioWorkspace.qml` into page, canvas, inspector, tool, mask/layer, proof, and status
  components with explicit typed context.
  The first four context-free presentation seams now live in focused QML types:
  `StudioRowRule`, `StudioSectionTitle`, `StudioPanel`, and `StudioTinyInput`. All call sites use the
  published module types, the workspace no longer declares those inline components, and the macOS
  QML cache build plus desktop source-contract test pass. This reduces the workspace from 27,430
  to 27,303 lines without moving workflow policy into the components. The active preview-target,
  soft-proof, ICC-asset, and DCP-asset controls now live in the focused
  `StudioColorManagementPanel`; its required controller/theme context is explicit, the type is
  published by the QML module, and the workspace remains exactly at its frozen 27,303-line ceiling.
  Preview-quality numerics are no longer another inline workspace responsibility:
  `StudioWorkspace` now publishes only content dimensions, device scale, and settled-mask state to
  the controller command, while `edit_render_policy.h` owns the ordinary and settled-mask floors,
  supersampling factors, finite-input handling, and bounded target edge.
  Viewport publication policy is also extracted: the workspace no longer owns the 160 ms settle
  timer, event-loop queued flag, viewport signature, duplicate suppression, patch-edge arithmetic,
  or retry invalidation. It submits visible presentation geometry through
  `schedulePreviewViewportPublication()`, reducing the workspace further to 27,256 lines while the
  frozen non-growth ceiling is tightened to 27,256.
  The shared system-health strip is now a published `StudioHealthStatusStrip` component with
  explicit health items, state-color mapping, theme, and border context. The workspace only
  positions it, reducing the active workspace to 27,214 lines and tightening its non-growth ceiling
  accordingly. The complete application QML cache, native target, and final app link pass. A
  bounded offscreen launch against an isolated managed PostgreSQL catalog stayed alive with no QML
  startup or required-property error. The edit-rail shard initially stopped at a stale source
  assertion that still searched the controller for outline-tracing limits; it now follows their
  focused `mask_outline_tracing.cpp` owner and passes the complete deterministic Studio route in
  6.69 seconds.
  Offline-original recovery presentation now lives in the published
  `StudioOfflineRecoveryBanner`. The component owns admission, status text, preservation contract,
  and button presentation through explicit typed inputs and four recovery signals; the workspace
  retains catalog mutation, folder selection, and refresh coordination. Existing automation object
  names remain stable, the source contract follows the focused owner, and the workspace shrinks to
  22,427 lines with that exact non-growth ceiling. The complete Framehouse QML cache and executable
  compile and link. The focused source contract passes in 2.08 seconds, and the direct component
  runtime passes in 1.12 seconds while proving missing-media visibility, surface/state publication,
  all four action signals, confirmation-state text, and online-state dismissal. The broader offline
  recovery shard initially ran a stale executable and stopped at an already-removed MCP-status
  assertion; after explicitly relinking its shard target, the complete real QML recovery route
  passes in 11.43 seconds.
  Export no longer carries a second copy of the same offline-original banner. The shared component
  derives stable Studio and Export automation identities plus the surface-specific preservation
  contract from its explicit page input, while `ExportView` retains only recovery coordination.
  This removes 118 lines from Export and leaves it at 1,481 lines. The direct component runtime now
  switches between Studio and Export identities and verifies the Export preservation contract in
  addition to the common action behavior. The focused source/runtime pair passes 2/2 in 2.62
  seconds, and the complete Framehouse QML cache and application link pass after the shared Export
  cutover.
  Edit-tool transition presentation now lives in the published `StudioToolTransitionOverlay` with
  explicit theme, activity, duration, title, detail, and accent inputs. The workspace retains tool
  selection and transition scheduling but no longer owns overlay geometry, animation, blocking
  input surface, labels, or progress presentation. Automation names and the motion contract remain
  stable, and the workspace shrinks to 22,353 lines with the non-growth ceiling tightened to that
  exact count. The source contract and direct component runtime pass 2/2 in 2.75 seconds, proving
  active/inactive visibility, title/detail publication, conditional detail presentation, indeterminate
  progress, and the duration-bound motion contract. The complete Framehouse QML cache, native
  executable, and final application link also pass after the extraction.
  The complete image-viewport implementation now lives in the published
  `StudioImageViewport` instead of an 813-line inline `ImageViewport` component. Its workspace,
  preview-controller, and theme dependencies are explicit and unambiguously named; zoom, pan,
  preview publication, sampling, mask interaction, native-preview presentation, overlays, loupe,
  brush cursors, retouch commands, and input dispatch remain composed behind the same workspace
  wrapper. `StudioWorkspace.qml` shrinks from 22,353 to 21,547 lines, and architecture checks freeze
  both the reduced workspace and the 823-line focused component while rejecting restoration of the
  inline rectangle. The first real QML route exposed invalid mechanically renamed child-property
  assignments that QML cache compilation did not reject; those assignments now target each child's
  actual `studioTheme` or `previewController` API. The rebuilt Framehouse application links, the
  source contract and bounded component test pass, the complete offline-recovery and edit-rail
  routes pass in 8.92 and 10.18 seconds, and the architecture and diff gates pass after the corrected
  wiring.
  Library, Edit, and Export empty-state presentation now shares the published
  `StudioEmptyStateCard` component. Page-specific availability, messages, errors, and actions remain
  in the workspace, while the component receives explicit theme, accent, border, and secondary-text
  context. The QML cache, full app link, and deterministic edit-rail route pass after the extraction
  in 5.44 seconds. Library filters, projects, collections, and Export destinations now share the
  published `StudioSidebarRow`; it owns only row chrome and click presentation while the workspace
  retains filtering and destination behavior. QML compilation, full app link, and the edit-rail
  route pass in 5.16 seconds after this extraction. Repeated inspector disclosure/reset chrome now
  lives in the published `StudioCollapsibleGroup`; adjustment values, reset commands, and workflow
  policy remain in the workspace. QML compilation, full app link, and the edit-rail route pass in
  5.07 seconds after this extraction. The shared inspector-section styling, availability mapping,
  reset tooltip, and before/after command forwarding now live in the published
  `StudioInspectorGroup`; its workspace dependency is explicit, while the workspace continues to
  own adjustment-layer selection, comparison state, and reset behavior. The complete QML cache and
  application link pass after this extraction, and the deterministic edit-rail route passes in
  5.48 seconds. The target-aware slider wrapper used by 109 adjustment call sites now lives in
  `StudioAdjustmentSlider`; workspace and preview-controller dependencies are explicit, and the
  published component owns target admission, disabled explanations, coalesced edit lifecycle, and
  reset forwarding while each call site still owns its numeric range and adjustment command. The
  complete QML cache and application link pass after this extraction. The edit-rail shard initially
  stopped at a stale source assertion that still searched the workspace for slider ownership; it
  now follows `StudioAdjustmentSlider` and passes the complete route in 5.78 seconds.
  The self-contained Amount/Balance grading surface is now the published
  `StudioGradingMacroControl`; its gradient, value mapping, modified-state presentation, drag
  lifecycle, and double-click reset moved intact, while the workspace retains the grading values,
  preview colors, coalescing calls, and edit commands. The complete QML cache and application link
  pass after this extraction, and the edit-rail route passes in 5.14 seconds.
  The white-balance two-dimensional input surface now lives in `StudioAdjustmentPad2D`. Its exact
  16 ms leading/trailing edit queue, pointer mapping, pending-value flush on release/cancel,
  double-click neutral reset, gradient, and handle presentation moved intact; workspace call sites
  still own temperature/tint values and commands. The complete QML cache and application link pass
  after this extraction, and the edit-rail route passes in 5.15 seconds. `StudioWorkspace.qml` is
  now 26,607 lines. The selective-color hue/luminance pad and saturation rail now live in
  `StudioChannelPadControl`. Its shared 16 ms leading/trailing queue, independent pending hue/luma
  and saturation state, release/cancel flush, neutral resets, pointer transforms, and visual field
  moved together; channel selection, values, and edit commands remain in the workspace. The full
  QML cache and app link pass after this extraction, and the edit-rail route passes in 5.21 seconds.
  The RGB channel-mixer matrix now lives in the published `StudioRgbMixerMatrix`. Its exact 16 ms
  leading/trailing cell-edit queue, value clamping, pending-value flush on release/cancel,
  double-click row-default reset, modified-state presentation, and pointer mapping moved intact;
  the workspace retains matrix values, row definitions, coalescing calls, and adjustment commands.
  The full QML cache and application link pass after this extraction, the deterministic edit-rail
  runtime route passes in 5.26 seconds, and the architecture guard requires the component to remain
  published rather than re-inline its implementation. The hue-replacement range rail now lives in
  `StudioHueRangeControl`; it owns its normalized hue mapping and swatch calculation, the explicit
  rainbow palette, exact 16 ms leading/trailing source-or-target queue, and release/cancel flush,
  while the workspace retains recipe values, sampling workflow, coalescing, and edit commands. The
  full QML cache and application link pass after the extraction, and the edit-rail route passes in
  5.33 seconds. The detail/texture focus pad and three vertical rails now live in
  `StudioDetailTextureControl`. The inline control previously emitted two recipe mutations for
  every focus-pad pointer event and one mutation for every rail event. The extracted owner now uses
  keyed 16 ms leading/trailing delivery so independently pending radius, sharpening, masking, luma
  noise reduction, and color noise reduction retain their latest values and flush before release
  or cancellation completes the coalesced interaction. Its QML cache and application link pass,
  the edit-rail runtime route passes in 5.44 seconds, and the architecture guard freezes both
  publication and the bounded delivery lifecycle. A direct QML component test proves actual signal
  delivery for this owner and the lens owner below: the first edit is immediate, repeated pending
  keys retain only their latest value, the trailing flush is deterministic, and pending state is
  cleared; it passes in 0.55 seconds. The optical-correction geometry pad and
  chromatic-aberration rail now live in `StudioLensCorrectionControl`. The former inline input also
  emitted two mutations per geometry event and one per chromatic event; the extracted owner uses a
  keyed 16 ms leading/trailing queue for all three values, flushes before release/cancel, formats
  its own summary, and retains explicit neutral resets. The complete QML cache and app link pass,
  the edit-rail runtime route passes in 4.94 seconds, and the architecture guard protects its
  publication and bounded delivery lifecycle. The Histogram, Waveform, Parade, and Vectorscope
  tile presentation now lives in the published `StudioScopeTile`. It retains the original
  histogram inputs and clipping ratios, waveform/density/vector drawing, size/visibility repaint
  triggers, controller histogram repaint, and five workspace display-option repaint connections;
  scope-domain and sampling policy remain in their typed controller services and workspace
  commands. The full QML cache and application link pass. The three focused scope service,
  coordinator, and session tests pass. The monolithic smoke test is intentionally skipped and a
  first attempted late-stop shard timed out in unrelated shared setup, so neither is counted as
  evidence. A focused direct runtime test instead instantiates the real published component with
  typed fake dependencies, exercises Histogram, Waveform, Parade, and Vectorscope modes, verifies
  large-Quad sizing and Histogram/Canvas visibility switching, toggles every presentation input,
  and emits the controller histogram signal through the repaint connection; it passes in 0.42
  seconds. Scope layout and dock presentation are now published separately as the 30-line
  `StudioScopeGrid` and 81-line `StudioScopeDockContent`. The grid owns only the responsive
  one/two-column repeater and tile instantiation; the dock owns the proof-status header, domain,
  dock-placement, presentation, layout, and sampling command surfaces while all policy and state
  remain in the workspace/controller context. Both types are part of the desktop QML module, the
  full application target compiles and links, and architecture guards prevent the inline versions
  from returning. Thumbnail/filmstrip presentation now lives in the published 210-line
  `StudioThumbnailCard`, with explicit workspace context and unchanged selected, rating,
  pick/reject, placeholder, aspect-badge, and activation behavior. The extraction exposed an
  invalid ownership boundary: the luminance-range drag sample patch had been nested inside every
  thumbnail even though it consumed `ImageViewport` geometry and preview state. That overlay now
  exists once inside `ImageViewport`; architecture checks prevent it from returning to the card.
  The complete QML cache and app link pass, the direct component test verifies bound asset state,
  signals, filmstrip children, selected/placeholder visibility, and absence of the viewport
  overlay, and the deterministic edit-rail route passes with it. The viewport decomposition has
  also begun at a presentation-only boundary: the viewport-bounded, visible-lines-only pixel-grid
  renderer is now the published 75-line `StudioPixelGridOverlay`, with explicit viewport geometry
  and theme context. The focus-check, clipping, peaking, false-color, gamut, zone-map, and
  color-tool-mask images plus their target labels now form the published 216-line
  `StudioDiagnosticOverlayStack`; it consumes explicit workspace, viewport, controller, and anchor
  context while leaving diagnostic generation and enablement policy with their existing owners.
  The complete QML cache and app link pass, as do the edit-rail, pixel-zoom, outline-trace, direct
  component, and coordinate-mapper proofs after their source contracts were routed through the
  published types. Preview-update/retry and mask-activity/progress presentation now share the
  published 69-line `StudioViewportStatusStack`, with processing, failure, activity text, progress,
  and retry behavior still supplied by the workspace/controller. The provider/native-texture loupe,
  crosshair, bounded positioning, zoom/pan carriage, and crisp-pixel selection now live in the
  published 86-line `StudioViewportLoupe`; sampling coordinates and enablement remain workspace/
  viewport state. Pixel-cell snapping and brush size/hardness/operation cursor presentation now
  live in the published 131-line `StudioViewportBrushCursors`; mask commands and input handling
  remain in the viewport/workspace. Clone-offset, clone-target, clone-source, and patch-source
  presentation plus their mapper-backed drag commands now form the published 293-line
  `StudioRetouchOverlayCommandSurface`. Unlike the passive overlay types, this boundary explicitly
  owns its two direct-manipulation commands; both remain routed through normalized viewport mapping
  and the typed preview-controller clone/patch APIs, with no render algorithm or policy moved into
  QML. Pan/zoom gestures, sampling dispatch, mask-tool dispatch, pixel-inspector sampling, and
  cancel/double-click handling now form the published 128-line
  `StudioViewportInputCommandSurface`; coordinate conversion and tool policy remain on their
  existing viewport/workspace owners. The complete QML cache and app link pass.
  After the complete viewport-owner extraction described above, `StudioWorkspace.qml` is now
  21,547 lines.
  Page, canvas, inspector, tool, mask/layer, and status ownership remain inline, so this item is
  partial.
- [x] Remove duplicated Edit and Studio workflow logic or select one production editing surface.
  `Main.qml` already instantiates `StudioWorkspace` as the production edit surface. The audit found
  that the otherwise unreferenced 10,632-line `EditView` still held the only UI for preview-target
  selection, proof intent/paper/ink simulation, ICC import/relink/clear, and DCP management. Those
  controls are now present on the production Studio surface through the extracted color-management
  panel; the complete app QML cache/executable build, architecture boundary, and desktop source
  contract pass. The obsolete `EditView.qml`, `EditWorkspace.qml`, and `EditWorkspaceHost.qml`
  surfaces are now physically deleted. Their source-only mask, shortcut, component, and workflow
  assertions were removed or migrated to `StudioWorkspace`, `MaskView`, and the focused component
  owners; `editor_preview_outline_trace_test` and `desktop_shell_source_contract_test` pass after
  deletion. The architecture boundary now requires all three files to remain absent while requiring
  `Main.qml` to instantiate `StudioWorkspace` directly and the active Studio surface to retain the
  adjustment-patch handoff.
- [~] Keep QML declarative: it presents state and issues commands; it does not decide render
  invalidation, cache policy, color targets, or migration behavior.
  The preview-quality seam now follows this rule. `updatePublishedPreviewQuality()` issues
  `setPreviewPresentationMetrics()` with presentation facts and contains no target-edge arithmetic,
  quality floors, supersampling constants, or direct `previewTargetEdge` assignment. The focused
  C++ policy and controller command own that decision. Viewport invalidation events now issue one
  publication command; `PreviewPublisher` decides settling, coalescing, sizing, deduplication, and
  forced retry. Other render invalidation, color-target, and migration decisions remain to be
  audited and extracted before this item can complete.
- [x] Split `color_adjustments.cpp` by engine stage and operator family without duplicating math.
  The immutable control-domain/stage metadata and lookup functions now live in
  `color_control_semantics.cpp`, and the reusable bounded worker pool now lives in
  `color_adjustment_scheduler.cpp` behind a small internal dispatch API. Monotone curve
  construction/evaluation, perceptual curve LUT generation, the public tone-curve evaluator, and
  layer/global tone-curve target evaluation now live in the 195-line
  `color_curve_evaluator.cpp` behind `color_curve_evaluator.h`, shared by CPU rendering and Metal
  LUT preparation. Declared-space resolution, transfer encoding/decoding, working/LUT
  matrix compilation, table validation, non-unit-domain normalization, trilinear sampling, neutral
  preservation, and stacked strength application now live in the 328-line
  `color_lut_runtime.cpp`; the adjustment monolith no longer implements the public LUT sampler.
  Modern monotone tone, the fixed-anchor compatibility implementation, exact legacy admission,
  filmic toe, adaptive contrast, levels, zone exposure, highlight-shoulder admission/compression,
  robust highlight-reference selection, and the bounded tone-table cache now live in the 550-line
  `color_tone_runtime.cpp`; the adjustment monolith no longer contains tone anchors, either
  photographic tone implementation, scalar scene-tone composition, shoulder math, or tone-table
  ownership.
  Global and layer RGB channel mixing, black-and-white channel response, hue-region selection,
  mixer-model normalization, and selective CMYK-style adjustment now live in the 227-line
  `color_mixer_runtime.cpp`. The caller supplies working-profile luminance to the black-and-white
  kernel, so the focused runtime does not duplicate color-space or stage ownership. The monolith
  cannot redeclare those operators, and CMake must compile the focused runtime, under the
  fail-closed architecture check. Sensor-domain RAW clipping analysis, bounded correction-reference
  sampling, reconstruction admission/strength, and opposed-channel reconstruction now live in the
  224-line `color_highlight_runtime.cpp`; working-profile legacy reconstruction remains at its
  declared later stage and does not leak into the camera-linear runtime. Shared bilinear sampling,
  mirrored crop-fill sampling, lens-vignette correction, chromatic-aberration sampling, perspective,
  distortion, transform, and liquify execution now live in the 261-line
  `color_spatial_runtime.cpp`; vignette work uses the existing bounded scheduler rather than owning
  another worker pool. Cancellation-aware box filtering, three-pass Gaussian approximation, guided
  luminance filtering, reusable workspaces, bounded scheduling, and the existing Metal Gaussian
  acceleration route now live in the 320-line `color_blur_runtime.cpp`, shared by detail, masks,
  healing, and layers. Smooth ramps, bell weights, circular hue selection, unit/radian wrapping,
  and shortest angular deltas now have one 97-line `color_math_runtime.cpp` owner shared by tone,
  mixer, spatial, masks, detail, and layers; the focused runtimes no longer duplicate these
  kernels. Working-profile context construction, scene headroom, native/D65 matrices, native
  luminance, linear-sRGB reference conversion, OKLab conversion, luminance-preserving scaling,
  lifted-shadow chroma stabilization, headroom fitting, and OKLab/OKLCH gamut fitting now live in
  the 529-line
  `color_working_space_runtime.cpp`; detail, tone, masks, and layers consume one color-space
  implementation. Spatial texture/clarity/dehaze and fine sharpening/luminance-noise/color-noise
  execution now live together in the 698-line `color_detail_runtime.cpp`. Backend capability
  admission remains coordinator-owned and is passed into the detail runtime, so the extraction does
  not create a second Metal policy authority. Post-tone perceptual color, grading, replacement,
  harmony, skin protection, HSL conversion, and highlight-chroma behavior now live in the 411-line
  `color_look_runtime.cpp`. The complete photo-layer family now lives in the 1,108-line
  `color_layer_runtime.cpp`: layer transforms, blend modes, opacity/mask/clipping composition,
  tone and curve payloads, creative color/LUT work, optical correction, and layer-local detail.
  It receives the authoritative common global/local adjustment kernel as a callback instead of
  copying exposure, white-balance, tone, or chroma math. Black-point compensation, output-profile
  and display-target resolution, working-to-output conversion, inverse output transforms, gamut
  analysis/mapping, and ACES 2 output-LUT compilation/application now live in the 828-line
  `color_output_runtime.cpp`. Preview, export, gamut warnings, and compatibility publication call
  that same owner rather than retaining coordinator-local output math. Selection geometry, range
  refinement, raster morphology, edge-aware mattes, mask-set composition, mask rasterization and
  identity, removal/heal sampling, clone, red-eye, skin cleanup, frequency smoothing, and inpaint
  now live in `color_mask_runtime.cpp`. Refined local-mask and mask-set hashing now lives in the
  dedicated 130-line `color_mask_hash_runtime.cpp`, sharing the public mask-weight evaluators
  instead of duplicating their math. Two unused Metal pre-tone support predicates were deleted
  rather than carried into the new unit, and the remaining mask runtime is 3,149 lines against its
  frozen 3,192-line ceiling. The adjustment coordinator consumes the focused mask API and no
  longer owns selection or retouch algorithms. Camera calibration and
  matching-look matrices, DCP/ICC camera-profile resolution, chromatic adaptation and white
  balance, RAW black/white normalization, camera-linear reconstruction admission, DCP profile
  stages, camera characterization, baseline exposure, and scene exposure now live in the 700-line
  `color_input_runtime.cpp`. Settled preview, export, and direct stage execution consume the same
  `InputStageParameters` contract. The shared global/local edit kernel, point-curve application,
  perceptual comparison, tone/look composition, reusable LUT construction, and LUT cache identity
  now live in the 762-line `color_adjustment_kernel_runtime.cpp`. Global, full-mask, local-mask, and
  photo-layer paths call this owner rather than copying exposure, white-balance, tone, or chroma
  behavior. Local brush acceleration, coordinate mapping, local mask-set evaluation, retouch
  sequencing, cancellation/telemetry, and complete Layers-stage coordination now live in the
  821-line `color_local_layer_runtime.cpp`; it delegates selection math, retouch algorithms, layer
  operators, and shared edit evaluation to their focused owners. The remaining
  `color_adjustments.cpp` is a 1,227-line capability and ordered-stage coordinator rather than an
  operator monolith. These extractions reduce it from 12,020 lines and tighten its non-growth
  ceiling to the current size.
  The nine-test RAW decoder/synthetic-pipeline-invariant/color pipeline/property/tool-invariant/chart-validation/
  layer-LUT-parity/render-parity/render-golden set passes without pixel or golden changes after the
  highlight extraction, and the complete desktop executable relinks successfully. The synthetic RAW invariant
  pass is not substituted for the still-open rights-cleared multi-camera corpus evidence. At that
  intermediate milestone, pixel operators and stage families still needed focused translation
  units. After the spatial extraction, the color pipeline/property/tool-invariant,
  layer-LUT/render parity, render-golden, and desktop layer-adjustment controller suites also pass
  without golden changes. After completing LUT ownership, the dedicated cube-LUT test and the
  six color/property/tool-invariant/layer-LUT/render-parity/render-golden suites pass without golden
  changes. After the blur-runtime extraction, the color pipeline/property/tool-invariant,
  layer-LUT/render parity, render-golden, and desktop layer-adjustment controller suites pass again.
  After consolidating scalar math, the six color/property/tool-invariant/layer-LUT/render-parity/
  render-golden suites pass without golden changes. After the working-space and complete detail-
  stage extractions, seven macOS color/property/tool-invariant/layer-LUT/render-parity/render-golden/
  desktop-layer suites pass, the non-Metal-capable `ci-headless` engine build is warning-clean, and
  its three focused color/property/tool-invariant tests pass. After the curve, shoulder, gamut,
  look, and complete photo-layer extractions, the same seven-test macOS slice passes again, the
  `ci-headless` build and its three focused tests pass, the architecture boundary passes, and the
  complete desktop executable relinks. After output-runtime extraction, nine macOS tests add the
  ACES/OCIO and color-transform reference fixtures to that color/layer/parity/golden slice; all
  pass. Five corresponding `ci-headless` tests pass, the architecture boundary passes, and the
  complete desktop executable relinks again. After complete mask/retouch extraction, twelve macOS
  color/property/tool-invariant/mask-math/mask-history/outline/layer/export/parity/golden tests pass,
  the valid three-test `ci-headless` color slice passes, the architecture boundary passes, and the
  desktop executable relinks. The mask-LUT regression now compares masked output with the same
  full-mask production ACES render instead of assuming a pre-output-transform pure-red pixel, and
  the outline source contract follows the extracted layer owner. No focused golden or parity
  expectation changed. After input-runtime extraction, twelve macOS color/property/DCP
  conformance/profile/RAW-decoder/parity/golden tests pass, nine corresponding headless
  color/property/tool-invariant/DCP tests pass, the architecture boundary passes, and the desktop
  executable relinks without changing a reference or golden expectation. After shared-kernel
  extraction, fourteen macOS color/property/DCP/RAW/mask/layer/export/parity/golden tests pass, six
  focused headless color/property/DCP/tool-invariant tests pass, the architecture boundary passes,
  and the desktop executable relinks again. After the final local/Layers-stage extraction, ten
  macOS color/property/tool-invariant/mask/layer/export/parity/golden tests pass, the valid three-test
  headless color slice passes, the architecture boundary passes, and the desktop executable relinks
  without warnings from the remaining coordinator or changes to golden expectations.
- [~] Split recipe schema, serialization, migration, mask payload, validation, and UI adaptation into
  separate translation units.
  Atomic sidecar path/import/export now lives in `edit_recipe_sidecar.cpp`, recursive normalized
  recipe comparison lives in `edit_recipe_diff.cpp`, and version admission plus canonical
  migration lives in `edit_recipe_migration.cpp`. Current-document validation now lives in
  `edit_recipe_validation.cpp`, with ICC/DCP asset policy isolated in
  `edit_recipe_profile_validation.cpp` and history/mask-payload validation isolated in
  `edit_recipe_content_validation.cpp`. The header no longer imports `QSaveFile` or owns filesystem
  writes, recursive diff execution, migration execution, JSON/schema validation, profile byte/hash
  limits, or history/mask payload validation. ICC/DCP identity encoding now lives in
  `edit_recipe_profile_codec.cpp`, point-curve encoding in `edit_recipe_curve_codec.cpp`, and
  recursive photo-layer binary payload encoding in `edit_recipe_layer_codec.cpp`. Generic
  fallback JSON readers now live in `edit_recipe_json_readers.cpp`, named history snapshots in
  `edit_recipe_history.cpp`, renderer-model normalization in
  `edit_recipe_model_normalization.cpp`, and adjustment-group normalization in
  `edit_recipe_group_normalization.cpp`. Output/input/working/monitor color-space policy, preview
  mode, render intent, gamut mapping, camera look, upright, highlight mode, chromatic adaptation,
  and demosaic-quality normalization now also live in `edit_recipe_model_normalization.cpp`;
  `QFileInfo` is no longer imported by the state header. Group-scoped state merging now lives beside
  group-name normalization in `edit_recipe_group_normalization.cpp`, so catalog copy/apply does not
  require another header-owned field mapping. The canonical document parser, object builder, and
  compact JSON serializer now live in `edit_recipe_document_codec.cpp`; the shared state header no
  longer imports `QJsonDocument` or compiles the roughly 2,850-line persistence mapping into every
  consumer. The mask schema, payload normalization, typed mask-set/source models, and mask
  serialization helpers now have a focused ownership boundary in `edit_recipe_mask_schema.h`
  instead of being embedded in the state definition. These extractions shrink
  `edit_recipe_state.h` from 7,834 to 624 lines. Structured legacy-mask state queries now live in
  the 28-line `edit_recipe_mask_state_queries.h`, and matte cleanup defaults live in the 20-line
  `edit_recipe_mask_types.h`; the focused mask header is 2,967 lines against its separate
  2,977-line growth cap. Export profile-name policy likewise moved out of
  `photo_render_utils.cpp` into the 56-line `output_profile_names.h`, reducing the render utility
  to 3,355 lines against its 3,393-line cap without changing output color-space selection.
  `edit_recipe_state_test` proves legacy versions, future-version rejection, current-schema
  validation, invalid ICC/DCP identity rejection, bounded mask payloads, canonical migration,
  diffing, explicit preview-target persistence, and atomic sidecar round-trip;
  `rendering_migration_proof_test` proves explicit legacy preview, apply, undo, redo, and reload
  through these units. After the additional decomposition, color-tool invariants, preview mask
  math, recipe state, render parity, render golden, and export controller tests pass 6/6 in 25.90
  seconds; the complete `Framehouse` target compiles and links, and the architecture and diff
  checks pass. Mask implementation remains mostly header-compiled behind its focused boundary, and
  UI adaptation still shares the desktop recipe representation, so this item is partial.
- [x] Add dependency rules that prevent the engine from importing Qt and prevent QML/controller code
  from bypassing the canonical compiler/executor.
  The first boundary is now enforced at configure time:
  `photo_editor_enforce_qt_free_target(photo_engine)` inspects every engine C/C++/Objective-C++
  source for Qt imports and rejects Qt link targets. Its CI fixture proves a clean engine target
  configures while both a `QImage` include and a `Qt6::Core` link fail with the named boundary.
  Modern agent analysis, learned-style reference, and before/after validation proofs now route
  through one `render_agent_proof_image()` owner. It admits an asset-bound v13 plan before calling
  the shared output executor, publishes the exact plan-cache identity in analysis and validation
  reports, and fails the proof rather than silently falling back when modern compilation fails.
  The explicitly detected pre-v13 renderer remains available only for compatibility and migration
  comparisons. The architecture test permits one `render_output_image()` call in the agent
  controller—the focused owner—and rejects new proof-render call sites or restoration of the
  request-less controller adjustment API. It also requires flattened export to remain a controller
  delegation and requires its focused owner to retain canonical admission plus executor-owned file
  decode. The shared production `render_output_image()` and display-target entry points now fail
  closed with `canonical_plan_required_for_aces2` whenever modern output lacks an admitted plan;
  preview geometry execution enforces the same runtime contract. Plan-less rendering remains only
  in the explicitly named diagnostic backend function and exact `legacy-oklch-v1` compatibility
  path. The architecture scan rejects every production desktop caller of the diagnostic backend,
  while engine/operator tests may use it explicitly without presenting that route as shipping-path
  proof. `render_parity_test` proves modern rejection, exact failure telemetry, successful canonical
  execution, and retained explicit legacy migration rendering. QML has no engine import or render
  entry point, and controllers cannot reach the diagnostic backend under the enforced source graph.
  This completes the dependency item without treating a source-token assertion alone as runtime
  proof.
- [~] Add focused tests per service and retain cross-service workflow tests.
  `system_monitor_profile_state_test` now instantiates `DisplayProfileService`, proves verified
  generation/cache ownership and transient same-display retention, rejects empty and invalid ICC
  discovery payloads, identity-binds a valid ICC payload, and retains the lower-level
  reconciliation/calibration cases. `color_pipeline_property_test` and
  `color_tool_invariant_test` pass against the extracted semantics unit.
  `color_adjustment_scheduler_test` proves serial and parallel exact-once execution plus exception
  propagation for the extracted worker service.
  `render_executor_service_test` proves stable process-wide preview/export ownership, independent
  cache identities and budgets, and idempotent ACES 2 prewarm admission.
  `ux_representative_workflow_test` remains green as the cross-service import/edit/mask/export
  gate. The fourteen-test macOS architecture slice also passes after the current recipe, display,
  QImage-boundary, curve, and QML extractions:
  `system_monitor_profile_state_test`, `color_pipeline_property_test`,
  `color_tool_invariant_test`, `qimage_color_conversion_test`,
  `render_executor_service_test`, `edit_recipe_v13_persistence_test`,
  `recipe_asset_store_test`, `catalog_asset_model_test`,
  `desktop_shell_source_contract_test`, `desktop_shell_crash_recovery_ux_test`,
  `ux_representative_workflow_test`,
  `crash_recovery_relaunch_test`, `edit_recipe_state_test`, and
  `rendering_migration_proof_test`. The current headless configuration/build also passes 32/32;
  its two Metal device-parity tests are explicitly skipped on the non-Metal preset rather than
  counted as hardware evidence. After canonical agent-proof admission,
  `agent_edit_controller_test` passes and verifies non-empty canonical plan identities for modern
  analysis and before/after proof reports; the exact macOS run took 136.85 seconds, so it is
  behavioral evidence, not evidence that Phase 7 latency budgets are complete. The desktop app
  also relinks with the controller bypass removed. After flattened export delegation, the complete
  `editor_preview_controller_test` passes in 308.69 seconds, including its exported-dimensions,
  visible-layer composition, reorder, undo/redo, and blend-mode assertions; the app relinks and the
  architecture boundary passes. This long aggregate run is behavioral regression evidence, not a
  latency-budget result. After fail-closed modern output admission, `render_parity_test` passes in
  2.34 seconds, `catalog_asset_model_test` in 25.37 seconds, `export_job_controller_test` in 17.03
  seconds, `editor_preview_feedback_test` in 11.84 seconds, `agent_edit_controller_test` in 143.52
  seconds, and the complete `editor_preview_controller_test` in 309.18 seconds. The controller test
  now gives its manually constructed modern mask request a stable asset identity and requires
  canonical compilation; operator-only layer/mask/HDR comparisons call the explicitly diagnostic
  backend rather than the production wrapper. More services must be extracted before this item can
  complete. `rendering_migration_proof_test` also passes in 3.99 seconds, proving the explicit
  compatibility renderer still previews, migrates, applies, undoes, redoes, and reloads after the
  modern fail-closed boundary; `edit_recipe_pipeline_bridge_test` passes in 0.31 seconds with the
  focused flattened-output compiler call recognized as another consumer of the sole shared
  compiler rather than another compiler implementation. After extracting preview-quality policy
  from QML, `edit_render_policy_test` passes in 0.03 seconds, `editor_preview_feedback_test` in
  9.05 seconds, and `desktop_shell_source_contract_test` in 1.05 seconds; the complete macOS QML
  cache and desktop executable also rebuild and link successfully. After the `PreviewPublisher`
  extraction, `preview_publisher_test` passes in 0.17 seconds,
  `canvas_coordinate_mapper_test` in 0.17 seconds, `desktop_shell_source_contract_test` in 0.51
  seconds, `editor_preview_feedback_test` in 11.33 seconds, `editor_preview_pixel_zoom_test` in
  2.53 seconds, and `editor_preview_outline_trace_test` in 3.68 seconds. The seven-test slice is
  7/7 green, the architecture boundary and diff check pass, and the complete app QML cache and
  executable rebuild and link. After interactive/full timer ownership moved to `RenderScheduler`,
  `render_scheduler_test` passes in 0.40 seconds, `editor_preview_feedback_test` in 11.78 seconds,
  `rendering_migration_proof_test` in 4.06 seconds, `editor_preview_outline_trace_test` in 4.01
  seconds, and `editor_preview_layer_adjustment_test` in 23.49 seconds. The complete
  `editor_preview_controller_test` also passes in 307.56 seconds, covering the broad scheduling,
  RAW, layer/mask, export, cache, cancellation, and undo/redo workflows rather than treating the
  focused timer test as sufficient proof. The production app rebuilds and links with no controller
  warning after deletion of the now-unused initialization-only debounce helper. After canonical
  admission, compilation, serialization, concurrency, and LRU ownership moved to
  `RenderPlanStore`, `render_plan_store_test` passes in 0.04 seconds and proves fail-closed identity
  and dimension validation, byte-identical cache reuse, and target-sensitive plan identity.
  `edit_recipe_pipeline_bridge_test` passes in 0.06 seconds, `render_parity_test` in 2.31 seconds,
  `rendering_migration_proof_test` in 4.03 seconds, `editor_preview_feedback_test` in 11.82 seconds,
  `export_job_controller_test` in 16.84 seconds, `catalog_asset_model_test` on isolated rerun in
  25.16 seconds, `agent_edit_controller_test` in 142.57 seconds, and the complete
  `editor_preview_controller_test` in 307.73 seconds. The first catalog run missed its asynchronous
  20-photo thumbnail deadline but passed when isolated; Carousel compiled and linked, then its
  runtime test stopped at the pre-existing external `pdfinfo` requirement. The complete macOS QML
  cache and desktop executable rebuild and link with the new store.
  After render-generation and publication-order ownership moved to `PreviewSession`,
  `preview_session_test` passes in under 0.01 seconds and proves shared cancellation-token updates,
  stale-completion rejection, independent interactive/full admission, and monotonic publication
  revisions. The exact session, feedback, pixel-zoom, smart-selection-stale, outline, layer, and
  timeline-revision contract set passes 7/7 in 45.49 seconds; the complete
  `editor_preview_controller_test` passes in 306.07 seconds. The macOS QML cache and complete
  desktop executable also rebuild and link.
  After mask worker lifecycle ownership moved to `MaskSession`, `mask_session_test` passes in 0.27
  seconds, `editor_preview_smart_selection_stale_test` in 1.13 seconds,
  `editor_preview_layer_mask_history_test` in 1.10 seconds, and `editor_preview_feedback_test` in
  11.76 seconds. After normalized stack, active selection, preview mode, and retry payload ownership
  followed, the focused set passes 4/4 in 14.13 seconds and the complete
  `editor_preview_controller_test` passes in 310.22 seconds, covering brush rasterization, smart
  selection, inpaint, masks/layers, cancellation, history, retry, and stale publication. The
  active-definition, bounded-preview, outline, color-tool, and smart-selection caches then moved
  behind the same session. `mask_session_test`, `editor_preview_feedback_test`,
  `editor_preview_smart_selection_stale_test`, and `editor_preview_outline_trace_test` pass 4/4 in
  17.30 seconds, and the complete controller test passes in 309.06 seconds. The architecture
  boundary and diff check pass, and the macOS app rebuilds and links.
  After durable layer, visibility-snapshot, and layer-adjustment clipboard ownership moved to
  `LayerSession`, `layer_session_test`, `editor_preview_layer_mask_history_test`, and
  `editor_preview_layer_adjustment_test` pass 3/3 in 24.87 seconds. The complete
  `editor_preview_controller_test` passes in 310.27 seconds, covering persistence, thumbnails,
  layer/mask interaction, exports, undo/redo, and RAW rendering across the extracted ownership
  boundary. The architecture boundary and diff check pass, and the complete Framehouse target,
  including the macOS QML cache and executable, rebuilds and links.
  After mask future/watcher and payload ownership moved to `MaskWorkerCoordinator`,
  `mask_worker_coordinator_test`, `editor_preview_smart_selection_stale_test`,
  `editor_preview_layer_mask_history_test`, and `editor_preview_feedback_test` pass 4/4 in 8.94
  seconds. The complete `editor_preview_controller_test` passes in 310.70 seconds, covering the
  smart-selection, brush, inpaint, cancellation, stale-publication, asset-switch, and persistence
  callbacks after QObject watcher ownership moved. The architecture boundary and diff check pass,
  and the complete Framehouse QML cache and executable rebuild and link.
  After complete preset-domain ownership moved to `PresetService`, `preset_service_test` passes in
  0.21 seconds, `editor_preview_layer_adjustment_test` in 23.64 seconds, and
  `editor_preview_outline_trace_test` in 3.99 seconds. The complete
  `editor_preview_controller_test` passes in 308.97 seconds, covering preset save/load,
  import/sync, version contracts, favorites, intensity, camera defaults, layer application, and
  persistence across the service boundary. The architecture boundary and diff check pass, and the
  complete Framehouse QML cache and executable rebuild and link.
  After history and edit-transaction ownership moved to `HistoryService`, `history_service_test`,
  `editor_preview_history_test`, `editor_preview_coalesced_undo_test`, and
  `editor_preview_layer_mask_history_test` pass 4/4 in 2.44 seconds. The complete
  `editor_preview_controller_test` passes in 307.52 seconds, covering per-asset restore, bounded
  history, snapshots, undo/redo, coalesced edits, persistence deferral, layers/masks, brush capture,
  and render coordination after transaction-depth ownership moved. The architecture boundary and
  diff check pass, and the complete Framehouse QML cache and executable rebuild and link.
  After shared thumbnail queue lifecycle ownership moved to `ThumbnailRefreshCoordinator`,
  `thumbnail_refresh_coordinator_test` passes in 0.33 seconds and directly proves same-signature
  coalescing, supersession cancellation, generation advancement, deferred accounting, active-result
  validation, completion consumption, stale-result accounting, warning deduplication, diagnostics,
  and reset semantics. `mask_worker_coordinator_test` and `editor_preview_feedback_test` also pass;
  the complete `editor_preview_controller_test` passes in 307.39 seconds, covering the real queue
  across thumbnails, RAW rendering, masks, layers, history, persistence, and export. The complete
  Framehouse QML cache, controller translation unit, and executable rebuild and link.
  `editor_preview_outline_trace_test` initially exposed two stale source-contract expectations that
  still looked for the layer-thumbnail ownership contract in the controller after it had moved to
  the queue service. Those assertions now follow the actual owner and the test passes in 4.14
  seconds; no production compatibility token was restored to satisfy the test.
  After thumbnail cache state and bounded LRU execution moved to `ThumbnailCacheSession`,
  `thumbnail_cache_session_test` passes in 0.33 seconds and directly proves initial invalidation,
  derived snapshot identity, miss/hit/store/byte telemetry, LRU promotion and eviction, oversize
  rejection, worker identity/counters, and entry release without lifetime-telemetry loss.
  `thumbnail_refresh_coordinator_test` and `editor_preview_layer_adjustment_test` pass with it, 3/3
  in 24.43 seconds, covering queued mask/layer thumbnail fill, cache reuse, invalidation, slider
  deferral, and worker publication. The complete `editor_preview_controller_test` passes in 310.34
  seconds after the state migration. The architecture boundary and diff check pass, and the
  complete Framehouse QML cache and executable rebuild and link.
  Request admission now also avoids complete edit-state hashing on the UI thread. Layer-composite
  alpha traces and mask-bearing cache identities are deferred to workers, while revision identity
  continues to reject stale results. Thumbnail-dependent controller proofs explicitly wait for the
  asynchronous queue before asserting fresh rows; they no longer depend on the deleted synchronous
  getter side effect. The focused thumbnail/feedback/outline/layer slice passes 6/6 in 53.06
  seconds, the complete controller suite passes through its RAW, mask, layer, inpaint, history,
  persistence, cache, cancellation, and export cases, and the complete Framehouse target rebuilds
  and links.
- [~] Set maintainability gates:
  no new god object, no new duplicated recipe mapping, no production source-contract test as a
  substitute for runtime behavior, and no new controller-owned render algorithm.
  The CI architecture-boundary test now freezes non-growth ceilings for the 43,845-line controller
  implementation and 2,707-line controller header, 24,133-line Studio workspace, 228-line
  `StudioAdjustmentPad2D`, 31-line `StudioAdjustmentSlider`, 244-line
  `StudioChannelPadControl`, 290-line `StudioDetailTextureControl`, 164-line
  `StudioGradingMacroControl`, 94-line
  `StudioCollapsibleGroup`, 212-line `StudioHueRangeControl`, 31-line `StudioInspectorGroup`,
  207-line `StudioLensCorrectionControl`, 203-line
  `StudioRgbMixerMatrix`, 259-line `StudioScopeTile`, 30-line `StudioScopeGrid`, 81-line
  `StudioScopeDockContent`, 210-line `StudioThumbnailCard`, 75-line
  `StudioPixelGridOverlay`, 216-line `StudioDiagnosticOverlayStack`, 69-line
  `StudioViewportStatusStack`, 86-line `StudioViewportLoupe`, 131-line
  `StudioViewportBrushCursors`, 293-line `StudioRetouchOverlayCommandSurface`, 128-line
  `StudioViewportInputCommandSurface`,
  164-line `PreviewPublisher`, 75-line
  `PreviewSession`, 391-line `MaskSession`,
  75-line `LayerSession`, 400-line `MaskWorkerCoordinator`, 345-line
  `ThumbnailRefreshCoordinator`, 194-line `PresetService`,
  210-line `ThumbnailCacheSession`, 115-line `MaskPresentationSchema`, 38-line
  generated-mask-cleanup interface and 245-line implementation, 34-line generated-mask-metadata
  interface and 185-line implementation, 23-line generated-mask-quality interface and 124-line
  implementation, 35-line generated object-removal-assist interface and 223-line implementation,
  33-line segmentation-mask-operations interface and 118-line implementation,
  27-line sensor-dust-detection interface and 247-line implementation,
  41-line sensor-dust-map-store interface and 115-line implementation,
  27-line retouch-mask-detection interface and 195-line implementation,
  35-line smart-selection-geometry interface and 122-line implementation,
  22-line smart-selection-instance-policy interface and 248-line implementation,
  32-line smart-selection-polygon-geometry interface and 206-line implementation, 45-line smart-selection-
  prompt-schema interface and 415-line implementation, 49-line segmentation-result-schema
  interface and 201-line implementation, 34-line segmentation-request-policy interface and
  188-line implementation, 39-line object-pick-policy interface and 206-line implementation,
  15-line localized-object-refinement interface and 214-line implementation,
  48-line brush-input-schema interface and 321-line implementation,
  25-line brush-stroke-sampling interface and 183-line implementation,
  22-line brush-rasterizer interface and 319-line implementation,
  12-line brush-matte-rebuild interface and 81-line implementation,
  19-line brush-outline-geometry interface and 145-line implementation,
  40-line mask-outline-simplification interface and 246-line implementation,
  35-line mask-outline-presentation interface and 322-line implementation,
  21-line mask-outline-tracing interface and 480-line implementation,
  68-line mask-range-preview-math interface and 501-line implementation,
  828-line `ThumbnailPresentationService` implementation and
  80-line presentation-service interface,
  1,301-line `ThumbnailRefreshQueueService` implementation and 344-line interface,
  179-line `HistoryService`,
  348-line `HistogramScopeService` implementation and 47-line interface,
  110-line `HistogramScopeCoordinator`, 105-line `HistogramScopeSession`,
  63-line `RenderScheduler`, 287-line
  `RenderPlanStore` implementation, 58-line store interface, 23-line RAW decode policy, and
  3,393-line shared photo-render utility,
  1,227-line adjustment-engine coordinator ceiling,
  762-line shared-adjustment-kernel ceiling, 821-line local-and-Layers-stage-runtime ceiling,
  550-line focused tone-runtime ceiling,
  328-line focused LUT-runtime ceiling, 320-line focused blur-runtime ceiling,
  698-line focused detail-runtime ceiling, 529-line focused working-space-runtime ceiling,
  195-line focused curve-runtime ceiling, 411-line focused look-runtime ceiling,
  1,108-line focused photo-layer-runtime ceiling,
  828-line focused output-runtime ceiling,
  3,192-line focused mask-and-retouch-runtime ceiling,
  700-line focused camera-and-input-runtime ceiling,
  224-line focused RAW-highlight-runtime ceiling,
  261-line focused spatial-runtime ceiling, 624-line desktop recipe state
  header, and 2,977-line focused mask schema. It
  also forbids increasing the current 1,182
  controller method count and prevents the controller from reclaiming `QSettings` or monitor
  profile cache ownership from `DisplayProfileService`, platform ICC discovery from
  `display_profile_discovery`. It also prevents the color monolith from reclaiming control
  semantics, worker-scheduler, curve-evaluator, post-tone look, working-space gamut-fit,
  highlight-shoulder, photo-layer, output-rendering, mask-and-retouch, camera/input, shared
  global/local adjustment-kernel, or local/Layers-stage ownership, and prevents the recipe header
  from reclaiming sidecar
  I/O, recursive diffing, migration execution, document/profile/content validation, or the
  extracted profile/curve/layer codecs, generic JSON readers, history encoding, or renderer,
  color-policy, group normalization/merging, and document codec. The Studio guard also prevents
  its extracted row, title, panel, input, and color-management types from returning inline and
  requires all five to remain published by the desktop QML module. The desktop source scan makes
  `render_executor_service.cpp` the sole owner allowed to construct preview/export executors or
  define ACES prewarming, and requires the app target to publish that service. It also makes
  `RenderScheduler` the sole owner of the interactive/full preview timers, requires controller
  scheduling and cancellation to delegate through it, and rejects restoration of either controller
  timer field. It makes `RenderPlanStore` the sole caller of the canonical desktop plan compiler,
  requires every production admission client to delegate through the store, freezes its focused
  implementation/interface ceilings, and rejects the deleted free admission header or API. It
  requires `PreviewSession` to own generation advancement, asynchronous cancellation identity,
  stale-result publication ordering, and preview revision while rejecting restoration of the four
  former controller fields. It requires `MaskSession` to own the normalized mask stack, active
  selection, preview mode, retry payload, mask-work revision, the four independent mask worker
  generations, their cancellation telemetry, and all five mask-specific cache domains while
  rejecting restoration of the former controller fields and cache containers. It requires
  `LayerSession` to own the normalized photo-layer list, saved visibility snapshots, and copied
  adjustment-layer recipe while rejecting restoration of the four former controller fields. It
  requires `MaskWorkerCoordinator` to own all five mask worker watchers, pending smart-selection
  payloads, latest brush/inpaint/smart-selection job identities, and shared brush generation while
  rejecting restoration of the former controller fields. It requires
  `ThumbnailRefreshCoordinator` to own shared thumbnail timing, cancellation, pending/active
  identity, completion, and telemetry while rejecting restoration of the former controller queue
  fields. It requires `ThumbnailRefreshQueueService` to own revision-only identity, immutable
  request/job construction, and ordered cache publication; rejects controller-owned request-job
  helpers and full serialized edit-state hashing in queue admission; and requires its direct
  service contract test. It requires `ThumbnailCacheSession` to own thumbnail dirty/deferred state, derived
  snapshots and identity, bounded entry storage/eviction, worker identities, and cache telemetry
  while rejecting restoration of the former controller fields. It requires
  `ThumbnailPresentationService` to own mask-set derivation, source/dependency audit metadata,
  aggregate combination state, cache-only mask/layer row assembly, progressive reuse, and
  contained failure presentation while rejecting restoration of controller-local grouping. The
  admission guard also requires revision-based render identity,
  deferred exact mask traces/cache identities, constant-time grouped-mask availability fields, and
  native-texture overlay-first feedback while rejecting synchronous exact mask hashing in
  `currentRenderRequest()`. It requires `PresetService` to own
  built-in and user preset state, every metadata collection, favorites, intensity, and camera
  defaults; requires atomic replacement/upsert/rename/move/delete operations; and rejects
  restoration of the former fifteen controller fields. It requires `HistoryService` to own cached
  asset state, undo/redo state-label pairs, snapshots, access-order eviction, recursive-application
  suppression, and nested edit transactions while rejecting restoration of the former fourteen
  controller fields. It requires `HistogramScopeCoordinator` to own the async watcher, full pending
  request snapshot, latest-only coalescing, and active lifecycle while rejecting restoration of the
  former eleven controller fields and requiring its deterministic concurrency test. It requires
  `HistogramScopeSession` to atomically own all typed histogram/scope arrays, sample and clipping
  counts, result identity, independent RAW-clipping publication, visibility, normalized domain
  names/labels, and Proof availability/fallback while rejecting restoration of the former
  twenty-five controller fields and controller-local domain policy and requiring complete
  field-by-field behavioral proof. The source graph now
  also rejects production use of `render_output_image_with_backend`, requires the modern output and
  preview fail-closed reason, and keeps flattened export behind the narrow
  `flattened_output_renderer.h` interface so the controller does not import the collision-prone
  broad render-utility surface. The Studio source guard also requires the workspace to publish
  presentation facts via `setPreviewPresentationMetrics()`, forbids it from reclaiming preview
  edge/floor/supersampling
  math, and requires the focused policy plus controller delegation to remain present. It also
  forbids QML-owned viewport settle timers, publication signatures, queued flags, and patch-edge
  policy while requiring `PreviewPublisher` ownership and controller delegation. These are debt
  ceilings, not
  acceptable target sizes, and
  runtime service tests remain mandatory;
  duplicated recipe-mapping and
  controller-algorithm semantic checks still need stronger compiler/dependency enforcement. The
  Windows monitor-profile contract
  now verifies WCS/advanced-color discovery in the extracted owner and timer/refresh coordination
  in the controller, and explicitly rejects moving WCS calls back into the controller. The complete
  32-test `ci-headless` suite passes after all current recipe, scheduler, semantics, and curve
  extractions; the two physical Metal parity members remain explicitly skipped in the headless
  preset. The macOS app QML cache and executable also build after the Studio extraction.
- [x] Delete the dormant AI walkthrough implementation after the active conversation component
  replaced it. `AgentWalkthroughDialog` no longer retains an uninstantiated
  `legacyContentItem`, its six private delegate types, or the obsolete review tree. The active
  `AgentConversationView` owns the real four-page flow and its review page exposes actual
  before/after sources plus discard and batch-approval actions; the focused contract and visual
  shard prove the instantiated path.
- [x] Remove superseded timeline presentation branches found during baseline repair.
  `TimelineClipContent` no longer carries the unused legacy effects property, and
  `TimelineEffectLibraryRow` no longer performs its own dead search filtering after the panel
  became the sole filtered-model owner. Effect and media rows also present their current text
  without silent ellipsis, matching the desktop visibility contract.
- [x] Extract custom brush-preset schema mapping from the preview controller.
  Name normalization, exact numeric/boolean snapshots, and guarded preset application now live in
  the 77-line `brush_preset_schema.h`; the controller only coordinates the calls and is 43,794
  lines against its unchanged 43,845-line ceiling. The architecture boundary passes with explicit
  ceilings for the new brush and retained-surface owners. A complete Framehouse rebuild exposed an
  unrelated Qt macro collision in the current agent calibration work: its local `signals` variable
  was renamed to `threshold_signals` without changing the persisted `thresholdSignals` contract.
  `agent_edit_controller_test` passes in 29.19 seconds and the complete application then compiles
  and links warning-clean.

### Exit gate

- The desktop controller coordinates rather than implements the image pipeline.
- The engine can compile and execute representative recipes without Qt.
- QML contains no color-space or render-backend policy.
- Preview, export, thumbnail, and carousel callers are thin graph clients.

## Phase 9 — Simplify catalog and product foundations

### Work

- [x] Correct the rejected SQLite cutover and restore PostgreSQL authority.
  The product owner confirmed PostgreSQL was an intentional requirement and the previous Phase 9
  inference was invalid. `CatalogRuntime` now wraps `CatalogService`, preserves an explicit
  `PHOTO_EDITOR_DATABASE_URL`, and otherwise uses `LocalCatalogDatabaseManager` to initialize and
  health-check a private loopback-only cluster. The managed process/state/port/runtime discovery
  code, `prepare-postgres-runtime.sh`, Rust dependencies, app environment propagation, `run-native`
  startup branch, CMake packaging requirement, and macOS package verification are restored. Newer
  v13 recipe, review, timeline, carousel, job, and migration-record fields remain in the PostgreSQL
  repository and migrations. The SQLite repository, PostgreSQL-to-SQLite migrator, normal helper
  routes, and production package entry are deleted. Architecture checks now fail if SQLite returns
  as a production authority.
  The restored catalog crate and every helper binary compile offline; managed-database unit tests
  pass 3/3; `app_bootstrap_test` passes; and fresh isolated PostgreSQL 14 clusters initialized,
  applied migrations 0001 through 0012, reported healthy, served `catalog_snapshot`, and stopped
  cleanly. The manual safety path now produces an owner-only custom-format `pg_dump`, validates it
  with `pg_restore --list`, creates a pre-restore rollback dump, performs fail-fast restore, and
  automatically reapplies the rollback dump if restore fails. A real isolated proof created
  `BeforeBackup`, backed up, created `AfterBackup`, restored, retained `BeforeBackup`, removed
  `AfterBackup`, passed health check, and shut down cleanly. Automatic restore is intentionally
  rejected for externally managed servers. Recipe migration acceptance also persists the legacy
  compatibility view, canonical v13 recipe, and migration audit record in one PostgreSQL
  transaction. Migration 0012 was corrected to use PostgreSQL-supported JSONB key exclusion
  instead of the unavailable `jsonb_object_length()` call. Clean macOS/Windows package, upgrade,
  corrupt-dump rollback, crash recovery, and large-library proofs remain open.
  The packaging closeout audit rebuilt all twelve catalog helper binaries from current source in
  release mode and found that the macOS verifier omitted `accept_recipe_migration` and
  `catalog_move_library`; it now requires both. Strict packaging also requires the `postgres`
  server executable in addition to `initdb`, `pg_ctl`, `pg_dump`, and `pg_restore`, preserves source
  executable permissions, and rejects a nominal Sparkle toggle without the framework, feed URL,
  and EdDSA public key. A dedicated staged Debug install proved that the application plus all twelve
  helpers are installable. It also exposed why the installed Homebrew PostgreSQL 14 prefix is not
  release evidence: its executables link to libraries outside the copied prefix, and the original
  install rule stripped their execute bits. Permissions are corrected, and `build-release.sh` now
  fails before tests/build/configure when any Mach-O runtime dependency points into Homebrew or
  when `share/**/postgres.bki` is absent. The local Homebrew candidate is therefore deliberately
  rejected rather than mislabeled portable. The install now copies only the runtime `bin`, `lib`,
  and `share` trees, excluding developer headers, package-manager metadata, manuals, and HTML
  documentation. A licensed self-contained runtime archive, signed/notarized DMG verification, and
  clean-machine launch remain open. The
  ordinary macOS release-compilation CI job no longer claims strict production packaging without
  any publisher, PostgreSQL, or Sparkle inputs; credentialed packaging remains in the release
  workflow. Final DMG verification now requires `Sparkle.framework`, `SUFeedURL`, and
  `SUPublicEDKey` in addition to the complete catalog/PostgreSQL inventory. Qt deployment appeared
  idle because `macdeployqt` suppresses its per-file work, but a process trace showed active
  `install_name_tool` relocation and the bounded install subsequently completed. The staged bundle
  retained execute permissions for all twelve catalog helpers plus `initdb`, `pg_ctl`, `pg_dump`,
  `pg_restore`, and `postgres`. From that bundle, `catalog_local_db` discovered its relative runtime,
  initialized an isolated cluster under `/tmp/framehouse-packaged-postgres-proof.ZAm3PQ`, applied
  the current schema, reported healthy on loopback, produced an empty canonical catalog snapshot,
  wrote both migration and manual custom-format backups with mode `0600`, and stopped cleanly. This
  proves staging and relative runtime discovery on the development host; it does not promote the
  Homebrew input to portable or clean-machine evidence.
  A follow-up authority audit removed the obsolete `libsqlite3-dev` package from all three Linux CI
  jobs after `cargo tree -e features` confirmed the catalog has no active SQLite dependency. It
  corrected release documentation that still instructed packages to contain no PostgreSQL runtime,
  replaced a stale schema-version-10/integrity status contract with the real managed PostgreSQL
  status and post-migration snapshot contract, and consistently names the packaged `postgres`
  server executable. The architecture boundary now rejects those stale release phrases, a SQLite
  CI package dependency, or omission of the self-contained PostgreSQL/no-SQLite release contract.
  The boundary and diff checks pass, and all seven focused local-database tests pass, including
  corrupt-dump rejection before restore and refusal to auto-restore an external PostgreSQL server.
  `scripts/test-postgres-catalog-runtime.sh` now makes the staged-package lifecycle repeatable. It
  overrides every runtime path with a fresh `/tmp` root, initializes and migrates managed
  PostgreSQL, backs up pre-mutation state, proves restore removes a post-backup mutation, validates
  managed health, reconnects through an explicit external URL without creating a second cluster,
  and stops and removes only its validated temporary root. The release checklist names this exact
  command, the architecture boundary requires its managed-backup/restore, external-authority, and
  external-restore-guard success markers, and a current development-host run passes all four
  markers. This run used the
  installed PostgreSQL 14 development runtime and therefore is runtime behavior evidence, not
  portable-package or clean-machine evidence. The macOS DMG verifier now runs the same isolated
  lifecycle against the helper binaries and PostgreSQL runtime inside the mounted candidate, so a
  package cannot pass by inventory alone. A relocatable PostgreSQL runtime prepared outside the
  Homebrew prefix is now exercised from the mounted local DMG; signed/notarized distribution and a
  clean-machine run remain separate production gates.
  The Windows release workflow now runs an equivalent PowerShell lifecycle against the staged
  install root before creating the MSIX. It validates the same backup/restore and explicit-external
  authority invariants using the packaged `.exe` helpers and runtime and cleans only its GUID-named
  temporary root. The architecture boundary requires that workflow step and all authority markers.
  PowerShell and a Windows staged runtime are unavailable on this host, so syntax/source contracts
  pass locally but Windows execution remains a platform gate.
  The ordinary Linux Rust CI job now installs PostgreSQL, rebuilds every catalog helper, resolves
  the distro runtime from `pg_config --bindir`, and runs the same managed/external authority proof
  on every pull request rather than waiting for a release build. Actionlint remains the workflow
  syntax gate in CI. This checkout validates the architecture/source contract; the new Ubuntu job
  has not yet executed remotely and is not counted as Linux runtime evidence until it does. The
  complete offline `photo-catalog` crate test run passes all 24 runnable library/helper tests,
  including managed import, deterministic discovery, runtime path authority, migration evidence,
  and relink-aware migration inventory; zero-test helper harnesses are not counted in that total.
  The lifecycle proof now also backs up the catalog while operating through the explicit external
  URL, verifies that archive exists, and requires `restore-latest` to fail with the administrator
  boundary before touching the server. Bash and PowerShell variants publish a fourth
  `postgres_catalog_external_restore_guard=passed` marker, the architecture gate requires it, and
  the strengthened macOS development-host lifecycle passes all nine current markers.
  Manual, external-service, and automatic pre-migration backups now share one atomic no-clobber
  archive publisher rather than maintaining a second migration-only `pg_dump` publication path.
  `pg_dump` writes only to a sibling `.partial` path; spawn, dump, permission, and publication
  failures remove it, the completed owner-only archive is published with an atomic hard link, and
  an existing destination is never replaced. The pre-migration caller retains its managed-server
  credentials while using the same publication contract. Direct tests prove an interrupted dump
  cannot publish or strand a partial archive and a completed `0600` archive cannot be overwritten.
  Nine focused local-database tests, the full 24 runnable catalog tests, current-toolchain clippy
  with warnings denied, and the real nine-marker PostgreSQL lifecycle all pass after this change.
  The managed lifecycle now also places a newer malformed archive beside a valid backup, requires
  `restore-latest` to fail at the `pg_restore --list` validation boundary, and proves a live project
  created immediately beforehand remains present. Bash and PowerShell package proofs publish the
  additional `postgres_catalog_corrupt_restore_guard=passed` marker, and the architecture boundary
  requires both platform implementations. This upgrades the development-host lifecycle from four
  to five behavioral markers. The lifecycle also injects an interrupted `pg_dump`: Unix writes a
  partial archive and exits unsuccessfully, while Windows selects an unavailable tool directory
  without modifying the packaged runtime. Both require a surfaced failure, zero stranded partials,
  no additional completed archive, and an unchanged live catalog, then publish
  `postgres_catalog_interrupted_backup_guard=passed`. The architecture boundary requires both
  implementations. This raises the development-host lifecycle to six behavioral markers; packaged
  macOS and Windows execution remains an external release gate.
  Managed startup now proves the schema-change safety contract directly: the first reopen of an
  initialized schema creates exactly one valid `pre-migration-v*.dump`, a post-backup catalog
  mutation is removed by restoring that archive, and later opens do not create duplicate backups
  for the same application version. Bash and PowerShell publish
  `postgres_catalog_pre_migration_rollback=passed`, raising the local lifecycle to seven behavioral
  markers. This proves the current-version backup/rollback mechanism; cross-version upgrade and
  downgrade candidates still require packaged release artifacts.
  The packaged lifecycle now imports referenced media from two independent drive identities,
  repeats one import to prove stable deduplication, marks that asset edited with canonical v13
  authority, removes its original, and verifies the migration inventory reports it unavailable.
  After an explicit relink, the same inventory must resolve the new path and report the source
  available. Bash and PowerShell publish `postgres_catalog_media_relink_dedup=passed`, raising the
  local lifecycle to eight behavioral markers. Slow/offline physical drives and large-library
  timing remain real-platform gates rather than synthetic claims.
  That real workflow exposed and fixed a PostgreSQL type-authority defect: asset snapshots selected
  canonical recipe provenance as `JSONB` and then decoded it as Rust `String`, panicking as soon as
  an edited canonical asset entered snapshot or migration inventory. The snapshot query now casts
  the immutable JSON payload to `TEXT`, matching the already-correct single-recipe query and the
  persisted Rust contract. The architecture boundary locks the cast and the nine-marker lifecycle
  exercises the formerly crashing path with a real canonical v13 row.
  Managed-library relocation is now part of the same package proof. It performs a real managed
  import, requires the pre-move PostgreSQL safety archive, copies and SHA-256-verifies the original,
  preserves the old library, publishes the new library, and proves the catalog adopts the new root.
  Bash and PowerShell publish `postgres_catalog_managed_library_move=passed`, raising the local
  lifecycle to nine behavioral markers. Interrupted/full-disk relocation and physical slow-drive
  timing remain open.
  Library relocation now stages beside the selected destination and publishes the entire verified
  directory with one same-volume rename instead of moving top-level folders individually. Unique
  nanosecond staging identities and millisecond backup names avoid same-process collisions; an RAII
  cleanup removes staging after copy, hash, missing-original, destination-race, or publish failure.
  The package lifecycle removes a managed original, requires relocation to fail explicitly, proves
  the selected destination remains empty, proves no sibling staging directory survives, and proves
  catalog root authority is unchanged. This closes the synthetic interrupted/missing-original move
  behavior; actual full-disk and physical slow-drive execution remain open.
  Failed-restore rollback is now a separate tenth lifecycle marker. The Bash proof creates a valid
  older candidate, adds a newer live-catalog mutation, substitutes a `pg_restore` wrapper that lets
  the candidate modify PostgreSQL and only then returns failure, and requires the manager to
  reapply its newly created `pre-restore` safety archive. The final snapshot must retain both the
  candidate baseline and the newer live mutation, proving that the error branch restores data
  rather than merely emitting a reassuring message. The exact macOS run against an isolated
  PostgreSQL 14 cluster passed all ten markers, including
  `postgres_catalog_failed_restore_rollback=passed`. The PowerShell lifecycle contains the same
  executable-wrapper and snapshot contract, and the architecture gate requires the marker in both
  platform scripts; physical Windows execution remains outstanding and is not inferred from the
  source contract.
  The lifecycle has since advanced to twelve behavioral markers. On 2026-09-02 the current Bash
  proof ran against the already-built catalog helpers and an isolated PostgreSQL 14 cluster in
  17.07 seconds, passed runtime startup, legacy-credential recovery, pre-migration rollback,
  ordinary backup/restore, corrupt-archive refusal, interrupted-backup cleanup, failed-restore
  rollback, media relink/deduplication, managed-library movement, external authority, external
  restore refusal, and corrupt-row decode protection, then removed its validated temporary root.
  No application build and no live-catalog mutation occurred. The final marker,
  `postgres_catalog_corrupt_decode_guard=passed`, is backed by a disposable second database that
  injects invalid capture date/import mode, review, relink, migration-profile, export-job, timeline,
  and carousel values. Typed readers must return a structured record identity without panicking;
  `catalog_integrity` must inventory all corrupt records and `--require-clean` must fail.
  The packaged-Windows PowerShell proof now implements the same corrupt-decode cases, requires the
  additional helpers plus `psql.exe`, and emits the same twelfth marker. Architecture checks bind
  the marker to its actual fixture/assertion body in both scripts. PowerShell is unavailable on
  this host, so the Windows implementation has source/architecture proof only and remains open
  until the release workflow executes it on Windows. The parent PostgreSQL checkbox also remains
  open for actual full-disk, slow-drive, large-library, cross-version packaged upgrade/rollback,
  and clean-machine platform evidence.
  The current local macOS Release closeout configured the non-strict packaging preset with Sparkle
  disabled, completed a fresh 2,605-step native/QML rebuild, and generated
  `build/release-macos/package/Framehouse-0.1.0-Darwin-arm64.dmg`. The verifier now has an explicit
  `--allow-no-sparkle` mode for this local artifact only; its default production behavior still
  requires `Sparkle.framework`, `SUFeedURL`, and `SUPublicEDKey`, and signature checks remain a
  separate opt-in gate. The local verification mounted the DMG read-only, found all required
  helpers and the bundled PostgreSQL server/runtime, passed all ten managed/external catalog
  lifecycle markers, and found no Homebrew or `/usr/local/opt` Mach-O dependency. That run also
  exposed and fixed a macOS system-Bash parser failure in the nested dynamic-library scan without
  relaxing the prohibited-path check. Exact proof commands were:

  - `cmake --build build/release-macos -j 6`
  - `cpack --config build/release-macos/CPackConfig.cmake -B build/release-macos/package`
  - `scripts/verify-macos-release.sh --package-dir build/release-macos/package --allow-no-sparkle`

  The final complete Release CTest invocation ran 165 tests from that rebuilt tree: 162 passed,
  zero failed, and three physical-device Metal parity tests skipped because their required Metal
  hardware path was unavailable. Two stale source contracts were updated to follow the extracted
  shared pipeline compiler and `StudioImageViewport`. One optimized inpaint telemetry check
  incorrectly required elapsed time to be greater than zero rather than non-negative; it now
  matches the surrounding contract, and the complete `editor_preview_controller_test` passes in
  62.67 seconds. Poppler was installed as a host-only test prerequisite so the Carousel PDF proof
  could execute rather than short-circuit; the test then exposed stale exact-sRGB assertions for
  photographic pixels now rendered through the canonical color pipeline. Those assertions still
  prove slot ordering, spatial placement, cutout alpha, and color identity, while authored board
  colors remain exact; the complete Carousel test passes in 87.34 seconds. The agent test failure
  was a same-project continuity race: an asynchronous catalog refresh could restore persisted model
  state over an explicit test/user model selection. Continuity is now restored once per project
  key, so the focused agent controller test and the complete suite are deterministic without an
  Ollama endpoint. Two apparent agent UI failures in the first suite were stale generated QML/test
  artifacts; rebuilding their exact targets made both pass without a behavior change, and the full
  Release rebuild plus rerun confirmed them. The refreshed DMG again passed checksum verification,
  read-only mount inspection, all 11 PostgreSQL managed/external lifecycle markers, and the Mach-O
  prohibited-path scan. `/bin/bash -n` for all changed release scripts,
  `scripts/test-architecture-boundaries.py`, and `git diff --check` pass after this closeout.
  A 2026-09-01 audit then found a compatibility hazard in managed PostgreSQL state recovery: an
  existing cluster whose state file was unreadable could receive a new generated credential, while
  an older owner-only `.credential` file was no longer consulted. The manager now fails closed when
  `data/PG_VERSION` proves that catalog authority already exists, accepts the legacy credential only
  after it authenticates against that exact local cluster, and atomically republishes the proven
  credential without changing catalog contents. Both Bash and Windows lifecycle contracts require
  the new `postgres_catalog_credential_recovery=passed` marker, and the architecture boundary
  rejects removal of the fail-closed behavior. On macOS, all 11 focused manager tests, all 57 Rust
  workspace tests, and a fresh disposable Homebrew PostgreSQL 14 lifecycle passed all 11 markers,
  including byte-identical snapshots across deliberate state-credential corruption and recovery.
  Rebuilding the optimized catalog helpers and DMG, then running
  `scripts/verify-macos-release.sh --package-dir build/release-macos/package --allow-no-sparkle`,
  repeated those 11 markers using only the helpers and PostgreSQL runtime mounted from the shipping
  candidate; checksum, read-only mount, and Mach-O prohibited-path verification also passed. This
  proof did not open, repair, migrate, or otherwise mutate either real user catalog. A separate
  direct read-only inventory authenticated with the existing owner-only credential and found the
  active PostgreSQL 14.17 catalog intact at migrations 1-9: 168 assets, 47 edit recipes, 3 projects,
  6 project memberships, 5 drives, 2 review rows, 1 carousel document, and 4 export jobs. Migrations
  10-13 are not yet installed there, so a verified non-overwriting backup and isolated compatibility
  rehearsal are mandatory before any real catalog upgrade.
  That prerequisite is now materially closed for this catalog without upgrading it in place. An
  owner-only custom-format archive was created at
  `backups/pre-v13-compatibility-20260901T195217Z.dump` (94,356 bytes, SHA-256
  `befa40dfbdf200ac96ae1143be363e687d2c4144f48e4728d9fe6c7ee3cef83d`) and validated with
  `pg_restore --list`. The new repeatable
  `scripts/rehearse-postgres-catalog-upgrade.sh` restored that archive into an isolated temporary
  PostgreSQL 14 cluster, applied migrations 10-13, and proved exact pre/post equality for every
  catalog table count named above. It then destroyed only the disposable rehearsal database,
  restored the original archive, and proved the same counts plus schema maximum 9. The rehearsal
  inventory reports 47 edited assets: 45 source files available, 2 missing, 15 explicitly using
  `legacy-oklch-v1`, 32 with no renderer identity, and zero canonical-v13 candidates yet. Both
  `postgres_catalog_real_backup_upgrade_rehearsal=passed` and
  `postgres_catalog_real_backup_rollback_rehearsal=passed` are now architecture-enforced markers.
  A 2026-09-02 audit found that the rehearsal itself still expected maximum migration 12 after
  migration 0013 was added. The stale assertion is corrected and architecture-guarded at maximum
  13. The preserved archive remained byte-identical to its recorded SHA-256, and a new isolated
  run completed in 1.25 seconds with migration count/minimum/maximum `13/1/13`, all migrations
  successful, and exact pre/post/rollback counts: 168 assets, 3 projects, 6 project memberships,
  47 edit recipes, 2 review rows, 0 relinks, 5 drives, 0 timeline documents, 1 carousel document,
  and 4 export jobs. The restored rollback database returned to schema maximum 9. No temporary
  cluster remained and the live catalog was not contacted or mutated.
  The live catalog remains at schema 9; its two missing sources and all 47 recipe candidates must be
  resolved and reviewed before an approved in-place compatibility migration.
  A subsequent read-only production batch assessment used that backup through an isolated
  PostgreSQL 14 database and processed all 47 edited assets at their complete decoded dimensions.
  It produced 418 immutable artifacts (approximately 14 GiB) and a report with canonical payload
  SHA-256 `e0f4728c2f3fe5facc4c92e767c270770d215e99614952232d1d1a465f4da837` and
  persisted-file SHA-256 `db2123e9d4d1131552926142cc0150eb4d45a2cafb797b77a922a1adaffc75eb`.
  Thirty-eight candidates compiled with valid evidence; two failed closed because their originals
  are absent from `/Volumes/Untitled`, and seven exposed legacy ellipse-mask or dangling
  layer-to-mask references. The compatibility renderer treats a missing referenced set as no mask,
  so candidate preparation now records and applies an explicit
  `dangling-reference-normalization`; it also records the lossless legacy geometry synonym
  `ellipse` to v13 `radial`. The adapter accepts both names on read. Focused migration and batch
  report tests pass, and the new
  `scripts/rehearse-postgres-rendering-migration-batch.sh` restored the backup, migrated only a
  disposable database, selected the seven affected recipes, and produced 90 immutable artifacts
  (approximately 2.6 GiB). All seven candidates compiled and all seven had valid evidence, with no
  candidate failure or unavailable structure attribution. Its report payload SHA-256 is
  `0f4afd63fa9df047d796003d389b049c7a924db6ef2c50a7d211d08bcf450477` and its file SHA-256 is
  `3f7966430fe6eca15388f3d65b09db9bc18e0b270f0d6c9b39adb209543f8c3f`.
  The same real 6000 by 4000 asset then exposed that libheif quality 100 remains chroma-subsampled:
  lossless mode alone did not improve the recorded pixel errors. The production 10-bit HDR AVIF
  writer now requires the AOM `chroma=444` parameter and true lossless mode for the quality-100 PQ
  and HLG presets. A saturated-color codec regression rejects subsampled output. The exact real
  asset rerun reduced candidate/reference PQ/HLG maximum encoded-signal error from as high as
  approximately `0.279` to `0.001467`; all four p99 errors are below `0.001434`, both formats are
  available, and their Rec.2020 transfer, range, mastering-display, MaxCLL, and MaxFALL contracts
  remain valid. The report payload SHA-256 is
  `fce4976e384fd094a3fab6826c32fed1bac45327cc657885f856d46f5a32a13e` and its file SHA-256 is
  `107c161b57bcfed13d1d282c31ec3b6e60c0430dd64e0367b10a7b696e58b444`.
  Batch correction no longer requires a two-hour all-or-nothing rerender. The report layer now
  merges only identity-valid component reports with identical thresholds and unchanged asset/source
  identities, refuses overlays for assets that were not blocked in the base, requires the final
  overlay to reduce rather than introduce blockers, rebuilds the complete summary, and binds every
  component report hash into the merged report. The packaged merge command stream-verifies every
  content-addressed artifact and creates a verified hard link when possible, with a verified copy
  fallback. Unit coverage rejects tampered component identity and changed source identity.
  Corrective PostgreSQL rehearsals then reran the five original shipping-format failures, the one
  7328 by 5496 TIFF that reproduced Qt's 256 MiB allocation rejection, and the three remaining
  structure-attribution failures. The standard image reader now receives only the calculated
  expected allocation for a trusted output size, capped at 1 GiB and restored immediately; the same
  bounded helper protects migration evidence and production export artifact verification. All five
  HDR cases pass lossless 4:4:4 PQ/HLG, the large TIFF candidate/reference pair decodes and compares,
  and all three structure-attribution cases are available.
  The final six-component 47-asset report contains 45 compiled, evidence-ready candidates, the same
  two explicit missing originals, zero unavailable shipping formats, zero unavailable structure
  attributions, 523 unique verified artifacts, and 625 per-asset artifact references. Its canonical
  payload SHA-256 is
  `995c7c02fcc2fe78aaf1885beab3c7df4cc2964e0733df5513ca174f1bde2dda`; its persisted-file SHA-256 is
  `e1173accb3c1f4beca047d2187f021c3b4c42b67e2e09aa3e522177660bbb573`.
  A later producer-contract audit found that this v1 merged report must remain historical rather
  than promotional evidence: 36 of its 45 available assets still embed the pre-fix HDR AVIF
  quality-90/subsampled artifacts. The corrective components proved the codec repair on nine
  assets, but the old merge contract did not bind all components to one exact executable. The
  evidence schema is now `photo-editor-rendering-migration-evidence:v2`, the batch schema is
  `photo-editor-rendering-migration-batch-report:v2`, and every v2 report records the exact Release
  executable SHA-256, executable name, and build mode. A merge refuses components from different
  executable identities, so no v1 artifact or differently built binary can be promoted through a
  current report.
  To avoid another two-hour all-or-nothing run, the packaged command and isolated PostgreSQL
  rehearsal now support `--rendering-migration-plan-only`. It creates a complete current-binary
  blocker baseline without rendering, and small same-binary asset reports replace only blockers
  through the existing identity-checked merge. The current 47-asset v2 plan was produced in seconds
  from the preserved PostgreSQL backup and is bound to Release executable SHA-256
  `0ee51bd1706caa4b14789d11c639ceb1a598efbc19635fb195980aa0763a8a93`. Its canonical report
  payload SHA-256 is `d25c8cd9bceb8557d66ec611a42c5b140b069335b4140b447e805ec041214812`
  and its persisted-file SHA-256 is
  `7225d3c3d293ae986cd8a5bf342969d989f85657764e48f3e38024586fc95469`.
  One bounded component then refreshed all five 1350 by 1080 assets in under a minute; its payload
  SHA-256 is `2895c08352f709cdd818722ed21e4f1ad5012a030bbbe54d39569a59962c513d`
  and its persisted-file SHA-256 is
  `aca9fc4d7ff3aa4778f780964692f5fe2950669fcb3745d747619d0a041e9389`.
  The resulting merged 47-asset progress report has payload SHA-256
  `04e2c825e6567cd5d9893686d2d1a770e922a1fa89ded02620f0248b7f6a1f67` and persisted-file
  SHA-256 `b66a96bb8453e9d5d81aeb55565827b8aebeff8e3022f04529ef655074c5edfc`.
  It contains exactly five current-binary candidates; all ten PQ/HLG artifacts record quality 100,
  with worst candidate round-trip p99 absolute errors of `0.00143926` for PQ and `0.00143608` for
  HLG. Forty available assets remain explicitly awaiting refresh, and the two missing originals
  remain blocked. This proves bounded resumability without representing five refreshed assets as
  catalog completion.
  The five current-binary candidates now have a tamper-evident local review packet at
  `build/release-macos/proofs/real-catalog-stable-binary-v2-review-05-20260902T1845Z`. It exposes
  synchronized current/candidate images, Delta E 00 heatmaps, dedicated Display P3 comparisons,
  measured difference/clipping context, decisions, notes, and five required 1-5 ratings for color
  fidelity, highlight detail, shadow detail, stack integrity, and preview/export match. The packet
  binds the source report file, exact producer executable, every displayed artifact, deterministic
  review UI, and ratings template. Its packet digest is
  `ef3d25ab105235d298d037c611ec1ac9dcb576bced1bfd2f4d52fb2f166a86d8`; manifest, pending-ratings,
  and HTML file SHA-256 identities are respectively
  `69494930a26e22d247853d06b5e98c4346e7f29ca4784a1a93240836cbe6ae2d`,
  `b67481b2c86f4d3f95799317e66cdc37f793444e3c9ffaee24a8a092ec246846`, and
  `94a72c4108ba6e3ee6f69ad6ad1d5d980d6bc8658a1d74af0542756cafb91fbf`.
  The regression test proves valid pending review, fail-closed completion, exact case coverage,
  required independent reviewer/timestamp/notes/scores, executable mismatch rejection, and UI or
  artifact tamper rejection. Artifact-level inspection confirmed that the packet displays a
  materially deeper-shadow candidate and a broad heatmap difference for
  `asset-07d5cdedbe8f3d50`; no machine approval was inferred. All five decisions remain `pending`, so
  `--require-complete` correctly fails with `human review is incomplete: 5 pending`. Browser layout
  proof remains unrecorded because the in-app browser runtime could not initialize in this session.
  Long-run prevention is now machine-readable rather than a conversational promise.
  `plan-rendering-migration-refresh.py` validates the exact v2 producer executable, excludes already
  refreshed and missing-original assets, imports trusted full dimensions from the identity-bound
  historical evidence, and creates deterministic chunks under both asset-count and pixel budgets.
  An asset larger than the pixel ceiling is isolated and labeled
  `single-asset-explicit-overage`; it can never be hidden in a multi-asset chunk. Regression coverage
  proves count/pixel packing, explicit oversized handling, output immutability, executable mismatch
  rejection, and missing-dimension rejection. The initial 15-megapixel/two-asset plan contained 40
  pending available assets in 40 chunks because 39 sources individually exceed the conservative
  ceiling; its plan SHA-256 was
  `80d278b87228176ad1926433c637699b8315b35a7ee3a7757e47c8e194ac6699` and its file SHA-256 was
  `2c36dcd3429eededb0438816f9686ea3c98d9cc02ef43a3e1579edfc83e4cffa`.
  Only its first 6.9312-megapixel chunk ran. `asset-f8c7313b45a1926f` completed in roughly ten
  seconds with quality-100 PQ/HLG p99 round-trip errors of `0.00143289` and `0.00143388`; the
  component payload/file SHA-256 identities are
  `f45594eb37e73e8328029310c14f3b41a19ebdf4c04609f4f48c5e37e8f1c77f` and
  `5a61cddfd45ff21da4535836b882ab5d382b8cfff75bf38d4ba525569da3dfec`.
  The merged six-candidate progress report has payload SHA-256
  `3fbd61476069be076251744847158d3b8be82ff364604dff2b6ec2c31ae699dc` and file SHA-256
  `c279a2fff8b638c66ad725414977706ba8a303fc5f2df08840ae19264210e9c1`.
  Its successor plan now reports six refreshed, 39 pending available, two missing, and 39 bounded
  chunks; plan/file SHA-256 identities are
  `feb4e04dbb0b5a79cea459d5ebd4c93f8eb99649b34813068232a621bfe102fb` and
  `9bcd1b58333d9bd7a846729830fbe8194d39860fb673c5f444d804723d254a69`.
  The current six-asset review packet supersedes the five-asset handoff at
  `build/release-macos/proofs/real-catalog-stable-binary-v2-review-06-20260902T1910Z`; it is valid
  with all six decisions pending. Its packet digest is
  `3e1164e455f8ae9419c59d1501cdcf99b2820b9c8c472d148180d7b15468ea33`, and its manifest,
  pending-ratings, and HTML file SHA-256 identities are
  `9b52de2f85fa67c8c848d0e7f81425dc200645e07a0f1f447c10beb39f30c360`,
  `c4f6ef307db9cc05b0f5695932eac85ab0bd5002bf8012a570c5676d14093de0`, and
  `608164e193037453cd0192f36b0557de675b6fc3316887a1fa552b2a9a649d85`.
  A v2 linear-light diagnosis of those six synchronized pairs now blocks further catalog refresh
  until the renderer transition is corrected. The diagnostic decodes the displayed sRGB artifacts
  with the IEC 61966-2-1 transfer function, computes Rec.709 linear luminance, and only then derives
  exposure ratios; its first version, which divided gamma-encoded code values, is retained as
  superseded exploratory evidence and is not eligible for a release claim. The current
  identity-bound report is
  `build/release-macos/proofs/real-catalog-stable-binary-v2-diagnosis-linear-bound-06-20260902T181130Z.json`.
  Its canonical diagnosis SHA-256 is
  `1f7dd5201a2e2fa495461781b2e8276206002b1d1065e24277b0ab74abf9b0c4` and its file SHA-256 is
  `1e74346cfb2b511657ba580b15265990f06a55982d28b2cef75549463ee35123`. It also binds the exact
  progress-report payload/file identities and Release producer executable rather than relying only
  on the review-packet digest. All six candidates are
  darker on average, the mean display-luma change is `-36.655503` code values, zero of six meet the
  `0.25`-stop cross-region consistency diagnostic, and the worst within-image linear-exposure
  spread is `3.786525` stops. The six individual spreads are `3.078254`, `2.908219`, `2.495577`,
  `3.003094`, `3.786525`, and `2.312546` stops.
  The ownership trace does not support changing the global Exposure operator: the SceneAdjustments
  stage multiplies every finite RGB component by exactly `2^stops` before tone, Look, layers, and
  PhotographicOutput, and the property/invariant tests cover composition, signed values, and
  extended range. The measured non-uniformity begins at the renderer transition. The official ACES
  tone scale intentionally anchors scene-linear `0.18` at about 10 nits on a 100-nit target—about
  `-0.85` stop relative to the legacy direct-linear mapping—and applies a progressively stronger
  toe below that anchor. The current migration candidate changes the renderer without adding an
  explicit, recipe-owned transition look, so it cannot be treated as an appearance-preserving
  upgrade. This is a migration-design defect even though the underlying ACES transform is behaving
  as specified.
  Before another asset chunk may run, a migration-only transition-look design must be explicit in
  canonical v13 (never hidden in Exposure), deterministic across preview/export, and separately
  identifiable in evidence. Neutral ramps must still prove exact scene-linear Exposure before the
  look; legacy, uncompensated ACES, and compensated ACES must be compared on the current six assets;
  the compensated candidate must reduce deep-shadow loss without increasing highlight clipping,
  color error, or stack divergence; and the independent review packet must expose both the
  uncompensated and compensated choices. If no bounded compensation meets those gates, the product
  must keep the legacy render until the user explicitly accepts the uncompensated change or chooses
  an archival/export disposition. It may not silently promote the renderer or delete the legacy
  path.
  The bounded refresh planner now requires an identity-bound quality diagnosis after a calibration
  chunk and before it will plan more available assets. It requires exact refreshed
  asset coverage, matching progress-report and producer identities, and no per-asset cross-region
  linear-exposure spread above `0.25` stop. The historical v2 six-asset invocation fails closed at
  `asset-07d5cdedbe8f3d50: renderer-transition exposure consistency gate failed` and creates no
  successor plan. Direct diagnostic, planner, architecture-boundary, and diff checks pass; this is
  a deliberate quality blocker, not an incomplete long-running job.
  The source correction is now implemented behind that blocker. A dedicated
  `preserve-legacy-direct-luminance-v1` transition operator remaps AP0 scene luminance before the
  official/native ACES output transform; it is not folded into Exposure, the ordinary tone
  controls, or a clipping master curve. Processor and LUT cache identity includes the transition,
  the canonical recipe is now `photo-editor-edit-recipe-v13.2` with v13/v13.1 read compatibility,
  and the corrected PhotographicOutput stage identity is `photographic-output-aces2-v4`. The
  production LUT identity is `aces2-output-lut:v2`; unlike the superseded first implementation,
  its table generation applies the transition before the official ACES transform instead of only
  changing the cache key. Neutral-ramp proof
  preserves legacy direct-linear values from `0.001` through `1.0` within `2e-5` and remains
  monotonic. Desktop JSON, canonical adaptation/reopen, interactive comparison, apply, undo/redo,
  clean reload, and the batch assessor all carry the same explicit transition identity. The render
  executor fails before decode if the canonical plan and runtime output configuration disagree.
  Focused verification passes: `color_pipeline_test`, `edit_recipe_v13_test`,
  `edit_recipe_v13_adapter_test`, `render_executor_test`, and
  `rendering_migration_proof_test`.
  Exact-binary calibration then exposed a production-only defect that the initial direct-processor
  proof did not cover: the first transition implementation changed the LUT cache key but generated
  the same 65/97-cube contents as uncompensated ACES. Production batch/export executes that LUT, so
  its partial calibration artifacts were byte-identical to the old producer. That interrupted,
  invalid evidence remains explicitly superseded at
  `build/release-macos/proofs/real-catalog-transition-v13-2-f1e39a-calibration-f8-20260902T1247Z.incomplete-lut-content-bug`.
  LUT construction and repair now call the same AP0 transition helper as the direct processor,
  `color_pipeline_test` requires different ordinary/transition table contents and LUT/direct
  agreement, the LUT/stage identities were advanced to `aces2-output-lut:v2` and
  `photographic-output-aces2-v4`, and the corrected exact Release producer SHA-256 is
  `a793e95ca1b8f45255794ba4b8459a159c31ea42cc6dd6e1a25f8d6636456b42`.
  This correction required only a targeted approximately one-minute build and a 1.1-second
  incremental relink, not a full two-hour rebuild.

  That exact producer rerendered only the six calibration assets through isolated temporary
  PostgreSQL restores; live PostgreSQL remained the sole catalog authority and was not mutated.
  The 6.9312-megapixel component at
  `build/release-macos/proofs/real-catalog-transition-v13-2-a793e9-calibration-f8-20260902T1252Z/report.json`
  has payload/file SHA-256 identities
  `0d3205f3c4e375fc3d37fbc7de60c7ffe9f226314b093a162c4a38d0c7bca378` and
  `1054bf7b489c8d3697b8a00f7203c8c8b4fe2a4ab228d87fe7b96c40aed1a49e`.
  Relative to its obsolete uncompensated candidate, added shadow clips fell from `466` to zero and
  p95 Delta E 00 fell from `16.846697` to `3.937580`. The five 1350 by 1080 components at
  `build/release-macos/proofs/real-catalog-transition-v13-2-a793e9-calibration-05-20260902T1256Z/report.json`
  have payload/file identities
  `4d970c96acc72a300a4f51e3ac28529ff19eecfd999b204a031779280ace96af` and
  `68f87886a9fc2856d34b41736947a937ca843070f77fa70402cb9f5f823f2dec`.
  All six compile with complete evidence and no clipping blocker. Their merged 47-asset progress
  report is
  `build/release-macos/proofs/real-catalog-transition-v13-2-a793e9-progress-06-20260902T1300Z/report.json`,
  with payload/file SHA-256 identities
  `07bcc402f68e07991d0bb9ecd8031a7328315a2cf7eb78479383c2642f2b856b` and
  `12f43498764ce29b7e7e04d116e012817d4feffa5cd61f0744742da82879a786`.
  It records six evidence-ready assets, 39 available assets still pending, the same two missing
  originals, and zero clipping-risk assets. High-difference, HDR, shipping-format, and target-review
  flags remain human-review obligations rather than being converted into machine approval.

  The corrected diagnostic contract is
  `photo-editor-rendering-migration-pair-diagnosis:v3`. Every sampled luminance band remains in the
  evidence, but the universal-exposure gate uses only bands containing at least `0.1%` of sampled
  pixels; sparse excluded tail bands remain reported in
  `allObservedLinearExposureRangeStops` and clipping/near-white detail is gated separately. This
  prevents 43-pixel highlight tails from dominating a whole-image exposure claim without hiding
  them. The six-asset diagnosis at
  `build/release-macos/proofs/real-catalog-transition-v13-2-a793e9-diagnosis-v3-06-20260902T1306Z.json`
  has canonical/file SHA-256 identities
  `e88e96c0ebb435c0ef9c9c1a25c7dc9262f0d4d77ce095030ae6ff7e3cd38b0c` and
  `c6b063ea8c71088980c154f43e722415dc87a53fd25861d60d38fafb6b5c32e9`.
  All six pass the `0.25`-stop universal-exposure gate; the worst statistically supported spread is
  `0.188887` stop. The widest all-observed spread remains visible as `0.444832` stop for
  `asset-07d5cdedbe8f3d50`, where two excluded bands contain only 702 and 43 of 839,680 sampled
  pixels and the separate strict clipping evidence reports no added clip blocker.

  The machine gate passes, but independent human approval remains a separate prerequisite. The
  pre-approval successor plan has therefore been renamed to
  `build/release-macos/proofs/real-catalog-transition-v13-2-a793e9-refresh-plan-after-06-20260902T1308Z.superseded-missing-human-approval.json`.
  Its historical canonical/file SHA-256 identities are
  `a35e3bf8fd7c8868508c3f562bb876562daa4a6ddb711a4aa3b7fed5c46d02d4` and
  `6242b36a775e57cf828cb91db2a27a2fbf84ec96568feb90f0677a2636a7bca7`;
  it uses the historical `photo-editor-rendering-migration-refresh-plan:v1` contract, binds the v3
  diagnosis and exact producer, and contains 39 bounded single-asset chunks, but it lacks
  `humanApprovalGate` and is not executable promotion evidence. The
  first 18.997942-megapixel asset is explicitly labeled `overBudgetSingleAsset:true` rather than
  hidden in a multi-asset chunk. The current planner refuses these real-catalog inputs without an
  external unanimously approved ratings file and creates no output. After approval, it must create
  a new plan that records the packet digest, manifest and ratings identities, validator identity,
  reviewer, review timestamp, exact reviewed-asset set, and `unanimously-approved` status. The
  approval-bound plan is published with an atomic create-only write and owner-only permissions;
  an existing output path is never replaced. New plans use the distinct
  `photo-editor-rendering-migration-refresh-plan:v2` contract so consumers can reject the
  pre-approval v1 shape without trusting a filename. Focused planner, review-packet, Python syntax,
  architecture-boundary, and diff checks pass. A direct invocation using the exact real progress
  report, historical dimension report, v3 diagnosis, and corrected Release producer fails with
  `unanimously approved independent review is required before planning more refresh chunks` and
  creates no output. The planner and review-packet builder now recompute the Qt report's canonical
  `reportSha256` before trusting any metadata; an edited report retaining its old digest fails.
  Current progress/review inputs must be v2 and bind the exact executable. The dimension-only input
  may use the existing historical v1 report, but only after its canonical report identity passes;
  v1 is never accepted as progress, approval, or executable evidence. Both current real reports
  pass those exact identity checks, and synthetic tampering is rejected in regression coverage.
  Post-approval component evidence is also fail-closed before merge:
  `validate-rendering-migration-refresh-chunk.py` accepts only a hash-valid approved v2 plan, exact
  producer executable, exact asset set for the selected one-based chunk, evidence-complete
  v2 component report, and an artifact directory containing exactly the report-bound files with
  matching content hashes. It publishes an atomic owner-only, create-only
  `photo-editor-rendering-migration-refresh-chunk-receipt:v1`; v1/pre-approval plans, wrong assets,
  incomplete evidence, tampered artifacts, and missing/extra artifacts produce no receipt. A
  component without that receipt is ineligible for the merged catalog report. The native merge
  entrypoint now requires exactly one receipt for every overlay component (the first component is
  the complete base/progress report), recomputes the receipt identity, and verifies component file
  and canonical report identities, producer, exact asset set, common plan identity, unique chunk
  indices, and no cross-component duplicate assets before it creates a merged report. Python/Qt
  canonical receipt hashing is locked by a cross-language fixture. The focused native target builds
  incrementally and `rendering_migration_batch_report_test` passes; missing and mismatched receipt
  cases fail in that test.
  Rendering is now bounded before pixel work as well as after it. The approval-derived
  `photo-editor-rendering-migration-refresh-chunk-authorization:v1` is an atomic owner-only,
  create-only artifact containing the v2 plan/file identities, exact producer, selected chunk,
  exact asset set, and pixel budget. The native batch command refuses every non-plan-only render
  without that authorization, validates its canonical identity and exact executable, and filters
  the complete PostgreSQL inventory to only the authorized assets. Unknown authorized assets fail
  closed; unselected assets are never passed to the assessor. Plan-only inventory remains
  non-rendering and rejects an authorization. Focused Python and native regressions cover obsolete
  plans, overwrite refusal, executable mismatch, exact one-asset selection, and an asset absent
  from the catalog. This removes the path that could accidentally launch an unapproved 39-asset or
  full-catalog render before the merge gate noticed.
  These native source changes intentionally invalidate any claim that the existing `a793e9...`
  executable is the final migration producer. They are command/control changes, not color-kernel
  changes, but executable identity is part of every report, diagnosis, packet, authorization, and
  receipt. The next exact Release build must therefore rerun the bounded six-image calibration and
  packet generation; visual or file identity may be compared with the current artifacts, but old
  ratings cannot be silently rebound to a different packet digest. This rebuild/rerender is a
  remaining final-candidate gate, not work performed in this bounded source pass.
  No desktop rebuild or asset render was needed for this contract proof. The
  current six-asset independent-review packet is
  `build/release-macos/proofs/real-catalog-transition-v13-2-a793e9-review-06-20260902T1301Z`.
  Its packet digest is
  `fa2e59461871f1e42d25cd2e9e902394a8849266c684789464bd6149846b3083`; manifest,
  pending-ratings, and HTML file SHA-256 identities are respectively
  `447e78773cd3b4a99d865ba0efab999b7498073f91f70786c13c5df10d3e9b57`,
  `cdb9520e55d5937c4bcf312e5cc1fa5d70c0a8874132fa64e73ba53cdee9ba14`, and
  `126333fd1c18428335b3118fb8c2637704b35ba005cccbbb0bb5171f20fb5082`.
  Structural validation passes, while `--require-complete` correctly fails with
  `human review is incomplete: 6 pending`. No remaining chunk may run until an independent
  photographer records all six decisions, notes, and five required scores against a packet bound
  to the new enforced Release producer. The current packet can be reviewed as calibration feedback,
  but its ratings cannot authorize that future binary. The next final-candidate build and bounded
  six-image rebind precede final review; the 39-asset sweep is intentionally not running.
  The packet no longer depends on repository access for handoff. The review tool now creates a
  deterministic, create-only ZIP containing exactly `index.html`, `manifest.json`, `ratings.json`,
  and the 30 identity-bound review images. It rejects traversal paths, duplicate names, directory
  entries, unexpected files, oversized archives, tampered UI/artifacts, and a mismatched executable;
  archive validation can also require a completed independent review. Two synthetic archives are
  byte-identical in regression coverage, pending completion fails closed, and a completed archive
  passes. Architecture guards require both archive commands and their safety checks. The current
  33-file, approximately 93 MiB handoff archive is
  `build/release-macos/proofs/real-catalog-transition-v13-2-a793e9-review-06-20260902T1301Z.zip`,
  with SHA-256
  `bbd92794ec118217e4df3eeb41ef084be94a86708aa2256ebde94ba98057e23c`.
  `validate-archive` passes against the exact corrected Release producer. The archive deliberately
  omits the source report, database, original-photo paths, and executable; the independent reviewer
  returns only the generated `ratings.json`.
  Returned ratings no longer overwrite the reproducible pending template. `validate --ratings`
  checks an external file against the immutable packet, while `record-ratings` requires every case,
  non-empty notes, five integer scores, independent-review attestation, reviewer identity, and a
  timezone-aware timestamp before publishing those exact returned bytes to a new owner-only file.
  Publication uses an atomic hard link and refuses an existing destination. Completeness and
  approval are separate gates: a finished `revise` or `reject` decision may be recorded as honest
  evidence, but `--require-approved` fails unless all six decisions are `approve`; it also implies
  all `--require-complete` checks. Missing/extra top-level, case, or score fields fail closed.
  The regression covers pending-record refusal, external complete validation, exact-byte immutable
  recording, no-overwrite behavior, archive plus external-ratings validation, negative-review
  completion without approval, unanimous approval, and unexpected-field rejection. Architecture
  checks require the external-ratings, recording, and approval boundaries. The operational sequence
  for the future rebound packet is: retain the packet's original `ratings.json`; validate and record the returned file with
  `record-ratings`; then run `validate --ratings <recorded-file> --require-approved`. Only that final
  success, against the same new producer used for rendering, permits creation of an approval-bound
  39-asset plan; it does not authorize use of the current reference packet, the superseded
  pre-approval plan, or rendering by itself.
  Before the v2 producer-contract correction, the optimized application and DMG were rebuilt and
  the mounted-package
  verifier passed the DMG checksum and read-only mount, found the managed PostgreSQL runtime, ran
  all 11 managed/external lifecycle markers using only packaged helpers/runtime, rejected prohibited
  Mach-O paths, and passed with only the explicitly permitted local no-Sparkle exception. The
  complete optimized desktop suite then ran 166 tests: 162 passed, zero failed, three hardware Metal
  parity tests skipped, and the separately labeled manual-soak desktop smoke test skipped. Those
  four explicit skips remain Phase 10 hardware/runtime work and are not counted as completion proof.
  Because the v2 report/producer changes postdate that DMG, packaging and mounted verification must
  be rerun once the bounded current-binary asset refresh is complete; the older package result is
  retained as regression evidence, not claimed as the final shipping artifact.
  Catalog scale now has a repeatable machine gate rather than an unmeasured assertion.
  `scripts/test-postgres-large-library.py` creates a disposable schema-13 PostgreSQL cluster,
  inserts exactly 100,000 assets, 20 projects with 100,000 memberships, 20,000 review states, 1,000
  relinks, and 10,000 edited assets split evenly between legacy and canonical authority, then runs
  the actual snapshot, migration-inventory, and integrity helpers. Every process has a hard timeout;
  the report has create-only publication, canonical identity, exact script and fixture-SQL hashes,
  exact helper-binary hashes, PostgreSQL/migration identity, accounting, latency, output-size, and
  macOS peak-RSS evidence. The current identity-bound report is
  `build/release-macos/proofs/postgres-large-library-100k-bound-20260902T192010Z.json`, with canonical
  report SHA-256 `f46de7693967634263ed1ffb5121d643db02a754ee90764c673010f10d8d8b58`
  and file SHA-256 `fadd4121fff63142818e3581266ce07a453d7ec0c4ce6ccc9251d4ba4a77c119`.
  On macOS 26.3.1 arm64 with PostgreSQL 14.17, fixture insertion took `1.616293` seconds; the complete
  100,000-asset/20-project snapshot produced 73,597,843 bytes in `2.916225` seconds at 245,989,376
  bytes peak RSS; the 10,000-edit inventory produced 6,066,113 bytes in `1.198194` seconds at
  163,463,168 bytes peak RSS; and the 121,021-record clean-integrity scan took `0.047112` seconds at
  10,829,824 bytes peak RSS. All exact counts matched and zero integrity findings were present.
  The first exploratory report is retained with the explicit
  `.superseded-unbound-generator-identity.json` suffix and is not promotion evidence. CI now runs
  and uploads this 100k proof from the Rust/PostgreSQL job, the release checklist requires a report
  from exact candidate helpers, and architecture checks lock asset/edit scale, timeouts, RSS,
  generator identity, invocation, and artifact upload. This closes the synthetic local catalog
  scale gap only; it does not simulate 100,000 source files, physical slow/offline storage,
  thumbnail/render load, a long interactive session, Windows, or a clean packaged machine.
- [~] Complete PostgreSQL schema, backup/restore, corruption, managed-runtime, external-service,
  large-library, multi-drive, missing-media, relink, duplicate, interrupted-operation, full-disk,
  upgrade/rollback, and clean-machine contract gates on every supported platform.
- [~] Keep import, cull, batch edit, local refine, export, relaunch, backup, and recovery as the
  product acceptance path.
  `ux_representative_workflow_test` executes 500-asset import/catalog state, culls 50 keepers,
  batch-applies modern edits to 20, performs masked local refinement with exact undo/redo, and
  completes and decodes 20 JPEG deliveries. Its fixtures now model new catalog assets with no
  recipe instead of the legacy-compatible `{}` document; this exposed and removed a false v13
  export failure from the acceptance proof. `crash_recovery_relaunch_test` separately proves edit,
  import, and export relaunch behavior, while the real helper tests cover catalog backup/restore.
  A representative real shoot on clean supported macOS and Windows installations remains required.
- [ ] Freeze new primary timeline, carousel, and AI scope until that acceptance path passes on a
  representative shoot.
- [x] Keep AI optional and truthful; core editing, persistence, and export may not depend on a model
  or MCP service.
  The complete representative workflow passes with an intentionally nonexistent AI model
  directory and Ollama executable, cloud models disabled, and every fake-model response removed.
  It edits, persists, masks, undoes/redoes, exports, and verifies all 20 outputs without invoking a
  model or MCP service. Model-specific UI continues to report unavailable/fallback states rather
  than changing core render or catalog behavior.

### Exit gate

- A clean-machine install creates and recovers a catalog without developer tools or a separately
  managed database service.
- Existing user catalogs migrate with reconciled counts and an automatic recoverable backup.
- The full professional-shoot acceptance path passes with missing-drive and interrupted-operation
  recovery.
- PostgreSQL remains the sole catalog authority and every rejected SQLite production path is
  deleted.

## Phase 10 — Complete real-world validation

### Required evidence

- [ ] Rights-cleared RAWs from at least three camera families and four cameras.
- [ ] Daylight, tungsten, fluorescent, LED, and mixed illumination.
- [ ] Measured chart values or independently rendered camera/profile references.
- [ ] Skin, foliage, sky, saturated objects, neon, night, deep shadows, speculars, and clipped
  highlights.
- [ ] Source DCP/ICC bytes with licenses and SHA-256 identities.
- [ ] Independent LCMS/CMM/OCIO comparison artifacts.
- [ ] Actual Metal and CUDA device runs on supported hardware.
- [ ] SDR sRGB, wide-gamut SDR, HDR PQ/HLG, monitor-switching, and soft-proof paths.
- [ ] Physical monitor measurement and at least one physical printer/paper proof.
- [ ] Blinded editorial approvals with reviewer independence and recorded outliers.
- [ ] Preview/export perceptual heatmaps and worst-patch provenance.
- [ ] Large-library, slow-drive, multi-drive, long-session, and batch-export performance.
- [ ] Accessibility, keyboard, clean-machine, crash recovery, update, and uninstall/reinstall proof.

### Exit gate

- All repository validators pass against immutable evidence.
- Hardware and human gates are recorded rather than replaced with synthetic substitutes.
- Every supported camera/output path has an explicit status: supported, bounded fallback, or
  unsupported.
- The evidence set is bound to the exact source revision and shipping binaries.

## Phase 11 — Migrate users and delete legacy code

Legacy deletion is a planned product transition, not a same-change cleanup.

### Compatibility release

- [ ] Ship one compatibility release containing both the old reader/renderer and the completed v13
  pipeline.
- [x] Back up the catalog and every recipe before migration.
  PostgreSQL backup is non-overwriting and uses owner-only custom-format `pg_dump` files. The
  accepted-migration helper creates a fresh `pre-recipe-migration-*.dump`, rejects an empty backup,
  records that exact path as the rollback pointer, verifies immutable evidence identity, and
  performs no authority write until those prerequisites succeed. Manual restore validates the
  selected dump with `pg_restore --list`, creates a separate pre-restore safety dump, restores with
  fail-fast behavior, and reapplies the safety dump if restoration fails. A disposable live
  PostgreSQL proof verified that pre-backup state survived while a post-backup mutation was removed.
- [ ] Compile a v13 candidate for every edited asset.
  Candidate compilation is now proven for all 45 edited assets whose originals are available:
  38 in the complete report and the seven corrected structural cases in the isolated corrective
  report. The checkbox remains open because `asset-73aed4da82ee3f96` and
  `asset-deddc4a2c280509e` still point to unavailable originals on `/Volumes/Untitled`; they must be
  mounted, relinked, or receive an explicit export/archive/unsupported disposition before the claim
  can cover every edited asset.
- [~] Generate side-by-side preview, perceptual difference, clipping, target, preset, mask/layer,
  and export-risk evidence.
  The existing rendering-upgrade comparison now produces synchronized candidate/current images,
  sampled CIEDE2000 mean/p95/maximum values, shadow/highlight clipping counts, image/recipe
  identities, and a deterministic evidence hash. `Use ACES 2` remains disabled until that measured
  pair exists, cancellation leaves the recipe untouched, and the focused migration proof covers
  compare, evidence, accept, undo, redo, and reload. The production batch assessor now projects
  the catalog's authoritative legacy or canonical state, decodes the original once with exact
  metadata, admits the official ACES candidate as an Export-purpose canonical plan, executes that
  exact plan through `render_output_image()`, and compares it with the compatibility render at the
  complete decoded source dimensions. Evidence now includes candidate/reference full-image
  SHA-256 identities and the full-resolution dimensions in addition to sampled perceptual and
  clipping metrics. It also encodes full-resolution candidate/reference PNGs, verifies their
  decoded pixel round trips exactly, records counts above Delta E 00 thresholds 1/2/5, and creates
  a bounded Delta E 00 heatmap. The packaged batch command persists all three PNGs beside the
  report under content-addressed immutable filenames; the report accepts artifact filenames only
  when they match the hashes already bound into the evidence. JPEG and TIFF migration evidence now
  uses the same standard-export pixel preparation and `QImageWriter` encoding boundary as the
  production export job. It encodes candidate and reference files at production settings, decodes
  both shipping files, records candidate/reference round-trip and candidate-versus-reference Delta
  E 00 statistics, file sizes, bit depths, writer formats, and SHA-256 identities, and persists all
  four files under content-addressed immutable names. The evidence fails closed into a distinct
  unavailable-format blocker when the installed writer or decoder cannot complete that round trip,
  and reports a separate shipping-format difference risk when decoded output exceeds the migration
  thresholds. The assessor also compiles and executes separate canonical Export plans for sRGB and
  Display P3 simulation, measures each target's candidate/reference pair after color-managed sRGB
  normalization, binds the target plan keys and full-file hashes, verifies lossless PNG round trips,
  and persists dedicated Display P3 candidate/reference artifacts. The batch report exposes those
  measurements and a distinct target-specific-difference risk. PQ and HLG migration evidence now
  compiles separate canonical Rec.2100 Export plans, renders candidate and reference images as
  linear Rec.2020 floating-point surfaces through the same display-target contract as production
  export, and sends both through the production 10-bit AVIF writer. The shared
  writer/verification boundary checks decoded pixels against the expected PQ or HLG signal using
  mean, p99, and bounded-maximum error, and verifies Rec.2020 primaries, PQ/HLG transfer
  characteristics, Rec.2020 matrix coefficients, full-range signaling, dimensions, bit depth,
  mastering luminance, MaxCLL, and MaxFALL. Available files are persisted under content-addressed
  immutable names; missing encoders, failed writes, failed inspections, or failed pixel/metadata
  contracts remain `shipping-format-unavailable` blockers. Evidence also binds linear Rec.2020
  candidate/reference differences and a fixed diagnostic SDR-proxy Delta E comparison. The latter
  is explicitly labeled `not-hdr-approval`, and the report emits
  `hdr-perceptual-approval-required` until physical HDR and independent-review evidence is
  attached. Mask, layer, and reversible-preset attribution now removes each
  structure from both renderers, compiles a distinct canonical sRGB Export plan for that controlled
  state, rerenders candidate and reference, persists a dedicated lossless pair, and records the
  without-structure Delta E 00 values plus their signed change from the full composite. Preset
  application now persists asset-bound before/after adjustment patches and their SHA-256 identities;
  attribution replays those applications in reverse only after both hashes and the recorded
  after-state match the current recipe. Older provenance-only preset records are never guessed:
  they produce a `structure-attribution-unavailable` blocker with the exact reason. A batch-report
  boundary now accepts
  one assessment for every catalog asset, verifies each individual evidence SHA-256 before using
  its metrics, and deterministically classifies missing originals, unsupported profiles, candidate
  compile failures, missing/tampered evidence, high perceptual difference, added clipping, and
  presets needing attention. Its canonical v1 report has its own SHA-256 identity and create-only,
  idempotent immutable-file writer. The focused regression proves deterministic ordering and
  identity, duplicate-asset rejection, evidence-tamper rejection, every blocker/risk summary, and
  immutable artifact tamper rejection. The packaged `catalog_migration_inventory` helper now
  enumerates every recipe-bearing catalog asset in deterministic asset-id order, resolves relinked
  originals, records real file availability and catalog drive state, distinguishes canonical-v13
  from legacy-compatibility authority, carries the canonical payload/cache key or compatibility
  recipe needed by the candidate runner, and includes any existing migration record. Its focused
  Rust test proves unedited filtering, ordering, authority, legacy renderer provenance, missing
  source classification, and relink recovery. The production helper now requests `i64::MAX`
  explicitly; its first disposable-catalog execution exposed that SQL `LIMIT 0` returned no rows,
  and the corrected helper enumerated the edited catalog asset. A sequential batch-runner boundary
  now validates the inventory schema and count, rejects duplicate or assessor-mismatched asset
  identities, skips no edited entry, bypasses rendering only for a genuinely unavailable original,
  recognizes already-current ACES assets, invokes an injected production assessor for every other
  available asset, and atomically feeds the resulting complete set into the immutable report. The
  batch regression proves available/offline/current/legacy routing and incomplete-inventory
  rejection. The assessor now resolves every configured DCP/ICC identity before rendering and
  reports unavailable or unsupported assets by their exact slot and identity instead of collapsing
  them into a generic compile failure. Its evidence binds preview/output/working targets, soft-proof
  state, profile and preset identities, and mask/layer counts; risky structured edits and custom
  color targets receive separate review categories in the batch report. The packaged desktop
  command `--rendering-migration-report <new-path>` now runs the
  packaged inventory helper without opening QML, assesses every available migration candidate at
  full resolution, and writes the immutable report. On 2026-08-03 a disposable one-asset catalog
  produced candidate and evidence counts of one, correctly classified the synthetic image as
  high-difference with added clipping, shipping-format risk, and target-specific risk, and persisted
  nine candidate, reference, heatmap, JPEG, TIFF, and Display P3 artifacts. TIFF
  candidate/reference round trips were pixel
  exact (Delta E 00 zero); JPEG quality 95 introduced bounded codec loss (candidate p95 0.531,
  maximum 4.112; reference p95 0.705, maximum 4.120). The candidate-versus-reference p95 remained
  about 14.87 in both formats, correctly preserving the underlying renderer-difference risk. An
  independently rendered Display P3 pair also round-tripped exactly and measured p95 Delta E 00
  14.923 and maximum 15.847, compared with sRGB p95 14.871 and maximum 15.699. An
  idempotent rerun reproduced canonical payload identity
  `bf224d121b8284b6c8d61d8f0cf9b324b7775774c089589838832767eb9bfcc8` and persisted-file identity
  `4b6c9de37aa1ad1d110ea0a1b830cb53bfb9309cc76b1cf715afdb612fd15f32`; the command now labels both
  identities explicitly instead of leaving the self-excluding canonical hash ambiguous. This
  evidence item remains partial because real RAW/catalog execution and release evidence are still
  required. A second packaged disposable catalog with
  one effective linear mask and one adjustment layer produced both isolated comparisons, thirteen
  immutable artifacts, no unavailable-attribution blocker, and the expected mask/layer review
  category. Its idempotent rerun reproduced canonical payload identity
  `2e08f729d0dba873a7cfecef4ada3def38204e681dff47ba8b574da223d2e5bb` and persisted-file identity
  `2fdb9ce38ef1059b8e11823c49e40f5eb5955c48b46543c85d1cbdcc4068a664`.
  On 2026-08-03 the same structured disposable catalog was rerun through the production PQ/HLG
  migration path. It produced seventeen immutable artifacts, including candidate/reference PQ and
  HLG AVIF files, with no unavailable shipping format or unavailable structure attribution. Both
  transfers decoded as 10-bit full-range Rec.2020 with the expected transfer code, mastering
  display fields, 1000-nit peak, 0.0005-nit black, MaxCLL 1000, and MaxFALL 400. PQ candidate and
  reference round-trip p99 encoded-signal errors were approximately 0.00398 and 0.00591; HLG values
  were approximately 0.00383 and 0.00587. An exact rerun reproduced canonical report payload
  identity `49fe7567506fd30cbecee08569bac31efa92a578de24a11b409d2a497912d550`
  and persisted-file identity
  `63a143bc6848a538b2b1afe8ad48990a178ce7dbe0390fce82b0ecf86eaceba7`.
  The report correctly remains attention-required under
  `hdrPerceptualApprovalRequiredAssetIds`; synthetic rendering and codec inspection do not satisfy
  the Phase 10 physical HDR/editorial gate.
- [~] Preserve the current render until the user explicitly accepts migration, individually or in a
  reviewed batch.
  The individual flow previews without mutation and requires an explicit measured `Use ACES 2`
  action; `Keep Current` restores the exact recipe and acceptance is one undoable renderer change.
  Durable archival rendering, catalog-recorded acceptance/rollback, and reviewed batch acceptance
  are not complete.
- [x] Record migration source version, old renderer, new renderer, profile identities, plan hash,
  evidence hash, approval time, and rollback pointer.
  PostgreSQL migration 0011 adds `recipe_migration_records` with all named fields, constrained
  candidate/accepted/rejected/rolled-back status, and a database rule that accepted records require
  approval time. The repository validates required identities and hashes, exposes list/upsert plus
  `catalog_state set-recipe-migration`, and cascades records with their asset. The individual UI
  generates and displays measured candidate evidence before enabling acceptance. Its dedicated
  helper derives referenced profile identities from the reopened canonical candidate, creates the
  mandatory PostgreSQL backup, verifies evidence bytes against their SHA-256 identity, stores those
  bytes immutably, and commits the compatibility projection, canonical v13 authority, and accepted
  record in one PostgreSQL transaction. Repository, immutable-evidence, focused UI workflow,
  catalog compilation, and disposable PostgreSQL helper proofs pass.
- [x] Refuse destructive migration when the source file, profile asset, catalog backup, or required
  renderer is unavailable.
  The UI does not offer acceptance without the pinned official renderer and synchronized measured
  images. Candidate preparation requires exact decoder/source provenance, reopens canonical v13,
  resolves its content-addressed assets, and fails closed on missing/tampered assets. The helper
  refuses missing or invalid evidence, cannot write before a verified catalog backup, and performs
  all accepted authority changes in one transaction.
- [~] Provide a migration report for unsupported profiles, missing originals, high-difference
  assets, and presets needing attention.
  `rendering_migration_batch_report` now owns the fail-closed, identity-bound report schema and
  immutable writer. It reports all four required categories plus candidate failures, invalid or
  missing evidence, added clipping, readiness totals, and whether every edited asset has a
  candidate and valid evidence. On 2026-08-03 `rendering_migration_proof_test` and
  `rendering_migration_batch_report_test` passed in 1.94 and 0.26 seconds, respectively;
  `export_job_controller_test` passed in 17.80 seconds after the shared production encoder
  extraction; the desktop app built; and the packaged command consumed the corrected inventory
  helper and generated nine immutable artifacts from a disposable catalog. The backed-up real
  catalog has now been processed: the complete 47-asset report has evidence for 38, and an isolated
  corrective report proves the other seven available originals after explicit structural semantic
  migration. The final identity-verified merged report now covers all 47 catalog entries and carries
  every corrected component identity: 45 have compiled candidates and valid evidence, while the two
  unavailable originals remain reported rather than silently omitted. No available asset has an
  unavailable shipping format or unavailable structure attribution. The item remains partial only
  because the two missing originals need an explicit disposition and high-difference, clipping,
  mask/layer, target, HDR, and shipping risks require the recorded Phase 10 review. The inventory
  helper's Rust test passes independently and release packaging/verification require the helper
  binary.
- [ ] Announce the last release capable of rendering unmigrated legacy recipes.

### Production cutover

- [~] Make v13 the only writable recipe.
  Current-state audit confirms this is a structural migration, not a version-number edit:
  `EditRecipeState` still writes `recipeVersion: 12`, while canonical v13 requires non-empty
  `recipe_id`, `input.source`, decoder provenance, rendering target, and external
  profile/mask/layer/LUT asset identities that are supplied only by render-call context today.
  The cutover therefore required persistence APIs to accept an asset-bound identity context,
  a complete v13-to-desktop projection for UI reload, and separate storage for presentation-only
  UI state. A wrapper that embeds the complete v12 render state beside v13 is explicitly not
  completion because it would preserve two writable rendering authorities. The new fail-closed
  `serialize_desktop_recipe_as_canonical_v13()` boundary completes canonical write construction and
  proves its payload with the engine reader. Asset-free v13-to-desktop projection is also complete
  and byte-identical on rewrite. The asset-backed audit also proves why those writes cannot be
  switched without durable assets: render admission derives content-bound identifiers for mask
  payloads, every layer kind, and LUTs. Persisting identifiers alone would create an unrecoverable
  recipe. `RecipeAssetStore` supplies the required storage primitive: payload bytes are
  atomically stored under canonical SHA-256 identities, identical writes deduplicate, reads verify
  the digest, and malformed identities, missing payloads, and tampering fail closed.
  `recipe_asset_store_test` proves binary round-trip, idempotency, identifier admission, and
  corruption detection. Its tagged v1 envelope also preserves nested byte arrays, signed and
  unsigned 64-bit integers, numbers, strings, booleans, maps, and lists without implicit Qt JSON
  coercion; unsupported types and non-finite numbers fail closed.
  `serialize_desktop_recipe_as_canonical_v13_with_assets()` now externalizes normalized mask
  payloads, all adjustment/pixel/text/fill/group layer content, and layer LUT payloads into that
  store before emitting v13. The matching resolver restores every layer and accepts it only when
  reserializing the resolved desktop projection produces the byte-identical canonical v13 payload;
  divergent structural/content authority fails closed. Focused proof covers binary brush masks,
  pixel and adjustment layers, a layer LUT, SHA-256 references, reopen, exact recovery, cache-key
  tampering, and divergence rejection.
  PostgreSQL migrations 0010/0012 store the canonical payload and cache key plus a separately
  validated non-rendering provenance payload in `canonical_edit_recipes_v13`.
  Update (2026-09-08): `catalog-recipe-reopen-2026-09-08.md` supersedes the v1-only
  persistence boundary below with backward-readable provenance v2 history and
  explicit v13.3 non-sRGB delivery-space serialization. Historical evidence:
  the provenance schema was `photo-editor-edit-recipe-provenance:v1` and owned only the
  ordered reversible `presetApplications` audit. It is deliberately excluded from the engine v13
  payload and cache identity: focused proof changes provenance while requiring the canonical render
  payload and cache key to remain byte-identical, then verifies document projection, catalog reload,
  migration inventory, and sidecar reopen restore the audit exactly. Empty provenance is admitted
  only as backward-compatible input for older canonical documents; every new write emits the v1
  payload. PostgreSQL migration 0012 upgrades existing rows to the explicit empty v1 payload and
  constrains the JSONB column to that schema. Repository snapshots, migration inventory, backup,
  restore, and dependent deletion include the complete canonical record. Normal editor and batch/agent
  catalog writes call the same preparation service, require exact decoder provenance, and use one
  transaction to update canonical authority plus the v12 compatibility projection. The packaged
  save helper no longer accepts a legacy-only invocation, and `catalog_state` no longer exposes
  `set-edit-recipe`. Catalog reload prefers and validates canonical v13 and refuses stale-v12
  fallback when canonical data exists. `catalog_recipe_projection` makes that authority decision a
  focused boundary; its regression supplies conflicting v12/v13 values, proves valid canonical v13
  wins, then tampers the canonical cache key and proves reload returns no v12 fallback.
  `write_canonical_v13_recipe_sidecar()` and its reader provide atomic canonical envelopes with
  adjacent immutable assets. The old JSON sidecar writer is deleted; only the data-only
  compatibility reader remains, with a fixture-driven regression proving import and malformed
  input rejection.
  On 2026-08-03 `edit_recipe_v13_persistence_test` passed in 0.12 seconds, the complete
  `photo-catalog` package passed 18 library tests plus all helper tests with only the explicitly
  provisioned-PostgreSQL test skipped, `rendering_migration_proof_test` and
  `rendering_migration_batch_report_test` passed in 4.17 and 0.29 seconds, the desktop application
  built, and the architecture boundary validator passed. This closes the preset-provenance loss
  at the canonical save/reopen boundary; the item remains partial only because the v12-shaped
  editor presentation model and compatibility-release renderer still exist.
  Presets, preset packs/sync, camera defaults, and persisted named history snapshots no longer
  create v12 recipe documents: they use the versioned adjustment-patch codec described in Phase 6,
  with explicit source-agnostic, camera-bound, or asset-bound scope and legacy input admitted
  read-only. History file ownership is extracted into `edit_history_snapshot_store`; its v2
  envelope writes only asset-bound patches, caps reopen to 50 snapshots per asset, and uses atomic
  replacement. Export admission now normalizes every non-empty legacy or patch input to an
  asset-bound patch before durable queue insertion. The C++ recovery store emits
  `adjustment_payload`, the Rust helper accepts the former `recipe_json` key only as a deserialization
  alias, and export preflight, render, mask trace, and parity identity decode the typed patch without
  direct v12 parsing. Empty export adjustments remain an explicit default asset-bound state.
  Crash-session autosave/recovery and Carousel's editor round trip now use the same asset-bound
  patch contract. New Carousel document media/layer writes use `adjustmentPatch`; legacy
  `recipeJson` document fields are read-only compatibility input and are rewritten on the next
  save. Catalog recovery overrides and virtual-copy session records likewise write asset-bound
  patches, and menu exports request a patch projection even when Edit is not the active workspace.
  The architecture guard rejects restoration of the old QML handoffs or new session, catalog
  recovery, Carousel, preset/default, history, or export legacy writers.
  `carousel_adjustment_patch_round_trip_test` independently proves an asset-bound patch applies
  exactly and undoes to the adjustment-free document state without requiring the full
  PDF/Poppler-dependent Carousel suite. That proof exposed and fixed a compatibility-reader bug:
  an absent legacy `recipeJson` field was being interpreted as a present default recipe during
  history restore. Compatibility parsing now runs only when the old field is actually non-empty.
  The rendering-upgrade UI now supplies the candidate/evidence/approval coordinator and accepted
  migration transaction described above. Catalog items, thumbnail tasks, recovery overrides,
  virtual copies, and multi-photo undo/redo now retain canonical asset-bound patches; patch state is
  decoded only when an operator or the v13 persistence service needs it. This item remains partial
  because the editor presentation model still retains a v12-shaped compatibility projection, and
  the Qt-independent v13 document is not yet the sole application state model. Copied settings,
  preset previews, selective batch sync, export jobs, session recovery, agent planning/model/repair
  requests, agent draft/rollback/commit state, editor-agent undo handoffs, and Carousel editor
  handoffs no longer cross their boundaries as v12 documents.
  The editor's public v12 projection surface is now physically removed:
  `currentRecipeJson`, `copiedSettingsRecipeJson`, `presetRecipeJson`,
  `presetPreviewRecipeJson`, `currentCompatibilityRecipeJson`,
  `compatibilityRecipeJsonForState`, `stateFromRecipeJson`, and
  `restoreStateForAsset` no longer exist. The remaining load/restore boundary is named
  `stateFromAdjustmentPayload`; it accepts typed patches and admits legacy documents only through
  the isolated compatibility reader. Cross-photo clipboard, preset, and selective-batch
  projections are explicitly source-agnostic. Same-photo session recovery, menu export,
  Carousel, Studio conflict comparison, migration evidence, and complete-state persistence are
  explicitly asset-bound.
  Preview cache/job identities, preset thumbnails, and human-readable history diffs are patch
  native; `editor_preview_controller.cpp` no longer calls the v12 serializer. The affected cache
  namespaces were bumped so old recipe-derived entries cannot alias the new identities. Before
  entering the recipe codec, identity generation replaces a mask or layer containing a string or
  binary payload above 4 KiB or a list above 256 entries with a small valid SHA-256 identity
  surrogate carrying its exact encoded byte count. This keeps large raster masks and 10,000-point
  brush histories out of hot cache keys while retaining whole-entry change sensitivity.
  `edit_recipe_state_test` proves deterministic bounded identity plus one-byte and one-point
  invalidation.
  The remaining batch-delivery map seam is now patch-native as well. `ExportView.qml` and
  Library's selected-export command publish `adjustmentPatchJson`, and both batch preflight and
  queue admission read that exact field instead of the deleted public `recipeJson` catalog key.
  Library's selection callback also dropped its eight unused metadata/edit parameters and the
  nonexistent `modelData.recipeJson` handoff. The representative 500-asset workflow and the
  export controller suite pass in 10.70 and 25.00 seconds, proving edited delivery assets retain
  their asset-bound patch through preflight, durable queue insertion, render, and output. That
  verification also exposed a proof-target ownership defect: the mask-thumbnail payload depended
  on image encoding and pixel-metric functions implemented inside the full refresh coordinator.
  Those stateless helpers now live in the focused header-only `thumbnail_image_payload.h`, so
  narrow render/export targets link without importing the entire thumbnail queue. The complete
  359-unit Framehouse application/QML-cache build then compiled and linked successfully, and the
  architecture boundary rejects any renewed Library or Export batch `recipeJson` handoff.
  Carousel's in-memory `MediaItem` and `CanvasItem` now retain the canonical asset-bound
  adjustment patch and derive the v12-shaped `EditRecipeState` only as a presentation projection.
  Add, replace, assign, and external-apply boundaries validate and canonicalize the patch and fail
  closed on invalid edit state. Current documents write only `adjustmentPatch`; the two old
  document `recipeJson` reads and the `primaryMediaRecipeJson` settings read remain explicit
  compatibility inputs and are normalized to a patch before use. Source search finds no
  `serialize_edit_recipe_json()` call, `recipe_json` field, or old recipe-named application helper
  in the Carousel controller. The focused Carousel adjustment-patch save/reopen/apply/undo proof
  passes in 1.90 seconds, the complete application builds, and the full Carousel suite reaches its
  existing external `pdfinfo` gate after 8.02 seconds rather than failing in the changed document
  migration path.
  The modern adjustment-patch codec no longer creates a temporary v12 document internally.
  `build_edit_adjustment_state_object()` and `parse_edit_adjustment_state_object()` encode the
  versionless application projection directly, reject an embedded `recipeVersion`, and leave the
  old versioned document codec solely to named compatibility readers/writers. `RenderPlanStore`
  and derived-asset v13 compilation also hash that versionless state instead of calling
  `serialize_edit_recipe_json()`. The adapter, persistence, render-plan, and state suites pass 4/4
  in 1.17 seconds, the architecture boundary freezes the separation, and the complete Framehouse
  target compiles and links without growing the frozen state header or render-plan owner.
  Catalog reopen no longer projects valid canonical v13 back into a v12 JSON document. The catalog
  authority boundary now returns a validated asset-bound adjustment patch directly; a legacy-only
  PostgreSQL row is converted once through the read-only compatibility path, while invalid
  canonical data still fails closed without falling back to the conflicting old row. Catalog and
  migration consumers parse only the patch. The focused v13 persistence, migration proof, and
  catalog suites pass in 0.50, 7.52, and 8.76 seconds; Framehouse links; architecture and diff
  checks pass. Production source search now limits direct v12 serialization to the deliberate
  PostgreSQL compatibility-pair writer and the explicitly compatibility-only automation response,
  plus the legacy migration codec itself. Those writers remain until the announced compatibility
  release and accepted catalog migration permit physical deletion.
- [x] Remove implicit legacy selection for blank, malformed, or unknown renderer names. Desktop and
  engine normalization now select `aces2-ocio-v1` unless the serialized value is the exact
  compatibility identity `legacy-oklch-v1`; focused recipe and engine tests preserve explicit old
  recipes while proving unknown names cannot create a new legacy write. Internal tone-table cache
  keys also default to official ACES 2, so an incompletely initialized cache identity cannot fall
  back to the compatibility renderer.
- [~] Stop creating new legacy caches, presets, thumbnails, or output.
  Catalog, saved-preset thumbnail, canonical sidecar, normal editor, batch/agent, preset/default,
  history-snapshot, export-job, crash-session, Carousel-document, and accepted-migration writes now
  use canonical v13 or adjustment-patch authority. Clipboard, preset preview, selective batch,
  catalog recovery/virtual-copy state, and Carousel editor traffic also use patches. Cache/job,
  preset-thumbnail, history-diff, Studio, session, export, Carousel, migration-evidence, and agent
  handoffs are patch-native as well. The remaining blocker is no longer a public editor/agent v12
  handoff: it is the internal v12-shaped `EditRecipeState` presentation projection plus the
  announced migration-release compatibility reader and renderer. They cannot be deleted until the
  compatibility release is shipped and migration evidence/approval permits the legacy-free major
  cutover.
  Crash recovery no longer exposes the obsolete `recoveredRecipeJson()` API or stores its modern
  payload in a recipe-named member. The only public recovery handoff is
  `recoveredAdjustmentPatchJson()`; the on-disk `recipeJson` key remains solely in the explicit
  read-only compatibility branch and the next autosave rewrites it as `adjustmentPatch`.
  Named in-state history snapshots also stopped creating nested v12 documents. New snapshots
  embed a validated asset-bound v1 `adjustmentPatch`; the former `recipeJson` member remains
  accepted only when reading an old snapshot. `app_shell_controller_test`,
  `agent_edit_controller_test`, and `edit_recipe_state_test` pass in 0.66, 39.93, and 0.62 seconds,
  and the architecture boundary rejects restoration of either modern legacy handoff.
  Modern output and preview publication now reject a missing canonical plan with the stable
  `canonical_plan_required_for_aces2` reason. The generic diagnostic backend cannot be called from
  production desktop sources, so a new ACES image-producing surface cannot silently create output
  through the compatibility executor. Plan-less output remains executable only when the state
  names exact `legacy-oklch-v1`, which is still required for migration reference comparisons until
  the compatibility release is retired.
  Catalog thumbnail cache paths and stale-result diagnostics no longer hash raw v12 serialization.
  They use a versioned SHA-256 identity of the canonical asset-bound adjustment patch, so equivalent
  supported legacy and patch inputs share one identity while the old cache namespace is invalidated.
  The focused catalog suite passes with regenerated edited thumbnails and stale-task detection.
  Agent draft application, cancellation restore, review rollback, whole-draft rollback, preset
  application, and crop-variant creation now also enter the catalog through scoped patches. The
  public catalog `applyRecipeJsonToAssetIds` and preset-recipe writer APIs are physically deleted.
  Import presets now require a validated source-agnostic adjustment patch and apply through the
  same patch entry point after import. Agent analysis, planning, proof, reporting, and batch paths
  now read and decode the asset-bound patch projection directly; local-model planning, clustering,
  and validation-repair requests and prompt audit use the patch document rather than materializing
  v12 JSON. Resume validation, iterative draft validation, annotated final validation, and the
  deterministic fast-preview path now serialize and parse an asset-bound adjustment patch
  directly instead of producing v12 only to convert it back into a patch. Source search finds no
  `serialize_edit_recipe_json()` call in `agent_edit_controller.cpp`; the complete agent suite
  passes in 39.09 seconds, Framehouse links, and the architecture boundary rejects reintroduction.
  They no longer read `selectedRecipeJson()` directly. Exactly one `asset.recipeJson`
  read remains inside a named adapter to admit migration-window callers that have not yet
  published `adjustmentPatchJson`. Agent
  original/next draft maps, cancellation restore, rollback, commit reports, and QML Studio
  handoffs retain asset-bound patches. `EditorPreviewController::applyExternalRecipeEdit` is
  deleted; `applyExternalAdjustmentPatchEdit` validates an asset-bound patch and enters the normal
  named undo/redo history path. The complete agent controller suite and focused external-agent edit
  history proof pass after this cutover. Automation and MCP now expose `photo.read_adjustments` /
  `photo_read_adjustments` as the canonical read API with a validated asset-bound-patch regression;
  `photo.read_recipe` is explicitly labeled compatibility-only until the announced migration
  release permits its removal. The complete automation bridge suite passes. The repair loop handles
  a compressed bright or dark histogram deterministically when a local model returns the unchanged
  unsafe patch, instead of stopping after one proof with `repair_produced_same_patch`.
  Catalog asset metadata maps (`assetAt`, visible/project assets, survey/export inputs) no longer
  publish `recipeJson`; `adjustmentPatchJson` is their only edit-intent field. Agent, catalog, and
  representative import-cull-edit-export workflow suites pass against that patch-only public map.
  The direct QAbstractListModel route is also patch-only: `RecipeJsonRole` and the
  `selectedRecipeJson` QML property are deleted and replaced by `AdjustmentPatchJsonRole` /
  `selectedAdjustmentPatchJson`. Desktop smoke fixtures also use that projection for offline-edit
  retention and multi-photo history. The complete desktop/QML target and smoke runtime build, and
  the agent, automation, representative-workflow, Carousel patch, and catalog suites pass with no
  direct legacy role, property, or writer API. On 2026-07-30 the post-removal
  `catalog_asset_model_test` passed in 25.55 seconds, the architecture boundary script and
  `git diff --check` passed, and source search found none of the deleted symbols. The broader
  `desktop_shell_source_contract_test` and `desktop_shell_offline_recovery_test` remained red
  before reaching the changed assertions, at their existing Carousel Edit-return and Library
  search-contract checks respectively; these failures are recorded as unrelated baseline blockers,
  not proof of this cutover.
  Exposing patch projections through `assetAt()` initially revealed an older culling-path defect:
  every decision synchronously rewrote the complete local recovery file. Culling now queues its
  authoritative per-asset persistence immediately while coalescing only that redundant recovery
  snapshot for 25 ms and flushing it on model destruction. The 500-photo scripted culling proof
  improved from a failing 70 ms p95 to 0 ms on the same debug build.
  Catalog's internal `CatalogAssetItem`, thumbnail task result, recovery override, virtual-copy,
  and batch-history storage no longer contain `recipe_json` or v12 undo maps. The persistence queue
  accepts an asset-bound patch, decodes it once for canonical v13 preparation, and produces the
  compatibility projection only as an argument to the migration-window helper transaction. The
  post-cutover catalog, agent, automation, and representative-workflow suites passed in 25.55,
  127.34, 35.52, and 16.85 seconds respectively; the focused local-model patch-request test,
  external-agent edit history proof, and
  agent walkthrough contract also passed. The desktop app, thumbnail benchmark, and smoke runtime
  compiled. The architecture guard now rejects any restoration of catalog `recipe_json`, direct
  v12 parsing, v12 batch-history maps, the old external-recipe editor handoff, or the old agent/QML
  recipe-map keys. On the final 2026-07-30 verification, the complete agent suite, desktop app
  target, architecture boundary script, deleted-symbol search, and `git diff --check` were green.
  The final editor-boundary verification also built the desktop app and complete controller target,
  passed `edit_recipe_state_test`, `agent_edit_recipe_patch_test`,
  `editor_preview_before_after_recipe_test` (0.70 seconds),
  `editor_preview_history_test` (0.72 seconds), `rendering_migration_proof_test` (1.06 seconds), and
  `ux_representative_workflow_test` (16.46 seconds). The architecture guard and diff check passed,
  deleted public recipe symbols were absent, and the controller returned below its frozen size
  ceiling at 51,457 lines. The complete `editor_preview_controller_test`, including repeated
  10,000-point mask undo/redo settling, passed in 307.50 seconds after the identity work was moved
  before the recipe codec.
- [ ] Require migration completion or explicit export/archive action before installing the
  legacy-free major version.
- [ ] Preserve a data-only legacy parser only if it is required to identify an unsupported old
  project; it may not contain rendering behavior or silently approximate output.
- [ ] Store accepted v13 recipes and any user-requested archival renders independently of the old
  runtime.

### Required deletion inventory

- [ ] Delete `LegacyOklchV1` and its output-rendering implementation.
- [ ] Delete the native `Aces2HellwigJmhV1` prototype if no approved v13 recipe uses it.
- [ ] Delete UI choices, presets, migration defaults, and cache identities for removed renderers.
- [x] Delete duplicate `color_pipeline_config_for_state()` implementations. Preview and export
  use `compile_color_pipeline_config()` from `edit_recipe_pipeline_bridge.h`; the focused bridge
  test rejects reintroduction of either caller-local implementation.
- [x] Delete duplicate `to_pipeline_state()` implementations. The shared bridge owns global
  desktop-to-engine adjustment mapping; explicitly named preview/export wrappers only attach
  target-specific raster and layer payloads, while carousel delegates its complete render.
- [x] Delete the carousel-specific color renderer. Carousel image decode now calls
  `decode_source_image()` with the complete recipe and retains `DecodedImageMetadata`; edited
  output calls `render_output_image()` with that metadata, so camera-linear RAW characterization,
  shared color operators, ACES output, and target conversion use the canonical desktop path.
  Carousel-specific crop composition remains outside the color renderer and runs after canonical
  full-source evaluation so lens and mask coordinates remain correct. The focused bridge
  source contract passes and the controller target builds; the broad carousel test reaches its
  pre-existing external `pdfinfo` gate and remains baseline-blocked there. The explicit
  original-media route disables engine input ownership during RAW decode, receives the
  decoder-characterized scene-linear fallback, and performs a real output color-space conversion;
  it no longer tags camera-linear pixels as display RGB.
- [x] Delete duplicate Metal backend ownership and the standalone mask/color entry points.
  Preview, export, engine stage helpers, working-image publication, and capability probing now
  share `shared_metal_pipeline_backend()`. The former independent backend instances created
  separate command queues and transform caches, and the now-unreferenced `apply_masks()` and
  `apply_color_lut()` APIs are physically removed after the production caller moved to one
  instrumented fused dispatch. The source contract rejects both deleted method definitions.
- [x] Delete the metadata-blind catalog QImage adjustment convenience path. Catalog thumbnails now
  retain `DecodedImageMetadata`, render the full source through `render_output_image()` before crop,
  explicitly target sRGB, and rebuild editable masks at thumbnail resolution. This prevents
  camera-linear RAW pixels from being treated as tagged preview RGB and keeps lens/mask coordinates
  in the full-source domain. The obsolete `apply_color_adjustments(QImage*)` API is deleted, and
  the thumbnail cache schema is bumped to `photo-v4-canonical-metadata`. ACES catalog thumbnails now
  fail closed through Thumbnail-purpose canonical admission; explicit legacy recipes retain only
  their compatibility render. `catalog_asset_model_test` passed in 33.26 seconds, and the bridge
  source contract requires the catalog admission boundary. Built-in preset thumbnails also admit a
  stable synthetic-source Thumbnail plan rather than invoking compatibility mode. Their independent
  expected-render test now compiles the same purpose-specific graph instead of comparing against
  the retired compatibility path. The non-RAW fixture excludes decoder-only RAW exposure while
  preserving creative exposure, and saved preset state removes only dangling layer-to-mask
  references before persistence rather than weakening v13 validation. The focused
  `PHOTO_EDITOR_TEST_PRESET_THUMBNAIL_ONLY=1` regression and complete
  `editor_preview_controller_test` passed; the latter completed in 332.31 seconds.
- [x] Delete the obsolete engine version-3 `EditRecipe` type and empty implementation. Its only
  caller was a test-only candidate migrator; no production reader admitted that schema, so the
  header, empty implementation, migrator, build entries, and obsolete migration tests are removed.
- [x] Delete the uninstantiated `AgentWalkthroughDialog.legacyContentItem` UI and its private
  delegates. The replacement `AgentConversationView` is the only popup content owner and retains
  executable before/after sample review and approval behavior.
- [x] Delete the unused timeline clip effects input and redundant per-row effect search path after
  their replacement owners were verified by focused desktop contracts.
- [ ] Delete desktop ownership of the v12 recipe after the compatibility reader is retired.
- [x] Delete the descriptive-only `RenderGraph` implementation. Typed plan/execution contracts live
  in `RecipeCompiler` and `RenderExecutor`; real tile-planning behavior was extracted into
  `render_tile_planner`, and the string-stage graph, diagnostics scaffold, build entry, and
  self-referential smoke test are removed.
- [x] Delete the Core Image `CIRAWFilter` color-rendering backend. The filter configuration,
  metadata synthesis, size-validation fallback, and all production references were physically
  removed from `raw_image_decoder.mm`; Core Image remains only for raster/native-image platform
  I/O, not RAW color rendering.
- [x] Delete duplicated engine exposure, temperature, and tint ownership. `ColorAdjustmentState`
  contains only `InputTransformSettings` for these global input-stage controls, effective-value
  accessors no longer fall back to shadow fields, state/cache equality has one value per control,
  and the desktop bridge writes those authoritative fields directly.
- [ ] Delete legacy `outputTransform`, flat output-color, and deprecated mask/layer fields after
  migrated writes and fixtures prove they are no longer needed.
- [x] Delete implicit core QColorSpace conversions that bypass the compiled color/output plan.
  All six remaining production Qt transforms are explicit boundary operations routed through
  `qimage_color_conversion.h` with a required purpose: decode normalization, output publication,
  monitor publication, or proof publication. The architecture gate rejects direct
  `convertedToColorSpace()`/`convertToColorSpace()` calls anywhere else in desktop production
  code, and `qimage_color_conversion_test` proves output format/profile/alpha preservation plus
  fail-closed invalid-image and invalid-profile behavior. Creative, camera, working-space, and
  photographic output transforms remain owned by the compiled engine plan.
- [ ] Delete stale fast/exact branches, LUT paths, cache versions, and backend fallbacks that serve
  only removed renderers.
- [x] Delete obsolete QML editing surfaces and controller properties after one production surface
  owns the workflow. The complete duplicate editing surface (`EditView.qml`, `EditWorkspace.qml`,
  and `EditWorkspaceHost.qml`) is physically deleted and guarded from reintroduction. An exhaustive
  active-caller scan of controller `Q_PROPERTY` names found three implementation-only APIs:
  `displayReferencePreviewOverlaySource`, `debugInfo`, and `systemMonitorProfileIdentity`. Their
  properties/getters were deleted; the unconsumed debug string, signal, refresh method, and all
  refresh calls were also removed. The same audit proved that the former reference-overlay result
  fields were never assigned and had no active provider/QML consumer, so their struct fields,
  controller state, provider slot, publication, and repeated clear operations were deleted while
  retaining the active edited overlay and full reference preview. The active Studio app build,
  source/outline contracts, and full
  `editor_preview_controller_test` pass, and the architecture boundary rejects restoration of those
  four retired API/method names.
- [x] Delete the rejected SQLite production authority and PostgreSQL-to-SQLite migrator. Retain
  PostgreSQL runtime, schema, backup, packaging, and operational code as required product behavior.
- [ ] Delete tests that only lock removed output after replacement migration, parity, and behavioral
  tests exist.
- [x] Delete stale documentation and mark historical plans as historical; do not leave two current
  sources of truth. `color-engine-industry-leading-plan.md` and `color-science-roadmap.md` now
  identify themselves as historical and point here as the sole execution/completion authority.
  Focused mathematical, RAW-fixture, color-tool, recipe, and preview plans now identify themselves
  as supporting evidence only and link back to this strategy; synthetic fixture documentation also
  states that it cannot satisfy the real-media, hardware, independent-reference, or review gates.

### Deletion gate

- Source search finds no production reference to removed renderer names, recipe fields, duplicate
  compilers, old RAW backend, or obsolete database runtime.
- A clean build cannot construct or select a legacy renderer.
- Migrated catalogs open, edit, export, relaunch, back up, and restore without compatibility code.
- Release notes clearly state the compatibility window and migration requirement.
- Deleted code is not retained behind an undocumented flag.

## Deferred full-application verification matrix

This matrix is retained for the later app-foundations and release program. It is not the active
color-engine checklist; the five gates at the top of this document are authoritative for the
current goal.

| Gate | Synthetic | Real media | CPU | Metal | CUDA | macOS | Windows | Human/physical |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Recipe compile/migrate | Required | Required | Required | N/A | N/A | Required | Required | Review |
| RAW input/characterization | Required | Required | Required | Required | Required | Required | Required | Editorial |
| Tone/highlight/shadow | Required | Required | Required | Required | Required | Required | Required | Editorial |
| Global/local/layer stack | Required | Required | Required | Required | Required | Required | Required | Review |
| Monitor preview | Required | Required | Required | Required | Required | Required | Required | Measured display |
| Preview/export parity | Required | Required | Required | Required | Required | Required | Required | Editorial |
| HDR/proof/print | Required | Required | Required | Required | Required | Required | Required | Display/print |
| Performance/memory | Required | Required | Required | Required | Required | Required | Required | Workflow |
| Catalog migration/recovery | Required | Required | Required | N/A | N/A | Required | Required | Clean machine |
| Legacy deletion | Required | Required | Required | Required | Required | Required | Required | Migration approval |

No row is complete if a required real-media, hardware, platform, or human cell is skipped.

## Historical full-program implementation order and change discipline

The required order is:

1. Stabilize current gates.
2. Introduce the canonical recipe and compiler beside current callers.
3. Make the graph executable.
4. Route preview and export through the graph.
5. Correct display targeting and RAW ownership.
6. Replace tone/stack semantics and promote the final renderer.
7. Finish GPU residency and performance.
8. Decompose desktop ownership.
9. Complete catalog/product-foundation migration.
10. Collect real-world evidence.
11. Ship the compatibility release.
12. Migrate users, cut over, and delete legacy code.

Each implementation pull request must:

- cover one bounded architectural seam;
- include before/after plan or pixel evidence where rendering changes;
- state recipe/cache/migration implications;
- run the smallest focused tests plus affected parity and migration gates;
- record hardware skips honestly;
- avoid updating golden hashes without an approved explanation and visual evidence;
- preserve unrelated dirty-tree work;
- include a deletion follow-up and release target for any temporary compatibility path.

## Deferred app-foundations completion gate

After the five-gate color-engine goal is complete, the broader application-foundations program is
complete only when all of the following are true. These items do not delay the current color-engine
completion decision unless they are explicitly mapped to Gates 1–5 above:

- [ ] One Qt-independent v13 recipe is the only writable recipe.
- [ ] One compiler creates the render plan for every image-producing surface.
- [ ] One executable graph runs preview, export, thumbnail, scopes, presets, carousel, and batch
  work.
- [ ] Settled preview and export use one camera-linear RAW contract.
- [ ] The monitor ICC conversion is the final display step, with no default sRGB bottleneck.
- [ ] Global exposure is universal scene-linear gain and tone controls are monotone and measured.
- [ ] Equivalent global/local/layer operators match before explicit composition.
- [ ] The official ACES 2 renderer is the only production photographic renderer.
- [ ] All built-in presets are retuned and approved for that renderer.
- [ ] Draft feedback, settled preview, memory, cancellation, and GPU budgets pass on target hardware.
- [ ] Preview/export, CPU/GPU, RAW, HDR, proof, and print gates pass on the real evidence corpus.
- [ ] The professional shoot workflow passes import through recovery on clean macOS and Windows
  machines.
- [ ] Existing catalogs and recipes migrate with backup, evidence, approval, and rollback.
- [ ] Legacy renderer, recipe, decoder, duplicate mapping, cache, UI, and optional database-runtime
  code in the deletion inventory is physically removed.
- [ ] Headless, desktop, sanitizer, Rust, packaging, clean-machine, and upgrade gates pass from a
  clean revision.
- [ ] Release documentation names no synthetic, skipped, or source-only check as real-world proof.

At that point the application can claim a unified, wide-gamut, scene-referred, RAW-trustworthy,
preview/export-consistent, performance-bounded color engine and a maintainable desktop foundation.
Until then, the status is “strong implementation in migration,” not “fully complete.”

## Color selection stability correction — 2026-10-22

A user report of hue/saturation edits producing noise in apparently unrelated areas exposed a
shared selection defect. The named mixer ranges could apply full control strength to tiny
near-neutral channel differences. OKLab/LCH also used RGB hue-range centers on their different
hue scale. The same weak-neutral selection affected black-and-white mixing and replacement.

The correction applies one fixed sRGB selection reference, adapted from the actual working gamut,
to all mixer models, black-and-white mixing, and color replacement. Selection hue uses the sRGB
encoding expected by the existing picker; negative values are bounded only in the selection copy,
and HDR selection encoding remains unbounded. Working pixels retain their original domain.
Replacement target hues now use that same reference rather than the current working primaries.
The mixer model retains its existing adjustment-strength response.

Selection confidence fades smoothly across relative linear-RGB spread 0.02–0.12 and the linear
black floor 0.00001–0.001. It is measured from the source, before the edit, and also attenuates
harmony near neutral. Hue-selective compiled edits return the original pixel exactly when
confidence is zero. Global saturation, vibrance, and intentional color grading retain their
existing behavior. This is selection stability, not spatial denoising.

The reference CPU, compiled CPU, local/layer paths, standalone Metal source, and embedded Metal
fallback share this contract. CPU mixer selection tables were consolidated; inactive compiled
channels are skipped. The GPU payload includes the working-to-selection matrix, validates it,
and includes it in cache identity. Thumbnail color-pipeline identity is now v7 so cached edited
thumbnails refresh. Existing mixer recipes may render differently with the corrected selection;
there is no new recipe field or user setting.

### Evidence and limits

- `color_selection_stability_test` covers all four models, eight named channels, seven channel
  controls, four working gamuts (sRGB, P3, ProPhoto, Rec.2020), six near-neutral noise directions,
  shadows through extended highlights, unrelated colors, continuous falloff, reference/compiled
  agreement, and related selectors. Unreliably colored compiled samples must remain exact.
- Fourteen focused headless native tests pass, covering the new regression, color invariants,
  transforms, properties, charts, curves, profiles, output, scheduling, LUTs, video transforms,
  and direct layer color.
- Six Release checks pass on the Apple M4 Pro: the regression, 64 dedicated real Metal selection
  dispatches, full retained Metal parity, direct layer color, preview/export render parity, and
  layer-LUT render parity. The embedded fallback shader also compiles on that physical device.
- Diagnostic renders compare actual operators from the baseline revision with the corrected
  compiled Look stage, followed by the same sRGB encoding. Maximum change on the synthetic
  near-gray patch falls from 0.000496805 (hue +50), 0.000367746 (saturation +50), and 0.100714
  (luminance +30) to exactly zero. Colored patches still respond. The repository landscape
  fixture was also rendered and visually reviewed. The original reported photograph was absent.
- Bounded single-thread CPU timings use identical 131,072-pixel inputs, one warmup and eight
  measured runs. The mixed neutral/color fixture improved from median 10.1964 ms to 7.275525 ms;
  the fully colored fixture increased from 10.8021 ms to 13.6363 ms. These measure only compiled
  Look execution, exclude first transfer-table initialization, and do not qualify app interaction
  latency or GPU performance. Color-heavy CPU fallback cost remains a follow-up.
- Two wider checks remain red on assertions outside this correction: the Metal source-contract
  test expects `Aces2OutputLutQuality::Interactive` in the preview controller, which is also absent
  in HEAD; the layer-adjustment controller test expects LUT table clamping while its unchanged
  normalization preserves signed/extended values. Their failures are not counted as passes.

Local evidence is in `build/color-selection-stability-2026-10-22/`: diagnostic PNGs, timing JSON,
CPU/Metal logs, build log, existing controller failure, proof-tool sources, and SHA-256 provenance
for the color sources and delivered binaries. The Release app is
`build/release-macos/apps/desktop/Looksy.app`. This is dirty-workspace engineering evidence; the
active board's corpus, editorial, migration, and release requirements retain their separate gates.

## Active color-control alignment plan — 2026-10-22

The product owner explicitly requested implementation of the eight shortcomings identified in the
follow-up review, using Luna agents at xhigh, rigorous proof, broad profile support, and no
quality reduction to obtain passing tests. This adds a targeted engineering workstream to the
acceptance board above; external corpus rights, independent human decisions, and physical
monitor/printer evidence remain required evidence rather than results software can invent.

### Execution and ownership

1. **Freeze the baseline and write failing behavior tests.** Save the current dirty engine/UI
   sources and SHA-256 manifest under `build/color-engine-alignment-2026-10-22/baseline/`.
   Reproduce the sign-discarding grading control and harmony discontinuity before changes.
   Preserve unrelated work. Root owns integration, CMake registration, reference CPU/Metal
   integration, final shared builds, performance acceptance, and artifact review.
2. **Luna xhigh: grading and truthful controls.** Correct signed grading behavior and establish
   one explicit display-hue-to-engine contract for grading, harmony swatches, and skin targeting.
   Test the actual control-to-pixel relationship. Simplify the ordinary mixer presentation while
   retaining compatibility for saved advanced model choices; no feature removal or profile loss.
3. **Luna xhigh: continuous harmony.** Replace hard nearest-anchor switching with a continuous,
   circular, deterministic operator. Preserve identity, neutral protection, wraparound, alpha,
   signed/extended working values, and meaningful palette guidance. Provide the exact formula
   and analytic boundary tests for CPU/Metal integration.
4. **Luna xhigh: independent profile and performance proof.** Add profile-matrix/ICC oracle
   coverage and an adversarial color fixture set; measure CPU cost on identical neutral, color,
   dark, HDR, masked, and layered inputs. Audit fixed protection thresholds and semantic limits.
   Optimize only measured paths; preserve precision and behavior. Expose concrete remaining gaps.
5. **Integrate protection and eliminate touched duplication.** Root aligns skin-hue sampling,
   protection behavior, reference/compiled execution, standalone/embedded Metal, payload/cache
   identities, and layer/mask paths. Reuse existing masks for subject-specific control and keep
   hue-only protection truthfully labeled. Do not silently activate an unavailable model or claim
   camera-noise adaptation from fixed thresholds.
6. **Run the complete acceptance matrix and review artifacts.** Complete focused native checks,
   independent transform references, real-device Metal checks, preview/export and layer parity,
   profile-tagged output/readback, deterministic repeats, visual inspection, and the Release app
   build. Preserve historical rendering compatibility through existing renderer/version contracts.
   Record source/binary/artifact hashes and leave unavailable evidence explicitly open.

### Required acceptance matrix

- Working spaces: linear sRGB, Display P3, Adobe RGB, ProPhoto RGB (D50), Rec.2020, ACEScg/AP1,
  and ACES2065-1/AP0 wherever admitted by the existing engine contract. Exercise Bradford and
  CAT02 adaptation where supported. Include payload-backed custom ICC profiles and invalid,
  missing, or changed payloads failing closed.
- Output: sRGB, Display P3, Adobe RGB, ProPhoto RGB, supported HDR targets and tagged 16-bit
  artifacts; compare profile-aware readback. Do not use raw RGB equality across unlike profiles.
- Samples: smooth hue/lightness/chroma ramps; every selector boundary and hue seam; near-neutral
  perturbations; deep shadows; saturated skin-like colors, foliage, sky, neon; signed/HDR values;
  alpha 0/partial/1; masks, layers, repeated adjustments, and canceled/stale work where applicable.
- Independent assertions: signed controls differ with the declared direction; wheel hue predicts
  the applied tint; infinitesimal input changes cannot cause finite harmony jumps; identity and
  neutral behavior hold; genuine faint color remains editable; real colors survive gamut changes.
  Cross-backend agreement is necessary and is not the sole correctness oracle.
- Keep existing tolerances, finite/alpha requirements, profile coverage, and performance gates.
  Do not remove cases, disable features, clamp away signed/HDR data, update goldens blindly,
  relabel failures as skips, or weaken checks to obtain a pass. An inaccurate old assertion may
  be corrected only with independent specification/reference evidence and an explicit record.
- Measure warm and cold costs separately. Use identical sources/settings for comparisons and
  inspect both mixed and fully colored cases. Report median and tail timing, memory, transfers,
  actual GPU dispatch, and fallback provenance. No blanket responsiveness claim from a microbench.
- Render and inspect real repository photographs plus synthetic stress charts. Inventory missing
  camera/reference/physical-output evidence; no generated or self-compared image is promoted to
  independent photographic truth. Final acceptance reports distinguish proved, failed, and open.

### Alignment implementation and proof — 2026-10-22

Three explicitly requested Luna xhigh agents implemented the grading/control, harmony, and
wide-profile/proof lanes. Root integrated the reference, compiled CPU, Metal, caches, builds,
and evidence. This is an engineering correction with bounded proof, not a claim of perfect
color science or completed external promotion approval.

Implemented behavior:

- Current ACES2/OCIO controls map display-wheel hues through canonical linear sRGB into OKLab.
  Positive grading adds the selected tint; negative grading removes chroma. Negative bands sum
  before attenuating the original chroma, so results do not depend on iteration order. Hue-only
  and zero-amount modern grading are exact full-Look identities, including signed/HDR samples.
- Modern harmony uses a shared circular sine field between adjacent palette anchors. It retains
  every anchor, repels bisector ties, wraps continuously, and has full-strength hue-map slope
  bounded by `[0.09, 1.91]`. Quarter-gap attraction retains approximately 89% of the old pull.
  The two historical renderer enums retain frozen raw-angle, abs-saturation, nearest-anchor
  creative behavior. Their UI swatches also retain the historical tint for either saturation
  sign; the modern UI shows negative saturation as neutral. Compatibility tests assert the
  original outputs independently, and native tests load actual recipes for both old models.
- Seven distinct named linear working profiles are admitted: sRGB, Display P3, Adobe RGB,
  ProPhoto RGB, Rec.2020, ACEScg/AP1, and ACES2065-1/AP0. The engine, saved recipes, and QImage
  boundary share canonical working-name normalization. Enum values were appended. AP0/AP1
  raster tags use generated ICC payloads; profile/recipe tests reject accidental ProPhoto fallback.
- Near-neutral selector protection uses relative channel spread. Removing an absolute brightness
  multiplier restores genuine faint color. Exact black/invalid maxima are guarded. This remains
  a color heuristic without a camera-noise estimate. Protection targets the displayed hue and
  is labeled as protecting skin-like colors, including similarly colored objects; subject masks
  remain the way to limit its area. Automatic hue suggestions use display-encoded hue in the
  current renderer. The ordinary mixer offers Standard/Gentle/Soft response names while keeping
  stored HSL/OKLab/HSV/LCH strings and their response scales.
- Grading and harmony scalar operators are shared with Metal. Build-generated shader embedding
  replaces the manually maintained shader copy and live source-tree fallback. Byte identity is
  checked after helper expansion, followed by actual system-device compilation. The expanded
  matrix also exposed and fixed omitted legacy HDR highlight compression in compiled execution,
  and non-finite GPU black pixels caused by evaluating a protected hue at zero chroma.
- Thumbnail pipeline identity is v8. Look payload identities include mapped hue values, reference
  selection transforms, and behavior flags; scalar/uniform member layout remains unchanged by
  this alignment phase.

Evidence lives under `build/color-engine-alignment-2026-10-22/`:

| Check | Final evidence |
| --- | --- |
| Focused native regression suite | `headless-acceptance.log`: 25/25 pass |
| Release Metal, custom ICC/OCIO, retained layer, preview/export parity | `release-gpu-acceptance.log`: 9/9 pass on the physical Apple M4 Pro |
| Creative matrix | 588 actual GPU dispatches: 7 profiles × 3 renderers × 2 headrooms × 14 cases, 384 samples each; CPU/reference tolerance `4e-5`, GPU/reference `8e-5` unchanged |
| Recipe, QImage, migration, layer controller and emitted exports | `desktop-acceptance.log`: 5/5 pass |
| No-ICC-backend behavior | `no-lcms-acceptance.log`: fixed matrix/edit checks and BackendUnavailable fail-closed assertions |
| Independent CMM references | `evidence/cmm-comparison.json`: direct Little CMS, Qt 6.10.2, macOS ColorSync; official ACES2/OCIO references also pass |
| Native control behavior and captures | `native-ui-acceptance.log`: 1/1 pass, Cocoa window and Metal validation, typed controls and both saved legacy renderers |
| Real repository photo and stress ramps | `evidence/visual/`: rendered and inspected; neutral mixer delta 0, seven-profile common linear-sRGB error at most `2.03e-6` |

The CMM relative-colorimetric p95 Delta E 2000 is `0.000270` against Little CMS,
`0.0000117` against Qt, and `0.002904` against ColorSync on the pinned 20-patch fixture.
The separate perceptual comparison remains informational (maximum `30.292`); these relative
conversion gates do not assert a perceptual match between different output-rendering operators.
The independent OKLab checks use the published [OKLab equations](https://bottosson.github.io/posts/oklab/)
and [W3C color conversion references](https://www.w3.org/TR/css-color-4/#color-conversion-code).

The final native check requires an exposed window and a painted, nonblank mountain photograph
before each capture. Wheel and numeric controls are verified across normal scrolling at 1280×850;
all controls remain covered. Native QML/controller behavior is exercised using the real Main.qml;
the separately completed Release app build contains the identical final StudioWorkspace.qml.
`final-provenance.json` pins source, executable, packaged QML and evidence hashes. This focused
proof does not qualify the entire desktop workflow or the unrelated failing broad UI shards.

Emitted export tests inspect PNG iCCP chunks and reassemble JPEG ICC APP2 segments for sRGB,
Display P3, Adobe RGB, ProPhoto RGB, and Rec.2020. Payload bytes must exactly match the selected
profile. PNG IHDR must declare 16-bit RGBA and decoded RGBA64 samples/alpha must exactly match
the converted source. Queued and atomic writer paths are exercised; ten retained image artifacts
and `evidence/export-profiles/manifest.txt` preserve the results.

The final CPU microbenchmark uses 9 fresh processes per case, 131,072 identical input pixels,
and 24 warm passes. `mixer-cpu-benchmark-final.json` reports cold, warm median and tail values
plus a defined FNV-1a hash over every output RGBA float bit. Warm medians are `12.5489 ->
11.2742 ms` for color, `12.7543 -> 12.0119 ms` for HDR, and `0.945750 -> 0.780917 ms` for
near-neutral inputs; their complete output bit hashes match. Dark input now edits 49,152 pixels
instead of zero, costing `11.0272 ms` instead of `0.853125 ms`; this is restored work, not a
like-for-like speedup. These are CPU Look timings, not desktop interaction or GPU latency claims.

Four stored preset hashes in `render_golden_test` remain red. A separately linked renderer built
from the frozen task-start CPU sources and the final current renderer produce identical RGBA
pixels for all four: zero changed pixels, maximum error and RMSE. Both disagree with the same
stored hashes. `evidence/golden-regression/` retains both binaries, link maps, source hashes,
images and exact comparisons. No golden was refreshed and no failure was counted as a pass.

Two inaccurate old assertions were corrected with additional proof: signed/HDR LUT table values
must survive persistence and native layer rendering, rather than be clamped to `[0,1]`; preset
application status must be checked before a later save operation replaces it. Kernel/source
contracts retain their assertions against the canonical shader and the actual output-quality
owner. Numeric tolerances, profile coverage, finite/alpha checks and existing goldens were not
relaxed, and no feature was disabled to obtain passing checks.

External licensed camera/RAW references, camera-noise characterization, independently approved
preset migration, calibrated monitor/printer measurements, broad native UI workflow qualification,
and complete end-to-end interaction performance remain separate open gates. The original reported
photo was not supplied. The diagnostic sheets clamp only their displayed sRGB copies; numeric
profile comparisons retain unclipped values. This work does not assert physical output fidelity
or a completely green repository beyond the specifically recorded checks.

### Real-photo Color Mix noise follow-up — 2026-10-22

The user's IMG_4447.CR2 screenshot exposed a failure missing from the earlier synthetic tests.
The actual managed RAW and saved tone settings reproduce the speckling on the full desktop CPU
path at 2048×1366. Isolating Reds / Standard / Hue +63 / Saturation 0 / Luminance +30 identifies
the absolute Color Mix lightness lift as the dominant trigger. Matching CPU/Metal implementations
did not prove acceptable noise behavior. A one-code near-neutral input pair amplifies independent
OKLab distance by 13.259× in the frozen previous engine.

The modern mixer now scales only its luminance offset by `min(1, C0 / 0.12)`, using pre-channel
chroma captured once. This bounds weak-chroma lightness changes, preserves the original response
at C≥0.12 and retains a nonzero proportional response for genuine faint color. It applies no
spatial blur and is not a noise estimator. Low-chroma colors deliberately receive less luminance
lift. This follow-up retains the task-start legacy mixer behavior; thumbnail identity is v9.

`build/color-mix-real-photo-2026-10-22/` retains the actual before/after renders, recipes,
reproducible probe, exact-pixel contact sheet, independent metric source/CSVs, frozen engine,
red/green tests and source/build hashes. Edit-lightness Laplacian RMS falls 77.1% on the face,
82.5% on the hillside and 57.4% on the shirt. The shirt still changes hue/lightness. No-mix and
hue-only PNGs are byte-identical before/after, and final rendered PNGs match the reviewed candidate.

The permanent regression adds two-code and seeded-noise gain gates across seven profiles, four
mixer responses and eight channel centers; saturated and faint-color response, overlap, signed
luminance, HDR and alpha are checked independently. Existing CPU/reference `4e-5` and GPU/reference
`8e-5` tolerances are retained. Metal uses 28 real dispatches over the noise fixtures in addition
to the existing creative matrix. The focused Release checks pass; the wider headless run retains
an independent Detail signed-range failure during concurrent detail/effect work in this dirty
tree. Direct Detail execution reproduces it with Look active mask zero. Stored goldens remain
unmodified; all five failing cases also fail with the mixer correction restored to its previous
CPU code. The portrait's low-chroma mixer luminance output intentionally changes with this fix.

The full-RAW artifact is CPU evidence. Strict GPU-only RAW admission rejects the camera-linear
LUT fidelity; separate Look-stage dispatches do not establish full-RAW GPU parity. See the new
evidence README for exact results, open failures and the final app-build record.
