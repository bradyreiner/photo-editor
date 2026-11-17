# Looksy app-derived motion elements

This is a Remotion adapter kit for current native Looksy components. It is not a
Qt runtime capture, and the component study is not a newly completed trailer.
Run `npm run prepare-app-kit` after native source changes. Source hashes and
adaptations are recorded in `app-kit-source-map.json` at the project root.

| Trailer element    | Native source                                       | Reuse/adaptation                                                                                                                                                                                                                                                                                       |
| ------------------ | --------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| AppWordmark        | BrandWordmark.qml and supplied looksy-logo-dark.png | Original image asset and full supplied logo aspect ratio; RGB period retained.                                                                                                                                                                                                                         |
| appTokens          | Theme.qml                                           | Palette generated directly from current standard-contrast tokens.                                                                                                                                                                                                                                      |
| AppIcon            | AppIcon.qml                                         | Twelve Canvas icon paths copied by the preparation script, with the native optical stroke formula and round caps/joins. No external icon set.                                                                                                                                                          |
| AppInspectorSlider | InspectorSlider.qml, StudioAdjustmentSlider.qml     | Compact 40px row, adaptive label allocation, 80px value column, 28px reset column, 3px rail, ticks, zero/default fill, fixed 16px handle reservation, 12→16px visual thumb, precision exposure response, native EV/percent formatting. Authored film value/press/cursor state; no controller attached. |
| AppOpticalField    | OpticalLightField.qml, optical_light.frag           | Same shader equations, RGB channels, exponential response, caustic wavefront, ring and spectral bar. Only Qt shader interface/UV convention translated to WebGL2. Deterministic frame clock replaces UniformAnimator.                                                                                  |
| AppActivity        | OpticalActivityIndicator.qml                        | Actual interference ring and the app check icon on completion.                                                                                                                                                                                                                                         |
| Progress example   | ProgressStrip.qml                                   | App spectral-bar shader and authored measured-progress example.                                                                                                                                                                                                                                        |
| AppCompletion      | CompletionCaustic.qml                               | Native finite RGB/CMY scanline-facet construction, 560ms phase and opacity envelope.                                                                                                                                                                                                                   |

`App-Elements-Study` is a twelve-second component proof: a source-derived
inspector, an authored slider drag/release/reset sequence, native optical
loading/progress states and completion. The photo brightness change is an
explicitly labelled graphic illustration, not a Looksy RAW-engine render.

The native app uses Qt's platform UI font. The adapter uses the browser system
UI font and Menlo numeric values; it has not passed a pixel-for-pixel Qt/Chrome
comparison. Cursor gestures and progress numbers are authored for the component
study and do not measure actual edit/import performance.

Use these reusable components in the rebuilt commercial scenes. Extend the kit
from actual curve editors, selection/layer rows, timeline controls, and carousel
authoring tools as those scenes are built. Match native states, geometry,
labels, icons, disabled/modified behavior and motion; do not introduce generic
replacement controls. Keep promotional camera movement outside the local
component geometry so controls remain identifiable as Looksy.

Verified in this pass: ESLint/TypeScript; live Studio playback; exported resting,
pressed/changed-value, and completed-state frames including WebGL optical light.
The original 72-second paper-led trailer has not been converted to this kit yet.

## Choreographed use

`Light-to-Library` uses an explicit `phase` override to bind the original optical
shader to an authored scene timeline. The same shader surface travels from
import wave to the exposure rail without remounting. Tile arrival, counts,
progress, completion, selection, camera focus, slider drag/release and finite
completion feedback share event times. It is an authored commercial transition,
not measured import/render latency. `App-Elements-Study` remains the isolated
component reference.


## Complete film revision

The current 120-second `Looksy-Trailer` uses this kit throughout. Its native
curve sampler lives in `curve-sampling.ts`; the film drives the actual media
aperture with native wave equations. Separate completion flashes were removed
from the complete film. Original studies remain for component provenance.
Controls are browser adapters and the workspace states are authored animation.
