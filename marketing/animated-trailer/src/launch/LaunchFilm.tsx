import { AbsoluteFill, Sequence, useVideoConfig } from "remotion";
import { C } from "./design";
import { LaunchOpening } from "./LaunchOpening";
import { ImageMaking } from "./ImageMaking";
import { ProductionScene, CarouselScene, DeliveryScene } from "./Production";
import { SearchScene, AgentScene } from "./Assistance";
import { ClosingScene } from "./Closing";
import { LaunchScore } from "./LaunchScore";
import { ColorDetailScene } from "./ColorDetail";

// Complete 124-second picture structure. This remains a work in progress until
// picture, motion, typography and the new soundtrack have survived full review.
export const LaunchFilm = () => {
  const { fps } = useVideoConfig();
  return (
    <AbsoluteFill style={{ background: C.brandInk }}>
      <LaunchScore />
      <Sequence
        name="01 / Meet Looksy — source, import, RAW, custom color"
        durationInFrames={18 * fps}
      >
        <LaunchOpening paced />
      </Sequence>
      <Sequence
        name="Color ranges / loupe / sharpening / noise"
        from={18 * fps}
        durationInFrames={4 * fps}
      >
        <ColorDetailScene />
      </Sequence>
      <Sequence
        name="02 / Image making — pixels, masks, layers, type, paint, effects"
        from={22 * fps}
        durationInFrames={26 * fps}
      >
        <ImageMaking paced />
      </Sequence>
      <Sequence
        name="03 / A full production — moving picture, tracks, motion, effects, audio"
        from={48 * fps}
        durationInFrames={22 * fps}
      >
        <ProductionScene />
      </Sequence>
      <Sequence
        name="04 / Carousel — photo, video, linked style, layout, order"
        from={70 * fps}
        durationInFrames={14 * fps}
      >
        <CarouselScene />
      </Sequence>
      <Sequence
        name="05 / Full-resolution delivery — manual workflow complete"
        from={84 * fps}
        durationInFrames={8 * fps}
      >
        <DeliveryScene />
      </Sequence>
      <Sequence
        name="06 / Optional local search — original source matches"
        from={92 * fps}
        durationInFrames={10 * fps}
      >
        <SearchScene />
      </Sequence>
      <Sequence
        name="07 / Optional local agent — review, batch, edit, undo, revise"
        from={102 * fps}
        durationInFrames={14 * fps}
      >
        <AgentScene />
      </Sequence>
      <Sequence
        name="08 / The completed creative workspace"
        from={116 * fps}
        durationInFrames={8 * fps}
      >
        <ClosingScene />
      </Sequence>
    </AbsoluteFill>
  );
};
