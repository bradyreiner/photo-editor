import React, { createContext, useContext } from "react";
import {
  Sequence as NativeSequence,
  useCurrentFrame,
  useVideoConfig,
} from "remotion";
import { Video as MediaVideo } from "@remotion/media";

const Rate = createContext(1);
export const PacedScene = ({
  original,
  duration,
  children,
}: {
  original: number;
  duration: number;
  children: React.ReactNode;
}) => <Rate.Provider value={original / duration}>{children}</Rate.Provider>;

export const useStoryTime = () => {
  const frame = useCurrentFrame(),
    { fps } = useVideoConfig();
  return (frame / fps) * useContext(Rate);
};

// Image motion, clip boundaries and source playback share one clock.
export const Sequence = ({
  from = 0,
  durationInFrames,
  ...props
}: React.ComponentProps<typeof NativeSequence>) => {
  const rate = useContext(Rate);
  return (
    <NativeSequence
      {...props}
      from={Math.round(from / rate)}
      durationInFrames={
        durationInFrames === undefined || !Number.isFinite(durationInFrames)
          ? durationInFrames
          : Math.round(durationInFrames / rate)
      }
    />
  );
};
export const Video = ({
  playbackRate = 1,
  ...props
}: React.ComponentProps<typeof MediaVideo>) => {
  const rate = useContext(Rate);
  return <MediaVideo {...props} playbackRate={playbackRate * rate} />;
};
