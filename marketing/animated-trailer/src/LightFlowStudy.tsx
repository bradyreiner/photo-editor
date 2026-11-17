import React from "react";
import { Audio } from "@remotion/media";
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
const ease = Easing.bezier(0.65, 0, 0.2, 1);
const out = Easing.bezier(0.18, 1, 0.25, 1);
const mix = (t: number, a: number, b: number, x = 0, y = 1) =>
  interpolate(t, [a, b], [x, y], { ...clamp, easing: ease });
const abs: React.CSSProperties = { position: "absolute" };
const photos = [
  "IMG_5071",
  "IMG_5106",
  "IMG_4870",
  "IMG_4969",
  "IMG_4856",
  "IMG_4744",
];

const SourceTile = ({
  name,
  index,
  t,
  edit,
  exposure,
}: {
  name: string;
  index: number;
  t: number;
  edit: number;
  exposure: number;
}) => {
  const hero = index === 1;
  const appear = hero
    ? 1
    : interpolate(t, [0.2 + index * 0.28, 0.8 + index * 0.28], [0, 1], {
        ...clamp,
        easing: out,
      });
  const libraryX = 90 + (index % 3) * 315,
    libraryY = 540 + Math.floor(index / 3) * 440;
  const x = hero
    ? libraryX + (72 - libraryX) * edit
    : libraryX + (libraryX < 500 ? -520 : 650) * edit;
  const y = hero ? libraryY + (430 - libraryY) * edit : libraryY;
  const w = hero ? 270 + 300 * edit : 270,
    h = hero ? 360 + 495 * edit : 360;
  return (
    <div
      style={{
        ...abs,
        left: x,
        top: y + (1 - appear) * 200,
        width: w,
        height: h,
        opacity: hero ? 1 : appear * (1 - edit),
        rotate: hero
          ? `${(1 - edit) * interpolate(t, [0, 2.2], [4, 0], clamp)}deg`
          : `${(1 - appear) * (index % 2 ? 10 : -10)}deg`,
        transformOrigin: "50% 70%",
        background: T.brandInk,
        overflow: "hidden",
      }}
    >
      <CanvasImage
        src={staticFile(`media/${name}.jpg`)}
        style={{
          width: "100%",
          height: "100%",
          objectFit: "cover",
          filter: hero ? `brightness(${2 ** (exposure * 0.24)})` : undefined,
        }}
      />
      <div
        style={{
          ...abs,
          left: 0,
          right: 0,
          bottom: 0,
          height: 35,
          background: "rgba(16,16,16,.9)",
          padding: "8px 12px",
          fontFamily: "Menlo,monospace",
          fontSize: 12,
          color: T.textMuted,
        }}
      >
        {name}.CR2
      </div>
    </div>
  );
};

