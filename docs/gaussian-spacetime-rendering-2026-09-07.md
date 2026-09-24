# Native space-time Gaussian rendering

Status: native temporal rendering **infrastructure**, not completed 4D training,
checkpoint import, or Scene UI integration. The full app goal remains active.

## Representation and implementation

The mathematical model factors a joint space-time Gaussian into conditional
spatial geometry and a temporal opacity weight, with position shifted according
to spatial-temporal covariance. This follows the conditional/marginal construction
in the [4D Gaussian paper, section 3.2](https://arxiv.org/html/2310.10642v3).
It is not interpolation of separately trained static frames.

- `compile_gaussian_spacetime` validates a full symmetric positive-definite
  double-precision XYZ/time covariance and creates conditional geometry, velocity,
  temporal mean and inverse temporal standard deviation. Invalid/unrepresentable
  inputs are rejected rather than silently repaired.
- Time-first Cholesky factorization preserves a square root of the conditional
  covariance. The native shader projects this factor before forming 2D covariance.
  Thin axes therefore do not have to survive a materialized float32 3D covariance.
- Static `GaussianPoint` remains 240 bytes. Temporal scenes add an 80-byte motion/
  covariance-factor record per point. The factor is authoritative for temporal
  rendering; the spatial point's float covariance is only a convenience view.
- Spatial SH degrees 0–3 and optional temporal cosine orders 0–2 are supported.
  Additional temporal appearance storage is allocated only when used. The
  [reference implementation's appearance convention](https://raw.githubusercontent.com/fudan-zvg/4d-gaussian-splatting/main/utils/sh_utils.py)
  uses spatial SH multiplied by temporal cosine terms; no upstream code was copied.
- Each render requires an explicit time inside the scene range. A double scene
  origin is subtracted before local float time evaluation, avoiding loss of
  subframe precision from a large absolute timestamp. The capture range supplies
  the temporal appearance period; an import adapter must convert training units.
- Native depth ordering uses the positions at the requested time. The shader
  evaluates the same positions with fused multiply-add, temporal opacity and
  time-varying view-dependent color. Immutable scene buffers persist across time
  requests; preview frames remain native textures with no CPU readback.
- Temporal layout, factor, appearance, variance, opacity, time bounds and counts
  are checked. Static SH degree and the two-million-point renderer limit are
  checked too. This does not broaden the supported display-referred sRGB output
  contract or establish HDR/scene-linear reconstruction.

The [reference trainer stores tensor checkpoint state](https://raw.githubusercontent.com/fudan-zvg/4d-gaussian-splatting/main/scene/gaussian_model.py),
not the static PLY artifact consumed by the current Scene project loader. No
checkpoint was deserialized or trainer installed. A safe importer must verify
the exact tensor layout, time/pose units, rotation conventions and appearance
ordering; blindly reusing static PLY or paper quaternion conventions is not proof
of compatibility. Dependency/runtime/license integration remains outstanding.

## Correctness and runtime evidence

- 4000 density comparisons against an independent full 4D matrix inverse:
  maximum absolute density error `2.44723e-7` (gate `2e-6`). Nine invalid inputs
  are rejected. A deliberately thin SPD input loses rank in its float covariance
  but preserves a valid rendering factor. Log:
  `/private/tmp/gaussian-spacetime-math-final.log`.
- 144 native Metal image comparisons: four spatial degrees, three temporal
  orders, two camera angles and six times (including a 1/120-second offset from
  an 86400-second origin). Independently assembled static oracles cover moving
  position/depth order, opacity and appearance. Maximum channel error is **1**
  8-bit unit (gate 2), following the factor-based projection's rounding order.
- Eleven invalid render requests are rejected. Thirty successive time requests
  return native textures without CPU images. Final proof/synthetic image sequence:
  `/private/tmp/gaussian-spacetime-proof-y9ENY5/matrix-final/receipt.json`.
  Representative before/after-time images were inspected; they are synthetic
  splat fixtures, not photographic reconstruction evidence.
- The initial large probe rejected 4051 of 753038 real static splats because
  their very flat ellipsoids lose SPD in float covariance storage. The revised
  probe retains their latent scale/rotation factors and renders all points with
  explicitly synthetic motion. No splats are dropped or enlarged to pass.
- Two isolated large temporal probes measured median worker render times of
  32.45 and 38.67ms at 800x596; maxima 46.92 and 57.23ms. The probe uses 17 frames,
  with the first excluded from timing. This is **not** foreground FPS, a 60fps
  guarantee, or a trained dynamic scene. Source shape retention plus artificial
  motion is only a throughput/numerical-range fixture.
- Static trained-scene output remains pixel-exact against the pre-feature image:
  `/private/tmp/gaussian-spacetime-static-final.log`. The original pre-feature
  timing sample was faster than later samples, so static performance parity is
  not established by the image equality.
- All fourteen Gaussian tests pass, including native rendering, timed preparation,
  controller lifetime, viewport handoff, saved cameras and film export. The
  factor-path suite completed in 6.19s; log:
  `/private/tmp/gaussian-spacetime-final-regressions.log`.
- Packaged app opens and renders the existing static scene. Receipt and inspected
  screenshot: `/private/tmp/gaussian-spacetime-proof-y9ENY5/native-static/`.
  The catalog-recovery banner is expected in this isolated closed-port QA profile.
  No user catalog or model weights were changed. One existing range-loop-copy
  warning in `gaussian_viewport_handoff_test.cpp` remains; new renderer code builds.

## Bounded static-path performance check

One candidate only: specialize the Metal vertex function for static versus
temporal input so static rendering does not retain the temporal shader branches.
The existing `gaussian_scene_render_test` real-scene route measures 30 camera
motion frames, using the same 753038-splat artifact, capture camera, 800x596
output and explicit readback photo for each before/after run. Preserve the old
binary to check measurements without changing source back and forth.

Acceptance gates, declared before the candidate edit:

- Median worker render time improves at least 10% against the immediately
  captured pre-specialization binary.
- P95 worker render time does not regress more than 5%.
- Peak RSS measured by `/usr/bin/time -l` does not regress more than 5%.
- Static pixels remain exact, and the full time/view correctness suite passes.
- Stop after one attempt; reject an unsuccessful candidate. This does not
  establish foreground FPS or a universal performance claim.

Baseline: `/private/tmp/gaussian-specialization-before.json` and `.time`.
The earlier pre-feature image remains the independent static-pixel baseline at
`/private/tmp/gaussian-spacetime-proof-y9ENY5/static-before.png`.

Outcome: **rejected and reverted**. Initial median 38.53 → 32.87ms improved
14.7%; a same-binary confirmation pair measured 36.15 → 33.62ms, only 7.0%,
below the declared 10% gate. Both pairs retained exact PNGs and stayed below
the P95/RSS caps. The bounded loop stopped after this one candidate rather than
changing its threshold. Artifact: `/private/tmp/gaussian-specialization-comparison.json`.
The space-time representation and factor-based numerical fix remain implemented.
The desktop app and renderer test targets rebuilt after the revert; the final
fourteen-test suite passed again. Logs:
`/private/tmp/gaussian-spacetime-release-build.log` and
`/private/tmp/gaussian-spacetime-release-regressions.log`.

## Remaining completion work

Connect a genuine time-aware trainer through a safe, versioned artifact adapter;
provide the native Scene loader/time controls and scene-time photo/film export;
validate real calibrated moving captures with held-out times/views; and prove
photographic quality, recovery, memory and foreground interaction performance.
The current Scene UI still correctly labels timed captures prepared, not trained.
The renderer API is not a new capture-to-4D product workflow by itself. RAW/color,
loading, embedded model/search/agent and full editing-tool requirements also remain
active; these synthetic renderer checks do not close them.
