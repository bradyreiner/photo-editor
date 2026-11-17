import { useCurrentFrame, useVideoConfig } from "remotion";
import { AppIcon } from "../app-kit/AppElements";
import { A, C, Copy, Grain, land, Plate } from "../launch/design";
import { Bar, Panel } from "./Shared";
export const DeliveryScene = () => {
  const { fps } = useVideoConfig(),
    t = useCurrentFrame() / fps;
  return (
    <Plate>
      <div style={{ ...A, left: 80, top: 150 }}>
        <Copy t={t} size={106}>
          <div>The whole job.</div>
          <div>Delivered.</div>
        </Copy>
      </div>
      <Panel style={{ ...A, left: 80, top: 520, width: 920 }}>
        <Bar left="Export queue" right="Local files" />
        {[
          ["Wedding photographs", "Full resolution · sRGB"],
          ["Layered artwork", "Custom canvas · Pixel detail"],
          ["Full production", "4K · Rec.709"],
          ["TikTok / Reels", "9:16 · H.264"],
          ["Social carousel", "Numbered photo & video boards"],
        ].map(([a, b], i) => (
          <div
            key={a}
            style={{
              padding: "35px 28px",
              borderBottom: `1px solid ${C.lineSoft}`,
              display: "flex",
              justifyContent: "space-between",
              alignItems: "center",
              opacity: land(t, 0.2 + i * 0.35, 0.7 + i * 0.35),
              translate: `${land(t, 0.2 + i * 0.35, 0.8 + i * 0.35, 40, 0)}px 0`,
            }}
          >
            <div>
              <div style={{ fontSize: 36, marginBottom: 14 }}>{a}</div>
              <div style={{ fontSize: 29, color: C.textMuted }}>{b}</div>
            </div>
            <AppIcon name="check" size={38} color={C.opticalLight} />
          </div>
        ))}
      </Panel>
      <div
        style={{
          ...A,
          left: 80,
          top: 1680,
          fontSize: 43,
          lineHeight: 1.25,
          color: C.textMuted,
        }}
      >
        From original media
        <br />
        to the formats the job needs.
      </div>
      <Grain />
    </Plate>
  );
};
