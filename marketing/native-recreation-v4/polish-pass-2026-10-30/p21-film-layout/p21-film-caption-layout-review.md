# P21 Film caption and layout review

## Result

Prepared a guarded, unapplied native edit proposal for the standard Film caption phases. It addresses mismatched line wrapping and vertical rhythm, restores the missing default footer pair in phases 1–4, and corrects a phase 3 font-size mismatch. It preserves the accepted footage, timing, image treatment, keyframes, effects, audio, special DeliveryProof phase, and existing timeline lanes.

The proposal is in [p21-film-caption-layout.commands-fragment.json](p21-film-caption-layout.commands-fragment.json). Its builder [build_p21_film_layout_plan.py](build_p21_film_layout_plan.py) verifies the P18 planning snapshot, source hashes, target clip state, keyframe hashes, landscape overrides, and empty footer intervals before producing the fragment. It is not an execution script and made no app or project calls.

## Findings from the rendered comparison

The source `Caption` component uses a 470 px landscape text column, a fixed 91 px heading, and 33 px body text. The native title clips do not reproduce the browser's automatic wrapping, so native headings and body lines differ even when the copy is the same. At 199.5 s, for example, native P16 has a two-line heading while the source has four lines; the body consequently sits much too high. At 218.5 s, the source phase 3 heading and body occupy four lines each, while native P15 has a two-line heading and a three-line body. Native P18 has the same clip records as those P16 proof frames throughout 184–252 s.

The 218.5 s comparison also exposed phase 3's native heading at 76 px; source `Caption` uses 91 px for every landscape phase. The proposal changes that size and repositions the heading/body based on source text flow. The submitted centers are first-pass A/B targets because Qt and browser text metrics can differ.

Default footer copy is visible in the source during the normal caption phases, but no matching native footer clips cover 196–232 s. The plan adds exactly two native title clips to existing, visible, unlocked lanes with empty intervals in that span. It does not add tracks. The separate DeliveryProof phase at 232–240 s remains untouched.

The source line breaks represented in the plan are:

| Global interval | Heading | Body |
|---|---|---|
| 196–208 | `Build the` / `edit.` / `Keep it` / `moving.` | `Layer footage, titles and` / `audio. Trim a clip; ripple` / `the following shots into place.` |
| 208–218 | `Motion, with` / `precise` / `control.` | `Position, scale, opacity and` / `easing. The animation` / `follows your keyframes.` |
| 218–226 | `Build the` / `look.` / `Layer the` / `effects.` | `A native color grade and a` / `monochrome effect. The` / `same footage, visibly` / `transformed.` |
| 226–232 | `Picture` / `meets` / `sound.` | `Pull down the music and` / `hear the location. Shape` / `the mix alongside the edit.` |
| 240–244 | existing two-line heading | four body lines, including the existing source copy |
| 244–252 | existing two-line heading | `Landscape productions` / `and independently framed` / `vertical videos.` |

## Evidence and bounds

- P18 planning file: `native-projects/looksy-polish-pass18-library-base-fixed.json`, SHA-256 `d31b7c3e3e9101334a13774c3ed976ff8162a7303e040645d55fe37f05873419`, document hash `fab7d7b461d52e134aca1b29afaf49f8a5d778f20e284d4076c1910e653aa1a9`.
- P16 closed-render evidence covers only partial chunks; the P18 clip records match P16 over 184–252 s. This is not a completed native film render.
- P15 evidence at 218.5, 229, and 247.5 s is from an earlier snapshot; the audit records a P15/P18 difference at 232–240 s and does not use that sample as same-baseline proof.
- Full-resolution source frames are stored in `native-reference-png/`; selected labeled samples are summarized in `p21-source-reference-contact-sheet.png`.
- The plan requires a fresh native task and exact current document hash at application time. It requires render A/B at the listed frames and saved-readback checks before acceptance. No live edits were made here.
