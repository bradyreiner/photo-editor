# Looksy — After the shoot

An isolated Remotion 4.0.538 project. The desktop app and its build are independent
of this directory. Node 22.14.0 was used for this setup.

## Open

```sh
cd marketing/animated-trailer
npm ci
npm run prepare-media
python3 scripts/prepare-polished-score.py
npm run dev
```

The local source previews must exist at the paths in `media-manifest.json`.
`prepare-media` copies those review files locally; it does not touch the SSD or
Downloads originals. Review media, dependencies, and rendered output are ignored
by Git. Local font files and their license are included.

## Current review

**Looksy-Trailer** is the current complete 120-second portrait assembly at
1080 × 1920, 60 fps. It replaces the short component studies as the primary
review. The standalone player is `out/polished.html`; the MP4 is
`out/looksy-polished-trailer.mp4`. Serve it with `npm run review` and open
http://127.0.0.1:8791/polished.html. The player has sound, full seeking and thirteen
chapter buttons.

The same Nepal project develops through desk/import/culling, RAW/custom color,
mask/pixel refinement, composed graphic, editable effects, moving multitrack
video, a five-board photo/video carousel, full-resolution delivery, optional
local visual search, optional local-agent review/revision, and completed work.
No voiceover or character is used.

The optical wave equations now drive actual image apertures and panel reveals.
Source photos are clipped by those shapes; the persistent primary media frame
moves and changes dimensions through the film. The source separates into image,
mask and typography planes; the poster typography becomes a title; timeline
tracks unfold; carousel boards inherit the same project art and linked accent.
There is no full-screen loader clip placed over unrelated scenes.

Music is the produced **Minimal Techno 01 — Alejandro Magaña (A. M.) / Mixkit**.
The entire cue is fitted to 120 seconds with pitch-preserving tempo adjustment;
there is no repeated middle segment. Act-level gain changes, selective recorded
interface clicks and quiet camera transitions are authored in `src/film/Score.tsx`.
`polished-soundtrack-source.json` records the exact public sources and arrangement.
The raw stock audio stays local; the review film includes the mixed soundtrack.

This revision also removes separate completion flashes, resolves chapter-title
overlap, preserves poster proportions, follows native controls with the camera,
and samples the native tone-curve interpolation. See the
[polish review](../concepts/animated-trailer/polish-review.md).
The previous complete film is preserved in `out/polish-baseline/`.

`App-Elements-Study` and `Light-to-Library` remain earlier isolated proofs.
`After-the-Shoot` and `out/looksy-trailer-draft.mp4` retain the previous 72-second
paper-led cut. The earlier player is preserved as
`out/review-previous-72s.html`. They are not the current complete review.

```sh
npm run prepare-app-kit
python3 scripts/prepare-polished-score.py
npm run lint
npx remotion render Looksy-Trailer out/looksy-polished-trailer.mp4 --codec=h264 --crf=16 --concurrency=4 --port=3022
python3 scripts/master-review-audio.py out/looksy-polished-trailer.mp4
cp polished-player.html out/polished.html
npm run review
```

Use a free render port; another local service owns port 3001. Native app builds
and unrelated working-tree changes are independent of this marketing project.

## The story and motion

The [full creative treatment](../concepts/animated-trailer/creative-reset.md),
[capability map](../concepts/animated-trailer/capability-map.md) and
[app-element source contract](../concepts/animated-trailer/app-elements.md)
record the brief. `src/CohesiveTrailer.tsx` owns the complete film and soundtrack;
`src/film/` owns the shared scene geometry, artwork, photographic controls,
production/carousel/delivery, and assistance scenes.

The main photo is one persistent media frame rather than a set of separate
screenshots. Camera, geometry, masks and typography are driven by the same film
timeline. The native source kit supplies original logo assets, twelve icon paths,
sliders and optical/progress/completion elements. Browser adapters and
illustrative workspace elements are identified in the source map.

## Sources and proof scope

`media-manifest.json` records exact local review assets and hashes. Photo previews
are embedded camera JPEGs from the Nepal RAWs. Video assets are review posters and
silent 1280px excerpts from Nepal camera footage, including the new 4K sources. They are not Looksy exports.
Repeated thumbnails convey library volume. The mask is an authored SVG silhouette.

Search matches, local connection, agent drafts, and history in the motion test
are authored illustrative states. The exposure response is a graphic brightness
illustration. These are not recorded app execution or generated agent results.
The curves, layer stack, waveform, and timeline are graphic representations.

Before publication, complete the corresponding real app workflow: batch import,
local search, model connection, sample review, selected batch, revise/save/reopen/
undo, photo refinements, designed layers, video production, carousel, and inspected
exports. Qualify local inference for the actual selected configuration. Existing
local-model code and reversible-draft contracts are not proof of autonomous
professional completion or of every timeline combination.

Do not imply RAW export, every camera format, realtime 4K, universal network
absence, or training policies from an animated sequence. Match advertised size,
format, profile, and revision behavior to inspected real results. Keep production
proof notes outside the product film.

## Review and publication

Review the entire cut for motion, hierarchy, music fit and technical coverage.
The two-minute assembly is an animated product draft, not proof that the native
app executed the pictured workflow. The illustrated grade, mask, search matches,
agent actions and output settings must be paired with the real native exercise
and inspected outputs before publication. This project does not claim a
completed studio-quality approval or a qualified public application release.

Official Remotion guidance is installed project-locally in
`skills/remotion-best-practices`; it is not registered globally.

## License

Remotion: https://github.com/remotion-dev/remotion/blob/main/LICENSE.md
Manrope: `public/fonts/LICENSE-Manrope.txt`.
