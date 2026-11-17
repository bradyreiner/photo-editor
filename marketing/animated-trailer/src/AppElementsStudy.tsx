import React from "react";
import {
  AbsoluteFill,
  CanvasImage,
  Easing,
  interpolate,
  staticFile,
  useCurrentFrame,
  useVideoConfig,
} from "remotion";
import {
  AppActivity,
  AppCompletion,
  AppIcon,
  AppInspectorSlider,
  AppOpticalField,
  AppWordmark,
  appTokens as T,
  appUiFont,
} from "./app-kit/AppElements";
const clamp = { extrapolateLeft: "clamp", extrapolateRight: "clamp" } as const;
const curve = Easing.bezier(0.2, 0.7, 0.2, 1);
const a: React.CSSProperties = { position: "absolute" };
export const AppElementsStudy = () => {
  const f = useCurrentFrame();
  const { fps } = useVideoConfig();
  const t = f / fps;
  const exposure = interpolate(
    t,
    [0, 1.2, 3.5, 4.8, 5.6, 6.4, 8, 10, 11.5],
    [0, 0, 1.25, 1.25, 0, 0, -0.35, -0.35, 0],
    { ...clamp, easing: curve },
  );
  const press = interpolate(
    t,
    [0, 1.1, 1.21, 3.55, 3.66, 6.35, 6.46, 8.05, 8.16],
    [0, 0, 1, 1, 0, 0, 1, 1, 0],
    clamp,
  );
  const progress = interpolate(t, [0, 9.6], [0.02, 1], clamp),
    complete = t >= 9.6;
  return (
    <AbsoluteFill
      style={{
        background: T.ink950,
        color: T.brandPaper,
        fontFamily: appUiFont,
      }}
    >
      <div style={{ ...a, left: 64, top: 42 }}>
        <AppWordmark width={240} />
      </div>
      <div
        style={{ ...a, right: 64, top: 54, fontSize: 17, color: T.textMuted }}
      >
        APP ELEMENTS · COMPONENT STUDY
      </div>
      <div
        style={{
          ...a,
          left: 64,
          top: 137,
          fontSize: 42,
          fontWeight: 600,
          letterSpacing: -1.4,
        }}
      >
        The controls are part of the identity.
      </div>
      <div
        style={{
          ...a,
          left: 64,
          top: 218,
          width: 700,
          height: 690,
          background: T.brandInk,
          overflow: "hidden",
        }}
      >
        <CanvasImage
          src={staticFile("media/IMG_5106.jpg")}
          style={{
            width: "100%",
            height: "100%",
            objectFit: "contain",
            filter: `brightness(${Math.pow(2, exposure * 0.24)})`,
          }}
        />
        <div
          style={{
            ...a,
            left: 24,
            top: 20,
            display: "flex",
            gap: 10,
            alignItems: "center",
            fontSize: 14,
            color: T.textMuted,
          }}
        >
          <AppIcon name="image" size={18} />
          IMG_5106.CR2 · 4000 × 6000
        </div>
        <div
          style={{
            ...a,
            left: 24,
            bottom: 20,
            fontSize: 13,
            color: T.textMuted,
          }}
        >
          Illustrative exposure response · component motion preview
        </div>
      </div>
      <div
        style={{
          ...a,
          left: 812,
          top: 218,
          width: 724,
          height: 434,
          background: T.ink900,
          padding: "28px 22px",
        }}
      >
        <div
          style={{
            display: "flex",
            alignItems: "center",
            gap: 14,
            marginBottom: 28,
            fontSize: 22,
            fontWeight: 600,
          }}
        >
          <AppIcon name="adjust" size={24} />
          Light
        </div>
        <AppInspectorSlider
          label="Exposure"
          value={exposure}
          valueText={`${exposure >= 0 ? "+" : ""}${exposure.toFixed(2)} EV`}
          from={-5}
          to={5}
          precisionResponse
          scale={2}
          press={press}
          showCursor={t > 1 && t < 8.5}
        />
        <AppInspectorSlider
          label="Contrast"
          value={8}
          valueText="+8%"
          scale={2}
        />
        <AppInspectorSlider
          label="Highlights"
          value={-22}
          valueText="-22%"
          scale={2}
        />
        <AppInspectorSlider
          label="Shadows"
          value={16}
          valueText="+16%"
          scale={2}
        />
      </div>
      <div
        style={{
          ...a,
          left: 824,
          top: 688,
          width: 440,
          height: 152,
          opacity: complete ? 0.35 : 1,
        }}
      >
        <AppOpticalField
          width={440}
          height={152}
          variant={3}
          active={!complete}
        />
      </div>
      <div
        style={{
          ...a,
          left: 870,
          top: 848,
          width: 348,
          height: 5,
          background: T.ink850,
        }}
      >
        <AppOpticalField
          width={348}
          height={5}
          kind={2}
          progress={progress}
          active={!complete}
        />
      </div>
      <div
        style={{
          ...a,
          left: 824,
          top: 875,
          width: 440,
          display: "flex",
          justifyContent: "space-between",
          fontSize: 16,
          color: T.textMuted,
        }}
      >
        <span>{complete ? "Import complete" : "Importing photographs"}</span>
        <span style={{ fontFamily: "Menlo,monospace" }}>
          {Math.round(progress * 100)}%
        </span>
      </div>
      <div style={{ ...a, left: 1355, top: 728 }}>
        <AppActivity
          size={56}
          active={!complete}
          progress={complete ? 1 : -1}
        />
      </div>
      <div
        style={{ ...a, left: 1325, top: 814, fontSize: 14, color: T.textMuted }}
      >
        {complete ? "Completed" : "Working locally"}
      </div>
      <div
        style={{
          ...a,
          left: 1350,
          top: 870,
          scale: 1.4,
          transformOrigin: "0 0",
        }}
      >
        <AppCompletion phase={(t - 9.6) / 0.56} />
      </div>
      <div
        style={{
          ...a,
          left: 64,
          bottom: 42,
          display: "flex",
          alignItems: "center",
          gap: 24,
        }}
      >
        {[
          "layers",
          "curve",
          "text",
          "carousel",
          "timeline",
          "search",
          "keyframe",
        ].map((name) => (
          <AppIcon key={name} name={name} size={26} />
        ))}
        <span style={{ marginLeft: 14, fontSize: 15, color: T.textMuted }}>
          App icon paths · source palette · original RGB lockup · app optical
          shader
        </span>
      </div>
    </AbsoluteFill>
  );
};
