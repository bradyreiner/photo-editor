import { Audio } from "@remotion/media";
import { Sequence, interpolate, staticFile, useVideoConfig } from "remotion";
import { clamp } from "./system";

const clicks = [
  8, 12, 15.3, 17.1, 20, 23, 27.1, 31.5, 32.7, 33.25, 44.2, 45.3, 50, 52.5,
  59.5, 61, 64, 67.7, 80, 89, 97, 102, 109.2, 113.4, 114.7,
];
const moves = [5, 12.45, 34.55, 55.7, 72.6, 86.85, 116.7];
// Whole produced cue; musical development and its actual ending are preserved.
// Small act-level changes make room for inspection and review, without looping.
export const FilmScore = () => {
  const { fps } = useVideoConfig();
  return (
    <>
      <Audio
        src={staticFile("audio/looksy-continuous-score.wav")}
        volume={(frame) =>
          interpolate(
            frame,
            [
              0, 0.3, 12.5, 13.2, 18, 24, 25, 34.5, 36, 48.5, 49.5, 56, 68,
              72.2, 73.5, 86.5, 87.5, 93.5, 94.5, 104, 105.5, 112.5, 113.3,
              116.4, 117.6, 119, 120,
            ].map((t) => t * fps),
            [
              0, 0.62, 0.62, 0.52, 0.52, 0.6, 0.62, 0.62, 0.72, 0.72, 0.6, 0.74,
              0.74, 0.65, 0.72, 0.72, 0.5, 0.5, 0.58, 0.58, 0.62, 0.62, 0.48,
              0.48, 0.65, 0.5, 0,
            ],
            clamp,
          )
        }
      />
      {clicks.map((time, index) => (
        <Sequence
          key={`click-${time}`}
          name={`Tactile control / ${time}s`}
          from={Math.round(time * fps)}
          durationInFrames={Math.round(0.45 * fps)}
          layout="none"
        >
          <Audio
            src={staticFile(
              `audio/foley/${index % 3 === 0 ? "classic-click" : "interface-click"}.mp3`,
            )}
            volume={index % 3 === 0 ? 0.14 : 0.09}
          />
        </Sequence>
      ))}
      {moves.map((time) => (
        <Sequence
          key={`move-${time}`}
          name={`Camera handoff / ${time}s`}
          from={Math.round(time * fps)}
          durationInFrames={Math.round(1.4 * fps)}
          layout="none"
        >
          <Audio
            src={staticFile("audio/foley/quick-air-woosh.mp3")}
            volume={(frame) =>
              interpolate(
                frame,
                [0, 0.06 * fps, 0.8 * fps, 1.4 * fps],
                [0, 0.065, 0.065, 0],
                clamp,
              )
            }
          />
        </Sequence>
      ))}
    </>
  );
};
