# P18 Library parity review: 62–92 seconds

## Scope and evidence

This review covers the Library chapter only: import grid 62–67, compare/cull 67–73, visual search 73–79, wedding gallery 79–86, and shared adjustment 86–92. Canvas starts at 92 seconds; this report does not cover Canvas.

The audited snapshot is P16, document hash `438df6c817d17155464f532f50dffb8cd718718fa4a86c83615e3fa1b342f24a`, raw SHA-256 `cf34f9c4e035ceb86d5ad0e9c3a5ea6d1c3f00d97b7e21175d383939fb21f26c`. The matching original reference movie is `/Users/bradyreiner/Downloads/landscape.mp4`, SHA-256 `9ec7b8b422f96818e61a1f2d3987cc3422027537bb6280c671a689c88e3e8640`.

Native evidence is from exact decoded frames in already closed P16 render chunks, not from a complete encoded film or continuous-motion review:

| Time | Native frame | Native SHA-256 | Reference frame | Reference SHA-256 |
|---:|---|---|---|---|
| 65s | `full-p16-in-progress-frame-review/frame-001950.png` | `b58dfdde56d03ea27d9f3e016f61a9ddf76ead49b74a609aab480d18e2658440` | `full-p16-in-progress-frame-review/reference-001950.png` | `2b3f51213aae882a18376d30b57ae80de7ee26a71c482ee962f25826a8363835` |
| 70s | `full-p16-in-progress-frame-review/frame-002100.png` | `aed07071fbcbe9859032ee91b8039dbee86bb36ba2950ecf13ce70bfa7229a33` | `full-p16-in-progress-frame-review/reference-002100.png` | `1d0090ddbbdb834c387473fe5818ce6b4f7f34dc6181918d85bf406360d860e5` |
| 75s | `full-p16-in-progress-frame-review/frame-002250.png` | `9fd52eba7dfc39c985f8388f271cf9129340db5be91d98aa68ec167a71be3aeb` | `full-p16-in-progress-frame-review/reference-002250.png` | `031e36ec067bdb083171e41f78eb3b827743f900ffbd8ff6cc6a64b55a66d1ef` |
| 80s | `full-p16-in-progress-frame-review/frame-002400.png` | `5df6549b1f3ea9a2d37850f1f1e1ba5b46c01fc462912731ccffb32c9da275fc` | `full-p16-in-progress-frame-review/reference-002400.png` | `ddab32c97e47ab446d91d4746f683d3042bfd5d69cd68f30d0c68fa8daf0ba84` |
| 86s | `full-p16-in-progress-frame-review/frame-002580.png` | `975bd32c0e0327b71bf55af25bd60651cf51e5fc565e846e94a8f1cf49effb1b` | `library-reference-p16-review/reference-t86.png` | `af7509c7fa1ea8c3a90b8179940470bad3de803ea4a685eeedcdfa0405ea2b75` |
| 88s | `full-p16-in-progress-frame-review/frame-002640.png` | `60f4b3a50afec0e48719e2052953c48d4fafb30743cbe1b98b9865f221408770` | `library-reference-p16-review/reference-t88.png` | `99a1243b2f480a40db3e148c5e7340fd1266e9156b0a77dbeb6a2dad63dda828` |
| 90s | `full-p16-in-progress-frame-review/frame-002700.png` | `7263381d5665a0fa2e881fb6896d64fbf4399dab0a8a32ac2c60451ae970f3a1` | `library-reference-p16-review/reference-t90.png` | `042f4940209bcec0e59c305fbbed7fc7664f4ac3d7de8205e5601b28bd229ab6` |

Additional reference frames at 83, 85, 86, 88, 90, and 91 seconds were extracted read-only from the same source movie into `library-reference-p16-review/`. P16 native frames at 86, 88, and 90 seconds are now reviewed alongside their references. The 86-second reference SHA-256 is `af7509c7fa1ea8c3a90b8179940470bad3de803ea4a685eeedcdfa0405ea2b75`; the 88-second reference SHA-256 is `99a1243b2f480a40db3e148c5e7340fd1266e9156b0a77dbeb6a2dad63dda828`; and the 90-second reference SHA-256 is `042f4940209bcec0e59c305fbbed7fc7664f4ac3d7de8205e5601b28bd229ab6`. Native frames are exact decoded frames from closed P16 export chunks; they do not establish fluid-motion parity or final A/V approval.

