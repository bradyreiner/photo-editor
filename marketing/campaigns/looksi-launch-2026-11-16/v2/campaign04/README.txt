LOOKSi campaign 04
The last tweak. Again.

The 24-second film uses a recurring original poster to tell a small story about
creative perfectionism. It ends with the builder’s photo-and-video invitation.
The artwork is a campaign graphic. It is not presented as a captured app result.

REVIEW
Open review.html for the film, opening selector, sound toggle and creative notes.
The image/cover.png cover is 1080 x 1350. The storyboard/board.png direction board
is 2400 x 1500 with five panels. Source type and vector geometry remain editable.

MASTERS
videos/hero.mp4: “The last tweak. Again.”
videos/hook-b.mp4: “Just one more pixel. Again.”
videos/hook-c.mp4: “You said it was done. Again.”
Each has a matching -silent.mp4 master. All are 1080 x 1920, 24 fps, 24 seconds,
H.264. Sound-on masters use original stereo AAC sound design. There is no speech.

The RGB frames undergo an explicit BT.709 matrix conversion to limited-range
YUV. Frame properties and encoded primaries/transfer/matrix/range declarations
are checked. The cover and storyboard PNGs carry an sRGB profile. These are
encoding controls, not independent display or player color qualification.

The hook variants change the opening and its short dissolve. Their body remains
the same. Every version is encoded continuously, including its audio, so there
are no concatenation seams. All essential meaning is visible with sound off.

SOURCE
source/make_campaign.py: artwork, choreography, typography, audio and exports.
source/timing.json: per-scene direction and timing.
source/copy-and-testing.json: captions, alternate hooks, alt text and test design.
source/on-screen-text.srt: hero on-screen text transcript, not recorded speech.
source/outline_svg.m: exact font outlines for SVG renderers.
source/fonts: the project fonts used here, with SIL Open Font Licenses.

images/cover.svg is the font-exact outlined vector master. Each copy group keeps
its wording in a data-copy attribute. images/cover-editable-text.svg retains live
editable text and embedded font declarations. The storyboard has the same pair.
The raster panels inside its SVG are keyed to editable source motion code.

REBUILD
Requires Python, Pillow, numpy, ffmpeg and ffprobe. Versions are listed in
source/requirements.txt. Run from this campaign directory:

python source/make_campaign.py --render all
python source/verify_campaign.py

On macOS the builder also uses clang and CoreText to create font-exact SVG
outlines. Font installation is unnecessary. Other platforms still render the
PNG and MP4 outputs and keep live-text SVG sources.

QA
qa/verification.json records delivered stream metadata, frame counts, full decode
results and measured sound levels. Decoded contact sheets and native-resolution
stills are in qa. Phone-size previews are included for the cover and closing CTA.

The music is synthesized from oscillators and deterministic noise. Clicks,
plucks and paper-like swells are timed in source code. The quiet pause before
the final one-pixel nudge is intentional. Source audio and mastered WAV are kept.

REMAINING PRODUCTION
Creative, name, caption and placement review. No founder filming or app capture
is required for this graphic campaign. Posting is outside this package.
