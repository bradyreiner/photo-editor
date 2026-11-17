import { Video } from "@remotion/media";
import { staticFile, useCurrentFrame, useVideoConfig } from "remotion";
import {
  A,
  Copy,
  Grain,
  land,
  LightMaterial,
  lerp,
  Lockup,
  p,
  Photo,
  Plate,
} from "./design";
import { CampaignArtwork } from "./Artwork";
import { CarouselArtwork } from "./Production";

export const ClosingShot = ({ t }: { t: number }) => {
  const close = p(t, 2.3, 4.6),
    appear = land(t, 3.4, 4.6);
  return (
    <Plate>
      <div
        style={{
          ...A,
          inset: 0,
          opacity: 1 - close,
          scale: 1 - close * 0.1,
          translate: `0 ${-close * 160}px`,
        }}
      >
        <div
          style={{
            ...A,
            left: 80,
            top: 180,
            fontSize: 112,
            fontWeight: 600,
            letterSpacing: -5,
            lineHeight: 0.98,
          }}
        >
          <Copy t={t} start={0.15} size={112}>
            <div>The shoot.</div>
            <div>The design.</div>
            <div>The film.</div>
          </Copy>
        </div>
        <div
          style={{
            ...A,
            left: 80,
            top: 640,
            width: 390,
            height: 487.5,
            rotate: "-5deg",
            translate: `${land(t, 0.15, 1.05, -180, 0)}px 0`,
            opacity: land(t, 0.15, 0.65),
            scale: land(t, 0.15, 1.05, 0.88, 1),
          }}
        >
          <CampaignArtwork t={8} />
        </div>
        <div
          style={{
            ...A,
            left: 520,
            top: 680,
            width: 480,
            height: 270,
            overflow: "hidden",
            rotate: "4deg",
            translate: `${land(t, 0.3, 1.15, 220, 0)}px 0`,
            opacity: land(t, 0.3, 0.8),
          }}
        >
          <Video
            src={staticFile("media/launch-1207.mp4")}
            objectFit="cover"
            muted
            style={{ width: "100%", height: "100%" }}
          />
          <div
            style={{
              ...A,
              left: 24,
              bottom: 25,
              fontFamily: "Manrope",
              fontWeight: 800,
              fontSize: 31,
              color: "white",
            }}
          >
            NEPAL.
          </div>
        </div>
        <div
          style={{
            ...A,
            left: lerp(80, 580, p(t, 0, 1.05)),
            top: lerp(590, 1030, p(t, 0, 1.05)),
            width: lerp(420, 265, p(t, 0, 1.05)),
            height: lerp(630, 397.5, p(t, 0, 1.05)),
            overflow: "hidden",
            rotate: `${land(t, 0, 1.05, 0, 5)}deg`,
          }}
        >
          <Photo
            style={{ filter: `brightness(${land(t, 0, 1.05, 2 ** -0.4, 1)})` }}
          />
        </div>
        {[0, 1, 2, 3, 4].map((i) => (
          <div
            key={i}
            style={{
              ...A,
              left: 80 + i * 142,
              top: 1510,
              width: 126,
              height: 157.5,
              rotate: `${-3 + i * 1.5}deg`,
              translate: `0 ${land(t, 0.6 + i * 0.08, 1.3 + i * 0.08, 70, 0)}px`,
              opacity: land(t, 0.6 + i * 0.08, 1.1 + i * 0.08),
            }}
          >
            <CarouselArtwork index={i} />
          </div>
        ))}
      </div>
      <div style={{ ...A, inset: 0, opacity: appear }}>
        <LightMaterial t={t + 7} form="brand-upper" width={280} />
        <LightMaterial t={t + 4} form="brand-lower" width={195} />
      </div>
      <div
        style={{
          ...A,
          left: 110,
          top: 765,
          opacity: appear,
          translate: `0 ${land(t, 3.4, 4.7, 100, 0)}px`,
        }}
      >
        <Lockup width={860} />
        <div
          style={{
            fontSize: 60,
            letterSpacing: -2,
            marginTop: 45,
            fontWeight: 500,
          }}
        >
          Photo. Design. Video.
        </div>
        <div style={{ fontSize: 40, letterSpacing: -0.5, marginTop: 50 }}>
          Your entire creative workspace.
        </div>
      </div>
      <Grain />
    </Plate>
  );
};
export const ClosingScene = () => {
  const f = useCurrentFrame(),
    { fps } = useVideoConfig();
  return <ClosingShot t={f / fps} />;
};
