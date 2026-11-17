# Looksy workflow trailer

Composition: `Looksy-Launch-Workflows` · 1080 × 1920 · 60 fps · 154 seconds.
Editable source: `src/campaign/`, with retained `src/launch/` image-making,
production, search and agent scenes. Earlier complete cuts are preserved.

## Picture structure

| Time | Pictured work | Reason to care |
| --- | --- | --- |
| 0:00–0:08 | Finished photograph, layered campaign, moving image; contact-sheet handoff | Introduce Looksy immediately as a bespoke photo and video editor |
| 0:08–0:30 | Wedding import/cull; exposure, highlights, temperature, RGB curve; actual source-resolution lace crop; sync; gallery export | Deliver a consistent photographic set with fine control |
| 0:30–0:38 | Brush local skin details; local fill; retained mask; undo and redo | Retouch selected details while retaining the original |
| 0:38–1:04 | Source pixels; subject mask; editable poster; image-filled text; brush layer; adjustable effects | Develop a photograph or make a complete graphic in the same workspace |
| 1:04–1:22 | Layered production timeline; trim; motion curves; effect controls; actual source-driven audio meters | Build and mix a full production, with sRGB/Rec.709 and supported Sony log input options identified accurately |
| 1:22–1:36 | Landscape-to-portrait sequence; position adjustment; animated type and keyframes; TikTok/Reels/Shorts safe preview; 9:16 H.264 export | Make vertical work with the same editing control |
| 1:36–1:56 | One source photo becomes two linked boards; common crop; swipe preview; photo/design/video board sequence; numbered files | Create a continuous social carousel rather than unrelated slides |
| 1:56–2:02 | Gallery, artwork, 4K film, vertical video and carousel delivery queue | Finish different client and social deliverables locally |
| 2:02–2:14 | Search originals by remembered subject/context; review matches and details | Find existing media by its content |
| 2:14–2:28 | Optional local model; reviewed sample; selected batch; retained layers/history; manual exposure; undo/redo | Delegate repetitive work and keep edit control |
| 2:28–2:34 | Completed work and original Looksy wordmark | Close on the product and the work it enables |

No voiceover, character, illustrated desk, flat rainbow opening graphic, or
"Moving pictures" beauty interlude. The manual editor is established before
local search and the optional agent. No anti-Adobe or anti-open-source rhetoric.

## Source and evidence boundaries

- This is an authored animated product film, not the separate native screen-recording video.
- Native visual language comes from the app kit: dark Theme tokens, original
  wordmark/icons, inspector slider geometry, activity/completion graphics,
  typography fields and mixer geometry. Scene wipe geometry comes from the
  app-derived optical aperture; it carries the outgoing pictured work through
  the transition rather than adding a floating loader on top.
- Wedding images are licensed **JPEG photographs**, not camera RAW inputs.
  `campaign-stock-sources.json` records authors, source URLs, license, sizes and
  original hashes. RAW workflow claims describe the editor's supported workflow;
  the wedding color-response frames are conventional local illustrations.
  No invented camera file extensions or source RAW metadata are shown.
- Wedding spot-retouch pixels were produced by the app's real C++
  `run_ai_inpaint` Local CPU Fill path, built headlessly without ONNX and with
  fake-model mode unset. The source image, brush mask, original pixels and exact
  native runtime source hash are retained. `native-skin-retouch.json` confirms
  that unmasked pixels are identical. This does not qualify all retouch cases.
- The separate large-chair fill test produced unacceptable artifacts, and the
  direct Python runtime could not execute the pinned LaMa model. Neither result
  is used in the film. These tests do not establish the behavior of a running
  Qt app with its complete ONNX environment. No application source was changed.
- The lace crop contains exactly 920 × 960 original photograph pixels for the
  920 × 960 display region. It is not upscaled or generatively enhanced.
- The native "Seamless spread" template is a **two-board linked horizontal pair**.
  Both boards share one photo and crop position. The platform overlay is a
  preview aid and is never represented as part of export.
- The original user footage used for the 4K story is Rec.709. The supported log
  input choices are listed separately; this footage is not mislabeled as log.
- The optional agent changes existing originals through editable actions. Local
  inpainting reconstructs selected pixels; it is not the agent generating a new
  image. Model-supported local search/agent behavior is not claimed as an
  end-to-end runtime benchmark by an animated film.

## Sound and review

The user-liked electronic cue, “Cat Walk” by Arulo, is retained under its Mixkit
license. `arrange-campaign-score.py` inserts a 16-bar phrase at native 130 BPM and
pitch, with a 12 ms splice crossfade. Control clicks follow pictured operations;
air is reserved for camera moves. The production-mixer ambience starts at 78.8 s
and follows its pictured fader. The complete master targets -14 LUFS and a
pre-encode ceiling of -1.5 dBTP; verification measures the final AAC output.

`out/launch-workflows.html` provides the full film with sound and chapter seeks.
Visual review covers the opening, workflow transitions, native controls, video
source timing and linked-board continuity. Encoding checks establish delivery
integrity, not professional creative approval. Subjective music approval still
requires human listening; the available model interface cannot hear audio.

Reproduce:

```sh
python3 scripts/prepare-campaign-stock.py
python3 scripts/prepare-campaign-looks.py
python3 scripts/arrange-campaign-score.py
npm run lint
npx remotion render Looksy-Launch-Workflows out/looksy-launch-workflows-final.mp4 --port=3022 --concurrency=4 --crf=16 --image-format=png --color-space=bt709
python3 scripts/master-review-audio.py out/looksy-launch-workflows-final.mp4 --receipt out/campaign/audio-mastering.json --mixer-ambience public/audio/launch-mixer-ambience.wav --mixer-start 78.8 --true-peak=-1.5
python3 scripts/verify-campaign.py out/looksy-launch-workflows-final.mp4 --rec709 --max-true-peak=-1
```

Run mastering once on a fresh render; it preserves a pre-master copy. Re-running
mastering on an already mastered film would add the mixer ambience twice.

The delivered cut replaces the final six seconds after a closing-row balance fix.
`out/campaign/final-picture-patch.json` records the reviewed base and closing
hashes. The assembled picture is encoded at CRF14; its original mastered AAC
stream is copied unchanged. A fresh full render of the current source already
contains the corrected ending and does not require this patch step.
