# Combined optical haze scheduling

The 960x639 imported-object shot with 64 aperture samples, 128 lighting samples,
received shadows, captured-environment reflections, shadowed haze and finishing
now renders in **32.36 seconds**, versus a fresh **76.62-second** frozen baseline.
Both the candidate and confirmation PNGs are byte-identical to the baseline.
This is a 2.37x export improvement, not realtime qualification.

## Bounded experiment

Evidence root: `/private/tmp/looksy-optical-haze-perf-v1`.

`plan-before.json` pins the scene plan, baseline binary and the three-attempt
ceiling before implementation. Acceptance requires at least 20% lower `renderMs`,
exact PNG/scene-plan identity, unchanged sampling and texture layout, GPU time no
more than 10% higher, maximum GPU submission below 16 ms, and RSS/peak footprint
within 10% of baseline. `/usr/bin/time -l` supplies host memory measurements.
All benchmark runs use a fresh process and the same read-only frozen scene plan.

The baseline records 182,624 lighting submissions and 52.36 seconds of GPU time.
The cheap haze-compositing pass samples an already completed prefix texture, yet
still submitted and waited for each roughly 512-pixel band. The independent
visibility-field strata also used fixed 128-node row batches.

| Run | Render time | GPU time | Submissions | Result |
| --- | ---: | ---: | ---: | --- |
| Frozen baseline | 76,621.45 ms | 52,362.72 ms | 182,624 | Reference |
| Attempt 1 | 61,387.43 ms | 51,531.46 ms | 59,872 | Rejected: 19.9% improvement misses the fixed 20% target |
| Attempt 2 | 32,537.34 ms | 26,317.87 ms | 34,784 | All guards pass |
| Confirmation | 32,364.27 ms | 26,331.24 ms | 34,784 | All guards pass |

Attempt 1 increases only the cheap lookup batch to 32,768 pixels. Attempt 2
extends the same scheduling approach to the independent field rows: start at the
original batch size, adapt toward four milliseconds per submission, and cap at
a 512-node row budget or one wider row. The ordered depth-prefix integration
still runs only after all strata finish. Cancellation is checked between batches.

No MSL shader, sample position, random seed, light/aperture count, field spacing,
depth-stratum count, tile edge/halo or texture allocation changes. The maximum
submission drops from 7.21 ms in baseline to 3.62 ms in confirmation. Peak RSS
falls from 792,838,144 to 745,144,320 bytes; peak footprint rises from
1,672,725,632 to 1,700,070,504 bytes, within its unchanged 10% guard.

`attempt1-comparison.json` retains the failed target. `attempt2-comparison.json`
and `confirmation-comparison.json` contain the independent gate decisions.
The loop accepts attempt 2 and stops without using its third attempt.

## Correctness and workflow checks

The rebuilt targets pass eleven focused renderer/export tests, including native
and software haze/shadow checks, exact tiling checks, 8K field admission and
observed-batch cancellation, materials, object compositing, capture-lighting
cache identity and film/sequence publication. See `final-ctest.log`.

Both native UI workflows pass unchanged: haze and captured-color shadow receiver
selection, undo/redo, setting retention, photo export, frozen film export during
editing, save/reopen, saved-shot recall and compact controls. See
`native-ui-ctest.log`. These checks establish workflow behavior, not active-window
input latency. Source and binary identities are recorded before the final export
in `final-identities-before.json`.

The manual staging driver can export the actual saved path against a frozen
composition with `--film-plan PROJECT PLAN_JSON NEW_OUTPUT`. It checks the path's
first camera and duration against the plan, snapshots the saved path, resolves
the immutable mesh/material assets, and calls the production PNG-sequence
exporter. No completed export receipt is fabricated to start that workflow.

The combined export completes all twelve 960x639 frames at 24 fps over the saved
0.5-second, 1.5-degree camera path, retaining 64 aperture and 128 lighting samples.
`combined-film-replay/receipt.json` independently replays every frame with zero
RGB error. The first exported frame also matches the pre-optimization baseline
still exactly in decoded RGB: `first-film-frame-comparison.json`. First, middle
and last exported images were visually inspected. This closes the combined
optical/atmosphere film case at these poses; it does not establish larger-motion
or sampling-convergence qualification.

The rebuilt app also passes its actual dense Scene/haze/control proof when
launched through macOS LaunchServices: `packaged-launchservices/receipt.json`,
9.70 seconds. Its window is exposed but inactive. The receipt's readiness timing
gate is separate from foreground input latency, which remains unqualified.

## Remaining scope

The combined scene is still an offline export workload at this quality. This
experiment does not qualify sustained foreground input latency, larger camera
moves, spatial-reflection/lens convergence or general reference-video parity.
It retains the inspected approximate surface placement and environment-probe
reflections of the existing combined composition.
