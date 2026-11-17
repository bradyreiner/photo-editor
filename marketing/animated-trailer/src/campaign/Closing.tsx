import { useCurrentFrame, useVideoConfig } from "remotion";
import { AppWordmark } from "../app-kit/AppElements";
import { A, Copy, Grain, land, lerp, p, Photo, Plate } from "../launch/design";
import { CampaignArtwork } from "../launch/Artwork";
import { Caption, WeddingImage } from "./Shared";
export const ClosingScene = () => {
  const { fps } = useVideoConfig(),
    t = useCurrentFrame() / fps,
    gather = p(t, 0, 1.3);
  return (
    <Plate>
      <div
        style={{
          ...A,
          left: 80,
          top: lerp(590, 290, gather),
          width: lerp(420, 285, gather),
          height: lerp(630, 428, gather),
          overflow: "hidden",
        }}
      >
        <Photo name="IMG_5106" style={{ filter: "brightness(.758)" }} />
      </div>
      <div
        style={{
          ...A,
          left: 398,
          top: 290,
          width: 285,
          height: 428,
          opacity: gather,
          overflow: "hidden",
        }}
      >
        <WeddingImage />
      </div>
      <div
        style={{
          ...A,
          left: 716,
          top: 290,
          width: 285,
          height: 428,
          opacity: gather,
          overflow: "hidden",
        }}
      >
        <div
          style={{
            width: 1200,
            height: 1500,
            scale: 285 / 1200,
            transformOrigin: "0 0",
          }}
        >
          <CampaignArtwork />
        </div>
      </div>
      <div
        style={{
          ...A,
          left: 80,
          top: 900,
          opacity: gather,
          translate: `0 ${land(t, 0.6, 1.6, 60, 0)}px`,
        }}
      >
        <AppWordmark width={690} />
      </div>
      <div style={{ ...A, left: 80, top: 1160 }}>
        <Copy t={t} start={1.1} size={89}>
          <div>Photo. Design. Video.</div>
          <div>Entirely in your hands.</div>
        </Copy>
      </div>
      <Caption
        style={{
          ...A,
          left: 80,
          top: 1480,
          fontSize: 42,
          opacity: land(t, 1.7, 2.4),
        }}
      >
        A bespoke creative workspace.
      </Caption>
      <Grain />
    </Plate>
  );
};
