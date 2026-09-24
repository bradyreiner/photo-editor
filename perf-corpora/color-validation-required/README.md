# Licensed Color-Validation Corpus

This directory defines the evidence contract for promoting a rendering model or built-in look. It is separate from `real-image-required`: the performance corpus proves representative workload behavior, while this corpus proves camera, profile, output, and editorial color behavior against evidence the engine did not generate itself.

Large or restricted photos do not belong in Git. A production manifest may point at a private CI volume, but every referenced byte must have an exact size, SHA-256 digest, provenance, and approved license record. Assets marked `repository` must be legally redistributable with the project; assets marked `private-ci` must have a durable internal evidence identifier.

## Public source inventory

`source-inventory.json` is a non-shipping acquisition inventory, not a populated promotion corpus.
It pins six visually reviewed HDR+ RAW/reference pairs from three camera make/model pairs, covers daylight,
tungsten, fluorescent/LED, and mixed light, and includes each scene's published sensor-to-linear-
sRGB matrix and lens-shading map. Every remote byte has an exact URL, size, and SHA-256 digest.

The source inventory deliberately records `promotion_eligible: false`. It cannot close the licensed
corpus or editorial gates: all six source DNGs omit recorded ISO, two museum scenes still require
depicted-work review, and the skin portrait requires depicted-person review. The set also has no
measured chart, real DCP profile, project license approval, or two-person editorial approval. Those
limitations are machine-readable so a useful source candidate cannot be confused with release
evidence. The portrait uses the explicit
`source-qualified-depicted-person-review-pending` state; validation requires its person-review
blocker and forbids promotion while that state is present.

Validate the inventory without downloading files:

```sh
python3 -B scripts/color/validate-color-source-inventory.py \
  perf-corpora/color-validation-required/source-inventory.json
```

Materialize it into a private CI or local evidence volume:

```sh
python3 -B scripts/color/fetch-color-source-inventory.py \
  --output /path/to/private/color-source-inventory
python3 -B scripts/color/validate-color-source-inventory.py \
  perf-corpora/color-validation-required/source-inventory.json \
  --materialized-root /path/to/private/color-source-inventory
```

The fetcher rejects redirects and any size or digest substitution, writes through temporary files,
removes a partial output after failure, and marks its summary non-promotable. The photos remain
outside the repository and application package. Preserve the dataset attribution and license record
with every private copy or CI artifact.

## Independent chart reference inventory

`independent-chart-source.json` pins the official Middlebury Registered ColorChecker 24 archive and
a compact selection spanning Canon, Fujifilm, Leica, Nikon, Panasonic, and Sony cameras. The
selection includes both controlled 3200 K and 4800 K illuminants, all four published exposures from
-2 through +1 stops, and paired dcraw-linear and camera-JPEG registered chart samples with their
per-channel noise estimates. The authoritative dataset page grants permission to use and publish
the images and requests citation of the accompanying BMVC 2009 paper.

The archive name contains `RAW`, but the archive does not contain sensor RAW files. Its `raw.png`
members are independently dcraw-rendered, registered 606-by-404, 8-bit linear standard-RGB chart
samples without embedded profiles. This inventory therefore strengthens independent real-sensor
chart and exposure evidence; it does not prove our RAW decoder, supply measured spectral patch
values, or by itself satisfy a promotion gate.

Validate the pinned manifest, or verify a previously downloaded archive and selected extraction:

```sh
python3 -B scripts/color/validate-independent-chart-source.py \
  perf-corpora/color-validation-required/independent-chart-source.json
python3 -B scripts/color/fetch-independent-chart-source.py \
  --output /path/to/private/middlebury-chart-reference
python3 -B scripts/color/validate-independent-chart-source.py \
  perf-corpora/color-validation-required/independent-chart-source.json \
  --materialized-root /path/to/private/middlebury-chart-reference
```

