# Campaign 01 / I wanted to make this.

The primary execution is a finished 32-second first-person letter film. It uses actual work, a genuine historical native interface recording, and text as Brady’s voice. It does not require a founder performance to be complete. No synthetic face or voice appears.

- `images/01-i-wanted-to-make-this.png` — 1080×1350 hero image.
- Matching `.svg` — editable typography, graphic marks and embedded image elements, with fonts embedded.
- `carousel/01-slide.png` through `06-slide.png` — six finished 1080×1350 Instagram slides, each with matching editable SVG.
- `boards/01-storyboard.png` and `.svg` — detailed eight-shot storyboard, 2520×1820.
- `video/01-builder-letter.mp4` — 32 seconds, 1080×1920, 30 fps, original sample-free audio, open text.
- `video/01-hook-bring-a-real-project.mp4` and `01-hook-what-should-this-handle.mp4` — two alternative four-second openings with the same 28-second body. These are hook tests, not three separate campaign ideas.
- `audio/01-original-letter-bed.wav` — synthesized original tonal/paper-tick bed; no imported samples or stock music.
- `letter.srt` — text transcript timed to the film; there is no spoken voiceover.
- `self-shoot-director.md` — a separate optional founder performance version. The letter film remains finished without it.

The photograph’s before/chosen comparison uses native output files exported 16 November 2026. The chosen file is specifically the corrected `...full-resolution-2.png`, not the failed earlier finish attempt. The comparison is editorial presentation of actual exports, not a simulated application interaction. The boat/tone-curve inserts are genuine native action footage from the 25 October 2026 archive; that date remains visible. The FIRST LIGHT board is an actual native board export dated 16 November 2026. These different projects are a builder’s montage, not a claim that the archived boat edit produced the current mountain image.

All source copies and their hashes are in `sources/` and `provenance.json`. The photograph colors are not retouched by the campaign renderer. It fits/crops images within editorial layouts and uses a labeled comparison wipe. Hand-style annotations, underlines, RGB marks, framing and motion are campaign graphic design.

## Suggested post caption

I wanted to make this.

I’m Brady. I built LOOKSi, a photo and video editor, and I want a better place to carry an idea through the work. A frame you want to keep. A look you want to shape. A story that needs somewhere to go.

For professional work, and for the work you make just because.

What would you want to test? Tell me what you make and the step that gets in the way.

The film includes genuine native app archive footage from 25 Oct 2026 and native image/board exports from 16 Nov 2026.

## Necessary central-copy proposals

The central `campaigns.json` is intentionally unchanged. Proposed updates:

1. Set campaign01 preferred video format to **finished text-led builder letter**, duration 32 seconds. Founder filming is optional for a future performed version, not a blocker for this film.
2. Use this folder’s eight-shot letter structure and six rendered slide copy rather than pretending the original 40 second founder master has been filmed.
3. Mark the current photo/board still exports as 16 Nov 2026 and native UI action insert as archived 25 Oct 2026. Do not label it a current product recording or a single continuous source-to-export run.
4. Keep the future face-to-camera script as a separate optional production project. No placeholder founder panel belongs in the primary letter film.

## Build and review

Run from the repository root:

`marketing/.venv/bin/python marketing/campaigns/looksi-launch-2026-11-16/v2/campaign01/tools/build.py`

The script regenerates original layouts, SVGs, sound and three MP4 variants. The archive excerpt is a copied/transcoded source in this folder. The font source paths refer to existing OFL fonts in the repository. A normal-zoom visual contact sheet and phone-size previews are in `qa/`. Final QA covers full decoded video, stream dimensions/frame rate/duration, static design, phone readability and genuine source identity; it does not establish a new current native video recording.
