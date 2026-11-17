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
      opacity: show(t, a - 0.45, b),
      pointerEvents: "none",
    }}
  >
    {lines.map((line, i) => (
      <div
        key={line}
        style={{ overflow: "hidden", paddingBottom: 7, marginBottom: -7 }}
      >
        <div
          style={{
            fontSize: size,
            fontWeight: 600,
            lineHeight: 1.01,
            letterSpacing: -size * 0.045,
            translate: `0 ${mix(115, 0, q(t, a + i * 0.07, a + 0.85 + i * 0.07)) - 18 * q(t, b - 0.6, b)}%`,
          }}
        >
          {line}
        </div>
      </div>
    ))}
    {detail && (
      <div
        style={{
          fontSize: 40,
          color: C.textMuted,
          lineHeight: 1.3,
          marginTop: 35,
          opacity: q(t, a + 0.5, a + 1.1),
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

// The same moving wavefront bends into editing geometry; no loader is pasted over the film.
export const Light = ({
  t,
  amount = 1,
  curve = 0,
  y = 1130,
}: {
  t: number;
  amount?: number;
  curve?: number;
  y?: number;
}) => (
  <svg
    width={1080}
    height={1920}
    style={{ ...A, inset: 0, pointerEvents: "none", opacity: amount }}
  >
    <defs>
      <filter id="soft-spectrum">
        <feGaussianBlur stdDeviation="11" />
      </filter>
    </defs>
    {["#ff655a", "#61e7a4", "#689dff"].map((c, ch) => (
      <g key={c} style={{ mixBlendMode: "screen" }}>
        {[-1, 0, 1].map((r) => {
          const pts = Array.from({ length: 65 }, (_, i) => {
            const u = i / 64,
              w =
                Math.sin(u * 7.4 - t * 0.9) * 100 +
                Math.sin(u * 12.3 + t * 0.6) * 31;
            return {
              x: mix(-100 + u * 1280, 688 + u * 275, curve),
              y: mix(
                y +
                  w +
                  (ch - 1) * 19 * Math.cos(u * 5 + t) +
                  r * 15 * Math.sin(u * 6 - t),
                1455 - 250 * (u * 0.7 + Math.pow(u, 0.6) * 0.3),
                curve,
              ),
            };
          });
          const d = smoothPath(pts);
          return (
            <g key={r}>
              <path
                d={d}
                fill="none"
                stroke={c}
                strokeWidth={16}
                opacity={0.15}
                filter="url(#soft-spectrum)"
              />
              <path
                d={d}
                fill="none"
                stroke={c}
                strokeWidth={1.8}
                opacity={0.65}
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
