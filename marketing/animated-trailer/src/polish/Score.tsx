import { Audio } from "@remotion/media";
import { Sequence, interpolate, staticFile, useVideoConfig } from "remotion";
import { clamp } from "../launch/design";
// Operation-driven foley and a phrase-length extension of the approved cue.
const operations = [
  -0.25, 1.65, 4.55, 9.15, 10.2, 12.75, 14.8, 16.6, 23.9, 26.2, 31.45, 32.1,
  32.75, 35.25, 36.1, 40.3, 45.15, 50.35, 51.15, 53.92, 54.56, 65, 68.6, 71.5,
  75.5, 84.7, 87.1, 89.3, 94.2, 99, 102.2, 107.2, 113.8, 126.8, 135.1, 139.1,
  143, 145,
];
const camera = [
  -1.9, -0.7, 1.4, 3.9, 6.5, 11.1, 18.7, 22.6, 29.8, 37.8, 42.9, 47.5, 62.8,
  74.8, 81.7, 95.7, 105, 109.5, 115.8, 137.7, 147.7,
];
export const CampaignScore = ({
  withMusic = true,
  track = "no-warning",
}: {
  withMusic?: boolean;
  track?: "no-warning" | "coffee-or-tea" | "sun-in-your-eyes";
}) => {
  const { fps } = useVideoConfig();
  return (
    <>
      {withMusic ? (
        <Audio
          src={staticFile(`audio/looksy-polish-${track}.wav`)}
          volume={1}
        />
      ) : null}
      {operations.map((t, i) => (
        <Sequence
          key={t}
          name={`Control / ${t}s`}
          from={Math.round((t + 4) * fps)}
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
          from={Math.round((t + 4) * fps)}
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
