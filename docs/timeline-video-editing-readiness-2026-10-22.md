# Video editing readiness

## Outcome

A creator can import camera clips and music, cut a sequence, crop and transform
shots, stabilize a handheld shot, grade it, mix dialogue and music, review the
result, save/reopen, and export a finished video. Program playback, the paused
picture, reopened edits, and delivery must agree. Existing controls and feature
checklists are implementation evidence; they do not establish this outcome.

This is the immediate Timeline focus in the [editor roadmap](timeline-editor-roadmap.md).
The [direct animation workflow](timeline-animation-workflow-2026-10-22.md) records
the subsequent placement, video/text keyframe, speed, opacity, and fade work.
The [capability verification plan](capability-verification-plan.md) defines the
completion standard. Findings below are based on the current source inspection;
only the explicitly recorded checks have fresh runtime evidence.

## Current findings and order

| Priority | Area | Current source evidence | Work needed to accept it |
| --- | --- | --- | --- |
| 1 | Playback, render caching, resolution | Program has bounded processed-frame reuse, four-second composite cache ranges, proxies, Auto/Full/Half/Quarter modes, and measured adaptive detail. Composite caches use a fixed 1280-pixel longest-edge cap. Half/Quarter previously bypassed ready composites; the first correction below addresses that mismatch. | Qualify actual decoded playback, repeated scrubbing, effects, and cache-boundary crossings with representative 1080p and 4K media. Make cache resolution and preview-source selection understandable. Verify paused and playing composites agree for edits that depend on cache rendering. |
| 2 | Timeline usability | Track targeting, snapping, precision trims, linked A/V, insert/overwrite, ripple, and layout presets exist. Important quality/cache actions are in the View menu; their presence does not prove discoverability. | Walk through a real cut using pointer and keyboard. Check destination tracks, trim feedback, zoom/pan, linked selection, locked tracks, cancellation, and Undo. Keep picture and sound feedback responsive while editing. Decide UI changes from observed friction. |
| 3 | Crop and transform | The Motion inspector offers Fit/Fill/Stretch under “Crop mode,” scale, offsets, rotation, and keyframes. Program now has direct placement handles, recorded in the animation workflow. There is no explicit per-clip source crop rectangle. | Define a true source crop with direct monitor handles, aspect constraints, reset, and precise values. Keep crop distinct from sequence aspect ratio and placement transforms. Prove the same edges and transforms in Program, cache, export, Undo, and reopened projects. |
| 4 | Music and audio | Typed music/dialogue/effects roles, waveforms, clip fades/gain, track mixing/routing, EQ, dynamics, cleanup, and editable ducking are implemented. | Prove adding independent music under dialogue, trimming to picture, fades, gain/pan automation, mute/solo, processors, and ducking through audible native playback and a measured exported mix. Check sync across seeks, cuts, rate changes, and render-cache boundaries. |
| 5 | Stabilization | Motion exposes cancellable two-pass analysis, smoothing/crop protection, rolling-shutter settings, preview artifacts, and full-resolution lossless delivery media. | Use actual handheld footage; inspect residual shake, edge reveal, crop, source-time mapping, and before/after. Check cancellation, stale analysis after edits, rebuild, save/reopen, audio sync, and export. Filter availability and artifact readiness must produce actionable states. |
| 6 | Color and effects | Shared photo-engine grades, camera input transforms, creative CUBEs, adjustment clips, keyframes, scopes, and comparisons exist. Live preview and delivery use several different routes. Spatial-grade delivery explicitly rejects some source-layer/overlap/effect combinations. | Compare decoded Program/cache/export frames with named tolerances. Check camera transform, creative look, adjustment ordering, spatial grades, animation, stabilization, and overlays together. Report unsupported combinations before a user invests in the edit; measure frame time and memory on the accepted combinations. |

Owners are `timeline_playback_controller.cpp`, `timeline_document_controller.cpp`,
`TimelineView.qml`, `TimelineEffectsPanel.qml`, `TimelineAudioMixer.qml`, and
`timeline_audio_output.cpp`. The [shared color integration plan](timeline-shared-color-engine-integration.md)
describes the remaining renderer and color-fidelity boundaries in more detail.

## Confirmed gaps and limits

