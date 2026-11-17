LOOKSi / THE LAST TWEAK. AGAIN.
Six-slide Instagram carousel, 1080 x 1350, in reading order 01–06.

USE
Upload slide-01.png through slide-06.png in number order.
carousel.json contains the matched caption, every exact on-screen text string,
per-slide alt text, provenance and the remaining review status.

EDIT
slide-NN-editable-text.svg keeps live editable headline and poster text,
with the original OFL font software embedded.
slide-NN.svg is a portable vector master: its same fonts are converted to
editable paths, avoiding substitution in apps that ignore embedded webfonts.
PNG exports were rasterized from these outlined SVGs; they use the same glyphs.
No raster artwork is embedded in the vector masters.

REVIEW
qa/phone-contact-sheet.png shows all six at 360-pixel width.
qa/slide-NN-phone-360.png gives each separate phone-size preview.
The main title, story action and closing category/question were reviewed at
that size. Tiny poster labels and editorial furniture are decorative.
qa/text-bounds.json and export-checks.json record dimensions/type placement
and per-file hashes. The initial crop annotation crossed the headline and
was moved clear before the final renders.

REBUILD ON MACOS
From the repository root:
marketing/.venv/bin/python marketing/campaigns/looksi-launch-2026-11-16/v2/campaign04/carousels/source/build_carousel.py
marketing/.venv/bin/python marketing/campaigns/looksi-launch-2026-11-16/v2/campaign04/carousels/source/build_carousel.py --render

The build uses Pillow for font measurement/phone previews, bundled original
fonts, clang/CoreText for font outlines, and Quick Look for SVG rendering.
Quick Look may need to run outside a restricted sandbox. The square render
wrappers retain 1:1 SVG coordinates; an unused right gutter is removed to
produce the exact 1080 x 1350 portrait canvas. Original sources are retained.

PROVENANCE AND STATUS
Original campaign04 poster geometry, six original variations, crop marks,
cursor, alignment arrows and magnified-pixel joke. Matches the current graphic
film's story. This is campaign artwork, not app UI, an app export, a feature
claim, or a likeness of the founder. No founder filming or app capture needed.
Review-ready; final caption and channel placement review remains. Not posted.
