import React from "react";
import { useCurrentFrame, useVideoConfig } from "remotion";
import {
  CanvasImage,
  Easing,
  interpolate,
  interpolateColors,
  staticFile,
} from "remotion";
import { AppIcon, appTokens as T, appUiFont } from "../app-kit/AppElements";
import { sampleAppCurve } from "../app-kit/curve-sampling";
export { T };
export const abs: React.CSSProperties = { position: "absolute" };
export const clamp = {
  extrapolateLeft: "clamp",
  extrapolateRight: "clamp",
} as const;
export const glide = Easing.bezier(0.72, 0, 0.16, 1);
export const arrive = Easing.bezier(0.16, 1, 0.26, 1);
export const mix = (t: number, a: number, b: number, x = 0, y = 1) =>
  interpolate(t, [a, b], [x, y], { ...clamp, easing: glide });
export const enter = (t: number, a: number, b: number, x = 0, y = 1) =>
  interpolate(t, [a, b], [x, y], { ...clamp, easing: arrive });
export const pressAt = (t: number, start: number, end: number) =>
  enter(t, start - 0.11, start) * (1 - mix(t, end, end + 0.11));
export const brandAccentAt = (t: number) =>
  t >= 73 && t < 87
    ? interpolateColors(t, [80, 80.25], [T.brandRed, T.brandBlue])
    : T.brandRed;
export const windowAt = (t: number, start: number, end: number) =>
  mix(t, start, start + 0.65) * (1 - mix(t, end - 0.65, end));