Items 1-6 are source-backed gaps. Item 7 is an open runtime qualification gate.
The cache correction below is implemented; these items remain open.

1. **True per-clip crop.** Fit/Fill/Stretch and placement transforms do not supply
   editable source crop edges. The timeline API has no explicit crop rectangle.
2. **Live audio processor coverage.** Track settings and FFmpeg delivery include
   limiter, gate, de-esser, high-pass, and low-pass processing. `TimelineAudioOutput`
   exposes EQ, compression, cleanup, and duck gain, but no properties or DSP for
   those five processors. The Program audio-layer bindings do not route them.
   Users therefore cannot audition these settings through the ordinary live
   path. A completed render cache can contain the exported processing.
3. **Positive audio gain in Program.** `TimelineAudioOutput::setVolume()` accepts
   gains up to 4.0, but both `TimelinePlaybackController::updateAudioVolume()` and
   `TimelineView.programAudioVolume()` clamp the combined clip/track/fade gain to
   1.0. A positive track boost produced by loudness normalization is discarded
   before reaching DSP. Track deserialization also clamps gain to a maximum of
   0 dB, so positive normalization gains do not survive reopen. The clip Gain
   control itself currently allows attenuation only, from -36 to 0 dB.
4. **Processor automation in live audio.** The clip projection evaluates track
   gain and pan at the playhead, but publishes EQ and compressor values from the
   static track fields. Export evaluates processor automation per segment.
   Broader processor controls and automation need one consistent playback path.
5. **Spatial grade combinations in delivery.** `frameStreamGradeClipIds()` rejects
   source spatial grades with stabilization, camera input transforms, creative
   LUTs, ordinary visual effects, transitions, changed geometry/opacity, or an
   overlapping picture layer. It also rejects some adjustment-layer orderings,
   alpha delivery, and two-pass delivery. These are missing combinations in a
   common video-finishing workflow, even though each control exists separately.
6. **Independent render-cache quality.** Cache generation caps the longest edge
   at 1280. Half/Quarter describe monitor processing scale, not an independently
   selected cache encoding resolution. Full mode selects original media; Auto
   restores originals when paused. A complete quality policy must preserve the
   authored composite while explaining which source/resolution is displayed.
7. **Continuous cached playback and mixed-audio sync qualification.** Each
   four-second cache range is a separate media source; live audio uses separate
   players corrected when drift exceeds 45 ms. These mechanisms need measured
   native boundary and sync tests. They do not establish seamless or
   sample-accurate playback by themselves.

Address audible preview/export disagreement and missing crop before expanding
the effects catalog. Keep the unfinished color combinations visible in the
readiness assessment even when their rejection is correctly reported by export.

## First correction: reduced preview resolution can reuse composited cache

Previously `preferredSourceUrl()` returned original media immediately for Full,
Half, and Quarter. A ready composite could be used only by Auto during playback
or interaction. Selecting a lower resolution therefore bypassed the pre-rendered
effects/composite instead of reusing that work.

Half and Quarter now select a matching ready composite, including while paused,
and retain their requested Program output scale. An edit invalidates the range
and falls back to the original at that scale. Undo can restore the matching
cache. Missing caches also fall back. Full retains original media, and Auto's
existing adaptive/proxy policy remains in place. Delivery arguments, cache
encoding resolution, and saved document format are unchanged.

This removes a source-selection mismatch. It is not a measured claim about 4K
playback speed, seamless cache boundaries, audible mix quality, or complete
preview/export parity.

### Verification

- [Verification receipt](evidence/timeline-readiness-2026-10-22/verification.json)
  records source/binary hashes, the 15 passing checks, and incomplete UI proof.
- The Release app linked successfully after both product corrections.
- Before the fix, the new playback regression failed with
  `reduced-resolution preview should reuse the ready composite at the requested scale`.
- Rebuilt Release playback, frame-scheduling, and cache-inspection targets.
- The playback controller checks Half/Quarter cache selection and scaling,
  retaining the source across Play/Pause, edit invalidation, Undo restoration,
  missing-cache fallback, Full-original selection, and the existing Auto policy.
- Frame-scheduling coverage injects decoded frames at the real sink boundary to
  verify actual 320x180 and 160x90 Program images from a 640x360 output target.
  Cached composite colors must survive without reapplying the clip's monochrome
  effect or shot-match grade.