The frame sidecars record request `f8e97299-e41d-458a-8af9-f6b004e5c5b4`, worker PID 98633, binary SHA-256 `7630662c8d35f37bc10a1b68983bdbae4a736eac7007b236f8e73c4ff2dfa8a3`, and the P16 document hash above. The sidecars say full render, A/V mux, and full-film review remain pending.

## Highest-impact defects

1. **62–67 import and 73–79 search:** in the native 65s frame, twelve dark card backplates cover most of the images. At 75s, the result images are scattered and translucent while lime outlines, headline, and search bar sit in unrelated places. The source shows a clean 4×3 grid in both beats, with four selected results outlined at 75s. Preserve the photos, their staggered entrance, opacity, and culling dim; repair their offset units, card backplate origins, and compositing order.
2. **67–73 compare:** the native 70s selected compare card/backplate blocks the mountain image and sits too high. The source shows three readable portrait cards with the selected middle image lifted and enlarged, filename/status strips below the image, and four compare/rate/pick/cull pills. Correct the quarter-canvas offsets and shape origins; preserve the selected scale and opacity animation.
3. **79–86 gallery:** native 80s collapses the three wedding photographs into overlapping translucent panels. The reference has three full-width landscape cards aligned at x≈85/676/1267 and y≈430, with “WEDDING / SELECTED FRAME” labels over the bottom edge. Correct the photo offsets and put the six label plate/text clips above the photo pixels.
4. **86–92 shared adjustment:** native 86/88/90 shows all six before/after layers compressed into an overlapping center stack; the oversized headline covers the chapter marker, and the three Applying/Editable labels have no visible backing plates. Reference frames show three horizontal 569×540 cards at x≈85/676/1266, y=430, dark label plates at the lower card edges, and the same applying-to-editable handoff at 89.383s. The text state changes correctly by 90s, so preserve the existing cut and wipe timing while correcting frame geometry, mask bounds, rotation, and z order.
5. **Copy silhouettes:** P16 uses no explicit wrapping equivalent to the narrow source caption column, so titles collide with the chapter label and body copy wraps into too few lines. Keep the accepted words/fonts, add line breaks, and lower the heading/body blocks to the source caption positions. Do not shorten the story or speed up its 30-second runtime.

## Geometry findings

The source artwork in `marketing/animated-trailer/src/creative-launch/Photo.tsx` uses a 1000×800 `Stage` at landscape left=650, top=120, scale=1.1. The native clip dimensions already include that scale: for example, a 232×173 tile becomes 255×190 pixels (`frameWidth=.1329167`, `frameHeight=.1762037` in 1920×1080); a 568.7×540 gallery card becomes 569×540 pixels (`.2961806×.5`). Do not scale those dimensions again.

`TimelineDocumentController::clipVideoFilter` maps `frameOffsetX/Y` to `targetWidth/Height * value * .25`. The source positions were stored as direct CSS-stage displacements. Multiply each applicable **base** offset and each authored `frameOffsetX/Y` key value by four; preserve key times/interpolation and every other channel. Example for the 62s grid, the current first row has X `[-.095, .0470833, .1891667, .33125]` and Y `-.2651389`. Correct native offsets are X `[-.38, .1883333, .7566667, 1.325]` and Y `-1.0605556`; the resulting image boxes are x≈650/923/1196/1468, y≈158, matching the source. The other two rows use current Y `-.03087963` and `.20337963`, also ×4. For the 62–67 entrance, 29–31 Y keys per photo and the 29–31 rotation/opacity channels remain otherwise unchanged.

