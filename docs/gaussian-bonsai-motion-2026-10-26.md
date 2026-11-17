# Bonsai composition over a wider camera move

The current follow-through uses the 255-photo Bonsai production reconstruction.
The sparse private SUV capture remains a difficult-input diagnostic rather than
the primary benchmark for finishing quality. Its reconstruction failure is
retained in the earlier report; this change does not turn it into a pass.

Evidence root: `/private/tmp/looksy-bonsai-motion-v1`.

## Frozen scene and inspected motion

The separate `staged/project` retains the 830,842-Gaussian artifact, copper OBJ
torus and chrome sphere, their transforms/materials and support anchors. The
camera path increases from 0.5 seconds / 1.5 degrees to **3 seconds / 12 degrees**
at 24 fps. The output remains 960x639 with 64 aperture samples, 128 lighting
samples, received shadows, probe reflections/diffuse illumination, shadowed
haze, tone and halation. No training or source photograph is changed.

`same-start-audit.json` verifies the identical artifact, starting camera,
composition and anchors. The new starting still has exactly the same decoded
RGB as the prior beauty shot. PNG file bytes differ, so file identity is not
claimed across those two saves.

The staging driver now supports:

```sh
build/dev-macos-debug/tests/gaussian_scene_staging_driver --inspect-path PROJECT PLAN_JSON NEW_OUTPUT
```

It validates and loads the actual saved path, freezes its resolved composition,
writes the planned sample cameras before rendering, then renders frames 0, 36
and 71 at their actual export times (`index / 24`). It publishes a completion
receipt only after all three images save successfully. This does not fabricate
an export receipt or substitute independently authored cameras for the path.

All three images were inspected. The torus and sphere retain their placement and
framing through the wider move; no sampled-view contact pop was observed. Render
times are 32.69, 31.08 and 30.46 seconds. These are descriptive offline timings,
not an isolated performance comparison or realtime qualification.

`motion-projection-audit.json` independently projects both object centers and
authored support anchors at those views; all stay inside the frame. The sphere's
signed support-plane error is -2.35e-8 scene units. Transforming all 1,536 authored
torus vertices gives a minimum signed plane distance of -2.44e-8 scene units.
These are checks against approximate picked planes, not recovered collision
geometry or a measured photographic contact reference.

## Combined spatial-lighting experiment

`spatial-plan.json` changes both reflection and diffuse illumination from probe
to spatial rays, retaining the other beauty settings. The exploratory render
was stopped after **9 minutes 48 seconds** with no completed first image. The
owned process was identified by PID and executable before termination; inputs
and the separately inspected probe composition were preserved.

`spatial-trial-stop.json` records cancellation, not a completed benchmark or a
photographic quality failure. Combined spatial lighting with this lens/haze
recipe remains unqualified. The known probe composition is used for the motion
export; sampling settings are not reduced to complete the spatial trial.

## Native checks

All six focused native scene, material, object, saved-view, camera-path and film
tests pass in 11.57 seconds (`focused-regression.log`). The earlier sandbox run
could not access Metal for the material test; the full native rerun passes.

The packaged debug app, launched through LaunchServices in isolated proof state,
loads the separate project, renders it and exposes the object/haze controls in
9.49 seconds. Its saved camera path retains three seconds and 24 fps.
`packaged-scene/scene-objects.png` was inspected. The window is exposed but
inactive; this does not establish active-window input latency or automatically
recall the saved beauty camera.

## Full sequence

The production exporter completes all **72 frames**, retaining 960x639 pixels,
24 fps, three seconds and the frozen camera/composition settings. Wall time is
2,476.00 seconds (41 minutes 16 seconds), with 779,026,432-byte maximum RSS and
1,708,344,712-byte peak footprint. This remains an offline result; these are
descriptive session measurements, not an isolated performance experiment.

`sequence-verification.json` decodes every PNG and verifies all production file
and stored-RGB hashes. Frames **0, 36 and 71** match their separate native
inspection renders exactly, with maximum channel error zero. The other 69 frames
were not independently rerendered. Both original project identities and the
pinned renderer/tool/recipe identities remain unchanged after export.

Full-quality export frames 18 and 54 were also visually inspected. A fixed
eight-pixel-grid consecutive-image diagnostic has its largest mean change at
frame 38 (3.087 codes), near the middle of the eased move. That frame and its
neighboring diagnostic values were inspected. This descriptive check has no
photographic/flicker acceptance threshold and does not establish continuous
foreground playback quality.

`bonsai-motion-preview.mp4` is a derived H.264 CRF-14 viewing copy. FFprobe verifies
72 decoded frames, 24 fps and three seconds. One black row pads the 960x639 master
to 960x640 for 4:2:0 compatibility; the original PNG pixels are retained in the
master. This viewing copy is not a replacement for the lossless sequence.

## Workspace deliverable and remaining scope

The editable project, original-resolution PNG master, opening/ending stills,
viewing copy, native app capture and verification receipts are copied to:

`build/scene-deliverables/bonsai-motion-2026-10-26/`

That directory is ignored by Git. `README.md` explains reopening the project and
recalling the two saved shots; `manifest.json` hashes its 88 delivered files.
Project resources were checked byte-for-byte after copying. In particular, the
workspace deliverable does not depend on retaining the temporary copy of the
trained model for normal scene rendering.

This closes the wider **three-second / twelve-degree probe-lit composition**
case at the recorded settings. Matched photographic contact/material comparisons,
combined spatial/lens convergence and performance, sustained active-window
interaction, broader capture qualification and actual C4D/Octane interoperability
remain open. It does not establish full reference-video parity.
