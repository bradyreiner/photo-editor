import { Video } from "@remotion/media";
import { staticFile, useCurrentFrame, useVideoConfig } from "remotion";
import { AppWordmark } from "../app-kit/AppElements";
import { CampaignArtwork } from "../launch/Artwork";
import { A, Copy, Grain, lerp, p, Plate } from "../launch/design";
import { WeddingImage, Caption } from "./Shared";

// One lateral camera journey through finished work. The canvases become the
// source-library contact sheet; no desk drawing or rainbow title interstitial.
export const CampaignOpening = () => {
  const { fps } = useVideoConfig(),
    t = useCurrentFrame() / fps;
  const travel = p(t, 1.65, 2.5) + p(t, 3.25, 4.1),
    gather = p(t, 5, 6.25),
    library = p(t, 6.6, 7.35);
  const x = 80 - 1010 * travel * (1 - gather),
    w = lerp(920, 285, gather),
    h = lerp(1150, 428, gather);
  return (
    <Plate>
      <div style={{ ...A, left: 80, top: 120, opacity: 1 - p(t, 5.95, 6.35) }}>
        <AppWordmark width={330} />
      </div>
      <div style={{ ...A, left: 80, top: 265, opacity: 1 - p(t, 5.95, 6.35) }}>
        <Copy t={t} size={108}>
          <div>Photo. Design.</div>
          <div>Video.</div>
        </Copy>
      </div>
      <div style={{ ...A, inset: 0, perspective: 2200 }}>
        {[0, 1, 2].map((i) => (
          <div
            key={i}
            style={{
              ...A,
              left: lerp(x + 1010 * i, 80 + 318 * i, gather),
              top:
                lerp(570, 735, gather) +
                Math.sin(t * 0.7 + i) * 5 * (1 - gather),
              width: w,
              height: h,
              overflow: "hidden",
              transform: `rotateY(${Math.sin(travel * Math.PI) * -5 * (1 - gather)}deg)`,
              boxShadow: "0 30px 90px #0007",
            }}
          >
            {i === 0 ? (
              <WeddingImage />
            ) : i === 1 ? (
              <div
                style={{
                  width: 1200,
                  height: 1500,
                  scale: w / 1200,
                  transformOrigin: "0 0",
                }}
              >
                <CampaignArtwork />
              </div>
            ) : (
              <>
                <Video
                  objectFit="cover"
                  src={staticFile("media/launch-1207.mp4")}
                  muted
                  style={{ width: "100%", height: "100%" }}
                />
                <div
                  style={{
                    ...A,
                    top: 90,
                    left: 30,
                    right: 30,
                    fontSize: 72,
                    fontWeight: 700,
                    letterSpacing: -4,
                    lineHeight: 0.95,
                    color: "white",
                  }}
                >
                  A DIFFERENT
                  <br />
                  PERSPECTIVE.
                </div>
              </>
            )}
            <div
              style={{
                position: "absolute",
                inset: 0,
                opacity: p(t, 6.65, 7.65),
              }}
            >
              <WeddingImage
                name={
                  ["wedding-bride-02", "wedding-bride-01", "wedding-bride-03"][
                    i
                  ]
                }
              />
            </div>
          </div>
        ))}
      </div>
      <Caption
        style={{
          ...A,
          left: 80,
          top: 1750,
          opacity: 1 - p(t, 5, 5.5),
          fontSize: 44,
        }}
      >
        A bespoke photo & video editor.
      </Caption>
      <div style={{ ...A, left: 80, top: 150, opacity: library }}>
        <Copy t={t} start={6.6} size={102}>
          <div>For the whole</div>
          <div>wedding gallery.</div>
        </Copy>
      </div>
      <div
        style={{
          ...A,
          left: 80,
          top: 595,
          opacity: library,
          fontSize: 29,
          letterSpacing: 1,
        }}
      >
        IMPORT → CULL → DEVELOP → DELIVER
      </div>
      <div
        style={{
          ...A,
          left: 80,
          top: 1220,
          display: "flex",
          gap: 33,
          opacity: library,
        }}
      >
        {["wedding-couple", "wedding-dawn", "wedding-bride-01"].map((n) => (
          <div key={n} style={{ width: 285, height: 360, overflow: "hidden" }}>
            <WeddingImage name={n} />
          </div>
        ))}
      </div>
      <Grain />
    </Plate>
  );
};
