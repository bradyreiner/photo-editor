# Timeline professional hardening

The follow-up [ten additional Timeline defects](timeline-ten-more-hardening-2026-10-22.md)
records the next hardening pass and its separate verification evidence.

This work follows the professional-readiness assessment and the request to
implement the tools and their UI using Luna agents at xhigh effort. The
[capability verification plan](capability-verification-plan.md) remains the
acceptance standard. Passing a source contract does not establish rendered
pixels, audible quality, sustained playback, or a complete professional release.

## Active implementation

| Workstream | Product behavior | Verification required |
| --- | --- | --- |
| Audio | Custom live limiter, gate, de-esser, high/low-pass, processor automation, positive clip/track gain, persisted settings, direct voiceover recording | Spectral/dynamics DSP checks, automation and save/reopen, encoded output comparison, native listening |
| Crop | Explicit source edges independent of Fit/Fill and placement, inspector values, direct monitor controls, reset, undo and persistence | Oriented/odd-sized decoded fixtures, cache identity, export pixels, actual pointer UI |
| Packaging | Collect a portable project and references with checksums; preserve originals; cancel and fail without publishing a partial archive | Real extracted-project reopen, collisions, missing dependencies, paths with spaces/Unicode, cancellation, integrity |
| Output inspection | Inspect the actual rendered streams and decode; report black/silent ranges as review findings | Encoded good/bad media, decoder failure, missing tools, cancellation, bounded process output |
| Interchange | Import/export an explicit OTIO cuts/gaps/trims/local-media subset with rational rates and a loss report requiring acceptance before changes | Unsupported-field reports, fractional rates, stale-plan guards, access checks, Undo and extracted save/reopen workflows |
| Keyboard | Context-aware Timeline remapping, effective-default conflict checks, atomic keymap import/export, no duplicate tool registrations | Runtime settings and malformed file checks, actual keyboard dispatch/focus, dialog interaction |
| Captions | Count grapheme clusters consistently in segmentation/reading-speed checks; normalize line separators | Combining accents, joined emoji, CJK, controller validation/segmentation and UI |
| Voiceover | Record a selected microphone into recoverable audio and insert it on a voiceover track | PCM file integrity, stop/cancel/device failure, atomic insert/undo, physical microphone/native audition |
| Recovery | Resolve a selected autosave by its durable identity even if a new journal contains identical content | Deterministic Save/recovery race, preserved canonical bytes, dirty recovered state and unrelated recent projects |
| Large projects | Reuse each clip's known owner track for tracking-readiness projection, retaining the complete model | Existing 10,000-clip complete-model, 1.5-second projection and memory/interaction budgets without relaxed limits |
| Offline local save | Skip catalog-helper retries when an explicit numeric loopback PostgreSQL endpoint definitely refuses connections; retain existing durable fallbacks | Refused and reachable endpoint tests, credential-free errors and the unchanged offline transform/save/reopen workflow |

Implementation and current test results must be reported separately. No existing
assertion should be removed, bypassed, or loosened to make this work appear green.
When an old source contract describes behavior intentionally changed here,
replace it with a test of the new behavior and retain the underlying invariant.

The live Timeline audio chain is custom preview DSP, not a parity implementation
of FFmpeg delivery filters. The limiter detects sample peaks and applies a
smoothed gain ceiling; it does not oversample or predict inter-sample true peaks.
The de-esser uses a fixed 3.8 kHz crossover with a 6.5 kHz band detector and
envelope-controlled high-frequency reduction. Gate, filters, EQ, and compressor
are likewise separate preview implementations. Their labels describe the live
behavior, and encoded-output agreement and native listening remain open gates.
The broad gain/pan-before-processors ordering is shared with export, and the
loudness-matched bypass trim now uses export's bounded compensation mapping.
Dialogue ducking still differs: preview applies its configured attenuation while
a dialogue clip is active, while export's sidechain responds to dialogue signal
level. Export processor automation also holds evaluated filter values within
each generated segment between keyframes; curved automation therefore does not
match the playhead-evaluated live path continuously. These differences, the
custom processor algorithms, and decoded-output listening remain open parity
work; bypass trim alignment alone is not an equivalence claim.

## Verified regression results

The final isolated regression pass completed **26/26 checks**. Each invocation
had a separate empty PostgreSQL database; the transform fixture still deliberately
uses its unreachable endpoint to exercise local fallback. Assertions and original
timeouts/performance thresholds were retained. The Release application and native
UI driver were rebuilt alongside the tests.

Coverage includes the complete Timeline controller, source crop/export pixels,
precision trims, shared/color/effect/title keyframes and renders, Save/render,
recovery and recovery lifecycle, audio DSP/WAV integrity, caption segmentation,
shortcut settings, playback/frame scheduling, archive integrity/extraction/reopen,
OTIO import/reopen and unsupported-field reporting, and local catalog refusal.
Archive fixtures retain Unicode source names and use the system extractor.

The full recovery fixture first exposed a real candidate-identity bug, then a
98-second projection bottleneck. After the fixes, the same 10,000-clip fixture
projected in **209 ms** against its existing 1,500 ms limit in the last run. Its
seek, selection, edit and Undo p95 values were 4, 1, 5 and 3 ms; retained process
growth was 441 MiB within the existing eight-snapshot history budget. This is a synthetic
model/interaction measurement, not decoded 4K or long-form playback qualification.

