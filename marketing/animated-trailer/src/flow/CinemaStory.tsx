import React from "react";
import { CanvasImage, staticFile, useVideoConfig } from "remotion";
import { AppIcon, AppInspectorSlider } from "../app-kit/AppElements";
import { sourceWaveforms } from "../launch/waveforms";
import {
  A,
  C,
  Chrome,
  Head,
  Label,
  Light,
  mix,
  Note,
  Picture,
  q,
  show,
  Surface,
} from "./Motion";
import { Sequence, useStoryTime, Video } from "./Timing";

const WAVE = Object.values(sourceWaveforms)[0];
const movieStyle: React.CSSProperties = { width: "100%", height: "100%" };

export const WalkingTitle = ({
  time = 0,
  build = 1,
  x = 0,
  scale = 1,
  mask = 1,
  overlay = 0,
}: {
  time?: number;
  build?: number;
  x?: number;
  scale?: number;
  mask?: number;
  overlay?: number;
}) => {
  const index = Math.min(104, Math.max(0, Math.floor(time * 30)));
  const name = String(index).padStart(3, "0");
  return (
    <div style={{ ...A, inset: 0, overflow: "hidden" }}>
      <div style={{ ...A, inset: 0, scale, transformOrigin: "42% 46%" }}>
        <CanvasImage
          src={staticFile(`media/flow/hike/${name}.jpg`)}
          style={movieStyle}
        />
        <div
          style={{
            ...A,
            left: `${8 + x / 12.8}%`,
            top: "25%",
            fontSize: 134,
            letterSpacing: -7,
            lineHeight: 0.86,
            fontWeight: 800,
            color: "#fff4de",
            opacity: build,
            translate: `0 ${60 * (1 - build)}px`,
          }}
        >
          A LITTLE
          <br />
          FURTHER.
        </div>
        <CanvasImage
          src={staticFile(`media/flow/hike/${name}.png`)}
          style={{
            ...movieStyle,
            ...A,
            inset: 0,
            opacity: mask,
            filter:
              overlay > 0
                ? `drop-shadow(0 0 ${4 * overlay}px #8ef4df)`
                : undefined,
          }}
        />
      </div>
    </div>
  );
};

