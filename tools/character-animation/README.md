# Native character animation

`timeline_rig_scene.h` is a deterministic Qt vector scene renderer shared by the
timeline vector-path generator and `rig-render`. It adds ordered parented nodes,
pivot transforms, scalar keyframes, four interpolation modes, inherited opacity,
stroke reveals, typography, radial/linear shading, blend modes and seeded paper
texture. It is not a 3D engine or a complete graphical rig editor.

Build and run focused tests, respecting the repository build lock:

```sh
bash tools/character-animation/build.sh
```

The original example film is
[`The Perfect Picnic`](../../marketing/short-films/the-perfect-picnic/SCREENPLAY.md).
Author its nine scenes and original synthesized soundtrack, then render:

```sh
marketing/.venv/bin/python marketing/short-films/the-perfect-picnic/build_film.py
marketing/.venv/bin/python marketing/short-films/the-perfect-picnic/render.py --width 1920 --height 1080
```

The existing marketing virtual environment supplies NumPy and Pillow; native
rendering requires Qt 6.10+, CMake and a C++20 compiler. FFmpeg encodes the output.
No model downloads, network service, generated video or licensed music is used.
Outputs are local under `marketing/outputs/the-perfect-picnic/` (gitignored).

## Scene contract

The `looksy-rig-scene-v1` object has `width`, `height`, `nodes`, and optional
`paperOpacity`. Each node has a unique `id`; an optional `parent` must refer to an
earlier node. Node kinds are `group`, `path`, `rect`, `ellipse`, and `text`.
Coordinates are in scene units. Rotation is in degrees. Scale is dimensionless.
Transforms apply local translation, pivot, rotation, scale and inverse pivot,
then the parent transform. Node array order is paint order, independently of
the hierarchy. Opacity multiplies through the parent chain.

`x`, `y`, `rotation`, `scaleX`, `scaleY`, `opacity` and `trim` can be constants or
ordered `[localSeconds, value, easing]` keyframes. Easing on a key controls the
interval to the next key: `linear`, `hold`, `smooth`, or `smoother`. Values hold
before the first and after the last key. `trim` reveals an open stroked path.
`scene.py` provides authoring helpers and a two-bone inverse-kinematics solver;
the solver's output is ordinary editable joint rotation keys.

Paint colors use Qt `#RRGGBB` or `#AARRGGBB`. Gradients have `type` (`linear` or
`radial`), `coordinates` (x1,y1,x2,y2 or cx,cy,radius), and ordered
`stops` (`[position,color]`). Blend modes are `normal`, `screen`, and `multiply`.
Paths use `["M",x,y]`, `["L",x,y]`, `["Q",cx,cy,x,y]`,
`["C",c1x,c1y,c2x,c2y,x,y]`, and `["Z"]` commands.

The scene is optional `parameters.vectorPath.scene` on the existing `vector-path`
timeline generator. It takes precedence over legacy commands. Keep legacy
`viewBoxWidth`, `viewBoxHeight`, and one `M` command for the authoring schema's
compatibility fields. `native-generator-shots.json` contains those complete
per-shot inputs; it is not a saved timeline document. Scenes are persisted inside
ordinary generator parameters, and the complete scene participates in the native
frame-cache hash. Cached export prepares scene geometry once per request.

The standalone `film.json` envelope stores shot intervals, titles, and scenes.
`rig-render FILM WIDTH HEIGHT SECONDS OUTPUT.png` renders an exact requested
frame; replacing the output path with `raw` streams the entire film as RGBA.

## Evidence boundaries

Focused checks exercise interpolation, hierarchy, parent opacity, malformed
inputs, cycles, deterministic JSON round-trip, native preview/cache pixel parity,
and cache invalidation after scene edits. A standalone film render and these
checks do not establish full-app startup, foreground playback latency, a graphical
rig-editing workflow, or a native project save/reopen session. The MP4 is a first
creative cut, with sampled-frame review; its receipt keeps full-motion human
review and native app qualification explicit.
