# Prepared search runtime signing

## Defect and fix

The runtime receipt hashes native libraries as well as Python/data files.
Previously, the normal macOS app signer force-signed every native leaf after
that receipt was compiled into the app. Changing a native signature changes the
file bytes and invalidates the app's runtime identity.

The corrected sequence is:

1. Verify the selected runtime against its trusted input receipt.
2. `--sign-copy` creates a new independent directory, signs native leaves and
   then nested code containers, verifies those signatures, and only then writes
   the final inventory. The input runtime is not modified. Output cannot already
   exist or be nested within the source. Failed work is not silently promoted.
3. Configure/build the app with the prepared output. CMake embeds the same final
   receipt digest in the executable and `FramehouseSearchRuntimeReceiptSHA256`
   in Info.plist. Plist generation now follows runtime selection.
4. The normal app signer verifies that exact inventory and its native signatures
   before any signing writes. It leaves the prepared runtime's files and nested
   frameworks untouched while signing the rest of the app. It checks the runtime
   again after the outer app seal and normal deep/strict signature verification.

For release signing, the runtime's prepared identity must match the requested
identity, and requested hardened-runtime/timestamp options must have been used
during preparation. Ad-hoc intermediate app sealing may retain a prepared
Developer ID runtime. Checksums are never regenerated inside an already built
app to paper over signing changes.

This follows Apple's [inside-out signing requirements](https://developer.apple.com/library/archive/technotes/tn2206/).
The receipt remains trust in the selected build artifact, not independent
attestation of the build environment or a replacement for notarization.

## Commands

With explicit absolute paths to an already verified bundle and a new output:

```sh
python3 -I scripts/build-search-runtime.py \
  --sign-copy /absolute/input-runtime \
  --receipt-sha256 TRUSTED_INPUT_DIGEST \
  --output /absolute/new-signed-runtime \
  --identity 'Developer ID Application: Name (TEAMID)' \
  --hardened-runtime --timestamp

cmake --preset release-macos \
  -DPHOTO_EDITOR_SEARCH_RUNTIME_DIR=/absolute/new-signed-runtime
```

The returned final digest, not the input digest, becomes the app's identity.
Existing app reseal/release scripts invoke the prepared-runtime preflight
automatically. Python is a packaging-host dependency, not a runtime dependency
on the user's machine. Developer ID commands above are documentation only;
no release identity, timestamp service or notarization submission was used here.

For local development use `--identity -` without hardened-runtime/timestamp
options. A real test of ad-hoc signing plus hardened runtime passed signature
verification but failed Python loading with the macOS different-Team-IDs error.
That combination is now rejected before output creation, rather than disabling
library validation. The failed artifact remains separate from accepted proof.

## Focused contracts

`search_runtime_bundle_test` verifies finalized inventory, unchanged source,
leaf-before-container order, refusal to overwrite/nest output, incompatible
ad-hoc options, required release identity/options, missing app runtime/digest,
corruption rejection and no receipt rewriting during app verification.

`macos_bundle_signing_contract` verifies ordinary inside-out app signing,
failure before any app writes when runtime trust is missing, skipping prepared
runtime files/frameworks, retaining other app signing, and pre/post verification.
These controlled tests are not real signing or model inference evidence.

Both focused packaging contracts pass in 0.91 seconds. The final expanded run
passes all thirteen signing/search/model/index/catalog/edit contracts in 31.16
seconds with scoped localhost fixture access:
`/private/tmp/framehouse-siglip2-O2ha9I/package-signing-regressions-final.log`.
This does not supersede the separate full catalog import timeout or certify
whole-app tool coverage. All owned build/signing/inference/UI/test handles for
this checkpoint are terminal.

## Real local signing and inference proof

Proof root: `/private/tmp/framehouse-siglip2-O2ha9I`.

- `package-signed-v4` is the accepted ad-hoc runtime. Its final receipt is
  `530f5d6a379ec8d6e77b58085239b1f83c4b4369a2e090352cbc3135238a69da`.
  Preparation output/log: `package-signed-v4.json` and `.log`. All native leaves
  and the Python framework were signed and signature-verified before inventory.
- A separate full desktop app builds at `signed-runtime-app-proof/Framehouse.app`.
  Its configured plist and executable compile definition use the final digest.
  Logs: `package-signed-v4-configure.log` and `package-signed-v4-app-build.log`.
- The real app signer succeeds with ad-hoc identity, and deep/strict app signature
  verification passes. `package-v4-outer-sign.json` records the same receipt before
  and after outer signing; `.log` shows the other app code was signed normally.
- Independent native verification after app signing succeeds:
  `package-v4-post-sign-native.json`, 14,775 entries and 510,867,011 file bytes.
- Real MPS inference through the signed standalone runtime ranks all ten queries
  as expected, limits the requested scope, reuses five vectors after restart and
  stops both owned workers (`package-signed-v4-mps.json`). The 33-second initial
  run overlapped app compilation and is not a usable latency comparison.
- `signed-ui-v4/receipt.json` and `signed-ui-v4.log` then exercise the actual native
  Library Find photos UI through the runtime in the outer-signed app. Expected
  dog first, five cached vectors reused, filter invalidation/recovery and shutdown
  pass. First query: 9,854 ms; repeats: 218/209 ms. The five inputs represent four
  scenes, including an existing RAW-derived preview, not a diverse RAW corpus.
  Wide/compact captures were inspected; no QML assignment, type/reference or
  binding-loop errors occur in this log. Independent full-resolution inspector
  loading remains outside this proof.

The earlier `package-signed-v3` is rejected evidence: hardened ad-hoc signing
passed cryptographic checks but failed actual loading due to missing Team ID.
It was never used to build the accepted app. The new preflight prevents creating
that combination. No real release credentials or additional model weights were
used. The user's running app process was not stopped or replaced. Development
output paths and the prior v2 runtime selection were restored after the separate
proof build; the accepted signed app remains isolated, not silently installed.

The UI driver explicitly verifies/configures the runtime from the signed app.
It does not prove `main.cpp` auto-discovery or user-friendly managed setup, nor
does ad-hoc sealing establish Developer ID/hardened release readiness.

## Remaining gates

Developer ID/hardened runtime, timestamps, notarization and clean-machine launch
still require real release proof. The local Python base still requires macOS 26;
no broad compatibility claim is made. Managed model installation/repair and
production auto-discovery, large-corpus search/agent quality, and the independent
RAW/loading/3D/4D/editing requirements remain open.