`ensureTitleFrame` treats shape `titlePositionX/Y` as the **center** of the shape, while the source shape positions are CSS top-left coordinates. For card backplates, selection outlines, and button plates whose P16 positions equal the source top-left, change only the base position by adding half the existing shape size. Example: the first 62s card has top-left `(.3385417,.1467593)` and size `(.1329167,.2138889)`; the target center is `(.405,.2537037)`. This draws the 255×231 card plate at x=650,y=158, under the 255×190 photo. Multiply any associated animated `frameOffsetY` values by four as above. Keep fill, border, radius, opacity, and frame-scale curves unchanged.

For the three 67s comparison cards, keep current `.171875×.448148` photo dimensions and multiply their offsets by four. This yields 330×484 image rectangles at x≈650/1008/1365, y≈230/186/230; preserve the center card’s 1→1.08 scale and side card opacity/entrance curves. The full card plates are `.171875×.529630`; their source top-left positions need the same half-size center correction. Preserve the three outline clips and move them above the photos.

For the 79–86 cards, multiply X/Y base offsets and their animated Y channels by four. Target offsets are X `[-1.230556, 0, 1.230556]`, Y `.592593`, with existing `.2961806×.5` dimensions and zero rotation. This gives the source card boxes at x≈85/676/1267, y=430, width≈569, height=540.

For the 86–92 before/after cards, set all six clips to the same 79–86 card geometry: dimensions `.2961806×.5`, X offsets `[-1.230556,0,1.230556]` per column, Y `.592593`, rotation 0, scale 1. Keep every existing after-image `mask:right` key time and interpolation and map its value through the same normalized wipe progress into the corrected card rectangle. The old static mask rectangle follows the incorrect portrait geometry, so rebase its left/top/bottom bounds as well. Keep each after image over its matching before image; do not remove or flatten either clip.

## Guarded stable-ID layer plan

The P16 snapshot has free, existing graphics lanes throughout 62–92; no track creation or global reorder is needed. I checked candidate lanes G36–G53: they are top-level `graphics` video tracks, unlocked, visible, independent, opacity 1, group scale 1, ungrouped, with no intersecting clip in this window. They do contain clips elsewhere, so this plan reuses the lanes only in this chapter. Re-read each lane before use; require the same metadata and no interval overlap in the fresh execution snapshot.

Use G36–G47 (P16 track indices 61–72) as twelve reusable media lanes. Map the 62–67 and 73–79 twelve-photo clips in row-major order to G36–G47. Reuse G36–G38 for the three compare clips at 67–73 and the three wedding-gallery clips at 79–86. For 86–92, place the three before clips on G36–G38 and the three after clips on G39–G41; the after clips therefore composite over their matching before frames.

Use G48–G53 (P16 indices 73–78) as six reusable overlay lanes. Higher lane indices render above lower ones. Move only overlays that overlap photo pixels:

These are the exact P16 track IDs to bind after a fresh snapshot:

| Lane | Stable track ID | Lane | Stable track ID |
|---|---|---|---|
| G36 | `graphics-b9386fc8-eb72-437d-ae40-739f6ef82f73` | G45 | `graphics-4faa8966-cfc3-49cc-87ae-81d3917a01de` |
| G37 | `graphics-f12c6ee1-99a6-42d7-b08e-89beb5e71786` | G46 | `graphics-1b1ee3fb-43ad-43df-87d8-f141ebd4fbc8` |
| G38 | `graphics-dcc0e157-877f-4c9e-b0a4-25621b7572a9` | G47 | `graphics-b9d94b10-6929-4439-bdf0-757679c61579` |
| G39 | `graphics-da4059a4-4787-433d-bad7-1ea24dd5cfe0` | G48 | `graphics-93fbb91b-5d57-4f48-a77b-3b1c28523b6a` |
| G40 | `graphics-1484295e-992e-49a8-aaf7-81933eecde09` | G49 | `graphics-10c7e5fa-8ca7-4768-a1c5-8b3a90f20e12` |
| G41 | `graphics-f1ca486e-ae18-4b55-bf8d-434bc9c653b0` | G50 | `graphics-d77585ff-bc9d-42e5-9908-a2494476c9b1` |
| G42 | `graphics-4eeba747-ebb1-4635-9b46-111e033e5f16` | G51 | `graphics-8e70fbee-995f-47ca-b8aa-644b39cbe9f3` |
| G43 | `graphics-dbf28d3a-51aa-478c-a5cf-026fdbb53413` | G52 | `graphics-9f474c18-ddd3-4269-a30a-4b683d0bd699` |
| G44 | `graphics-8ddd7b2b-74a6-496a-a3f7-b93f9764d60b` | G53 | `graphics-e97923c3-2aa5-4c89-b853-c92fea49728c` |

