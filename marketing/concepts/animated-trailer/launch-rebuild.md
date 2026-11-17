# Looksy launch film — rebuild after rejected cut

The 120-second `looksy-polished-trailer.mp4` was rejected by the user. It is a
reference for failure, not a quality-approved baseline. Technical encoding,
lint and playback evidence did not establish professional creative quality.

## Fixed brief

A fully animated portrait launch film, no voiceover, no character, dark native
Looksy identity, abstract vector light, additive RGB, caustic forms, expressive
2D motion with camera depth. Real source photography and moving Nepal footage.
Native-source controls and original identity, magnified enough to read. Complete
manual photo/design/video/carousel/delivery story first; optional local search
and an agent editing existing media later. The separate actual app recording is
outside this film. No invented universal formats or camera-RAW export claims.

## What changes structurally

No persistent corner logo/header, repeated heading/image/footer template,
small illustrative workstation, or endless single-photo pose interpolation.
Each shot gets an independently composed focal point. Typography participates
in the frame. Camera travel has a subject. Vector light forms the pictured
aperture, control, curve or artwork; it is not a transparent overlay on a slide.
An action, its result, and the next subject are connected in the shot design.

Keep the exact native palette and lockup. Use native platform sans for product
copy, locally licensed Manrope for the created campaign art, native mono for
values. Key information is readable at a 360px-wide phone preview. UI details
need macro shots, not miniature entire screenshots. Finished creative work must
look designed enough to sell the editor that made it.

## Shot plan / full scope retained

| Seconds | Picture action | Screen copy / visible proof |
| --- | --- | --- |
| 0–4 | Camera traverses a cropped vector creative desk into a mixed-media shoot | Looksy within the workspace / original source photography |
| 3.6–6 | Filled additive RGB material converges into the original logo period | Looksy / Photo. Design. Video. |
| 6–10 | Full-frame contact sheet; pick three originals, sync their exposure, follow the first | Batch import / rate & cull / sync edits |
| 10–18 | RAW channel plates register, five source-derived controls move; camera travels into the native tone curve | Native resolution / exposure, contrast, highlights, shadows, temperature / custom color engine |
| 18–22 | Blue color-range adjustment, source loupe, sharpening and noise reduction | Hue / saturation / luminance / detail |
| 22–26 | Zoom into the source pixels, paint, erase, undo and return to the photograph | Every pixel |
| 26–30 | Real local subject matte, refine/feather, local desaturation; original photo returns | Select. Refine. Mask. |
| 30–36 | Same photo changes aspect ratio into an editorial poster; independent type, subject and image planes separate and rejoin | Image layers / subject mask / editable type |
| 36–40 | Original orange leaves fill large editable text; native numeric typography fields change tracking | Editable type / text masks |
| 40–44 | Paint a custom stroke, undo, redo on the same poster | Paint / shapes / history |
| 44–48 | Same poster moves into effect amount/bypass and grain | Build the look |
| 48–70 | Poster reshapes into moving picture; camera follows title trim, motion/easing and effect response, then a full mixer and final film | Full 4K timeline / layered picture / keyframes / effects / real source waveform and meter data / output color options |
| 70–84 | Camera traverses five different boards, including video; linked accent change, elastic reorder, export | Photo + video carousel / linked styles / layout / preview |
| 84–92 | Photo, composed poster, film and carousel become a finished delivery suite | Full resolution / dimensions / output color |
| 92–102 | Content query becomes original source matches, detail review, then the same three selected originals | Local visual search / reuse descriptions / review matches |
| 102–116 | Selected batch, sample review and apply, followed by native edit layers/history and manual exposure revision/undo/redo | Your model. Your machine. / Still yours. |
| 116–124 | Finished shoot/design/film/carousel suite resolves into original identity and a deliberate hold | Photo. Design. Video. / Your entire creative workspace. |

## Creative gates (unproven until reviewed)

1. Four strong independent style frames, with legible typography and an authored
   material/lighting system. Their compositions must differ visibly from the old
   film. Evaluate at 1080px and 360px widths.
2. An opening sequence proves timing, silhouettes, material continuity and camera
   travel in actual playback. No approval inferred from a pretty still.
3. Complete animatic includes every manual discipline, then optional assistance;
   no short proof substituted for the full film.
4. Full picture pass: inspect beginnings/midpoints/landings; no collisions,
   unmotivated drift, looped idle movement, cut-off controls or stretched artwork.
5. Music chosen and edited around the film's phrasing. Sound reinforces selected
   operations. Technical loudness is separate from auditory/creative review.
6. Final professional review remains unproven until the whole result withstands
   the same explicit criteria as the user's references. No claim of professional
   sign-off from codec checks or a self-authored manifest.

