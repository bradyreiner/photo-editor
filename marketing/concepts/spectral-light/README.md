# Looksy — spectral light pilot

This replaces the previous graphic-only direction with actual recordings of
Looksy's current desktop surface. It is a local review pilot, not a published ad.

## Creative reference

Primary reference: [Dabarti — The Photons](https://dabarti.com/work/the-photons/),
an optical study of light bending, refraction and caustic patterns. Use its
material and light research as a direction, without copying its shots or footage.

Secondary reference: [Found — Orluna, Breathe In Light](https://www.found-studio.com/work/orluna-midi/),
for linking a physical property of light to a product story.

The requested direction is light as the creative material: wavelength separation,
additive RGB, concentrated silver caustic folds, fluid movement, black optical
space, clean bold typography, and readable app demonstrations. The CGI is
transition material; the product evidence is the real recording.

## What was captured

- Genuine Canon EOS 80D CR2 landscapes imported through Looksy's normal UI into
  `Light Studies — Reel Demo`. Separate source copies preserve the originals.
- Native RAW and light controls, with real shadow and exposure adjustments.
- An editable RGB tone curve and the actual rendering response in the continuous
  source recording. The pilot does not make a real-time performance claim.
- A linked linear mask on an adjustment layer, followed by a local exposure edit.
- Editable Helvetica text with a linked selection used as its layer mask.
- A separate pixel-paint layer with a completed brush stroke; an erase operation
  was also attempted after the main capture and is not asserted by the pilot.
- The actual Canon metadata, sRGB information, ACES 2 renderer information, and
  preview/proof controls displayed by the app. These UI captures do not establish
  independent color accuracy or certify a delivered ICC profile.

The working composition and recordings are local. The RAW photo/metadata and
publication suitability have not received a separate public-release review.
The earlier iPhone DNG import was a setup trial and is omitted from the pilot.

## Edit and rendering

`render_native.py` selects bounded excerpts from the untouched native recordings.
It removes window shadows, uses simultaneous crops from each recorded timestamp,
and compresses idle time. Reframing and compression are disclosed on-screen.
No interface or result is reconstructed. The local pilot uses direct cuts between
app shots, with moving caustic passes for the opening and ending.

`render_reel.py` also preserves a more expensive 60 fps software composition with
optical wipes. The delivered native-pipeline review cut is 30 fps. Its caustic
passes are evaluated at 720×1280 and resampled into the 1080×1920 master, while
the real app panels are processed from the native-resolution window recordings.

`optics.py` implements an art-directed thin refractive heightfield. Forward photon
splats form light concentrations, with three slightly different refractive paths
for RGB dispersion. Channels combine in linear light before display encoding.
This is a visual approximation, not a spectral or measured optics simulator.

The soundtrack is original, deterministic, sample-free synthesis. No outside
reference footage, music, or paid service is used in this render.

```sh
marketing/.venv/bin/python marketing/concepts/spectral-light/render_reel.py --stills
marketing/.venv/bin/python marketing/concepts/spectral-light/render_native.py
```

Requires Pillow, NumPy, FFmpeg and ffprobe. The current marketing virtual
environment contains these dependencies. The source script uses the macOS
Helvetica Neue collection for this local prototype.

Outputs remain in the ignored `marketing/renders/spectral-light/` directory:

- `looksy-spectral-light-pilot.mp4`: vertical 1080×1920 / 30 fps / H.264 + AAC.
- `pilot-contact-sheet.jpg`: six readable layout samples.
- `pilot-provenance.json`: source hashes, exact excerpt times and crop coordinates.
- `*-session-*.mov`: continuous native app-window recordings without microphone.
- `original-optical-sound.wav`: editable synthesized audio bed.

## Remaining production decisions

Judge the motion and footage together before multiplying the format into a series.
The current mask is a gradient, not a finished subject cutout or a text-behind-
subject hero composition. A dedicated text-mask reel should show a more dramatic
mask change and finished result. A dedicated RAW reel should include native
export and the exported file. Export/save/reopen are not demonstrated by this cut.
Physically modeled glass, volumetric lighting and path-traced caustics would be
the next production step if this optical direction is accepted; the present pass
does not claim that level of simulation or final studio quality.