const ProgramContent = ({ t }: { t: number }) => {
  const { fps } = useVideoConfig();
  const camera = q(t, 19.4, 22.1),
    grade = q(t, 24.1, 25.8),
    effect = q(t, 27.2, 28.5);
  return (
    <>
      <Sequence durationInFrames={Math.round(1.85 * fps)} layout="none">
        <Video
          src={staticFile("media/refine/arrival.mp4")}
          trimBefore={4 * fps}
          objectFit="cover"
          muted
          style={movieStyle}
        />
        <div
          style={{
            ...A,
            left: 300,
            top: 100,
            color: "#fff8e7",
            fontSize: 36,
            letterSpacing: 8,
          }}
        >
          NEPAL / FIELD NOTES
        </div>
      </Sequence>
      <Sequence
        from={Math.round(1.85 * fps)}
        durationInFrames={Math.round(1.23 * fps)}
        layout="none"
      >
        <Video
          src={staticFile("media/MVI_5051-2.3-5.9.mp4")}
          objectFit="cover"
          muted
          style={movieStyle}
        />
        <div
          style={{
            ...A,
            left: 300,
            bottom: 90,
            color: "#fff9e9",
            fontSize: 70,
            fontWeight: 600,
            letterSpacing: -4,
          }}
        >
          Take the long way.
        </div>
      </Sequence>
      <Sequence
        from={Math.round(3.08 * fps)}
        durationInFrames={Math.round(1.85 * fps)}
        layout="none"
      >
        <WalkingTitle
          time={t - 3.08}
          build={q(t, 3.08, 3.45)}
          x={198 + 40 * q(t, 3.4, 4.8)}
        />
      </Sequence>
      <Sequence
        from={Math.round(4.93 * fps)}
        durationInFrames={Math.round(1.87 * fps)}
        layout="none"
      >
        <Video
          src={staticFile("media/refine/night.mp4")}
          objectFit="cover"
          muted
          style={movieStyle}
        />
        <div
          style={{
            ...A,
            left: 300,
            bottom: 90,
            fontSize: 34,
            color: "white",
            letterSpacing: 4,
          }}
        >
          STORIES WORTH KEEPING.
        </div>
      </Sequence>
      <Sequence
        from={Math.round(6.8 * fps)}
        durationInFrames={Math.round(5.6 * fps)}
        layout="none"
      >
        <Video
          src={staticFile("media/launch-1268.mp4")}
          objectFit="cover"
          muted
          style={movieStyle}
        />
        <div
          style={{
            ...A,
            left: 85,
            bottom: 80,
            fontSize: 42,
            color: "white",
            fontWeight: 600,
            background: "#1117",
            padding: "14px 22px",
          }}
        >
          The next story is already out there.
        </div>
      </Sequence>
      <Sequence
        from={Math.round(12.4 * fps)}
        durationInFrames={Math.round(11.1 * fps)}
        layout="none"
      >
        <WalkingTitle
          time={
            t < 15.8
              ? t - 12.4
              : t < 18.8
                ? 3.36
                : Math.min(3.4, (t - 18.8) * 0.72)
          }
          build={q(t, 12.8, 13.6)}
          mask={q(t, 14.4, 15.3)}
          overlay={show(t, 14.2, 16.4)}
          x={mix(0, 110, camera)}
          scale={1 + 0.12 * camera}
        />
        <div
          style={{
            ...A,
            left: 75,
            right: 75,
            bottom: 40,
            fontSize: 34,
            fontWeight: 500,
            textAlign: "center",
            color: "white",
            opacity: q(t, 21.3, 21.9),
            textShadow: "0 2px 8px #000",
          }}
        >
          One step. And then another.
        </div>
      </Sequence>
      <Sequence
        from={Math.round(23.5 * fps)}
        durationInFrames={Math.round(6.5 * fps)}
        layout="none"
      >
        <div
          style={{
            ...A,
            inset: 0,
            filter: `brightness(${1 + 0.2 * grade}) saturate(${1 + 0.2 * grade}) contrast(${1 + 0.13 * effect})`,
          }}
        >
          <Video
            src={staticFile("media/launch-1268.mp4")}
            trimBefore={2 * fps}
            muted
            objectFit="cover"
            style={movieStyle}
          />
        </div>
        <div
          style={{
            ...A,
            inset: 0,
            clipPath: `inset(0 0 0 ${mix(100, 82, effect)}%)`,
            backdropFilter: `blur(${effect * 8}px)`,
            opacity: effect,
          }}
        />
        <div
          style={{
            ...A,
            left: 85,
            bottom: 65,
            color: "white",
            fontSize: 58,
            fontWeight: 600,
            letterSpacing: -2,
          }}
        >
          Color that feels like being there.
        </div>
      </Sequence>
      <Sequence
        from={30 * fps}
        durationInFrames={Math.round(3.7 * fps)}
        layout="none"
      >
        <Video
          src={staticFile("media/refine/night.mp4")}
          muted
          objectFit="cover"
          style={movieStyle}
        />
        <div
          style={{
            ...A,
            left: 85,
            bottom: 70,
            color: "white",
            fontSize: 72,
            fontWeight: 600,
            letterSpacing: -3,
          }}
        >
          Stay a little longer.
        </div>
      </Sequence>
      <Sequence
        from={Math.round(33.7 * fps)}
        durationInFrames={Math.round(1.3 * fps)}
        layout="none"
      >
        <Video
          src={staticFile("media/refine/hike.mp4")}
          muted
          objectFit="cover"
          style={movieStyle}
        />
      </Sequence>
    </>
  );
};

const Wave = ({
  width = 650,
  height = 37,
  color = "#9fbcae",
}: {
  width?: number;
  height?: number;
  color?: string;
}) => (
  <svg width={width} height={height} style={{ display: "block" }}>
    <path
      d={WAVE.map(
        (v, i) =>
          `M${(i / (WAVE.length - 1)) * width} ${height / 2 - v * height * 0.44}V${height / 2 + v * height * 0.44}`,
      ).join(" ")}
      stroke={color}
      strokeWidth={1.5}
    />
  </svg>
);