Reference viewed: Adobe Daily Creative Challenge, linked by the user through
CraveFX: https://player.vimeo.com/video/343536999. Useful observations: large
changes of scale, silhouettes, overlapping planes and traversal through artwork.
Borrow those principles; do not copy its character artwork, palette or scenes.

## Current implementation and review scope

`src/launch/` is a complete replacement composition, registered as
`Looksy-Launch-Rebuild`: 1080 × 1920, 60 fps, 124 seconds. The rejected
120-second composition and encoded outputs are preserved.

The subject matte was computed locally using the app's pinned SAM2 ONNX models,
with trailer-authored box/point prompts. The mask contour and transparent
foreground are model-derived rather than a hand-drawn approximation. The
mask-to-poster handoff preserves the photograph's geometry while its canvas
changes aspect ratio. Typography, subject and base are genuinely separate
rendered planes.

Controls were checked against the QML sources: typography uses numeric fields;
blur uses a normalized percentage; the mixer uses 0.5 dB steps and paired meters;
RAW and color/detail controls use native-source styling. This is authored product
animation, not evidence of native Qt operations. Photo adjustment previews use
conventional local source-image transforms and are documented as illustrative,
not exported results of the bespoke Looksy engine.

Moving footage uses longer excerpts from the original 4K Downloads sources.
The source clips inspected here are Rec.709, not Sony log recordings. Log text
identifies input support options. Timeline waveform/meter data was measured from
actual source audio rather than decorative random curves. Portrait review media
is prepared at an appropriate proxy size; the movie itself is 1080 × 1920.

The produced electronic cue is Cat Walk by Arulo from Mixkit, used at native tempo
and pitch, with sparse operation clicks and camera moves. Native operation sounds
are placed against pictured actions. Loudness/decode validation is separate from
human music approval or professional creative sign-off.

Current targeted visual QA caught and corrected duplicate type planes, a clipped
search headline, footer text floating over the photo during the aspect-ratio
change, a static timeline composition, and invented typography/blur controls.
Full-film playback and encoded delivery checks remain separate gates. No claim
of independent professional review is made.

The complete revised review MP4 passed 7,440-frame decode at 60 fps, exact
124.000-second audio/video durations, stereo 48 kHz audio, measured -13.95 LUFS
and -0.91 dBTP. The original location recording is mixed at 62.8–66 seconds with
the same -6 to 0 dB / 0.5 dB-step automation shown by the mixer. The earlier
master's 100 ms audio overrun was corrected by capping mastering to the actual
video duration. Full playback uses a new, separate chaptered review page with
sound and MP4 download. These facts establish delivery integrity only.

## Current delivery

- Current master: `marketing/animated-trailer/out/looksy-launch-refined.mp4`
- Review player: http://127.0.0.1:8791/launch-refined.html
- Editable composition: `marketing/animated-trailer/src/launch/LaunchFilm.tsx`
- Current evidence: `out/launch-refined-verification.json` and `out/launch-refined-audio-mastering.json`

The delivery master was rendered from PNG frames with explicit BT.709 conversion,
then mastered with the original automated location audio. Full decode passed:
124.000 seconds, 7,440 frames, 1080 × 1920 / 60 fps, H.264, limited-range Rec.709,
stereo AAC 48 kHz, -13.95 LUFS / -0.91 dBTP. Browser playback loaded this exact
master, unmuted, with correct duration and no media error. The complete earlier
review revision reached the ending at normal speed. The new master changes
frame encoding/color handling, not the edited scene structure.

Creative feedback remains pending. No independent professional sign-off has
been obtained, and this project is not marked creatively complete.

## Motion and sound revision

The identity-to-library handoff now preserves the original full wordmark across
the cut. Photographs originate at its RGB period, take curved paths through
perspective and a restrained size overshoot, then become the working contact
sheet. RGB edging belongs to the arriving photo frames and collapses as they
settle; no loader movie is superimposed. Header and selection controls arrive
after the media. Selection no longer claims three picks before they exist.

A 3-second encoded comparison (`out/launch-inlet-review.html`) was played at
normal speed and reviewed at four points through the handoff. The entire 124s
composition is being rendered with this change; the previous full master is
preserved. This short comparison is not a replacement deliverable.

Measured stereo source peaks exposed a mixing inconsistency: interface click
-10.36 dBFS × 0.06 yielded roughly -34.80 dBFS, while classic click -3.45 dBFS ×
0.08 yielded -25.39 dBFS. Gains are now 0.22 and 0.10, respectively, placing
both near -23.5 dBFS before overall mastering. Camera sounds use 0.08 rather
than 0.035. No scene timing was blindly snapped to an inferred beat grid.

