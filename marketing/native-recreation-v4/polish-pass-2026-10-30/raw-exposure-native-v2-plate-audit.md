# RAW Exposure scene audit — pass3

Status: audit only; no native mutation is needed from the pass3 snapshot.

The 16–22 second comparison already uses nine actual native color-engine output plates from `IMG_4969.CR2`. `studio-color.cpp` decodes the CR2 with LibRaw camera white balance, applies the Looksy color pipeline, then publishes the plates through ACES2 and sRGB. The first stage writes `0-0` through `0-8` at exposure values 0.000 through +1.400 EV in +0.175 EV steps. The files are 1440×960 preview renders from the 6024×4020 source.

The native timeline uses G3–G11 to crossfade those plates into the AFTER side. G12 remains the unexposed BEFORE image. The EV labels on G24–G26 and G22’s slider knob are already synchronized with that plate blend. Across 180 sampled frames from 16.000s through 21.967s, the readout differs from the log2 weighted plate estimate by at most 0.00734 EV (mean 0.00163 EV). The knob differs from the normalized readout by at most 0.00356 (mean 0.00106). Representative points:

| Sequence time | Dominant plates / weights | Blend estimate | Existing readout |
|---:|---|---:|---:|
| 16.000 | 0-0 100% | +0.000 EV | +0.00 EV |
| 17.500 | 0-0 46.9%, 0-1 53.1% | +0.095 EV | +0.09 EV |
| 17.733 | 0-1 100% | +0.175 EV | +0.17 EV |
| 18.000 | 0-1 21.6%, 0-2 78.4% | +0.314 EV | +0.31 EV |
| 18.500 | 0-3 39.4%, 0-4 60.6% | +0.634 EV | +0.63 EV |
| 19.000 | 0-5 46.7%, 0-6 53.3% | +0.971 EV | +0.97 EV |
| 19.500 | 0-7 94.3%, 0-8 5.7% | +1.236 EV | +1.23 EV |
| 20.000 | 0-7 14.8%, 0-8 85.2% | +1.375 EV | +1.37 EV |
| 20.500–21.900 | 0-8 100% | +1.400 EV | +1.40 EV |

The ordinary timeline Exposure effect is a different operation: amount −1…+1 is compiled to a brightness offset of `amount × 0.40`, not EV. The shared photo-grade recipe and `grade:exposure` keyframe path reject photo clips and only accept video or adjustment clips. Adding ordinary Exposure to these output plates would apply a second transform and break the correct EV labels; using an adjustment clip could also grade interface elements. The v1 normalized-brightness overlay proposal has been marked superseded and must not be executed.

## Native review captures

The root owns the native render review. Capture the existing pass3 sequence at 16.000, 17.000, 17.733, 18.000, 18.500, 19.000, 19.500, 20.000, 20.500, 21.000, and 21.900 seconds. Use [raw-exposure-native-v2-plate-audit.json](raw-exposure-native-v2-plate-audit.json) for stable clip IDs, plate hashes, and expected blend weights.

Compare these 1920×1080 regions:

- AFTER photo: `(1110,260)–(1570,840)`; median and mean luminance should rise monotonically as the native engine plates advance.
- BEFORE reference: `(690,260)–(1030,840)`; image pixels should remain unchanged apart from the split/control graphics.
- Slider/readout: `(730,880)–(1530,1030)`; preserve the accepted composition and legible `+N.NN EV` copy.

The expected exposure values above are inferred from the native source plate EVs and timeline alpha weights. They are not pixel-measurement receipts. No MCP call, native mutation, or new asset/track was made in this audit.

The accurate claim for this scene is: native RAW-derived color-engine output plates are blended by editable native timeline opacity curves. This does not show a live RAW grade recipe attached to the timeline photo clip.
