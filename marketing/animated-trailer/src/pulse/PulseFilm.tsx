import { Audio } from "@remotion/media";
import { AbsoluteFill, Sequence, staticFile, useVideoConfig } from "remotion";
import { C } from "./Motion";
import { PacedScene } from "./Timing";
import { Carousel, Design, Opening, Photo, Retouch } from "./PhotoStory";
import { Delivery, Film, Formats } from "./FilmStory";
import { Closing, Local } from "./LocalStory";

export const PulseFilm = ({ withMusic = true }: { withMusic?: boolean }) => {
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
          withMusic ? "audio/pulse/sun-in-your-eyes-master.wav" : "audio/pulse/foley.wav",
        )}
      />
      <Sequence
        name="Introducing Looksy / focused brand reveal"
        durationInFrames={10 * fps}
      >
        <PacedScene original={14} duration={10}>
          <Opening />
        </PacedScene>
      </Sequence>
      <Sequence
        name="Photography / RAW, custom color, detail, batch"
        from={10 * fps}
        durationInFrames={22 * fps}
      >
        <PacedScene original={24} duration={22}>
          <Photo />
        </PacedScene>
      </Sequence>
      <Sequence
        name="Retouch / selection, remove, retain control"
        from={32 * fps}
        durationInFrames={7 * fps}
      >
        <PacedScene original={8} duration={7}>
          <Retouch />
        </PacedScene>
      </Sequence>
      <Sequence
        name="Design / masks, layers, type, brushwork"
        from={39 * fps}
        durationInFrames={14 * fps}
      >
        <PacedScene original={16} duration={14}>
          <Design />
        </PacedScene>
      </Sequence>
      <Sequence
        name="Carousel / continuous source, split, crop, swipe"
        from={53 * fps}
        durationInFrames={12 * fps}
      >
        <PacedScene original={14} duration={12}>
          <Carousel />
        </PacedScene>
      </Sequence>
      <Sequence
        name="Filmmaking / picture, motion, color, effects, sound"
        from={65 * fps}
        durationInFrames={23 * fps}
      >
        <PacedScene original={24} duration={23}>
          <Film />
        </PacedScene>
      </Sequence>
      <Sequence
        name="Every format / full films and vertical content"
        from={88 * fps}
        durationInFrames={9 * fps}
      >
        <PacedScene original={10} duration={9}>
          <Formats />
        </PacedScene>
      </Sequence>
      <Sequence
        name="Delivery / the finished frame becomes the export"
        from={97 * fps}
        durationInFrames={7 * fps}
      >
        <PacedScene original={8} duration={7}>
          <Delivery />
        </PacedScene>
      </Sequence>
      <Sequence
        name="Optional local assistance / search, edits, layers, history"
        from={104 * fps}
        durationInFrames={15 * fps}
      >
        <PacedScene original={16} duration={15}>
          <Local />
        </PacedScene>
      </Sequence>
      <Sequence
        name="Looksy / the final image becomes the brand reveal"
        from={119 * fps}
        durationInFrames={7 * fps}
      >
        <PacedScene original={8} duration={7}>
          <Closing />
        </PacedScene>
      </Sequence>
    </AbsoluteFill>
  );
};