Audio tools can measure the mix, but this model does not receive audible input
from the review playback or AudioContent tool output. Human listening quality
remains unverified; measured levels do not establish musical taste or fit.

The integrated revision is now the complete review player's source. It retains
124.000 seconds / 7,440 frames / 1080 × 1920 / 60 fps / Rec.709, passed full
video/audio decode, and measures -13.91 LUFS / -1.28 dBTP after AAC encoding.
The pre-encode ceiling was lowered to -1.5 dBTP to retain at least -1 dBTP after
encoding. The previous complete master and the sound-only revision are retained.

## Completion audit after the integrated revision

| Requirement | Current evidence | Result |
| --- | --- | --- |
| Complete film with images, moving footage, music and an ending | Exact 124s MP4, all 7,440 frames decoded; complete chaptered player and editable full composition open | Delivery integrity established |
| Dark native identity, original wordmark, source-derived controls | App kit source map; rendered native controls; unchanged original brand assets | Authored visual fidelity reviewed |
| Fluid planar motion and camera depth | Logo/media curved depth handoff, mask-to-poster aspect change, separated real planes, production camera moves, linked board movement | Present and visually reviewed; overall taste remains reviewable |
| Manual photo, design, timeline, carousel and delivery before optional assistance | Full-film sequence source and chapter times; manual delivery ends 92s, then search and agent | Scope and ordering established |
| Local assistance retains original media and editable adjustments | Search originals, sample review, apply, layers/history, manual exposure revision and undo/redo animation | Demonstrated in authored trailer; not a claim of native execution proof |
| Electronic soundtrack and tactile action sounds | Complete licensed produced cue; measured click balancing; camera and original location sound coupled to pictured operations | Sound present and technically mastered |
| Professional musical fit and listening quality | AudioContent returned that this model does not support audio input; browser playback alone does not provide auditory perception | Unverified; needs human listening feedback |
| Alignment with the user's creative bar | Full new review cut available; the existing direction question is still awaiting an answer | Unverified; no approval inferred from silence |

No professional completion claim is made from this audit. The remaining musical
judgment cannot be supplied by numerical loudness checks.

Integrated-master browser check: exact current source
`http://127.0.0.1:8791/looksy-launch-integrated.mp4` reached 124/124 seconds at
1x, unmuted, without media errors. Temporary comparison loops were closed;
only the full review player and editable full Remotion composition remain.
Listening approval is still not established.

## Transition and pacing refinement requested by the user

The user reviewed `/Users/bradyreiner/Downloads/looksy-launch-integrated.mp4`,
called the cut pretty good, and requested further polish focused on transitions
and pacing. Its hash exactly matches the preserved integrated project master.
`out/approved-integrated-source/` preserves the source of that liked direction.
This pass retains the 124-second story, feature coverage, original art direction,
and soundtrack.

- At 22s, the detail loupe's exact crop and 155% scale continue into the pixel
  inspection. Controls leave before the move; the source does not reset.
- At 36s, the poster and text mask occupy adjacent canvases. One camera path
  crosses the sequence boundary with continuous position/velocity and restrained
  directional smear; typography is crisp again at the landing.
- At 48s, diffusion and grain stay on the finished poster as it reshapes into
  moving picture. The preceding controls fade before the next workspace arrives.
- At 70s, a short continuation of the actual final source-video tail remains
  underneath the native optical aperture, which reveals the carousel. Headline
  arrivals avoid being sliced by the reveal.
- At 116s, the edited portrait retains its exact position, dimensions and exposure
  across the cut, then moves into the closing suite while other pieces arrive.
  The final logo hold remains unchanged.

Exact cut-boundary stills were inspected for the photo, canvas, poster, timeline
and closing transitions. A three-second encoded camera-motion check was played
at normal speed and its settled frame inspected separately. The user-facing
output will remain the entire film rather than this short check.

The complete transition refinement passed full decode at 124.000 seconds,
7,440 frames, 1080 × 1920 / 60 fps, Rec.709 and stereo 48 kHz AAC. Final encoded
levels measure -13.91 LUFS / -1.33 dBTP. It is saved in Downloads as
`looksy-launch-refined.mp4`, with a hash matching the verified project master.
The original `looksy-launch-integrated.mp4` and its source snapshot are preserved.
The new review player includes chapter jumps just before each refined transition
and a Previous cut link for comparison.

Final playback check for this refinement: the current `looksy-launch-refined.mp4`
source reached exactly 124/124 seconds in the review player at normal speed,
unmuted, with no media error. Downloads and project copies have identical hashes.
The requested transition/pacing pass is delivered while preserving the direction
the user liked; no independent external professional sign-off is claimed.
