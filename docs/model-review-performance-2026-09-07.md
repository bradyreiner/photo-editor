# Rendered model review performance loop

Scope: reproduce the real JPG background-edit proof review through
`critique_agent_rendered_proof`, without repeating search or modifying a catalog.
The source is the isolated `jpg-aces2-verified/draft-report.json` under
`/private/tmp/photo-editor-search-edit-SJmVJX`. The captured prompt and rendered
after image are the fixed input, not the source photograph.

Configuration boundary: the isolated workflow manifest uses a 30-second default
deadline and 16,384-token context. The bundled production manifest currently
specifies 120,000 ms and 12,288 tokens. These measurements diagnose the isolated
request's responsiveness; they are not proof that every packaged-app request
times out at 30 seconds. Neither manifest was changed for this measurement.

Before changing production behavior, measure a fresh baseline. Allow at most
three optimization attempts. Accept only a completed, schema-valid, image-grounded
review within 21 seconds (at least 30% below the existing 30-second timeout), with
unchanged user instructions, style, validation facts, calibration, and image.
A timeout is censored latency, not a completed baseline. No claim of p95 or
universal model speed will be made from a small local sample.

Guardrails: do not weaken review/confirmation, change the selected model, increase
the deadline, or introduce a fallback. Preserve missing-evidence human review.
Compare two candidate repetitions with the baseline and inspect model-resident
memory; reject an increase above 10%. Record host/model state and process RSS.
Stop this bounded loop if the metric cannot be obtained or the candidate is
unstable; leave the broader app-completion goal active.

Initial host check: 48 GiB physical memory; local qwen3-vl:32b Q4_K_M resident at
24,524,315,688 bytes with a 16,384-token context. This is one host, not tier-wide
release evidence.

## Baseline and stop decision

- `agent_proof_review_replay` replays the captured prompt sections through the
  production critic and checksum-binds the actual rendered after PNG. It does
  not load a catalog or apply any edit. `capturedPromptMatches=true` and
  `imageIncluded=true` in both receipts below.
- `/private/tmp/agent-review-baseline.json`: 30,069 ms, response timeout, process
  peak RSS 172,621,824 bytes. Server log: 7,297 input tokens, still pre-filling
  when cancelled. Latency is censored, not a completed result.
- `/private/tmp/agent-review-baseline-warm.json`: identical input and unchanged
  code, 18,162 ms, completed critique requesting human review. Peak process RSS
  173,129,728 bytes. Server log: only 53 prompt tokens remained to process,
  followed by 180 generated tokens at approximately 10.4 tokens/second.
- The rendered proof SHA-256 in both runs is
  `00dce8c0f18fd4981decda7f9becd6cc05863f57301113eb1abb32b827769c38`.

Stopped before a production optimization: partial prompt-cache reuse changes
the timing by more than the proposed acceptance threshold without any code
change. No model speedup is claimed. Reliable first-review latency remains an
open runtime/model-sizing problem; cached success is not its solution.

## Separate review-loading measurement

Target the QML report boundary, keeping full reports authoritative in C++ and
on disk. The captured report repeats multi-megabyte inline mask rasters and
recipe JSON, though the UI displays only metadata. Before patching the
controller boundary, compare the full report and a read-only display projection
through the same Qt JavaScript conversion route. Allow at most three attempts;
require at least 90% reduction in transferred JSON bytes and 20% reduction in
median conversion time, with no more than 10% process peak-RSS regression.
Preserve every warning, verification state, asset ID, proof path, ordinary
recipe field, confirmation state, and original full-report value. The display
projection must be explicitly non-authoritative and must never feed edit,
commit, rollback, or persistence code. Runtime UI proof is required separately.

Boundary result: `agent_report_boundary_benchmark` uses an actual QObject
QVariantMap property exposed to Qt JavaScript, with ordinary report/validation/
asset/tool/warning/diff reads matching the QML consumers. It deliberately does
not eagerly stringify the report. On the existing 58,074,996-byte report, the
20-sample median is 3,792 ns (about 0.004 ms). The process exits successfully;
peak RSS is 256,212,992 bytes including fixture parsing. Receipt:
`/private/tmp/agent-report-boundary-baseline-host.json`.