const Timeline = ({ t, top }: { t: number; top: number }) => {
  const trim = q(t, 9.2, 10.4),
    cut = q(t, 10.7, 11.8),
    motion = q(t, 18.8, 20.4);
  const playhead =
    t < 6.8
      ? (t / 6.8) * 0.85
      : t < 12.4
        ? mix(0.4, 0.58, q(t, 7, 12.4))
        : t < 23.5
          ? mix(0.32, 0.69, q(t, 12.4, 23.5))
          : mix(0.69, 0.98, q(t, 23.5, 35));
  return (
    <Surface
      style={{
        ...A,
        left: 80,
        top,
        width: 920,
        height: 320,
        overflow: "hidden",
      }}
    >
      <div
        style={{
          padding: "17px 22px",
          fontSize: 23,
          display: "flex",
          alignItems: "center",
          gap: 18,
          borderBottom: "1px solid #343434",
        }}
      >
        <AppIcon name="play" size={24} />
        NEPAL / MASTER
        <span style={{ marginLeft: "auto", color: C.textMuted }}>
          3840 × 2160 · 30 fps
        </span>
      </div>
      <div
        style={{
          ...A,
          left: 118,
          right: 15,
          top: 62,
          height: 32,
          display: "flex",
          justifyContent: "space-between",
          fontFamily: "Menlo",
          fontSize: 17,
          color: C.textMuted,
        }}
      >
        {["00:00", "00:05", "00:10", "00:15", "00:20", "00:25", "00:30"].map(
          (v) => (
            <span key={v}>{v}</span>
          ),
        )}
      </div>
      {["TYPE", "V2", "V1", "A1", "A2"].map((name, i) => (
        <div
          key={name}
          style={{
            ...A,
            left: 18,
            top: 104 + i * 40,
            fontSize: 17,
            color: C.textMuted,
          }}
        >
          {name}
        </div>
      ))}
      <div
        style={{
          ...A,
          left: 118,
          top: 95,
          width: 785,
          height: 32,
          background: "#746b95",
          borderRadius: 3,
          opacity: q(t, 12.8, 13.8),
          clipPath: "inset(0 34% 0 24%)",
        }}
      >
        <span style={{ ...A, left: "27%", top: 8, fontSize: 17 }}>
          A LITTLE FURTHER. ◇
        </span>
      </div>
      <div
        style={{
          ...A,
          left: 118 + 186,
          top: 135,
          width: 330,
          height: 32,
          background: "#6d9694",
          opacity: q(t, 14.4, 15.3),
          fontSize: 17,
          padding: "6px 10px",
          boxSizing: "border-box",
          borderRadius: 3,
        }}
      >
        Foreground mask / editable
      </div>
      <div
        style={{
          ...A,
          left: 118,
          top: 175,
          display: "flex",
          gap: 3,
          alignItems: "center",
        }}
      >
        {[
          "flow/arrival-thumb.jpg",
          "flow/ridge-thumb.jpg",
          "IMG_1207-2-7.jpg",
          "IMG_1268-2-7.jpg",
          "flow/night-thumb.jpg",
        ].map((src, i) => {
          const w = [140 - 24 * trim, 82 + 20 * cut, 220, 180 - 20 * cut, 150][
            i
          ];
          return (
            <div
              key={src}
              style={{
                position: "relative",
                width: w,
                height: 40,
                overflow: "hidden",
                borderRadius: 3,
                outline: i === 2 ? "2px solid #d6e4eb" : "none",
                background: "#52606b",
              }}
            >
              <Picture src={src} />
              <div style={{ ...A, inset: 0, background: "#14141433" }} />
              <span
                style={{
                  ...A,
                  left: 5,
                  bottom: 3,
                  fontSize: 14,
                  color: "white",
                }}
              >
                {["ARRIVAL", "RIDGE", "THE WALK", "CITY", "NIGHT"][i]}
              </span>
              {i === 0 && (
                <div
                  style={{
                    ...A,
                    right: 0,
                    top: 0,
                    width: 4,
                    height: 40,
                    background: "#ddd",
                  }}
                />
              )}
            </div>
          );
        })}
      </div>
      <div
        style={{
          ...A,
          left: 118,
          top: 222,
          background: "#273e37",
          width: 780,
          borderRadius: 3,
          padding: "3px 0",
        }}
      >
        <Wave width={780} height={29} />
      </div>
      <div
        style={{
          ...A,
          left: 118,
          top: 266,
          background: "#333448",
          width: 650,
          borderRadius: 3,
          padding: "3px 0",
        }}
      >
        <Wave width={650} height={27} color="#adb0d2" />
      </div>
      {motion > 0 && (
        <svg
          width={330}
          height={32}
          style={{ ...A, left: 304, top: 132, opacity: motion }}
        >
          <path
            d="M0 29 C100 29 180 3 320 3"
            stroke="#d9f4e9"
            fill="none"
            strokeWidth={1.7}
          />
          {[0, 1].map((i) => (
            <rect
              key={i}
              x={i === 0 ? 4 : 312}
              y={i === 0 ? 22 : 0}
              width={8}
              height={8}
              fill="white"
              transform={`rotate(45 ${i === 0 ? 8 : 316} ${i === 0 ? 26 : 4})`}
            />
          ))}
        </svg>
      )}
      <div
        style={{
          ...A,
          left: 118 + playhead * 780,
          top: 60,
          bottom: 0,
          width: 2,
          background: "#e7efed",
          boxShadow: "0 0 6px #fff5",
        }}
      >
        <div
          style={{
            position: "absolute",
            left: -5,
            top: 0,
            borderTop: "9px solid #e7efed",
            borderLeft: "6px solid transparent",
            borderRight: "6px solid transparent",
          }}
        />
      </div>
    </Surface>
  );
};

