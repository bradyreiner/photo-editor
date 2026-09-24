# Gaussian grain and halation: creative-effects foundation

Later checkpoint: explicit still-photo exports now use tiled rendering up to
8192 pixels per edge, including grain and halation. The 4096 limit below remains
for texture previews and film-video output. See
`gaussian-tiled-photo-export-2026-09-09.md`; film calibration remains open.

The settled Gaussian photo now supports grain and red/orange highlight halation.
These are explicitly creative, display-referred effects, **not calibrated Portra,
CineStill or another named stock**. Existing thin-lens sampling runs first. No
production app bundle, capture, catalog or model weight is replaced by this work.

## Rendering contract

- Halation extracts positive luminance above a user threshold after linearized
  exposure. A normalized separable Gaussian spreads that signal, with zero
  extension beyond image boundaries. A fixed linear-RGB tint `(1, .15, .025)` is
  added before monochrome conversion, tone shoulder and sRGB encoding. Amount,
  spread and threshold are adjustable. This additive approximation is not a
  measured spectral film-layer response or an energy-conserving scattering model.
- Grain uses seeded, smoothly interpolated scalar lattice noise shared between
  channels, with a per-channel `sqrt(code * (1-code))` envelope and bounded output.
  It is a code-value-dependent texture proxy, not an emulsion-density simulation.
  It preserves black/white endpoints and neutral monochrome. Grain amount and
  size are adjustable. Both effects default to zero; disabled effects preserve
  the previous exact output. Halation that receives no bright signal also keeps
  untouched neutral pixels byte-identical, avoiding unnecessary transfer rounding.
- Effect sizes reference a 2048-pixel long edge rather than a physical film
  format. Small outputs use a minimum one-pixel noise lattice with amplitude
  attenuation; this is a bounded preview approximation, not integrated film-grain
  sampling or cross-resolution statistical equivalence. Halation sigma has a
  quarter-pixel floor. Measured resolution/spectrum matching remains open.
- The seed is persisted, never derived from wall-clock time. Repeated stills are
  deterministic at identical settings and dimensions. Movies currently keep the
  same film-plane grain pattern across frames; independent per-frame emulsion
  realizations are not implemented. Paths reject mismatched film settings/seeds
  rather than silently jumping between them.
- Camera movement skips grain/halation along with finite-aperture integration.
  Settled preview and exports use the same GPU path. Halation requires one extra
  R32Float image; grain requires no image allocation. Film effects are limited
  to 4096 pixels per edge, with explicit rejection above that bound. No production
  preview CPU readback is added. Cancellation waits for the active GPU command;
  accepted exports still share the serialized renderer with previews.

## UI and persistence

Photo settings now contain a Film tab with compact Grain and Halation sections.
All settings, including the seed, live in `framehouse-creative-film-v1`, nested
inside `framehouse-gaussian-photo-settings-v3`. V3 always carries both lens and
film settings. V1/v2 still parse, and default settings retain their original
serialization and fingerprints. Strict parsing rejects unknown fields, invalid
types/ranges, nonfinite values and noninteger/out-of-range seeds. Changing a lens
control must retain V3 film settings instead of downgrading the schema.

## Proof and remaining gates

Native coverage includes an independent double-precision halation convolution
reference, warm spread beyond a bright emitter, below-threshold identity,
monochrome output, deterministic grain/native preview texture parity, grain mean
and variance, seed variation, all-black protection, disabled-effect identity,
motion-draft bypass, output bounds, exact saved-shot roundtrip and malformed
settings/path rejection.

Final focused native run: six of six tests pass in 3.26 seconds:
`gaussian_scene_render_test`, `gaussian_spacetime_render_test`,
`gaussian_film_export_test`, `gaussian_saved_views_test`,
`gaussian_camera_path_test`, `gaussian_viewport_handoff_test`.
The initial below-threshold identity test exposed transfer-function rounding;
the renderer now bypasses unchanged neutral pixels and the test passes exactly.

Actual Main.qml UI proof:
`/private/tmp/framehouse-film-effects-QJLs8m/proof/receipt.json`.
Benchmark wrapper:
`/private/tmp/framehouse-film-effects-QJLs8m/ui/20260909T184022.197389Z-perf-workspace-switch-be75b470`.
Foreground eligibility and film, lens, tone, immersive and rapid-navigation
checks all pass. On the existing 753,038-splat scene, controls set grain `.6`,
size `3`, halation `.5`, spread `8`, threshold `.3`, seed `1`, together with the
previous 16-sample seven-blade optics. Settings survive Library/Scene navigation;
two 800-by-596 PNG exports are byte-identical, SHA-256:
`f983cf34094a793efe625e657b56cc8fbd63b473ce6cbe0606737af1a559271c`.
JPEG export succeeds at the same dimensions with an sRGB tag. Both compact
control sections and the rendered PNG were visually inspected. Existing Export
QML warnings and the preexisting handoff-test range-copy warning remain separate;
no Scene error was observed. Only isolated test binaries were built/run; the
user's running application was not restarted.

Passing these tests is not evidence of measured film-stock fidelity, sustained realtime
performance, genuine RAW scene-radiance recovery or complete photographic optics.
The capture's existing ghosting and missing geometry are unchanged. Next gates
include measured film scans/response curves, density and spatial-spectrum grain
validation, channel correlation controls, spectral halation calibration, physical
camera scale, real lens-reference matching and sustained latency/memory testing.
