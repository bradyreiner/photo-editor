# Downloaded soundtrack candidates

Research date: 2026-10-30. These are ordinary public downloads from creator or licensed source pages; no signup, payment, or access-control workaround was used. Full MP3s are unmodified. The attached AAC excerpts are 15-second source cuts for audition, with no looping, time stretch, or level processing. I have not listened to these files, so this is a concrete audition pack, not a musical approval.

The target cut is 298 seconds. The fit notes below compare exact `ffprobe` duration against that runtime. The 5-second samples are useful for arrangement, but the full tracks should be auditioned before editing the trailer. The existing `EDMDetectionMode` should remain an A/B control until one complete cue is selected.

## Shortlist

| Candidate | Full track | Runtime difference | First audition | Source and license |
|---|---|---:|---|---|
| **Realizer — Kevin MacLeod** | [MP3](<downloads/public-candidates/Kevin MacLeod - Realizer.mp3>) · 310.152 s | +12.152 s | [opening](<auditions/realizer-opening-15s.m4a>) · [middle](<auditions/realizer-middle-15s.m4a>) · [ending](<auditions/realizer-ending-15s.m4a>) | [official track page](https://incompetech.com/music/royalty-free/index.html?Search=Search&isrc=USUAN1900024), CC BY 4.0 |
| **New Gods — HoliznaCC0** | [MP3](<downloads/public-candidates/HoliznaCC0 - New Gods.mp3>) · 315.828 s | +17.828 s | [opening](<auditions/new-gods-opening-15s.m4a>) · [middle](<auditions/new-gods-middle-15s.m4a>) · [ending](<auditions/new-gods-ending-15s.m4a>) | [FMA track page](https://freemusicarchive.org/music/holiznacc0/waves-of-nostalgia-2/new-gods/), CC0 1.0 |
| **Ethernight Club — Kevin MacLeod** | [MP3](<downloads/public-candidates/Kevin MacLeod - Ethernight Club.mp3>) · 305.711 s | +7.711 s | [opening](<auditions/ethernight-club-opening-15s.m4a>) · [middle](<auditions/ethernight-club-middle-15s.m4a>) · [ending](<auditions/ethernight-club-ending-15s.m4a>) | [official track page](https://incompetech.com/music/royalty-free/index.html?Search=Search&isrc=USUAN2100002), CC BY 4.0 |
| **Inspired — Kevin MacLeod** | [MP3](<downloads/public-candidates/Kevin MacLeod - Inspired.mp3>) · 286.067 s | −11.933 s | [opening](<auditions/inspired-opening-15s.m4a>) · [middle](<auditions/inspired-middle-15s.m4a>) · [ending](<auditions/inspired-ending-15s.m4a>) | [official track page](https://remote.incompetech.com/music/royalty-free/index.html?Search=Search&isrc=USUAN1600022), CC BY 4.0 |
| **Limit 70 — Kevin MacLeod** | [MP3](<downloads/public-candidates/Kevin MacLeod - Limit 70.mp3>) · 301.662 s | +3.662 s | [opening](<auditions/limit-70-opening-15s.m4a>) · [middle](<auditions/limit-70-middle-15s.m4a>) · [ending](<auditions/limit-70-ending-15s.m4a>) | [official track page](https://incompetech.com/music/royalty-free/index.html?Search=Search&isrc=USUAN1900042), CC BY 4.0 |

The Incompetech pages describe *Realizer* as bright, driving disco/electronic music at 125 BPM; its possible dance-floor character needs human review. *Ethernight Club* is 117 BPM and described as driving but mysterious, so its darker mood may conflict with the trailer. *Limit 70* is the closest runtime fit but is explicitly relaxed. *Inspired* is bright/uplifting and could fit a 12-second opening ident plus the complete track, if its actual arrangement supports that. *New Gods* is the closest creator-described synth-pop/synthwave option and CC0, but needs an 18-second natural trim or outro solution. Those are metadata-based observations, not listening conclusions.

For Incompetech tracks, use the official credit strings:

```text
"Realizer" Kevin MacLeod (incompetech.com)
Licensed under Creative Commons: By Attribution 4.0 License
https://creativecommons.org/licenses/by/4.0/

"Ethernight Club" Kevin MacLeod (incompetech.com)
Licensed under Creative Commons: By Attribution 4.0 License
https://creativecommons.org/licenses/by/4.0/

"Inspired" Kevin MacLeod (incompetech.com)
Licensed under Creative Commons: By Attribution 4.0 License
https://creativecommons.org/licenses/by/4.0/

"Limit 70" Kevin MacLeod (incompetech.com)
Licensed under Creative Commons: By Attribution 4.0 License
https://creativecommons.org/licenses/by/4.0/
```

The official Incompetech license page says its Creative Commons option is no-charge and requires credit. HoliznaCC0’s FMA page identifies *New Gods* as CC0 1.0 Universal and marks “AI generated? No.”

## Source evidence and integrity

- [ffprobe/SHA manifest](downloaded-candidate-audit.json) records the exact duration, byte count, SHA-256, excerpt source offsets, and excerpt hashes for every file.
- [FMA New Gods page capture](source-evidence/fma-new-gods.html) is the full track page HTML, including its CC0 statement and public file URL.
- [Incompetech public catalog snapshot](source-evidence/incompetech-pieces.json) supplies ISRC, duration, filename, genre, instruments, tempo, and feel metadata for its tracks. [License page capture](source-evidence/incompetech-licenses.html) preserves the site's no-charge CC requirement. The individual Incompetech page snapshots are the creator site's client-rendered shell; track metadata is supplied from its linked catalog, and rendered track pages are linked above.
- The downloaded HoliznaCC0 [Retro Wave collection](downloads/holizna-retro-wave-2.zip) is a CC0 OpenGameArt pack, but its six tracks run only 130.668–222.948 seconds, so none is a viable single 298-second cue. Its archive SHA-256 is `a6bd090f2e10dcac9e26debc9421cb8f6b18d5266f3941b320dee663d79237ba`.

The only candidate close enough to fit with a tiny natural trim is *Limit 70*, though its listed mood suggests it may be too relaxed. No cue has been selected, trimmed, arranged, or added to the native project. Root should audition the full files against the cut and compare a single-song timeline against `EDMDetectionMode` before deciding.
