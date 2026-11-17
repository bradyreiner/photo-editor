import React from "react";
import { curveData } from "./curve-data";
import {
  CanvasImage,
  Easing,
  interpolate,
  staticFile,
  useCurrentFrame,
  useVideoConfig,
} from "remotion";
import {
  AppIcon,
  AppInspectorSlider,
  AppWordmark,
  appTokens,
} from "../app-kit/AppElements";
export const C = appTokens;
export const A: React.CSSProperties = { position: "absolute" };
export const ink = "#090b0c",
  paper = "#f4f0e8",
  accent = "#b5d5d7",
  muted = "#9fa5a7";
export const useTime = () => {
  const f = useCurrentFrame(),
    { fps } = useVideoConfig();
  return f / fps;
};
export const clamp = (x: number) => Math.max(0, Math.min(1, x));
export const s = (t: number, a: number, b: number) => {
  const u = clamp((t - a) / (b - a));
  return u * u * u * (10 + u * (-15 + u * 6));
};
export const fast = (t: number, a: number, b: number) =>
  interpolate(t, [a, b], [0, 1], {
    easing: Easing.bezier(0.22, 1, 0.36, 1),
    extrapolateLeft: "clamp",
    extrapolateRight: "clamp",
  });
export const mix = (a: number, b: number, u: number) => a + (b - a) * u;
export const window = (t: number, a: number, b: number) =>
  s(t, a, a + 0.3) * (1 - s(t, b - 0.3, b));
export const Photo = ({
  src,
  style = {},
}: {
  src: string;
  style?: React.CSSProperties;
}) => (
  <CanvasImage
    src={staticFile("media/" + src)}
    style={{ width: "100%", height: "100%", objectFit: "cover", ...style }}
  />
);
export const Tag = ({
  children,
  style = {},
}: {
  children: React.ReactNode;
  style?: React.CSSProperties;
}) => (
  <div
    style={{
      fontSize: 24,
      fontFamily: "Menlo,monospace",
      letterSpacing: 1.5,
      color: muted,
      ...style,
    }}
  >
    {children}
  </div>
);
export const Bar = ({
  mode,
  opacity = 1,
}: {
  mode: string;
  opacity?: number;
}) => (
  <div
    style={{
      ...A,
      left: 80,
      top: 76,
      width: 920,
      display: "flex",
      alignItems: "center",
      gap: 28,
      opacity,
    }}
  >
    <AppWordmark width={145} />
    <span style={{ height: 23, width: 1, background: "#444" }} />
    <Tag>{mode}</Tag>
  </div>
);
export const Title = ({
  t,
  a = 0,
  b = 999,
  lines,
  detail,
  size = 100,
  top = 165,
}: {
  t: number;
  a?: number;
  b?: number;
  lines: string[];
  detail?: string;
  size?: number;
  top?: number;
}) => (
  <div
    style={{
      ...A,
      left: 80,
      top,
      width: 920,
      opacity: (1 - s(t, b - 0.25, b)) * fast(t, a, a + 0.35),
      translate: `0 ${24 * (1 - fast(t, a, a + 0.65))}px`,
      pointerEvents: "none",
    }}
  >
    {lines.map((x, i) => (
      <div
        key={x}
        style={{
          fontSize: size,
          fontWeight: 600,
          letterSpacing: -size * 0.046,
          lineHeight: 1.01,
          translate: `0 ${i * 8 * (1 - fast(t, a, a + 0.7))}px`,
        }}
      >
        {x}
      </div>
    ))}
    {detail && (
      <div
        style={{
          fontSize: 44,
          lineHeight: 1.26,
          color: muted,
          marginTop: 32,
          opacity: fast(t, a + 0.15, a + 0.6),
        }}
      >
        {detail}
      </div>
    )}
  </div>
);
export const Caption = ({
  children,
  opacity = 1,
  top = 1740,
}: {
  children: React.ReactNode;
  opacity?: number;
  top?: number;
}) => (
  <div
    style={{
      ...A,
      left: 80,
      top,
      width: 920,
      fontSize: 38,
      lineHeight: 1.3,
      color: muted,
      opacity,
    }}
  >
    {children}
  </div>
);
export const Panel = ({
  children,
  style = {},
}: {
  children: React.ReactNode;
  style?: React.CSSProperties;
}) => (
  <div
    style={{
      background: "#15191b",
      border: "1px solid #303638",
      borderRadius: 12,
      boxSizing: "border-box",
      ...style,
    }}
  >
    {children}
  </div>
);
export const Control = ({
  label,
  value,
  text,
  min = -100,
  max = 100,
  cursor = false,
  colors,
}: {
  label: string;
  value: number;
  text: string;
  min?: number;
  max?: number;
  cursor?: boolean;
  colors?: string[];
}) => (
  <AppInspectorSlider
    label={label}
    value={value}
    valueText={text}
    from={min}
    to={max}
    scale={2.5}
    width={340}
    showCursor={cursor}
    press={cursor ? 1 : 0}
    trackColors={colors}
  />
);
export const Pointer = ({
  x,
  y,
  press = 0,
  opacity = 1,
}: {
  x: number;
  y: number;
  press?: number;
  opacity?: number;
}) => (
  <svg
    width={42}
    height={54}
    style={{
      ...A,
      left: x,
      top: y,
      opacity,
      filter: "drop-shadow(0 2px 4px #0008)",
      scale: 1 - 0.1 * press,
      transformOrigin: "0 0",
    }}
  >
    <path
      d="M3 2 L3 37 L12 29 L20 46 L27 43 L19 27 L32 26 Z"
      fill="white"
      stroke="#101415"
      strokeWidth={2}
    />
  </svg>
);
export const Dot = ({ active = true }: { active?: boolean }) => (
  <span
    style={{
      display: "inline-block",
      width: 10,
      height: 10,
      borderRadius: 20,
      background: active ? "#c5dcd4" : "#50595b",
    }}
  />
);
export const Icon = AppIcon;