The allocation uses 18 existing lanes total: 12 for the 12 simultaneous grid images and 6 for the maximum six simultaneous photo-overlapping overlays. The same lanes are reused at non-overlapping times in later beats; no per-clip tracks are added.

| Beat | Move above the media lanes | Keep below / unchanged |
|---|---|---|
| Compare 67–73 | Border-only shapes `c61d5cbd-55d2-4a7a-8b5c-7f1df1ab7975`, `1e7add49-8707-4a64-b814-d2d045080872`, `9aca83c4-d678-406e-a7f6-07a286022267` → G48–G50 | The three card fills, text labels, and bottom pills stay in their current lanes; the source photo ends before filename/status text begins. |
| Search 73–79 | Lime-stroke shapes `ace92d4c-0887-4dd0-8207-2d517b8fe3bb`, `d0af3de8-68c2-42dd-a4a4-b05426953d8f`, `bdfcf7d9-bd92-4393-a8cc-c6d8810b38b9`, `03eef68f-8d63-440a-866d-111bf8cde8f8` → G48–G51 | Keep the three `#ff17202b` dark-fill shapes below the images; they are backplates/dimming, not outlines. Search bar/tag and filename captions sit outside the image pixels. |
| Gallery 79–86 | Three bottom plate shapes `05fe922e-905a-4f47-9166-2cf20f4bc61f`, `9b35696c-ede5-49fc-b748-0abd689c2c3e`, `2c0762e0-41ad-4b8f-ad56-4ea9e0ec206f` → G48–G50; labels `a9f3501b-250f-4be1-90ea-0b3293a981c7`, `6b8fc43b-281e-4c5e-af79-6aa5e1d908df`, `de25d660-3024-4d71-8856-361da105aadf` → G51–G53 | Keep image source, start/end, opacity, timing, and card order. |
| Batch 86–92 | Three plate shapes `59a498af-cd8c-4cdb-9c9a-25b1dbd606ba`, `ab213889-6a49-4700-bcfb-f63867e543cd`, `6e3a4121-e7a8-4dad-a674-2ba2f3230001` → G48–G50; applying titles `2ee01fd6-590d-42cc-9903-95c485edd5e6`, `38c62147-d520-4e14-8c82-0cd831541984`, `992af829-06ba-437b-a05e-91af8c14eb48` → G51–G53; editable titles `cb877b86-0fd2-43a1-ba99-4bb8f98a25e0`, `b879bdf0-2cb1-4f5c-98db-bca6081c95e5`, `fbac9457-e916-4b8f-84f6-8f1b5d8dbdf2` reuse G51–G53 | Keep the existing applying→editable cut at 89.383s and all source/card content. |

Existing actions support this without a controller change: `timeline.action.setTimelineClipTiming` takes stable `clipId`, `trackId`, `startSeconds`, and `durationSeconds` and moves an existing visual clip to a compatible unlocked video lane. For these P16 stills playback rate is 1, trim start is 0, trim end equals duration, and there is no retime curve; passing the unchanged start/duration retains source marks. It rejects a destination overlap. Rebind target track IDs from the fresh task snapshot rather than assuming P16 lane identities survived later work. Moving a clip preserves its clip ID and clip-level animation/mask data. Do not move the dark search fills above the photos.

## Copy and shape adjustments

Explicit line breaks are needed because native title render does not inherit the source `Caption` column’s 470px CSS width:

