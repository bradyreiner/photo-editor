import { staticFile, useCurrentFrame, useVideoConfig } from "remotion";
import { AppIcon, AppInspectorSlider } from "../app-kit/AppElements";
import {
  A,
  C,
  Copy,
  Grain,
  land,
  lerp,
  mono,
  p,
  Plate,
} from "../launch/design";
import { Bar, Caption, Panel } from "./Shared";
const base = staticFile("media/campaign/wedding-bride-02-developed.jpg"),
  after = staticFile("media/campaign/wedding-skin-retouched.png");
const spots = [
  [519, 739, 7],
  [552, 761, 8],
  [536, 790, 7],
  [603, 737, 6],
  [592, 830, 7],
];
export const RetouchScene = ({ at }: { at?: number }) => {
  const { fps } = useVideoConfig(),
    frame = useCurrentFrame(),
    t = at ?? frame / fps,
    zoom = p(t, 0, 1.1);
  const filled = p(t, 3, 3.8) * (1 - p(t, 5.1, 5.5)) + p(t, 5.9, 6.3),
    blend = filled;
  const cursor = Math.min(4, Math.floor(p(t, 1.2, 2.6) * 5)),
    pos = spots[cursor];
  return (
    <Plate>
      <div style={{ ...A, left: 80, top: 150 }}>
        <Copy t={t} size={104}>
          <div>Retouch.</div>
          <div>Keep it natural.</div>
        </Copy>
      </div>
      <div
        style={{
          ...A,
          left: 80,
          top: 440,
          width: 920,
          height: 1030,
          overflow: "hidden",
        }}
      >
        <svg width="920" height="1030" viewBox="0 0 920 1030">
          <g
            transform={`translate(460 515) scale(${lerp(0.7667, 2.7, zoom)}) translate(${-lerp(600, 565, zoom)} ${-lerp(900, 770, zoom)})`}
          >
            <image href={base} width="1200" height="1800" />
            <image href={after} width="1200" height="1800" opacity={blend} />
            {spots.map(([x, y, r], i) => (
              <circle
                key={i}
                cx={x}
                cy={y}
                r={r + 1}
                fill={C.brandCoral}
                opacity={
                  t > 1.2 + i * 0.24 && t < 3 ? (1 - p(t, 2.7, 3)) * 0.6 : 0
                }
              />
            ))}
            <circle
              cx={pos[0]}
              cy={pos[1]}
              r="13"
              stroke="white"
              strokeWidth=".9"
              fill="none"
              opacity={land(t, 1, 1.3) * (1 - p(t, 2.7, 3))}
            />
          </g>
        </svg>
        <div
          style={{
            ...A,
            left: 24,
            bottom: 24,
            fontFamily: mono,
            fontSize: 24,
            background: "#050505b0",
            padding: "10px 16px",
          }}
        >
          {t < 3
            ? "PAINT RETOUCH MASK"
            : t > 5.1 && t < 5.9
              ? "ORIGINAL / UNDO"
              : "LOCAL FILL / MASKED PIXELS ONLY"}
        </div>
      </div>
      <Panel style={{ ...A, left: 80, top: 1490, width: 920 }}>
        <Bar left="Retouch · Local CPU Fill" right="Remove" />
        <div style={{ padding: 20 }}>
          <AppInspectorSlider
            label="INPAINT BLEND"
            value={100 * blend}
            valueText={`${Math.round(100 * blend)}%`}
            from={0}
            to={100}
            width={438}
            scale={2}
          />
        </div>
        <div
          style={{
            padding: "0 30px 24px",
            display: "flex",
            justifyContent: "space-between",
            fontSize: 28,
            color: C.textMuted,
          }}
        >
          <span>Mask retained</span>
          <span
            style={{
              display: "flex",
              gap: 16,
              color: t > 5.1 && t < 5.9 ? C.brandPaper : C.textMuted,
            }}
          >
            <AppIcon name="undo" size={30} />
            Undo
          </span>
          <span
            style={{
              display: "flex",
              gap: 16,
              color: t > 5.9 ? C.brandPaper : C.textMuted,
            }}
          >
            <AppIcon name="redo" size={30} />
            Redo
          </span>
        </div>
      </Panel>
      <Caption style={{ ...A, left: 80, top: 1790, fontSize: 35 }}>
        Local reconstruction. Original retained.
      </Caption>
      <Grain />
    </Plate>
  );
};
