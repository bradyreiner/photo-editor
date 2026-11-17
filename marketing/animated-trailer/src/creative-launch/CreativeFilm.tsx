import { AbsoluteFill, Sequence, staticFile, useVideoConfig } from "remotion";
import { Audio } from "@remotion/media";
import {
  BuyerOpening as Opening,
  BuyerClosing as Closing,
  BuyerCarousel as CarouselChapter,
} from "./BuyerScenes";
import { AgentChapter } from "./AgentPolish";
import { PhotoChapter, LibraryChapter } from "./Photo";
import { CanvasChapter } from "./CanvasPolish";
import { FilmChapter } from "./FilmPolish";
import { creativePlan } from "./Plan";
import { ink } from "./Kit";
import { TransitionVeil } from "./TransitionVeil";

export const CreativeFilm = ({ withMusic = true }: { withMusic?: boolean }) => {
  const { fps } = useVideoConfig();
  return (
    <AbsoluteFill style={{ background: ink, overflow: "hidden" }}>
      <Sequence name="01 / Meet Looksy" durationInFrames={12 * fps}>
        <Opening />
      </Sequence>
      <Sequence
        name="02 / The photo editor"
        from={12 * fps}
        durationInFrames={50 * fps}
      >
        <PhotoChapter />
      </Sequence>
      <Sequence
        name="03 / The whole shoot"
        from={62 * fps}
        durationInFrames={30 * fps}
      >
        <LibraryChapter />
      </Sequence>
      <Sequence
        name="04 / The canvas"
        from={92 * fps}
        durationInFrames={68 * fps}
      >
        <CanvasChapter />
      </Sequence>
      <Sequence
        name="05 / The carousel"
        from={160 * fps}
        durationInFrames={24 * fps}
      >
        <CarouselChapter />
      </Sequence>
      <Sequence
        name="06 / The video timeline"
        from={184 * fps}
        durationInFrames={68 * fps}
      >
        <FilmChapter />
      </Sequence>
      <Sequence
        name="07 / An optional local collaborator"
        from={252 * fps}
        durationInFrames={28 * fps}
      >
        <AgentChapter />
      </Sequence>
      <Sequence
        name="08 / Make the whole thing"
        from={280 * fps}
        durationInFrames={18 * fps}
      >
        <Closing />
      </Sequence>
      {[...creativePlan.slice(1).map((p) => p.at), 50, 133].map((at) => (
        <TransitionVeil key={at} at={at} />
      ))}
      {withMusic && (
        <Audio src={staticFile("audio/creative-polish-v4-master.wav")} />
      )}
    </AbsoluteFill>
  );
};
