import React from "react";
import { CanvasImage, staticFile } from "remotion";
import { AppIcon } from "../app-kit/AppElements";
import { A, C, mono, p } from "../launch/design";

export const WeddingImage = ({
  name = "wedding-bride-02-developed",
  ext = "jpg",
  style = {},
}: {
  name?: string;
  ext?: string;
  style?: React.CSSProperties;
}) => (
  <CanvasImage
    src={staticFile(
      name.startsWith("range/")
        ? `media/${name}.${ext}`
        : `media/campaign/${name}.${ext}`,
    )}
    style={{ width: "100%", height: "100%", objectFit: "cover", ...style }}
  />
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
      background: C.ink900,
      border: `1px solid ${C.lineSoft}`,
      boxSizing: "border-box",
      ...style,
    }}
  >
    {children}
  </div>
);
export const Bar = ({ left, right }: { left: string; right?: string }) => (
  <div
    style={{
      height: 64,
      display: "flex",
      alignItems: "center",
      justifyContent: "space-between",
      padding: "0 24px",
      fontSize: 25,
      borderBottom: `1px solid ${C.lineSoft}`,
      color: C.textMuted,
    }}
  >
    <span>{left}</span>
    <span style={{ fontFamily: mono, fontSize: 21 }}>{right}</span>
  </div>
);
export const Caption = ({
  children,
  style = {},
}: {
  children: React.ReactNode;
  style?: React.CSSProperties;
}) => (
  <div style={{ fontSize: 42, lineHeight: 1.25, color: C.textMuted, ...style }}>
    {children}
  </div>
);
export const Tool = ({
  name,
  label,
  active = false,
}: {
  name: string;
  label: string;
  active?: boolean;
}) => (
  <div
    style={{
      display: "flex",
      gap: 18,
      alignItems: "center",
      padding: "15px 20px",
      background: active ? C.ink850 : "transparent",
      color: active ? C.brandPaper : C.textMuted,
      fontSize: 28,
    }}
  >
    <AppIcon name={name} size={29} />
    {label}
  </div>
);
export const Cursor = ({
  x,
  y,
  opacity = 1,
}: {
  x: number;
  y: number;
  opacity?: number;
}) => (
  <svg
    width="34"
    height="45"
    viewBox="0 0 20 26"
    style={{
      ...A,
      left: x,
      top: y,
      opacity,
      filter: "drop-shadow(0 2px 3px #0008)",
    }}
  >
    <path
      d="M2 1V20L7 15L11 24L15 22L11 13H18Z"
      fill="white"
      stroke="#111"
      strokeWidth="1"
    />
  </svg>
);
export const PhaseCopy = ({
  t,
  start,
  end,
  children,
}: {
  t: number;
  start: number;
  end: number;
  children: React.ReactNode;
}) => (
  <div
    style={{
      ...A,
      left: 80,
      top: 150,
      opacity: p(t, start, start + 0.35) * (1 - p(t, end - 0.55, end - 0.15)),
      translate: `0 ${p(t, start, start + 0.65, 45, 0)}px`,
    }}
  >
    {children}
  </div>
);
