import {
  AbsoluteFill,
  Freeze,
  Sequence,
  useCurrentFrame,
  useVideoConfig,
} from "remotion";
import { C, p, Wipe } from "../launch/design";
import { CampaignOpening } from "./Opening";
import { WeddingScene, WeddingShot } from "./Wedding";
import { RetouchScene } from "./Retouch";
import { ImageMaking } from "../launch/ImageMaking";
import { ProductionScene, ProductionShot } from "../launch/Production";
import { VerticalScene } from "./Vertical";
import { CarouselScene } from "./Carousel";
import { DeliveryScene } from "./Delivery";
import { SearchShot, AgentScene } from "../launch/Assistance";
import { ClosingScene } from "./Closing";
import { CampaignScore } from "./Score";
const Design = () => {
  const { fps } = useVideoConfig(),
    t = useCurrentFrame() / fps;
  return (
    <>
      <ImageMaking paced />
      {t < 0.85 ? (
        <Wipe amount={1 - p(t, 0, 0.85)} phase={t}>
          <RetouchScene at={7.99} />
        </Wipe>
      ) : null}
    </>
  );
};
const Retouch = () => {
  const { fps } = useVideoConfig(),
    t = useCurrentFrame() / fps;
  return (
    <>
      <RetouchScene />
      {t < 0.8 ? (
        <Wipe amount={1 - p(t, 0, 0.8)} phase={t}>
          <WeddingShot t={21.999} />
        </Wipe>
      ) : null}
    </>
  );
};
const Vertical = () => {
  const { fps } = useVideoConfig(),
    t = useCurrentFrame() / fps;
  return (
    <>
      <VerticalScene />
      {t < 0.8 ? (
        <Wipe amount={1 - p(t, 0, 0.8)} phase={t}>
          <ProductionShot
            t={18 - 1 / fps}
            programStill="media/campaign/production-final.png"
          />
        </Wipe>
      ) : null}
    </>
  );
};
const Carousel = () => {
  const { fps } = useVideoConfig(),
    t = useCurrentFrame() / fps;
  return (
    <>
      <CarouselScene />
      {t < 0.8 ? (
        <Wipe amount={1 - p(t, 0, 0.8)} phase={t}>
          <Freeze frame={14 * fps - 1}>
            <VerticalScene at={14 - 1 / fps} />
          </Freeze>
        </Wipe>
      ) : null}
    </>
  );
};
const Search = () => {
  const { fps } = useVideoConfig();
  return <SearchShot t={((useCurrentFrame() / fps) * 10) / 12} />;
};
export const CampaignFilm = () => {
  const { fps } = useVideoConfig();
  return (
    <AbsoluteFill style={{ background: C.brandInk }}>
      <CampaignScore />
      <Sequence
        name="01 / Looksy — photo, design, video"
        durationInFrames={8 * fps}
      >
        <CampaignOpening />
      </Sequence>
      <Sequence
        name="02 / Wedding photographers — cull, RAW controls, detail, batch, gallery"
        from={8 * fps}
        durationInFrames={22 * fps}
      >
        <WeddingScene />
      </Sequence>
      <Sequence
        name="03 / Native local inpainting — mask, fill, original, undo, redo"
        from={30 * fps}
        durationInFrames={8 * fps}
      >
        <Retouch />
      </Sequence>
      <Sequence
        name="04 / Image making — pixels, subject masks, layers, text, paint, effects"
        from={38 * fps}
        durationInFrames={26 * fps}
      >
        <Design />
      </Sequence>
      <Sequence
        name="05 / Production timeline — trim, keyframes, effects, audio"
        from={64 * fps}
        durationInFrames={18 * fps}
      >
        <ProductionScene />
      </Sequence>
      <Sequence
        name="06 / TikTok and Reels — reframe, animate, safe areas, export"
        from={82 * fps}
        durationInFrames={14 * fps}
      >
        <Vertical />
      </Sequence>
      <Sequence
        name="07 / Seamless spread — shared source, crop, swipe, mixed boards, export"
        from={96 * fps}
        durationInFrames={20 * fps}
      >
        <Carousel />
      </Sequence>
      <Sequence
        name="08 / Deliver every format"
        from={116 * fps}
        durationInFrames={6 * fps}
      >
        <DeliveryScene />
      </Sequence>
      <Sequence
        name="09 / Optional local visual search — find original media"
        from={122 * fps}
        durationInFrames={12 * fps}
      >
        <Search />
      </Sequence>
      <Sequence
        name="10 / Optional local agent — batch, retain layers, edit, undo, redo"
        from={134 * fps}
        durationInFrames={14 * fps}
      >
        <AgentScene />
      </Sequence>
      <Sequence
        name="11 / Looksy — the finished workspace"
        from={148 * fps}
        durationInFrames={6 * fps}
      >
        <ClosingScene />
      </Sequence>
    </AbsoluteFill>
  );
};