The fetcher downloads the 128,651,408-byte archive without redirects, verifies its SHA-256 before
opening it, rejects duplicate or traversal member paths, and extracts only the checksum-covered
192-member, 22,013,141-byte selection. The aggregate member-set digest detects member substitution
without adding the image payloads to Git or the application package. Project license review,
explicit source-space tagging of derived comparison artifacts, and any ColorChecker trademark or
target-use review remain recorded blockers.

The independently generated `raw.png` samples are declared linear standard RGB by the source page
but contain no PNG color-profile chunks. Do not silently assume sRGB when comparing them. The
desktop build provides `independent_chart_profile_tag_tool`, which attaches Qt's verified linear
sRGB ICC payload to a derived PNG, reloads it, and refuses publication unless every decoded RGBA
sample remains byte-identical:

```sh
./build/dev-macos-debug/tests/independent_chart_profile_tag_tool \
  --input /path/to/members/checker24/LeicaM8/wb1i1e3-raw.png \
  --output /path/to/derived/LeicaM8-wb1i1e3-linear-srgb.png \
  --report /path/to/derived/LeicaM8-wb1i1e3-linear-srgb.json \
  --source-id members/checker24/LeicaM8/wb1i1e3-raw.png \
  --output-id references/checker24/LeicaM8/wb1i1e3-linear-srgb.png
```

The report pins source/output file hashes, decoded-pixel hashes, ICC payload hash, dimensions,
primaries, transfer function, tagger/Qt versions, stable logical identities, and the tag-only
assertion. It rejects already tagged inputs, absolute identities, and traversal identities rather
than retagging or publishing machine-local paths.

Build and validate the complete 48-reference matrix after materializing the source inventory:

```sh
python3 -B scripts/color/prepare-independent-chart-references.py \
  --materialized-root /path/to/private/middlebury-chart-reference \
  --tag-tool ./build/dev-macos-debug/tests/independent_chart_profile_tag_tool \
  --output /path/to/private/middlebury-chart-tagged-set
python3 -B scripts/color/validate-independent-chart-tagged-set.py \
  /path/to/private/middlebury-chart-tagged-set/tagged-reference-set.json \
  --artifact-root /path/to/private/middlebury-chart-tagged-set \
  --source-manifest perf-corpora/color-validation-required/independent-chart-source.json \
  --source-root /path/to/private/middlebury-chart-reference
./build/dev-macos-debug/tests/metal_aces2_output_lut_parity_test \
  --tagged-set /path/to/private/middlebury-chart-tagged-set/tagged-reference-set.json
```

The builder covers six manufacturers, two illuminants, and four exposures; it creates 48 tagged
images plus 48 provenance reports and publishes only after the strict validator rehashes all 96
artifacts and the complete Cartesian matrix. The validator rejects false promotion, path escapes,
missing or duplicate cells, artifact substitution, report substitution, inconsistent ICC payloads,
and any decoded-pixel identity failure. Source files remain immutable. This evidence set remains
non-promotable until it is bound into the renderer comparison corpus and the outstanding RAW,
measurement, rights, DCP, skin, and human-review gates are satisfied.

On a real Metal device, the final command verifies every one of the 11,751,552 chart pixels against
the CPU implementation without retaining an unbounded error array. The p99 gate is evaluated by an
exact threshold-exceedance count (`Delta E 00 <= 0.25`) and the maximum must remain at or below
`0.75`; alpha, finite output, and maximum channel error are checked independently. A host without a
Metal device returns the standard skipped-test code and cannot close this hardware gate.

Validate the checked-in contract without materializing assets:

```sh
python3 -B scripts/color/validate-color-corpus.py \
  perf-corpora/color-validation-required/manifest.example.json \
  --schema-only
```

Validate a populated corpus, including every referenced byte:

```sh
python3 -B scripts/color/validate-color-corpus.py \
  /path/to/licensed-color-corpus/manifest.json
```

Before changing the default rendering model or a built-in look, require the blinded promotion gate:

```sh
python3 -B scripts/color/validate-color-corpus.py \
  /path/to/licensed-color-corpus/manifest.json \
  --require-promotion-approval
```