- Cache inspection and recovery checks protect managed cleanup and recovery
  behavior. These are focused controller/frame checks, not a native visual or
  audio review of a finished project.

## Broader workflow checks and UI correction

Fifteen distinct checks passed: playback, frame scheduling, cache inspection,
cache recovery, video color math, UI source contracts, precision trimming,
color-grade curves, CUBE LUTs, shared keyframe curves, keyframe coverage,
effect-keyframe rendering, recovery lifecycle, the complete timeline controller
suite, and the save/render workflow. The full controller suite exercises encoded
fixtures for mixed music/dialogue ducking, stabilization analysis and lossless
artifacts, framing, cache generation, and final picture/audio delivery. This is
fixture-level coverage, not natural-footage or audible native qualification.

Initial full-controller and save/render runs could not dispatch jobs because the
sandbox denied local PostgreSQL startup. Both passed with a temporary PostgreSQL
service and separate disposable databases. That service was stopped afterward.
This resolves the environment failure without treating it as an editing defect.

The UI check initially assumed Source was the default tab, while the product
opens Media. Its setup now selects Source explicitly and waits for header and
deferred-panel readiness. It also saves a failure screenshot and header geometry
when that check fails.

The check then exposed unequal Source/Media tab widths. Both tabs now request
equal layout widths, and the redundant expanding spacer is removed. The empty
layouts at 1180x768, 1440x900, and 1920x1080, Source empty state, and Media layout
checks passed after this correction. The 1440x900 captures were visually reviewed:
[before](evidence/timeline-readiness-2026-10-22/source-tabs-before.png) and
[after](evidence/timeline-readiness-2026-10-22/source-tabs-after.png).

**The complete UI/video check did not pass.** The sandbox run next failed to
display decoded Source video despite ready 640x426 source metadata. The host
retry encountered repeated 15-second offline-catalog helper timeouts and was
stopped; it does not establish decoder failure or valid playback performance.
The offscreen run also logged a delayed callback against a destroyed QML context
during workspace recreation. Native Source/Program decoding, teardown callbacks,
mixed audio, and sustained performance remain open. Continue with a native
display session and an isolated working catalog before changing decoder logic or
accepting timing measurements.

Commands:

```sh
cmake --build --preset build-release-macos --target timeline_playback_controller_test timeline_frame_scheduling_test timeline_cache_inspection_test -j 4
ctest --test-dir build/release-macos -R '^timeline_(playback_controller|frame_scheduling|cache_inspection|cache_recovery)_test$' --output-on-failure
```

## Integrated acceptance project

Use an isolated project containing landscape and portrait footage, one real
handheld shot, dialogue, music, an overlay/title, a technical input transform on
known camera footage, a creative grade, and an adjustment clip. Include enough
duration to cross multiple cache ranges and exercise a sequence gap.

1. Import and arrange shots plus independent dialogue/music tracks. Record the
   exact source identities, codecs, frame rates, dimensions, and build identity.
2. Trim, split, ripple, move linked A/V, undo/redo, and seek forward/backward.
   Check destinations and resulting pictures/sound directly.
3. Transform and reframe shots; test true crop once implemented. Stabilize the
   handheld shot, compare before/after, and verify timing and edges.
4. Grade clips, add an adjustment layer, and verify effects ordering. Review
   supported and rejected export combinations before rendering the whole edit.
5. Add music, fades, dialogue processing, and ducking. Listen to transitions and
   compare exported channel levels and sync with Program.
6. Run uncached, proxy, and cached playback at each quality. Capture real frame
   timestamps, processing/publication times, dropped frames, cache-boundary
   stalls, UI latency, process memory, and cache growth. Require changing decoded
   pictures; a moving playhead alone is insufficient.
7. Change one graded shot and check only affected ranges regenerate. Cancel and
   resume work, save, close, reopen, and verify authored state and media links.
8. Export and inspect the decoded output, audio, duration, framing, grade, and
   stabilization against Program. Repeat a longer edit before claiming long-form
   readiness.

Set performance acceptance thresholds from an identical-artifact baseline before
optimizing. Keep each optimization bounded and compare the same project, build
configuration, quality, and playback route. Record any missing native display,
GPU, decoder, or audio proof separately; focused passing tests do not close those
gates.
