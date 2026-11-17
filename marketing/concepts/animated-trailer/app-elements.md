# Actual Looksy elements in the animated trailer

23 October 2026. The trailer's UI and motion materials must come from the app.
A matching dark palette alone is insufficient.

The first reusable kit is implemented at `../../animated-trailer/src/app-kit/`.
Open Studio composition **App-Elements-Study** to inspect the twelve-second
component proof. This is a source-derived browser adaptation, not live Qt
execution or a replacement full trailer.

## Mandatory source reuse

- **Sliders:** InspectorSlider/StudioAdjustmentSlider geometry, precision
  exposure response, exact numeric labels/units, neutral rail and ticks,
  modified fill, numeric/reset columns, pressed handle and release behavior.
  Show pointer contact, value movement, resulting image change and revision.
- **Loading:** the actual optical_light.frag caustic wavefront, interference
  activity ring and spectral progress bar. Preserve the app's state vocabulary
  and distinguish bounded progress from indeterminate work.
- **Completion:** the finite CompletionCaustic RGB/CMY registration mark and
  actual check icon; a completion cue follows the depicted result.
- **Brand:** original supplied logo/wordmark/orb assets, source aspect ratios,
  current Theme colors, app icon paths and platform-style interface typography.
- **Further controls:** derive curve editor, mask/layer stack, effect rows,
  keyframe editor, transport and carousel authoring controls from their actual
  components before finishing those scenes.

A scene can enlarge, rotate, separate and rejoin actual controls under the
commercial's camera. Its local control anatomy remains native. Optical light
may connect scenes, but photographed grading comparisons keep a neutral surround.

## Source and review trail

`../../animated-trailer/app-kit-source-map.json` records source hashes and every
adapter boundary. `npm run prepare-app-kit` regenerates theme tokens, exact icon
paths, original brand media and the shader interface adapter from the native
checkout. The manual slider and completion adapters identify their QML owners.

Browser/Qt font rendering and control ports still need direct visual comparison;
source-derived does not mean the current proof is a pixel-identical native
recording. The complete revised film remains to be assembled with this kit.

## Light as scene choreography

The component sheet is superseded as the motion example by **Light-to-Library**:
a fourteen-second portrait continuity study with the existing electronic review
cue. Source thumbnails arrive along the import field; their arrival drives the
visible progress and count. Once ready, selection carries one persistent image
into the editor. The same persistent shader surface compresses and moves into
the actual exposure rail; the camera follows the native slider. Release of the
slider triggers the finite completion mark after the authored edit settles.
The camera returns to the same photograph. No independent idle-loop field sits
next to the action.

The optical shader now accepts an explicit phase supplied by the scene. Keep
native source equations and local control geometry, but choreograph the larger
camera, clipping, light placement and phase around the pictured operation.
These timings illustrate a workflow; they do not measure processing performance.

Carry that principle across the full revised film:

| Operation                           | App motion material                                | Action and transition                                                                                                                                                                                     |
| ----------------------------------- | -------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Import → library → RAW              | Caustic wavefront + progress + completion          | Files arrive along the field, counts/progress follow arrival, selected source stays under the camera; the wave contracts into the first native adjustment rail. Implemented in the new study.             |
| Photo correction → composition      | Slider release + finite completion facets          | Commit the local adjustment, converge RGB facets at its actual control, carry that geometric focus into the selected layer/mask boundary, then reveal the editable composition.                           |
| Mask ready → type behind subject    | Activity ring + actual completion cue              | Resolve the pending mask state, inspect the resulting edge, then carry the camera between the actual subject and text planes. The loader ends when the depicted mask is ready.                            |
| Design → timeline                   | Optical line/rail + keyframe geometry              | Extend an authored edge into the timeline; the playhead takes over that line's motion. Beat accents coincide with actual trim/keyframe decisions and finished playback.                                   |
| Carousel creation → sequence review | Bounded progress + board guides                    | Populate each authored board, advance progress with that population, resolve completion, and follow the real panel boundaries through the coordinated design.                                             |
| AI search → selected images         | Local activity ring + result boundaries            | Ring/progress accompanies the depicted search, ends when results land, and the selected result stays under the camera into its explicit editing handoff. No model activity after the operation has ended. |
| Export → finished work → identity   | Delivery progress + completion + original RGB mark | Progress belongs to the deliverable; on completion the actual output lands, finite light resolves around it, and the camera follows finished work back to the Looksy identity.                            |

The rest of the master still needs these handoffs built. The isolated component
board and the new transition study are not the completed revised trailer.
