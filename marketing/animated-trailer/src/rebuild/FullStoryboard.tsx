import {
  AbsoluteFill,
  Sequence,
  useVideoConfig,
  useCurrentFrame,
} from "remotion";
import { Audio } from "@remotion/media";
import { staticFile } from "remotion";
import { font } from "./Type";
import { A, ink, paper, lime } from "./Kit";
import { NewOpening, MeaningfulBrush } from "./MotionProof";
import {
  WeddingStoryboard,
  RawStoryboard,
  PixelStoryboard,
  RetouchStoryboard,
  DesignStoryboard,
} from "./StoryboardPhoto";
import {
  ThumbnailStoryboard,
  CarouselStoryboard,
  GaussianStoryboard,
  AgentStoryboard,
  ClosingStoryboard,
} from "./StoryboardOther";
import {
  TeaserStoryboard,
  TimelineStoryboard,
  MotionStoryboard,
  ColorStoryboard,
  SoundDeliveryStoryboard,
} from "./StoryboardFilm";
import { storyboardPlan } from "./StoryboardPlan";
const clock = (n: number) =>
  `${Math.floor(n / 60)}:${String(Math.floor(n % 60)).padStart(2, "0")}`;
const EditorialHUD = () => {
  const t = useCurrentFrame() / useVideoConfig().fps,
    s =
      storyboardPlan.scenes.find((s) => t >= s.start && t < s.end) ||
      storyboardPlan.scenes[16];
  return (
    <div
      style={{
        ...A,
        left: 0,
        bottom: 0,
        width: 1080,
        height: 55,
        background: "#080a0fee",
        borderTop: "1px solid #3f4c5b",
        fontFamily: font.mono,
        fontSize: 19,
        display: "flex",
        alignItems: "center",
        padding: "0 30px",
        gap: 20,
        boxSizing: "border-box",
        zIndex: 100,
      }}
    >
      <span style={{ color: lime }}>STORYBOARD ANIMATIC</span>
      <span style={{ color: "#b9c4cf" }}>{s.title}</span>
      <span style={{ marginLeft: "auto", color: paper }}>
        {clock(t)} / 3:21
      </span>
    </div>
  );
};
export const FullStoryboard = () => {
  const { fps } = useVideoConfig();
  return (
    <AbsoluteFill
      style={{
        background: ink,
        color: paper,
        fontFamily: font.sans,
        overflow: "hidden",
      }}
    >
      <Audio src={staticFile("audio/rebuild/master.wav")} />
      <Sequence name="opening / Why Looksy" durationInFrames={12 * fps}>
        <NewOpening destination="wedding" />
      </Sequence>
      <Sequence
        name="wedding / Import, cull, synchronize, deliver"
        from={12 * fps}
        durationInFrames={16 * fps}
      >
        <WeddingStoryboard />
      </Sequence>
      <Sequence
        name="raw / Custom native color engine"
        from={28 * fps}
        durationInFrames={20 * fps}
      >
        <RawStoryboard />
      </Sequence>
      <Sequence
        name="mask / Paint a purposeful selection"
        from={48 * fps}
        durationInFrames={12 * fps}
      >
        <MeaningfulBrush bridgeToDesign={false} />
      </Sequence>
      <Sequence
        name="pixel / Native source detail"
        from={60 * fps}
        durationInFrames={6 * fps}
      >
        <PixelStoryboard />
      </Sequence>
      <Sequence
        name="retouch / Preserve the photograph"
        from={66 * fps}
        durationInFrames={8 * fps}
      >
        <RetouchStoryboard />
      </Sequence>
      <Sequence
        name="design / Layers, type, graphics"
        from={74 * fps}
        durationInFrames={18 * fps}
      >
        <DesignStoryboard />
      </Sequence>
      <Sequence
        name="thumbnails / Different content and formats"
        from={92 * fps}
        durationInFrames={8 * fps}
      >
        <ThumbnailStoryboard />
      </Sequence>
      <Sequence
        name="carousel / One photograph across canvases"
        from={100 * fps}
        durationInFrames={14 * fps}
      >
        <CarouselStoryboard />
      </Sequence>
      <Sequence
        name="teaser / A finished film first"
        from={114 * fps}
        durationInFrames={10 * fps}
      >
        <TeaserStoryboard />
      </Sequence>
      <Sequence
        name="timeline / The production underneath"
        from={124 * fps}
        durationInFrames={12 * fps}
      >
        <TimelineStoryboard />
      </Sequence>
      <Sequence
        name="motion / Keyframes, curves, compositing, effects"
        from={136 * fps}
        durationInFrames={10 * fps}
      >
        <MotionStoryboard />
      </Sequence>
      <Sequence
        name="color / sRGB, Rec.709, Sony Log, ProRes"
        from={146 * fps}
        durationInFrames={10 * fps}
      >
        <ColorStoryboard />
      </Sequence>
      <Sequence
        name="delivery / Sound, full masters and vertical versions"
        from={156 * fps}
        durationInFrames={8 * fps}
      >
        <SoundDeliveryStoryboard />
      </Sequence>
      <Sequence
        name="gaussian / A new viewpoint"
        from={164 * fps}
        durationInFrames={10 * fps}
      >
        <GaussianStoryboard />
      </Sequence>
      <Sequence
        name="agent / Optional local assistance with retained control"
        from={174 * fps}
        durationInFrames={18 * fps}
      >
        <AgentStoryboard />
      </Sequence>
      <Sequence
        name="close / Finished work and brand"
        from={192 * fps}
        durationInFrames={9.25 * fps}
      >
        <ClosingStoryboard />
      </Sequence>
      <EditorialHUD />
    </AbsoluteFill>
  );
};
