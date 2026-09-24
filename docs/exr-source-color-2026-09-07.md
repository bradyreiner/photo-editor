# EXR source color: numeric conversion and metadata precedence

This fixes a confirmed source-color error while preserving the broader RAW,
loading, Gaussian 3D/4D, model/search/agent and professional-editing objective in
`active-app-completion-goal.md`. It is not full color-engine certification.

## Defects and implementation

The old desktop EXR decoder copied source RGB unchanged and constructed a Qt
color space from float chromaticities. When Qt rejected those primaries, it
silently assigned linear sRGB. The ProPhoto fixture produced an RGB error of
1.74694 against the established engine transform. ACES imaginary primaries
and float rounding at the ProPhoto x+y=1 boundary also require support beyond
Qt's primary validator.

`openexr_source_color.h` now constructs a double-precision transform from the
actual stored chromaticities using homogeneous XYZ primary columns. It rejects
nonfinite, singular, severely ill-conditioned, or unusable white-point data.
The decoder converts RGB into canonical linear sRGB before applying that tag.
Alpha is unchanged; negative values and values above 1 are retained. Float
overflow rejects the result. Conversion checks cancellation per row; underlying
OpenEXR file reading remains a separate cancellation/memory limitation.

RGB working spaces use Bradford reference-white adaptation. Pure XYZ channels
with equal-energy basis coordinates must not treat that basis as an adopted
scene illuminant: the independent OpenEXR corpus exposed a 0.02795 normalized
RMS mismatch with inappropriate adaptation. The absolute-XYZ case now omits
that adaptation and produces 0.000108106 normalized RMS error. This distinction
is covered by the external paired-image check, not only our matrix reference.

Both decoded-source cache identity and canonical decoder provenance include
`openexr-source-color:v2|chromaticities-to-linear-srgb|bradford-rgb:xyz-absolute|float-unbounded|interop-v1`.

The engine reader resolves the ACES container flag first, then six recognized
linear scene Interop IDs (Rec.709, AP0, AP1, P3-D65, Rec.2020, Adobe RGB), then
explicit chromaticities when the ID is absent/unknown. Unsupported IDs and
non-color data fail closed; unknown/missing metadata no longer means sRGB.
The writer adds recognized linear Interop IDs without discarding its existing
consistent chromaticities. This follows the relevant supported subset of the
[ASWF EXR color metadata guidance](https://github.com/AcademySoftwareFoundation/ColorInterop/blob/main/Recommendations/04_OpenEXRFiles/OpenEXRFiles.md)
and [OpenEXR attribute definitions](https://openexr.com/en/latest/TechnicalIntroduction.html).

## Verification receipts

- `/private/tmp/exr-source-before.log`: new decoder test fails before the fix
  with the ProPhoto error and invalid-primary Qt warning.
- `/private/tmp/exr-source-accepted-build.log` and
  `/private/tmp/exr-source-test-final-build.log`: app and tested targets build.
- `/private/tmp/exr-source-accepted-tests.log`: 6/6 tests pass in 4.43 seconds:
  EXR I/O, EXR source color, engine color pipeline, render executor, live mask
  sampling and mask-thumbnail color tests.
- `/private/tmp/exr-source-accepted-corpus.log`: seven linear RGB spaces pass
  with maximum absolute RGB error at most 0.0000621 against the separately
  established engine transform (float EXR vs double canonical chromaticities).
  Cases include negative/HDR RGB and fractional alpha. Direct-header tests
  exercise six ID-only files, conflicting chromaticities, ACES flag precedence,
  a custom gamut, singular metadata, missing/unknown/data rejection and cache
  provenance. The header tests were initially excluded by dependency scope;
  CMake discovery was corrected and the final receipt explicitly includes them.
- `/private/tmp/exr-source-raw-jpeg-regression.log`: real DNG/JPEG masks still
  match through four working RGB spaces and crop states; seven inspection
  modes, notify-driven completion, original/refined switching and 16 MiB
  request admission also pass.
- `/private/tmp/exr-source-accepted-controller.log`: the existing controller
  scope-domain mode passes without the previous invalid-primary warnings.
  This is not a passing full controller suite; its slider-scheduling assertion
  remains a separate task.

Independent corpus: the official
[OpenEXR chromaticity examples](https://github.com/AcademySoftwareFoundation/openexr-images/tree/main/Chromaticities)
describe their Rec.709 and XYZ images as equivalent encodings. The original
Rec.709 file omits color metadata, so the test oracle explicitly assigns its
documented Rec.709 encoding. Production does not infer that for arbitrary
untagged files. The XYZ file follows the actual desktop decoder.

| Fixture | SHA-256 |
| --- | --- |
| Rec709.exr | `15028cbf7bf0efa43738890af715ec550d26ae9e32a1020d0f4e7c785d3a370e` |
| XYZ.exr | `8d97c476726ae67a086220463c91b7202279a4c7c1ea94810ce75a4c28e7825f` |

Both files are in `/private/tmp/framehouse-exr-corpus.f25GHw`. Their final
normalized maximum error is 0.00106073 and RMS is 0.000108106, within the test's
half-float tolerance. `/private/tmp/exr-final-corpus-reference.png` and
`/private/tmp/exr-final-corpus-candidate.png` were visually inspected and appear
consistent. These are rendered artifacts, not a foreground native UI session.

## Open work

- Untagged or unsupported-color EXRs now fail closed. A clear user-facing source
  color assignment/recovery path remains required; generic input profile
  selection does not currently resolve this decoder path.
- Arbitrary namespaced/nonlinear Interop IDs, OCIO/file rules, adopted-neutral
  workflows, multipart/AOV/data rendering and luminance/chroma channels are not
  completed here. Private descriptive color-space labels are retained by
  inspection but are not conversion authority.
- Broader real-camera/real-project color calibration, photographic preview/
  export parity, full-resolution memory limits and foreground realtime still
  require their own evidence. The model package still has no installed weights.
