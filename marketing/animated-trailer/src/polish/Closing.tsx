import { useCurrentFrame, useVideoConfig } from "remotion";
import { AppWordmark } from "../app-kit/AppElements";
import { A, C, Grain, land, lerp, p, Photo, Plate } from "../launch/design";
import { WeddingImage } from "./Shared";
export const ClosingScene = () => {
  const { fps } = useVideoConfig(),
    t = useCurrentFrame() / fps,
    gather = p(t, 0, 1.25),
    brand = p(t, 1.4, 2.5);
  return (
    <Plate>
      <div
        style={{
          ...A,
          left: 80,
          top: 650,
          width: 920,
          height: 1000,
          background: C.ink950,
          border: `1px solid ${C.lineSoft}`,
          opacity: gather,
          scale: lerp(1, 0.86, brand),
          transformOrigin: "50% 50%",
          translate: `0 ${brand * 55}px`,
        }}
      >
        <div
          style={{
            height: 70,
            borderBottom: `1px solid ${C.lineSoft}`,
            padding: "24px 24px",
            fontSize: 24,
            color: C.textMuted,
            display: "flex",
            justifyContent: "flex-end",
          }}
        >
          Library · Edit · Boards · Timeline
        </div>
        {[
          "IMG_5106",
          "range/product",
          "range/portrait",
          "range/food",
          "range/city",
          "wedding-bride-02",
        ].map((name, i) => (
          <div
            key={name}
            style={{
              ...A,
              left: 24 + (i % 3) * 294,
              top: 100 + Math.floor(i / 3) * 420,
              width: 270,
              height: 395,
              overflow: "hidden",
              opacity: land(t, 0.35 + i * 0.06, 1.1 + i * 0.06),
              translate: `0 ${land(t, 0.35 + i * 0.06, 1.1 + i * 0.06, 70, 0)}px`,
            }}
          >
            {name === "IMG_5106" ? (
              <Photo style={{ filter: "brightness(.758)" }} />
            ) : (
              <WeddingImage name={name} />
            )}
          </div>
        ))}
      </div>
      <div
        style={{
          ...A,
          left: lerp(80, 104, gather),
          top: lerp(590, 750, gather),
          width: lerp(420, 270, gather),
          height: lerp(630, 395, gather),
          opacity: 1 - p(t, 0.8, 1.25),
          overflow: "hidden",
        }}
      >
        <Photo style={{ filter: "brightness(.758)" }} />
      </div>
      <div
        style={{
          ...A,
          left: lerp(104, 80, brand),
          top: lerp(665, 170, brand),
          opacity: gather,
          zIndex: 3,
        }}
      >
        <AppWordmark width={lerp(158, 760, brand)} />
      </div>
      <div
        style={{
          ...A,
          left: 80,
          top: 470,
          fontSize: 72,
          fontWeight: 500,
          lineHeight: 1.07,
          letterSpacing: -3,
          opacity: land(t, 2.2, 3),
          translate: `0 ${land(t, 2.2, 3, 45, 0)}px`,
        }}
      >
        Professional photo
        <br />& video editing.
      </div>
      <div
        style={{
          ...A,
          left: 80,
          top: 1800,
          fontSize: 43,
          color: C.textMuted,
          opacity: land(t, 2.8, 3.4),
        }}
      >
        All in one workspace.
      </div>
      <Grain />
    </Plate>
  );
};
