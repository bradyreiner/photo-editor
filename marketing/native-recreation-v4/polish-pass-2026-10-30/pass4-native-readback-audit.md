# Pass 4 native readback audit

## Result

P4 matches the completed 12-operation native receipt. Exactly 12 clips changed, and they are exactly the 12 receipt targets. The project retains 192 tracks, 1,222 clips, a 1920×1080 landscape sequence at 30 fps, and 298 seconds. No non-target clip fields or track metadata changed.

P3/P4 file SHA-256: `1c3eac4733991206cd41c661cd15c716513bfca520e2c52e2a6fb388b97e4234` / `57e413fa5d17f5cc46bc7ca211aba85d5d65ab436641daa1a3a69bf8dea778a8`. P4 native document hash: `37805b1fe77d62329132e3ef1cd87194c8236580597e9ad7284d3d616f51e1d3`. Receipt status: `authored-awaiting-native-review`; native task: `b9fc7d05-629e-47e7-8bd3-920a94207f0c`.

## Authored changes

- **Five closing hero clips:** opacity is 1 through local 7, reaches 0 at local 8, and stays 0 through local 18.
- **Four endcard clips:** opacity reaches 1 at local 9 and holds through local 18; vertical framing reaches its final position at local 9 and holds. Every landscape `framingKeyframes` array matches that clip’s main `frameOffsetY` keys. Other landscape typography/style overrides match P3.
- **Three RAW chapter titles:** start remains 44; duration and trim become 6 seconds. Title text, animation keyframes, and landscape typography/framing overrides match P3. At local 5, opacity is 1 and vertical offset is 0; those final values hold through the extended clip end.

## Scope and metadata

All 12 receipt steps returned `ok: true`, and the receipt plan hash matches the bound plan. P4 preserves track IDs, clip IDs, and per-track clip ordering. The only top-level project field change is `updatedAt`. Track metadata, source catalog/asset metadata, source identity, paths, and target source fields are unchanged.

## Limits

This audit reads saved project and receipt files only; it did not contact the live worker. It does not qualify decoded frames or a native review render. The receipt remains `authored-awaiting-native-review`.

## Detailed records

| Group | Count | Verified result |
|---|---:|---|
| Closing hero opacity | 5 | Clear by local 8, held at zero through local 18 |
| Endcard opacity and settle | 4 | Opacity and Y settle at local 9; landscape framing keys match main Y keys |
| RAW title hold | 3 | 44–50 timing; title keyframes and typography unchanged |
| Non-target clip field changes | 0 | All 1,210 non-target clips unchanged |