The current 14 built-in photographic looks are also bound to
`built-in-preset-inventory.json`. The validator hashes the exact source region containing their
parameters and independently extracts every name, pack, and category. Any value, order, count, or
metadata change therefore invalidates prior review evidence before preset retuning can be promoted:

```sh
python3 -B scripts/color/validate-built-in-preset-inventory.py
```

Final preset approval uses `photo-editor-built-in-preset-approval:v1`. Every inventory look gets a
separate license-validated corpus whose candidate identity is
`aces2-ocio-v1|preset:<exact name>`, a complete camera/ICC/SDR/HDR/proof/preview/export aggregate,
and the corpus's two blinded editorial approvals. With the current inventory that is 14 looks x 4
minimum captures x 7 paths, or at least 392 independently referenced comparisons. The approval
validator reruns every aggregate instead of trusting its `passed` field:

```sh
python3 -B scripts/color/validate-built-in-preset-approval.py \
  /path/to/production/preset-approval.json
```

Physical display and print sign-off uses `photo-editor-physical-color-output-proof:v1`. Display
proof requires distinct SDR and HDR devices, immutable assigned ICC payloads, at least four live
switches with cache rebuild and zero stale frames, calibrated-instrument patch measurements, and
two independent viewers. Printer proof requires at least two paper/ink combinations, measured
output-profile results, profile/readback identity, and the same independent approval rule:

```sh
python3 -B scripts/color/validate-physical-color-output-proof.py display-proof.json
python3 -B scripts/color/validate-physical-color-output-proof.py printer-proof.json
```

## Required evidence

The v1 contract rejects a corpus unless it has all of the following:

- At least four RAW captures from at least three distinct camera make/model pairs.
- Daylight, tungsten, fluorescent/LED, and mixed-light captures.
- Deep skin, high-ISO shadows, clipped highlights, foliage, sky, neon/LED, and saturated-textile coverage.
- DCP and ICC profile bytes linked by ID rather than inferred from filenames.
- A measured-chart reference and an independently produced render, both explicitly independent of this engine.
- Camera-input, ICC, SDR, HDR, proof, preview, and export execution-path coverage.
- Precommitted, path-specific Delta E 00, Delta E ITP, alpha, and linear-nit quality gates stored inside the hashed manifest.
- A candidate-model recipe and normalized, content-described review regions for every capture.
- SHA-256 and byte-size verification for captures, profiles, references, recipes, and blinded-comparison manifests.
- An approved license review for every file-backed asset.

`--require-promotion-approval` additionally requires a blinded comparison set containing every capture and all editorial categories, plus approvals from two distinct reviewers. Review identifiers are durable pseudonymous IDs; they must identify separate people in the project’s protected review register.

The example manifest is schema documentation, not color evidence. Its paths, digests, sizes, cameras, licenses, and reviewers are deliberate placeholders and must never be accepted as a populated corpus.

## Perceptual comparison artifacts

Desktop builds include `color_corpus_comparison_tool`. It rejects untagged inputs, converts both embedded source color spaces to linear Rec.2020 D65, computes Delta E 00 and Delta E ITP, checks alpha independently, and writes a machine-readable report plus a bounded false-color PNG heatmap. Percentile gates use exact threshold exceedance counts; the reported percentile value is a conservative fixed-histogram upper bound, so full-resolution images do not require an unbounded per-pixel error allocation.

Each invocation names the exact corpus, capture, recipe, profile, rendering models, and execution path:

```sh
build/dev-macos-debug/tests/color_corpus_comparison_tool \
  --candidate /path/to/rendered/candidate-preview.tif \
  --reference /path/to/reference/independent-preview.tif \
  --report /path/to/artifacts/capture-daylight-preview.json \
  --heatmap /path/to/artifacts/capture-daylight-preview.png \
  --corpus-manifest /path/to/licensed-color-corpus/manifest.json \
  --capture-id capture-daylight-chart \
  --execution-path preview \
  --candidate-rendering-model aces2-ocio-v1 \
  --reference-rendering-model independent-cmm-v1 \
  --recipe-sha256 REPLACE_WITH_64_LOWERCASE_HEX_CHARACTERS \
  --output-profile-sha256 REPLACE_WITH_64_LOWERCASE_HEX_CHARACTERS
```

