import React, { useId } from "react";
import {
  CanvasImage,
  staticFile,
  spring,
  useCurrentFrame,
  useVideoConfig,
} from "remotion";
import { font } from "./Type";
export const A: React.CSSProperties = { position: "absolute" };
export const ink = "#0b0d12",
  paper = "#f1efdf",
  lime = "#d7ff48",
  blue = "#7270ff";
export const clamp = (v: number) => Math.max(0, Math.min(1, v));
export const ramp = (t: number, a: number, b: number) =>
  clamp((t - a) / (b - a));
export const smooth = (t: number, a: number, b: number) => {
  const u = ramp(t, a, b);
  return u * u * u * (10 + u * (-15 + 6 * u));
};
export const out = (t: number, a: number, b: number) =>
  1 - Math.pow(1 - ramp(t, a, b), 4);
export const mix = (a: number, b: number, v: number) => a + (b - a) * v;
export const pop = (t: number, a: number, fps = 60) =>
  spring({
    frame: Math.max(0, (t - a) * fps),
    fps,
    config: { mass: 0.7, damping: 17, stiffness: 230 },
  });
export const useTime = () => useCurrentFrame() / useVideoConfig().fps;
export const Image = ({
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
      fontFamily: font.mono,
      fontSize: 25,
      lineHeight: 1.4,
      letterSpacing: 1,
      ...style,
    }}
  >
    {children}
  </div>
);
export const PaperGrain = ({ opacity = 0.16 }: { opacity?: number }) => {
  const id = useId().replace(/:/g, "");
  return (
    <svg
      width="100%"
      height="100%"
      style={{
        ...A,
        inset: 0,
        pointerEvents: "none",
        mixBlendMode: "soft-light",
        opacity,
      }}
    >
      <defs>
        <filter id={id}>
          <feTurbulence
            type="fractalNoise"
            baseFrequency=".78"
            numOctaves={3}
            seed={19}
            stitchTiles="stitch"
          />
          <feColorMatrix type="saturate" values="0" />
        </filter>
      </defs>
      <rect width="100%" height="100%" filter={`url(#${id})`} />
    </svg>
  );
};
export const Cursor = ({
  x,
  y,
  down = false,
}: {
  x: number;
  y: number;
  down?: boolean;
}) => (
  <svg
    width={42}
    height={52}
    style={{
      ...A,
      left: x,
      top: y,
      scale: down ? 0.86 : 1,
      filter: "drop-shadow(0 3px 3px #0008)",
    }}
  >
    <path
      d="M5 3L7 39L17 29L25 47L32 43L23 26L38 25Z"
      fill={paper}
      stroke={ink}
      strokeWidth={2}
    />
  </svg>
);
export const Display = ({
  children,
  style = {},
}: {
  children: React.ReactNode;
  style?: React.CSSProperties;
}) => (
  <div
    style={{
      fontFamily: font.display,
      fontSize: 150,
      lineHeight: 0.97,
      letterSpacing: -2,
      ...style,
    }}
  >
    {children}
  </div>
);
