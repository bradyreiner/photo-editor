# Agent Photographer Taste Calibration

Use this workflow to calibrate the Agent's deterministic pixel gates against a photographer's judgment. Machine evidence can decide whether a proof is safe enough to show, but it cannot set the human rating.

## 1. Run the exact corpus

```sh
build/dev-macos-debug/tests/agent_edit_controller_test \
  --real-photo-corpus /path/to/manifest.json \
  /path/to/agent-quality-report.json
```

Do not build a review packet from a partial or stale report. The packet builder requires the manifest and report to contain exactly the same case IDs.

## 2. Build the local review packet

```sh
python3 -B scripts/perf/agent-human-review-packet.py build \
  --manifest /path/to/manifest.json \
  --report /path/to/agent-quality-report.json \
  --output /path/to/review-packet
```

Open `review-packet/index.html`. For every before/after pair, the photographer must choose **Deliver** or **Do not deliver**, select an overall 1–5 rating, score intent match, naturalness, artifact freedom, and protected fidelity, and write a note. A failed case also requires at least one structured reason tag. Protected regions and retouch targets are drawn over the After proof. The page will not export until every case and the photographer name are complete.

The same handoff is available from the desktop Agent: open **Taste**, choose **Review proofs**, and select either the packet's `index.html` or `review-contract.json`. Before opening the page, the controller verifies the packet digest, both proof hashes for every case, packet-local paths, case counts, blind-review safety flags, and the absence of machine verdicts or source paths from the HTML payload. The handoff writes no memory and cannot complete a rating for the photographer.

Each case includes a blind light table with side-by-side, movable split, and After-only views. Hold `B` to reveal the Before proof while judging a single result; the left and right arrow keys move the active split. **Next unfinished** returns to the first incomplete case. Decisions, scores, notes, and the photographer name autosave under the exact packet digest in private browser storage, so a long review can be resumed without copying proof pixels or leaking into a different packet. **Clear draft** removes that local session after confirmation.

The packet copies and hashes the exact proof pixels. The machine result remains in the signed `review-contract.json` evidence but is not embedded in the review page or its JavaScript payload. Source manifest and machine-report paths are also omitted from the page, so neither the verdict nor local filesystem provenance can anchor the photographer's judgment or leak through browser inspection.

## 3. Export and finish signed ratings

After the ninth case, choose **Copy for Framehouse**, return to the desktop Taste card, and choose **Finish copied ratings**. The clipboard contains only the signed ratings, notes, and packet digests—never proof pixels or source paths—and the app clears it only after successful verification. The app reuses the exact packet it already verified, so no file selection is required. **Download JSON** plus **Finish ratings** remains the explicit recovery path when clipboard access is unavailable. The verified packet reference is session-only, bound to the exact active project, and cleared after a successful finish or project change. After relaunch, project change, or when no verified packet is active, select the packet's `index.html` or `review-contract.json` as the explicit safe fallback.

The controller re-verifies the packet, every proof hash, the unchanged source manifest and machine report, the exact case set, the blind-review attestation, photographer identity, timezone-aware signature, decisions, integer scores, dimensions, issue tags, and notes. It writes a new reviewed manifest and signed taste profile under `review-packet/calibration-results/`, leaves the source manifest unchanged, and activates only the compact profile for the current project. Repeating the same finish action is idempotent; an existing result with different bytes fails closed. The Agent cannot supply or repair a photographer rating, mutate a proof threshold, or claim professional readiness.

For automation or recovery, the equivalent CLI apply step remains available:

```sh
python3 -B scripts/perf/agent-human-review-packet.py apply \
  --manifest /path/to/manifest.json \
  --packet /path/to/review-packet/review-contract.json \
  --ratings /path/to/agent-photographer-calibration.json \
  --output-manifest /path/to/reviewed-manifest.json
```

The apply command fails closed if the manifest changed, the packet was altered, the ratings refer to different proofs, the blind-review attestation is missing, any case is missing, or any decision, dimension, required failure tag, note, photographer, or timestamp is incomplete. It refuses to overwrite the source manifest.

## 4. Build the local taste profile from the CLI fallback

```sh
python3 -B scripts/perf/agent-human-review-packet.py profile \
  --manifest /path/to/reviewed-manifest.json \
  --packet /path/to/review-packet/review-contract.json \
  --report /path/to/agent-quality-report.json \
  --output /path/to/photographer-taste-profile.json
```

The versioned profile records dimension averages, issue frequencies, every machine/human agreement, false-confidence case, and over-conservative case. It connects those outcomes to the relevant pixel and retouch threshold evidence. A disagreement produces a `tighten-and-rerun` or `relax-and-rerun` signal, but the tool never mutates a threshold automatically. Any threshold change requires a fresh complete corpus run and a new blind packet. Profile signatures use sorted compact JSON with integral floating-point values normalized to integers so Python-generated evidence verifies byte-for-byte in Qt.

The profile is local and auditable. It explicitly keeps `professionalReadinessClaimed: false`; generating a profile is not the same as passing photographer calibration.

## 5. Import an existing profile into the active project

If **Finish ratings** was used, the profile is already active. To bring in an existing profile produced by the CLI or replace the active one, open **Taste**, choose **Import profile**, and select `photographer-taste-profile.json`. The app verifies the profile signature, safety attestations, provenance digests, case totals, dimension scores, issue tags, and machine/human outcomes before storing a compact project-local copy.

An accepted profile changes future behavior in two bounded ways:

- Planning becomes more restrained around photographer-rejected tendencies and prioritizes the weakest rated photographic dimension.
- Model proof critique receives the same calibration card and keeps calibration-sensitive results in user review when taste remains uncertain.

When the signed profile contains a disagreement, Taste also shows a compact **Calibration Coach** lesson. It names whether the Agent was too confident or too cautious, restates the photographer's deliverability decision, identifies the weakest scored photographic dimension and issue tags, and explains the bounded next move. The lesson is derived deterministically from the signed profile and writes no new memory. It cannot set a rating, train the model automatically, or mutate a threshold; threshold work still requires explicit review and a fresh corpus.

When a finished draft touches one of those calibration-sensitive dimensions, the workspace can offer **Compare restrained version**. This is never automatic: the action renders the current draft beside one bounded, more restrained alternative while preserving framing, masks, retouching, and unrelated edit dimensions. Neither direction becomes durable taste until the photographer explicitly chooses it and commits the edit. The comparison records the calibration profile digest and concern keys so the teaching signal remains auditable and project-local.

The Taste card shows the blind-case count and active calibration summary. It stores no image bytes or source paths, never changes numeric proof thresholds, never overrides the current request, and never claims professional readiness. **Replace calibration** imports a newer signed profile. **Forget** requires confirmation, affects only future plans in the active project, and leaves existing edits unchanged.

## 6. Run the human gate

```sh
python3 -B scripts/perf/check-real-image-corpus.py \
  --require-agent-cases \
  --require-human-ratings \
  /path/to/reviewed-manifest.json
```

A failed human case is a calibration result, not something to relabel. Keep its proof, dimensions, tags, and notes, inspect the profile's threshold signal, fix the editor or threshold, rerun the complete corpus, and create a new blind packet with a new machine-report digest.
