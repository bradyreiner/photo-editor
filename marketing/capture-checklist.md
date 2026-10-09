# Looksy marketing capture checklist

Use this before recording footage that could appear in an ad. Campaign scripts are plans; the current [capture status](evidence/current-capture-status.md) records what has actually been demonstrated. Re-record the real product workflow and review the final exports before public use.

## Before capture

- Pick one campaign script, one audience question, and one clean demo project. Keep private client files, personal catalog names, notifications, and unrelated windows out of frame.
- Confirm the current app build opens cleanly and the needed workflow is available. Capture the app version/build identity with the uncut recording notes.
- Use source media you made or media whose exact commercial-use terms you have saved. Record the original filename, source/creator, license, download date, relevant releases, and SHA-256. A filename or stock-site homepage alone is not a rights record.
- Choose the capture type before recording: still screen capture (`image`), moving screen recording (`video`), source photo (`image`), source clip (`video`), or licensed/owned audio (`audio`). App evidence uses `capture` plus `--media-type image|video`.
- If using a founder face, voice, location, or another recognizable person, confirm consent and permitted ad use.

## Record one honest proof path

1. **Founder take:** Record a short natural hook, why this workflow matters to you, one real reaction, and the CTA. Keep a clean audio take. Candid, cinematic, challenge, and reply styles can use the same factual app capture.
2. **Source:** Show the real starting file and what it is. For RAW, keep the supported original camera file and its metadata. For Sony log, record the camera mode/profile and preserve the source clip. Avoid calling a flat-looking image “log” when the source profile is unknown.
3. **App action:** Keep a continuous screen recording of the real interaction. For the local agent, show the actual model/setup used, your exact brief, its real response, and the relevant tool/app action. Never replace a missed step with staged output.
4. **Human revision:** Show what you reviewed, the correction or manual change you made, and the version you chose. If the first result was imperfect, keep that real iteration; do not imply one-click or automatic work that did not occur.
5. **Output:** Complete the actual export. Open the saved file outside the app and inspect it at phone size. For Carousel, check every exported board and order. For Timeline, review the exported clip. Keep the exact output files and note any limitation.
6. **Color evidence:** Keep color-specific proof separate from general creative proof. For a social sRGB statement, inspect the saved export’s profile. For S-Log, retain original source metadata and the matching S-Log2/S-Gamut or S-Log3/S-Gamut3 profile selection. Review source, preview, and export; avoid broad accuracy or parity claims from one attractive frame.
7. **Offline wording:** Say a run stayed on the computer only when the complete workflow was captured with the network disconnected. A visible local model setting by itself is not proof of an offline run.

Record in 9:16 for Reels-first edits, keep important UI and captions legible above platform overlays, and save a clean master without borrowed trend audio. Add music only from a source whose current license covers the intended organic or paid use. Keep a separate organic trend-audio version only after checking that platform’s current terms.

## Import an asset safely

The intake tool copies a validated file into `marketing/media/incoming/` using a SHA-256-prefixed sanitized name, then changes only the selected asset record in the manifest. It leaves the original source in place. If bytes differ from the manifest’s asset, it resets rights and proof fields to `internal-review` / `pending` and clears both reference lists; review and restore evidence manually after the new file is checked.

Preview the update first:

```sh
marketing/.venv/bin/python marketing/tools/import_asset.py \
  --manifest marketing/campaigns/creative-control/campaign.json \
  --asset agent-ui \
  --source /path/to/your/actual-screen-recording.png \
  --kind capture \
  --media-type image \
  --note "Clean current-build agent workflow capture" \
  --dry-run
```

Then repeat the command without `--dry-run`. Use `--media-type video` for a moving app capture; omit `--kind` when retaining the asset’s existing kind. For a source still or clip, select `--kind image` or `--kind video`. Raster images are fully decoded with Pillow; audio/video streams are probed and fully decoded with FFmpeg. Keep genuine camera RAW originals alongside the capture evidence and import a rendered PNG/JPEG proxy for campaign layouts; a filename extension or RAW header alone does not prove Looksy decoded it.

The note is stored on that asset as `import_note`; it does not clear rights or prove a product claim. After import, add reviewed rights/evidence files and references to the manifest only when they support the new bytes. Keep the uncut master and a small capture note alongside the project, including actual model identity, source/output hashes, edits shown, and human review.

## Before publishing

- Review the current capture-status evidence and confirm this footage is from the current app build.
- Confirm source, model, edits, human revision, and export all match the narration and captions.
- Confirm the exact asset IDs, hash, rights record, attribution, and person/property permissions. New media starts in internal review after its bytes change.
- Check captions against spoken words, sound level, crop, and platform safe area on an actual phone.
- Keep campaign, hook, platform, audio/license, post copy, and performance observations together in the experiment tracker. Publish only when the CTA destination is live.
