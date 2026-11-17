# Editor regression protection

The executable contract is `tests/fixtures/editor-regression/contract.json`.
`docs/capability-verification-plan.md` remains the capability acceptance owner.
A green portable job establishes correctness coverage; only the separate native
qualification can establish release eligibility for the guarded editor paths.

The portable inventory includes canonical RAW exposure preview/export and
save/reopen checks, signed/HDR Detail preservation, shared compiler ownership,
and bounded local highlight reconstruction. Native qualification additionally
runs the combined-exposure preview proof on each of the three checksum-pinned
Canon RAWs. Every RAW must prove both decoder contracts, draft and settled
output, additive and cancelling EV, reopen, total-exposure reset, and superseded-request
cancellation; missing cases or lifecycle markers reject the proof. This real
photo regression set does not replace the independent photographic acceptance
corpus required by the color-engine completion strategy.

Canonical v13 owns one scene-exposure value. Saving mixed RAW/Light EV stores
their sum; reopening normally places that total in Light, with excess above
Light's +/-5 EV range retained in RAW's +/-4 EV range. Pixel output and total
EV are preserved. The original split between groups is not persisted, so the
reset proof resets both exposure fields; it does not assert that resetting only
the RAW group has the same effect before and after canonical normalization.

Brush fidelity is also part of the mandatory inventory: source-coordinate
pointer/render checks, premultiplied paint area sampling, persistent paint crop
basis, versioned mask replay, contextual retouch, and source/mask/catalog
thumbnail comparisons. The regression fixtures include non-square crops,
rotation/scale/skew/perspective, display scale, legacy and new strokes,
partial erasing, signed HDR paint, save/reopen, and clone sources outside the
visible crop. Same-size draft/commit checks alone do not establish reduced
preview versus full-resolution fidelity.

Settled source-dependent edits composite on the native source grid before final
display resizing, including both Compare images and edited catalog thumbnails.
Literal mask thumbnails may skip unused RGB processing and provably empty
support, but must retain exact native alpha. Native ICC qualification also
covers CPU-composited paint entering the Metal device-output stage without
changing signed/HDR color, alpha, or source geometry.

## Enforced checks

The **desktop to PostgreSQL recipe compatibility** check feeds the production
desktop serializer's history, initialized-selection, and component payloads
through the real Rust save helper into a disposable PostgreSQL cluster. It
compares stored canonical bytes, cache identity, legacy data, provenance and
high precision numbers, then reopens each recipe through the desktop catalog
projection. Malformed payloads must fail in both Rust and the SQL constraint
without changing the last saved edit. macOS Release CI and release packaging
run this check with `--require-postgres`; a missing runtime cannot pass it.
For local execution:

```sh
python3 scripts/test-recipe-catalog-contract.py \
  --desktop-test build/release-macos/tests/edit_recipe_v13_persistence_test \
  --catalog-bin-dir rust/target/debug \
  --postgres-bin /opt/homebrew/opt/postgresql@14/bin --require-postgres
```

Ordinary CTest registers this as `recipe_catalog_contract_test` and skips only
when PostgreSQL is unavailable. Set `PHOTO_EDITOR_TEST_POSTGRES_BIN_DIR` when
its tools are outside `PATH`. The mandatory CI/release step does not skip.

The stable **Editor regression gate** status requires all existing CI jobs:
source closure, Rust/catalog, Linux/Windows headless checks, sanitizers, desktop
checks on both platforms, macOS Release build, and repository hygiene. Failed,
canceled, or skipped prerequisites cannot produce a passing merge status.

- **Portable desktop:** 55 core checks plus six close/save/export/
  Timeline recovery checks. macOS Debug, Windows Debug, macOS Release CI, and
  release packages run this explicit 61-test inventory after their broad suite.
  Timeline color parity checks instantiate the actual Edit inspector and render every
  registered photo effect, including displacement maps, with clip recipe and Undo readback.
  Missing, disabled, duplicated, failed, and skipped tests fail the gate. The
  remaining three core checks require physical Metal/monitor ICC and run in
  hardware/native qualification. Ordinary CTest skip semantics cannot satisfy it.
- **Release performance:** five alternating-order replicates for exact CPU ICC,
  large-brush commit, Selection raster, slider rendering, and styled-text
  rendering. Every case must retain at least 15% improvement over its same-run
  serial/cache-miss reference and meet independent absolute median/tail budgets.
  Rounded October 27 baselines allow at most 5% regression; changing a limit
  without its reviewed baseline is rejected. The CPU tail is explicitly the p95
  of replicate medians, not input-to-photon latency. Peak brush RSS must stay
  below 231,000,000 bytes (the rounded 220 MB baseline plus 5%). These budgets apply only to
  macOS arm64 **14-core Apple M4 Pro** Release builds. Host-load samples are
  required before and throughout measurement; 1-minute load above 7 marks the
  host unqualified and blocks release without changing the software baseline. Another machine needs a reviewed
  profile and its own measurements; there is no automatic baseline reset.
- **Native qualification:** all 47 core/recovery checks, all performance gates,
  then visible held text movement/corner/edge resize, Exposure, Selection,
  Paint/Eraser, Crop captures, and the existing actual-shell workflow gate.
  The separate real-shoot workflow must also import three pinned Canon CR2s,
  cull, batch/refine with undo/redo, fail/retry export, relink, and reopen in a
  genuinely different process with matching saved edits and exported bytes.
  Native text timing uses glyph/texture revisions, not unrelated frame swaps.
  Text also requires changed held pixels, v10 styling, exact undo/redo, and
  foreground samples. Slider and Selection require continuous qualified native
  foreground samples; Paint/Eraser require visible pixel checks and qualified
  before/after samples. These are the existing probes' actual proof scopes.
  Human review still covers crop feel, layout polish, and photographic output.
