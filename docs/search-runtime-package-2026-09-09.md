# Self-contained search runtime: development package proof

Follow-up: `search-runtime-signing-2026-09-09.md` implements preparation before
app compilation and preservation during signing, with real ad-hoc app/inference
proof. The v2 results below are the earlier checkpoint; Developer ID release
signing, minimum-OS compatibility and managed model setup remain open.

## Implemented

`scripts/build-search-runtime.py` builds an offline PyInstaller one-directory
worker from the pinned inference and build environment. Python, inference
libraries and redistribution notices travel with the worker; model weights
remain separate. Neither build nor startup implicitly downloads weights.

The receipt inventories every regular file and relative library link. CMake
checks source/dependency identity, anchors the receipt SHA-256 in the app, copies
the runtime to `Contents/Resources/search-runtime` while preserving links, then
verifies the copy. The native verifier checks the trusted receipt, file sizes,
checksums, containment, missing/unexpected files and executable availability.
This establishes integrity against a selected build artifact, not independently
reproducible provenance or immunity to a privileged concurrent filesystem writer.

`LocalImageTextEncoder::startExecutable` retains the owned process protocol,
bounded request lifecycle, cancellation and shutdown without an external Python
interpreter. Production startup verifies the bundled runtime off the UI thread
and exposes setup errors. Explicit development executable/Python overrides remain
available. The default separate model location is
`<AppData>/search-models/siglip2-base-patch16-224`; user-friendly model installation
and repair are not implemented by this package change.

## Verified current artifact

Proof root: `/private/tmp/framehouse-siglip2-O2ha9I`.

- Version 2 bundle: `package-build-v2/dist/framehouse-search-worker`.
- Trusted receipt SHA-256:
  `409c489d8865d60f13bdb87600a02532b37b0ce560b96065d02107174a2eb7ba`.
- App resource copy: 14,774 regular-file/link entries, 510,864,553 regular-file
  bytes. Native verification passes in `package-v2-app-verification.json`.
- Current build is up to date for the desktop app, native verifier, encoder,
  coordinator, catalog tests and actual-app UI driver. See
  `/private/tmp/search-bundle-v2-app-build.log`.
- Four focused bundle/encoder/coordinator tests pass in 3.08 seconds. This does
  not supersede the separate unresolved full catalog import test timeout.
- The expanded twelve-test search/model/index/catalog/edit-contract run passes
  in 32.56 seconds in `package-v2-regressions-native.log`. An initial sandboxed
  run passed ten but could not exercise the localhost-backed search fixtures;
  the complete rerun had scoped localhost access. No test was weakened.
- Read-only audit of 120 native files finds zero non-system absolute native
  dependencies or search paths. Highest recorded minimum macOS is **26.0**.
  Evidence: `package-v2-app-linkage.json`. Relative linkage and execution on this
  machine are not fresh-machine or older-OS compatibility proof.

## Real model and native UI proof

Both runs use the approved local model and five inputs/four scenes, including an
existing RAW-derived JPEG preview, with `PATH=/usr/bin:/bin`. They launch the
worker from the app resource copy, not the original build output or virtualenv.
No original RAW is freshly decoded by these search proofs.

`package-v2-native-mps.json` records actual GPU inference: all ten queries rank
an expected image first, the restricted view does not broaden, all five vectors
are reused after worker restart, and both owned processes stop. Initial load,
indexing and query take 13,924 ms with 1,392 event-loop heartbeats; later queries
take 52–65 ms. These are tiny-scope coordinator timings, not UI frame latency.

`packaged-ui-v2/receipt.json` and `packaged-ui-v2.log` record the actual Main.qml
Library interaction through the native test driver: mode selection, enabled Find
photos action, expected dog first, result projection/clear, text-filter
invalidation, recovery, repeat vector reuse and owned-worker shutdown. The first
search takes 9,987 ms; repeats take 235 and 204 ms. Wide and compact result captures
were inspected; no ReferenceError, TypeError, assignment or binding-loop messages
occur in this proof log. The inspector's separate unfinished full-resolution
loading state is visible and is not certified by this search test.

The driver explicitly verifies and configures the app resource runtime. This is
NOT proof of production `main.cpp` auto-discovery, model-manager setup, signed
app launch, or relocation of the entire app to a clean machine. Initial timings
are not controlled cold-cache benchmarks or an accepted performance improvement.

All owned build, inference, UI and linkage-audit processes for these runs exited
successfully. No additional model weights were downloaded or copied, no global
Python installation was modified, and no user catalog was used.

## Release and full-goal gates still open

1. Replace the local macOS-26 Python base with a runtime matching the supported
   OS/hardware matrix; validate minimum OS and actual clean-machine launch.
2. Complete Developer ID/hardened signing and notarization proof. Signing order
   and preservation now pass local ad-hoc proof in the follow-up, without disabling
   hash checks. Ad-hoc proof is not a release-ready artifact.
3. Complete production auto-start/model installation, update, repair, offline and
   missing/corrupt-model UI flows; validate cancellation and resource usage.
4. Prove larger labeled libraries, background indexing and deletion pruning,
   photographic retrieval/detail quality and actual reversible agent edits.

The RAW/color, loading/export, true temporal 4D reconstruction and complete
professional editing requirements in `active-app-completion-goal.md` remain open.
