import React, { useId } from "react";
import { CanvasImage, Easing, interpolate, staticFile } from "remotion";
import { AppIcon, AppWordmark, appTokens } from "../app-kit/AppElements";
import { GrainPlate } from "../film/GrainPlate";

export const C = appTokens;
export const A: React.CSSProperties = { position: "absolute" };
export const sans = '"Helvetica Neue", Helvetica, Arial, sans-serif';
export const mono = "Menlo, monospace";
export const asset = (name: string) => staticFile(`media/${name}.jpg`);
export const clamp = {
  extrapolateLeft: "clamp",
  extrapolateRight: "clamp",
} as const;
export const ease = Easing.bezier(0.22, 1, 0.36, 1);
export const move = Easing.bezier(0.65, 0, 0.18, 1);
export const p = (t: number, a: number, b: number, x = 0, y = 1) =>
  interpolate(t, [a, b], [x, y], { ...clamp, easing: move });
export const land = (t: number, a: number, b: number, x = 0, y = 1) =>
  interpolate(t, [a, b], [x, y], { ...clamp, easing: ease });
export const lerp = (a: number, b: number, k: number) => a + (b - a) * k;
export const Photo = ({
  name = "IMG_5106",
  style = {},
}: {
  name?: string;
  style?: React.CSSProperties;
}) => (
  <CanvasImage
    src={asset(name)}
    style={{ width: "100%", height: "100%", objectFit: "cover", ...style }}
  />
);
export const Plate = ({
  children,
  style = {},
}: {
  children?: React.ReactNode;
  style?: React.CSSProperties;
}) => (
  <div
    style={{
      ...A,
      inset: 0,
      overflow: "hidden",
      background: C.brandInk,
      color: C.brandPaper,
      fontFamily: sans,
      ...style,
    }}
  >
    {children}
  </div>
);
export const Grain = () => (
  <div style={{ ...A, inset: 0, pointerEvents: "none", zIndex: 80 }}>
    <GrainPlate amount={0.025} />
  </div>
);
export const Meta = ({
  children,
  style = {},
}: {
  children: React.ReactNode;
  style?: React.CSSProperties;
}) => (
  <div
    style={{
      fontFamily: mono,
      fontSize: 30,
      lineHeight: 1.4,
      color: C.textMuted,
      letterSpacing: 1,
      ...style,
    }}
  >
    {children}
  </div>
);
export const Copy = ({
  children,
  t,
  start = 0,
  size = 116,
  style = {},
}: {
  children: React.ReactNode;
  t: number;
  start?: number;
  size?: number;
  style?: React.CSSProperties;
}) => (
  <div
    style={{
      fontSize: size,
      fontWeight: 600,
      letterSpacing: -size * 0.045,
      lineHeight: 0.98,
      ...style,
    }}
  >
    {React.Children.map(children, (line, i) => (
      <div style={{ overflow: "hidden", paddingBottom: 10, marginBottom: -10 }}>
        <div
          style={{
            translate: `0 ${land(t, start + i * 0.075, start + 0.7 + i * 0.075, 110, 0)}%`,
          }}
        >
          {line}
        </div>
      </div>
    ))}
  </div>
);
export const Lockup = ({ width = 690 }: { width?: number }) => (
  <AppWordmark width={width} />
);
export const Badge = ({
  children,
  style = {},
}: {
  children: React.ReactNode;
  style?: React.CSSProperties;
}) => (
  <div
    style={{
      display: "inline-flex",
      alignItems: "center",
      gap: 14,
      border: `1px solid ${C.lineStrong}`,
      padding: "12px 20px",
      fontSize: 30,
      fontWeight: 500,
      lineHeight: 1.2,
      background: C.ink900,
      ...style,
    }}
  >
    {children}
  </div>
);
export const NativeLabel = ({
  name,
  children,
}: {
  name: string;
  children: React.ReactNode;
}) => (
  <span style={{ display: "flex", gap: 20, alignItems: "center" }}>
    <AppIcon name={name} size={34} />
    {children}
  </span>
);

export type Point = { x: number; y: number };
export const smoothPath = (points: Point[], close = false) => {
  let d = `M${points[0].x},${points[0].y}`;
  for (let i = 0; i < points.length - 1; i++) {
    const a = points[Math.max(0, i - 1)],
      b = points[i],
      c = points[i + 1],
      e = points[Math.min(points.length - 1, i + 2)];
    d += ` C${b.x + (c.x - a.x) / 6},${b.y + (c.y - a.y) / 6} ${c.x - (e.x - b.x) / 6},${c.y - (e.y - b.y) / 6} ${c.x},${c.y}`;
  }
  return d + (close ? " Z" : "");
};
const opticalWave = (x: number, t: number) =>
  0.34 * Math.sin(x * 1.5 + t) + 0.2 * Math.sin(x * 2.3 - t);
export const aperture = (w: number, h: number, open: number, phase: number) => {
  const upper: Point[] = [],
    lower: Point[] = [];
  for (let i = 0; i <= 32; i++) {
    const u = i / 32,
      a = Math.sin(Math.PI * open) * Math.min(h * 0.19, 150);
    const wave = opticalWave((u * 2 - 1) * 2.5, phase);
    upper.push({ x: u * w, y: (1 - open) * h * 0.5 + a * (0.7 + wave) });
    lower.unshift({ x: u * w, y: h - (1 - open) * h * 0.5 - a * (0.7 - wave) });
  }
  return smoothPath(upper) + smoothPath(lower).replace(/^M/, " L") + " Z";
};

