# Optional planar surface tracking

Timeline planar tracking uses a local Python process with OpenCV and NumPy. It
does not download models or send frames outside the machine. The tracker follows
textured 2D planes with sparse Shi-Tomasi features, forward/backward Lucas-Kanade
flow, ORB reference matches, and RANSAC homographies. It checks support
distribution, agreement between independent matches, reprojection error, and
corner topology, and reports failure when the evidence is weak.
Samples below the 0.30 support-confidence floor fail the complete track. The
score is a heuristic, not a probability or quality certification.

The desktop runtime looks for Python in `PHOTO_EDITOR_SURFACE_PYTHON`, then in
`.photo-editor/tracking/surface-runtime/venv/bin/python` in the source checkout,
then as `python3` on `PATH`. The application package embeds
the worker script. Set `PHOTO_EDITOR_SURFACE_WORKER` only when diagnosing or
testing a worker override. A compatible environment needs Python 3, NumPy, and
`opencv-python-headless`; no checkpoint is needed.

The runtime accepts 2 to 180 same-sized numbered PNGs (`000000.png` onward),
with a maximum long edge of 960 pixels, and strictly increasing source times.
The seed and exclusion rectangles use normalized image coordinates. Output
corners are normalized and ordered top-left, top-right, bottom-left,
bottom-right. Corrections provide a frame index and the four corrected corners.
An unresolved gap still fails the complete track, even if a later correction
lets tracking continue from that frame.

Tracking can fail on textureless, reflective, repetitive, blurred, occluded, or
non-planar content. This is a local 2D planar tracker; it does not estimate lens
distortion, depth, or 3D motion, and its confidence is a support heuristic rather
than a quality certification.

For a source checkout, the tested isolated setup is:

```sh
/opt/homebrew/bin/python3 -m venv .photo-editor/tracking/surface-runtime/venv
.photo-editor/tracking/surface-runtime/venv/bin/python -m pip install opencv-python-headless==4.13.0.92
```

`run-native.sh` exports the checkout runtime when present. A separately installed
app needs `PHOTO_EDITOR_SURFACE_PYTHON` pointing to a compatible environment;
OpenCV itself is not included in the app bundle. Availability checks import the
libraries once per interpreter path and report a missing runtime before analysis.
