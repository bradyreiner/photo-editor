import { Audio } from "@remotion/media";
import { AbsoluteFill, Sequence, staticFile, useVideoConfig } from "remotion";
import { ink, paper } from "./Core";
import { Intro, ColorStudio, MaskStudio } from "./Photography";
import { CanvasStudio, CarouselStudio } from "./Design";
import { FilmStudio } from "./Film";
import { SceneStudio, LocalStudio, DeliveryStudio } from "./Scenes";
export const StudioFilm = () => {
  const { fps } = useVideoConfig();
  return (
    <AbsoluteFill
      style={{
        background: ink,
        color: paper,
        fontFamily: '"Helvetica Neue",Helvetica,Arial,sans-serif',
        overflow: "hidden",
      }}
    >
      <Audio src={staticFile("audio/studio/master.wav")} />
      <Sequence name="01 / Meet Looksy" durationInFrames={14 * fps}>
        <Intro />
      </Sequence>
      <Sequence
        name="02 / A custom color engine / actual CR2 source"
        from={14 * fps}
        durationInFrames={26 * fps}
      >
        <ColorStudio />
      </Sequence>
      <Sequence
        name="03 / Masks and native-pixel editing"
        from={40 * fps}
        durationInFrames={20 * fps}
      >
        <MaskStudio />
      </Sequence>
      <Sequence
        name="04 / Layers, typography, posters and thumbnails"
        from={60 * fps}
        durationInFrames={24 * fps}
      >
        <CanvasStudio />
      </Sequence>
      <Sequence
        name="05 / Seamless mixed-media carousel"
        from={84 * fps}
        durationInFrames={20 * fps}
      >
        <CarouselStudio />
      </Sequence>
      <Sequence
        name="06 / Complete filmmaking / picture, motion, color, sound"
        from={104 * fps}
        durationInFrames={36 * fps}
      >
        <FilmStudio />
      </Sequence>
      <Sequence
        name="07 / Gaussian scenes / native camera renders"
        from={140 * fps}
        durationInFrames={18 * fps}
      >
        <SceneStudio />
      </Sequence>
      <Sequence
        name="08 / Optional local search and editable agent operations"
        from={158 * fps}
        durationInFrames={22 * fps}
      >
        <LocalStudio />
      </Sequence>
      <Sequence
        name="09 / Finished work / Looksy"
        from={180 * fps}
        durationInFrames={12 * fps}
      >
        <DeliveryStudio />
      </Sequence>
    </AbsoluteFill>
  );
};
