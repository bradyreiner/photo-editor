import { Audio } from "@remotion/media";
import { AbsoluteFill, Sequence, staticFile, useVideoConfig } from "remotion";
import { C, q } from "./Motion";
import { Carousel, Design, Opening, Photo, Retouch } from "./PhotoStory";
import { Delivery, Film, Formats } from "./FilmStory";
import { Closing, Local } from "./LocalStory";

export const RefinedFilm = ({ withMusic = true }: { withMusic?: boolean }) => {
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
      {withMusic && <Audio src={staticFile("audio/refine/quiet-score.wav")} volume={0.7762471166} />}
      <Sequence
        name="Introducing Looksy / professionals and hobbyists"
        durationInFrames={14 * fps}
      >
        <Opening />
      </Sequence>
      <Sequence
        name="Photography / import, RAW, custom color, detail, batch galleries"
        from={14 * fps}
        durationInFrames={24 * fps}
      >
        <Photo />
      </Sequence>
      <Sequence
        name="Retouch / remove an unwanted object, preserve the subject"
        from={38 * fps}
        durationInFrames={8 * fps}
      >
        <Retouch />
      </Sequence>
      <Sequence
        name="Design / selection, masks, type, brushwork, layers"
        from={46 * fps}
        durationInFrames={16 * fps}
      >
        <Design />
      </Sequence>
      <Sequence
        name="Carousel / one photo split across canvases, shared crop, swipe"
        from={62 * fps}
        durationInFrames={14 * fps}
      >
        <Carousel />
      </Sequence>
      <Sequence
        name="Full filmmaking / multi-track editing, motion, color, effects, sound"
        from={76 * fps}
        durationInFrames={24 * fps}
      >
        <Film />
      </Sequence>
      <Sequence
        name="Social formats / keep the full film, adapt for vertical"
        from={100 * fps}
        durationInFrames={10 * fps}
      >
        <Formats />
      </Sequence>
      <Sequence
        name="Delivery / photographs, designs, 4K films"
        from={110 * fps}
        durationInFrames={8 * fps}
      >
        <Delivery />
      </Sequence>
      <Sequence
        name="Optional local assistance / search, batch, layers, history, manual control"
        from={118 * fps}
        durationInFrames={16 * fps}
      >
        <Local />
      </Sequence>
      <Sequence
        name="Looksy / photo and video made easier"
        from={134 * fps}
        durationInFrames={8 * fps}
      >
        <Closing />
      </Sequence>
      <Sequence
        name="Tactile controls / restrained sound design"
        durationInFrames={142 * fps}
      >
        <Audio
          src={staticFile("audio/refine/quiet-foley.wav")}
          volume={(f) => 0.7762471166 * q(f / fps, 0, 1)}
        />
      </Sequence>
    </AbsoluteFill>
  );
};
