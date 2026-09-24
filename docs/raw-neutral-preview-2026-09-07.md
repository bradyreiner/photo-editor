# Neutral RAW preview and comparison correction

Status: neutral and reference-render defects fixed and exercised through the
actual preview request renderer on one real DNG and its JPG reference, in legacy
and ACES 2 modes. Full-resolution checks are described separately below. This is
not all-camera/color-space, perceptual-quality or realtime certification.

## Defects and changes

The earlier transparent-mask proof exercised the agent/export renderer. A new
`legacy_preview_locality_driver` instead invokes the same preview request entry
point used by `EditorPreviewController`, with isolated app/cache directories and
no model or catalog. It found two distinct preview admission issues:

1. `preview_gpu_fallback_policy.h` still admitted camera RGB into the sampled
   compatibility LUT, unlike export. It now rejects that route. Already
   characterized canonical working surfaces retain Metal eligibility, and the
   ordinary raster admission remains unchanged. The regression in
   `canonical_native_working_fusion_test` fails before and passes after this
   policy change. The reduced retained preview calls Metal directly, so the
   engine's separate high-level incompatibility guard is not sufficient.
2. A neutral legacy preview skipped adjustments entirely and published decoded
   camera RGB without camera characterization. The DNG preview's backend was
   `none`; adding a transparent mask activated the CPU pipeline and changed all
   1,897,600 output pixels, maximum RGBA8 channel difference **174**. JPG did not
   exhibit that failure. Neutral controls now still run the mandatory input
   transform when decoder metadata says camera characterization is required.

The preview keeps decode metadata, including on decoded-cache hits. Ordinary
same-image Before comparisons use the existing neutral reference recipe's
decode/profile contract instead of camera RGB or current creative edits; named
neutral snapshots also receive the mandatory prefix. Rendered-image and disk-JPEG
cache identities were versioned so old uncharacterized previews are not reused.
Existing cache files and originals were not deleted.

## Real image evidence

Source DNG:
`/private/tmp/framehouse-six-raw-source-v3/hdrplus-nexus6-skin-0218/hdrplus-nexus6-skin-0218.dng`.
Matching JPG: `hdrplus-nexus6-skin-0218-reference.jpg` in the same directory.

- Before the neutral-prefix fix:
  `/private/tmp/framehouse-raw-preview-locality-nbjQXF/receipt.json` fails with
  1,897,600 changed pixels / maximum 174; the paired JPG proof in
  `/private/tmp/framehouse-jpg-preview-locality-L2xSWa` passes.
- Fixed DNG: `/private/tmp/framehouse-raw-neutral-fixed-JDnEyD`.
- Fixed JPG: `/private/tmp/framehouse-jpg-neutral-fixed-cWniHq`.
- Both fixed proofs are 1600x1186 and exit zero. Zero-alpha mask, neutral Before
  reference and neutral snapshot reference each match the baseline with zero
  RGBA8 differences. The comparison's edited image must actually differ after
  +1 stop exposure; a no-op comparison cannot pass. Repeated passes prove the
  decoded metadata cache-hit route as well as initial decode.
- The corrected DNG baseline was visually inspected. This does not establish
  independent colorimetric truth or adequate noise/reconstruction quality.
- DNG worker timings were 393 ms neutral, 342 ms masked, and 607/605 ms for the
  two-image comparisons. All used the CPU path, with zero GPU dispatches. These
  are correctness results, not a speedup or realtime pass.

Reproduce with a new empty output directory (input is never modified):

```sh
cmake --build build/release-macos --target legacy_preview_locality_driver -j4
QT_QPA_PLATFORM=offscreen build/release-macos/tests/legacy_preview_locality_driver SOURCE NEW_OUTPUT_DIRECTORY
```

The driver now also registers two portable CTests, `preview_reference_workflow_test`
and `preview_reference_aces2_workflow_test`, using a generated tagged PNG. Those
tests are raster regressions, not RAW evidence. Real RAW proof still requires an
explicitly supplied file; a missing explicit fixture fails. The earlier
agent/export checks remain separate from this preview entry-point proof.

Final verification: the rebuilt app and real-image driver link successfully;
the selected compiler logs contain no warnings. Eight focused tests pass in
4.14 seconds: color pipeline, canonical native working fusion, preview history,
preview feedback, app shell, Gaussian controller, Gaussian renderer and
interaction-proof eligibility. Log:
`/private/tmp/neutral-raw-final-focused-tests.log`. This is not a full-suite pass;
the broader pre-existing source-contract failure remains separately recorded in
`gaussian-native-viewport-2026-09-07.md`.

## Independent references and selected photographic output

A wider seven-pass audit found two further defects:

