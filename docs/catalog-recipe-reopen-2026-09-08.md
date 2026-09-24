# Canonical recipe save/reopen fidelity — 2026-09-08

## Defects reproduced

The earlier export handoff fix retained the complete recipe in memory. The
authoritative catalog projection is a different boundary: canonical provenance
v1 stored preset applications but omitted recipe-level history. A regression
in `edit_recipe_v13_persistence_test` reproduced lost branch/snapshots before
the fix (`/private/tmp/canonical-history-before.log`).

The first real imported DNG autosave/fresh-process reopen check then found
another loss: Display P3 export intent reopened as sRGB. Complete JSON comparison
isolated the difference to `exportOutputProfile` and `exportTransform.colorSpace`.
Evidence is retained under `/private/tmp/catalog-reopen-proof-Qvuuf0`; its database
has been stopped. History survived this run, but the full reopen test failed.

## Implementation

- Canonical provenance v2 retains presets plus recipe history. Older v1 and
  empty legacy provenance remain readable without inventing missing history.
  History does not change active rendering payload/cache identity.
- PostgreSQL migration 0014 admits v1/v2 and rejects malformed shapes, including
  missing keys that could otherwise pass a nullable SQL CHECK. It does not
  rewrite existing provenance or change catalog authority. The helper validator
  and desktop reader apply matching structural checks. New writes require an
  updated app/helper/database; old app versions cannot read the new format.
- The canonical engine output recipe now has explicit delivery color-space
  intent, separate from the current viewport rendering target. The additive
  v13.3 serialization is used for non-sRGB delivery spaces and participates in
  cache identity. Default sRGB retains established v13.2 serialized/cache bytes.
  Desktop adaptation and canonical projection preserve all six currently
  supported delivery spaces. This is persistence support, not a new gamut or
  calibration accuracy claim.

## Evidence and remaining gates

- Final native tests: `edit_recipe_v13_test`, `edit_recipe_v13_persistence_test`
  and `export_recipe_handoff_test` all pass (3/3, 17.35 seconds), including all
  six delivery-space round-trips and output-only cache invalidation.
  Log: `/private/tmp/canonical-history-space-regressions.log`.
- Rust workspace tests passed: 41 tests across crates, no failures; numerous
  binary/doc targets contain zero tests. Log:
  `/private/tmp/canonical-history-rust-workspace-final.log`. All catalog helper
  binaries and the debug desktop app also rebuilt successfully.
- Real PostgreSQL migration 14: v1/v2 accepted, seven malformed shapes rejected,
  all test updates rolled back. SQL fixture:
  `tests/catalog_recipe_provenance_constraints.sql`; log:
  `/private/tmp/catalog-reopen-proof-Qvuuf0/schema-check.log`.
- `catalog_recipe_reopen_test` is an explicit integration harness (ordinary
  CTest invocation skips with code 77). It requires a marked isolated root and
  actual imported asset ID. Separate write/reopen processes use production
  autosave and catalog projection, compare complete asset-bound recipes and
  full-resolution exports, including two layers, a mask, Display P3, presets,
  branch and snapshot. No in-memory catalog replacement is used.
- Corrected real RAW/JPEG proof: `/private/tmp/catalog-reopen-proof-MajJCZ`.
  Both referenced imports use stable catalog asset IDs. `raw-write.log`,
  `raw-reopen.log`, `jpeg-write.log` and `jpeg-reopen.log` all exit successfully.
  Both fresh-process reopens retain exact full recipe JSON and exact 4208×3120
  PNG pixels and profiles. `saved.json`, read independently with the catalog
  helper, confirms both authoritative records contain v13.3 rendering payloads,
  provenance v2, review branch and one snapshot. The database was stopped after
  testing; records and media artifacts are retained. The user catalog was not
  changed or migrated.
- Diagnostic downsampled JPEG copies of both reopened outputs were visually
  inspected; the small source-layer patch and full photographic canvas are
  present. These diagnostic copies are not the pixel oracle: comparisons use
  the original full-resolution PNGs. Direct viewing of the large 16-bit PNG
  exceeded the image transport size limit, without affecting the tests.
- The broader architecture check still fails on six separate preview/thumbnail
  pipeline-boundary requirements; `/private/tmp/canonical-history-architecture.log`.
  No broad architecture completion is claimed.

The proof uses real media references and normal controller/catalog APIs, but
does not operate the full desktop window, test crash-during-save, or establish
realtime rendering. History/export intent already omitted by older canonical
writes is not automatically recovered from potentially stale compatibility
records. The broader objective remains in `active-app-completion-goal.md`.
