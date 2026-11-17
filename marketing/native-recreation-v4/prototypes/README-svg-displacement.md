# SVG brush displacement prototype

These files are an isolated, unregistered pixel prototype. They do not modify
the desktop app or its build graph.

## Reference behavior

The active filter is `marketing/animated-trailer/src/creative-launch/Canvas.tsx`
(`CampaignPoster`, filter `campaign-ink`):

```xml
<feTurbulence type="fractalNoise" baseFrequency=".07 .2"
              numOctaves="2" seed="8" />
<feDisplacementMap in="SourceGraphic" scale="8" />
```

The omitted SVG attributes resolve to `stitchTiles="noStitch"` and
`xChannelSelector="A" yChannelSelector="A"`. W3C defines displacement as
sampling the source at
`(x + scale * (XC - 0.5), y + scale * (YC - 0.5))`. Thus both axes use the same
fourth turbulence channel for this source filter. The turbulence alpha is a
real fractal-noise channel; its RGB values are immaterial to this displacement
map. The existing Looksy `render_fractal_noise_frame()` helper implements the
W3C noise algorithm and preserves that alpha output. Its linearRGB-to-sRGB RGB
conversion does not change alpha.

The source brush path is `M-120 955L1100 400`, orange `#ee6d33`, 180 user units
wide, round capped, in a `1000x1250` SVG viewBox. Its dash draw animates during
Canvas chapter local seconds `1.4–4.3`, or trailer time `134.4–137.3 s` (Canvas
chapter starts at 92 s and its active brush phase starts at 133 s). The parent
card is scaled to `.6`, so the authored scale of 8 local SVG units is about 4.8
sequence pixels while that card scale is active.

## Prototype contract

`svg_fe_displacement_map_prototype.{h,cpp}` reuses the existing turbulence
renderer and implements the scalar displacement equation. It requires aligned
equal-size source and map rasters, samples the chosen map channel as a straight
scalar, samples the source bilinearly in premultiplied RGBA, and treats source
samples outside the raster as transparent. Source color-space metadata is
preserved. It assumes one raster pixel per SVG user unit; for this poster, use a
`1000x1250` map and source raster.

The standalone test compares every pixel of a small synthetic fixture against
an independent scalar equation oracle. It also checks default alpha-channel
selection, the +4 px result for an opaque map, transparent edges, premultiplied
edge interpolation, exact turbulence parameters, and a `1000x1250` raster of
the actual CampaignPoster brush path. That full-size raster changed 6,642
pixels under the native map in the current run. The generated full-size alpha
range was `20..235`, corresponding to approximately `-3.37..+3.37` user units
on each axis with `scale=8`.

Compile and run without CMake or Ninja:

```sh
clang++ -std=c++20 -O0 -w \
  -Iapps/desktop/src \
  -Imarketing/native-recreation-v4/prototypes \
  marketing/native-recreation-v4/prototypes/svg_fe_displacement_map_prototype_test.cpp \
  marketing/native-recreation-v4/prototypes/svg_fe_displacement_map_prototype.cpp \
  apps/desktop/src/timeline_fractal_noise.cpp \
  $(pkg-config --cflags --libs Qt6Core Qt6Gui) \
  -o /tmp/looksy-svg-displacement-prototype-test
/tmp/looksy-svg-displacement-prototype-test
```

## Limits of this evidence

The oracle verifies the W3C equation and Looksy's alpha map, not raster equality
against Chromium or Remotion's browser renderer. Browser filter precision,
filter-region clipping at the outer poster edge, vector antialiasing, and the
browser's source-image interpolation are not covered by this prototype.

References: [W3C Filter Effects: `feDisplacementMap`](https://www.w3.org/TR/filter-effects-1/#elementdef-fedisplacementmap),
[W3C Filter Effects: `feTurbulence`](https://www.w3.org/TR/filter-effects-1/#elementdef-feturbulence).