const AudioMixer = ({ t }: { t: number }) => (
  <Surface
    style={{
      ...A,
      left: 80,
      top: 1070,
      width: 920,
      height: 355,
      padding: 25,
      boxSizing: "border-box",
      opacity: show(t, 29.9, 33.7),
    }}
  >
    <Label>AUDIO MIXER / SHAPE THE WHOLE SOUNDTRACK</Label>
    <div style={{ display: "flex", gap: 20, marginTop: 22 }}>
      {["Location", "Music", "Effects", "Master"].map((name, i) => {
        const v =
          i === 1 ? mix(-6, -12, q(t, 30.6, 31.8)) : [-3, -6, -5, -1][i];
        return (
          <div
            key={name}
            style={{
              width: 202,
              position: "relative",
              height: 255,
              borderRight: i === 3 ? undefined : "1px solid #393939",
            }}
          >
            <div style={{ fontSize: 25 }}>{name}</div>
            <div
              style={{
                ...A,
                left: 49,
                top: 48,
                width: 3,
                height: 133,
                background: "#5e5e5e",
              }}
            />
            <div
              style={{
                ...A,
                left: 31,
                top: 58 - v * 3,
                width: 39,
                height: 16,
                background: "#cfdddd",
                borderRadius: 3,
                boxShadow: "0 3px 10px #0008",
              }}
            />
            {[0, 1].map((c) => (
              <div
                key={c}
                style={{
                  ...A,
                  left: 104 + c * 17,
                  top: 52,
                  width: 10,
                  height: 131,
                  background: "#202824",
                  overflow: "hidden",
                }}
              >
                <div
                  style={{
                    ...A,
                    bottom: 0,
                    left: 0,
                    right: 0,
                    height: `${53 + 29 * Math.sin(t * 8 + i * 1.7 + c * 0.7)}%`,
                    background:
                      "linear-gradient(to top,#669b85,#b1d6a7 80%,#e1ce91)",
                  }}
                />
              </div>
            ))}
            <div
              style={{
                ...A,
                left: 20,
                top: 198,
                fontSize: 24,
                fontFamily: "Menlo",
              }}
            >
              {v.toFixed(1)} dB
            </div>
            <div
              style={{
                ...A,
                left: 20,
                top: 233,
                fontSize: 18,
                color: C.textMuted,
              }}
            >
              {i === 1
                ? "Volume automation"
                : i === 3
                  ? "Stereo output"
                  : "Gain · Pan · FX"}
            </div>
          </div>
        );
      })}
    </div>
  </Surface>
);

