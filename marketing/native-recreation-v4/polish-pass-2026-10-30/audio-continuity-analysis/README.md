# Native excerpt audio continuity check

Run this after a completed 8-second native intro excerpt is available. It is offline: it reads the MP4 and the selected New Gods MP3, decodes them with FFmpeg, measures sample-offset/correlation/gain, and records ffprobe output. It does not listen, open the app, or call the native worker.

From the repository root:

```sh
marketing/.venv/bin/python marketing/native-recreation-v4/polish-pass-2026-10-30/audio-continuity-analysis/analyze_native_excerpt_audio.py \
  marketing/native-recreation-v4/polish-pass-2026-10-30/native-intro-audio-clock-p11/looksy-intro-new-gods-audio-fixed-0-8.mp4 \
  --report marketing/native-recreation-v4/polish-pass-2026-10-30/native-intro-audio-clock-p11/audio-clock-continuity-analysis.json \
  --native-request-id c4208b03-66c4-43c7-a9f2-a981935e981a \
  --native-job-id 9168b593-f265-41d2-9bf1-f81e4257948a \
  --native-document-hash bbf04be03ea2c7848adfc4b549fbacd60dd99adbf006faa29f3006ee4147553d
```

By default it uses the selected source file at `music-candidates/downloads/public-candidates/HoliznaCC0 - New Gods.mp3`, the 17.828-second source trim, −3.7 dB gain, and the previous Pass 10 intro as a before comparison. It checks 1–2, 4–5, 5–6, and 6–7 seconds with ±1500 decoded samples of lag search. The lag sign matches the prior Pass 10 diagnostic: a negative value means the excerpt is late relative to the expected source position. Pass criteria are lag within ±1 sample for every window, gain within ±0.1 dB, and H.264/AAC stereo 48 kHz, 240 frames at 30 fps, eight-second stream/container durations, and aligned stream starts.

The report contains full ffprobe JSON, SHA-256 for source/MP4/decoded PCM, sample counts, per-window and per-channel fit statistics, and an explicit `audioListeningPerformed: false`. A nonzero exit code means the current excerpt failed one or more stated checks; inspect the window metrics before drawing conclusions about which stage drifted.

The earlier Pass 10 MP4’s known 4–7s offset is −790 samples (−16.4583 ms). The analyzer remeasures that baseline rather than treating the historical number as proof that the current file is fixed.
