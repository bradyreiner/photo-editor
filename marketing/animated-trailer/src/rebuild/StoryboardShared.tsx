import React from "react";
import { staticFile } from "remotion";
import { AppInspectorSlider } from "../app-kit/AppElements";
import {
  A,
  Display,
  Image,
  Tag,
  ink,
  lime,
  paper,
  out,
  smooth,
  useTime,
} from "./Kit";
import { font } from "./Type";
import { storyboardPlan } from "./StoryboardPlan";
export const Cue = ({ id }: { id: string }) => {
  const t = useTime(),
    scene = storyboardPlan.scenes.find((s) => s.id === id)!;
  const b =
    scene.beats.find((b) => t >= b.from && t < b.to) ||
    scene.beats[scene.beats.length - 1];
  if (!b.copy) return null;
  return (
    <div
      style={{
        ...A,
        left: 80,
        top: 115,
        width: 920,
        pointerEvents: "none",
        opacity: out(t, b.from, b.from + 0.25),
        translate: `0 ${25 * (1 - out(t, b.from, b.from + 0.4))}px`,
        zIndex: 12,
      }}
    >
      <Tag style={{ color: lime, marginBottom: 22 }}>
        {scene.act.toUpperCase()} / {scene.title.toUpperCase()}
      </Tag>
      <Display
        style={{
          fontFamily:
            b.kind === "payoff" ||
            (id === "wedding" && t < 3.7) ||
            id === "teaser"
              ? font.serif
              : id === "color"
                ? font.sans
                : font.display,
          fontStyle:
            b.kind === "payoff" ||
            (id === "wedding" && t < 3.7) ||
            id === "teaser"
              ? "italic"
              : "normal",
          fontSize: b.copy.length > 34 ? 102 : id === "color" ? 110 : 124,
          lineHeight: 1.01,
          letterSpacing: -1,
          maxWidth: 920,
        }}
      >
        {id === "color" && t >= 3.4 && t < 6.6 ? (
          <>
            Sony S-Log2.
            <br />
            S-Log3.
          </>
        ) : (
          b.copy
        )}
      </Display>
    </div>
  );
};
export const Slider = ({
  label,
  value,
  text,
  min = -5,
  max = 5,
  cursor = false,
}: {
  label: string;
  value: number;
  text: string;
  min?: number;
  max?: number;
  cursor?: boolean;
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
  />
);
export const Dock = ({
  children,
  top = 1460,
  style = {},
}: {
  children: React.ReactNode;
  top?: number;
  style?: React.CSSProperties;
}) => (
  <div
    style={{
      ...A,
      left: 80,
      top,
      width: 920,
      padding: 34,
      boxSizing: "border-box",
      background: "#121820ee",
      border: "1px solid #52616b",
      borderRadius: 17,
      ...style,
    }}
  >
    {children}
  </div>
);
export const Caption = ({ children }: { children: React.ReactNode }) => (
  <div
    style={{
      ...A,
      left: 80,
      top: 1750,
      width: 920,
      fontFamily: font.sans,
      fontSize: 40,
      lineHeight: 1.25,
      color: paper,
    }}
  >
    {children}
  </div>
);
export const NativeResponse = ({
  stage,
  u,
  family = "color",
  style = {},
}: {
  stage: number;
  u: number;
  family?: string;
  style?: React.CSSProperties;
}) => {
  const n = Math.max(0, Math.min(8, u * 8)),
    a = Math.floor(n),
    b = Math.min(8, a + 1);
  return (
    <div style={{ ...A, inset: 0, ...style }}>
      <Image src={`studio/${family}/${stage}-${a}.png`} />
      {b !== a && (
        <Image
          src={`studio/${family}/${stage}-${b}.png`}
          style={{ ...A, inset: 0, opacity: n - a }}
        />
      )}
    </div>
  );
};
export const MatteWalk = ({
  source,
  motion = 0,
  title = true,
}: {
  source: number;
  motion?: number;
  title?: boolean;
}) => {
  const n = String(
    Math.max(0, Math.min(184, Math.floor(source * 30))),
  ).padStart(3, "0");
  return (
    <div style={{ ...A, inset: 0, overflow: "hidden" }}>
      <div
        style={{
          ...A,
          inset: 0,
          scale: 1 + 0.12 * motion,
          transformOrigin: "43% 46%",
        }}
      >
        <Image src={`studio/hike/${n}.jpg`} />
        {title && (
          <div
            style={{
              ...A,
              left: 120 + 150 * motion,
              top: 125,
              fontFamily: font.display,
              fontSize: 146,
              lineHeight: 0.94,
              color: paper,
            }}
          >
            OFF THE
            <br />
            MAP.
          </div>
        )}
        <Image src={`studio/hike/${n}.png`} style={{ ...A, inset: 0 }} />
      </div>
    </div>
  );
};
export const MovieFrame = ({
  children,
  style = {},
}: {
  children: React.ReactNode;
  style?: React.CSSProperties;
}) => {
  const w = typeof style.width === "number" ? style.width : 1080,
    h = typeof style.height === "number" ? style.height : 607.5,
    s = Math.max(w / 1280, h / 720);
  return (
    <div
      style={{
        ...A,
        left: 0,
        top: 620,
        width: 1080,
        height: 607.5,
        overflow: "hidden",
        background: ink,
        ...style,
      }}
    >
      <div
        style={{
          ...A,
          left: (w - 1280 * s) / 2,
          top: (h - 720 * s) / 2,
          width: 1280,
          height: 720,
          scale: s,
          transformOrigin: "0 0",
        }}
      >
        {children}
      </div>
    </div>
  );
};
export const filmStyle = { width: "100%", height: "100%" };
export const asset = (s: string) => staticFile("media/" + s);
export const Thumb = ({
  src,
  style = {},
}: {
  src: string;
  style?: React.CSSProperties;
}) => (
  <div style={{ overflow: "hidden", ...style }}>
    <Image src={src} />
  </div>
);
export const FadeEnd = ({ t, a, b }: { t: number; a: number; b: number }) => (
  <div
    style={{
      ...A,
      inset: 0,
      background: ink,
      opacity: smooth(t, a, b),
      pointerEvents: "none",
    }}
  />
);
