# RAW detail scene visual review (P6)

Read-only review of saved native checkpoint `looksy-polish-pass6.json` at document hash `167bf753c47f812c4da8d16ab75607a51a54f4ada94b850e9cf9409ad696b423` and saved-file SHA-256 `43bc28e826c913e5ace1da497ced74e8fb617f668c997cb825f9aaff65e79263`.

No timeline or app mutation was made. The full render job `6cc3ee70-ee49-4911-8dca-82ed0135de1f` was left untouched.

## Matched-frame comparison

The native partial frame is `native-raw-pass6-progress/frame-global1337.png`. Its metadata records source window `video-000003.mp4`, decoded ordinal 15, inferred global frame 1337, and global time 44.5666667s.

I extracted reference frame 1337 from `/Users/bradyreiner/Downloads/landscape.mp4`. `ffprobe` reports 1920×1080, 30fps, 298 seconds. The exact reference and nearby reference frames are saved under `raw-pass6-reference-review/`; `footer-crop-reference-vs-p6-1337` inputs can be read as separate `reference-footer-crop.png` and `p6-native-footer-crop.png` files.

At 44.5667s, both reference and P6 show the lower instruction laid over the source/sharpen/noise badge row. This is a reproduced source collision. The proposed native polish moves the two alternating instruction/result captions into the existing left column; it does not remove the copy or readouts.

At the matched frame, the reference cursor lands inside the loupe grid on the white-outlined target cell. The P6 cursor appears about 160 pixels to the left of the grid on the underlying photo. Preserve the existing cursor track and inspect adjacent native frames before changing its path: the reference transition is moving, so one sampled mismatch is not enough to design a new trajectory.

P6's visible photo plate is about 806px wide in this frame versus about 960px in the reference, while the loupe grid is the same size and position. This reflects the prior `.84` right-column inset choice, not an engine defect. The unchanged outline clip `7165d277-b4ce-49ea-9f44-d5190a693d7f` is already 960px wide and sits near x=650–1610. The separate proposal `raw-photo-fullsize-rightcolumn-p6-proposal.json` reverses the `.84` affine photo mapping and adds only X=0.35 to the recovered gesture, restoring the reference-sized photo inside that outline. It is a proposal for a native A/B after the active P6 render completes; it does not weaken the revised four-line copy or change the grid.

## Exact bottom-row collision

All three readout labels are centered at y=0.8866111, with native pill backgrounds underneath:

| Clip | Text / role | Timeline | Current x |
|---|---|---:|---:|
| `c1cc47de-88dc-41f7-b635-7ca1ea04ebb0` | `960 × 960 SOURCE PIXELS` | 39.0–50.0 | 0.3603125 |
| `bf28394e-7283-4de5-8e58-606e26a06d1d` | `SHARPEN 32` | 43.5–50.0333 | 0.5390625 |
| `bc2952e8-5a5e-4be1-9cd6-e858ca22af7a` | `NOISE 24` | 43.4667–50.0333 | 0.6376042 |
| `eb42beff-b310-4af6-acdc-2ef6e9abd182` | Source badge background | 39.0–50.0 | 0.4359375 |
| `ba5721a6-f627-4468-b423-78fc6d160cd7` | Sharpen badge background | 39.0–50.0 | 0.5745833 |
| `ac176913-7e8c-4fa7-ad35-d63e0c7bf5b2` | Noise badge background | 39.0–50.0 | 0.6673958 |

The captions occupying the same lower row are:

| Clip | Copy | Timeline | Current position |
|---|---|---:|---:|
| `4e48de4a-52a3-4eea-a74d-75823a37c9e9` | `SELECT THE EDGE. LEAVE THE BACKGROUND.` | 44.1–48.5 | x=.35, y=.8847778 |
| `1d058b45-1e13-457b-a8c0-111a51aa04f0` | `STRAY PIXEL REMOVED. EDGE RETAINED.` | 48.5–49.8 | x=.35, y=.8847778 |

The P6 proposal is `raw-detail-status-reflow-p6-proposal.json`. It moves only those two captions to the established left text anchor x=.0427083, retaining their y, typography, wording, timing, and opacity. The badge labels, badge shapes, per-frame Sharpen/Noise values, photo, grid, and all track counts remain unchanged. The mono setup caption is about 472px wide in the native render; from the left anchor it clears the photo/footer group and fits in the space below the three-line body and above the existing bottom footer.

## Status sequence from 39–50s

- 39.0–41.633: `SHARPEN 0`; 39.0–41.667: `NOISE 0`; source-pixel label remains in the photo footer throughout 39–50.
- 41.633–43.5: readout steps progress through intermediate values using the existing short native title clips.
- 43.467 onward: `NOISE 24`; 43.5 onward: `SHARPEN 32`; their badges stay through 50.033.
- 44.1–48.5: setup sentence overlaps the footer row in the reference and P6.
- 45.0–48.4667: `SELECTION EDGE / ADD / 1 PX` appears in the loupe header; source/count samples remain in the loupe’s own lower readout band. Keep those in place.
- 48.5–49.5: `SELECTION EDGE / ERASE / 1 PX` replaces the add label. `STRAY PIXEL REMOVED. EDGE RETAINED.` takes the setup sentence’s lower position through 49.8, colliding with the same badges.

The two-caption left-column reflow preserves the cause/result sequence and the technical controls. No extra caption track or additional fade is needed: the authored captions already hand off at 48.5s.

## Motion/storyboard review points

Render native frames at 39.0, 39.2, 41.2, 44.0, 44.5667, 45.0, 48.4667, 48.5, 49.0, 49.8, and 50.0 after the root rebinds each proposal to a fresh readback. The key visual beats should be:

1. 39.0–41.2: photo grows and settles to the full right-side outline on the existing entrance timing.
2. 44.0: photo and footer controls established; grid still compact.
3. 44.5667: source cursor visibly targets the outlined pixel in the grid; status captions are legible at left.
4. 45.0: enlarged grid header says `ADD / 1 PX`, and the cursor settles on the corresponding outlined cell while the selected-pixel count holds at 25.
5. 48.4667→48.5: clean ADD-to-ERASE and setup-to-result handoff, with no double caption frame.
6. 49.0: result copy holds left; badge row remains grouped under the photo; count and target state agree.

No cursor keyframe or geometry edit is included because adjacent P6 native frames were not available in this partial render. The exact-frame comparison is enough to flag a target/selection mismatch for the full native A/B, but not to invent an unverified cursor trajectory.

## Execution boundary

The JSON is a proposal only. Before root applies it, read the current live document hash and both target clips again. Apply only if document hash and all listed main/landscape preconditions equal P6. Use native title setters; require the landscape overrides to reflect x=.0427083 afterward. If any guard differs, rebind from native readback rather than editing the saved project file.