// Filled refractive bands share a centerline with the reveal or control they
// become. RGB plates overlap additively; these are not a loader-video overlay.
export const LightMaterial = ({
  t,
  form = "sweep",
  width = 150,
  style = {},
}: {
  t: number;
  form?: "sweep" | "loop" | "curve" | "brand-upper" | "brand-lower";
  width?: number;
  style?: React.CSSProperties;
}) => {
  const id = useId().replace(/:/g, "");
  const points: Point[] = Array.from({ length: 81 }, (_, i) => {
    const u = i / 80;
    if (form === "brand-upper" || form === "brand-lower") {
      const q = 1 - u,
        upper = form === "brand-upper";
      const y0 = upper ? 380 : 2090,
        y1 = upper ? 30 : 2360,
        y2 = upper ? 1250 : 1120;
      return {
        x:
          q * q * q * -260 +
          3 * q * q * u * 350 +
          3 * q * u * u * (upper ? 120 : 1350) +
          u * u * u * 935,
        y:
          q * q * q * y0 +
          3 * q * q * u * y1 +
          3 * q * u * u * y2 +
          u * u * u * 930 +
          Math.sin(Math.PI * u) * opticalWave(u * 5, t * 0.7) * 28,
      };
    }
    if (form === "loop") {
      const theta = u * Math.PI * 2 + 0.2;
      const r = 590 + opticalWave(u * 5, t * 0.8) * 42;
      return {
        x: 540 + Math.cos(theta) * r,
        y: 940 + Math.sin(theta) * r * 0.8,
      };
    }
    if (form === "curve")
      return { x: -80 + u * 1240, y: 1470 - 1100 * (u * u * (3 - 2 * u)) };
    return {
      x: -240 + u * 1560,
      y:
        1030 +
        Math.sin(u * Math.PI * 2 + t * 0.35) * 370 +
        Math.cos(u * Math.PI * 3 - t * 0.2) * 75,
    };
  });
  const band = (offset: number, thickness: number) => {
    const a: Point[] = [],
      b: Point[] = [];
    for (let i = 0; i < points.length; i++) {
      const before = points[Math.max(0, i - 1)],
        after = points[Math.min(points.length - 1, i + 1)];
      const dx = after.x - before.x,
        dy = after.y - before.y,
        mag = Math.hypot(dx, dy) || 1;
      const u = i / (points.length - 1),
        n =
          width *
          (0.5 + 0.5 * Math.sin(u * Math.PI)) *
          (form.startsWith("brand") ? Math.pow(1 - u, 0.55) + 0.002 : 1);
      const center =
          (offset + Math.sin(u * 9 + t * 0.8 + offset * 4) * 0.08) * n,
        half = n * thickness * 0.5;
      a.push({
        x: points[i].x - (dy / mag) * (center + half),
        y: points[i].y + (dx / mag) * (center + half),
      });
      b.unshift({
        x: points[i].x - (dy / mag) * (center - half),
        y: points[i].y + (dx / mag) * (center - half),
      });
    }
    return smoothPath(a) + smoothPath(b).replace(/^M/, " L") + " Z";
  };
  return (
    <svg
      width={1080}
      height={1920}
      viewBox="0 0 1080 1920"
      style={{ ...A, inset: 0, overflow: "visible", ...style }}
    >
      <defs>
        <filter id={`${id}glow`} x="-30%" y="-30%" width="160%" height="160%">
          <feGaussianBlur stdDeviation={19} />
        </filter>
      </defs>
      <g style={{ mixBlendMode: "screen" }}>
        {[C.brandBlue, C.brandGreen, C.brandRed].map((c, i) => (
          <g key={c} style={{ mixBlendMode: "screen" }}>
            <path
              d={band((i - 1) * 0.29, 1.22)}
              fill={c}
              opacity={0.12}
              filter={`url(#${id}glow)`}
            />
            <path d={band((i - 1) * 0.29, 0.9)} fill={c} opacity={0.94} />
            <path d={band((i - 1) * 0.29 - 0.25, 0.045)} fill={c} />
            <path
              d={band((i - 1) * 0.29 + 0.24, 0.008)}
              fill="white"
              opacity={0.4}
            />
          </g>
        ))}
        <path d={band(0, 0.045)} fill="white" />
        <path d={band(-0.08, 0.08)} fill="white" opacity={0.25} />
      </g>
    </svg>
  );
};

export const Wipe = ({
  amount,
  children,
  phase = 0,
}: {
  amount: number;
  children: React.ReactNode;
  phase?: number;
}) => (
  <div
    style={{
      ...A,
      inset: 0,
      clipPath: `path('${aperture(1080, 1920, amount, phase)}')`,
    }}
  >
    {children}
  </div>
);

// One restrained overshoot for deliberate object arrivals and board reordering.
export const pop = (t: number, start: number, end: number) => {
  const u = Math.max(0, Math.min(1, (t - start) / (end - start)));
  if (u === 0 || u === 1) return u;
  const response = (v: number) =>
    1 - Math.exp(-7 * v) * (Math.cos(10 * v) + 0.7 * Math.sin(10 * v));
  return response(u) / response(1);
};
