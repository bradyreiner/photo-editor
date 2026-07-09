# Color Control Limit Decisions

The current control limits stay conservative until a sweep test proves a wider
range is stable and useful.

## Accepted Current Limits

- Exposure: `-2.0..2.0` stops.
- Core tone controls: `-100..100` for contrast, highlights, shadows, whites,
  blacks, and brightness.
- Core color controls: `-100..100` for temperature/tint offsets, vibrance,
  saturation, mixer hue/saturation/luminance, and color grading
  saturation/luminance.
- Hue-wheel controls remain normalized `0..1`.
- Strength/range controls that are naturally one-sided remain `0..100`.

## Accepted Response Tuning

- The Blacks control uses stronger deep-shadow tone anchors so a small negative
  move produces a measurable near-black response instead of a barely visible
  lift/crush.
- The default OKLab Color Mixer uses stronger hue, saturation, and luminance
  scales than the legacy conservative response; HSV and LCH retain their
  existing scale choices.

## Gate For Expanding A Limit

Do not expand a visible slider limit unless the change adds tests for:

- finite scene-linear output at both endpoints,
- bounded preview and export output transforms,
- neutral preservation for tone-only and chroma-only controls,
- monotonic luminance behavior for tone controls,
- hue stability for non-clipped colors,
- a visible effect threshold that proves the new range is useful rather than
  just numerically larger.

`color_science_sweep_test` is the default home for limit and response sweeps.
`render_parity_test` should be extended when a wider limit changes desktop
preview/export behavior.
