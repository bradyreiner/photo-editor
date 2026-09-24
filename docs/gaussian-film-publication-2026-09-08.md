# Complete-file Gaussian film publication

The previous exporter wrote the final receipt first and copied the movie into
its final name. A movie publication failure left an orphan receipt, and copying
could expose an incomplete final movie to other readers.

Both closed output files are now staged in the worker-owned temporary directory
beside the destination. macOS exclusive renames publish the receipt and then the
complete movie without overwriting existing files, including symlinks and
destinations created after preflight. A movie publication failure rolls back the
receipt after checking its device/inode identity. Rollback failure is explicit
in the error and receiptRetained result; no success is reported.

Cancellation is checked again after staging and before the short publication
sequence. There is no copy fallback on unsupported filesystems. Rendering, frame
timing, encoder settings, and receipt contents are unchanged.

This is not a crash-atomic two-file transaction. Process/power loss between
renames can still leave an orphan receipt; disk durability and adversarial
concurrent parent-directory mutation are not guaranteed. No automatic cleanup
of preexisting/orphan user files was added.

## Verification

The focused publication test covers eight cases: successful complete-byte
publication, late movie file/symlink/directory collisions with receipt rollback,
existing receipt preservation, dangling receipt symlink preservation, missing
staged movie and empty staged receipt. It uses only disposable owned fixtures.

The actual static and timed film tests also pass after the change, including
95 decoded temporal frames across six forward/reverse/frozen films, fixed/moving
cameras, overwrite rejection and active cancellation. Initial focused results:
/private/tmp/gaussian-film-publication-tests.log (3/3).

- Actual encoder integration also creates movie and receipt collisions after
  the first frame has been accepted, requiring failure without overwriting the
  new occupant or leaving an orphan counterpart.
- Rebuilt packaged-app proof:
  /private/tmp/gaussian-film-publication-proof-hpN2lS/native/receipt.json.
  The real QML export popup was opened and visually inspected, and the accepted
  export produced 48 frames at 800x600/24 fps. The proof uses an explicit new
  temporary destination rather than interacting with the OS file picker.
- All 48 decoded app frames pass the independent scene-time/camera calculation
  check: minimum RGB PSNR 48.10045 dB (gate 35), wrong-frozen-time negative control
  24.74743 dB. Log: /private/tmp/gaussian-film-publication-native-parity.log.
  This remains a synthetic scene, not trained dynamic capture evidence.
- The app proof isolates settings/cache/catalog and deliberately uses a closed
  database port. Its catalog-recovery banner is expected and does not diagnose
  the user's actual catalog. No user catalog or external model store was changed.
- App build succeeds. Existing Metal fastMathEnabled deprecation and viewport
  test QString-copy warnings remain unrelated to publication. git diff --check
  passes.
- The first wider-suite attempt had 13 missing test executables, not 13 runtime
  failures; the five built tests passed. Its log is retained at
  /private/tmp/gaussian-film-publication-regressions.log.
- After building the missing targets, all 18 Gaussian CTests pass in 10.56
  seconds, with no skips/not-run cases:
  /private/tmp/gaussian-film-publication-final-regressions.log.
