# Looksy optical motion — reel direction

Exploration, 2026-10-23. This extends the existing campaign studio. The rendered
12-second film is a motion study using existing generated source imagery. It
contains no current app footage and makes no editing, performance, or agent claims.

## Visual direction

Use the current Theme.qml identity: #050505 graphite, #F3F3F3 paper,
#FF3B30 red, #30D158 green, #3282FF blue. Bold neutral grotesk typography,
sharp photographic crops, generous empty space, restrained technical labels.
The study uses system Arial Bold as a prototype font; use a properly licensed
brand typeface or an approved open font for the production series.

Type enters through baseline masks with 160–480 ms staggered motion. Frames
slide and settle with strong deceleration. Match transitions to creative actions:
a crop becomes a frame; a frame becomes a carousel panel; a panel becomes a
timeline shot. RGB appears briefly at decisive beats. Allow finished work to hold.

## First three production reels

| Reel | Hook | Real footage | Length |
| --- | --- | --- | --- |
| Make it yours | Open on the finished work; “YOUR NEXT IDEA.” | A striking photo, a real layer composition, carousel boards, timeline and exported results from the current app | 20–24 s |
| One photo. Your direction. | Before/after fills the screen immediately | Genuine source, two intentional adjustments, editable layer detail, actual exported result | 15–18 s |
| One shoot. More possibilities. | “ONE SHOOT.” lands on the beat | One coherent source set becoming a photo, carousel, and short video, using genuine completed workflows | 24–30 s |

Follow with focused reels for culling/batch editing, masks and layers, color,
typography, carousel design, timeline motion/audio, and the local agent. Choose
claims from the demonstrated session; film the agent reel after a successful
ordinary run, creator revision, save, export, and reopen.

## Flagship 24-second storyboard

| Time | Picture | Copy and motion |
| --- | --- | --- |
| 0–2 | Best genuine finished result full frame | “YOUR NEXT IDEA.” Three masked word entries; first impact on frame one |
| 2–6 | Photo result → matched app close-up → visible adjustment | “SHAPE THE LOOK.” Crop brackets become the actual canvas framing |
| 6–10 | Real layer composition and visible typography change | “BUILD THE DETAILS.” Type settles beside readable app detail |
| 10–14 | Real carousel boards and exported panels | “TELL THE STORY.” Panels advance with restrained parallax |
| 14–19 | Real timeline action, then clean rendered playback | “SET IT IN MOTION.” Timeline playhead motivates the transition |
| 19–22 | Finished deliverables, with room to see each | “MAKE IT YOURS.” Energy drops slightly so the work can register |
| 22–24 | Supplied Looksy wordmark | Brief RGB signature, “Follow the build.” |

This is a shot plan, not a claim that these captures have been obtained.

## Production stack

- Core recommendation: official Remotion Agent Skills and a separate editable
  React video project. Use frame-driven animation, reusable compositions,
  per-shot crop paths, audio cues, hook variants, and local rendering.
  https://www.remotion.dev/docs/ai/skills
- Avoid starting with the old Remotion documentation MCP; it is deprecated.
  https://www.remotion.dev/docs/ai/mcp
- Capture real Looksy interactions through the available computer-use tools;
  record continuous high-resolution footage with a dedicated recorder. Screen
  Studio is an optional Mac capture tool with zoom, cursor and framing controls.
  https://screen.studio/
- Keep FFmpeg for encoding, thumbnails, audio measurements and delivery checks.
  It and ffprobe are already installed locally.
- Figma is an optional storyboard/style-frame workspace. Its plugin was found
  in the directory but was not connected during this exploration.
- Runway is optional for abstract interstitials or source imagery. Its plugin was
  found but not connected. Generated footage must not stand in for real app UI.
- DaVinci Resolve/Fusion is an optional finishing environment for more involved
  compositing, color and sound. Direct integration has not been verified here.
  https://www.blackmagicdesign.com/products/davinciresolve/fusion
- A future Looksy reel-production skill could preserve approved brand tokens,
  capture recipes, animation timing, audio conventions and review steps. That
  would be custom work, not an existing installed skill.

## Quality bar

1. Start with a distinctive completed creative project. Capture each demonstrated
   workflow on the current build with the input and real exported result intact.
2. Record high-resolution native footage and crop around one visible action per
   shot. Enlarge relevant UI before recording; a whole desktop shrunk into 9:16
   rarely communicates well. Keep clean captures for compositing flexibility.
3. Design native vertical compositions at 1080×1920. Render graphics at 60 fps
   when useful, matching footage cadence deliberately rather than duplicating
   low-frame-rate recordings and calling them smoother.
4. Use 2–6 words per headline, purposeful acceleration, short transition accents,
   longer readable holds, and a consistent transition vocabulary.
5. Cut to an intentional rhythm. Add sparse tactile clicks, short air movements,
   restrained impacts, and a resolved ending. Keep the story clear with sound off.
6. Review SDR Rec.709 color, tiny text, edge clipping, frame cadence, captions,
   audio peaks and the encoded file at phone size. Check the current Instagram
   placement preview for overlays and cover crops rather than assuming fixed
   safe-area pixels.
7. Deliver editable source, clean H.264/AAC MP4, a high-quality master, cover
   image, captions when spoken audio exists, source notes, and three hook variants.

## Motion study

Render with:

```sh
marketing/.venv/bin/python marketing/concepts/optical-motion/render_study.py
```

Outputs: marketing/renders/optical-motion/. The existing sample-free optical
soundbed is reused as temporary music. This is a coded local prototype; it does
not require installing Remotion or connecting any external service. Generated
outputs remain ignored by Git.