The tool reads thresholds and the linear-to-nits interpretation from the selected execution path's `quality_gates` entry. It does not accept command-line threshold overrides. Values in the schema example demonstrate the required explicit contract; they are not blanket acceptance limits. Establish production limits from an independent baseline before evaluating a candidate, then change them only through a reviewed manifest version.

After producing one report per capture and execution path, require the complete matrix and write the aggregate CI artifact:

```sh
python3 -B scripts/color/aggregate-color-corpus-report.py \
  /path/to/licensed-color-corpus/manifest.json \
  /path/to/comparison-reports \
  --output /path/to/artifacts/color-corpus-aggregate.json
```

The aggregate gate revalidates every corpus byte, requires all seven paths for every capture, re-hashes candidate/reference/heatmap artifacts, and resolves the reported model, recipe, and profile identities through the selected capture. It also rejects duplicate or missing reports, post-hoc threshold changes, inconsistent pixel counts, false metric booleans, incomplete review-region provenance, and tampered artifacts. Failed runs still write an aggregate JSON error artifact so CI can preserve the exact reason.

## Full-matrix runner

The comparison set's `artifact_manifest_path` must identify a `photo-editor-color-corpus-render-set:v1` document. The corpus hashes that render set, avoiding mutable path discovery. The render set declares:

- The corpus ID/version and blinded comparison-set ID.
- The candidate model and independently produced reference-renderer identity.
- Exactly one candidate/reference pair for every capture and execution path.
- The linked independent-reference asset and output-profile asset for each pair.
- Exact byte size, SHA-256 digest, and declared color encoding for both rendered files.

Execute the entire matrix with one command:

```sh
python3 -B scripts/color/run-color-corpus-gate.py \
  /path/to/licensed-color-corpus/manifest.json \
  candidate-default-ab-v1 \
  --comparison-tool build/release-macos/tests/color_corpus_comparison_tool \
  --output-dir /path/to/new/color-corpus-run
```

Use `--validate-only` to verify corpus, comparison-set, render-set, relation, license, and artifact bytes before the native tool is available. A normal run creates a new output directory and never mixes with stale reports. It executes all matrix cells even when one comparison fails, then writes `aggregate.json` and `run-summary.json`; the final status remains failed while preserving every usable report, heatmap, return code, and bounded stdout/stderr record.

The hash chain is intentionally acyclic:

1. The corpus manifest pins source/profile/reference/recipe bytes and the render-set bytes.
2. The render set pins candidate and independent-reference render bytes.
3. Each native report records the corpus-manifest hash and re-hashes its candidate, reference, and heatmap.

## Per-camera support and fallback publication

After producing a real-RAW render set, publish the camera support matrix from the validated reports:

```sh
python3 -B scripts/color/publish-camera-support-report.py \
  /path/to/private/real-raw-render-set/real-raw-render-set.json \
  --source-inventory /path/to/private/color-source-inventory/source-inventory.json \
  --artifact-root /path/to/private/real-raw-render-set \
  --output /path/to/artifacts/camera-support-report.json
```

The report groups every capture by exact make/model, lists both portable-LibRaw and shipping-default
decoder backends and camera-characterization identities, and explicitly identifies when the shipping
route resolved to the portable LibRaw fallback. It first revalidates all underlying render artifacts
and retains every promotion blocker. A report produced from the public source inventory therefore
remains non-promotable; support execution is not a substitute for license, independent-reference,
or editorial approval. Revalidate a published report against current evidence with
`--validate-report /path/to/artifacts/camera-support-report.json` instead of `--output`.
4. The run summary hashes the render set and aggregate report.

This prevents a candidate, reference, threshold, recipe, profile, or report from being replaced without invalidating a named upstream identity.