- External RAW references inherited the current image's decode settings and
  bypassed camera characterization. Snapshots with a different RAW input
  contract reused current-photo pixels and metadata. Before the fix, all
  1,897,600 pixels changed in external, differing-decode snapshot and warm-cache
  snapshot references, with maximum RGBA8 difference 102. Evidence:
  `/private/tmp/framehouse-raw-reference-before-v2zVzi/receipt.json`.
- Neutral ACES 2 JPGs bypassed the selected photographic output transform until
  an edit or transparent mask activated it. The zero-alpha mask changed
  1,892,955 pixels, maximum RGBA8 difference 77. Evidence:
  `/private/tmp/framehouse-jpg-reference-aces2-9e4gzU/receipt.json`.

Comparison references now use bounded, non-comparing child preview requests
with their own decode metadata, complete recipe, rebuilt plans and cache
identities. External images inherit only the current view/output contract, not
its camera/input profile or creative edits. Ordinary Before preserves the
neutral same-photo decode/crop contract. Named snapshots preserve the complete
historical recipe. The child performs its display transform exactly once;
missing references fail at the reference boundary. Reference disk cache keys
include the complete reference recipe and source revision, and cache reuse
checks source freshness. Unrelated base-linear cache semantics are unchanged.

Neutral ACES 2 inputs always apply the selected photographic output transform.
This makes the baseline consistent with subsequent edits; it does not promise
pixel identity with the original display-referred JPG. Legacy rendering remains
legacy. Rendered memory/disk cache versions invalidate the old bypassed outputs
without deleting existing files.

Final real-file 1600x1186 proofs:

| Input/model | Proof directory under `/private/tmp` | Result |
| --- | --- | --- |
| DNG / legacy | `framehouse-raw-legacy-final-loaAJ8` | Pass |
| JPG / legacy | `framehouse-jpg-legacy-final-UhigAd` | Pass |
| DNG / ACES 2 | `framehouse-raw-aces2-final-GhX4LQ` | Pass |
| JPG / ACES 2 | `framehouse-jpg-aces2-final-AZVnYE` | Pass |

Each receipt records zero RGBA8 differences against its own baseline for the
transparent mask, ordinary Before, neutral snapshot, separate byte-copy external
source, differing-decode snapshot and warm snapshot. The edited comparison image
must differ after exposure adjustment. Additional gates reject a missing
reference and verify distinct disk images/paths for +0.25 and +0.75 EV snapshots,
then exact reuse when returning to +0.25 EV. These are within-model invariants,
not RAW-versus-JPG equivalence or independent colorimetric accuracy.

Full-resolution ACES 2 proofs:
`/private/tmp/framehouse-raw-reference-full-uaHmLJ/receipt.json` and
`/private/tmp/framehouse-jpg-reference-full-1kjCZi/receipt.json`, both 4208x3120.
All the above gates pass with zero changed pixels. RAW worker timings range from
664 to 2243 ms, with CPU backend and zero GPU dispatches; JPG timings range from
560 to 934 ms. The corrected ACES 2 JPG preview and full-resolution RAW output
were visually inspected. RAW noise remains plainly visible; photographic quality
is a separate gate. These CPU-image tests are not foreground interaction timing.

The driver accepts optional model and resolution arguments:

```sh
QT_QPA_PLATFORM=offscreen build/release-macos/tests/legacy_preview_locality_driver SOURCE NEW_OUTPUT_DIRECTORY aces2 0
```

The last argument is maximum edge, or zero for full resolution. Default is
legacy/1600. Output files use exclusive creation; app/cache data is isolated.

The final app/driver rebuild passes. Six focused color/preview tests pass in
4.19 seconds (`/private/tmp/photographic-baseline-final-tests.log`), and four
app-shell/Gaussian regressions pass in 1.13 seconds
(`/private/tmp/reference-gaussian-regression-tests.log`). This does not supersede
the separately recorded broad source-contract failure or establish foreground
interactive performance.

## Remaining work

Follow-up: the driver now also renders an actual half-image temperature edit,
checks the untouched region, and records existing stage timings. Final DNG/JPG
legacy/ACES 2 runs pass that expanded check; see
`raw-local-edit-performance-2026-09-07.md`. The first performance candidate was
reverted after missing its acceptance gate. The second compiles adjustment setup
once per mask set and improves the real RAW local-edit median by 35.53%, with
bit-exact output and no memory regression. Full-resolution RAW/JPG local-edit
guards also pass; realtime remains open.

- Accelerated parity-verified RAW camera prefix and local-mask execution.
- Broader full-resolution, preview/export, working/output-space and multi-camera
  corpus checks; meaningful local edits beyond a transparent mask.
- Different-camera external references, custom DCP/ICC/proof combinations,
  crop/rotation/group comparisons and source replacement cache invalidation need
  broader independent coverage; the current external image is a byte-copy control.
- Unlocked foreground interaction/visual QA, real stylus input and broad release
  validation. Gaussian, embedded-model and professional-tool requirements remain
  in `active-app-completion-goal.md`.
