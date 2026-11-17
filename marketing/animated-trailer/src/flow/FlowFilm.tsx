import { Audio } from "@remotion/media";
import { AbsoluteFill, Sequence, staticFile, useVideoConfig } from "remotion";
import { C } from "./Motion";
import { PacedScene } from "./Timing";
import { Design, Opening, Photo, Retouch } from "./PhotoStory";
import { Delivery, Formats } from "./FilmStory";
import { Closing, Local } from "./LocalStory";
import { CarouselStory } from "./CarouselStory";
import { CinemaStory } from "./CinemaStory";

export const FlowFilm = ({ withMusic = true }: { withMusic?: boolean }) => {
  const { fps } = useVideoConfig();
  return (
    <AbsoluteFill
      style={{
        background: C.brandInk,
        color: C.brandPaper,
        fontFamily: '"Helvetica Neue",Helvetica,Arial,sans-serif',
        overflow: "hidden",
      }}
    >
      <Audio
        src={staticFile(
          withMusic
            ? "audio/flow/sun-in-your-eyes-master.wav"
            : "audio/flow/foley.wav",
        )}
      />
      <Sequence
        name="Introducing Looksy / your photography"
        durationInFrames={9 * fps}
      >
        <PacedScene original={14} duration={9}>
          <Opening />
        </PacedScene>
      </Sequence>
      <Sequence
        name="Photos / RAW, color, HSL, pixels, batch"
        from={9 * fps}
        durationInFrames={24 * fps}
      >
        <PacedScene original={24} duration={24}>
          <Photo />
        </PacedScene>
      </Sequence>
      <Sequence
        name="Retouch / a clean, source-preserving illustration"
        from={33 * fps}
        durationInFrames={6 * fps}
      >
        <PacedScene original={8} duration={6}>
          <Retouch />
        </PacedScene>
      </Sequence>
      <Sequence
        name="Canvas / personal photography, masks, type, brush, layers"
        from={39 * fps}
        durationInFrames={16 * fps}
      >
        <PacedScene original={16} duration={16}>
          <Design />
        </PacedScene>
      </Sequence>
      <Sequence
        name="Carousel / shared crop, seamless swipe, mixed-media export"
        from={55 * fps}
        durationInFrames={13 * fps}
      >
        <PacedScene original={13} duration={13}>
          <CarouselStory />
        </PacedScene>
      </Sequence>
      <Sequence
        name="Film / finished teaser becomes a full editable production"
        from={68 * fps}
        durationInFrames={35 * fps}
      >
        <PacedScene original={35} duration={35}>
          <CinemaStory />
        </PacedScene>
      </Sequence>
      <Sequence
        name="Reframe / films, YouTube, Reels, TikTok"
        from={103 * fps}
        durationInFrames={8 * fps}
      >
        <PacedScene original={10} duration={8}>
          <Formats />
        </PacedScene>
      </Sequence>
      <Sequence
        name="Deliver / photographs, designs, full films"
        from={111 * fps}
        durationInFrames={7 * fps}
      >
        <PacedScene original={8} duration={7}>
          <Delivery />
        </PacedScene>
      </Sequence>
      <Sequence
        name="Optional local search and agent / original work, retained control"
        from={118 * fps}
        durationInFrames={14 * fps}
      >
        <PacedScene original={16} duration={14}>
          <Local />
        </PacedScene>
      </Sequence>
      <Sequence
        name="Looksy / photography, design, film"
        from={132 * fps}
        durationInFrames={6 * fps}
      >
        <PacedScene original={8} duration={6}>
          <Closing />
        </PacedScene>
      </Sequence>
    </AbsoluteFill>
  );
};
