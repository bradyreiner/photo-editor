# Preview cache decode recovery

## Verified defect

The disk-cache scheduling check reads image headers and dimensions. A JPEG with
a valid SOF/SOS header but missing quantization tables passes that check while
pixel decoding fails. The edited-preview worker previously marked the result as
a successful disk-cache hit before checking decoded pixels. Comparison reference
publication also trusted header freshness without decoding or checking the
expected dimensions.

The eight-case failing-before regression reproduces successful result reports
with undecodable published files for both legacy and ACES2 rendering, across:

1. Max-preview image publication.
2. Full-preview file-URL publication.
3. Stored-recipe snapshot comparison.
4. External-file comparison.

Log: `/private/tmp/corrupt-preview-cache-before.log`, exit 1, all eight cases
fail. Earlier test-setup attempts were not counted as defect evidence: the final
test explicitly distinguishes external references from snapshots and supplies
the canonical render-plan admission required for ACES2.

## Change

`read_validated_preview_disk_cache` is a worker-only helper. It checks the header
size against the expected render size, decodes the image, verifies the decoded
size, and checks cancellation before and after reading. The GUI's scheduling
check remains header-only; full validation stays on the asynchronous renderer.

Edited-preview success/cache-hit/publication fields are populated only after
validation and required image-format conversion succeed. Rejected entries fall
through into the normal renderer and existing atomic JPEG writer. If rendering
or writing fails, that path reports failure instead of publishing a false cache
success. Rejection is recorded as `preview.render.disk_cache_rejected`.

Comparison references validate decoding and expected dimensions before reusing
their encoded file. Invalid files are atomically replaced from the already
rendered reference image. Originals, recipe identities, cache keys, color math,
and JPEG encoding settings are unchanged. The existing display-referred pixels
and ICC tags remain preserved on a successful cache hit.

## Regression evidence

`preview_corrupt_cache_test` seeds a private cache through the real preview
entry point, removes JPEG quantization-table segments while retaining readable
headers, and verifies the fixture actually fails pixel decoding. It then checks:

- Pre-canceled requests neither succeed nor modify the corrupted cache bytes.
- Recovery succeeds without claiming an edited-cache hit.
- The replacement decodes to exactly the original cached pixels and ICC tag.
- A subsequent warm edited preview hits the repaired cache.
- A decodable 8x8 cache is replaced at the correct full dimensions and with
  exactly the original cached pixels, including comparison references.

All eight final synthetic cases pass, including cancellation and size repair:
`/private/tmp/corrupt-preview-cache-after.log`. The source synthetic image carries
a Display P3 tag. This is a pipeline/cache invariant test, not an independent
colorimetric reference.

Six focused regressions pass in 5.06s: corrupt-cache recovery, asynchronous warm
asset loading/retry, future lifetime, preview math, legacy reference workflow,
and ACES2 reference workflow. Log:
`/private/tmp/corrupt-preview-cache-regressions.log`.

App and focused targets rebuild successfully. Final build log:
`/private/tmp/corrupt-preview-cache-final-build.log`. Intentional malformed-JPEG
decoder warnings appear in recovery test logs; changed C++ compiles without
warnings.

## Full-resolution and native verification

The existing Nexus 6 DNG and paired reference JPEG under
`/private/tmp/framehouse-six-raw-source-v3/hdrplus-nexus6-skin-0218/` each pass all
eight cases at **4208x3120**, in both rendering models. All cancellation, dimension
repair and exact regenerated-pixel checks pass. Receipts:

- `/private/tmp/corrupt-preview-cache-raw.json`
- `/private/tmp/corrupt-preview-cache-jpeg.json`

Corresponding `.log` files contain the process output. Including the synthetic
fixture, this is 24 passing recovery cases, not broad camera-corpus validation.

The rebuilt native app's isolated editing smoke also passes:
`/private/tmp/corrupt-preview-cache-app-6KLgra/receipt.json` records successful
JPEG placement over a RAW base through QML, preview publication and undo/redo,
with an isolated catalog and no model download. The workspace screenshot was
visually inspected. Its catalog-recovery banner is expected for the deliberately
unavailable-catalog test profile and is not a finding about the user's catalog.
This app smoke checks integration; malformed-cache recovery is exercised by the
dedicated preview-entry-point tests, not by the smoke screenshot.

## Reproduce and limits

```sh
QT_QPA_PLATFORM=offscreen build/release-macos/tests/preview_corrupt_cache_test
QT_QPA_PLATFORM=offscreen build/release-macos/tests/preview_corrupt_cache_test \
  SOURCE_IMAGE NEW_RECEIPT.json
```

The optional source run uses its full dimensions. All corruption is confined to
the temporary private cache; the external-reference fixture is a temporary copy
of the input, not a modification of it. The receipt must not already exist.

This detects undecodable and wrong-size cache images, not every bit corruption
that still decodes to plausible pixels. Validation does not provide cryptographic
cache authenticity or prevent unrelated file mutation after validation. Reference
cache reuse now incurs a worker-side decode; no performance improvement is
claimed. Cold loading, native input-to-photon timing, diverse-camera color truth,
Gaussian 4D/film, embedded inference/search and full professional editing remain
unproven and the broad goal remains active.