- **Release intake:** public tag/publish runs require the qualified artifact from
  a successful manual native workflow on `main` for the exact release commit.
  Wrong repository/workflow/event/branch/SHA, dirty sources, missing cases,
  weakened contract hashes, skipped commands, changed files, or missing memory
  and timing evidence reject the release. Candidate-only builds can remain
  explicitly unqualified and cannot gain public release approval that way.

Native tests use disposable app-data/cache/backup roots. They do not close the
user's editor or use its live catalog. Visible runs are opt-in and must use an
idle dedicated Mac with a logged-in, unlocked graphical session and appropriate
macOS automation/screen permissions. Do not run them on a busy personal session.

## Local commands

From the repository root, configure/build the relevant desktop preset first.

```sh
python3 -B scripts/perf/test-editor-regression-gate.py
python3 -B scripts/perf/editor-regression-gate.py \
  --build-dir build/release-macos --tier portable \
  --output /tmp/looksy-portable-proof-UNIQUE
python3 -B scripts/perf/editor-regression-gate.py \
  --build-dir build/release-macos --tier hardware \
  --output /tmp/looksy-hardware-proof-UNIQUE
python3 -B scripts/perf/editor-regression-gate.py \
  --build-dir build/release-macos --tier performance \
  --output /tmp/looksy-performance-proof-UNIQUE
```

Use a fresh output directory for each run. Hardware/performance commands remain
offscreen; they fail when the required device/profile is unavailable. Native
qualification additionally requires a clean committed checkout, the contract's
build targets, and `--tier native --allow-visible`. That option opens disposable
native test windows and must only be used when visible testing is authorized.

## Remote activation

These are repository/account settings, not settings a YAML file can activate.
They have **not** been changed by the local implementation.

1. Commit the fixes and their tests/fixtures along with the enforcement files in
   reviewed, scoped commits. Preserve unrelated working-tree work. No ignored
   build/evidence directories belong in source control. A fresh CI checkout
   must contain the new tests, fixtures, scripts, and contract.
2. Push through a PR and verify macOS/Windows Debug and macOS Release jobs pass.
   Make **Editor regression gate** required on `main`. The versioned
   `.github/editor-regression-ruleset.json` additionally requires one review,
   invalidates stale approvals, and rejects force-push/deletion. After reviewing
   existing rules, an administrator can apply it with:

   ```sh
   gh api --method POST repos/OWNER/REPO/rulesets \
     --input .github/editor-regression-ruleset.json
   ```

   Do not blindly create duplicate rulesets or enable this before the named
   status has run. Baseline, fixture, skip-policy, and CI changes must receive
   explicit review; use the PR template's regression section.
3. Provision an idle Mac runner labelled `self-hosted`, `macOS`, `ARM64`, and
   `looksy-m4-pro`, with the validated Qt/CMake/Ninja/Rust/color dependencies,
   FFmpeg, and the managed PostgreSQL runtime needed by the real-shoot workflow.
   Stage the existing three CR2 real-shoot fixtures outside Git and set the
   `LOOKSY_REAL_SHOOT_SOURCE_DIR` repository variable to their runner directory.
   Their filenames/hashes are pinned in the contract; missing RAWs cannot skip.
   The existing strict CMake dependency-version checks stay enabled. The Windows
   vcpkg manifest pins OpenColorIO 2.5.1 using the existing registry baseline.
4. Configure the **looksy-native-review** GitHub environment with required human
   reviewers and prevent self-review. Review captures/logs before approving its
   `qualification` job. Naming an environment in YAML alone does not configure
   its required reviewers.
5. On `main`, manually run **Native editor qualification** with visible testing
   explicitly enabled. It never runs PR code on the persistent hardware runner.
   After successful capture and review, set `LOOKSY_NATIVE_PROOF_RUN_ID` to that
   workflow's run ID (or pass `native_proof_run_id` to manual release). Tag that
   exact qualified commit. Later commits need new qualification; expired or
   missing artifacts block release. Proof artifacts are retained for 30 days.

Cross-workflow artifact retrieval follows the official
[GitHub artifact action interface](https://github.com/actions/download-artifact),
and the optional ruleset payload follows the
[GitHub ruleset API](https://docs.github.com/en/rest/repos/rules#create-a-repository-ruleset).
The release job has read-only Actions access and verifies run provenance before
retrieving evidence; it never executes downloaded scripts.

## Changing a baseline or adding a capability

Add a reproducible regression for every repaired bug, then add it to the owning
suite/contract where appropriate. Add tests alongside new features before
retiring old paths. Preserve old recipe/document fixtures and assert migration,
export, reopen, undo, failure, and recovery behavior. Use reviewed reference
images and explicit per-platform tolerances when exact raster equality is not
appropriate; do not regenerate expected images simply to make a test pass.

Record before/after workloads, hardware/build/dependency identities, sample
counts, pixels/alpha, memory, and raw logs for performance changes. Threshold
changes require measured evidence and review, separately from the code that
needs them relaxed. Never relabel offscreen proof as native, treat a missing
GPU as a release pass, or drop an inconvenient case from the required inventory.
This contract guards named scenarios; it does not certify every app feature or
replace the broader CI suite and native review.
