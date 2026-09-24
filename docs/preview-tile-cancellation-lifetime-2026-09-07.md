# Preview tile cancellation lifetime

## Defect and fix

The exact-cover brush patch assembler launches parallel copies that borrow its
stack-owned `working_patch` and the caller's dirty-tile images. Its result loop
previously returned immediately on the first failed copy. `QFuture` destruction
does not join unfinished work, so cancellation or a failed copy could destroy
those buffers while another copy still used them. Task exceptions exposed the
same lifetime problem in the adjusted-dirty-tile batch.

`PreviewFutureDrain` now guards both future collections. All submitted tasks
finish before their borrowed buffers leave scope, on success, early return, or
exception unwinding. A completed future's exception cannot prevent draining the
remaining futures. Callers still receive exceptions through their existing
result path; this is lifetime protection, not a new error-recovery policy.

The guard is inside render-worker assembly, not controller teardown. Normal
render requests remain dispatched through `QtConcurrent`; it does not add a
controller-side wait. Existing row-level cancellation checks remain intact.
There are no changes to pixel math, decoding, source files, recipes, or caches.

## Focused proof

`preview_future_drain_test` holds one real QtConcurrent worker while another
fails, throws, or the submitting scope throws. An independent supervisor checks
whether scope exit happens before releasing the held worker. A buffer-lifetime
marker models the old invalid access without deliberately dereferencing freed
memory.

- `--unguarded` reproduces the original lifetime failure in all three failing
  modes: scope escapes and the held worker observes an expired buffer. Exit 1.
- Guarded early-return, task-exception, submitting-scope-exception, normal
  success, and empty-group cases pass. The held worker always sees a live
  buffer; exception propagation is preserved. Exit 0.
- Logs: `/private/tmp/preview-future-drain-before.log` and
  `/private/tmp/preview-future-drain-after.log`.
- Six focused CTest regressions pass in 3.50s: lifetime, preview math, compiled
  local adjustments, coverage cache, and legacy/ACES2 reference workflows.
  Log: `/private/tmp/preview-future-drain-regressions.log`.
- App and preview driver rebuild pass. Build logs:
  `/private/tmp/preview-future-drain-build.log` and
  `/private/tmp/preview-future-drain-app-build.log`. Initial CMake configure
  reports existing runtime-package configuration warnings; changed C++ compiles
  without warnings.

## Real-image and native proof

The rebuilt preview driver runs the actual ACES2 preview entry point using the
existing Nexus 6 DNG and its paired reference JPEG, at full source resolution.
The RAW receipt at `/private/tmp/preview-future-drain-raw-flOp0I/receipt.json`
passes at 4208x3120: reference comparison changes zero pixels with zero maximum
RGBA8 difference, local temperature changes zero protected pixels, disk
reference recipe isolation passes, and a missing reference is rejected.
The paired JPEG receipt at
`/private/tmp/preview-future-drain-jpeg-irsRXI/receipt.json` passes the same
checks at 4208x3120, also with zero reference/protected-region pixel changes.

Native application smoke artifacts are under
`/private/tmp/preview-future-drain-app-mUKuAj/`. The receipt reports `ok: true`,
`catalogIsolated: true`, successful QML placement of the JPEG over the RAW,
preview publication, and undo/redo, with no model weights downloaded. Both the
workspace screenshot and exported layer image were visually inspected. The
catalog-recovery banner is expected in this intentionally unavailable-catalog
test profile, not evidence about the user's catalog.

## Boundaries

The deterministic race test exercises the production lifetime guard with real
concurrent tasks, not a forced cancellation inside a live RAW decoder. The
real-image and native checks are rendering regressions, not proof that the
native smoke run hit the narrow race. No input-to-photon, import-speed, complete
asset-switching, all-camera/color-space, Photoshop, Gaussian 4D, embedded-model,
or agent readiness claim follows from this fix. The broader goal remains open.
