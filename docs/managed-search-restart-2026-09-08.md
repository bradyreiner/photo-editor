# Managed search restart identity and runtime validation

## Outcome

Fixed a reproduced integration defect: the description index included the local
server endpoint in model identity, but the managed server selects a new port on
each launch. An unchanged, verified model in the same private store therefore
generated new image descriptions after restart instead of reusing its index.

Managed cache identity now includes the private model-store path, model name and
verified artifact digest, not the temporary port or PID. External-server identity
still includes the endpoint. Descriptions cannot cross private stores or distinct
external endpoints merely because their reported model names match.

The search controller also enforces the managed runtime boundary itself, beyond
the UI's model-availability flag. Before requests and when accepting replies it
requires the current owned, ready process, matching endpoint/store and a verified
model. The advertised model digest must match the application artifact pin before
any image or description is sent. A changed process invalidates pending results.
Verification lost during image preparation also rejects a cached direct score.
This guards application-owned runtime transitions; it is not authentication
against a hostile process controlling the local machine.

## Verification

Artifacts: `/private/tmp/managed-search-proof-S26alI`.

- `baseline-test.log`: the new two-server regression fails before the fix at
  "verified managed restart on a new port must reuse the same saved image
  description".
- `fixed-test.log`: the initial corrected search suite passes.
- `regression-tests.log`: all four focused tests pass in 9.67 seconds: artifact
  verification, managed-runtime lifecycle, visual search and search/edit scope.
  Final search tests cover changed ports, same-store reuse, private-store isolation,
  external-endpoint separation, unverified rejection before any request, changed
  artifact rejection before inference, replaced-process replies and verification
  loss while preparing a cached score. The managed registry is injected in these
  tests: they prove mechanics, not actual managed-weight inference.
- `integrated-build.log`: production desktop and search test build successfully.
  `final-regression-build.log`: extended search tests and agent controller rebuild
  successfully. No compiler warning/error diagnostics found in either log.

## Real-image observations: external local server only

The already-installed local `qwen3-vl:32b` model reports the application's pinned
manifest digest. The external server on 127.0.0.1:11434 initially had no loaded
model. It was not stopped, reconfigured or replaced. Only its inference API and a
temporary derived index were used; no model was copied or downloaded.

The source is the existing, visually inspected HDR+ reference JPEG at
`/private/tmp/framehouse-six-raw-source-v3/hdrplus-nexus6-skin-0218/hdrplus-nexus6-skin-0218-reference.jpg`.
It shows a close-up portrait with a dark hat and red collar accents. Search sends
only the controller's reduced, re-encoded image, not source metadata.

| Query | New / reused descriptions | Result | Observed time |
| --- | --- | --- | --- |
| Close-up portrait wearing a dark hat | 1 / 0 | Miss: no match | 108,406 ms |
| Close-up portrait of a person | 0 / 1 | Match at model relevance 90 | 32,056 ms |
| Snowy mountain landscape without people | 0 / 1 | Correct negative | 7,632 ms |

The persisted description omitted the hat. This independently reproduces the
known description-only recall problem; repairing cache identity does not solve
it. Queries and model warm-up differ, so these are smoke timings, not comparable
speedup measurements or a retrieval-quality benchmark. They do not establish
fresh RAW decoding, multiple-camera coverage or fast whole-library search.
The later JSON receipts explicitly say `runtime: external`; their empty managed
snapshot and no-owned-process flag are not evidence of loaded managed shutdown.

## Prepared managed-runtime proof and open work

The search test now accepts an explicit owned-runtime mode:

```sh
build/dev-macos-debug/tests/visual_search_controller_test \
  --live /absolute/photo.jpg qwen3-vl:32b 'a close-up portrait' \
  --index-path /absolute/temporary-index.sqlite3 \
  --managed-runtime /absolute/bundled/ollama /absolute/existing-private-model-store
```

That mode starts the actual supervisor, waits for full artifact verification,
searches through its owned endpoint, records its snapshot, and stops its owned
server. A second independent invocation with the same store/index can prove real
port-changing restart reuse. This mode is implemented and built, but **not yet
run with real weights**. Permission to reuse the existing 20.9 GB payload in a
private copy-on-write test store was requested; no copy has been made.

Real managed inference/restart/loaded-runner shutdown, packaged UI search,
small-detail recall, larger-corpus retrieval, an image/text embedding index and
its import/update/delete lifecycle remain open. The app-wide RAW/rendering,
Gaussian 3D/4D, agentic editing and professional-tool requirements remain intact
in `active-app-completion-goal.md`. Export throughput/shutdown issues from the
preceding candidate are also still open, not superseded by this search work.
