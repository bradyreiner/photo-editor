import React from "react";
import {
  CanvasImage,
  Sequence,
  staticFile,
  useCurrentFrame,
  useVideoConfig,
} from "remotion";
import { Video } from "@remotion/media";
import { AppWordmark, AppInspectorSlider } from "../app-kit/AppElements";
import { DesktopLight } from "../desktop-launch/Light";
import {
  A,
  ink,
  paper,
  lime,
  smooth,
  out,
  mix,
  PaperGrain,
} from "../rebuild/Kit";
import { font } from "../rebuild/Type";
export { A, ink, paper, lime, smooth, out, mix, font, AppWordmark, PaperGrain };
export const useShot = () => {
  const { fps, width, height } = useVideoConfig();
  return {
    t: useCurrentFrame() / fps,
    fps,
    width,
    height,
    portrait: height > width,
  };
};
export const Pic = ({
  src,
  style = {},
}: {
  src: string;
  style?: React.CSSProperties;
}) => (
  <CanvasImage
    src={staticFile(`media/${src}`)}
    style={{
      ...A,
      inset: 0,
      width: "100%",
      height: "100%",
      objectFit: "cover",
      ...style,
    }}
  />
);
export const Film = ({
  src,
  trim = 0,
  style = {},
}: {
  src: string;
  trim?: number;
  style?: React.CSSProperties;
}) => {
  const { fps } = useVideoConfig();
  const { objectFit = "cover", ...rest } = style;
  return (
    <Video
      src={staticFile(`media/${src}`)}
      muted
      trimBefore={Math.round(trim * fps)}
      objectFit={objectFit as "cover" | "contain" | "fill"}
      style={{ ...A, inset: 0, width: "100%", height: "100%", ...rest }}
    />
  );
};
export const At = ({
  from,
  seconds,
  children,
  name,
}: {
  from: number;
  seconds?: number;
  children: React.ReactNode;
  name: string;
}) => {
  const { fps } = useVideoConfig();
  return (
    <Sequence
      from={Math.round(from * fps)}
      durationInFrames={seconds ? Math.round(seconds * fps) : Infinity}
      name={name}
    >
      {children}
    </Sequence>
  );
};
export const Light = ({
  t,
  opacity = 0.55,
  gather = 0,
}: {
  t: number;
  opacity?: number;
  gather?: number;
}) => <DesktopLight t={t} gather={gather} style={{ opacity }} />;
export const Label = ({
  children,
  style = {},
}: {
  children: React.ReactNode;
  style?: React.CSSProperties;
}) => (
  <div
    style={{
      fontFamily: font.mono,
      fontSize: 21,
      letterSpacing: 1.8,
      color: lime,
      ...style,
    }}
  >
    {children}
  </div>
);
export const Type = ({
  children,
  size = 100,
  serif = false,
  style = {},
}: {
  children: React.ReactNode;
  size?: number;
  serif?: boolean;
  style?: React.CSSProperties;
}) => (
  <div
    style={{
      fontFamily: serif ? font.serif : font.display,
      fontStyle: serif ? "italic" : "normal",
      fontSize: size,
      lineHeight: 1.01,
      letterSpacing: -1.8,
      color: paper,
      whiteSpace: "pre-line",
      ...style,
    }}
  >
    {children}
  </div>
);
export const Enter = ({
  children,
  at = 0,
  time,
  style = {},
}: {
  children: React.ReactNode;
  at?: number;
  time?: number;
  style?: React.CSSProperties;
}) => {
  const { t: clock } = useShot();
  const t = time ?? clock;
  return (
    <div
      style={{
        opacity: out(t, at, at + 0.55),
        transform: `translateY(${45 * (1 - out(t, at, at + 0.75))}px)`,
        ...style,
      }}
    >
      {children}
    </div>
  );
};
export const Caption = ({
  chapter,
  title,
  detail,
  serif = false,
  time,
  focus = 0,
}: {
  chapter: string;
  title: string;
  detail: string;
  serif?: boolean;
  time?: number;
  focus?: number;
}) => {
  const { portrait } = useShot();
  return (
    <div
      style={{
        ...A,
        left: portrait ? 62 : 82,
        top: portrait ? 160 : 254,
        width: portrait ? 950 : 470,
        zIndex: 5,
        opacity: portrait ? 1 : 1 - smooth(focus, 0, 0.65),
      }}
    >
      <Enter time={time}>
        <Label>{chapter}</Label>
      </Enter>
      <Enter at={0.05} time={time}>
        <Type
          size={portrait ? 108 : 91}
          serif={serif}
          style={{ marginTop: 24 }}
        >
          {title}
        </Type>
      </Enter>
      <Enter at={0.2} time={time}>
        <div
          style={{
            fontFamily: font.sans,
            fontSize: portrait ? 36 : 33,
            lineHeight: 1.4,
            marginTop: 28,
            maxWidth: portrait ? 900 : 445,
            color: "#c9ced6",
          }}
        >
          {detail}
        </div>
      </Enter>
    </div>
  );
};
// Each scene keeps its own artwork in a 1000 x 800 design space. Portrait
// repositions that stage below its own typography rather than letterboxing it.
export const Stage = ({
  children,
  style = {},
  focus = 0,
}: {
  children: React.ReactNode;
  style?: React.CSSProperties;
  focus?: number;
}) => {
  const { portrait } = useShot();
  return (
    <div
      style={{
        ...A,
        left: portrait ? 40 : mix(650, 210, focus),
        top: portrait ? mix(710, 650, focus) : mix(120, -30, focus),
        width: 1000,
        height: 800,
        transform: `scale(${portrait ? mix(1, 1.02, focus) : mix(1.1, 1.5, focus)})`,
        transformOrigin: "0 0",
        ...style,
      }}
    >
      {children}
    </div>
  );
};
export const Surface = ({
  children,
  style = {},
}: {
  children: React.ReactNode;
  style?: React.CSSProperties;
}) => (
  <div
    style={{
      ...A,
      inset: 0,
      overflow: "hidden",
      borderRadius: 20,
      background: ink,
      ...style,
    }}
  >
    {children}
  </div>
);
export const Native = ({
  src,
  video = false,
  trim = 0,
  style = {},
}: {
  src: string;
  video?: boolean;
  trim?: number;
  style?: React.CSSProperties;
}) => (
  <div
    style={{
      ...A,
      left: 0,
      top: 80,
      width: 1000,
      height: 585,
      overflow: "hidden",
      borderRadius: 13,
      border: "1px solid #78849955",
      boxShadow: "0 35px 100px #0009",
      background: "#11151a",
      ...style,
    }}
  >
    {video ? (
      <Film src={src} trim={trim} style={{ objectFit: "contain" }} />
    ) : (
      <Pic src={src} style={{ objectFit: "contain" }} />
    )}
  </div>
);
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
    scale={2.1}
    width={360}
    showCursor={cursor}
    press={cursor ? 1 : 0}
  />
);
export const Pills = ({
  items,
  style = {},
}: {
  items: string[];
  style?: React.CSSProperties;
}) => (
  <div style={{ display: "flex", gap: 12, flexWrap: "wrap", ...style }}>
    {items.map((v, i) => (
      <div
        key={v}
        style={{
          padding: "13px 18px",
          border: "1px solid #c4c8ce55",
          borderRadius: 40,
          fontFamily: font.mono,
          fontSize: 19,
          color: i === 0 ? lime : paper,
          background: "#0b0d12bd",
        }}
      >
        {v}
      </div>
    ))}
  </div>
);
export const Footer = ({ left, right }: { left: string; right: string }) => {
  const { portrait } = useShot();
  return (
    <div
      style={{
        ...A,
        left: portrait ? 62 : 82,
        right: portrait ? 62 : 82,
        bottom: portrait ? 130 : 22,
        display: "flex",
        justifyContent: "space-between",
        alignItems: "end",
        color: "#aeb6c3",
        fontFamily: font.mono,
        fontSize: portrait ? 20 : 17,
        letterSpacing: 1,
      }}
    >
      <span>{left}</span>
      <span>{right}</span>
    </div>
  );
};
export const Sweep = ({ u, color = lime }: { u: number; color?: string }) => (
  <svg
    style={{
      ...A,
      inset: 0,
      width: "100%",
      height: "100%",
      pointerEvents: "none",
      mixBlendMode: "screen",
    }}
    viewBox="0 0 1000 800"
  >
    <path
      d="M-250 830C160 780 245 60 600 220S1000 590 1250 -70"
      fill="none"
      stroke={color}
      strokeWidth={130}
      pathLength={1}
      strokeDasharray={1}
      strokeDashoffset={1 - u}
      strokeLinecap="round"
      opacity={0.28}
    />
    <path
      d="M-250 830C160 780 245 60 600 220S1000 590 1250 -70"
      fill="none"
      stroke={color}
      strokeWidth={3}
      pathLength={1}
      strokeDasharray={1}
      strokeDashoffset={1 - u}
      strokeLinecap="round"
      opacity={0.7}
    />
  </svg>
);
