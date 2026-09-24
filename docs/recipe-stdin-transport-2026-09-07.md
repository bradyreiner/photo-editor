# Large-recipe transport and save-performance investigation

## Correctness change

Ordinary saves and rendering-migration approvals now send a versioned JSON
argument envelope through the helper's stdin, not through operating-system
command-line arguments. The helper command line contains `--stdin-json-v1` only
(plus any development Cargo launcher arguments). Masks, pixels and approval
evidence no longer appear in process argument listings.

The envelope schema is `photo-editor-catalog-arguments:v1`, with an `arguments`
array of strings. Rust and Qt enforce a 256 MiB UTF-8 payload limit and a maximum
32 arguments; helper-specific arity and existing recipe/evidence validation still
apply. Unknown schema/fields, malformed JSON, non-string arguments and oversized
input fail closed. Legacy argv callers remain supported. Stdout/exit status and
the atomic canonical/legacy database transaction are unchanged.

Qt queues bytes without waiting for process startup or pipe drain on the UI
thread, then closes the write channel after the queued bytes are delivered.
An already-running QProcess is rejected rather than receiving another job's
payload. No recipe temporary files are created by this transport.

## Proof

`catalog_helper_payload_test` sends a 2 MiB payload through a real QProcess pipe,
checks a byte-for-byte SHA-256 response, Unicode/quotes/NUL, EOF, argument privacy,
argument-count rejection and rejection of a busy process without corrupting its
existing input. The Rust parser tests cover large Unicode data, legacy calls
that never read stdin, malformed/versioned messages and bounded input including
the exact limit boundary. `cargo test --workspace` passes.

The real PostgreSQL integration test now has a size-checked large-mask mode. It
creates a valid 1024x1024 alpha raster with deterministic high-entropy values,
then verifies the serialized recipe exceeds 512 KiB before running the test.
This avoids mistaking a discarded or tiny fixture for a large-recipe test.

- JPEG recipe: 2,812,246 bytes; `/private/tmp/recipe-stdin-large-jpeg.log`.
- DNG document/JPEG document/linked DNG: 2,812,348 bytes;
  `/private/tmp/recipe-stdin-large-raw.log`.

Both reproduce the old helper-start failure through argv:
`execve: Argument list too long`. Both pass through stdin: rapid-switch saves,
injected PostgreSQL rejection, previous-commit preservation, newest-edit retry,
actual canonical readback, fresh-controller state equality and identical rendered
pixels. The test checks the real save process uses the stdin flag and a short
argument list. Private PostgreSQL ports 49354 and 49518 independently report
Connection refused after successful cluster shutdown. User catalogs, models and
original RAW files are untouched.

The approval helper uses the same tested parser/transport and retains its
existing backup/evidence validation. A complete photographer-approved migration
transaction was not replayed against PostgreSQL in this slice.

Final focused acceptance: four tests pass in 10.71 seconds in
`/private/tmp/recipe-stdin-final-acceptance.log` (pipe transport, save queue,
canonical persistence, linked-layer relink). Final Rust workspace log:
`/private/tmp/recipe-stdin-rust-final-tests.log`. App/test build:
`/private/tmp/recipe-stdin-final-build.log`. No compiler warnings/errors were
found in those final logs; scoped `git diff --check` passes.

## Performance-loop outcome

The bounded investigation used the existing native RAW/relink route, with at most
three optimization attempts, a target of 50% less relink render time and completion
within its existing 45-second deadline. Save/recipe/pixel correctness were protected
gates. A 20% peak-RSS regression cap was intended for an accepted optimization;
complete comparable peak-RSS traces were not obtained, so no memory/performance
promotion is made. No speculative performance patch was accepted.

Earlier enabled-save runs had 9,873/11,334 ms settled relink renders; the disabled-
save diagnostic had 1,604 ms. Instrumentation then produced an enabled-save pass
without an optimization: `/private/tmp/framehouse-save-profile-urZbFz/receipt.json`,
30,649 ms total, 812 ms interactive and 1,645 ms settled relink. Save preparation
and helper startup took 15 ms. A host process sample measured the save helper at
0.0% CPU and 9,568 KiB RSS, and the app at 2,580,752 KiB RSS; these are samples,
not peaks. The private runtime stopped and port 64404 independently closed.

This contradicts a simple claim that the new save queue inherently caused the
slowdown. Following the performance-loop stop rule for variable results, the
optimization loop stopped before a speculative scheduling change. The new
`recipe.save.prepared` telemetry remains for controlled foreground investigation.
The stdin change addresses an independently reproduced correctness failure; it
is not presented as a rendering speed-up.

The final stdin-enabled native correctness attempt,
`/private/tmp/framehouse-recipe-stdin-native-hTCrU9/`, again exceeded the existing
deadline during the slow relink render and produced no receipt. Its save
preparations were 15 ms and 103 ms, insufficient by themselves to explain the
multi-second render slowdown. A scoped process check confirmed no private bundled
model runtime remained. This run is not accepted full-workflow UI evidence.
Controlled foreground/render profiling remains necessary; the protocol and real
database correctness gates above do not substitute for realtime validation.

## Remaining limits

At the transport-only milestone, pending saves remained in memory. The later
`recipe-crash-recovery-2026-09-07.md` adds atomic completed-edit checkpoints,
killed-process recovery and persistent save-error UI. Failed-checkpoint close
coordination and the broader recovery boundaries documented there remain open. Preparation,
canonical asset storage and serialization still run synchronously; large payloads
can therefore cost UI time even though pipe delivery is asynchronous. The 256 MiB
limit is explicit, not a claim of unlimited Photoshop-scale document support.
Immutable linked-file packaging, complete app relaunch,
non-neutral crop intent and broad camera/mask/performance corpora remain open.