const ImportToEdit = () => {
  const frame = useCurrentFrame();
  const { fps } = useVideoConfig();
  const t = frame / fps;
  // A single set of event times governs import, progress, selection, the optical
  // handoff, the camera, slider drag, release, and native completion feedback.
  const edit = mix(t, 3.6, 5.7);
  const importReady = 2.2;
  const arrivals = photos.map((_, i) =>
    i === 1
      ? 1
      : interpolate(t, [0.2 + i * 0.28, 0.8 + i * 0.28], [0, 1], {
          ...clamp,
          easing: out,
        }),
  );
  const progress =
    arrivals.reduce((sum, arrival) => sum + arrival, 0) / photos.length;
  const imported = photos.filter(
    (_, i) => i === 1 || t >= 0.8 + i * 0.28,
  ).length;
  const zoom = mix(t, 6.2, 7.8) * (1 - mix(t, 11.35, 13.1));
  const cameraScale = 1 + 1.6 * zoom;
  const exposure = interpolate(
    t,
    [0, 8, 9.45, 10.1, 10.7, 14],
    [0, 0, 1.25, 1.25, 0.65, 0.65],
    { ...clamp, easing: out },
  );
  const press = interpolate(
    t,
    [0, 7.88, 7.99, 9.46, 9.57, 10.08, 10.19, 10.72, 10.83],
    [0, 0, 1, 1, 0, 0, 1, 1, 0],
    clamp,
  );
  const waveHandoff = mix(t, 3.35, 5.7);
  const waveOpacity = 1 - mix(t, 5.45, 5.95);
  return (
    <AbsoluteFill
      style={{
        background: T.ink950,
        color: T.brandPaper,
        fontFamily: appUiFont,
        overflow: "hidden",
      }}
    >
      <div style={{ ...abs, left: 72, top: 85, zIndex: 5 }}>
        <AppWordmark width={240} />
      </div>
      <div
        style={{
          ...abs,
          right: 72,
          top: 107,
          fontFamily: "Menlo,monospace",
          fontSize: 16,
          color: T.textMuted,
          zIndex: 5,
        }}
      >
        LIGHT → LIBRARY → EDIT
      </div>
      <div
        style={{
          ...abs,
          left: 72,
          top: 235,
          zIndex: 5,
          fontSize: 83,
          lineHeight: 1.04,
          fontWeight: 650,
          letterSpacing: -3.5,
        }}
      >
        <div
          style={{
            opacity: 1 - mix(t, 3.2, 3.55),
            translate: `0 ${mix(t, 3.2, 3.55, 0, -30)}px`,
          }}
        >
          Bring in
          <br />
          the whole shoot.
        </div>
        <div
          style={{
            ...abs,
            left: 0,
            top: 0,
            opacity: mix(t, 3.5, 4),
            translate: `0 ${mix(t, 3.5, 4, 25, 0)}px`,
          }}
        >
          RAW editing.
          <br />
          Full control.
        </div>
      </div>
      <div
        style={{
          ...abs,
          inset: 0,
          transformOrigin: "0 0",
          scale: cameraScale,
          translate: `${-1675 * zoom}px ${-742 * zoom}px`,
        }}
      >
        <div
          style={{
            ...abs,
            left: 90,
            top: 470,
            opacity: 1 - edit,
            fontSize: 20,
            color: T.textMuted,
          }}
        >
          NEPAL / SHOOT LIBRARY
        </div>
        {photos.map((name, index) => (
          <SourceTile
            key={name}
            name={name}
            index={index}
            t={t}
            edit={edit}
            exposure={exposure}
          />
        ))}
        <div
          style={{
            ...abs,
            left: 675,
            top: 590,
            width: 374,
            height: 385,
            background: T.ink900,
            opacity: mix(t, 4.75, 5.55),
            translate: `${mix(t, 4.75, 5.55, 65, 0)}px 0`,
          }}
        >
          <div
            style={{
              ...abs,
              left: 20,
              top: 18,
              display: "flex",
              gap: 10,
              alignItems: "center",
              fontSize: 16,
              fontWeight: 600,
            }}
          >
            <AppIcon name="adjust" size={18} />
            Light
          </div>
          <div style={{ ...abs, left: 20, top: 60 }}>
            <AppInspectorSlider
              label="Exposure"
              value={exposure}
              valueText={`${exposure >= 0 ? "+" : ""}${exposure.toFixed(2)} EV`}
              from={-5}
              to={5}
              precisionResponse
              press={press}
              showCursor={t > 7.8 && t < 11}
            />
            <AppInspectorSlider label="Contrast" value={8} valueText="+8%" />
            <AppInspectorSlider
              label="Highlights"
              value={-22}
              valueText="-22%"
            />
            <AppInspectorSlider label="Shadows" value={16} valueText="+16%" />
            <AppInspectorSlider label="Whites" value={0} valueText="0%" />
            <AppInspectorSlider label="Blacks" value={0} valueText="0%" />
          </div>
          <div style={{ ...abs, left: 260, top: 18 }}>
            <AppCompletion phase={(t - 9.57) / 0.56} />
            <div style={{ ...abs, left: 0, top: 0 }}>
              <AppCompletion phase={(t - 10.83) / 0.56} />
            </div>
          </div>
        </div>
        <div
          style={{
            ...abs,
            left: 72,
            top: 1325,
            width: 570,
            display: "flex",
            justifyContent: "space-between",
            opacity: edit,
            fontFamily: "Menlo,monospace",
            fontSize: 16,
            color: T.textMuted,
          }}
        >
          <span>IMG_5106.CR2</span>
          <span>4000 × 6000</span>
        </div>
        {/* This is one persistent shader surface. Its actual loading wave hands off
       spatially to the exposure rail; it is not restarted in an unrelated panel. */}
        {t < 6 && (
          <div
            style={{
              ...abs,
              left: 80 + (805 - 80) * waveHandoff,
              top: 770 + (668.5 - 770) * waveHandoff,
              transformOrigin: "0 0",
              scale: `${1 + (100 / 920 - 1) * waveHandoff} ${1 + (3 / 240 - 1) * waveHandoff}`,
              opacity: waveOpacity,
              mixBlendMode: "screen",
              pointerEvents: "none",
            }}
          >
            <AppOpticalField
              width={920}
              height={240}
              variant={3}
              phase={((Math.min(t, 5.7) * 1.7) / 11) % 1}
            />
          </div>
        )}
        <div
          style={{
            ...abs,
            left: 90,
            top: 1460,
            width: 900,
            opacity: 1 - mix(t, 3.1, 3.8),
          }}
        >
          <div
            style={{
              display: "flex",
              alignItems: "center",
              gap: 18,
              fontSize: 24,
              marginBottom: 22,
            }}
          >
            <AppActivity
              size={30}
              active={t < importReady}
              progress={t < importReady ? progress : 1}
            />
            <span>
              {t < importReady ? "Importing photographs" : "Ready to edit"}
            </span>
            <span
              style={{
                marginLeft: "auto",
                fontFamily: "Menlo,monospace",
                color: T.textMuted,
              }}
            >
              {imported} / 6
            </span>
          </div>
          <div style={{ width: 900, height: 5, background: T.ink850 }}>
            <AppOpticalField
              kind={2}
              width={900}
              height={5}
              progress={progress}
              phase={t / 1.9}
            />
          </div>
          <div
            style={{
              ...abs,
              right: 0,
              top: 70,
              scale: 1.5,
              transformOrigin: "100% 0",
            }}
          >
            <AppCompletion phase={(t - importReady) / 0.56} />
          </div>
        </div>
      </div>
      <div
        style={{
          ...abs,
          left: 72,
          bottom: 110,
          fontSize: 19,
          color: T.textMuted,
          opacity: mix(t, 11.8, 12.5),
        }}
      >
        Exposure. Tone. Color. Detail.
      </div>
      <div
        style={{
          ...abs,
          left: 72,
          bottom: 58,
          fontSize: 15,
          color: T.textMuted,
        }}
      >
        Transition study · authored import timing and image response
      </div>
    </AbsoluteFill>
  );
};

export const LightFlowStudy = () => {
  const { fps } = useVideoConfig();
  return (
    <>
      <ImportToEdit />
      <Audio
        src={staticFile("audio/a-game-michael-ramir-c.mp3")}
        trimBefore={8 * fps}
        volume={(f) =>
          interpolate(
            f,
            [0, 0.25 * fps, 13.2 * fps, 14 * fps],
            [0, 0.55, 0.55, 0],
            clamp,
          )
        }
      />
    </>
  );
};