export const ColorResponse = ({
  stage = 4,
  amount = 1,
  style = {},
  family = "color",
}: {
  stage?: number;
  amount?: number;
  style?: React.CSSProperties;
  family?: string;
}) => {
  const v = clamp(amount) * 8,
    lo = Math.floor(v),
    hi = Math.min(8, lo + 1),
    alpha = v - lo;
  return (
    <div
      style={{ position: "relative", width: "100%", height: "100%", ...style }}
    >
      <Photo src={`studio/${family}/${stage}-${lo}.png`} />
      {hi !== lo && (
        <Photo
          src={`studio/${family}/${stage}-${hi}.png`}
          style={{ ...A, inset: 0, opacity: alpha }}
        />
      )}
    </div>
  );
};
export const PixelPhoto = ({ amount = 0 }: { amount?: number }) => (
  <div
    style={{
      position: "relative",
      width: "100%",
      height: "100%",
      overflow: "hidden",
    }}
  >
    <Photo src="studio/color/4-8.png" />
    <Photo
      src="studio/subject-lift.png"
      style={{ ...A, inset: 0, opacity: clamp(amount) }}
    />
  </div>
);
export const Curve = ({
  amount = 0,
  width = 350,
  height = 260,
}: {
  amount?: number;
  width?: number;
  height?: number;
}) => {
  const v = clamp(amount) * 8,
    lo = Math.floor(v),
    hi = Math.min(8, lo + 1),
    alpha = v - lo;
  const d = curveData.x
    .map(
      (x, i) =>
        `${i === 0 ? "M" : "L"}${x * 350} ${(1 - mix(curveData.series[lo][i], curveData.series[hi][i], alpha)) * 260}`,
    )
    .join(" ");
  const pointY = (1 - (0.22 - 0.06 * amount)) * 260;
  return (
    <svg width={width} height={height} viewBox="0 0 350 260">
      <path
        d="M1 0V259H350 M87.5 0V260 M175 0V260 M262.5 0V260 M0 65H350 M0 130H350 M0 195H350"
        stroke="#3d4548"
        fill="none"
      />
      <path d={d} stroke="#d9e7e5" strokeWidth={3} fill="none" />
      <circle cx={77} cy={pointY} r={7} fill={accent} />
      <circle
        cx={192.5}
        cy={(1 - (0.55 + 0.05 * amount)) * 260}
        r={6}
        fill={accent}
      />
      <g transform={`translate(79 ${pointY + 2})`}>
        <path
          d="M0 0L0 27L7 21L14 35L20 32L12 18L23 18Z"
          fill="white"
          stroke="#11181b"
          strokeWidth={1.5}
        />
      </g>
    </svg>
  );
};