export const asset = (name: string) => staticFile(`media/${name}`);
export const photos = [
  "IMG_5071",
  "IMG_5106",
  "IMG_4870",
  "IMG_4969",
  "IMG_5103",
  "IMG_5111",
  "IMG_5125",
  "IMG_4856",
  "IMG_4744",
  "IMG_5053",
  "IMG_5124",
  "IMG_5077",
];
export type Pose = {
  x: number;
  y: number;
  w: number;
  h: number;
  r: number;
  ry: number;
};
const stops: [number, Pose][] = [
  [0, { x: 460, y: 825, w: 130, h: 195, r: 0, ry: 0 }],
  [6.5, { x: 405, y: 570, w: 270, h: 405, r: 0, ry: 0 }],
  [13.8, { x: 72, y: 480, w: 570, h: 855, r: 0, ry: 0 }],
  [26.8, { x: 74, y: 480, w: 620, h: 930, r: -2, ry: -5 }],
  [35.9, { x: 135, y: 470, w: 810, h: 1215, r: -4, ry: -9 }],
  [49.8, { x: 135, y: 470, w: 810, h: 1215, r: 0, ry: 0 }],
  [57, { x: 80, y: 500, w: 920, h: 518, r: 0, ry: 0 }],
  [73.9, { x: 72, y: 520, w: 580, h: 725, r: 0, ry: 0 }],
  [84.9, { x: 72, y: 780, w: 168, h: 210, r: 0, ry: 0 }],
  [88.1, { x: 72, y: 550, w: 355, h: 532.5, r: 0, ry: 0 }],
  [94.8, { x: 405, y: 665, w: 270, h: 405, r: 0, ry: 0 }],
  [105.9, { x: 72, y: 650, w: 290, h: 435, r: 0, ry: 0 }],
  [118.2, { x: 220, y: 800, w: 240, h: 360, r: -5, ry: -8 }],
  [120, { x: 220, y: 800, w: 240, h: 360, r: -5, ry: -8 }],
];
const moveDurations = [
  0, 1.5, 1.35, 1.2, 1.35, 0.95, 1.3, 1.3, 1.6, 1.25, 1.25, 1.2, 1.5, 0,
];
// A short acceleration followed by one restrained settle. This response is
// normalized at its endpoint, so held frames cannot drift or keep wobbling.
export const settle = (t: number, start: number, end: number) => {
  const p = Math.max(0, Math.min(1, (t - start) / (end - start)));
  if (p === 0 || p === 1) return p;
  const response = (v: number) =>
    1 - Math.exp(-8 * v) * (Math.cos(9 * v) + (8 / 9) * Math.sin(9 * v));
  return response(p) / response(1);
};
export const poseAt = (t: number): Pose => {
  let previous = stops[0][1];
  for (let i = 1; i < stops.length; i++) {
    const [end, next] = stops[i],
      start = end - moveDurations[i];
    if (t < start) return previous;
    if (t <= end && end > start) {
      const p = settle(t, start, end);
      const result = {} as Pose;
      for (const key of ["x", "y", "w", "h", "r", "ry"] as const)
        result[key] = previous[key] + (next[key] - previous[key]) * p;
      return result;
    }
    previous = next;
  }
  return previous;
};
// Native optical_light.frag wave equations drive the ACTUAL aperture geometry.
// The light is a media boundary: it reveals/crops media and becomes panel edges.
export const wave = (u: number, phase: number, channel = 0) => {
  const x = (u * 2 - 1) * 2.5,
    t = phase * Math.PI * 2;
  return (
    0.34 * Math.sin(x * 1.5 + t) +
    0.2 * Math.sin(x * 2.3 - t) +
    channel * 0.055 * Math.cos(x + t)
  );
};
export const contour = (
  w: number,
  h: number,
  amount: number,
  phase: number,
  open = 1,
  channel = 0,
) => {
  const amplitude = Math.min(h * 0.12, 54) * amount * open;
  const upper: { x: number; y: number }[] = [],
    lower: { x: number; y: number }[] = [];
  for (let i = 0; i <= 32; i++) {
    const u = i / 32,
      x = u * w;
    upper.push({
      x,
      y: ((1 - open) * h) / 2 + amplitude * (0.65 + wave(u, phase, channel)),
    });
    lower.unshift({
      x,
      y:
        h -
        ((1 - open) * h) / 2 -
        amplitude * (0.65 - wave(u, phase + 0.13, channel)),
    });
  }
  const smooth = (points: { x: number; y: number }[], first: boolean) => {
    let d = `${first ? "M" : "L"}${points[0].x.toFixed(2)},${points[0].y.toFixed(2)}`;
    for (let i = 0; i < points.length - 1; i++) {
      const a = points[Math.max(0, i - 1)],
        b = points[i],
        c = points[i + 1],
        e = points[Math.min(points.length - 1, i + 2)];
      d += ` C${(b.x + (c.x - a.x) / 6).toFixed(2)},${(b.y + (c.y - a.y) / 6).toFixed(2)} ${(c.x - (e.x - b.x) / 6).toFixed(2)},${(c.y - (e.y - b.y) / 6).toFixed(2)} ${c.x.toFixed(2)},${c.y.toFixed(2)}`;
    }
    return d;
  };
  return smooth(upper, true) + smooth(lower, false) + " Z";
};
export const rippleAt = (t: number) =>
  stops
    .slice(1)
    .reduce(
      (sum, [end], i) =>
        moveDurations[i + 1] > 0
          ? sum + Math.sin(Math.PI * mix(t, end - moveDurations[i + 1], end))
          : sum,
      0,
    );
