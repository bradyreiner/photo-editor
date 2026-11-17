import { Audio } from "@remotion/media";
import { Sequence, interpolate, staticFile, useVideoConfig } from "remotion";
import { clamp } from "../launch/design";
// Operation-driven foley and a phrase-length extension of the approved cue.
const operations = [
  9.15, 10.2, 12.75, 14.8, 16.6, 23.9, 26.2, 31.45, 32.1, 32.75, 35.25, 36.1,
  40.3, 45.15, 50.35, 51.15, 53.92, 54.56, 65, 68.6, 71.5, 75.5, 84.7, 87.1,
  89.3, 94.2, 99, 102.2, 107.2, 113.8, 126.8, 135.1, 139.1, 143, 145,
];
const camera = [
  1.6, 3.2, 5.1, 11.1, 18.7, 22.6, 29.8, 37.8, 42.9, 47.5, 62.8, 74.8, 81.7,
  95.7, 105, 109.5, 115.8, 137.7, 147.7,
];
export const CampaignScore = () => {
  const { fps } = useVideoConfig();
  return (
    <>
      <Audio
        src={staticFile("audio/looksy-workflows-score.wav")}
        volume={(f) =>
          interpolate(
            f,
            [
              0, 0.18, 5, 12, 24, 30, 38, 52, 64, 78, 82, 96, 110, 116, 122,
              134, 144, 148, 152, 154,
            ].map((s) => s * fps),
            [
              0.65, 0.74, 0.78, 0.68, 0.74, 0.62, 0.78, 0.8, 0.82, 0.65, 0.82,
              0.76, 0.78, 0.82, 0.57, 0.62, 0.72, 0.85, 0.7, 0,
            ],
            clamp,
          )
        }
      />
      {operations.map((t, i) => (
        <Sequence
          key={t}
          name={`Control / ${t}s`}
          from={Math.round(t * fps)}
          durationInFrames={Math.round(0.45 * fps)}
          layout="none"
        >
          <Audio
            src={staticFile(
              `audio/foley/${i % 4 === 0 ? "classic-click" : "interface-click"}.mp3`,
            )}
            volume={i % 4 === 0 ? 0.08 : 0.18}
          />
        </Sequence>
      ))}
      {camera.map((t) => (
        <Sequence
          key={t}
          name={`Camera / ${t}s`}
          from={Math.round(t * fps)}
          durationInFrames={Math.round(1.4 * fps)}
          layout="none"
        >
          <Audio
            src={staticFile("audio/foley/quick-air-woosh.mp3")}
            volume={(f) =>
              interpolate(
                f,
                [0, 0.06 * fps, 0.8 * fps, 1.4 * fps],
                [0, 0.055, 0.055, 0],
                clamp,
              )
            }
          />
        </Sequence>
      ))}
    </>
  );
};
