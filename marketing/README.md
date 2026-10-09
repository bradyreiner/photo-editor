# Looksy campaign studio

Two marketing campaigns and a local production factory. Start with [review.html](review.html), then choose a script from the campaign briefs. Nothing here posts content or spends advertising money.

- **The creative future is yours:** local creative power, human direction, candid founder voice.
- **One shoot, many deliverables:** a photo, a carousel, and a video from one coherent creative project.

Each brief contains six timed scripts with narration, captions, shot lists, CTAs, and Instagram/Facebook/Reddit/LinkedIn copy. The scene manifests provide two 24-second cinematic review cuts, plus founder and challenge hook variations. The current cuts use source imagery and clearly labeled development interface previews, not a completed agent/project demonstration. [Capture status](evidence/current-capture-status.md) records the exact limits.

## Start locally

Requirements: Python 3.10+, FFmpeg/ffprobe on PATH, and Pillow. No cloud renderer, paid API, or service account is required for rendering. The bundled generated image was made with the built-in imagegen tool; its prompt and origin are recorded separately. Media can be replaced with owned/cleared footage.

From the repository root:

```sh
python3 -m venv marketing/.venv
marketing/.venv/bin/python -m pip install -r marketing/requirements.txt
marketing/.venv/bin/python marketing/tools/queue.py --campaign all --variant baseline --size preview --mode draft
marketing/.venv/bin/python marketing/tools/build_review.py
python3 -m http.server 8765 --bind 127.0.0.1 --directory marketing
```

Open `http://127.0.0.1:8765/review.html`. The review page has playable films, version selection, download links, scripts, the source ledger, and the production loop. A local server avoids browser restrictions on local video files.

The renderer outputs H.264 MP4 at 30 fps with yuv420p, optional AAC audio, per-scene thumbnails, a contact sheet, SRT captions, and render metadata. Preview canvases are half resolution; full-size canvases are 1080×1920, 1080×1350, and 1920×1080. Caption safe areas are a conservative working layout, not certification for every platform placement; always inspect the actual placement preview.

## Render variations

```sh
# Both campaigns, all three ratios, full resolution, visibly marked drafts:
marketing/.venv/bin/python marketing/tools/queue.py --size final --mode draft

# A founder hook, only vertical, for fast experiments:
marketing/.venv/bin/python marketing/tools/queue.py --campaign creative-control --variant founder --aspect 9:16

# Prepare and validate every hook recipe without rendering:
marketing/.venv/bin/python marketing/tools/queue.py --variant all --prepare-only

# Render every hook and ratio after the footage is ready:
marketing/.venv/bin/python marketing/tools/queue.py --variant all --size final --mode draft

# Refresh the library after rendering:
marketing/.venv/bin/python marketing/tools/build_review.py
```

The generated recipes and movies live under `build/<campaign>/<variant>/<size>/<ratio>/`. Masters stay under `campaigns/<campaign>/campaign.json`; hooks stay in `hooks.json`. Variant preparation copies the master and changes only the opening headline/body so the rest of a hook experiment remains consistent. `--size final` means full resolution. `--mode final` separately means unwatermarked release rendering and has stricter rights/proof checks.

## Add the real captures

Use [capture-checklist.md](capture-checklist.md) for the recording session. The intake CLI preserves the original source and updates one selected asset; changed bytes reset rights/proof review. Preview an import, then repeat without `--dry-run`:

```sh
marketing/.venv/bin/python marketing/tools/import_asset.py \
  --manifest marketing/campaigns/creative-control/campaign.json \
  --asset agent-ui --source /path/to/screen-recording.mp4 \
  --kind capture --media-type video \
  --note "Current build; real local agent run and export" --dry-run
```


1. Pick a campaign script and record an isolated demo project. Keep the continuous recording, founder camera/audio, actual inputs, real agent/actions, a human revision, and completed exports. Avoid personal catalog material.
2. Put images, videos, and audio under `media/`. Record the asset source, current commercial-use terms, attribution, releases where applicable, download date, and SHA-256 in the source ledger. Keep generated media labeled as generated. The supplied stock pack is not a license to publish every conceivable use; see [rights-guide.md](rights-guide.md).
3. Replace a scene asset’s path and kind. For app footage use `type: "app"` and a `kind: "capture"` asset; add `media_type: "video"` for a moving screen recording. This preserves the capture-proof gate for both still and moving app footage. Use ordinary `type: "video"` / `kind: "video"` for source footage. Keep product claim references attached when changing scene types. Use images as creative source material, not fabricated before/after proof.
4. Update rights and proof records after actual review, not merely to satisfy validation. Save evidence files under `evidence/` and reference them from the manifest. The validator checks records and file existence; a human still needs to judge whether the evidence supports the claim.
5. Render drafts, watch them at phone size, review audio/captions and the placement safe zone, and only then use `--mode final`. Current manifests intentionally fail final validation because capture/rights/claim reviews are pending. The scripts do not publish.

For RAW demonstrations use real supported camera files. For camera-log demonstrations preserve the actual supported Sony profile/gamut and source metadata. For sRGB claims inspect the genuine exported file profile. For local-execution messaging preserve the actual local model identity and offline workflow evidence. These proof captures are not present in the initial review cuts.

The audio bed is an original sample-free sine-wave composition with reproducible source at `tools/make_soundbed.py`. Use `audio_track` to replace it with a cleared track or recorded narration; omit the key for silent output. Keep a clean cross-platform ad version. Platform trend audio should be a separately cleared version. SRT currently reflects scene text; replace it with timed spoken-word captions when founder narration is recorded.

## Make the next batch

Use [strategy.md](strategy.md) for the two-week cadence and trend-scanning rubric. Log one row per published platform variant in [experiments.csv](experiments.csv), with observations at roughly 24 hours, 72 hours, and seven days. Compare similar-length videos within one platform, keep paid and organic results separate, and repeat promising ideas before choosing a winner.

One weekly session should collect one real project walkthrough, three opening takes, and a few candid reactions. Turn that into a result-led cut, founder cut, challenge, and a useful comment response. Change one main variable per experiment. Reuse footage; preserve your own words and reactions.

## Validation

```sh
marketing/.venv/bin/python -m unittest discover -s marketing/tests -v
marketing/.venv/bin/python marketing/tools/render_campaign.py validate marketing/campaigns/creative-control/campaign.json
marketing/.venv/bin/python marketing/tools/render_campaign.py validate marketing/campaigns/one-shoot-many-deliverables/campaign.json
```

[evidence/verification.json](evidence/verification.json) records the completed checks and visual review scope for this batch. Downloaded stock files, license snapshots, scripts, recipes, and final review artifacts are local and inspectable; temporary app runtime and the virtual environment are ignored.