Stopped before a display-projection patch: this boundary is not a measured
interaction bottleneck. The first harness revision had a QObject ownership
error on shutdown; that was fixed and those abnormal-exit runs are not proof.
Large persistence/serialization and full-app loading remain separate targets;
the boundary microbenchmark does not prove they are fast.

## Review correctness repair

The measurement pass exposed a separate fail-open result contract. Failed
reviews previously defaulted to `needs_user_review=false`, and malformed JSON
with missing booleans or non-string issue arrays could be accepted/coerced.
The controller already guarded outright errors, but callers should not need to
repair an unsafe result object.

- Default to human review; publish pass/review fields only after validation.
- Reject unfinished/token-limited responses and missing/wrongly typed review
  fields. Constrain issue, repair, and warning arrays to strings.
- Preserve deterministic failures and human-review requirements. An optimistic
  model cannot waive its own issues or truncated validation/calibration evidence.
- Record actual server prefill/generation/cache metrics when supplied; missing
  metrics stay missing. Field units follow the official
  [Ollama chat API](https://docs.ollama.com/api/chat).
- The pre-existing live smoke now supplies its synthetic proof explicitly;
  it no longer relies on the prohibited original-image fallback.

Focused `agent_local_model_test` passes with cases for missing fields, coerced
booleans, hidden object issues, unfinished/truncated responses, missing images,
truncated qualitative evidence, deterministic overrides, and valid output.
This is a correctness change, not an accepted latency optimization.

Live recheck after the safety repair:
`/private/tmp/agent-review-safety-live.json`, 17,732 ms with identical captured
prompt and proof. Completed, schema-valid critique; `pass=false` and
`needsUserReview=true`. The new server metrics explain the cached result:
7,296 of 7,297 prompt tokens cached, 570 ms prefill, 17,080 ms generation,
180 output tokens. This remains a warm functional check, not first-review
performance proof. The source-independent pixel checks from the prior workflow
remain the evidence for the actual small background shift; model critique is
not a substitute for those checks.

Real QML recheck of the critic safety repair (before the subsequent repair-loop
change): `/private/tmp/photo-editor-review-safety-1J7nPu`, native Cocoa/Metal,
filesystem `Main.qml`, real local search, confirmation, native background mask,
ACES 2 JPG rendering and exact rollback all pass. Model review times out in the
isolated 30-second configuration and visibly remains `0 verified · 1 needs
review`; no automatic commit. The screenshot was inspected. The stacked review
surfaces and temporarily blank main viewport remain UX/loading defects, not
polished visual completion. This is not a packaged-AOT UI test.

The broader controller suite exposed a model-repair failure. Inspection found
that repair requests duplicated full validation/previous-attempt JSON into
`user_instructions`, bypassing bounded evidence serialization and exposing
machine text to scope inference. Repair now retains the original instruction,
uses the revision phase and forced model planning, and supplies feedback only
through the existing bounded `prior_tool_results`. The repair fixture now uses
the string issue contract actually consumed by the UI.

Final build passes for the app, actual-QML driver, full agent controller test,
and automation bridge test. All eight selected regressions pass, including the
previously failing full controller repair workflow (46.40 seconds total):
`/private/tmp/agent-review-repair-final-tests.log`. Tests requiring fake loopback
servers were rerun with host access after sandbox binding failures. No failures
were relabeled as passes or skipped. `git diff --check` passes.

The real JPG proof additionally passes independent pixel gates: all 37,920
sampled cheek pixels are unchanged; 227,840 sampled background pixels gain
0.0121766064 in normalized mean red-minus-blue. Exact recipe rollback passes.
These are focused locality checks, not broad masking accuracy or full-resolution
export proof. The final repair-code change is covered by the controller suite;
the earlier real-QML run did not exercise an automatic repair (local-only scope
correctly stopped for human review).
