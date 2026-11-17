import React from "react";
import { CanvasImage, staticFile } from "remotion";
import { AppIcon, AppWordmark } from "../app-kit/AppElements";
import { A, C, mono, smoothPath } from "../launch/design";

export { A, C, mono };
export const mix = (a: number, b: number, v: number) => a + (b - a) * v;
export const q = (t: number, a: number, b: number) => {
  const x = Math.min(1, Math.max(0, (t - a) / (b - a)));
  return x * x * x * (x * (x * 6 - 15) + 10);
};
export const show = (t: number, a: number, b: number) =>
  q(t, a, a + 0.45) * (1 - q(t, b - 0.45, b));
export const Picture = ({
  src,
  style = {},
}: {
  src: string;
  style?: React.CSSProperties;
}) => (
  <CanvasImage
    src={staticFile(`media/${src}`)}
    style={{ width: "100%", height: "100%", objectFit: "cover", ...style }}
  />
);
export const out = (t: number, a: number, b: number) => {
  const x = Math.max(0, Math.min(1, (t - a) / (b - a)));
  return 1 - Math.pow(1 - x, 3);
};
export const softness = (t: number, a: number, b: number, distance = 1080) =>
  Math.min(
    3.2,
    Math.abs(q(t + 1 / 120, a, b) - q(t - 1 / 120, a, b)) * distance * 0.18,
  );
export const Head = ({
  t,
  a = 0,
  b = 100,
  lines,
  detail,
  size = 104,
  top = 150,
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
      opacity: 1 - q(t, b - 0.3, b),
      pointerEvents: "none",
    }}
  >
    {lines.map((line, i) => (
      <div
        key={line}
        style={{
          fontSize: size,
          fontWeight: 600,
          lineHeight: 1.01,
          letterSpacing: -size * 0.045,
          opacity: out(t, a + i * 0.065, a + 0.32 + i * 0.065),
          translate: `0 ${30 * (1 - out(t, a + i * 0.065, a + 0.52 + i * 0.065)) - 12 * q(t, b - 0.3, b)}px`,
        }}
      >
        {line}
      </div>
    ))}
    {detail && (
      <div
        style={{
          fontSize: 40,
          color: C.textMuted,
          lineHeight: 1.3,
          marginTop: 35,
          opacity: out(t, a + 0.25, a + 0.65),
          translate: `0 ${12 * (1 - out(t, a + 0.25, a + 0.75))}px`,
        }}
      >
        {detail}
      </div>
    )}
  </div>
);
export const Note = ({
  children,
  style = {},
}: {
  children: React.ReactNode;
  style?: React.CSSProperties;
}) => (
  <div
    style={{
      ...A,
      left: 80,
      top: 1710,
      width: 920,
      fontSize: 42,
      lineHeight: 1.3,
      color: C.textMuted,
      ...style,
    }}
  >
    {children}
  </div>
);
export const Chrome = ({ mode }: { mode: string }) => (
  <div
    style={{
      ...A,
      left: 80,
      top: 80,
      width: 920,
      height: 42,
      display: "flex",
      alignItems: "center",
      gap: 36,
      color: C.textMuted,
      fontSize: 24,
    }}
  >
    <AppWordmark width={130} />
    <span style={{ height: 22, width: 1, background: C.lineStrong }} />
    {mode}
  </div>
);
export const Surface = ({
  children,
  style = {},
}: {
  children: React.ReactNode;
  style?: React.CSSProperties;
}) => (
  <div
    style={{
      background: C.ink900,
      border: `1px solid ${C.lineSoft}`,
      borderRadius: 12,
      boxSizing: "border-box",
      ...style,
    }}
  >
    {children}
  </div>
);
export const Label = ({
  children,
  style = {},
}: {
  children: React.ReactNode;
  style?: React.CSSProperties;
}) => (
  <div
    style={{
      fontFamily: mono,
      fontSize: 24,
      color: C.textMuted,
      letterSpacing: 1,
      ...style,
    }}
  >
    {children}
  </div>
);

// Spectral filaments focus into the exact two-segment native curve geometry.
const cubic = (a: number, b: number, c: number, d: number, s: number) =>
  Math.pow(1 - s, 3) * a +
  3 * Math.pow(1 - s, 2) * s * b +
  3 * (1 - s) * s * s * c +
  s * s * s * d;
const curvePoint = (u: number, tone: number) =>
  u < 0.5
    ? {
        x: cubic(0, 70, 105, 140, u * 2),
        y: cubic(260, 205 + 25 * tone, 190 + 12 * tone, 140, u * 2),
      }
    : {
        x: cubic(140, 175, 230, 280, (u - 0.5) * 2),
        y: cubic(140, 90 - 12 * tone, 60 - 20 * tone, 0, (u - 0.5) * 2),
      };
export const Light = ({
  t,
  amount = 1,
  curve = 0,
  tone = 0,
  y = 1130,
}: {
  t: number;
  amount?: number;
  curve?: number;
  tone?: number;
  y?: number;
}) => (
  <svg
    width={1080}
    height={1920}
    style={{ ...A, inset: 0, pointerEvents: "none", opacity: amount }}
  >
    <defs>
      <filter id="spectrum-halo">
        <feGaussianBlur stdDeviation="15" />
      </filter>
    </defs>
    {["#ff655a", "#61e7a4", "#689dff"].map((color, ch) => (
      <g key={color} style={{ mixBlendMode: "screen" }}>
        {Array.from({ length: 7 }, (_, k) => k - 3).map((r) => {
          const points = Array.from({ length: 81 }, (_, i) => {
            const u = i / 80,
              focus = curvePoint(u, tone),
              envelope = Math.sin(Math.PI * u);
            const bend =
              Math.sin(u * 7.4 - t * 0.67) * 85 +
              Math.sin(u * 12.3 + t * 0.43) * 23;
            const fold =
              r * 13 * Math.sin(u * 6.2 - t * 0.54) * envelope +
              (ch - 1) * 22 * Math.cos(u * 5.1 + t * 0.45) * envelope;
            return {
              x: mix(-100 + u * 1280, 660 + focus.x, curve),
              y: mix(y + bend + fold, 1112 + focus.y, curve),
            };
          });
          const d = smoothPath(points);
          return (
            <g key={r}>
              <path
                d={d}
                fill="none"
                stroke={color}
                strokeWidth={25}
                opacity={0.035}
                filter="url(#spectrum-halo)"
              />
              <path
                d={d}
                fill="none"
                stroke={color}
                strokeWidth={mix(1.1, 1.6, curve)}
                opacity={0.32 + 0.22 * Math.cos(r * 0.4)}
              />
            </g>
          );
        })}
      </g>
    ))}
  </svg>
);

export const Layers = ({
  amount = 1,
  title = "NEPAL / FIELD NOTES",
}: {
  amount?: number;
  title?: string;
}) => (
  <Surface style={{ padding: "20px 24px", fontSize: 27, opacity: amount }}>
    <Label style={{ marginBottom: 15 }}>LAYERS</Label>
    {[
      ["selection", "Subject mask"],
      ["text", title],
      ["adjust", "Color adjustments"],
      ["image", "Original photo"],
    ].map(([icon, text]) => (
      <div
        key={text}
        style={{
          display: "flex",
          gap: 17,
          alignItems: "center",
          padding: "15px 0",
          borderTop: `1px solid ${C.lineSoft}`,
        }}
      >
        <AppIcon name={icon} size={26} />
        {text}
      </div>
    ))}
  </Surface>
);
