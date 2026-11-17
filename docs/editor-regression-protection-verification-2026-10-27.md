# Regression enforcement verification — October 27

Implemented the contract, strict runner, native collector/validator, workflow
provenance checker, CI merge status, release intake, ruleset template, and PR
review requirements described in `editor-regression-protection.md`. Existing
broad test suites remain in place. The stable merge status requires all ten CI
jobs, including source closure, Rust/catalog, and sanitizers.

Local results:

- 27 required portable tests passed, including the six added recovery gates.
- Three required physical Metal/monitor checks passed with Qt offscreen.
- All 15 Python enforcement tests passed, both directly and through CTest.
  Negative cases cover missing/duplicate/skipped/failed tests, invalid numbers,
  absolute/relative/tail slowdowns, budget drift, busy hosts, source changes,
  stale/fork/wrong-workflow provenance, missing commands/executable identities,
  changed files, incomplete native scenarios, fast frames with stale glyphs,
  RAW decode omission, same-process reopen, and changed export bytes.
- Actionlint passed the CI, release, and native qualification workflows.
- `git diff --check` passed. Focused C++ targets built successfully.

The earlier quiet offscreen performance run passed all five frozen budgets:
ICC median 8.645 ms, brush commit 38.5395 ms, Selection raster 0.194 ms, slider
24.7425 ms, and styled render 16 ms. Brush peak RSS was 220,954,624 bytes.

The next confirmation was deliberately retained as **failed**: ICC median
11.379 ms exceeded 9.45 ms; other references also slowed during the latter half.
The unchanged benchmark executables produced both runs. A host sample after
that run showed 1-minute load 13.22 on 14 logical CPUs, with no reported thermal
warning. No timing/memory limit was relaxed and no baseline was overwritten.

The final runner now samples host load before and throughout measurement. Its
live validation rejected load 9.89 above the limit of 7 and produced
`status: unqualified`, `release_eligible: false`, and a nonzero exit. The earlier
accepted/rejected reports predate that added qualification field; they are
component evidence, not final native release approval. Their original contract
hashes are preserved. Unit tests validate a complete synthetic proof bundle and
its rejection cases; synthetic data is not native runtime evidence.

The final native qualification workflow and the real-shoot corpus path were
prepared, not visibly executed. The Mac remains in the user's requested
no-visible-check state. Windows and GitHub-hosted workflow execution also remain
unverified locally. Required checks, the Mac runner, the protected review
environment, source commits/push, and the proof run ID still need remote
activation. Public release qualification fails closed until the required native
proof exists for the exact commit. Candidate-only builds remain unqualified.

The new Windows OpenColorIO 2.5.1 override was checked against the repository's
existing pinned vcpkg registry; the registry baseline and LibRaw pin were retained.
macOS CI installs the required color dependencies and retains strict version
checks. The stale app-smoke executable path now points to Looksy.

Local ignored evidence: `docs/evidence/editor-regression-protection-2026-10-27/`.
The receipt contains source hashes, test inventories, workflow checks, and both
successful and rejected measurement reports. Runtime/catalog data is excluded.
No remote settings, tags, releases, or live editor processes were changed.