| Time | Keep headline | Body line breaks |
|---|---|---|
| 62–67 | `Bring the\nwhole shoot.` | `Batch import your\nphotographs and footage.\nKeep the work together.` |
| 67–73 | `Keep the\nkeepers.` | `Compare nearby frames.\nRate, pick and cull before\nthe detailed edit.` |
| 73–79 | `Remember\nthe scene.\nFind the\nframe.` | `Visual AI search finds what\nis in the frame, beyond a\nfilename.` |
| 79–86 | `For the moments\nyou only get once.` | Keep the one-line source text. |
| 86–92 | `A consistent finish.\nAcross the gallery.` | Keep the one-line source text. |

Suggested center-Y readbacks for initial native A/B, based on the source CSS text blocks and native text-block center semantics: Caption headline/body ≈`.37`/`.55` for 62–67 and 67–73; ≈`.45`/`.71` for 73–79; GalleryDelivery headline/body ≈`.216`/`.36` for 79–92. These are placement targets to verify against the native frames, not pre-proven values. Preserve title copy, family, size, color, alignment, and timing. Existing GalleryDelivery bottom text is inside the card area, so move its title clips above the photos as listed.

For the 86–92 labels, source CSS puts each card at x=85/675.67/1266.33, y=430, width≈568.67, height=540, with label left=card+14 and bottom=18. The P16 label anchors and plate centers miss those card coordinates. The appendix sets the text left anchors to `(cardLeft+26)/1920`, the existing shape centers to `(cardLeft+14+shapeWidth/2)/1920`, and both vertical centers to the plate center at `.863148`. It keeps the existing plate dimensions/colors and text typography. The chapter marker, headline, and body centers are set to `.087`, `.216`, and `.36` to match the source layout.

## Execution guards and review points

- The executable, still-unbound P16 planning artifacts are [p18-library-confirmed-62-86.fragment.json](</Users/bradyreiner/projects/PersonalProjects/photo-editor/marketing/native-recreation-v4/polish-pass-2026-10-30/p18-library-confirmed-62-86.fragment.json>) and [p18-library-86-92.fragment.json](</Users/bradyreiner/projects/PersonalProjects/photo-editor/marketing/native-recreation-v4/polish-pass-2026-10-30/p18-library-86-92.fragment.json>). The first covers four beats (62–86): 187 actions across 104 guarded clips. The appendix covers the shared-adjustment beat: 45 actions across 18 guarded clips, retaining the three existing mask wipe sample timelines and moving only existing content to existing lanes. Static batch and lane checks pass against P16. Combine both fragments before binding if applying them as one P18 pass; the appendix expects the P16 preimage and the shared lane occupancy after the preceding 62–86 moves must be checked at the 86s half-open boundary.
- Bind P18 only after a fresh saved P17/P18 task snapshot. Require document, resource, and project hashes to match the intended base, and verify every listed stable ID still has the expected clip type, timing, source asset, base geometry, and animation counts/hashes before mutation.
- For each moved still, verify same clip ID, source asset, start/end, source in/out, playback rate, opacity/mask/effect/keyframe data; only `trackId` and explicitly named geometry/title fields should differ. Check the destination lane interval for no overlap before each move.
- Keep all scene start/end times and the 62–92 pacing unchanged. Do not replace the source photos, flatten the grid, or remove before/after clips.
- Render short native A/B ranges at 64.967–65.033, 69.967–70.033, 74.967–75.033, 79.967–80.033, 85.967–86.033, 89.967–90.033, plus both sides of 67/73/79/86/92 cuts. Compare card edges, title and body line silhouettes, overlay text, and image order to the matching source frames. Only then expand to a broader Library render.

Source references: `Photo.tsx` `LibraryChapter` lines 531–682 and `BuyerScenes.tsx` `GalleryDelivery` lines 153–218; `Kit.tsx` `Caption`/`Stage` lines 180–255. Native shape-center rendering is in `timeline_document_controller.cpp` `ensureTitleFrame` near line 50041, and quarter-canvas offsets are in `clipVideoFilter` near lines 55590–55601. The P16 frames and this analysis establish discrete-frame layout defects; they do not establish fluid-motion parity or completed-film approval.
