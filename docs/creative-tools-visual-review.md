# Creative-tools photographic review

Use this gate after the fixed photographic corpus has been assembled and its bytes and license records pass `scripts/perf/check-real-image-corpus.py`. The corpus manifest must contain exactly 12 assets and assign one `creative_categories` entry to every photo: `portrait` (3), `night` (2), `architecture` (2), `product_glass` (2), `landscape` (2), or `monochrome` (1). Keeping the groups exclusive preserves the plan's intended 12-photo mix. Include `creative_visual_tags`; at least one portrait must carry each of `hair_detail` and `skin_tone`, and at least one product/glass photo must carry `glass_transparency`, so the difficult subjects appear in the right category.

Prepare a local copy of the pinned, source-traced corpus (the downloads are kept outside the repository):

```sh
python3 scripts/perf/prepare-creative-tools-corpus.py /path/to/creative-tools-corpus
```

The downloader waits between Wikimedia requests, verifies every SHA-256 against the pin, and writes `manifest.json`. To verify a corpus already on disk without network access, add `--verify-only`. See the [source and license receipt](evidence/creative-tools/visual-review-corpus/README.md).

The review packet is JSON with a `reviews` array. Record one row for every family/photo/strength/zoom combination. Each row contains `family` (`F1`…`B3`), `asset_id`, `strength` (`low`, `default`, `expressive`), `zoom` (`fit`, `100%`), `reviewer`, `ratings`, and `artifacts`. Scores are integers from 1–5. Every family requires photographic integration, material credibility, restraint, and artifact freedom; text families also require typographic readability. Mark the non-applicable readability field as `null` for non-text families. `artifacts` must be an empty array on a passing row; list observed seams, halos, clipped glyphs, or repeated marks to fail that row. Keep disagreements and notes in additional row fields so the machine gate does not erase reviewer context. A second reviewer may add a separate row for the same cell; a repeated submission from the same reviewer is rejected.

The verifier requires at least four relevant photos per family, a complete low/default/expressive × fit/100% matrix for each selected photo, no applicable score below 3, and a family mean of at least 4. It rejects duplicate cells, unknown assets, and unlicensed/unverified corpus entries. It does not decide whether an image is tasteful; a human reviewer must enter the scores and own them.

Run the contract tests:

```sh
python3 scripts/perf/test-validate-creative-tools-review.py
```

Run the qualification gate against real, hash-verified local assets and the completed review packet:

```sh
python3 scripts/perf/validate-creative-tools-review.py \
  /path/to/creative-corpus/manifest.json \
  /path/to/creative-corpus/visual-review.json
```

`--skip-corpus-file-check` exists only for review-packet schema development against synthetic fixtures. Never use it to report acceptance.