export const Photo = ({
  name,
  style,
}: {
  name: string;
  style?: React.CSSProperties;
}) => (
  <CanvasImage
    src={asset(`${name}.jpg`)}
    style={{ width: "100%", height: "100%", objectFit: "cover", ...style }}
  />
);
export const IconLabel = ({
  icon,
  children,
  color = T.textMuted,
}: {
  icon: string;
  children: React.ReactNode;
  color?: string;
}) => (
  <span
    style={{ display: "inline-flex", alignItems: "center", gap: 12, color }}
  >
    <AppIcon name={icon} size={22} color={color} />
    {children}
  </span>
);
export const Panel = ({
  x,
  y,
  w,
  children,
  style,
}: {
  x: number;
  y: number;
  w: number;
  children: React.ReactNode;
  style?: React.CSSProperties;
}) => {
  const frame = useCurrentFrame();
  const { fps } = useVideoConfig();
  const open =
    typeof style?.opacity === "number"
      ? Math.min(1, Math.max(0, style.opacity))
      : 1;
  const bend = Math.sin(Math.PI * open) * 4;
  const upper = [],
    lower = [];
  for (let i = 0; i <= 24; i++) {
    const u = i / 24;
    upper.push(
      `${u * 100}% ${(1 - open) * 50 + bend * (0.65 + wave(u, frame / fps / 11))}%`,
    );
    lower.unshift(
      `${u * 100}% ${100 - (1 - open) * 50 - bend * (0.65 - wave(u, frame / fps / 11 + 0.13))}%`,
    );
  }
  return (
    <div
      style={{
        ...abs,
        left: x,
        top: y,
        width: w,
        background: T.ink900,
        fontFamily: appUiFont,
        ...style,
        opacity: 1,
        clipPath: `polygon(${upper.concat(lower).join(",")})`,
      }}
    >
      {children}
    </div>
  );
};
export const Eyebrow = ({ children }: { children: React.ReactNode }) => (
  <div
    style={{
      fontFamily: "Menlo,monospace",
      fontSize: 17,
      letterSpacing: 1.5,
      color: T.textMuted,
    }}
  >
    {children}
  </div>
);
export const NativeRows = ({
  rows,
  active = 0,
}: {
  rows: string[];
  active?: number;
}) => (
  <div>
    {rows.map((label, i) => (
      <div
        key={label}
        style={{
          height: 49,
          display: "flex",
          alignItems: "center",
          gap: 12,
          padding: "0 14px",
          background: i === active ? T.ink800 : "transparent",
          fontSize: 19,
          color: i === active ? T.brandPaper : T.textMuted,
        }}
      >
        <AppIcon
          name={i === 0 ? "image" : i === 1 ? "layers" : "adjust"}
          size={18}
        />
        <span>{label}</span>
        <span style={{ marginLeft: "auto", opacity: 0.5 }}>
          <AppIcon name="visible" size={17} />
        </span>
      </div>
    ))}
  </div>
);
export const ToneCurve = ({
  amount = 1,
  size = 300,
}: {
  amount?: number;
  size?: number;
}) => {
  const points = [
    { x: 0, y: 0 },
    { x: 0.34, y: 0.34 - amount * 0.095 },
    { x: 0.7, y: 0.7 + amount * 0.08 },
    { x: 1, y: 1 },
  ];
  const samples = sampleAppCurve(points);
  const path = samples
    .map((p, i) => `${i ? "L" : "M"}${2 + p.x * 296},${298 - p.y * 296}`)
    .join(" ");
  return (
    <svg
      width={size}
      height={size}
      viewBox="0 0 300 300"
      style={{ display: "block", background: T.ink950 }}
    >
      {[0, 1, 2, 3, 4].map((i) => (
        <g key={i} stroke={T.lineSoft} strokeWidth={1}>
          <path d={`M${i * 74 + 2} 2V298M2 ${i * 74 + 2}H298`} />
        </g>
      ))}
      <path
        d="M2 298L298 2"
        stroke={T.lineStrong}
        strokeDasharray="3 6"
        fill="none"
      />
      <path d={path} stroke={T.brandPaper} strokeWidth={2.4} fill="none" />
      {points.map((p, i) => (
        <circle
          key={i}
          cx={2 + p.x * 296}
          cy={298 - p.y * 296}
          r={i === 1 ? 5 : 3}
          fill={i === 1 ? T.brandPaper : T.ink900}
          stroke={T.brandPaper}
          strokeWidth={1.5}
        />
      ))}
    </svg>
  );
};