An earlier combined test run inherited queued jobs between fixtures and failed
the empty-queue Save assertion. It also exposed repeated offline helper waits in
the transform fixture. The final pass isolated databases and fixed the production
refusal path; it did not remove the Save assertion or raise the transform timeout.

Native UI results and exact source/binary identities are recorded separately in
the [verification receipt](evidence/timeline-professional-2026-10-22/verification.json).
The broader acceptance gates below remain required even when every focused test
passes.

Native testing also found defects that controller-only proof could not establish:
read-only crop percentage inputs, cropped-out secondary pixels leaking into Fit
letterboxes, crop aspect applied twice, image crop bindings feeding back into
their own source size, and a crop-only edit leaving the decoder's processing
snapshot unchanged. Their fixes require the final typing, paused-frame
publication and actual Program pixel checks; screenshots taken before current
frame publication or popup transition completion are not accepted evidence.

## Native UI result

The final Cocoa/Metal crop-and-tools route passed, including direct Program
dragging, one-step Undo, typed 12.0% entry, Reset, fresh decoded primary crop
pixels, live secondary video and still-image crops, and neutral Fit letterboxes.
The fixture primes video decoding with Play/Pause; it does not qualify cold
paused decoding for every imported format. The source-crop receipt records
sampled colors and the unchanged pixel assertions.

Delivery, OTIO and voiceover panels were opened through their actual workspace
actions, checked after their opening transitions completed, captured within the
window and dismissed. The final captures were visually reviewed:
[crop inspector](evidence/timeline-professional-2026-10-22/native-ui/source-crop-inspector.png),
[delivery](evidence/timeline-professional-2026-10-22/native-ui/project-delivery-tools.png),
[OTIO](evidence/timeline-professional-2026-10-22/native-ui/timeline-interchange.png),
[voiceover](evidence/timeline-professional-2026-10-22/native-ui/voiceover-controls.png).
Voiceover UI proof does not establish physical microphone capture or listening.

**The full animation/native-shortcut proof did not pass.** Its active-window
assertion remained intact. A direct host run and a LaunchServices run using a
copied Mach-O inside a signed test bundle both left the visible window inactive
and non-key. The latter reported bundle ID `com.looksy.timelineproof` but
`activated=0` and `active=0`. Native keyboard dispatch and the full animation
route remain unqualified; controller keymap validation/import/export tests do
not close that gate. The separate crop/tools pass is not presented as a full
animation/shortcut pass.

All proof processes exited and the exact owned temporary PostgreSQL cluster was
stopped after verification. No user database was stopped.

## Remaining professional scope

These requirements are not closed by the implementation table. Existing code is
ahead of several older unchecked feature lists; re-inspect each area before
adding another implementation.

- Preview/cache/export agreement for spatial grades combined with input
  transforms, LUTs, stabilization, transitions, alpha, overlapping layers and
  two-pass delivery.
- Sustained decoded 1080p/4K playback, scrubbing, cache-boundary continuity,
  independently selected cache resolution, and long-duration audio sync.
- A shared color-managed SDR/HDR monitoring and delivery contract, camera RAW
  control coverage, external reference comparisons and HDR metadata validation.
- Interchange beyond the implemented OTIO cuts/gaps/trims/local-media subset:
  wider OTIO schemas, AAF, EDL and XML/FCPXML, with explicit loss reports and
  golden round-trip projects.
- Professional edit speed and remapping coverage for every editor operation,
  including focus, trim, clipboard, multi-sequence and multi-display workflows.
- Sample-level audio editing, external microphone alignment, automation
  Read/Write/Touch/Latch modes, plugin latency compensation and advanced routing.
- Fractional frame rates/drop-frame timecode, mixed rates, variable-rate phone
  footage, sample-rate changes, anamorphic and orientation handling.
- Retiming quality on occlusion, fine detail, fast movement and motion blur,
  comparing live and cached preview against exported optical flow/frame blends.
- Multilingual caption versions, RTL shaping/font coverage, rendered layout and
  visual contrast against moving footage.
- Deeper editable graphics/templates and safe third-party effect/audio hosting.
- Shared projects, conflict-safe edit locking, review/version exchange and
  production permissions.
- Complete dependency packaging (including fonts subject to redistribution
  rights), optional media trimming with handles and fresh-machine archive proof.
- Long-project reliability, disk pressure, interrupted save/export, GPU/device
  recovery and crash-injection tests preserving acknowledged edits.
- Actual-render QC beyond decoding: expected duration/stream layout, freezes,
  captions, loudness, color tags, source completeness and reviewable findings.

## Integrated acceptance

1. Record exact build/source hashes and the identities/codecs/rates of real media.
2. Complete a short social edit and an interview with independent dialogue/music,
   crop, animation, captions, stabilization, grading and overlays.
3. Review uncached, proxy and cached pictures/sound, including cuts, seeks, gaps,
   speed changes and cache boundaries. Measure decoded frames and actual sync.
4. Save, reopen, undo/redo, relink, cancel background work and recover interrupted
   work without manual document repair.
5. Render, inspect and compare decoded pixels/audio with Program. Extract the
   archive elsewhere, make original paths unavailable and reopen the project.
6. Repeat with a longer multicamera project before claiming long-form readiness.

Native captures, audible review, external hardware and real-footage qualification
remain explicit gates when the local test environment cannot supply them.
