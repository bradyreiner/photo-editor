import { Audio } from "@remotion/media";
import { Sequence, interpolate, staticFile, useVideoConfig } from "remotion";
import { clamp } from "./design";

// Produced cue, sparse tactile events, and a few purposeful camera air moves.
// All sound placements refer to pictured operations, not chapter title changes.
const touches = [
  6.64, 7.05, 10.66, 14.47, 20.2, 22.15, 24.27, 33.15, 38.35, 39.15, 41.92,
  42.56, 49, 52.6, 55.5, 59.5, 76.1, 96.5, 103.1, 107, 109, 110,
];
const air = [
  3.5, 5.9, 9.15, 13.75, 18, 26.9, 31.5, 46.8, 58.8, 65.95, 74.1, 105.2, 115.4,
];
export const LaunchScore = () => {
  const { fps } = useVideoConfig();
  const pictureTime = (time: number) => (time >= 18 ? time + 4 : time);
  return (
    <>
      <Audio
        src={staticFile("audio/looksy-launch-score.wav")}
        volume={(frame) =>
          interpolate(
            frame,
            [
              0, 0.2, 3.5, 4, 10, 18, 26, 32, 44, 58, 66, 80, 88, 98, 106, 112,
              117, 119, 120,
            ].map((t) => pictureTime(t) * fps),
            [
              0.35, 0.55, 0.55, 0.75, 0.75, 0.65, 0.7, 0.72, 0.82, 0.75, 0.85,
              0.7, 0.6, 0.66, 0.6, 0.8, 0.8, 0.5, 0,
            ],
            clamp,
          )
        }
      />
      {touches.map((time, i) => (
        <Sequence
          key={`touch-${time}`}
          name={`Native operation / ${time}s`}
          from={Math.round(pictureTime(time) * fps)}
          durationInFrames={Math.round(0.45 * fps)}
          layout="none"
        >
          <Audio
            src={staticFile(
              `audio/foley/${i % 4 === 0 ? "classic-click" : "interface-click"}.mp3`,
            )}
            // The source clicks differ by almost 7 dB at their stereo peaks.
            // Match them near -23.5 dBFS instead of burying the interface click.
            volume={i % 4 === 0 ? 0.1 : 0.22}
          />
        </Sequence>
      ))}
      {air.map((time) => (
        <Sequence
          key={`air-${time}`}
          name={`Camera / ${time}s`}
          from={Math.round(pictureTime(time) * fps)}
          durationInFrames={Math.round(1.4 * fps)}
          layout="none"
        >
          <Audio
            src={staticFile("audio/foley/quick-air-woosh.mp3")}
            volume={(frame) =>
              interpolate(
                frame,
                [0, 0.06 * fps, 0.8 * fps, 1.4 * fps],
                [0, 0.08, 0.08, 0],
                clamp,
              )
            }
          />
        </Sequence>
      ))}
    </>
  );
};