export const CinemaStory = () => {
  const t = useStoryTime(),
    unfold = q(t, 6.8, 8.4),
    focus = q(t, 12.3, 13.2) * (1 - q(t, 18, 19));
  const w = mix(1080, 920, unfold),
    h = mix(980, 518, unfold),
    x = mix(0, 80, unfold),
    y = mix(480, 490, unfold);
  const innerScale = Math.max(w / 1280, h / 720);
  const motion = q(t, 19.4, 22.1),
    grade = q(t, 24.1, 25.8),
    effect = q(t, 27.2, 28.5);
  const finish = q(t, 33.7, 35);
  const sound = q(t, 29.7, 30.2) * (1 - q(t, 33.5, 34.2));
  return (
    <>
      <Chrome mode="Film / complete productions" />
      <div style={{ opacity: 1 - finish }}>
        <Head
          t={t}
          b={6.6}
          lines={["Make something", "worth watching."]}
          detail="Full films. Your story, from first cut to final mix."
          size={100}
        />
        <Head
          t={t}
          a={6.8}
          b={12.2}
          lines={["Build the story.", "Shape every cut."]}
          detail="Trim. Split. Ripple. Arrange. Layer."
          size={100}
        />
        <Head
          t={t}
          a={12.4}
          b={18.7}
          lines={["Put your ideas", "in the frame."]}
          detail="Moving masks. Layered titles. Full control."
          size={100}
        />
        <Head
          t={t}
          a={18.9}
          b={23.3}
          lines={["Set it in motion."]}
          detail="Keyframes. Animation curves. Editable captions."
          size={104}
        />
        <Head
          t={t}
          a={23.5}
          b={26.8}
          lines={["Your color.", "Across every cut."]}
          detail="Custom color engine · sRGB · Rec.709 · Log"
          size={100}
        />
        <Head
          t={t}
          a={27}
          b={29.7}
          lines={["Make the look", "your own."]}
          detail="Stack effects. Refine them. Keep them editable."
          size={100}
        />
        <Head
          t={t}
          a={29.9}
          b={35.3}
          lines={["Finish the film.", "Sound and all."]}
          detail="Layered audio. Track effects. A complete mix."
          size={100}
        />
      </div>
      <Light
        t={t + 68}
        y={1470}
        amount={0.7 * (1 - unfold) + 0.4 * q(t, 33.7, 35)}
      />
      <div
        style={{
          ...A,
          left: x,
          top: y,
          width: w,
          height: h,
          overflow: "hidden",
          borderRadius: 8 * unfold,
          boxShadow: "0 22px 80px #0008",
          scale: 1 + 0.035 * focus,
          transformOrigin: "50% 40%",
        }}
      >
        <div
          style={{
            ...A,
            left: (w - 1280 * innerScale) / 2,
            top: (h - 720 * innerScale) / 2,
            width: 1280,
            height: 720,
            transform: `scale(${innerScale})`,
            transformOrigin: "0 0",
          }}
        >
          <ProgramContent t={t} />
        </div>
        {t > 6.8 && (
          <Label
            style={{
              ...A,
              left: 20,
              top: 20,
              padding: "8px 12px",
              background: "#111a",
              color: "#eee",
              opacity: unfold,
            }}
          >
            {t < 12.4
              ? "PROGRAM / 4K MASTER"
              : t < 18.8
                ? "COMPOSITE / MASK + TYPE"
                : t < 23.5
                  ? "MOTION / ANIMATION"
                  : t < 27
                    ? "OUTPUT / Rec.709"
                    : t < 30
                      ? "EFFECTS / EDITABLE"
                      : "PROGRAM / FINAL MIX"}
          </Label>
        )}
        {t > 14.4 && t < 16.5 && (
          <div
            style={{
              ...A,
              left: 20,
              right: 20,
              bottom: 20,
              border: "1px solid #c2f3de",
              height: 60,
              opacity: show(t, 14.4, 16.5),
              fontSize: 24,
              padding: 12,
              background: "#1117",
            }}
          >
            Foreground mask · Original footage retained
          </div>
        )}
      </div>
      <div
        style={{
          ...A,
          inset: 0,
          opacity: unfold * (1 - finish),
          translate: `0 ${500 * (1 - unfold) + 100 * finish}px`,
        }}
      >
        <Timeline t={t} top={mix(1090, 1460, sound)} />
      </div>
      <Surface
        style={{
          ...A,
          left: 80,
          top: 1460,
          width: 920,
          height: 270,
          padding: "22px 25px",
          boxSizing: "border-box",
          opacity: unfold * (1 - sound) * (1 - finish),
          translate: `0 ${50 * (1 - unfold)}px`,
        }}
      >
        {t < 12.4 ? (
          <>
            <Label>EDIT / SOURCE → SEQUENCE</Label>
            <AppInspectorSlider
              label="Out point"
              value={mix(18, 16, q(t, 9.2, 10.4))}
              valueText={`00:00:${Math.round(mix(18, 16, q(t, 9.2, 10.4)))}`}
              from={0}
              to={30}
              scale={2}
              width={435}
              showCursor={t > 9.2 && t < 10.4}
            />
            <div style={{ fontSize: 30, color: C.textMuted, marginTop: 12 }}>
              Ripple the cut. Keep the sequence connected.
            </div>
          </>
        ) : t < 18.8 ? (
          <>
            <Label>LAYERS / COMPOSITE</Label>
            <div
              style={{
                display: "flex",
                gap: 20,
                marginTop: 20,
                alignItems: "center",
                fontSize: 28,
              }}
            >
              <AppIcon name="selection" size={30} />
              Foreground mask<span style={{ color: C.textMuted }}>↑</span>
              <AppIcon name="text" size={30} />
              Title<span style={{ color: C.textMuted }}>↑</span>Original
            </div>
            <AppInspectorSlider
              label="Feather"
              value={1.4 * q(t, 15.5, 16.6)}
              valueText={`${(1.4 * q(t, 15.5, 16.6)).toFixed(1)} px`}
              from={0}
              to={10}
              width={435}
              scale={2}
              showCursor={t > 15.5 && t < 16.6}
            />
          </>
        ) : t < 23.5 ? (
          <>
            <Label>ANIMATE / POSITION + SCALE</Label>
            <AppInspectorSlider
              label="Position X"
              value={110 * motion}
              valueText={`${Math.round(110 * motion)} px`}
              from={-500}
              to={500}
              width={435}
              scale={2}
              showCursor={t > 19.4 && t < 22.1}
            />
            <svg
              width={300}
              height={65}
              style={{ position: "absolute", right: 35, bottom: 24 }}
            >
              <path
                d="M4 60 C125 60 160 5 290 5"
                stroke="#d7e4ee"
                strokeWidth={2.5}
                fill="none"
              />
              <circle cx={4} cy={60} r={4} fill="white" />
              <circle cx={290} cy={5} r={4} fill="white" />
            </svg>
            <div style={{ fontSize: 27, color: C.textMuted, marginTop: 12 }}>
              Scale {(100 + 12 * motion).toFixed(0)}% · Smooth easing
            </div>
          </>
        ) : t < 27 ? (
          <>
            <Label>COLOR / INPUT → WORKING SPACE → OUTPUT</Label>
            <AppInspectorSlider
              label="Exposure"
              value={0.2 * grade}
              valueText={`+${(0.2 * grade).toFixed(2)} EV`}
              from={-5}
              to={5}
              width={435}
              scale={2}
              showCursor={t > 24.1 && t < 25.8}
            />
            <div style={{ fontSize: 29, marginTop: 14, color: C.textMuted }}>
              White balance · Curves · HSL · Detail
            </div>
          </>
        ) : (
          <>
            <Label>EFFECT STACK / REORDER · MASK · KEYFRAME</Label>
            <div
              style={{
                display: "flex",
                gap: 16,
                alignItems: "center",
                fontSize: 28,
                marginTop: 18,
              }}
            >
              <AppIcon name="layers" size={30} />
              Color adjustments → Punch → Blur
            </div>
            <AppInspectorSlider
              label="Punch"
              value={28 * effect}
              valueText={`${Math.round(28 * effect)}%`}
              from={0}
              to={100}
              width={435}
              scale={2}
              showCursor={t > 27.2 && t < 28.5}
            />
          </>
        )}
      </Surface>
      <AudioMixer t={t} />
      <Note style={{ top: 1770, fontSize: 34, opacity: unfold * (1 - finish) }}>
        {t < 12.4
          ? "From short films to full-length productions."
          : t < 18.8
            ? "Real footage. Editable masks and layers."
            : t < 23.5
              ? "Make the movement. Adjust every keyframe."
              : t < 27
                ? "Full-resolution picture. A consistent color workflow."
                : t < 30
                  ? "Build a treatment. Keep the original intact."
                  : "Picture, music and location sound. One timeline."}
      </Note>
    </>
  );
};
